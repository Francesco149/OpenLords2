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

/* The 27 per-turn passes the assembled driver sequences, in their owning
 * modules.  engine_turn_run_passes is the integrating chip — it pulls in
 * every realm / economy / territory / combat / ranking turn pass. */
#include "engine_combat.h"        /* mp_resign_sweep (0049b2a3)            */
#include "engine_ranking.h"       /* snapshot_player_metrics (004486a9)    */
#include "engine_realm_events.h"  /* roll_events (00448819)                */
#include "engine_realm.h"         /* the engine_realm turn passes          */
#include "engine_realm_upkeep.h"  /* pay_upkeep (004acbd4)                 */
#include "engine_territory.h"     /* census / workforce passes             */
#include "engine_army_economy.h"  /* season / stock-A / resource passes    */

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

/* Assembled per-turn driver — the deterministic tail of FUN_00448440.
 * One straight transcription of the decompiled call sequence
 * (docs/decompiled/by-address/448440.c): the inline calendar block, then
 * the 27 passes in retail order.  The head two calls (AI brain + RNG
 * stir) are run by the engine.c wrapper before this; see the header. */
void engine_turn_run_passes(engine_event_queue_state_t *evq,
                            uint32_t rng_7bit_b,
                            uint32_t rng_7bit_a,
                            int mp_active)
{
    /* calendar/season-rollover (the inline block at 0x448440's top, after
     * the AI/RNG head — turn counter ++, season/year rotation). */
    engine_turn_advance_calendar();

    engine_combat_mp_resign_sweep(evq, mp_active);     /* 0049b2a3 */
    engine_ranking_snapshot_player_metrics();          /* 004486a9 */
    engine_realm_roll_events(rng_7bit_b);              /* 00448819 */
    engine_realm_update_mood(rng_7bit_b, rng_7bit_a, evq); /* 00449889 */
    engine_realm_accrue_income();                      /* 0044b59b */
    engine_realm_pay_upkeep(evq);                      /* 004acbd4 */
    engine_realm_settle_provisions();                  /* 0044bf04 */
    engine_realm_apply_tax_approval();                 /* 0044bd9f */
    engine_realm_advance_rating();                     /* 0044baea */
    engine_realm_escalate_unrest(evq);                 /* 0044aa41 */
    engine_realm_coalition_pass(evq, mp_active);       /* 0044ae3c */
    engine_territory_census_all_realms();              /* 00469b51 */
    engine_realm_spread_development();                 /* 0044bf7a */
    engine_army_economy_apply_season_pass();           /* 0044c8ae */
    engine_army_economy_settle_stock_a();              /* 0044d60d */
    engine_army_economy_resource_pass();               /* 0044e852 */
    engine_realm_complete_settlements(evq);            /* 004508de */
    engine_territory_allocate_all_workforce();         /* 0044f699 */
    engine_realm_migrate_population();                 /* 0044a6ba */
    engine_realm_advance_population();                 /* 00449ef3 */
    engine_realm_recount_armies();                     /* 00451061 */
    engine_turn_hook_noop();                           /* 00428471 */
    engine_realm_collect_territory_income();           /* 004ad6c0 */
    engine_realm_clear_turn_scratch();                 /* 00448d16 */
    engine_territory_allocate_all_workforce();         /* 0044f699 (2nd) */
    engine_realm_record_history();                     /* 004ae7dd */
    engine_realm_replan_economies();                   /* 00448545 */
}
