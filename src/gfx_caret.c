/*
 * OpenLords2 — text-input caret renderer (implementation).
 *
 * Port of FUN_0040acce.  See gfx_caret.h for the cross-module map and
 * the blink-cycle description.
 */

#include "gfx_caret.h"

#include "gfx_text.h"   /* gfx_text_first_pos_x / _y — the caret anchor */
#include "gfx_line.h"   /* gfx_line_fill_rect (FUN_0040437d)            */

/* DAT_004e5998 — blink counter.  Advances by one per render; wraps
 * past 0x10 to 0; caret visible while > 8. */
static int g_caret_blink;

void gfx_caret_reset(void)  { g_caret_blink = 0; }
int  gfx_caret_blink(void)  { return g_caret_blink; }

void gfx_caret_render(const text_input_state_t *editor,
                      font_metrics_state_t *fm,
                      const uint8_t *font,
                      uint8_t color)
{
    /* Blink advance + wrap (DAT_004e5998). */
    g_caret_blink++;
    if (g_caret_blink > 0x10) {
        g_caret_blink = 0;
    }

    int caret_y = gfx_text_first_pos_y();   /* DAT_0058fd54 */
    if (caret_y == 0) {
        return;
    }

    /* Measure the editor's cursor char (DAT_005cd550[DAT_005bb4a8])
     * against the field font.  Retail evaluates this inside the same
     * `&&` as the visibility test, so the measure (and its cache side
     * effect) runs whenever caret_y != 0, regardless of blink phase. */
    unsigned char cursor_char = editor->buf[editor->cursor];
    int char_w = font_metrics_char_width(fm, cursor_char, font);

    if (g_caret_blink <= 8) {
        return;     /* off-phase — measured but not drawn */
    }

    int caret_x = gfx_text_first_pos_x();   /* DAT_0058fd58 */

    if (editor->insert_mode == 0) {
        /* Overwrite-mode underscore: char width + 2 px wide, 2 px tall,
         * sitting one anchor-row below the glyph (caret_y + 0xe). */
        gfx_line_fill_rect(caret_x - 1, caret_y + 0xe, char_w + 2, 2, color);
    } else {
        /* Insert-mode hollow box bracketing the cursor column. */
        gfx_line_fill_rect(caret_x - 2, caret_y - 2, 1, 0xf, color);
        gfx_line_fill_rect(caret_x - 4, caret_y - 3, 2, 1, color);
        gfx_line_fill_rect(caret_x - 4, caret_y + 0xe, 2, 1, color);
        gfx_line_fill_rect(caret_x - 1, caret_y - 3, 2, 1, color);
        gfx_line_fill_rect(caret_x - 1, caret_y + 0xe, 2, 1, color);
    }
}
