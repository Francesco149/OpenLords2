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
