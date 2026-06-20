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
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2, NULL, NULL), 0);
    occupy(11, 11, 1, 0x01);   /* bit0 set, head-link 0 → match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2, NULL, NULL), 1);
    return 0;
}

int test_map_query_box_bit0_requires_headlink_zero(void)
{
    grid_reset();
    occupy(11, 11, 1, 0x01);   /* bit0 set ... */
    occupy(11, 11, 5, 0x07);   /* ... but head-link nonzero → no match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2, NULL, NULL), 0);
    return 0;
}

int test_map_query_box_bit0_mask(void)
{
    grid_reset();
    occupy(11, 11, 1, 0x02);   /* bit1 only → bit0 clear → no match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2, NULL, NULL), 0);
    occupy(11, 11, 1, 0x03);   /* bit0 now set → match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2, NULL, NULL), 1);
    return 0;
}

int test_map_query_box_only_bit1_empty_matches(void)
{
    grid_reset();
    /* All-zero cells satisfy (0 & 0xfd) == 0 → the first cell matches. */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2, NULL, NULL), 1);
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
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2, NULL, NULL), 0);
    occupy(10, 10, 1, 0x02);   /* flag == 2 (only bit1), head-link 0 → match */
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2, NULL, NULL), 1);
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
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2, NULL, NULL), 0);
    return 0;
}

int test_map_query_box_clamps_at_right_edge(void)
{
    grid_reset();
    /* region_clear rejects col+side>0x40; the box scan CLAMPS instead.
     * Centre (62,10) r3 → box cols 59..63 (clamped from 59..65). */
    occupy(63, 10, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(62, 10, 3, NULL, NULL), 1);
    return 0;
}

int test_map_query_box_clamps_negative_anchor(void)
{
    grid_reset();
    /* Centre (1,1) r3 → box [-2..4]² clamps to [0..4]². */
    occupy(4, 4, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(1, 1, 3, NULL, NULL), 1);

    /* A bit0 cell just outside the clamped box (col 5) is not seen. */
    grid_reset();
    occupy(5, 0, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(1, 1, 3, NULL, NULL), 0);
    return 0;
}

int test_map_query_box_radius0_single_cell(void)
{
    grid_reset();
    occupy(20, 20, 1, 0x01);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(20, 20, 0, NULL, NULL), 1);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(21, 20, 0, NULL, NULL), 0);
    return 0;
}

int test_map_query_box_unbound_returns_0(void)
{
    engine_map_query_bind_cell_descriptors(NULL);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_bit0(10, 10, 2, NULL, NULL), 0);
    T_ASSERT_EQ_I(engine_map_query_box_any_rawflag_only_bit1(10, 10, 2, NULL, NULL), 0);
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

/* The _at variant surfaces the matched cell's (col,row) — the position
 * retail leaves in its DAT_00569570/78 scratch for FUN_00437535. */
int test_map_query_ring_at_reports_matched_cell(void)
{
    grid_reset();
    grid_fill_rawflag(4);
    occupy(22, 20, 1, 0x01);     /* the lone bit0 cell, distance 2 */
    int col = -1, row = -1;
    int d = engine_map_query_nearest_feature_ring_at(20, 20, &col, &row);
    T_ASSERT_EQ_I(d, 1);
    T_ASSERT_EQ_I(col, 22);
    T_ASSERT_EQ_I(row, 20);
    return 0;
}

/* On a miss the outputs are left untouched (callers only trust them on a
 * non-zero return, mirroring retail's scratch reuse). */
int test_map_query_ring_at_miss_leaves_outputs(void)
{
    grid_reset();
    grid_fill_rawflag(4);
    occupy(27, 20, 1, 0x01);     /* distance 7 — out of range */
    int col = 7, row = 9;
    int d = engine_map_query_nearest_feature_ring_at(20, 20, &col, &row);
    T_ASSERT_EQ_I(d, 0);
    T_ASSERT_EQ_I(col, 7);
    T_ASSERT_EQ_I(row, 9);
    return 0;
}

/* ── terrain-info compute (FUN_0046d5fe) ─────────────────────────────────
 * Cell bytes: +0 terrain_type, +1 flags, +2 (derived-bit mask), +3 cell3,
 * +4 sub-tile nibble, +5 county_id, +7 head_link.  tile_off is a byte
 * offset (cell index * 8).  The army record array is stride 0x300, byte +5
 * read per record. */
static uint8_t g_ti_cells[0x800];
static uint8_t g_ti_army[0x40 * 0x300];   /* sized to cover a 0x20 head-link */

static void ti_reset(void)
{
    memset(g_ti_cells, 0, sizeof g_ti_cells);
    memset(g_ti_army, 0, sizeof g_ti_army);
}

static void ti_cell(int off, uint8_t b0, uint8_t b1, uint8_t b2,
                    uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b7)
{
    g_ti_cells[off + 0] = b0; g_ti_cells[off + 1] = b1; g_ti_cells[off + 2] = b2;
    g_ti_cells[off + 3] = b3; g_ti_cells[off + 4] = b4; g_ti_cells[off + 5] = b5;
    g_ti_cells[off + 7] = b7;
}

static engine_map_terrain_out_t ti_run(int off)
{
    engine_map_terrain_in_t in = {
        .cell_grid = g_ti_cells, .army_recs = g_ti_army, .tile_off = off,
    };
    engine_map_terrain_out_t out;
    memset(&out, 0, sizeof out);
    engine_map_query_terrain_info(&in, &out);
    return out;
}

/* width-1 single cell, simple terrain — origin == cursor cell. */
int test_map_query_terrain_width1_simple(void)
{
    ti_reset();
    ti_cell(0x40, /*t*/5, /*flags*/0, /*b2*/0, /*cell3*/9, /*nib*/0, /*county*/3, /*head*/2);
    g_ti_army[2 * 0x300 + 5] = 77;
    engine_map_terrain_out_t o = ti_run(0x40);
    T_ASSERT_EQ_I(o.head_link, 2);
    T_ASSERT_EQ_I(o.army_byte5, 77);
    T_ASSERT_EQ_I(o.origin_off, 0x40);
    T_ASSERT_EQ_I(o.terrain_type, 5);
    T_ASSERT_EQ_I(o.county_id, 3);
    T_ASSERT_EQ_I(o.cell3, 9);
    T_ASSERT_EQ_I(o.flags, 0);
    T_ASSERT_EQ_I(o.derived_bit, 0);
    return 0;
}

/* width-2 via flag bit6: nibble 3 walks back one col (-8) + one row (-0x200);
 * attributes read from the ORIGIN cell, not the cursor cell. */
int test_map_query_terrain_width2_bit6_walkback(void)
{
    ti_reset();
    /* cursor cell @ 0x600: width-2 (bit6), nib=3, head=4 */
    ti_cell(0x600, /*t*/0x16, /*flags*/0x40, 0, 0, /*nib*/3, /*county*/0xff, /*head*/4);
    /* origin @ 0x600 - 8 - 0x200 = 0x3F8 with distinct attrs */
    ti_cell(0x3F8, /*t*/0x10, /*flags*/0x40, 0, /*cell3*/0x11, 0, /*county*/0x12, /*head*/0);
    g_ti_army[4 * 0x300 + 5] = 88;
    engine_map_terrain_out_t o = ti_run(0x600);
    T_ASSERT_EQ_I(o.head_link, 4);
    T_ASSERT_EQ_I(o.army_byte5, 88);
    T_ASSERT_EQ_I(o.origin_off, 0x3F8);
    T_ASSERT_EQ_I(o.terrain_type, 0x10);   /* from origin */
    T_ASSERT_EQ_I(o.county_id, 0x12);      /* from origin */
    T_ASSERT_EQ_I(o.cell3, 0x11);
    T_ASSERT_EQ_I(o.flags, 0x40);
    return 0;
}

/* width-2 via flag bit7 + terrain >= 0x15. */
int test_map_query_terrain_width2_bit7_highterrain(void)
{
    ti_reset();
    ti_cell(0x80, /*t*/0x20, /*flags*/0x80, 0, /*cell3*/7, /*nib*/0, /*county*/4, /*head*/1);
    g_ti_army[1 * 0x300 + 5] = 5;
    engine_map_terrain_out_t o = ti_run(0x80);
    T_ASSERT_EQ_I(o.origin_off, 0x80);     /* nib 0 → no walk */
    T_ASSERT_EQ_I(o.terrain_type, 0x20);
    T_ASSERT_EQ_I(o.flags, 0x80);          /* rule1 needs terrain 0x14 → unchanged */
    T_ASSERT_EQ_I(o.county_id, 4);
    T_ASSERT_EQ_I(o.head_link, 1);
    T_ASSERT_EQ_I(o.army_byte5, 5);
    return 0;
}

/* width-1 via flag bit7 + terrain < 0x15. */
int test_map_query_terrain_width1_bit7_lowterrain(void)
{
    ti_reset();
    ti_cell(0xC0, /*t*/0x10, /*flags*/0x80, 0, /*cell3*/2, 0, /*county*/6, /*head*/3);
    engine_map_terrain_out_t o = ti_run(0xC0);
    T_ASSERT_EQ_I(o.origin_off, 0xC0);
    T_ASSERT_EQ_I(o.terrain_type, 0x10);
    T_ASSERT_EQ_I(o.flags, 0x80);
    return 0;
}

/* head_link 0x20 → 0 reset, but the army-record read uses the ORIGINAL 0x20. */
int test_map_query_terrain_head_link_reset(void)
{
    ti_reset();
    ti_cell(0x100, /*t*/4, /*flags*/0, 0, /*cell3*/1, 0, /*county*/2, /*head*/0x20);
    g_ti_army[0x20 * 0x300 + 5] = 0x55;
    engine_map_terrain_out_t o = ti_run(0x100);
    T_ASSERT_EQ_I(o.head_link, 0);         /* reset */
    T_ASSERT_EQ_I(o.army_byte5, 0x55);     /* read at original 0x20 */
    return 0;
}

/* flag-clear rule 1: origin flags bit7 set AND terrain == 0x14 → flags 0. */
int test_map_query_terrain_flag_rule1(void)
{
    ti_reset();
    ti_cell(0x140, /*t*/0x14, /*flags*/0x80, 0, /*cell3*/3, 0, /*county*/4, /*head*/1);
    engine_map_terrain_out_t o = ti_run(0x140);
    T_ASSERT_EQ_I(o.terrain_type, 0x14);
    T_ASSERT_EQ_I(o.flags, 0);             /* cleared */
    return 0;
}

/* flag-clear rule 2: flags bit4 (0x10) AND terrain == 0 → flags 0. */
int test_map_query_terrain_flag_rule2(void)
{
    ti_reset();
    ti_cell(0x180, /*t*/0, /*flags*/0x10, 0, /*cell3*/0, 0, /*county*/1, /*head*/2);
    engine_map_terrain_out_t o = ti_run(0x180);
    T_ASSERT_EQ_I(o.terrain_type, 0);
    T_ASSERT_EQ_I(o.flags, 0);             /* cleared */
    return 0;
}

/* derived_bit: flags bit3 (8) AND (cell+2 & 0x1c) == 4 → 1. */
int test_map_query_terrain_derived_bit(void)
{
    ti_reset();
    ti_cell(0x1C0, /*t*/5, /*flags*/8, /*b2*/4, /*cell3*/0, 0, /*county*/3, /*head*/0);
    engine_map_terrain_out_t o = ti_run(0x1C0);
    T_ASSERT_EQ_I(o.flags, 8);
    T_ASSERT_EQ_I(o.derived_bit, 1);
    /* same flags but b2 not matching the mask → no derived bit */
    ti_cell(0x1C0, 5, 8, /*b2*/2, 0, 0, 3, 0);
    o = ti_run(0x1C0);
    T_ASSERT_EQ_I(o.derived_bit, 0);
    return 0;
}
