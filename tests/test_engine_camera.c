/*
 * test_engine_camera.c — coverage for engine_camera_clamp_scroll and
 * engine_camera_clamp_anchor, the pure-C ports of retail FUN_00429b1d
 * and FUN_0047ed34 (strategic-map scroll + camera-anchor clamps).
 *
 * Behaviour matrix:
 *
 *   scroll clamp (variable bounds 0x40 / 0x80 minus visible count):
 *     col < 0                            → col := 0
 *     row < 0                            → row := 0
 *     col >= 0x41 - col_count            → col := 0x40 - col_count
 *     row >= 0x81 - row_count            → row := 0x80 - row_count
 *     in-range                           → unchanged
 *     lower clamp THEN upper clamp       → 0 can be pushed negative
 *                                          if col_count > 0x40
 *
 *   anchor clamp (hardcoded asymmetric bounds 0x41 / 0x42):
 *     x < 0                              → x := 0
 *     y < 0                              → y := 0
 *     x > 0x41                           → x := 0x41
 *     y > 0x42                           → y := 0x42
 *     in-range (including bounds)        → unchanged
 */

#include "t.h"
#include "engine_camera.h"

/* ─── scroll clamp ─────────────────────────────────────────────────── */

int test_engine_camera_scroll_in_range_unchanged(void)
{
    int col = 10, row = 50;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(col, 10);
    T_ASSERT_EQ_I(row, 50);
    return 0;
}

int test_engine_camera_scroll_zero_in_range(void)
{
    int col = 0, row = 0;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(col, 0);
    T_ASSERT_EQ_I(row, 0);
    return 0;
}

int test_engine_camera_scroll_negative_col_clamped_to_zero(void)
{
    int col = -1, row = 0;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(col, 0);
    T_ASSERT_EQ_I(row, 0);
    return 0;
}

int test_engine_camera_scroll_negative_row_clamped_to_zero(void)
{
    int col = 0, row = -100;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(col, 0);
    T_ASSERT_EQ_I(row, 0);
    return 0;
}

int test_engine_camera_scroll_at_max_col_unchanged(void)
{
    /* col = 0x40 - col_count is the maximum valid value (test is
     * `0x41 - col_count <= col`, which fails at exactly that value). */
    int col = 0x40 - 16, row = 0;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(col, 0x40 - 16);
    return 0;
}

int test_engine_camera_scroll_one_over_max_col_clamped(void)
{
    /* col = 0x41 - col_count is one over the max — should clamp to
     * 0x40 - col_count. */
    int col = 0x41 - 16, row = 0;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(col, 0x40 - 16);
    return 0;
}

int test_engine_camera_scroll_huge_col_clamped(void)
{
    int col = 1000, row = 0;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(col, 0x40 - 16);
    return 0;
}

int test_engine_camera_scroll_at_max_row_unchanged(void)
{
    int col = 0, row = 0x80 - 16;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(row, 0x80 - 16);
    return 0;
}

int test_engine_camera_scroll_one_over_max_row_clamped(void)
{
    int col = 0, row = 0x81 - 16;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(row, 0x80 - 16);
    return 0;
}

int test_engine_camera_scroll_huge_row_clamped(void)
{
    int col = 0, row = 1000;
    engine_camera_clamp_scroll(&col, &row, 16, 16);
    T_ASSERT_EQ_I(row, 0x80 - 16);
    return 0;
}

int test_engine_camera_scroll_both_axes_independent(void)
{
    /* col out of range high, row out of range low — both clamped
     * independently. */
    int col = 1000, row = -1000;
    engine_camera_clamp_scroll(&col, &row, 8, 8);
    T_ASSERT_EQ_I(col, 0x40 - 8);
    T_ASSERT_EQ_I(row, 0);
    return 0;
}

int test_engine_camera_scroll_full_map_visible_col_pinned_to_zero(void)
{
    /* col_count == 0x40 (full map width visible): max valid col = 0,
     * and the test `0x41 - 0x40 <= col` ≡ `1 <= col` triggers for
     * any col >= 1.  At col = 0, in-range. */
    int col = 0, row = 0;
    engine_camera_clamp_scroll(&col, &row, 0x40, 0x80);
    T_ASSERT_EQ_I(col, 0);
    T_ASSERT_EQ_I(row, 0);
    return 0;
}

int test_engine_camera_scroll_oversize_visible_count_pushes_negative(void)
{
    /* Quirk: lower clamp fires FIRST.  If col_count > 0x40 (visible
     * width exceeds map width), the upper clamp resets col to
     * `0x40 - col_count` (negative), and that negative value is what
     * remains — the lower clamp has already fired this call.  Same
     * for row.
     *
     * Never observed in practice (real col_count maxes around 0x10),
     * but the boundary is in the binary and the test pins it. */
    int col = 0, row = 0;
    engine_camera_clamp_scroll(&col, &row, 0x50, 0x90);
    T_ASSERT_EQ_I(col, 0x40 - 0x50);    /* -16 */
    T_ASSERT_EQ_I(row, 0x80 - 0x90);    /* -16 */
    return 0;
}

int test_engine_camera_scroll_zero_visible_count(void)
{
    /* Edge: col_count = 0, row_count = 0.  Test becomes
     * `0x41 <= col` / `0x81 <= row`, so max valid is 0x40 / 0x80. */
    int col = 0x40, row = 0x80;
    engine_camera_clamp_scroll(&col, &row, 0, 0);
    T_ASSERT_EQ_I(col, 0x40);
    T_ASSERT_EQ_I(row, 0x80);
    return 0;
}

/* ─── anchor clamp ─────────────────────────────────────────────────── */

int test_engine_camera_anchor_zero_unchanged(void)
{
    int x = 0, y = 0;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0);
    T_ASSERT_EQ_I(y, 0);
    return 0;
}

int test_engine_camera_anchor_in_range_unchanged(void)
{
    int x = 0x20, y = 0x21;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0x20);
    T_ASSERT_EQ_I(y, 0x21);
    return 0;
}

int test_engine_camera_anchor_at_max_x_unchanged(void)
{
    /* Upper test is `if (0x41 < x)` — strict less-than, so 0x41 is
     * INCLUSIVE.  Same shape for y at 0x42. */
    int x = 0x41, y = 0;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0x41);
    return 0;
}

int test_engine_camera_anchor_at_max_y_unchanged(void)
{
    int x = 0, y = 0x42;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(y, 0x42);
    return 0;
}

int test_engine_camera_anchor_one_over_max_x_clamped(void)
{
    int x = 0x42, y = 0;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0x41);
    return 0;
}

int test_engine_camera_anchor_one_over_max_y_clamped(void)
{
    int x = 0, y = 0x43;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(y, 0x42);
    return 0;
}

int test_engine_camera_anchor_negative_x_clamped_to_zero(void)
{
    int x = -5, y = 0;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0);
    return 0;
}

int test_engine_camera_anchor_negative_y_clamped_to_zero(void)
{
    int x = 0, y = -100;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(y, 0);
    return 0;
}

int test_engine_camera_anchor_huge_x_clamped(void)
{
    int x = 1000, y = 0;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0x41);
    return 0;
}

int test_engine_camera_anchor_huge_y_clamped(void)
{
    int x = 0, y = 1000;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(y, 0x42);
    return 0;
}

int test_engine_camera_anchor_both_axes_independent(void)
{
    int x = -10, y = 0xff;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0);
    T_ASSERT_EQ_I(y, 0x42);
    return 0;
}

int test_engine_camera_anchor_asymmetric_bounds_pinned(void)
{
    /* Quirk #103: x bound is 0x41, y bound is 0x42 — asymmetric by
     * one.  Pin both at 0x42 to confirm only x clamps. */
    int x = 0x42, y = 0x42;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0x41);    /* x clamped */
    T_ASSERT_EQ_I(y, 0x42);    /* y unchanged — exact upper bound */
    return 0;
}

int test_engine_camera_anchor_idempotent(void)
{
    /* Once clamped, a second call produces no further changes. */
    int x = 1000, y = 1000;
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0x41);
    T_ASSERT_EQ_I(y, 0x42);
    engine_camera_clamp_anchor(&x, &y);
    T_ASSERT_EQ_I(x, 0x41);
    T_ASSERT_EQ_I(y, 0x42);
    return 0;
}

int test_engine_camera_scroll_idempotent(void)
{
    int col = 1000, row = 1000;
    engine_camera_clamp_scroll(&col, &row, 16, 32);
    T_ASSERT_EQ_I(col, 0x40 - 16);
    T_ASSERT_EQ_I(row, 0x80 - 32);
    engine_camera_clamp_scroll(&col, &row, 16, 32);
    T_ASSERT_EQ_I(col, 0x40 - 16);
    T_ASSERT_EQ_I(row, 0x80 - 32);
    return 0;
}

/* ── scroll-pixel conversion (FUN_0042a15f) ──────────────────────── */

/* Even row: sx = col_base*x_step - half_step + x_origin; sy = y_step*row + y_origin. */
int test_engine_camera_scroll_pixels_even_row(void)
{
    int sx = -1, sy = -1;
    /* col_base=2, x_step=10, half_step=3, x_origin=5 → 2*10-3+5 = 22.
     * row_subpos=4 (even), y_step=7, y_origin=1 → 7*4+1 = 29. */
    engine_camera_compute_scroll_pixels(1, /*scroll_col*/0, /*scroll_row*/0,
                                        /*col_base*/2, /*row_subpos*/4,
                                        /*x_step*/10, /*half_step*/3,
                                        /*x_origin*/5, /*y_step*/7,
                                        /*y_origin*/1, &sx, &sy);
    T_ASSERT_EQ_I(sx, 22);
    T_ASSERT_EQ_I(sy, 29);
    return 0;
}

/* Odd row subtracts an extra half_step from X (staggered rows). */
int test_engine_camera_scroll_pixels_odd_row_half_step(void)
{
    int sx = -1, sy = -1;
    engine_camera_compute_scroll_pixels(1, 0, 0, 2, /*row_subpos*/5,
                                        10, 3, 5, 7, 1, &sx, &sy);
    T_ASSERT_EQ_I(sx, 22 - 3);    /* 19 */
    T_ASSERT_EQ_I(sy, 7 * 5 + 1); /* 36 */
    return 0;
}

/* map_ready == 0 leaves both outputs untouched. */
int test_engine_camera_scroll_pixels_not_ready_no_write(void)
{
    int sx = 111, sy = 222;
    engine_camera_compute_scroll_pixels(0, 0, 0, 2, 4, 10, 3, 5, 7, 1, &sx, &sy);
    T_ASSERT_EQ_I(sx, 111);
    T_ASSERT_EQ_I(sy, 222);
    return 0;
}

/* Absolute column off the map (col_base+scroll_col >= 0x41) → no write. */
int test_engine_camera_scroll_pixels_col_off_map_no_write(void)
{
    int sx = 111, sy = 222;
    engine_camera_compute_scroll_pixels(1, /*scroll_col*/0x40, 0,
                                        /*col_base*/1, 4, 10, 3, 5, 7, 1, &sx, &sy);
    T_ASSERT_EQ_I(sx, 111);
    T_ASSERT_EQ_I(sy, 222);
    return 0;
}

/* Absolute row off the map (scroll_row+row_subpos >= 0x81) → no write. */
int test_engine_camera_scroll_pixels_row_off_map_no_write(void)
{
    int sx = 111, sy = 222;
    engine_camera_compute_scroll_pixels(1, 0, /*scroll_row*/0x80,
                                        2, /*row_subpos*/1, 10, 3, 5, 7, 1, &sx, &sy);
    T_ASSERT_EQ_I(sx, 111);
    T_ASSERT_EQ_I(sy, 222);
    return 0;
}

/* Negative absolute cell (col_base+scroll_col < 0) → no write. */
int test_engine_camera_scroll_pixels_negative_cell_no_write(void)
{
    int sx = 111, sy = 222;
    engine_camera_compute_scroll_pixels(1, /*scroll_col*/-5, 0,
                                        /*col_base*/2, 4, 10, 3, 5, 7, 1, &sx, &sy);
    T_ASSERT_EQ_I(sx, 111);
    T_ASSERT_EQ_I(sy, 222);
    return 0;
}

/* ── cursor → cell resolver (FUN_00429ba4) ───────────────────────────
 *
 * Standard test geometry (chosen so the staggered-row pick math is
 * hand-checkable):
 *   x_origin 0, x_step 0x20 (32), half_step 0x10 (16)
 *   y_origin 0, y_step 0x10 (16)
 *   col_count 4, row_count 4, scroll 0/0
 * Viewport band:  X ∈ [0, 128)   Y ∈ [16, 80)
 * Tile grid: int32, row stride 0x41; default cell = 0x7fffffff (invalid,
 * since 0x7fffffff >= 0xfff0000). */
#define HOVER_GRID_N (0x41 * 8)

static void hover_grid_init(int32_t *g)
{
    for (int i = 0; i < HOVER_GRID_N; i++) {
        g[i] = 0x7fffffff;
    }
}

static engine_camera_hover_in_t hover_base(const int32_t *grid,
                                           int cx, int cy, int game_mode)
{
    engine_camera_hover_in_t in;
    in.cursor_x   = cx;
    in.cursor_y   = cy;
    in.x_origin   = 0;
    in.x_step     = 0x20;
    in.half_step  = 0x10;
    in.y_origin   = 0;
    in.y_step     = 0x10;
    in.col_count  = 4;
    in.row_count  = 4;
    in.scroll_col = 0;
    in.scroll_row = 0;
    in.game_mode  = game_mode;
    in.tile_grid  = grid;
    return in;
}

/* Out-of-band early returns must leave *out untouched (retail keeps its
 * stale globals on a band miss). */
static int hover_miss_keeps_out(int cx, int cy)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    engine_camera_hover_in_t in = hover_base(grid, cx, cy, 0);
    engine_camera_hover_out_t out;
    out.cell_col = 777; out.cell_row = 777;
    out.cell_px_x = 777; out.cell_px_y = 777;
    out.tile_index = 777; out.edge_row = 777; out.edge_col = 777;
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(out.cell_col, 777);
    T_ASSERT_EQ_I(out.tile_index, 777);
    return 0;
}

int test_engine_camera_hover_miss_left(void)   { return hover_miss_keeps_out(-1, 40); }
int test_engine_camera_hover_miss_right(void)  { return hover_miss_keeps_out(128, 40); }
int test_engine_camera_hover_miss_top(void)    { return hover_miss_keeps_out(40, 15); }
int test_engine_camera_hover_miss_bottom(void) { return hover_miss_keeps_out(40, 80); }

/* Valid pick, parity_cr==1 no-bump branch.  cursor (40,40):
 *   row_span 24 → cell_row 1, col_raw 2 → cell_col 1, parity_cr 1,
 *   y_frac 8, x_frac 4 → 15 <= 12 false → no bump.
 *   px = (1*32-16, 16*1) = (16,16); cell (1,1) → grid[1 + 1*0x41]=grid[0x42]. */
int test_engine_camera_hover_valid_pick(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0x42] = 5;
    engine_camera_hover_in_t in = hover_base(grid, 40, 40, 0);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 1);
    T_ASSERT_EQ_I(out.cell_row, 1);
    T_ASSERT_EQ_I(out.cell_px_x, 16);
    T_ASSERT_EQ_I(out.cell_px_y, 16);
    T_ASSERT_EQ_I(out.tile_index, 5);
    T_ASSERT_EQ_I(out.edge_row, 0);
    T_ASSERT_EQ_I(out.edge_col, 0);
    return 0;
}

/* Same cell as above but the grid cell is the invalid sentinel: returns 0,
 * yet cell_col/row/px/tile are STILL written (only edge flags are gated on
 * the valid-tile path). */
int test_engine_camera_hover_invalid_tile_writes_cell(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);                 /* grid[0x42] stays 0x7fffffff */
    engine_camera_hover_in_t in = hover_base(grid, 40, 40, 0);
    engine_camera_hover_out_t out = {0};
    out.edge_row = 333; out.edge_col = 333;
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(out.cell_col, 1);
    T_ASSERT_EQ_I(out.cell_row, 1);
    T_ASSERT_EQ_I(out.tile_index, 0x7fffffff);
    T_ASSERT_EQ_I(out.edge_row, 333);      /* untouched — edge flags gated */
    T_ASSERT_EQ_I(out.edge_col, 333);
    return 0;
}

/* parity_cr==0, x_frac<y_frac → cell_row += 1.  cursor (20,40):
 *   col_raw 1, x_frac 2; row_span 24 → cell_row 1, y_frac 8; parity_cr 0;
 *   2<8 → cell_row 2.  cell_col = 1/2 = 0.  px=(0,32); grid[0 + 2*0x41]. */
int test_engine_camera_hover_par0_row_bump(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0x82] = 7;
    engine_camera_hover_in_t in = hover_base(grid, 20, 40, 0);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 0);
    T_ASSERT_EQ_I(out.cell_row, 2);
    T_ASSERT_EQ_I(out.cell_px_x, 0);
    T_ASSERT_EQ_I(out.cell_px_y, 32);
    T_ASSERT_EQ_I(out.tile_index, 7);
    return 0;
}

/* parity_cr==0, else-if (col_raw odd & parity_r) → cell_col += 1.
 * cursor (30,36): col_raw 1, x_frac 7; row_span 20 → cell_row 1, y_frac 4;
 * parity_cr 0, parity_r 1; 7<4 false → else-if true → cell_col 0+1=1. */
int test_engine_camera_hover_par0_col_bump(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0x42] = 21;
    engine_camera_hover_in_t in = hover_base(grid, 30, 36, 0);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 1);
    T_ASSERT_EQ_I(out.cell_row, 1);
    T_ASSERT_EQ_I(out.cell_px_x, 16);
    T_ASSERT_EQ_I(out.cell_px_y, 16);
    T_ASSERT_EQ_I(out.tile_index, 21);
    return 0;
}

/* parity_cr==1, y_step-1 <= y_frac+x_frac → cell_row += 1.  cursor (44,44):
 *   col_raw 2, x_frac 6; row_span 28 → cell_row 1, y_frac 12; parity_cr 1;
 *   15 <= 18 → cell_row 2 (col_raw even → no col bump). cell_col 1. */
int test_engine_camera_hover_par1_row_bump(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0x83] = 9;
    engine_camera_hover_in_t in = hover_base(grid, 44, 44, 0);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 1);
    T_ASSERT_EQ_I(out.cell_row, 2);
    T_ASSERT_EQ_I(out.cell_px_x, 32);
    T_ASSERT_EQ_I(out.cell_px_y, 32);
    T_ASSERT_EQ_I(out.tile_index, 9);
    return 0;
}

/* game_mode 2 adds a +0x10 cursor-Y bias.  cursor (40,40) game_mode 2:
 *   cursor_y_b 56 → row_span 40 → cell_row 2, y_frac 8; col_raw 2,
 *   x_frac 4; parity_cr 0; 4<8 → cell_row 3. cell_col 1.
 *   px=(1*32-16, 16*3)=(16,48); grid[1 + 3*0x41]=grid[0xc4]. */
int test_engine_camera_hover_game_mode2_bias(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0xc4] = 11;
    engine_camera_hover_in_t in = hover_base(grid, 40, 40, 2);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 1);
    T_ASSERT_EQ_I(out.cell_row, 3);
    T_ASSERT_EQ_I(out.cell_px_x, 16);
    T_ASSERT_EQ_I(out.cell_px_y, 48);
    T_ASSERT_EQ_I(out.tile_index, 11);
    return 0;
}

/* Top-row edge flag: cell_row 0 → edge_row 2.  cursor (40,18):
 *   row_span 2 → cell_row 0, y_frac 2; col_raw 2, x_frac 4; parity_cr 0;
 *   4<2 false, col_raw even → no bump. cell (1,0); grid[1]. */
int test_engine_camera_hover_edge_top(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[1] = 3;
    engine_camera_hover_in_t in = hover_base(grid, 40, 18, 0);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 1);
    T_ASSERT_EQ_I(out.cell_row, 0);
    T_ASSERT_EQ_I(out.cell_px_x, 32);
    T_ASSERT_EQ_I(out.cell_px_y, 0);
    T_ASSERT_EQ_I(out.edge_row, 2);
    T_ASSERT_EQ_I(out.edge_col, 0);
    T_ASSERT_EQ_I(out.tile_index, 3);
    return 0;
}

/* Bottom-row edge flag: cell_row reaches row_count(4) via a parity_cr==1
 * bump → edge_row 1.  cursor (46,78): col_raw 2, x_frac 7; row_span 62 →
 * cell_row 3, y_frac 14; parity_cr 1; 15 <= 21 → cell_row 4.  cell_col 1. */
int test_engine_camera_hover_edge_bottom(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0x105] = 13;
    engine_camera_hover_in_t in = hover_base(grid, 46, 78, 0);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 1);
    T_ASSERT_EQ_I(out.cell_row, 4);
    T_ASSERT_EQ_I(out.cell_px_x, 32);
    T_ASSERT_EQ_I(out.cell_px_y, 64);
    T_ASSERT_EQ_I(out.edge_row, 1);
    T_ASSERT_EQ_I(out.edge_col, 0);
    T_ASSERT_EQ_I(out.tile_index, 13);
    return 0;
}

/* Left col-edge on an ODD row: cell_col 0, cell_row 1 → edge_col 2.
 * cursor (8,40): col_raw 0, x_frac 4; row_span 24 → cell_row 1, y_frac 8;
 * parity_cr 1; 15 <= 12 false → no bump. odd-row X subtracts half_step:
 * px=(0*32-16, 16)= (-16,16). cell (0,1) → grid[0 + 1*0x41]=grid[0x41]. */
int test_engine_camera_hover_edge_left_odd_row(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0x41] = 15;
    engine_camera_hover_in_t in = hover_base(grid, 8, 40, 0);
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 0);
    T_ASSERT_EQ_I(out.cell_row, 1);
    T_ASSERT_EQ_I(out.cell_px_x, -16);
    T_ASSERT_EQ_I(out.cell_px_y, 16);
    T_ASSERT_EQ_I(out.edge_row, 0);
    T_ASSERT_EQ_I(out.edge_col, 2);
    T_ASSERT_EQ_I(out.tile_index, 15);
    return 0;
}

/* scroll offset shifts the grid lookup: same pick as valid_pick but
 * scroll (2,3) → grid index (1+2) + (3+1)*0x41 = 3 + 0x104 = 0x107. */
int test_engine_camera_hover_scroll_offset(void)
{
    int32_t grid[HOVER_GRID_N];
    hover_grid_init(grid);
    grid[0x107] = 99;
    engine_camera_hover_in_t in = hover_base(grid, 40, 40, 0);
    in.scroll_col = 2;
    in.scroll_row = 3;
    engine_camera_hover_out_t out = {0};
    int r = engine_camera_cursor_to_cell(&in, &out);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(out.cell_col, 1);     /* cell unchanged — scroll only moves the lookup */
    T_ASSERT_EQ_I(out.cell_row, 1);
    T_ASSERT_EQ_I(out.tile_index, 99);
    return 0;
}
