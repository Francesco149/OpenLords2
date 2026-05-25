/*
 * Delayed-action dispatcher — pure-C port of retail FUN_004472ce.
 * See engine_delayed_action.h for the API doc and the retail address
 * map.  This file holds the implementation + the preserved-quirk
 * callouts.
 *
 * The function shape is straightforward in concept (drain a pending-
 * action table and call each due record's handler) but retail's
 * implementation has two interesting wrinkles:
 *
 *   1. MP-relay reorder.  In tactical combat (mode 2) with 2+ due
 *      records, records of dispatch type 0x21..0x23 that originated
 *      from a non-local player are pulled out of the dispatch order
 *      and re-appended to the back.  Locally-originated copies of
 *      these types — and all other types — execute in record-index
 *      order.  This biases toward "local actions commit first, peer
 *      relays second" which presumably keeps the per-player tick
 *      counter monotonic vs. the peer-relay broadcast.
 *
 *   2. 10-pass shift compaction.  After moving entries to the high-
 *      priority stash, the holes (QUEUE_EMPTY sentinels) are
 *      compacted leftward by a hard-coded 10 passes of a single-slot
 *      shift loop.  This is bug-adjacent — consecutive QUEUE_EMPTY
 *      entries need O(n_consecutive) passes to fully compact, and 10
 *      passes hard-caps that.  In practice hipri_count is small (a
 *      handful of relayed types per tick at most), so 10 passes
 *      suffice.  See engine-quirks.md.
 *
 * Both quirks are preserved verbatim — they're load-bearing for the
 * resulting dispatch order, which is observable to MP peers via the
 * desync detector (cross-references in docs/findings/main-loop-tick.md).
 */

#include "engine_delayed_action.h"

#include <stddef.h>

/* Reorder filter — retail predicate `0x20 < type && type < 0x24
 * && record.arg0 != local_player`.  Hoisted to a tiny helper purely
 * for readability; inlined by any sane compiler. */
static int is_mp_relay_candidate(const engine_delayed_action_record_t *r,
                                 int local_player)
{
    return (r->type > 0x20) && (r->type < 0x24) && (r->arg0 != local_player);
}

/* Mirror dispatch args into the 10 dispatch-arg fields and remember
 * the record index.  Retail copies these in a specific (non-record-
 * order) sequence to globals at scattered addresses; we keep the
 * source-field order in our struct member names but the assignment
 * order matches retail. */
static void copy_dispatch_args(engine_delayed_action_state_t *state,
                               int rec_idx)
{
    const engine_delayed_action_record_t *r = &state->records[rec_idx];
    state->disp_a0 = r->arg0;
    state->disp_a1 = r->arg1;
    state->disp_a2 = r->arg2;
    state->disp_a3 = r->arg3;
    state->disp_a4 = r->arg4;
    state->disp_a5 = r->arg5;
    state->disp_a6 = r->arg6;
    state->disp_a7 = r->arg7;
    state->disp_a8 = r->arg8;
    state->disp_a9 = r->arg9;
    state->current_idx = rec_idx;
}

void engine_dispatch_delayed_actions(engine_delayed_action_state_t *state,
                                     int current_tick)
{
    /* Step 0 — master switch.  Retail bails before touching any state
     * if DAT_00553030 is 0.  This means queue / hipri_stash / counters
     * carry through unchanged from whatever the prior caller left them
     * at; the dispatcher is a no-op in this case. */
    if (state->master_switch == 0) {
        return;
    }

    /* Step 1 — clear the compaction queue + counters.  Retail spelling
     * uses `0xffff` directly; we use the named constant.  Note that
     * queue is int32, so the slot value is 0x0000ffff, not -1. */
    for (int i = 0; i < ENGINE_DELAYED_ACTION_QUEUE_LEN; i++) {
        state->queue[i] = ENGINE_DELAYED_ACTION_QUEUE_EMPTY;
    }
    state->hipri_count = 0;
    state->queue_count = 0;

    /* Step 2 — sweep the 400-record table.  Append the index of any
     * active record whose due_tick has arrived.  Order = ascending
     * record index. */
    for (int i = 0; i < ENGINE_DELAYED_ACTION_RECORD_COUNT; i++) {
        const engine_delayed_action_record_t *r = &state->records[i];
        if (r->active == 1 && r->due_tick <= current_tick) {
            state->queue[state->queue_count] = i;
            state->queue_count++;
        }
    }

    /* Step 3 — early-exit if nothing to dispatch.  Note that retail
     * also returns here without touching the dispatch-arg mirrors or
     * current_idx — they carry through from a prior call (or remain
     * zero-init if this is the first call). */
    if (state->queue_count <= 0) {
        return;
    }

    /* Step 4 — MP-relay reorder.  Only fires when there are 2+ due
     * records AND we're in tactical combat (mode 2). */
    if (state->queue_count > 1 && state->game_mode == 2) {
        /* 4a — move qualifying entries to the high-priority stash and
         * replace the queue slot with QUEUE_EMPTY. */
        for (int c = 0; c < state->queue_count; c++) {
            int rec_idx = state->queue[c];
            if (is_mp_relay_candidate(&state->records[rec_idx],
                                       state->local_player)) {
                state->hipri_stash[state->hipri_count] = rec_idx;
                state->hipri_count++;
                state->queue[c] = ENGINE_DELAYED_ACTION_QUEUE_EMPTY;
            }
        }

        /* 4b — 10-pass shift compaction.  Each pass walks the queue;
         * for every QUEUE_EMPTY slot found, shift everything from
         * that slot rightward leftward by one.  One contiguous
         * QUEUE_EMPTY run collapses by one slot per pass — 10 passes
         * cap how many consecutive holes can collapse.  Quirk
         * preserved (engine-quirks.md).
         *
         * The inner shift reads queue[count] on its last iteration,
         * which is past queue_count.  As long as queue_count < 100,
         * that read lands within our queue array (and sees
         * QUEUE_EMPTY from the step-1 clear), so the post-shift queue
         * is well-formed.  Retail's read would land in
         * DAT_00515b20 if queue_count == 100; the diff harness
         * constrains test vectors so this never fires. */
        for (int pass = 0; pass < 10; pass++) {
            for (int c = 0; c < state->queue_count; c++) {
                if (state->queue[c] == ENGINE_DELAYED_ACTION_QUEUE_EMPTY) {
                    for (int k = c; k < state->queue_count; k++) {
                        state->queue[k] = state->queue[k + 1];
                    }
                }
            }
        }

        /* 4c — find the first QUEUE_EMPTY slot (or stop at 100) and
         * append the hipri stash there.  Retail's loop is:
         *
         *   for (c = 0; c < 100 && queue[c] < 0xffff; c++) {}
         *
         * Note the < (not !=) comparison: QUEUE_EMPTY (0xffff) is the
         * maximum sentinel, so the loop terminates at the first slot
         * that holds it OR at index 100.
         *
         * Then append hipri_stash entries starting at queue[c]. */
        int append_at = 0;
        while (append_at < ENGINE_DELAYED_ACTION_QUEUE_LEN
               && state->queue[append_at] < ENGINE_DELAYED_ACTION_QUEUE_EMPTY) {
            append_at++;
        }
        for (int k = 0; k < state->hipri_count; k++) {
            /* Retail's write index is `k + append_at`; the assignment
             * has no bounds guard.  In well-formed inputs (queue_count
             * + hipri_count ≤ 100), this stays in-bounds. */
            state->queue[k + append_at] = state->hipri_stash[k];
        }
    }

    /* Step 5 — dispatch loop.  Iterate the queue until QUEUE_EMPTY or
     * 100.  For each surviving entry: copy dispatch args, set current_
     * idx, invoke the handler, clear active, decrement the counter. */
    for (int c = 0; c < ENGINE_DELAYED_ACTION_QUEUE_LEN; c++) {
        int rec_idx = state->queue[c];
        if (rec_idx >= ENGINE_DELAYED_ACTION_QUEUE_EMPTY) {
            break;
        }

        copy_dispatch_args(state, rec_idx);

        /* Handler call.  Retail spelling: (*table[type])().  The
         * handler is permitted to mutate any global it likes (this is
         * how MP broadcasts, combat-action commits, etc. happen) —
         * we surface the call point through a function-pointer table
         * the caller supplies.  NULL = no-op (skip the call); known
         * retail callers always stamp types in [0, 0x32). */
        uint8_t type = state->records[rec_idx].type;
        engine_delayed_action_handler_t fn = state->dispatch[type];
        if (fn != NULL) {
            fn(state->dispatch_user);
        }

        state->records[rec_idx].active = 0;
        state->action_counter--;
    }
}
