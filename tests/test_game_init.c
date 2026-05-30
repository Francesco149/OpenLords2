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
