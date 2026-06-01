/*
 * Unit tests for engine_map_init — the strategic-map tile_cells builder
 * and camera (ports of FUN_004298c1/0046797d/00451fcc/00498270).
 *
 * The iso projection is the deterministic core (validated against the
 * captured retail tile_cells.bin dump: off980=0 matches 4093/4093 playable
 * cells).  These tests lock in that projection + the view-mode-0 camera
 * without needing L2_maps.dat — the terrain-layer read fails gracefully
 * when the file is absent (cwd = tests dir), leaving the projection cells
 * intact, which is exactly what we assert.
 */

#include "t.h"

#include "engine_map_init.h"

/* tile_cells int-stride: 0x104 bytes = 65 ints per screen row. */
#define CELL_STRIDE_INT (0x104 / 4)

static int32_t cell_at(int screen_row, int screen_col)
{
    return engine_map_init_tile_cells()[screen_col + screen_row * CELL_STRIDE_INT];
}

int test_map_init_projection_matches_retail_samples(void)
{
    /* Build the view-mode-0 projection (rotation 0).  Default dims 64×64,
     * stride 8.  L2_maps.dat is absent in the test cwd → terrain read
     * fails, but the projection still runs. */
    engine_map_init_set_dims(0x40, 0x40, 8);
    engine_map_init_build_cells(NULL, /*map_id=*/0, /*rotation=*/0);

    /* Samples taken from the captured tile_cells.bin (screen_row,
     * screen_col → value): each playable cell holds (cols*map_row +
     * map_col) * stride. */
    T_ASSERT_EQ_I(cell_at(1, 32), 0);      /* mr=0 mc=0 */
    T_ASSERT_EQ_I(cell_at(2, 32), 8);      /* mr=0 mc=1 */
    T_ASSERT_EQ_I(cell_at(2, 31), 512);    /* mr=1 mc=0 → 64*8 */
    T_ASSERT_EQ_I(cell_at(3, 31), 1024);   /* mr=2 mc=0 → 128*8 */
    T_ASSERT_EQ_I(cell_at(3, 32), 520);    /* mr=1 mc=1 → 65*8 */
    return 0;
}

int test_map_init_inactive_cells_stay_empty(void)
{
    engine_map_init_set_dims(0x40, 0x40, 8);
    engine_map_init_build_cells(NULL, 0, 0);

    /* Screen (0,0) is above the diamond (start_row=1) and untouched by the
     * projection; with no terrain file it keeps the 0xfff0000 "empty" tag. */
    T_ASSERT_EQ_U((uint32_t)cell_at(0, 0), 0x0fff0000u);
    return 0;
}

int test_map_init_camera_view0(void)
{
    engine_map_init_set_camera(0);
    T_ASSERT_EQ_I(engine_map_init_cam_x0(), 0);
    T_ASSERT_EQ_I(engine_map_init_cam_y0(), 9);
    T_ASSERT_EQ_I(engine_map_init_col_count(), 8);
    T_ASSERT_EQ_I(engine_map_init_row_count(), 0x1e);
    T_ASSERT_EQ_I(engine_map_init_x_step(), 0x3c);
    T_ASSERT_EQ_I(engine_map_init_y_step(), 0xf);
    T_ASSERT_EQ_I(engine_map_init_x_half_step(), 0x1e);
    T_ASSERT_EQ_I(engine_map_init_y_viewport(), 0x1e);
    return 0;
}

int test_map_init_strategic_sets_default_scroll(void)
{
    /* FUN_00498270 default scroll origin (pre-scroll view). */
    engine_map_init_strategic(NULL, 0);
    T_ASSERT_EQ_I(engine_map_init_col_start(), 0x14);   /* 20 */
    T_ASSERT_EQ_I(engine_map_init_row_start(), 0x4a);   /* 74 */
    return 0;
}
