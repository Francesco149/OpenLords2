/*
 * OpenLords2 — strategic-map state initializer.  See engine_map_init.h.
 */

#include "engine_map_init.h"

#include <stdio.h>
#include <string.h>

#include "io.h"
#include "engine_territory.h"   /* g_cell_sweep_counter (DAT_0053e974) */

/* tile_cells (retail DAT_0055cea0).  int view over the 0x81×0x104 extent. */
static int32_t g_tile_cells[ENGINE_MAP_INIT_CELLS_BYTES / 4];

/* Map dims (retail DAT_0052afd4 / DAT_005694f8 / DAT_0057d338) + the two
 * projection offsets (DAT_0053e980 / DAT_0052afc8) and rotation
 * (DAT_00522f7c).  Defaults = the values FUN_00498270 writes. */
static int g_cols   = 0x40;
static int g_rows   = 0x40;
static int g_stride = 8;
static int g_off980 = 0;     /* DAT_0053e980 */
static int g_offafc8 = 0;    /* DAT_0052afc8 */
static int g_rotation = 0;   /* DAT_00522f7c */

/* Camera scalars (retail DAT_*; see header getters). */
static int g_cam_x0, g_cam_y0, g_col_count, g_row_count;
static int g_x_step, g_y_step, g_x_half, g_y_viewport;
static int g_col_start = 0x14;   /* DAT_005651b4 (map-init default 20) */
static int g_row_start = 0x4a;   /* DAT_005651b8 (map-init default 74) */

int32_t *engine_map_init_tile_cells(void) { return g_tile_cells; }

void engine_map_init_set_dims(int cols, int rows, int stride)
{
    g_cols = cols;
    g_rows = rows;
    g_stride = stride;
}

/* Index a tile_cells entry by (screen_row, screen_col) in int units. */
static int32_t *cell(int screen_row, int screen_col)
{
    /* byte offset = screen_col*4 + screen_row*0x104; /4 → int index. */
    return &g_tile_cells[screen_col + screen_row * (ENGINE_MAP_INIT_CELLS_STRIDE / 4)];
}

static char *join_path(char *out, size_t cap, const char *dir, const char *name)
{
    if (dir && dir[0]) {
        size_t dl = strlen(dir);
        int slash = (dl > 0 && (dir[dl - 1] == '/' || dir[dl - 1] == '\\'));
        snprintf(out, cap, "%s%s%s", dir, slash ? "" : "/", name);
    } else {
        snprintf(out, cap, "%s", name);
    }
    return out;
}

/* Port of FUN_0046797d.  Read the L2_maps.dat terrain block for `map_id`
 * and seed every tile_cells entry (0x81×0x41) with the terrain byte at
 * block offset 0x6000 + linear-index, OR'd into the 0xfff0000 "empty"
 * tag.  Returns 1 on success, 0 if the file read fails. */
static int seed_terrain_layer(const char *map_dir, int map_id)
{
    /* One L2_maps.dat block is 0x80c1 bytes; the terrain grid is the
     * 0x81×0x41 = 0x2085 bytes at block offset 0x6000. */
    static uint8_t block[0x80c1];
    char path[512];
    join_path(path, sizeof path, map_dir, "L2_maps.dat");

    if (io_read_file_at(path, block, sizeof block, map_id * (int)sizeof block) == 0) {
        return 0;
    }

    g_cell_sweep_counter = 0;        /* DAT_0053e974: retail's index here */
    for (int r = 0; r < 0x81; r++) {
        for (int c = 0; c < 0x41; c++) {
            *cell(r, c) = (int32_t)(0xfff0000u + block[g_cell_sweep_counter + 0x6000]);
            g_cell_sweep_counter++;
        }
    }
    return 1;
}

int engine_map_init_build_cells(const char *map_dir, int map_id, int rotation)
{
    /* Init every cell to 0xfff0000 ("empty"). */
    for (int r = 0; r < 0x81; r++) {
        for (int c = 0; c < 0x41; c++) {
            *cell(r, c) = (int32_t)0x0fff0000;
        }
    }

    /* Terrain layer from L2_maps.dat (overwrites the init above). */
    int ok = seed_terrain_layer(map_dir, map_id);

    g_rotation = rotation;           /* DAT_00522f7c */

    /* Per-rotation iso projection seeds (retail FUN_004298c1 branch). */
    int start_row, row_step_outer, row_step_inner;
    int start_col2, col2_step_outer, col2_step_inner;
    if (rotation == 0) {
        start_row = g_off980 * 2 + 1; row_step_outer = 1;  row_step_inner = 1;
        start_col2 = 0x40;            col2_step_outer = -1; col2_step_inner = 1;
    } else if (rotation == 2) {
        start_row = 0x42;             row_step_outer = 1;  row_step_inner = -1;
        start_col2 = g_offafc8 * 2 + 1; col2_step_outer = 1; col2_step_inner = 1;
    } else if (rotation == 4) {
        start_row = g_off980 * -2 + 0x7f; row_step_outer = -1; row_step_inner = -1;
        start_col2 = 0x42;            col2_step_outer = 1;  col2_step_inner = -1;
    } else { /* rotation == 6 */
        start_row = 0x42;             row_step_outer = -1; row_step_inner = 1;
        start_col2 = g_off980 * -2 + 0x7f; col2_step_outer = -1; col2_step_inner = -1;
    }

    /* Project each active map cell (map_row, map_col) to its screen
     * diamond position and store its linear-index * stride. */
    for (int mr = 0; mr < g_rows; mr++) {
        int sr = start_row;
        int sc2 = start_col2;
        for (int mc = 0; mc < g_cols; mc++) {
            *cell(sr, sc2 / 2) = (int32_t)((g_cols * mr + mc) * g_stride);
            sr += row_step_inner;
            sc2 += col2_step_inner;
        }
        start_row += row_step_outer;
        start_col2 += col2_step_outer;
    }

    return ok;
}

/* Port of FUN_00451fcc. */
void engine_map_init_set_camera(int view_mode)
{
    if (view_mode == 0) {
        g_col_count = 8;    g_row_count = 0x1e; g_cam_x0 = 0;
        g_cam_y0 = 9;       g_x_step = 0x3c;    g_y_viewport = 0x1e;
        g_x_half = 0x1e;    g_y_step = 0xf;
    } else if (view_mode == 1) {
        g_col_count = 0x11; g_row_count = 0x40; g_cam_x0 = 4;
        g_cam_y0 = 0x11;    g_x_step = 0x1c;    g_y_viewport = 0xe;
        g_x_half = 0xe;     g_y_step = 7;
    } else { /* view_mode == 2 */
        g_col_count = 0x28; g_row_count = 0x80; g_cam_x0 = 0;
        g_cam_y0 = 0x15;    g_x_step = 0xc;     g_y_viewport = 6;
        g_x_half = 6;       g_y_step = 3;
    }
    /* The two derived scroll-extent globals (DAT_0055cd60 / DAT_00553254)
     * gate map scrolling; not read by the render path, so not surfaced. */
}

int engine_map_init_cam_x0(void)      { return g_cam_x0; }
int engine_map_init_cam_y0(void)      { return g_cam_y0; }
int engine_map_init_col_count(void)   { return g_col_count; }
int engine_map_init_row_count(void)   { return g_row_count; }
int engine_map_init_x_step(void)      { return g_x_step; }
int engine_map_init_y_step(void)      { return g_y_step; }
int engine_map_init_x_half_step(void) { return g_x_half; }
int engine_map_init_y_viewport(void)  { return g_y_viewport; }
int engine_map_init_col_start(void)   { return g_col_start; }
int engine_map_init_row_start(void)   { return g_row_start; }

/* Override the scroll origin (DAT_005651b4 / DAT_005651b8).  The LOAD path
 * uses this to restore the save-carried scroll after engine_map_init_strategic
 * resets it to the new-game default — retail's load (FUN_004976a1) never
 * runs FUN_00498270's default-scroll write, so the save's camera survives;
 * the port reuses _strategic and restores afterward. */
void engine_map_init_set_scroll(int col_start, int row_start)
{
    g_col_start = col_start;
    g_row_start = row_start;
}

int engine_map_init_strategic(const char *map_dir, int map_id)
{
    /* FUN_00498270: strategic-map dims + default scroll origin. */
    g_cols = 0x40;
    g_rows = 0x40;
    g_stride = 8;
    g_off980 = 0;
    g_offafc8 = 0;
    g_col_start = 0x14;   /* DAT_005651b4 */
    g_row_start = 0x4a;   /* DAT_005651b8 */

    int ok = engine_map_init_build_cells(map_dir, map_id, 0);
    engine_map_init_set_camera(0);

    /* Retail also runs the per-territory post-load fixup FUN_0046f3cd
     * (151× FUN_0046ead0) here; that recomputes per-territory derived
     * fields the AI/economy read, not the render arrays — deferred. */
    return ok;
}
