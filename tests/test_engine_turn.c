/*
 * Tests for engine_turn_end_check — the end-of-turn readiness
 * predicate (pure core of FUN_0049b762).
 */

#include "t.h"

#include "engine_turn.h"

#include <string.h>

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
