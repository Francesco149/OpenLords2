/*
 * OpenLords2 — strategic-map grid queries.
 *
 * Pure read-only predicates over the 64×64 map-cell descriptor grid
 * (retail `DAT_00522f90`, 8 bytes per cell, row stride 0x40 cells).
 * The grid is bound once via engine_map_query_bind_cell_descriptors;
 * no Win32 deps, no save state.
 *
 *   FUN_0046d2a3 @ 0x46d2a3 (301 B) → engine_map_query_region_clear
 */

#include <stddef.h>

#include "engine_map_query.h"

/* Per-cell descriptor grid base (retail `DAT_00522f90`). */
static const uint8_t *g_cells;

void engine_map_query_bind_cell_descriptors(const uint8_t *cells)
{
    g_cells = cells;
}

/* Port of FUN_0046d2a3.  Returns 1 iff every cell in the `size`×`size`
 * square anchored at (col, row) is "clear" — i.e. both the raw-flag
 * byte (cell + 1) and the head-link byte (cell + 5) are zero.  Returns
 * 0 if any cell is occupied OR the square would leave the 0..63 grid.
 *
 * Retail reuses the find-nearest scratch globals DAT_00569570 /
 * DAT_00569578 as the inner/outer loop counters (a codegen artifact —
 * they save a couple of stack slots).  The loop-exit values are never
 * consumed, so we use locals here rather than clobber engine_path's
 * nearest-result globals.  See quirk noted in the commit.
 */
int engine_map_query_region_clear(int col, int row, int size)
{
    if (col < 0)            return 0;
    if (col + size >= 0x41) return 0;
    if (row < 0)            return 0;
    if (row + size >= 0x41) return 0;

    if (g_cells == NULL) return 0;

    int idx = (row * 0x40 + col) * 8;   /* byte offset of cell (col,row) */
    for (int r = row; r < row + size; r++) {
        for (int c = col; c < col + size; c++) {
            if (g_cells[idx + 5] != 0) return 0;   /* head-link byte */
            if (g_cells[idx + 1] != 0) return 0;   /* raw-flag byte  */
            idx += 8;
        }
        idx += (0x40 - size) * 8;   /* skip to next row, same start col */
    }
    return 1;
}
