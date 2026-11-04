/* rl_render.c — a retail-layout port, partition 'render' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The partition draws through the gfx layer (src/gfx_*.c), which runs on retail's own globals (gfx_globals.h;
 * docs/findings/render-under-rhost.md): a port here calls the gfx ports by name. Three of the gfx ports take a
 * state struct rather than retail's globals — the localization walker (blob DAT_00591580, pointer DAT_005c9270),
 * the text editor + measurer cache of the caret, the dirty rectangle; the helpers below load them from retail's
 * globals and store them back around the call. Findings: docs/findings/rl-render.md. */
#include "rl.h"
#include "engine_battlefield_render.h"
#include "rl_render_globals.h"
#include "rl_io_globals.h"
#include "engine_bf_input_globals.h"
#include "engine.h"
#include "engine_combat.h"
#include "engine_territory.h"
#include "engine_dirty_rect.h"
#include "engine_army_ai.h"
#include "engine_realm_upkeep.h"
#include "engine_sendsupplies.h"
#include "engine_math.h"
#include "engine_turn.h"
#include "gfx_globals.h"
#include "gfx_line.h"
#include "gfx_panel.h"
#include "gfx_sprite.h"
#include "gfx_sprite_render.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
#include "gfx_wrap.h"
#include "gfx_caret.h"
#include "gfx_paint.h"
#include "font_metrics.h"
#include "localization.h"
#include "text_input.h"
#include "rl_io_plat.h"

#include <string.h>

/* ---------------------------------------------------------------- retail's blobs and tables, by address */
#define FONT       ((const uint8_t *)g_bf_5af8f0)        /* &DAT_005af8f0: the default 16 px font */
#define FONT_TALL  ((const uint8_t *)g_rl_5b2fa0)        /* &DAT_005b2fa0: the tall 24 px font */
#define FONT_SMALL ((const uint8_t *)g_rl_5cbfb0)        /* &DAT_005cbfb0: the small (debug) font */
#define CHROME     ((const uint8_t *)g_rl_57d3d0)        /* &DAT_0057d3d0: the chrome sheet */
#define SYSTEM     ((const uint8_t *)g_rl_5bb540)        /* &DAT_005bb540: system2.pl8 (the seal) */
#define GLYPHS     ((const uint8_t *)g_rl_4d71d0 + 0x20) /* DAT_004d71f0: the renderers' char -> glyph table */
#define METRICS    ((const unsigned char *)g_rl_4d71d0)  /* DAT_004d71d0: the measurers' table */
#define PTR(v)     ((const uint8_t *)(uintptr_t)(uint32_t)(v))   /* a retail pointer held in a dword global */
#define MISC       PTR(g_bf_5530c8)                      /* DAT_005530c8: the misc / icon sheet */
#define STR(blk, blkva, va) ((const char *)&AT(char, blk, blkva, va))
#define SCREEN     ((unsigned)engine_current_screen())   /* byte_4EAC50 (movzx) */
#define DIRTY      engine_territory_dirty_state()        /* 0x4527a6's six globals (a mirror: rh_hooks_ai.c) */
/* the player / county / territory records (save.h) */
#define PL32(p, va)  AT(int32_t, g_player_strategic_352, 0x57bf00u, (va) + 352 * (p))
#define PL8(p, va)   AT(uint8_t, g_player_strategic_352, 0x57bf00u, (va) + 352 * (p))
#define CO8(c, va)   AT(uint8_t, g_per_army_768, 0x53f9b0u, (va) + 768 * (c))
#define TE8(t, va)   AT(uint8_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))
#define TE16(t, va)  AT(int16_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))
#define TE32(t, va)  AT(int32_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))

/* ---------------------------------------------------------------- the gfx layer's state structs */
/* the localized-text walker: the port's localization_state_t over retail's blob / pointer, loaded and stored around
 * each call (the wrap renderers move the pointer) */
static localization_state_t s_loc;
static localization_state_t *loc_in(void)
{
    s_loc.blob = g_rl_591580;
    s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
    return &s_loc;
}
static void loc_out(void)
{
    uint8_t *w = g_rl_591580 + s_loc.walker_offset;
    if (g_rl_5c9270 != w) g_rl_5c9270 = w;
}
static void wrap_line(int idx, int skip, int x, int y, const uint8_t *font, int color)   /* 0x402d37 */
{
    gfx_wrap_localized_line(loc_in(), idx, skip, x, y, font, GLYPHS, (uint8_t)color);
    loc_out();
}
static void wrap_line_c(int idx, int skip, int x, int y, int w, const uint8_t *font, int color)   /* 0x402c5e */
{
    gfx_wrap_localized_line_centered(loc_in(), idx, skip, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
    loc_out();
}
static void wrap_loc(int idx, int skip, int x, int y, int w1, int n1, int x2, int w2, const uint8_t *font,
                     int color)   /* 0x40328e */
{
    gfx_wrap_localized(loc_in(), idx, skip, x, y, w1, n1, x2, w2, font, GLYPHS, METRICS, (uint8_t)color);
    loc_out();
}
static void wrap_buf(const uint8_t *buf, int x, int y, int w, const uint8_t *font, int color)   /* 0x40352f */
{
    gfx_wrap_buffer(buf, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
}
static void text_line(const uint8_t *buf, int x, int y, const uint8_t *font, int color)   /* 0x402637 */
{
    gfx_text_render_line(buf, x, y, font, GLYPHS, (uint8_t)color);
}
static void text_line_c(const uint8_t *buf, int x, int y, int w, const uint8_t *font, int color)   /* 0x4025d7 */
{
    gfx_text_render_line_centered(buf, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
}
static void label(int v, int pad, const char *suffix, int x, int y, const uint8_t *font, int color)   /* 0x402f64 */
{
    gfx_text_label_render_dec(v, (char)pad, suffix, x, y, font, GLYPHS, (uint8_t)color);
}
static void label_c(int v, int pad, const char *suffix, int x, int y, int w, const uint8_t *font,
                    int color)   /* 0x4030c6 */
{
    gfx_text_label_render_dec_centered(v, (char)pad, suffix, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
}
static void label_ls(int v, int mode, const uint8_t *lab, const char *suffix, int x, int y, const uint8_t *font,
                     int cpos, int cneg)   /* 0x402e0c */
{
    gfx_text_label_render_labeled_signed(v, mode, lab, suffix, x, y, font, GLYPHS, (uint8_t)cpos, (uint8_t)cneg);
}
/* the caret: the port's editor + measurer cache over retail's (buf DAT_005cd550, cursor DAT_005bb4a8, insert
 * DAT_005c9280; cache DAT_005c9288 / DAT_0058fd88) */
static void caret(const uint8_t *font, int color)   /* 0x40acce */
{
    static text_input_state_t ed;
    int cur = g_gfx_first_pos_target;   /* DAT_005bb4a8: also the editor's cursor */
    ed.cursor = cur;
    ed.insert_mode = g_rl_5c9280;
    if (cur >= 0 && cur < TEXT_INPUT_BUF_SIZE) ed.buf[cur] = g_rl_5cd550[cur];
    font_metrics_state_t fm = { .char_glyph_index = METRICS, .last_glyph_index = g_gfx_last_glyph_index,
                                .last_glyph_offset = g_gfx_last_glyph_offset };
    gfx_caret_render(&ed, &fm, font, (uint8_t)color);
    if (g_gfx_last_glyph_index != fm.last_glyph_index) g_gfx_last_glyph_index = fm.last_glyph_index;
    if (g_gfx_last_glyph_offset != fm.last_glyph_offset) g_gfx_last_glyph_offset = fm.last_glyph_offset;
}
static void mark_box(int x, int y, int w, int h, int force)   /* 0x4527a6 */
{
    engine_dirty_rect_mark_box_tiled(DIRTY, x, y, w, h, force);
}
static void mark_full(void) { engine_territory_mark_full_screen_dirty(); }   /* 0x45240a */

/* ---------------------------------------------------------------- seams (callees outside the partition) */
/* ported, not on retail's layout / not linked in rhost — TODO(rl-link) */
static void minimap_render(int x, int y) { engine_retail_call(0x410aa9u, 2, (uint32_t)x, (uint32_t)y); }   /* TODO(rl-link): engine_minimap_render */
static void minimap_plot(int a, int x, int y, int b)   /* TODO(rl-link): engine_minimap_plot */
{ engine_retail_call(0x410cbdu, 4, (uint32_t)a, (uint32_t)x, (uint32_t)y, (uint32_t)b); }
static void workforce_restore(const uint8_t *blob, int w) { engine_retail_call(0x4b3ec0u, 2, (uint32_t)(uintptr_t)blob, (uint32_t)w); }   /* TODO(rl-link): engine_workforce_hud_restore_backdrop */
static void workforce_capture(const uint8_t *blob, int w) { engine_retail_call(0x4b3f0au, 2, (uint32_t)(uintptr_t)blob, (uint32_t)w); }   /* TODO(rl-link): engine_workforce_hud_capture_backdrop */
static void workforce_rows(void) { engine_retail_call(0x412666u, 0); }   /* TODO(rl-link): engine_workforce_draw_all_rows */
static int  bf_px_col(int px) { return engine_battlefield_render_pixel_to_col(px); }   /* 0x4bc2a5 */
static int  bf_px_row(int py) { return engine_battlefield_render_pixel_to_row(py); }   /* 0x4bc2c5 */
static int  bf_zoom(void) { return engine_battlefield_render_get_zoom(); }   /* 0x4bc505 */
static void text_recompute_length(void) { engine_retail_call(0x401f49u, 0); }   /* TODO(rl-link): text_input_recompute_length */
static void text_copy_out(uint8_t *dst, int n) { engine_retail_call(0x40210cu, 2, (uint32_t)(uintptr_t)dst, (uint32_t)n); }   /* TODO(rl-link): text_input_copy_out */
static int  state_hash(const void *p, int n, int salt)   /* TODO(rl-link): engine_state_hash 0x44015a */
{ return (int)engine_retail_call(0x44015au, 3, (uint32_t)(uintptr_t)p, (uint32_t)n, (uint32_t)salt); }
/* unported — TODO(rl-extern) */
static void mp_debug_tail(void) { engine_retail_call(0x42563cu, 0); }   /* TODO(rl-extern): 0x42563c */

/* ================================================================ the frame driver's HUD draws */

/* Port of FUN_00410f4b (358 B) — the strategic minimap refresh: on a game screen (state >= 3; not 0x23 / 0x24 /
 * 8..0xd / 0x17 / 0x1b..0x20 / 0x22; sub-state 0) redraw the minimap at (480, 25) when the combat-redraw flag
 * DAT_00567d0c asks (1) or the player's county DAT_0057c93c changed; flag 2 only re-syncs the county. */
int rl_410f4b(void)
{
    int redraw = 0;
    if (g_engine_state < 3) return 0;
    unsigned s = SCREEN;
    if (s == 0x23 || s == 0x24 || s_slot_0057c8fc != 0 || (s >= 8 && s <= 0xd) || s == 0x17 ||
        (s >= 0x1b && s <= 0x20) || s == 0x22)
        return 0;
    if (g_combat_flag_567d0c == 2)
        g_rl_4e599c = (int32_t)s_slot_0057c93c;
    else if (g_combat_flag_567d0c != 0)
        redraw = 1;
    if ((int32_t)s_slot_0057c93c != g_rl_4e599c)
        redraw = 1;
    g_rl_4e599c = (int32_t)s_slot_0057c93c;
    g_combat_flag_567d0c = 0;
    if (redraw) {
        minimap_render(480, 25);
        mark_box(464, 24, 11, 10, 1);
    }
    return 0;
}

/* Port of FUN_0041a639 (251 B) — the turn counter's icon: on a game screen (sub-state 0, state >= 3) with a turn
 * count DAT_005440c8 > 0, outside the end-game countdown (DAT_0055403c <= 0, or the player's realm +0 == 999), with
 * DAT_0053f26c > 0 and the screen's HUD-off flag byte_4D2E80[screen] clear: icon 96 at (404, 430) + the count. */
int rl_41a639(void)
{
    if (s_slot_0057c8fc != 0 || g_engine_state < 3 || (int32_t)s_slot_005440c8 <= 0) return 0;
    if (g_bf_55403c > 0 && PL32(s_slot_0057c8cc, 0x57bf00u) != 999) return 0;
    if ((int32_t)s_slot_0053f26c <= 0) return 0;
    if (AT(uint8_t, g_rl_4d2dd0, 0x4d2dd0u, 0x4d2e80u + SCREEN) != 0) return 0;
    gfx_sprite_render_from_blob(MISC, 96, 404, 430);
    label_c(s_slot_005440c8, 32, STR(g_rl_4d41d0, 0x4d41d0u, 0x4d41d0u), 424, 442, 50, FONT, 63);
    mark_box(404, 430, 5, 3, 1);
    return 0;
}

/* Port of FUN_0041a844 (188 B) — the MP lag light: in MP on the strategic map (state 3), every 12 frames a 8x3
 * light at (2, 476) — 250 while the game time DAT_0057c998 advanced since the last look, else 249 — in a frame. */
int rl_41a844(void)
{
    if (g_sim_mp_active == 0 || g_engine_state != 3) return 0;
    if (g_rl_4e59a0 > 0) g_rl_4e59a0--;
    if (g_rl_4e59a0 != 0) return 0;
    int c = g_bf_57c998[0] > g_rl_4e59c0 ? 250 : 249;
    g_rl_4e59a0 = 12;
    g_rl_4e59c0 = g_bf_57c998[0];
    gfx_line_fill_rect(2, 476, 8, 3, (uint8_t)c);
    gfx_line_rect(1, 475, 10, 5, 63);
    return 0;
}

/* Port of FUN_0042476b (138 B) — the MP "message sent" flag: 1.6 s after DAT_00553c6c was stamped it clears and
 * gates a panel redraw (DAT_0053e9ac = 1); while set, on the strategic map, chrome sprite 261 at (x, y). */
int rl_42476b(int x, int y)
{
    if (g_sim_mp_active == 0 || g_bf_553c6c == 0) return 0;
    if ((int)(rl_plat_timeGetTime() - (DWORD)g_bf_553c6c) > 1600) {
        g_bf_553c6c = 0;
        DIRTY->below_status_redraw = 1;   /* DAT_0053e9ac */
    }
    if (s_slot_0057c8fc == 0)
        gfx_sprite_render_from_blob(CHROME, 261, x, y);
    return 0;
}

/* Port of FUN_004248a3 (239 B) — the debug counters panel (DAT_0053f0e0): a 10x8 chrome panel at (x, y) with four
 * numbers DAT_00544044 / 00544040 / 00544038 / 00544028. */
int rl_4248a3(int x, int y)
{
    if (g_rl_53f0e0 == 0) return 0;
    gfx_panel_fill_interior_chrome(CHROME, x, y, 10, 8);
    label(g_rl_544044, 32, STR(g_rl_4d4554, 0x4d4554u, 0x4d4554u), x + 10, y + 10, FONT, 63);
    label(g_rl_544040, 32, STR(g_rl_4d4554, 0x4d4554u, 0x4d4558u), x + 10, y + 30, FONT, 63);
    label(g_rl_544038, 32, STR(g_rl_4d4554, 0x4d4554u, 0x4d455cu), x + 10, y + 50, FONT, 63);
    label(g_rl_544028, 32, STR(g_rl_4d4554, 0x4d4554u, 0x4d4560u), x + 10, y + 70, FONT, 63);
    mark_box(x, y, 10, 8, 1);
    return 0;
}

/* Port of FUN_00423530 (521 B) — the battle HUD's score box: on the battlefield (sub-state 2, screens < 0x2b but
 * 0x22) when a redraw is pending (DAT_00553ed8, counted down) or either side's count (DAT_0053f028 / DAT_00553c58)
 * changed: the box sprites, the night / pause marks, both sides' banners and counts. */
int rl_423530(void)
{
    if (g_engine_state < 3 || SCREEN == 0x22 || SCREEN >= 0x2b || s_slot_0057c8fc != 2) return 0;
    int redraw = g_bf_553ed8 != 0;
    if ((int32_t)s_slot_0053f028 != g_rl_4e59b0) redraw = 1;
    if (g_rl_4e59b4 != (int32_t)s_slot_00553c58) redraw = 1;
    if (!redraw) return 0;
    if (g_bf_553ed8 != 0) g_bf_553ed8--;
    g_rl_4e59b0 = (int32_t)s_slot_0053f028;
    g_rl_4e59b4 = (int32_t)s_slot_00553c58;
    gfx_sprite_render_opaque_from_blob(MISC, 1, 480, 448);
    if (g_bf_53f238 != 0)
        gfx_sprite_render_opaque_from_blob(MISC, 5, 481, 449);
    if (s_slot_00568964 == 1)
        gfx_sprite_render_opaque_from_blob(MISC, 6, 513, 449);
    gfx_sprite_render_opaque_from_blob(MISC, 2, 480, 412);
    gfx_sprite_render_opaque_from_blob(MISC, s_slot_00568934 + 6, 482, 413);
    gfx_sprite_render_opaque_from_blob(MISC, s_slot_00568938 + 6, 560, 413);
    label_c(s_slot_0053f028, 32, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4448u), 506, 422, 56, FONT, 32);
    label_c(s_slot_00553c58, 32, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d444cu), 586, 422, 56, FONT, 32);
    mark_box(480, 344, 10, 15, 1);
    return 0;
}

/* Port of FUN_00423739 (383 B) — the battle roster panel: on the battle screens 0x28..0x2a (sub-state 2, a battle
 * DAT_005533d0) every 51st frame, on a pending redraw DAT_00568204 (counts only) or when the selection count
 * DAT_00553078 changed: page DAT_00553220 = 0 / 12 / 30 by the count (capped at 80), then the backdrop + icons
 * (0x4238b8) and the troop counts (0x4239d5). */
int rl_423739(void)
{
    if (SCREEN >= 0x2b || SCREEN < 0x28 || s_slot_0057c8fc != 2 || s_slot_005533d0 == 0) return 0;
    int v = ++g_rl_4e59a8 > 50;
    if (g_rl_568204 != 0) v = 2;
    if (g_rl_4e59ac != g_bf_553078) v = 1;
    if (v == 0) return 0;
    g_rl_4e59ac = g_bf_553078;
    g_rl_4e59a8 = 0;
    if (g_bf_553078 <= 12)
        g_bf_553220 = 0;
    else if (g_bf_553078 <= 18)
        g_bf_553220 = 12;
    else
        g_bf_553220 = 30;
    if (g_bf_553078 > 80) g_bf_553078 = 80;
    if (v == 1) {
        gfx_sprite_render_opaque_from_blob(MISC, 0, 480, 184);
        bf_4238b8();
        bf_4239d5();
    } else if (v == 2) {
        bf_4239d5();
    }
    mark_box(480, 184, 10, 21, 1);
    return 0;
}

/* Port of FUN_0044098f (595 B) — the MP checksum overlay: while DAT_00553c6c is set on the strategic map with
 * DAT_0056d690: per county (1..16), realm (1..5) and territory (1..24) the low byte of its record's checksum
 * (0x44015a) and its index, then the MP tail 0x42563c. */
int rl_44098f(void)
{
    if (g_bf_553c6c <= 0 || s_slot_0057c8fc != 0 || g_rl_56d690[0] == 0) return 0;
    g_gfx_no_shadow = 1;
    for (int i = 1; i < 17; i++) {
        int v = (uint8_t)state_hash(&CO8(i, 0x53f9b0u), 768, 5);
        g_gfx_line_x_acc = 0;
        label(v, 32, STR(g_rl_4d5f68, 0x4d5f68u, 0x4d5f68u), 4, 16 * i + 30, FONT, 32);
        label(i, 32, STR(g_rl_4d5f68, 0x4d5f68u, 0x4d5f70u), g_gfx_line_x_acc + 8, 16 * i + 30, FONT, 32);
    }
    for (int j = 1; j < 6; j++) {
        int v = (uint8_t)state_hash(&PL32(j, 0x57bf00u), 352, 6);
        g_gfx_line_x_acc = 0;
        label(v, 32, STR(g_rl_4d5f68, 0x4d5f68u, 0x4d5f74u), 124, 16 * j + 30, FONT, 32);
        label(j, 32, STR(g_rl_4d5f68, 0x4d5f68u, 0x4d5f7cu), g_gfx_line_x_acc + 128, 16 * j + 30, FONT, 32);
    }
    for (int k = 1; k < 25; k++) {
        int v = (uint8_t)state_hash(&TE8(k, 0x52f0b0u), 420, 0);
        g_gfx_line_x_acc = 0;
        label(v, 32, STR(g_rl_4d5f68, 0x4d5f68u, 0x4d5f80u), 244, 16 * k + 30, FONT, 32);
        label(k, 32, STR(g_rl_4d5f68, 0x4d5f68u, 0x4d5f88u), g_gfx_line_x_acc + 248, 16 * k + 30, FONT, 32);
    }
    g_gfx_no_shadow = 0;
    mp_debug_tail();
    return 0;
}

/* Port of FUN_00420316 (274 B) — the MP chat line: while the countdown DAT_0057a0cc runs, the editor's text
 * (copied to DAT_00553e80) under the last received line DAT_00565440, each centred in its realm's colour over a
 * 30x32 black block at (0, 24); at 0 it asks a full redraw (DAT_0057d340 = 1, DAT_0053e9ac = 1). */
int rl_420316(void)
{
    if (g_rl_57a0cc <= 0) return 0;
    if (--g_rl_57a0cc <= 0) {
        engine_set_demand_redraw(1);
        DIRTY->below_status_redraw = 1;   /* DAT_0053e9ac */
        return 0;
    }
    g_rl_5aeb78 = 1;
    text_recompute_length();
    text_copy_out(g_rl_553e80, 64);
    g_gfx_glyph_w = 30;
    g_gfx_glyph_height_short = 32;
    gfx_sprite_fill_block(0, 24, 0);
    g_gfx_no_shadow = 1;
    text_line_c(g_rl_565440, 0, 25, 480, FONT, PL8(g_rl_55325c, 0x57bf09u));
    text_line_c(g_rl_553e80, 0, 41, 480, FONT, PL8(s_slot_0057c8cc, 0x57bf09u));
    g_gfx_no_shadow = 0;
    return 0;
}

/* Port of FUN_0040c725 (428 B) — the open drop-down menu (screen 0x32, menu DAT_00522cb4): its record
 * DAT_00522cac (+0 x, +4 y, +6 localization group, +8 the item table, +0xc the item count; words, signed) — a
 * 12-wide chrome box (0x409429), each item's string, the hovered one (DAT_00522cb0, 1-based) on a 11x16 block in
 * colour 24 without shadow. DAT_00522ca8 walks the 12-byte item records. */
int rl_40c725(void)
{
    if (SCREEN != 0x32 || g_rl_522cb4 == 0) return 0;
    const uint8_t *m = PTR(g_rl_522cac);
    g_rl_522ca8 = *(const int32_t *)(m + 8);
    int x = *(const int16_t *)m, y = *(const int16_t *)(m + 4), grp = *(const int16_t *)(m + 6);
    int n = *(const int16_t *)(m + 12);
    g_gfx_glyph_w = 12;
    g_gfx_glyph_height_short = (int16_t)(21 * n / 16 + 2);
    rl_409429(x, y + 18, g_gfx_glyph_w, g_gfx_glyph_height_short);
    for (int i = 1; n >= i; i++) {
        const uint8_t *it = PTR(g_rl_522ca8);
        int str = *(const int16_t *)(it + 2);
        int iy = y + *(const int16_t *)it + 32;
        if (i == g_rl_522cb0) {
            g_gfx_no_shadow = 1;
            g_gfx_glyph_w = 11;
            g_gfx_glyph_height_short = 16;
            gfx_sprite_fill_block(x + 8, iy - 2, 63);
            wrap_line(grp, str, x + 16, iy, FONT, 24);
            g_gfx_no_shadow = 0;
        } else {
            wrap_line(grp, str, x + 16, iy, FONT, 63);
        }
        g_rl_522ca8 += 12;
    }
    mark_box(x, y + 18, 13, 12, 2);
    return 0;
}

/* Port of FUN_004120e0 (99 B) — the workforce panel: restore its 120x320 backdrop at (0, DAT_0057c908) from the
 * panel sheet (0x4b3ec0, pitch 160), draw the eight rows (0x412666) and the seal at (384, +280). */
int rl_4120e0(void)
{
    g_gfx_cursor_x = 0;
    g_gfx_cursor_y = g_rl_57c908;
    g_gfx_glyph_w = 120;
    g_gfx_glyph_height_short = 320;
    workforce_restore(g_gfx_current_blob, 160);
    workforce_rows();
    gfx_sprite_seal_draw(SYSTEM, 384, g_rl_57c908 + 280, 1);
    return 0;
}

/* Port of FUN_00412795 (501 B) — the drag box on the county map (screens 5 / 6): while DAT_00565408 counts down,
 * redraw the workforce panel (0x4120e0); on screen 5 the rubber band from the press point (DAT_005530fc,
 * DAT_005530f4) to the cursor, clipped to x < 512 and DAT_0057c908 .. +376, in colour 32. */
int rl_412795(void)
{
    if (g_rl_565408 == 0) return 0;
    g_rl_565408--;
    if (SCREEN != 5 && SCREEN != 6 && SCREEN != 2) return 0;
    if (SCREEN == 2) return 0;
    rl_4120e0();
    if (SCREEN == 6) return 0;
    int x, w, y, h;
    if (g_bf_4e65fc > g_rl_5530fc) { x = g_rl_5530fc; w = g_bf_4e65fc + 1 - g_rl_5530fc; }
    else                           { x = g_bf_4e65fc; w = g_rl_5530fc + 1 - g_bf_4e65fc; }
    if (g_bf_4e65f4 > g_rl_5530f4) { y = g_rl_5530f4; h = g_bf_4e65f4 + 1 - g_rl_5530f4; }
    else                           { y = g_bf_4e65f4; h = g_rl_5530f4 + 1 - g_bf_4e65f4; }
    if (x >= 0) {
        if (x + w >= 512) w = 512 - x;
    } else {
        w += x;
        x = 0;
    }
    if (y >= g_rl_57c908) {
        if (y + h >= g_rl_57c908 + 376) h = g_rl_57c908 + 376 - y;
    } else {
        h -= g_rl_57c908 - y;
        y = g_rl_57c908;
    }
    gfx_line_rect(x, y, w, h, 32);
    mark_box(x, y, w / 16 + 2, h / 16 + 2, 2);
    return 0;
}

/* Port of FUN_0041298a (425 B) — the battlefield drag box (screen 0x2a): the rubber band from the press point
 * (DAT_0057a0f4, DAT_0057a0e8) to the cursor, clipped to x < 480 (no left clip) and 24 <= y < 472, in colour 32;
 * then the box in cells (0x4bc2a5 / 0x4bc2c5 + the camera DAT_0055ce70 / DAT_005678a0, the zoom 0x4bc505) to the
 * selection preview 0x46c4a3. */
int rl_41298a(void)
{
    if (SCREEN != 0x2a) return 0;
    int px = g_bf_57a0f4, py = g_bf_57a0e8, x, w, y, h;
    if (px >= g_bf_4e65fc) { x = g_bf_4e65fc; w = px + 1 - g_bf_4e65fc; }
    else                   { x = px; w = g_bf_4e65fc + 1 - px; }
    if (py >= g_bf_4e65f4) { y = g_bf_4e65f4; h = py + 1 - g_bf_4e65f4; }
    else                   { y = py; h = g_bf_4e65f4 + 1 - py; }
    if (x + w >= 480) w = 480 - x;
    if (y < 24) {
        h -= 24 - y;
        y = 24;
    }
    if (y + h >= 472) h = 472 - y;
    gfx_line_rect(x, y, w, h, 32);
    mark_box(x, y, w / 16 + 2, h / 16 + 2, 2);
    int col = bf_px_col(x) + s_slot_0055ce70;
    int row = bf_px_row(y) + s_slot_005678a0;
    int zoom = bf_zoom();
    return rl_46c4a3(col, row, w / zoom + 2, h / zoom + 2);
}

/* Port of FUN_00410c71 (76 B) — the minimap with its 128x128 frame: the raw buffer DAT_00569590 at (x - 2, y + 3),
 * then the county plot 0x410cbd(a, x - 2, y + 3, 0). */
int rl_410c71(int a, int x, int y)
{
    gfx_sprite_render_raw(g_rl_569590, x - 2, y + 3, 128, 128);
    minimap_plot(a, x - 2, y + 3, 0);
    return 0;
}

/* ================================================================ the screen dispatcher's arms (0x40f1a0) */

/* seams of the screen arms */
static void panel_box(int x, int y, int cols, int rows)   /* TODO(rl-link): 0x409397 = gfx_panel_draw_border_chrome(mode 0) + gfx_panel_fill_interior_chrome(x + 16, y + 16, cols - 2, rows - 2); a static in engine_armory / county_panels / labor / terrain_info */
{ engine_retail_call(0x409397u, 4, (uint32_t)x, (uint32_t)y, (uint32_t)cols, (uint32_t)rows); }
static void chdir_base(void) { engine_retail_call(0x4af502u, 0); }   /* TODO(rl-link): io_path_chdir_base */
static void read_file_at(const void *name, const void *dst, uint32_t n, uint32_t off)   /* TODO(rl-link): io_read_file_at 0x4af8d9 */
{ engine_retail_call(0x4af8d9u, 4, (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)dst, n, off); }
static void unit_noun(int n, int str, int x, int y, const uint8_t *font, int color)   /* TODO(rl-link): armory_unit_noun 0x41ac3e */
{ engine_retail_call(0x41ac3eu, 6, (uint32_t)n, (uint32_t)str, (uint32_t)x, (uint32_t)y, (uint32_t)(uintptr_t)font, (uint32_t)color); }
static int  num_units(int n, int str, int x, int y, const uint8_t *font, int color)   /* TODO(rl-link): labor_num_units 0x41ab67 */
{ return (int)engine_retail_call(0x41ab67u, 6, (uint32_t)n, (uint32_t)str, (uint32_t)x, (uint32_t)y, (uint32_t)(uintptr_t)font, (uint32_t)color); }
static void load_background(const void *name, int h)   /* TODO(rl-link): gfx_load_background_image 0x408fcb */
{ engine_retail_call(0x408fcbu, 2, (uint32_t)(uintptr_t)name, (uint32_t)h); }
static void load_panel_sheet(int n) { engine_retail_call(0x4b25b0u, 1, (uint32_t)n); }   /* TODO(rl-link): gfx_load_panel_sheet */
static int  set_palette(const void *pal) { return (int)engine_retail_call(0x4b0ab5u, 1, (uint32_t)(uintptr_t)pal); }   /* TODO(rl-link): gfx.c's palette set 0x4b0ab5 */
static void clear_back_buffer(void) { engine_retail_call(0x4b1867u, 0); }   /* TODO(rl-link): engine_screen_strat.c's 0x4b1867 */
static void load_season(void) { engine_retail_call(0x4984dcu, 0); }   /* TODO(rl-link): engine_pl8.c's season loader 0x4984dc */
static int  strat_render(int full) { return (int)engine_retail_call(0x40f5fdu, 1, (uint32_t)full); }   /* TODO(rl-link): engine_screen_strat_render */
static void filelist_content(int a, int b) { engine_retail_call(0x4149ecu, 2, (uint32_t)a, (uint32_t)b); }   /* TODO(rl-link): engine_title_render_filelist_content */
static void bf_minimap_scroll(int a) { engine_retail_call(0x4bc1d1u, 1, (uint32_t)a); }   /* TODO(rl-link): engine_battlefield_render_minimap_scroll */
static void menubar_chrome(void) { engine_retail_call(0x419c78u, 0); }   /* TODO(rl-link): engine_hud_menubar_render_chrome (the linked port takes its state as arguments) */
static void present(int full) { engine_retail_call(0x452160u, 1, (uint32_t)full); }   /* TODO(rl-link): gfx.c's present 0x452160 */
/* 0x4b1de0 is an empty function (11 B: no state, EAX passes through): its calls are elided */

/* Port of FUN_00414819 (203 B) — the save / load file box (screens 0x2f / 0x30): a 28x20 chrome panel at (16, 144),
 * the title string a1 of group 40 in the tall font, four frames, the file list from the top (DAT_004e65dc = 999). */
int rl_414819(int a1)
{
    panel_box(16, 144, 28, 20);
    mark_full();
    wrap_line(40, a1, 32, 160, FONT_TALL, 63);
    gfx_line_rect(32, 200, 400, 256, 63);
    gfx_line_rect(40, 208, 192, 32, 63);
    gfx_line_rect(40, 248, 352, 164, 63);
    gfx_line_rect(40, 420, 384, 28, 63);
    g_rl_4e65dc = 999;
    filelist_content(g_rl_4e65dc, 0);
    g_gfx_menu_redraw_a = 1;
    return 0;
}

/* on / off: string 0 or 1 of group `grp` at (x, y) */
static void on_off(int grp, int on, int x, int y) { wrap_line(grp, on ? 0 : 1, x, y, FONT, 63); }

/* Port of FUN_00414f68 (500 B) — the sound options panel: group 50's title + four rows, each row's on / off
 * (group 18) from DAT_0053f25c / 0053f260 / 0053f264 and the INVERTED DAT_0053f284 (the quick-anim suppressor);
 * DAT_0056d8a0 = 4 (rows), the seal. */
int rl_414f68(void)
{
    gfx_panel_draw_bevel(CHROME, 48, 96, 24, 13);
    wrap_line(50, 0, 64, 116, FONT_TALL, 63);
    wrap_line(50, 1, 96, 160, FONT, 63);
    wrap_line(50, 2, 96, 192, FONT, 63);
    wrap_line(50, 3, 96, 224, FONT, 63);
    wrap_line(50, 4, 96, 256, FONT, 63);
    on_off(18, s_slot_0053f25c != 0, 320, 160);
    on_off(18, s_slot_0053f260 != 0, 320, 192);
    on_off(18, s_slot_0053f264 != 0, 320, 224);
    on_off(18, g_combat_flag_53f284 == 0, 320, 256);
    g_rl_56d8a0 = 4;
    gfx_sprite_seal_draw(SYSTEM, 392, 256, 0);
    mark_full();
    g_gfx_menu_redraw_a = 1;
    return 0;
}

/* Port of FUN_0041515c (398 B) — the game options panel: group 51's title + three rows with their on / off
 * (group 19) from DAT_0053f218 / 0053f214 / 0053f20c; DAT_00553f00 = 3 (rows), the seal. */
int rl_41515c(void)
{
    gfx_panel_draw_bevel(CHROME, 48, 96, 24, 12);
    wrap_line(51, 0, 64, 116, FONT_TALL, 63);
    wrap_line(51, 1, 96, 160, FONT, 63);
    wrap_line(51, 2, 96, 192, FONT, 63);
    wrap_line(51, 3, 96, 224, FONT, 63);
    on_off(19, g_rl_53f218 != 0, 320, 160);
    on_off(19, g_bf_53f214 != 0, 320, 192);
    on_off(19, g_rl_53f20c != 0, 320, 224);
    g_rl_553f00 = 3;
    gfx_sprite_seal_draw(SYSTEM, 392, 240, 0);
    mark_full();
    g_gfx_menu_redraw_a = 1;
    return 0;
}

/* Port of FUN_004152ea (341 B) — the battle options panel: group 52; row 2's on / off (group 18) from
 * DAT_0053f228 == 1, row 3 only when it is 0; the result-movies row's (group 19) from DAT_0053f248; DAT_005533ac = 2. */
int rl_4152ea(void)
{
    gfx_panel_draw_bevel(CHROME, 48, 144, 24, 10);
    wrap_line(52, 2, 96, 240, FONT, 63);
    on_off(18, g_rl_53f228 == 1, 320, 240);
    if (g_rl_53f228 == 0)
        wrap_line(52, 3, 72, 264, FONT, 63);
    g_rl_5533ac = 2;
    gfx_sprite_seal_draw(SYSTEM, 392, 256, 0);
    wrap_line(52, 0, 64, 164, FONT_TALL, 63);
    wrap_line(52, 1, 96, 208, FONT, 63);
    on_off(19, g_combat_flag_53f248 != 0, 320, 208);
    mark_full();
    g_gfx_menu_redraw_a = 1;
    return 0;
}

/* the per-player troop table DAT_0053f690 + 128 p: entry i (0..7) at +0x10 + 16 i, two dwords (+0x10 / +0x14) */
#define TROOPS(p, va) AT(int32_t, g_rl_53f690, 0x53f690u, (va) + 128 * (p))

/* Port of FUN_004192b1 (163 B) — the army overview screen (0x1a): icon_tmp.pl8 into the panel sheet, a 28x26 chrome
 * panel at (8, 48), the seal, icon 43, group 17's title (tall) and footer, the troop table (0x419354). */
int rl_4192b1(void)
{
    chdir_base();
    mark_full();
    read_file_at(STR(g_rl_4d40d8, 0x4d40d8u, 0x4d40d8u), g_gfx_current_blob, 0x27100u, 0);
    panel_box(8, 48, 28, 26);
    gfx_sprite_seal_draw(SYSTEM, 428, 436, 0);
    gfx_sprite_render(43, 24, 64);
    wrap_line(17, 0, 104, 68, FONT_TALL, 63);
    wrap_line(17, 1, 120, 430, FONT, 63);
    rl_419354();
    g_gfx_menu_redraw_a = 1;
    return 0;
}

/* Port of FUN_00419354 (1077 B) — the troop table: per class i (0..6) its noun (group 8, plural), the class icon
 * twice and the player's two counts (DAT_0053f6a0 / 0053f6a4 + 128 p + 16 i); when the player holds a siege
 * (entry 7 non-zero) that row with the besieged territory's (DAT_00553074) engine kind (+0x197) and noun
 * (+0x195; the noun's count is entry 7's FIRST dword, 0 on the second-dword path: plural) — then the totals row
 * (DAT_00554468 / DAT_00554040) one line lower. */
int rl_419354(void)
{
    int p = (int)s_slot_0057c8cc;
    int siege = TROOPS(p, 0x53f710u) != 0 || TROOPS(p, 0x53f714u) != 0;
    gfx_panel_fill_interior_chrome(CHROME, 24, 128, 26, 18);
    mark_box(24, 128, 27, 19, 1);
    int i;
    for (i = 0; i < 7; i++) {
        unit_noun(2, 2 * i + 52, 24, 32 * i + 128, FONT, 63);
        gfx_sprite_render_from_blob(MISC, i + 47, 168, 32 * i + 120);
        label(TROOPS(p, 0x53f6a0u + 16 * i), 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d40e8u), 216, 32 * i + 128, FONT, 63);
        gfx_sprite_render_from_blob(MISC, i + 47, 344, 32 * i + 120);
        label(TROOPS(p, 0x53f6a4u + 16 * i), 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d40ecu), 392, 32 * i + 128, FONT, 63);
    }
    if (siege) {
        int t = g_rl_553074;
        if (TROOPS(p, 0x53f6a0u + 16 * i) != 0) {
            label(TROOPS(p, 0x53f6a0u + 16 * i), 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d40f0u), 216, 32 * i + 128, FONT, 63);
            wrap_line(16, TE8(t, 0x52f247u), 24, 32 * i + 128, FONT, 63);
            unit_noun(TROOPS(p, 0x53f6a0u + 16 * i), 2 * TE8(t, 0x52f245u) + 52, 88, 32 * i + 144, FONT, 63);
        } else if (TROOPS(p, 0x53f6a4u + 16 * i) != 0) {
            label(TROOPS(p, 0x53f6a4u + 16 * i), 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d40f4u), 392, 32 * i + 128, FONT, 63);
            wrap_line(16, TE8(t, 0x52f247u), 24, 32 * i + 128, FONT, 63);
            unit_noun(TROOPS(p, 0x53f6a0u + 16 * i), 2 * TE8(t, 0x52f245u) + 52, 88, 32 * i + 144, FONT, 63);
        }
        unit_noun(1, 72, 24, 388, FONT, 63);
        label(g_rl_554468, 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d40f8u), 216, 388, FONT, 63);
        label(g_rl_554040, 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d40fcu), 392, 388, FONT, 63);
    } else {
        unit_noun(1, 72, 24, 352, FONT, 63);
        label(g_rl_554468, 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4100u), 216, 352, FONT, 63);
        label(g_rl_554040, 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4104u), 392, 352, FONT, 63);
    }
    return 0;
}

/* Port of FUN_0041e1dd (1011 B) — the gateway screen (0x1f; the realm-select / victory scroll): unless the
 * transition flag DAT_0055302c == 2 (then only the movie-palette reset 0x4b11ce), the setup music (setup2.wav
 * from turn DAT_0053f258 >= 8, else setup.wav looping), gateway.256 / .pl8, the panel sheet (grown to 220000 B),
 * panels2.pl8, a 26 x (14 | 18) panel at (112, 8) and the centred scroll text of group 36 — the opening (with
 * the realm name, group 101, DAT_00553e78; the variant of DAT_0053f0c4 == 10 names the player DAT_0053f034) or
 * the closing lines — then the palette. */
int rl_41e1dd(void)
{
    if (s_slot_0055302c == 2)
        return rl_4b11ce();
    chdir_base();
    g_combat_audio_latch = 0;
    if ((int32_t)s_slot_0053f258 < 8)
        rl_4263ad((int)(uintptr_t)STR(g_rl_4d4298, 0x4d4298u, 0x4d42a4u), 0, 1);
    else
        rl_4263ad((int)(uintptr_t)STR(g_rl_4d4298, 0x4d4298u, 0x4d4298u), 0, 0);
    rl_4b11ce();
    read_file_at(STR(g_rl_4d4298, 0x4d4298u, 0x4d42b0u), g_rl_4ea8a0, 0x300u, 0);
    load_background(STR(g_rl_4d4298, 0x4d4298u, 0x4d42bcu), 480);
    if (AT(int32_t, g_rl_4eabcc, 0x4eabccu, 0x4eabccu) < 220000)
        load_panel_sheet(220000);
    read_file_at(STR(g_rl_4d4298, 0x4d4298u, 0x4d42c8u), g_gfx_current_blob, 0x27100u, 0);
    if ((int32_t)s_slot_0053f258 < 8)
        gfx_panel_draw(g_gfx_current_blob, 112, 8, 26, 14);
    else
        gfx_panel_draw(g_gfx_current_blob, 112, 8, 26, 18);
    g_gfx_uppercase_highlight = 1;
    if ((int32_t)s_slot_0053f258 < 8) {
        if (g_rl_53f0c4 == 10) {
            wrap_line_c(36, 0, 112, 32, 416, FONT_TALL, 63);
            wrap_line_c(36, 1, 112, 64, 416, FONT, 63);
            wrap_line_c(101, g_rl_553e78, 112, 88, 416, FONT_TALL, 63);
            wrap_line_c(36, 2, 112, 128, 416, FONT, 63);
            wrap_line_c(36, 3, 112, 152, 416, FONT, 63);
            wrap_line_c(101, (int)s_slot_0053f034, 112, 176, 416, FONT_TALL, 63);
        } else {
            wrap_line_c(36, 4, 112, 32, 416, FONT_TALL, 63);
            wrap_line_c(36, 5, 112, 64, 416, FONT, 63);
            wrap_line_c(101, g_rl_553e78, 112, 88, 416, FONT_TALL, 63);
            wrap_line_c(36, 6, 112, 128, 416, FONT, 63);
            wrap_line_c(36, 7, 112, 152, 416, FONT, 63);
            wrap_line_c(36, 8, 112, 176, 416, FONT, 63);
        }
    } else {
        wrap_line_c(36, 0, 112, 32, 416, FONT_TALL, 63);
        wrap_line_c(36, 1, 112, 64, 416, FONT, 63);
        wrap_line_c(101, g_rl_553e78, 112, 88, 416, FONT_TALL, 63);
        for (int k = 0; k < 7; k++)
            wrap_line_c(36, 9 + k, 112, 128 + 20 * k, 416, FONT, 63);
    }
    g_gfx_uppercase_highlight = 0;
    set_palette(g_rl_4ea8a0);
    mark_full();
    return 0;
}

/* a strength bar: a 52x8 bevel at (80, y - 1), its 50x6 well in 249, the value / 2 in 250, the value + "%" */
static void siege_bar(int y, int v, const char *pct)
{
    gfx_line_rect_bevel_dark_br(80, y - 1, 52, 8);
    gfx_line_fill_rect(81, y, 50, 6, 249);
    gfx_line_fill_rect(81, y, v / 2, 6, 250);
    label(v, 64, pct, 144, y, FONT, 63);
}

/* Port of FUN_00421f14 (1491 B) — the siege screen (0x11): the besieger's (DAT_00568ed0) territory record — its
 * castle plan (sgeplans.pl8, the county's castle kind +0x1c0 - 1), the title, the defenders (+0x19c), three
 * strength bars (+0x184 / +0x18a / +0x190, words) each with its engine icons (+0x182 / +0x188 / +0x18e; the third
 * row 80 px apart) or "none", and the two buttons. */
int rl_421f14(void)
{
    rl_47703a();
    mark_full();
    gfx_panel_draw_bevel(CHROME, 16, 48, 28, 25);
    int c = TE8(g_siege_besieger, 0x52f249u);
    chdir_base();
    g_gfx_uppercase_highlight = 1;
    read_file_at(STR(g_rl_4d43f4, 0x4d43f4u, 0x4d43f4u), g_gfx_current_blob, 0x186a0u, 0);
    if (CO8(c, 0x53fb70u) != 0)
        gfx_sprite_render(CO8(c, 0x53fb70u) - 1, 336, 64);
    wrap_line(83, 0, 48, 88, FONT_TALL, 63);
    g_gfx_line_x_acc = 32;
    wrap_line(83, 4, 64, 120, FONT, 63);
    g_gfx_line_x_acc = 48;
    num_units(TE8(g_siege_besieger, 0x52f24cu), 66, 80, 136, FONT, 63);
    wrap_line(83, 5, g_gfx_line_x_acc + 32, 136, FONT, 63);
    wrap_line(83, 1, 72, 192, FONT, 63);
    siege_bar(217, TE16(g_siege_besieger, 0x52f234u), STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4404u));
    if (TE16(g_siege_besieger, 0x52f232u) != 0) {
        for (int i = 0; TE16(g_siege_besieger, 0x52f232u) > i; i++)
            gfx_sprite_render_from_blob(MISC, 67, 60 * i + 208, 184);
    } else {
        wrap_line(83, 8, 240, 208, FONT, 63);
    }
    wrap_line(83, 2, 72, 260, FONT, 63);
    siege_bar(285, TE16(g_siege_besieger, 0x52f23au), STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4408u));
    if (TE16(g_siege_besieger, 0x52f238u) != 0) {
        for (int j = 0; TE16(g_siege_besieger, 0x52f238u) > j; j++)
            gfx_sprite_render_from_blob(MISC, 68, 60 * j + 208, 252);
    } else {
        wrap_line(83, 8, 240, 276, FONT, 63);
    }
    wrap_line(83, 3, 72, 328, FONT, 63);
    siege_bar(353, TE16(g_siege_besieger, 0x52f240u), STR(g_rl_4d43f4, 0x4d43f4u, 0x4d440cu));
    if (TE16(g_siege_besieger, 0x52f23eu) != 0) {
        for (int k = 0; TE16(g_siege_besieger, 0x52f23eu) > k; k++)
            gfx_sprite_render_from_blob(MISC, 69, 80 * k + 208, 320);
    } else {
        wrap_line(83, 8, 240, 344, FONT, 63);
    }
    gfx_line_rect_bevel_dark_tl(104, 396, 100, 28);
    gfx_line_fill_rect(105, 397, 98, 26, 24);
    wrap_line(83, 6, 112, 404, FONT, 63);
    gfx_line_rect_bevel_dark_tl(264, 396, 100, 28);
    gfx_line_fill_rect(265, 397, 98, 26, 24);
    wrap_line(83, 7, 272, 404, FONT, 63);
    g_gfx_uppercase_highlight = 0;
    return 0;
}

/* a territory's class count i: word +0x16c + 2 i, plus the mercenaries (+0x196) when i is its class (+0x195) */
static int class_count(int t, int i)
{
    int v = AT(int16_t, g_per_territory_420, 0x52f0b0u, 0x52f21cu + 420 * t + 2 * i);
    if (TE8(t, 0x52f245u) == i)
        v += TE8(t, 0x52f246u);
    return v;
}

/* Port of FUN_004224e7 (1835 B) — the two armies' class table (the battle result / pre-battle screens, terrain
 * info): mode 0 — per class each side's count (kept in DAT_00568420 / DAT_0056843c), icon and noun; mode 1 — the
 * same beside the kept counts (before / after); mode 2 — one army's classes in two columns. Then the totals
 * (+0x168) as "N men". `font` is the caller's. */
int rl_4224e7(int a, int b, int x, int y, int font_p, int mode)
{
    const uint8_t *font = PTR(font_p);
    int i, j;
    switch (mode) {
    case 0:
        for (i = 0; i < 7; i++) {
            int va = class_count(a, i);
            label(va, 32, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4410u), x + 40, y + 24 * i + 4, font, 63);
            AT(int32_t, s_slot_00568420, 0x568420u, 0x568420u + 4 * i) = va;
            gfx_sprite_render_from_blob(MISC, i + 47, x + 85, y + 24 * i);
            unit_noun(2, 2 * i + 52, x + 125, y + 24 * i + 4, font, 63);
            gfx_sprite_render_from_blob(MISC, i + 47, x + 245, y + 24 * i);
            int vb = class_count(b, i);
            label(vb, 32, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4414u), x + 280, y + 24 * i + 4, font, 63);
            AT(int32_t, s_slot_00568420, 0x568420u, 0x56843cu + 4 * i) = vb;
        }
        break;
    case 1:
        for (i = 0; i < 7; i++) {
            label(AT(int32_t, s_slot_00568420, 0x568420u, 0x568420u + 4 * i), 40, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4418u),
                  x - 6, y + 24 * i + 4, font, 63);
            label(class_count(a, i), 32, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d441cu), x + 40, y + 24 * i + 4, font, 63);
            gfx_sprite_render_from_blob(MISC, i + 47, x + 85, y + 24 * i);
            unit_noun(2, 2 * i + 52, x + 125, y + 24 * i + 4, font, 63);
            gfx_sprite_render_from_blob(MISC, i + 47, x + 245, y + 24 * i);
            label(class_count(b, i), 32, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4420u), x + 280, y + 24 * i + 4, font, 63);
            label(AT(int32_t, s_slot_00568420, 0x568420u, 0x56843cu + 4 * i), 40, STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4424u),
                  x + 326, y + 24 * i + 4, font, 63);
        }
        break;
    case 2:
        for (j = 0; j < 6; j += 2) {
            gfx_sprite_render_from_blob(MISC, j + 47, x, y + 13 * j);
            num_units(AT(int16_t, g_per_territory_420, 0x52f0b0u, 0x52f21cu + 420 * a + 2 * j), 2 * j + 52, x + 32,
                      y + 13 * j + 4, font, 63);
            gfx_sprite_render_from_blob(MISC, j + 48, x + 192, y + 13 * j);
            num_units(AT(int16_t, g_per_territory_420, 0x52f0b0u, 0x52f21eu + 420 * a + 2 * j), 2 * j + 54, x + 224,
                      y + 13 * j + 4, font, 63);
        }
        gfx_sprite_render_from_blob(MISC, j + 47, x, y + 13 * j);
        num_units(AT(int16_t, g_per_territory_420, 0x52f0b0u, 0x52f21cu + 420 * a + 2 * j), 2 * j + 52, x + 32,
                  y + 13 * j + 4, font, 63);
        return num_units(TE32(a, 0x52f218u), 72, x + 224, y + 13 * j + 4, font, 63);
    default:
        return 0;
    }
    num_units(TE32(a, 0x52f218u), 72, x + 20, y + 24 * i + 12, font, 63);
    return num_units(TE32(b, 0x52f218u), 72, x + 220, y + 24 * i + 12, font, 63);
}

/* the two leaders' portraits and names (screens 0x13 / 0x14): icon_tmp.pl8 into the panel sheet, the player's
 * shield (56 when DAT_0053e8fc, else 55) in a bevel, both sides' banners (DAT_00568934 / 00568938 + 48), each
 * side's realm name (DAT_005681ec / 005681d8; 6 = the rebels, group 99) */
static void battle_sides(const void *icon_file)
{
    read_file_at(icon_file, g_gfx_current_blob, 0x27100u, 0);
    gfx_line_rect_bevel_dark_tl(52, 68, 82, 82);
    gfx_sprite_render(s_slot_0053e8fc != 0 ? 56 : 55, 53, 69);
    gfx_sprite_render((int)s_slot_00568934 + 48, 122, 180);
    gfx_sprite_render((int)s_slot_00568938 + 48, 322, 180);
    if (s_slot_005681ec == 6)
        wrap_line_c(99, 0, 40, 160, 200, FONT, 63);
    else
        text_line_c(&AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d54u + 44 * s_slot_005681ec), 40, 160, 200, FONT, 63);
    if (s_slot_005681d8 == 6)
        wrap_line_c(99, 0, 240, 160, 200, FONT, 63);
    else
        text_line_c(&AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d54u + 44 * s_slot_005681d8), 240, 160, 200, FONT, 63);
}

/* Port of FUN_00422d22 (719 B) — the pre-battle screen (0x13): the strategic map underneath (the season sheets
 * 0x4984dc + 0x40f5fd), a 26x25 panel, the territory's name (group 100, DAT_0056822c + 20 x the player
 * DAT_0053f034), the title (group 80: 7 when DAT_0053e8fc, else 0) and the battle kind's text (DAT_005533d0:
 * 1 / 2 / other), both sides (icon_tmp.pl8) and the class table (0x4224e7 mode 0 of DAT_00568210 vs DAT_00568218). */
int rl_422d22(void)
{
    chdir_base();
    load_season();
    strat_render(1);
    chdir_base();
    rl_47703a();
    mark_full();
    gfx_panel_draw_bevel(CHROME, 32, 48, 26, 25);
    wrap_line_c(100, (int)s_slot_0056822c + 20 * (int)s_slot_0053f034, 132, 66, 306, FONT, 63);
    wrap_line(80, s_slot_0053e8fc != 0 ? 7 : 0, 142, 86, FONT_TALL, 63);
    if (s_slot_005533d0 == 1)
        wrap_loc(80, 1, 142, 122, 180, 400, 0, 0, FONT, 63);
    else if (s_slot_005533d0 == 2)
        wrap_loc(80, 2, 142, 122, 280, 400, 0, 0, FONT, 63);
    else
        wrap_loc(80, 3, 142, 122, 280, 400, 0, 0, FONT, 63);
    battle_sides(STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4428u));
    return rl_4224e7((int)s_slot_00568210, (int)s_slot_00568218, 62, 224, (int)(uintptr_t)FONT, 0);
}

/* Port of FUN_00422ff1 (592 B) — the battle result screen (0x14): the strategic map underneath, a 26x25 panel, the
 * seal, both sides (icon_tmp.pl8), the territory's name, the title (group 81: 8 when DAT_0053e8fc, else 0), the
 * class table before / after (0x4224e7 mode 1 of DAT_00553c70 vs DAT_00553d20), then the loser's absorb
 * (0x4aba5a on DAT_00553d20). */
int rl_422ff1(void)
{
    chdir_base();
    rl_47703a();
    load_season();
    strat_render(1);
    mark_full();
    gfx_panel_draw_bevel(CHROME, 32, 48, 26, 25);
    gfx_sprite_seal_draw(SYSTEM, 400, 400, 0);
    battle_sides(STR(g_rl_4d43f4, 0x4d43f4u, 0x4d4438u));
    wrap_line_c(100, (int)s_slot_0056822c + 20 * (int)s_slot_0053f034, 132, 66, 306, FONT, 63);
    wrap_line(81, s_slot_0053e8fc != 0 ? 8 : 0, 142, 86, FONT_TALL, 63);
    rl_4224e7((int)s_slot_00553c70, (int)s_slot_00553d20, 62, 224, (int)(uintptr_t)FONT, 1);
    engine_unit_absorb((int)s_slot_00553d20);
    return 0;
}

/* Port of FUN_00423241 (438 B) — the battle result panel (0x20; also after an auto-resolved battle): the menu-bar
 * redraw latches, then for a battle (DAT_005533d0) with result movies on (DAT_0053f248): the palette darkened
 * (0x4b1310), a 28x22 panel framing the movie, the seal and the result row DAT_00553fb8's title + text (group 82,
 * 2 row / 2 row + 1) under it; result movies off: a 28x10 panel with the same text; no battle: strings 12 / 13. */
int rl_423241(void)
{
    g_gfx_menu_redraw_a = 2;
    g_gfx_menu_redraw_b = 1;
    int row = g_combat_result_row;
    mark_full();
    if (s_slot_005533d0 != 0) {
        if (g_combat_flag_53f248 != 0) {
            rl_47703a();
            rl_4b1310();
            gfx_panel_draw_bevel(CHROME, 16, 48, 28, 22);
            gfx_line_rect_bevel_dark_br(39, 72, 402, 194);
            gfx_sprite_seal_draw(SYSTEM, 416, 352, 0);
            wrap_line(82, 2 * row, 48, 312, FONT_TALL, 63);
            wrap_loc(82, 2 * row + 1, 48, 344, 384, 100, 32, 416, FONT, 63);
        } else {
            gfx_panel_draw_bevel(CHROME, 16, 144, 28, 10);
            gfx_sprite_seal_draw(SYSTEM, 416, 256, 0);
            wrap_line(82, 2 * row, 48, 168, FONT_TALL, 63);
            wrap_loc(82, 2 * row + 1, 48, 224, 384, 100, 0, 0, FONT, 63);
        }
    } else {
        gfx_panel_draw_bevel(CHROME, 16, 144, 28, 10);
        gfx_sprite_seal_draw(SYSTEM, 416, 256, 0);
        wrap_line(82, 12, 48, 168, FONT_TALL, 63);
        wrap_loc(82, 13, 48, 224, 384, 100, 0, 0, FONT, 63);
    }
    return 0;
}

/* Port of FUN_004233f7 (313 B) — enter the battlefield's screen: (a1 == 1: the movie-palette reset 0x4b11ce) the
 * back buffer cleared (0x4b1867), the side HUD (roster backdrop, score box, both banners), the battlefield
 * minimap (0x4bc1d1(80)), the menu bar (0x419c78, ranking-dirty raised), the score box redrawn (0x423530), a full
 * present (0x452160(1)) and the palette (night DAT_0053e8fc: DAT_005675a0, else DAT_00568ee0). */
int rl_4233f7(int a1)
{
    if (a1 == 1)
        rl_4b11ce();
    clear_back_buffer();
    g_gfx_menu_redraw_a = 1;
    mark_full();
    gfx_sprite_render_opaque_from_blob(MISC, 0, 480, 184);
    gfx_sprite_render_opaque_from_blob(MISC, 1, 480, 448);
    gfx_sprite_render_opaque_from_blob(MISC, 2, 480, 412);
    gfx_sprite_render_opaque_from_blob(MISC, (int)s_slot_00568934 + 6, 482, 413);
    gfx_sprite_render_opaque_from_blob(MISC, (int)s_slot_00568938 + 6, 560, 413);
    DIRTY->below_status_redraw = 1;   /* DAT_0053e9ac */
    bf_minimap_scroll(80);
    engine_army_ai_set_ranking_dirty(1);   /* DAT_0056d6a0 */
    menubar_chrome();
    DIRTY->below_status_redraw = 1;
    g_bf_553ed8 = 1;
    rl_423530();
    present(1);
    mark_full();
    int r = set_palette(s_slot_0053e8fc != 0 ? g_rl_5675a0 : g_rl_568ee0);
    g_gfx_menu_redraw_a = 1;
    return r;
}

/* ================================================================ more arms, panels and transitions */

static void caspics_load(const void *name, const void *dst, int idx)   /* rl_40ae12 (glue): the caspics.pl8 frame loader */
{ rl_40ae12((int)(uintptr_t)name, (int)(uintptr_t)dst, idx); }
static void popup_scroll_dismiss(void) { engine_retail_call(0x476768u, 0); }   /* TODO(rl-link): engine_popup_scroll_dismiss */
static void labor_render(void) { engine_retail_call(0x412b33u, 0); }   /* TODO(rl-link): engine_labor_render */
static void fill4(int x, int y, int c) { engine_retail_call(0x4b4056u, 3, (uint32_t)x, (uint32_t)y, (uint32_t)c); }   /* TODO(rl-link): engine_overlay_render's 4-byte fill 0x4b4056 */
static void movie_text_box(int x, int y, int w, int h)   /* TODO(rl-extern): 0x42e45e */
{ engine_retail_call(0x42e45eu, 4, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h); }
static void mp_session_end(void) { engine_retail_call(0x4b743bu, 0); }   /* TODO(rl-extern): 0x4b743b */
static void map_build_cells(int a) { engine_retail_call(0x4298c1u, 1, (uint32_t)a); }   /* TODO(rl-link): engine_map_init_build_cells */
static void map_set_camera(int a) { engine_retail_call(0x451fccu, 1, (uint32_t)a); }   /* TODO(rl-link): engine_map_init_set_camera */
static void classify_feature_tiles(void) { rl_46dbf5(); }
static void randomise_special_tiles(void) { rl_46dd34(); }

/* Port of FUN_0041608b (637 B) — the workforce category tooltip (screen 0xc): every 500th frame (DAT_0053ef54
 * counts down) when the hovered category DAT_0056d8ac changed (DAT_0055cd68 = the shown one): the old box's 32x64
 * backdrop restored (0x4b3ec0), the new one's saved (0x4b3f0a) and an 8x4 panel with the category's name (group 6)
 * and its count "N/M" — M = N + DAT_005532a0's percentage of the territory DAT_00553c64's +0x166 (at least 1; 0
 * for category 4). Positions: the 20-byte records DAT_004d2b70 + 20 (category - 1). */
int rl_41608b(void)
{
    if (g_rl_53ef54 != 0) {
        g_rl_53ef54--;
        return 0;
    }
    if (g_rl_55cd68 == (int32_t)s_slot_0056d8ac) return 0;
    g_gfx_uppercase_highlight = 1;
    if (g_rl_55cd68 != 0) {
        int x = AT(int32_t, g_rl_4d2b70, 0x4d2b70u, 0x4d2b70u + 20 * g_rl_55cd68 - 20);
        int y = AT(int32_t, g_rl_4d2b70, 0x4d2b70u, 0x4d2b74u + 20 * g_rl_55cd68 - 20);
        g_gfx_cursor_x = x;
        g_gfx_cursor_y = y;
        g_gfx_glyph_w = 32;
        g_gfx_glyph_height_short = 64;
        workforce_restore(g_gfx_current_blob, 512);
        mark_box(x, y, 9, 5, 1);
    }
    int cat = (int)s_slot_0056d8ac;
    if (cat != 0) {
        int x = AT(int32_t, g_rl_4d2b70, 0x4d2b70u, 0x4d2b70u + 20 * cat - 20);
        int y = AT(int32_t, g_rl_4d2b70, 0x4d2b70u, 0x4d2b74u + 20 * cat - 20);
        g_gfx_cursor_x = x;
        g_gfx_cursor_y = y;
        g_gfx_glyph_w = 32;
        g_gfx_glyph_height_short = 64;
        workforce_capture(g_gfx_current_blob, 512);
        mark_box(x, y, 9, 5, 1);
        gfx_panel_draw_bevel(CHROME, x, y, 8, 4);
        wrap_line_c(6, (int)s_slot_0056d8ac, x, y + 16, 128, FONT, 63);
        g_gfx_line_x_acc = 0;
        int n = AT(int32_t, s_slot_005532a0, 0x5532a0u, 0x5532a0u + 16 * s_slot_0056d8ac);
        label(n, 64, STR(g_rl_4d3f58, 0x4d3f58u, 0x4d3f58u), x + 36, y + 34, FONT, 63);
        n = AT(int32_t, s_slot_005532a0, 0x5532a0u, 0x5532a0u + 16 * s_slot_0056d8ac);
        int more = engine_math_percent_of(n, TE8(s_slot_00553c64, 0x52f216u));
        if (more <= 0) more = 1;
        if (s_slot_0056d8ac == 4) more = 0;
        label(n + more, 64, STR(g_rl_4d3f58, 0x4d3f58u, 0x4d3f5cu), g_gfx_line_x_acc + x + 36, y + 34, FONT, 63);
    }
    g_rl_55cd68 = (int32_t)s_slot_0056d8ac;
    g_rl_53ef54 = 500;
    g_gfx_uppercase_highlight = 0;
    return 0;
}

/* Port of FUN_00417bd3 (284 B) — the treaty text box: the caret anchor and the text accumulators cleared, a 26x6
 * chrome well at (32, 192) in a bevel, message a1's text (the 200-byte records at DAT_0053f1e0 + 216) word-wrapped
 * into 384 px, the caret at the end (first-character anchor + (48, 200)). */
int rl_417bd3(int a1)
{
    g_gfx_uppercase_highlight = 1;
    g_gfx_first_pos_latch = 0;
    g_gfx_first_pos_x = g_gfx_first_pos_latch;
    g_gfx_char_counter = g_gfx_first_pos_x;
    g_gfx_first_pos_y = 200;
    g_gfx_collapse_active = 1;
    g_gfx_line_x_acc = 0;
    g_gfx_line_y_acc = 0;
    gfx_panel_fill_interior_chrome(CHROME, 32, 192, 26, 6);
    mark_box(32, 192, 28, 8, 2);
    gfx_line_rect_bevel_dark_br(32, 192, 416, 96);
    wrap_buf(&AT(uint8_t, g_bf_53f1e0, 0x53f1e0u, 0x53f1e0u + 200 * a1 + 216), 48, 200, 384, FONT, 63);
    if (g_gfx_first_pos_latch == 0) {
        g_gfx_first_pos_x = g_gfx_line_x_acc;
        g_gfx_first_pos_y = g_gfx_line_y_acc;
        g_gfx_first_pos_latch = 1;
    }
    g_gfx_first_pos_y += 200;
    g_gfx_first_pos_x += 48;
    caret(FONT, 63);
    g_gfx_uppercase_highlight = 0;
    return 0;
}

/* Port of FUN_00417aef (228 B) — the treaty message panel (diplomacy, messages 1..4): a 28x13 panel, the seal,
 * message a1's heading (group 72, a1 + 11) with the player's name (DAT_00553d50 + 44 DAT_00553fbc + 4), the text box
 * (0x417bd3) and the footer (string 17). */
int rl_417aef(int a1)
{
    g_gfx_uppercase_highlight = 1;
    gfx_panel_draw_bevel(CHROME, 16, 144, 28, 13);
    mark_box(16, 144, 29, 14, 2);
    gfx_sprite_seal_draw(SYSTEM, 424, 310, 0);
    mark_full();
    g_gfx_line_x_acc = 0;
    wrap_line(72, a1 + 11, 48, 168, FONT, 63);
    text_line(&AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d54u + 44 * s_slot_00553fbc), g_gfx_line_x_acc + 48, 168, FONT, 63);
    rl_417bd3(a1);
    wrap_line(72, 17, 112, 304, FONT, 63);
    g_gfx_uppercase_highlight = 0;
    return 0;
}

/* Port of FUN_00417cef (440 B) — the treaty offer panel (diplomacy, messages 5 / 6): the menu-bar latches, a 28x15
 * panel, the seal, the heading (group 72, a1 + 15) + the player's name, the minimap (0x410c71) with the offered
 * county DAT_005530b0 (or 20: none), and the offer's text — a county (group 72, a1 + 21, + its name, group 100,
 * DAT_005530b0 + 20 DAT_0053f034) or none (a1 + 19) — and the footer. */
int rl_417cef(int a1)
{
    g_gfx_menu_redraw_a = 0;
    g_gfx_menu_redraw_a = 2;
    g_gfx_menu_redraw_b = 1;
    g_gfx_uppercase_highlight = 1;
    gfx_panel_draw_bevel(CHROME, 48, 128, 24, 15);
    mark_box(48, 128, 25, 16, 2);
    gfx_sprite_seal_draw(SYSTEM, 392, 326, 0);
    mark_full();
    g_gfx_line_x_acc = 0;
    wrap_line(72, a1 + 15, 80, 152, FONT, 63);
    text_line(&AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d54u + 44 * s_slot_00553fbc), g_gfx_line_x_acc + 80, 152, FONT, 63);
    if (s_slot_005530b0 != 0)
        rl_410c71((int)s_slot_005530b0, 96, 176);
    else
        rl_410c71(20, 96, 176);
    if (s_slot_005530b0 != 0) {
        g_gfx_line_x_acc = 0;
        wrap_line(72, a1 + 21, 80, 320, FONT, 63);
        wrap_line(100, (int)s_slot_005530b0 + 20 * (int)s_slot_0053f034, g_gfx_line_x_acc + 80, 320, FONT, 63);
    } else {
        wrap_line(72, a1 + 19, 80, 320, FONT, 63);
    }
    wrap_line(72, 17, 320, 224, FONT, 63);
    g_gfx_uppercase_highlight = 0;
    return 0;
}

/* Port of FUN_0040cd58 (433 B) — the number entry box (screen 0x24): a 15x7 chrome panel at (DAT_0058fe20 - 16,
 * DAT_0058fe24 - 16), the seal, the prompt (group 12, DAT_0058fe1c) and footer, a 3x1 well and the value
 * *DAT_005cd410 by the mode DAT_005aeb74 (1: tenths, 2 / other: as is, three suffixes); DAT_0056d8b8 = 10. */
int rl_40cd58(void)
{
    g_gfx_menu_redraw_a = 1;
    g_rl_56d8b8 = 10;
    panel_box(g_rl_58fe20 - 16, g_rl_58fe24 - 16, 15, 7);
    gfx_sprite_seal_draw(SYSTEM, g_rl_58fe20 + 184, g_rl_58fe24 + 56, 0);
    wrap_line(12, g_rl_58fe1c, g_rl_58fe20 + 16, g_rl_58fe24 + 8, FONT, 63);
    wrap_line(12, 0, g_rl_58fe20 + 16, g_rl_58fe24 + 60, FONT, 63);
    g_rl_56d8b8 = 10;
    gfx_panel_fill_interior_chrome(CHROME, g_rl_58fe20 + 92, g_rl_58fe24 + 30, 3, 1);
    int v = *(const int32_t *)PTR(g_rl_5cd410);
    if (g_rl_5aeb74 == 1)
        label(v / 10, 32, STR(g_rl_4d2778, 0x4d2778u, 0x4d2778u), g_rl_58fe20 + 100, g_rl_58fe24 + 34, FONT, 63);
    else if (g_rl_5aeb74 == 2)
        label(v, 32, STR(g_rl_4d2778, 0x4d2778u, 0x4d277cu), g_rl_58fe20 + 100, g_rl_58fe24 + 34, FONT, 63);
    else
        label(v, 32, STR(g_rl_4d2778, 0x4d2778u, 0x4d2780u), g_rl_58fe20 + 100, g_rl_58fe24 + 34, FONT, 63);
    engine_dirty_rect_mark_tile_80x48_noclamp(DIRTY, (g_rl_58fe20 + 100) >> 4, (g_rl_58fe24 + 34) >> 4);
    return 0;
}

/* Port of FUN_0041ff75 (466 B) — the MP chat log: in MP when a line arrived (DAT_00553fc4 > 0, cleared): the big
 * log (a1 == 0: 8 lines up from y 125 on a 303x133 well at (168, 8)) or the small one (2 lines up from y 29 on
 * 433x40 at (11, 8)) of the 65-byte ring DAT_005651e0 (newest DAT_0055cd80 - 1, wrapping at 0 to the top: 7 / 1),
 * each line in its sender's (DAT_0053e880) realm colour. */
int rl_41ff75(int a1)
{
    if (g_sim_mp_active == 0 || g_rl_553fc4 <= 0) return 0;
    g_rl_553fc4 = 0;
    g_gfx_no_shadow = 1;
    if (a1 != 0) {
        gfx_line_fill_rect(11, 8, 433, 40, 63);
        int k = g_rl_55cd80 - 1;
        if (g_rl_55cd80 - 1 < 0) k = 1;
        for (int n = 0, y = 29; n < 2; n++, y -= 16) {
            text_line(&g_rl_5651e0[65 * k], 15, y, FONT, PL8(g_rl_53e880[k], 0x57bf09u));
            if (--k < 0) k = 1;
        }
    } else {
        gfx_line_fill_rect(168, 8, 303, 133, 63);
        int k = g_rl_55cd80 - 1;
        if (g_rl_55cd80 - 1 < 0) k = 7;
        for (int n = 0, y = 125; n < 8; n++, y -= 16) {
            text_line(&g_rl_5651e0[65 * k], 174, y, FONT, PL8(g_rl_53e880[k], 0x57bf09u));
            if (--k < 0) k = 7;
        }
    }
    g_gfx_no_shadow = 0;
    return 0;
}

/* Port of FUN_00412421 (581 B) — the county scene's animations (screen 0xb): the frame counters DAT_004d2930..
 * 004d294c step (the water / smoke ones while DAT_00591510, the rest while DAT_0057d3ac), the mill, the smoke and
 * the water from DAT_005533c4 (the scene sheet), the county's (DAT_0057c93c) optional animals / smithy / church
 * (+0x295 / +0x2dd; +0x2ad from DAT_0053e918), all at DAT_0057c908 + y. */
int rl_412421(void)
{
    if (g_rl_591510 != 0) {
        if (++g_rl_4d2940 >= 7) g_rl_4d2940 = 0;
        if (++g_rl_4d2948 >= 16) g_rl_4d2948 = 0;
    }
    if (g_rl_57d3ac != 0) {
        if (++g_rl_4d2930 >= 7) g_rl_4d2930 = 0;
        if (++g_rl_4d293c >= 8) g_rl_4d293c = 0;
        if (++g_rl_4d2944 >= 8) g_rl_4d2944 = 0;
        if (++g_rl_4d294c >= 10) g_rl_4d294c = 0;
        if (++g_rl_4d2934 >= 21) g_rl_4d2934 = 0;
        if (++g_rl_4d2938 >= 18) g_rl_4d2938 = 0;
    }
    const uint8_t *sheet = PTR(g_rl_5533c4);
    gfx_sprite_render_from_blob(sheet, g_rl_4d293c + 25, 80, g_rl_57c908 + 175);
    gfx_sprite_render_from_blob(sheet, g_rl_4d2940 + 33, 114, g_rl_57c908 + 195);
    gfx_sprite_render_from_blob(sheet, g_rl_4d294c + 15, 366, g_rl_57c908 + 293);
    if (CO8(s_slot_0057c93c, 0x53fc45u) != 0)
        gfx_sprite_render_from_blob(PTR(g_rl_5533c4), g_rl_4d2944 + 7, 243, g_rl_57c908 + 260);
    if (CO8(s_slot_0057c93c, 0x53fc8du) != 0)
        gfx_sprite_render_from_blob(PTR(g_rl_5533c4), g_rl_4d2930, 163, g_rl_57c908 + 27);
    if (CO8(s_slot_0057c93c, 0x53fc5du) != 0)
        gfx_sprite_render_from_blob(PTR(g_rl_53e918), g_rl_4d2938, 164, g_rl_57c908 + 12);
    mark_box(80, g_rl_57c908, 23, 20, 1);
    return 0;
}

/* Port of FUN_004788c6 (140 B) — back from the battlefield to the county level: the sub-state 0, the season sheets
 * (0x4984dc), the strategic cells and camera (0x4298c1(0), 0x451fcc(DAT_0057cb18)), the tile features
 * (0x46dbf5 / 0x46dd34), the HUD sheets (0x499d7d / 0x499aca), screen 0x13, the animation clock, the strategic
 * map; full redraw, DAT_0053f238 = 0, logs "county level restored". Returns the log's EAX. */
int rl_4788c6(void)
{
    bf_43f304();
    s_slot_0057c8fc = 0;
    load_season();
    map_build_cells(0);
    map_set_camera((int)s_slot_0057cb18);
    classify_feature_tiles();
    randomise_special_tiles();
    rl_499d7d();
    rl_499aca();
    engine_set_current_screen(0x13);
    g_combat_anim_clock = (int)rl_plat_timeGetTime();
    strat_render(1);
    engine_set_demand_redraw(2);
    g_bf_53f238 = 0;
    return rl_4afab9((int)(uintptr_t)STR(g_rl_4d9ecc, 0x4d9eccu, 0x4d9eccu), 0, 0);
}

/* Port of FUN_00438e3b (574 B) — a click on the labour panel's rows (x 478..640, y 302..430) by the county's owner
 * (+5 == the player) with the panel open (DAT_004eafb4): the left column picks a worker kind by the row's type
 * (DAT_0053f690[row]: 0 / 1 / 2 → 1 / 2 / 3; rows of DAT_0053e970 px, DAT_00553c60 of them), the right one a
 * building (DAT_00553fd0[row]: 3..7 → 4 / 5 / 6 / 7 / 8; DAT_0053f04c px, DAT_005440b4 rows) into DAT_00553f54;
 * then the labour screen (0xf). Returns 1 when it took the click. */
int rl_438e3b(void)
{
    if (CO8(s_slot_0057c93c, 0x53f9b5u) != (uint32_t)s_slot_0057c8cc) return 0;
    if (g_bf_4eafb4 == 0) return 0;
    if (g_bf_4e65fc < 478 || g_bf_4e65fc >= 640 || g_bf_4e65f4 < 302 || g_bf_4e65f4 >= 430) return 0;
    if (g_bf_4e65fc >= 560) {
        int r = (g_bf_4e65f4 - 302) / g_rl_53f04c;
        if (r >= g_rl_5440b4) return 0;
        switch (g_rl_553fd0[r]) {
        case 6: g_rl_553f54 = 7; break;
        case 5: g_rl_553f54 = 6; break;
        case 4: g_rl_553f54 = 5; break;
        case 7: g_rl_553f54 = 8; break;
        case 3: g_rl_553f54 = 4; break;
        default: return 0;
        }
    } else {
        int r = (g_bf_4e65f4 - 302) / g_rl_53e970;
        if (r >= g_rl_553c60) return 0;
        int k = AT(int32_t, g_rl_53f690, 0x53f690u, 0x53f690u + 4 * r);
        if (k == 1)      g_rl_553f54 = 2;
        else if (k == 0) g_rl_553f54 = 1;
        else if (k == 2) g_rl_553f54 = 3;
        else return 0;
    }
    engine_set_current_screen(0x0f);
    popup_scroll_dismiss();
    g_rl_5533f4 = 1;
    labor_render();
    return 1;
}

/* Port of FUN_0042e060 (527 B) — after a movie / screen transition: the return screen (DAT_005169b0's low byte).
 * At the title (state 1): intro.smk → imptitle.smk at (80, 80), imptitle.smk → credits.smk; any other (or a
 * failed play) raises DAT_005c9274 + a redraw. In game: the MP session end (0x4b743b, DAT_0053f0c4 10 / 11), the
 * strategic palette back (screen 0x44 → 0), the battle-result delay (0x2b: DAT_00568470 = 5001), the gateway's
 * setup.wav (0x1f with DAT_005530f0 == 1), the options (0x1c) — else the screen rebuilt: the battlefield's
 * (0x4987b7 + 0x480f8b) or the season's sheets (0x4984dc), then the strategic map or the battlefield screen
 * (0x4233f7(1)), and on the map the HUD sheet (0x499aca). The argument is unused. */
int rl_42e060(int a0)
{
    (void)a0;
    g_rl_516a4c = 0;
    engine_set_current_screen((uint8_t)g_rl_5169b0);
    if (g_engine_state == 1) {
        rl_4b11ce();
        if (strcmp((const char *)g_rl_5169d0, STR(g_rl_4d50ac, 0x4d50acu, 0x4d50acu)) == 0) {
            if (rl_42d91b((int)(uintptr_t)STR(g_rl_4d50ac, 0x4d50acu, 0x4d50b8u), 80, 80, 0, 31) == 0) {
                g_rl_5c9274 = 1;
                engine_set_demand_redraw(1);
            }
        } else if (strcmp((const char *)g_rl_5169d0, STR(g_rl_4d50ac, 0x4d50acu, 0x4d50c8u)) != 0) {
            g_rl_5c9274 = 1;
            engine_set_demand_redraw(1);
        } else if (rl_42d91b((int)(uintptr_t)STR(g_rl_4d50ac, 0x4d50acu, 0x4d50d8u), 0, 0, 0, 31) == 0) {
            g_rl_5c9274 = 1;
            engine_set_demand_redraw(1);
        }
        return 0;
    }
    if (g_rl_53f0c4 == 11 || g_rl_53f0c4 == 10)
        mp_session_end();
    if (SCREEN == 0x44) {
        set_palette(g_rl_5691f0);
        engine_set_current_screen(0);
        engine_set_demand_redraw(1);
    } else if (SCREEN == 0x2b) {
        s_slot_00568470 = 5001;
    }
    if (SCREEN == 0x1f && g_rl_5530f0 == 1) {
        engine_set_demand_redraw(1);
        rl_4263ad((int)(uintptr_t)STR(g_rl_4d50ac, 0x4d50acu, 0x4d50e4u), 0, 0);
        return 0;
    }
    if (SCREEN == 0x1c) {
        engine_set_demand_redraw(1);
        return 0;
    }
    if (g_engine_state == 1) return 0;
    if (s_slot_0057c8fc == 2) {
        bf_4987b7();
        bf_480f8b();
    } else {
        load_season();
    }
    if (s_slot_0057c8fc == 0)
        strat_render(1);
    else
        rl_4233f7(1);
    if (s_slot_0057c8fc == 0)
        rl_499aca();
    return 0;
}

/* Port of FUN_0041a166 (1095 B) — a movie's subtitle (not in the English build: the localization's language name,
 * string 300, is compared with "English"): per movie cue a1 the centred line(s) of group 301 at y 400 / 416 in
 * colour 245, or the subtitle strip cleared (16 or 32 px), then the text box 0x42e45e and its dirty mark. */
int rl_41a166(int a1)
{
    for (int i = 0; i < 20; i++)
        g_rl_522c80[i] = 0;
    localization_read(loc_in(), g_rl_522c80, 300, 0, 7);   /* retail walks a LOCAL pointer: DAT_005c9270 stays */
    if (strcmp(STR(g_rl_4d41c4, 0x4d41c4u, 0x4d41c4u), (const char *)g_rl_522c80) == 0) return 0;
    int line = -1, line2 = -1, clear = 0;
    switch (a1) {
    case 29:   line = 0; break;
    case 65:   line = 1; break;
    case 230:  line = 2; break;
    case 262:  line = 3; break;
    case 279:  line = 4; line2 = 5; break;
    case 365:  line = 6; break;
    case 413:  line = 7; line2 = 8; break;
    case 1092: line = 9; break;
    case 1221: line = 10; break;
    case 50: case 92: case 261: case 278: case 412: case 1120: case 1262: clear = 16; break;
    case 364: case 500: clear = 32; break;
    default: return 0;
    }
    if (clear)
        gfx_line_fill_rect(0, 398, 640, clear, 0);
    if (line >= 0)
        wrap_line_c(301, line, 0, 400, 640, FONT, 245);
    if (line2 >= 0)
        wrap_line_c(301, line2, 0, 416, 640, FONT, 245);
    movie_text_box(0, 400, 640, 32);
    mark_box(0, 400, 40, 2, 1);
    return 0;
}

/* Port of FUN_00419789 (289 B) — the castle screen (0x1b): cas_back (with its palette when a1 == 1, after the
 * movie-palette reset), the seal at the corner, the tier's picture (caspics.pl8 frame DAT_004d2dd0[tier] - 1, raw
 * 320x200 at (158, 20)), the castle sheet (0x4996c8 + cas_bits.pl8 into DAT_005533c4), the tier panel (0x4198aa,
 * forced by DAT_005440b8 = 1); a1 == 1: the palette. */
int rl_419789(int a1)
{
    chdir_base();
    if (a1 == 1) {
        rl_4b11ce();
        read_file_at(STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4108u), g_rl_4ea8a0, 0x300u, 0);
        load_background(STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4118u), 480);
    } else {
        load_background(STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4128u), 480);
    }
    gfx_sprite_seal_draw(SYSTEM, g_gfx_canvas_width - 28, g_gfx_canvas_height - 28, 1);
    if (AT(int32_t, g_rl_4d2dd0, 0x4d2dd0u, 0x4d2dd0u + 4 * g_castle_tier) != 0) {
        caspics_load(STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4138u), g_gfx_current_blob,
                     AT(int32_t, g_rl_4d2dd0, 0x4d2dd0u, 0x4d2dd0u + 4 * g_castle_tier) - 1);
        gfx_sprite_render_raw(g_gfx_current_blob, 158, 20, 320, 200);
    }
    rl_4996c8();
    read_file_at(STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4144u), PTR(g_rl_5533c4), 0x249f0u, 0);
    g_rl_5440b8 = 1;
    rl_4198aa();
    mark_full();
    if (a1 == 1)
        set_palette(g_rl_4ea8a0);
    g_gfx_menu_redraw_a = 1;
    return 0;
}

#define CT(va) AT(int32_t, g_rl_4d2dd0, 0x4d2dd0u, (va) + 12 * g_castle_tier)   /* the tiers' 12-byte sprite records */
#define TIER(va, k) AT(int32_t, g_rl_4d89b8, 0x4d89b8u, (va) + (k))          /* the tiers' tables at DAT_004d89b8 */

/* Port of FUN_004198aa (974 B) — the castle tier panel (when DAT_005440b8, cleared): the tier's two sprites, its
 * name (group 71, tier + 1, tall, wrapped), its two costs (DAT_004d89c4 / c0 [2 tier], icons 10 / 11; minus the
 * county's built castle's (+0x1c0, DAT_004d89b8 pairs) — and that castle's marker on the tier bar), the garrison
 * (DAT_004d89e8 [2 tier], in men), and the rows of DAT_004d8a28 / DAT_004d8a10 [tier]. */
int rl_4198aa(void)
{
    if (g_rl_5440b8 == 0) return 0;
    g_rl_5440b8 = 0;
    int t = g_castle_tier;
    int c0 = TIER(0x4d89c0u, 8 * t), c4 = TIER(0x4d89c4u, 8 * t);
    if (CO8(s_slot_0057c93c, 0x53fb70u) != 0) {
        c0 -= TIER(0x4d89b8u, 8 * CO8(s_slot_0057c93c, 0x53fb70u));
        c4 -= TIER(0x4d89bcu, 8 * CO8(s_slot_0057c93c, 0x53fb70u));
    }
    gfx_sprite_render_from_blob(PTR(g_rl_5533c4), CT(0x4d2de8u), CT(0x4d2decu), CT(0x4d2df0u));
    gfx_sprite_render_from_blob(PTR(g_rl_5533c4), CT(0x4d2e28u), CT(0x4d2e2cu), CT(0x4d2e30u));
    wrap_loc(71, g_castle_tier + 1, 502, 24, 160, 100, 0, 0, FONT_TALL, 63);
    gfx_sprite_render_from_blob(PTR(g_rl_5533c4), 10, 520, 92);
    label(c4, 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4154u), 560, 96, FONT, 63);
    gfx_sprite_render_from_blob(PTR(g_rl_5533c4), 11, 520, 124);
    label(c0, 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4158u), 560, 128, FONT, 63);
    if (CO8(s_slot_0057c93c, 0x53fb70u) != 0)
        gfx_sprite_render_from_blob(PTR(g_rl_5533c4), 12,
                                    AT(int32_t, g_rl_4d2dd0, 0x4d2dd0u, 0x4d2e64u + 4 * CO8(s_slot_0057c93c, 0x53fb70u)) - 10, 272);
    int garrison = TIER(0x4d89e8u, 8 * g_castle_tier);
    wrap_line(71, 8, 524, 180, FONT, 63);
    num_units(garrison, 38, 498, 196, FONT, 63);
    num_units(1, 66, 508, 212, FONT, 63);
    wrap_line(71, 9, 524, 228, FONT, 63);
    panel_box(112, 428, 26, 3);
    g_gfx_line_x_acc = 0;
    wrap_line(71, 16, 144, 436, FONT, 63);
    label(TIER(0x4d8a28u, 4 * g_castle_tier), 32, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d415cu), g_gfx_line_x_acc + 144, 436,
          FONT, 63);
    wrap_line(71, 15, 224, 456, FONT, 63);
    panel_box(8, 200, 8, 3);
    wrap_line(71, 11, 12, 210, FONT, 63);
    g_gfx_line_x_acc = 0;
    label(TIER(0x4d8a10u, 4 * g_castle_tier), 64, STR(g_rl_4d40d8, 0x4d40d8u, 0x4d4160u), 12, 226, FONT, 63);
    wrap_line(71, 12, g_gfx_line_x_acc + 14, 226, FONT, 63);
    return 0;
}

/* Port of FUN_0041438c (446 B) — clear the battlefield's bottom strip: rows 474..479, x 0..475 in 4-byte fills
 * (0x4b4056) and the last 2 pixels at x 476. */
int rl_41438c(void)
{
    for (int y = 474; y < 480; y++) {
        int x;
        for (x = 0; x < 476; x += 4)
            fill4(x, y, 0);
        gfx_paint_2x1(x, y, 0);
    }
    return 0;
}

/* ================================================================ terrain info (the territory / county panel) */

static void year_number(int v, int x, int y, int mode)   /* TODO(rl-link): county_year_number 0x41a900 */
{ engine_retail_call(0x41a900u, 4, (uint32_t)v, (uint32_t)x, (uint32_t)y, (uint32_t)mode); }
static void castle_block(int c, int x, int y, int row)   /* TODO(rl-link): labor_castle_block 0x41dedb (engine_labor.c) */
{ engine_retail_call(0x41dedbu, 4, (uint32_t)c, (uint32_t)x, (uint32_t)y, (uint32_t)row); }
static void button_strip(int x, int y, const void *recs, int n)   /* 0x40cfd2 = engine_button_render_list (linked), called through
                                                                     retail: its pressed-button mark seam is bound by rh_hooks_gfx.c to
                                                                     the gfx table's own dirty copy, not the mirror this port runs on */
{ engine_retail_call(0x40cfd2u, 4, (uint32_t)x, (uint32_t)y, (uint32_t)(uintptr_t)recs, (uint32_t)n); }

#define ROW16 (16 * g_rl_553d2c)   /* the panel's first row (DAT_00553d2c: 2 / 15 / 18) in pixels */
#define T_SEL g_rl_52afbc          /* the selected territory */
#define TOWN(t) ((int)(int8_t)TE8(t, 0x52f0b0u))   /* a territory's owner (+0, signed: movsx) */
#define CTY32(va) AT(int32_t, g_per_army_768, 0x53f9b0u, (va) + 768 * g_rl_5681e0[0])   /* the selected county's dwords */

/* Port of FUN_0041b19d (3425 B) — the territory info panel: by the territory's (DAT_0052afbc) kind (+8: 1 a county,
 * 2 / 3 / 4 other kinds) the panel's first row DAT_00553d2c (2 for the player's own county, 18 for another's, 15
 * otherwise), the chrome panel and seal, a frame, the kind's title (group 31) and description, its icon
 * (icon_tmp.pl8), then per kind: 3 — the terrain line (group 5); 4 — the owner's shield, the two class counts;
 * 1 — the county's ownership texts (by its +5 owner vs the player / the territory's owner; DAT_0053f260 adds the
 * county's state line, +0x155, in 249 when set), its name (group owner + 93) and realm (group 100), and for the
 * player's own: the year, the population (+0x15c), the castle pictures (41..43, or 40.. with the build countdown
 * 15 - (signed) +0x153 while +0x198 is clear), the class table (0x4224e7 mode 2) and the mercenaries (+0x196). */
int rl_41b19d(void)
{
    int v1 = 0, v2 = 0, v4 = 0, v3 = 0;
    int kind = TE8(T_SEL, 0x52f0b8u);
    if (kind == 1)
        g_rl_553d2c = TOWN(T_SEL) == (int32_t)s_slot_0057c8cc ? 2 : 18;
    else
        g_rl_553d2c = 15;
    panel_box(8, ROW16 + 32, 28, 27 - g_rl_553d2c);
    gfx_sprite_seal_draw(SYSTEM, 428, 438, 0);
    if (TE8(T_SEL, 0x52f0b8u) == 3)
        gfx_line_rect_bevel_dark_br(32, ROW16 + 56, 400, 16 * (24 - g_rl_553d2c));
    if (TE8(T_SEL, 0x52f0b8u) == 2)
        gfx_line_rect_bevel_dark_br(32, ROW16 + 56, 400, 16 * (24 - g_rl_553d2c));
    if (TE8(T_SEL, 0x52f0b8u) == 4)
        gfx_line_rect_bevel_dark_br(32, ROW16 + 80, 400, 16 * (22 - g_rl_553d2c));
    switch (TE8(T_SEL, 0x52f0b8u)) {
    case 3: v1 = 0; v2 = 12; v4 = 35; break;
    case 2: v1 = 5; v2 = 13; v4 = 37; break;
    case 4: v1 = 2; v2 = 14; v4 = 36; break;
    case 1:
        v1 = 6;
        if (TOWN(T_SEL) == (int32_t)s_slot_0057c8cc) { v2 = 16; v4 = 39; }
        else                                         { v2 = 15; v4 = 38; }
        break;
    default: break;
    }
    if (v1 == 2) {
        g_gfx_line_x_acc = 0;
        wrap_line(31, v1, 24, ROW16 + 48, FONT_TALL, 63);
        wrap_line(100, 20 * (int)s_slot_0053f034 + TE8(T_SEL, 0x52f217u), g_gfx_line_x_acc + 24, ROW16 + 48, FONT_TALL, 63);
    } else if (v1 != 6) {
        wrap_line(31, v1, 40, ROW16 + 64, FONT_TALL, 63);
    }
    if (TE8(T_SEL, 0x52f0b8u) != 1)
        wrap_loc(31, v2, 104, ROW16 + 100, 288, 64, 40, 368, FONT, 63);
    chdir_base();
    read_file_at(STR(g_rl_4d4218, 0x4d4218u, 0x4d4218u), g_gfx_current_blob, 0x27100u, 0);
    gfx_sprite_render(v4, 40, ROW16 + 96);
    if (TE8(T_SEL, 0x52f0b8u) == 3) {
        wrap_line(5, TE8(T_SEL, 0x52f1ffu), 104, ROW16 + 164, FONT, 63);
        return 0;
    }
    if (TE8(T_SEL, 0x52f0b8u) == 2) return 0;
    if (TE8(T_SEL, 0x52f0b8u) == 4) {
        gfx_sprite_render_from_blob(MISC, PL8(TOWN(T_SEL), 0x57bf0au) + 85, 408, ROW16 + 96);
        gfx_sprite_render_from_blob(MISC, 33, 56, ROW16 + 160);
        num_units(TE16(T_SEL, 0x52f21cu), 68, 96, ROW16 + 164, FONT, 63);
        gfx_sprite_render_from_blob(MISC, 38, 260, ROW16 + 160);
        num_units(TE16(T_SEL, 0x52f220u), 70, 302, ROW16 + 164, FONT, 63);
        return 0;
    }
    if (TE8(T_SEL, 0x52f0b8u) != 1) return 0;
    int cown = CO8(g_rl_5681e0[0], 0x53f9b5u);
    if (TOWN(T_SEL) == (int32_t)s_slot_0057c8cc)
        v3 = cown == (int)s_slot_0057c8cc ? 23 : 24;
    else
        v3 = cown == TOWN(T_SEL) ? 25 : 24;
    if (s_slot_0053f260 == 1) {
        wrap_loc(31, v2, 104, ROW16 + 94, 288, 64, 40, 368, FONT, 63);
        wrap_loc(31, v3, 104, ROW16 + 114, 288, 64, 40, 368, FONT, 63);
        wrap_loc(31, TE8(T_SEL, 0x52f205u) + 27, 104, ROW16 + 134, 320, 64, 40, 368, FONT,
                 TE8(T_SEL, 0x52f205u) != 0 ? 249 : 63);
    } else {
        wrap_loc(31, v2, 104, ROW16 + 112, 288, 64, 40, 368, FONT, 63);
    }
    g_gfx_line_x_acc = 0;
    wrap_line(TOWN(T_SEL) + 93, TE8(T_SEL, 0x52f1ffu), 40, ROW16 + 48, FONT_TALL, 63);
    g_gfx_line_x_acc = 0;
    wrap_line(31, 9, 40, ROW16 + 74, FONT, 63);
    wrap_line(100, 20 * (int)s_slot_0053f034 + TE8(T_SEL, 0x52f0c1u), g_gfx_line_x_acc + 40, ROW16 + 74, FONT, 63);
    if (TOWN(T_SEL) != (int32_t)s_slot_0057c8cc) return 0;
    gfx_line_rect_bevel_dark_br(24, 16 * (g_rl_553d2c + 8) + 24, 416, 16 * (14 - g_rl_553d2c));
    g_gfx_line_x_acc = 0;
    wrap_line(31, 20, 40, ROW16 + 160, FONT, 63);
    year_number(TE16(T_SEL, 0x52f214u), g_gfx_line_x_acc + 40, ROW16 + 160, 0);
    g_gfx_line_x_acc = 0;
    wrap_line(31, 8, 248, ROW16 + 160, FONT, 63);
    num_units(TE32(T_SEL, 0x52f20cu), 0, g_gfx_line_x_acc + 248, ROW16 + 160, FONT, 63);
    if (TE8(T_SEL, 0x52f248u) != 0) {
        gfx_sprite_render(41, g_rl_4dc560, ROW16 + g_rl_4dc562);
        gfx_sprite_render(42, g_rl_4dc578, ROW16 + g_rl_4dc57a);
        gfx_sprite_render(43, g_rl_4dc590, ROW16 + g_rl_4dc592);
    } else {
        g_gfx_line_x_acc = 0;
        int left = 15 - (int8_t)TE8(T_SEL, 0x52f203u);
        if (left < 0) left = 0;
        label(left, 64, STR(g_rl_4d4218, 0x4d4218u, 0x4d4228u), g_gfx_line_x_acc + 248, ROW16 + 368, FONT, 63);
        wrap_line(31, 22, g_gfx_line_x_acc + 248, ROW16 + 368, FONT, 63);
        gfx_sprite_render(40, g_rl_4dc560, ROW16 + g_rl_4dc562);
        gfx_sprite_render(42, g_rl_4dc578, ROW16 + g_rl_4dc57a);
        gfx_sprite_render(43, g_rl_4dc590, ROW16 + g_rl_4dc592);
    }
    rl_4224e7(T_SEL, 0, 56, ROW16 + 190, (int)(uintptr_t)FONT, 2);
    if (TE8(T_SEL, 0x52f246u) != 0) {
        g_gfx_line_x_acc = 0;
        label(TE8(T_SEL, 0x52f246u), 64, STR(g_rl_4d4218, 0x4d4218u, 0x4d422cu), 56, ROW16 + 304, FONT_TALL, 63);
        wrap_line(16, TE8(T_SEL, 0x52f247u), g_gfx_line_x_acc + 56, ROW16 + 304, FONT_TALL, 63);
        unit_noun(TE8(T_SEL, 0x52f246u), 2 * TE8(T_SEL, 0x52f245u) + 52, g_gfx_line_x_acc + 56, ROW16 + 304, FONT_TALL, 63);
    } else {
        wrap_line(16, 0, 56, ROW16 + 304, FONT_TALL, 63);
    }
    return 0;
}

/* Port of FUN_0041c996 (420 B) — the county panel's land-use box (not at the season's end, DAT_0057cb2c 23 / 24; the
 * player's county DAT_005681d0): in turns 1..24 a 384x64 frame 7 rows lower with three field icons (1 / 13 / 22),
 * else (turn 0 or past 24) at the first row with two (17 / 0); and group 30's string 52 wrapped in it. */
int rl_41c996(void)
{
    if (g_rl_57cb2c == 23 || g_rl_57cb2c == 24) return 0;
    if ((int32_t)s_slot_0057c8cc != g_rl_5681d0) return 0;
    if (g_rl_57cb2c != 0 && g_rl_57cb2c < 25) {
        int y = 16 * (g_rl_553d2c + 7);
        gfx_line_rect_bevel_dark_tl(40, y + 176, 384, 64);
        gfx_sprite_render(1, 240, y + 184);
        gfx_sprite_render(13, 304, y + 184);
        gfx_sprite_render(22, 368, y + 184);
        wrap_loc(30, 52, 48, y + 194, 192, 100, 48, 128, FONT, 63);
    } else {
        gfx_line_rect_bevel_dark_tl(40, ROW16 + 176, 384, 64);
        gfx_sprite_render(17, 304, ROW16 + 184);
        gfx_sprite_render(0, 368, ROW16 + 184);
        wrap_loc(30, 52, 48, ROW16 + 194, 192, 100, 48, 128, FONT, 63);
    }
    return 0;
}

/* Port of FUN_0041d299 (1942 B) — the county panel's people box (the player's county DAT_005681e0): the population
 * (+0xd0; 249 below +0xd4, 252 above +0xd8) and the families (+0x250), the happiness word (+0x25c: 10 / 20 / 30,
 * while +0x200), the plague / unrest line (+0x274 with its kind +0x1aa: 136 / 137 / 140 / 141, or "none"), the
 * growth (+0x270, signed, with DAT_0053f25c), the births / deaths (+0x268 / +0x26c) and their balance, the tax
 * (-(+0x17c)) and the tithes (+0x258) as signed labels. */
int rl_41d299(void)
{
    if (CO8(g_rl_5681e0[0], 0x53f9b5u) != (uint32_t)s_slot_0057c8cc) return 0;
    int c = 63;
    if (CTY32(0x53fa80u) < CTY32(0x53fa84u))
        c = 249;
    else if (CTY32(0x53fa88u) < CTY32(0x53fa80u))
        c = 252;
    num_units(CTY32(0x53fa80u), 34, 104, ROW16 + 104, FONT, c);
    num_units(CTY32(0x53fc00u), 4, 296, ROW16 + 104, FONT, 63);
    if (CO8(g_rl_5681e0[0], 0x53fbb0u) != 0) {
        int s;
        switch (CTY32(0x53fc0cu)) {
        case 10: s = 8; break;
        case 20: s = 9; break;
        case 30: s = 10; break;
        default: s = 11; break;
        }
        wrap_line(77, s, 104, ROW16 + 120, FONT, 63);
    }
    g_gfx_line_x_acc = 0;
    if (CTY32(0x53fc24u) != 0) {
        num_units(CTY32(0x53fc24u), 4, 40, ROW16 + 148, FONT, 63);
        int s = -1;
        switch (AT(int16_t, g_per_army_768, 0x53f9b0u, 0x53fb5au + 768 * g_rl_5681e0[0])) {
        case 136: s = 20; break;
        case 137: s = 21; break;
        case 140: s = 22; break;
        case 141: s = 23; break;
        default: break;
        }
        if (s >= 0)
            wrap_line(77, s, g_gfx_line_x_acc + 40, ROW16 + 148, FONT, 63);
    } else {
        wrap_line(77, 19, 40, ROW16 + 148, FONT, 63);
    }
    if (s_slot_0053f25c == 1) {
        g_gfx_line_x_acc = 0;
        if (CTY32(0x53fc20u) > 0) {
            num_units(CTY32(0x53fc20u), 4, g_gfx_line_x_acc + 40, ROW16 + 164, FONT, 63);
            wrap_line(77, 16, g_gfx_line_x_acc + 40, ROW16 + 164, FONT, 63);
        } else if (CTY32(0x53fc20u) < 0) {
            num_units(-CTY32(0x53fc20u), 4, g_gfx_line_x_acc + 40, ROW16 + 164, FONT, 63);
            wrap_line(77, 17, g_gfx_line_x_acc + 40, ROW16 + 164, FONT, 63);
        } else {
            wrap_line(77, 18, g_gfx_line_x_acc + 40, ROW16 + 164, FONT, 63);
        }
    }
    wrap_line(77, 5, 40, ROW16 + 192, FONT, 63);
    num_units(CTY32(0x53fc18u), 4, 264, ROW16 + 192, FONT, 63);
    wrap_line(77, 6, 40, ROW16 + 208, FONT, 63);
    num_units(CTY32(0x53fc1cu), 4, 264, ROW16 + 208, FONT, 63);
    wrap_line(77, 7, 40, ROW16 + 224, FONT, 63);
    if (CTY32(0x53fc18u) == CTY32(0x53fc1cu))
        label(0, 64, STR(g_rl_4d4258, 0x4d4258u, 0x4d4258u), 264, ROW16 + 224, FONT, 63);
    else
        label_ls(CTY32(0x53fc18u) - CTY32(0x53fc1cu), 0, (const uint8_t *)STR(g_rl_4d4258, 0x4d4258u, 0x4d4260u),
                 STR(g_rl_4d4258, 0x4d4258u, 0x4d425cu), 264, ROW16 + 224, FONT, 63, 249);
    wrap_line(77, 27, 40, ROW16 + 240, FONT, 63);
    if (CTY32(0x53fb2cu) != 0)
        label_ls(-CTY32(0x53fb2cu), 0, (const uint8_t *)STR(g_rl_4d4258, 0x4d4258u, 0x4d426cu),
                 STR(g_rl_4d4258, 0x4d4258u, 0x4d4268u), 264, ROW16 + 240, FONT, 63, 249);
    else
        label(0, 64, STR(g_rl_4d4258, 0x4d4258u, 0x4d4264u), 264, ROW16 + 240, FONT, 63);
    wrap_line(77, 28, 40, ROW16 + 256, FONT, 63);
    if (CTY32(0x53fc08u) != 0)
        label_ls(CTY32(0x53fc08u), 0, (const uint8_t *)STR(g_rl_4d4258, 0x4d4258u, 0x4d4278u),
                 STR(g_rl_4d4258, 0x4d4258u, 0x4d4274u), 264, ROW16 + 256, FONT, 63, 249);
    else
        label(0, 64, STR(g_rl_4d4258, 0x4d4258u, 0x4d4270u), 264, ROW16 + 256, FONT, 63);
    return 0;
}

/* Port of FUN_0041da2f (1196 B) — the county panel's castle box (DAT_005681e0): DAT_00568474 = a castle is being
 * built (+0x1bc, the builder's territory); a built castle (+0x1c3): the player's garrison (+0xe8) and the castle
 * block (0x41dedb), the "continue" button when building; none, not started (+0x1c2 clear): with a builder its
 * build time / walls (DAT_004d8a24 / DAT_004d8a0c by the kind +0x1c0), the builder's men (+0x168, the player's
 * territory) or "foreign", the button — without, just the two rows. */
int rl_41da2f(void)
{
    g_rl_568474 = CTY32(0x53fb6cu) != 0;
    int kind = CO8(g_rl_5681e0[0], 0x53fb70u);
    if (CO8(g_rl_5681e0[0], 0x53fb73u) != 0) {
        if (CO8(g_rl_5681e0[0], 0x53f9b5u) == (uint32_t)s_slot_0057c8cc) {
            num_units(CTY32(0x53fa98u), 38, 104, ROW16 + 136, FONT, 63);
            castle_block(g_rl_5681e0[0], 8, 48, g_rl_553d2c);
        }
        if (g_rl_568474 != 0) {
            g_gfx_line_x_acc = 0;
            wrap_line(71, 14, 104, ROW16 + 260, FONT, 63);
            button_strip(8, 32, &g_rl_4dd640, g_rl_568474);
        }
        return 0;
    }
    if (CO8(g_rl_5681e0[0], 0x53fb72u) != 0) return 0;
    if (CTY32(0x53fb6cu) != 0) {
        int b = CTY32(0x53fb6cu);
        g_gfx_line_x_acc = 0;
        wrap_line(71, 16, 104, ROW16 + 136, FONT, 63);
        label(AT(int32_t, g_rl_4d89b8, 0x4d89b8u, 0x4d8a24u + 4 * kind), 32, STR(g_rl_4d4258, 0x4d4258u, 0x4d427cu),
              g_gfx_line_x_acc + 104, ROW16 + 136, FONT, 63);
        g_gfx_line_x_acc = 0;
        wrap_line(71, 11, 104, ROW16 + 152, FONT, 63);
        label(AT(int32_t, g_rl_4d89b8, 0x4d89b8u, 0x4d8a0cu + 4 * CO8(g_rl_5681e0[0], 0x53fb70u)), 32,
              STR(g_rl_4d4258, 0x4d4258u, 0x4d4280u), g_gfx_line_x_acc + 104, ROW16 + 152, FONT, 63);
        wrap_line(71, 12, g_gfx_line_x_acc + 104, ROW16 + 152, FONT, 63);
        if (TOWN(b) == (int32_t)s_slot_0057c8cc) {
            g_gfx_line_x_acc = 0;
            label(TE32(b, 0x52f218u), 32, STR(g_rl_4d4258, 0x4d4258u, 0x4d4284u), 104, ROW16 + 168, FONT, 63);
            wrap_line(71, 13, g_gfx_line_x_acc + 104, ROW16 + 168, FONT, 63);
        } else {
            wrap_line(71, 19, 104, ROW16 + 168, FONT, 63);
        }
        g_gfx_line_x_acc = 0;
        wrap_line(71, 14, 104, ROW16 + 196, FONT, 63);
        button_strip(8, 32, &g_rl_4dd640, g_rl_568474);
        return 0;
    }
    g_gfx_line_x_acc = 0;
    wrap_line(71, 16, 104, ROW16 + 136, FONT, 63);
    label(AT(int32_t, g_rl_4d89b8, 0x4d89b8u, 0x4d8a24u + 4 * kind), 32, STR(g_rl_4d4258, 0x4d4258u, 0x4d4288u),
          g_gfx_line_x_acc + 104, ROW16 + 136, FONT, 63);
    g_gfx_line_x_acc = 0;
    wrap_line(71, 11, 104, ROW16 + 152, FONT, 63);
    label(AT(int32_t, g_rl_4d89b8, 0x4d89b8u, 0x4d8a0cu + 4 * CO8(g_rl_5681e0[0], 0x53fb70u)), 32,
          STR(g_rl_4d4258, 0x4d4258u, 0x4d428cu), g_gfx_line_x_acc + 104, ROW16 + 152, FONT, 63);
    wrap_line(71, 12, g_gfx_line_x_acc + 104, ROW16 + 152, FONT, 63);
    return 0;
}

/* ================================================================ the debug HUDs (MP network panel, unit inspector) */

static int min_tick_filtered(int a) { return (int)engine_retail_call(0x43e330u, 1, (uint32_t)a); }   /* TODO(rl-link): min_tick_filtered (engine_rate / engine_turn) */
#define D4458(va) STR(g_rl_4d4458, 0x4d4458u, va)   /* the MP panel's labels (DAT_004d4458..) */

/* Port of FUN_00423ba4 (3015 B) — the MP network debug panel at (x, y) (MP + DAT_0052eff8): a 258x130 frame on a
 * 16x128 black block, the two hashes (in 249 unless the player's id DAT_00553d50 + 44 p is the host DAT_004e2a28),
 * the message / timing / divergence counters, and per realm 1..5 its tick (DAT_0053e8d4..) in its colour (+8 when
 * +4) with the realm's sync bytes DAT_0056953a.. — in 63 when they agree, else 32 / 249 (retail's chained compare). */
int rl_423ba4(int x, int y)
{
    if (g_sim_mp_active == 0 || g_rl_52eff8 == 0) return 0;
    g_gfx_no_shadow = 1;
    mark_box(x, y, 17, 7, 1);
    g_gfx_glyph_w = 16;
    g_gfx_glyph_height_short = 128;
    gfx_line_rect(x, y, g_gfx_glyph_w * 16 + 2, g_gfx_glyph_height_short + 2, 249);
    gfx_sprite_fill_block(x + 1, y + 1, 24);
    int c = AT(int32_t, g_player_aux_44, 0x553d50u, 0x553d50u + 44 * s_slot_0057c8cc) == AT(int32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a28u) ? 63 : 249;
    g_gfx_line_x_acc = 0;
    label(AT(int32_t, g_player_aux_44, 0x553d50u, 0x553d7cu), 32, D4458(0x4d4458u), x + 8, y + 4, FONT_SMALL, c);
    label(AT(int32_t, g_player_aux_44, 0x553d50u, 0x553da8u), 32, D4458(0x4d445cu), x + g_gfx_line_x_acc + 8, y + 4, FONT_SMALL, c);
    label(g_rl_53e988, 32, D4458(0x4d4460u), x + 4, y + 16, FONT_SMALL, 63);
    label(g_rl_569580, 32, D4458(0x4d4468u), x + 60, y + 16, FONT_SMALL, 63);
    g_gfx_line_x_acc = 0;
    label(g_rl_4d2168, 32, D4458(0x4d4470u), x + 120, y + 16, FONT_SMALL, 63);
    label(g_rl_4d216c, 32, D4458(0x4d4474u), x + g_gfx_line_x_acc + 120, y + 16, FONT_SMALL, 63);
    label(g_rl_4d2160, 32, D4458(0x4d4478u), x + g_gfx_line_x_acc + 120, y + 16, FONT_SMALL, 63);
    label(g_bf_57c998[0], 32, D4458(0x4d447cu), x + 4, y + 28, FONT_SMALL, 63);
    label(g_sim_turn_progress, 32, D4458(0x4d4484u), x + 84, y + 28, FONT_SMALL, 63);
    label(g_rl_56d6a4, 32, D4458(0x4d4488u), x + 104, y + 28, FONT_SMALL, 63);
    label(g_bf_55403c, 32, D4458(0x4d448cu), x + 154, y + 28, FONT_SMALL, 63);
    label(g_rl_553018, 32, D4458(0x4d4494u), x + 204, y + 28, FONT_SMALL, 63);
    label(g_rl_554038, 32, D4458(0x4d449cu), x + 4, y + 40, FONT_SMALL, 63);
    label(g_rl_57cb3c, 32, D4458(0x4d44acu), x + 90, y + 40, FONT_SMALL, 63);
    label(g_rl_553f7c, 32, D4458(0x4d44b4u), x + 150, y + 40, FONT_SMALL, g_rl_553fa8 != 0 ? 249 : 63);
    if (g_rl_553214 != 0)
        label(g_rl_553214, 32, D4458(0x4d44bcu), x + 210, y + 40, FONT_SMALL, 249);
    else
        label(0, 32, D4458(0x4d44bcu), x + 210, y + 40, FONT_SMALL, 63);
    label(g_rl_512f6c, 32, D4458(0x4d44c4u), x + 4, y + 52, FONT_SMALL, 63);
    label(g_rl_512fa0, 32, D4458(0x4d44ccu), x + 64, y + 52, FONT_SMALL, 63);
    if (g_rl_51587c != 0)
        label(g_rl_51587c, 32, D4458(0x4d44d4u), x + 114, y + 52, FONT_SMALL, 249);
    else
        label(0, 32, D4458(0x4d44d4u), x + 114, y + 52, FONT_SMALL, 63);
    label(g_rl_544094, 32, D4458(0x4d44dcu), x + 210, y + 52, FONT_SMALL, 63);
    g_gfx_line_x_acc = 0;
    int t = min_tick_filtered(0);
    label(AT(int32_t, &g_bf_53e8d0, 0x53e8d0u, 0x53e8d0u + 4 * s_slot_0057c8cc) - t, 32, D4458(0x4d44e4u), x + 4, y + 64,
          FONT_SMALL, 63);
    label(g_rl_515908, 32, D4458(0x4d44ecu), x + 74, y + 64, FONT_SMALL, 63);
    label(g_rl_512fa8, 32, D4458(0x4d44f0u), x + 144, y + 64, FONT_SMALL, 63);
    label(g_rl_4d215c, 32, D4458(0x4d44f8u), x + 220, y + 64, FONT_SMALL, 63);
    int sc;
    if (((g_rl_569544 == g_rl_56953a) == g_rl_56954e) == g_rl_56954e)
        sc = 63;
    else if (g_rl_569543 == g_rl_56954d && g_rl_569557 == g_rl_569543 && g_rl_569543 == g_rl_569561 &&
             g_rl_569557 == g_rl_56954d && g_rl_56954d == g_rl_569561 && g_rl_569557 == g_rl_569561)
        sc = 249;
    else
        sc = 32;
    /* per realm: its tick in its colour, its two sync bytes (x / y offsets per slot) */
    static const struct { uint32_t tick, sfx_t, sfx_a, sfx_b; int tx, ty, ax, ay, bx; } k[5] = {
        { 0x53e8d4u, 0x4d44fcu, 0x4d4500u, 0x4d4504u,   4,  76,  70,  80,  60 },
        { 0x53e8d8u, 0x4d4508u, 0x4d450cu, 0x4d4510u, 140,  76, 205,  80, 195 },
        { 0x53e8dcu, 0x4d4514u, 0x4d4518u, 0x4d451cu,   4,  96,  70, 100,  60 },
        { 0x53e8e0u, 0x4d4520u, 0x4d4524u, 0x4d4528u, 140,  96, 205, 100, 195 },
        { 0x53e8e4u, 0x4d452cu, 0x4d4530u, 0x4d4534u,   4, 114,  70, 118,  60 },
    };
    const uint8_t sa[5] = { g_rl_56953a, g_rl_569544, g_rl_56954e, g_rl_569558, g_rl_569562 };
    const uint8_t sb[5] = { g_rl_569543, g_rl_56954d, g_rl_569557, g_rl_569561, g_rl_56956b };
    int32_t ticks[5] = { g_rl_53e8d4, g_rl_53e8d8, g_rl_53e8dc, g_rl_53e8e0, g_rl_53e8e4 };
    for (int p = 1; p <= 5; p++) {
        int col = PL8(p, 0x57bf04u) != 0 ? PL8(p, 0x57bf08u) : 32;
        label(ticks[p - 1], 32, D4458(k[p - 1].sfx_t), x + k[p - 1].tx, y + k[p - 1].ty, FONT, col);
        g_gfx_line_x_acc = 0;
        label(sa[p - 1], 32, D4458(k[p - 1].sfx_a), g_gfx_line_x_acc + x + k[p - 1].ax, y + k[p - 1].ay, FONT_SMALL, sc);
        label(sb[p - 1], 32, D4458(k[p - 1].sfx_b), g_gfx_line_x_acc + x + k[p - 1].bx, y + k[p - 1].ay, FONT_SMALL, sc);
    }
    label(g_bf_569510, 32, D4458(0x4d4538u), x + 140, y + 118, FONT_SMALL, 63);
    label(g_bf_569514, 32, D4458(0x4d453cu), x + 180, y + 118, FONT_SMALL, 63);
    label(g_bf_5694fc, 32, D4458(0x4d4540u), x + 220, y + 118, FONT_SMALL, 63);
    g_gfx_no_shadow = 0;
    return 0;
}

#define D4554(va) STR(g_rl_4d4554, 0x4d4554u, va)   /* the unit inspector's labels (DAT_004d4554..) */
#define L4554(va) ((const uint8_t *)D4554(va))

/* Port of FUN_00424992 (2434 B) — the battlefield unit inspector (DAT_0055402c, sub-state 2): a 10x384 block at
 * (480, 24) with unit DAT_00553e68's fields (position, target and its offset, routed, hold, state, route, hits,
 * walking, barred, target, direction, delay, delayed state, targeted, selection, polar direction, strafe) and its
 * group's (+0x178: re-target, position, target, orders, firing) as signed labels in the small font. */
int rl_424992(void)
{
    if (g_rl_55402c == 0 || s_slot_0057c8fc != 2) return 0;
    g_gfx_glyph_w = 10;
    g_gfx_glyph_height_short = 384;
    gfx_sprite_fill_block(480, 24, 34);
    mark_box(480, 24, 10, 24, 1);
    int u = g_bf_553e68;
    int grp = BS16(u, 0x5545f8);
    g_gfx_no_shadow = 1;
    g_gfx_line_x_acc = 0;
    text_line(L4554(0x4d4564u), 484, 28, FONT_SMALL, 63);
    label(u, 32, D4554(0x4d456cu), g_gfx_line_x_acc + 484, 28, FONT_SMALL, 63);
    label_ls(BS16(u, 0x5544a0), 2, L4554(0x4d4574u), D4554(0x4d4570u), 484, 44, FONT_SMALL, 63, 249);
    label_ls(BS16(u, 0x5544a2), 2, L4554(0x4d4580u), D4554(0x4d457cu), 484, 56, FONT_SMALL, 63, 249);
    g_gfx_line_x_acc = 0;
    label_ls(BS16(u, 0x5544a4), 2, L4554(0x4d458cu), D4554(0x4d4588u), 484, 68, FONT_SMALL, 63, 249);
    label_ls(BS16(u, 0x5544a4) - BS16(u, 0x5544a0), 1, L4554(0x4d4598u), D4554(0x4d4594u), g_gfx_line_x_acc + 484, 68,
             FONT_SMALL, 63, 249);
    g_gfx_line_x_acc = 0;
    label_ls(BS16(u, 0x5544a6), 2, L4554(0x4d45a0u), D4554(0x4d459cu), 484, 80, FONT_SMALL, 63, 249);
    label_ls(BS16(u, 0x5544a6) - BS16(u, 0x5544a2), 1, L4554(0x4d45acu), D4554(0x4d45a8u), g_gfx_line_x_acc + 484, 80,
             FONT_SMALL, 63, 249);
    label_ls(BS16(u, 0x5545e6), 2, L4554(0x4d45b4u), D4554(0x4d45b0u), 484, 92, FONT_SMALL, 63, 249);
    label_ls(BU8(u, 0x5545e5), 2, L4554(0x4d45c0u), D4554(0x4d45bcu), 484, 104, FONT_SMALL, 63, 249);
    label_ls(BS8(u, 0x5544b1), 2, L4554(0x4d45ccu), D4554(0x4d45c8u), 484, 116, FONT_SMALL, 63, 249);
    label_ls(BU8(u, 0x5545e4), 2, L4554(0x4d45d8u), D4554(0x4d45d4u), 484, 128, FONT_SMALL, 63, 249);
    label_ls(BS16(u, 0x55461a), 2, L4554(0x4d45e8u), D4554(0x4d45e4u), 484, 140, FONT_SMALL, 63, 249);
    label_ls(BS8(u, 0x5544b2), 2, L4554(0x4d45f4u), D4554(0x4d45f0u), 484, 152, FONT_SMALL, 63, 249);
    label_ls(BU8(u, 0x5545f6), 2, L4554(0x4d4600u), D4554(0x4d45fcu), 484, 164, FONT_SMALL, 63, 249);
    label_ls(BU8(u, 0x5545f4), 2, L4554(0x4d460cu), D4554(0x4d4608u), 484, 176, FONT_SMALL, 63, 249);
    label_ls(BS8(u, 0x554498), 2, L4554(0x4d4618u), D4554(0x4d4614u), 484, 188, FONT_SMALL, 63, 249);
    label_ls(BS8(u, 0x5544b0), 2, L4554(0x4d4624u), D4554(0x4d4620u), 484, 200, FONT_SMALL, 63, 249);
    label_ls(BS8(u, 0x5544af), 2, L4554(0x4d4630u), D4554(0x4d462cu), 484, 212, FONT_SMALL, 63, 249);
    label_ls(BS8(u, 0x5545f5), 2, L4554(0x4d4640u), D4554(0x4d463cu), 484, 224, FONT_SMALL, 63, 249);
    label_ls(BU8(u, 0x554489), 2, L4554(0x4d4650u), D4554(0x4d464cu), 484, 236, FONT_SMALL, 63, 249);
    label_ls(BU8(u, 0x55448a), 2, L4554(0x4d4660u), D4554(0x4d465cu), 484, 248, FONT_SMALL, 63, 249);
    label_ls(BU8(u, 0x5545e8), 2, L4554(0x4d4670u), D4554(0x4d466cu), 484, 260, FONT_SMALL, 63, 249);
    label_ls(BS16(u, 0x554624), 2, L4554(0x4d4680u), D4554(0x4d467cu), 484, 272, FONT_SMALL, 63, 249);
    g_gfx_line_x_acc = 0;
    text_line(L4554(0x4d468cu), 484, 304, FONT_SMALL, 63);
    label(grp, 32, D4554(0x4d4694u), g_gfx_line_x_acc + 484, 304, FONT_SMALL, 63);
    label_ls(BG16(grp, 0x566534), 2, L4554(0x4d469cu), D4554(0x4d4698u), 484, 320, FONT_SMALL, 63, 249);
    label_ls(BG16(grp, 0x56653e), 2, L4554(0x4d46a8u), D4554(0x4d46a4u), 484, 332, FONT_SMALL, 63, 249);
    label_ls(BG16(grp, 0x566540), 2, L4554(0x4d46b4u), D4554(0x4d46b0u), 484, 344, FONT_SMALL, 63, 249);
    label_ls(BG16(grp, 0x566542), 2, L4554(0x4d46c0u), D4554(0x4d46bcu), 484, 356, FONT_SMALL, 63, 249);
    label_ls(BG16(grp, 0x566544), 2, L4554(0x4d46ccu), D4554(0x4d46c8u), 484, 368, FONT_SMALL, 63, 249);
    label_ls(BG16(grp, 0x56653a), 2, L4554(0x4d46d8u), D4554(0x4d46d4u), 484, 380, FONT_SMALL, 63, 249);
    label_ls(BG8(grp, 0x56652f), 2, L4554(0x4d46e4u), D4554(0x4d46e0u), 484, 392, FONT_SMALL, 63, 249);
    g_gfx_no_shadow = 0;
    return 0;
}
