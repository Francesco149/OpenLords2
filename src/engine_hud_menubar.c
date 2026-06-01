/*
 * OpenLords2 — strategic-map top menu bar (HUD chrome).  See
 * engine_hud_menubar.h for the relationship to FUN_00419c78 and the list
 * of deferred (data-dependent) pieces.
 */

#include "engine_hud_menubar.h"

#include <stddef.h>

#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_sprite.h"
#include "gfx_line.h"
#include "gfx_text.h"
#include "gfx_wrap.h"
#include "font_metrics.h"

/* Sheet directory: 8-byte header, then 16-byte descriptors (same layout
 * gfx_panel.c / engine_button.c stage from). */
#define DESC(sheet, idx) ((sheet) + 8 + (idx) * 0x10)

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Port of FUN_00409e09 @ 0x409e09. */
void engine_hud_menubar_tile_strip(const uint8_t *chrome_sheet,
                                   int x, int y, int cols, int rows)
{
    if (!chrome_sheet) {
        return;
    }

    /* Retail snapshots the running cursor (DAT_00591508/504 ← 524/528)
     * and restores it on exit; the menu-bar caller rewrites the cursor
     * before every later draw, so the save/restore is inert here. */
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int idx = (col & 7) + 0xc4;          /* parchment tiles 0xc4..0xcb */
            const uint8_t *d = DESC(chrome_sheet, idx);
            int width  = (int)rd16(d + 0);       /* DAT_005c9258 */
            int hshort = (int)rd16(d + 2);       /* DAT_005aeb70 */
            int offset = (int)rd32(d + 4);       /* DAT_0058fe04 */
            int rowc   = d[0xd];                 /* DAT_005bb478 (glyph_h) */

            if (offset > 310000 || offset < 1) {
                return;                          /* retail logs + latches, aborts */
            }

            gfx_dispatch_set_last_glyph_index(idx);        /* DAT_005c9288 */
            gfx_glyph_set_dest(col * 0x18 + x, row * 0x18 + y);
            gfx_glyph_set_glyph(offset, width, rowc);
            gfx_dispatch_set_glyph_height_short(hshort);
            gfx_sprite_blit_24x24_raw(chrome_sheet);
        }
    }
}

/* Port of FUN_0040c5b0 @ 0x40c5b0. */
void engine_hud_menubar_labels(int16_t *table, int count, int hovered,
                               const uint8_t *font,
                               const uint8_t *glyph_table,
                               const unsigned char *glyph_metrics,
                               localization_state_t *loc, uint8_t color)
{
    /* FUN_004b1de0() is empty; the DAT_004eb260 / DAT_004e65f8 redraw
     * latches it sets have no consumer in the offscreen compose path. */
    int16_t *rec = table;
    int y = rec[2];

    gfx_text_set_line_x_acc(rec[0]);             /* DAT_005cd404 = item0.x */

    for (int i = 1; i <= count; i++) {
        int textid = rec[3];

        rec[0] = (int16_t)gfx_text_line_x_acc(); /* *param_1 = running cursor */
        int x = rec[0];
        y = rec[2];

        /* Stage the localization walker (retail FUN_004018d7) — its only
         * consumer is the hovered-item measure below. */
        localization_seek_skip(loc, textid, 0);

        if (i == hovered) {
            /* Highlight box sized from the measured label width. */
            font_metrics_state_t fm;
            fm.char_glyph_index = glyph_metrics;
            int w = font_metrics_buffer_width_bounded(
                        &fm, loc->blob + loc->walker_offset, font);
            int glyph_w = ((w + 4) >> 4) + 2;    /* 16-px column groups + 2 */

            gfx_text_set_no_shadow(1);           /* DAT_005aea40 = 1 */
            gfx_glyph_set_glyph(0, glyph_w, 0);  /* stage glyph_w (DAT_005c9258) */
            gfx_dispatch_set_glyph_height_short(0x12);
            gfx_sprite_fill_block(x - 2, y - 3, 0x3f);
            gfx_wrap_localized_line(loc, textid, 0, x, y, font, glyph_table, 0x18);
            gfx_text_set_no_shadow(0);
        } else {
            gfx_wrap_localized_line(loc, textid, 0, x, y, font, glyph_table, color);
        }

        rec[1] = (int16_t)gfx_text_line_x_acc();           /* param_1[1] */
        gfx_text_set_line_x_acc(gfx_text_line_x_acc() + 0x20);
        rec += 8;
    }

    (void)y;
    /* Retail tails with FUN_004527a6(item0.x-2, y-1, 0x14, 2, 2) — a
     * dirty-rect mark; the offscreen compose path tracks dirty regions at
     * the shim level, so it is elided here. */
}

void engine_hud_menubar_render_chrome(const uint8_t *chrome_sheet,
                                      const uint8_t *menu_font,
                                      const uint8_t *glyph_table,
                                      const unsigned char *glyph_metrics,
                                      localization_state_t *loc,
                                      int16_t *menu_table)
{
    /* FUN_00419c78 front: two parchment strips then the bevel frame. */
    engine_hud_menubar_tile_strip(chrome_sheet, 0,     0, 0x19, 1);
    engine_hud_menubar_tile_strip(chrome_sheet, 0x250, 0, 2,    1);
    gfx_line_rect_bevel_dark_tl(0, 0, 0x280, 0x18);   /* FUN_00403fdd */

    /* The File/Options/Help labels (no item hovered in the static frame). */
    engine_hud_menubar_labels(menu_table, 3, 0,
                              menu_font, glyph_table, glyph_metrics, loc, 0x3f);
}

int16_t *engine_hud_menubar_default_table(int16_t buf[24])
{
    /* DAT_004dc428, extracted from retail (three int16_t[8] records,
     * stride 0x10).  Only the fields the labels fn reads are meaningful:
     *   [0] x = 10   [2] y = 6   [3] loc id = 1/2/3 (File/Options/Help)
     * The remaining shorts ([4..5] submenu-table pointer, [6] item count)
     * drive the unported click/submenu path and are left zero. */
    for (int i = 0; i < 24; i++) {
        buf[i] = 0;
    }
    for (int i = 0; i < 3; i++) {
        buf[i * 8 + 0] = 10;        /* x  */
        buf[i * 8 + 2] = 6;         /* y  */
        buf[i * 8 + 3] = i + 1;     /* localization id */
    }
    return buf;
}
