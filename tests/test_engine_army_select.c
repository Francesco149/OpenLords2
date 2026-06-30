/*
 * Tests for engine_army_select — the army-select / move dispatcher
 * (FUN_0043ce1a) + its leaves (FUN_004a8426, FUN_0043d309, FUN_0043723a,
 * FUN_004371ce).  See src/engine_army_select.h for the retail address map.
 *
 * Direct-extern style (matches engine_realm.c's tests): reset the shared
 * record arrays, write fields, call the function, assert.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_army_select.h"
#include "engine_path_field.h"
#include "engine_territory.h"
#include "engine_event_queue.h"
#include "save.h"
#include "save_schema_globals.h"

#define AREC(key)  (&g_per_army_768[(key) * 0x300])
#define TREC(key)  (&g_per_territory_420[(key) * 0x1a4])
#define PREC(slot) (&g_player_strategic_352[(slot) * 0x160])
#define AI32(p, o) (*(int32_t *)((p) + (o)))

/* engine_path_field_fill (reached via engine_army_select_open_handoff)
 * needs its move-cost table bound — an all-zero table (every direction
 * cost 0) is enough for the tests here, which only check the source
 * cell's cost (always 1, set unconditionally before the flood). */
static int16_t s_pathfield_terrain[ENGINE_PATH_FIELD_CELLS + 0x83];

static void army_select_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    memset(s_pathfield_terrain, 0, sizeof s_pathfield_terrain);
    s_slot_0056d5dc = 1;
    s_slot_0057c93c = 0;
    engine_territory_bind_cells(g_overworld_map);
    engine_path_field_bind_terrain(s_pathfield_terrain);
}

/* ── engine_army_select_territory_link_check ───────────────────────── */

int test_army_select_link_check_clears_when_target_zero(void)
{
    army_select_reset();
    TREC(5)[0x199] = 9;             /* county 5 links to army 9 */
    AI32(AREC(9), 0x1bc) = 0;        /* army 9 has no target */
    engine_army_select_territory_link_check(5);
    T_ASSERT_EQ_I(TREC(5)[0x199], 0);
    return 0;
}

int test_army_select_link_check_clears_when_backref_mismatches(void)
{
    army_select_reset();
    TREC(5)[0x199] = 9;
    AI32(AREC(9), 0x1bc) = 7;        /* army 9 targets territory 7 */
    TREC(7)[0x198] = 3;              /* but territory 7 points back to army 3, not 9 */
    engine_army_select_territory_link_check(5);
    T_ASSERT_EQ_I(TREC(5)[0x199], 0);
    return 0;
}

int test_army_select_link_check_preserves_valid_mutual_link(void)
{
    army_select_reset();
    TREC(5)[0x199] = 9;
    AI32(AREC(9), 0x1bc) = 7;
    TREC(7)[0x198] = 9;              /* mutual: 7 points back to 9 */
    engine_army_select_territory_link_check(5);
    T_ASSERT_EQ_I(TREC(5)[0x199], 9);
    return 0;
}

/* ── engine_army_select_assign_task ────────────────────────────────── */

int test_army_select_assign_task_garrison_toggle_on(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    AREC(1)[0x1bb] = 0;
    AREC(1)[0x05] = 1;               /* owned by player 1 */
    engine_army_select_assign_task(1, 4, /*local_player=*/1, 0, 0, &evq);
    T_ASSERT_EQ_I(AREC(1)[0x1bb], 1);
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xe5);   /* 0xe6 - 1 */
    return 0;
}

int test_army_select_assign_task_garrison_toggle_off(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    AREC(1)[0x1bb] = 1;
    AREC(1)[0x05] = 1;
    engine_army_select_assign_task(1, 4, 1, 0, 0, &evq);
    T_ASSERT_EQ_I(AREC(1)[0x1bb], 0);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xe4);   /* 0xe6 - 2 */
    return 0;
}

int test_army_select_assign_task_class_toggle_flips_flag(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    AREC(1)[0x05] = 1;
    T_ASSERT_EQ_I(AREC(1)[0x297 + 0 * 0x18], 0);
    engine_army_select_assign_task(1, 0, 1, 0, 0, &evq);
    T_ASSERT_EQ_I(AREC(1)[0x297 + 0 * 0x18], 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xe6 + 1);   /* task*2=0, flag now set: +1 */
    engine_army_select_assign_task(1, 0, 1, 0, 0, &evq);
    T_ASSERT_EQ_I(AREC(1)[0x297 + 0 * 0x18], 0);
    T_ASSERT_EQ_I(evq.ring[1].code, 0xe6 + 0);   /* flag now clear: +0 */
    return 0;
}

int test_army_select_assign_task_not_owned_posts_no_event(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    AREC(1)[0x05] = 2;                /* owned by player 2 */
    engine_army_select_assign_task(1, 0, /*local_player=*/1, 0, 0, &evq);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* ── engine_army_select_open_handoff ───────────────────────────────── */

int test_army_select_open_handoff_not_ready_resets_screen(void)
{
    army_select_reset();
    int screen = -1, redraw = -1, cooldown = -1, col = -1, row = -1;
    engine_army_select_open_handoff(3, /*ready=*/0, 1, 8, 0x40,
                                    &screen, &redraw, &cooldown, &col, &row);
    T_ASSERT_EQ_I(screen, 0);
    T_ASSERT_EQ_I(redraw, 2);
    return 0;
}

int test_army_select_open_handoff_ready_arms_screen_and_cooldown(void)
{
    army_select_reset();
    TREC(3)[0x0a] = 5;   /* col */
    TREC(3)[0x0b] = 7;   /* row */
    AI32(TREC(3), 0x0c) = 0x50;   /* cell byte offset */
    TREC(3)[0x14e] = 0xAA;

    int screen = -1, redraw = -1, cooldown = -1, col = -1, row = -1;
    engine_army_select_open_handoff(3, /*ready=*/1, /*local_player=*/1,
                                    /*tile_stride=*/8, /*map_width=*/0x40,
                                    &screen, &redraw, &cooldown, &col, &row);
    T_ASSERT_EQ_I(screen, 0x10);
    T_ASSERT_EQ_I(cooldown, 0x28);
    T_ASSERT_EQ_I(TREC(3)[0x14e], 0);
    T_ASSERT_EQ_I(col, (0x50 / 8) % 0x40);
    T_ASSERT_EQ_I(row, (0x50 / 8) / 0x40);
    /* A path field was flooded from (5,7): the source cell itself costs 1. */
    T_ASSERT_EQ_I(engine_path_field_cost(1, 7 * ENGINE_PATH_FIELD_DIM + 5), 1);
    return 0;
}

/* ── engine_army_select_confirm_or_open ────────────────────────────── */

int test_army_select_confirm_or_open_no_link_opens_directly(void)
{
    army_select_reset();
    TREC(4)[0x199] = 0;
    TREC(4)[0x0a] = 1;
    TREC(4)[0x0b] = 1;
    int screen, redraw, cooldown, col, row;
    int fired = engine_army_select_confirm_or_open(4, 1, 8, 0x40,
                                                   &screen, &redraw, &cooldown,
                                                   &col, &row, NULL);
    T_ASSERT_EQ_I(fired, 1);
    T_ASSERT_EQ_I(screen, 0x10);
    return 0;
}

int test_army_select_confirm_or_open_has_link_defers(void)
{
    army_select_reset();
    TREC(4)[0x199] = 9;             /* a pending link */
    int screen = -1, redraw, cooldown, col, row;
    int fired = engine_army_select_confirm_or_open(4, 1, 8, 0x40,
                                                   &screen, &redraw, &cooldown,
                                                   &col, &row, NULL);
    T_ASSERT_EQ_I(fired, 0);
    T_ASSERT_EQ_I(screen, 0);
    return 0;
}

/* ── engine_army_select_dispatch ────────────────────────────────────── */

static engine_army_select_in_t dispatch_in_base(void)
{
    engine_army_select_in_t in;
    memset(&in, 0, sizeof in);
    in.game_mode     = 0;
    in.turn_progress = 0;
    in.turn_busy     = 0;
    in.local_player  = 1;
    in.tile_stride   = 8;
    in.map_width     = 0x40;
    return in;
}

int test_army_select_dispatch_wrong_game_mode_is_noop(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.game_mode = 1;
    engine_army_select_state_t st = {0};
    st.screen = 7;
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 7);     /* untouched */
    return 0;
}

int test_army_select_dispatch_turn_not_idle_is_noop(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.turn_busy = 1;
    in.timeout_reached = 0;
    engine_army_select_state_t st = {0};
    st.screen = 7;
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 7);
    return 0;
}

int test_army_select_dispatch_view_mode2_is_noop(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.view_mode = 2;
    engine_army_select_state_t st = {0};
    st.screen = 7;
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 7);
    return 0;
}

int test_army_select_dispatch_hold_pending_consumed(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    engine_army_select_state_t st = {0};
    st.screen = 7;
    st.hold_pending = 1;
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 7);          /* main body never runs */
    T_ASSERT_EQ_I(st.hold_pending, 0);    /* cookie consumed */
    return 0;
}

static int g_dismiss_calls;
static void test_dismiss_hook(void) { g_dismiss_calls++; }

int test_army_select_dispatch_extended_pause_dismisses_unlisted_popup(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.extended_pause = 1;
    in.popup_type = 5;                    /* not in the {0x11,0xa,0xb,0xc} keep-list */
    engine_army_select_state_t st = {0};
    g_dismiss_calls = 0;
    engine_army_select_hooks_t hooks = {0};
    hooks.dismiss_on_paused_click = test_dismiss_hook;
    engine_army_select_dispatch(&in, &st, &hooks, &evq);
    T_ASSERT_EQ_I(g_dismiss_calls, 1);
    return 0;
}

int test_army_select_dispatch_extended_pause_keeps_listed_popup(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.extended_pause = 1;
    in.popup_type = 0x11;                 /* in the keep-list */
    engine_army_select_state_t st = {0};
    g_dismiss_calls = 0;
    engine_army_select_hooks_t hooks = {0};
    hooks.dismiss_on_paused_click = test_dismiss_hook;
    engine_army_select_dispatch(&in, &st, &hooks, &evq);
    T_ASSERT_EQ_I(g_dismiss_calls, 0);
    return 0;
}

/* Empty cell (no county/army), flags bit 0x20 set, bits 0x80/0x40 clear,
 * the hovered army belongs to local_player → opens terrain-info (screen 4). */
int test_army_select_dispatch_empty_cell_own_army_bit20_opens_terrain_info(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 0;
    in.head_link = 2;
    in.flags = 0x20;
    AREC(2)[0x05] = 1;     /* owned by local_player */
    engine_army_select_state_t st = {0};
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 4);
    return 0;
}

/* Empty cell, flags bit 0x40 set, own army with a pending order → recenters,
 * arms screen 2, fires the workforce_render hook. */
static int g_wf_render_calls;
static int g_wf_render_last_army;
static void test_workforce_render_hook(int army_key)
{
    g_wf_render_calls++;
    g_wf_render_last_army = army_key;
}

int test_army_select_dispatch_empty_cell_own_army_bit40_arms_workforce_screen(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 0;
    in.head_link = 2;
    in.flags = 0x40;
    AREC(2)[0x05] = 1;
    AI32(AREC(2), 0x70) = 1234;     /* order_ptr (army_active_plane) — nonzero */
    engine_army_select_state_t st = {0};
    g_wf_render_calls = 0;
    g_wf_render_last_army = -1;
    engine_army_select_hooks_t hooks = {0};
    hooks.workforce_render = test_workforce_render_hook;
    engine_army_select_dispatch(&in, &st, &hooks, &evq);
    T_ASSERT_EQ_I(st.screen, 2);
    T_ASSERT_EQ_I((int)s_slot_0057c93c, 2);
    T_ASSERT_EQ_I(g_wf_render_calls, 1);
    T_ASSERT_EQ_I(g_wf_render_last_army, 2);
    return 0;
}

/* Re-click an already-armed army (the bit-0x80 branch): the terrain type
 * under the click selects a workforce task. */
int test_army_select_dispatch_reclick_picks_task_by_terrain_type(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 0;
    in.head_link = 2;
    in.flags = 0x80;
    in.terrain_type = 2;             /* < 4 → task 1 → slot 4 */
    AREC(2)[0x05] = 1;
    s_slot_0057c93c = 2;             /* already selected — skip the recenter branch */
    engine_army_select_state_t st = {0};
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(AREC(2)[0x297 + 1 * 0x18], 1);   /* task 1's flag toggled on */
    return 0;
}

/* terrain_type in [0xd, 0x15) is the "no task" dead zone — below that
 * range picks a resource task (0..3), at/above 0x15 picks garrison (4). */
int test_army_select_dispatch_reclick_high_terrain_is_noop(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 0;
    in.head_link = 2;
    in.flags = 0x80;
    in.terrain_type = 0x10;
    AREC(2)[0x05] = 1;
    s_slot_0057c93c = 2;
    engine_army_select_state_t st = {0};
    st.screen = 9;
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 9);     /* untouched: bare return */
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

int test_army_select_dispatch_click_other_owner_army_posts_notice(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 0;
    in.head_link = 2;
    in.flags = 0x80;
    AREC(2)[0x05] = 2;                /* owned by someone else */
    engine_army_select_state_t st = {0};
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x70);
    return 0;
}

int test_army_select_dispatch_own_county_no_link_opens_handoff(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 4;
    TREC(4)[0x08] = 1;       /* kind == county */
    TREC(4)[0x00] = 1;       /* owned by local_player */
    TREC(4)[0x199] = 0;      /* no link */
    engine_army_select_state_t st = {0};
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 0x10);
    return 0;
}

int test_army_select_dispatch_own_county_with_link_screen_0x1d(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 4;
    TREC(4)[0x08] = 1;
    TREC(4)[0x00] = 1;
    TREC(4)[0x199] = 9;
    AI32(AREC(9), 0x1bc) = 7;
    TREC(7)[0x198] = 9;       /* valid mutual link — survives the sanity check */
    engine_army_select_state_t st = {0};
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(st.screen, 0x1d);
    return 0;
}

int test_army_select_dispatch_other_owner_county_posts_notice(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 4;
    TREC(4)[0x08] = 1;
    TREC(4)[0x00] = 2;       /* owned by someone else */
    engine_army_select_state_t st = {0};
    engine_army_select_dispatch(&in, &st, NULL, &evq);
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x70);
    return 0;
}

int test_army_select_dispatch_null_hooks_safe(void)
{
    army_select_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_select_in_t in = dispatch_in_base();
    in.county_id = 0;
    in.head_link = 2;
    in.flags = 0x40;
    AREC(2)[0x05] = 1;
    engine_army_select_state_t st = {0};
    engine_army_select_dispatch(&in, &st, NULL, &evq);   /* must not crash */
    T_ASSERT_EQ_I(st.screen, 2);
    return 0;
}
