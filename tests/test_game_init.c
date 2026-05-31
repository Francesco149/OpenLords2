/*
 * Tests for engine_game_init_clear_tables (table-clear phase of the
 * new-game initialiser FUN_00497ced).
 *
 * The phase zeroes the three big per-record save blocks the later
 * seeders write into:
 *
 *   g_per_army_768      17 × 768 B   (FUN_0046f1dc → FUN_0046ea28)
 *   g_unit_table_20     13 × 20 B    (FUN_0046f2ae → FUN_0046eb09)
 *   g_per_territory_420 151 × 420 B  (FUN_0046f3cd → FUN_0046ead0)
 *
 * Each retail loop bound × stride covers the full named block, so the
 * verified post-condition is "every byte of each block is zero".  We
 * dirty the blocks first to prove the clear actually runs (a no-op
 * would pass a zero-init baseline).
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "game_init.h"
#include "engine.h"               /* engine_rng_step / engine_rng_in_t (seat shuffle) */
#include "save.h"
#include "save_schema_globals.h"  /* s_slot_<va> — seeded calendar scalars */
#include "engine_turn.h"          /* turn-handshake globals reset by the seed */
#include "engine_map_query.h"     /* cell-grid binding for the army probes */
#include "engine_territory.h"     /* entity placer + record/cell binding */
#include "engine_army_economy.h"  /* army-record bind (NULLed to isolate setup_realms) */

/* The save blocks under test live in save.c (linked into the suite). */

int test_game_init_clear_tables_zeroes_per_army(void)
{
    memset(g_per_army_768, 0xAB, sizeof g_per_army_768);
    engine_game_init_clear_tables();
    for (size_t i = 0; i < sizeof g_per_army_768; i++)
        if (g_per_army_768[i] != 0)
            T_FAIL("g_per_army_768[%zu] = %u, want 0", i, g_per_army_768[i]);
    return 0;
}

int test_game_init_clear_tables_zeroes_unit_table(void)
{
    memset(g_unit_table_20, 0xCD, sizeof g_unit_table_20);
    engine_game_init_clear_tables();
    for (size_t i = 0; i < sizeof g_unit_table_20; i++)
        if (g_unit_table_20[i] != 0)
            T_FAIL("g_unit_table_20[%zu] = %u, want 0", i, g_unit_table_20[i]);
    return 0;
}

int test_game_init_clear_tables_zeroes_territories(void)
{
    memset(g_per_territory_420, 0x5A, sizeof g_per_territory_420);
    engine_game_init_clear_tables();
    for (size_t i = 0; i < sizeof g_per_territory_420; i++)
        if (g_per_territory_420[i] != 0)
            T_FAIL("g_per_territory_420[%zu] = %u, want 0", i, g_per_territory_420[i]);
    return 0;
}

/* The clear must touch exactly the named blocks and not run past them
 * onto an adjacent block.  Sentinel the unit-table's neighbours by
 * clearing the per-army block (a different block) and confirming the
 * unit-table clear leaves a pre-seeded per-army byte alone when only
 * the unit clear is conceptually responsible — here we assert the full
 * phase zeroes all three and nothing relies on ordering side effects. */
int test_game_init_clear_tables_all_three_independent(void)
{
    memset(g_per_army_768,      0x11, sizeof g_per_army_768);
    memset(g_unit_table_20,     0x22, sizeof g_unit_table_20);
    memset(g_per_territory_420, 0x33, sizeof g_per_territory_420);

    engine_game_init_clear_tables();

    T_ASSERT(g_per_army_768[0]      == 0);
    T_ASSERT(g_per_army_768[sizeof g_per_army_768 - 1]           == 0);
    T_ASSERT(g_unit_table_20[0]     == 0);
    T_ASSERT(g_unit_table_20[sizeof g_unit_table_20 - 1]         == 0);
    T_ASSERT(g_per_territory_420[0] == 0);
    T_ASSERT(g_per_territory_420[sizeof g_per_territory_420 - 1] == 0);
    return 0;
}

/* ── Scalar-seed phase (engine_game_init_seed_scalars) ──────────────
 *
 * The phase writes 20 dword globals.  Each test pre-dirties every
 * target with a non-seed sentinel so a no-op can't pass on a zero
 * baseline, then asserts the exact retail post-condition.
 */

/* Dirty all 20 targets with a value distinct from every seeded one. */
static void dirty_scalar_targets(void)
{
    g_sim_turn_progress         = 0x7777;
    g_sim_turn_sync_baseline_ms = 0x7777u;
    g_anim_gate_53f0c4          = 0x7777;
    s_slot_0055cd5c = s_slot_00553edc = s_slot_00553228 = 0x7777u;
    s_slot_0057c900 = s_slot_00553e74 = s_slot_00553240 = 0x7777u;
    s_slot_0056d6b0 = s_slot_0057c934 = s_slot_0057c990 = 0x7777u;
    s_slot_00554470 = s_slot_0057c92c = s_slot_0055cd40 = 0x7777u;
    s_slot_0057c918 = s_slot_0057a0c4 = s_slot_0053e920 = 0x7777u;
    s_slot_0056d5e0 = s_slot_0055ce7c                   = 0x7777u;
}

/* The calendar block: year/season bases, their prev/next mirrors, and
 * the same-valued copies. */
int test_game_init_seed_scalars_calendar_block(void)
{
    dirty_scalar_targets();
    engine_game_init_seed_scalars();

    /* Year triple + copy (entries 20/21/22/28). */
    T_ASSERT(s_slot_00553edc == 0x4f3);          /* base = 1267 */
    T_ASSERT(s_slot_0057c900 == 0x4f2);          /* prev = 1266 */
    T_ASSERT(s_slot_00553e74 == 0x4f4);          /* next = 1268 */
    T_ASSERT(s_slot_00553228 == 0x4f3);          /* copy */

    /* Season triple + copy (entries 23/24/25/29). */
    T_ASSERT(s_slot_0057c934 == 3);              /* base */
    T_ASSERT(s_slot_00554470 == 2);              /* prev */
    T_ASSERT(s_slot_0057c92c == 4);              /* next (clamp dead at 4) */
    T_ASSERT(s_slot_0057c990 == 3);              /* copy */
    return 0;
}

/* The 9 globals seeded to a fixed non-derived value (8 zeros + the
 * lone 6), and the orphan/turn globals. */
int test_game_init_seed_scalars_fixed_values(void)
{
    dirty_scalar_targets();
    engine_game_init_seed_scalars();

    T_ASSERT(s_slot_0055ce7c == 6);              /* entry 190 */

    T_ASSERT(s_slot_0055cd5c == 0);              /* entry 30 */
    T_ASSERT(s_slot_00553240 == 0);              /* entry 31 */
    T_ASSERT(s_slot_0056d6b0 == 0);              /* entry 92 */
    T_ASSERT(s_slot_0055cd40 == 0);              /* entry 26 */
    T_ASSERT(s_slot_0057c918 == 0);              /* entry 27 */
    T_ASSERT(s_slot_0057a0c4 == 0);              /* entry 175 */
    T_ASSERT(s_slot_0053e920 == 0);              /* entry 176 */
    T_ASSERT(s_slot_0056d5e0 == 0);              /* entry 177 */

    T_ASSERT(g_sim_turn_progress         == 0);  /* DAT_00553fc8 */
    T_ASSERT(g_sim_turn_sync_baseline_ms == 0u); /* _DAT_0056d658 */
    T_ASSERT(g_anim_gate_53f0c4          == 0);  /* DAT_0053f0c4 */
    return 0;
}

/* ── Value-table seed (engine_game_init_seed_value_table) ────────────
 *
 * FUN_0042847c fills save entry 82 (s_slot_005532a0, 16 records × 16 B)
 * with record[i] = { defaults[i], 0 } and leaves +8..+15 of each record
 * alone.  The defaults are the retail DAT_004d8910 table.
 */

/* DAT_004d8910 — verified byte-for-byte from the PE .rdata. */
static const uint32_t k_expect_value_defaults[16] = {
    0x00, 0x02, 0x0c, 0x00,  0x01, 0x00, 0x01, 0x02,
    0x01, 0x0d, 0x10, 0x0a,  0x18, 0x17, 0x2c, 0x00,
};

int test_game_init_seed_value_table_defaults(void)
{
    /* Pre-dirty the whole 256-B block with a sentinel distinct from
     * every default (and from 0), so a no-op can't pass. */
    memset(s_slot_005532a0, 0xAB, sizeof s_slot_005532a0);

    engine_game_init_seed_value_table();

    for (int i = 0; i < 16; i++) {
        const uint8_t *rec = &s_slot_005532a0[i * 0x10];
        uint32_t v0, v4;
        memcpy(&v0, rec + 0, 4);
        memcpy(&v4, rec + 4, 4);
        if (v0 != k_expect_value_defaults[i])
            T_FAIL("record %d dword+0 = %u, want %u", i, v0,
                   k_expect_value_defaults[i]);
        if (v4 != 0)
            T_FAIL("record %d dword+4 = %u, want 0", i, v4);
    }
    return 0;
}

/* The seeder must touch ONLY the first 8 B of each 16-B record; the
 * trailing 8 B (+8..+15) stay as they were. */
int test_game_init_seed_value_table_leaves_tail_untouched(void)
{
    memset(s_slot_005532a0, 0x5C, sizeof s_slot_005532a0);

    engine_game_init_seed_value_table();

    for (int i = 0; i < 16; i++)
        for (int b = 8; b < 16; b++)
            if (s_slot_005532a0[i * 0x10 + b] != 0x5C)
                T_FAIL("record %d byte+%d = %u, want 0x5C (untouched)",
                       i, b, s_slot_005532a0[i * 0x10 + b]);
    return 0;
}

/* ============================================================== *
 *  engine_game_init_seat_shuffle (heavy seeder FUN_00497e65)
 * ============================================================== */

/* Retail RNG seeds (InitInstance FUN_004b2258 lines 83635-83636). */
#define RNG_SEED_A 0x54657687u
#define RNG_SEED_B 0x72641663u

/* Test-side mirror of game_init.c's rng_next_7b: one engine_rng_step,
 * thread the state, return the 7-bit-b mirror.  Lets the tests replay
 * the exact draw sequence the seeder consumes. */
static uint32_t t_rng_next_7b(engine_rng_in_t *rng)
{
    engine_rng_out_t out = {0};
    engine_rng_step(rng, &out);
    rng->state_a = out.new_state_a;
    rng->state_b = out.new_state_b;
    return out.rng_7bit_b;
}

/* Lay down a compacted 3-player roster (seats 1..3 occupied) plus a
 * seat-0 reserved payload; seats 4,5 empty.  Seat byte == index so the
 * test can prove id↔seat payloads travel together through the shuffle. */
static void seed_roster_3players(void)
{
    static const uint8_t ids[6] = { 0x09, 0x11, 0x22, 0x33, 0x00, 0x00 };
    for (int i = 0; i < 6; i++) {
        g_player_roster[i * 2]     = ids[i];
        g_player_roster[i * 2 + 1] = (uint8_t)i;   /* seat byte = index */
    }
}

/* Phase 1: the shuffle is a permutation of array slots — every occupied
 * payload survives exactly once, seat 0 is cleared, empty tail stays 0,
 * and each id keeps its own seat byte. */
int test_game_init_seat_shuffle_preserves_payloads(void)
{
    seed_roster_3players();
    engine_rng_in_t rng = { RNG_SEED_A, RNG_SEED_B };

    engine_game_init_seat_shuffle(&rng);

    /* Seat 0 id cleared; its seat byte left intact (retail clears only
     * the id bytes). */
    T_ASSERT_EQ_U(g_player_roster[0], 0);
    T_ASSERT_EQ_U(g_player_roster[1], 0);

    /* Seats 4,5 were empty and out of the 1..active probe range. */
    for (int i = 4; i < 6; i++) {
        T_ASSERT_EQ_U(g_player_roster[i * 2], 0);
        T_ASSERT_EQ_U(g_player_roster[i * 2 + 1], (uint8_t)i);
    }

    /* The three occupied ids each appear exactly once among seats 1..3,
     * paired with their original seat byte (0x11↔1, 0x22↔2, 0x33↔3). */
    int seen11 = 0, seen22 = 0, seen33 = 0;
    for (int i = 1; i <= 3; i++) {
        uint8_t id = g_player_roster[i * 2];
        uint8_t st = g_player_roster[i * 2 + 1];
        if (id == 0x11) { seen11++; T_ASSERT_EQ_U(st, 1); }
        else if (id == 0x22) { seen22++; T_ASSERT_EQ_U(st, 2); }
        else if (id == 0x33) { seen33++; T_ASSERT_EQ_U(st, 3); }
        else T_FAIL("seat %d holds unexpected id 0x%02x", i, id);
    }
    if (seen11 != 1 || seen22 != 1 || seen33 != 1)
        T_FAIL("payload lost: counts 0x11=%d 0x22=%d 0x33=%d",
               seen11, seen22, seen33);
    return 0;
}

/* active == 0 (only seat 0 occupied): phase 1 consumes no draws and the
 * id bytes are all cleared. */
int test_game_init_seat_shuffle_active_zero(void)
{
    for (int i = 0; i < 12; i++) g_player_roster[i] = 0;
    g_player_roster[0] = 0x09;          /* seat 0 only */
    engine_rng_in_t rng = { RNG_SEED_A, RNG_SEED_B };

    engine_game_init_seat_shuffle(&rng);

    for (int i = 0; i < 6; i++)
        T_ASSERT_EQ_U(g_player_roster[i * 2], 0);
    return 0;
}

/* Phase 2: the permutation table holds only values 0..47, and every
 * non-zero value 1..47 appears at most once (slot value 0 aliases empty,
 * so it is exempt from the uniqueness check). */
int test_game_init_seat_shuffle_perm_unique(void)
{
    seed_roster_3players();
    engine_rng_in_t rng = { RNG_SEED_A, RNG_SEED_B };

    engine_game_init_seat_shuffle(&rng);

    int count[48] = {0};
    for (int i = 0; i < 48; i++) {
        uint8_t v = s_slot_0057cae0[i];
        if (v > 47) T_FAIL("perm[%d] = %u out of range 0..47", i, v);
        if (v != 0 && ++count[v] > 1)
            T_FAIL("perm value %u appears more than once", v);
    }
    return 0;
}

/* Phase 3: every value-table cell is one of the ladder outputs. */
int test_game_init_seat_shuffle_value_in_range(void)
{
    seed_roster_3players();
    engine_rng_in_t rng = { RNG_SEED_A, RNG_SEED_B };

    engine_game_init_seat_shuffle(&rng);

    for (int i = 0; i < 48; i++) {
        uint8_t v = s_slot_00553080[i];
        if (v != 0 && v != 6 && v != 11 && v != 16 &&
            v != 21 && v != 26 && v != 41 && v != 100)
            T_FAIL("value[%d] = %u not in ladder set", i, v);
    }
    return 0;
}

/* Phase 3 + draw position: replay the RNG independently — skip the
 * phase-1 (active) and phase-2 (48) draws, then recompute the value
 * ladder and compare cell-for-cell.  Locks both the ladder mapping and
 * that phases 1+2 consumed exactly active+48 draws. */
int test_game_init_seat_shuffle_value_ladder_replay(void)
{
    seed_roster_3players();
    const int active = 3;               /* matches seed_roster_3players */

    engine_rng_in_t live = { RNG_SEED_A, RNG_SEED_B };
    engine_game_init_seat_shuffle(&live);

    engine_rng_in_t rep = { RNG_SEED_A, RNG_SEED_B };
    for (int i = 0; i < active + 48; i++) /* skip phase 1 + phase 2 */
        (void)t_rng_next_7b(&rep);
    for (int i = 0; i < 48; i++) {
        uint32_t r = t_rng_next_7b(&rep) & 7;
        uint8_t want = (r == 5) ? 0 : (r == 6) ? 100 : (uint8_t)(r * 5 + 6);
        T_ASSERT_EQ_U(s_slot_00553080[i], want);
    }
    return 0;
}

/* Total draw count is exactly active + 48 + 48: the live state after the
 * seeder equals active+96 independent advances from the same seed. */
int test_game_init_seat_shuffle_draw_count(void)
{
    seed_roster_3players();
    const int active = 3;

    engine_rng_in_t live = { RNG_SEED_A, RNG_SEED_B };
    engine_game_init_seat_shuffle(&live);

    engine_rng_in_t rep = { RNG_SEED_A, RNG_SEED_B };
    for (int i = 0; i < active + 48 + 48; i++)
        (void)t_rng_next_7b(&rep);

    T_ASSERT_EQ_U(live.state_a, rep.state_a);
    T_ASSERT_EQ_U(live.state_b, rep.state_b);
    return 0;
}

/* Same seed → identical roster + both tables (pure given rng). */
int test_game_init_seat_shuffle_deterministic(void)
{
    uint8_t roster1[12], perm1[48], val1[48];

    seed_roster_3players();
    engine_rng_in_t r1 = { RNG_SEED_A, RNG_SEED_B };
    engine_game_init_seat_shuffle(&r1);
    memcpy(roster1, g_player_roster,  12);
    memcpy(perm1,   s_slot_0057cae0,  48);
    memcpy(val1,    s_slot_00553080,  48);

    seed_roster_3players();
    engine_rng_in_t r2 = { RNG_SEED_A, RNG_SEED_B };
    engine_game_init_seat_shuffle(&r2);

    T_ASSERT_MEM_EQ(roster1, g_player_roster, 12);
    T_ASSERT_MEM_EQ(perm1,   s_slot_0057cae0, 48);
    T_ASSERT_MEM_EQ(val1,    s_slot_00553080, 48);
    T_ASSERT_EQ_U(r1.state_a, r2.state_a);
    T_ASSERT_EQ_U(r1.state_b, r2.state_b);
    return 0;
}

/* ============================================================== *
 *  engine_game_init_seed_unit_table (leaf seeder FUN_004ac904)
 * ============================================================== */

/* Independent mirrors of the six retail .rdata columns (verified
 * byte-for-byte from the PE) and the config→count lookup table.  Entry
 * [0] of each column is the real overlap byte/dword and is never read
 * (rows are 1-based). */
static const int32_t  exp_count_by_config[20] = {     /* DAT_004de820 */
    0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12, 12, 12, 12, 12,
};
static const uint8_t  exp_col_a[13] =                 /* DAT_004de79f → +0x2,+0x4 */
    { 0x00,0x01,0x02,0x04,0x05,0x07,0x08,0x0a,0x0b,0x0d,0x0e,0x10,0x11 };
static const uint8_t  exp_col_b[13] =                 /* DAT_004de78f → +0x5 */
    { 0x00,0x04,0x04,0x05,0x05,0x03,0x03,0x01,0x01,0x02,0x02,0x06,0x06 };
static const uint8_t  exp_col_c[13] =                 /* DAT_004de7af → +0x6,+0x7 */
    { 0x00,0x05,0x03,0x06,0x04,0x02,0x07,0x05,0x02,0x01,0x03,0x06,0x07 };
static const uint32_t exp_col_d[13] =                 /* DAT_004de75c → +0x8 */
    { 0x62,0x64,0xc8,0x96,0xc8,0xc8,0x64,0x64,0xc8,0x96,0xfa,0x32,0x64 };
static const uint32_t exp_col_e[13] =                 /* DAT_004de7bc → +0xc */
    { 0x0000,0x0708,0x0dac,0x0bb8,0x0fa0,0x157c,0x0a8c,
      0x0bb8,0x1770,0x076c,0x0c1c,0x0a8c,0x157c };
static const uint32_t exp_col_f[13] =                 /* DAT_004de7ec → +0x10 */
    { 0x157c,0x00b4,0x015e,0x012c,0x0190,0x0226,0x010e,
      0x012c,0x0258,0x00be,0x0136,0x010e,0x0226 };

/* Assert record i (1-based) matches the column mirrors field-for-field,
 * and that its three retail-untouched bytes (+0/+1/+3) are zero.  Returns
 * 0 on success, non-zero on the first failed assertion (the T_* macros
 * `return 1`), so callers propagate with `if (check_unit_record(i)) ...`. */
static int check_unit_record(int i)
{
    const uint8_t *rec = &g_unit_table_20[i * 0x14];
    uint32_t d, e, f;
    memcpy(&d, rec + 0x8, 4);
    memcpy(&e, rec + 0xc, 4);
    memcpy(&f, rec + 0x10, 4);

    T_ASSERT_EQ_U(rec[0x2], exp_col_a[i]);
    T_ASSERT_EQ_U(rec[0x4], exp_col_a[i]);    /* dup of +0x2 */
    T_ASSERT_EQ_U(rec[0x5], exp_col_b[i]);
    T_ASSERT_EQ_U(rec[0x6], exp_col_c[i]);
    T_ASSERT_EQ_U(rec[0x7], exp_col_c[i]);    /* dup of +0x6 */
    T_ASSERT_EQ_U(d, exp_col_d[i]);
    T_ASSERT_EQ_U(e, exp_col_e[i]);
    T_ASSERT_EQ_U(f, exp_col_f[i]);

    T_ASSERT_EQ_U(rec[0x0], 0);               /* retail never writes these */
    T_ASSERT_EQ_U(rec[0x1], 0);
    T_ASSERT_EQ_U(rec[0x3], 0);
    return 0;
}

/* Full table: config index 13 selects the max count (12), so every
 * record 1..12 is populated; field-for-field against the column
 * mirrors.  Pre-dirty the block so the internal clear must run. */
int test_game_init_seed_unit_table_full(void)
{
    memset(g_unit_table_20, 0xEE, sizeof g_unit_table_20);
    s_slot_0056d5dc = 13;                     /* → count 12 */
    s_slot_00554030 = 0x7777;                 /* clobber the persisted count */

    engine_game_init_seed_unit_table();

    T_ASSERT_EQ_U(s_slot_00554030, 12);       /* count persisted */
    for (int i = 1; i <= 12; i++)
        if (check_unit_record(i)) return 1;
    return 0;
}

/* Record 0 is never written by the 1-based loop — it stays cleared even
 * though the block was dirtied beforehand. */
int test_game_init_seed_unit_table_record0_cleared(void)
{
    memset(g_unit_table_20, 0x5A, sizeof g_unit_table_20);
    s_slot_0056d5dc = 13;                     /* → count 12 */

    engine_game_init_seed_unit_table();

    for (int b = 0; b < 0x14; b++)
        if (g_unit_table_20[b] != 0)
            T_FAIL("record 0 byte+%d = %u, want 0", b, g_unit_table_20[b]);
    return 0;
}

/* A partial count: config index 4 → count 3.  Records 1..3 populated;
 * records 4..12 left cleared (the clear ran, the loop stopped early). */
int test_game_init_seed_unit_table_partial_count(void)
{
    memset(g_unit_table_20, 0xCD, sizeof g_unit_table_20);
    s_slot_0056d5dc = 4;                      /* exp_count_by_config[4] == 3 */

    engine_game_init_seed_unit_table();

    T_ASSERT_EQ_U(s_slot_00554030, 3);
    T_ASSERT_EQ_U(exp_count_by_config[4], 3); /* lock the expectation source */

    for (int i = 1; i <= 3; i++)
        if (check_unit_record(i)) return 1;

    for (int i = 4; i <= 12; i++)
        for (int b = 0; b < 0x14; b++)
            if (g_unit_table_20[i * 0x14 + b] != 0)
                T_FAIL("record %d byte+%d = %u, want 0 (beyond count)",
                       i, b, g_unit_table_20[i * 0x14 + b]);
    return 0;
}

/* The config→count selection matches the lookup table across its full
 * 0..19 domain, and is persisted to entry 122 each call. */
int test_game_init_seed_unit_table_count_selection(void)
{
    for (int sel = 0; sel < 20; sel++) {
        s_slot_0056d5dc = (uint32_t)sel;
        s_slot_00554030 = 0x7777;
        engine_game_init_seed_unit_table();

        /* The clamp only catches >12 / <0, so a table value of 0
         * (selector 0) is persisted as-is, not bumped to 1. */
        T_ASSERT_EQ_U(s_slot_00554030, (uint32_t)exp_count_by_config[sel]);
    }
    return 0;
}

/* ============================================================== *
 *  engine_game_init_purge_inactive_records (FUN_0049b35e)
 * ============================================================== */

#define PURGE_STRIDE   0x160
#define PURGE_ACTIVE   0x04
#define PURGE_HASDATA  0x05
#define PURGE_OPPTYPE  0x07
#define PREC(s)        (&g_player_strategic_352[(s) * PURGE_STRIDE])

/* Set the three decision flags of a slot's record. */
static void set_slot_flags(int slot, uint8_t active, uint8_t has_data,
                           uint8_t opp_type)
{
    uint8_t *r = PREC(slot);
    r[PURGE_ACTIVE]  = active;
    r[PURGE_HASDATA] = has_data;
    r[PURGE_OPPTYPE] = opp_type;
}

static int slot_all_zero(int slot)
{
    const uint8_t *r = PREC(slot);
    for (int b = 0; b < PURGE_STRIDE; b++)
        if (r[b] != 0) return 0;
    return 1;
}

/* Slots that are unoccupied (+0x4 == 0) or occupied-but-uninitialised
 * (+0x5 == 0 AND +0x7 == 0) get their whole record zeroed. */
int test_game_init_purge_zeroes_inactive(void)
{
    memset(g_player_strategic_352, 0xAB, sizeof g_player_strategic_352);

    set_slot_flags(1, 0,    0xAB, 0xAB);   /* unoccupied            → zero */
    set_slot_flags(2, 0x01, 0,    0);      /* occupied, no data/opp → zero */
    set_slot_flags(5, 0,    0,    0);      /* unoccupied            → zero */

    /* Keep slots 3,4 active so they survive and can't mask a bug. */
    set_slot_flags(3, 0x01, 0x01, 0);
    set_slot_flags(4, 0x01, 0,    0x02);

    engine_game_init_purge_inactive_records();

    T_ASSERT(slot_all_zero(1));
    T_ASSERT(slot_all_zero(2));
    T_ASSERT(slot_all_zero(5));
    return 0;
}

/* Active slots are preserved byte-for-byte; the half-init exemption
 * needs BOTH +0x5 and +0x7 clear, so a set opp-type alone keeps the
 * record.  Slot 0 (reserved front slot) is never visited even when it
 * looks inactive. */
int test_game_init_purge_preserves_active_and_slot0(void)
{
    memset(g_player_strategic_352, 0xAB, sizeof g_player_strategic_352);

    /* Slot 0 looks unoccupied (+0x4 == 0) but must be skipped entirely. */
    set_slot_flags(0, 0, 0, 0);
    /* Slot 3: occupied + has_data → keep. */
    set_slot_flags(3, 0x01, 0x01, 0);
    /* Slot 4: occupied, no has_data but opp_type set → keep (AND fails). */
    set_slot_flags(4, 0x01, 0, 0x02);

    /* Snapshot the records we expect to survive. */
    uint8_t snap0[PURGE_STRIDE], snap3[PURGE_STRIDE], snap4[PURGE_STRIDE];
    memcpy(snap0, PREC(0), PURGE_STRIDE);
    memcpy(snap3, PREC(3), PURGE_STRIDE);
    memcpy(snap4, PREC(4), PURGE_STRIDE);

    engine_game_init_purge_inactive_records();

    T_ASSERT_MEM_EQ(snap0, PREC(0), PURGE_STRIDE);   /* slot 0 untouched */
    T_ASSERT_MEM_EQ(snap3, PREC(3), PURGE_STRIDE);
    T_ASSERT_MEM_EQ(snap4, PREC(4), PURGE_STRIDE);
    return 0;
}

/* ============================================================== *
 *  engine_game_init_reset_player_subtables (FUN_004a1c53 + FUN_004a2584)
 * ============================================================== */

/* Element (slot, sub) of the per-player sub-table: five 0x10-B elements
 * at record offset 0x94 (= 0x84 + sub*0x10), sub = 1..5. */
#define SUB_ELEM(slot, sub)  (&g_player_strategic_352[(slot) * 0x160 + 0x84 + (sub) * 0x10])

/* Assert one sub-element matches the reset post-condition for the given
 * expected lead byte (5 for AI, else 0).  Bytes +0x6/+0x7 are expected to
 * be the 0xAB pre-dirty sentinel (untouched). */
static int check_sub_elem(int slot, int sub, uint8_t lead)
{
    const uint8_t *e = SUB_ELEM(slot, sub);
    uint32_t d8; uint16_t we;
    memcpy(&d8, e + 8,   4);
    memcpy(&we, e + 0xe, 2);

    T_ASSERT_EQ_U(e[0x0], lead);
    T_ASSERT_EQ_U(e[0x1], 0);
    T_ASSERT_EQ_U(e[0x2], 0);
    T_ASSERT_EQ_U(e[0x3], 0);
    T_ASSERT_EQ_U(e[0x4], 0);
    T_ASSERT_EQ_U(e[0x5], 0);
    T_ASSERT_EQ_U(e[0x6], 0xAB);          /* untouched */
    T_ASSERT_EQ_U(e[0x7], 0xAB);          /* untouched */
    T_ASSERT_EQ_U(d8, 0);
    T_ASSERT_EQ_U(e[0xc], 0);
    T_ASSERT_EQ_U(e[0xd], 1);
    T_ASSERT_EQ_U(we, 0);
    return 0;
}

/* The entry-146 grid clear (FUN_004a2584): each of the 6×5 8-B elements
 * has +0/+1/+2 and the dword +4 zeroed, byte +3 left alone; plus the
 * entry-144 scalar reset to 0. */
int test_game_init_reset_subtables_clears_grid(void)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_0053f0f0, 0xAB, sizeof s_slot_0053f0f0);
    s_slot_0057a0f8 = 0x7777;

    engine_game_init_reset_player_subtables();

    T_ASSERT_EQ_U(s_slot_0057a0f8, 0);
    for (int row = 0; row < 6; row++)
        for (int col = 0; col < 5; col++) {
            const uint8_t *g = &s_slot_0053f0f0[row * 0x28 + col * 8];
            uint32_t d4; memcpy(&d4, g + 4, 4);
            T_ASSERT_EQ_U(g[0], 0);
            T_ASSERT_EQ_U(g[1], 0);
            T_ASSERT_EQ_U(g[2], 0);
            T_ASSERT_EQ_U(g[3], 0xAB);    /* byte +3 untouched */
            T_ASSERT_EQ_U(d4, 0);
        }
    return 0;
}

/* The per-player sub-table reset: AI players (active, no human data) seed
 * the lead byte to 5; human and inactive players seed it to 0.  Element
 * fields and the two standalone bytes (+0x81/+0xeb) are reset; +0x6/+0x7
 * of each element survive. */
int test_game_init_reset_subtables_lead_byte(void)
{
    memset(g_player_strategic_352, 0xAB, sizeof g_player_strategic_352);

    /* slot 1 = AI (active, no data); slot 2 = human (active + data);
     * slot 3 = inactive. */
    g_player_strategic_352[1 * 0x160 + PURGE_ACTIVE]  = 1;
    g_player_strategic_352[1 * 0x160 + PURGE_HASDATA] = 0;
    g_player_strategic_352[2 * 0x160 + PURGE_ACTIVE]  = 1;
    g_player_strategic_352[2 * 0x160 + PURGE_HASDATA] = 1;
    g_player_strategic_352[3 * 0x160 + PURGE_ACTIVE]  = 0;
    g_player_strategic_352[4 * 0x160 + PURGE_ACTIVE]  = 0;
    g_player_strategic_352[5 * 0x160 + PURGE_ACTIVE]  = 0;
    s_slot_0057c8cc = 9;                  /* local = none here */

    engine_game_init_reset_player_subtables();

    for (int sub = 1; sub <= 5; sub++) {
        if (check_sub_elem(1, sub, 5)) return 1;   /* AI → 5 */
        if (check_sub_elem(2, sub, 0)) return 1;   /* human → 0 */
        if (check_sub_elem(3, sub, 0)) return 1;   /* inactive → 0 */
    }

    /* Standalone bytes zeroed for every slot 1..5. */
    for (int slot = 1; slot <= 5; slot++) {
        T_ASSERT_EQ_U(g_player_strategic_352[slot * 0x160 + 0x81], 0);
        T_ASSERT_EQ_U(g_player_strategic_352[slot * 0x160 + 0xeb], 0);
    }
    return 0;
}

/* Slot 0 (reserved front slot) is never visited, and the parent recomputes
 * the first-active-non-local cursor (s_slot_00553fbc). */
int test_game_init_reset_subtables_slot0_and_cursor(void)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_0053f0f0, 0, sizeof s_slot_0053f0f0);

    /* Slot 0 carries sentinels that the reset must not touch. */
    memset(&g_player_strategic_352[0], 0x5A, 0x160);

    /* Slots 1,2 active; local = 1 → cursor should pick slot 2. */
    g_player_strategic_352[1 * 0x160 + PURGE_ACTIVE] = 1;
    g_player_strategic_352[2 * 0x160 + PURGE_ACTIVE] = 1;
    s_slot_0057c8cc = 1;
    s_slot_00553fbc = 0x77;

    uint8_t snap0[0x160];
    memcpy(snap0, &g_player_strategic_352[0], 0x160);

    engine_game_init_reset_player_subtables();

    T_ASSERT_MEM_EQ(snap0, &g_player_strategic_352[0], 0x160);  /* slot 0 untouched */
    T_ASSERT_EQ_U(s_slot_00553fbc, 2);                          /* cursor recomputed */
    return 0;
}

/* ── engine_game_init_clear_castle_shadow (FUN_004ae74d) ───────────────
 *
 * g_castles_shadow (entry 10) is 6400 8-byte castle records.  The clear
 * zeroes each record's +0 dword and +4 byte, preserving +5..+7.  We
 * dirty the whole block with 0xAB so the preserved tail is observable
 * and a no-op clear would fail.  ASan guards against the 1-based-column
 * arithmetic running past the 51200-byte array. */
int test_game_init_clear_castle_shadow_transient(void)
{
    memset(g_castles_shadow, 0xAB, sizeof g_castles_shadow);

    engine_game_init_clear_castle_shadow();

    for (int i = 0; i < 6400; i++) {
        const uint8_t *rec = g_castles_shadow + i * 8;
        for (int b = 0; b < 5; b++)
            if (rec[b] != 0)
                T_FAIL("record %d byte +%d not cleared (got 0x%02x)",
                       i, b, rec[b]);
        for (int b = 5; b < 8; b++)
            if (rec[b] != 0xAB)
                T_FAIL("record %d byte +%d clobbered (got 0x%02x)",
                       i, b, rec[b]);
    }
    return 0;
}

int test_game_init_clear_castle_shadow_scalars(void)
{
    memset(g_castles_shadow, 0, sizeof g_castles_shadow);
    s_slot_0055300c = 0x1111;   /* entry 66 */
    s_slot_00568da8 = 0x2222;   /* entry 67 */
    s_slot_00553f30 = 0x3333;   /* entry 68 */

    engine_game_init_clear_castle_shadow();

    T_ASSERT_EQ_U(s_slot_0055300c, 0);
    T_ASSERT_EQ_U(s_slot_00568da8, 0);
    T_ASSERT_EQ_U(s_slot_00553f30, 0);
    return 0;
}

/* ── Army-neighbourhood probes (FUN_00428007 / FUN_00428078) ─────────── */

enum { ARMY_GRID = 0x40, ARMY_STRIDE = 8, ARMY_COORD_OFF = 0x6c };
static uint8_t g_army_cells[ARMY_GRID * ARMY_GRID * ARMY_STRIDE];

/* Set every cell's raw-flag byte (+1) to v; head-link bytes stay 0. */
static void army_fill_rawflag(uint8_t v)
{
    for (int i = 0; i < ARMY_GRID * ARMY_GRID; i++)
        g_army_cells[i * ARMY_STRIDE + 1] = v;
}

/* Reset the cell grid + per-army block, bind the grid, and seat army
 * `id`'s map coordinate into the 3-byte-stride sub-table at +0x6c. */
static void army_setup(int id, int col, int row, uint8_t fill)
{
    memset(g_army_cells, 0, sizeof g_army_cells);
    if (fill) army_fill_rawflag(fill);
    engine_map_query_bind_cell_descriptors(g_army_cells);

    memset(g_per_army_768, 0, sizeof g_per_army_768);
    g_per_army_768[ARMY_COORD_OFF + id * 3]     = (uint8_t)col;
    g_per_army_768[ARMY_COORD_OFF + id * 3 + 1] = (uint8_t)row;
}

static void army_set_rawflag(int col, int row, uint8_t v)
{
    g_army_cells[(row * ARMY_GRID + col) * ARMY_STRIDE + 1] = v;
}

/* bit0 probe: a rawflag-bit0 cell at Chebyshev distance 3 is inside the
 * radius-3 box; one at distance 4 is not. */
int test_game_init_army_bit0_within_radius3(void)
{
    army_setup(1, 20, 20, 4 /* surround with "neither" flag */);
    army_set_rawflag(23, 20, 0x01);     /* distance 3 */
    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_bit0(1, NULL, NULL), 1);

    army_setup(1, 20, 20, 4);
    army_set_rawflag(24, 20, 0x01);     /* distance 4 — out of reach */
    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_bit0(1, NULL, NULL), 0);
    return 0;
}

/* only_bit1 probe: matches a flag∈{0,2} cell within radius 3.  With the
 * grid filled with flag 4 (neither 0 nor 2), only the carved cell hits. */
int test_game_init_army_only_bit1_within_radius3(void)
{
    army_setup(1, 20, 20, 4);
    army_set_rawflag(22, 22, 0x02);     /* flag 2 → only_bit1 match, dist 2 */
    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_only_bit1(1, NULL, NULL), 1);
    /* bit0 probe must NOT see a flag-2 cell. */
    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_bit0(1, NULL, NULL), 0);

    army_setup(1, 20, 20, 4);           /* no flag∈{0,2} anywhere */
    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_only_bit1(1, NULL, NULL), 0);
    return 0;
}

/* The coordinate sub-table is 3-byte-stride (r2 `id*3`), NOT the 0x300
 * per-army-record stride Ghidra rendered.  Seat two armies one record
 * apart and confirm each probe reads its own (col,row): army 1 sits on a
 * matching cell, army 2 three bytes further sits far away. */
int test_game_init_army_coord_stride_is_3(void)
{
    memset(g_army_cells, 0, sizeof g_army_cells);
    army_fill_rawflag(4);
    engine_map_query_bind_cell_descriptors(g_army_cells);

    memset(g_per_army_768, 0, sizeof g_per_army_768);
    g_per_army_768[ARMY_COORD_OFF + 1 * 3]     = 10;  /* army 1 col */
    g_per_army_768[ARMY_COORD_OFF + 1 * 3 + 1] = 10;  /* army 1 row */
    g_per_army_768[ARMY_COORD_OFF + 2 * 3]     = 50;  /* army 2 col */
    g_per_army_768[ARMY_COORD_OFF + 2 * 3 + 1] = 50;  /* army 2 row */

    army_set_rawflag(10, 10, 0x01);     /* feature on army 1 only */

    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_bit0(1, NULL, NULL), 1);
    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_bit0(2, NULL, NULL), 0);
    return 0;
}

/* The probes report the matched cell via the out-params (quirk #130). */
int test_game_init_army_probe_reports_matched_cell(void)
{
    army_setup(1, 20, 20, 4);
    army_set_rawflag(22, 21, 0x01);     /* a bit0 feature within radius 3 */

    int col = -1, row = -1;
    T_ASSERT_EQ_I(engine_game_init_army_near_rawflag_bit0(1, &col, &row), 1);
    T_ASSERT_EQ_I(col, 22);
    T_ASSERT_EQ_I(row, 21);
    return 0;
}

/* ── Army start-marker placement (FUN_00427ed0) ──────────────────────── */

#define TREC(slot, off) g_per_territory_420[(slot) * 0x1a4 + (off)]

/* Bind the shared cell grid to both the read (query) and write
 * (territory) seats, and reset the real entity table. */
static void army_starts_setup(void)
{
    memset(g_army_cells, 0, sizeof g_army_cells);
    engine_map_query_bind_cell_descriptors(g_army_cells);
    engine_territory_bind_cells(g_army_cells);

    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    engine_territory_bind_records(g_per_territory_420);

    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(s_slot_00569518, 0, sizeof s_slot_00569518);
    s_slot_005530b4 = 0xdead;
}

static void army_seat(int id, int col, int row)
{
    g_per_army_768[ARMY_COORD_OFF + id * 3]     = (uint8_t)col;
    g_per_army_768[ARMY_COORD_OFF + id * 3 + 1] = (uint8_t)row;
}

/* Two seated armies, each with a feature on its own cell, are placed:
 * the counter reaches 2 and each marker lands AT the matched cell with
 * the army id/index stamped in. */
int test_game_init_place_army_starts_places_two(void)
{
    army_starts_setup();
    army_seat(7, 10, 10);
    army_seat(4, 40, 40);
    army_set_rawflag(10, 10, 0x01);
    army_set_rawflag(40, 40, 0x01);
    s_slot_00569518[0] = 7;
    s_slot_00569518[1] = 4;             /* [2] = 0 → terminator */

    engine_game_init_place_army_starts();

    T_ASSERT_EQ_U(s_slot_005530b4, 2);

    /* slot 1 ← army 7 at (10,10), loop index 0 */
    T_ASSERT_EQ_I(TREC(1, 0x00), 6);    /* placer type   */
    T_ASSERT_EQ_I(TREC(1, 0x08), 3);    /* placer kind   */
    T_ASSERT_EQ_I(TREC(1, 0x0a), 10);   /* matched col   */
    T_ASSERT_EQ_I(TREC(1, 0x0b), 10);   /* matched row   */
    T_ASSERT_EQ_I(TREC(1, 0x10), 7);    /* army id       */
    T_ASSERT_EQ_I(TREC(1, 0x150), 1);
    T_ASSERT_EQ_I(TREC(1, 0x166), 100);
    T_ASSERT_EQ_I(TREC(1, 0x167), 7);
    T_ASSERT_EQ_I(TREC(1, 0x164), 1);
    T_ASSERT_EQ_I(TREC(1, 0x14f), 0);   /* loop index    */

    /* slot 2 ← army 4 at (40,40), loop index 1 */
    T_ASSERT_EQ_I(TREC(2, 0x0a), 40);
    T_ASSERT_EQ_I(TREC(2, 0x10), 4);
    T_ASSERT_EQ_I(TREC(2, 0x167), 4);
    T_ASSERT_EQ_I(TREC(2, 0x14f), 1);
    return 0;
}

/* A zero id terminates the walk; an army with no nearby feature is
 * skipped (counter not bumped) but the walk continues. */
int test_game_init_place_army_starts_skips_and_terminates(void)
{
    army_starts_setup();
    army_fill_rawflag(4);               /* default: matches neither probe */

    army_seat(3, 20, 20);
    army_seat(5, 50, 50);
    army_seat(9, 30, 30);
    army_set_rawflag(20, 20, 0x01);     /* feature for army 3 only */
    army_set_rawflag(30, 30, 0x01);     /* feature for army 9 (must be unreached) */

    s_slot_00569518[0] = 3;             /* placed   */
    s_slot_00569518[1] = 5;             /* skipped  */
    s_slot_00569518[2] = 0;             /* terminator */
    s_slot_00569518[3] = 9;             /* must NOT be processed */

    engine_game_init_place_army_starts();

    T_ASSERT_EQ_U(s_slot_005530b4, 1);  /* only army 3 placed */
    T_ASSERT_EQ_I(TREC(1, 0x10), 3);    /* slot 1 = army 3 */
    T_ASSERT_EQ_I(TREC(2, 0x00), 0);    /* no second record */
    /* army 9's cell was never claimed */
    T_ASSERT_EQ_I(g_army_cells[(30 * 0x40 + 30) * 8 + 5], 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * engine_game_init_setup_realms (FUN_0049bd99) — new-game per-realm /
 * country setup pass.
 *
 * The function writes the per-army (g_per_army_768) and per-player
 * (g_player_strategic_352) save blocks directly; all of its economy /
 * territory callees route through their module binds.  We NULL every one
 * of those binds so the callees no-op, isolating setup_realms's own field
 * writes for verification.
 * ────────────────────────────────────────────────────────────────── */

#define AREC(key)   (&g_per_army_768[(key) * 0x300])
#define PREC(slot)  (&g_player_strategic_352[(slot) * 0x160])
#define AI32(p, o)  (*(int32_t *)((p) + (o)))

/* Player-record flag offsets (private to game_init.c; restated locally). */
#define STRAT_HAS_DATA   0x05    /* human player_has_data flag */
#define STRAT_OPP_TYPE   0x07    /* AI opponent-type code */

static void realms_reset(void)
{
    memset(g_per_army_768,         0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_player_roster,        0, sizeof g_player_roster);
    memset(g_per_territory_420,    0, sizeof g_per_territory_420);

    /* Force every callee to no-op (test only setup_realms's own writes). */
    engine_territory_bind_army_records(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_cell_roster(NULL);
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_bind_cells(NULL);
    engine_army_economy_bind_cell_roster(NULL);
    engine_army_economy_bind_resource_pool(NULL);
    engine_game_init_bind_troop_spawn(NULL);

    /* Clear the troop muster pool via a benign run-then-reset is not
     * possible, so zero it through the accessor. */
    for (int slot = 0; slot < 6; slot++)
        memset(engine_game_init_troop_pool_row(slot), 0, 0x80);

    s_slot_0056d5dc = 0;     /* army count   */
    s_slot_0057c8cc = 0;     /* local player */
    s_slot_0057c934 = 0;     /* season (phase arg) */
    s_slot_0057c92c = 0;     /* season-next (phase arg) */
    s_slot_0053f23c = 0;     /* density      */
}

/* Phase 1 — every army gets the economy preset row; with no player owning
 * any army the finalise pass adds the +100 gold head-start to each. */
int test_game_init_setup_realms_army_seed(void)
{
    realms_reset();
    engine_game_init_set_realm_presets(0, 0, 0, 0, 0);   /* economy preset 0 */
    s_slot_0056d5dc = 3;

    engine_game_init_setup_realms();

    for (int key = 1; key <= 3; key++) {
        uint8_t *r = AREC(key);
        /* gold 0x0a + 100 unowned bonus on both copies (+0x224/+0x228) */
        T_ASSERT_EQ_I(AI32(r, 0x224), 0x0a + 100);
        T_ASSERT_EQ_I(AI32(r, 0x228), 0x0a + 100);
        /* stock 0x28, population 0xa7 (no finalise bonus) */
        T_ASSERT_EQ_I(AI32(r, 0x250), 0x28);
        T_ASSERT_EQ_I(AI32(r, 0x254), 0x28);
        T_ASSERT_EQ_I(AI32(r, 0x024), 0xa7);
        T_ASSERT_EQ_I(AI32(r, 0x028), 0xa7);
        T_ASSERT_EQ_I(r[0x0b], 0x2d);
        T_ASSERT_EQ_I(r[0x0d], 0x29);
        T_ASSERT_EQ_I(r[0x0c], 0x29);             /* copy of +0xd */
    }
    /* Army 4 is past the count — untouched. */
    for (int o = 0; o < 0x300; o++)
        T_ASSERT_EQ_I(AREC(4)[o], 0);
    return 0;
}

/* Phase 2 — a human player mapped to an army: record defaults, gold base,
 * troop muster (preset 2 → row [.,.,.,0x32,0x32,0x32,.]), resource pool, the
 * economic-model gate latch, and the owned-army "complete" flag. */
int test_game_init_setup_realms_player_human(void)
{
    realms_reset();
    engine_game_init_set_realm_presets(0, 2, 2, 500, 0);
    s_slot_0056d5dc = 1;

    PREC(1)[STRAT_HAS_DATA] = 1;       /* human */
    PREC(1)[0x0a]           = 0x55;    /* identity byte copied to army +0x07 */
    g_player_roster[1 * 2]  = 1;       /* player 1 → army 1 */
    AREC(1)[0x295]          = 7;       /* pre-set model gate flag (slot 0, +1) */

    engine_game_init_setup_realms();

    uint8_t *p = PREC(1);
    T_ASSERT_EQ_I(p[0x04], 1);              /* ACTIVE latched */
    T_ASSERT_EQ_I(p[0x29], 1);              /* seated defaults */
    T_ASSERT_EQ_I(p[0x2a], 1);
    T_ASSERT_EQ_I(p[0xe4], 1);              /* player → army id */
    T_ASSERT_EQ_I(AI32(p, 0x118), 500);    /* gold base (no territory bonus) */

    /* Resource pool: three 0x32 defaults + preset row 2. */
    T_ASSERT_EQ_I(AI32(p, 0x120), 0x32);
    T_ASSERT_EQ_I(AI32(p, 0x128), 0x32);
    T_ASSERT_EQ_I(AI32(p, 0x130), 0x32);
    T_ASSERT_EQ_I(AI32(p, 0x140), 0);
    T_ASSERT_EQ_I(AI32(p, 0x148), 0x32);
    T_ASSERT_EQ_I(AI32(p, 0x14c), 0x32);
    T_ASSERT_EQ_I(AI32(p, 0x150), 0x32);   /* human → NO density bonus */
    T_ASSERT_EQ_I(AI32(p, 0x154), 0);

    /* Troop muster row 1: row-2 preset = 7 i32 [0,0,0,0x32,0x32,0x32,0]. */
    uint8_t *pool = engine_game_init_troop_pool_row(1);
    T_ASSERT_EQ_I(AI32(pool, 0x00), 0);
    T_ASSERT_EQ_I(AI32(pool, 0x30), 0x32);
    T_ASSERT_EQ_I(AI32(pool, 0x40), 0x32);
    T_ASSERT_EQ_I(AI32(pool, 0x50), 0x32);
    T_ASSERT_EQ_I(AI32(pool, 0x60), 0);
    T_ASSERT_EQ_I(AI32(pool, 0x70), 0x32 * 3);   /* muster total */

    uint8_t *a = AREC(1);
    T_ASSERT_EQ_I(AI32(a, 0x24), 0xa7 + 0x32 * 3);  /* pop seed + troops */
    T_ASSERT_EQ_I(a[0x05], 1);              /* owning player */
    T_ASSERT_EQ_I(a[0x07], 0x55);           /* ← player +0x0a */
    T_ASSERT_EQ_I(a[0x1b0], 1);             /* setup-complete flag */
    T_ASSERT_EQ_I(a[0x297], 1);             /* model gate latched (+0x294+3) */
    /* owned army → no +100 finalise bonus on gold */
    T_ASSERT_EQ_I(AI32(a, 0x224), 0x0a);
    return 0;
}

/* An AI realm (OPP_TYPE set, HAS_DATA clear) takes the density gold bonus on
 * resource slot +0x150; an empty slot (both clear) is left untouched. */
int test_game_init_setup_realms_ai_density_and_empty(void)
{
    realms_reset();
    engine_game_init_set_realm_presets(0, 2, 0, 0, 0);   /* troop preset 0 → skip */
    s_slot_0056d5dc = 2;
    s_slot_0053f23c = 4;               /* density */

    PREC(1)[STRAT_OPP_TYPE] = 2;       /* AI (no human data) */
    g_player_roster[1 * 2]  = 1;       /* player 1 → army 1 */
    /* slot 2 left all-zero → empty, must be skipped */

    engine_game_init_setup_realms();

    uint8_t *p1 = PREC(1);
    T_ASSERT_EQ_I(p1[0x04], 1);                       /* AI slot activated */
    T_ASSERT_EQ_I(AI32(p1, 0x150), 0x32 + 4 * 0x14);  /* density bonus */

    /* troop preset 0 → muster pool untouched */
    uint8_t *pool = engine_game_init_troop_pool_row(1);
    for (int o = 0; o < 0x80; o++)
        T_ASSERT_EQ_I(pool[o], 0);

    /* empty slot 2 never activated */
    T_ASSERT_EQ_I(PREC(2)[0x04], 0);
    return 0;
}

/* The player→army mapping (roster id byte) drives both army fields and the
 * troop-skip path; a roster id different from the slot index proves it. */
int test_game_init_setup_realms_roster_mapping(void)
{
    realms_reset();
    engine_game_init_set_realm_presets(0, 0, 0, 0, 0);
    s_slot_0056d5dc = 2;

    PREC(1)[STRAT_HAS_DATA] = 1;       /* human in slot 1 */
    PREC(1)[0x0a]           = 0x33;
    g_player_roster[1 * 2]  = 2;       /* player 1 → army 2 (not 1) */

    engine_game_init_setup_realms();

    T_ASSERT_EQ_I(PREC(1)[0xe4], 2);   /* player records its army id */
    T_ASSERT_EQ_I(AREC(2)[0x05], 1);   /* army 2 → owning player 1 */
    T_ASSERT_EQ_I(AREC(2)[0x07], 0x33);
    T_ASSERT_EQ_I(AREC(2)[0x1b0], 1);
    /* army 1 unowned → finalise +100 bonus, army 2 owned → none */
    T_ASSERT_EQ_I(AI32(AREC(1), 0x224), 0x0a + 100);
    T_ASSERT_EQ_I(AI32(AREC(2), 0x224), 0x0a);
    return 0;
}
