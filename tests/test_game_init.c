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

#include <string.h>

#include "t.h"
#include "game_init.h"
#include "save.h"

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
