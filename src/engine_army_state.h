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

/* Port of FUN_004a4f5b (168 B, __cdecl uint).  Sweep the same 150 occupant
 * records (g_per_territory_420, stride 0x1a4, index 1..150 through the shared
 * cursor g_army_state_index) and, for every record whose state byte (+0x08)
 * equals `state` AND whose sub-state (+0x14c) is currently non-zero, force the
 * sub-state to 2 — arming the deep movement-step executor for that occupant on
 * its next driver pass.  Returns 1 if any record was armed, else 0.
 *
 * The strategic turn driver (retail 0x49a010, unported) calls this once per
 * phase — `arm_movement(3)` in phase 6, `arm_movement(4)` in phase 3 — and
 * advances to the next phase (engine_turn_advance_phase) only when the sweep
 * arms nothing — i.e. all occupants of that state have been driven through
 * their movement. */
int engine_army_state_arm_movement(unsigned state);

/* Port of FUN_004a4e3d (286 B, __cdecl uint).  The owner-gated sibling of
 * engine_army_state_arm_movement: same sweep + arm-to-2, but it additionally
 * requires the record's owner byte (+0x00, signed) == `owner` and the
 * control-type byte (+0x01) == 0 (a local/viewer occupant).  When the matched
 * state is 1 it also tears down that occupant's siege link
 * (engine_combat_break_siege_link).  Returns 1 if any record was armed.
 *
 * The turn driver (retail 0x49a010) calls this in phase 5 as `(2, 6)` — arm
 * the settle-state, locally-controlled occupants owned by realm 6 (so the
 * state-1 siege-break hook is inert on that call) — advancing the phase only
 * when nothing was armed. */
int engine_army_state_arm_movement_owned(unsigned state, int owner);

#endif /* OPENLORDS2_ENGINE_ARMY_STATE_H */
