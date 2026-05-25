/*
 * OpenLords2 — word-wrap renderer + localization-string render wrappers.
 *
 * Sits one layer above `gfx_text_render_line`.  Provides four entry
 * points that pre-process source text (load from the localization
 * blob, walk to a sub-string, wrap by per-word pixel width) before
 * handing each assembled line to the per-line text renderer.
 *
 * Retail address map:
 *
 *   FUN_00402d37 @ 0x402d37 (213 B) → gfx_wrap_localized_line
 *                                     Single-line localized render.  Loads
 *                                     a localization sub-string and calls
 *                                     gfx_text_render_line on it.
 *
 *   FUN_00402c5e @ 0x402c5e (217 B) → gfx_wrap_localized_line_centered
 *                                     Single-line localized centered render.
 *                                     Same as above but calls the centering
 *                                     wrapper.
 *
 *   FUN_0040352f @ 0x40352f (458 B) → gfx_wrap_buffer
 *                                     Multi-line word-wrap of a caller-
 *                                     supplied byte buffer (no localization
 *                                     lookup, no leading-space suppression,
 *                                     no pagination).  Uses the small
 *                                     200-byte line buffer.
 *
 *   FUN_0040328e @ 0x40328e (673 B) → gfx_wrap_localized
 *                                     Multi-line word-wrap of a localization
 *                                     sub-string with two-stage pagination
 *                                     (first-page width then continuation
 *                                     width).  Suppresses spaces at the
 *                                     start of every wrapped line.  Uses
 *                                     the large 2000-byte line buffer and
 *                                     reports `max_line_x`.
 *
 * Why these are one module:
 *
 *   All four call `gfx_text_render_line` (or its centered wrapper)
 *   under the same renderer-mode flags (`gfx_text_set_*`), all four
 *   prime the same one-shot "substitution armed / collapse armed"
 *   register set, and the two multi-line variants share an inner-loop
 *   structure that just differs in feature flags.  Grouping them in
 *   `gfx_wrap` keeps the per-line renderer's source clean and lets
 *   tests exercise the wrap-specific quirks in one file.
 *
 * Module-private state (mirrors retail BSS):
 *
 *   max_line_x  DAT_005aeb88 — running max of `gfx_text_line_x_acc`
 *                              across every emitted line of the most
 *                              recent multi-line render.  Reset to 0 at
 *                              the top of `gfx_wrap_localized`; updated
 *                              after each line by both multi-line
 *                              entries.  `gfx_wrap_buffer` does NOT
 *                              reset it — see quirk #36.  Read by
 *                              upstream UI code (e.g. retail's scenario-
 *                              hint centering at 0x476e95).
 *
 *   tall_font   DAT_005b2fa0 — pointer-equality discriminator: when
 *                              the caller's `font_base` equals the
 *                              registered tall_font pointer, the per-
 *                              line Y advance is 24 px instead of 16.
 *                              Retail uses the address of a specific
 *                              font blob's BSS location; callers in
 *                              our port register that same blob via
 *                              `gfx_wrap_set_tall_font_marker`.  Until
 *                              set, every font renders at 16-px line
 *                              height (the small-font default).
 *
 * Pure C; no Win32 deps.
 */

#ifndef OPENLORDS2_GFX_WRAP_H
#define OPENLORDS2_GFX_WRAP_H

#include <stdint.h>

#include "localization.h"

/* ──────────────────────────────────────────────────────────────────
 * Module-private state
 * ────────────────────────────────────────────────────────────────── */

/* DAT_005aeb88.  Running max of `gfx_text_line_x_acc` across every
 * line of the most recent `gfx_wrap_localized` / `gfx_wrap_buffer`
 * call.  Read by upstream UI code to center variable-width text. */
int  gfx_wrap_max_line_x(void);
void gfx_wrap_set_max_line_x(int v);   /* exposed for callers that
                                          need to seed it externally */

/* DAT_005b2fa0 — pointer-equality discriminator for the line-height
 * switch.  When `font_base == marker`, per-line Y advance is 24 px;
 * else 16 px.  Caller registers the address of the "tall" font blob
 * (the 24-px-tall variant) once at startup. */
void gfx_wrap_set_tall_font_marker(const uint8_t *marker);

/* ──────────────────────────────────────────────────────────────────
 * Single-line localized renderers
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00402d37 — render one localization sub-string in a
 * single line.  Seeks `loc->blob` to entry `loc_index`, walks
 * `skip_strings` NUL-boundaries (with the NUL-after-NUL boundary
 * mode), skips leading control bytes, then calls
 * `gfx_text_render_line`.  Clears the dispatcher's alt-clip flag at
 * the end.  When the initial offset is out of bounds, the call is a
 * no-op.  All renderer-mode flags (`gfx_text_set_*`) take effect. */
void gfx_wrap_localized_line(localization_state_t *loc,
                             int loc_index,
                             int skip_strings,
                             int cursor_x,
                             int cursor_y,
                             const uint8_t *font_base,
                             const uint8_t *char_glyph_table,
                             uint8_t color);

/* Port of FUN_00402c5e — same as `gfx_wrap_localized_line` but
 * routes through `gfx_text_render_line_centered`.  `width` is the
 * target box width in pixels; `char_glyph_table_metrics` is passed
 * to the pre-measurer (see gfx_text.h). */
void gfx_wrap_localized_line_centered(localization_state_t *loc,
                                      int loc_index,
                                      int skip_strings,
                                      int cursor_x,
                                      int cursor_y,
                                      int width,
                                      const uint8_t *font_base,
                                      const uint8_t *char_glyph_table,
                                      const unsigned char *char_glyph_table_metrics,
                                      uint8_t color);

/* ──────────────────────────────────────────────────────────────────
 * Multi-line word-wrap renderers
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_0040352f — multi-line word-wrap of a caller-supplied
 * NUL-terminated byte buffer.
 *
 * Per-word measurement uses `font_metrics_word_width` (the same
 * "word ends at internal space/dollar/NUL" rule used by retail).
 * Each line is assembled into a 200-byte buffer by appending whole
 * words until the running pixel width would exceed `line_width`,
 * then handed to `gfx_text_render_line`.  Hard cap of 99 lines.
 *
 * Behavior notes (verbatim from retail):
 *   - The 200-byte intermediate buffer is module-static — callers
 *     must not call this re-entrantly across threads (engine is
 *     single-threaded).
 *   - `gfx_wrap_max_line_x` is updated after each line but NOT
 *     reset at the top.  Caller must reset via
 *     `gfx_wrap_set_max_line_x(0)` if a clean reading is needed.
 *     See quirk #36.
 *   - `gfx_text_line_x_acc` and the dispatcher's alt-clip flag are
 *     reset by the per-line renderer for every line.
 *   - Per-line Y advance is 24 px iff `font_base == tall_font_marker`,
 *     else 16 px.  The bump is applied to both the local cursor and
 *     `gfx_text_line_y_acc`.
 *   - The `gfx_text_enable_substitution` and `gfx_text_set_collapse_active`
 *     flags are armed before each per-line render — i.e. substitution
 *     fires on every line, and the collapse_active flag suppresses
 *     the consecutive-space-collapse mid-line (retail behavior:
 *     wrap-driven calls handle word boundaries themselves and don't
 *     want the renderer's collapse rewinding). */
void gfx_wrap_buffer(const uint8_t *buf,
                     int cursor_x,
                     int cursor_y,
                     int line_width,
                     const uint8_t *font_base,
                     const uint8_t *char_glyph_table,
                     const unsigned char *char_glyph_table_metrics,
                     uint8_t color);

/* Port of FUN_0040328e — multi-line word-wrap of a localization
 * sub-string, with two-stage pagination and leading-space
 * suppression.
 *
 * The text is fetched via `localization_offset` + walker advance
 * (same seek as `gfx_wrap_localized_line`).  Then:
 *
 *   - For the first `first_page_lines` lines, use `cursor_x` /
 *     `first_page_width` as the per-line origin / pixel budget.
 *   - From line `first_page_lines + 1` onward, switch to
 *     `next_page_x` / `next_page_width`.  This models the retail
 *     pattern where the first page leaves room for a leading
 *     decoration and continuation pages reclaim it.
 *
 * Differences from `gfx_wrap_buffer`:
 *   - Resets `gfx_wrap_max_line_x` to 0 at the top — caller does
 *     NOT need to seed it.
 *   - Suppresses leading spaces on every wrapped line until the
 *     first printable char (so a soft wrap doesn't push the next
 *     line in by a few pixels).
 *   - Uses a 2000-byte intermediate line buffer (vs. 200 in
 *     `gfx_wrap_buffer`) — supports much longer lines.
 *
 * Hard cap of 99 lines.  Returns when the localization walker hits
 * a sub-string terminator. */
void gfx_wrap_localized(localization_state_t *loc,
                        int loc_index,
                        int skip_strings,
                        int cursor_x,
                        int cursor_y,
                        int first_page_width,
                        int first_page_lines,
                        int next_page_x,
                        int next_page_width,
                        const uint8_t *font_base,
                        const uint8_t *char_glyph_table,
                        const unsigned char *char_glyph_table_metrics,
                        uint8_t color);

#endif /* OPENLORDS2_GFX_WRAP_H */
