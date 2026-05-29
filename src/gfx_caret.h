/*
 * OpenLords2 — text-input caret renderer.
 *
 * Pure-C port of FUN_0040acce @ 0x40acce (324 B) — the blinking
 * cursor drawn over a one-line text-input field (the title-screen
 * name box FUN_0041f321, save-name prompts, chat lines, …).
 *
 * The caret bridges four already-ported modules:
 *   - gfx_text   — the caret anchor (DAT_0058fd58 / DAT_0058fd54,
 *                  read via gfx_text_first_pos_x/y), recorded by the
 *                  text-field renderer when it reaches the cursor char.
 *   - text_input — the editor buffer + cursor + insert-mode flag
 *                  (DAT_005cd550 / DAT_005bb4a8 / DAT_005c9280).
 *   - font_metrics — the cursor-char pixel width (FUN_004015b9), used
 *                  to size the underscore caret.
 *   - gfx_line   — the filled-rect primitive (FUN_0040437d) the caret
 *                  paints with.
 *
 * Blink: a module-local counter (DAT_004e5998) advances by one each
 * render call, wrapping past 0x10 back to 0; the caret is visible only
 * while the counter exceeds 8 — i.e. roughly half a 17-frame cycle.
 * A single static render therefore never draws the caret (counter goes
 * 0 → 1).  Call gfx_caret_reset() to force the off-phase deterministically
 * (bring-up frame captures, tests).
 */

#ifndef OPENLORDS2_GFX_CARET_H
#define OPENLORDS2_GFX_CARET_H

#include <stdint.h>

#include "text_input.h"
#include "font_metrics.h"

/* Reset the blink counter (DAT_004e5998) to 0 — the next render call
 * lands at 1, in the off-phase. */
void gfx_caret_reset(void);

/* Current blink-counter value (DAT_004e5998).  Exposed for tests. */
int gfx_caret_blink(void);

/* Port of FUN_0040acce.  Advance the blink counter, then — when the
 * caret anchor y (gfx_text_first_pos_y / DAT_0058fd54) is non-zero —
 * measure the editor's cursor char against `font` (side effect: the
 * font_metrics cache is updated) and, if the counter is in the visible
 * phase (> 8), paint the caret at the anchor in `color`:
 *
 *   - insert_mode == 0 (overwrite): a 2-px-tall underscore spanning
 *     the cursor char's width + 2, just below the glyph row.
 *   - insert_mode != 0 (insert):    a hollow box outline (5 fill
 *     rects) bracketing the cursor column.
 *
 * `editor` supplies buf[cursor] / cursor / insert_mode; `fm` is the
 * font-metrics scratch state for the width measure. */
void gfx_caret_render(const text_input_state_t *editor,
                      font_metrics_state_t *fm,
                      const uint8_t *font,
                      uint8_t color);

#endif /* OPENLORDS2_GFX_CARET_H */
