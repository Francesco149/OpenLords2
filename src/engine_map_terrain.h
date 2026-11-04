/*
 * OpenLords2 — strategic-mode overworld BASE-TERRAIN renderer (pass 1).
 *
 * The strategic map is drawn by three walker clusters over the same cell array (docs/findings/map-render.md,
 * docs/findings/render-mapview.md), driven by FUN_004050c0 (engine_map_scene):
 *   pass 1  base terrain     — FUN_0040526e  (THIS MODULE)
 *   pass 2  overlay          — FUN_00405602  (engine_overlay_render.c)
 *   pass 3  features         — FUN_00405487  (engine_map_render.c)
 *
 * Retail layout: every global the pass reads or writes is a retail-layout symbol (engine_mapview_globals.h,
 * gfx_globals.h, the save slots) — rhost aliases them at their VAs and runs this module inside retail.
 *
 *   FUN_0040526e → engine_map_terrain_frame          (top phantom row, shifted row, (n-2)/2 pairs, bottom row)
 *   FUN_00405ae9 → engine_map_terrain_row_aligned    (phase-0 row; counts each county's drawn cells)
 *   FUN_00405c2f → engine_map_terrain_row_shifted    (phase 3, 0 …, 4)
 *   FUN_004063c1 → engine_map_terrain_drawer_a       (the ground diamond; stages the descriptor)
 *   FUN_00406673 → engine_map_terrain_drawer_b       (the overhang strip of tall sprites)
 *   FUN_0042a7f1 / 0042a8d1 / 0042a9ab → engine_map_terrain_empty_cell / _empty_left / _empty_right
 *   the unrolled blitters (view modes 0 / 1 / 2): engine_map_terrain_blit_<va> — transliterated from the asm by
 *   tools/gen_mapblit.py (src/gen/mapblit_<va>.h)
 */

#ifndef OPENLORDS2_ENGINE_MAP_TERRAIN_H
#define OPENLORDS2_ENGINE_MAP_TERRAIN_H

#include <stdint.h>

#include "engine_dirty_rect.h"

/* Tile-cell array (retail DAT_0055cea0, row stride 0x104 bytes).  Cell values >= ENGINE_MAP_TERRAIN_CELL_EMPTY
 * (signed) are the empty / off-map marker (low bits = a bank-0 sprite index).  A port binding (retail indexes the
 * array directly): the standalone binds engine_map_init's grid, rhost retail's. */
void engine_map_terrain_bind_tile_array(const int32_t *cells, int row_stride_bytes);

/* Per-cell descriptor table (retail DAT_00522f90 = g_overworld_map, byte-indexed by the raw cell value).  Default
 * g_overworld_map.  Drawer A clears the first-paint bit (+2 bit 0): the table must be writable memory (retail's
 * is; the parameter stays const for the seats that pass a loaded dump). */
void engine_map_terrain_bind_descriptors(const uint8_t *descriptors);

/* The five terrain banks (DAT_00568208 / 0053e914 / 0057d344 / 0055409c / 0056898c): writes the retail globals. */
void engine_map_terrain_bind_blobs(const uint8_t *blob0, const uint8_t *blob1, const uint8_t *blob2,
                                   const uint8_t *blob3, const uint8_t *blob4);
void engine_map_terrain_set_blob_lengths(int n0, int n1, int n2, int n3, int n4);   /* (unused; kept for the seats) */

/* Camera parameters — the shared retail globals (engine_mapview_globals.h). */
void engine_map_terrain_bind_camera(int x0, int y0, int col_start, int row_start, int col_count, int row_count,
                                    int x_step, int y_step, int x_half_step);

void engine_map_terrain_set_view_mode(int view_mode);     /* DAT_0057cb18 */
void engine_map_terrain_set_render_mode(int mode);        /* DAT_0053f264 */

/* Empty-cell redraw gate (retail DAT_0053e9ac — engine_dirty_rect_state_t.below_status_redraw).  When 0 the inner
 * iterators draw an empty cell only when its sprite index is >= 7; the phantom rows always draw.  The gate lives in
 * the bound int (default: this module's own; rhost binds the dirty-rect state's field — the owning port symbol). */
void engine_map_terrain_bind_empty_gate(int *gate);
void engine_map_terrain_set_empty_redraw_gate(int flag);
int  engine_map_terrain_empty_redraw_gate(void);

/* Drawer A's first-paint dirty mark (FUN_00452628).  NULL (default): no mark. */
void engine_map_terrain_set_dirty_state(engine_dirty_rect_state_t *state);

int  engine_map_terrain_error_count(void);                /* DAT_0058fea8 */
void engine_map_terrain_set_error_count(int n);

/* Stats (test-only, not retail globals). */
int  engine_map_terrain_drawa_count(void);
int  engine_map_terrain_empty_draw_count(void);
int  engine_map_terrain_drawb_count(void);
int  engine_map_terrain_drawb_skipped(void);              /* always 0: every blitter is ported */
int  engine_map_terrain_oob_copies(void);                 /* always 0: retail does no bounds check */
void engine_map_terrain_reset_stats(void);

#define ENGINE_MAP_TERRAIN_CELL_EMPTY  0xfff0000

/* Port of FUN_0040526e — render one pass-1 base-terrain frame into the canvas. */
void engine_map_terrain_frame(void);
void engine_map_terrain_row_aligned(void);               /* FUN_00405ae9 */
void engine_map_terrain_row_shifted(void);               /* FUN_00405c2f */
void engine_map_terrain_drawer_a(int phase);             /* FUN_004063c1 */
void engine_map_terrain_drawer_b(int phase);             /* FUN_00406673 */
void engine_map_terrain_empty_cell(int phase);           /* FUN_0042a7f1 */
void engine_map_terrain_empty_left(void);                /* FUN_0042a8d1 */
void engine_map_terrain_empty_right(void);               /* FUN_0042a9ab */

/* The unrolled blitters (retail cdecl (blob, a1[, a2])): the iso tile (view 0 / 1 / 2), its left- / right-edge
 * halves, and the overhang strips (a1 = the walker's y DAT_0056957c, a2 = a source shift 0 / 2). */
void engine_map_terrain_blit_452820(const uint8_t *blob, int phase);
void engine_map_terrain_blit_45337b(const uint8_t *blob, int unused);
void engine_map_terrain_blit_4538f6(const uint8_t *blob, int unused);
void engine_map_terrain_blit_460501(const uint8_t *blob, int phase);
void engine_map_terrain_blit_46079d(const uint8_t *blob, int unused);
void engine_map_terrain_blit_4608de(const uint8_t *blob, int unused);
void engine_map_terrain_blit_463b42(const uint8_t *blob, int phase);
void engine_map_terrain_blit_463c06(const uint8_t *blob, int unused);
void engine_map_terrain_blit_463c62(const uint8_t *blob, int unused);
void engine_map_terrain_strip_453e71(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_455f04(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_456fd8(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_4580a9(const uint8_t *blob, int col_y, int shift);
void engine_map_terrain_strip_459258(const uint8_t *blob, int col_y, int shift);
void engine_map_terrain_strip_460a1c(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_4611ef(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_461623(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_461a54(const uint8_t *blob, int col_y, int shift);
void engine_map_terrain_strip_461efb(const uint8_t *blob, int col_y, int shift);
void engine_map_terrain_strip_463cbe(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_463e91(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_463fa5(const uint8_t *blob, int col_y);
void engine_map_terrain_strip_4640b6(const uint8_t *blob, int col_y, int shift);
void engine_map_terrain_strip_464209(const uint8_t *blob, int col_y, int shift);

#endif /* OPENLORDS2_ENGINE_MAP_TERRAIN_H */
