/*
 * OpenLords2 — end-of-turn readiness check (implementation).
 *
 * Pure-C port of retail FUN_0049b762 @ 0x49b762.  Same loop bounds,
 * same branch order, same writeback semantics — so the resulting
 * global state matches retail bit-for-bit.
 *
 * See engine_turn.h for the retail global ↔ field mapping and the
 * three-state handshake description.
 */

#include "engine_turn.h"
#include "save_schema_globals.h"  /* s_slot_<va> — calendar scalars */
#include "io_path.h"              /* io_path_probe_cd_audio (FUN_004aef7e) */

#include <string.h>   /* memcpy — aligned int read without aliasing UB */

#define PLAYER_STRIDE      0x160   /* 352 B per strategic record */
#define READY_DONE         999     /* ready-value sentinel: turn finished */

/* Canonical storage for the turn-handshake runtime globals declared in
 * engine_turn.h.  Default 0 at .bss-init; re-zeroed at new-game by the
 * scalar-seed phase.  The pure check function below operates on
 * snapshots, not these globals — engine.c does the per-tick marshalling
 * (it owns the live read/write; the storage just lives in this module). */
int      g_sim_turn_progress;          /* DAT_00553fc8 */
uint32_t g_sim_turn_sync_baseline_ms;  /* _DAT_0056d658 */

void engine_turn_end_check(const engine_turn_end_in_t *in,
                           engine_turn_end_out_t *out)
{
    /* Carry-through defaults: paths that skip the turn-sync init block
     * leave the state globals at their prior values (the wrapper writes
     * them back unconditionally, so an echo is a no-op). */
    out->advance              = 0;
    out->new_turn_progress    = in->turn_progress;
    out->new_timeout_reached  = in->timeout_reached;
    out->saved_timeout        = in->saved_timeout;
    out->new_sync_baseline_ms = in->sync_baseline_ms;
    out->clear_ready_flags    = 0;

    /* (1) Readiness scan — roster slots 1..5 (slot 0 unused; retail's
     * loop is `local_c < 6`, i.e. it stops at 5, unlike the sibling
     * min-tick walker FUN_0043e330 which runs 1..6). */
    int not_ready = 0;
    for (int i = 1; i < 6; i++) {
        const uint8_t *slot = &in->players[i * PLAYER_STRIDE];
        int ready_value;
        memcpy(&ready_value, slot, sizeof(ready_value));   /* *(int*)(slot+0) */
        if (slot[4] != 0 && ready_value < READY_DONE)
            not_ready++;
    }
    out->not_ready_count = not_ready;

    if (not_ready != 0)
        return;                       /* someone still mid-turn */

    /* (2) Everyone done. */
    if (in->mp_active == 0) {         /* single-player → advance */
        out->advance = 1;
        return;
    }
    if (in->active_count == 1) {      /* MP, alone → advance */
        out->advance = 1;
        return;
    }

    /* MP turn-sync handshake (DAT_00553fc8). */
    int progress = in->turn_progress;
    if (progress == 0) {
        /* Arm: clear ready flags, snapshot timeout, advance to state 1,
         * set the sync baseline 10 s in the past so the periodic
         * broadcast (0x49b8b0's `9999 < now - baseline` gate) fires
         * on the very next poll. */
        out->clear_ready_flags    = 1;
        out->new_turn_progress    = 1;
        progress                  = 1;
        out->saved_timeout        = in->timeout_reached;
        out->new_timeout_reached  = 0;
        out->new_sync_baseline_ms = in->now_ms - 10000;
    }
    if (progress == 2) {              /* peers acked → finalise, advance */
        out->new_turn_progress = 0;
        out->advance           = 1;
    }
    /* progress == 1 falls through: advance stays 0 */
}

void engine_turn_advance_calendar(void)
{
    /* Snapshot the season-next *before* the rotation: its old value is
     * what the year-rollover branch tests (retail `iVar1 = DAT_0057c92c`
     * read at the top, checked as `iVar1 == 4` after the season write). */
    uint32_t season_next_old = s_slot_0057c92c;

    s_slot_00553240 += 1;                       /* turn counter ++        */

    s_slot_00554470  = s_slot_0057c934;          /* season prev = cur      */
    s_slot_0057c934  = s_slot_0057c92c;          /* season cur  = next     */
    s_slot_0057c92c += 1;                        /* season next ++         */
    if (s_slot_0057c92c > 4)                     /* wrap to the 1..4 range */
        s_slot_0057c92c = 1;

    if (season_next_old == 4) {                  /* year rollover          */
        s_slot_0055cd5c += 1;                    /* rollover counter ++    */
        s_slot_0057c900  = s_slot_00553edc;      /* year prev = cur        */
        s_slot_00553edc  = s_slot_00553e74;      /* year cur  = next       */
        s_slot_00553e74 += 1;                    /* year next ++ (no wrap) */
        io_path_probe_cd_audio();                /* FUN_004aef7e           */
    }
}

/* retail FUN_00428471 @ 0x428471 — empty turn-driver hook (no body). */
void engine_turn_hook_noop(void)
{
}
