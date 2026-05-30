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

/* Shared box scan for FUN_0046cfbd / FUN_0046d130.  Scans the
 * (2*radius+1)-square box centred at (col, row), CLAMPED to the
 * 0..63 grid (unlike region_clear, which rejects out-of-range), in
 * row-major order; returns 1 at the first cell whose head-link byte
 * (+5) is zero AND whose raw-flag byte (+1) satisfies
 * (flag & mask) == expect, else 0.
 *
 * Retail reuses the find-nearest scratch globals DAT_00569570 /
 * DAT_00569578 as the loop counters (a codegen artifact); no caller
 * reads them after these scans (0x46733c / 0x428007 / 0x428078 use
 * only the 0/1 result), so we use locals rather than clobber
 * engine_path's nearest-result globals — same call as region_clear. */
static int box_scan(int col, int row, int radius,
                    unsigned mask, unsigned expect)
{
    if (g_cells == NULL) return 0;

    int side = radius * 2 + 1;
    int x = col - radius;
    int y = row - radius;

    /* Clamp the box to [0, 0x40) per axis, shrinking the extent. */
    int xext, yext;
    if (x < 0)                  { xext = side + x; x = 0; }
    else if (x + side > 0x40)   { xext = 0x40 - x; }
    else                        { xext = side; }
    if (y < 0)                  { yext = side + y; y = 0; }
    else if (y + side > 0x40)   { yext = 0x40 - y; }
    else                        { yext = side; }

    int idx = (y * 0x40 + x) * 8;
    for (int r = y; r < y + yext; r++) {
        for (int c = x; c < x + xext; c++) {
            if (g_cells[idx + 5] == 0 &&
                ((unsigned)g_cells[idx + 1] & mask) == expect)
                return 1;
            idx += 8;
        }
        idx += (0x40 - xext) * 8;   /* skip to next row's box start */
    }
    return 0;
}

/* Port of FUN_0046cfbd.  1 iff the clamped (2*radius+1)-box at
 * (col, row) contains an unlinked cell (head-link +5 == 0) whose
 * raw-flag byte (+1) has bit 0 set; 0 otherwise. */
int engine_map_query_box_any_rawflag_bit0(int col, int row, int radius)
{
    return box_scan(col, row, radius, 0x01, 0x01);
}

/* Port of FUN_0046d130.  1 iff the clamped (2*radius+1)-box at
 * (col, row) contains an unlinked cell (head-link +5 == 0) whose
 * raw-flag byte (+1) has no bits set other than bit 1 (i.e. the
 * value is 0 or 2 — (flag & 0xfd) == 0); 0 otherwise. */
int engine_map_query_box_any_rawflag_only_bit1(int col, int row, int radius)
{
    return box_scan(col, row, radius, 0xfd, 0x00);
}
