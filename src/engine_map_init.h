/*
 * OpenLords2 — strategic-map state initializer (the "live data path").
 *
 * Builds the derived render arrays the strategic-map walk consumes, from
 * loaded save state + the L2_maps.dat terrain file — the native producer
 * that replaces the --strat-map bring-up shim's captured-dump seeds.
 *
 * Ports the map-entry chain that runs once the engine switches to the
 * strategic-map screen (after a new game or a save load):
 *
 *   FUN_00498270 (327 B) → engine_map_init_strategic
 *       sets the map dims (64×64, cell stride 8) + assorted flags, then:
 *     FUN_004298c1 (604 B) → engine_map_init_build_cells
 *         init tile_cells (0x55cea0) to 0xfff0000 ("empty"), seed the
 *         terrain layer from L2_maps.dat, then overwrite the active
 *         region with the iso territory-index projection
 *       FUN_0046797d (185 B) → (folded in) reads L2_maps.dat[map_id] and
 *         fills tile_cells with terrain bytes (byte + 0xfff0000)
 *     FUN_00451fcc (385 B) → engine_map_init_set_camera
 *         the per-view-mode camera scalars (origin / step / counts)
 *
 * tile_cells (retail `DAT_0055cea0`) is a 0x81-row × 0x104-byte-stride
 * int array; each entry is either 0xfff0000|terrain_byte for an inactive
 * (sky/border) cell or `linear_map_index * stride` for an active map cell
 * (the index the descriptor / territory / blitter tables are addressed
 * by).  See docs/findings/map-render.md.
 */

#ifndef OPENLORDS2_ENGINE_MAP_INIT_H
#define OPENLORDS2_ENGINE_MAP_INIT_H

#include <stdint.h>

/* tile_cells extent (retail 0x55cea0): 0x81 rows × 0x104 bytes. */
#define ENGINE_MAP_INIT_CELLS_ROWS    0x81
#define ENGINE_MAP_INIT_CELLS_STRIDE  0x104
#define ENGINE_MAP_INIT_CELLS_BYTES   (ENGINE_MAP_INIT_CELLS_ROWS * ENGINE_MAP_INIT_CELLS_STRIDE)

/* The built tile_cells array (int view).  Stable for the program
 * lifetime; bind it to the render clusters via their *_bind_tile_array
 * setters with stride ENGINE_MAP_INIT_CELLS_STRIDE. */
int32_t *engine_map_init_tile_cells(void);

/* Override the map dimensions (retail DAT_0052afd4 cols / DAT_005694f8
 * rows / DAT_0057d338 stride).  Defaults to the strategic-map values the
 * map-init sets (64 / 64 / 8); exposed for tests. */
void engine_map_init_set_dims(int cols, int rows, int stride);

/* Port of FUN_004298c1 (+ folded FUN_0046797d).  Build tile_cells for the
 * current dims at `rotation` (0/2/4/6; view-mode 0 uses 0).  Reads the
 * terrain layer from "<map_dir>/L2_maps.dat" block `map_id` (retail
 * DAT_0053f034 / s_slot_0053f034); map_dir NULL/"" = cwd.  Returns 1 on
 * success, 0 if the L2_maps.dat read fails (tile_cells is still fully
 * built minus the terrain layer in that case). */
int engine_map_init_build_cells(const char *map_dir, int map_id, int rotation);

/* Port of FUN_00451fcc.  Set the camera scalars for `view_mode` (0/1/2).
 * The values land in module state readable through the getters below
 * (retail writes them to the DAT_* camera globals the clusters read). */
void engine_map_init_set_camera(int view_mode);

/* Camera getters (after engine_map_init_set_camera / _strategic).  Names
 * mirror the engine_map_render bind params. */
int engine_map_init_cam_x0(void);        /* DAT_0052afdc */
int engine_map_init_cam_y0(void);        /* DAT_00553244 */
int engine_map_init_col_count(void);     /* DAT_0053e8ac */
int engine_map_init_row_count(void);     /* DAT_0056d67c */
int engine_map_init_x_step(void);        /* DAT_00568220 */
int engine_map_init_y_step(void);        /* DAT_0053f65c */
int engine_map_init_x_half_step(void);   /* DAT_00567950 */
int engine_map_init_y_viewport(void);    /* DAT_0057c97c */
int engine_map_init_col_start(void);     /* DAT_005651b4 (map-init default 20) */
int engine_map_init_row_start(void);     /* DAT_005651b8 (map-init default 74) */

/* Port of FUN_00498270.  Full strategic-map init: set dims (64×64,
 * stride 8) + the default col/row start (20/74), build tile_cells
 * (rotation 0) from "<map_dir>/L2_maps.dat" block `map_id`, and set the
 * view-mode-0 camera.  Returns the build_cells result. */
int engine_map_init_strategic(const char *map_dir, int map_id);

#endif /* OPENLORDS2_ENGINE_MAP_INIT_H */
