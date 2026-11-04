/*
 * OpenLords2 — strategic-map territory-info panel (see engine_hud_panel.h, docs/findings/render-mapview.md).
 *
 * Ports FUN_0040f7d3 (the entry with its change-detection gates) + FUN_0040fec1 (resource-queue builder) + the
 * eight resource subdrawers FUN_004100af / 41023a / 4103c5 / 410502 / 410598 / 41062e / 4106c4 / 4107d1 (and the
 * trivial FUN_0041ac3e wrapper), and FUN_0041a734 (the End-turn strip), on retail's layout: the county records
 * (g_per_army_768), the save slots, the queues / rows / heights (engine_mapview_globals.h), the gfx text flags
 * (gfx_globals.h).  The fonts, char maps, localization walker and misc sheet come from the bound environment.
 */

#include "engine_hud_panel.h"

#include <stddef.h>
#include <string.h>

#include "engine_combat.h"         /* g_combat_flag_530d0 (DAT_005530d0) */
#include "engine_dirty_rect.h"
#include "engine_mapview_globals.h"
#include "gfx_globals.h"
#include "gfx_sprite_render.h"     /* gfx_sprite_render_from_blob (40a21a), _opaque_from_blob (40a567) */
#include "gfx_text.h"              /* gfx_text_render_line_centered (4025d7) */
#include "gfx_text_label.h"        /* render_dec (402f64) / _dec_centered (4030c6) / _labeled_signed (402e0c) */
#include "gfx_wrap.h"              /* gfx_wrap_localized_line (402d37) / _centered (402c5e) */
#include "save.h"
#include "save_schema_globals.h"

extern int g_engine_state;         /* DAT_0052af94 (engine.c) */

/* The label / suffix strings the panel passes (retail DAT_004d3d34..84): single spaces except the ration's "%". */
static const char LBL_SPACE[] = " ";
static const char LBL_PCT[]   = "%";

/* ----------------------------------------------------------------- */
/* The bound environment (not retail globals). */

static engine_hud_panel_env_t g_env;

void engine_hud_panel_bind_env(const engine_hud_panel_env_t *env)
{
    if (env) g_env = *env;
    else memset(&g_env, 0, sizeof g_env);
}

static const uint8_t *rec(void) { return g_per_army_768 + (size_t)s_slot_0057c93c * 0x300; }
static int32_t rec_i32(int off) { int32_t v; memcpy(&v, rec() + off, sizeof v); return v; }
static int rec_s8(int off) { return (int8_t)rec()[off]; }
static int rec_u8(int off) { return rec()[off]; }

/* ----------------------------------------------------------------- */
/* FUN_0040fec1 — build the two resource-type queues from the county's resource-present flags. */
void engine_hud_panel_build_queues(int a1)
{
    const uint8_t *r = g_per_army_768 + (size_t)a1 * 0x300;
    int32_t v200, v1d4;
    memcpy(&v200, r + 0x250, 4);
    memcpy(&v1d4, r + 0x224, 4);
    for (int i = 0; i < 4; ++i) {
        g_mv_panel_queue2[i] = 0;
        g_mv_panel_queue1[i] = g_mv_panel_queue2[i];
    }
    g_mv_panel_count2 = 0;
    g_mv_panel_count1 = 0;
    if (r[0x200] != 0 || v200 != 0) g_mv_panel_queue1[g_mv_panel_count1++] = 1;
    if (r[0x201] != 0 || v1d4 != 0) g_mv_panel_queue1[g_mv_panel_count1++] = 0;
    if (r[0x204] != 0)              g_mv_panel_queue1[g_mv_panel_count1++] = 2;
    if (r[0x297] != 0)              g_mv_panel_queue2[g_mv_panel_count2++] = 6;
    if (r[0x2af] != 0)              g_mv_panel_queue2[g_mv_panel_count2++] = 4;
    if (r[0x2df] != 0)              g_mv_panel_queue2[g_mv_panel_count2++] = 5;
    if (r[0x2c7] != 0)              g_mv_panel_queue2[g_mv_panel_count2++] = 7;
    if (r[0x1c3] != 0)              g_mv_panel_queue2[g_mv_panel_count2++] = 3;
}

/* ----------------------------------------------------------------- */
/* Queue-1 subdrawers (row DAT_00553038, height DAT_0053e970). */

#define Y1 (g_mv_panel_row1 * g_mv_panel_height1)
#define Y2 (g_mv_panel_row2 * g_mv_panel_height2)

/* FUN_004100af — type 1 (wealth). */
void engine_hud_panel_draw_q1_type1(void)
{
    g_gfx_tight_shadow = 1;
    if (rec_i32(0xd0) <= rec_i32(0xd8)) {
        if (rec_i32(0xd0) < rec_i32(0xd4))
            gfx_sprite_render_from_blob(g_env.chrome, 41, 482, Y1 + 305);
        else
            gfx_sprite_render_from_blob(g_env.chrome, 38, 484, Y1 + 307);
    } else {
        gfx_sprite_render_from_blob(g_env.chrome, 76, 482, Y1 + 305);
    }
    gfx_text_label_render_labeled_signed(rec_i32(0x258), 0, (const uint8_t *)LBL_SPACE, LBL_SPACE, 516, Y1 + 313,
                                         g_env.font_res, g_env.glyph, 250, 249);
    g_gfx_tight_shadow = 0;
    g_gfx_no_shadow = 1;
    gfx_text_label_render_dec_centered(rec_i32(0x250), ' ', LBL_SPACE, 480, Y1 + 333, 60, g_env.font_menu, g_env.glyph,
                                       g_env.metrics, 63);
    ++g_mv_panel_row1;
    g_gfx_no_shadow = 0;
}

/* FUN_0041023a — type 0 (food). */
void engine_hud_panel_draw_q1_type0(void)
{
    g_gfx_tight_shadow = 1;
    if (rec_i32(0xcc) >= rec_i32(0xc4)) {
        if (rec_i32(0xc8) > rec_i32(0xc4))
            gfx_sprite_render_from_blob(g_env.chrome, 35, 485, Y1 + 302);
        else
            gfx_sprite_render_from_blob(g_env.chrome, 33, 486, Y1 + 303);
    } else {
        gfx_sprite_render_from_blob(g_env.chrome, 75, 484, Y1 + 301);
    }
    gfx_text_label_render_labeled_signed(rec_i32(0x22c), 0, (const uint8_t *)LBL_SPACE, LBL_SPACE, 516, Y1 + 313,
                                         g_env.font_res, g_env.glyph, 250, 249);
    g_gfx_tight_shadow = 0;
    g_gfx_no_shadow = 1;
    gfx_text_label_render_dec_centered(rec_i32(0x224), ' ', LBL_SPACE, 480, Y1 + 333, 60, g_env.font_menu, g_env.glyph,
                                       g_env.metrics, 63);
    ++g_mv_panel_row1;
    g_gfx_no_shadow = 0;
}

/* FUN_004103c5 — type 2. */
void engine_hud_panel_draw_q1_type2(void)
{
    g_gfx_tight_shadow = 1;
    if (rec_i32(0xdc) <= rec_i32(0xe4))
        gfx_sprite_render_from_blob(g_env.chrome, 63, 493, Y1 + 306);
    else
        gfx_sprite_render_from_blob(g_env.chrome, 77, 491, Y1 + 304);
    gfx_text_label_render_labeled_signed(rec_i32(0x20c), 0, (const uint8_t *)LBL_SPACE, LBL_SPACE, 516, Y1 + 307,
                                         g_env.font_res, g_env.glyph, 250, 249);
    if (rec_i32(0x214) != 0)
        gfx_text_label_render_dec(rec_i32(0x214), ' ', LBL_SPACE, 522, Y1 + 323, g_env.font_res, g_env.glyph, 250);
    ++g_mv_panel_row1;
    g_gfx_tight_shadow = 0;
}

/* ----------------------------------------------------------------- */
/* Queue-2 subdrawers (row DAT_0056d68c, height DAT_0053f04c). */

static void q2_simple(int sprite, int x, int value_off)
{
    g_gfx_tight_shadow = 1;
    gfx_sprite_render_from_blob(g_env.chrome, sprite, x, Y2 + 307);
    gfx_text_label_render_labeled_signed(rec_i32(value_off), 0, (const uint8_t *)LBL_SPACE, LBL_SPACE, 556, Y2 + 313,
                                         g_env.font_res, g_env.glyph, 250, 249);
    ++g_mv_panel_row2;
    g_gfx_tight_shadow = 0;
}

void engine_hud_panel_draw_q2_type4(void) { q2_simple(44, 600, 0x2c0); }   /* FUN_00410502 */
void engine_hud_panel_draw_q2_type5(void) { q2_simple(45, 594, 0x2f0); }   /* FUN_00410598 */
void engine_hud_panel_draw_q2_type6(void) { q2_simple(46, 594, 0x2a8); }   /* FUN_0041062e */

/* FUN_004106c4 — type 7. */
void engine_hud_panel_draw_q2_type7(void)
{
    g_gfx_tight_shadow = 1;
    if (rec_i32(0x120) >= rec_i32(0x118))
        gfx_sprite_render_from_blob(g_env.chrome, rec_u8(0x290) + 48, 600, Y2 + 307);
    else
        gfx_sprite_render_from_blob(g_env.chrome, rec_u8(0x290) + 79, 598, Y2 + 305);
    gfx_text_label_render_labeled_signed(rec_i32(0x2d8), 0, (const uint8_t *)LBL_SPACE, LBL_SPACE, 556, Y2 + 313,
                                         g_env.font_res, g_env.glyph, 250, 249);
    ++g_mv_panel_row2;
    g_gfx_tight_shadow = 0;
}

/* FUN_0041ac3e — the unit noun: loc group 8, sub a2 (one) / a2 + 1 (several). */
static void unit_noun(int n, int sub, int x, int y, const uint8_t *font, uint8_t color)
{
    gfx_wrap_localized_line(g_env.loc, 8, n == 1 ? sub : sub + 1, x, y, font, g_env.glyph, color);
}

/* FUN_004107d1 — type 3 (the garrison). */
void engine_hud_panel_draw_q2_type3(void)
{
    int v1 = 0;
    if (g_mv_panel_row2 <= 1)
        v1 = 6;
    g_gfx_tight_shadow = 1;
    if (rec_u8(0x1b0) != 0) {
        if (rec_i32(0xe8) <= rec_i32(0xf0))
            gfx_sprite_render_from_blob(g_env.chrome, 64, 603, v1 + Y2 + 300);
        else
            gfx_sprite_render_from_blob(g_env.chrome, 78, 597, v1 + Y2 + 297);
        int v2 = rec_u8(0x1a6);
        if (rec_i32(0x1d0) != 0 || rec_i32(0x1d4) != 0) {
            if (rec_i32(0x1d0) != 0 && rec_i32(0x1d4) != 0)
                gfx_sprite_render_from_blob(g_env.chrome, 27, 572, v1 + Y2 + 305);
            else if (rec_i32(0x1d0) != 0)
                gfx_sprite_render_from_blob(g_env.chrome, 25, 572, v1 + Y2 + 311);
            else
                gfx_sprite_render_from_blob(g_env.chrome, 26, 572, v1 + Y2 + 311);
            gfx_wrap_localized_line(g_env.loc, 71, 18, 564, v1 + Y2 + 326, g_env.font_name, g_env.glyph, 250);
        } else if (v2 != 0) {
            gfx_text_label_render_dec(v2, ' ', LBL_SPACE, 572, v1 + Y2 + 314, g_env.font_res, g_env.glyph, 250);
            unit_noun(v2, 66, 564, v1 + Y2 + 326, g_env.font_name, 250);
        }
    }
    ++g_mv_panel_row2;
    g_gfx_tight_shadow = 0;
}

/* ----------------------------------------------------------------- */
/* FUN_0040f7d3's body (LABEL_14 on): the workforce refresh on screen 2, the screen exclusions, the right-half
 * dirty mark, the owned / not-owned panel. */
static void panel_body(void)
{
    g_mv_panel_shown = (int32_t)s_slot_0057c93c;
    uint8_t scr = g_gfx_screen_byte;
    if (scr == 2) {
        if (g_env.workforce_rows) g_env.workforce_rows((int)s_slot_0057c93c);   /* FUN_0045161e */
        if (g_env.workforce_redraw) g_env.workforce_redraw();                   /* 0x4120e0 */
    }
    if (!((scr < 8 || scr > 13) && scr != 23 && (scr < 27 || scr > 32) && scr != 34))
        return;
    if (g_env.dirty) engine_dirty_rect_mark_right_half(g_env.dirty);
    int terr = (int)s_slot_0057c93c;
    int name_sub = terr + 20 * (int)s_slot_0053f034;
    if (rec_u8(5) == (int)s_slot_0057c8cc) {
        /* ---- OWNED branch ---- */
        gfx_sprite_render_opaque_from_blob(g_env.chrome, 55, 478, 156);
        gfx_sprite_render_opaque_from_blob(g_env.chrome, 56, 478, 302);
        gfx_wrap_localized_line_centered(g_env.loc, 100, name_sub, 480, 165, 160, g_env.font_menu, g_env.glyph,
                                         g_env.metrics, 63);
        g_gfx_no_shadow = 1;
        gfx_text_label_render_dec(rec_i32(0x24), ' ', LBL_SPACE, 508, 189, g_env.font_name, g_env.glyph, 63);
        gfx_text_label_render_dec(rec_s8(0x0c), ' ', LBL_SPACE, 602, 189, g_env.font_name, g_env.glyph, 63);
        gfx_wrap_localized_line_centered(g_env.loc, 61, 0, 480, 213, 76, g_env.font_name, g_env.glyph, g_env.metrics, 63);
        gfx_text_label_render_dec(rec_u8(0xb9), ' ', LBL_PCT, 506, 226, g_env.font_name, g_env.glyph, 63);
        gfx_wrap_localized_line_centered(g_env.loc, 61, 1, 564, 213, 76, g_env.font_name, g_env.glyph, g_env.metrics, 63);
        gfx_wrap_localized_line_centered(g_env.loc, 21, rec_s8(0x15d), 564, 226, 76, g_env.font_name, g_env.glyph,
                                         g_env.metrics, rec_s8(0x15d) == rec_s8(0x15e) ? 63 : 249);
        g_gfx_no_shadow = 0;
        gfx_sprite_render_from_blob(g_env.chrome, rec_s8(0x09) + 70, 552, 181);
        gfx_sprite_render_opaque_from_blob(g_env.chrome, 66, 478, 250);
        if (rec_i32(0x124) != 0)
            gfx_sprite_render_from_blob(g_env.chrome, 85, rec_s8(0x08) / 2 + 530, 260);
        else
            gfx_sprite_render_from_blob(g_env.chrome, 61, rec_s8(0x08) / 2 + 532, 262);
        engine_hud_panel_build_queues(terr);
        g_mv_panel_height1 = (g_mv_panel_count1 > 2) ? 45 : 60;
        if (g_mv_panel_count2 > 2)
            g_mv_panel_height2 = (g_mv_panel_count2 > 3) ? 30 : 45;
        else
            g_mv_panel_height2 = 60;
        g_mv_panel_row1 = 0;
        g_mv_panel_row2 = 0;
        for (int i = 0; i < g_mv_panel_count1; ++i) {
            if (g_mv_panel_queue1[i] == 1) engine_hud_panel_draw_q1_type1();
            else if (g_mv_panel_queue1[i] != 0) { if (g_mv_panel_queue1[i] == 2) engine_hud_panel_draw_q1_type2(); }
            else engine_hud_panel_draw_q1_type0();
        }
        for (int j = 0; j < g_mv_panel_count2; ++j) {
            switch (g_mv_panel_queue2[j]) {
            case 4: engine_hud_panel_draw_q2_type4(); break;
            case 5: engine_hud_panel_draw_q2_type5(); break;
            case 6: engine_hud_panel_draw_q2_type6(); break;
            case 7: engine_hud_panel_draw_q2_type7(); break;
            case 3: engine_hud_panel_draw_q2_type3(); break;
            default: break;
            }
        }
    } else {
        /* ---- NOT-OWNED branch ---- */
        gfx_sprite_render_opaque_from_blob(g_env.chrome, 58, 478, 156);
        gfx_wrap_localized_line_centered(g_env.loc, 100, name_sub, 480, 180, 160, g_env.font_menu, g_env.glyph,
                                         g_env.metrics, 63);
        int owner = rec_u8(5);
        if (owner != 0) {
            g_gfx_outline = 1;
            uint8_t v4 = g_player_strategic_352[owner * 0x160 + 8];
            gfx_wrap_localized_line_centered(g_env.loc, 15, 0, 480, 240, 160, g_env.font_menu, g_env.glyph,
                                             g_env.metrics, v4);
            gfx_wrap_localized_line_centered(g_env.loc, 15, 1, 480, 260, 160, g_env.font_menu, g_env.glyph,
                                             g_env.metrics, v4);
            gfx_text_render_line_centered(g_player_aux_44 + owner * 0x2c + 4, 480, 280, 160, g_env.font_menu,
                                          g_env.glyph, g_env.metrics, v4);
            g_gfx_outline = 0;
        }
    }
}

/* FUN_0040f7d3 — the panel, redrawn when forced (a1 == 1) or when the current county changed (the in-game,
 * strategic-ready, mode-0 gate; DAT_005530d0 2 = "redraw next", other non-zero = "suppress, count down"). */
void engine_hud_panel_draw(int a1)
{
    if (a1 == 1) {
        panel_body();
        return;
    }
    if (g_engine_state < 3) return;
    uint8_t scr = g_gfx_screen_byte;
    if (scr == 35 || scr == 36 || g_mv_strat_ready != 1 || s_slot_0057c8fc != 0) return;
    if (g_combat_flag_530d0 == 2) {
        g_mv_panel_shown = (int32_t)s_slot_0057c93c;
        g_combat_flag_530d0 = 0;
    } else if (g_combat_flag_530d0 != 0) {
        g_mv_panel_shown = 999;
    }
    if (g_combat_flag_530d0 != 0)
        --g_combat_flag_530d0;
    if (g_mv_panel_shown != (int32_t)s_slot_0057c93c)
        panel_body();
}

/* ----------------------------------------------------------------- */
/* The standalone's entry (FUN_0040f7d3(1), the forced path): `st` must describe the save-slot state (the panel
 * reads the county / realm / map id from the slots, as retail). */
void engine_hud_panel_render(const engine_hud_panel_state_t *st,
                             const uint8_t *chrome,
                             const uint8_t *font_name,
                             const uint8_t *font_res,
                             const uint8_t *font_menu,
                             const uint8_t *glyph_table,
                             const unsigned char *glyph_metrics,
                             localization_state_t *loc)
{
    if (!st || !st->army_block)
        return;
    engine_hud_panel_env_t env = g_env;
    env.chrome = chrome; env.font_name = font_name; env.font_res = font_res; env.font_menu = font_menu;
    env.glyph = glyph_table; env.metrics = glyph_metrics; env.loc = loc;
    g_env = env;
    panel_body();
}

/* ----------------------------------------------------------------- */
/* FUN_0041a734 — the End-turn strip: redrawn when forced or when DAT_00554004 changed (in game, mode 0, not on
 * screen 2): the parchment sprite 59, its 11×2-tile forced dirty box, the "End turn" label while the realm's
 * strategic +0 is below 999, the menu redraw latch. */
void engine_hud_panel_endturn(int a1)
{
    if (s_slot_0057c8fc != 0 || g_engine_state < 3)
        return;
    int v2 = g_mv_endturn_shown != g_mv_endturn_key;
    if (a1 != 0)
        v2 = 1;
    if (!v2)
        return;
    g_mv_endturn_shown = g_mv_endturn_key;
    if (g_gfx_screen_byte == 2)
        return;
    /* 0x4b1de0: an empty function */
    gfx_sprite_render_opaque_from_blob(g_env.chrome, 59, 478, 460);
    if (g_env.dirty) engine_dirty_rect_mark_box_tiled(g_env.dirty, 464, 460, 11, 2, 1);
    int32_t field0;
    memcpy(&field0, g_player_strategic_352 + (size_t)s_slot_0057c8cc * 0x160, sizeof field0);
    if (field0 < 999)
        gfx_wrap_localized_line_centered(g_env.loc, 4, 0, 478, 462, 162, g_env.font_name, g_env.glyph, g_env.metrics,
                                         22);
    g_gfx_menu_redraw_a = 1;
}

void engine_hud_panel_render_endturn(const uint8_t *chrome,
                                     const uint8_t *font_name,
                                     const uint8_t *glyph_table,
                                     const unsigned char *glyph_metrics,
                                     localization_state_t *loc,
                                     const uint8_t *realm_block,
                                     int current_realm)
{
    (void)realm_block; (void)current_realm;   /* retail reads g_player_strategic_352 / DAT_0057c8cc */
    if (!chrome)
        return;
    g_env.chrome = chrome; g_env.font_name = font_name; g_env.glyph = glyph_table; g_env.metrics = glyph_metrics;
    g_env.loc = loc;
    /* the forced body without the in-game gate (the standalone's caller owns it) */
    gfx_sprite_render_opaque_from_blob(chrome, 59, 478, 460);
    if (g_env.dirty) engine_dirty_rect_mark_box_tiled(g_env.dirty, 464, 460, 11, 2, 1);
    int32_t field0;
    memcpy(&field0, g_player_strategic_352 + (size_t)s_slot_0057c8cc * 0x160, sizeof field0);
    if (field0 < 999)
        gfx_wrap_localized_line_centered(loc, 4, 0, 478, 462, 162, font_name, glyph_table, glyph_metrics, 22);
}
