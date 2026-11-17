/* rl_ctl.c — a retail-layout port, partition 'ctl' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * Round 3: every function here had an old port (engine*.c, game_init.c, …). Route (a) = a wrapper over a
 * linked old port that already works on retail's layout; route (b) = a fresh port (the old one a reading aid).
 * Findings: docs/findings/rl-ctl.md. */
#include "rl_va.h"
#include "rl.h"
#include "rl_ctl_globals.h"
#include "engine.h"                    /* the screen / redraw bridges, g_sim_mp_active */
#include "engine_territory.h"          /* the bound dirty state (0x4527a6's six globals + DAT_0053e9ac: a mirror) */
#include "engine_dirty_rect.h"
#include "engine_title_render.h"       /* g_tr_scene_flag_a (DAT_005651ac) */
#include "engine_realm.h"              /* the coalition table DAT_00568240 (g_coalition_tbl), 0x44b042 */
#include "engine_army_economy.h"       /* 0x450fb4 */
#include "engine_mapview_globals.h"    /* the map camera scalars (DAT_0053e8ac …), the animation tick DAT_0053e9a4 */
#include "gfx_globals.h"               /* the canvas size, the alt clip (DAT_0052afdc / DAT_00553254) */
#include "engine_combat.h"             /* g_combat_flag_530d0 / _567d0c, g_combat_anim_deadline / _scratch, g_siege_besieger */
#include "engine_army_ai.h"            /* the turn, the ranking-dirty flag DAT_0056d6a0 (a mirror), 0x4502ca */
#include "engine_ranking.h"            /* 0x4486a9 */
#include "rl_io_plat.h"                /* timeGetTime */
#include "engine_diplomacy_action.h"   /* g_diplo_action_taken (DAT_0052afa4) */
#include "engine_map_scene.h"          /* engine_map_scene_tick_frame (0x4050c0) */
#include "engine_turn.h"               /* g_sim_turn_progress (DAT_00553fc8) */
#include "engine_armory.h"             /* the muster screen's globals (DAT_00553f20, DAT_0055446c …) */
#include "engine_workforce_hud.h"       /* g_wf_y_origin (DAT_0057c908), 0x4517ca */
#include "engine_army_state.h"         /* g_army_state_index (DAT_005533e8, a mirror) */
#include "engine_sendsupplies.h"       /* g_castle_tier (DAT_0056d898), g_send_target (DAT_00553c78), 0x43b145 */
#include "engine_bf_input_globals.h"   /* the cursor (DAT_0053e98c / 9c), DAT_00553e68, DAT_0059151c, the router param */

#include <string.h>
#include <windows.h>

#define DIRTY engine_territory_dirty_state()   /* DAT_0050d09c / 0050a4c4 / c8 / bc / c0 / 0053e9ac (a mirror: rh_hooks_ai.c) */

/* ================================================================ the dirty-rect marks (route a: engine_dirty_rect.c)
 * EAX: each returns 0x452306's, which is its fourth argument (the box's bottom edge). */

/* Port of FUN_00452306 (224 B) — grow the dirty rectangle by (x1, y1)..(x2, y2), the mark at least 1; clamp
 * (the bottom edge is tested against 640, quirk #11). */
int rl_452306(int x1, int y1, int x2, int y2)
{
    engine_dirty_rect_expand(DIRTY, x1, y1, x2, y2);
    return y2;
}

/* Port of FUN_004523e6 (36 B) — mark the right half (320, 0)..(639, 479). */
int rl_4523e6(void)
{
    engine_dirty_rect_mark_right_half(DIRTY);
    return 479;
}

/* Port of FUN_0045240a (33 B) — mark the whole screen. */
int rl_45240a(void)
{
    engine_dirty_rect_mark_full_screen(DIRTY);
    return 479;
}

/* Port of FUN_00452436 (83 B) — a 32 px box at tile (col, row) (negative clamped to 0). */
int rl_452436(int col, int row)
{
    engine_dirty_rect_mark_tile_32(DIRTY, col, row);
    return (row < 0 ? 0 : row) * 16 + 32;
}

/* Port of FUN_00452489 (83 B) — a 48 px box at tile (col, row). */
int rl_452489(int col, int row)
{
    engine_dirty_rect_mark_tile_48(DIRTY, col, row);
    return (row < 0 ? 0 : row) * 16 + 48;
}

/* Port of FUN_004524dc (83 B) — a 64 px box at tile (col, row). */
int rl_4524dc(int col, int row)
{
    engine_dirty_rect_mark_tile_64(DIRTY, col, row);
    return (row < 0 ? 0 : row) * 16 + 64;
}

/* Port of FUN_00452628 (56 B) — an 80 × 48 px box at tile (col, row), no clamp. The callers push a third
 * argument; retail zeroes that slot and never reads it. */
int rl_452628(int col, int row, int unused)
{
    (void)unused;
    engine_dirty_rect_mark_tile_80x48_noclamp(DIRTY, col, row);
    return row * 16 + 48;
}

/* Port of FUN_0045270a (43 B) — mark below the status bar (0, 24)..(478, 479); DAT_0053e9ac = 1. */
int rl_45270a(void)
{
    engine_dirty_rect_mark_below_status_bar(DIRTY);
    return 479;
}

/* Port of FUN_0043cc56 (16 B) — a thunk: 0x45270a. */
int rl_43cc56(void)
{
    return rl_45270a();
}

/* Port of FUN_004527a6 (101 B) — a w × h tile box at pixel (x, y) (negative clamped to 0); force > 1 sets the
 * mark to 2 (a full repaint). */
int rl_4527a6(int x, int y, int w, int h, int force)
{
    engine_dirty_rect_mark_box_tiled(DIRTY, x, y, w, h, force);
    return (y < 0 ? 0 : y) + 16 * h;
}

/* Port of FUN_00452775 (49 B) — a pressed button's mark: the scene flag DAT_005651ac = 1, then 0x4527a6. */
int rl_452775(int x, int y, int w, int h, int force)
{
    g_tr_scene_flag_a = 1;
    return rl_4527a6(x, y, w, h, force);
}

/* ================================================================ records */
#define CO8(c, va)    AT(uint8_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))           /* county record */
#define CO16(c, va)   AT(int16_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define CO32(c, va)   AT(int32_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define AR8(i, va)    AT(uint8_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))      /* army record */
#define AR16(i, va)   AT(int16_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
#define AR32(i, va)   AT(int32_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
#define PL8(p, va)    AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))   /* realm record */
#define PL32(p, va)   AT(int32_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define COAL8(g, va)  AT(uint8_t, g_coalition_tbl, 0x568240u, (int32_t)(va) + 28 * (g))           /* coalition group (17 × 28 B) */
#define COAL32(g, va) AT(int32_t, g_coalition_tbl, 0x568240u, (int32_t)(va) + 28 * (g))
#define LOCAL         ((int)s_slot_0057c8cc)                                                       /* DAT_0057c8cc: the local player */
#define BELOW_STATUS() (DIRTY->below_status_redraw)                                                /* DAT_0053e9ac (a mirror) */

/* other partitions' globals (declared here as in their headers) */
extern int32_t g_rl_55cea0[8385];    /* DAT_0055cea0 (rl_ui2_globals.h): the 129 × 65 strategic tile grid */
extern int32_t g_rl_53f0dc;          /* DAT_0053f0dc (rl_glue_globals.h): the recenter's staged tile */

/* ================================================================ the realm passes (route a: engine_realm.c) */
int  realm_mood_delta(int realm);                 /* engine_realm.c (extern for these wrappers) */
int  coalition_army_grouped(int a);
int  coalition_try_join(int a);
int  coalition_create_group(int a);
void realm_refresh_player_income_totals(void);
void realm_dev_roll_accumulator(int realm);
void realm_dev_spread_pool(int realm);
int  prod_capacity_clamp(int key, int value);     /* engine_army_economy.c */
int  prod_yield_bonus(int key, int value);

/* Port of FUN_00449d6e (389 B) — the season's mood delta by the county's disposition (+0x21e): season 2
 * {4, 2, -8, _, -12}, season 4 {_, -2, -4, -6, -10}, else 0. */
int rl_449d6e(int county)
{
    return realm_mood_delta(county);
}

/* Port of FUN_0044b4d0 (203 B) — is county `a` a member of a coalition group keyed on its owner (+5)? */
int rl_44b4d0(int a)
{
    return coalition_army_grouped(a);
}

/* Port of FUN_0044b391 (319 B) — join county `a` to its owner's first group holding a neighbour (0x467e2c). */
int rl_44b391(int a)
{
    return coalition_try_join(a);
}

/* Port of FUN_0044b302 (143 B) — seed a new coalition group (the first empty of 17) with county `a`. */
int rl_44b302(int a)
{
    return coalition_create_group(a);
}

/* Port of FUN_0044b042 (704 B) — build the coalition groups (the owners' connected county sets; 100 rounds
 * at most), then each group's strength (the members' +0x24) and the count DAT_00553ef4. */
int rl_44b042(void)
{
    engine_realm_build_coalitions();
    return 0;
}

/* Port of FUN_0044ba35 (181 B) — every realm's income total (+0x15c) = the sum of its counties' +0xc0. */
int rl_44ba35(void)
{
    realm_refresh_player_income_totals();
    return 0;
}

/* Port of FUN_0044bfd5 (190 B) — roll county +0x208 by 6·(+0x1ff) − 3·(+0x201), clamp ±100; 0 while
 * DAT_0053f25c is 0. */
int rl_44bfd5(int county)
{
    realm_dev_roll_accumulator(county);
    return 0;
}

/* Port of FUN_0044c093 (485 B) — spread the county's pool (+0xdc) over its developed cells from the
 * fullest (0x44c5fc / 0x44c53b), 200 a cell, the cell's stage stamped by its value. */
int rl_44c093(int county)
{
    realm_dev_spread_pool(county);
    return 0;
}

/* Port of FUN_0044d281 (130 B) — scale `value` by (+0x201 / +0x202) when +0x201 < +0x202. */
int rl_44d281(int county, int value)
{
    return prod_capacity_clamp(county, value);
}

/* Port of FUN_0044d303 (113 B) — value + value · (+0x208 / 2) %. */
int rl_44d303(int county, int value)
{
    return prod_yield_bonus(county, value);
}

/* Port of FUN_00450fb4 (173 B) — the smaller headroom of the county's reserve pairs 1 / 2 (0 without +0x1c3). */
int rl_450fb4(int county)
{
    return engine_army_economy_secondary_headroom(county);
}

/* Port of FUN_0044c6c4 (182 B) — develop up to `count` of the county's empty cells to stage 0x19 (cells
 * already at 0x19+ count too). EAX: the caller's or a callee's (nobody reads it). */
int rl_44c6c4(int county, int count)
{
    engine_territory_develop_empty_cells(county, count);
    return 0;
}

/* Port of FUN_004651b9 (91 B) — zero army +0x14c / +0x153 for armies 1..150. */
int rl_4651b9(void)
{
    engine_territory_clear_size_state();
    return 0;
}

/* Port of FUN_0044ae51 (497 B) — keep each realm's strongest coalition group (ties: the later one) and
 * demolish every county of its other groups (0x4ac3c6); the local player is told (event 127 with the county
 * when one fell, else 128) when it had more than one group. Route b: the old port posts into a port queue. */
int rl_44ae51(void)
{
    for (int p = 1; p < 6; p++) {
        if (PL8(p, 0x57bf04) == 0)
            continue;
        int best = 0, best_g = 0, groups = 0, fell = 0, last = 0;
        for (int g = 0; g < 17; g++) {
            if (COAL8(g, 0x568244) == p) {
                groups++;
                if (best <= COAL32(g, 0x568240)) {
                    best = COAL32(g, 0x568240);
                    best_g = g;
                }
            }
        }
        for (int g = 0; g < 17; g++) {
            if (COAL8(g, 0x568244) != p || g == best_g)
                continue;
            for (int m = 0; m < 20; m++) {
                int c = COAL8(g, 0x568245 + m);
                if (c != 0) {
                    rl_4ac3c6(c);   /* rl_link: engine_combat_demolish_realm (a port queue) */
                    last = c;
                    fell++;
                }
            }
        }
        if (groups > 1 && LOCAL == p) {
            if (fell == 1)
                rl_472bc5(0, LOCAL, 127, 0, 0, last, 0, 0);
            else
                rl_472bc5(0, LOCAL, 128, 0, 0, 0, 0, 0);
        }
    }
    return 0;
}

/* ================================================================ the map recenter */

/* Port of FUN_0043278b (228 B) — find `tile` in the 129 × 65 tile grid DAT_0055cea0 (row-major); in the near
 * view (DAT_0057cb18 == 0) put the scroll at (col − 4, (row & ~1) − 12); clamp the scroll (0x429b1d); latch
 * DAT_0057cac4 and DAT_0053e9ac; 2 (0 when the tile is absent). Route b: the old port staged nothing, clamped
 * with zero view counts and skipped the clamp's own globals. */
int rl_43278b(int tile)
{
    int row, col = 0;
    for (row = 0; row < 129; row++) {
        for (col = 0; col < 65; col++)
            if (g_rl_55cea0[65 * row + col] == tile)
                break;
        if (col < 65)
            break;
    }
    if (row >= 129)
        return 0;
    if (s_slot_0057cb18 == 0) {
        s_slot_005651b4 = (uint32_t)(col - 4);
        s_slot_005651b8 = (uint32_t)((row & 0xfffe) - 12);
    }
    rl_429b1d();
    g_rl_57cac4 = 1;
    BELOW_STATUS() = 1;
    return 2;
}

/* Port of FUN_00432746 (69 B) — recenter on county `c`'s capital tile (+0x70, staged in DAT_0053f0dc); select
 * the county (DAT_0057c93c). EAX: the tile (0) or `c`. */
int rl_432746(int c)
{
    g_rl_53f0dc = CO32(c, 0x53fa20);
    if (g_rl_53f0dc == 0)
        return 0;
    rl_43278b(g_rl_53f0dc);
    s_slot_0057c93c = (uint32_t)c;
    return c;
}

/* ================================================================ the frame driver's steps */
#define VT(obj, off) (*(void **)(uintptr_t)(*(volatile uint32_t *)(uintptr_t)(obj) + (off)))
extern int32_t g_rl_4e65a0;          /* DAT_004e65a0 (rl_ui3_globals.h): the window width */
extern int32_t g_rl_591510;          /* DAT_00591510 (rl_render_globals.h): the animation gate */
extern uint8_t g_rl_4eabd0[0x5];     /* DAT_004eabd0 (rl_io_globals.h): the primary surface (IDirectDrawSurface *) */
extern int32_t g_rl_53f228;          /* DAT_0053f228 (rl_io_globals.h): full screen */
extern int32_t g_rl_4df28c;          /* DAT_004df28c (rl_io_globals.h): the movie present gate */

/* Port of FUN_00451f1a (81 B) — the 16-frame animation counter DAT_0053e9a4, stepped while DAT_00591510. */
int rl_451f1a(void)
{
    if (g_rl_591510 != 0) {
        if (++g_mv_anim_tick >= 16)
            g_mv_anim_tick = 0;
        if (g_mv_anim_tick < 0)
            g_mv_anim_tick = 0;
    }
    return 0;
}

/* Port of FUN_00451fcc (385 B) — the strategic view mode (DAT_0057cb18: 0 near, 1 far, 2 mid) and its
 * camera scalars: step, columns, rows, the left clip, the top row's y, cell width, the feature bias, half a
 * cell, row height; DAT_0055cd60 = clip + cols · width; EAX = DAT_00553254 = y0 + height · (rows + 1).
 * A mode other than 0..2 keeps the previous scalars. */
int rl_451fcc(int mode)
{
    s_slot_0057cb18 = (uint32_t)mode;
    if (mode == 0) {
        g_rl_55cd48 = 1; g_mv_col_count = 8; g_mv_row_count = 30; g_gfx_alt_clip_left = 0; g_mv_cam_y0 = 9;
        g_mv_x_step = 60; g_mv_y_viewport = 30; g_mv_x_half = 30; g_mv_y_step = 15;
    } else if (mode == 1) {
        g_rl_55cd48 = 2; g_mv_col_count = 17; g_mv_row_count = 64; g_gfx_alt_clip_left = 4; g_mv_cam_y0 = 17;
        g_mv_x_step = 28; g_mv_y_viewport = 14; g_mv_x_half = 14; g_mv_y_step = 7;
    } else if (mode == 2) {
        g_rl_55cd48 = 4; g_mv_col_count = 40; g_mv_row_count = 128; g_gfx_alt_clip_left = 0; g_mv_cam_y0 = 21;
        g_mv_x_step = 12; g_mv_y_viewport = 6; g_mv_x_half = 6; g_mv_y_step = 3;
    }
    g_rl_55cd60 = g_gfx_alt_clip_left + g_mv_col_count * g_mv_x_step;
    g_gfx_alt_clip_bottom = g_mv_cam_y0 + g_mv_y_step * (g_mv_row_count + 1);
    return g_gfx_alt_clip_bottom;
}

/* Port of FUN_00452160 (422 B) — the rate-gated present: unless a movie owns the screen (0x42e26f and
 * DAT_004df28c), the frame budget DAT_004eabe8 += 0x4b27bf's step (never below 0; 50 when forced or the scene
 * flag DAT_005651ac is up); at 50 (and not while DAT_0056d650 with DAT_00553008 > 0) it resets both and, while
 * the dirty mark is up, copies the back surface DAT_004eafac to the primary over the WHOLE rect Point
 * (BltFast at (0, 0) full screen, Blt DDBLT_WAIT windowed) — retail stages the dirty rect into a local and never
 * passes it (quirk) — and counts the mark down; at 0 the rect is reset to empty (canvas − 1, 0).
 * EAX (0x451f70 returns it): 0x42e26f's, the step, Point.bottom, the blit's HRESULT or canvas height − 1. */
int rl_452160(int force)
{
    int r = rl_42e26f();
    if (r != 0 && g_rl_4df28c != 0)
        return r;
    r = rl_4b27bf();
    g_rl_4eabe8 += r;
    if (g_rl_4eabe8 < 0)
        g_rl_4eabe8 = 0;
    if (force != 0)
        g_rl_4eabe8 = 50;
    if (g_tr_scene_flag_a != 0)
        g_rl_4eabe8 = 50;
    if (g_rl_4eabe8 < 50)
        return r;
    if (g_rl_56d650 != 0 && g_rl_553008 > 0)
        return r;
    g_rl_4eabe8 = 0;
    g_tr_scene_flag_a = 0;
    RECT *pt = (RECT *)(void *)g_rl_4ea180;
    engine_dirty_rect_state_t *d = DIRTY;
    if (d->dirty_mark <= 0)
        return (int)pt->bottom;
    uint32_t prim = AT(uint32_t, g_rl_4eabd0, 0x4eabd0u, 0x4eabd0u);
    if (g_rl_53f228 != 0) {
        typedef HRESULT (WINAPI *bltfast_t)(uint32_t, DWORD, DWORD, uint32_t, RECT *, DWORD);
        r = (int)((bltfast_t)VT(prim, 0x1c))(prim, 0, 0, (uint32_t)g_rl_4eafac, pt, 0x10);   /* DDBLTFAST_WAIT */
    } else {
        typedef HRESULT (WINAPI *blt_t)(uint32_t, RECT *, uint32_t, RECT *, DWORD, void *);
        r = (int)((blt_t)VT(prim, 0x14))(prim, pt, (uint32_t)g_rl_4eafac, NULL, 0x1000000, NULL);   /* DDBLT_WAIT */
    }
    if (--d->dirty_mark <= 0) {
        d->dirty_mark = 0;
        d->dirty_xmin = g_gfx_canvas_width - 1;
        d->dirty_ymin = g_gfx_canvas_height - 1;
        d->dirty_xmax = 0;
        d->dirty_ymax = 0;
        return g_gfx_canvas_height - 1;
    }
    return r;
}

/* ================================================================ the in-game input's helpers */

/* Port of FUN_00432640 (262 B) — the cursor region DAT_005534f0 (screens below 8 only): 1 over the status bar
 * (y < 24), 2 inside the map viewport (x in [DAT_0056d698, + DAT_0056d694), y in [DAT_0056d69c (− 24 on the
 * strategic map), + DAT_0056d674)), else 0. */
int rl_432640(void)
{
    if (engine_current_screen() >= 8)
        return 0;
    g_rl_5534f0 = 0;
    if (g_bf_4e65f4 < 24) {
        g_rl_5534f0 = 1;
        return 0;
    }
    if (g_rl_56d698 <= g_bf_4e65fc && g_rl_56d694 + g_rl_56d698 > g_bf_4e65fc) {
        int top = s_slot_0057c8fc != 0 ? g_rl_56d69c : g_rl_56d69c - 24;
        if (top <= g_bf_4e65f4 && g_rl_56d674 + g_rl_56d69c > g_bf_4e65f4)
            g_rl_5534f0 = 2;
    }
    return 0;
}

/* Port of FUN_00432967 (61 B) — the HUD strip's hotspots (6 at (478, 430), DAT_004dc680) through 0x40e3ee:
 * 1 when one fired. */
int rl_432967(void)
{
    return rl_40e3ee(478, 430, (int)(uintptr_t)g_rl_4dc680, 6) != 0;
}

/* Port of FUN_00431f59 (712 B) — one edge-scroll step in direction DAT_005533a4 (0 N … 7 NW): on the strategic
 * map (DAT_0057c8fc == 0) the cell scroll (DAT_005651b4 x step, DAT_005651b8 2 × step), in the battle view
 * (== 2) the anchor (DAT_0055ce70 / DAT_005678a0, 1); then DAT_0057cac4 = 1 and the rate gate (0x4bbbe3):
 * passed → DAT_0053e9ac = 1 + the below-status mark (0x45270a), else everything put back. A direction past 7
 * does nothing. */
int rl_431f59(void)
{
    int sx = (int)s_slot_005651b4, sy = (int)s_slot_005651b8;
    int ax = (int)s_slot_0055ce70, ay = (int)s_slot_005678a0;
    int dir = g_rl_5533a4, st = g_rl_55cd48;
    /* the compass: N, NE, E, SE, S, SW, W, NW (y up = −1) */
    static const int8_t ky[8] = { -1, -1, 0, 1, 1, 1, 0, -1 }, kx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    if (s_slot_0057c8fc == 0) {            /* the cell scroll: y by 2 × step, x by step */
        if (dir < 0 || dir > 7)
            return 0;
        if (ky[dir]) s_slot_005651b8 = (uint32_t)((int)s_slot_005651b8 + 2 * st * ky[dir]);
        if (kx[dir]) s_slot_005651b4 = (uint32_t)((int)s_slot_005651b4 + st * kx[dir]);
    } else if (s_slot_0057c8fc == 2) {     /* the battle anchor: 1 */
        if (dir < 0 || dir > 7)
            return 0;
        if (ky[dir]) s_slot_005678a0 = (uint32_t)((int)s_slot_005678a0 + ky[dir]);
        if (kx[dir]) s_slot_0055ce70 = (uint32_t)((int)s_slot_0055ce70 + kx[dir]);
    }
    g_rl_57cac4 = 1;
    if (rl_4bbbe3() != 0) {
        BELOW_STATUS() = 1;
        rl_45270a();
    } else {
        s_slot_005651b4 = (uint32_t)sx;
        s_slot_005651b8 = (uint32_t)sy;
        s_slot_0055ce70 = (uint32_t)ax;
        s_slot_005678a0 = (uint32_t)ay;
        g_rl_57cac4 = 0;
    }
    return 0;
}

/* Port of FUN_00432221 (477 B) — the edge scroll: DAT_005533a4 = 8; nothing while a scroll is open
 * (DAT_0053e910) or on the strategic map's mid view; else the cursor (DAT_004e6594 / 98) at a window edge
 * (≤ 0 or ≥ size − 1; corners first) picks the direction and steps (0x431f59): 1. */
int rl_432221(void)
{
    g_rl_5533a4 = 8;
    if (g_rl_53e910 != 0)
        return 0;
    if (s_slot_0057c8fc == 0 && s_slot_0057cb18 == 2)
        return 0;
    int left = g_rl_4e6594 <= 0, right = g_rl_4e65a0 - 1 <= g_rl_4e6594;
    int top = g_rl_4e6598 <= 0, bottom = g_rl_4ea838 - 1 <= g_rl_4e6598;
    if (left && top)          g_rl_5533a4 = 7;
    else if (left && bottom)  g_rl_5533a4 = 5;
    else if (right && top)    g_rl_5533a4 = 1;
    else if (right && bottom) g_rl_5533a4 = 3;
    else if (left)            g_rl_5533a4 = 6;
    else if (top)             g_rl_5533a4 = 0;
    else if (bottom)          g_rl_5533a4 = 4;
    else if (right)           g_rl_5533a4 = 2;
    else                      g_rl_5533a4 = 8;
    if (g_rl_5533a4 == 8)
        return 0;
    rl_431f59();
    return 1;
}

/* ================================================================ the turn */
extern uint8_t g_rl_5aeb8c;          /* DAT_005aeb8c (rl_io_globals.h): a game is being loaded (not new) */
extern uint8_t g_rl_5530e0[8];       /* DAT_005530e0 (rl_io_globals.h): the MP seats taken */
extern int32_t g_rl_553248;          /* DAT_00553248 (rl_io_globals.h): the MP host */
extern uint8_t g_rl_4e2a28[0x8];     /* DAT_004e2a28 (rl_io_globals.h): the local seat stamp */
#define MP_ON (g_sim_mp_active != 0)  /* DAT_00553030 */

/* Port of FUN_004329ec (281 B) — start the game: in MP (unless loading) only once DAT_00553f98 (≥ 2) seats are
 * taken; single player: a new game is built (0x497ced, screen 0) unless loading, the engine state 3, the full
 * repaint 0x451f8a; MP: the system sheets 0 / 1 and message 4. Then the host's 0x4b7514, redraw 2, the loading
 * flag cleared, DAT_005440c8 = DAT_0053f26c, DAT_00568d9c = timeGetTime. EAX: the caller's (nobody reads it). */
int rl_4329ec(void)
{
    if (MP_ON && g_rl_5aeb8c == 0) {
        int n = 0;
        for (int v = 1; v < 6; v++)
            if (g_rl_5530e0[v] != 0)
                n++;
        if ((int)s_slot_00553f98 < 2 || n < (int)s_slot_00553f98)
            return 0;
    }
    if (!MP_ON) {
        if (g_rl_5aeb8c == 0) {
            rl_497ced();
            engine_set_current_screen(0);
        }
        g_engine_state = 3;
        rl_451f8a();
    } else {
        rl_499a1c(0);
        rl_499a1c(1);
        rl_43eda0(4, 0);
    }
    if (g_rl_553248 != 0)
        rl_4b7514();   /* the MP host's session step */
    engine_set_demand_redraw(2);
    g_rl_5aeb8c = 0;
    s_slot_005440c8 = s_slot_0053f26c;
    g_combat_anim_deadline = rl_plat_timeGetTime();
    return 0;
}

/* Port of FUN_0043ad25 (256 B) — close the open sub-screen: screens 10 / 23 / 13 / 27 / 32 / 8 / 12 free their
 * sheet set (0x4984dc), 34 also stops its movie (0x42df30) — those repaint mode 1, else 2; screen 0, the
 * dispatch (0x40f1a0), the scene tick (0x4050c0), the full mark and the present (0x452160(0)). */
int rl_43ad25(void)
{
    int mode = 2;
    int s = engine_current_screen();
    if (s == 10 || s == 23 || s == 13 || s == 27 || s == 32 || s == 8 || s == 12) {
        mode = 1;
        rl_4984dc();
    } else if (s == 34) {
        rl_42df30();   /* the screen-34 movie stop */
        mode = 1;
        rl_4984dc();
    }
    engine_set_current_screen(0);
    rl_40f1a0(mode);
    rl_4050c0();       /* rl_link: engine_map_scene_tick */
    rl_45240a();
    rl_452160(0);
    return 0;
}

/* Port of FUN_0043ac23 (258 B) — End Turn: a scroll open is dismissed (0x476768); unless the local realm
 * already ended (+0 == 999): close the sub-screen (0x43ad25); MP: the turn vote cleared and message 30 sent
 * twice 50 ms apart, else the realm marked 999; DAT_005530d0 = the ranking dirty = 1; the end-turn phase
 * DAT_0055403c = 1 (2 in single player); DAT_004ee8c0 = timeGetTime. */
int rl_43ac23(void)
{
    if (g_rl_53e910 != 0)
        rl_476768();
    if (PL32(LOCAL, 0x57bf00) == 999)
        return 0;
    rl_43ad25();
    if (MP_ON) {
        for (int i = 0; i < 10; i++)
            g_combat_anim_scratch[i] = 0;
        rl_43eda0(30, 0);
        bf_sleep(50);
        rl_43eda0(30, 0);
    } else {
        PL32(LOCAL, 0x57bf00) = 999;
    }
    g_combat_flag_530d0 = 1;
    engine_army_ai_set_ranking_dirty(1);
    g_bf_55403c = 1;
    g_rl_4ee8c0 = (int32_t)rl_plat_timeGetTime();
    if (!MP_ON)
        g_bf_55403c = 2;
    return 0;
}

/* Port of FUN_00448440 (261 B) — the turn advance: the AI turn, an RNG step, the turn count, the season
 * (DAT_0057c934 ← DAT_0057c92c, which cycles 1..4; DAT_00554470 the previous), each winter the year
 * (DAT_0055cd5c, DAT_00553edc / DAT_00553e74 / DAT_0057c900) and the CD probe, then the 27 realm / economy
 * passes in retail's order. Callees whose old port takes a port queue: their retail-layout entries (src/rl_link_logic.c). */
int rl_448440(void)
{
    engine_army_ai_run_turn();                         /* 0x49a990 */
    bf_rng_step();                                     /* 0x404a46 */
    s_slot_00553240++;
    s_slot_00554470 = s_slot_0057c934;
    s_slot_0057c934 = s_slot_0057c92c++;
    if ((int)s_slot_0057c92c > 4)
        s_slot_0057c92c = 1;
    if (s_slot_0057c934 == 4) {
        s_slot_0055cd5c++;
        s_slot_0057c900 = s_slot_00553edc;
        s_slot_00553edc = s_slot_00553e74++;
        rl_4aef7e();
    }
    rl_49b2a3();                  /* rl_link: engine_combat_mp_resign_sweep (a port queue) */
    engine_ranking_snapshot_player_metrics();          /* 0x4486a9 */
    rl_448819();                  /* rl_link: engine_realm_roll_events (its seed argument) */
    rl_449889();                  /* rl_link: engine_realm_update_mood (a port queue) */
    engine_realm_accrue_income();                      /* 0x44b59b */
    rl_4acbd4();                  /* rl_link: engine_realm_pay_upkeep (a port queue) */
    engine_realm_settle_provisions();                  /* 0x44bf04 */
    engine_realm_apply_tax_approval();                 /* 0x44bd9f */
    engine_realm_advance_rating();                     /* 0x44baea */
    rl_44aa41();                  /* rl_link: engine_realm_escalate_unrest (a port queue) */
    rl_44ae3c();                  /* rl_link: engine_realm_coalition_pass (a port queue) */
    engine_territory_census_all_realms();              /* 0x469b51 */
    engine_realm_spread_development();                 /* 0x44bf7a */
    engine_army_economy_apply_season_pass();           /* 0x44c8ae */
    engine_army_economy_settle_stock_a();              /* 0x44d60d */
    engine_army_economy_resource_pass();               /* 0x44e852 */
    rl_4508de();                  /* rl_link: engine_realm_complete_settlements (a port queue) */
    engine_territory_allocate_all_workforce();         /* 0x44f699 */
    engine_realm_migrate_population();                 /* 0x44a6ba */
    engine_realm_advance_population();                 /* 0x449ef3 */
    engine_realm_recount_armies();                     /* 0x451061 */
    rl_428471();
    engine_realm_collect_territory_income();           /* 0x4ad6c0 */
    engine_realm_clear_turn_scratch();                 /* 0x448d16 */
    engine_territory_allocate_all_workforce();         /* 0x44f699 */
    engine_realm_record_history();                     /* 0x4ae7dd */
    engine_realm_replan_economies();                   /* 0x448545 */
    return 0;
}

/* Port of FUN_004514f8 (147 B) — county `c`'s workforce ratios: +0x130 33, +0x134 50, +0x138 17, +0x148 100,
 * +0x13c / +0x140 / +0x144 / +0x14c 0. EAX: c << 8. */
int rl_4514f8(int c)
{
    CO32(c, 0x53fae0) = 33;
    CO32(c, 0x53fae4) = 50;
    CO32(c, 0x53fae8) = 17;
    CO32(c, 0x53faf8) = 100;
    CO32(c, 0x53faf4) = 0;
    CO32(c, 0x53faf0) = 0;
    CO32(c, 0x53fafc) = 0;
    CO32(c, 0x53faec) = 0;
    return c << 8;
}

/* Port of FUN_00451150 (936 B) — every county's new-game baseline (DAT_00554020 = 1): the disposition by index
 * (1..3 → 0, 4..5 → 1, 6..9 → 2, 10..11 → 3, 12+ → 4), unowned, population 150 (+0x24 / +0x28) and its band,
 * the standing / ration / tax / health seeds, the workforce ratios (0x4514f8), the stock rows cleared; then the
 * terrain census, the feature grades, the candidate, the stock, the workforce and the provision (linked
 * ports). */
int rl_451150(void)
{
    s_slot_00554020 = 1;
    for (int i = 1; i <= (int)s_slot_0056d5dc; i++) {
        CO8(i, 0x53fbcb) = 3;
        CO8(i, 0x53fbcd) = 50;
        CO8(i, 0x53fbce) = i < 4 ? 0 : i < 6 ? 1 : i < 10 ? 2 : i < 12 ? 3 : 4;
        CO8(i, 0x53f9b5) = 0;
        CO32(i, 0x53f9d8) = 150;
        CO32(i, 0x53f9d4) = CO32(i, 0x53f9d8);
        CO8(i, 0x53fa68) = (uint8_t)((CO32(i, 0x53f9d4) - 1) / 25 + 1);
        CO8(i, 0x53fb0e) = 3;
        CO8(i, 0x53fb0f) = 100;
        CO8(i, 0x53f9ba) = 2;
        CO8(i, 0x53f9b9) = CO8(i, 0x53f9ba);
        CO8(i, 0x53f9bb) = 50;
        CO8(i, 0x53f9bd) = 50;
        CO8(i, 0x53f9bc) = CO8(i, 0x53f9bd);
        CO8(i, 0x53f9c8) = 0;
        CO32(i, 0x53f9cc) = 0;
        CO32(i, 0x53fbb8) = 0;
        CO8(i, 0x53fbae) = (i & 1) ? 1 : 0;
        CO8(i, 0x53fc40) = 4;
        CO8(i, 0x53f9b8) = 25;
        CO32(i, 0x53fc04) = 40;
        CO32(i, 0x53fc00) = CO32(i, 0x53fc04);
        CO32(i, 0x53fbd8) = 100;
        CO32(i, 0x53fbd4) = CO32(i, 0x53fbd8);
        rl_4514f8(i);
        for (int j = 0; j < 4; j++)
            CO8(i, 0x53fc47 + 24 * j) = 0;
        for (int k = 0; k < 9; k++) {
            CO32(i, 0x53fa7c + 12 * k) = 0;
            CO32(i, 0x53fa78 + 12 * k) = CO32(i, 0x53fa7c + 12 * k);
            CO32(i, 0x53fa74 + 12 * k) = CO32(i, 0x53fa78 + 12 * k);
        }
        engine_territory_census_army_terrain(i);                       /* 0x469b8d */
        engine_territory_grade_feature_cells(i);                       /* 0x44d913 */
        engine_army_economy_select_candidate(i, (int)s_slot_0057c92c); /* 0x44dd4d */
        engine_army_economy_distribute_stock(i);                       /* 0x44c278 */
        engine_territory_allocate_realm_workforce(i);                  /* 0x44f6e7 */
        engine_army_economy_provision(i, (int)s_slot_0057c934);        /* 0x44df5f */
    }
    return 0;
}

/* Port of FUN_0043e9e2 (223 B) — the session's seats: every realm's aux +0 cleared, realms with +5 set taken
 * off +4; the local realm's aux +0 = DAT_004e2a28 and +4 = 1; MP: message 15. */
int rl_43e9e2(void)
{
    for (int i = 1; i < 6; i++)
        AT(int32_t, g_player_aux_44, 0x553d50u, 0x553d50 + 44 * i) = 0;
    for (int j = 1; j < 6; j++)
        if (PL8(j, 0x57bf05) != 0)
            PL8(j, 0x57bf04) = 0;
    AT(int32_t, g_player_aux_44, 0x553d50u, 0x553d50 + 44 * LOCAL) = AT(int32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a28u);
    PL8(LOCAL, 0x57bf04) = 1;
    if (MP_ON)
        return rl_43eda0(15, 0);
    return 352 * LOCAL;
}

/* Port of FUN_00448648 (97 B) — replan resource 2 (record 7, 15, 4) for every county of `owner` (0x44f318). */
int rl_448648(int owner)
{
    for (int i = 1; i <= (int)s_slot_0056d5dc; i++)
        if (CO8(i, 0x53f9b5) == (uint32_t)owner)
            engine_army_economy_plan_resource(i, 2, 7, 15, 4);
    return 0;
}

/* ================================================================ the map clicks */
extern int32_t g_rl_568974;          /* DAT_00568974 (rl_glue_globals.h): the cell under the cursor (0x429ba4) */
extern int32_t g_rl_57d338;          /* DAT_0057d338 (rl_glue_globals.h): bytes per cell */
extern int32_t g_rl_52afd4;          /* DAT_0052afd4 (rl_glue_globals.h): cells per row */
extern int32_t g_rl_56795c;          /* DAT_0056795c (rl_ui2_globals.h): the cell byte offset under the cursor */
extern int32_t g_rl_553018;          /* DAT_00553018 (rl_glue_globals.h) */

/* the cursor cell's split, as 0x43caf4 / 0x43ce1a store it after 0x429ba4(1) */
static void cursor_cell_split(void)
{
    g_rl_57a0d4 = g_rl_568974;
    g_rl_53f064 = g_rl_568974 / g_rl_57d338;
    g_bf_53e98c = g_rl_53f064 % g_rl_52afd4;
    g_bf_53e99c = g_rl_53f064 / g_rl_52afd4;
    g_rl_56795c = g_rl_568974;
}

/* Port of FUN_0043caf4 (354 B) — the map RMB (strategic only): the cell under the cursor (0x429ba4(1), split
 * into DAT_0053f064 / DAT_0053e98c / DAT_0053e99c); a diplomacy pick pending (DAT_0052afa4) or the mid view
 * closes instead (the latter through 0x4350a1); else the cell's terrain info (0x46d5fe), the army remembered
 * (DAT_00553e68), a scroll dismissed, the info mode 3, a county other than the selected one recentred and
 * selected (+ the menu redraw byte and the scene tick), then the info panel (0x41b032) and 0x4b37bc. */
int rl_43caf4(void)
{
    if (s_slot_0057c8fc != 0)
        return 0;
    rl_429ba4(1);
    cursor_cell_split();
    if (g_diplo_action_taken != 0) {
        engine_set_current_screen(0);
        g_diplo_action_taken = 0;
        return 0;
    }
    if (s_slot_0057cb18 == 2) {
        engine_set_current_screen(0);
        rl_4350a1();
        return 0;
    }
    rl_46d5fe();
    if (g_mv_ti_county != 0)
        g_bf_553e68 = g_mv_ti_county;
    if (g_rl_53e910 != 0)
        rl_476768();
    g_rl_5681cc = 3;
    if (g_mv_ti_head != 0 && g_mv_ti_head != (int)s_slot_0057c93c && g_mv_ti_county == 0) {
        g_rl_53f0dc = CO32(g_mv_ti_head, 0x53fa20);
        if (g_rl_53f0dc != 0) {
            s_slot_0057c93c = (uint32_t)g_mv_ti_head;
            rl_43278b(g_rl_53f0dc);
        }
        g_gfx_menu_redraw_a = 1;
        engine_map_scene_tick_frame();               /* 0x4050c0 */
    }
    rl_41b032();                /* rl_link: engine_terrain_info_panel (its env) */
    rl_4b37bc();
    return 0;
}

/* Port of FUN_0043723a (245 B) — the move / merge handoff (also the confirm dialog's callback): redraw 2; with
 * the pending flag DAT_0059151c: the move costs rebuilt (0x46ff43), screen 0x10 for army DAT_0052afbc
 * (DAT_0057c8c4), 0x4a8d9c, the cooldown DAT_00553ecc = 40, army +0x14e cleared, the path field from its cell
 * (+0x0a / +0x0b, signed) for the local realm (0x46f700), and its tile split like the cursor's; else screen 0. */
int rl_43723a(void)
{
    engine_set_demand_redraw(2);
    if (g_bf_59151c[0] == 0) {
        engine_set_current_screen(0);
        return 0;
    }
    rl_46ff43();
    engine_set_current_screen(16);
    g_mv_active_county = g_mv_ti_county;
    rl_4a8d9c();
    g_rl_553ecc = 40;
    AR8(g_mv_active_county, 0x52f1fe) = 0;
    rl_46f700(LOCAL, (int8_t)AR8(g_mv_active_county, 0x52f0ba), (int8_t)AR8(g_mv_active_county, 0x52f0bb), 0);
    g_rl_53f064 = AR32(g_mv_active_county, 0x52f0bc) / g_rl_57d338;
    g_bf_53e98c = g_rl_53f064 % g_rl_52afd4;
    g_bf_53e99c = g_rl_53f064 / g_rl_52afd4;
    return g_bf_53e99c;
}

/* Port of FUN_004371ce (108 B) — an own army clicked with nothing linked: the below-status mark (0x43cc56),
 * screen 0, redraw 2, the pending flag DAT_0059151c = 1; a linked army (+0x199) asks first (dialog 13 at
 * (160, 160), its callback retail's 0x43723a — the dialog stores the VA), else the handoff now. */
int rl_4371ce(void)
{
    rl_43cc56();
    engine_set_current_screen(0);
    engine_set_demand_redraw(2);
    g_bf_59151c[0] = 1;
    if (AR8(g_mv_ti_county, 0x52f249) != 0)
        return rl_40e6f2(13, 160, 160, 0x43723a);
    return rl_43723a();
}

/* Port of FUN_0043d309 (675 B) — toggle county `c`'s task `task` (0..3: the +0x297 + 24 task flag; 4: the
 * +0x1b0 flag through 0x4502ca slot 3), re-plan (0x4485a5 / 0x4502ca / the workforce / the provision / the
 * resource cell 0x44edc2 / every county of the owner 0x448648); the local realm's: DAT_005530d0 = 1, the cursor
 * kept (DAT_0053f0a0 / 9c) and event 230 + 2 · task (+1 on) at slot 4. */
int rl_43d309(int c, int task)
{
    int code, slot, on;
    if (task == 4) {
        slot = 3;
        engine_army_ai_rebalance_workforce(c, 3, CO8(c, 0x53fb60) != 0);
        if (CO8(c, 0x53fb60) != 0) {
            CO8(c, 0x53fb60) = 0;
            code = -2;
        } else {
            CO8(c, 0x53fb60) = 1;
            code = -1;
        }
        on = CO8(c, 0x53fb60) != 0;
    } else {
        code = 2 * task;
        CO8(c, 0x53fc47 + 24 * task) ^= 1;
        if (CO8(c, 0x53fc47 + 24 * task) != 0)
            code++;
        slot = task == 0 ? 6 : task == 3 ? 5 : task == 1 ? 4 : 7;
        on = CO8(c, 0x53fc47 + 24 * task) != 0;
    }
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);    /* 0x4485a5 */
    engine_army_ai_rebalance_workforce(c, slot, on);           /* 0x4502ca */
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(c);              /* 0x44f6e7 */
    engine_army_economy_provision(c, (int)s_slot_0057c934);    /* 0x44df5f */
    engine_territory_allocate_realm_workforce(c);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    engine_army_economy_stamp_resource_cell(c, task);          /* 0x44edc2 */
    rl_448648(CO8(c, 0x53f9b5));
    if (CO8(c, 0x53f9b5) == (uint32_t)LOCAL) {
        g_combat_flag_530d0 = 1;
        g_rl_53f0a0 = g_bf_4e65fc;
        g_rl_53f09c = g_bf_4e65f4;
        return rl_472bc5(0, LOCAL, code + 230, 0, 4, 0, 0, 0);
    }
    return c << 8;
}

/* Port of FUN_0043ce1a (1263 B) — the map LMB (strategic only): the cursor cell (0x429ba4(1), split); nothing
 * while the turn runs (DAT_00553fc8) or the end turn is pending in SP (DAT_0055403c, DAT_00553018 0); a
 * diplomacy pick pending closes; not in the mid view; a swallowed click (DAT_0056d64c) is cleared; a scroll
 * open is dismissed (0x476710). Else the cell (0x46d5fe): an own army (kind 1, owner signed == local) with a
 * link (+0x199, re-checked by 0x4a8426) opens the siege prompt 29 (DAT_00568ed0), else 0x4371ce; a castle
 * (kind 3) of an own county opens screen 8 on it (recentred, combat flags 2), a foreign one: event 112; the
 * cell flags (DAT_005440b0): 0x80 an own county's task by the terrain type (DAT_0057cb2c: ≤3 → 1, ≤6 → 3,
 * ≤9 → 2, ≤12 → 0, >20 → 4; 13..20 nothing) through 0x43d309 (MP: message 58), 0x40 the town (screen 2, the
 * workforce render 0x412143), 0x20 the info panel (screen 4). */
int rl_43ce1a(void)
{
    if (s_slot_0057c8fc != 0)
        return 0;
    rl_429ba4(1);
    cursor_cell_split();
    if (g_sim_turn_progress != 0 || (g_bf_55403c != 0 && g_rl_553018 == 0))
        return 0;
    if (g_diplo_action_taken != 0) {
        engine_set_current_screen(0);
        g_diplo_action_taken = 0;
        return 0;
    }
    if (s_slot_0057cb18 == 2)
        return 0;
    if (g_rl_56d64c != 0) {
        g_rl_56d64c = 0;
        return 0;
    }
    if (g_rl_53e910 != 0)
        return rl_476710();
    rl_46d5fe();
    if (g_mv_ti_county != 0 && AR8(g_mv_ti_county, 0x52f0b8) == 1) {
        if ((int8_t)AR8(g_mv_ti_county, 0x52f0b0) == LOCAL) {
            if (AR8(g_mv_ti_county, 0x52f249) != 0)
                rl_4a8426(g_mv_ti_county);
            if (AR8(g_mv_ti_county, 0x52f249) != 0) {
                g_siege_besieger = g_mv_ti_county;
                engine_set_current_screen(29);
                engine_set_demand_redraw(2);
            } else {
                rl_4371ce();
            }
        }
        return 0;
    }
    if (g_mv_ti_county != 0 && AR8(g_mv_ti_county, 0x52f0b8) == 3) {
        if (CO8(g_mv_ti_head, 0x53f9b5) != (uint32_t)LOCAL) {
            rl_472bc5(0, LOCAL, 112, 0, 0, 0, 0, 0);
            return 0;
        }
        s_slot_00553c64 = (uint32_t)g_mv_ti_county;
        if (CO32(g_mv_ti_head, 0x53fa20) == 0)
            return 0;
        s_slot_0057c93c = (uint32_t)g_mv_ti_head;
        rl_43278b(CO32(g_mv_ti_head, 0x53fa20));
        engine_set_current_screen(8);
        engine_set_demand_redraw(1);
        g_combat_flag_530d0 = 2;
        g_combat_flag_567d0c = 2;
        return 0;
    }
    if (g_mv_ti_flags & 0x80) {
        if (CO8(g_mv_ti_head, 0x53f9b5) != (uint32_t)LOCAL) {
            rl_472bc5(0, LOCAL, 112, 0, 0, 0, 0, 0);
            return 0;
        }
        if (g_mv_ti_head != (int)s_slot_0057c93c) {
            if (CO32(g_mv_ti_head, 0x53fa20) == 0)
                return 0;
            s_slot_0057c93c = (uint32_t)g_mv_ti_head;
            rl_43278b(CO32(g_mv_ti_head, 0x53fa20));
        }
        int t = g_mv_ti_type;
        if (t <= 3)       g_rl_55416c = 1;
        else if (t <= 6)  g_rl_55416c = 3;
        else if (t <= 9)  g_rl_55416c = 2;
        else if (t <= 12) g_rl_55416c = 0;
        else if (t > 20)  g_rl_55416c = 4;
        else              return 0;
        rl_43d309(g_mv_ti_head, g_rl_55416c);
        if (MP_ON)
            rl_43eda0(58, 0);
        return 0;
    }
    if (g_mv_ti_flags & 0x40) {
        if (CO8(g_mv_ti_head, 0x53f9b5) != (uint32_t)LOCAL)
            return 0;
        if (CO32(g_mv_ti_head, 0x53fa20) != 0) {
            s_slot_0057c93c = (uint32_t)g_mv_ti_head;
            rl_43278b(CO32(g_mv_ti_head, 0x53fa20));
            engine_map_scene_tick_frame();           /* 0x4050c0 */
            g_combat_flag_530d0 = 1;
        }
        engine_set_current_screen(2);
        rl_412143(1u);        /* rl_link: engine_workforce_hud_render (its localization env) */
        return 0;
    }
    if (g_mv_ti_flags & 0x20) {
        if (CO8(g_mv_ti_head, 0x53f9b5) != (uint32_t)LOCAL)
            return 0;
        g_rl_5681cc = 3;
        engine_set_current_screen(4);
        rl_41b032();            /* rl_link: engine_terrain_info_panel (its env) */
    }
    return 0;
}

/* ================================================================ the title menus' actions (router actions: entry-patched)
 * The router's param0 DAT_0059154c picks the button. */
#define PARAM0   AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu)
#define STRVA(blk, blkva, va) ((int)(uintptr_t)&AT(uint8_t, blk, blkva, va))   /* a retail string, as the int arg */
#define S4D5620(va) STRVA(g_rl_4d5620, 0x4d5620u, va)   /* the menus' strings: s011_02.wav, lom.smk, *.sva … */
extern int32_t g_rl_58fe10;          /* DAT_0058fe10 (rl_ui2_globals.h): the MP lobby result */
extern int32_t g_rl_5c9a74;          /* DAT_005c9a74 (rl_ui2_globals.h): the CD check */
extern int32_t g_rl_55ce74;          /* DAT_0055ce74 (rl_io_globals.h): the name box's clock */
extern int32_t g_rl_568464;          /* DAT_00568464 (rl_io_globals.h): an MP game exists */
extern uint8_t g_rl_4d5508[0x54];    /* DAT_004d5508 (rl_glue_globals.h): DAT_004d5548 the shields' colours (4 B a slot) */

/* Port of FUN_00432b05 (451 B) — the main menu (page 1): param 3 quit (DAT_005aeb68 = 1); 2 single player (the
 * CD check 0x4978ad, the MP probe 0x4b7250: 1 → page 4, the name box (DAT_0053f1e0, 16, 192), the voice
 * s011_02.wav; else page 10 when DAT_0058fe10); 1 multiplayer (page 2, 10 without a CD, seat 0); 4 the intro
 * movie (the stream stopped, lom.smk at (70, 80)). Every press: DAT_0057d320 = 0, DAT_0053f648 = -1,
 * DAT_0055302c = 2, DAT_0058fe10 = 0. */
int rl_432b05(void)
{
    g_rl_57d320 = 0;
    g_rl_53f648 = -1;
    s_slot_0055302c = 2;
    g_rl_58fe10 = 0;
    switch (PARAM0) {
    case 3:
        g_gfx_last_error = 1;
        break;
    case 2:
        engine_set_demand_redraw(2);
        rl_4978ad();
        g_rl_5bb4a0 = 0;
        if (rl_4b7250() == 1) {
            g_tr_p4_continue_disabled = 0;
            g_rl_55ce74 = (int32_t)rl_plat_timeGetTime();
            g_tr_page = 4;
            rl_402009((int)(uintptr_t)g_tr_p4_name_buf, 16, 192, 0);
            rl_401f49();
            s_slot_0055302c = 2;
            g_combat_audio_latch = 1;
            rl_427d19();
            rl_427990(S4D5620(0x4d5680), 1, 0);
        } else if (g_rl_58fe10 != 0) {
            g_tr_page = 10;
        }
        break;
    case 1:
        engine_set_demand_redraw(2);
        g_tr_page = 2;
        if (g_rl_5c9a74 < 1 || g_rl_5c9a74 > 2)
            g_tr_page = 10;
        rl_49bae9(0, LOCAL, (int)(uintptr_t)g_tr_p4_name_buf);
        break;
    case 4:
        rl_426d11(0);
        g_bf_4e65d8 = 0;
        rl_4b1897();
        rl_4b11ce();
        if (rl_42d91b(S4D5620(0x4d568c), 70, 80, 0, engine_current_screen()) == 0)
            engine_set_demand_redraw(2);
        engine_set_demand_redraw(2);
        break;
    default:
        break;
    }
    return 0;
}

/* Port of FUN_00432cc8 (542 B) — "Your options" (page 2): 4 back (page 1); 1 page 5; 2 load (the save list
 * *.sva / *.svb / *.sav by the MP role, the name field, the return page kept, page 3); 3 / 5 a new game
 * (page 4, the name box, s011_02.wav; DAT_0055302c 2 / 3). */
int rl_432cc8(void)
{
    g_rl_53f648 = -1;
    switch (PARAM0) {
    case 4:
        g_tr_page = 1;
        engine_set_demand_redraw(2);
        g_rl_57d320 = 0;
        break;
    case 1:
        g_tr_page = 5;
        engine_set_demand_redraw(2);
        break;
    case 2:
        if (MP_ON)
            rl_4aecf0(g_tr_p4_gate_b != 0 ? S4D5620(0x4d5694) : S4D5620(0x4d569c), 0);
        else
            rl_4aecf0(S4D5620(0x4d56a4), 0);
        rl_402009((int)(uintptr_t)g_tr_fl_name, 8, 160, 1);
        g_tr_p9_bookmark = g_tr_page;
        g_tr_fl_show_status = 0;
        g_tr_fl_flag = 0;
        g_tr_page = 3;
        g_rl_57d320 = 1;
        engine_set_demand_redraw(2);
        break;
    case 3:
        g_tr_page = 4;
        rl_402009((int)(uintptr_t)g_tr_p4_name_buf, 16, 192, 0);
        rl_401f49();
        g_combat_audio_latch = 1;
        rl_427d19();
        rl_427990(S4D5620(0x4d56ac), 1, 0);
        s_slot_0055302c = 2;
        engine_set_demand_redraw(2);
        g_rl_57d320 = 0;
        break;
    case 5:
        engine_set_demand_redraw(2);
        g_tr_page = 4;
        rl_402009((int)(uintptr_t)g_tr_p4_name_buf, 16, 192, 0);
        rl_401f49();
        g_combat_audio_latch = 1;
        rl_427d19();
        rl_427990(S4D5620(0x4d56b8), 1, 0);
        s_slot_0055302c = 3;
        g_rl_57d320 = 0;
        break;
    default:
        break;
    }
    return 0;
}

/* Port of FUN_00432fab (191 B) — give shield `slot` (1..5) to the local realm if free: its old shield freed,
 * the slot's owner byte, the colour (DAT_004d5548[4 slot]) into aux +0x25 and realm +0x0a. */
int rl_432fab(int slot)
{
    if (g_tr_p4_shield_slots[slot] != 0)
        return slot;
    for (int i = 1; i < 6; i++)
        if (g_tr_p4_shield_slots[i] == (uint32_t)LOCAL)
            g_tr_p4_shield_slots[i] = 0;
    g_tr_p4_shield_slots[slot] = (uint8_t)LOCAL;
    AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d75 + 44 * LOCAL) = AT(uint8_t, g_rl_4d5508, 0x4d5508u, 0x4d5548 + 4 * slot);
    PL8(LOCAL, 0x57bf0a) = AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d75 + 44 * LOCAL);
    return 0;
}

/* Port of FUN_00432ee6 (197 B) — a shield click (page 4): single player: the shield (0x432fab), the AI seats
 * re-dealt (0x49caaa), redraw 2; the MP host: seat taken, the shield, the seats, messages 7 and 3; an MP guest
 * asks once (DAT_00568984 = the clock; DAT_0053e8b4 = the shield; message 5). */
int rl_432ee6(void)
{
    if (MP_ON) {
        if (g_tr_p4_gate_b != 0) {
            g_rl_5530e0[LOCAL] = 1;
            rl_432fab(PARAM0);
            rl_49caaa();
            rl_43eda0(7, 0);
            rl_43eda0(3, 0);
            engine_set_demand_redraw(2);
        } else if (g_tr_p4_flash_timer == 0) {
            g_rl_53e8b4 = PARAM0;
            g_tr_p4_flash_timer = (int32_t)rl_plat_timeGetTime();
            rl_43eda0(5, 0);
            engine_set_demand_redraw(2);
        }
    } else {
        rl_432fab(PARAM0);
        rl_49caaa();
        engine_set_demand_redraw(2);
    }
    return 0;
}

/* Port of FUN_00433155 (569 B) — page 4's Back (1) / Continue (2): the seat released (999); Back: an MP session
 * left (0x4b743b, 0x43e8f4(1)), page 1, the misc sheet and system sheet 0 reloaded; Continue (single player,
 * or the MP host with DAT_0057c940): a campaign (DAT_0057d320 1) applies the scenario (0x499e5d) and starts
 * (0x4329ec); else custom (DAT_0055302c 3): page 11 (MP) / 12 with setup3.wav and the name box (44, 400); MP:
 * message 7, page 11 / 8 by DAT_0057cb38 when a game exists (message 84), else 6; single player page 7; then
 * 0x4330c6, message 3, redraw 2. */
int rl_433155(void)
{
    g_rl_53f648 = -1;
    rl_49bae9(999, LOCAL, (int)(uintptr_t)g_tr_p4_name_buf);
    if (PARAM0 == 1) {
        if (MP_ON) {
            rl_4b743b();                               /* leave the MP session (rl mpnet2) */
            rl_43e8f4(1u);
        }
        g_tr_page = 1;
        s_slot_0057a0f0 = 0;
        s_slot_0057c8fc = 0;
        rl_499034();
        rl_499a1c(0);
        engine_set_demand_redraw(2);
    } else if (PARAM0 == 2 && (!MP_ON || g_tr_p4_continue_disabled != 0)) {
        if (g_rl_57d320 == 1) {
            rl_499e5d();
            rl_4329ec();
            g_diplo_action_taken = 1;
        } else {
            if (s_slot_0055302c == 3) {
                g_tr_page = MP_ON ? 11 : 12;
                rl_4263ad(S4D5620(0x4d56c4), 0, 1);
                rl_42b919();
                rl_42b7f7();
                rl_402009(S4D5620(0x4d56d0), 44, 400, 0);
                rl_401f49();
            } else if (MP_ON) {
                rl_43eda0(7, 0);
                if (g_rl_568464 != 0) {
                    s_slot_0057a0f0 = 0;
                    if (g_rl_57cb38 != 0) {
                        g_tr_page = 11;
                        rl_42b919();
                        rl_42b7f7();
                        rl_402009(S4D5620(0x4d56d4), 44, 400, 0);
                    } else {
                        g_tr_page = 8;
                        rl_402009(S4D5620(0x4d56d8), 40, 280, 0);
                    }
                    rl_401f49();
                    rl_43eda0(84, 0);
                } else {
                    g_tr_page = 6;
                }
            } else {
                g_tr_page = 7;
            }
            rl_4330c6();
            rl_43eda0(3, 0);
            engine_set_demand_redraw(2);
        }
    }
    return 0;
}

/* Port of FUN_00433461 (240 B) — page 5 (Play Now!'s campaign pick): page 4, the name box, s011_02.wav; a
 * campaign (DAT_0057d320 = 1, DAT_0055302c = 1); DAT_0053f640 = the pick; DAT_0053f258 2 for pick 1 else 0. */
int rl_433461(void)
{
    engine_set_demand_redraw(2);
    g_tr_page = 4;
    rl_402009((int)(uintptr_t)g_tr_p4_name_buf, 16, 192, 0);
    rl_401f49();
    g_combat_audio_latch = 1;
    rl_427d19();
    rl_427990(STRVA(g_rl_4d56e4, 0x4d56e4u, 0x4d56e4), 1, 0);
    g_rl_57d320 = 1;
    s_slot_0055302c = 1;
    s_slot_0053f640 = (uint32_t)PARAM0;
    s_slot_0053f258 = PARAM0 == 1 ? 2 : 0;
    return PARAM0;
}

/* Port of FUN_004335f0 (452 B) — the custom setup's buttons (page 7): 1 back (as page 4's); 2 start when
 * DAT_00553f98 ≤ DAT_0053e8a4 (0x499dc3, 0x4329ec, DAT_0052afa4 = 1); 3 defaults (MP: message 13; 0x4ae539,
 * 0x4ae5e2(DAT_0053e8a4), 0x433cc1, the tabs redrawn); 4 load (the save list by role, page 3, DAT_0052afa4 1). */
int rl_4335f0(void)
{
    g_rl_53f648 = -1;
    switch (PARAM0) {
    case 1:
        if (MP_ON) {
            rl_4b743b();                               /* leave the MP session (rl mpnet2) */
            rl_43e8f4(1u);
        }
        g_tr_page = 1;
        s_slot_0057a0f0 = 0;
        s_slot_0057c8fc = 0;
        rl_499034();
        rl_499a1c(0);
        engine_set_demand_redraw(2);
        break;
    case 2:
        if ((int)s_slot_00553f98 <= (int)s_slot_0053e8a4) {
            rl_499dc3();
            rl_4329ec();
            g_diplo_action_taken = 1;
        }
        break;
    case 3:
        if (MP_ON)
            rl_43eda0(13, 0);
        engine_title_render_setup_apply_defaults();                         /* 0x4ae539 */
        engine_title_render_setup_clamp_player_count((int)s_slot_0053e8a4); /* 0x4ae5e2 */
        rl_433cc1();
        g_tr_tabs_dirty = 1;
        engine_set_demand_redraw(2);
        break;
    case 4:
        if (MP_ON)
            rl_4aecf0(g_tr_p4_gate_b != 0 ? STRVA(g_rl_4d56f8, 0x4d56f8u, 0x4d56f8) : STRVA(g_rl_4d56f8, 0x4d56f8u, 0x4d5700), 0);
        else
            rl_4aecf0(STRVA(g_rl_4d56f8, 0x4d56f8u, 0x4d5708), 0);
        rl_402009((int)(uintptr_t)g_tr_fl_name, 8, 160, 1);
        g_tr_p9_bookmark = g_tr_page;
        g_tr_fl_show_status = 0;
        g_tr_fl_flag = 0;
        g_tr_page = 3;
        g_rl_57d320 = 0;
        engine_set_demand_redraw(2);
        g_diplo_action_taken = 1;
        break;
    default:
        break;
    }
    return 0;
}

/* ---------------------------------------------------------------- the setup page's country list and options */
extern uint8_t g_rl_50a460[60];      /* DAT_0050a460 (rl_io_globals.h): the country list's map numbers */
extern uint8_t g_rl_4e8790[6500];    /* DAT_004e8790 (rl_io_globals.h): the save list, 65 B a name */
extern int32_t g_rl_5cd41c;          /* DAT_005cd41c (rl_glue_globals.h): the page-3 load request */

/* the country picked: its map loaded (message 76, 0x46a037, 0x467770, 0x4298c1(0)), the player count clamped,
 * the options refreshed (0x433cc1), the list and tabs redrawn */
static void country_load(void)
{
    rl_43eda0(76, 0);
    rl_46a037((int)s_slot_0053f034);
    rl_467770((int)s_slot_0053f034);
    rl_4298c1(0);
    engine_title_render_setup_clamp_player_count((int)s_slot_0053e8a4);   /* 0x4ae5e2 */
    rl_433cc1();
    g_tr_country_dirty = 1;
    g_tr_tabs_dirty = 1;
    engine_set_demand_redraw(2);
}

/* Port of FUN_004337b4 (337 B) — the country list's arrows: -1 up while the scroll is above 0, 1 down while it
 * is below the count − 5; the selected row's country (DAT_0050a460[sel + scroll]) loaded. */
int rl_4337b4(void)
{
    if (PARAM0 == -1) {
        if (g_tr_country_scroll > 0) {
            --g_tr_country_scroll;
            s_slot_0053f034 = g_rl_50a460[g_tr_country_sel + g_tr_country_scroll];
            country_load();
        }
    } else if (PARAM0 == 1) {
        if (g_tr_country_count - 5 > g_tr_country_scroll) {
            ++g_tr_country_scroll;
            s_slot_0053f034 = g_rl_50a460[g_tr_country_sel + g_tr_country_scroll];
            country_load();
        }
    }
    return 0;
}

/* Port of FUN_00433905 (147 B) — a country row (param 1..5) selected and loaded. */
int rl_433905(void)
{
    g_tr_country_sel = PARAM0 - 1;
    s_slot_0053f034 = g_rl_50a460[PARAM0 - 1 + g_tr_country_scroll];
    country_load();
    return 0;
}

/* Port of FUN_00433998 (139 B) — an option tab (param 1..12): page 9 (the return page kept), the tab, its value
 * count (DAT_004d5560; the player-count tab 3 shows DAT_0053e8a4 − 1 when ≤ 4), DAT_00553530 = 0, redraw 2. */
int rl_433998(void)
{
    g_rl_53f648 = -1;
    if (g_tr_page != 9) {
        g_tr_p9_bookmark = g_tr_page;
        g_tr_page = 9;
    }
    g_tr_p9_tab = PARAM0 - 1;
    g_tr_p9_count = g_rl_4d5560[PARAM0 - 1];
    if (PARAM0 == 3 && (int)s_slot_0053e8a4 <= 4)
        g_tr_p9_count = (int)s_slot_0053e8a4 - 1;
    g_rl_553530 = 0;
    engine_set_demand_redraw(2);
    return g_tr_p9_count;
}

/* Port of FUN_00433ba2 (287 B) — store value `v` for option tab `tab` (the scalars in retail's permuted order:
 * tab 0 → DAT_0053f29c, 1 → 2a4, 2 → 288, 3 → 2a0, 4 → 290, 5 → 298, 6 → 2b0, 7 → 294, 8 → 2a8, 9 → 2ac,
 * 10 → 28c, 11 → 2b4), then the options refreshed (0x433cc1). */
int rl_433ba2(int tab, int v)
{
    static const uint16_t k_off[12] = { 0x29c, 0x2a4, 0x288, 0x2a0, 0x290, 0x298, 0x2b0, 0x294, 0x2a8, 0x2ac, 0x28c, 0x2b4 };
    if (tab >= 0 && tab < 12)
        AT(int32_t, g_tr_options, 0x53f288u, 0x53f000 + k_off[tab]) = v;
    return rl_433cc1();
}

/* Port of FUN_00433a23 (77 B) — the open dropdown's pick: the highlighted row (DAT_00553114 − 1) stored
 * (0x433ba2), MP message 9, back to the return page, redraw 2. */
int rl_433a23(void)
{
    rl_433ba2(g_tr_p9_tab, g_tr_selected_button - 1);
    if (MP_ON)
        rl_43eda0(9, 0);
    g_tr_page = g_tr_p9_bookmark;
    engine_set_demand_redraw(2);
    return g_tr_p9_bookmark;
}

/* Port of FUN_00433ce4 (217 B) — the dropdown row under the cursor (1-based; 0 none): the tab's box at
 * DAT_004d3158[3 tab] (x, y), rows 16 px from y + 16 (the player-count tab 2 grows upwards), 96 px wide. */
int rl_433ce4(void)
{
    for (int i = 0; i < g_tr_p9_count; i++) {
        int x = AT(int32_t, g_rl_4d3158, 0x4d3158u, 0x4d3158 + 12 * g_tr_p9_tab);
        int y = 16 * i + AT(int32_t, g_rl_4d3158, 0x4d3158u, 0x4d315c + 12 * g_tr_p9_tab) + 16;
        if (g_tr_p9_tab == 2)
            y -= 16 * (g_tr_p9_count - 1);
        if (x <= g_bf_4e65fc && x + 96 > g_bf_4e65fc && y <= g_bf_4e65f4 && y + 16 > g_bf_4e65f4)
            return i + 1;
    }
    return 0;
}

/* Port of FUN_0043400b (361 B) — the save list's grid (3 × 10 names, 120 px columns, 16 px rows) under the
 * cursor: the load page's (at (96, 10)) or the in-game dialog's (16, 144); the entry (first + column + 3 · row)
 * highlighted (DAT_004e65dc); on a fresh LMB (DAT_004eafb4) a listed one is copied to the name field (13 B) and
 * the field reopened (0x402009 → EAX); else 0. */
int rl_43400b(int load_page)
{
    int x0 = load_page ? 96 : 16, y0 = load_page ? 10 : 144;
    if (x0 + 32 > g_bf_4e65fc || x0 + 384 <= g_bf_4e65fc)
        return 0;
    if (y0 + 108 > g_bf_4e65f4 || y0 + 268 <= g_bf_4e65f4)
        return 0;
    if (x0 + 120 > g_bf_4e65fc)
        g_tr_fl_selected = g_tr_fl_first;
    else if (x0 + 240 > g_bf_4e65fc)
        g_tr_fl_selected = g_tr_fl_first + 1;
    else
        g_tr_fl_selected = g_tr_fl_first + 2;
    g_tr_fl_selected += 3 * ((g_bf_4e65f4 - (y0 + 108)) / 16);
    if (g_bf_4eafb4 == 0)
        return 0;
    if (g_tr_fl_selected >= g_tr_fl_count)
        return 0;
    rl_401136((int)(uintptr_t)&g_rl_4e8790[65 * g_tr_fl_selected], (int)(uintptr_t)g_tr_fl_name, 13);
    return rl_402009((int)(uintptr_t)g_tr_fl_name, 8, 160, 1);
}

/* Port of FUN_004342f3 (21 B) — the load page's Load button: the request DAT_005cd41c = 100. */
int rl_4342f3(void)
{
    g_rl_5cd41c = 100;
    return 0;
}

/* Port of FUN_00434346 (178 B) — the save list's arrows: first += param (≥ 0); the load page (DAT_00591550 1:
 * 15 shown) backs off 3 past count − 15 (0 under 30 names), the dialog (2: 5 shown) 1 past count − 5 (0 under
 * 10); redraw 2. */
int rl_434346(void)
{
    engine_set_demand_redraw(2);
    g_tr_fl_first += PARAM0;
    if (g_tr_fl_first < 0)
        g_tr_fl_first = 0;
    int kind = AT(int32_t, g_bf_59154c, 0x59154cu, 0x591550u);
    if (kind == 1) {
        if (g_tr_fl_count - 15 < g_tr_fl_first)
            g_tr_fl_first -= 3;
        if (g_tr_fl_count < 30)
            g_tr_fl_first = 0;
    } else if (kind == 2) {
        if (g_tr_fl_count - 5 < g_tr_fl_first)
            --g_tr_fl_first;
        if (g_tr_fl_count < 10)
            g_tr_fl_first = 0;
    }
    return 0;
}

/* ================================================================ the in-game panels' actions (router actions: entry-patched) */
extern uint8_t g_rl_53f6a0[0x300];   /* DAT_0053f6a0 (rl_glue_globals.h): the muster rows, 128 B a realm, 16 B a troop */
#define MUSTER(p, i, off) AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f6a0 + 128 * (p) + 16 * (i) + (off))

/* Port of FUN_00434e1f (37 B) — the confirm dialog's buttons: the answer DAT_0059151c = param0's low byte, the
 * dialog's callback (DAT_00552ff4, a retail VA) run, redraw 2. */
int rl_434e1f(void)
{
    g_bf_59151c[0] = (uint8_t)PARAM0;
    int r = (int)rl_va_call((uint32_t)g_rl_552ff4, 0);   /* the callback VA (0x43723a, 0x43744f …) */
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_00435211 (61 B) — the rankings recomputed (0x49aa0e) and every realm's aggregate (0x49d1e0). */
int rl_435211(void)
{
    rl_49aa0e();
    for (int i = 1; i < 6; i++)
        engine_ranking_recompute_player_aggregate((unsigned)i);   /* 0x49d1e0 */
    return 0;
}

/* Port of FUN_004351c4 (77 B) — the treasury's seal: screen 0x20 (the rankings), redraw 1; MP message 59, else
 * the rankings recomputed (0x435211); the category DAT_0055ce7c shown (0x4b3994). */
int rl_4351c4(void)
{
    engine_set_current_screen(32);
    engine_set_demand_redraw(1);
    if (MP_ON)
        rl_43eda0(59, 0);
    else
        rl_435211();
    return rl_4b3994((int)s_slot_0055ce7c);
}

/* Port of FUN_0043524e (45 B) — a rankings category icon: the category = param0, redraw 2, shown (0x4b3994). */
int rl_43524e(void)
{
    s_slot_0055ce7c = (uint32_t)PARAM0;
    engine_set_demand_redraw(2);
    return rl_4b3994(PARAM0);
}

/* Port of FUN_004358b0 (138 B) — a muster figure (troop param0): with men available (+8 > 0) the march trigger
 * (0x4aabd8 for the county and the previous troop), the troop DAT_00553f20, screen 13 (assign), +0xc = +0, the
 * assign render (0x4184c6). */
int rl_4358b0(void)
{
    if (MUSTER(LOCAL, PARAM0, 8) > 0) {
        rl_4aabd8((int)s_slot_0057c93c, g_arm_weapon);
        g_arm_weapon = PARAM0;
        engine_set_current_screen(13);
        MUSTER(LOCAL, PARAM0, 12) = MUSTER(LOCAL, PARAM0, 0);
        return rl_4184c6();
    }
    return LOCAL << 7;
}

/* Port of FUN_0043582a (134 B) — the muster floor's click map (arm_grid.pl8 at DAT_00542cf8, 80 × 8-px cells):
 * on a fresh LMB over a zone, param0 = the zone and 0x4358b0: 1; else 0. */
int rl_43582a(void)
{
    if (g_bf_4eafb4 == 0)
        return 0;
    uint8_t z = g_rl_542cf8[80 * (g_bf_4e65f4 / 8) + g_bf_4e65fc / 8];
    if (z == 0)
        return 0;
    PARAM0 = z;
    rl_4358b0();
    return 1;
}

/* Port of FUN_0043593a (130 B) — assign +1: troop DAT_00553f20 below its cap (+8) and men in the pool (row 0). */
int rl_43593a(void)
{
    if (MUSTER(LOCAL, g_arm_weapon, 8) > MUSTER(LOCAL, g_arm_weapon, 0) && MUSTER(LOCAL, 0, 0) != 0) {
        ++MUSTER(LOCAL, g_arm_weapon, 0);
        --MUSTER(LOCAL, 0, 0);
    }
    return LOCAL << 7;
}

/* Port of FUN_004359bc (80 B) — assign −1: one man of the troop back to the pool. */
int rl_4359bc(void)
{
    if (MUSTER(LOCAL, g_arm_weapon, 0) > 0) {
        --MUSTER(LOCAL, g_arm_weapon, 0);
        ++MUSTER(LOCAL, 0, 0);
    }
    return LOCAL << 7;
}

/* Port of FUN_00435a0c (85 B) — assign none: the whole troop back to the pool. */
int rl_435a0c(void)
{
    while (MUSTER(LOCAL, g_arm_weapon, 0) > 0) {
        --MUSTER(LOCAL, g_arm_weapon, 0);
        ++MUSTER(LOCAL, 0, 0);
    }
    return LOCAL << 7;
}

/* Port of FUN_00435a61 (135 B) — assign all: the pool into the troop up to its cap. */
int rl_435a61(void)
{
    while (MUSTER(LOCAL, 0, 0) != 0 && MUSTER(LOCAL, g_arm_weapon, 8) > MUSTER(LOCAL, g_arm_weapon, 0)) {
        ++MUSTER(LOCAL, g_arm_weapon, 0);
        --MUSTER(LOCAL, 0, 0);
    }
    return LOCAL << 7;
}

/* Port of FUN_00435b4d (316 B) — the muster's commit: Create (DAT_0059151c) needs men (row 7 +0, or the
 * mercenaries DAT_0055446c; else event 168) and 50 of them without mercenaries (else 148); MP message 42, else
 * the army spawned (0x4a9a9a; refused: event 221). Then screen 0, the sheets freed (0x4984dc), redraw 1. */
int rl_435b4d(void)
{
    if (g_bf_59151c[0] != 0) {
        if (MUSTER(LOCAL, 7, 0) == 0 && g_arm_merc == 0) {
            rl_472bc5(0, LOCAL, 168, 0, 0, 0, 0, 0);
        } else if (MUSTER(LOCAL, 7, 0) < 50 && g_arm_merc == 0) {
            rl_472bc5(0, LOCAL, 148, 0, 0, 0, 0, 0);
        } else if (MP_ON) {
            rl_43eda0(42, 0);
        } else if (engine_combat_spawn_unit(LOCAL, (int)s_slot_0057c93c, g_arm_merc, g_arm_happy_cost) == 0) {
            rl_472bc5(0, LOCAL, 221, 0, 0, 0, 0, 0);
        }
    }
    engine_set_current_screen(0);
    int r = rl_4984dc();
    engine_set_demand_redraw(1);
    return r;
}

/* Port of FUN_00435ae8 (101 B) — Create (1) / Change (2: screen 23) / Cancel (3) on the muster. */
int rl_435ae8(void)
{
    switch (PARAM0) {
    case 1:
        g_bf_59151c[0] = 1;
        return rl_435b4d();
    case 2:
        engine_set_current_screen(23);
        engine_set_demand_redraw(2);
        break;
    case 3:
        g_bf_59151c[0] = 0;
        return rl_435b4d();
    default:
        break;
    }
    return 0;
}

/* Port of FUN_00435c89 (54 B) — the mercenaries Yes (1) / No toggle DAT_0055446c; redraw 2. */
int rl_435c89(void)
{
    g_arm_merc = PARAM0 == 1;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00435cbf (48 B) — the muster's seal: screen 10, the muster re-seeded (0x4aa90a for the county and
 * DAT_00543fd8), redraw 2. */
int rl_435cbf(void)
{
    engine_set_current_screen(10);
    int r = rl_4aa90a(s_slot_0057c93c, (int)g_arm_conscripts);   /* rl_link: engine_armory_muster_init (a port struct) */
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043611b (38 B) — the county's Diplomacy tab: screen 11, the gift DAT_0057a0f8 = 0, redraw 2. */
int rl_43611b(void)
{
    engine_set_current_screen(11);
    s_slot_0057a0f8 = 0;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00436141 (74 B) — the gift screen: screen 26, the mode DAT_005651cc = 0, the scene recomposited
 * (0x4b1de0, 0x4050c0, 0x4b1dd5 with the menu redraw byte 0 then 2), DAT_004e65f8 = 1, redraw 2. */
int rl_436141(void)
{
    engine_set_current_screen(26);
    g_diplo_action_mode = 0;
    g_gfx_menu_redraw_a = 0;
    rl_4b1de0();
    engine_map_scene_tick_frame();                   /* 0x4050c0 */
    int r = rl_4b1dd5();
    g_gfx_menu_redraw_a = 2;
    g_gfx_menu_redraw_b = 1;
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_00436a88 (154 B) — the county's Castle tab: a foreign county is event 112; else the tier
 * DAT_0056d898 = +0x1c0 − 1 (0 without one), screen 27, redraw 1. */
int rl_436a88(void)
{
    if (CO8(s_slot_0057c93c, 0x53f9b5) != (uint32_t)LOCAL)
        return rl_472bc5(0, LOCAL, 112, 0, 0, 0, 0, 0);
    if (CO8(s_slot_0057c93c, 0x53fb70) != 0)
        g_castle_tier = CO8(s_slot_0057c93c, 0x53fb70) - 1;
    else
        g_castle_tier = 0;
    engine_set_current_screen(27);
    engine_set_demand_redraw(1);
    return (int)s_slot_0057c93c << 8;
}

/* ---------------------------------------------------------------- the county panels */
#define SELC ((int)s_slot_0057c93c)   /* DAT_0057c93c: the selected county */

/* Port of FUN_0043a1e9 (86 B) — the wanted level +1 (+0x15e signed, below 5: 0x43a23f; MP message 49). */
int rl_43a1e9(void)
{
    if ((int8_t)CO8(SELC, 0x53fb0e) < 5) {
        rl_43a23f((int)SELC);   /* rl_link: engine_county_wanted_step (the panel struct) */
        if (MP_ON)
            return rl_43eda0(49, 0);
    }
    return 0;
}

/* Port of FUN_0043a2b2 (85 B) — the wanted level −1 (above 0: 0x43a307; MP message 49). */
int rl_43a2b2(void)
{
    if ((int8_t)CO8(SELC, 0x53fb0e) > 0) {
        rl_43a307((int)SELC);   /* rl_link: engine_county_wanted_step (the panel struct) */
        if (MP_ON)
            return rl_43eda0(49, 0);
    }
    return 0;
}

/* Port of FUN_0043aa32 (81 B) — the tax rate +1 (+0xb9 below 50: 0x43aa83; MP message 35). */
int rl_43aa32(void)
{
    if (CO8(SELC, 0x53fa69) < 50) {
        rl_43aa83((int)SELC);   /* rl_link: engine_county_tax_step (the panel struct) */
        if (MP_ON)
            return rl_43eda0(35, 0);
    }
    return SELC << 8;
}

/* Port of FUN_0043aad5 (80 B) — the tax rate −1 (+0xb9 above 0: 0x43ab25; MP message 35). */
int rl_43aad5(void)
{
    if (CO8(SELC, 0x53fa69) != 0) {
        rl_43ab25((int)SELC);   /* rl_link: engine_county_tax_step (the panel struct) */
        if (MP_ON)
            return rl_43eda0(35, 0);
    }
    return SELC << 8;
}

/* Port of FUN_0043a8f2 (48 B) — the population panel: screen 20, the health cue (0x4b3768(+9)), its render. */
int rl_43a8f2(void)
{
    engine_set_current_screen(20);
    rl_4b3768(CO8(SELC, 0x53f9b9));
    return rl_4110b1();   /* rl_link: engine_county_panel_population (the panel struct) */
}

/* Port of FUN_0043a922 (23 B) — the happiness panel: screen 22 and its render. */
int rl_43a922(void)
{
    engine_set_current_screen(22);
    return rl_4116fb();   /* rl_link: engine_county_panel_happiness (the panel struct) */
}

/* Port of FUN_0043a939 (23 B) — the tax panel: screen 21 and its render. */
int rl_43a939(void)
{
    engine_set_current_screen(21);
    return rl_41152f();   /* rl_link: engine_county_panel_tax (the panel struct) */
}

/* Port of FUN_0043a846 (172 B) — the rations panel: screen 25, its render; the voice: s021_02 without food
 * (+0x15d 0), s021_01 when fed (+0x250) with neither +0x17c nor +0x178. */
int rl_43a846(void)
{
    engine_set_current_screen(25);
    rl_411b72();               /* rl_link: engine_county_panel_rations (the panel struct) */
    if (CO8(SELC, 0x53fb0d) == 0) {
        g_combat_audio_latch = 1;
        return rl_427990(STRVA(g_rl_4d57ac, 0x4d57acu, 0x4d57ac), 1, 0);
    }
    if (CO32(SELC, 0x53fc00) != 0 && CO32(SELC, 0x53fb2c) == 0 && CO32(SELC, 0x53fb28) == 0) {
        g_combat_audio_latch = 1;
        return rl_427990(STRVA(g_rl_4d57ac, 0x4d57acu, 0x4d57b8), 1, 0);
    }
    return SELC << 8;
}

/* Port of FUN_0043a5a9 (647 B) — county `c`'s ration to `r` (+0x15f, a signed byte), provisioned (0x44df5f);
 * with food (+0x250), a shortfall (+0x17c) the change left unchanged, a real step and r not 0 / 100: back to the
 * old ration and stepped 1 at a time (≤ 100 steps, clamped 0..100) until the shortfall changes — or, `stop` set,
 * the target reached (then back to the old ration). Then the workforce / plan twice; the selected county's
 * panel re-rendered (DAT_005530d0, the menu redraw byte). */
int rl_43a5a9(int c, int r, int stop)
{
    int old = (int8_t)CO8(c, 0x53fb0f);
    int dir = old > r ? -1 : old < r ? 1 : 0;
    CO8(c, 0x53fb0f) = (uint8_t)r;
    int short0 = CO32(c, 0x53fb2c);
    engine_army_economy_provision(c, (int)s_slot_0057c934);
    if (CO32(c, 0x53fc00) != 0 && CO32(c, 0x53fb2c) != 0 && dir != 0 && r != 0 && r != 100 && CO32(c, 0x53fb2c) == short0) {
        CO8(c, 0x53fb0f) = (uint8_t)old;
        for (int n = 1; n < 101; n++) {
            if (dir == 1)
                CO8(c, 0x53fb0f)++;
            else
                CO8(c, 0x53fb0f)--;
            if ((int8_t)CO8(c, 0x53fb0f) < 0)
                CO8(c, 0x53fb0f) = 0;
            else if ((int8_t)CO8(c, 0x53fb0f) > 100)
                CO8(c, 0x53fb0f) = 100;
            engine_army_economy_provision(c, (int)s_slot_0057c934);
            if (CO32(c, 0x53fb2c) != short0)
                break;
            if ((int8_t)CO8(c, 0x53fb0f) == r && stop != 0) {
                CO8(c, 0x53fb0f) = (uint8_t)old;
                engine_army_economy_provision(c, (int)s_slot_0057c934);
                break;
            }
        }
    }
    engine_territory_allocate_realm_workforce(c);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(c);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    if (SELC == c) {
        rl_411b72();           /* rl_link: engine_county_panel_rations (the panel struct) */
        g_combat_flag_530d0 = 1;
        g_gfx_menu_redraw_a = 1;
    }
    return c;
}

/* Port of FUN_0043b04c (181 B) — Send supplies' Send (1) / Cancel (0): the full mark, redraw 2; Send to the
 * county itself asks (dialog 14 at (160, 160), callback 0x43b101); else screen 0 and the below-status flag, MP
 * message 47 or the shipment (0x43b145). */
int rl_43b04c(void)
{
    rl_45240a();
    engine_set_demand_redraw(2);
    if (PARAM0 != 0) {
        if (g_send_target == SELC)
            return rl_40e6f2(14, 160, 160, 0x43b101);
        engine_set_current_screen(0);
        BELOW_STATUS() = 1;
        if (MP_ON)
            return rl_43eda0(47, 0);
        engine_sendsupplies_commit(LOCAL, SELC, (uint8_t)g_send_target);   /* 0x43b145 */
        return 0;
    }
    engine_set_current_screen(0);
    BELOW_STATUS() = 1;
    return 0;
}

/* ================================================================ the in-game input dispatch */
extern uint8_t g_rl_4eabe0;          /* DAT_004eabe0 (rl_glue_globals.h): a fresh RMB */
extern int32_t g_rl_5533b0;          /* DAT_005533b0 (rl_glue_globals.h): the cursor over the map */
extern int32_t g_rl_5533f4;          /* DAT_005533f4 (rl_render_globals.h): the labor screen came from the map */
extern int32_t g_rl_565408;          /* DAT_00565408 (rl_render_globals.h): the workforce drag mode */
extern int32_t g_rl_56d894;          /* DAT_0056d894 (rl_glue_globals.h): the screen a help / message page returns to */
extern int32_t g_rl_57c920;          /* DAT_0057c920 (rl_io_globals.h): screen 58's return */
extern uint8_t g_rl_5c9274;          /* DAT_005c9274 (rl_io_globals.h): leave to the title */
extern int32_t g_rl_5c9278;          /* DAT_005c9278 (rl_glue_globals.h): screen 33's return */
extern uint8_t g_rl_53e9a0[4];       /* DAT_0053e9a0 (rl_ui2_globals.h): the workforce hover category */
#define TURN_IDLE() (g_sim_turn_progress == 0 && (g_bf_55403c == 0 || g_rl_553018 != 0))   /* DAT_00553fc8, DAT_0055403c */
#define LMB_UP   g_bf_4e6900   /* DAT_004e6900: an RMB / cancel click (the "close" edge) */
#define LMB_DOWN g_bf_4eafb4   /* DAT_004eafb4: a fresh LMB */
#define HUD_MENU ((int)(uintptr_t)g_rl_4dc428)    /* word_4DC428: the menu bar's 3 pull-downs (16-B records) */

/* the closing tail every unhandled click reaches (0x431ea8): a press on the minimap (0x4323fe, not screen 18)
 * leaves the screen for the map (screen 15 first drops its cue and the labor sheets 0x41438c), the
 * below-status flag 2 */
static void dispatch_tail(void)
{
    if ((LMB_DOWN != 0 || g_rl_4eabe0 != 0) && engine_current_screen() != 18 && rl_4323fe() != 0) {
        if (engine_current_screen() == 15) {
            rl_427d19();
            rl_41438c();
        }
        if (s_slot_0057c8fc == 0)
            engine_set_current_screen(0);
        BELOW_STATUS() = 2;
    }
}

/* the county panels' common buttons: the HUD hotspots (0x43292d, 0x432967), the county action grid (0x438ceb),
 * the tax slider (0x439122), the portraits (0x438e3b), the route overlay (0x439079) */
static int county_buttons(void)
{
    return rl_43292d() != 0 || rl_432967() != 0
        || rl_438ceb() != 0     /* rl_link: engine_county_action_grid_classify (the panel env) */
        || rl_439122() != 0 || rl_438e3b() != 0 || rl_439079() != 0;
}

/* a "close on RMB / the close button" screen: back to `to` with redraw `mode` */
static int close_to(int to, int mode)
{
    if (!TURN_IDLE() || LMB_UP != 0 || rl_40e7e4() != 0) {
        engine_set_current_screen(to);
        engine_set_demand_redraw(mode);
        return 1;
    }
    return 0;
}

/* Port of FUN_0042ff10 (8265 B) — the in-game input dispatch, once a frame: the cursor-cache flag saved
 * (DAT_0052afa0) and cleared, DAT_00565488 = 0, the edge direction 8, the cursor region (0x432640); the popup
 * scroll (0x47685d) or the title menus (0x4ba9c8) take the input first; a pending swallow (DAT_0052afa8) eats
 * an RMB. Then per screen (DAT_004eac50) its buttons, its close edge (RMB DAT_004e6900 / the close button
 * 0x40e7e4 / the turn running) and the minimap tail (0x431ea8 above). Last: DAT_00565488 = 1 when the cursor
 * cache flag dropped from 1, the fresh-key latch DAT_004eabb4 cleared (both skipped by the mid view's map click,
 * which returns 0x4350a1's EAX). */
int rl_42ff10(void)
{
    g_rl_52afa0 = g_rl_57cac4;
    g_rl_57cac4 = 0;
    g_rl_565488 = 0;
    g_rl_5533a4 = 8;
    rl_432640();
    if (rl_47685d() != 0 || rl_4ba9c8() != 0)
        goto done;
    if (g_rl_52afa8 != 0 && LMB_UP != 0) {
        g_rl_52afa8 = 0;
        engine_set_demand_redraw(2);
        goto done;
    }
    switch (engine_current_screen()) {
    case 0:
        if (rl_432221() != 0 || rl_439079() != 0 || rl_432967() != 0)
            break;
        if (TURN_IDLE()) {
            if (rl_40deca(HUD_MENU, 3) != 0 || rl_43292d() != 0
                || rl_438ceb() != 0   /* rl_link: engine_county_action_grid_classify */
                || rl_439122() != 0 || rl_438e3b() != 0)
                break;
            if (g_rl_53e910 != 0 && LMB_UP != 0) {
                rl_476768();
                rl_45270a();
                break;
            }
            if (g_rl_5533b0 != 0 && LMB_DOWN != 0 && s_slot_0057cb18 == 2) {
                engine_set_current_screen(0);
                return rl_4350a1();
            }
            if (g_rl_5533b0 != 0 && g_bf_4e65d8 != 0) {
                rl_43ce1a();
                break;
            }
            if (g_rl_5533b0 != 0 && LMB_UP != 0) {
                engine_set_current_screen(4);
                rl_43caf4();
                break;
            }
            if (g_rl_5533b0 == 0 || g_bf_4e65cc == 0)
                g_rl_56d64c = 0;
        }
        dispatch_tail();
        break;
    case 2:
        if (TURN_IDLE()) {
            if (rl_43292d() != 0 || rl_432967() != 0
                || rl_438ceb() != 0   /* rl_link: engine_county_action_grid_classify */
                || rl_439122() != 0 || rl_438e3b() != 0 || rl_439079() != 0)
                break;
            if (rl_40e7e4() == 0) {
                if (rl_4393eb() != 0) {
                    engine_set_current_screen(5);
                    break;
                }
                if (rl_439df0() != 0)
                    break;
                if (rl_43a123() != 0) {
                    engine_set_current_screen(15);
                    rl_476768();
                    g_rl_5533f4 = 0;
                    rl_412b33();
                    break;
                }
                if (LMB_UP == 0) {
                    g_rl_4e65f0 = 1;
                    dispatch_tail();
                    break;
                }
            }
        }
        engine_set_current_screen(0);
        engine_army_ai_set_ranking_dirty(1);
        engine_set_demand_redraw(2);
        rl_432893(1);
        rl_41438c();
        break;
    case 15:
        if (rl_40e7e4() == 0 && LMB_UP == 0) {
            dispatch_tail();
            break;
        }
        engine_set_current_screen(g_rl_5533f4 != 0 ? 0 : 2);
        g_rl_5533f4 = 0;
        rl_427d19();
        engine_set_demand_redraw(2);
        rl_41438c();
        break;
    case 5:
        g_rl_565408 = 2;
        if (TURN_IDLE() && rl_439541() != 1) {
            if (rl_439541() == 2) {
                engine_set_current_screen(6);
                rl_432893(1);
                break;
            }
            rl_43958a();
            dispatch_tail();
            break;
        }
        engine_set_current_screen(2);
        rl_432893(1);
        break;
    case 6:
        if (TURN_IDLE()) {
            g_rl_565408 = 2;
            if (rl_4399b0() == 1) {
                engine_set_current_screen(2);
                rl_432893(1);
                break;
            }
            if (LMB_UP == 0) {
                dispatch_tail();
                break;
            }
        }
        engine_set_current_screen(2);
        g_rl_53e9a0[0] = 0;
        rl_4120e0();
        rl_432893(1);
        break;
    case 4:
        if (TURN_IDLE() && rl_432221() == 0 && rl_40e7e4() == 0) {
            if (rl_438990() != 0 || rl_438a91() != 0 || rl_437002() != 0)
                break;
            if (LMB_UP == 0) {
                dispatch_tail();
                break;
            }
        }
        engine_set_current_screen(0);
        rl_43cc56();
        break;
    case 32:
        close_to(0, 1);
        break;
    case 8:
        if (!TURN_IDLE() || LMB_UP != 0 || rl_40e7e4() != 0) {
            /* the three arms read the same vill_gd8.pl8 (three copies of the name: 0x4d5620 / 30 / 40) */
            uint32_t name = !TURN_IDLE() ? 0x4d5620u : LMB_UP != 0 ? 0x4d5630u : 0x4d5640u;
            engine_set_current_screen(0);
            rl_4af502();
            rl_4af8d9(S4D5620(name), (int)(uintptr_t)g_bf_542ce0, 0x12d8, 0);
            engine_set_demand_redraw(1);
        }
        break;
    case 9:
    case 11:
        close_to(0, 2);
        break;
    case 26:
        if (!TURN_IDLE()) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        } else if (rl_43b4cb(96, 176) != 0) {
            engine_set_demand_redraw(2);
        } else if (LMB_UP != 0 || rl_40e7e4() != 0) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        }
        break;
    case 10:
    case 27:
        if (TURN_IDLE() && LMB_UP == 0 && rl_40e7e4() == 0)
            break;
        engine_set_current_screen(0);
        rl_4984dc();
        engine_set_demand_redraw(1);
        break;
    case 23:
        if (TURN_IDLE()) {
            if (rl_435cef() != 0)   /* rl_link: engine_armory_slider_click (the screen env) */
                break;
            if (LMB_UP != 0) {
                engine_set_current_screen(10);
                rl_4aa90a(s_slot_0057c93c, (int)g_arm_conscripts);   /* rl_link: engine_armory_muster_init */
                engine_set_demand_redraw(2);
                break;
            }
            if (rl_40e7e4() == 0)
                break;
        }
        engine_set_current_screen(0);
        rl_4984dc();
        engine_set_demand_redraw(1);
        break;
    case 12:
        close_to(8, 2);
        break;
    case 13:
        if (!TURN_IDLE() || LMB_UP != 0 || rl_40e7e4() != 0) {
            engine_set_current_screen(10);
            rl_4180f6();
        }
        break;
    case 24:
        if (!TURN_IDLE()) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        } else if (rl_43b412(96, 104) != 0) {   /* rl_link: engine_sendsupplies_target_select */
            engine_set_demand_redraw(2);
        } else if (LMB_UP != 0) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        }
        break;
    case 20:
    case 22:
    case 21:
        if (!TURN_IDLE()) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        } else if (!county_buttons()) {
            if (LMB_UP != 0 || rl_40e7e4() != 0) {
                engine_set_current_screen(0);
                engine_set_demand_redraw(2);
            }
        }
        break;
    case 25:
        if (!TURN_IDLE()) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        } else if (rl_43292d() == 0 && rl_432967() == 0
                   && rl_43a379() == 0   /* rl_link: engine_county_rations_input (the panel env) */
                   && rl_438ceb() == 0   /* rl_link: engine_county_action_grid_classify */
                   && rl_439122() == 0 && rl_438e3b() == 0 && rl_439079() == 0) {
            if (LMB_UP != 0 || rl_40e7e4() != 0) {
                engine_set_current_screen(0);
                engine_set_demand_redraw(2);
            }
        }
        break;
    case 16:
        if (!TURN_IDLE()) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
            break;
        }
        if (rl_432221() != 0)
            break;
        if (LMB_DOWN != 0 && g_rl_553ecc <= 0) {
            engine_set_current_screen(0);
            g_rl_56d64c = 1;
            rl_4a9252();
            break;
        }
        if (LMB_UP != 0) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
            break;
        }
        dispatch_tail();
        break;
    case 29:
        if (!TURN_IDLE() || LMB_UP != 0) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
            break;
        }
        dispatch_tail();
        break;
    case 17:
        if (!close_to(4, 2))
            dispatch_tail();
        break;
    case 18:
        if (g_sim_turn_progress != 0 || rl_4bbea7() != 0) {
            rl_43b622();        /* rl_link: engine_combat_auto_resolve_button (a port queue) */
            engine_set_demand_redraw(2);
            break;
        }
        dispatch_tail();
        break;
    case 19:
        if (!TURN_IDLE() || rl_4bbf15() != 0 || LMB_UP != 0 || rl_40e7e4() != 0) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        }
        break;
    case 40:
        rl_432221();
        if (LMB_UP != 0) {
            engine_set_current_screen(41);
            g_bf_53f238 = 0;
            BELOW_STATUS() = 1;
            break;
        }
        dispatch_tail();
        break;
    case 41:
        if (rl_40deca(HUD_MENU, 3) != 0 || rl_432221() != 0 || rl_4329a4() != 0 || bf_43bf07() != 0
            || bf_43c57d() != 0 || bf_43c2a9() != 0)
            break;
        dispatch_tail();
        break;
    case 42:
        if (bf_43bf07() != 0 || bf_43c57d() != 0)
            break;
        if (LMB_UP != 0 || g_bf_5651d0 == 0) {
            engine_set_current_screen(41);
            break;
        }
        dispatch_tail();
        break;
    case 43:
        if (LMB_UP != 0) {
            s_slot_00568470 = 5001;
            break;
        }
        dispatch_tail();
        break;
    case 31:
        if (g_rl_53f648 != g_tr_selected_button) {
            g_rl_53f648 = g_tr_selected_button;
            break;
        }
        if (g_tr_page == 10) {
            if (LMB_UP != 0) {
                rl_4aef7e();
                g_rl_58fd84 = g_rl_5c9a74;
                g_tr_page = 1;
                engine_set_demand_redraw(2);
            }
            break;
        }
        if (g_tr_page == 9 && LMB_UP != 0) {
            g_tr_page = g_tr_p9_bookmark;
            engine_set_demand_redraw(2);
            break;
        }
        if (g_tr_page == 3) {
            if (rl_43400b(1) != 0)
                break;
            if (LMB_UP != 0) {
                g_tr_page = g_tr_p9_bookmark;
                engine_set_demand_redraw(2);
                break;
            }
        }
        if (g_tr_page == 13) {
            if (rl_434174(0) != 0)
                break;
            if (LMB_UP != 0 || rl_40e7e4() != 0) {
                g_tr_page = g_tr_p9_bookmark;
                engine_set_demand_redraw(2);
                break;
            }
        }
        if ((g_tr_page == 8 && g_rl_5cd41c != 0) || (g_tr_page == 11 && g_rl_5cd41c != 0)) {
            g_rl_5cd41c = 0;
            rl_43eda0(6, 0);
        }
        break;
    case 46:
        if (LMB_DOWN != 0) {
            engine_set_current_screen(47);
            engine_set_demand_redraw(2);
        }
        if (g_rl_4eabe0 != 0) {
            engine_set_current_screen(47);
            engine_set_demand_redraw(2);
        }
        dispatch_tail();
        break;
    case 47:
        if (g_rl_4eabe0 != 0) {
            rl_4263ad(S4D5620(0x4d5650), 0, 1);
            rl_42b780();
        }
        if (LMB_DOWN != 0) {
            rl_4263ad(S4D5620(0x4d565c), 0, 1);
            rl_42b780();
        }
        dispatch_tail();
        break;
    case 28:
        if (s_slot_0055302c == 2) {
            engine_set_current_screen(0);
            g_rl_5c9274 = 1;
            dispatch_tail();
            break;
        }
        if (LMB_UP != 0 || g_bf_4e65d8 != 0) {
            if ((int)s_slot_0053f258 < 8) {
                rl_4329ec();
            } else {
                engine_set_current_screen(0);
                g_rl_5c9274 = 1;
            }
            break;
        }
        dispatch_tail();
        break;
    case 50:
        if (rl_40dd92(HUD_MENU, 3) == 0 && LMB_UP != 0)
            rl_40df62();
        break;
    case 53:
    case 54:
        if (TURN_IDLE() || s_slot_0057c8fc != 0) {
            if (rl_43400b(0) == 0 && LMB_UP != 0) {
                engine_set_demand_redraw(2);
                engine_set_current_screen(g_bf_57c90c);
            }
        } else {
            engine_set_demand_redraw(2);
            engine_set_current_screen(g_bf_57c90c);
        }
        break;
    case 57:
        if ((!TURN_IDLE() && s_slot_0057c8fc == 0) || LMB_UP != 0 || rl_40e7e4() != 0) {
            engine_set_demand_redraw(2);
            engine_set_current_screen(g_rl_56d894);
            break;
        }
        dispatch_tail();
        break;
    case 66:
    case 67:
    case 49:
    case 37:
        if (LMB_UP != 0 || rl_40e7e4() != 0) {
            engine_set_demand_redraw(2);
            engine_set_current_screen(g_rl_56d894);
            break;
        }
        dispatch_tail();
        break;
    case 38:
        if (!TURN_IDLE() || LMB_UP != 0) {
            rl_497a34(0);
            break;
        }
        dispatch_tail();
        break;
    case 33:
        if (LMB_UP != 0 || rl_40e7e4() != 0) {
            engine_set_demand_redraw(2);
            engine_set_current_screen(g_rl_5c9278);
        }
        break;
    case 34:
        if (g_sim_turn_progress != 0)
            rl_42df30();        /* the screen-34 movie stop */
        if (LMB_UP != 0)
            rl_42df30();
        if (g_bf_4e65d8 != 0)
            rl_42df30();
        if (g_rl_4eabb4 != 0)
            rl_42df30();
        break;
    case 35:
        break;
    case 58:
        if (g_bf_4e65d8 != 0 || LMB_UP != 0) {
            engine_set_current_screen(g_rl_57c920);
            engine_set_demand_redraw(1);
        }
        break;
    case 60:
        if (g_bf_4e65d8 != 0 || LMB_UP != 0) {
            engine_set_current_screen(61);
            rl_40902b((int)(uintptr_t)S4D5620(0x4d5674), (int)(uintptr_t)S4D5620(0x4d5668));   /* the demo screen's sheet + palette (rl ui4) */
        }
        break;
    case 61:
    case 69:
        if (g_bf_4e65d8 != 0 || LMB_UP != 0)
            g_gfx_last_error = 1;
        break;
    default:
        dispatch_tail();
        break;
    }
done:
    if (g_rl_57cac4 != g_rl_52afa0 && g_rl_52afa0 == 1)
        g_rl_565488 = 1;
    g_rl_4eabb4 = 0;
    return g_rl_52afa0;
}

/* ---------------------------------------------------------------- the workforce screen's grid */
extern int32_t g_rl_553f54;          /* DAT_00553f54 (rl_render_globals.h): the labor screen's sprite row */

/* Port of FUN_004398f5 (187 B) — the workforce grid's category (0..8) at (x, y): the click map DAT_00542cf8
 * (45 × 8-px cells from (64, DAT_0057c908)), 0 outside [64, 424) × [y0, y0 + 320), clamped to 8. */
int rl_4398f5(int x, int y)
{
    if (x < 64 || x >= 424)
        return 0;
    if (g_wf_y_origin > y || g_wf_y_origin + 320 <= y)
        return 0;
    unsigned v = g_rl_542cf8[45 * ((y - g_wf_y_origin) / 8) + (x - 64) / 8];
    return v >= 8 ? 8 : (int)v;
}

/* Port of FUN_0045183a (107 B) — is task `cat` shown for county `c`: any but 0, or category 0 with +0x2dd or
 * +0x2ad set. */
int rl_45183a(int c, int cat)
{
    if (cat != 0)
        return 1;
    if (CO8(c, 0x53fc8d) != 0)
        return 1;
    return CO8(c, 0x53fc5d) != 0;
}

/* Port of FUN_00439df0 (235 B) — a press on the workforce grid (DAT_004eabc5, inside [64, 512) ×
 * [y0, y0 + 376)): its category (DAT_0053f0d8, 0x4398f5) reassigned — MP message 38, else 0x439edb for the
 * county: 1; else 0. */
int rl_439df0(void)
{
    if (g_bf_4eabc5 == 0)
        return 0;
    if (g_bf_4e65fc < 64 || g_bf_4e65fc >= 512)
        return 0;
    if (g_wf_y_origin > g_bf_4e65f4 || g_wf_y_origin + 376 <= g_bf_4e65f4)
        return 0;
    g_rl_53f0d8 = rl_4398f5(g_bf_4e65fc, g_bf_4e65f4);
    if (g_rl_53f0d8 == 0)
        return 0;
    if (MP_ON)
        rl_43eda0(38, 0);
    else
        rl_439edb(SELC, g_rl_53f0d8 - 1);
    return 1;
}

/* Port of FUN_0043a123 (145 B) — a release on the workforce grid (DAT_004eabf0 at DAT_004eac04 / 08) over a
 * shown task opens its labor screen: the sprite row DAT_00553f54 = 0x4517ca + 1; 1, else 0. */
int rl_43a123(void)
{
    if (g_rl_4eabf0 == 0)
        return 0;
    int cat = rl_4398f5(g_rl_4eac04, g_rl_4eac08);
    if (cat == 0)
        return 0;
    if (rl_45183a(SELC, cat - 1) == 0)
        return 0;
    g_rl_553f54 = engine_workforce_row_sprite_index(SELC, cat - 1) + 1;   /* 0x4517ca */
    return 1;
}

/* ================================================================ the multiplayer pacing, the delayed actions */
extern int32_t g_rl_4d2160;          /* DAT_004d2160 (rl_render_globals.h): the MP rate base */
extern int32_t g_rl_512fa8;          /* DAT_00512fa8 (rl_render_globals.h): the largest lead seen */
extern int32_t g_rl_515908;          /* DAT_00515908 (rl_render_globals.h): the mean lead */
extern int32_t g_rl_512f6c;          /* DAT_00512f6c (rl_render_globals.h): the delayed actions pending */
extern uint8_t g_rl_50d7c0[0x5780];  /* DAT_0050d7c0 (rl_ui3_globals.h): the delayed-action records, 400 × 56 B */
#define DA32(j, va) AT(int32_t, g_rl_50d7c0, 0x50d7c0u, (int32_t)(va) + 56 * (j))
#define DA8(j, va)  AT(uint8_t, g_rl_50d7c0, 0x50d7c0u, (int32_t)(va) + 56 * (j))
#define DQ(k)       AT(int32_t, g_rl_515990, 0x515990u, 0x515990 + 4 * (k))   /* the due list, 100 entries (0xffff: none) */

/* Port of FUN_0043de70 (1216 B) — the scenario / MP pacing hint for the frame driver: 0 unless the engine runs
 * (state 3); single player (or the local realm out) 1. MP: with few allies active (0x43e433) the local tick's lead
 * over the slowest (0x43e50d) picks 90 / 70 / 30 / 1 (-1 when not ahead), at least 3 · DAT_004d2160; else the lead
 * over the filtered slowest (0x43e330(0)): 1 while DAT_0053f068 == 1; ahead → a rate by the lead (two tables:
 * the battle mode 5's and the map's) + DAT_004d2160, the lead statistics kept (DAT_00512fa8 / 515914 / 515908;
 * 100 frames at the top rate send message 10); behind → -2 … -10 by how far. */
int rl_43de70(void)
{
    if (g_engine_state != 3)
        return 0;
    if (!MP_ON)
        return 1;
    if (PL8(LOCAL, 0x57bf04) == 0)
        return 1;
    if (rl_43e433() != 0) {   /* count_active_allies_low */
        int lo = (int)rl_43e50d();   /* min_tick_all */
        int lead = AT(int32_t, &g_bf_53e8d0, 0x53e8d0u, 0x53e8d0 + 4 * LOCAL) - lo;
        int v;
        if (lead > 800)      v = 90;
        else if (lead > 400) v = 70;
        else if (lead > 50)  v = 30;
        else if (lead > 0)   v = 1;
        else                 return -1;
        return 3 * g_rl_4d2160 > v ? 3 * g_rl_4d2160 : v;
    }
    int lo = (int)rl_43e330(0u);   /* min_tick_filtered */
    if (g_rl_53f068 == 1)
        return 1;
    g_rl_515b20++;
    int lead = AT(int32_t, &g_bf_53e8d0, 0x53e8d0u, 0x53e8d0 + 4 * LOCAL) - lo;
    if (lead <= 0) {
        int behind = (int)rl_43e330((uint32_t)LOCAL)   /* min_tick_filtered */
                     - AT(int32_t, &g_bf_53e8d0, 0x53e8d0u, 0x53e8d0 + 4 * LOCAL);
        if (behind < 20)    return -2;
        if (behind < 50)    return -3;
        if (behind < 100)   return -4;
        if (behind < 200)   return -5;
        if (behind < 400)   return -6;
        if (behind < 1000)  return -7;
        if (behind < 10000) return -8;
        return -10;
    }
    static const int16_t k_lim5[13] = { 709, 609, 509, 409, 309, 209, 169, 129, 100, 89, 49, 29, 9 };
    static const int16_t k_lim[8] = { 400, 200, 59, 49, 39, 29, 19, 9 };
    static const uint8_t k_v[8] = { 140, 130, 120, 110, 100, 80, 60, 40 };
    int v;
    if (s_slot_0057c8fc == 5) {
        int k = 0;
        while (k < 13 && lead <= k_lim5[k])
            k++;
        v = 140 - 10 * k;
    } else {
        int k = 0;
        while (k < 8 && lead <= k_lim[k])
            k++;
        v = k < 8 ? k_v[k] : 20;
    }
    if (g_rl_512fa8 < lead)
        g_rl_512fa8 = lead;
    g_rl_515914 += lead;
    if (g_rl_515b20 != 0)
        g_rl_515908 = g_rl_515914 / g_rl_515b20;
    if (v < 140) {
        g_rl_4d5c90 = 0;
    } else if (++g_rl_4d5c90 > 100) {
        rl_43eda0(10, 0);
        g_rl_4d5c90 = 0;
    }
    return v + g_rl_4d2160;
}

/* Port of FUN_004472ce (1046 B) — MP only: run the delayed actions due by tick `now` (records with +0x36 == 1
 * and +0 ≤ now, in record order; in the battle with several due, the moves 0x21..0x23 of other realms than
 * DAT_005681ec go last); each loads its ten arguments into DAT_005158e8 … DAT_0050d7bc (DAT_0051590c = the
 * record), runs its handler (the table DAT_004d5c98 by +0x37) and is retired (DAT_00512f6c counts down). EAX:
 * the caller's in single player (nobody reads it). */
int rl_4472ce(int now)
{
    if (!MP_ON)
        return 0;
    for (int i = 0; i < 100; i++)
        DQ(i) = 0xffff;
    g_rl_512f58 = 0;
    g_rl_515910 = 0;
    for (int j = 0; j < 400; j++)
        if (DA8(j, 0x50d7f6) == 1 && DA32(j, 0x50d7c0) <= now)
            DQ(g_rl_515910++) = j;
    if (g_rl_515910 <= 0)
        return 0;
    if (g_rl_515910 > 1 && s_slot_0057c8fc == 2) {
        for (int k = 0; g_rl_515910 > k; k++) {
            int r = DQ(k);
            if (DA8(r, 0x50d7f7) >= 0x21 && DA8(r, 0x50d7f7) <= 0x23 && DA32(r, 0x50d7c4) != (int)s_slot_005681ec) {
                g_rl_50d620[g_rl_512f58++] = r;
                DQ(k) = 0xffff;
            }
        }
        for (int m = 0; m < 10; m++)
            for (int n = 0; g_rl_515910 > n; n++)
                if (DQ(n) == 0xffff)
                    for (int ii = n; g_rl_515910 > ii; ii++)
                        DQ(ii) = DQ(ii + 1);
        int jj = 0;
        while (jj < 100 && DQ(jj) < 0xffff)
            jj++;
        for (int kk = 0; g_rl_512f58 > kk; kk++)
            DQ(jj + kk) = g_rl_50d620[kk];
    }
    for (int mm = 0; mm < 100; mm++) {
        int r = DQ(mm);
        if (r >= 0xffff)
            break;
        g_rl_5158e8 = DA32(r, 0x50d7c4);
        g_rl_5158a8 = DA32(r, 0x50d7c8);
        g_rl_5158a4 = DA32(r, 0x50d7cc);
        g_rl_5158b0 = DA32(r, 0x50d7d0);
        g_rl_5158ac = DA32(r, 0x50d7d4);
        g_rl_5158bc = DA32(r, 0x50d7d8);
        g_rl_5158b4 = DA32(r, 0x50d7dc);
        g_rl_5158c4 = DA32(r, 0x50d7e0);
        g_rl_5158c0 = DA32(r, 0x50d7e4);
        g_rl_50d7bc = DA32(r, 0x50d7e8);
        g_rl_51590c = r;
        rl_va_call((uint32_t)g_rl_4d5c98[DA8(r, 0x50d7f7)], 0);   /* the 50 delayed-action handlers (rl_mpmsg2 / 3) */
        DA8(r, 0x50d7f6) = 0;
        --g_rl_512f6c;
    }
    return 0;
}

/* ================================================================ the army state machine's march */
/* The executor 0x465d28 never initializes four locals (dir, outcome code, waypoint col / row: quirk #229); called
 * from this handler they sit 0x20..0x2c below its entry esp. They hold what the previous handler's executor left —
 * or, after a region change, the four locals of 0x4ad6c0 (sub esp, 0x10 at the same depth: its loop counters j = 151
 * and k = 17, army 150's owner (+0, signed) and its county's owner), which overwrite them last. The port models them
 * as rl_46521f_res[] (in and out; rhost moves them: rh_res_get / rh_res_put). s_move_run is the executor with those
 * locals (rhost: the linked engine_army_move_run with its queue; unbound: retail's, which then sees the C stack —
 * the standalone's TODO). */
uint32_t rl_46521f_res[4];
int rl_46521f_moved;                 /* the executor ran this call */
static void (*s_move_run)(uint32_t *res);
void rl_ctl_bind_move_run(void (*fn)(uint32_t *res)) { s_move_run = fn; }

/* Port of FUN_0046521f (615 B) — army state 1, marching / occupying (the army DAT_005533e8, a mirror): ticks
 * 15; the sprite by head count (≤ 300 72, ≤ 600 96, else 120, + 3 · ((anim + 1) & 7)) plus the facing's offset
 * DAT_004d6a78[+0x1b, signed]; a new region under it entered (0x4abb36) and the income recollected (0x4ad6c0);
 * the timer +0x160 down; then the executor (substate +0x14c == 2), or substate 2 once not ready (+0x06 == 0). */
int rl_46521f(void)
{
    int a = g_army_state_index;
    rl_46521f_moved = 0;
    AR8(a, 0x52f204) = 15;
    int anim = 3 * (((int8_t)AR8(a, 0x52f0b9) + 1) & 7);
    if (AR32(a, 0x52f218) <= 300)
        AR8(a, 0x52f0b7) = (uint8_t)(anim + 72);
    else if (AR32(a, 0x52f218) <= 600)
        AR8(a, 0x52f0b7) = (uint8_t)(anim + 96);
    else
        AR8(a, 0x52f0b7) = (uint8_t)(anim + 120);
    AR8(a, 0x52f0b7) = (uint8_t)(AT(int32_t, g_rl_4d6a78, 0x4d6a78u, 0x4d6a78 + 4 * (int8_t)AR8(a, 0x52f0cb)) + AR8(a, 0x52f0b7));
    int region = AT(uint8_t, g_overworld_map, 0x522f90u, 0x522f97 + AR32(a, 0x52f0bc));
    if (AR8(a, 0x52f0c0) != region) {
        rl_4abb36(a, region);   /* rl_link: engine_combat_army_enter_region (a port queue) */
        engine_realm_collect_territory_income();                          /* 0x4ad6c0 */
        rl_46521f_res[0] = 151;                                           /* 0x4ad6c0's locals, left below this frame */
        rl_46521f_res[1] = (uint32_t)(int32_t)(int8_t)AR8(150, 0x52f0b0);
        rl_46521f_res[2] = 17;
        rl_46521f_res[3] = CO8(AR8(150, 0x52f0c0), 0x53f9b5);
        a = g_army_state_index;
    }
    if (AR32(a, 0x52f210) > 0)
        --AR32(a, 0x52f210);
    if ((int8_t)AR8(a, 0x52f1fc) == 2) {
        rl_46521f_moved = 1;
        if (s_move_run)
            s_move_run(rl_46521f_res);
        else
            engine_retail_call(0x465d28u, 0);   /* rl_link: engine_army_move_run (a port queue; the locals' residue) */
        return 0;
    }
    if (AR8(a, 0x52f0b6) == 0)
        AR8(a, 0x52f1fc) = 2;
    return 84 * a;
}
