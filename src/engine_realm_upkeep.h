/*
 * OpenLords2 — per-realm unit upkeep / wages / desertion turn pass and its
 * shared unit-lifecycle primitives.  See engine_realm_upkeep.c.
 *
 * The root pass (FUN_004acbd4) is the turn-advance engine's (retail 0x448440)
 * per-realm "pay your standing units" sweep.  It runs after the development
 * passes and operates on the per-record occupant block (g_per_territory_420,
 * save entry 5, retail 0x52f0b0, 0x1a4-byte stride, slot 0 reserved) — the
 * same block the territory/map modules bind.  In this view each occupied
 * record is a standing unit with an owner (+0x00), a per-turn wage derived
 * from its value (+0x168 → +0x15c), and a contract; the player pays the sum
 * of its units' wages out of a resource account in the owning player's
 * strategic record (g_player_strategic_352, entry 3, retail 0x57bf00, 0x160
 * stride).  When a player cannot pay, an escalating warning → disband state
 * machine runs (per-player state byte +0x158).
 *
 * The low-level helpers it drives — disband a unit (FUN_004aa039), remove it
 * from the overworld-occupant linked list (FUN_0046ec6e / FUN_0046f0a9),
 * clear its hire contract (FUN_004ac6fe), absorb it back into its army
 * (FUN_004aba5a), and total / compute its wage (FUN_004ad495 / FUN_004ad52b)
 * — are shared engine primitives called from several other subsystems (army
 * management, territory capture), so they are exposed here.
 *
 * Touches only already-named save blocks: g_per_territory_420 (entry 5),
 * g_player_strategic_352 (entry 3), g_per_army_768 (entry 4), g_unit_table_20
 * (entry 6) and g_overworld_map; plus the scalars s_slot_0053f23c (wage
 * divisor / difficulty), s_slot_0053f260 (morale-pass gate), s_slot_0057c8cc
 * (local player) and s_slot_0056d5dc (live realm count).
 */

#ifndef OPENLORDS2_ENGINE_REALM_UPKEEP_H
#define OPENLORDS2_ENGINE_REALM_UPKEEP_H

#include <stdint.h>

#include "engine_event_queue.h"   /* engine_event_queue_state_t */

/* --- shared unit-lifecycle primitives (each takes a g_per_territory_420
 *     record index 1..150) -------------------------------------------------*/

/* Port of FUN_004ad52b @ 0x4ad52b (405 B, __cdecl int(int)).  Recompute the
 * unit's per-turn wage from its value (+0x168) and store it at +0x15c, then
 * return it.  Human owners (strategic +0x05 != 0) pay value/4 (signed,
 * rounding toward zero); AI owners pay value / {3,5,10,10} selected by the
 * difficulty scalar s_slot_0053f23c. */
int engine_unit_recompute_wage(int rec);

/* Port of FUN_004ad495 @ 0x4ad495 (150 B, __cdecl int(int)).  Sum
 * engine_unit_recompute_wage over every active unit owned by `owner`. */
int engine_unit_total_wage(int owner);

/* Port of FUN_004ac6fe @ 0x4ac6fe (245 B, __cdecl void(int)).  If the unit is
 * active and holds a hire contract (+0x197 != 0), refund its contract amount
 * (+0x196) out of its value (+0x168), clear the three contract bytes
 * (+0x195/+0x196/+0x197) and free its mercenary-pool slot in g_unit_table_20. */
void engine_unit_clear_contract(int rec);

/* Port of FUN_0046ec6e @ 0x46ec6e (369 B, __cdecl void(uint)).  Unlink the
 * unit from the singly-linked occupant list of its overworld-map cell
 * (g_overworld_map[rec+0x0c]+0x05 is the list head; +0x04 is the next link). */
void engine_unit_map_unlink(int rec);

/* Port of FUN_0046eddf @ 0x46eddf (349 B, __cdecl void(int)).  Link the unit
 * into the singly-linked occupant list of its overworld-map cell — the inverse
 * of engine_unit_map_unlink, used by the territory-transfer path to re-home a
 * record after its cell changes.  Skips active army members that already hold
 * an army back-link (U_ACTIVE==1 && U_LINK_A!=0); otherwise appends the record
 * (head when empty, else tail after ≤10 hops) and records via +0x06 whether it
 * became the head. */
void engine_unit_map_link(int rec);

/* Port of FUN_0046f0a9 @ 0x46f0a9 (71 B, __cdecl void(uint)).  Fully remove a
 * unit: clear its contract, unlink it from the map, zero its whole 0x1a4-byte
 * record, then re-run the territory-income aggregation. */
void engine_unit_remove(int rec);

/* Port of FUN_004aa039 @ 0x4aa039 (328 B, __cdecl void(uint)).  Disband a
 * unit: drop its army back-links (+0x198/+0x199 → g_per_army_768+0x1bc),
 * decrement the owner's per-type strategic counter (+0x2d + rec[+0x14f]),
 * remove the unit from the map, then refresh the owner's total wage (+0xfc). */
void engine_unit_disband(int rec);

/* Port of FUN_004aba5a @ 0x4aba5a (220 B, __cdecl void(uint)).  If the unit
 * has its absorb flag (+0x167) set: when it is 1, fold its value (+0x168)
 * into its army's two accumulators (+0x24/+0x38), remove the unit and re-plan
 * the realm economies; otherwise just clear the flag. */
void engine_unit_absorb(int rec);

/* --- the turn pass ------------------------------------------------------- */

/* Port of FUN_004acbd4 @ 0x4acbd4 (650 B, __stdcall void) — the per-realm
 * unit-upkeep turn pass of the turn-advance engine (0x448440).
 *
 *   1. FUN_004ad39a: absorb-or-overflow sweep over all 150 records.
 *   2. FUN_004ace5e: per-unit morale/supply sweep (gated on s_slot_0053f260).
 *   3. For each player 1..5: total this player's wages into strategic +0xfc;
 *      if the resource account (+0x118) covers it, pay it (subtract) and reset
 *      the escalation state (+0x158); otherwise run the can't-pay escalation:
 *        state 0 → settle expiring contracts, warn (0x10e / 0xa0), state := 1
 *        state 1..3 → warn (0x11f), decay every unit's sub-stocks, state++
 *        state 4 → warn (0x10f), decay sub-stocks, state++
 *        state >=5 → disband ALL of this player's units, warn (0x110 / 0x111),
 *                    state := 0
 *
 * `evq` receives the warning messages (audience-filtered to the local player,
 * exactly as the binary posts them via FUN_00472bc5). */
void engine_realm_pay_upkeep(engine_event_queue_state_t *evq);

#endif /* OPENLORDS2_ENGINE_REALM_UPKEEP_H */
