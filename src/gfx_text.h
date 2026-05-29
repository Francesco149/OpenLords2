/*
 * OpenLords2 — text-line renderer (per-character driver).
 *
 * Sits one layer above the glyph dispatcher (gfx_dispatch).  Walks a
 * null-terminated byte buffer one character at a time and, for each
 * printable char, runs the dispatcher up to three times to paint a
 * drop shadow, an outline, or a plain glyph depending on the mode
 * flags set before the call.  Also implements the in-band substitution
 * stream (the '#' macro), the consecutive-space collapse, and the
 * uppercase-color override.
 *
 * Retail address map:
 *
 *   FUN_00402637 @ 0x402637 (989 B) → gfx_text_render_line
 *                                     Per-line text renderer.  Walks
 *                                     a byte buffer with mode-dependent
 *                                     shadow/outline rendering.
 *   FUN_00402c16 @ 0x402c16 ( 72 B) → gfx_text_substitution_next (static)
 *                                     Pulls one byte from the '#' macro
 *                                     stream; wraps to space (' ') and
 *                                     resets the cursor when the stream
 *                                     terminator (NUL) is hit.
 *   FUN_004025d7 @ 0x4025d7 ( 96 B) → gfx_text_render_line_centered
 *                                     Centering wrapper.  Pre-measures
 *                                     via font_metrics_buffer_width_bounded
 *                                     and shifts X to center within a
 *                                     supplied width.
 *
 * Rendering modes (DAT_005aea40 / DAT_0058fe9c / DAT_005aeb90):
 *
 *   shadow      — default (no_shadow=0, outline=0, tight_shadow=0):
 *                 paints the glyph THREE times.  Pass 1 at
 *                 (cursor_x, cursor_y-1) with shadow_color_a, pass 2 at
 *                 (cursor_x, cursor_y+1) with shadow_color_b, pass 3 at
 *                 (cursor_x, cursor_y) with the foreground color.
 *                 shadow_color_a/b depend on a language tag — see
 *                 `gfx_text_set_language_tweak` and quirk #32.
 *   tight_shadow — no_shadow=0, outline=0, tight_shadow=1: pass 1 at
 *                 (cursor_x+1, cursor_y+1) with color 0x3f, pass 2 at
 *                 (cursor_x, cursor_y) with the foreground color.
 *                 Used by the in-game statistic-readouts cluster.
 *   outline     — no_shadow=0, outline=1 (tight_shadow ignored):
 *                 pass 1 at (cursor_x, cursor_y-1) color 0x3f, pass 2 at
 *                 (cursor_x, cursor_y+1) color 0x26, pass 3 at
 *                 (cursor_x, cursor_y) with the foreground color.
 *                 Used by emboss/border text in the scenario picker.
 *   plain       — no_shadow=1: ONE pass at (cursor_x, cursor_y) with
 *                 the foreground color.  No shadow / outline.  Used by
 *                 character-selector UI text and editor-cursor previews.
 *
 * Renderer-private state (mirrors retail DAT_*):
 *
 *   substitution_buf  DAT_005bb4d0 — 256-byte byte stream used as the
 *                                    '#' macro source.  Loaded via
 *                                    `gfx_text_load_substitution_stream`;
 *                                    consumed one byte at a time by the
 *                                    renderer.  A NUL byte resets the
 *                                    cursor to 0 and yields ' ' (the
 *                                    stream is "circular with a soft
 *                                    space terminator").
 *   substitution_idx  DAT_005bb4b4 — cursor into substitution_buf.
 *                                    When non-zero AT THE TOP OF THE LOOP
 *                                    the renderer reads the NEXT char
 *                                    from the stream instead of advancing
 *                                    the source pointer — i.e. once a
 *                                    substitution kicks in, the renderer
 *                                    stays on the stream until it wraps
 *                                    (NUL hit → cursor = 0 → next loop
 *                                    iter resumes from source).
 *   substitution_on   DAT_005bb4c4 — bool.  When 1, the '#' character in
 *                                    the source triggers a substitution
 *                                    read; when 0, '#' renders as space.
 *                                    Cleared at end of every render.
 *   collapse_spaces       DAT_005aeb7c — bool.  When 1, runs of two
 *                                        consecutive ' ' or '_' chars
 *                                        collapse to a single advance.
 *   collapse_active       DAT_005c925a — caller-set bool.  When the word-
 *                                        wrap caller is doing the LAST
 *                                        word on a line, it sets this so
 *                                        the renderer's collapse-spaces
 *                                        path stays disarmed.  Cleared
 *                                        at end of every render.
 *   uppercase_highlight   DAT_0058fe2c — bool.  When 1, uppercase A..Z
 *                                        chars force the foreground color
 *                                        to 1 (a UI accent palette index).
 *
 *   line_x_acc        DAT_005cd404 — running X advance for the current
 *                                    line.  The renderer accumulates
 *                                    `+= glyph_w + 1` per char (and
 *                                    `+= 4` at end-of-line).  Word-wrap
 *                                    callers reset this to 0 before each
 *                                    line and read it after.
 *   line_y_acc        DAT_005cd4f8 — running Y for the current line.
 *                                    Read & written by word-wrap callers;
 *                                    the renderer itself only consults it
 *                                    via the first-position hook.
 *   char_counter      DAT_0058fd78 — count of characters dispatched so
 *                                    far across calls.  Tested against
 *                                    `first_pos_target` once per char to
 *                                    record the position where a target
 *                                    character index lands.  Incremented
 *                                    every iteration regardless of glyph
 *                                    presence.
 *   first_pos_target  DAT_005bb4a8 — caller-set char index to capture.
 *                                    When `char_counter == first_pos_target`
 *                                    on a given iteration (and the latch
 *                                    isn't already set), the renderer
 *                                    snapshots `line_x_acc` and
 *                                    `line_y_acc` for the caller.
 *   first_pos_latch   DAT_005aeb64 — bool.  Set when the first-position
 *                                    hook fires; the caller clears it
 *                                    before each render.
 *   first_pos_x       DAT_0058fd58 — recorded `line_x_acc` at the hook.
 *   first_pos_y       DAT_0058fd54 — recorded `line_y_acc` at the hook.
 *
 * The char→glyph translation table is the same 256-byte one used by
 * font_metrics and the dispatcher (retail's DAT_004d71f0).  Callers
 * supply it explicitly here for testability; the runtime will pass a
 * fixed table pointer once we wire that up.
 *
 * Quirks preserved verbatim — see engine-quirks.md for narratives:
 *
 *   - Substitution-stream priority is one-shot at the top of every loop
 *     iteration, NOT just on '#'.  Once the substitution cursor leaves
 *     0 (first '#' hit), every subsequent character comes from the
 *     stream until the NUL terminator wraps the cursor back to 0.
 *
 *   - Char codes < 0x20 are silently skipped (no glyph, no width).  The
 *     check `0x1f < local_c` gates the entire glyph + cursor-advance
 *     block.  Control bytes pass through the loop and trigger the
 *     loop-advance / consecutive-space-collapse logic but emit nothing.
 *
 *   - The renderer subtracts 0x20 from the char code BEFORE indexing
 *     the char-glyph table and passing it to the dispatcher.  So the
 *     table is 0x20-relative (entry 0 = ASCII 0x20 = space).  Faithful
 *     to retail's `(&DAT_004d71f0)[local_c - 0x20]` indexing.
 *
 *   - Missing glyphs (table entry = 0) skip the dispatcher entirely
 *     but STILL advance the cursor by 4 pixels.  Retail's fallback
 *     spacing for unknown characters.
 *
 *   - The trailing-space collapse compares the post-decrement char to
 *     the pre-decrement char (bVar1 in retail).  Two adjacent
 *     space/underscore characters rewind by the just-applied width.
 *     The intent appears to be word-wrap-style collapsing of multiple
 *     spaces, gated on `collapse_active` being clear AND
 *     `collapse_spaces` being set.
 *
 *   - The shadow-color pair (a,b) defaults to (0x10, 0x1f) but flips
 *     to (0x36, 0x2c) when DAT_004eac50 is 0x1f or 0x1c.  Reads like
 *     a language-tagged palette swap — Polish and Czech localizations
 *     likely use the alternate scheme.  Per-call configurable here.
 *
 *   - The renderer clears `substitution_on`, `substitution_idx`,
 *     `collapse_active`, and the dispatcher's `alt_clip_enabled` flag
 *     at the END of every call.  The fixed substitution_buf bytes are
 *     left intact; the caller arms a fresh substitution by re-setting
 *     `substitution_on` (and optionally re-loading the buffer).
 *
 *   - The end-of-line ` += 4` advance on `line_x_acc` is unconditional —
 *     even if the buffer was empty (no chars rendered) the accumulator
 *     advances by 4.  Acts as a per-call gap so word-wrap loops can
 *     concatenate empty fragments without breaking layout.
 */

#ifndef OPENLORDS2_GFX_TEXT_H
#define OPENLORDS2_GFX_TEXT_H

#include <stdint.h>

/* ──────────────────────────────────────────────────────────────────
 * Render-mode setters
 * ────────────────────────────────────────────────────────────────── */

/* DAT_005aea40 — when 1, render with a single pass (no shadow/outline).
 * Default 0 (shadow).  Callers set this before the render and the
 * renderer leaves it alone (does NOT clear at end). */
void gfx_text_set_no_shadow(int enabled);

/* DAT_0058fe9c — when 1 (and no_shadow == 0), use the outline path
 * (3 passes, colors 0x3f / 0x26 / fg).  Default 0. */
void gfx_text_set_outline(int enabled);

/* DAT_005aeb90 — when 1 (and no_shadow == 0 AND outline == 0), use the
 * tight-shadow path (2 passes, +1/+1 offset color 0x3f then fg).
 * Default 0.  Callers usually toggle this on/off around small UI
 * statistic readouts. */
void gfx_text_set_tight_shadow(int enabled);

/* DAT_0058fe2c — when 1, uppercase A..Z chars force the foreground
 * color to 1 for that character.  Used by the help-key indicator that
 * embeds capital letters in the menu labels. */
void gfx_text_set_uppercase_highlight(int enabled);

/* DAT_005aeb7c — when 1, the runs-of-spaces collapse path is armed.
 * Two adjacent ' ' / '_' chars collapse to one advance.  Used by the
 * word-wrap renderer to keep mid-line word breaks tidy. */
void gfx_text_set_collapse_spaces(int enabled);

/* DAT_005c925a — per-call "collapse_active" arm written by the word-
 * wrap callers immediately before each per-line render.  When set,
 * suppresses the renderer's consecutive-space-collapse rewind for
 * this call (the wrap layer is already chopping at word boundaries
 * and doesn't want adjacent spaces eaten mid-line).  Cleared by the
 * renderer at the end of every call. */
void gfx_text_set_collapse_active(int enabled);

/* DAT_004eac50 — language tag governing the shadow color pair.  When
 * 0x1c or 0x1f, shadow colors are (0x36, 0x2c); else (0x10, 0x1f).
 * Set once at startup from the language config; the renderer reads
 * this every call. */
void gfx_text_set_language_tag(int tag);

/* ──────────────────────────────────────────────────────────────────
 * Substitution stream — the '#' macro
 * ────────────────────────────────────────────────────────────────── */

/* Load a null-terminated byte stream into the substitution buffer
 * (DAT_005bb4d0).  Truncates to 255 bytes + NUL terminator.  Pass NULL
 * to leave the buffer untouched but disable substitution; pass an
 * empty string to leave it untouched but reset the cursor.
 *
 * Loading the stream does NOT automatically enable substitution — the
 * caller arms it with `gfx_text_enable_substitution(1)` so the next
 * '#' in the rendered text triggers a stream read. */
void gfx_text_load_substitution_stream(const char *bytes);

/* DAT_005bb4c4 — when 1, a '#' in the source triggers a substitution
 * read; when 0, '#' renders as space.  Cleared at the end of every
 * render. */
void gfx_text_enable_substitution(int enabled);

/* DAT_005bb4b4 — the substitution cursor.  Read-only.  Mostly useful
 * for tests that want to observe stream consumption. */
int gfx_text_substitution_cursor(void);

/* ──────────────────────────────────────────────────────────────────
 * First-position hook (DAT_005bb4a8 / DAT_005aeb64 / DAT_0058fd58,54)
 * ────────────────────────────────────────────────────────────────── */

/* Caller arms the hook by:
 *   1. Setting `target_char_index` to the 0-based char index to capture
 *      (matched against the renderer's char_counter — see header doc).
 *   2. Clearing the latch via `gfx_text_clear_first_pos_latch`.
 * After the render, the caller reads `gfx_text_first_pos_x/y()` to
 * obtain `line_x_acc` / `line_y_acc` at the matching iteration. */
void gfx_text_set_first_pos_target(int target_char_index);
void gfx_text_clear_first_pos_latch(void);
int  gfx_text_first_pos_latched(void);   /* DAT_005aeb64 */
int  gfx_text_first_pos_x(void);          /* DAT_0058fd58 */
int  gfx_text_first_pos_y(void);          /* DAT_0058fd54 */
/* Direct anchor writes (text-input field renderer FUN_0041f321). */
void gfx_text_set_first_pos_x(int x);
void gfx_text_set_first_pos_y(int y);

/* ──────────────────────────────────────────────────────────────────
 * Running accumulators (caller-owned, renderer-mutated)
 * ────────────────────────────────────────────────────────────────── */

/* DAT_005cd404 / DAT_005cd4f8.  Word-wrap callers reset both to 0
 * before each line and read line_x_acc afterwards to know how far the
 * line advanced.  line_y_acc is advanced by the caller (per-line height)
 * but the renderer reads it for the first-position hook snapshot. */
void gfx_text_set_line_x_acc(int x);
void gfx_text_set_line_y_acc(int y);
int  gfx_text_line_x_acc(void);
int  gfx_text_line_y_acc(void);

/* DAT_0058fd78 — total characters dispatched.  Persistent across
 * renders (retail does not reset it; word-wrap callers consult it
 * as a running index for the first-position hook).  Exposed for
 * tests. */
int  gfx_text_char_counter(void);
void gfx_text_set_char_counter(int n);

/* ──────────────────────────────────────────────────────────────────
 * Main entry point — port of FUN_00402637
 * ────────────────────────────────────────────────────────────────── */

/* Render a null-terminated byte buffer onto the bound gfx_paint canvas
 * at (cursor_x, cursor_y), using `font_base` as the glyph descriptor
 * source and `char_glyph_table` as the 256-byte char→glyph-index map
 * (table is 0x20-offset; entry 0 == "no glyph", maps ASCII 0x20 →
 * glyph 0).  `color` is the foreground palette index.
 *
 * Mutates the running accumulators (line_x_acc, char_counter) and the
 * dispatcher's cursor.  At the end of the call, clears
 * substitution_on / substitution_idx / collapse_active and the
 * dispatcher's alt_clip_enabled flag — caller re-arms for the next
 * render. */
void gfx_text_render_line(const uint8_t *buf,
                          int cursor_x,
                          int cursor_y,
                          const uint8_t *font_base,
                          const uint8_t *char_glyph_table,
                          uint8_t color);

/* Port of FUN_004025d7.  Centered variant — pre-measures via
 * font_metrics_buffer_width_bounded, then calls gfx_text_render_line
 * with a left margin that centers the buffer within `width` pixels.
 * If the measured width exceeds `width`, the offset is clamped to 0
 * (left-aligned with overflow).  `char_glyph_table_metrics` is passed
 * to the measurer; usually the same table as `char_glyph_table` but
 * decoupled to mirror retail's separate-table-per-call topology. */
void gfx_text_render_line_centered(const uint8_t *buf,
                                   int cursor_x,
                                   int cursor_y,
                                   int width,
                                   const uint8_t *font_base,
                                   const uint8_t *char_glyph_table,
                                   const unsigned char *char_glyph_table_metrics,
                                   uint8_t color);

#endif /* OPENLORDS2_GFX_TEXT_H */
