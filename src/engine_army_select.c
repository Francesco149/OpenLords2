/*
 * OpenLords2 — strategic-map army-select / move dispatcher.  See
 * engine_army_select.h for the full retail address map + design notes.
 */

#include <stdint.h>

#include "engine_army_select.h"
#include "save.h"                  /* g_per_army_768, g_per_territory_420,
                                      g_overworld_map */
#include "save_schema_globals.h"   /* s_slot_0056d5dc, s_slot_0057c93c */
#include "engine_army_ai.h"        /* engine_army_ai_rebalance_workforce */
#include "engine_army_economy.h"   /* provision / plan_realm / plan_resource /
                                      stamp_resource_cell */
#include "engine_territory.h"      /* allocate_realm_workforce /
                                      clear_cell_path_overlay /
                                      recenter_on_tile */
#include "engine_strat_cost.h"     /* engine_strat_cost_rebuild */
#include "engine_path_field.h"     /* fill / clear_slot */
#include "engine_combat.h"         /* g_combat_flag_530d0 / set_flag_567d0c */
#include "engine_dirty_rect.h"
#include "engine_confirm_dialog.h" /* the has-link Yes/No confirm modal */

#define ARMY_REC(key)  (&g_per_army_768[(key) * 0x300])
#define TERR_REC(key)  (&g_per_territory_420[(key) * 0x1a4])
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])

/* Audience filter for engine_event_queue_post — same per-call snapshot
 * idiom engine_realm.c uses (no shared helper exists yet to reuse). */
static void player_has_data(uint8_t out[7])
{
    for (int i = 0; i < 6; i++)
        out[i] = STRAT_REC(i)[0x05];
    out[6] = 0;
}

/* The "wrong owner" notice (code 0x70) FUN_0043ce1a posts from all three
 * branches where the clicked county/army doesn't belong to local_player. */
static void post_wrong_owner_notice(engine_event_queue_state_t *evq,
                                    int local_player)
{
    uint8_t has_data[7];
    player_has_data(has_data);
    engine_event_queue_post(evq, 0, local_player, 0x70, 0, 0, 0, 0, 0,
                            has_data, local_player);
}

void engine_army_select_territory_link_check(int county_key)
{
    uint8_t *t = TERR_REC(county_key);
    uint8_t  linked_army = t[0x199];

    int32_t target_key = *(int32_t *)&ARMY_REC(linked_army)[0x1bc];

    if (target_key == 0) {
        t[0x199] = 0;
    }
    if (TERR_REC(target_key)[0x198] != linked_army) {
        t[0x199] = 0;
    }
}

/* Leaf of FUN_0043d309 (FUN_00448648): notify every army owned by `owner`
 * with a slot-2 resource re-plan (the fixed (rec_idx=7, target=0xf,
 * divisor=4) the 0x4485a5 call table uses for slot 2). */
static void army_select_notify_owner_class(int owner)
{
    int count = (int)s_slot_0056d5dc;
    for (int key = 1; key <= count; key++) {
        if (ARMY_REC(key)[0x05] == (uint8_t)owner) {
            engine_army_economy_plan_resource(key, 2, 7, 0xf, 4);
        }
    }
}

void engine_army_select_assign_task(int army_key, int task,
                                    int local_player,
                                    int season_event_mode, int season_map,
                                    engine_event_queue_state_t *evq)
{
    uint8_t *r = ARMY_REC(army_key);
    int slot;            /* the workforce-rebalance slot (local_8) */
    int mode;             /* rebalance mode (local_c) */
    int event_delta;      /* local_10 */

    if (task == 4) {
        slot = 3;
        mode = r[0x1bb] != 0;
        engine_army_ai_rebalance_workforce(army_key, 3, mode);
        if (r[0x1bb] == 0) {
            r[0x1bb] = 1;
            event_delta = -1;
        } else {
            r[0x1bb] = 0;
            event_delta = -2;
        }
        mode = r[0x1bb] != 0;
    } else {
        event_delta = task * 2;
        uint8_t *flag = &r[0x297 + task * 0x18];
        *flag ^= 1;
        if (*flag != 0) {
            event_delta += 1;
        }
        switch (task) {
        case 0:  slot = 6; break;
        case 3:  slot = 5; break;
        case 1:  slot = 4; break;
        default: slot = 7; break;   /* task == 2 (only remaining 0..3 value) */
        }
        mode = (*flag != 0);
    }

    engine_army_economy_plan_realm(army_key, season_map);
    engine_army_ai_rebalance_workforce(army_key, slot, mode);
    engine_army_economy_plan_realm(army_key, season_map);
    engine_territory_allocate_realm_workforce(army_key);
    engine_army_economy_provision(army_key, season_event_mode);
    engine_territory_allocate_realm_workforce(army_key);
    engine_army_economy_plan_realm(army_key, season_map);
    engine_army_economy_stamp_resource_cell(army_key, task);
    army_select_notify_owner_class(r[0x05]);

    /* Retail also mirrors the cursor position into ARMY_REC[+0x60c]/[+0x608]
     * scratch (DAT_0053f0a0/DAT_0053f09c) here — no reader found anywhere
     * in the corpus searched this session; faithfully omitted. */
    if (r[0x05] == (uint8_t)local_player) {
        uint8_t has_data[7];
        player_has_data(has_data);
        engine_event_queue_post(evq, 0, local_player, event_delta + 0xe6,
                                0, 4, 0, 0, 0, has_data, local_player);
    }
}

/* Leaf of FUN_0043723a (FUN_0046f68e via FUN_004a8d9c): clear the path
 * overlay + the local player's path-field slot.  Retail's explicit
 * 0x2000-byte field zero (FUN_004b3e51) is already folded into
 * engine_path_field_fill itself (see its memset, "retail 0x4b3e51"); the
 * (col,row)-cache writes to DAT_00504020/24/DAT_004f4070 have no reader
 * found in this session's corpus search — faithfully omitted. */
static void army_select_arm_handoff(int local_player)
{
    engine_territory_clear_cell_path_overlay();
    engine_path_field_clear_slot(local_player);
}

void engine_army_select_open_handoff(int county_key, int ready,
                                     int local_player, int tile_stride,
                                     int map_width,
                                     int *out_screen, int *out_demand_redraw,
                                     int *out_handoff_cooldown,
                                     int *out_cell_col, int *out_cell_row,
                                     int *out_armed_county)
{
    *out_demand_redraw = 2;
    if (!ready) {
        *out_screen = 0;
        return;
    }

    engine_strat_cost_rebuild(g_overworld_map);
    *out_screen = 0x10;
    *out_handoff_cooldown = 0x28;
    *out_armed_county = county_key;

    uint8_t *t = TERR_REC(county_key);
    int col = (int8_t)t[0x0a];
    int row = (int8_t)t[0x0b];

    army_select_arm_handoff(local_player);
    t[0x14e] = 0;

    engine_path_field_fill(local_player, col, row, 0);

    int32_t cell_off = *(int32_t *)&t[0x0c];
    int idx = tile_stride ? cell_off / tile_stride : 0;
    *out_cell_col = map_width ? idx % map_width : 0;
    *out_cell_row = map_width ? idx / map_width : 0;
}

/* Staged result of a confirm-dialog callback firing on a later tick than
 * engine_army_select_confirm_or_open's own call — see the header doc for
 * both functions.  Zero-initialized statics: no result pending until the
 * first confirm dialog resolves. */
static int g_confirm_result_pending;
static int g_confirm_result_screen;
static int g_confirm_result_redraw;
static int g_confirm_result_cooldown;
static int g_confirm_result_armed_county;

/* The confirm-dialog's callback (retail FUN_0043723a called via
 * FUN_0040e6f2(0xd,...)).  Zero-arg per engine_confirm_dialog_callback_fn
 * — reads the answer + the county/player/geometry the opener stashed. */
static int g_confirm_county_key;
static int g_confirm_local_player;
static int g_confirm_tile_stride;
static int g_confirm_map_width;

static void army_select_confirm_move_cb(void)
{
    int ready = engine_confirm_dialog_answer();
    int screen = 0, redraw = 0, cooldown = 0, col = 0, row = 0, armed = 0;
    engine_army_select_open_handoff(g_confirm_county_key, ready,
                                    g_confirm_local_player,
                                    g_confirm_tile_stride, g_confirm_map_width,
                                    &screen, &redraw, &cooldown, &col, &row,
                                    &armed);
    g_confirm_result_pending      = 1;
    g_confirm_result_screen       = screen;
    g_confirm_result_redraw       = redraw;
    g_confirm_result_cooldown     = cooldown;
    g_confirm_result_armed_county = armed;
}

int engine_army_select_confirm_or_open(int county_key,
                                       int local_player, int tile_stride,
                                       int map_width,
                                       int *out_screen, int *out_demand_redraw,
                                       int *out_handoff_cooldown,
                                       int *out_cell_col, int *out_cell_row,
                                       int *out_armed_county,
                                       engine_dirty_rect_state_t *dirty)
{
    if (dirty != NULL) {
        engine_dirty_rect_mark_below_status_bar(dirty);
    }
    *out_screen = 0;
    *out_demand_redraw = 2;

    int has_link = TERR_REC(county_key)[0x199] != 0;
    if (!has_link) {
        /* Direct path: retail sets DAT_0059151c = 1 unconditionally right
         * before this branch and nothing resets it before FUN_0043723a
         * runs synchronously — ready is forced 1, not an external input
         * here (see header doc). */
        engine_army_select_open_handoff(county_key, /*ready=*/1, local_player,
                                        tile_stride, map_width,
                                        out_screen, out_demand_redraw,
                                        out_handoff_cooldown,
                                        out_cell_col, out_cell_row,
                                        out_armed_county);
        return 1;
    }

    /* Confirm-dialog path (FUN_0040e6f2(0xd,0xa0,0xa0,FUN_0043723a)).  The
     * callback fires later (see header doc); stash what it needs. */
    g_confirm_county_key   = county_key;
    g_confirm_local_player = local_player;
    g_confirm_tile_stride  = tile_stride;
    g_confirm_map_width    = map_width;
    engine_confirm_dialog_open(/*msg_id=*/0xd, 0xa0, 0xa0, /*prev_screen=*/0,
                               army_select_confirm_move_cb,
                               out_screen, out_demand_redraw);
    return 0;
}

int engine_army_select_poll_confirm_result(int *out_screen,
                                           int *out_demand_redraw,
                                           int *out_handoff_cooldown,
                                           int *out_armed_county)
{
    if (!g_confirm_result_pending) {
        return 0;
    }
    g_confirm_result_pending = 0;
    *out_screen           = g_confirm_result_screen;
    *out_demand_redraw    = g_confirm_result_redraw;
    *out_handoff_cooldown = g_confirm_result_cooldown;
    *out_armed_county     = g_confirm_result_armed_county;
    return 1;
}

void engine_army_select_dispatch(const engine_army_select_in_t *in,
                                 engine_army_select_state_t *st,
                                 const engine_army_select_hooks_t *hooks,
                                 engine_event_queue_state_t *evq)
{
    static const engine_army_select_hooks_t empty_hooks = {0};
    if (!hooks) hooks = &empty_hooks;

    if (in->game_mode != 0) return;

    int turn_idle = (in->turn_progress == 0 &&
                     (in->turn_busy == 0 || in->timeout_reached != 0));
    if (!turn_idle) return;
    if (in->game_active != 0) {
        st->screen = 0;
        return;
    }
    if (in->view_mode == 2) return;
    if (st->hold_pending != 0) {
        st->hold_pending = 0;
        return;
    }

    if (in->extended_pause != 0) {
        /* FUN_00476710: dismiss the open scroll UNLESS it's one of the 4
         * popup types that should survive a map click while paused. */
        if (hooks->dismiss_on_paused_click &&
            in->popup_type != 0x11 && in->popup_type != 0xa &&
            in->popup_type != 0xb && in->popup_type != 0xc) {
            hooks->dismiss_on_paused_click();
        }
        return;
    }

    int county_id  = in->county_id;
    int head_link  = in->head_link;
    uint8_t *terr  = county_id ? TERR_REC(county_id) : NULL;
    int kind       = terr ? terr[0x08] : 0;

    if (county_id == 0 || kind != 1) {
        if (county_id == 0 || kind != 3) {
            /* Neither county nor army at this cell. */
            if ((in->flags & 0x80) == 0) {
                if ((in->flags & 0x40) == 0) {
                    if ((in->flags & 0x20) != 0 &&
                        ARMY_REC(head_link)[0x05] == (uint8_t)in->local_player) {
                        /* Open terrain-info (FUN_0041b032).  This module
                         * only requests the screen + redraw — the actual
                         * panel content is g_ti_attrs, normally populated
                         * by ti_compute (the terrain-info-open path); this
                         * branch reaching screen 4 WITHOUT going through
                         * that path is a known narrow gap (rare flag combo:
                         * bit 0x20 set, bits 0x80/0x40 clear, own army). */
                        st->screen = 4;
                    }
                } else if (ARMY_REC(head_link)[0x05] == (uint8_t)in->local_player) {
                    int32_t order_ptr = *(int32_t *)&ARMY_REC(head_link)[0x70];
                    if (order_ptr != 0) {
                        s_slot_0057c93c = (uint32_t)head_link;
                        engine_territory_recenter_on_tile(order_ptr);
                        if (hooks->map_scene_tick) hooks->map_scene_tick(in->now_ms);
                        g_combat_flag_530d0 = 1;
                    }
                    st->screen = 2;
                    if (hooks->workforce_render) hooks->workforce_render(head_link);
                }
            } else if (ARMY_REC(head_link)[0x05] == (uint8_t)in->local_player) {
                if (head_link != (int)s_slot_0057c93c) {
                    int32_t order_ptr = *(int32_t *)&ARMY_REC(head_link)[0x70];
                    if (order_ptr == 0) return;
                    s_slot_0057c93c = (uint32_t)head_link;
                    engine_territory_recenter_on_tile(order_ptr);
                }
                int task;
                if (in->terrain_type < 4)        task = 1;
                else if (in->terrain_type < 7)    task = 3;
                else if (in->terrain_type < 10)   task = 2;
                else if (in->terrain_type < 0xd)  task = 0;
                else if (in->terrain_type < 0x15) { return; }
                else                                task = 4;
                engine_army_select_assign_task(head_link, task, in->local_player,
                                               in->season_event_mode,
                                               in->season_map, evq);
                /* 0x43eda0(0x3a,0) — multiplayer-only command echo,
                 * dead in SP (g_sim_mp_active == 0); not ported. */
            } else {
                post_wrong_owner_notice(evq, in->local_player);
            }
        } else if (ARMY_REC(head_link)[0x05] == (uint8_t)in->local_player) {
            /* county_id != 0, kind == 3 (an army occupies the clicked
             * county's cell). */
            st->hud_outcome = county_id;
            int32_t order_ptr = *(int32_t *)&ARMY_REC(head_link)[0x70];
            if (order_ptr != 0) {
                s_slot_0057c93c = (uint32_t)head_link;
                engine_territory_recenter_on_tile(order_ptr);
                st->screen = 8;
                st->demand_redraw = 1;
                g_combat_flag_530d0 = 2;
                engine_combat_set_flag_567d0c(2);
            }
        } else {
            post_wrong_owner_notice(evq, in->local_player);
        }
    } else if ((int8_t)terr[0x00] == in->local_player) {
        /* county_id != 0, kind == 1 (an owned county). */
        if (terr[0x199] != 0) {
            engine_army_select_territory_link_check(county_id);
        }
        if (terr[0x199] == 0) {
            int screen, redraw, cooldown, col, row;
            int armed = st->armed_county;
            engine_army_select_confirm_or_open(county_id, in->local_player,
                                               in->tile_stride, in->map_width,
                                               &screen, &redraw, &cooldown,
                                               &col, &row, &armed, NULL);
            st->screen = screen;
            st->demand_redraw = redraw;
            st->handoff_cooldown = cooldown;
            st->armed_county = armed;
        } else {
            st->screen = 0x1d;
            st->demand_redraw = 2;
        }
    } else {
        post_wrong_owner_notice(evq, in->local_player);
    }
}
