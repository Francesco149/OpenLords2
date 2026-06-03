/*
 * OpenLords2 — new-game / world-state initialiser (0x497ced cluster).
 * See game_init.h for the phase plan.
 */

#include <stdint.h>
#include <string.h>

#include "game_init.h"
#include "engine.h"               /* engine_rng_step, engine_rng_in/out_t */
#include "save.h"
#include "save_schema_globals.h"  /* s_slot_<va> — the 17 calendar/world scalars */
#include "engine_turn.h"          /* g_sim_turn_progress, g_sim_turn_sync_baseline_ms */
#include "engine_roster.h"        /* player_first_active_nonlocal (FUN_004a1e6c) */
#include "engine_map_query.h"     /* box predicates for the army-coord probes */
#include "engine_territory.h"     /* entity placer FUN_0046e1b0 (army starts) */
#include "engine_army_economy.h"  /* provision / plan_realm (per-realm economy) */
#include "engine_army_state.h"    /* per-tick army-state machine cell binding   */
#include "engine_army_move.h"     /* army-movement cell-evaluator binding       */
#include "engine_combat.h"        /* combat_b seam (engine_combat_contest_region)*/
#include "engine_npc_schedule.h"  /* engine_npc_schedule_init (FUN_0049caaa)     */
#include "engine_strat_cost.h"    /* engine_strat_cost_rebuild (FUN_0046ff43)    */
#include "engine_event_queue.h"   /* engine_event_queue_init_wrapper (FUN_00472ad0) */
#include "engine_ranking.h"       /* engine_ranking_recompute (FUN_0049aa0e)     */
#include "engine_map_init.h"      /* engine_map_init_tile_cells                  */
#include "engine_map_terrain.h"   /* engine_map_terrain_set_empty_redraw_gate    */
#include "engine_category_buckets.h" /* the strategic category-bucket digest      */
#include "engine_title_render.h"   /* engine_title_render_tab_value (config tabs)  */

/* Fixed-length record zeroer — retail FUN_0046ea28 (len 0x300),
 * FUN_0046eb09 (len 0x14), FUN_0046ead0 (len 0x1a4) and FUN_0046ea61
 * (len 0x160) are four instances of this same `memset(base, 0, len)`
 * leaf, one per block stride.  Each retail clearer below calls its
 * matching length. */
static void clear_record(void *base, size_t len)
{
    memset(base, 0, len);
}

/* FUN_0046f1dc — clear the 17 per-army records (stride 0x300). */
static void clear_per_army(void)
{
    for (int i = 0; i < 0x11; i++)               /* i < 17 */
        clear_record(&g_per_army_768[i * 0x300], 0x300);  /* FUN_0046ea28 */
}

/* FUN_0046f2ae — clear the 13 unit-type records (stride 0x14). */
static void clear_unit_table(void)
{
    for (int i = 0; i < 0xd; i++)                /* i < 13 */
        clear_record(&g_unit_table_20[i * 0x14], 0x14);   /* FUN_0046eb09 */
}

/* FUN_0046f3cd — clear the 151 per-territory records (stride 0x1a4). */
static void clear_territories(void)
{
    for (int i = 0; i < 0x97; i++)               /* i < 151 */
        clear_record(&g_per_territory_420[i * 0x1a4], 0x1a4); /* FUN_0046ead0 */
}

/* Table-clear phase of 0x497ced — the first three calls of the
 * new-game initialiser, in retail order (territories, unit-table,
 * per-army).  NB: 0x497ced issues them as
 *   FUN_0046f3cd(); FUN_0046f2ae(); FUN_0046f1dc();
 * so the territory block is cleared first. */
void engine_game_init_clear_tables(void)
{
    clear_territories();   /* FUN_0046f3cd */
    clear_unit_table();    /* FUN_0046f2ae */
    clear_per_army();      /* FUN_0046f1dc */
}

/* Provisional home for retail DAT_0053f0c4 — see game_init.h. */
int g_anim_gate_53f0c4;

/*
 * Scalar-seed phase of 0x497ced — the 20 dword world-state globals
 * written between the NPC-schedule seed (FUN_0049caaa, ported) and the
 * first heavy seeder (0x4676e0, unported).  Writes are issued in retail
 * order; all
 * 20 target distinct addresses, so the order is provenance, not a
 * dependency.
 *
 * Three storage classes:
 *   - 17 are save-schema scalars → their shared s_slot_<va> homes.
 *   - 2 are the turn-handshake runtime globals owned by engine_turn.c.
 *   - 1 is the render/anim gate counter parked in this module.
 *
 * The retail asm derives several values from a base via inc/dec rather
 * than as independent constants — preserved here as expressions so the
 * structure stays legible.  Two parallel calendar triples:
 *
 *     year   base = 0x4f3 (1267)   prev = base-1 (1266)   next = base+1 (1268)
 *     season base = 3              prev = base-1 (2)      next = base+1 (4)
 *
 * plus a same-valued copy of each base (save entries 28/29).  The
 * season-next write has a retail clamp `if (next > 4) next = 1` (a 1..4
 * seasonal wrap) that is dead at the seeded value 4 — the decompiler
 * folds the unreachable block away; noted for provenance, not
 * reproduced.  Save-entry numbers in the trailing comments index the
 * contiguous calendar block (entries 20-31).
 */
void engine_game_init_seed_scalars(void)
{
    /* Turn-handshake runtime state (engine_turn.c) — reset to idle. */
    g_sim_turn_progress         = 0;            /* DAT_00553fc8 */
    g_sim_turn_sync_baseline_ms = 0;            /* _DAT_0056d658 */

    /* Render/anim gate counter (reader 0x41e1dd, unported). */
    g_anim_gate_53f0c4 = 0;                     /* DAT_0053f0c4 */

    s_slot_0055cd5c = 0;                        /* entry 30 */

    /* Calendar block — year triple + copy. */
    s_slot_00553edc = 0x4f3;                    /* entry 20: year base (1267) */
    s_slot_00553228 = s_slot_00553edc;          /* entry 28: year copy */
    s_slot_0057c900 = s_slot_00553edc - 1;      /* entry 21: year prev (1266) */
    s_slot_00553e74 = s_slot_00553edc + 1;      /* entry 22: year next (1268) */

    s_slot_00553240 = 0;                        /* entry 31 */
    s_slot_0056d6b0 = 0;                        /* entry 92 */

    /* Calendar block — season triple + copy. */
    s_slot_0057c934 = 3;                        /* entry 23: season base */
    s_slot_0057c990 = s_slot_0057c934;          /* entry 29: season copy */
    s_slot_00554470 = s_slot_0057c934 - 1;      /* entry 24: season prev (2) */
    s_slot_0057c92c = s_slot_0057c934 + 1;      /* entry 25: season next (4);
                                                 *   retail clamp >4→1 dead here */

    s_slot_0055cd40 = 0;                        /* entry 26 */
    s_slot_0057c918 = 0;                        /* entry 27 */
    s_slot_0057a0c4 = 0;                        /* entry 175 */
    s_slot_0053e920 = 0;                        /* entry 176 */
    s_slot_0056d5e0 = 0;                        /* entry 177 */
    s_slot_0055ce7c = 6;                        /* entry 190 */
}

/*
 * Leaf seeder FUN_0042847c — seed the 16-entry value table that lives at
 * save entry 82 (s_slot_005532a0[256], laid out as 16 records of 16 B).
 *
 * For each record i: dword at +0 ← k_value_table_defaults[i], dword at
 * +4 ← 0.  Bytes +8..+15 of every record are left untouched — retail
 * writes only the two dwords (the table-clear phase or the prior save
 * state owns the rest).
 *
 * Record `i` is later consumed via its first dword, indexed by the
 * current-selection cursor DAT_0056d8ac (render reader 0x41608b /
 * 0x428daf, getters 0x4a4a3f / 0x4a4b12 / 0x4a4c41).  The defaults come
 * from the retail .rdata table at DAT_004d8910 (16 dwords), extracted
 * byte-for-byte from the PE.
 *
 * Unlike the scalar-seed phase, this seeder has four retail callers
 * (0x497ced new-game, plus 0x43723a / 0x49a010 / 0x4a98fe), so it is a
 * reusable "reset table-82 to defaults" rather than a one-shot.
 */
static const uint32_t k_value_table_defaults[16] = {
    0x00, 0x02, 0x0c, 0x00,  0x01, 0x00, 0x01, 0x02,
    0x01, 0x0d, 0x10, 0x0a,  0x18, 0x17, 0x2c, 0x00,
};

void engine_game_init_seed_value_table(void)
{
    for (int i = 0; i < 0x10; i++) {            /* 16 records, stride 0x10 */
        uint8_t *rec = &s_slot_005532a0[i * 0x10];
        *(uint32_t *)(rec + 0) = k_value_table_defaults[i];  /* DAT_004d8910[i] */
        *(uint32_t *)(rec + 4) = 0;
    }
}

/*
 * RNG draw helper — one retail FUN_00404a46 advance, threading the live
 * LFSR state through `rng`, returning the 7-bit-b mirror (retail reads
 * DAT_0058fd60 after each advance — the only field FUN_00497e65 samples).
 * Sub-masking that 7-bit value by 3 / 0x1f / 7 below is identical to
 * masking the raw state, since every mask is a subset of 0x7f.
 */
static uint32_t rng_next_7b(engine_rng_in_t *rng)
{
    engine_rng_out_t out = {0};
    engine_rng_step(rng, &out);          /* FUN_00404a46 */
    rng->state_a = out.new_state_a;
    rng->state_b = out.new_state_b;
    return out.rng_7bit_b;               /* DAT_0058fd60 */
}

/*
 * Heavy seeder FUN_00497e65 @ 0x497e65 (1025 B) — new-game randomiser.
 * See game_init.h for the three-output overview.  Ported verbatim from
 * the decompile (r2-verified: masks 3 / 0x1f / 7, the `+1` target bias,
 * the 0x30 wrap, the 0x32 probe cap, and the value ladder eax·5+6).
 *
 * The single retail function is kept as a single C function because the
 * three phases share one RNG stream and their draw ordering is part of
 * the persisted result: phase 1 consumes `active` draws, phase 2 another
 * 48, phase 3 a final 48.
 */
void engine_game_init_seat_shuffle(engine_rng_in_t *rng)
{
    uint8_t saved_id[6];
    uint8_t saved_seat[6];

    /* --- Phase 1: roster shuffle ---------------------------------- */

    /* Count occupied pairs among seats 1..5 (seat 0 excluded). */
    int active = 0;
    for (int i = 1; i < 6; i++)
        if (g_player_roster[i * 2] != 0)
            active++;

    /* Snapshot all 6 pairs, then clear only the id bytes (the seat
     * bytes are left in place — retail clears DAT_00568db0 only). */
    for (int i = 0; i < 6; i++) {
        saved_id[i]   = g_player_roster[i * 2];
        saved_seat[i] = g_player_roster[i * 2 + 1];
    }
    for (int i = 0; i < 6; i++)
        g_player_roster[i * 2] = 0;

    /* Re-seat each originally-occupied pair (1..active) into a random
     * empty slot, probing forward with a 1..active wrap. */
    for (int i = 1; i <= active; i++) {
        int t = (int)(rng_next_7b(rng) & 3) + 1 + i;
        if (t > active) t = 1;
        for (int probe = 0; probe < active; probe++) {
            if (g_player_roster[t * 2] == 0) {
                g_player_roster[t * 2]     = saved_id[i];
                g_player_roster[t * 2 + 1] = saved_seat[i];
                break;
            }
            if (++t > active) t = 1;
        }
    }

    /* --- Phase 2: 48-entry permutation table (entry 178) ---------- */

    /* Retail fills an identity scratch [0..47] then clears the table;
     * the identity value is just the index, inlined as `i` below. */
    for (int i = 0; i < 0x30; i++)
        s_slot_0057cae0[i] = 0;

    for (int i = 0; i < 0x30; i++) {
        int t = (int)(rng_next_7b(rng) & 0x1f) + 1 + i;
        if (t > 0x2f) t -= 0x30;
        for (int probe = 0; probe < 0x32; probe++) {   /* 50-attempt cap */
            if (t != i && s_slot_0057cae0[t] == 0) {
                s_slot_0057cae0[t] = (uint8_t)i;        /* identity value */
                break;
            }
            if (++t > 0x2f) t = 0;
        }
    }

    /* --- Phase 3: 48-entry value/threshold table (entry 191) ------ */

    for (int i = 0; i < 0x30; i++) {
        uint32_t r = rng_next_7b(rng) & 7;
        if (r == 5)
            s_slot_00553080[i] = 0;
        else if (r == 6)
            s_slot_00553080[i] = 100;
        else
            s_slot_00553080[i] = (uint8_t)(r * 5 + 6);  /* lea eax,[eax+eax*4]; add 6 */
    }
}

/*
 * Leaf seeder FUN_004ac904 @ 0x4ac904 (295 B) — seed the unit/opponent-
 * type table (save entry 6, g_unit_table_20, 13 records × 20 B) from a
 * bank of retail .rdata const columns.
 *
 * The row count is config-driven: the scenario/config selector
 * s_slot_0056d5dc (save entry 77) indexes k_unit_count_by_config; the
 * looked-up count is clamped to 1..12 and persisted to s_slot_00554030
 * (save entry 122 — this function is its sole writer).  The clamp
 * (`>12 → 1`, `<0 → 1`) is dead for any valid scenario index 0..19 (the
 * table caps at 12); it is retail's defence against an out-of-range
 * selector reading past the table, ported verbatim.
 *
 * After clearing the whole block (retail re-issues FUN_0046f2ae here,
 * the same unit-table clearer the table-clear phase runs), record i
 * (1..count) is filled from six parallel const columns.  The retail
 * .rdata lays the columns out so each one's entry[0] aliases the
 * previous column's entry[12] (col_e[12] and col_f[0] are the literal
 * same dword at 0x4de7ec); only entries 1..count are ever read, so the
 * entry-0 slots below hold the real overlap bytes purely for a faithful
 * copy.  Record 0 stays cleared (the loop is 1-based).
 *
 * Two columns are each written to two adjacent record fields (col_a →
 * +0x2 and +0x4; col_c → +0x6 and +0x7) — retail duplicates the source
 * byte; preserved.  Bytes +0x0/+0x1/+0x3 of every record are left
 * cleared (retail writes only the eight fields below).  All columns
 * extracted byte-for-byte from the PE .rdata.
 */
static const int32_t k_unit_count_by_config[20] = {   /* DAT_004de820 */
    0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12, 12, 12, 12, 12,
};
/* entry[0] = the real overlap byte/dword; rows used are [1..count]. */
static const uint8_t  k_unit_col_a[13] = {            /* DAT_004de79f → +0x2,+0x4 */
    0x00, 0x01, 0x02, 0x04, 0x05, 0x07, 0x08, 0x0a, 0x0b, 0x0d, 0x0e, 0x10, 0x11,
};
static const uint8_t  k_unit_col_b[13] = {            /* DAT_004de78f → +0x5 */
    0x00, 0x04, 0x04, 0x05, 0x05, 0x03, 0x03, 0x01, 0x01, 0x02, 0x02, 0x06, 0x06,
};
static const uint8_t  k_unit_col_c[13] = {            /* DAT_004de7af → +0x6,+0x7 */
    0x00, 0x05, 0x03, 0x06, 0x04, 0x02, 0x07, 0x05, 0x02, 0x01, 0x03, 0x06, 0x07,
};
static const uint32_t k_unit_col_d[13] = {            /* DAT_004de75c → +0x8 */
    0x62, 0x64, 0xc8, 0x96, 0xc8, 0xc8, 0x64, 0x64, 0xc8, 0x96, 0xfa, 0x32, 0x64,
};
static const uint32_t k_unit_col_e[13] = {            /* DAT_004de7bc → +0xc */
    0x0000, 0x0708, 0x0dac, 0x0bb8, 0x0fa0, 0x157c, 0x0a8c,
    0x0bb8, 0x1770, 0x076c, 0x0c1c, 0x0a8c, 0x157c,
};
static const uint32_t k_unit_col_f[13] = {            /* DAT_004de7ec → +0x10 */
    0x157c, 0x00b4, 0x015e, 0x012c, 0x0190, 0x0226, 0x010e,
    0x012c, 0x0258, 0x00be, 0x0136, 0x010e, 0x0226,
};

void engine_game_init_seed_unit_table(void)
{
    int count = k_unit_count_by_config[s_slot_0056d5dc];
    if (count > 0xc) count = 1;                  /* dead for valid selector */
    if (count < 0)   count = 1;                  /*   "    "    "      "     */
    s_slot_00554030 = (uint32_t)count;           /* persist (entry 122) */

    clear_unit_table();                          /* FUN_0046f2ae */

    for (int i = 1; i <= count; i++) {
        uint8_t *rec = &g_unit_table_20[i * 0x14];   /* stride 0x14 */
        *(uint32_t *)(rec + 0x8)  = k_unit_col_d[i];
        rec[0x5]                  = k_unit_col_b[i];
        rec[0x2]                  = k_unit_col_a[i];
        rec[0x4]                  = k_unit_col_a[i];  /* dup of +0x2 */
        rec[0x6]                  = k_unit_col_c[i];
        rec[0x7]                  = k_unit_col_c[i];  /* dup of +0x6 */
        *(uint32_t *)(rec + 0xc)  = k_unit_col_e[i];
        *(uint32_t *)(rec + 0x10) = k_unit_col_f[i];
    }
}

/*
 * Init-only seeder FUN_0049b35e @ 0x49b35e (205 B) — purge the strategic
 * record of any player slot that is unoccupied or only half-initialised.
 *
 * Walks player slots 1..5 of g_player_strategic_352 (save entry 3, stride
 * 0x160; slot 0 — the reserved front slot — is skipped) and zeroes the
 * whole 0x160-byte record (retail FUN_0046ea61, the len-0x160 instance of
 * the fixed-length zeroer) when either:
 *
 *   - the slot-occupied flag (+0x4) is clear, OR
 *   - the slot is occupied but neither the human-data flag (+0x5) nor the
 *     AI opponent-type code (+0x7) is set.
 *
 * The three field offsets are the same ones engine_ranking.c names
 * F_ACTIVE / F_HAS_DATA / F_OPP_TYPE; re-stated locally here to keep this
 * module self-contained (they are private byte offsets, not a shared
 * symbol).  Runs once at new-game (sole caller 0x497ced) to scrub any
 * leftover/garbage player state ahead of the heavy seeders.
 */
#define STRAT_REC(slot)   (&g_player_strategic_352[(slot) * 0x160])
#define STRAT_ACTIVE      0x04   /* slot-occupied flag */
#define STRAT_HAS_DATA    0x05   /* human player_has_data flag */
#define STRAT_OPP_TYPE    0x07   /* AI opponent-type code */

void engine_game_init_purge_inactive_records(void)
{
    for (int slot = 1; slot < 6; slot++) {
        uint8_t *rec = STRAT_REC(slot);
        if (rec[STRAT_ACTIVE] == 0)
            clear_record(rec, 0x160);                       /* FUN_0046ea61 */
        else if (rec[STRAT_HAS_DATA] == 0 && rec[STRAT_OPP_TYPE] == 0)
            clear_record(rec, 0x160);                       /* FUN_0046ea61 */
    }
}

/*
 * FUN_004a2584 (157 B) — selective clear of the 6×5 grid of 8-B elements
 * at save entry 146 (s_slot_0053f0f0, 240 B).  Per element it zeroes
 * bytes +0/+1/+2 and the dword at +4, leaving byte +3 untouched.  Sole
 * caller is the per-player reset below; kept as a static helper. */
static void clear_grid_53f0f0(void)
{
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 5; col++) {
            uint8_t *e = &s_slot_0053f0f0[row * 0x28 + col * 8];
            e[0] = 0;
            e[1] = 0;
            e[2] = 0;                         /* byte +3 left untouched */
            *(uint32_t *)(e + 4) = 0;
        }
    }
}

/*
 * Init-only seeder FUN_004a1c53 @ 0x4a1c53 (537 B) — reset the per-player
 * sub-tables before the new game starts.  Sole caller 0x497ced.
 *
 * Three steps:
 *   1. s_slot_0057a0f8 (entry 144) = 0.
 *   2. clear the entry-146 grid (FUN_004a2584) and recompute the
 *      first-active-non-local-player cursor (FUN_004a1e6c, now the shared
 *      roster helper player_first_active_nonlocal).
 *   3. for each player slot 1..5 of g_player_strategic_352 (stride 0x160;
 *      slot 0 skipped), reset a 5-entry sub-table at record offset 0x94
 *      (five 0x10-B elements, sub = 1..5) plus two standalone bytes
 *      (+0x81, +0xeb).
 *
 * Each sub-element's lead byte (+0x0) is seeded to 5 for an AI player
 * (occupied with the F_ACTIVE flag AND no human-data flag) and 0
 * otherwise; +0xd is set to 1; the remaining written fields (+0x1..+0x5,
 * the dword +0x8, +0xc, and the word +0xe) are zeroed.  Bytes +0x6/+0x7
 * of every element are left as they were.  Field semantics are
 * provisional — consumers index the sub-table through a computed pointer
 * (no static field xrefs) — so it is ported byte-exact from the
 * r2-confirmed write set rather than named.
 */
void engine_game_init_reset_player_subtables(void)
{
    s_slot_0057a0f8 = 0;                          /* entry 144 */
    clear_grid_53f0f0();                          /* FUN_004a2584 */
    player_first_active_nonlocal();               /* FUN_004a1e6c */

    for (int slot = 1; slot < 6; slot++) {
        uint8_t *rec = STRAT_REC(slot);

        /* AI players (active, no human data) seed the lead byte to 5. */
        uint8_t lead = (rec[STRAT_ACTIVE] == 0 || rec[STRAT_HAS_DATA] != 0)
                           ? 0 : 5;

        rec[0xeb] = 0;
        rec[0x81] = 0;

        for (int sub = 1; sub < 6; sub++) {
            uint8_t *e = rec + 0x84 + sub * 0x10; /* element base (rec+0x94..) */
            e[0x0]               = lead;          /* AI → 5, else 0 */
            *(uint32_t *)(e + 8) = 0;
            e[0xd]               = 1;
            e[0x4]               = 0;
            e[0x1]               = 0;
            e[0x3]               = 0;
            e[0x2]               = 0;
            e[0x5]               = 0;
            e[0xc]               = 0;
            *(uint16_t *)(e + 0xe) = 0;           /* bytes +0x6/+0x7 untouched */
        }
    }
}

/* Init-only seeder FUN_004ae74d — reset the per-realm stat-history ring at
 * new-game.  g_realm_history (save entry 10, retail 0x56d8c0) is a 400-turn ×
 * 16-realm ring of 8-byte records (6400 total); the per-turn producer
 * FUN_004ae7dd (engine_realm_record_history) writes each realm's population
 * (+0) and approval (+4) into the current turn-slot, leaving +5..+7 alone.
 * This seeder zeroes the +0 dword and the +4 byte of every record, addressing
 * the buffer as 400 rows × 16 records (row stride 0x80, record stride 8) with a
 * base pointer of entry10-8 and a 1-based column, so col 1..16 maps to record
 * offsets 0..0x78.  Then the three ring cursors (write index / oldest page /
 * count) are zeroed.  Sole caller is the orchestrator 0x497ced (init-only). */
void engine_game_init_clear_realm_history(void)
{
    for (int row = 0; row < 400; row++) {
        for (int col = 1; col < 0x11; col++) {    /* 1..16 (retail 1-based) */
            /* retail: mov dword [row*0x80 + col*8 + (entry10-8)], 0
             *         mov byte  [row*0x80 + col*8 + (entry10-4)], 0 */
            uint8_t *rec = g_realm_history + row * 0x80 + (col - 1) * 8;
            memset(rec, 0, 5);   /* +0 dword + +4 byte; +5..+7 preserved */
        }
    }
    s_slot_0055300c = 0;                          /* entry 66 */
    s_slot_00568da8 = 0;                          /* entry 67 */
    s_slot_00553f30 = 0;                          /* entry 68 */
}

/* Army `id`'s capital cursor lives at offset +0x6c/+0x6d (col/row) of its
 * per-army record in save entry 4 (g_per_army_768, retail base 0x53f9b0):
 * the very cursor engine_territory_setup_capitals (FUN_00468d4f) stamps at
 * +0x6c/+0x6d for key=1..count.  The record stride is 0x300 (ARMY_ROW_STRIDE),
 * the universal per-army-record size.
 *
 * Addressing (r2 @ FUN_00428007/428078): `mov eax,[id]; shl eax,8;
 * mov cl, byte [eax + eax*2 + 0x53fa1c]` → 3*(id<<8) + 0x53fa1c =
 * id*0x300 + 0x53fa1c = g_per_army_768[id*0x300 + 0x6c].  The `shl eax,8`
 * is what makes the effective stride 0x300, NOT 3 — Ghidra's original
 * `(&DAT_0053fa1c)[id * 0x300]` rendering was correct (engine-quirks #174). */
#define ARMY_COORD_OFF      0x6c
#define ARMY_COORD_STRIDE   0x300

/* Shared body for FUN_00428007 / FUN_00428078: look up army `id`'s map
 * coordinate, then probe Chebyshev radii 1..3 around it, returning 1 at
 * the first radius where the given box predicate matches, else 0.  The
 * coords are read as unsigned bytes (retail `(uint)(byte)`); an out-of-
 * range coord simply yields an empty clamped box (→ 0).
 *
 * On a match the found cell is reported via *out_col / *out_row (when
 * non-NULL) — the box scan leaves it there, and FUN_00427ed0 needs it to
 * place the army's start marker AT the matched cell (quirk #130). */
static int army_near(int id, int (*probe)(int, int, int, int *, int *),
                     int *out_col, int *out_row)
{
    int col = g_per_army_768[ARMY_COORD_OFF + id * ARMY_COORD_STRIDE];
    int row = g_per_army_768[ARMY_COORD_OFF + id * ARMY_COORD_STRIDE + 1];
    for (int r = 1; r <= 3; r++)
        if (probe(col, row, r, out_col, out_row))
            return 1;
    return 0;
}

/* Port of FUN_00428007 — 1 iff a rawflag-bit0 cell lies within Chebyshev
 * radius 3 of army `id` (via FUN_0046cfbd); the matched cell is reported
 * via *out_col / *out_row (pass NULL to ignore). */
int engine_game_init_army_near_rawflag_bit0(int id, int *out_col, int *out_row)
{
    return army_near(id, engine_map_query_box_any_rawflag_bit0,
                     out_col, out_row);
}

/* Port of FUN_00428078 — 1 iff a rawflag-only-bit1 cell (flag 0 or 2)
 * lies within Chebyshev radius 3 of army `id` (via FUN_0046d130); the
 * matched cell is reported via *out_col / *out_row (pass NULL). */
int engine_game_init_army_near_rawflag_only_bit1(int id,
                                                 int *out_col, int *out_row)
{
    return army_near(id, engine_map_query_box_any_rawflag_only_bit1,
                     out_col, out_row);
}

/*
 * Port of FUN_00427ed0 (311 B) — place each army's strategic-map start
 * marker.  A direct 0x497ced new-game callee.
 *
 * Walks the army-id list at save entry 139 (s_slot_00569518, up to 6
 * entries, terminated by a zero id).  For each army it probes the map
 * for a nearby feature — first a rawflag-bit0 cell, then a flag∈{0,2}
 * cell, both within Chebyshev radius 3 of the army's coord — and on a
 * hit allocates a type-6 entity (kind 3) at the matched cell via the
 * shared placer (FUN_0046e1b0), then stamps army-specific fields into
 * the freshly allocated record (addressed by the placer's current-slot
 * cursor, retail DAT_0052afe0):
 *
 *   +0x150 = 1     +0x10  = army id   +0x166 = 100
 *   +0x167 = id    +0x164 = 1         +0x14f = loop index (0-based)
 *
 * and bumps the placed-army counter at save entry 78 (s_slot_005530b4).
 *
 * Faithful note: retail ignores the placer's return value and always
 * stamps record[DAT_0052afe0].  We mirror that — if the placer fails
 * (the matched cell turns out blocked, or the table is full) it still
 * leaves the cursor set, and we stamp whatever record it points at, as
 * retail does.  In practice the box scan only matches free cells, so a
 * post-probe placement failure does not arise at new-game.  Sole caller
 * 0x497ced (init-only). */
void engine_game_init_place_army_starts(void)
{
    s_slot_005530b4 = 0;                          /* entry 78 — count */

    for (int i = 0; i < 6; i++) {
        uint8_t id = s_slot_00569518[i];          /* entry 139 — id list */
        if (id == 0) break;

        int col = 0, row = 0;
        if (engine_game_init_army_near_rawflag_bit0(id, &col, &row) ||
            engine_game_init_army_near_rawflag_only_bit1(id, &col, &row)) {
            engine_territory_place_entity(3, col, row, 6);   /* FUN_0046e1b0 */

            uint8_t *rec = g_per_territory_420 +
                           engine_territory_current_slot() * 0x1a4;
            rec[0x150] = 1;
            rec[0x10]  = id;
            rec[0x166] = 100;
            rec[0x167] = id;
            rec[0x164] = 1;
            rec[0x14f] = (uint8_t)i;

            s_slot_005530b4++;
        }
    }
}

/* ──────────────────────────────────────────────────────────────────
 * New-game per-realm / country setup pass — port of FUN_0049bd99
 * ────────────────────────────────────────────────────────────────── */

/* Per-army record block (g_per_army_768, stride 0x300) — local accessor. */
#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])

/* The five preset-selection inputs (see game_init.h).  Owned here; latched
 * by the config-screen apply (unported) via the setter below. */
static int g_economy_preset;    /* DAT_0053f27c */
static int g_resource_preset;   /* DAT_0053f270 */
static int g_troop_preset;      /* DAT_0053f274 */
static int g_gold_base;         /* DAT_0053f278 */
static int g_keep_level;        /* DAT_0053f280 (read as a byte) */

void engine_game_init_set_realm_presets(int economy_preset, int resource_preset,
                                        int troop_preset, int gold_base,
                                        int keep_level)
{
    g_economy_preset  = economy_preset;
    g_resource_preset = resource_preset;
    g_troop_preset    = troop_preset;
    g_gold_base       = gold_base;
    g_keep_level      = keep_level;
}

void engine_game_init_preset_snapshot(int out[5])
{
    if (!out) return;
    out[0] = g_economy_preset;
    out[1] = g_resource_preset;
    out[2] = g_troop_preset;
    out[3] = g_gold_base;
    out[4] = g_keep_level;
}

/* Const selector tables retail's FUN_00499dc3 indexes (baked from Lords2.exe).
 *   0x4dbbf8 — picked by config tab 10 → s_slot_0053f26c (a turn/difficulty
 *              budget the game-start also mirrors to DAT_005440c8); 7 valid
 *              entries (tab 10 offers 7 values).
 *   0x4dbc18 — picked by config tab 8 → g_gold_base (starting gold); 5 valid
 *              entries (tab 8 offers 5 values; the bytes past entry 5 are
 *              unrelated rdata string data, not table entries). */
static const int32_t k_cfg_budget_0x4dbbf8[7] = {
    30, 60, 120, 240, 480, 600, 0,
};
static const int32_t k_cfg_gold_0x4dbc18[5] = {
    100, 500, 1000, 2500, 5000,
};

/* Port of FUN_00499dc3 (154 B) — the custom-game config-apply, retail's page-7
 * "Start" finalise.  Copies the 12 config-screen option-tab selections (the
 * port holds them in engine_title_render's g_p7_tab_values, retail
 * DAT_0053f288..0x53f2b4; the tab->VA map lives in apply_defaults there) into
 * the live game-config globals the new-game initialiser reads:
 *   - the five realm presets (economy/resource/troop/gold/keep) — the SAME
 *     storage engine_game_init_set_realm_presets latches, so this is the
 *     config-driven alternative to that hard-coded setter;
 *   - the config scalars s_slot_0053f25c/260/264/23c (standings / two mode
 *     flags / render-mode) and the table-selected budget s_slot_0053f26c;
 *   - the AI count s_slot_0053f268 = (player-count selector + 2) - human count.
 *
 * Must run AFTER the player setup (engine_game_init_set_player_config), which
 * seeds the human count s_slot_00553f98 the AI-count formula reads.
 *
 * The one retail store dropped: DAT_0053f284 (= tab 11) is engine_combat's
 * quick-anim suppressor — a battle-time flag, not a save scalar and never read
 * on the new-game / first-frame path; engine_combat owns it. */
void engine_game_init_apply_custom_config(void)
{
    /* AI/worker count: total players (selector + 2) minus the humans. */
    s_slot_0053f268 = (uint32_t)((engine_title_render_tab_value(2) + 2)
                                 - (int)s_slot_00553f98);

    s_slot_0053f25c = (uint32_t)engine_title_render_tab_value(0);  /* standings  */
    s_slot_0053f264 = (uint32_t)engine_title_render_tab_value(1);  /* render-mode */
    s_slot_0053f260 = (uint32_t)engine_title_render_tab_value(3);  /* mode flag  */
    s_slot_0053f23c = (uint32_t)engine_title_render_tab_value(4);

    int budget_sel = engine_title_render_tab_value(10);
    s_slot_0053f26c = (uint32_t)((budget_sel >= 0 && budget_sel < 7)
                                 ? k_cfg_budget_0x4dbbf8[budget_sel] : 0);

    int gold_sel = engine_title_render_tab_value(8);
    g_gold_base = (gold_sel >= 0 && gold_sel < 5)
                  ? k_cfg_gold_0x4dbc18[gold_sel] : 0;

    g_keep_level      = engine_title_render_tab_value(6);
    g_resource_preset = engine_title_render_tab_value(7);
    g_troop_preset    = engine_title_render_tab_value(5);
    g_economy_preset  = engine_title_render_tab_value(9);
}

/* The two scenario / "Play Now!" preset tables FUN_00499e5d indexes (baked from
 * Lords2.exe via r2 `pxw @ 0x4d8e18 / 0x4d8f58`).  Each is 10 entries of eight
 * i32 fields at a 0x20-byte stride; the selector s_slot_0053f258 (set by the
 * difficulty picker 0x4334f0, range 0..7) picks the row, the campaign flag
 * s_slot_0053f640 picks the table (1 = campaign, 0 = skirmish/deathmatch).  The
 * fields land in the SAME config globals the page-7 custom apply fills — so the
 * difference between a scenario start and a custom start is just where the
 * presets come from (a fixed table row vs. the option tabs), plus that scenario
 * starts force standings off + a zero turn budget (below).  Entries 8/9 are
 * unused padding (selector never reaches them); kept so the table is indexable
 * 1:1 with retail.  Field order matches FUN_00499e5d's stores:
 *   +0x00 map_id     +0x10 economy
 *   +0x04 density    +0x14 resource     (s_slot_0053f23c — the density/
 *   +0x08 gold       +0x18 troop          difficulty scalar)
 *   +0x0c keep       +0x1c ai_count                                          */
typedef struct {
    int32_t map_id, density, gold, keep, economy, resource, troop, ai_count;
} scenario_preset_t;

static const scenario_preset_t k_scenario_presets[10] = {   /* retail 0x4d8e18 */
    { 17, 0, 5000, 1, 1, 1, 0, 1 },
    { 12, 0, 2500, 1, 1, 1, 0, 2 },
    {  2, 1, 5000, 2, 1, 1, 0, 3 },
    {  5, 1, 2500, 3, 0, 1, 0, 4 },
    {  0, 2, 5000, 4, 2, 1, 0, 4 },   /* map 0 (England) = the default new game */
    {  3, 2, 2500, 4, 1, 1, 0, 4 },
    {  7, 2, 1000, 5, 0, 1, 0, 4 },
    {  4, 2, 1000, 5, 0, 1, 0, 4 },
    {  0, 0,    0, 0, 0, 0, 0, 4 },   /* padding */
    {  0, 0,    0, 0, 0, 0, 0, 4 },   /* padding */
};

static const scenario_preset_t k_campaign_presets[10] = {   /* retail 0x4d8f58 */
    {  0, 0,    0, 0, 0, 0, 0, 4 },   /* padding */
    {  0, 0,    0, 0, 0, 0, 0, 4 },   /* padding */
    { 52, 2, 5000, 2, 1, 1, 0, 1 },
    { 53, 2, 2500, 3, 0, 1, 0, 1 },
    { 45, 2, 2500, 4, 2, 1, 0, 2 },
    { 44, 2, 2500, 4, 1, 1, 0, 3 },
    { 41, 2, 1000, 5, 0, 1, 0, 4 },
    { 42, 2, 1000, 5, 0, 1, 0, 4 },
    {  0, 0,    0, 0, 0, 0, 0, 4 },   /* padding */
    {  0, 0,    0, 0, 0, 0, 0, 4 },   /* padding */
};

/* Port of FUN_00499e5d (421 B) — the scenario / "Play Now!" config-apply, the
 * sibling of engine_game_init_apply_custom_config.  Where the custom apply reads
 * the page-7 option tabs, this reads a fixed row of the scenario/campaign preset
 * table (selected by `campaign_flag` + `scenario_idx`, retail s_slot_0053f640 /
 * s_slot_0053f258), writing the SAME config globals: the map id, the density
 * scalar, the five realm presets (gold/keep/economy/resource/troop) and the AI
 * count.  Scenario starts force standings off (s_slot_0053f25c), render-mode 0
 * (s_slot_0053f264), the morale-pass gate off (s_slot_0053f260) and a zero turn
 * budget (s_slot_0053f26c).
 *
 * Two retail stores are intentionally dropped, matching the custom apply:
 *   - DAT_0053f284 (= 1): engine_combat's quick-anim suppressor — a battle-time
 *     flag, not read on the new-game / first-frame path; engine_combat owns it.
 *   - the tail map-setup calls FUN_0046a037 (minimap grids) / FUN_00467770
 *     (strat-map record / plane-unpack) / FUN_004298c1 (build cells): the port's
 *     newgame_build_world_and_start runs all three (plus camera + game-start) as
 *     the shared world-build tail, so reproducing them here would double them.
 *
 * Sets s_slot_0053f268 (AI count) directly from the table, so — unlike the
 * custom apply — it does NOT depend on the human count and may run before
 * engine_game_init_set_player_config (which then rewrites the same value to
 * count - human_count; the caller sizes count = ai_count + human_count). */
void engine_game_init_apply_scenario_config(int campaign_flag, int scenario_idx)
{
    const scenario_preset_t *tbl = campaign_flag ? k_campaign_presets
                                                 : k_scenario_presets;
    const scenario_preset_t *e =
        (scenario_idx >= 0 && scenario_idx < 10) ? &tbl[scenario_idx] : &tbl[0];

    s_slot_0053f26c = 0;                       /* turn budget (none in table) */

    s_slot_0053f034  = (uint32_t)e->map_id;
    s_slot_0053f23c  = (uint32_t)e->density;
    g_gold_base      = e->gold;
    g_keep_level     = e->keep;
    g_economy_preset = e->economy;
    g_resource_preset = e->resource;
    g_troop_preset   = e->troop;
    s_slot_0053f268  = (uint32_t)e->ai_count;

    s_slot_0053f25c = 0;                       /* standings off          */
    s_slot_0053f264 = 0;                       /* render-mode 0          */
    s_slot_0053f260 = 0;                       /* morale-pass gate off   */
}

/* engine_game_init_set_player_config — seat the config-screen's player setup,
 * the OTHER half of the new-game preconditions (the realm presets above being
 * the first half).  The custom-game / scenario config screen (FUN_00499dc3 +
 * the screen-entry default population, both unported) writes a small set of
 * player fields BEFORE FUN_00497ced runs; this setter reproduces them so the
 * ported initialiser lands byte-exact on retail.
 *
 * The config screen sets surprisingly little per slot — almost everything is
 * (re)derived by the ported initialiser from the map + the NPC scheduler:
 *   - the player→army ROSTER (g_player_roster) is rebuilt from the map's
 *     plane-4/5 owner tiles by engine_territory_load_strat_map, so it is NOT
 *     seeded here (seeding it is overwritten);
 *   - the AI "opponent-type" byte +0x07 is actually the NPC scheduler's
 *     work-location field: engine_npc_schedule_init CLEARS +0x07 for every
 *     slot, then assigns a work-location to the first `s_slot_0053f268`
 *     UNOWNED (has_data==0) slots — that count, not a per-slot flag, is what
 *     decides how many AI players exist (and Phase 2 / the inactive-purge read
 *     the resulting +0x07 as the active-or-empty test).  So the AI count is
 *     seeded once via s_slot_0053f268, not per slot;
 *   - the work-position bytes +0x08/+0x09 and the job-slot id +0x0a are set by
 *     the NPC scheduler too — except for owned (human) slots, which it skips,
 *     so the human's +0x0a (its job-slot id) must be pre-seeded.
 *
 * What the config screen genuinely owns, and we seed here, is therefore:
 *   +0x05 has_data   — 1 for the human slot(s), 0 for AI
 *   +0x0a            — the slot index (the human's survives the scheduler)
 *   +0xee            — the player bit 1<<(slot-1) (scheduler never touches it)
 * plus the local-player index s_slot_0057c8cc (save entry 72) and the AI count
 * s_slot_0053f268 (read by the NPC scheduler).  ACTIVE (+0x04) is left clear —
 * Phase 2 latches it.
 *
 * Players occupy slots 1..count; slots 1..human_count are human, the rest AI.
 * cur_player is the local/human player index (1 for a standard single-player
 * game). */
void engine_game_init_set_player_config(int count, int human_count,
                                        int cur_player, const void *player_name)
{
    for (int s = 1; s <= count && s < 6; s++) {
        uint8_t *p = STRAT_REC(s);
        p[STRAT_HAS_DATA] = (s <= human_count) ? 1 : 0;   /* +0x05 */
        p[0x0a]           = (uint8_t)s;                    /* job-slot id */
        p[0xee]           = (uint8_t)(1u << (s - 1));      /* player bit */

        /* Human slots reserve their job slot via the aux record (retail's SP
         * seating player_seat_assign sets aux[+0x25] = strategic[+0x0a] +
         * aux[+0x27] = 1).  engine_npc_schedule_init reads aux[+0x25] (with
         * aux[+0x26]==0) to pre-reserve that job, so the AI players are
         * scheduled around it — without it the AI work-location assignment
         * shifts and diverges from retail. */
        if (s <= human_count) {
            g_player_aux_44[s * 0x2c + 0x25] = p[0x0a];   /* fixed-job id   */
            g_player_aux_44[s * 0x2c + 0x27] = 1;         /* aux in-use flag */

            /* Player name + kingdom/title.  Retail's SP seat (0x4410bd →
             * player_seat_assign 0x49bae9) copies the 31-byte name field
             * from the lords2.inf prefs buffer (DAT_0053f1e0) into the aux
             * record at +0x04; that field carries the name at prefs +0x00
             * and the title at prefs +0x18, so the 31-byte copy lands the
             * name at aux +0x04 and the title at aux +0x1c (save entry 2).
             * Left zero when the prefs file is absent. */
            if (player_name) {
                memcpy(&g_player_aux_44[s * 0x2c + 0x04], player_name, 0x1f);
            }
        }
    }
    s_slot_0057c8cc = (uint32_t)cur_player;        /* entry 72: local player */
    s_slot_0053f268 = count - human_count;         /* NPC/worker (AI) count */
    s_slot_00553f98 = (uint32_t)human_count;       /* entry 86: human/active count */

    /* The four config-screen substate scalars written at the TOP of the
     * same setup routine this function reproduces (retail FUN_0049792e @
     * 0x49792e, before its player loop + npc-schedule call): a screen/page
     * index, two selection cursors, and a player iterator.  Constants for a
     * default new game; they persist to the save (entries 73/75/76/84) so
     * the headless seat must seat them too. */
    s_slot_00569584 = 1;   /* entry 73: config-screen page/state index   */
    s_slot_0053f06c = 7;   /* entry 75                                   */
    s_slot_0053f678 = 4;   /* entry 76                                   */
    s_slot_0057c944 = 1;   /* entry 84: player iterator (1..6)           */
}

/* Optional battlefield-spawn seam (retail 0x4a9a9a). */
static void (*g_troop_spawn)(int player, int army);

void engine_game_init_bind_troop_spawn(void (*fn)(int player, int army))
{
    g_troop_spawn = fn;
}

/* Per-player troop muster pool (retail DAT_0053f6a0, 6 rows × 0x80 B). */
static uint8_t g_troop_pool[6 * 0x80];

uint8_t *engine_game_init_troop_pool_row(int player)
{
    return &g_troop_pool[player * 0x80];
}

/*
 * Retail's three new-game preset tables share one contiguous .data region
 * (0x4dc070..0x4dc180, 68 dwords); each is read with its own base offset and
 * stride, and the strides do not tile, so the three views overlap in address
 * space — only an in-range index (which retail guarantees from the bounded
 * config-screen selection) reads its own table's rows.  Baked dword-for-dword
 * from the PE and read through the byte-offset accessors below (host + target
 * are both little-endian, so the byte columns of the economy table read
 * correctly off this dword array).
 */
static const uint32_t k_preset_words[68] = {           /* base = retail 0x4dc070 */
    /* +0x00 */ 0,     0,     0,      0,
    /* +0x10 */ 0,     0,     0,      0,
    /* +0x20 */ 0x19,  0,     0x19,   0,
    /* +0x30 */ 0,     0,     0x32,   0x32,
    /* +0x40 */ 0x32,  0,     0x64,   0x64,
    /* +0x50 */ 0x64,  0x64,  0x64,   0,
    /* +0x60 */ 0x0a,  0x28,  0xa7,   0x2d,    /* economy table row 0 begins @ +0x60 */
    /* +0x70 */ 0x29,  0,     0x5f,   0x1a1,
    /* +0x80 */ 0x41,  0x41,  0x1f4,  0x14a,
    /* +0x90 */ 0x49d, 0x55,  0x55,   0,
    /* +0xa0 */ 0,     0,     0,      0,        /* troop table row 0 begins @ +0xa0 */
    /* +0xb0 */ 0,     0,     0,      0,
    /* +0xc0 */ 0,     0,     0x19,   0,
    /* +0xd0 */ 0x19,  0,     0,      0,
    /* +0xe0 */ 0,     0x32,  0x32,   0x32,
    /* +0xf0 */ 0,     0,     0,      0,
    /* +0x100*/ 0x64,  0x64,  0x64,   0,
};

#define PRESET_ECON_OFF    0x60   /* economy table base, stride 0x14 */
#define PRESET_RES_OFF     0x00   /* resource table base, stride 0x18 */
#define PRESET_TROOP_OFF   0xa0   /* troop table base, stride 0x1c */

static const uint8_t *preset_bytes(void)
{
    return (const uint8_t *)k_preset_words;
}

/*
 * Phase 2 helper — the sparse field-zero block at the head of each active
 * player's record (retail LAB_0049bf5e).  Retail does NOT memset the whole
 * 0x160 record; it clears a specific working-field set and seats two defaults
 * (+0x29 = +0x2a = 1), leaving the rest of the record (the identity fields
 * +0x4..+0x7, +0xa, etc.) intact.  Reproduced field-exact for bit-parity.
 *
 * Retail loops `for (i=0; i<4; i++)` over the +0x80 dword write without using
 * the index (a redundant 4× zero of +0x80); folded to one write here.  The
 * +0x140 loop DOES index (six dwords +0x140..+0x154), kept as a loop.
 */
static void init_player_record(uint8_t *p)
{
    *(uint32_t *)(p + 0x00) = 0;
    p[0x0c] = 0;
    *(uint32_t *)(p + 0x10) = 0;
    *(uint32_t *)(p + 0x14) = 0;
    *(uint32_t *)(p + 0x18) = 0;
    p[0x1c] = 0;  p[0x1d] = 0;  p[0x1e] = 0;
    *(uint32_t *)(p + 0x20) = 0;
    *(uint32_t *)(p + 0x24) = 0;
    p[0x28] = 0;
    p[0x29] = 1;  p[0x2a] = 1;          /* two seated defaults */
    p[0x2b] = 0;  p[0x2c] = 0;
    p[0x45] = 0;  p[0x46] = 0;  p[0x47] = 0;
    p[0x48] = 0;  p[0x49] = 0;  p[0x4a] = 0;
    p[0x4b] = 0;  p[0x4c] = 0;  p[0x4d] = 0;
    *(uint32_t *)(p + 0x50) = 0;
    *(uint32_t *)(p + 0x54) = 0;
    p[0x58] = 0;
    *(uint32_t *)(p + 0x5c) = 0;
    p[0x60] = 0;  p[0x61] = 0;
    *(uint32_t *)(p + 0x64) = 0;
    *(uint32_t *)(p + 0x68) = 0;
    *(uint32_t *)(p + 0x6c) = 0;
    *(uint32_t *)(p + 0x80) = 0;        /* retail's 4× redundant loop, folded */
    p[0x80] = 0;  p[0x81] = 0;
    p[0xe7] = 0;
    p[0xe8] = 0;  p[0xe9] = 0;  p[0xea] = 0;  p[0xeb] = 0;
    p[0xec] = 0;  p[0xed] = 0;
    p[0xef] = 0;  p[0xf0] = 0;
    *(uint32_t *)(p + 0xf4) = 0;
    *(uint32_t *)(p + 0xf8) = 0;
    *(uint32_t *)(p + 0xfc) = 0;
    for (int off = 0x100; off <= 0x13c; off += 4)   /* +0x100..+0x13c dwords */
        *(uint32_t *)(p + off) = 0;
    for (int i = 0; i < 6; i++)                      /* +0x140..+0x154 (indexed) */
        *(uint32_t *)(p + 0x140 + i * 4) = 0;
    p[0x158] = 0;  p[0x159] = 0;
}

void engine_game_init_setup_realms(void)
{
    const uint8_t *preset = preset_bytes();
    const int armies = (int)s_slot_0056d5dc;

    /* --- Phase 1: per-army economy seed -------------------------------- */
    {
        const uint8_t *econ = preset + PRESET_ECON_OFF + g_economy_preset * 0x14;
        const int32_t gold  = *(const int32_t *)(econ + 0x00);
        const int32_t stock = *(const int32_t *)(econ + 0x04);
        const int32_t pop   = *(const int32_t *)(econ + 0x08);
        const uint8_t b_b   = econ[0x0c];
        const uint8_t b_d   = econ[0x10];

        for (int key = 1; key <= armies; key++) {
            uint8_t *rec = ARMY_REC(key);
            *(int32_t *)(rec + 0x228) = gold;
            *(int32_t *)(rec + 0x224) = gold;     /* copy of +0x228 */
            *(int32_t *)(rec + 0x254) = stock;
            *(int32_t *)(rec + 0x250) = stock;    /* copy of +0x254 */
            *(int32_t *)(rec + 0x028) = pop;
            *(int32_t *)(rec + 0x024) = pop;      /* copy of +0x28 */
            rec[0x0b] = b_b;
            rec[0x0d] = b_d;
            rec[0x0c] = b_d;                      /* copy of +0xd */

            engine_territory_census_army_terrain(key);   /* FUN_00469b8d */
            engine_territory_grade_feature_cells(key);   /* FUN_0044d913 */
        }
    }

    /* --- Phase 2: per-player record init ------------------------------- */
    for (int slot = 1; slot < 6; slot++) {
        uint8_t *p = STRAT_REC(slot);

        /* Active iff: human (HAS_DATA set) OR AI (HAS_DATA clear, OPP_TYPE
         * set).  An empty slot (both clear) is skipped.  Either way an active
         * slot latches the ACTIVE flag first. */
        if (p[STRAT_HAS_DATA] == 0) {
            if (p[STRAT_OPP_TYPE] == 0)
                continue;                         /* empty slot */
            p[STRAT_ACTIVE] = 1;
        } else {
            p[STRAT_ACTIVE] = 1;
        }

        init_player_record(p);

        /* Map this player to its army (g_player_roster pair, id byte @ *2). */
        int army = (uint8_t)g_player_roster[slot * 2];
        uint8_t *arec = ARMY_REC(army);
        arec[0x05] = (uint8_t)slot;               /* army → owning player */
        arec[0x07] = p[0x0a];                     /* army +0x07 ← player +0x0a */

        if (army > armies)
            continue;                             /* unmapped / out-of-range */

        p[0xe4] = (uint8_t)army;                  /* player → army id */
        *(int32_t *)(p + 0x118) = g_gold_base;    /* starting gold base */

        if (g_troop_preset != 0) {
            const uint8_t *tr = preset + PRESET_TROOP_OFF + g_troop_preset * 0x1c;
            uint8_t *pool = engine_game_init_troop_pool_row(slot);
            *(int32_t *)(pool + 0x70) = 0;
            for (int i = 0; i < 7; i++) {
                int32_t n = *(const int32_t *)(tr + i * 4);
                *(int32_t *)(pool + i * 0x10) = n;
                *(int32_t *)(pool + 0x70)        += n;   /* muster total */
                *(int32_t *)(arec + 0x24)        += n;   /* army population */
            }
            if (g_troop_spawn)
                g_troop_spawn(slot, army);        /* 0x4a9a9a (deferred) */
            *(int32_t *)(arec + 0x2f4) = 0;
            /* Retail adds the freshly placed entity's territory yield (record
             * +0x15c at the placer's current slot) — exact once the spawn
             * hook above runs; with it unbound the slot cursor is stale. */
            *(int32_t *)(p + 0x118) +=
                *(const int32_t *)(g_per_territory_420 +
                                   engine_territory_current_slot() * 0x1a4 + 0x15c);
        }

        /* Per-realm economy passes — each issued twice, in retail order. */
        for (int pass = 0; pass < 2; pass++) {
            engine_territory_allocate_realm_workforce(army);     /* FUN_0044f6e7 */
            engine_army_economy_provision(army, (int)s_slot_0057c934); /* 0x44df5f */
            engine_army_economy_plan_realm(army, (int)s_slot_0057c92c); /* 0x4485a5 */
        }

        /* Secondary-settlement keep tier (byte → realm +0x1c0). */
        arec[0x1c0] = (uint8_t)g_keep_level;
        if (arec[0x1c0] != 0)
            engine_territory_stamp_secondary_keep(army, arec[0x1c0] - 1); /* 0x46826c */

        /* Resource-pool defaults + the per-race resource preset. */
        *(int32_t *)(p + 0x120) = 0x32;
        *(int32_t *)(p + 0x130) = 0x32;
        *(int32_t *)(p + 0x128) = 0x32;
        {
            const uint8_t *res = preset + PRESET_RES_OFF + g_resource_preset * 0x18;
            for (int i = 0; i < 6; i++)
                *(int32_t *)(p + 0x140 + i * 4) = *(const int32_t *)(res + i * 4);
        }
        if (p[STRAT_HAS_DATA] == 0)               /* AI realm: density gold bonus */
            *(int32_t *)(p + 0x150) += (int)s_slot_0053f23c * 0x14;

        /* Latch the first present economic-model gate (slot records @ +0x294,
         * stride 0x18; skip slot 2; gate flag +1, output flag +3). */
        for (int i = 0; i < 4; i++) {
            if (i != 2 && arec[0x294 + i * 0x18 + 1] != 0) {
                arec[0x294 + i * 0x18 + 3] = 1;
                break;
            }
        }
        arec[0x1b0] = 1;                          /* realm setup-complete flag */
    }

    /* --- Phase 3: finalise --------------------------------------------- */

    /* Unowned (no human/AI) armies get a +100 gold/stock head-start. */
    for (int key = 1; key <= armies; key++) {
        uint8_t *rec = ARMY_REC(key);
        if (rec[0x05] == 0) {
            *(int32_t *)(rec + 0x224) += 100;
            *(int32_t *)(rec + 0x228) += 100;
        }
    }

    /* Recenter + highlight the strategic map on the local player's army. */
    int local_army = (uint8_t)g_player_roster[s_slot_0057c8cc * 2];
    engine_territory_recenter_on_capital(local_army);   /* FUN_00432746 */
    engine_territory_highlight_key_cells(local_army);   /* FUN_0046dfd5 */
}

/* Port of FUN_004514f8 (147 B) — seed army `key`'s workforce target-ratio run
 * (record +0x130..+0x14c, the eight dwords engine_territory_allocate_realm_
 * workforce reads as per-bucket targets): +0x130 = 0x21, +0x134 = 0x32,
 * +0x138 = 0x11, +0x148 = 100, the rest (+0x13c/+0x140/+0x144/+0x14c) zero.
 * The sole caller is the baseline seed below. */
static void seed_workforce_ratios(int key)
{
    uint8_t *r = ARMY_REC(key);
    *(int32_t *)(r + 0x130) = 0x21;
    *(int32_t *)(r + 0x134) = 0x32;
    *(int32_t *)(r + 0x138) = 0x11;
    *(int32_t *)(r + 0x148) = 100;
    *(int32_t *)(r + 0x144) = 0;
    *(int32_t *)(r + 0x140) = 0;
    *(int32_t *)(r + 0x14c) = 0;
    *(int32_t *)(r + 0x13c) = 0;
}

/* Port of FUN_00451150 — see game_init.h for the field overview. */
void engine_game_init_seed_army_baseline(void)
{
    s_slot_00554020 = 1;                          /* armies-initialised flag */

    const int armies = (int)s_slot_0056d5dc;
    for (int key = 1; key <= armies; key++) {
        uint8_t *r = ARMY_REC(key);

        r[0x21b] = 3;
        r[0x21d] = 0x32;
        /* Difficulty tier bucketed by army index. */
        r[0x21e] = (key < 4)  ? 0 :
                   (key < 6)  ? 1 :
                   (key < 10) ? 2 :
                   (key < 12) ? 3 : 4;
        r[0x05] = 0;
        *(int32_t *)(r + 0x28) = 0x96;            /* population */
        *(int32_t *)(r + 0x24) = *(int32_t *)(r + 0x28);
        r[0xb8] = (uint8_t)((*(int32_t *)(r + 0x24) - 1) / 0x19 + 1); /* rank */
        r[0x15e] = 3;
        r[0x15f] = 100;
        r[0x0a] = 2;
        r[0x09] = r[0x0a];
        r[0x0b] = 0x32;
        r[0x0d] = 0x32;
        r[0x0c] = r[0x0d];
        r[0x18] = 0;
        *(int32_t *)(r + 0x1c) = 0;
        *(int32_t *)(r + 0x208) = 0;
        r[0x1fe] = (key & 1) ? 1 : 0;             /* even/odd parity */
        r[0x290] = 4;
        r[0x08] = 0x19;
        *(int32_t *)(r + 0x254) = 0x28;           /* stock */
        *(int32_t *)(r + 0x250) = *(int32_t *)(r + 0x254);
        *(int32_t *)(r + 0x228) = 100;            /* gold */
        *(int32_t *)(r + 0x224) = *(int32_t *)(r + 0x228);

        seed_workforce_ratios(key);               /* FUN_004514f8 */

        for (int i = 0; i < 4; i++)               /* model-gate output flags */
            r[0x297 + i * 0x18] = 0;
        for (int i = 0; i < 9; i++) {             /* workforce job-slot records */
            *(int32_t *)(r + 0xc4 + i * 0xc + 0x08) = 0;
            *(int32_t *)(r + 0xc4 + i * 0xc + 0x04) = *(int32_t *)(r + 0xc4 + i * 0xc + 0x08);
            *(int32_t *)(r + 0xc4 + i * 0xc + 0x00) = *(int32_t *)(r + 0xc4 + i * 0xc + 0x04);
        }

        engine_territory_census_army_terrain(key);            /* FUN_00469b8d */
        engine_territory_grade_feature_cells(key);            /* FUN_0044d913 */
        engine_army_economy_select_candidate(key, (int)s_slot_0057c92c); /* 0x44dd4d */
        engine_army_economy_distribute_stock(key);            /* FUN_0044c278 */
        engine_territory_allocate_realm_workforce(key);       /* FUN_0044f6e7 */
        engine_army_economy_provision(key, (int)s_slot_0057c934); /* 0x44df5f */
    }
}

/* ────────────────────────────────────────────────────────────────────────
 * New-game orchestrator (FUN_00497ced) + game-start (FUN_004329ec)
 * ──────────────────────────────────────────────────────────────────────── */

/* Runtime-only state the orchestrator owns + binds for the territory passes.
 * The cell-score table is retail DAT_0053fa40 (17 rows × 384 i16, the per-
 * army cell scoring grid the new-game rebuild resets); the category-bucket
 * digest is the strategic FUN_004290d0/429153 working set. */
#define GI_SCORE_ROWS 17
#define GI_SCORE_ROW_I16 384
static int16_t g_gi_cell_score[GI_SCORE_ROWS * GI_SCORE_ROW_I16];   /* DAT_0053fa40 */
static engine_category_buckets_state_t g_gi_strat_buckets;

/* Optional seams (see game_init.h) — default NULL = skip. */
static void (*g_gi_turn_driver_fn)(void);   /* FUN_00448440 (deferred)        */
static void (*g_gi_banks_fn)(void);         /* FUN_004984dc (seat-bound)      */
static void (*g_gi_castles_init_fn)(void);  /* FUN_004ae8a9 (file IO)         */
static void (*g_gi_save_rotate_fn)(void);   /* FUN_0049a453 (file IO)         */

void engine_game_init_bind_turn_driver_fn(void (*fn)(void)) { g_gi_turn_driver_fn = fn; }
void engine_game_init_bind_banks_fn(void (*fn)(void))       { g_gi_banks_fn = fn; }
void engine_game_init_bind_castles_init_fn(void (*fn)(void)){ g_gi_castles_init_fn = fn; }
void engine_game_init_bind_save_rotate_fn(void (*fn)(void)) { g_gi_save_rotate_fn = fn; }

void engine_game_init(void)
{
    /* RNG streams.  The double LFSR is consumed by the roster/value randomiser
     * (497e65); the single LFSR by the overworld autotiler (46dbf5/46dd34).
     * They are independent streams in this orchestrator.
     *
     * The double LFSR is NOT a private stream: retail's seat_shuffle stirs the
     * GLOBAL double-LFSR (DAT_0058fe28/0058fe08) via FUN_00404a46, advancing
     * the same state the turn driver later reads.  So load the live global in
     * (seeded to the InitInstance constants 0x54657687/0x72641663, matching a
     * --rng-seed capture), thread it through the seeder, and write the advanced
     * state back below — otherwise the global stays at its seed and the turn
     * driver's stir (step 17) starts from the wrong state.  The single LFSR is
     * a genuinely separate stream, seeded to its own InitInstance constant. */
    engine_rng_in_t        rng;
    engine_rng_global_get(&rng.state_a, &rng.state_b);
    engine_rng_single_in_t rngs = { .state   = 0x34518632u };

    /* Bind the runtime tables the seeders read/write to the live save blocks
     * (the same blocks the load path + tests bind). */
    int32_t *tile_cells = engine_map_init_tile_cells();      /* built by the caller */
    engine_territory_bind_records(g_per_territory_420);
    engine_territory_bind_cells(g_overworld_map);            /* descriptor table */
    /* The army-proximity probes (FUN_0046cfbd/46d130, used by step-8
     * place_army_starts and the live combat path) read the SAME 8-byte cell
     * grid through engine_map_query's own pointer — retail has one shared
     * grid global; the port must bind the query module to it too, or every
     * box scan sees NULL and returns 0 (→ 0 armies placed). */
    engine_map_query_bind_cell_descriptors(g_overworld_map);
    engine_territory_bind_cell_roster((int32_t *)g_misc_1360);
    engine_territory_bind_cell_score_table(g_gi_cell_score);
    engine_territory_bind_terrain_tiles(tile_cells);
    engine_territory_bind_army_records(g_per_army_768);
    engine_territory_bind_category_buckets(&g_gi_strat_buckets);
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_bind_cell_roster((int32_t *)g_misc_1360);
    engine_army_economy_bind_cells(g_overworld_map);
    /* The per-tick army-state machine (FUN_004650b0) reads each occupant's
     * cell region key (cell +7) through the same shared 8-byte grid. */
    engine_army_state_bind_cells(g_overworld_map);
    /* The army-movement destination-cell evaluator (FUN_00466893 / 466c3c /
     * 4658c1) reads the same shared 8-byte grid (cell flags +1, region +7,
     * terrain +0, occupant +5). */
    engine_army_move_bind_cells(g_overworld_map);
    /* Connect the per-tick army-state driver to the deep movement-step executor
     * (FUN_00465d28 → engine_army_move_run): when an occupant's sub-state is 2
     * the driver runs the executor, which steps the stack toward its waypoints
     * and threads the driver's evq / mp_active into the per-step classifier. */
    engine_army_state_bind_move_fn(engine_army_move_run);
    /* Bind the ported special-tile plunder handler (FUN_00468ae2) into the
     * executor's outcome-7 seam, and the region-event low-terrain develop
     * handler (FUN_0046873f) into the outcome-6 region-lo seam.  The remaining
     * dispatcher seams (region-hi / arrival / present) stay NULL no-ops until
     * ported. */
    engine_army_move_bind_special_fn(engine_army_move_special_tile);
    engine_army_move_bind_region_fns(engine_army_move_region_event_lo,
                                     engine_army_move_region_event_hi);
    /* Bind BOTH combat-outcome (code-5) seams together now that the pair is
     * ported: combat_a engine_army_move_combat_engage (FUN_004296b5 settle-
     * deposit) then combat_b engine_combat_contest_region (FUN_004a6c68 resolve).
     * Firing only half would deposit without resolving, so they bind as a unit.
     * Inert until the army-state driver (engine_army_state_run) is wired into the
     * tick — a contested move then settles its stack and resolves the battle. */
    engine_army_move_bind_combat_fns(engine_army_move_combat_engage,
                                     engine_combat_contest_region);
    /* The per-group resource-demand pool (retail DAT_0057c020, stride 0x160)
     * physically lives INSIDE the player-strategic block at +0x120 — retail's
     * commodity pools +0x120/+0x128/+0x130 ARE the resource accumulators, viewed
     * through two bases (the economy pass via DAT_0057c020, the AI brain via the
     * strategic record).  The port split them: the strategic block was real but
     * the resource pool stayed NULL, so the turn-1 resource pass (apply_resource_
     * quota → strategic +0x130 += amount) was silently dropped.  Bridge the
     * scalar wall — bind the pool to alias the strategic block, exactly as retail
     * overlaps them. */
    engine_army_economy_bind_resource_pool(g_player_strategic_352 + 0x120);

    /* 1–3: clear the per-territory / unit-type / per-army save blocks. */
    engine_game_init_clear_tables();                  /* 46f3cd / 46f2ae / 46f1dc */
    /* 4: NPC schedule seed. */
    engine_npc_schedule_init();                       /* 49caaa */
    /* 5: scalar world-state block (calendar etc.). */
    engine_game_init_seed_scalars();
    /* 6: overworld descriptor build (plane-unpack + autotile, single LFSR). */
    engine_territory_setup_overworld(&rngs);          /* 4676e0 */
    /* 6b: mirror the computed category-bucket cluster into its two save homes.
     * Retail's buckets/digest ARE the globals DAT_00567970 / DAT_00569518 (save
     * entries 138/139); the port merges them into one struct (g_gi_strat_buckets)
     * for cluster locality, so the homes must be reconciled here — before step 8
     * (place_army_starts reads the digest at s_slot_00569518) and before the
     * step-19 save.  The two retail globals are non-contiguous, so the single
     * struct can't overlay them; we copy each member to its home.  See
     * docs/findings/new-game-save-parity.md #1 + engine-quirks. */
    memcpy(s_slot_00567970, g_gi_strat_buckets.buckets,
           sizeof g_gi_strat_buckets.buckets);        /* entry 138 — buckets[6][16] */
    memcpy(s_slot_00569518, g_gi_strat_buckets.digest,
           sizeof g_gi_strat_buckets.digest);         /* entry 139 — digest[6]      */
    /* 7: per-army baseline economy/state row. */
    engine_game_init_seed_army_baseline();            /* 451150 */
    /* 8: place each army's start marker. */
    engine_game_init_place_army_starts();             /* 427ed0 */
    /* 9: unit/opponent-type table. */
    engine_game_init_seed_unit_table();               /* 4ac904 */
    /* 10: roster shuffle + the two 48-entry tables (double LFSR).  Write the
     * advanced state back into the global the seeder stirred (retail's
     * seat_shuffle stirs DAT_0058fe28/08 directly), so the turn driver and the
     * save mirror (entries 18/19) see the same post-seeder state retail does. */
    engine_game_init_seat_shuffle(&rng);              /* 497e65 */
    engine_rng_global_set(rng.state_a, rng.state_b);
    /* 11: compact the player roster. */
    player_roster_compact();                          /* 49bc5f */
    /* 12: per-realm / country setup. */
    engine_game_init_setup_realms();                  /* 49bd99 */
    /* 13: per-player sub-table reset. */
    engine_game_init_reset_player_subtables();        /* 4a1c53 */
    /* 14: 16-entry value table. */
    engine_game_init_seed_value_table();              /* 42847c */
    /* 15: per-realm stat-history ring. */
    engine_game_init_clear_realm_history();           /* 4ae74d */
    /* 16: write the fresh castles.dat (file IO seam). */
    if (g_gi_castles_init_fn) g_gi_castles_init_fn();  /* 4ae8a9 */
    /* 17: per-turn driver — DEFERRED (advances calendar + economy/AI). */
    if (g_gi_turn_driver_fn) g_gi_turn_driver_fn();    /* 448440 */
    /* 18: rebuild the strategic move-cost table from the descriptors. */
    engine_strat_cost_rebuild(g_overworld_map);       /* 46ff43 */
    /* 19: rotate + write the initial save slots (file IO seam). */
    if (g_gi_save_rotate_fn) g_gi_save_rotate_fn();    /* 49a453 */
    /* 20–21: event queue init + ranking recompute. */
    {
        engine_event_queue_state_t evq;
        engine_event_queue_init_wrapper(&evq);        /* 472ad0 */
        engine_ranking_recompute(&evq);               /* 49aa0e */
    }
    /* 22: audio cue — no-op (seats muted; see feedback_mute_and_skip_intro). */
    /* 23: seasonal sprite banks (render data-ready gate; seat-bound). */
    if (g_gi_banks_fn) g_gi_banks_fn();               /* 4984dc */
    /* 24: FUN_004b18f8(0xe8,0xc0) — synthetic WM_MOUSEMOVE to centre the
     *      cursor; a no-op for the headless render path. */
    /* 25: audio cue — no-op. */
    /* 26: purge inactive player records. */
    engine_game_init_purge_inactive_records();        /* 49b35e */
}

void engine_game_start(void)
{
    /* Single-player new-game branch of FUN_004329ec: build the world, then
     * flip to the in-game strategic-map screen + ingame engine state. The
     * MP / load branches are not ported. */
    engine_game_init();                               /* 497ced */
    engine_set_current_screen(0);                     /* DAT_004eac50 = 0 (ingame map) */
    g_engine_state = 3;                               /* DAT_0052af94 = 3 (ingame) */
    /* FUN_00451f8a's render-relevant flag: force a full empty-cell redraw on
     * the first frame (DAT_0053e9ac = 1; the others it sets are render-neutral
     * UI flags the live tick re-establishes). */
    engine_map_terrain_set_empty_redraw_gate(1);
    engine_set_demand_redraw(2);                      /* DAT_0057d340 = 2 */
}
