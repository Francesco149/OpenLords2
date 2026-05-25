/*
 * Trivial delayed-action handlers — pure-C ports of the no-op +
 * counter slots in retail's `PTR_FUN_004d5c98` dispatch table.  See
 * engine_handlers_da.h for the scope.
 */

#include "engine_handlers_da.h"

#include <stddef.h>

void engine_handlers_da_noop(void *user)
{
    /* Retail body is literally `ret`.  Our port bumps a sentinel
     * counter so tests can confirm the dispatcher routed to this
     * handler (vs. skipping a NULL slot).  No engine-observable
     * effect. */
    engine_handlers_da_state_t *s = (engine_handlers_da_state_t *)user;
    if (s) {
        s->noop_invocations++;
    }
}

void engine_handlers_da_divergence_counter(void *user)
{
    /* Retail:
     *   DAT_00554038 = DAT_00554038 + 1;
     * The state struct's `divergence_counter` mirrors that single
     * global.  No reads of dispatch args. */
    engine_handlers_da_state_t *s = (engine_handlers_da_state_t *)user;
    if (s) {
        s->divergence_counter++;
    }
}

void engine_handlers_da_install_trivial(
    engine_delayed_action_state_t *d,
    engine_handlers_da_state_t    *h)
{
    d->dispatch_user = h;

    /* Five no-op slots — types 0, 21, 45, 46, 47.  All five retail
     * functions are 11-byte `ret`-only bodies; all map to the same
     * handler here. */
    d->dispatch[0]  = engine_handlers_da_noop;
    d->dispatch[21] = engine_handlers_da_noop;
    d->dispatch[45] = engine_handlers_da_noop;
    d->dispatch[46] = engine_handlers_da_noop;
    d->dispatch[47] = engine_handlers_da_noop;

    /* One counter slot — type 48 — divergence counter increment. */
    d->dispatch[48] = engine_handlers_da_divergence_counter;
}
