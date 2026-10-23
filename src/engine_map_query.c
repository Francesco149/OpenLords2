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

int32_t g_cell_cursor_col;   /* DAT_00569570 */
int32_t g_cell_cursor_row;   /* DAT_00569578 */

/* Port of FUN_0046d2a3.  Returns 1 iff every cell in the `size`×`size`
 * square anchored at (col, row) is "clear" — i.e. both the raw-flag
 * byte (cell + 1) and the head-link byte (cell + 5) are zero.  Returns
 * 0 if any cell is occupied OR the square would leave the 0..63 grid.
 *
 * Retail uses the shared cell cursor DAT_00569570 / DAT_00569578 as the
 * inner/outer loop counters; nothing consumes the exit values, but they are
 * live process state, so the port counts in g_cell_cursor_col/row too
 * (in-process shadow, docs/findings/engine-quirks.md #226).
 */
int engine_map_query_region_clear(int col, int row, int size)
{
    if (col < 0)            return 0;
    if (col + size >= 0x41) return 0;
    if (row < 0)            return 0;
    if (row + size >= 0x41) return 0;

    if (g_cells == NULL) return 0;

    int idx = (row * 0x40 + col) * 8;   /* byte offset of cell (col,row) */
    for (g_cell_cursor_row = row; g_cell_cursor_row < row + size; g_cell_cursor_row++) {
        for (g_cell_cursor_col = col; g_cell_cursor_col < col + size; g_cell_cursor_col++) {
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
 * *out_row when those pointers are non-NULL.  Retail has no out-param:
 * its loop counters ARE the shared cell cursor DAT_00569570 (col) /
 * DAT_00569578 (row), so on the return-1 path they hold the matched
 * cell (quirk #130) and on a miss the scan's end position.  The port
 * counts in g_cell_cursor_col/row exactly the same way (quirk #226) and
 * also surfaces the hit through the out-params its callers use. */
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
    for (g_cell_cursor_row = y; g_cell_cursor_row < y + yext; g_cell_cursor_row++) {
        for (g_cell_cursor_col = x; g_cell_cursor_col < x + xext; g_cell_cursor_col++) {
            if (g_cells[idx + 5] == 0 &&
                ((unsigned)g_cells[idx + 1] & mask) == expect) {
                if (out_col) *out_col = g_cell_cursor_col;
                if (out_row) *out_row = g_cell_cursor_row;
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
    return engine_map_query_nearest_feature_ring_at(col, row, NULL, NULL);
}

/* As engine_map_query_nearest_feature_ring, but also surfaces the matched
 * cell's (col, row) — the position retail leaves in the DAT_00569570/78 scratch
 * for its callers (e.g. FUN_00437535 re-homes the moved army AT this cell).
 * On a hit the matching box scan's cell is written to *out_col / *out_row; on a
 * miss they are left untouched (callers only read them when the return is
 * non-zero, mirroring retail). */
int engine_map_query_nearest_feature_ring_at(int col, int row,
                                             int *out_col, int *out_row)
{
    for (int d = 1; d <= 5; d++) {
        if (engine_map_query_box_any_rawflag_bit0(col, row, d + 1, out_col, out_row))
            return d;
        if (engine_map_query_box_any_rawflag_only_bit1(col, row, d, out_col, out_row))
            return d;
    }
    return 0;
}

/* Port of FUN_0046d5fe @ 0x46d5fe (502 B).  Verbatim branch shape.  See
 * the header for the field/global mapping.  The descriptor grid + army
 * record array are passed explicitly (pure) rather than via the module's
 * g_cells binding — the data flows from engine_camera_cursor_to_cell,
 * which produces tile_off as a cell BYTE offset (cell index * 8). */
void engine_map_query_terrain_info(const engine_map_terrain_in_t *in,
                                   engine_map_terrain_out_t *out)
{
    const uint8_t *cell = in->cell_grid;
    int off = in->tile_off;

    /* Cursor cell's head-link (byte +7); the army-record read uses the
     * ORIGINAL value, the 0x20 → 0 reset happens after. */
    int head_link  = cell[off + 7];
    int army_byte5 = in->army_recs[head_link * 0x300 + 5];

    /* Feature width (local_10): 1 or 2.  bit7 of the flag byte switches to
     * the terrain-type test; else bit6 picks 1 vs 2. */
    int width;
    if ((cell[off + 1] & 0x80) == 0) {
        width = (cell[off + 1] & 0x40) == 0 ? 1 : 2;
    } else if (cell[off + 0] < 0x15) {
        width = 1;
    } else {
        width = 2;
    }

    if (head_link == 0x20) {
        head_link = 0;
    }

    /* For a width-2 feature, walk back to the origin cell using the low
     * nibble of byte +4 (sub-tile position): 1 cell = 8 B, 1 row = 0x200 B. */
    int origin;
    if (width < 2) {
        origin = off;
    } else {
        int nib = cell[off + 4] & 0xf;
        origin = off - (nib % width) * 8 - (nib / width) * 0x200;
    }

    int flags        = cell[origin + 1];
    int county_id    = cell[origin + 5];
    int terrain_type = cell[origin + 0];
    int cell3        = cell[origin + 3];
    int derived_bit  = 0;

    /* Two terrain-specific flag-clear rules, then the derived bit. */
    if ((cell[origin + 1] & 0x80) != 0 && terrain_type == 0x14) {
        flags = 0;
    }
    if ((flags & 0x10) != 0 && terrain_type == 0) {
        flags = 0;
    }
    if ((flags & 8) != 0 && (cell[origin + 2] & 0x1c) == 4) {
        derived_bit = 1;
    }

    out->head_link    = head_link;
    out->army_byte5   = army_byte5;
    out->county_id    = county_id;
    out->terrain_type = terrain_type;
    out->flags        = flags;
    out->cell3        = cell3;
    out->derived_bit  = derived_bit;
    out->origin_off   = origin;
}
