/*
 * OpenLords2 — strategic-map grid queries (read-only predicates over
 * the 64×64 map-cell descriptor grid).  See engine_map_query.c.
 */

#ifndef OPENLORDS2_ENGINE_MAP_QUERY_H
#define OPENLORDS2_ENGINE_MAP_QUERY_H

#include <stdint.h>

/* Bind the per-cell descriptor grid (retail `DAT_00522f90`): 8 bytes
 * per cell, 0x40 cells per row, 0x40 rows.  This module reads cell
 * byte +1 (raw-flag) and byte +5 (head-link). */
void engine_map_query_bind_cell_descriptors(const uint8_t *cells);

/* Port of FUN_0046d2a3.  1 iff the `size`×`size` square at (col, row)
 * is entirely clear (raw-flag and head-link bytes zero in every cell)
 * and fits within the 0..63 grid; 0 otherwise. */
int engine_map_query_region_clear(int col, int row, int size);

#endif /* OPENLORDS2_ENGINE_MAP_QUERY_H */
