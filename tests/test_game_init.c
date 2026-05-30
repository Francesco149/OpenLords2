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
