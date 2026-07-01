/*
 * OpenLords2 — screen-0x10 army-move/merge confirm chain.  See
 * engine_army_move_confirm.h for the retail address map + design notes.
 */

#include "engine_army_move_confirm.h"

#include <stdint.h>

#include "save.h"                  /* g_overworld_map, g_per_army_768,
                                       g_per_territory_420,
                                       g_player_strategic_352 */
#include "engine_path_field.h"     /* cost/fill/trace/wp (already-ported
                                       pathfinder — the "producer" for the
                                       hover-range gate, see .h) */
#include "engine_strat_cost.h"     /* engine_strat_cost_rebuild */
#include "engine_combat.h"         /* engine_combat_garrison_cap,
                                       engine_combat_break_siege_link */
#include "engine_confirm_dialog.h" /* the generic Yes/No modal (screen 0x1e) */
#include "engine.h"                /* g_sim_mp_active (retail DAT_00553030) */

#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])
#define TERR_REC(key)   (&g_per_territory_420[(key) * 0x1a4])
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])
#define CELL(idx)       (&g_overworld_map[(idx) * 8])

/* ════════════════════════════════════════════════════════════════════
 * Hover classifier (render-tick) — FUN_004a8e0b + leaves
 * ════════════════════════════════════════════════════════════════════ */

/* FUN_00469e0e: cell[+0] (terrain TYPE) in [2, 0x17) — used by the
 * "attack target" classification as an extra terrain gate. */
static int hover_terrain_attackable(int cell)
{
    uint8_t terrain_type = CELL(cell)[0];
    return (terrain_type >= 2 && terrain_type < 0x17) ? 1 : 0;
}

/* FUN_004a91ba: mark bit 0x40 on cell[+2] (the "dispatch flag" byte, per
 * engine_overlay_render.h) for every waypoint of the local player's
 * just-traced path — an overlay hint for the render side, not consumed
 * by anything else in this chain. */
static void hover_mark_path_overlay(int local_player)
{
    int count = engine_path_field_wp_count(local_player);
    for (int i = 0; i < count; i++) {
        int col, row;
        engine_path_field_wp(local_player, i, &col, &row);
        int cell = row * ENGINE_PATH_FIELD_DIM + col;
        CELL(cell)[2] |= 0x40;
    }
}

/* Persistent hover-classify state — retail's DAT_005691e0 (last-seen
 * hover cell), DAT_0057d358 (armed flag), the six classification
 * scratch globals, and the "confirmed" (col,row) + cursor snapshot the
 * click resolver reads.  All transient/session state, no save-schema
 * home (mirrors engine.c's own "no home in state struct" statics, one
 * layer down — see engine_army_select.c's g_confirm_result_* precedent). */
static int g_last_hover_cell = -1;
static int g_armed;                  /* DAT_0057d358 */
static int g_confirmed_col, g_confirmed_row;      /* DAT_0056d5ac/0056d5a8 */
static int g_cursor_snapshot;        /* DAT_004ee898 (stashed, no traced
                                      * consumer this session — kept for
                                      * fidelity, same status as several
                                      * other "no reader found" writes
                                      * elsewhere in this codebase) */

/* The six classification scratch globals FUN_004a9252 branches on. */
static int g_classify_merge;      /* DAT_005651a4 — friendly army, mergeable */
static int g_classify_claim;      /* DAT_0057cb1c — unclaimed cell, new-county-claim */
static int g_classify_attack;     /* DAT_00544020 — enemy-owned, attackable */
static int g_classify_garrison;   /* DAT_005689ac — own idle army/keep, reinforce */
static int g_classify_direct;     /* DAT_0057c8c8 — direct-execute (no dialog) case */
static int g_classify_swap;       /* DAT_00553f44 — a different owned county under cursor */

void engine_army_move_confirm_hover_tick(const engine_army_move_hover_in_t *in,
                                         engine_army_move_hover_state_t *st)
{
    if (!in->hover_valid) {
        g_armed = 0;
        return;
    }

    if (st->handoff_cooldown != 0) {
        st->handoff_cooldown--;
    }
    hover_mark_path_overlay(in->local_player);

    /* retail: DAT_00568974 compared directly (a byte offset); the port's
     * `hover_cell` is already the cell INDEX (see .h), so compare that —
     * equivalent since the byte offset is a fixed multiple of it. */
    if (g_last_hover_cell == in->hover_cell) {
        return;
    }
    g_last_hover_cell = in->hover_cell;

    int col = in->map_width ? in->hover_cell % in->map_width : 0;
    int row = in->map_width ? in->hover_cell / in->map_width : 0;

    /* The already-flooded path-field cost to the hovered cell (flooded by
     * engine_army_select_open_handoff when the handoff was armed — see
     * .h).  >99 falls back to the cheapest of the 8 neighbours + 1
     * (FUN_00470678) — retail's own heuristic for a not-yet-relaxed or
     * very expensive direct cell. */
    int cost = engine_path_field_cost(in->local_player, in->hover_cell);
    if (cost > 99) {
        static const int8_t k_deltas[8] = {
            -0x40, -0x3f, 0x01, 0x41, 0x40, 0x3f, -0x01, -0x41,
        };
        int best = 0;
        for (int i = 0; i < 8; i++) {
            int ncell = in->hover_cell + k_deltas[i];
            int c = engine_path_field_cost(in->local_player, ncell);
            if (c != 0 && (best == 0 || c < best)) {
                best = c;
            }
        }
        cost = best + 1;
    }

    if (cost < 2) {
        g_armed = 0;
        return;
    }

    g_classify_claim = 0;
    g_classify_attack = 0;
    g_classify_merge = 0;
    g_classify_swap = 0;
    g_classify_garrison = 0;
    g_classify_direct = 0;

    int head_army = CELL(in->hover_cell)[7];
    uint8_t armed_owner = TERR_REC(in->armed_county)[0];

    if ((CELL(in->hover_cell)[1] & 0x40) &&
        ARMY_REC(head_army)[5] != armed_owner) {
        g_classify_merge = head_army;
    }
    if ((CELL(in->hover_cell)[1] & 0x10) &&
        ARMY_REC(head_army)[5] != armed_owner) {
        g_classify_claim = head_army;
    }
    if ((CELL(in->hover_cell)[1] & 0x20) &&
        hover_terrain_attackable(in->hover_cell) &&
        ARMY_REC(head_army)[5] != armed_owner) {
        g_classify_attack = head_army;
    }

    /* Garrison/direct classification — retail's uVar1/uVar3 comma-operator
     * dance (see .h): the owner-match test always assigns `direct =
     * head_army` as a side effect once the outer gate passes, then
     * overwrites it back to its old value (a no-op reset) ONLY on the
     * owner-MATCH path.  Net effect: owner mismatch -> direct = head_army;
     * owner match -> garrison = head_army (direct untouched); outer gate
     * false -> direct untouched. */
    if ((CELL(in->hover_cell)[1] & 0x80) && CELL(in->hover_cell)[0] > 0x14) {
        if (ARMY_REC(head_army)[5] == armed_owner) {
            g_classify_garrison = head_army;
        } else {
            g_classify_direct = head_army;
        }
    }

    int head_terr = CELL(in->hover_cell)[5];
    if (head_terr != 0) {
        if ((int8_t)TERR_REC(head_terr)[0] == in->local_player &&
            in->armed_county != head_terr &&
            TERR_REC(head_terr)[8] != 4) {
            g_classify_swap = head_terr;
        }
    }

    int traced = engine_path_field_trace(in->local_player, col, row);
    if (!traced) {
        g_armed = 0;
        return;
    }

    if ((int)TERR_REC(in->armed_county)[0x154] -
        (int)TERR_REC(in->armed_county)[0x153] < cost) {
        g_classify_claim = 0;
        g_classify_attack = 0;
        g_classify_swap = 0;
        g_classify_garrison = 0;
        g_classify_direct = 0;
        g_classify_merge = 0;
    }

    g_armed = 1;
    g_confirmed_col = col;
    g_confirmed_row = row;
    g_cursor_snapshot = in->hover_cell;
}

/* ════════════════════════════════════════════════════════════════════
 * Merge/move EXECUTE primitive — FUN_004a98fe
 * ════════════════════════════════════════════════════════════════════ */

static void army_move_confirm_execute(int target_key, int col, int row,
                                      int flag)
{
    engine_strat_cost_rebuild(g_overworld_map);

    uint8_t *t = TERR_REC(target_key);
    engine_path_field_fill(0, (int8_t)t[0x0a], (int8_t)t[0x0b], 0);
    if (!engine_path_field_trace(0, col, row)) {
        return;
    }
    engine_path_field_copy_record(0, target_key);
    t[0x14] = (uint8_t)col;
    t[0x16] = t[0x14];
    t[0x15] = (uint8_t)row;
    t[0x17] = t[0x15];
    t[0x14c] = 2;
    if (t[0x08] == 1) {
        engine_combat_break_siege_link(target_key);
    }
    t[0x152] = (uint8_t)flag;
    t[0x156] = 1;
    t[0x157] = 1;
    t[0x158] = 1;
}

/* ════════════════════════════════════════════════════════════════════
 * Click resolver — FUN_004a9252 + its 4 confirm-dialog siblings
 * ════════════════════════════════════════════════════════════════════ */

/* Staged by a sibling firing via the confirm dialog (async — see .h);
 * drained by engine_army_move_confirm_poll_screen_reset(). */
static int g_pending_screen_reset;

/* Same per-call snapshot idiom engine_army_select.c uses (no shared
 * helper exists yet to reuse). */
static void player_has_data(uint8_t out[7])
{
    for (int i = 0; i < 6; i++) {
        out[i] = STRAT_REC(i)[5];
    }
    out[6] = 0;
}

static int g_click_local_player;     /* stashed for the siblings, which
                                      * (like retail) don't take params */
static int g_click_armed_county;
static engine_event_queue_state_t *g_click_evq;

/* FUN_004a983b: the "direct" path + the msg-4/msg-10 confirm-dialog
 * targets.  Garrison-occupied check (event 0x11e) vs. execute. */
static void sibling_983b(void)
{
    g_classify_swap = 0;
    if (g_classify_merge == 0 ||
        *(int32_t *)&ARMY_REC(g_classify_merge)[0x1bc] == 0) {
        if (engine_confirm_dialog_answer()) {
            if (!g_sim_mp_active) {
                army_move_confirm_execute(g_click_armed_county,
                                          g_confirmed_col, g_confirmed_row, 0);
            }
        }
    } else {
        uint8_t has_data[7];
        player_has_data(has_data);
        engine_event_queue_post(g_click_evq, 0, g_click_local_player, 0x11e,
                                0, 0, 0, 0, 0, has_data, g_click_local_player);
    }
    g_pending_screen_reset = 1;
}

/* FUN_004a960f: empty-cell / new-county-claim (events 0x11d/0x11c/0x113),
 * msg-8 confirm-dialog target. */
static void sibling_960f(void)
{
    g_classify_swap = 0;
    if (!engine_confirm_dialog_answer()) {
        g_pending_screen_reset = 1;
        return;
    }

    int32_t link = *(int32_t *)&ARMY_REC(g_classify_direct)[0x1bc];
    if (link == 0 || TERR_REC(link)[0x19a] == 0) {
        if (link == 0) {
            uint8_t has_data[7];
            player_has_data(has_data);
            int code = (ARMY_REC(g_classify_direct)[0x1c3] == 0) ? 0x11d : 0x11c;
            engine_event_queue_post(g_click_evq, 0, g_click_local_player, code,
                                    0, 0, 0, 0, 0, has_data,
                                    g_click_local_player);
        } else if (!g_sim_mp_active) {
            army_move_confirm_execute(g_click_armed_county,
                                      g_confirmed_col, g_confirmed_row, 0);
        }
    } else {
        uint8_t has_data[7];
        player_has_data(has_data);
        engine_event_queue_post(g_click_evq, 0, g_click_local_player, 0x113,
                                0, 0, 0, 0, 0, has_data, g_click_local_player);
    }
    g_pending_screen_reset = 1;
}

/* FUN_004a975d: merge headcount-cap check (event 0x112), msg-5 target. */
static void sibling_975d(void)
{
    if (engine_confirm_dialog_answer()) {
        int32_t headcount = *(int32_t *)&TERR_REC(g_classify_swap)[0x168] +
                            *(int32_t *)&TERR_REC(g_click_armed_county)[0x168];
        if (headcount < 0x5dd) {
            if (!g_sim_mp_active) {
                army_move_confirm_execute(g_click_armed_county,
                                          g_confirmed_col, g_confirmed_row,
                                          (uint8_t)g_classify_swap);
            }
        } else {
            uint8_t has_data[7];
            player_has_data(has_data);
            engine_event_queue_post(g_click_evq, 0, g_click_local_player,
                                    0x112, 0, 0, 0, 0, 0, has_data,
                                    g_click_local_player);
        }
    }
    g_pending_screen_reset = 1;
}

/* FUN_004a9372: garrison-merge-cap notice — THE 0x11 EVENT-POPUP
 * PRODUCER (code 0xa6), also posts 0xa4/0xa5/0xa7/0x11b/0x121.  msg-7
 * confirm-dialog target. */
static void sibling_9372(void)
{
    g_classify_swap = 0;
    if (!engine_confirm_dialog_answer()) {
        g_pending_screen_reset = 1;
        return;
    }

    int8_t audience = (int8_t)TERR_REC(g_click_armed_county)[0];
    int cap = engine_combat_garrison_cap(ARMY_REC(g_classify_garrison)[0x1c0]);
    int32_t link = *(int32_t *)&ARMY_REC(g_classify_garrison)[0x1bc];

    int besieged_or_no_link;
    if (link == 0) {
        besieged_or_no_link = 1;
    } else {
        cap -= *(int32_t *)&TERR_REC(link)[0x168];
        besieged_or_no_link = (TERR_REC(link)[0x19a] == 0);
    }

    uint8_t has_data[7];
    if (!besieged_or_no_link) {
        player_has_data(has_data);
        engine_event_queue_post(g_click_evq, 0, audience, 0x121, 0, 0, 0, 0, 0,
                                has_data, g_click_local_player);
    } else if (ARMY_REC(g_classify_garrison)[0x1c3] == 1 &&
              ARMY_REC(g_classify_garrison)[0x1c1] == 0) {
        player_has_data(has_data);
        engine_event_queue_post(g_click_evq, 0, audience, 0xa4, 0, 0, 0, 0, 0,
                                has_data, g_click_local_player);
    } else if (ARMY_REC(g_classify_garrison)[0x1c2] == 0) {
        if (cap < 1) {
            player_has_data(has_data);
            engine_event_queue_post(g_click_evq, 0, audience, 0x11b, 0, 0, 0,
                                    0, 0, has_data, g_click_local_player);
        } else if (cap < *(int32_t *)&TERR_REC(g_click_armed_county)[0x168]) {
            player_has_data(has_data);
            engine_event_queue_post(g_click_evq, 0, audience, 0xa6,
                                    g_click_armed_county, 0x11,
                                    (uint8_t)g_classify_garrison, 0,
                                    g_cursor_snapshot,
                                    has_data, g_click_local_player);
        } else if (TERR_REC(g_click_armed_county)[0x196] == 0 ||
                  TERR_REC(link)[0x196] == 0) {
            if (!g_sim_mp_active) {
                army_move_confirm_execute(g_click_armed_county,
                                          g_confirmed_col, g_confirmed_row, 0);
            }
        } else {
            player_has_data(has_data);
            engine_event_queue_post(g_click_evq, 0, audience, 0xa7, 0, 0, 0,
                                    0, 0, has_data, g_click_local_player);
        }
    } else {
        player_has_data(has_data);
        engine_event_queue_post(g_click_evq, 0, audience, 0xa5, 0, 0, 0, 0, 0,
                                has_data, g_click_local_player);
    }
    g_pending_screen_reset = 1;
}

void engine_army_move_confirm_click_resolve(int local_player, int armed_county,
                                            engine_army_move_click_state_t *st,
                                            engine_event_queue_state_t *evq)
{
    if (!g_armed) {
        return;
    }

    engine_confirm_dialog_force_answer(1);
    g_click_local_player = local_player;
    g_click_armed_county = armed_county;
    g_click_evq          = evq;

    if (g_classify_claim != 0) {
        engine_confirm_dialog_open(4, 0xa0, 0xa0, 0, sibling_983b,
                                   &st->screen, &st->demand_redraw);
        return;
    }
    if (g_classify_attack != 0) {
        engine_confirm_dialog_open(10, 0xa0, 0xa0, 0, sibling_983b,
                                   &st->screen, &st->demand_redraw);
        return;
    }
    if (g_classify_swap != 0) {
        engine_confirm_dialog_open(5, 0xa0, 0xa0, 0, sibling_975d,
                                   &st->screen, &st->demand_redraw);
        return;
    }
    if (g_classify_garrison != 0) {
        engine_confirm_dialog_open(7, 0xa0, 0xa0, 0, sibling_9372,
                                   &st->screen, &st->demand_redraw);
        return;
    }
    if (g_classify_direct != 0) {
        engine_confirm_dialog_open(8, 0xa0, 0xa0, 0, sibling_960f,
                                   &st->screen, &st->demand_redraw);
        return;
    }

    sibling_983b();
    /* Direct path: the screen-0 outcome is synchronous (st->screen right
     * here), so drain the flag sibling_983b() also set — it doesn't know
     * whether it ran directly or as a dialog callback — to avoid a
     * spurious extra reset being picked up by a later, unrelated poll. */
    g_pending_screen_reset = 0;
    st->screen        = 0;
    st->demand_redraw = 2;
}

int engine_army_move_confirm_poll_screen_reset(int *out_demand_redraw)
{
    if (!g_pending_screen_reset) {
        return 0;
    }
    g_pending_screen_reset = 0;
    *out_demand_redraw = 2;
    return 1;
}
