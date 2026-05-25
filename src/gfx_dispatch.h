/*
 * OpenLords2 — glyph dispatcher + clip setters.
 *
 * Three retail leaves sit between the (deferred) text renderer and the
 * already-ported glyph blitters in gfx_glyph.c.  They translate a
 * single (font_blob, char_code) into a fully-staged blitter call:
 *
 *   - Look up the character's glyph index from a 256-byte translation
 *     table (retail global DAT_004d71f0; we accept it as a parameter).
 *   - Pull width/height/source-offset and three byte flags from the
 *     glyph's 16-byte descriptor inside the font blob.
 *   - Advance the running cursor_y by the glyph's per-character
 *     advance byte (font[+0xd]).
 *   - Apply per-font character-spacing tweaks (retail hard-codes them
 *     for `DAT_005af8f0`, which we expose via `set_default_font`).
 *   - Run two clip setters (X then Y) to compute the visible row
 *     count, the source pointer pad, and the dest stride pad.
 *   - Pick the matching blitter (unclipped / left-clipped / right-
 *     clipped) based on the X-clip mode tag, gated on Y-clip != "skip".
 *
 * Retail address map:
 *
 *   FUN_00402a14 @ 0x402a14 (514 B) → gfx_dispatch_glyph
 *                                     Per-character entry point; calls
 *                                     the two clip setters and one of
 *                                     the three blitters.
 *   FUN_0040464c @ 0x40464c (303 B) → gfx_dispatch_clip_x
 *                                     Horizontal clip setup.  Reads
 *                                     glyph_w + cursor_x, writes
 *                                     clipped_w / src_stride_pad /
 *                                     dst_stride_pad / cursor_x / left
 *                                     skip / X-clip mode tag.
 *   FUN_0040477b @ 0x40477b (291 B) → gfx_dispatch_clip_y
 *                                     Vertical clip setup.  Reads
 *                                     glyph_height_short + cursor_y,
 *                                     writes glyph_h (clipped row
 *                                     count, overwriting the per-
 *                                     character advance byte) / top
 *                                     skip / Y-clip mode tag.
 *                                     Propagates the X-clip "skip" tag
 *                                     into the Y-clip output.
 *
 * State globals (mirror retail DAT_* names):
 *
 *   alt_clip_mode      DAT_005aeb84 — flag.  0 = clip to bound canvas
 *                                    (the common case).  Non-zero =
 *                                    clip to the panel-overlay rect
 *                                    used during scenario-list / hint
 *                                    text rendering.
 *   alt_clip_left      DAT_0052afdc — left X of the panel-overlay rect.
 *   alt_clip_bottom    DAT_00553254 — bottom Y of the panel-overlay
 *                                    rect.  Retail hard-codes top=0x18
 *                                    and right=0x1de inline; we mirror
 *                                    those as compile-time constants.
 *   default_font       DAT_005af8f0 — the one font that triggers the
 *                                    per-char-range cursor_y tweaks
 *                                    (see narrow-char quirk).  Any
 *                                    other font skips the tweaks.
 *   last_glyph_index   DAT_005c9288 — 0-based glyph index of the most
 *                                    recently dispatched character.
 *                                    Shared with font_metrics_state's
 *                                    last_glyph_index — both retail
 *                                    paths write the same global, but
 *                                    via separate state structs in our
 *                                    port (no coupling needed; nothing
 *                                    reads the dispatcher's value).
 *   glyph_height_short DAT_005aeb70 — the SHORT at glyph descriptor
 *                                    offset +2.  Distinct from the
 *                                    byte at +0xd (used as per-glyph
 *                                    cursor_y advance AND as initial
 *                                    row count).  The Y-clip setter
 *                                    consults this short for the
 *                                    "natural" glyph extent.  See
 *                                    quirk on the dual-height fields.
 *   x_clip_mode        DAT_005cdd20 — tag set by clip_x:
 *                                       0 = no clip
 *                                       1 = left clip (glyph straddles
 *                                           left edge — cursor_x moves
 *                                           up to `left`, src skips
 *                                           ahead by left_skip cols)
 *                                       2 = right clip (glyph straddles
 *                                           right edge — clipped_w
 *                                           shortened)
 *                                       5 = skip (glyph entirely off
 *                                           canvas horizontally OR
 *                                           glyph_w < 1)
 *   y_clip_mode        DAT_0058fe18 — tag set by clip_y:
 *                                       0 = no clip
 *                                       3 = top clip
 *                                       4 = bottom clip
 *                                       5 = skip (off-canvas or
 *                                           propagated from x_clip)
 *   left_skip          DAT_0058fd50 — bytes to advance the glyph
 *                                    source pointer past clipped-off
 *                                    left columns.  Read by dispatcher
 *                                    to bake into glyph_offset before
 *                                    blitting.
 *   top_skip           DAT_0058feac — rows to advance the glyph source
 *                                    pointer past clipped-off top.
 *                                    Multiplied by glyph_w (the FULL
 *                                    row width, not the clipped one)
 *                                    by the dispatcher.
 *
 * Quirks preserved verbatim — see engine-quirks.md for narratives:
 *
 *   - The "height" byte at glyph descriptor offset +0xd is read into
 *     DAT_005bb478 and immediately added to cursor_y AS IF it were a
 *     per-glyph line advance.  Then clip_y OVERWRITES DAT_005bb478
 *     with the visible row count (= what the blitter reads).  Two
 *     unrelated uses of the same global; the dispatcher relies on the
 *     order to avoid clobbering before the cursor update.
 *
 *   - The default font (`DAT_005af8f0`) gets three character-range
 *     decrements applied to cursor_y after the advance: chars in
 *     [0x61..0x6d] / [0x73..0x77] / [0x80..0x84] each shave one pixel.
 *     Almost certainly a per-glyph kerning tweak baked into the
 *     dispatcher rather than the font data (so the same font blob can
 *     be reused with or without the tweak just by passing a different
 *     font pointer).
 *
 *   - The alt-clip rect mixes runtime values (left/bottom — settable
 *     here) with compile-time constants (top=0x18, right=0x1de).  The
 *     constants are baked into FUN_00402a14's body, not loaded from a
 *     global, so we encode them the same way (no setter).
 *
 *   - Final dst_stride_pad comes from canvas_width (gfx_paint), not
 *     from the alt-clip's right.  Even when clipping to the panel
 *     overlay, the dest pointer still advances by canvas-width bytes
 *     per row — because the canvas pitch is fixed regardless of the
 *     logical clip rect.
 */

#ifndef OPENLORDS2_GFX_DISPATCH_H
#define OPENLORDS2_GFX_DISPATCH_H

#include <stdint.h>

/* Configure the panel-overlay clip rect.  When `enabled` is non-zero
 * the next `gfx_dispatch_glyph` call clips to the rect:
 *   x ∈ [left, 0x1de)   y ∈ [0x18, bottom)
 *
 * `left` mirrors retail's DAT_0052afdc; `bottom` mirrors DAT_00553254.
 * Pass enabled=0 to clip to the bound gfx_paint canvas instead — the
 * common path. */
void gfx_dispatch_set_alt_clip(int enabled, int left, int bottom);

/* Register the font pointer that triggers the per-char-range cursor_y
 * decrements.  Retail's special-case font is `DAT_005af8f0` (one of the
 * Charset3 / Font_C2 blobs depending on language).  Any other font
 * passed to `gfx_dispatch_glyph` skips the tweaks.  Pass NULL to
 * disable the tweak entirely. */
void gfx_dispatch_set_default_font(const uint8_t *font_base);

/* Port of FUN_00402a14.  Dispatch one character against the supplied
 * font and char→glyph table.
 *
 *   - `font_base`: pointer to the 16-bytes-per-glyph descriptor array
 *                  used by both font_metrics and the blitter (descriptor
 *                  N starts at font_base + N*16 + 8).
 *   - `char_glyph_table`: 256-byte translation from char-code to 1-based
 *                  glyph index (0 = "no glyph for this char").  Retail
 *                  uses DAT_004d71f0; the test font_metrics uses
 *                  DAT_004d71d0.  Both are accepted here.
 *   - `char_code`: the byte value of the character to dispatch.
 *
 * Returns the visible cursor advance (`glyph_w + 1`) for the caller's
 * cursor_x bookkeeping, or 0 if the character has no glyph (table
 * entry was 0). */
int gfx_dispatch_glyph(const uint8_t *font_base,
                       const uint8_t *char_glyph_table,
                       unsigned char  char_code);

/* Port of FUN_0040464c.  Horizontal clip setup.  Reads glyph_w and
 * cursor_x from gfx_glyph state and writes the clip-mode tag, left
 * skip, clipped_w, src_stride_pad, and dst_stride_pad back via the
 * gfx_glyph setters (plus dispatcher-private state for the tag/skip).
 *
 * `left` / `right` are the horizontal clip bounds; the glyph is
 * considered visible at columns [cursor_x .. cursor_x + glyph_w),
 * intersected with [left, right). */
void gfx_dispatch_clip_x(int left, int right);

/* Port of FUN_0040477b.  Vertical clip setup.  Symmetric to clip_x
 * but reads the glyph height SHORT (descriptor +2) — stored in
 * dispatcher-private state by gfx_dispatch_glyph — and propagates the
 * X-clip "skip" tag into the Y-clip output (a Y-clip "no clip" becomes
 * "skip" if X-clip was "skip").  Writes the visible row count back via
 * gfx_glyph_set_glyph (overwriting the per-glyph advance byte the
 * dispatcher staged earlier). */
void gfx_dispatch_clip_y(int top, int bottom);

/* Read-only accessors for state the clip setters mutate.  Used by
 * tests; not consumed by other modules.
 *
 *   clip_x_mode / clip_y_mode: see header doc for tag values.
 *   left_skip / top_skip:      offsets into the glyph source.
 *   last_glyph_index:          0-based index of the most recent dispatch.
 *   glyph_height_short:        the short at descriptor +2 staged by the
 *                              dispatcher; consumed by clip_y.
 */
int gfx_dispatch_clip_x_mode(void);
int gfx_dispatch_clip_y_mode(void);
int gfx_dispatch_left_skip(void);
int gfx_dispatch_top_skip(void);
int gfx_dispatch_last_glyph_index(void);
int gfx_dispatch_glyph_height_short(void);

/* For tests that want to stage the dispatcher-private height short
 * without running a full dispatch (e.g. to exercise clip_y in
 * isolation). */
void gfx_dispatch_set_glyph_height_short(int h);

/* Same idea for the X-clip mode tag — lets tests prepare the
 * "X-clip says skip" state before invoking clip_y to verify the
 * propagation rule. */
void gfx_dispatch_set_clip_x_mode(int mode);

#endif /* OPENLORDS2_GFX_DISPATCH_H */
