/*
 * OpenLords2 — per-realm random-event onset pass (the 0x448440 turn-advance
 * engine's turn-START event roll).  See engine_realm_events.c.
 *
 * Operates on the per-army/realm record block (g_per_army_768, save entry 4,
 * retail 0x53f9b0, 0x300-byte stride, slot 0 reserved) and credits/debits the
 * owning player's strategic record (g_player_strategic_352, entry 3, retail
 * 0x57bf00, 0x160 stride) — both already named save globals.  Two event IDs
 * also flip overworld-map cells via the territory module.
 */

#ifndef OPENLORDS2_ENGINE_REALM_EVENTS_H
#define OPENLORDS2_ENGINE_REALM_EVENTS_H

#include <stdint.h>

/* Port of FUN_00448819 @ 0x448819 (1277 B, __stdcall void) — the per-realm
 * random-event ONSET pass, the first sim pass the turn-advance orchestrator
 * (retail 0x448440) runs after the standings snapshot.
 *
 * For every realm 1..s_slot_0056d5dc:
 *
 *   1. Unconditionally clear four turn-scratch bytes in the army record —
 *      the three consecutive +0x1fb/+0x1fc/+0x1fd and the lone +0x1a8 (the
 *      same set the mid-turn reset 0x448d16 re-zeroes later in the turn).
 *
 *   2. Advance a cursor into the 256-entry event "deck" (retail const table
 *      0x4d6108).  The cursor is seeded ONCE at entry from the cached 7-bit
 *      RNG byte (rng_7bit · 2, retail reads DAT_0058fd60) and incremented one
 *      slot per realm (wrapping at 256), so consecutive realms read
 *      consecutive deck slots.
 *
 *   3. Roll an event for this realm only when all three gates pass: the
 *      realm's OWNER has events enabled (g_player_strategic_352[owner]+0x05),
 *      the global turn/era counter s_slot_00553edc has passed 0x4f4, and the
 *      deck slot under the cursor is non-zero.  On a hit, mark the realm's
 *      event active (+0x00 = 1), stamp the event id (+0x1aa, i16), and run
 *      the per-event handler.
 *
 * Each handler either COMMITS the event (sets a per-realm scratch colour/
 * code byte and/or credits/debits the owner's resource accounts, optionally
 * adjusting a morale stat) or CANCELS it when a precondition fails (event id
 * +0x1aa and active flag +0x00 both back to 0).  Event magnitudes are keyed
 * on the turn's event-mode s_slot_0057c934 (entry 23, the season cycled in by
 * the orchestrator: 1..4).  Two handlers (ids 0x12f / 0x136) instead probe
 * the realm's territory roster (g_misc_1360, 0x50-byte rows of 20 i32 cell
 * offsets) for an eligible overworld cell and flip it via the territory
 * module's engine_territory_set_cell_type (FUN_0046d7f4); they cancel when no
 * eligible cell is found.
 *
 * Touches only shared save blocks (g_per_army_768, g_player_strategic_352,
 * g_overworld_map, g_misc_1360) and the shared scalars (s_slot_0056d5dc /
 * 00553edc / 0057c934) — no new persisted globals.  The one runtime input,
 * the cached 7-bit RNG byte (retail's DAT_0058fd60 / engine.c's g_rng_7bit_b),
 * is passed in rather than read directly: it is a file-static mirror in the
 * Win32-only engine.c TU, which the host test build does not link.  The
 * turn-advance orchestrator (retail 0x448440) passes the live value when it
 * is ported; the cursor (·2-and-walk) logic stays in the port.
 *
 * Dead-but-tested in the SP drop-in today: nothing yet drives s_slot_00553edc
 * past 0x4f4, so the gate never opens at runtime; the host suite exercises the
 * trigger + every handler branch directly. */
void engine_realm_roll_events(uint32_t rng_7bit);

#endif /* OPENLORDS2_ENGINE_REALM_EVENTS_H */
