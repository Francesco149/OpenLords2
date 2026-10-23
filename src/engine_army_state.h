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

/* The movement executor's stack residue as the driver's handlers see it: the
 * dword at (driver entry esp) - 0x30 - 4k, k = 0..4 (engine_army_state.c,
 * quirks #229 / #232).  Kept across calls; the hybrid host loads it from
 * retail's stack before each driver call and stores it back after. */
extern uint32_t g_army_state_res[5];

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

/* Port of FUN_004a8507 (1444 B, __cdecl uint).  Advance one besieging stack's
 * siege-equipment construction by one turn-phase tick.  A no-op returning 0
 * unless the record at `idx` is live (owner +0x00 != 0), state 1, actively
 * besieging (+0x199 != 0) and has head-count (+0x168 >= 1, the per-tick build
 * budget).  Otherwise it pours the head-count budget — split evenly across the
 * still-unfinished slots, then topped up in slot order — into the three siege
 * slots' build values (each capped at k_siege_build_target[slot] * count),
 * recomputes each slot's percent, and writes the aggregate remaining-work byte
 * (+0x19c, ceil(total gap / head)).  Returns 1 the tick that byte reaches 0 —
 * all ordered siege engines built — else 0. */
int engine_army_state_siege_build_step(int idx);

/* Port of FUN_004a84ba (77 B, __cdecl uint).  Resume the persistent siege-scan
 * cursor (s_slot_0057cad4, reset to 1 at phase-2 entry by retail FUN_004a82b9)
 * and step it forward one record at a time, advancing each one's siege
 * construction (engine_army_state_siege_build_step); stop on the first stack
 * that finishes building this pass, leaving the cursor parked on it and
 * returning 1.  Returns 0 once the cursor walks past record 150 with nothing
 * completed.
 *
 * The turn driver (retail 0x49a010) calls this in phase 2 every 100th sub-tick:
 * on 1 it launches the assault on the parked record (retail FUN_004a8aab, the
 * A/V assault trigger — not yet ported); on 0 it advances the phase. */
int engine_army_state_scan_siege_built(void);

/* Round-robin realm cursor used by raider targeting (retail DAT_004e59dc — a
 * runtime scalar, not save-schema; exposed so the planner and tests can read /
 * reset it). */
extern int g_raider_target_cursor;

/* Port of FUN_004ac5ba (324 B, __cdecl bool).  Assign roving stack `idx` a
 * target realm and point its path-trace goal (+0x16/+0x17) at that realm's
 * capital cell.  When the global planner is in force-mode 2 (s_slot_0057c934)
 * or the stack has no target yet (+0x151 == 0), step the shared realm cursor
 * g_raider_target_cursor (wrapping at the realm count s_slot_0056d5dc) and, if
 * it lands on the stack's own region (+0x10), advance once more; store it as the
 * target.  Then copy the target realm's capital (g_per_army_768 +0x6c/+0x6d, via
 * the shared cursor) into the path-trace goal (+0x16/+0x17) and clear the retry
 * flag (+0x150).
 * Returns non-zero when a target is set.  Pure record/scalar bookkeeping. */
int engine_army_state_raid_assign_target(int idx);

/* Port of FUN_004ac499 (289 B, __cdecl void).  Plan raider movement for every
 * settling stack: sweep the 150 occupant records and, for each in state 2 that
 * engine_army_state_raid_assign_target gives a target, clear its planning-pending
 * flag (+0x14e), flood a path-field cost map from the stack's cell
 * (engine_path_field_fill, slot 0), trace it to the target capital
 * (engine_path_field_trace); if reachable, commit the path into the stack's
 * waypoints (engine_path_field_copy_record) and arm its sub-state (+0x14c = 1).
 * A phase-5 callee of the strategic turn driver 0x49a010. */
void engine_army_state_plan_raids(void);

/* Port of FUN_00429418 (669 B, __cdecl void).  Phase-3 callee of the strategic
 * turn driver 0x49a010.  Sweep the 150 occupant records and, for each in state 4
 * (idle/in-transit), set its march goal to its target realm's (+0x167) capital
 * cell (g_per_army_768 +0x6c/+0x6d): mirror that into the executor destination
 * (+0x14/+0x15) and the path-trace goal (+0x16/+0x17), republish the target byte
 * (+0x151), and clear the retarget/plan flags (+0x150/+0x14e).  Then flood a
 * path-field from the stack and, if it traces to the capital in under 150
 * waypoints, commit the route + arm the executor (+0x14c = 1); a degenerate
 * (>= 150-waypoint) route re-floods permissively and retries once.  Pure
 * record/path-field bookkeeping. */
void engine_army_state_plan_capital_marches(void);

/* Port of FUN_004280e9 (904 B, __cdecl void).  Phase-6 callee of the strategic
 * turn driver 0x49a010.  Sweep the 150 occupant records; for each in state 3
 * (idle/in-transit) that is flagged "arrived" (+0x150), scan up to 16 entries of
 * its category-bucket row (s_slot_00567970 +(i-1)*16) from a round-robin cursor
 * (+0x164) for the first in-range realm id, find a settle cell near that realm's
 * capital (FUN_00428007 / box scan), and on success point the march goal there
 * (+0x14..+0x17), republish the target (+0x151) and clear the arrived flag.
 * Every stack then not "arrived" routes to its goal via the shared dual-flood
 * tail (clearing +0x14e, arming +0x14c).  Pure record/path-field bookkeeping. */
void engine_army_state_plan_settle_marches(void);

/* Probe for a settle spot near `realm`'s capital (FUN_00428007); on a hit return
 * 1 and the matched cell via the out-params.  Exposes the box-scan scratch for
 * the AI caravan dispatcher (engine_army_ai, brain step 7). */
int engine_army_state_find_settle_spot(int realm, int *col, int *row);

#endif /* OPENLORDS2_ENGINE_ARMY_STATE_H */
