/*
 * OpenLords2 — player roster (seat → army-id) table + compactor
 * (implementation).
 *
 * Verbatim ports of the retail decompile.  Same write order, same loop
 * bounds, same comparison sentinels — including the non-convergent
 * compaction pass (quirk #106) — so the resulting byte state of
 * g_player_roster matches retail exactly.
 *
 * See engine_roster.h for the retail global ↔ storage mapping.
 */

#include "engine_roster.h"

#include "engine.h"     /* g_sim_mp_active (retail DAT_00553030) */
#include "save.h"       /* g_player_aux_44 (retail 0x553d50) */
#include "strutil.h"    /* strutil_bounded_copy (FUN_00401136) */

void player_seat_assign(int player_id, int seat, const void *name)
{
    /* Port of FUN_0049bae9 @ 0x49bae9 (180 B).  Seats a player: marks the
     * strategic slot occupied, flags the aux record, copies the 31-byte
     * name, optionally stamps the id, and (single-player only) mirrors the
     * assigned job-slot id into the aux fixed-job field.  Decompile:
     *
     *   (&DAT_0057bf05)[param_2 * 0x160] = 1;             // player_has_data
     *   (&DAT_00553d77)[param_2 * 0x2c]  = 1;             // aux in-use flag
     *   FUN_00401136(param_3, param_2*0x2c + 0x553d54, 0x1f);  // 31-byte name
     *   if (param_1 != 999) *(int*)(&DAT_00553d50 + param_2*0x2c) = param_1;
     *   if (DAT_00553030 == 0)
     *       (&DAT_00553d75)[param_2*0x2c] = (&DAT_0057bf0a)[param_2*0x160];
     *
     * Callers: single-player setup (0x4410bd, name buffer 0x53f1e0) and
     * the network join handlers (0x4412cc / 0x44121a), which read
     * `player_id` and the name out of the join packet.  `player_id == 999`
     * is the sentinel meaning "leave the existing id untouched".  No bounds
     * check on `seat`; retail has none (callers pass 1..6). */

    g_player_strategic_352[seat * 0x160 + 0x05] = 1;   /* player_has_data */
    g_player_aux_44[seat * 0x2c + 0x27]         = 1;   /* aux in-use flag */

    /* Bounded copy (no NUL), dst/src swapped vs retail's (src, dst, n). */
    strutil_bounded_copy(&g_player_aux_44[seat * 0x2c + 0x04], name, 0x1f);

    if (player_id != 999) {
        /* aux+0: player id/army field (4-byte store), retail DAT_00553d50. */
        *(int32_t *)&g_player_aux_44[seat * 0x2c] = player_id;
    }

    if (g_sim_mp_active == 0) {
        /* Single-player: aux fixed-job-slot id (0x25) ← strategic assigned
         * job-slot id (0x0a).  In multiplayer this is left as-is. */
        g_player_aux_44[seat * 0x2c + 0x25] =
            g_player_strategic_352[seat * 0x160 + 0x0a];
    }
}

void player_roster_add(uint8_t army_id, int seat)
{
    /* Port of FUN_0049bbe8 @ 0x49bbe8 (53 B).  Decompile:
     *
     *   (&DAT_00568db0)[param_2 * 2] = param_1;          // pair[seat].id
     *   (&DAT_00568db1)[param_2 * 2] = (undefined1)param_2;  // pair[seat].seat
     *   if (param_2 != 0) DAT_0053e8a4 = DAT_0053e8a4 + 1;
     *
     * `seat` is the strategic-tile owner byte from the map loader; it
     * doubles as the pair index AND the value stored in the seat field.
     * No bounds check (retail has none); callers pass 0..5. */

    g_player_roster[seat * 2]     = army_id;
    g_player_roster[seat * 2 + 1] = (uint8_t)seat;

    if (seat != 0) {
        g_player_roster_count++;
    }
}

void player_roster_clear(void)
{
    /* Port of FUN_0049bc1d @ 0x49bc1d (66 B).  Zeroes all 6 pairs as a
     * two-store-per-iteration loop; preserved as the loop (rather than a
     * memset) to keep the seam at the single entry point. */

    for (int i = 0; i < 6; i++) {
        g_player_roster[i * 2]     = 0;
        g_player_roster[i * 2 + 1] = 0;
    }
}

void player_roster_compact(void)
{
    /* Port of FUN_0049bc5f @ 0x49bc5f (314 B).
     *
     * Step 1: count active strategic-player slots (seats 1..5).  A slot
     * is active when either occupancy flag is set:
     *   (&DAT_0057bf05)[seat * 0x160] != 0   — player_has_data
     *   (&DAT_0057bf07)[seat * 0x160] != 0   — AI opponent-type code
     */
    int active = 0;
    for (int seat = 1; seat < 6; seat++) {
        const uint8_t *slot = &g_player_strategic_352[seat * 0x160];
        if (slot[0x05] != 0 || slot[0x07] != 0) {
            active++;
        }
    }

    /* Step 2: bubble out any pair whose seat index exceeds `active`,
     * shifting the trailing pairs down one slot.  Retail's pass counter
     * runs up to 6 passes and stops early once a full pass makes no
     * change (`bVar2`).
     *
     * Quirk #106 — non-convergent tail.  The shift copies pair[j+1] into
     * pair[j] for j = i..4, so it can never remove pair[5] (when i == 5
     * the j-loop body never runs).  An out-of-range seat-5 entry is
     * therefore re-detected every pass, keeps `changed` true, and the
     * loop spins to the full 6-pass cap leaving the entry in place as a
     * trailing duplicate.  The consumer (at 0x49bd99) reads only the
     * front `active` entries, so this is harmless — but the surviving
     * bytes are part of the persisted state, so it is ported verbatim. */
    int changed = 1;
    for (int pass = 0; changed && pass < 6; pass++) {
        changed = 0;
        for (int i = 1; i < 6; i++) {
            if (active < (int)g_player_roster[i * 2 + 1]) {  /* pair[i].seat > active */
                for (int j = i; j < 5; j++) {
                    g_player_roster[j * 2]     = g_player_roster[(j + 1) * 2];
                    g_player_roster[j * 2 + 1] = g_player_roster[(j + 1) * 2 + 1];
                }
                changed = 1;
            }
        }
    }
}
