/*
 * Unit tests for engine_delayed_action.c — covers each branch of the
 * dispatch state machine and the two preserved quirks (MP-relay
 * reorder, 10-pass shift compaction).  No vendor data needed.
 */

#include "t.h"
#include "engine_delayed_action.h"

#include <string.h>

/* ─── tiny test helpers ─────────────────────────────────────────── */

/* Zeroed state ready for one-call tests.  master_switch on, mode 0,
 * local_player 1.  All records inactive. */
static void init_state(engine_delayed_action_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->master_switch = 1;
    s->game_mode = 0;
    s->local_player = 1;
}

/* Stamp record `i` active with the given (type, due_tick, arg0).
 * Other args left at zero.  Handles padding via memset-in-init. */
static void stamp(engine_delayed_action_state_t *s, int i,
                  uint8_t type, int32_t due, int32_t arg0)
{
    engine_delayed_action_record_t *r = &s->records[i];
    r->active = 1;
    r->type = type;
    r->due_tick = due;
    r->arg0 = arg0;
}

/* Handler that records how many times it was called via user ptr.
 * The user pointer is an int* counter. */
static void count_handler(void *user)
{
    int *count = (int *)user;
    (*count)++;
}

/* ─── tests ─────────────────────────────────────────────────────── */

int test_engine_delayed_action_master_switch_off_is_noop(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.master_switch = 0;
    s.queue_count = 42;  /* sentinel — should carry through unchanged */
    s.queue[0] = 7;
    stamp(&s, 0, 0, 0, 0);

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.queue_count, 42);
    T_ASSERT_EQ_I(s.queue[0], 7);
    /* Records untouched too. */
    T_ASSERT_EQ_I(s.records[0].active, 1);
    return 0;
}

int test_engine_delayed_action_no_due_records_clears_queue(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.queue_count = 99;
    s.hipri_count = 99;
    /* All records inactive — sweep finds nothing. */

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.queue_count, 0);
    T_ASSERT_EQ_I(s.hipri_count, 0);
    for (int i = 0; i < ENGINE_DELAYED_ACTION_QUEUE_LEN; i++) {
        T_ASSERT_EQ_I(s.queue[i], ENGINE_DELAYED_ACTION_QUEUE_EMPTY);
    }
    return 0;
}

int test_engine_delayed_action_inactive_records_excluded(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    /* Record at 5 has due_tick fine but active=0. */
    s.records[5].type = 0;
    s.records[5].due_tick = 10;
    s.records[5].active = 0;

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.queue_count, 0);
    return 0;
}

int test_engine_delayed_action_future_records_excluded(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    stamp(&s, 5, 0, 200, 0);  /* due_tick > current_tick (100) */

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.queue_count, 0);
    T_ASSERT_EQ_I(s.records[5].active, 1);  /* untouched */
    return 0;
}

int test_engine_delayed_action_due_at_boundary_includes(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    stamp(&s, 5, 0, 100, 0);  /* due_tick == current_tick — inclusive */
    s.action_counter = 1;

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.records[5].active, 0);  /* dispatched */
    T_ASSERT_EQ_I(s.action_counter, 0);
    return 0;
}

int test_engine_delayed_action_in_order_dispatch_clears_active(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    stamp(&s, 10, 0, 0, 0);
    stamp(&s, 50, 1, 0, 0);
    stamp(&s, 200, 0, 0, 0);
    s.action_counter = 5;

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.records[10].active, 0);
    T_ASSERT_EQ_I(s.records[50].active, 0);
    T_ASSERT_EQ_I(s.records[200].active, 0);
    T_ASSERT_EQ_I(s.action_counter, 2);  /* 5 - 3 dispatches */
    T_ASSERT_EQ_I(s.current_idx, 200);   /* last dispatched index */
    return 0;
}

int test_engine_delayed_action_dispatch_args_mirror_last_record(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    /* Two records — second one wins on the dispatch-arg mirrors. */
    s.records[3].active = 1;
    s.records[3].type = 0;
    s.records[3].due_tick = 0;
    s.records[3].arg0 = 100; s.records[3].arg9 = 109;

    s.records[7].active = 1;
    s.records[7].type = 0;
    s.records[7].due_tick = 0;
    s.records[7].arg0 = 200;
    s.records[7].arg1 = 201; s.records[7].arg2 = 202; s.records[7].arg3 = 203;
    s.records[7].arg4 = 204; s.records[7].arg5 = 205; s.records[7].arg6 = 206;
    s.records[7].arg7 = 207; s.records[7].arg8 = 208; s.records[7].arg9 = 209;

    engine_dispatch_delayed_actions(&s, 100);

    /* Last record dispatched (index 7) sets all mirrors. */
    T_ASSERT_EQ_I(s.disp_a0, 200);
    T_ASSERT_EQ_I(s.disp_a1, 201);
    T_ASSERT_EQ_I(s.disp_a2, 202);
    T_ASSERT_EQ_I(s.disp_a3, 203);
    T_ASSERT_EQ_I(s.disp_a4, 204);
    T_ASSERT_EQ_I(s.disp_a5, 205);
    T_ASSERT_EQ_I(s.disp_a6, 206);
    T_ASSERT_EQ_I(s.disp_a7, 207);
    T_ASSERT_EQ_I(s.disp_a8, 208);
    T_ASSERT_EQ_I(s.disp_a9, 209);
    T_ASSERT_EQ_I(s.current_idx, 7);
    return 0;
}

int test_engine_delayed_action_handler_called_per_dispatch(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    int n_calls = 0;
    s.dispatch[5] = count_handler;
    s.dispatch[7] = count_handler;
    s.dispatch_user = &n_calls;
    stamp(&s, 1, 5, 0, 0);
    stamp(&s, 2, 7, 0, 0);
    stamp(&s, 3, 5, 0, 0);

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(n_calls, 3);
    return 0;
}

int test_engine_delayed_action_null_handler_treated_as_noop(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    /* dispatch[10] is NULL — record should still clear active. */
    stamp(&s, 0, 10, 0, 0);

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.records[0].active, 0);
    return 0;
}

int test_engine_delayed_action_mode2_with_one_record_skips_reorder(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 2;
    /* Single record of relay type 0x22 from non-local player —
     * would qualify for reorder, but queue_count==1 short-circuits. */
    stamp(&s, 10, 0x22, 0, 2);

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.hipri_count, 0);
    T_ASSERT_EQ_I(s.records[10].active, 0);
    return 0;
}

int test_engine_delayed_action_mode_other_skips_reorder(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 0;  /* not 2 — reorder skipped */
    stamp(&s, 5,  0x22, 0, 2);  /* relay type from non-local */
    stamp(&s, 10, 0x00, 0, 0);

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.hipri_count, 0);
    /* In-order dispatch: 5 then 10.  Last dispatched index = 10. */
    T_ASSERT_EQ_I(s.current_idx, 10);
    return 0;
}

int test_engine_delayed_action_mp_reorder_pulls_relay_types_back(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 2;
    s.local_player = 1;
    /* Queue [5, 10, 20]: 5 is non-relay, 10 is relay from non-local
     * (qualifies), 20 is non-relay.  Reorder should move 10 to end. */
    stamp(&s, 5,  0x10, 0, 99);  /* type < 0x21 — not a relay candidate */
    stamp(&s, 10, 0x22, 0, 2);   /* relay from non-local — moves */
    stamp(&s, 20, 0x00, 0, 99);  /* non-relay — stays in place */

    engine_dispatch_delayed_actions(&s, 100);

    /* Last dispatched should be record 10 (the relay). */
    T_ASSERT_EQ_I(s.current_idx, 10);
    return 0;
}

int test_engine_delayed_action_mp_reorder_local_player_relay_stays(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 2;
    s.local_player = 1;
    /* Two relay records: one from local (stays), one from non-local (moves). */
    stamp(&s, 5,  0x22, 0, 1);   /* relay arg0=local — does NOT qualify */
    stamp(&s, 10, 0x22, 0, 2);   /* relay arg0=peer — qualifies */
    stamp(&s, 20, 0x00, 0, 0);   /* non-relay */

    engine_dispatch_delayed_actions(&s, 100);

    /* After reorder: [5, 20, 10].  Last dispatched = 10. */
    T_ASSERT_EQ_I(s.current_idx, 10);
    return 0;
}

int test_engine_delayed_action_mp_reorder_type_boundary_exclusive(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 2;
    s.local_player = 1;
    /* Types 0x20 and 0x24 are JUST outside the relay range.
     * Types 0x21..0x23 are inside.  Verify boundary handling. */
    stamp(&s, 5,  0x20, 0, 2);  /* NOT a relay (type == 0x20) */
    stamp(&s, 10, 0x24, 0, 2);  /* NOT a relay (type == 0x24) */
    stamp(&s, 20, 0x21, 0, 2);  /* relay */
    stamp(&s, 30, 0x23, 0, 2);  /* relay */

    engine_dispatch_delayed_actions(&s, 100);

    /* Records 20 and 30 are pulled to the end; 5 and 10 stay.
     * Dispatch order: [5, 10, 20, 30].  Last = 30. */
    T_ASSERT_EQ_I(s.current_idx, 30);
    return 0;
}

int test_engine_delayed_action_mp_reorder_no_relays_is_in_order(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 2;
    s.local_player = 1;
    /* All types non-relay — reorder branch runs but no entries move. */
    stamp(&s, 7,  0x00, 0, 99);
    stamp(&s, 14, 0x10, 0, 99);
    stamp(&s, 21, 0x30, 0, 99);

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.hipri_count, 0);
    T_ASSERT_EQ_I(s.current_idx, 21);
    return 0;
}

int test_engine_delayed_action_mp_reorder_compaction_two_consecutive_holes(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 2;
    s.local_player = 1;
    /* Two consecutive relay records at the START of the queue —
     * exercises the two-hole shift case (needs 2 of the 10 passes). */
    stamp(&s, 5,  0x22, 0, 2);   /* relay — moves */
    stamp(&s, 10, 0x23, 0, 2);   /* relay — moves */
    stamp(&s, 20, 0x10, 0, 0);   /* non-relay — stays */
    stamp(&s, 30, 0x11, 0, 0);   /* non-relay — stays */

    engine_dispatch_delayed_actions(&s, 100);

    /* After reorder: queue should be [20, 30, 5, 10].  Last
     * dispatched = 10. */
    T_ASSERT_EQ_I(s.current_idx, 10);
    return 0;
}

int test_engine_delayed_action_mp_reorder_hipri_preserves_record_order(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 2;
    s.local_player = 1;
    /* Record order matters: 5 sweeps before 10 sweeps before 20.
     * All three are relay candidates → hipri_stash = [5, 10, 20].
     * After compaction: queue = [].  Append: queue = [5, 10, 20]. */
    stamp(&s, 5,  0x21, 0, 2);
    stamp(&s, 10, 0x22, 0, 2);
    stamp(&s, 20, 0x23, 0, 2);

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.hipri_count, 3);
    /* Last dispatched = 20 (the tail of the appended hipri). */
    T_ASSERT_EQ_I(s.current_idx, 20);
    return 0;
}

int test_engine_delayed_action_dispatch_stops_at_first_empty_sentinel(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    s.game_mode = 0;
    /* Pre-seed queue with a sentinel followed by a "stale" entry —
     * the dispatch loop must NOT consume past the sentinel.  Since
     * the sweep clears the queue at start though, we instead use a
     * record that gets included then verify nothing beyond it runs. */
    stamp(&s, 5, 0, 0, 0);
    /* records[10] is inactive — won't enter queue */
    /* After sweep: queue = [5, EMPTY, EMPTY, ...].  Dispatch loop
     * stops at index 1.  current_idx should be 5. */

    engine_dispatch_delayed_actions(&s, 100);

    T_ASSERT_EQ_I(s.current_idx, 5);
    return 0;
}

int test_engine_delayed_action_record_padding_is_inert(void)
{
    engine_delayed_action_state_t s;
    init_state(&s);
    /* Touch the padding region directly — the dispatcher should never
     * read or write +0x2c..+0x35 of any record, so the bytes carry
     * through. */
    memset(s.records[7].pad, 0xab, sizeof s.records[7].pad);
    stamp(&s, 7, 0, 0, 0);

    engine_dispatch_delayed_actions(&s, 100);

    for (int i = 0; i < (int)sizeof s.records[7].pad; i++) {
        T_ASSERT_EQ_I(s.records[7].pad[i], 0xab);
    }
    return 0;
}

int test_engine_delayed_action_record_size_layout(void)
{
    /* Cross-check the static struct layout matches retail's 0x38
     * stride.  Compile-time, but the runtime check catches a packed-
     * struct attribute being dropped accidentally. */
    T_ASSERT_EQ_I(sizeof(engine_delayed_action_record_t), 0x38);
    /* Field offsets — explicit so the diff harness's ctypes mirror
     * doesn't drift if a member ever gets resized. */
    engine_delayed_action_record_t r;
    T_ASSERT_EQ_I((char *)&r.due_tick - (char *)&r, 0x00);
    T_ASSERT_EQ_I((char *)&r.arg0     - (char *)&r, 0x04);
    T_ASSERT_EQ_I((char *)&r.arg9     - (char *)&r, 0x28);
    T_ASSERT_EQ_I((char *)&r.pad      - (char *)&r, 0x2c);
    T_ASSERT_EQ_I((char *)&r.active   - (char *)&r, 0x36);
    T_ASSERT_EQ_I((char *)&r.type     - (char *)&r, 0x37);
    return 0;
}
