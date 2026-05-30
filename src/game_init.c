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

/* Init-only seeder FUN_004ae74d — reset the castle-shadow table at
 * new-game.  g_castles_shadow (save entry 10, retail 0x56d8c0) is 6400
 * 8-byte castle records; retail's producer addresses it as 400 rows of
 * 16 records (row stride 0x80, record stride 8 — its base pointer is
 * entry10-8 with a 1-based column, so col 1..16 maps to record offsets
 * 0..0x78).  For every record it zeroes the +0 dword and the +4 byte,
 * leaving +5..+7 — where +5 is the per-castle-slot back-pointer (see
 * save-format.md) — intact.  Then three world scalars are zeroed.
 * Sole caller is the orchestrator 0x497ced (init-only). */
void engine_game_init_clear_castle_shadow(void)
{
    for (int row = 0; row < 400; row++) {
        for (int col = 1; col < 0x11; col++) {    /* 1..16 (retail 1-based) */
            /* retail: mov dword [row*0x80 + col*8 + (entry10-8)], 0
             *         mov byte  [row*0x80 + col*8 + (entry10-4)], 0 */
            uint8_t *rec = g_castles_shadow + row * 0x80 + (col - 1) * 8;
            memset(rec, 0, 5);   /* +0 dword + +4 byte; +5..+7 preserved */
        }
    }
    s_slot_0055300c = 0;                          /* entry 66 */
    s_slot_00568da8 = 0;                          /* entry 67 */
    s_slot_00553f30 = 0;                          /* entry 68 */
}
