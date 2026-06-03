/*
 * OpenLords2 — strategic-map army movement: destination-cell evaluator.
 *
 * The bottom of the real-time army-movement engine.  When the per-tick army-
 * state driver (engine_army_state.c) hands an occupant off to the deep movement
 * executor (retail FUN_00465d28, a later port), the executor steps the occupant
 * one cell along its path and asks "what is in the cell I am about to enter?".
 * That question is answered by this evaluator sub-tree:
 *
 *   FUN_00466893 → engine_army_move_eval_dir   directional neighbour pick
 *   FUN_00466c3c → classify_cell (static)      cell-flag → outcome code
 *   FUN_004658c1 → eval_occupied (static)      resolve a cell already occupied
 *
 * The leaf relations predicate FUN_004a16f7 lives in engine_diplomacy.c
 * (engine_diplomacy_should_engage); the collision resolver reaches the already-
 * ported combat callees (engine_combat_dispatch / engine_combat_merge_stack /
 * engine_diplomacy_battle_adjust / engine_unit_remove) and the event queue.
 *
 * See docs/findings/combat-resolution.md.
 */
#ifndef OPENLORDS2_ENGINE_ARMY_MOVE_H
#define OPENLORDS2_ENGINE_ARMY_MOVE_H

#include <stdint.h>

#include "engine_event_queue.h"

/* Bind the per-cell descriptor grid (retail DAT_00522f90): a byte-addressed
 * grid of 8-byte cells, 64 columns (row stride 0x200 bytes).  Cell fields read
 * by the evaluator: +0 terrain/owner, +1 movement flags, +5 occupant-record
 * index, +7 effective-region key.  Same grid engine_army_state binds. */
void engine_army_move_bind_cells(const uint8_t *cells);

/* Bind the movement-SFX seam (retail 0x426120 — an indexed DirectSound
 * footstep buffer keyed by sound id; not yet ported).  Called by the commit-
 * step with the per-state id (0xb idle, 5 settle, 0xc march).  NULL (the
 * default) is a no-op, keeping the drop-in silent unless audio is wired —
 * matching the combat seams. */
typedef void (*engine_army_move_sfx_fn)(int sound_id);
void engine_army_move_bind_sfx_fn(engine_army_move_sfx_fn fn);

/* Outcome scratch latched by the evaluator from the cell it classified (retail
 * runtime scalars).  The movement executor (FUN_00465d28, later chip) consumes
 * them to drive arrival / region-event / combat-engage.  Not save state. */
extern uint8_t g_army_move_cell_flags;    /* DAT_0050a4a4 — cell +1 movement flags */
extern uint8_t g_army_move_cell_region;   /* DAT_0050a49c — cell +7 region key      */
extern uint8_t g_army_move_cell_terrain;  /* DAT_0050a49d — cell +0 terrain/owner    */
extern int     g_army_move_cell_off;      /* DAT_0050a4a0 — byte offset of the cell  */

/* Port of FUN_00466893 @ 0x466893 (900 B, __cdecl int(int)).  Evaluate the
 * cell one step away from the current occupant (engine_army_state's
 * g_army_state_index) in compass direction `dir` (0=N, 1=NE, 2=E, 3=SE, 4=S,
 * 5=SW, 6=W, 7=NW).  Returns 0 when the step would leave the 64×64 grid;
 * otherwise the destination cell's outcome code from classify_cell:
 *   1 open · 3 rough/blocked-base · 5 combat · 6 region-event · 7 special ·
 *   8 impassable · 10 arrival (current stack destroyed in the collision).
 * May mutate occupant records / post notifications via the collision resolver,
 * so `evq` / `mp_active` are threaded through to the combat callees. */
int engine_army_move_eval_dir(int dir, engine_event_queue_state_t *evq,
                              int mp_active);

/* Port of FUN_00466d84 @ 0x466d84 (995 B, __stdcall void).  Commit the current
 * occupant's move into the neighbour cell in the direction latched at record
 * +0x09 (the classifier writes it there): unlink from the old cell, play the
 * per-state footstep SFX, apply the direction's column/row/cell-offset delta,
 * relink into the new cell, then set the sticky rough-terrain flag (+0x14d) iff
 * the new cell's movement flags carry bit0.  A direction byte > 7 is a no-op
 * (no relink). */
void engine_army_move_commit_step(void);

#endif /* OPENLORDS2_ENGINE_ARMY_MOVE_H */
