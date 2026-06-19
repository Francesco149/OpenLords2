/*
 * Tests for engine_strat_cost_rebuild (port of FUN_0046ff43) — the
 * 64×64 strategic movement-cost grid builder.
 *
 * Each overworld cell descriptor is 8 bytes; the builder reads byte +0
 * (type), +1 (flags) and +7 (level), and maps them to a cost word via a
 * fixed branch ladder.  We drive a synthetic descriptor grid and assert
 * the cost the ladder must produce for every branch, plus the
 * load-bearing predicate ordering and whole-grid coverage.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_strat_cost.h"
#include "engine_path_field.h"    /* the AI pathfinder consumes the move-cost grid */

#define MAP_BYTES (ENGINE_STRAT_COST_CELLS * ENGINE_STRAT_COST_DESC_STRIDE)

static uint8_t g_map[MAP_BYTES];

/* Set descriptor for linear cell `idx` (type, flags, +7 level); the
 * other five bytes are zeroed. */
static void set_cell(int idx, uint8_t type, uint8_t flags, uint8_t lvl)
{
    uint8_t *d = &g_map[idx * ENGINE_STRAT_COST_DESC_STRIDE];
    memset(d, 0, ENGINE_STRAT_COST_DESC_STRIDE);
    d[0] = type;
    d[1] = flags;
    d[7] = lvl;
}

/* Exercise one branch: write cell 100 with (type, flags, lvl), rebuild,
 * return the resulting cost. */
static int cost_of(uint8_t type, uint8_t flags, uint8_t lvl)
{
    memset(g_map, 0, sizeof g_map);
    set_cell(100, type, flags, lvl);
    engine_strat_cost_rebuild(g_map);
    return g_strat_move_cost[100];
}

int test_strat_cost_water_blocked(void)
{
    /* flags & 0xc set → 0, even with the road bit also set. */
    T_ASSERT(cost_of(0, 0x04, 0) == 0);
    T_ASSERT(cost_of(0, 0x08, 0) == 0);
    T_ASSERT(cost_of(0, 0x0c, 0) == 0);
    T_ASSERT(cost_of(0, 0x04 | 0x01, 0) == 0);   /* 0xc wins over road */
    return 0;
}

int test_strat_cost_level_gate(void)
{
    /* +7 >= 0x11 → 0 (checked before the road bit). */
    T_ASSERT(cost_of(0, 0x00, 0x11) == 0);
    T_ASSERT(cost_of(0, 0x00, 0xff) == 0);
    T_ASSERT(cost_of(0, 0x01, 0x11) == 0);       /* gate wins over road */
    T_ASSERT(cost_of(0, 0x00, 0x10) == 3);       /* just under the gate */
    return 0;
}

int test_strat_cost_road(void)
{
    /* flags bit0 (road) → 1, and it beats bits 5/6/7. */
    T_ASSERT(cost_of(0, 0x01, 0) == 1);
    T_ASSERT(cost_of(0, 0x01 | 0x20, 0) == 1);
    T_ASSERT(cost_of(0, 0x01 | 0x40, 0) == 1);
    T_ASSERT(cost_of(0, 0x01 | 0x80, 0) == 1);
    return 0;
}

int test_strat_cost_bit5_terrain(void)
{
    /* flags bit5: type<2 → 3, 2..0x16 → 6, >=0x17 → 3. */
    T_ASSERT(cost_of(0,    0x20, 0) == 3);
    T_ASSERT(cost_of(1,    0x20, 0) == 3);
    T_ASSERT(cost_of(2,    0x20, 0) == 6);
    T_ASSERT(cost_of(0x16, 0x20, 0) == 6);
    T_ASSERT(cost_of(0x17, 0x20, 0) == 3);
    T_ASSERT(cost_of(0x40, 0x20, 0) == 3);
    return 0;
}

int test_strat_cost_bit6(void)
{
    /* flags bit6 (and not bit5/road) → 100. */
    T_ASSERT(cost_of(0,    0x40, 0) == 100);
    T_ASSERT(cost_of(0x14, 0x40, 0) == 100);
    return 0;
}

int test_strat_cost_bit7_by_type(void)
{
    /* flags bit7: 0x14 → 3; {3,6,9,0xc} → 0; everything else → 100. */
    T_ASSERT(cost_of(0x14, 0x80, 0) == 3);
    T_ASSERT(cost_of(3,    0x80, 0) == 0);
    T_ASSERT(cost_of(6,    0x80, 0) == 0);
    T_ASSERT(cost_of(9,    0x80, 0) == 0);
    T_ASSERT(cost_of(0xc,  0x80, 0) == 0);
    T_ASSERT(cost_of(0,    0x80, 0) == 100);
    T_ASSERT(cost_of(0x20, 0x80, 0) == 100);
    return 0;
}

int test_strat_cost_bit4(void)
{
    /* flags bit4 only: type 0x10 → 100, else → 0. */
    T_ASSERT(cost_of(0x10, 0x10, 0) == 100);
    T_ASSERT(cost_of(0,    0x10, 0) == 0);
    T_ASSERT(cost_of(0x11, 0x10, 0) == 0);
    return 0;
}

int test_strat_cost_default(void)
{
    /* No flag bits, level under the gate → 3. */
    T_ASSERT(cost_of(0,    0x00, 0) == 3);
    T_ASSERT(cost_of(0xff, 0x00, 0) == 3);
    return 0;
}

/* Whole-grid coverage: every one of the 4096 cells is written (none
 * skipped, none overrun).  Pre-dirty the cost grid with a sentinel,
 * fill the map so each cell hits the default (cost 3), and assert all
 * 4096 became 3. */
int test_strat_cost_writes_every_cell(void)
{
    for (int i = 0; i < ENGINE_STRAT_COST_CELLS; i++)
        g_strat_move_cost[i] = 0x7777;

    memset(g_map, 0, sizeof g_map);   /* all cells → default branch (3) */
    engine_strat_cost_rebuild(g_map);

    for (int i = 0; i < ENGINE_STRAT_COST_CELLS; i++)
        if (g_strat_move_cost[i] != 3)
            T_FAIL("cell %d = %d, want 3 (uncovered)", i,
                   g_strat_move_cost[i]);
    return 0;
}

/* The (col,row) → index mapping is row*64 + col: a value placed at a
 * specific (col,row) descriptor lands at the matching cost index. */
int test_strat_cost_cell_indexing(void)
{
    memset(g_map, 0, sizeof g_map);
    /* (col=5, row=3) → idx 197; make it a road so it stands out. */
    int idx = 3 * ENGINE_STRAT_COST_GRID_W + 5;
    set_cell(idx, 0, 0x01, 0);
    engine_strat_cost_rebuild(g_map);

    T_ASSERT(g_strat_move_cost[idx] == 1);
    T_ASSERT(g_strat_move_cost[idx - 1] == 3);   /* neighbour stays default */
    T_ASSERT(g_strat_move_cost[idx + 1] == 3);
    return 0;
}

/* Regression: the AI cost-field pathfinder must bind to
 * engine_strat_cost_pathfield_base() (the 0x4f3ffe NW-sub-table base, 0x41 int16
 * before the cost cells), NOT to g_strat_move_cost directly.  With a uniform
 * passable grid and one costly east neighbour, a flood from (20,20) reaches that
 * neighbour at cost 1 + 100 only if the base + its 0x41 skirt are correct; a bind
 * to bare g_strat_move_cost would mis-read the cost (off by 0x41) and/or run off
 * the unpadded array end.  Guards the brain step-6/7 on-engine crash that an
 * unbound grid caused (the first AI army to move dereferenced a NULL/short grid). */
int test_strat_cost_pathfield_base_feeds_flood(void)
{
    for (int i = 0; i < ENGINE_STRAT_COST_CELLS; i++)
        g_strat_move_cost[i] = 3;                          /* uniform open terrain */
    int src  = 20 * ENGINE_PATH_FIELD_DIM + 20;
    int east = 20 * ENGINE_PATH_FIELD_DIM + 21;
    g_strat_move_cost[east] = 100;                          /* costly east neighbour */

    engine_path_field_bind_terrain(engine_strat_cost_pathfield_base());
    engine_path_field_fill(0, 20, 20, 0);

    T_ASSERT_EQ_I(engine_path_field_cost(0, src), 1);       /* source seeded */
    T_ASSERT_EQ_I(engine_path_field_cost(0, east), 101);    /* 1 + cost(east)=100 */
    return 0;
}
