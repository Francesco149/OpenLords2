/*
 * Unit tests for engine_handlers_da — trivial delayed-action
 * handlers (no-op slots + divergence counter).
 *
 * The handlers are invoked through the real engine_dispatch_delayed_
 * actions() loop so the tests double as integration tests of the
 * dispatcher's per-record handler invocation path.
 */

#include "t.h"
#include "engine_handlers_da.h"
#include "engine_delayed_action.h"
#include <string.h>

/* Helper: stamp a single record into the dispatch state. */
static void stamp_record(engine_delayed_action_state_t *d,
                         int idx, uint8_t type, int32_t due_tick,
                         int32_t arg0)
{
    d->records[idx].due_tick = due_tick;
    d->records[idx].arg0     = arg0;
    d->records[idx].active   = 1;
    d->records[idx].type     = type;
    d->action_counter++;
}

/* ──────────────────────────────────────────────────────────────────
 * No-op handler — single-record dispatch round-trip
 * ────────────────────────────────────────────────────────────────── */

int test_engine_handlers_da_install_wires_noop_slots(void) {
    engine_delayed_action_state_t d = {0};
    engine_handlers_da_state_t   h  = {0};

    engine_handlers_da_install_trivial(&d, &h);

    /* All five no-op slots point to the same handler. */
    T_ASSERT(d.dispatch[0]  == engine_handlers_da_noop);
    T_ASSERT(d.dispatch[21] == engine_handlers_da_noop);
    T_ASSERT(d.dispatch[45] == engine_handlers_da_noop);
    T_ASSERT(d.dispatch[46] == engine_handlers_da_noop);
    T_ASSERT(d.dispatch[47] == engine_handlers_da_noop);
    /* Counter slot. */
    T_ASSERT(d.dispatch[48] == engine_handlers_da_divergence_counter);
    /* dispatch_user pointer is wired to the handlers state. */
    T_ASSERT(d.dispatch_user == &h);
    /* Other slots untouched. */
    T_ASSERT(d.dispatch[1] == NULL);
    T_ASSERT(d.dispatch[49] == NULL);
    return 0;
}

int test_engine_handlers_da_noop_increments_sentinel(void) {
    /* Direct-call sanity: noop bumps noop_invocations, nothing else. */
    engine_handlers_da_state_t h = {0};
    engine_handlers_da_noop(&h);
    T_ASSERT_EQ_I(h.noop_invocations, 1);
    T_ASSERT_EQ_I(h.divergence_counter, 0);
    engine_handlers_da_noop(&h);
    T_ASSERT_EQ_I(h.noop_invocations, 2);
    return 0;
}

int test_engine_handlers_da_noop_tolerates_null_user(void) {
    /* Defensive: if dispatch_user wasn't set (e.g. caller installed
     * handlers without calling _install_trivial), the no-op shouldn't
     * crash.  Retail body is `ret` — no user-pointer access at all. */
    engine_handlers_da_noop(NULL);
    /* If we got here, we didn't crash. */
    return 0;
}

int test_engine_handlers_da_noop_via_dispatcher_at_type_0(void) {
    /* Drive the no-op through the real dispatcher.  Stamp a record of
     * type 0 (a no-op slot), tick it, verify the record was cleared
     * and the handler ran. */
    engine_delayed_action_state_t d = {0};
    engine_handlers_da_state_t   h  = {0};
    d.master_switch = 1;
    engine_handlers_da_install_trivial(&d, &h);

    stamp_record(&d, /*idx*/ 5, /*type*/ 0, /*due*/ 10, /*arg0*/ 0);

    engine_dispatch_delayed_actions(&d, /*current_tick*/ 10);

    /* Handler ran. */
    T_ASSERT_EQ_I(h.noop_invocations, 1);
    /* Record was cleared. */
    T_ASSERT_EQ_I(d.records[5].active, 0);
    /* Action counter decremented from 1 to 0. */
    T_ASSERT_EQ_I(d.action_counter, 0);
    return 0;
}

int test_engine_handlers_da_noop_via_dispatcher_at_each_type(void) {
    /* Stamp ONE record of each no-op type; verify all five fire and
     * the divergence_counter stays at zero. */
    engine_delayed_action_state_t d = {0};
    engine_handlers_da_state_t   h  = {0};
    d.master_switch = 1;
    engine_handlers_da_install_trivial(&d, &h);

    uint8_t types[] = {0, 21, 45, 46, 47};
    for (int i = 0; i < 5; i++) {
        stamp_record(&d, /*idx*/ i, types[i], /*due*/ 10, /*arg0*/ 0);
    }

    engine_dispatch_delayed_actions(&d, /*current_tick*/ 10);

    T_ASSERT_EQ_I(h.noop_invocations, 5);
    T_ASSERT_EQ_I(h.divergence_counter, 0);
    T_ASSERT_EQ_I(d.action_counter, 0);
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_I(d.records[i].active, 0);
    }
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Divergence counter handler
 * ────────────────────────────────────────────────────────────────── */

int test_engine_handlers_da_counter_increments_once(void) {
    /* Direct-call sanity. */
    engine_handlers_da_state_t h = {0};
    engine_handlers_da_divergence_counter(&h);
    T_ASSERT_EQ_I(h.divergence_counter, 1);
    engine_handlers_da_divergence_counter(&h);
    T_ASSERT_EQ_I(h.divergence_counter, 2);
    /* noop_invocations untouched. */
    T_ASSERT_EQ_I(h.noop_invocations, 0);
    return 0;
}

int test_engine_handlers_da_counter_via_dispatcher(void) {
    /* Stamp 3 records of type 48; verify counter goes to 3. */
    engine_delayed_action_state_t d = {0};
    engine_handlers_da_state_t   h  = {0};
    d.master_switch = 1;
    engine_handlers_da_install_trivial(&d, &h);

    for (int i = 0; i < 3; i++) {
        stamp_record(&d, /*idx*/ i, /*type*/ 48, /*due*/ 5, /*arg0*/ 0);
    }

    engine_dispatch_delayed_actions(&d, /*current_tick*/ 5);

    T_ASSERT_EQ_I(h.divergence_counter, 3);
    T_ASSERT_EQ_I(h.noop_invocations, 0);
    T_ASSERT_EQ_I(d.action_counter, 0);
    return 0;
}

int test_engine_handlers_da_counter_ignores_dispatch_args(void) {
    /* Retail FUN_00448411's only operation is `DAT_00554038 += 1` —
     * no dispatch arg reads.  Verify a record with extreme args
     * doesn't change behavior. */
    engine_delayed_action_state_t d = {0};
    engine_handlers_da_state_t   h  = {0};
    d.master_switch = 1;
    engine_handlers_da_install_trivial(&d, &h);

    d.records[7].due_tick = 0;
    d.records[7].arg0     = -42;
    d.records[7].arg1     = 0x7fffffff;
    d.records[7].arg9     = 0xdeadbeef;
    d.records[7].active   = 1;
    d.records[7].type     = 48;
    d.action_counter = 1;

    engine_dispatch_delayed_actions(&d, /*current_tick*/ 0);

    T_ASSERT_EQ_I(h.divergence_counter, 1);
    return 0;
}

int test_engine_handlers_da_mixed_records_in_order(void) {
    /* Mix no-op and counter records; verify the dispatcher routes
     * each to its correct handler. */
    engine_delayed_action_state_t d = {0};
    engine_handlers_da_state_t   h  = {0};
    d.master_switch = 1;
    engine_handlers_da_install_trivial(&d, &h);

    stamp_record(&d, 0, /*type*/ 0,  10, 0);  /* no-op */
    stamp_record(&d, 1, /*type*/ 48, 10, 0);  /* counter */
    stamp_record(&d, 2, /*type*/ 47, 10, 0);  /* no-op */
    stamp_record(&d, 3, /*type*/ 48, 10, 0);  /* counter */

    engine_dispatch_delayed_actions(&d, /*current_tick*/ 10);

    T_ASSERT_EQ_I(h.noop_invocations, 2);
    T_ASSERT_EQ_I(h.divergence_counter, 2);
    T_ASSERT_EQ_I(d.action_counter, 0);
    return 0;
}

int test_engine_handlers_da_other_types_still_treated_as_noop(void) {
    /* A record with a type the handler module didn't install (e.g.
     * type 1) hits a NULL slot — dispatcher's NULL-tolerance path
     * still fires, record gets cleared, but neither sentinel changes. */
    engine_delayed_action_state_t d = {0};
    engine_handlers_da_state_t   h  = {0};
    d.master_switch = 1;
    engine_handlers_da_install_trivial(&d, &h);

    stamp_record(&d, 0, /*type*/ 1, 10, 0);   /* uninstalled slot */
    stamp_record(&d, 1, /*type*/ 48, 10, 0);  /* counter */

    engine_dispatch_delayed_actions(&d, /*current_tick*/ 10);

    T_ASSERT_EQ_I(h.noop_invocations, 0);     /* uninstalled doesn't bump */
    T_ASSERT_EQ_I(h.divergence_counter, 1);
    T_ASSERT_EQ_I(d.records[0].active, 0);    /* still cleared */
    T_ASSERT_EQ_I(d.records[1].active, 0);
    T_ASSERT_EQ_I(d.action_counter, 0);
    return 0;
}
