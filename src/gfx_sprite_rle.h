/*
 * OpenLords2 — RLE sprite blitter (per-sprite RLE-decoded pixel emitter).
 *
 * Three retail leaves form the SECOND sprite-class blit family — the
 * RLE-decoding counterpart of the first (raw-pixel) family in
 * gfx_sprite.c.  Where the first family copies tightly-packed pixel
 * bytes verbatim, this family decodes a row-major RLE stream:
 *
 *   opcode 0x00 N         skip N pixels (transparent run)
 *   opcode 0x01..0x10 ...  copy N literal pixel bytes
 *
 * Each row's RLE stream is terminated implicitly when the cumulative
 * pixel count reaches sprite_w (the natural row width).  Opcodes
 * 0x01..0x0F are each emitted by retail as a separate switch arm;
 * 0x10 is a separate arm too (16-byte literal); any other byte value
 * falls through to retail's `else` arm which handles a 15-byte literal
 * (so values 0x0F and 0x11..0xFF share a tail).  No retail blob is
 * known to emit opcodes above 0x10.
 *
 * Retail address map:
 *
 *   FUN_004b4537 @ 0x4b4537 (733 B) → gfx_sprite_rle_blit
 *                                     Unclipped path.  Per-row RLE
 *                                     decode with an unrolled per-
 *                                     opcode switch (16 arms).
 *   FUN_004b4814 @ 0x4b4814 (164 B) → gfx_sprite_rle_blit_left_clipped
 *                                     Pre-shifts the dest pointer
 *                                     LEFT by `left_skip` pixels and
 *                                     gates each literal write on
 *                                     "have we walked past the
 *                                     skipped left edge yet".
 *   FUN_004b48b8 @ 0x4b48b8 (162 B) → gfx_sprite_rle_blit_right_clipped
 *                                     Tracks remaining visible row
 *                                     width and stops writing once
 *                                     exhausted — but keeps walking
 *                                     the RLE source pointer to the
 *                                     row's natural end.
 *
 *   FUN_004b495a @ 0x4b495a (93 B)  — private rle_skip_top_rows helper:
 *                                     given the RLE start, walk past
 *                                     `top_skip` rows of sprite_w
 *                                     pixels each and return the
 *                                     pointer to the first un-skipped
 *                                     row.  Called from the prologue
 *                                     of all three blitters.
 *
 * State globals — all reused from gfx_glyph / gfx_dispatch via existing
 * getters.  The wrapper FUN_0040a3d0 stages the same fields the first-
 * family wrapper (FUN_0040a21a) stages, but does NOT pre-bake the
 * top_skip/left_skip into src_offset — the RLE family handles both
 * intrinsically (top_skip via rle_skip_top_rows, left_skip via the
 * pre-shifted dest pointer in the left-clip blitter).
 *
 *   sprite_w         DAT_005c9258 — descriptor width.  gfx_glyph_glyph_w().
 *   src_offset       DAT_0058fe04 — byte offset from sprite_blob base
 *                                   to the RLE-stream start of row 0.
 *                                   gfx_glyph_glyph_offset().
 *   height           DAT_005bb478 — visible row count (post-clip).
 *                                   gfx_glyph_glyph_h().
 *   cursor_x         DAT_00591524 — top-left dest X.  gfx_glyph_cursor_x()
 *                                   (consumed indirectly via dest_origin).
 *   cursor_y         DAT_00591528 — top-left dest Y.  gfx_glyph_cursor_y().
 *   canvas pitch     DAT_004eb274 — gfx_paint_canvas_width().
 *   top_skip         DAT_0058feac — number of source rows to walk past
 *                                   before the first visible row.
 *                                   gfx_dispatch_top_skip().
 *   left_skip        DAT_0059150c — number of source pixels at the
 *                                   start of each row whose writes are
 *                                   suppressed (left-clip variant).
 *                                   Aliased with src_stride_pad in the
 *                                   first family — for x_clip_mode == 1
 *                                   the values are equal.
 *                                   gfx_glyph_src_stride_pad().
 *   clipped_w        DAT_00591514 — visible row width budget (right-
 *                                   clip variant).
 *                                   gfx_glyph_clipped_w().
 *
 * Canvas binding: same as gfx_sprite — bind once via gfx_paint_bind_canvas;
 * the blitter computes its dest pointer from gfx_paint_canvas_pixels()
 * + cursor_x + cursor_y * canvas_width.
 *
 * Quirks preserved verbatim — see engine-quirks.md for narratives:
 *
 *   - Unclipped opcode 0x0F is folded into retail's `else` arm.  Any
 *     byte value 0x11..0xFF therefore decodes as a 15-byte literal
 *     (with one byte of pixel data lost to the opcode advance).  No
 *     known blob emits these, but the misalignment is reachable in
 *     principle and preserved.
 *
 *   - Per-row pixel-budget exhaustion isn't checked inside literal
 *     opcodes 0x01..0x10 — once the row's natural sprite_w pixels are
 *     consumed by skip-or-literal opcodes, the loop exits.  But a
 *     literal-of-N can run iVar5 past zero by up to N pixels (the
 *     opcode commits to writing all N bytes before the row counter is
 *     consulted again).  Preserved verbatim — retail RLE streams are
 *     expected to terminate at exactly sprite_w pixels per row.
 *
 *   - Left-clip variant DECREMENTS the per-row pixel budget on
 *     skip-opcode advances (iVar6 -= skip_count) using the same
 *     counter that gates writes (iVar6 < 1 → write).  A skip-opcode
 *     larger than left_skip in the FIRST row consumes the entire
 *     left-skip budget at once.  Behaviorally correct for the
 *     "walked past the left edge" semantics.
 *
 *   - Right-clip variant SHARES the row budget with the literal
 *     opcodes — iVar6 decrements both on skip-opcode advances AND on
 *     per-pixel literal writes.  When iVar6 reaches 0 writes stop but
 *     the source pointer keeps walking until the outer iVar8 (= row
 *     pixels remaining) hits 0.  Equivalent to "stop writing after
 *     clipped_w visible pixels".
 */

#ifndef OPENLORDS2_GFX_SPRITE_RLE_H
#define OPENLORDS2_GFX_SPRITE_RLE_H

#include <stdint.h>

/* Port of FUN_004b4537 @ 0x4b4537.  Unclipped RLE sprite blit.
 *
 * Reads `gfx_glyph_glyph_h()` rows of `gfx_glyph_glyph_w()` pixels
 * each.  The RLE source starts at `sprite_blob + glyph_offset + Σ rows
 * skipped` (the prologue calls the row-skip helper to advance past
 * `top_skip` rows of source).  Within each row, opcodes are:
 *   0x00 N   → advance dest pointer by N pixels (transparency)
 *   0x01..N  → copy N source bytes to dest (literal pixels)
 *
 * Dest pointer advances by canvas_width at end of each row (via the
 * dst_stride_pad term `0x280 - sprite_w` — but the retail wrapper
 * computes this as `canvas_w - sprite_w`, which equals canvas_width's
 * stride between successive natural-width rows). */
void gfx_sprite_rle_blit(const uint8_t *sprite_blob);

/* Port of FUN_004b4814 @ 0x4b4814.  Left-clipped RLE sprite blit.
 *
 * Same RLE format as the unclipped variant; the prologue pre-shifts
 * the dest pointer LEFT by `left_skip` pixels so that the first
 * `left_skip` source pixels (which would lie before the visible dest)
 * are walked over without affecting visible output.  Writes are
 * gated on a per-row counter that decrements by both opcode-skip
 * counts and literal-pixel writes; once the counter has been
 * decremented by `left_skip` total, writes are enabled. */
void gfx_sprite_rle_blit_left_clipped(const uint8_t *sprite_blob);

/* Port of FUN_004b48b8 @ 0x4b48b8.  Right-clipped RLE sprite blit.
 *
 * Same RLE format; the per-row visible budget starts at `clipped_w`
 * and decrements on every advance (skip or literal).  Writes are
 * gated on "budget > 0".  Once the budget reaches 0, writes stop;
 * the source pointer keeps walking until the natural row-pixel
 * counter reaches 0 (so the next row's RLE start lines up with the
 * source data). */
void gfx_sprite_rle_blit_right_clipped(const uint8_t *sprite_blob);

#endif /* OPENLORDS2_GFX_SPRITE_RLE_H */
