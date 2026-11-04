/*
 * OpenLords2 — strategic-map top menu bar (HUD chrome).  See
 * engine_hud_menubar.h for the relationship to FUN_00419c78 and the list
 * of deferred (data-dependent) pieces.
 */

#include "engine_hud_menubar.h"

#include <stddef.h>

#include "gfx_globals.h"
#include "gfx_glyph.h"
#include "gfx_sprite_render.h"   /* gfx_sprite_render_log_error (FUN_004afab9) */
#include "gfx_dispatch.h"
#include "gfx_sprite.h"
#include "gfx_line.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
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

/* Port of FUN_00409e09 @ 0x409e09 — retail's shape (rhost shadow): the
 * glyph cursor is saved in DAT_00591508 / DAT_00591504 and put back on
 * exit; every tile writes the last-glyph index, the cursor and the whole
 * descriptor (incl. the flag bytes) before the src_offset gate; a bad
 * offset logs (FUN_004afab9: 0x4d22a4 / 0x4d22c0, tile index + 1) and
 * latches error 1, returning WITHOUT the restore.  Retail bakes in the
 * chrome sheet DAT_0057d3d0; the caller passes the bound blob. */
void engine_hud_menubar_tile_strip(const uint8_t *chrome_sheet,
                                   int x, int y, int cols, int rows)
{
    if (!chrome_sheet) {
        return;                                  /* drop-in guard: retail's sheet is resident */
    }

    g_gfx_saved_cursor_x = g_gfx_cursor_x;
    g_gfx_saved_cursor_y = g_gfx_cursor_y;
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            int k = col % 8;                     /* parchment tiles 0xc4..0xcb */
            g_gfx_last_glyph_index = k + 0xc4;
            g_gfx_cursor_x = col * 0x18 + x;
            g_gfx_cursor_y = row * 0x18 + y;
            const uint8_t *d = DESC(chrome_sheet, g_gfx_last_glyph_index);
            g_gfx_glyph_w            = (int16_t)rd16(d + 0);
            g_gfx_glyph_height_short = (int16_t)rd16(d + 2);
            g_gfx_glyph_offset       = (int32_t)rd32(d + 4);
            g_gfx_flag_a             = d[0xc];
            g_gfx_glyph_h            = d[0xd];
            g_gfx_flag_b             = d[0xe];

            if (g_gfx_glyph_offset > 310000) {
                gfx_sprite_render_log_error(0x4d22a4, k + 0xc5);
                g_gfx_last_error = 1;
                return;
            }
            if (g_gfx_glyph_offset < 1) {
                gfx_sprite_render_log_error(0x4d22c0, k + 0xc5);
                g_gfx_last_error = 1;
                return;
            }
            gfx_sprite_blit_24x24_raw(chrome_sheet);
        }
    }
    g_gfx_cursor_x = g_gfx_saved_cursor_x;
    g_gfx_cursor_y = g_gfx_saved_cursor_y;
}

/* Port of FUN_0040c5b0 @ 0x40c5b0 — retail's shape (rhost shadow): it
 * raises the two redraw latches DAT_004eb260 / DAT_004e65f8 (after the
 * empty FUN_004b1de0), measures the hovered label on the SHARED
 * measurer cache (DAT_005c9288 / DAT_0058fd88), stages only the
 * highlight's width / height (DAT_005c9258 / DAT_005aeb70 — not the
 * offset or row count), and tails with the dirty mark FUN_004527a6
 * (item0.x - 2, the last item's y - 1, 20 × 2 tiles, force 2) when a
 * dirty state is given.  `hovered` is retail's DAT_00522cb4. */
void engine_hud_menubar_labels(int16_t *table, int count, int hovered,
                               const uint8_t *font,
                               const uint8_t *glyph_table,
                               const unsigned char *glyph_metrics,
                               localization_state_t *loc, uint8_t color,
                               engine_dirty_rect_state_t *dirty)
{
    g_gfx_menu_redraw_a = 1;                     /* DAT_004eb260 */
    g_gfx_menu_redraw_b = 1;                     /* DAT_004e65f8 */
    int16_t *rec = table;
    int x0 = rec[0];
    int y = 0;

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
            font_metrics_state_t fm = { 0 };
            fm.char_glyph_index  = glyph_metrics;
            fm.last_glyph_index  = g_gfx_last_glyph_index;
            fm.last_glyph_offset = g_gfx_last_glyph_offset;
            int w = font_metrics_buffer_width_bounded(
                        &fm, loc->blob + loc->walker_offset, font);
            g_gfx_last_glyph_index  = fm.last_glyph_index;
            g_gfx_last_glyph_offset = fm.last_glyph_offset;

            gfx_text_set_no_shadow(1);           /* DAT_005aea40 = 1 */
            g_gfx_glyph_w = (int16_t)((w + 4) / 16 + 2);   /* 16-px column groups + 2 */
            g_gfx_glyph_height_short = 0x12;
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

    if (dirty) {
        engine_dirty_rect_mark_box_tiled(dirty, x0 - 2, y - 1, 0x14, 2, 2);   /* FUN_004527a6 */
    }
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
                              menu_font, glyph_table, glyph_metrics, loc, 0x3f, NULL);
}

/* Per-realm record layout inside g_player_strategic_352 (retail
 * DAT_0057bf00), stride 0x160 — only the fields FUN_00419c78's status
 * tail reads. */
#define HUD_REALM_STRIDE   0x160
#define HUD_REALM_FILTER   0x00    /* int32; flag drawn only when < 999 */
#define HUD_REALM_ACTIVE   0x04    /* byte;  nonzero ⇒ realm in play     */
#define HUD_REALM_FLAGID   0x0a    /* byte;  +0x55 = flag sprite index   */
#define HUD_REALM_GOLD     0x118   /* int32; crowns (retail DAT_0057c018) */

void engine_hud_menubar_render_status(const engine_hud_menubar_status_t *st,
                                      const uint8_t *flag_sheet,
                                      const uint8_t *font,
                                      const uint8_t *glyph_table,
                                      localization_state_t *loc)
{
    const uint8_t *block = st->realm_block;
    if (!block) {
        return;
    }

    /* FUN_00419c78: the per-realm flag shields (gated on DAT_0057c8fc==0).
     * Realms 1..5; draw a 16-px-wide flag for every active realm whose +0
     * field is < 999, packed left-to-right from x=0x10e (270), y=4. */
    if (st->hud_gate == 0 && flag_sheet) {
        int drawn = 0;
        for (int realm = 1; realm < 6; realm++) {
            const uint8_t *r = block + realm * HUD_REALM_STRIDE;
            int32_t filter = (int32_t)rd32(r + HUD_REALM_FILTER);
            if (r[HUD_REALM_ACTIVE] != 0 && filter < 999) {
                int idx = r[HUD_REALM_FLAGID] + 0x55;
                /* FUN_0040a21a(misc_cty, idx, x, 4): its success arm is
                 * byte-identical to gfx_dispatch_sprite (FUN_00404442) —
                 * the bounds-check / error-log arms never fire for a valid
                 * sheet + sprite index, so the dispatcher stands in 1:1. */
                gfx_dispatch_sprite(flag_sheet, idx, drawn * 0x10 + 0x10e, 4);
                drawn++;
            }
        }
    }

    /* Year + season (gated on DAT_0057c8fc==0). */
    if (st->hud_gate == 0) {
        /* FUN_0041a900(year, 0x168, 6, 3) — mode 3 renders the (signed)
         * number with a leading-space prefix and a single-space suffix
         * (DAT_004d41ec / DAT_004d41f0 are both " ").  Retail zeroes the
         * line-x accumulator (DAT_005cd404) at 41a900 entry so it measures
         * just this number's advance for the season's relative x. */
        gfx_text_set_line_x_acc(0);
        if (st->year < 0) {
            gfx_text_label_render_dec(-st->year, ' ', " ", 0x168, 6,
                                      font, glyph_table, 0x3f);
        } else {
            gfx_text_label_render_dec(st->year, ' ', " ", 0x168, 6,
                                      font, glyph_table, 0x3f);
        }
        /* FUN_00402d37(0x1d, season, DAT_005cd404 + 0x16c, 6, font, 0x3f):
         * the season name (loc group 0x1d, sub = season index) follows the
         * year at +0x16c past the year's measured advance. */
        int year_adv = gfx_text_line_x_acc();
        gfx_wrap_localized_line(loc, 0x1d, st->season, year_adv + 0x16c, 6,
                                font, glyph_table, 0x3f);
    }

    /* Gold "N Crowns." — ALWAYS drawn (not gated in retail).
     * FUN_0041ab67(gold, 0, 500, 6, font, 0x3f): the number with a blank
     * '@' sign placeholder and an empty suffix (DAT_004d41f4 = ""), then
     * loc group 8 — sub 0 ("Crown.") for ±1, sub 1 ("Crowns.") otherwise —
     * at x = 500 + the number's advance.  (41ab67's save/restore of
     * DAT_005cd404 has no consumer here, so it is elided.) */
    {
        int gold = (int)rd32(block + st->current_realm * HUD_REALM_STRIDE
                             + HUD_REALM_GOLD);
        gfx_text_set_line_x_acc(0);
        gfx_text_label_render_dec(gold, '@', "", 500, 6,
                                  font, glyph_table, 0x3f);
        int gold_adv = gfx_text_line_x_acc();
        int sub = (gold == 1 || gold == -1) ? 0 : 1;
        gfx_wrap_localized_line(loc, 8, sub, 500 + gold_adv, 6,
                                font, glyph_table, 0x3f);
    }

    /* FUN_004527a6(0xeb, 0, 0x1a, 2, 1) — a dirty-rect mark for the status
     * strip; the offscreen compose path tracks dirty regions at the shim
     * level, so it is elided (same as the labels' 4527a6 tail). */
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
