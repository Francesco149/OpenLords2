/*
 * Unit tests for engine_map_query — the region-clear predicate
 * (port of FUN_0046d2a3).
 */

#include "t.h"
#include "engine_map_query.h"

#include <string.h>

enum { GRID = 0x40, STRIDE = 8 };
static uint8_t g_grid[GRID * GRID * STRIDE];

static void grid_reset(void)
{
    memset(g_grid, 0, sizeof g_grid);
    engine_map_query_bind_cell_descriptors(g_grid);
}

/* Mark cell (col,row) occupied via byte `which` (1 = raw-flag, 5 = head-link). */
static void occupy(int col, int row, int which, uint8_t v)
{
    g_grid[(row * GRID + col) * STRIDE + which] = v;
}

int test_map_query_all_clear_returns_1(void)
{
    grid_reset();
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 10, 3), 1);
    return 0;
}

int test_map_query_rawflag_occupied_returns_0(void)
{
    grid_reset();
    occupy(11, 11, 1, 0x40);   /* inside a 3x3 at (10,10) */
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 10, 3), 0);
    return 0;
}

int test_map_query_headlink_occupied_returns_0(void)
{
    grid_reset();
    occupy(12, 12, 5, 1);      /* corner cell of a 3x3 at (10,10) */
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 10, 3), 0);
    return 0;
}

int test_map_query_occupied_just_outside_region_is_clear(void)
{
    grid_reset();
    /* (13,13) is outside a 3x3 spanning cols/rows 10..12. */
    occupy(13, 13, 1, 1);
    occupy(9, 9, 5, 1);
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 10, 3), 1);
    return 0;
}

int test_map_query_negative_anchor_returns_0(void)
{
    grid_reset();
    T_ASSERT_EQ_I(engine_map_query_region_clear(-1, 10, 2), 0);
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, -1, 2), 0);
    return 0;
}

int test_map_query_overruns_grid_returns_0(void)
{
    grid_reset();
    /* col + size must be < 0x41.  62 + 3 = 65 → rejected. */
    T_ASSERT_EQ_I(engine_map_query_region_clear(62, 10, 3), 0);
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 62, 3), 0);
    /* 61 + 3 = 64 < 65 → accepted, and clear. */
    T_ASSERT_EQ_I(engine_map_query_region_clear(61, 10, 3), 1);
    return 0;
}

int test_map_query_row_stride_isolation(void)
{
    grid_reset();
    /* A cell one row below the region's last row, same column, must
     * not be seen (verifies the (0x40 - size) row-skip arithmetic). */
    occupy(10, 13, 1, 1);   /* row 13, region rows 10..12 */
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 10, 3), 1);
    /* But a cell inside the last row IS seen. */
    occupy(10, 12, 1, 1);
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 10, 3), 0);
    return 0;
}

int test_map_query_size_one_single_cell(void)
{
    grid_reset();
    T_ASSERT_EQ_I(engine_map_query_region_clear(5, 5, 1), 1);
    occupy(5, 5, 5, 1);
    T_ASSERT_EQ_I(engine_map_query_region_clear(5, 5, 1), 0);
    return 0;
}

int test_map_query_unbound_grid_returns_0(void)
{
    engine_map_query_bind_cell_descriptors(NULL);
    T_ASSERT_EQ_I(engine_map_query_region_clear(10, 10, 3), 0);
    return 0;
}
