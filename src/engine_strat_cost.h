/*
 * OpenLords2 — strategic-map movement-cost grid.
 *
 * The 64×64 strategic overworld has a per-cell movement-cost word grid
 * (retail `DAT_004f4080`, 4096 × i16, row stride 64) that the strategic
 * pathfinder walks.  This is a SEPARATE system from engine_path's 80×80
 * army router (which uses the BFS cost map at retail 0x504030); do not
 * conflate the two.
 *
 *   FUN_0046ff43 @ 0x46ff43 (617 B) → engine_strat_cost_rebuild
 *
 * The cost grid is consumed (read at directional neighbour offsets:
 * ±2 = ±1 col, ±0x80 = ±1 row) by the still-unported strategic
 * flood-fill (0x46f700) and path-trace (0x4701ac).  Those ports
 * will share `g_strat_move_cost` when they land, which is why the grid
 * is owned here rather than parked inside the game-start initialiser
 * (FUN_0046ff43 is one of 0x497ced's leaf seeders).
 *
 * --- The overworld cell descriptor (input) --------------------------
 *
 * The builder reads the overworld terrain grid `g_overworld_map`
 * (retail `DAT_00522f90`, save entry 0 — 8 bytes per cell, 64×64 cells
 * = 32768 B).  Per cell it consults three of the eight bytes:
 *
 *   +0  terrain TYPE  (DAT_00522f90)
 *   +1  terrain FLAGS (DAT_00522f91)
 *   +7  a level/count (DAT_00522f97), gated `< 0x11`
 *
 * (Bytes +1 and +5 are the same "raw flag" / "head-link" bytes that
 * engine_map_query reads for occupancy; see engine_map_query.c.)
 */

#ifndef OPENLORDS2_ENGINE_STRAT_COST_H
#define OPENLORDS2_ENGINE_STRAT_COST_H

#include <stdint.h>

/* Grid geometry — 64×64 cells, one i16 cost word each. */
#define ENGINE_STRAT_COST_GRID_W   0x40   /* 64 */
#define ENGINE_STRAT_COST_GRID_H   0x40   /* 64 */
#define ENGINE_STRAT_COST_CELLS \
    (ENGINE_STRAT_COST_GRID_W * ENGINE_STRAT_COST_GRID_H)   /* 4096 */

/* Bytes per overworld cell descriptor (g_overworld_map stride). */
#define ENGINE_STRAT_COST_DESC_STRIDE 8

/* Canonical home for retail DAT_004f4080 — the strategic per-cell
 * movement-cost grid.  Cell (col, row) lives at index row*64 + col. */
extern int16_t g_strat_move_cost[ENGINE_STRAT_COST_CELLS];

/* Port of FUN_0046ff43.  Rebuilds g_strat_move_cost from the overworld
 * terrain descriptors at `overworld_map` (the 64×64×8-byte grid, i.e.
 * g_overworld_map / retail DAT_00522f90).  Every cell is written; no
 * other state is touched.  Cost legend: 0 = impassable, 1 = road,
 * 3/6 = open/rough terrain, 100 = high-cost (forest/swamp gate). */
void engine_strat_cost_rebuild(const uint8_t *overworld_map);

#endif /* OPENLORDS2_ENGINE_STRAT_COST_H */
