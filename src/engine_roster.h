/*
 * OpenLords2 — player roster (seat → army-id) table + compactor.
 *
 * Three small strategic-layer functions that maintain the player
 * roster: a fixed table of 6 interleaved {army-id, seat} byte pairs
 * indexed by seat, plus a participant counter.
 *
 *   FUN_0049bbe8 @ 0x49bbe8 ( 53 B) → player_roster_add
 *   FUN_0049bc1d @ 0x49bc1d ( 66 B) → player_roster_clear
 *   FUN_0049bc5f @ 0x49bc5f (314 B) → player_roster_compact
 *
 * The producers run during scenario / map load: the map loader at
 * 0x467770 walks the 64×64 strategic-tile grid, clears the table with
 * player_roster_clear, then calls player_roster_add for every garrisoned
 * tile.  The compactor runs once during new-game / load setup,
 * immediately before the deferred 3 KB consumer at 0x49bd99 (a port
 * carries the literal FUN_<va> token; these neighbours stay bare-hex
 * until they are actually ported).
 *
 * --- Storage --------------------------------------------------------
 *
 *   g_player_roster[12]   (save.h, retail 0x568db0)
 *       6 interleaved {army-id, seat} pairs.  Pair i occupies bytes
 *       [2i] = army-id, [2i+1] = seat-index.  RUNTIME size is 12 B; the
 *       save schema (entry 140) persists only the leading 6 B = the
 *       first 3 pairs.  See quirk #105.
 *
 *   g_player_roster_count  (alias of s_slot_0053e8a4, schema entry 141, u32)
 *       Count of roster entries added with a non-zero seat.  Reset to 0
 *       by the loader before the add loop — NOT by player_roster_clear.
 *
 *   g_player_strategic_352  (save.h, retail 0x57bf00, 6 × 352 B)
 *       The compactor reads two per-player slot-occupancy flags:
 *         [seat·0x160 + 5]  player_has_data        (retail DAT_0057bf05)
 *         [seat·0x160 + 7]  AI opponent-type code  (retail DAT_0057bf07,
 *                           non-zero = slot present)
 *       `active` = number of seats 1..5 with either flag set.
 *
 * Reference: docs/findings/save-format.md §"player roster";
 * engine-quirks #105 (6-vs-12-byte split), #106 (non-convergent compact).
 */

#ifndef OPENLORDS2_ENGINE_ROSTER_H
#define OPENLORDS2_ENGINE_ROSTER_H

#include <stdint.h>

#include "save.h"                 /* g_player_roster, g_player_strategic_352 */
#include "save_schema_globals.h"  /* s_slot_0053e8a4 */

/* Roster participant counter (retail DAT_0053e8a4).  Aliased onto the
 * shared save-schema scalar so the count the producers bump is the same
 * one the save system serialises (schema entry 141). */
#define g_player_roster_count s_slot_0053e8a4

/* Add/overwrite the roster entry for `seat` with `army_id`.  Writes the
 * pair {army_id, seat} at byte offset seat·2, then bumps the participant
 * count when seat != 0 (seat 0 is the reserved front slot).  Port of
 * FUN_0049bbe8.  Callers pass seat ∈ 0..5 (the strategic-tile owner
 * byte); no bounds check, matching retail. */
void player_roster_add(uint8_t army_id, int seat);

/* Zero all 6 roster pairs.  Does NOT touch the count.  Port of
 * FUN_0049bc1d. */
void player_roster_clear(void);

/* Compact the roster: drop entries whose seat index exceeds the number
 * of active strategic-player slots, shifting later entries down.  Port
 * of FUN_0049bc5f.  See quirk #106 — the shift cannot remove the last
 * (index-5) entry, so an out-of-range seat-5 entry spins the pass loop
 * to its 6-pass cap and survives as a trailing duplicate.  Harmless: the
 * consumer reads only the front `active` entries. */
void player_roster_compact(void);

#endif /* OPENLORDS2_ENGINE_ROSTER_H */
