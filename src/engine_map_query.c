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
 * On a hit the matching cell's (col, row) is written to *out_col /
 * *out_row when those pointers are non-NULL.  Retail does not need an
 * out-param: it reuses engine_path's find-nearest scratch globals
 * DAT_00569570 (col) / DAT_00569578 (row) as the loop counters (a
 * codegen artifact), so on the return-1 path those globals already
 * hold the matched cell.  We surface the cell explicitly instead of
 * clobbering engine_path's result globals; callers that ignore the
 * position (the ring finder, region_clear) pass NULL.  This is what
 * lets FUN_00427ed0 place an entity AT the found cell — see
 * docs/findings/engine-quirks.md quirk #130. */
static int box_scan(int col, int row, int radius,
                    unsigned mask, unsigned expect,
                    int *out_col, int *out_row)
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
                ((unsigned)g_cells[idx + 1] & mask) == expect) {
                if (out_col) *out_col = c;
                if (out_row) *out_row = r;
                return 1;
            }
            idx += 8;
        }
        idx += (0x40 - xext) * 8;   /* skip to next row's box start */
    }
    return 0;
}

/* Port of FUN_0046cfbd.  1 iff the clamped (2*radius+1)-box at
 * (col, row) contains an unlinked cell (head-link +5 == 0) whose
 * raw-flag byte (+1) has bit 0 set; 0 otherwise.  On a hit the cell is
 * reported via *out_col / *out_row (pass NULL to ignore). */
int engine_map_query_box_any_rawflag_bit0(int col, int row, int radius,
                                          int *out_col, int *out_row)
{
    return box_scan(col, row, radius, 0x01, 0x01, out_col, out_row);
}

/* Port of FUN_0046d130.  1 iff the clamped (2*radius+1)-box at
 * (col, row) contains an unlinked cell (head-link +5 == 0) whose
 * raw-flag byte (+1) has no bits set other than bit 1 (i.e. the
 * value is 0 or 2 — (flag & 0xfd) == 0); 0 otherwise.  On a hit the
 * cell is reported via *out_col / *out_row (pass NULL to ignore). */
int engine_map_query_box_any_rawflag_only_bit1(int col, int row, int radius,
                                               int *out_col, int *out_row)
{
    return box_scan(col, row, radius, 0xfd, 0x00, out_col, out_row);
}

/* Port of FUN_0046733c.  Ring-distance probe centred at (col, row):
 * returns the smallest radius d in 1..5 at which a feature is found,
 * else 0.  At each d it tests the two box predicates with an
 * ASYMMETRIC radius — the bit0 box uses radius d+1, the only-bit1 box
 * uses radius d — and returns d at the first hit (bit0 tested first).
 *
 * Caveat for future callers: like the box scans it delegates to, the
 * retail body uses engine_path's nearest-result globals
 * (DAT_00569570 / DAT_00569578) as its loop counters, so on a hit
 * those globals hold the matching cell's (col, row).  This finder
 * returns only the ring distance, so it passes NULL for the box scans'
 * position out-params (the matched cell is surfaced where a caller
 * needs it — the army-coord probes in game_init.c thread it through to
 * the entity placer).  See docs/findings/engine-quirks.md quirk #130. */
int engine_map_query_nearest_feature_ring(int col, int row)
{
    for (int d = 1; d <= 5; d++) {
        if (engine_map_query_box_any_rawflag_bit0(col, row, d + 1, NULL, NULL))
            return d;
        if (engine_map_query_box_any_rawflag_only_bit1(col, row, d, NULL, NULL))
            return d;
    }
    return 0;
}
