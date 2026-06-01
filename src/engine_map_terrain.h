/*
 * OpenLords2 — strategic-mode overworld BASE-TERRAIN renderer (pass 1).
 *
 * The strategic map is drawn by three walker clusters over the same
 * cell array (see docs/findings/map-render.md):
 *   pass 1  base terrain   — FUN_0040526e  (THIS MODULE)
 *   pass 2  overlay terrain — 0x405602
 *   pass 3  features        — FUN_00405487  (engine_map_render.c)
 *
 * This module ports pass 1's frame driver + row iterators + the two
 * terrain drawers + the sky/empty drawer:
 *
 *   FUN_0040526e (537 B) → engine_map_terrain_frame
 *   FUN_00405ae9 (326 B) → (internal) aligned phase-0 row
 *   FUN_00405c2f (646 B) → (internal) half-shifted phase-3/0/4 row
 *   FUN_004063c1 (690 B) → (internal) terrain drawer A (base ground)
 *   0x406673     (1351 B)→ (internal) terrain drawer B (decoration) — STUB
 *   FUN_0042a7f1 (224 B) → (internal) sky/empty-cell drawer
 *
 * Drawer A and the sky drawer dispatch the fully-unrolled iso-tile
 * blitters FUN_00452820 / FUN_0045337b / FUN_004538f6 (view-mode 0),
 * replayed byte-exactly from the table-extracted headers in src/gen/.
 *
 * Drawer B (the decoration layer) pulls in a separate, not-yet-ported
 * blitter family (0x455f04 / 459258 / 453e71 / 4580a9 / …); it is
 * currently stubbed (cells counted, not drawn).  See
 * engine_map_terrain_drawb_skipped().
 *
 * State model mirrors engine_map_render.c: private statics for the
 * camera + modes + blob bases, consuming gfx_paint's bound canvas and
 * gfx_glyph's cursor (retail DAT_00591524 / DAT_00591528).
 */

#ifndef OPENLORDS2_ENGINE_MAP_TERRAIN_H
#define OPENLORDS2_ENGINE_MAP_TERRAIN_H

#include <stdint.h>

/* Tile-cell array (retail DAT_0055cea0, row stride 0x104 bytes).
 * Cell values >= ENGINE_MAP_TERRAIN_CELL_EMPTY are the sky/off-map
 * marker (low 20 bits = a blob-0 sprite index). */
void engine_map_terrain_bind_tile_array(const int32_t *cells,
                                        int row_stride_bytes);

/* Per-cell descriptor table (retail 0x522f90, byte-indexed by the
 * raw cell value).  Fields read: +2 flag (bits 0x1c select blob,
 * bit 0x20 render-mode mask), +3 sprite index, +1/+7 territory. */
void engine_map_terrain_bind_descriptors(const uint8_t *descriptors);

/* The five base-terrain sprite blobs (retail DAT_00568208 /
 * 0053e914 / 0057d344 / 0055409c / 0056898c), selected by the
 * descriptor +2 flag's 0x1c bits.  blob[0] is also the sky blob. */
void engine_map_terrain_bind_blobs(const uint8_t *blob0,
                                   const uint8_t *blob1,
                                   const uint8_t *blob2,
                                   const uint8_t *blob3,
                                   const uint8_t *blob4);
void engine_map_terrain_set_blob_lengths(int n0, int n1, int n2,
                                         int n3, int n4);

/* Camera parameters — same retail globals as the feature walker. */
void engine_map_terrain_bind_camera(int x0, int y0,
                                    int col_start, int row_start,
                                    int col_count, int row_count,
                                    int x_step, int y_step,
                                    int x_half_step);

void engine_map_terrain_set_view_mode(int view_mode);     /* DAT_0057cb18 */
void engine_map_terrain_set_render_mode(int mode);        /* DAT_0053f264 */
/* Sky-draw gate companion (retail DAT_0053e9ac).  When 0, the inner
 * iterators draw a sky tile only for empty cells whose sprite index
 * exceeds 6; the phantom rows always draw.  Default 0. */
void engine_map_terrain_set_sky_gate_flag(int flag);

int  engine_map_terrain_error_count(void);                /* _DAT_0058fea8 */
void engine_map_terrain_set_error_count(int n);

/* Stats (test-only, not retail globals). */
int  engine_map_terrain_drawa_count(void);
int  engine_map_terrain_sky_count(void);
int  engine_map_terrain_drawb_count(void);
int  engine_map_terrain_drawb_skipped(void);
int  engine_map_terrain_oob_copies(void);
void engine_map_terrain_reset_stats(void);

#define ENGINE_MAP_TERRAIN_CELL_EMPTY  0xfff0000

/* Port of FUN_0040526e — render one pass-1 base-terrain frame into
 * the gfx_paint-bound canvas. */
void engine_map_terrain_frame(void);

#endif /* OPENLORDS2_ENGINE_MAP_TERRAIN_H */
