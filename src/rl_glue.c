/* rl_glue.c — a retail-layout port, partition 'glue' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * docs/findings/rl-glue.md: the partition's notes (quirks, seams, evidence). */
#include "rl.h"
#include "rl_glue_globals.h"
#include "engine_bf_input_globals.h"   /* the input partition's globals (the cursor / button edges, the router param) */
#include "engine.h"                    /* g_sim_mp_active, the screen / redraw bridges */
#include "engine_combat.h"             /* g_combat_flag_530d0 / _567d0c (DAT_005530d0 / DAT_00567d0c) */
#include "engine_territory.h"          /* the bound dirty state (DAT_0053e9ac …), 0x45240a, 0x43278b */
#include "engine_map_query.h"          /* g_cell_cursor_col / _row: DAT_00569570 / DAT_00569578 (rhost: a synced mirror) */
#include "engine_army_ai.h"            /* the ranking-dirty flag DAT_0056d6a0 (rhost: a synced mirror) */
#include "engine_diplomacy_action.h"   /* g_diplo_action_mode (DAT_005651cc) */
#include "engine_minimap.h"            /* g_minimap_region_grid (DAT_0052aff0) */
#include "engine_turn.h"               /* g_sim_turn_progress (DAT_00553fc8) */
#include "engine_camera.h"             /* engine_camera_clamp_anchor (0x47ed34) */
#include "engine_dirty_rect.h"         /* engine_dirty_rect_mark_box_tiled (0x4527a6) */
#include "engine_army_economy.h"       /* the realm plan (0x44df5f, 0x4485a5) */
#include "engine_diplomacy.h"          /* engine_diplomacy_form_alliance (0x4a1774) */
#include "engine_sendsupplies.h"       /* g_castle_tier (DAT_0056d898) */
#include "strutil.h"                   /* strutil_bounded_copy (0x401136; the port takes (dst, src, n)) */

#include <string.h>
#include <windows.h>
#include <mmsystem.h>

/* ---------------------------------------------------------------- retail records (save block) */
#define PL8(p, va)   AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))   /* realm record */
#define PX8(p, va)   AT(uint8_t, g_player_aux_44, 0x553d50u, (int32_t)(va) + 44 * (p))            /* realm aux */
#define ARMY8(i, va) AT(uint8_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (i))           /* county */
#define TER8(i, va)  AT(uint8_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))      /* army */
#define TER16(i, va) AT(int16_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
#define TER32(i, va) AT(int32_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
#define ARMY16(i, va) AT(int16_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (i))
#define ARMY32(i, va) AT(int32_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (i))

/* ---------------------------------------------------------------- retail globals through their owners */
#define PARAM0   AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu)   /* DAT_0059154c: the button router's param0 */
#define LOCAL    ((int)s_slot_0057c8cc)                           /* DAT_0057c8cc: the local player */
#define MP_ON    (g_sim_mp_active != 0)                           /* DAT_00553030 */
#define BELOW_STATUS() (engine_territory_dirty_state()->below_status_redraw)   /* DAT_0053e9ac */

/* Port of FUN_0041423f (11 B) — an empty frame-driver step (retail: prologue / epilogue only). */
int rl_41423f(void)
{
    return 0;
}

/* Port of FUN_0042e26f (41 B) — "a movie is playing": DAT_00516a4c != 0. */
int rl_42e26f(void)
{
    return g_rl_516a4c != 0;
}

/* Port of FUN_004360f2 (41 B) — clear the popup's hover / scroll state (DAT_0057a0cc, DAT_005cd41c); redraw 2. */
int rl_4360f2(void)
{
    g_rl_57a0cc = 0;
    g_rl_5cd41c = 0;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_0043496c (28 B) — a router action: screen 0x42; redraw 2. */
int rl_43496c(void)
{
    engine_set_current_screen(66);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00434988 (28 B) — a router action: screen 0x43; redraw 2. */
int rl_434988(void)
{
    engine_set_current_screen(67);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_0040dfcd (61 B) — close the pull-down menu: the open item / hovered row / menu table
 * (DAT_00522cb0 / DAT_00522cb4 / DAT_00522cac) cleared; the ranking redraw DAT_0056d6a0 = 1, redraw 2. */
int rl_40dfcd(void)
{
    g_rl_522cb0 = 0;
    g_rl_522cb4 = 0;
    g_rl_522cac = 0;
    engine_army_ai_set_ranking_dirty(1);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00451f8a (66 B) — full repaint: the whole screen dirty (0x45240a), the below-status redraw
 * DAT_0053e9ac = 1, redraw 1, and the map / focus / ranking redraw flags (DAT_00553ed8, DAT_005530d0,
 * DAT_0056d6a0) set. */
int rl_451f8a(void)
{
    engine_territory_mark_full_screen_dirty();
    BELOW_STATUS() = 1;
    engine_set_demand_redraw(1);
    g_bf_553ed8 = 1;
    g_combat_flag_530d0 = 1;
    engine_army_ai_set_ranking_dirty(1);
    return 0;
}

/* Port of FUN_00472986 (63 B) — split the hovered cell index DAT_00568974 by the row pitch DAT_0057d338:
 * DAT_0057a0e0 = the quotient, then by DAT_0052afd4: DAT_0057d330 = remainder, DAT_0057d334 = quotient
 * (signed idiv). Returns the last quotient (retail's EAX). */
int rl_472986(void)
{
    g_rl_57a0e0 = g_rl_568974 / g_rl_57d338;
    g_rl_57d330 = g_rl_57a0e0 % g_rl_52afd4;
    g_rl_57d334 = g_rl_57a0e0 / g_rl_52afd4;
    return g_rl_57d334;
}

/* Port of FUN_00433a70 (306 B) — the options screen's tab `k` (0..11) → its setting (the lords2.inf block
 * DAT_0053f288..0053f2b4 in retail's permuted order). Any other k returns the caller's EAX in retail
 * (unmodelled: 0 here; the one caller passes 0..11). */
int rl_433a70(int k)
{
    static const uint16_t va_lo[12] = { 0xf29c, 0xf2a4, 0xf288, 0xf2a0, 0xf290, 0xf298,
                                        0xf2b0, 0xf294, 0xf2a8, 0xf2ac, 0xf28c, 0xf2b4 };
    if (k < 0 || k > 11)
        return 0;
    return AT(int32_t, g_rl_53f288, 0x53f288u, 0x530000u + va_lo[k]);
}

/* Port of FUN_004330c6 (143 B) — the local player's shield colour from the new-game colour picks: for each
 * slot 1..5 the local player picked (DAT_0057cb40[slot]), realm aux +0x25 and realm record +0xa = the slot's
 * colour (DAT_004d5548[4 * slot]). Returns retail's EAX (char: the last index, or the colour byte over
 * 44 * local's upper bytes). */
int rl_4330c6(void)
{
    int r = 0;
    for (int i = 1; i < 6; i++) {
        r = i;
        if (AT(uint8_t, g_rl_57cb40, 0x57cb40u, 0x57cb40u + i) == (uint32_t)LOCAL) {
            PX8(LOCAL, 0x553d75) = AT(uint8_t, g_rl_4d5508, 0x4d5508u, 0x4d5548u + 4 * i);
            uint8_t c = PX8(LOCAL, 0x553d75);
            r = (int)(((uint32_t)(44 * LOCAL) & ~0xffu) | c);
            PL8(LOCAL, 0x57bf0a) = c;
        }
    }
    return r;
}

/* Port of FUN_0040e00a (143 B) — the pull-down menu bar's hit test: the first of `n` 16-B titles (int16 x0 +0,
 * x1 +2, y +4; 12 px tall) under the cursor (DAT_004e65fc, DAT_004e65f4), 1-based; 0 = none. */
int rl_40e00a(int titles, int n)
{
    const int16_t *t = (const int16_t *)(uintptr_t)titles;
    for (int i = 1; i <= n; i++) {
        if (t[0] <= g_bf_4e65fc && t[1] > g_bf_4e65fc && t[2] <= g_bf_4e65f4 && t[2] + 12 > g_bf_4e65f4)
            return i;
        t += 8;
    }
    return 0;
}

/* Port of FUN_0040e099 (149 B) — an open pull-down's hit test: the first of `n` 12-B items (int16 y offset +0;
 * 144 x 15 px at (x, y + 31 + off)) under the cursor, 1-based; 0 = none. */
int rl_40e099(int items, int n, int x, int y)
{
    const int16_t *t = (const int16_t *)(uintptr_t)items;
    for (int i = 1; i <= n; i++) {
        int top = y + 31 + t[0];
        if (g_bf_4e65fc >= x && x + 144 > g_bf_4e65fc && top <= g_bf_4e65f4 && top + 15 > g_bf_4e65f4)
            return i;
        t += 6;
    }
    return 0;
}

/* Port of FUN_0043b4cb (200 B) — the diplomacy screen's county map (modes 5+, DAT_005651cc): a press inside
 * the 128 × 128 thumbnail at (x, y) picks the county under the cursor (the region grid DAT_0052aff0) into
 * DAT_005530b0; returns whether one was picked. */
int rl_43b4cb(int x, int y)
{
    if (g_diplo_action_mode < 5)
        return 0;
    if (g_bf_4eafb4 == 0)
        return 0;
    if (x > g_bf_4e65fc || x + 128 <= g_bf_4e65fc)
        return 0;
    if (y > g_bf_4e65f4 || y + 128 <= g_bf_4e65f4)
        return 0;
    s_slot_005530b0 = g_minimap_region_grid[((g_bf_4e65f4 - y) << 7) - x + g_bf_4e65fc];
    return s_slot_005530b0 != 0;
}

/* Port of FUN_004393eb (342 B) — the workforce grid's drag start: a press over the grid (x 0..511, y from
 * DAT_0057c908, 376 px) arms the drag (DAT_004e59c8, the press point DAT_004e59cc / 004e59d0); once the cursor
 * is more than 8 px away on either axis the drag starts (its origin → DAT_005530fc / 005530f4, disarmed):
 * returns 1 (→ screen 5), else 0. */
int rl_4393eb(void)
{
    if (g_rl_4e59c8 == 0) {
        if (g_bf_4e65fc < 0)
            return 0;
        if (g_bf_4e65fc >= 512)
            return 0;
        if (g_rl_57c908 > g_bf_4e65f4)
            return 0;
        if (g_rl_57c908 + 376 <= g_bf_4e65f4)
            return 0;
        if (g_bf_4eafb4 == 0)
            return 0;
        g_rl_4e59c8 = 1;
        g_rl_4e59cc = g_bf_4e65fc;
        g_rl_4e59d0 = g_bf_4e65f4;
    }
    int dx = g_rl_4e59cc < g_bf_4e65fc ? g_bf_4e65fc - g_rl_4e59cc : g_rl_4e59cc - g_bf_4e65fc;
    int dy = g_rl_4e59d0 < g_bf_4e65f4 ? g_bf_4e65f4 - g_rl_4e59d0 : g_rl_4e59d0 - g_bf_4e65f4;
    if (dx <= 8 && dy <= 8)
        return 0;
    g_rl_5530fc = g_rl_4e59cc;
    g_rl_5530f4 = g_rl_4e59d0;
    g_rl_4e59c8 = 0;
    return 1;
}

/* Port of FUN_00439079 (169 B) — a right click over the minimap (x >= 478, y 24..152) while the army route
 * overlay is up (DAT_0057a0c4) drops it (DAT_0057a0c4 / DAT_0053e920 = 0), redraw 2, combat redraw
 * DAT_00567d0c = 1: returns 1 (consumed), else 0. */
int rl_439079(void)
{
    if (g_bf_4e65fc < 478)
        return 0;
    if (g_bf_4e65f4 < 24)
        return 0;
    if (g_bf_4e65f4 > 152)
        return 0;
    if (g_bf_4e6900 == 0)
        return 0;
    if (s_slot_0057a0c4 == 0)
        return 0;
    s_slot_0057a0c4 = 0;
    s_slot_0053e920 = 0;
    engine_set_demand_redraw(2);
    g_combat_flag_567d0c = 1;
    return 1;
}

/* Port of FUN_0046c4a3 (277 B) — mark the w × h cell box at (col, row) of the battlefield grid for redraw:
 * cell byte +2 bit 0 (cells off the 80 × 80 grid skipped). The walk uses retail's globals: the cell offset
 * DAT_0053e8b8 and the cell cursor DAT_00569570 / 00569578. Returns row + h (retail's EAX). */
int rl_46c4a3(int col, int row, int w, int h)
{
    g_bf_53e8b8 = 8 * (col + 80 * row);
    for (g_cell_cursor_row = row; row + h > g_cell_cursor_row; g_cell_cursor_row++) {
        for (g_cell_cursor_col = col; col + w > g_cell_cursor_col; g_cell_cursor_col++) {
            if ((uint32_t)g_cell_cursor_row < 80 && (uint32_t)g_cell_cursor_col < 80 && (uint32_t)g_bf_53e8b8 < 0xc800)
                BCELL8(g_bf_53e8b8, 0x5440e2) |= 1;
            g_bf_53e8b8 += 8;
        }
        g_bf_53e8b8 += 8 * (80 - w);
    }
    return row + h;
}

/* ---------------------------------------------------------------- seams: retail functions this partition calls */
/* the hotspot router 0x40e3ee(x, y, table, n) / the button-record router 0x40da1e(x, y, table, n): retail tables
 * of retail action VAs (engine_menu_input.c ports both over port structs and an input snapshot: TODO(rl-link)) */
static int hotspot_click(int x, int y, const void *table, int n)
{ return rl_40e3ee(x, y, (int)(uintptr_t)table, n); }
static int button_click(int x, int y, const void *table, int n)
{ return rl_40da1e(x, y, (int)(uintptr_t)table, n); }
/* the panel backdrop strips 0x4b3f0a (capture) / 0x4b3ec0 (restore) (buf, height) on retail's blit globals
 * (engine_workforce_hud.c, not linked: TODO(rl-link)) */
static int backdrop_capture(int buf, int h) { return (int)engine_retail_call(0x4b3f0au, 2, (uint32_t)buf, (uint32_t)h); }
static int backdrop_restore(int buf, int h) { return (int)engine_retail_call(0x4b3ec0u, 2, (uint32_t)buf, (uint32_t)h); }
/* the confirm dialog 0x40e6f2(msg, x, y, callback VA): retail keeps the callback's ADDRESS — retail's VA here
 * (rl_40e6f2, rl ui4) */
static int confirm(int msg, uint32_t cb_va)
{ return rl_40e6f2(msg, 160, 160, (int)cb_va); }

/* Port of FUN_004323fe (69 B) — the minimap press: the strategic map's (sub-state 0: 0x43253a) or the
 * battlefield's (2: 0x432443). Returns whether it consumed the press. */
int rl_4323fe(void)
{
    if (s_slot_0057c8fc == 0)
        return rl_43253a();
    if (s_slot_0057c8fc == 2)
        return rl_432443();
    return 0;
}

/* Port of FUN_00432443 (247 B) — the battlefield minimap (x 480..639, y 24..183, 2 px a cell): the whole screen
 * dirty, then an order to the clicked cell (0x43c634) — or, with the right button held (DAT_004eabe0), nothing
 * of the player's selected (DAT_00553078), a siege engine selection (DAT_0053e8bc) or the pause (DAT_0053f238),
 * the camera recentred there (DAT_0055ce70 / 005678a0 = cell - 7, clamped 0x47ed34). Returns 1 inside. */
int rl_432443(void)
{
    if (g_bf_4e65fc < 480 || g_bf_4e65fc >= 640)
        return 0;
    if (g_bf_4e65f4 < 24 || g_bf_4e65f4 >= 184)
        return 0;
    engine_territory_mark_full_screen_dirty();
    int col = (g_bf_4e65fc - 480) / 2;
    int row = (g_bf_4e65f4 - 24) / 2;
    if (g_rl_4eabe0 != 0 || g_bf_553078 == 0 || s_slot_0053e8bc != 0 || g_bf_53f238 != 0) {
        s_slot_0055ce70 = (uint32_t)(col - 7);
        s_slot_005678a0 = (uint32_t)(row - 7);
        engine_camera_clamp_anchor((int *)&s_slot_0055ce70, (int *)&s_slot_005678a0);
        return 1;
    }
    bf_43c634(col, row);
    return 1;
}

/* Port of FUN_0043253a (262 B) — the strategic minimap (x 480..607, y 25..152, 1 px a county cell): not with the
 * right button held or on screens 5 / 6; the county under the cursor (the region grid DAT_0052aff0) with a seat
 * (county +0x70 → DAT_0053f0dc) becomes the current county DAT_0057c93c, the map recentres on its seat
 * (0x43278b) and the combat redraw DAT_00567d0c = 1. Returns 1 inside the minimap. */
int rl_43253a(void)
{
    if (g_rl_4eabe0 != 0)
        return 0;
    if (engine_current_screen() == 5 || engine_current_screen() == 6)
        return 0;
    if (g_bf_4e65fc < 480 || g_bf_4e65fc >= 608)
        return 0;
    if (g_bf_4e65f4 < 25 || g_bf_4e65f4 >= 153)
        return 0;
    int c = g_minimap_region_grid[((g_bf_4e65f4 - 25) << 7) + g_bf_4e65fc - 480];   /* IDA: byte_52AE10[...] */
    if (c != 0) {
        g_rl_53f0dc = ARMY32(c, 0x53fa20);
        if (g_rl_53f0dc != 0) {
            s_slot_0057c93c = (uint32_t)c;
            engine_territory_recenter_on_tile(g_rl_53f0dc);
            g_combat_flag_567d0c = 1;
        }
    }
    return 1;
}

/* Port of FUN_00432893 (154 B) — dirty the screen box of HUD panel `k` (the 16-B rectangles x0 / x1 / y0 / y1
 * at DAT_004d5508): from (x0 - 32, y0 - 32), (x1 + 10 - (x0 - 32)) / 16 + 2 by (y1 + 10 - (y0 - 32)) / 16 + 2
 * tiles (signed / 16), escalation 1 (0x4527a6). */
int rl_432893(int k)
{
    int x0 = AT(int32_t, g_rl_4d5508, 0x4d5508u, 0x4d5508u + 16 * k) - 32;
    int x1 = AT(int32_t, g_rl_4d5508, 0x4d5508u, 0x4d550cu + 16 * k) + 10;
    int y0 = AT(int32_t, g_rl_4d5508, 0x4d5508u, 0x4d5510u + 16 * k) - 32;
    int y1 = AT(int32_t, g_rl_4d5508, 0x4d5508u, 0x4d5514u + 16 * k) + 10;
    engine_dirty_rect_mark_box_tiled(engine_territory_dirty_state(), x0, y0, (x1 - x0) / 16 + 2, (y1 - y0) / 16 + 2, 1);
    return 0;
}

/* Port of FUN_0043292d (58 B) — the strategic HUD's top-right buttons (4 hotspots at DAT_004dc620, origin
 * (610, 32); 0x40e3ee). Returns whether one fired. */
int rl_43292d(void)
{
    return hotspot_click(610, 32, g_rl_4dc620, 4) != 0;
}

/* Port of FUN_004329a4 (61 B) — the strategic HUD's bottom-right buttons (5 hotspots at DAT_004dc710, origin
 * (480, 448)). Returns whether one fired. */
int rl_4329a4(void)
{
    return hotspot_click(480, 448, g_rl_4dc710, 5) != 0;
}

/* Port of FUN_00437002 (261 B) — the selected army's panel buttons: with an army selected (DAT_0052afbc, or a
 * pending DAT_0052afb4) that is a field army (+8 == 1) of the local player (+0 signed), its 3 hotspots (in a
 * castle +0x198: DAT_004dc5a8, else DAT_004dc560) at row DAT_00553d2c. Returns whether one fired. */
int rl_437002(void)
{
    if (g_rl_52afbc == 0 && g_rl_52afb4 == 0)
        return 0;
    if (TER8(g_rl_52afbc, 0x52f0b8) != 1)
        return 0;
    if ((int8_t)TER8(g_rl_52afbc, 0x52f0b0) != LOCAL)
        return 0;
    if (TER8(g_rl_52afbc, 0x52f248) != 0) {
        if (hotspot_click(0, 16 * g_rl_553d2c, g_rl_4dc5a8, 3) != 0)
            return 1;
    } else if (hotspot_click(0, 16 * g_rl_553d2c, g_rl_4dc4d0 + (0x4dc560 - 0x4dc4d0), 3) != 0) {
        return 1;
    }
    return 0;
}

/* Port of FUN_00438990 (257 B) — the county panel's buttons: with the panel's tab bit 0x20 (DAT_005440b0), no
 * army selected, the local player owning the county shown (DAT_005681d0) and not the overlays 23 / 24
 * (DAT_0057cb2c): a county view 1..24 → its 3 hotspots (DAT_004dc4d0, row DAT_00553d2c + 7), else (0, 25+) the
 * 2 at DAT_004dc530 (row DAT_00553d2c). Returns whether one fired. */
int rl_438990(void)
{
    if ((g_rl_5440b0 & 0x20) == 0)
        return 0;
    if (g_rl_52afbc != 0 || g_rl_52afb4 != 0)
        return 0;
    if (LOCAL != g_rl_5681d0)
        return 0;
    if (g_rl_57cb2c == 23)
        return 0;
    if (g_rl_57cb2c == 24)
        return 0;
    if (g_rl_57cb2c != 0 && g_rl_57cb2c < 25) {
        if (hotspot_click(0, 16 * (g_rl_553d2c + 7), g_rl_4dc4d0, 3) != 0)
            return 1;
    } else if (hotspot_click(0, 16 * g_rl_553d2c, g_rl_4dc4d0 + (0x4dc530 - 0x4dc4d0), 2) != 0) {
        return 1;
    }
    return 0;
}

/* Port of FUN_00438a91 (59 B) — the county panel castle box's builder-army button (0x438acc): DAT_00568474 button records at DAT_004dd640,
 * origin (8, 32) (0x40da1e). Returns whether one fired. */
int rl_438a91(void)
{
    return button_click(8, 32, g_rl_4dd640, g_rl_568474) != 0;
}

/* Port of FUN_0043933b (176 B) — set county `c`'s tax rate (+8, a char) and re-plan its realm: the workforce
 * (0x44f6e7), provisioning (0x44df5f, phase DAT_0057c934) and the realm plan (0x4485a5, DAT_0057c92c); focus
 * redraw DAT_005530d0 = 1, and for the current county owned locally the panel / combat redraws (DAT_004eb260,
 * DAT_00567d0c). Returns retail's EAX (DAT_0057c93c, << 8 when it is c). */
int rl_43933b(int c, int rate)
{
    ARMY8(c, 0x53f9b8) = (uint8_t)rate;
    engine_territory_allocate_realm_workforce(c);
    engine_army_economy_provision(c, (int)s_slot_0057c934);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    g_combat_flag_530d0 = 1;
    int r = (int)s_slot_0057c93c;
    if (c == (int)s_slot_0057c93c) {
        r = (int)s_slot_0057c93c << 8;
        if (ARMY8(s_slot_0057c93c, 0x53f9b5) == LOCAL) {
            g_rl_4eb260 = 1;
            g_combat_flag_567d0c = 1;
        }
    }
    return r;
}

/* Port of FUN_00439122 (537 B) — the county panel's tax slider (x 478..639, y 257..296) on a county the local
 * player owns, the turn idle (DAT_00553fc8 == 0; in MP DAT_0055403c without DAT_00553018 blocks): a held left
 * button (DAT_004e65d8) is consumed as is; a press repeat (DAT_004e65cc with DAT_004ea4b0) moves the rate
 * (+8, signed) DAT_00553534: left of 531 −4, right of 593 +4, else 2 (x − 531) & 0xfc; clamped 0..100; a change
 * is applied (MP: message 0x27; else 0x43933b). Returns 1 when consumed. */
int rl_439122(void)
{
    if (g_sim_turn_progress != 0 || (g_bf_55403c != 0 && g_rl_553018 == 0))
        return 0;
    if (ARMY8(s_slot_0057c93c, 0x53f9b5) != LOCAL)
        return 0;
    if (g_bf_4e65fc < 478)
        return 0;
    if (g_bf_4e65fc >= 640)
        return 0;
    if (g_bf_4e65f4 < 257)
        return 0;
    if (g_bf_4e65f4 >= 297)
        return 0;
    if (g_bf_4e65d8 != 0)
        return 1;
    if (g_bf_4e65cc == 0)
        return 0;
    if (g_bf_4ea4b0 == 0)
        return 0;
    int old = (int8_t)ARMY8(s_slot_0057c93c, 0x53f9b8);
    g_rl_553534 = old;
    if (g_bf_4e65fc < 531) {
        g_rl_553534 -= 4;
    } else if (g_bf_4e65fc > 593) {
        g_rl_553534 += 4;
    } else {
        g_rl_553534 = 2 * (g_bf_4e65fc - 531);
        g_rl_553534 &= 0xfc;
    }
    if (g_rl_553534 < 0)
        g_rl_553534 = 0;
    else if (g_rl_553534 > 100)
        g_rl_553534 = 100;
    if (g_rl_553534 == old)
        return 1;
    if (MP_ON)
        rl_43eda0(39, 0);
    else
        rl_43933b((int)s_slot_0057c93c, g_rl_553534);
    return 1;
}

/* Port of FUN_0040deca (152 B) — open the pull-down menu bar: a press on one of the `n` titles (0x40e00a)
 * clears the popup (0x4360f2), saves the screen (DAT_0056d894), shows screen 0x32 with that menu (DAT_00522cb4
 * the hovered title, DAT_00522cac its 16-B record, no item DAT_00522cb0) and captures the backdrop (0x40c8d1);
 * ranking redraw DAT_0056d6a0. Returns 1 when opened. */
int rl_40deca(int titles, int n)
{
    if (g_bf_4eafb4 == 0)
        return 0;
    g_rl_522cb4 = rl_40e00a(titles, n);
    if (g_rl_522cb4 == 0)
        return 0;
    rl_4360f2();
    g_rl_56d894 = engine_current_screen();
    engine_set_current_screen(50);
    g_rl_522cb0 = 0;
    g_rl_522cac = titles + 16 * g_rl_522cb4 - 16;
    rl_40c8d1();
    engine_army_ai_set_ranking_dirty(1);
    return 1;
}

/* Port of FUN_0040dd92 (312 B) — the open pull-down (screen 0x32): a press runs the hovered item (the menu
 * record's item table +8, 12-B items: the action at +4 — called through its retail address) after restoring
 * the screen (DAT_0056d894) and closes the menu (0x40dfcd), or with no item closes it (0x40df62): returns 1.
 * Otherwise hovering another title (0x40e00a) switches menus (DAT_00522cb4 / 00522cac, item 0, ranking redraw,
 * redraw 2) and the hovered item DAT_00522cb0 is re-hit-tested (0x40e099): returns 0. */
int rl_40dd92(int titles, int n)
{
    if (g_bf_4eafb4 != 0) {
        if (g_rl_522cb0 != 0) {
            g_rl_4eb260 = 1;
            g_rl_4e65f8 = 1;
            engine_set_current_screen((uint8_t)g_rl_56d894);
            g_rl_522ca8 = *(const int32_t *)(uintptr_t)(g_rl_522cac + 8);   /* the menu record's item table */
            g_rl_522ca8 += 4 * (3 * g_rl_522cb0 - 3);
            engine_retail_call(*(const uint32_t *)(uintptr_t)(g_rl_522ca8 + 4), 0);   /* the item's action */
            rl_40dfcd();
        } else {
            rl_40df62();   /* close the menu (partition ui3) */
        }
        return 1;
    }
    int t = rl_40e00a(titles, n);
    if (t != g_rl_522cb4 && t != 0) {
        g_rl_4eb260 = 1;
        g_rl_4e65f8 = 1;
        g_rl_522cb4 = t;
        g_rl_522cac = titles + 16 * g_rl_522cb4 - 16;
        g_rl_522cb0 = 0;
        engine_army_ai_set_ranking_dirty(1);
        engine_set_demand_redraw(2);
    }
    const uint8_t *rec = (const uint8_t *)(uintptr_t)g_rl_522cac;
    g_rl_522cb0 = rl_40e099(*(const int32_t *)(rec + 8), *(const int16_t *)(rec + 12), *(const int16_t *)rec,
                            *(const int16_t *)(rec + 4));
    return 0;
}

/* Port of FUN_0040c8d1 (87 B) — capture the pull-down's backdrop: panel / redraw flags DAT_004eb260 /
 * DAT_004e65f8, the blit box (0, 24) 100 × 180 (DAT_00591524 / 591528, DAT_005c9258 / 5aeb70) out of the back
 * buffer into DAT_004eabec, 240 rows (0x4b3f0a). */
int rl_40c8d1(void)
{
    g_rl_4eb260 = 1;
    g_rl_4e65f8 = 1;
    g_rl_591524 = 0;
    g_rl_591528 = 24;
    g_rl_5c9258 = 100;
    g_rl_5aeb70 = 180;
    return backdrop_capture(g_bf_4eabec, 240);
}

/* Port of FUN_0040c928 (68 B) — restore the pull-down's backdrop (the box of 0x40c8d1, 0x4b3ec0). */
int rl_40c928(void)
{
    g_rl_591524 = 0;
    g_rl_591528 = 24;
    g_rl_5c9258 = 100;
    g_rl_5aeb70 = 180;
    return backdrop_restore(g_bf_4eabec, 240);
}

/* Port of FUN_0040e761 (131 B) — open the slider dialog (screen 0x21, the previous one kept in DAT_005c9278):
 * kind `kind` (DAT_0058fe1c) over the setting at address `value` (DAT_005cd410), range lo..hi (DAT_0058fd64 /
 * 005c9260), step DAT_005c927c, at (x, y) (DAT_0058fe20 / 58fe24), flag DAT_005aeb74; the answer DAT_0059151c
 * cleared; its render 0x40cd58, the whole screen dirty, below-status redraw 1, redraw 2. */
int rl_40e761(int kind, int value, int lo, int hi, int step, int x, int y, int flag)
{
    g_rl_5c9278 = engine_current_screen();
    engine_set_current_screen(33);
    g_bf_59151c[0] = 0;
    g_rl_58fe20 = x;
    g_rl_58fe24 = y;
    g_rl_5cd410 = value;
    g_rl_58fd64 = lo;
    g_rl_5c9260 = hi;
    g_rl_5c927c = step;
    g_rl_5aeb74 = flag;
    g_rl_58fe1c = kind;
    engine_retail_call(0x40cd58u, 0);   /* TODO(rl-extern): 0x40cd58 (the slider dialog's render) */
    engine_territory_mark_full_screen_dirty();
    BELOW_STATUS() = 1;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00434cbe (48 B) — options: the music volume slider (kind 1 over DAT_0053f230, 10..100, at 160, 160). */
int rl_434cbe(void)
{
    return rl_40e761(1, (int)(uintptr_t)g_rl_53f230, 10, 100, 0, 160, 160, 1);
}

/* Port of FUN_00434cee (48 B) — options: the sound volume slider (kind 2 over DAT_0053f234). */
int rl_434cee(void)
{
    return rl_40e761(2, (int)(uintptr_t)(g_rl_53f230 + 4), 10, 100, 0, 160, 160, 1);
}

/* ---------------------------------------------------------------- more seams */
/* the event queue post 0x472bc5 */
static int evq_post(int a, int b, int code, int d, int e, int f, int g, int h) { return rl_472bc5(a, b, code, d, e, f, g, h); }
/* the line editor 0x402009(buf, max, width px, flag) / 0x401f49() on retail's editor globals (text_input.c works on a
 * port struct: TODO(rl-link)) */
static void editor_open(const void *buf, int max, int w, int flag)
{ rl_402009((int)(uintptr_t)buf, max, w, flag); }
static void editor_length(void) { rl_401f49(); }
/* unported or unlinked retail helpers */
static int map_scene_tick(void) { return (int)engine_retail_call(0x4050c0u, 0); }   /* TODO(rl-link): engine_map_scene_tick */
static int popup_dismiss(void) { return rl_476768(); }    /* engine_popup_scroll_dismiss (rl turn) */
static int cursor_in(int x, int y, int w, int h)   /* engine_popup_cursor_in_rect 0x4b1deb (rl_host) */
{ return rl_4b1deb(x, y, w, h); }
static int strategic_view(void) { return rl_4984dc(); }  /* the season's sheets 0x4984dc (rl turn) */

/* the save / load dialogs' file mask (DAT_004d5710..: *.sva / *.svb / *.sav, twice) */
#define MASK(k) (g_rl_4d5710 + 8 * (k))

/* Port of FUN_00433e93 (182 B) — the menu bar's Save: list the saves (0x4aecf0: MP host *.sva, MP client *.svb,
 * else *.sav), open the name editor on DAT_004ea130 (8 chars, 160 px), keep the screen (DAT_0057c90c) and show
 * the save dialog 0x35; the list scroll / pick (DAT_0057d3c4, DAT_00591548) reset; redraw 2. */
int rl_433e93(void)
{
    g_rl_57d3c4 = 0;
    g_rl_591548 = 0;
    if (MP_ON)
        rl_4aecf0((int)(uintptr_t)MASK(g_rl_553248 != 0 ? 0 : 1), 0);
    else
        rl_4aecf0((int)(uintptr_t)MASK(2), 0);
    editor_open(&g_rl_4ea130, 8, 160, 1);
    editor_length();
    g_bf_57c90c = engine_current_screen();
    engine_set_current_screen(53);
    g_rl_57d3c4 = 0;
    g_rl_591548 = 0;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00433f49 (194 B) — the menu bar's Load: as Save (0x433e93) with the second masks, the screen kept
 * in DAT_0057c90c and DAT_004eafb0, dialog 0x36. */
int rl_433f49(void)
{
    g_rl_57d3c4 = 0;
    g_rl_591548 = 0;
    if (MP_ON)
        rl_4aecf0((int)(uintptr_t)MASK(g_rl_553248 != 0 ? 3 : 4), 0);
    else
        rl_4aecf0((int)(uintptr_t)MASK(5), 0);
    editor_open(&g_rl_4ea130, 8, 160, 1);
    editor_length();
    g_bf_57c90c = engine_current_screen();
    s_slot_004eafb0 = (uint32_t)engine_current_screen();
    engine_set_current_screen(54);
    g_rl_57d3c4 = 0;
    g_rl_591548 = 0;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_004344f2 (100 B) — the menu bar's Quit: single player / the MP host → the quit confirm screen 0x39
 * (redraw 2); an MP client posts "end the game" (event 227) and marks the action taken (DAT_0052afa4). */
int rl_4344f2(void)
{
    if (!MP_ON || g_rl_553248 != 0) {
        engine_set_current_screen(57);
        engine_set_demand_redraw(2);
        return 0;
    }
    int r = evq_post(0, LOCAL, 227, 0, 0, 0, 0, 0);
    g_diplo_action_taken = 1;
    return r;
}

/* Port of FUN_0043470d (122 B) — the options' quick-battle toggle (router param0 forced to 4): MP sends message
 * 0x32 and returns to the saved screen (DAT_0056d894); single player flips DAT_0053f284. Redraw 2. */
int rl_43470d(void)
{
    PARAM0 = 4;
    if (MP_ON)
        rl_43eda0(50, 0);
    else
        g_combat_flag_53f284 = g_combat_flag_53f284 == 0;
    if (MP_ON)
        engine_set_current_screen((uint8_t)g_rl_56d894);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_0043618b (79 B) — the diplomacy roster's Message: the diplomacy screen 0x1a in mode 1
 * (DAT_005651cc), the message editor on the lords2.inf text DAT_0053f2b8 (200 chars, 10000 px, flag 0), the map
 * scene ticked (0x4050c0); redraw 2. */
int rl_43618b(void)
{
    engine_set_current_screen(26);
    g_diplo_action_mode = 1;
    editor_open(g_rl_53f2b8, 200, 10000, 0);
    editor_length();
    int r = map_scene_tick();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043631e (84 B) — the diplomacy roster's county exchange: no county picked (DAT_005530b0), the
 * diplomacy screen 0x1a in mode 6, the map scene ticked with the panel flag DAT_004eb260 at 0 (then 2) and the
 * redraw flag DAT_004e65f8; redraw 2. (0x4b1de0 / 0x4b1dd5 around the tick are empty.) */
int rl_43631e(void)
{
    s_slot_005530b0 = 0;
    engine_set_current_screen(26);
    g_diplo_action_mode = 6;
    g_rl_4eb260 = 0;
    map_scene_tick();
    g_rl_4eb260 = 2;
    g_rl_4e65f8 = 1;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00436872 (139 B) — the alliance proposal's answer (param0 → DAT_0056d678): the popup dismissed
 * (0x476768); when the partner (DAT_0057c8b8) is a human (+5) or the answer is yes: MP message 0x47, else on yes
 * the alliance (0x4a1774). Returns retail's EAX (352 × partner on the no-op path). */
int rl_436872(void)
{
    popup_dismiss();
    g_rl_56d678 = PARAM0;
    int r = 352 * g_rl_57c8b8;
    if (PL8(g_rl_57c8b8, 0x57bf05) != 0 || PARAM0 != 0) {
        if (MP_ON)
            return rl_43eda0(71, 0);
        if (g_rl_56d678 == 1)
            engine_diplomacy_form_alliance(LOCAL, g_rl_57c8b8);
    }
    return r;
}

/* Port of FUN_004369bd (203 B) — the diplomacy roster's portrait pick: on a press, the other living realms
 * (+4) in order fill the 82 × 78 portraits at (48, 49 + 100 k); the one under the cursor (0x4b1deb) becomes the
 * partner DAT_00553fbc, redraw 2: returns 1, else 0. */
int rl_4369bd(void)
{
    if (g_bf_4eafb4 == 0)
        return 0;
    int k = 0;
    for (int p = 1; p < 6; p++) {
        if (p == LOCAL || PL8(p, 0x57bf04) == 0)
            continue;
        if (cursor_in(48, 100 * k + 49, 82, 78) != 0) {
            s_slot_00553fbc = (uint32_t)p;
            engine_set_demand_redraw(2);
            return 1;
        }
        k++;
    }
    return 0;
}

/* Port of FUN_00436b22 (55 B) — the castle screen's tier tab (param0 → DAT_0056d898): redraw 2, the castle panel
 * flag DAT_005440b8 = 1, the tier's voice line (0x4b3940). */
int rl_436b22(void)
{
    g_castle_tier = PARAM0;
    engine_set_demand_redraw(2);
    g_rl_5440b8 = 1;
    return rl_4b3940(g_castle_tier);
}

/* Port of FUN_00436b59 (425 B) — the castle screen's seal (param0: 0 cancel, 1 build): the click state cleared
 * (0x4b18e3). Build on the current county DAT_0057c93c: its castle stage (+0x1c0) already tier + 1 → notice 147,
 * past it → notice 290 (back to the map either way); else MP message 0x2b / the keep build (0x436d02, tier
 * DAT_0056d898). Then, with the result movies on (DAT_0053f248), the castle movie castle<tier>.smk (0x42d91b
 * after stopping the stream 0x426d11; a failed start leaves to the map and reloads 0x499aca); otherwise / on
 * cancel the strategic view (screen 0, 0x4984dc, redraw 1). */
int rl_436b59(void)
{
    rl_4b18e3();
    if (PARAM0 == 0)
        goto map;
    if (PARAM0 == 1) {
        int stage = ARMY8(s_slot_0057c93c, 0x53fb70);
        if (stage == g_castle_tier + 1) {
            evq_post(0, LOCAL, 147, 0, 0, 0, 0, 0);
            goto map;
        }
        if (stage > g_castle_tier + 1) {
            evq_post(0, LOCAL, 290, 0, 0, 0, 0, 0);
            goto map;
        }
        if (MP_ON)
            rl_43eda0(43, 0);
        else
            engine_army_ai_build_keep((int)s_slot_0057c93c, g_castle_tier);
    }
    if (g_combat_flag_53f248 == 0)
        goto map;
    rl_426d11(0);   /* 0x426d11 (stop the music stream; rl ui4) */
    {
        int r = rl_42d91b((int)(uintptr_t)(g_rl_4d5590 + 16 * g_castle_tier), 158, 20, 0, 0);
        if (r == 0) {
            engine_set_current_screen(0);
            strategic_view();
            engine_set_demand_redraw(1);
            return rl_499aca();
        }
        return r;
    }
map:
    engine_set_current_screen(0);
    {
        int r = strategic_view();
        engine_set_demand_redraw(1);
        return r;
    }
}

/* Port of FUN_0043733a (277 B) — the army panel's Disband: the army DAT_0052afbc (→ DAT_0053e904) standing in a
 * county of its owner's (+0x11, else +0x10: the county's owner +5 == the army's +0 signed) asks the confirm
 * "Disband army?" (6, callback 0x43744f); elsewhere notice 145, the map, full redraw, the action taken. */
int rl_43733a(void)
{
    AT(int32_t, g_rl_53e904, 0x53e904u, 0x53e904u) = g_rl_52afbc;
    int c = TER8(g_rl_52afbc, 0x52f0c1);
    if ((int8_t)TER8(g_rl_52afbc, 0x52f0b0) != ARMY8(c, 0x53f9b5))
        c = TER8(g_rl_52afbc, 0x52f0c0);
    if ((int8_t)TER8(g_rl_52afbc, 0x52f0b0) == ARMY8(c, 0x53f9b5))
        return confirm(6, 0x43744fu);
    evq_post(0, LOCAL, 145, 0, 0, 0, 0, 0);
    engine_set_current_screen(0);
    engine_territory_mark_full_screen_dirty();
    engine_set_demand_redraw(2);
    BELOW_STATUS() = 1;
    g_diplo_action_taken = 1;
    return 0;
}

/* Port of FUN_0043744f (117 B) — the disband confirm's callback: back to the screen before it (DAT_0057c90c),
 * full redraw; on yes (DAT_0059151c) MP message 0x2e / disband the army DAT_0053e904 into its realm (0x438681),
 * then the map. */
int rl_43744f(void)
{
    engine_set_current_screen((uint8_t)g_bf_57c90c);
    engine_territory_mark_full_screen_dirty();
    engine_set_demand_redraw(2);
    BELOW_STATUS() = 1;
    int r = g_bf_59151c[0];
    if (g_bf_59151c[0] != 0) {
        if (MP_ON)
            r = rl_43eda0(46, 0);
        else
            engine_army_ai_disband_into_realm(AT(int32_t, g_rl_53e904, 0x53e904u, 0x53e904u));
        engine_set_current_screen(0);
    }
    return r;
}

/* Port of FUN_004374c4 (113 B) — the army panel's garrison / transfer: the army DAT_0052afbc into the county's
 * castle DAT_005681e0 (DAT_0055cd58 / DAT_0052f0a8), the map; MP message 0x36, else the transfer (0x437535);
 * full redraw. */
int rl_4374c4(void)
{
    g_siege_sel_attacker = g_rl_52afbc;
    g_siege_sel_region = g_rl_5681e0[0];
    engine_set_current_screen(0);
    if (MP_ON)
        rl_43eda0(54, 0);
    else   /* TODO(rl-link): engine_combat_transfer_army (takes a port event queue) */
        engine_retail_call(0x437535u, 2, (uint32_t)g_siege_sel_attacker, (uint32_t)g_siege_sel_region);
    engine_territory_mark_full_screen_dirty();
    engine_set_demand_redraw(2);
    BELOW_STATUS() = 1;
    return 0;
}

/* the per-player muster block DAT_0053f6a0: 6 × 128 B, 8 rows of 16 B (+0 the count to send, +4 the count sent) */
#define MUSTER(p, i, off) AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f6a0u + 128 * (p) + 16 * (i) + (off))

/* Port of FUN_004376bb (504 B) — the battle prompt's Attack (param0 != 0): the fight (DAT_00553f5c → DAT_00553074,
 * DAT_0055cd54 → DAT_0053f080, DAT_00553f0c → DAT_00568d98), the popup dismissed (0x476768), the siege kind
 * DAT_0055321c (18 with engines +0x197, else 16); the local player's muster rows 0..6 = the army's troops
 * (+0x16c, int16), sent 0; row 7 = its engines (+0x196) / 0; the totals DAT_00554468 / DAT_00554040; the muster
 * screen 0x11 with its cue s017_01.wav (0x427990). Screen 0 + redraw 2 first in any case. */
int rl_4376bb(void)
{
    engine_set_current_screen(0);
    engine_set_demand_redraw(2);
    if (PARAM0 == 0)
        return 0;
    g_rl_553074 = g_rl_553f5c;
    g_rl_53f080 = g_rl_55cd54;
    g_rl_568d98 = g_rl_553f0c;
    popup_dismiss();
    g_rl_55321c = TER8(g_rl_553074, 0x52f247) != 0 ? 18 : 16;
    for (int i = 0; i < 7; i++)
        MUSTER(LOCAL, i, 0) = TER16(g_rl_553074, 0x52f21c + 2 * i);
    for (int i = 0; i < 7; i++)
        MUSTER(LOCAL, i, 4) = 0;
    MUSTER(LOCAL, 7, 0) = TER8(g_rl_553074, 0x52f246);
    MUSTER(LOCAL, 7, 4) = 0;
    g_rl_554040 = 0;
    g_rl_554468 = g_rl_554040;
    for (int i = 0; i < 8; i++)
        g_rl_554468 += MUSTER(LOCAL, i, 0);
    for (int i = 0; i < 8; i++)
        g_rl_554040 += MUSTER(LOCAL, i, 4);
    engine_set_current_screen(17);
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)g_rl_4d5794, 1, 0);
}

/* Port of FUN_00438acc (54 B) — the county panel's army button: outside the battlefield (DAT_0057cb18 != 2) the
 * county DAT_005681e0's army (+0x1bc) becomes the selected army DAT_0052afbc and its panel opens (0x41b032). */
int rl_438acc(void)
{
    if (s_slot_0057cb18 == 2)
        return 0;
    g_rl_52afbc = ARMY32(g_rl_5681e0[0], 0x53fb6c);
    return (int)engine_retail_call(0x41b032u, 0);   /* TODO(rl-link): engine.c's 0x41b032 */
}

/* ---------------------------------------------------------------- the battle prompt / siege preparation */
/* Port of FUN_0043b593 (143 B) — the battle prompt's answer (param0): 1 "take the field" → the battlefield
 * (0x4778a0; MP: 2 s, then message 0x3c); 0 "auto-resolve" → 0x43b622 (MP: message 0x3d twice, 10 ms apart). */
int rl_43b593(void)
{
    if (PARAM0 == 1) {
        if (MP_ON) {
            bf_sleep(2000);
            return rl_43eda0(60, 0);
        }
        return bf_4778a0();
    }
    if (PARAM0 == 0) {
        if (MP_ON) {
            rl_43eda0(61, 0);
            bf_sleep(10);
            return rl_43eda0(61, 0);
        }   /* TODO(rl-link): engine_combat_auto_resolve_button (takes a port event queue) */
        return (int)engine_retail_call(0x43b622u, 0);
    }
    return 0;
}

/* Port of FUN_0043b7c4 (132 B) — add `n` siege engines of kind `k` to army `a` (the int16 triple +0x182 + 6 k),
 * recompute its condition (0x4a80db); redraw 1 when it is the local player's (+0 signed). Returns the owner. */
int rl_43b7c4(int a, int k, int n)
{
    TER16(a, 0x52f232 + 6 * k) = (int16_t)(TER16(a, 0x52f232 + 6 * k) + n);
    engine_combat_recompute_condition(a);
    int r = (int8_t)TER8(a, 0x52f0b0);
    if (r == LOCAL)
        engine_set_demand_redraw(1);
    return r;
}

/* Port of FUN_0043b681 (192 B) — the siege screen's build-an-engine button (param0 = the kind → DAT_00544090;
 * DAT_00567d08 = 1 to build): under the kind's cap (4, kind 2+: 2) the besieger DAT_00568ed0 gets one (MP:
 * message 0x37; else 0x43b7c4). Returns retail's EAX (the current count when capped). */
int rl_43b681(void)
{
    g_rl_544090 = PARAM0;
    g_rl_567d08 = 1;
    int cap = g_rl_544090 == 0 ? 4 : g_rl_544090 == 1 ? 4 : 2;
    int r = TER16(g_siege_besieger, 0x52f232 + 6 * g_rl_544090);
    if (r < cap) {
        if (MP_ON)
            return rl_43eda0(55, 0);
        return rl_43b7c4(g_siege_besieger, g_rl_544090, g_rl_567d08);
    }
    return r;
}

/* Port of FUN_0043b848 (207 B) — the siege screen's choice (param0): 1 lift the siege (the map; MP message 0x38,
 * else 0x43b917 on the besieger DAT_00568ed0); 2 assault (the map, the action taken) unless the castle's
 * assault flag (+0x19c) is up: MP message 0x39, else 0x43b98a. */
int rl_43b848(void)
{
    if (PARAM0 == 1) {
        engine_set_current_screen(0);
        engine_set_demand_redraw(2);
        if (MP_ON)
            return rl_43eda0(56, 0);
        engine_combat_break_siege_link(g_siege_besieger);
        return 0;
    }
    if (PARAM0 == 2) {
        engine_set_current_screen(0);
        engine_set_demand_redraw(2);
        g_diplo_action_taken = 1;
        int r = 84 * g_siege_besieger;
        if (TER8(g_siege_besieger, 0x52f24c) == 0) {
            if (MP_ON)
                return rl_43eda0(57, 0);
            return rl_43b98a(g_siege_besieger);
        }
        return r;
    }
    return 0;
}

/* Port of FUN_0043b98a (23 B) — the siege assault by besieger `occ` (0x4a8aab). */
int rl_43b98a(int occ)
{   /* TODO(rl-link): engine_combat_siege_assault (takes a port event queue) */
    return (int)engine_retail_call(0x4a8aabu, 1, (uint32_t)occ);
}

/* ---------------------------------------------------------------- the frame driver's pre-tick steps */
/* retail's clock imports (timeGetTime 0x5cf5c4, GetTickCount 0x5cf3d8): rhost binds them to retail's own imports
 * (its virtual clock, rh_hooks_rl_glue.c); the standalone reads the system clocks */
static uint32_t (*s_time_fn)(void);
static uint32_t (*s_ticks_fn)(void);
void rl_glue_bind_clock(uint32_t (*time_fn)(void), uint32_t (*ticks_fn)(void));
void rl_glue_bind_clock(uint32_t (*time_fn)(void), uint32_t (*ticks_fn)(void))
{
    s_time_fn = time_fn;
    s_ticks_fn = ticks_fn;
}
static uint32_t clock_ms(void) { return s_time_fn ? s_time_fn() : (uint32_t)timeGetTime(); }
static uint32_t ticks_ms(void) { return s_ticks_fn ? s_ticks_fn() : (uint32_t)GetTickCount(); }

/* Port of FUN_004782c5 (340 B) — the battlefield result panel's timeout (screen 0x2b, sub-state 2): past 5000
 * frames (DAT_00568470) the castle record is updated for a single-player field battle (0x4784ca); the local
 * player's own battle (DAT_005533d0 == 1) writes the casualties back (0x47f474), stages the MP result block
 * (param0 = 1, 0x47f76d) and sends message 0x33 three times, 50 ms apart; then a siege (DAT_0057a0f0) writes
 * back again and shows the siege result 0x2e (0x42c64f; sub-state 0, engine state 2), a field battle runs the
 * rendered aftermath (0x4ab383) and the result screen 0x13 (its clock DAT_005689a0, the two armies kept in
 * DAT_00553c70 / 00553d20) and leaves (0x4788c6). Redraw 2. */
int rl_4782c5(void)
{
    int r = engine_current_screen();
    if (engine_current_screen() != 43 || s_slot_0057c8fc != 2 || (int)++s_slot_00568470 <= 5000)
        return r;
    if (s_slot_0057a0f0 == 0 && !MP_ON)
        bf_4784ca();
    if (s_slot_005533d0 == 1 && s_slot_0057a0f0 == 0) {
        rl_47f474();
        PARAM0 = 1;
        bf_47f76d();
        rl_43eda0(51, 0);
        bf_sleep(50);
        rl_43eda0(51, 0);
        bf_sleep(50);
        rl_43eda0(51, 0);
    }
    if (s_slot_0057a0f0 != 0) {
        rl_47f474();
        engine_set_current_screen(46);
        r = rl_42c64f();   /* the siege result (partition skirmish) */
        s_slot_0057c8fc = 0;
        g_engine_state = 2;
    } else {   /* TODO(rl-link): engine_combat_aftermath (takes a port event queue) */
        engine_retail_call(0x4ab383u, 1, 1u);
        if (engine_current_screen() != 19) {
            engine_set_current_screen(19);
            g_combat_anim_clock = clock_ms();
            s_slot_00553c70 = s_slot_00568210;
            s_slot_00553d20 = s_slot_00568218;
        }
        r = (int)engine_retail_call(0x4788c6u, 0);   /* TODO(rl-extern): 0x4788c6 (back to the map) */
    }
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_00472a31 (152 B) — the strategic map's hovered army: DAT_005533c8 = the army standing on the
 * hovered cell DAT_00568974 (cell +5, whose record's cell +0xc is that cell), on the bare map (sub-state 1,
 * screen 0) with the cursor over the map (DAT_005533b0); else 0. Returns retail's EAX. */
int rl_472a31(void)
{
    g_rl_5533c8 = 0;
    if (s_slot_0057c8fc != 1)
        return (int)s_slot_0057c8fc;
    int r = engine_current_screen();
    if (r != 0 || g_rl_5533b0 == 0)
        return r;
    int a = AT(uint8_t, g_overworld_map, 0x522f90u, 0x522f95 + g_rl_568974);
    if (a == 0)
        return g_rl_568974;
    r = 84 * a;
    if (TER32(a, 0x52f0bc) == g_rl_568974) {
        g_rl_5533c8 = a;
        r = a;
    }
    return r;
}

/* Port of FUN_00498458 (132 B) — the graphics check on the bare screen 0: the strategic sheets not loaded
 * (DAT_0053f050 != 1) on the map → reload them (0x4984dc) and log it; the battle sheets (!= 2) on the battlefield
 * → reload (0x4987b7, 0x480f8b) and log the error (0x4afab9). */
int rl_498458(void)
{
    int r = engine_current_screen();
    if (r != 0)
        return r;
    if (s_slot_0057c8fc == 0 && g_bf_53f050 != 1) {
        strategic_view();
        r = rl_4afab9((int)(uintptr_t)(g_rl_4dbc7c + 0), 0, 0);
    }
    if (s_slot_0057c8fc == 2 && g_bf_53f050 != 2) {
        bf_4987b7();
        bf_480f8b();
        return rl_4afab9((int)(uintptr_t)(g_rl_4dbc7c + (0x4dbca8 - 0x4dbc7c)), 0, 0);
    }
    return r;
}

/* Port of FUN_00448d7e (148 B) — county `c`'s pending notice (+0): unless the notices are held (DAT_004e8780)
 * it is cleared and, for a county the local player owns, posted (event +0x1aa, kind 15, the county). */
int rl_448d7e(int c)
{
    int r = c << 8;
    if (ARMY8(c, 0x53f9b0) == 0)
        return r;
    if (g_rl_4e8780 != 0)
        return g_rl_4e8780;
    ARMY8(c, 0x53f9b0) = 0;
    if (ARMY8(c, 0x53f9b5) != LOCAL)
        return c << 8;
    return evq_post(0, LOCAL, ARMY16(c, 0x53fb5a), 0, 15, c, 0, 0);
}

/* Port of FUN_0047703a (126 B) — take the tooltip down (DAT_004eac00): clamp (0x477249), restore its 48 × 40
 * backdrop at (DAT_004eb268, DAT_004eb270) from DAT_004e6980 (448 rows, 0x4b3ec0) and dirty those 14 × 4 tiles. */
int rl_47703a(void)
{
    if (g_rl_4eac00 == 0)
        return 0;
    g_rl_4eac00 = 0;
    engine_retail_call(0x477249u, 0);   /* TODO(rl-link): engine_tooltip.c's tooltip_clamp */
    g_rl_591524 = g_rl_4eb268;
    g_rl_591528 = g_rl_4eb270;
    g_rl_5c9258 = 48;
    g_rl_5aeb70 = 40;
    backdrop_restore((int)(uintptr_t)g_rl_4e6980, 448);
    engine_dirty_rect_mark_box_tiled(engine_territory_dirty_state(), g_rl_4eb268, g_rl_4eb270, 14, 4, 1);
    return 0;
}

/* Port of FUN_00440666 (383 B) — the MP link's per-frame poll: the received-this-frame flags (DAT_00553fa8,
 * DAT_0053e8a8, DAT_0053f090[0..9]) cleared; single player returns 1. MP: a hold-off (DAT_0052afcc, counted down)
 * or a running result panel (DAT_00568470) returns 1; else the link is read (0x440dd2): a packet (1) is handled
 * (0x43f8ff → 0x43fd2a; DAT_0057cb3c counted, the resend / stall counters DAT_00512f88 / 00553214 down), a
 * resend request (2) past 2 sends message 0x5b; nothing (0) stalls: DAT_00553214 up, DAT_00553fa8, the stall
 * clock DAT_00553c6c, screen 0x2a → 0x29, 0x43f5c5, returns 0. Returns 1 unless stalled. */
int rl_440666(void)
{
    int stalled = 0;
    g_rl_553fa8 = 0;
    g_rl_53e8a8 = 0;
    for (int i = 0; i < 10; i++)
        g_rl_53f090[i] = 0;
    if (!MP_ON)
        return 1;
    if (g_combat_render_dirty != 0) {
        g_combat_render_dirty--;
        return 1;
    }
    if (s_slot_00568470 != 0)
        return 1;
    int k = (int)engine_retail_call(0x440dd2u, 1, 0u);   /* TODO(rl-extern): 0x440dd2 (read the link) */
    if (k == 0) {
        stalled = 1;
    } else if (k == 1) {
        /* retail calls 0x43fd2a with no argument pushed (IDA's v1 = 0x43f8ff's EAX is a guess; 0x43fd2a reads the
         * caller's saved EDI): MP only, unmodelled — 0x43f8ff's result passed */
        uint32_t v = engine_retail_call(0x43f8ffu, 0);   /* TODO(rl-extern): 0x43f8ff */
        engine_retail_call(0x43fd2au, 1, v);              /* TODO(rl-extern): 0x43fd2a */
        g_rl_57cb3c++;
        if (g_rl_512f88 > 0)
            g_rl_512f88--;
        if (g_rl_553214 > 0)
            g_rl_553214--;
    } else if (k == 2 && ++g_rl_512f88 > 2) {
        rl_43eda0(91, 0);
    }
    if (stalled == 0)
        return 1;
    g_rl_553214++;
    g_rl_553fa8 = 1;
    g_bf_553c6c = (int32_t)clock_ms();
    if (engine_current_screen() == 42)
        engine_set_current_screen(41);
    engine_retail_call(0x43f5c5u, 0);   /* TODO(rl-extern): 0x43f5c5 */
    return 0;
}

/* Port of FUN_0049b8b0 (195 B) — the MP host's end-of-turn vote (the turn committing DAT_00553fc8, the host
 * DAT_00553248): a result from the clients (0x4407e5) sets the answer DAT_0059151c (1 = yes), sends message 0x16,
 * waits 1 s and applies it (0x49b973); none yet → message 0x14 every 10 s (DAT_0056d658). */
int rl_49b8b0(void)
{
    if (g_sim_turn_progress == 0 || g_rl_553248 == 0)
        return 0;
    uint32_t now = clock_ms();
    int v = (int)engine_retail_call(0x4407e5u, 0);   /* TODO(rl-extern): 0x4407e5 */
    if (v != 0) {
        g_bf_59151c[0] = v == 1;
        rl_43eda0(22, 0);
        bf_sleep(1000);
        engine_retail_call(0x49b973u, 1, (uint32_t)g_bf_59151c[0]);   /* TODO(rl-extern): 0x49b973 */
    } else if ((int)(now - (uint32_t)g_rl_56d658) >= 10000) {
        g_rl_56d658 = (int32_t)now;
        rl_43eda0(20, 0);
    }
    return 0;
}

/* Port of FUN_0047f474 (761 B) — the battlefield casualty write-back: both armies (DAT_00568210 / 00568218) lose
 * their troop total (+0x168), engines (+0x196) and the 11 troop counts (+0x16c); then every living foot unit
 * (class +0x12 < 7) of either side adds its count (+0x1a0) back to its army: the total, and the engines (+0x14
 * set) or its class's count. The unit loop index is retail's DAT_0053e8f8. (Retail also tests DAT_0053e8fc and
 * ignores it.) */
int rl_47f474(void)
{
    int a = (int)s_slot_00568210, d = (int)s_slot_00568218;
    TER32(a, 0x52f218) = 0;
    TER32(d, 0x52f218) = 0;
    TER8(a, 0x52f246) = 0;
    TER8(d, 0x52f246) = 0;
    for (int i = 0; i < 11; i++)
        TER16(s_slot_00568210, 0x52f21c + 2 * i) = 0;
    for (int i = 0; i < 11; i++)
        TER16(s_slot_00568218, 0x52f21c + 2 * i) = 0;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) == 0 || BU8(u, 0x554492) >= 7)
            continue;
        int v;
        if (BU8(u, 0x5544ac) == (int8_t)TER8(s_slot_00568210, 0x52f0b0))
            v = (int)s_slot_00568210;
        else if (BU8(u, 0x5544ac) == (int8_t)TER8(s_slot_00568218, 0x52f0b0))
            v = (int)s_slot_00568218;
        else
            continue;
        TER32(v, 0x52f218) += BS16(u, 0x554620);
        if (BU8(u, 0x554494) != 0)
            TER8(v, 0x52f246) = (uint8_t)(BS16(u, 0x554620) + TER8(v, 0x52f246));
        else
            TER16(v, 0x52f21c + 2 * BU8(u, 0x554492)) =
                (int16_t)(TER16(v, 0x52f21c + 2 * BU8(u, 0x554492)) + BS16(u, 0x554620));
    }
    return 0;
}

/* mark the cell box (c0..c1) × (r0..r1) of the battlefield grid for redraw (cell +2 bit 0) */
static void mark_box(int c0, int r0, int c1, int r1)
{
    int off = 8 * (c0 + 80 * r0);
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            BCELL8(off, 0x5440e2) |= 1;
            off += 8;
        }
        off += 8 * (80 - (c1 - c0 + 1));
    }
}

/* Port of FUN_00481b9a (1399 B) — the battlefield's per-frame census (sub-state 2): units alive (DAT_00568ecc);
 * foot troops per side (attacker DAT_0053f028, defender DAT_00553c58); of the not-routed (+0x13 == 0) the total
 * DAT_00553f74, peasants (class 0) DAT_00567960, classes 1 + 5 DAT_0056d6ac, class 6 DAT_0057c980; the local
 * player's selection (+9): count DAT_00553078, per class DAT_004eeb30[11], wounded (+0x1a0 != +0x1a2)
 * DAT_00568204 — its majority class → DAT_0055408c; no catapult (class 9) on the field clears DAT_004eeb6c. Every
 * unit's cell box (+0x20 / +0x22 ± 2, engines ± 3, clamped) is marked for redraw. DAT_0053f010 / 00553238
 * cleared. The unit loop index is retail's DAT_0053e8f8. */
int rl_481b9a(void)
{
    if (s_slot_0057c8fc != 2)
        return 0;
    g_rl_568ecc = 0;
    g_bf_553078 = 0;
    s_slot_0053f028 = 0;
    s_slot_00553c58 = 0;
    s_slot_0053f010 = 0;
    s_slot_00553238 = 0;
    s_slot_00567960 = 0;
    s_slot_0056d6ac = 0;
    s_slot_0057c980 = 0;
    s_slot_00553f74 = 0;
    g_rl_568204 = 0;
    for (int i = 0; i < 11; i++)
        g_rl_4eeb30[i] = 0;
    int catapult = 0;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) == 0)
            continue;
        g_rl_568ecc++;
        int cls = BU8(u, 0x554492);
        if (cls < 7) {
            if (BU8(u, 0x5544ac) == (int8_t)TER8(s_slot_00568210, 0x52f0b0))
                s_slot_0053f028 += (uint32_t)(int32_t)BS16(u, 0x554620);
            else if (BU8(u, 0x5544ac) == (int8_t)TER8(s_slot_00568218, 0x52f0b0))
                s_slot_00553c58 += (uint32_t)(int32_t)BS16(u, 0x554620);
            if (BU8(u, 0x554493) == 0) {
                s_slot_00553f74 += (uint32_t)(int32_t)BS16(u, 0x554620);
                if (cls == 0)
                    s_slot_00567960 += (uint32_t)(int32_t)BS16(u, 0x554620);
                else if (cls == 1 || cls == 5)
                    s_slot_0056d6ac += (uint32_t)(int32_t)BS16(u, 0x554620);
                else if (cls == 6)
                    s_slot_0057c980 += (uint32_t)(int32_t)BS16(u, 0x554620);
            }
        }
        if (BU8(u, 0x554492) == 9)
            catapult = 1;
        if (BU8(u, 0x554489) == LOCAL) {
            g_bf_553078++;
            g_rl_4eeb30[BU8(u, 0x554492)]++;
            if (BS16(u, 0x554620) != BS16(u, 0x554622))
                g_rl_568204++;
        }
        int m = BU8(u, 0x554492) >= 7 ? 3 : 2;
        int c0 = BS16(u, 0x5544a0) - m, r0 = BS16(u, 0x5544a2) - m;
        int c1 = BS16(u, 0x5544a0) + m, r1 = BS16(u, 0x5544a2) + m;
        if (c0 < 0)
            c0 = 0;
        if (c1 >= 80)
            c1 = 79;
        if (r0 < 0)
            r0 = 0;
        if (r1 >= 80)
            r1 = 79;
        mark_box(c0, r0, c1, r1);
    }
    g_bf_55408c = 0;
    int best = 0;
    for (int i = 0; i < 11; i++) {
        if (g_rl_4eeb30[i] > best) {
            g_bf_55408c = i;
            best = g_rl_4eeb30[i];
        }
    }
    if (catapult == 0)
        g_bf_4eeb6c = 0;
    return 0;
}

/* Port of FUN_00482111 (476 B) — the battlefield's projectile census (sub-state 2): live projectiles (combat-unit
 * table 1..100, +8) counted into DAT_00566510 and each one's cell box (+0x14 / +0x16 ± 1, kind +9 >= 4: ± 2,
 * clamped) marked for redraw. The loop index is retail's DAT_0056d5b4. */
int rl_482111(void)
{
    if (s_slot_0057c8fc != 2)
        return 0;
    g_rl_566510 = 0;
    for (g_bf_56d5b4 = 1; g_bf_56d5b4 < 101; g_bf_56d5b4++) {
        int p = g_bf_56d5b4;
        if (BC8(p, 0x57a108) == 0)
            continue;
        g_rl_566510++;
        int m = BC8(p, 0x57a109) >= 4 ? 2 : 1;
        int c0 = BC16(p, 0x57a114) - m, r0 = BC16(p, 0x57a116) - m;
        int c1 = BC16(p, 0x57a114) + m, r1 = BC16(p, 0x57a116) + m;
        if (c0 < 0)
            c0 = 0;
        if (c1 >= 80)
            c1 = 79;
        if (r0 < 0)
            r0 = 0;
        if (r1 >= 80)
            r1 = 79;
        mark_box(c0, r0, c1, r1);
    }
    return 0;
}

/* Port of FUN_004a82b9 (365 B) — the turn driver's garrison-link sweep: DAT_0052afac = 0; each county 1..16's
 * army (+0x1bc) whose castle link (+0x19a) names an army not linked back (+0x199 != the county) drops both
 * links; then every field army (+8 == 1) of a living owner with a link (+0x199) is re-checked (0x4a8426); the
 * army redraw DAT_0057cad4 = 1. */
int rl_4a82b9(void)
{
    g_combat_ctrl_52afac = 0;
    for (int i = 1; i < 17; i++) {
        int a = ARMY32(i, 0x53fb6c);
        int l = TER8(a, 0x52f24a);
        if (a != 0 && TER8(a, 0x52f24a) != 0 && TER8(l, 0x52f249) != i) {
            TER8(a, 0x52f24a) = 0;
            TER8(l, 0x52f249) = 0;
        }
    }
    for (int j = 1; j < 151; j++) {
        if (TER8(j, 0x52f0b0) != 0 && TER8(j, 0x52f0b8) == 1 && TER8(j, 0x52f249) != 0)
            rl_4a8426(j);
    }
    s_slot_0057cad4 = 1;
    return 0;
}

/* ---------------------------------------------------------------- the MP message sender */
/* Port of FUN_0043eda0 (637 B) — send MP message `id` to `dest`: single player does nothing (retail returns the
 * caller's EAX). MP: an unknown size (DAT_004d5b90[id] == 255) logs "Undefined packet size." (0x43e668); else
 * at least 3 ms after the last send (DAT_004e59d4, GetTickCount; Sleep 3), the ring slot DAT_00512f4c (40 × 260 B
 * at DAT_00512fb0; payload pointer DAT_00512f8c = slot + 4) is filled by the message's builder (the table
 * DAT_004d57f0[id], retail — the MP handlers stay retail), stamped (id, size, checksum 0x440038, slot) and sent
 * (0x4b90fb(dest, slot, size + 4, reliable)): the 11 unreliable ids once, the rest three times (slot +0x40 /
 * +0x80 retry marks); DAT_00569580 counts sends; the slot wraps 39 → 1. */
int rl_43eda0(int id, int dest)
{
    if (!MP_ON)
        return 0;
    uint8_t k = (uint8_t)id;
    if (g_rl_4d5b90[k] == 255) {   /* TODO(rl-extern): 0x43e668 (the MP error log) */
        engine_retail_call(0x43e668u, 1, (uint32_t)(uintptr_t)g_rl_4d5f10);
        return 0;
    }
    if ((int)(ticks_ms() - (uint32_t)g_rl_4e59d4) < 3)
        bf_sleep(3);
    g_rl_4e59d4 = (int32_t)ticks_ms();
    uint8_t *slot = g_rl_512fb0 + 260 * g_rl_512f4c;
    g_rl_512f8c = (int32_t)(uintptr_t)(slot + 4);
    rl_mpmsg_build(k);   /* the payload builder (table 0x4d57f0; port side: rl_mpmsg.c) */
    slot[0] = k;
    slot[1] = g_rl_4d5b90[k];
    slot[3] = (uint8_t)g_rl_512f4c;
    slot[2] = (uint8_t)rl_440038(g_rl_4d5b90[k], (int)(uintptr_t)(g_rl_512fb0 + 260 * g_rl_512f4c));   /* checksum */
    int len = g_rl_4d5b90[k] + 4;
    /* TODO(rl-extern): 0x4b90fb (the DirectPlay send) */
    if (k == 10 || k == 89 || k == 67 || k == 68 || k == 66 || k == 39 || k == 49 || k == 35 || k == 14 ||
        k == 84 || k == 29) {
        engine_retail_call(0x4b90fbu, 4, (uint32_t)dest, (uint32_t)g_rl_512f4c, (uint32_t)len, 0u);
        g_rl_569580++;
    } else {
        engine_retail_call(0x4b90fbu, 4, (uint32_t)dest, (uint32_t)g_rl_512f4c, (uint32_t)len, 1u);
        slot[3] |= 0x40;
        engine_retail_call(0x4b90fbu, 4, (uint32_t)dest, (uint32_t)g_rl_512f4c, (uint32_t)len, 1u);
        slot[3] &= 0x3f;
        slot[3] |= 0x80;
        engine_retail_call(0x4b90fbu, 4, (uint32_t)dest, (uint32_t)g_rl_512f4c, (uint32_t)len, 1u);
        g_rl_569580 += 3;
    }
    if (++g_rl_512f4c >= 40)
        g_rl_512f4c = 1;
    return 0;
}

/* ---------------------------------------------------------------- loaders, startup, rendering glue */
/* Port of FUN_0040ae12 (175 B) — load sprite `k` of a .pl8 file into `dst`: its 16-B header (at 16 k + 8, into
 * DAT_00522d10) gives the blit width / height (DAT_005c9258 / 5aeb70), the file offset (DAT_0058fe04), the
 * flags DAT_00591568, DAT_005bb478 (zero-extended), DAT_0057d3c0; the w × h pixels are read from there (0x4af8d9;
 * TODO(rl-link): io_read_file_at) and the offset reset. Returns the header read's result (retail's EAX: the
 * pixel read's result, or 0). */
int rl_40ae12(int file, int dst, int k)
{
    int r = (int)engine_retail_call(0x4af8d9u, 4, (uint32_t)file, (uint32_t)(uintptr_t)g_rl_522d10, 16u,
                                    (uint32_t)(16 * k + 8));
    if (r == 0)
        return r;
    const uint8_t *h = g_rl_522d10;
    g_rl_5c9258 = *(const int16_t *)h;
    g_rl_5aeb70 = *(const int16_t *)(h + 2);
    AT(int32_t, g_rl_58fe04, 0x58fe04u, 0x58fe04u) = *(const int32_t *)(h + 4);
    g_rl_591568 = h[12];
    g_rl_5bb478 = h[13];
    g_rl_57d3c0 = h[14];
    r = (int)engine_retail_call(0x4af8d9u, 4, (uint32_t)file, (uint32_t)dst, (uint32_t)(g_rl_5aeb70 * g_rl_5c9258),
                                (uint32_t)AT(int32_t, g_rl_58fe04, 0x58fe04u, 0x58fe04u));
    AT(int32_t, g_rl_58fe04, 0x58fe04u, 0x58fe04u) = 0;
    return r;
}

/* the strategic map's tile drawers for the sheet DAT_00568208 (the terrain sets DAT_0057cb18 0 / 1 / 2) — the
 * blit header (16 B at sheet + 8 + 16 × DAT_005c9288) loaded as in 0x40ae12; an offset past 310000 or negative
 * counts a bad tile (DAT_0058fea8) instead of drawing */
static int tile_header(void)
{
    const uint8_t *h = (const uint8_t *)(uintptr_t)(16 * g_rl_5c9288 + 8 + g_bf_568208);
    g_rl_5c9258 = *(const int16_t *)h;
    g_rl_5aeb70 = *(const int16_t *)(h + 2);
    AT(int32_t, g_rl_58fe04, 0x58fe04u, 0x58fe04u) = *(const int32_t *)(h + 4);
    g_rl_591568 = h[12];
    g_rl_5bb478 = h[13];
    g_rl_57d3c0 = h[14];
    int off = AT(int32_t, g_rl_58fe04, 0x58fe04u, 0x58fe04u);
    if (off > 310000 || off < 0) {
        g_rl_58fea8++;
        return 0;
    }
    return 1;
}

/* Port of FUN_0042a8d1 (218 B) — draw the current tile, drawer A (0x45337b / 0x46079d / 0x463c06 by terrain set;
 * TODO(rl-link): engine_map_terrain.c, gen/blit_table_463c06.h). Returns retail's EAX (char). */
int rl_42a8d1(void)
{
    if (!tile_header())
        return g_rl_57d3c0;
    uint32_t f = s_slot_0057cb18 == 0 ? 0x45337bu : s_slot_0057cb18 == 1 ? 0x46079du : 0x463c06u;
    return (int)engine_retail_call(f, 2, (uint32_t)g_bf_568208, 0u);
}

/* Port of FUN_0042a9ab (218 B) — draw the current tile, drawer B (0x4538f6 / 0x4608de / 0x463c62). */
int rl_42a9ab(void)
{
    if (!tile_header())
        return g_rl_57d3c0;
    uint32_t f = s_slot_0057cb18 == 0 ? 0x4538f6u : s_slot_0057cb18 == 1 ? 0x4608deu : 0x463c62u;
    return (int)engine_retail_call(f, 2, (uint32_t)g_bf_568208, 0u);
}

/* Port of FUN_0042d179 (157 B) — the high-score table's defaults: 10 scores (DAT_004d49c8 → DAT_0051fd00), the
 * 10 extra fields DAT_0051fd28 zeroed, the 10 names from lords2 string group 7 (0x4018d7: the strings
 * DAT_004d49f0[j]; the current string DAT_005c9270) into the 32-B rows of DAT_0051fbc0. */
int rl_42d179(void)
{
    for (int i = 0; i < 10; i++) {
        AT(int32_t, g_rl_51fd00, 0x51fd00u, 0x51fd00u + 4 * i) = AT(int32_t, g_rl_4d49c8, 0x4d49c8u, 0x4d49c8u + 4 * i);
        AT(int32_t, g_rl_51fd28, 0x51fd28u, 0x51fd28u + 4 * i) = 0;
    }
    for (int j = 0; j < 10; j++) {   /* TODO(rl-link): localization_seek_skip */
        rl_4018d7(7, AT(int32_t, g_rl_4d49c8, 0x4d49c8u, 0x4d49f0u + 4 * j));
        strutil_bounded_copy(g_rl_51fbc0 + 32 * j, (const void *)(uintptr_t)g_rl_5c9270, 32);
    }
    return 0;
}

/* Port of FUN_0042f148 (151 B) — create an off-screen DirectDraw surface w × h (DDSURFACEDESC: caps | height |
 * width, DDSCAPS_OFFSCREENPLAIN, + DDSCAPS_SYSTEMMEMORY with `sysmem`) through the DirectDraw object
 * DAT_004e6974 (IDirectDraw::CreateSurface, vtable +0x18); a failure is logged ("ERR:DD ExtraSurface failed.",
 * 0x4afab9) and gives 0. Returns the surface. */
int rl_42f148(int w, int h, int sysmem)
{
    uint32_t desc[27];
    uint32_t surf = 0;
    memset(desc, 0, sizeof desc);
    desc[0] = 108;
    desc[1] = 7;
    desc[26] = 64;
    if (sysmem != 0)
        desc[26] |= 0x800u;
    desc[2] = (uint32_t)h;
    desc[3] = (uint32_t)w;
    void *dd = (void *)(uintptr_t)g_rl_4e6974;
    typedef int32_t (__stdcall *create_t)(void *, void *, void *, void *);
    create_t create = (create_t)(*(void ***)dd)[6];
    if (create(dd, desc, &surf, 0) != 0) {
        rl_4afab9((int)(uintptr_t)g_rl_4d548c, 0, 0);
        surf = 0;
    }
    return (int)surf;
}

/* Port of FUN_004b1310 (221 B) — grey out the 640 × 480 back buffer (DAT_004ea1a8): the palette DAT_004ea1b0
 * mapped to the 64 greys 63 - ((r + g + b) / 3 >> 3) (DAT_004eac60), then every pixel through it. Returns
 * retail's EAX (char: the last pixel). */
int rl_4b1310(void)
{
    uint8_t *buf = (uint8_t *)(uintptr_t)AT(uint32_t, g_rl_4ea1a8, 0x4ea1a8u, 0x4ea1a8u);
    if (buf == NULL)
        return 0;
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = g_rl_4ea1b0 + 3 * i;
        int v = (c[0] + c[1] + c[2]) / 3;
        g_rl_4eac60[i] = (uint8_t)(63 - ((v >> 2) >> 1));
    }
    uint8_t last = 0;
    for (int j = 0; j < 307200; j++) {
        last = g_rl_4eac60[buf[j]];
        buf[j] = last;
    }
    return last;
}

/* Port of FUN_00409429 (68 B) — a framed panel: the frame (0x409934, style 2) at (x, y) w × h, its title strip
 * (0x409c93) at (x + 16, y), w - 2 by h - 1. TODO(rl-link): gfx_panel_draw_border_chrome /
 * gfx_panel_fill_interior_chrome (gfx_panel.c, linked and on retail's layout since 7965fc7, but they take the chrome
 * sheet retail bakes in, DAT_0057d3d0, which has no port symbol yet). */
int rl_409429(int x, int y, int w, int h)
{
    engine_retail_call(0x409934u, 5, 2u, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
    return (int)engine_retail_call(0x409c93u, 4, (uint32_t)(x + 16), (uint32_t)y, (uint32_t)(w - 2), (uint32_t)(h - 1));
}

/* Port of FUN_0040f10f (145 B) — the startup speed probe: the time of 10 × 0x4b209a(20) → DAT_004d2168 and of
 * 10 full-screen repaints (0x45240a + 0x452160(1)) → DAT_004d216c (timeGetTime; rhost: the virtual clock). */
int rl_40f10f(void)
{
    uint32_t t = clock_ms();
    for (int i = 0; i < 10; i++)
        engine_retail_call(0x4b209au, 1, 20u);   /* TODO(rl-extern): 0x4b209a */
    g_rl_4d2168 = (int32_t)(clock_ms() - t);
    t = clock_ms();
    for (int j = 0; j < 10; j++) {
        engine_territory_mark_full_screen_dirty();
        rl_452160(1);
    }
    g_rl_4d216c = (int32_t)(clock_ms() - t);
    return g_rl_4d216c;
}

/* Port of FUN_00475b41 (562 B) — the end-of-game movie for realm `p` and reason `why` into DAT_004f0340 (13 B):
 * 225 (won) win_game.smk; a fallen noble (realm aux +0x27): by the years past the start (DAT_00553edc vs
 * DAT_00553228) jail < 12, hang < 32, else axmen; a fallen commoner the cart row of their portrait (realm +7,
 * 80-B rows at DAT_004d6cf8) by 6-year steps (< 6, < 12, < 18, < 24, else the fifth). */
int rl_475b41(int p, int why)
{
    uint8_t *dst = g_rl_4f0340;
    const uint8_t *m = g_rl_4d6cf8;
    if (why == 225) {
        strutil_bounded_copy(dst, g_rl_4d7058, 13);
        return 0;
    }
    int y0 = (int)s_slot_00553228, now = (int)s_slot_00553edc;
    if (PX8(p, 0x553d77) != 0) {
        if (y0 + 12 > now)
            strutil_bounded_copy(dst, m + 0x20, 13);
        else if (y0 + 32 > now)
            strutil_bounded_copy(dst, m + 0x30, 13);
        else
            strutil_bounded_copy(dst, m + 0x40, 13);
        return 0;
    }
    int row = 80 * PL8(p, 0x57bf07);
    int col = y0 + 6 > now ? 0 : y0 + 12 > now ? 16 : y0 + 18 > now ? 32 : y0 + 24 > now ? 48 : 64;
    strutil_bounded_copy(dst, m + row + col, 13);
    return 0;
}

/* Port of FUN_00475d73 (148 B) — load realm `p`'s portrait for the popup: faces.pl8 sprite 3 × face (+7) - 3, a
 * human's (+5) 12, below 1 16, past 5 17, into DAT_004eeb80 (0x40ae12; the base directory first, 0x4af502:
 * TODO(rl-link): io_path_chdir_base). */
int rl_475d73(int p)
{
    rl_4af502();
    int k;
    if (p <= 0)   /* (retail reads the realm record first whatever p is: a read past the table here) */
        k = 16;
    else if (p >= 6)
        k = 17;
    else if (PL8(p, 0x57bf05) != 0)
        k = 12;
    else
        k = 3 * PL8(p, 0x57bf07) - 3;
    return rl_40ae12((int)(uintptr_t)(g_rl_4d7058 + (0x4d7068 - 0x4d7058)), (int)(uintptr_t)g_rl_4eeb80, k);
}
