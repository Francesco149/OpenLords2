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

/* Retail's shared cell-cursor pair: DAT_00569570 (col) / DAT_00569578 (row).
 * region_clear and the box scans use them as their loop counters (so a hit
 * leaves the matching cell in them, a miss the scan's end position), and the
 * AI cell finders (FUN_004a65a3 / FUN_004a6735) leave their result in them.
 * One pair of process globals, written exactly where retail writes them. */
extern int32_t g_cell_cursor_col;   /* DAT_00569570 */
extern int32_t g_cell_cursor_row;   /* DAT_00569578 */

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
 * *out_row; pass NULL for either to ignore it.  The scan cursor itself is
 * g_cell_cursor_col/row (DAT_00569570/78), as in retail. */
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

/* As above, but also writes the matched cell's (col, row) to *out_col /
 * *out_row (the position retail leaves in its DAT_00569570/78 scratch).  Pass
 * NULL to ignore either; the outputs are only meaningful on a non-zero return. */
int engine_map_query_nearest_feature_ring_at(int col, int row,
                                             int *out_col, int *out_row);

/* ── Terrain-info compute — port of FUN_0046d5fe @ 0x46d5fe (502 B) ─────
 * Resolves the descriptor cell under the cursor into the attribute bundle
 * the terrain-info popup ("Here Be Dragons / <terrain>") + the right-panel
 * HUD read.  Pure (unlike the bound-grid predicates above, it takes both
 * source arrays explicitly — fed by engine_camera_cursor_to_cell upstream,
 * which produces the cell byte-offset in `tile_off`).
 *
 *   Inputs:
 *     cell_grid  &DAT_00522f90  8-byte cell descriptors (the same grid the
 *                               predicates above bind as g_cells)
 *     army_recs  &DAT_0053f9b0  per-army/territory records, stride 0x300
 *                               (the port's g_per_army_768)
 *     tile_off   DAT_0056795c   cell BYTE offset under the cursor (cell*8),
 *                               i.e. the hovered tile_index from the cursor
 *                               resolver (the descriptor grid stores byte
 *                               offsets, not cell indices)
 *
 * For a multi-tile feature (width 2) the low nibble of the cell's byte +4
 * encodes the sub-tile position; the compute walks back to the feature's
 * ORIGIN cell (one cell = 8 B, one row = 0x200 B) and reads the attributes
 * there.  The flag byte is post-masked by two terrain-specific rules.
 *
 *   Outputs (retail global ↔ field):
 *     head_link    DAT_005681e0  origin? no — the cursor cell's byte +7
 *                                (army/head-link record index; 0x20 → 0)
 *     army_byte5   DAT_005681d0  army_recs[head_link]+5 (read with the
 *                                ORIGINAL head_link, before the 0x20 reset)
 *     county_id    DAT_0052afbc  origin cell +5 (territory/county id)
 *     terrain_type DAT_0057cb2c  origin cell +0
 *     flags        _DAT_005440b0 origin cell +1, post-masked
 *     cell3        _DAT_00553f78 origin cell +3
 *     derived_bit  DAT_005651bc  1 iff flags&8 and (origin cell +2 & 0x1c)==4
 *     origin_off   the resolved feature-origin cell byte offset (local_8)
 */
typedef struct engine_map_terrain_in {
    const uint8_t *cell_grid;   /* DAT_00522f90, 8 bytes/cell */
    const uint8_t *army_recs;   /* DAT_0053f9b0, stride 0x300 */
    int            tile_off;    /* DAT_0056795c, cell byte offset */
} engine_map_terrain_in_t;

typedef struct engine_map_terrain_out {
    int head_link;      /* DAT_005681e0 */
    int army_byte5;     /* DAT_005681d0 */
    int county_id;      /* DAT_0052afbc */
    int terrain_type;   /* DAT_0057cb2c */
    int flags;          /* _DAT_005440b0 */
    int cell3;          /* _DAT_00553f78 */
    int derived_bit;    /* DAT_005651bc */
    int origin_off;     /* local_8 — resolved feature-origin byte offset */
} engine_map_terrain_out_t;

void engine_map_query_terrain_info(const engine_map_terrain_in_t *in,
                                   engine_map_terrain_out_t *out);

#endif /* OPENLORDS2_ENGINE_MAP_QUERY_H */
