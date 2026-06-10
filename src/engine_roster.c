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
#include "save_schema_globals.h" /* s_slot_0057c8cc / s_slot_00553fbc */
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

void player_first_active_nonlocal(void)
{
    /* Port of FUN_004a1e6c @ 0x4a1e6c (117 B).  Scans player slots 1..5
     * of g_player_strategic_352 (stride 0x160) for the first that is both
     * occupied (the +0x4 F_ACTIVE flag set) and not the local player
     * (s_slot_0057c8cc, entry 72), and stores that slot in s_slot_00553fbc
     * (entry 143).  If no such slot exists the result is 0.
     *
     *   for (slot = 1; slot <= 5; slot++)
     *       if (active[slot] && local != slot) { result = slot; return; }
     *   result = 0;
     *
     * Two callers: the new-game per-player record init (0x4a1c53, via
     * game_init) and a render/UI path (0x416cf3) — hence a shared roster
     * helper rather than living in game_init.  Verbatim: slot 0 (the
     * reserved front slot) is never considered. */
    for (int slot = 1; slot < 6; slot++) {
        if (g_player_strategic_352[slot * 0x160 + 0x04] != 0 &&
            (int)s_slot_0057c8cc != slot) {
            s_slot_00553fbc = (uint32_t)slot;
            return;
        }
    }
    s_slot_00553fbc = 0;
}

/* Port of FUN_0043e9e2 @ 0x43e9e2 (223 B).  Session-start player
 * activation reset, run from the load tail (FUN_004976a1) right after
 * the turn-progress reset (and from the MP session paths, 0x49c995
 * band).  Zeroes the per-player aux ints (g_player_aux_44 +0, players
 * 1..5), DEACTIVATES every human-flagged slot (+5 != 0 → +4 = 0; in a
 * loaded SP session the other ex-human seats go dormant), then
 * re-activates the current player and stamps its aux int with the
 * session stamp (retail DAT_004e2a28 — a DirectPlay-session-derived
 * value, 0x4b7a9a writes `dplay_handle() + 1`; .data ships 0x6578).
 * The port has no DirectPlay layer, so it PRESERVES the loaded save's
 * own stamp through the reset instead: the save was written by the
 * same session code, so retail's re-stamp reproduces the saved value —
 * re-stamping the loaded value is byte-equivalent without modelling
 * dplay (state-oracle-gated; documented drop-in deviation).  The
 * MP-only autosave broadcast (0x43eda0(0xf,0)) is a no-op in SP and
 * stays bare-hex. */
void player_session_activate_current(void)
{
    int cur = (int)(s_slot_0057c8cc & 7);
    int32_t session_stamp = *(int32_t *)(g_player_aux_44 + cur * 0x2c);
    for (int p = 1; p < 6; p++)
        *(int32_t *)(g_player_aux_44 + p * 0x2c) = 0;
    for (int p = 1; p < 6; p++) {
        if (g_player_strategic_352[p * 0x160 + 0x05] != 0)
            g_player_strategic_352[p * 0x160 + 0x04] = 0;
    }
    *(int32_t *)(g_player_aux_44 + cur * 0x2c) = session_stamp;
    g_player_strategic_352[cur * 0x160 + 0x04] = 1;
    /* if (MP active) 0x43eda0(0xf, 0) — MP-only, no-op in SP. */
}

/* Port of FUN_0049ca32 @ 0x49ca32 (46 B).  Seed the five per-player
 * visibility bitmasks with their one-hot values 1/2/4/8/0x10 for
 * players 1..5.  Run from the load tail after the descriptor rebuild.
 *
 * Retail is five direct stores 0x57c14e/0x57c2ae/0x57c40e/0x57c56e/
 * 0x57c6ce = block-3 bytes 0x24e + (p-1)*0x160 — i.e. player p's bit
 * lives at STRATEGIC RECORD p, offset +0xee... shifted one record up
 * (0x24e = record 1 + 0xee).  All five land inside the 2112-byte
 * block.  An earlier transcription used p*0x160 + 0x24e (one record
 * further still): records 2..5 got the wrong one-hot values — the
 * whole block-3 state-ribbon residual (sub-ranges 7/9/12/15) — and
 * the p=5 store ran 238 bytes past the block into whatever the
 * linker placed next (g_player_aux_44[238], the block-2 player-5
 * name residual).  Caught by the trace-studio save-schema oracle +
 * a -Warray-bounds warning that was dismissed as pre-existing. */
void player_seed_visibility_bits(void)
{
    for (int p = 1; p < 6; p++)
        g_player_strategic_352[(p - 1) * 0x160 + 0x24e] =
            (uint8_t)(1 << (p - 1));
}
