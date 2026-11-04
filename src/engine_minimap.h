#ifndef OPENLORDS2_ENGINE_MINIMAP_H
#define OPENLORDS2_ENGINE_MINIMAP_H

#include <stdint.h>

/*
 * OpenLords2 — strategic-map minimap (the political overview drawn in
 * the top of the right-edge HUD panel).
 *
 * Retail chain (called from the screen-id-0 arm FUN_0040f5fd via
 * FUN_00410aa9):
 *
 *   FUN_0046a037 (0x46a037) → engine_minimap_load_grids
 *       Reads two 128x128 per-cell byte grids from map0{N}.pl8 into
 *       DAT_0052aff0 (per-cell region/territory id) and DAT_00569590
 *       (per-cell terrain type).  The file index is map_id>>2, the
 *       sub-map is map_id&3; both grid offsets come from a 4-byte
 *       directory entry inside the file header.
 *   FUN_00451bba (0x451bba) → engine_minimap_compute_bands
 *       Per-territory pre-pass: recomputes the three minimap "band"
 *       fields (+1/+2/+3 of each 0x300-stride g_per_army_768 record)
 *       used by the population / food / wealth overlay modes.
 *   FUN_00410cbd (0x410cbd) → engine_minimap_plot
 *       Plots one coloured pixel per land cell of the 128x128 grid via
 *       gfx_paint_pixel_clipped.  Mode 0 = political (owner colour);
 *       modes 1/2/3 = population/food/wealth bands.  Border cells of
 *       the current territory are painted white (index 0x20).
 *   FUN_00410aa9 (0x410aa9) → engine_minimap_render
 *       Composition wrapper: raw-blit the terrain-type grid as the
 *       backdrop, draw the panel frame sprite, run the plot, draw the
 *       mode-toggle button icon.
 *
 * The two grids are real engine globals (retail addresses below); the
 * remaining inputs are save-backed scalars / records already populated
 * by the load path (current territory s_slot_0057c93c, current realm
 * s_slot_0057c8cc, minimap mode s_slot_0057a0c4, territory count
 * s_slot_0056d5dc, per-territory records g_per_army_768).
 */

/* The grids are retail globals (definitions: engine_minimap_globals.c; rhost aliases them at their VAs). */
/* DAT_0052aff0 — 128x128 per-cell region/territory-id grid. */
extern uint8_t g_minimap_region_grid[0x4000];
/* DAT_00569590 — 128x128 per-cell terrain-type grid. */
extern uint8_t g_minimap_type_grid[0x4000];

/* Point the colour lookup at another copy of retail's tables (the band colours DAT_004d28f8, the owner colours
 * DAT_004d2900 = +8; rhost: retail's own).  NULL: the module's copy. */
void engine_minimap_bind_color_tables(const uint8_t *band_colors);

/* Port of FUN_0046a037.  Load both grids from map0{(map_id>>2)+1}.pl8,
 * sub-map (map_id & 3), found in `game_dir`.  Returns 1 on success,
 * 0 if any read fails. */
int engine_minimap_load_grids(const char *game_dir, int map_id);

/* Port of FUN_00451bba.  Recompute the per-territory minimap band
 * fields for territories 1..s_slot_0056d5dc. */
void engine_minimap_compute_bands(void);

/* Port of FUN_00410cbd.  Plot the 128x128 minimap at top-left (x0,y0).
 * `current_territory` (retail param_1, = s_slot_0057c93c) selects the
 * white-outlined region; `mode` (retail param_4, = s_slot_0057a0c4)
 * picks political (0) vs the three overlay modes (1/2/3).  Calls
 * engine_minimap_compute_bands first, exactly as retail. */
void engine_minimap_plot(unsigned current_territory, int x0, int y0, int mode);

/* Port of FUN_00410aa9.  Compose the full minimap at panel anchor
 * (x,y) (retail call site passes 0x1e0,0x19).  `misc_cty` is the chrome
 * sprite blob (retail DAT_005530c8) used for the frame + mode-toggle
 * button; pass NULL to skip those sprites and render only the backdrop
 * blit + coloured plot. */
void engine_minimap_render(const uint8_t *misc_cty, int x, int y);

#endif /* OPENLORDS2_ENGINE_MINIMAP_H */
