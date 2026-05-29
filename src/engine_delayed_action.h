/*
 * OpenLords2 — engine delayed-action dispatcher.
 *
 * Port of retail FUN_004472ce @ 0x4472ce (1046 bytes), the per-tick
 * scheduler that drains the 400-slot pending-action table for any
 * record whose due-tick has arrived.  Called once per sim sub-step
 * (placement TBD when the per-tick driver port lands; see
 * docs/findings/main-loop-tick.md for the dispatch chain).
 *
 * Conceptually a delayed message queue:
 *
 *   - enqueue side (retail 0x447136 — `engine_delayed_action_enqueue`):
 *     find a free slot, stamp it with a type byte, due tick, and up to
 *     10 scalar args.
 *
 *   - drain side (this port): every tick, sweep the 400-slot table
 *     for `active == 1 && due_tick <= now`, collect indices into a
 *     compaction queue (100 slots), optionally reorder MP-relayed
 *     types 0x21..0x23 to the back of the queue, then for each
 *     surviving entry copy its 10 scalar args into the 10 dispatch-
 *     argument globals and call the matching handler from a 256-entry
 *     dispatch function-pointer table.  The handler clears its slot
 *     and decrements the action counter.
 *
 * The handlers are tactical-game actions (combat-action commits, MP
 * sim broadcasts, etc.) we have not ported yet.  The port keeps the
 * dispatch surface explicit: callers supply the table and an optional
 * user pointer; entries that are NULL are treated as no-ops.  The
 * diff harness installs a Frida-allocated "ret" stub at every PTR_FUN_
 * 004d5c98 entry on the retail side and an all-NULL table on the port
 * side — both produce the same observable post-state.
 *
 * Pure function — every input/output through the state struct.  No
 * globals read or written.  Same shape as engine_state_hash + engine_
 * tile (see docs/findings/pure-function-diff.md).
 */

#ifndef OPENLORDS2_ENGINE_DELAYED_ACTION_H
#define OPENLORDS2_ENGINE_DELAYED_ACTION_H

#include <stdint.h>

/* Capacities — mirror retail. */
#define ENGINE_DELAYED_ACTION_RECORD_COUNT     400
#define ENGINE_DELAYED_ACTION_QUEUE_LEN        100
#define ENGINE_DELAYED_ACTION_HIPRI_LEN        100
#define ENGINE_DELAYED_ACTION_DISPATCH_TABLE_LEN 256

/* Sentinel used in the compaction queue to mark a slot that has been
 * moved to the high-priority stash and is awaiting back-fill.  Retail
 * spelling: 0xffff (the queue slots are read as int and the sentinel
 * is the int value 0xffff, NOT 0xffffffff). */
#define ENGINE_DELAYED_ACTION_QUEUE_EMPTY      0xffff

/* One pending-action record.  Retail per-slot stride is 0x38 (56 B):
 * one int + 10 ints + 10 bytes of padding + active flag + type byte.
 * The padding is preserved as an explicit member so sizeof() matches
 * retail and host-side ctypes binding gets the same layout. */
typedef struct {
    int32_t  due_tick;   /* +0x00 — fires when current_tick >= due_tick */
    int32_t  arg0;       /* +0x04 — first scalar arg (player slot in MP types) */
    int32_t  arg1;       /* +0x08 */
    int32_t  arg2;       /* +0x0c */
    int32_t  arg3;       /* +0x10 */
    int32_t  arg4;       /* +0x14 */
    int32_t  arg5;       /* +0x18 */
    int32_t  arg6;       /* +0x1c */
    int32_t  arg7;       /* +0x20 */
    int32_t  arg8;       /* +0x24 */
    int32_t  arg9;       /* +0x28 */
    uint8_t  pad[10];    /* +0x2c..+0x35 — never read or written by any
                          * code path; preserved for layout parity */
    uint8_t  active;     /* +0x36 — 1 = pending, 0 = inactive/free */
    uint8_t  type;       /* +0x37 — dispatch table index, [0, 0x32) in retail */
} engine_delayed_action_record_t;

/* Handler shape — retail dispatch is `(*func)()` with zero declared args.
 * Handlers consume the dispatch-arg globals; the port surfaces those
 * fields in the state struct and passes a user pointer so handlers can
 * see the dispatch surface without resorting to file-statics. */
typedef void (*engine_delayed_action_handler_t)(void *user);

/* All state read and written by engine_dispatch_delayed_actions().
 * Sized to match retail's adjacent globals where the layout matters
 * (records + queue + hipri_stash sit in fixed-size retail data regions
 * the diff harness mirrors byte-for-byte). */
typedef struct {
    /* ─── input scalars ─────────────────────────────────────────── */

    /* Master switch.  Retail DAT_00553030 — when 0 the entire dispatch
     * is skipped (no state changes at all).  Gated by game-mode boot;
     * cleared on quit-to-menu. */
    int      master_switch;

    /* Game mode.  Retail DAT_0057c8fc.  Mode 2 (tactical combat)
     * enables the MP-relay reorder branch; any other value falls
     * through to the plain in-order dispatch. */
    int      game_mode;

    /* Local player slot.  Retail DAT_005681ec.  The reorder branch
     * skips types 0x21..0x23 originated from the local player (they
     * already executed locally; only peer-relayed copies need defer). */
    int      local_player;

    /* ─── output scalars (also seeded from caller's prior state) ── */

    /* Pending-action counter.  Retail DAT_00512f6c.  Decremented once
     * per dispatched record.  Retail also increments this on enqueue —
     * we mirror only the dispatcher half here. */
    int      action_counter;

    /* Currently-dispatching record index.  Retail DAT_0051590c.  Set
     * to the record index just before invoking its handler so the
     * handler can identify itself.  Carries through with the LAST
     * dispatched record's index after the loop. */
    int      current_idx;

    /* ─── 10 dispatch-argument mirror globals ────────────────────
     *
     * Retail copies these from the record being dispatched RIGHT
     * BEFORE the handler call.  Handlers then read these globals
     * instead of taking arguments.  After the loop they hold the LAST
     * dispatched record's args — that's the observable "fingerprint"
     * of dispatch order in the diff harness.
     *
     * Note the non-sequential global addresses: retail places these
     * in different cache lines that have nothing to do with the
     * record's field order.  We mirror the source-field-order in the
     * struct name (a0..a9 match record.arg0..arg9), but the retail
     * global addresses each get a comment so the diff harness can
     * match them up. */
    int32_t  disp_a0;   /* DAT_005158e8 ← record.arg0 */
    int32_t  disp_a1;   /* DAT_005158a8 ← record.arg1 */
    int32_t  disp_a2;   /* DAT_005158a4 ← record.arg2 */
    int32_t  disp_a3;   /* DAT_005158b0 ← record.arg3 */
    int32_t  disp_a4;   /* DAT_005158ac ← record.arg4 */
    int32_t  disp_a5;   /* DAT_005158bc ← record.arg5 */
    int32_t  disp_a6;   /* DAT_005158b4 ← record.arg6 */
    int32_t  disp_a7;   /* _DAT_005158c4 ← record.arg7 */
    int32_t  disp_a8;   /* _DAT_005158c0 ← record.arg8 */
    int32_t  disp_a9;   /* _DAT_0050d7bc ← record.arg9 */

    /* ─── working storage ───────────────────────────────────────── */

    /* Compaction queue.  Retail DAT_00515990 (100 × int32).  Cleared
     * to ENGINE_DELAYED_ACTION_QUEUE_EMPTY at function entry, then
     * filled with the indices of records whose due_tick has arrived.
     * The reorder branch swaps in QUEUE_EMPTY sentinels and shifts;
     * the dispatch loop scans until the first sentinel.
     *
     * NOTE: retail's shift loop reads queue[queue_count] on its last
     * iteration, which is the slot AFTER queue_count.  As long as
     * queue_count < 100, that read lands within our queue array (and
     * sees QUEUE_EMPTY from the initial clear).  If queue_count == 100
     * retail would read DAT_00515b20 (an unrelated adjacent global) —
     * the diff harness pins test vectors to queue_count ≤ 99 so this
     * never fires.  See engine-quirks.md. */
    int32_t  queue[ENGINE_DELAYED_ACTION_QUEUE_LEN];
    int      queue_count;  /* DAT_00515910 */

    /* High-priority stash for MP-relay reorder.  Retail DAT_0050d620
     * (100 × int32).  Only used when game_mode == 2 and queue_count
     * > 1.  Records of type 0x21..0x23 originated from a non-local
     * player are pulled out of the main queue and re-appended after
     * the local-originated records execute. */
    int32_t  hipri_stash[ENGINE_DELAYED_ACTION_HIPRI_LEN];
    int      hipri_count;  /* DAT_00512f58 */

    /* The 400-slot pending-action table.  Retail DAT_0050d7c0 (400 ×
     * 0x38 = 22400 B).  Records are stamped active=1 by the enqueue
     * helper; cleared active=0 by this dispatcher after a handler
     * runs. */
    engine_delayed_action_record_t records[ENGINE_DELAYED_ACTION_RECORD_COUNT];

    /* ─── dispatch table (caller-supplied) ──────────────────────── */

    /* Per-type handler.  NULL = no-op (we still mirror the dispatch-
     * arg globals + clear active + decrement counter, just skip the
     * call).  Retail does NOT NULL-check the table — out-of-range
     * types crash there too.  All known callers stamp types in
     * [0, 0x32). */
    engine_delayed_action_handler_t dispatch[ENGINE_DELAYED_ACTION_DISPATCH_TABLE_LEN];

    /* Opaque pointer passed to every handler.  Lets test/diff vectors
     * thread state through without file-statics. */
    void    *dispatch_user;
} engine_delayed_action_state_t;

/* Drain the pending-action queue.  Pure function over `state`; no
 * external state read or written.
 *
 *   1. If `master_switch == 0`, return immediately (no state changes).
 *   2. Clear queue[0..99] to QUEUE_EMPTY, hipri_count = 0, queue_count = 0.
 *   3. Sweep records[0..399]: append i to queue if active==1 AND
 *      due_tick <= current_tick.  queue_count tracks the append cursor.
 *   4. If queue_count > 1 AND game_mode == 2: MP-relay reorder.
 *      a. For each entry in queue: if record.type ∈ (0x20, 0x24) AND
 *         record.arg0 != local_player, move to hipri_stash and replace
 *         the queue slot with QUEUE_EMPTY.
 *      b. 10 compaction passes shift QUEUE_EMPTY entries leftward
 *         (one slot per pass).
 *      c. Find the first QUEUE_EMPTY slot; append hipri_stash[0..count)
 *         starting there.
 *   5. Dispatch loop: for each non-EMPTY entry (until QUEUE_EMPTY or 100):
 *      - copy record.arg0..arg9 into the 10 dispatch-arg mirrors,
 *      - set current_idx = entry,
 *      - call dispatch[record.type] if non-NULL,
 *      - clear record.active = 0, decrement action_counter.
 *
 * See engine_delayed_action.c for the per-stage commentary and the
 * preserved-quirk callouts.
 */
void engine_dispatch_delayed_actions(engine_delayed_action_state_t *state,
                                     int current_tick);

#endif /* OPENLORDS2_ENGINE_DELAYED_ACTION_H */
