/*
 * OpenLords2 — trivial delayed-action handlers.
 *
 * The retail delayed-action dispatcher (see engine_delayed_action.h)
 * routes each due record through a 50-entry function-pointer table
 * at retail VA 0x4d5c98 (`PTR_FUN_004d5c98`).  The table indices are
 * the record's `type` byte; each entry is a handler that consumes
 * the 10 dispatch-arg globals (mirrored on the state struct's
 * disp_a0..a9) and applies whatever effect that action type encodes.
 *
 * Most handlers call into engine subsystems we have not ported yet
 * (combat-action commits, MP broadcasts, per-player struct updates)
 * — those wait for their callee subsystems to land.  This module
 * covers the trivial subset that requires no helpers:
 *
 *   type indices 0, 21, 45, 46, 47   → no-op
 *     FUN_004479de / FUN_00447ddb /
 *     FUN_004483f0 / FUN_004483fb /
 *     FUN_00448406 — all 11-byte `ret`-only retail bodies.
 *
 *   type index 48                    → divergence counter increment
 *     FUN_00448411 — DAT_00554038 += 1.  DAT_00554038 is the MP
 *     "divergences" counter (named via the 0x4d449c string literal
 *     "divergances" used elsewhere; the counter has multiple writers
 *     across the engine, this handler is one of them).
 *
 * NOT covered here (require unported callees, deferred to a later
 * dispatch):
 *
 *   - type 49 FUN_00448422 — calls FUN_00435211 (per-player struct
 *     refresh) and sets DAT_0057d340 = 2.  The FUN_00435211 callee
 *     pulls in FUN_0049aa0e (2197 B per-player summarizer) and
 *     FUN_0049d1e0 (1024 B per-player update) — substantial port.
 *
 *   - the remaining ~40 single-callee wrappers — each calls one or
 *     two unported helpers (FUN_0043a23f, FUN_00428xxx etc) that
 *     model the actual action semantics.
 *
 * The dispatcher already treats a NULL slot as "no-op" (still
 * mirrors dispatch args + clears `active` + decrements
 * action_counter), so the no-op handlers' value is mostly
 * documentary — they make the dispatch surface explicit and let
 * tests verify the handler IS being invoked (via a side-effect-free
 * sentinel touch on the state).  The counter handler does observable
 * work and is the actual functional contribution of this chip.
 *
 * Pure C; same shape as other engine_* modules (no Win32 deps, all
 * state through the caller-provided `engine_delayed_action_state_t`
 * via the dispatch_user pointer).
 */

#ifndef OPENLORDS2_ENGINE_HANDLERS_DA_H
#define OPENLORDS2_ENGINE_HANDLERS_DA_H

#include "engine_delayed_action.h"

#include <stdint.h>

/* Per-handler-module state.  Held by the dispatch_user pointer so
 * the handlers can find it without resorting to file-statics.
 *
 * Currently a single counter; will grow as more handlers land. */
typedef struct {
    /* DAT_00554038 — "divergances" counter (retail typo preserved
     * via the string-literal at 0x4d449c).  Multiple writers in
     * retail; this module exposes the delayed-action increment-by-1
     * writer only.  Caller is responsible for sharing the counter
     * across all writers. */
    int divergence_counter;

    /* Sentinel: incremented by each no-op handler invocation.  Lets
     * tests verify the dispatcher actually called the handler vs.
     * skipped via the NULL-slot fast path. */
    int noop_invocations;
} engine_handlers_da_state_t;

/* No-op handler — port of FUN_004479de et al.  Retail body is
 * literally `ret`.  Our port touches `noop_invocations` so tests can
 * see the handler ran; the observable engine effect is still zero
 * (the counter is internal to the test harness, not engine state). */
void engine_handlers_da_noop(void *user);

/* Divergence-counter increment — port of FUN_00448411.  Retail body:
 *   `DAT_00554038 = DAT_00554038 + 1;`
 * Single global write, no reads of dispatch args. */
void engine_handlers_da_divergence_counter(void *user);

/* Install the trivial handlers into a dispatch state.  Pre-populates
 * type indices 0, 21, 45, 46, 47 with engine_handlers_da_noop and
 * type index 48 with engine_handlers_da_divergence_counter.  Other
 * entries are left as the caller had them (NULL or whatever).  Sets
 * the dispatch_user pointer to `handlers`. */
void engine_handlers_da_install_trivial(
    engine_delayed_action_state_t *dispatch_state,
    engine_handlers_da_state_t    *handlers);

#endif /* OPENLORDS2_ENGINE_HANDLERS_DA_H */
