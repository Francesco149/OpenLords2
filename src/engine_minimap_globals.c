/*
 * OpenLords2 — the minimap's two 128×128 grids (engine_minimap.h): the standalone's / host tests' definitions.
 * rhost does not link this file: g_minimap_region_grid is aliased at 0x52aff0 by tools/rhost/rh_hooks_economy.c,
 * g_minimap_type_grid at 0x569590 by tools/rhost/rh_hooks_mapview.c.
 */
#include "engine_minimap.h"

uint8_t g_minimap_region_grid[0x4000];   /* DAT_0052aff0 per-cell county id   */
uint8_t g_minimap_type_grid[0x4000];     /* DAT_00569590 per-cell terrain type */
