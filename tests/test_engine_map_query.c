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

/* Set every cell's raw-flag byte (+1) to v (head-link bytes stay 0).
 * Used to blanket the grid with a "neither predicate" flag (4) so a
 * single carved cell drives the ring-distance probe. */
static void grid_fill_rawflag(uint8_t v)
{
    for (int i = 0; i < GRID * GRID; i++)
        g_grid[i * STRIDE + 1] = v;
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

/* ── box-scan predicates (FUN_0046cfbd / FUN_0046d130) ─────────────── */

int test_map_query_box_bit0_finds_set_cell(void)
{
    grid_reset();
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2), 0);
    occupy(11, 11, 1, 0x01);   /* bit0 set, head-link 0 → match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2), 1);
    return 0;
}

int test_map_query_box_bit0_requires_headlink_zero(void)
{
    grid_reset();
    occupy(11, 11, 1, 0x01);   /* bit0 set ... */
    occupy(11, 11, 5, 0x07);   /* ... but head-link nonzero → no match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2), 0);
    return 0;
}

int test_map_query_box_bit0_mask(void)
{
    grid_reset();
    occupy(11, 11, 1, 0x02);   /* bit1 only → bit0 clear → no match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2), 0);
    occupy(11, 11, 1, 0x03);   /* bit0 now set → match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2), 1);
    return 0;
}

int test_map_query_box_only_bit1_empty_matches(void)
{
    grid_reset();
    /* All-zero cells satisfy (0 & 0xfd) == 0 → the first cell matches. */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2), 1);
    return 0;
}

int test_map_query_box_only_bit1_predicate(void)
{
    grid_reset();
    /* Fill the 5×5 box (centre 10,10 r2 → cols/rows 8..12) with bit2 set
     * so every cell is disqualified ((flag & 0xfd) != 0). */
    for (int r = 8; r <= 12; r++)
        for (int c = 8; c <= 12; c++)
            occupy(c, r, 1, 0x04);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2), 0);
    occupy(10, 10, 1, 0x02);   /* flag == 2 (only bit1), head-link 0 → match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2), 1);
    return 0;
}

int test_map_query_box_only_bit1_headlink(void)
{
    grid_reset();
    for (int r = 8; r <= 12; r++)
        for (int c = 8; c <= 12; c++)
            occupy(c, r, 1, 0x04);   /* all flag-disqualified */
    occupy(10, 10, 1, 0x02);         /* qualifies by flag ... */
    occupy(10, 10, 5, 0x01);         /* ... but head-link nonzero → no */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2), 0);
    return 0;
}

int test_map_query_box_clamps_at_right_edge(void)
{
    grid_reset();
    /* region_clear rejects col+side>0x40; the box scan CLAMPS instead.
     * Centre (62,10) r3 → box cols 59..63 (clamped from 59..65). */
    occupy(63, 10, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(62, 10, 3), 1);
    return 0;
}

int test_map_query_box_clamps_negative_anchor(void)
{
    grid_reset();
    /* Centre (1,1) r3 → box [-2..4]² clamps to [0..4]². */
    occupy(4, 4, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(1, 1, 3), 1);

    /* A bit0 cell just outside the clamped box (col 5) is not seen. */
    grid_reset();
    occupy(5, 0, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(1, 1, 3), 0);
    return 0;
}

int test_map_query_box_radius0_single_cell(void)
{
    grid_reset();
    occupy(20, 20, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(20, 20, 0), 1);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(21, 20, 0), 0);
    return 0;
}

int test_map_query_box_unbound_returns_0(void)
{
    engine_map_query_bind_cell_descriptors(NULL);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2), 0);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2), 0);
    return 0;
}

/* ── Ring-distance probe (FUN_0046733c) ─────────────────────────────── */

/* An empty grid is full of flag-0 cells, so the only_bit1 box matches at
 * radius 1 → ring distance 1. */
int test_map_query_ring_empty_grid_returns_1(void)
{
    grid_reset();
    T_ASSERT_EQ_I(engine_map_query_nearest_feature_ring(20, 20), 1);
    return 0;
}

/* Grid blanketed with flag 4 (neither predicate matches anywhere) → 0. */
int test_map_query_ring_no_feature_returns_0(void)
{
    grid_reset();
    grid_fill_rawflag(4);
    T_ASSERT_EQ_I(engine_map_query_nearest_feature_ring(20, 20), 0);
    return 0;
}

/* bit0 box uses radius d+1: a bit0 cell at Chebyshev distance 2 is found
 * at d=1 (box radius 2), so the ring distance is 1 — NOT 2.  This pins
 * the +1 asymmetry; a port using radius d for bit0 would return 2. */
int test_map_query_ring_bit0_uses_radius_plus_1(void)
{
    grid_reset();
    grid_fill_rawflag(4);
    occupy(22, 20, 1, 0x01);     /* distance 2 */
    T_ASSERT_EQ_I(engine_map_query_nearest_feature_ring(20, 20), 1);
    return 0;
}

/* only_bit1 box uses radius d (no +1): a lone flag-2 cell at distance 2
 * is first seen at d=2.  Pins that only_bit1 does NOT get the +1. */
int test_map_query_ring_only_bit1_uses_radius_d(void)
{
    grid_reset();
    grid_fill_rawflag(4);
    occupy(22, 20, 1, 0x02);     /* flag 2, distance 2 */
    T_ASSERT_EQ_I(engine_map_query_nearest_feature_ring(20, 20), 2);
    return 0;
}

/* A bit0 cell at distance 4 is reached at d=3 (box radius 4). */
int test_map_query_ring_bit0_distance_4_returns_3(void)
{
    grid_reset();
    grid_fill_rawflag(4);
    occupy(24, 20, 1, 0x01);     /* distance 4 */
    T_ASSERT_EQ_I(engine_map_query_nearest_feature_ring(20, 20), 3);
    return 0;
}

/* A bit0 cell at distance 7 is beyond every tested radius (max box
 * radius is 6 at d=5) → ring distance 0. */
int test_map_query_ring_out_of_range_returns_0(void)
{
    grid_reset();
    grid_fill_rawflag(4);
    occupy(27, 20, 1, 0x01);     /* distance 7 */
    T_ASSERT_EQ_I(engine_map_query_nearest_feature_ring(20, 20), 0);
    return 0;
}
