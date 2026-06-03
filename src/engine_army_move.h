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

/* Bind the blocked-step alert seam (retail 0x426216 — a distinct indexed
 * DirectSound play call, separate from the 0x426120 footstep family).  The
 * blocked branch plays id 3 when a stack bulldozes a foreign-region cell.  NULL
 * (the default) is a no-op, keeping the drop-in silent unless audio is wired. */
void engine_army_move_bind_alert_sfx_fn(engine_army_move_sfx_fn fn);

/* ── Movement-executor dispatcher seams (retail callees not yet ported) ──────
 * The executor (engine_army_move_run, FUN_00465d28) dispatches each step's
 * outcome into a handful of deeper sub-trees, each its own later port chip.
 * Until one lands it is a bind-able seam defaulting to a no-op — exactly like
 * the SFX seams above and the army-state move_fn seam.  Binding them is how each
 * future chip wires itself into the executor.  The gameplay handlers receive the
 * driver's `evq` / `mp_active` (retail reads them from globals); the present seam
 * is pure render and takes only its mode arg.
 *
 *   present  0x426360  flush/scroll the strategic-map surface (DirectDraw
 *                          vtable present; pure render side-effect)
 *   arrive   0x467196  path-end / arrival finaliser (clears the order)
 *   combat_a 0x4296b5  combat-engage on a hostile-occupied destination
 *   combat_b 0x4a6c68  combat resolution / aftermath
 *   region_lo 0x46873f region-event handler, low terrain (< 0x10)
 *   region_hi 0x4686a0 region-event handler, high terrain (0x15..0x19)
 *   special  0x468ae2  special-tile event (outcome code 7) — ported, see below */
typedef void (*engine_army_move_present_fn)(int mode);
typedef void (*engine_army_move_arrive_fn)(int self);
typedef void (*engine_army_move_region_fn)(int self, unsigned region,
                                           engine_event_queue_state_t *evq,
                                           int mp_active);
typedef void (*engine_army_move_cell_fn)(int self, int cell_off,
                                         engine_event_queue_state_t *evq,
                                         int mp_active);
typedef void (*engine_army_move_redraw_fn)(int mode);

void engine_army_move_bind_present_fn(engine_army_move_present_fn fn);
void engine_army_move_bind_arrive_fn(engine_army_move_arrive_fn fn);
void engine_army_move_bind_combat_fns(engine_army_move_region_fn engage,
                                      engine_army_move_region_fn resolve);
void engine_army_move_bind_region_fns(engine_army_move_cell_fn lo,
                                      engine_army_move_cell_fn hi);
void engine_army_move_bind_special_fn(engine_army_move_cell_fn fn);
/* Bind the demand-redraw seam used by the settle-deposit handler (DAT_0057d340).
 * NULL (default) → no-op; the render wiring binds engine_set_demand_redraw. */
void engine_army_move_bind_redraw_fn(engine_army_move_redraw_fn fn);

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

/* Port of FUN_00469e0e @ 0x469e0e (72 B, __cdecl int(int)).  1 iff the cell at
 * byte offset `cell_off` is real land terrain (2 ≤ terrain < 0x17); 0 for
 * sea/edge and special tiles.  Reads the bound cell grid's terrain byte (+0). */
int engine_army_move_terrain_passable(int cell_off);

/* Port of FUN_0046634d @ 0x46634d (1002 B, __cdecl int(int)).  The per-step
 * result classifier the movement executor (FUN_00465d28) calls each tick with
 * the path-step direction `dir` (0..7).  Runs the stall/retry hysteresis until
 * the step-eval is armed, then evaluates the step (engine_army_move_eval_dir),
 * post-processes the outcome (running the blocked branch on an impassable step,
 * latching the rough flag on a rough one), and commits the move on a legal
 * step.  Returns:
 *   1 open / committed · 2 step-eval just armed · 3 rough (handled internally
 *   then committed as 1's sibling) · 5 combat · 6 region-event · 7 special ·
 *   10 arrival or walk aborted · 0 grid edge.
 * Codes 5/6/7/10 are passed straight up for the executor to dispatch; 1/3/8 are
 * resolved here.  Threads `evq` / `mp_active` to the step evaluator and the
 * blocked branch's relation hit. */
int engine_army_move_classify_step(int dir, engine_event_queue_state_t *evq,
                                   int mp_active);

/* Port of FUN_00465d28 @ 0x465d28 (1441 B, __cdecl void).  The real-time
 * movement-step executor: the army-state driver hands the current occupant
 * (engine_army_state's g_army_state_index) off here when its sub-state (+0x14c)
 * is 2.  Each entry resets the per-occupant timer (+0x160 = 4) and walks the
 * stack toward its next waypoint, looping until the per-tick step budget (+0x154,
 * set by the state handler) is spent, the path ends, or an event interrupts:
 *
 *   • While disarmed it spins the classifier's stall/retry hysteresis (the
 *     classifier ignores the direction until armed), then on arming loads the
 *     next waypoint from the per-record list (+0x1d col / +0x1e row, indexed by
 *     the waypoint counter +0x1c) and evaluates the step toward it.
 *   • The local player's marching stack stamps a 13×13 alternate-draw box and
 *     marks the screen dirty each armed pass (render only).
 *   • Budget spent or a turn commit in progress (g_sim_turn_progress 1/2) snaps
 *     progress to the budget, recenters the map on the stack, and finalises via
 *     the arrival seam unless the record's +0x06 byte suppresses it.
 *   • A classifier outcome of 5 runs the two combat seams (and aborts the whole
 *     army walk if the battle screen 0x12 is up); 6 runs a region-event seam
 *     chosen by the destination terrain; 7 runs the special-tile seam; 10 ends
 *     the walk (arrival / collision-destroyed).
 *
 * `evq` / `mp_active` are forwarded straight to engine_army_move_classify_step.
 * All combat / region-event / arrival / camera callees are bind-able seams
 * above (NULL = no-op), so until they are ported the executor faithfully steps
 * and commits the stack across the grid without firing those deeper events. */
void engine_army_move_run(engine_event_queue_state_t *evq, int mp_active);

/* Port of FUN_00468ae2 @ 0x468ae2 (351 B, __cdecl void).  The special-tile event
 * the executor dispatches on outcome code 7: a non-idle stack (state ≠ 3/4)
 * stepping onto an *untouched* special tile (cell terrain +0 == 0x10) in a
 * foreign region (region owner +0x05 ≠ mover owner +0x00) plunders it:
 *   • bumps the stack's path-progress accumulator (+0x153) by 7,
 *   • flips the tile to terrain 0x13 and stamps a 0x3c countdown at cell +3,
 *   • drains a quarter of the region's +0x24 stockpile (x -= x/4, round toward
 *     zero — matches the cdq/and-3/sar retail signed div),
 *   • levies a relation hit (engine_diplomacy_battle_adjust(region_owner,
 *     mover_owner, 0x14, evq)) and plays the blocked-step alert cue (id 3).
 * Idle/same-owner/already-plundered tiles are a no-op.  `mp_active` is unused
 * (accepted for seam uniformity).  Bound to the executor's special seam via
 * engine_army_move_bind_special_fn in game_init. */
void engine_army_move_special_tile(int self, int cell_off,
                                   engine_event_queue_state_t *evq, int mp_active);

/* Port of FUN_0046873f @ 0x46873f (931 B, __cdecl void).  The region-event
 * handler the executor dispatches on outcome code 6 for *low* terrain (cell +0
 * < 0x10): a non-idle stack (state ≠ 3/4) stepping onto a foreign region's
 * (region owner +0x05 ≠ mover owner +0x00) *undeveloped* resource tile develops
 * it.  The cell terrain selects one of four resource bands:
 *   {0,1,2} → 3 (regrow 0x2e) · {4,5} → 6 (regrow 9) ·
 *   {7,8} → 9 (regrow 0x13)   · {10,11} → 0xc (regrow 0x1d)
 * Already-developed sentinels (3/6/9/0xc) and terrain ≥ 0xd are a no-op.  A
 * developed tile: flips the cell terrain to the band sentinel, stamps the regrow
 * countdown at cell +3, resets the region's matching resource node (a +0x2xx
 * triple: u8 state := 3, u8 counter := 0, i32 := 0; plus a +0xfx pair: i32 timer
 * := -1, i32 := 0), plays the alert cue (id 3) and bumps the stack's path-
 * progress accumulator (+0x153) by 7.  `evq` / `mp_active` are unused (accepted
 * for seam uniformity).  Bound to the executor's region-lo seam via
 * engine_army_move_bind_region_fns in game_init. */
void engine_army_move_region_event_lo(int self, int cell_off,
                                      engine_event_queue_state_t *evq,
                                      int mp_active);

/* Port of FUN_004296b5 @ 0x4296b5 (319 B, __cdecl void).  The first of the two
 * seams the executor fires on a combat outcome (code 5): combat_a (this), then
 * combat_b (FUN_004a6c68, unported).  A settling stack (state 4) whose target
 * settle region (+0x167) is the very region it enters is *absorbed*: it deposits
 * the two contingent values it carries — provision-raw (+0x16c i16) into the
 * region's +0x224 and stock-A (+0x170 i16) into +0x250 — then (recensus/reduce
 * iff stock-A > 0) the realm is fully recomputed (grade cells, provision, plan,
 * allocate workforce, provision + plan again), the stack is removed, and a
 * demand-redraw (DAT_0057d340 = 2, the redraw seam above) is raised iff the
 * absorbed region is the on-screen one (s_slot_0057c93c).  Any other state /
 * mismatched region → no-op.  `evq` / `mp_active` are unused (combat-seam
 * signature).  Bind-able via engine_army_move_bind_combat_fns; left NULL until
 * combat_b (FUN_004a6c68) lands, since firing only half the pair would skip the
 * actual combat resolution on a contested step. */
void engine_army_move_combat_engage(int self, unsigned region,
                                    engine_event_queue_state_t *evq, int mp_active);

/* Port of FUN_00467196 @ 0x467196 (422 B, __cdecl void).  The executor's arrival
 * seam: when a stack reaches its path end / spends its step budget, probe for
 * the nearest map feature from the stack's current cell; if one is in range,
 * adopt it as the new destination (+0x14/+0x15), debit the path-progress
 * accumulator (+0x153) by the feature's ring distance (clamp 0), set sub-state 2
 * and clear the region-event suppressor (+0x14e), then re-path with the cost-
 * field pathfinder (flood from current cell → trace from new dest → copy route
 * into the waypoint list).  No feature in range → a no-op.  Bound to the
 * executor's arrive seam via engine_army_move_bind_arrive_fn — pending the
 * move-cost grid populator (retail 0x46ee31) so the flood has real terrain. */
void engine_army_move_arrival(int self);

#endif /* OPENLORDS2_ENGINE_ARMY_MOVE_H */
