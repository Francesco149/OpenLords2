/*
 * Tests for engine_turn_end_check — the end-of-turn readiness
 * predicate (pure core of FUN_0049b762).
 */

#include "t.h"

#include "engine_turn.h"
#include "save_schema_globals.h"
#include "io_path.h"
#include "engine_menu_input.h"
#include "engine_event_queue.h"
#include "engine_army_economy.h"
#include "engine_territory.h"
#include "save.h"                 /* g_per_army_768 / _territory_420 / strategic */
#include "config.h"

#include <string.h>
#if defined(_WIN32)
#  include <direct.h>
#else
#  include <unistd.h>
#endif

#define STRIDE 0x160

/* Build a 6-slot (0..5) player record array.  Each slot: int ready-value
 * at +0, active byte at +4.  `ready[i]` / `active[i]` for i in 0..5. */
static void build_players(uint8_t buf[6 * STRIDE],
                          const int ready[6], const uint8_t active[6])
{
    memset(buf, 0, 6 * STRIDE);
    for (int i = 0; i < 6; i++) {
        memcpy(&buf[i * STRIDE], &ready[i], sizeof(int));
        buf[i * STRIDE + 4] = active[i];
    }
}

/* SP, all active players done (ready == 999) → advance. */
int test_turn_end_sp_all_done_advances(void)
{
    uint8_t buf[6 * STRIDE];
    int     ready[6]  = {0, 999, 999, 999, 999, 999};
    uint8_t active[6] = {0, 1, 1, 1, 1, 1};
    build_players(buf, ready, active);

    engine_turn_end_in_t in = {
        .now_ms = 50000, .mp_active = 0, .active_count = 0,
        .turn_progress = 0, .players = buf,
    };
    engine_turn_end_out_t out = {0};
    engine_turn_end_check(&in, &out);

    T_ASSERT_EQ_I(out.not_ready_count, 0);
    T_ASSERT_EQ_I(out.advance, 1);
    /* SP path doesn't touch the handshake state. */
    T_ASSERT_EQ_I(out.new_turn_progress, 0);
    T_ASSERT_EQ_I(out.clear_ready_flags, 0);
    return 0;
}

/* One active player still below 999 → not_ready, no advance. */
int test_turn_end_someone_mid_turn_waits(void)
{
    uint8_t buf[6 * STRIDE];
    int     ready[6]  = {0, 999, 12, 999, 999, 999};   /* slot 2 mid-turn */
    uint8_t active[6] = {0, 1, 1, 1, 1, 1};
    build_players(buf, ready, active);

    engine_turn_end_in_t in = {
        .now_ms = 1, .mp_active = 0, .players = buf,
    };
    engine_turn_end_out_t out = {0};
    engine_turn_end_check(&in, &out);

    T_ASSERT_EQ_I(out.not_ready_count, 1);
    T_ASSERT_EQ_I(out.advance, 0);
    return 0;
}

/* Count only slots that are BOTH active AND below 999.  An inactive
 * slot below 999, and an active slot at exactly 999, are both ignored. */
int test_turn_end_counts_only_active_below_999(void)
{
    uint8_t buf[6 * STRIDE];
    int     ready[6]  = {0, 5, 999, 998, 0, 7};
    uint8_t active[6] = {0, 0, 1, 1, 0, 1};
    /* slot1: below 999 but INACTIVE  → no
     * slot2: active but == 999        → no
     * slot3: active & 998 < 999       → yes
     * slot4: inactive                 → no
     * slot5: active & 7 < 999         → yes  → count 2 */
    build_players(buf, ready, active);

    engine_turn_end_in_t in = { .mp_active = 0, .players = buf };
    engine_turn_end_out_t out = {0};
    engine_turn_end_check(&in, &out);

    T_ASSERT_EQ_I(out.not_ready_count, 2);
    T_ASSERT_EQ_I(out.advance, 0);
    return 0;
}

/* The scan stops at slot 5 — slot 0 and a hypothetical slot 6 are not
 * counted (retail loop is 1..5).  Slot 0 set mid-turn must be ignored. */
int test_turn_end_scans_slots_1_to_5_only(void)
{
    uint8_t buf[6 * STRIDE];
    int     ready[6]  = {3, 999, 999, 999, 999, 999};  /* slot 0 mid-turn */
    uint8_t active[6] = {1, 1, 1, 1, 1, 1};
    build_players(buf, ready, active);

    engine_turn_end_in_t in = { .mp_active = 0, .players = buf };
    engine_turn_end_out_t out = {0};
    engine_turn_end_check(&in, &out);

    /* slot 0 ignored → everyone (1..5) done → advance. */
    T_ASSERT_EQ_I(out.not_ready_count, 0);
    T_ASSERT_EQ_I(out.advance, 1);
    return 0;
}

/* MP, all done, exactly one active player → advance without handshake. */
int test_turn_end_mp_single_active_advances(void)
{
    uint8_t buf[6 * STRIDE];
    int     ready[6]  = {0, 999, 999, 999, 999, 999};
    uint8_t active[6] = {0, 1, 1, 1, 1, 1};
    build_players(buf, ready, active);

    engine_turn_end_in_t in = {
        .mp_active = 1, .active_count = 1, .turn_progress = 0,
        .players = buf,
    };
    engine_turn_end_out_t out = {0};
    engine_turn_end_check(&in, &out);

    T_ASSERT_EQ_I(out.advance, 1);
    T_ASSERT_EQ_I(out.new_turn_progress, 0);   /* untouched */
    T_ASSERT_EQ_I(out.clear_ready_flags, 0);
    return 0;
}

/* MP, multiple active, all done → handshake.  State 0 arms (no advance,
 * snapshots timeout, sets state 1, arms the sync timer 10 s in the past),
 * then state 2 finalises (advance, reset to 0). */
int test_turn_end_mp_handshake_arms_then_finalises(void)
{
    uint8_t buf[6 * STRIDE];
    int     ready[6]  = {0, 999, 999, 999, 999, 999};
    uint8_t active[6] = {0, 1, 1, 1, 1, 1};
    build_players(buf, ready, active);

    /* State 0 → arm. */
    engine_turn_end_in_t in = {
        .now_ms = 100000, .mp_active = 1, .active_count = 3,
        .turn_progress = 0, .timeout_reached = 1,
        .saved_timeout = 0, .sync_baseline_ms = 7,
        .players = buf,
    };
    engine_turn_end_out_t out = {0};
    engine_turn_end_check(&in, &out);

    T_ASSERT_EQ_I(out.advance, 0);
    T_ASSERT_EQ_I(out.new_turn_progress, 1);
    T_ASSERT_EQ_I(out.clear_ready_flags, 1);
    T_ASSERT_EQ_I(out.saved_timeout, 1);          /* snapshot of timeout_reached */
    T_ASSERT_EQ_I(out.new_timeout_reached, 0);    /* cleared */
    T_ASSERT_EQ_U(out.new_sync_baseline_ms, 100000u - 10000u);

    /* State 2 → finalise. */
    in.turn_progress = 2;
    in.timeout_reached = 0;
    engine_turn_end_out_t out2 = {0};
    engine_turn_end_check(&in, &out2);

    T_ASSERT_EQ_I(out2.advance, 1);
    T_ASSERT_EQ_I(out2.new_turn_progress, 0);
    T_ASSERT_EQ_I(out2.clear_ready_flags, 0);     /* arm block didn't run */
    return 0;
}

/* MP handshake state 1: still waiting — no advance, state carried. */
int test_turn_end_mp_state1_waits(void)
{
    uint8_t buf[6 * STRIDE];
    int     ready[6]  = {0, 999, 999, 999, 999, 999};
    uint8_t active[6] = {0, 1, 1, 1, 1, 1};
    build_players(buf, ready, active);

    engine_turn_end_in_t in = {
        .now_ms = 1, .mp_active = 1, .active_count = 4,
        .turn_progress = 1, .timeout_reached = 1,
        .saved_timeout = 9, .sync_baseline_ms = 42,
        .players = buf,
    };
    engine_turn_end_out_t out = {0};
    engine_turn_end_check(&in, &out);

    T_ASSERT_EQ_I(out.advance, 0);
    T_ASSERT_EQ_I(out.new_turn_progress, 1);      /* carried */
    T_ASSERT_EQ_I(out.clear_ready_flags, 0);
    /* Carry-through: untouched state echoes the inputs. */
    T_ASSERT_EQ_I(out.new_timeout_reached, 1);
    T_ASSERT_EQ_I(out.saved_timeout, 9);
    T_ASSERT_EQ_U(out.new_sync_baseline_ms, 42u);
    return 0;
}

/* engine_turn_hook_noop (retail FUN_00428471): empty stub — calling it
 * must have no effect.  Smoke-test that it links and returns. */
int test_turn_hook_noop_is_inert(void)
{
    engine_turn_hook_noop();   /* no observable state to assert */
    return 0;
}

/* engine_turn_advance_calendar (the calendar block of FUN_00448440):
 * a non-rollover turn rotates only the season triple + turn counter;
 * the year triple, the copy slots, and the CD-audio gate are untouched. */
int test_turn_calendar_non_rollover(void)
{
    s_slot_00553240 = 5;          /* turn counter        */
    s_slot_0057c934 = 1;          /* season cur          */
    s_slot_00554470 = 0;          /* season prev         */
    s_slot_0057c92c = 2;          /* season next (!= 4)  */
    s_slot_0057c990 = 1;          /* season copy (seed)  */
    s_slot_0055cd5c = 7;          /* rollover counter    */
    s_slot_00553edc = 1267;       /* year cur            */
    s_slot_0057c900 = 1266;       /* year prev           */
    s_slot_00553e74 = 1268;       /* year next           */
    s_slot_00553228 = 1267;       /* year copy (seed)    */
    engine_menu_input_set_sp_available(9);

    engine_turn_advance_calendar();

    T_ASSERT_EQ_U(s_slot_00553240, 6u);     /* turn ++              */
    T_ASSERT_EQ_U(s_slot_00554470, 1u);     /* prev = old cur       */
    T_ASSERT_EQ_U(s_slot_0057c934, 2u);     /* cur  = old next      */
    T_ASSERT_EQ_U(s_slot_0057c92c, 3u);     /* next ++ (no wrap)    */
    /* Year triple + copies + gate untouched. */
    T_ASSERT_EQ_U(s_slot_0055cd5c, 7u);
    T_ASSERT_EQ_U(s_slot_00553edc, 1267u);
    T_ASSERT_EQ_U(s_slot_0057c900, 1266u);
    T_ASSERT_EQ_U(s_slot_00553e74, 1268u);
    T_ASSERT_EQ_U(s_slot_0057c990, 1u);
    T_ASSERT_EQ_U(s_slot_00553228, 1267u);
    T_ASSERT_EQ_I(engine_menu_input_sp_available(), 9);   /* probe not fired */
    return 0;
}

/* When the just-consumed season-next was 4, the season wraps to 1, the
 * year triple rotates, the rollover counter ticks, and the CD-audio
 * probe fires (forcing the gate to 1). */
int test_turn_calendar_year_rollover(void)
{
    char orig[256];
#if defined(_WIN32)
    if (_getcwd(orig, sizeof orig) == NULL) T_SKIP("getcwd");
#else
    if (getcwd(orig, sizeof orig) == NULL) T_SKIP("getcwd");
#endif
    io_path_set_base(orig);       /* clean revert for the probe tail */
    g_config.cd_drive = 'D';

    s_slot_00553240 = 10;
    s_slot_0057c934 = 3;          /* season cur          */
    s_slot_00554470 = 2;          /* season prev         */
    s_slot_0057c92c = 4;          /* season next == 4 → rollover */
    s_slot_0055cd5c = 7;
    s_slot_00553edc = 1267;       /* year cur            */
    s_slot_0057c900 = 1266;       /* year prev           */
    s_slot_00553e74 = 1268;       /* year next           */
    engine_menu_input_set_sp_available(9);

    engine_turn_advance_calendar();

    T_ASSERT_EQ_U(s_slot_00553240, 11u);    /* turn ++              */
    T_ASSERT_EQ_U(s_slot_00554470, 3u);     /* season prev = old cur */
    T_ASSERT_EQ_U(s_slot_0057c934, 4u);     /* season cur  = old next */
    T_ASSERT_EQ_U(s_slot_0057c92c, 1u);     /* season next wrapped 5→1 */
    T_ASSERT_EQ_U(s_slot_0055cd5c, 8u);     /* rollover counter ++   */
    T_ASSERT_EQ_U(s_slot_00553edc, 1268u);  /* year cur  = old next  */
    T_ASSERT_EQ_U(s_slot_0057c900, 1267u);  /* year prev = old cur   */
    T_ASSERT_EQ_U(s_slot_00553e74, 1269u);  /* year next ++          */
    T_ASSERT_EQ_I(engine_menu_input_sp_available(), 1);  /* probe fired */
    return 0;
}

/* engine_turn_run_passes (the assembled tail of FUN_00448440): drive the
 * full 27-pass sequence end-to-end over an empty world and confirm it
 * links, returns, and lands the calendar correctly amid all the passes
 * (no pass clobbers the calendar scalars).
 *
 * Hermetic empty world: realm count 0 (every per-realm loop empty),
 * every economy/territory seam unbound (the fixed-range planner loops —
 * collect_territory_income's realms-1..16 re-plan, the resource-pool
 * group loop — all early-return on the NULL guard), and the per-territory
 * + per-player save blocks zeroed (the territory-income scan finds no
 * counted territory, so no out-of-range strategic read).  The only
 * observable effect is the calendar advance, which the per-pass economy
 * effects (price-scalar-gated, covered by their own diff targets) never
 * touch.  This is the orchestrator's sequencing + linkage gate; calendar
 * parity through the *retail* full driver is the `turn_driver` diff. */
int test_turn_run_passes_sequences_full_driver(void)
{
    /* Empty, hermetic world. */
    s_slot_0056d5dc = 0;                       /* realm count → all loops empty */
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768,      0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);

    engine_army_economy_bind_army_records(NULL);   /* pass-3 planners → no-op */
    engine_army_economy_bind_cells(NULL);
    engine_army_economy_bind_resource_pool(NULL);  /* group loop → no-op      */
    engine_territory_bind_records(NULL);
    engine_territory_bind_cells(NULL);

    /* Non-rollover calendar pre-state (same shape as the calendar test). */
    s_slot_00553240 = 20;          /* turn counter        */
    s_slot_0057c934 = 1;           /* season cur          */
    s_slot_00554470 = 0;           /* season prev         */
    s_slot_0057c92c = 2;           /* season next (!= 4)  */
    s_slot_0055cd5c = 3;           /* rollover counter    */

    engine_event_queue_state_t evq;
    engine_event_queue_init_wrapper(&evq);

    /* Post-stir RNG bytes are arbitrary here (no realm consumes them in
     * an empty world); pass representative non-zero values, SP. */
    engine_turn_run_passes(&evq, 0x44, 0x12, /*mp_active=*/0);

    /* Calendar advanced exactly as the standalone block does. */
    T_ASSERT_EQ_U(s_slot_00553240, 21u);   /* turn ++          */
    T_ASSERT_EQ_U(s_slot_00554470, 1u);    /* prev = old cur   */
    T_ASSERT_EQ_U(s_slot_0057c934, 2u);    /* cur  = old next  */
    T_ASSERT_EQ_U(s_slot_0057c92c, 3u);    /* next ++          */
    T_ASSERT_EQ_U(s_slot_0055cd5c, 3u);    /* no year rollover */

    /* Restore the unbound seams (other tests bind their own, but leave
     * the shared globals tidy). */
    engine_army_economy_bind_army_records(NULL);
    return 0;
}
