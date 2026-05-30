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

/* Box-scan predicates over a (2*radius+1)-square centred at (col, row),
 * CLAMPED to the 0..63 grid (the box shrinks at the edges rather than
 * being rejected).  Each returns 1 at the first matching cell, else 0.
 * Both require the cell's head-link byte (+5) to be zero; they differ
 * only in the raw-flag byte (+1) test:
 *
 *   _bit0       — (flag & 0x01) == 0x01  (bit 0 set)        ← FUN_0046cfbd
 *   _only_bit1  — (flag & 0xfd) == 0      (flag is 0 or 2)  ← FUN_0046d130
 *
 * On a hit the matching cell's (col, row) is written to *out_col /
 * *out_row; pass NULL for either to ignore it.  (Retail leaves the cell
 * in engine_path's scratch globals DAT_00569570/78 — see the .c.) */
int engine_map_query_box_any_rawflag_bit0(int col, int row, int radius,
                                          int *out_col, int *out_row);
int engine_map_query_box_any_rawflag_only_bit1(int col, int row, int radius,
                                               int *out_col, int *out_row);

/* Port of FUN_0046733c.  Ring-distance probe at (col, row): returns the
 * smallest radius d in 1..5 at which the bit0 box (radius d+1) or the
 * only-bit1 box (radius d) matches, else 0.  bit0 is tested first at
 * each d.  Returns only the distance — see the .c for the caveat about
 * the matched cell position. */
int engine_map_query_nearest_feature_ring(int col, int row);

#endif /* OPENLORDS2_ENGINE_MAP_QUERY_H */
