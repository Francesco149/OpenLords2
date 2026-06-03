/*
 * OpenLords2 — real-time strategic-map army-state machine (per-tick SP driver).
 *
 * Port of FUN_004650b0 and its four state→display handlers.  See
 * engine_army_state.c and docs/findings/combat-resolution.md.
 */
#ifndef OPENLORDS2_ENGINE_ARMY_STATE_H
#define OPENLORDS2_ENGINE_ARMY_STATE_H

#include <stdint.h>

#include "engine_event_queue.h"

/* Bind the per-cell descriptor grid (retail DAT_00522f90, 8 bytes/cell, the
 * effective-region key at cell offset +7).  The handlers read it to sync each
 * occupant's region colour against the cell it stands on. */
void engine_army_state_bind_cells(const uint8_t *cells);

/* Bind the deep movement-step executor seam (retail FUN_00465d28, now ported as
 * engine_army_move_run).  Invoked by a handler when the occupant's sub-state
 * (+0x14c) is 2; the driver forwards its own `evq` / `mp_active` so the executor
 * can thread them into the per-step classifier (retail reads them from globals).
 * NULL (the default) leaves it an unbound no-op. */
typedef void (*engine_army_move_fn)(engine_event_queue_state_t *evq, int mp_active);
void engine_army_state_bind_move_fn(engine_army_move_fn fn);

/* Current occupant index the driver is processing (retail DAT_005533e8).
 * Exposed so the bound movement seam (which retail calls argless) can read it. */
extern int     g_army_state_index;        /* DAT_005533e8 */
extern int     g_army_state_live_count;    /* DAT_00553f08 — live occupants this walk */
extern uint8_t g_army_state_abort;         /* DAT_0050a49e — seam-set early-exit flag */

/* Port of FUN_004650b0 (265 B, __cdecl void).  Walk all 150 occupant records
 * (g_per_territory_420, stride 0x1a4, index 1..150) and, for each live one
 * (owner +0x00 != 0), dispatch by its state byte (+0x08) through the
 * per-state display handlers; state 0 or >= 6 zeroes (disbands) the record.
 * `evq` / `mp_active` are forwarded to the region-occupation reconcile helpers
 * (retail reads them from globals). */
void engine_army_state_run(engine_event_queue_state_t *evq, int mp_active);

#endif /* OPENLORDS2_ENGINE_ARMY_STATE_H */
