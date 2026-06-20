/*
 * OpenLords2 — strategic-map camera-state clamps.
 *
 * Two trivial pure functions sitting at the head of retail's
 * render-phase tail.  Both clamp 2-D integer state to fixed bounds
 * pulled straight from the strategic-map / battlefield viewport
 * geometry.
 *
 *   FUN_00429b1d @ 0x429b1d (135 B) → engine_camera_clamp_scroll
 *   FUN_0047ed34 @ 0x47ed34 (103 B) → engine_camera_clamp_anchor
 *
 * Both run unconditionally every frame inside the
 * `DAT_005c9274 == 0` (g_savepump_request) gate at the top of
 * retail's render tail (decompile lines 88725-88726).  They are
 * the first two functions in that block; the FPS counter
 * (`FUN_004b27bf`, already ported) comes a couple of slots later.
 *
 * Pure C; no callees; every read/write goes through the int*
 * out-params so the host tests don't need globals.  Matches the
 * shape of engine_path's 8-way direction helper.
 *
 * Reference: docs/findings/main-loop-tick.md §"render-phase tail";
 * decompile lines 17006 (FUN_00429b1d) and 62798 (FUN_0047ed34).
 *
 * --- Retail global ↔ parameter mapping ------------------------------
 *
 *   FUN_00429b1d (scroll clamp — strategic-map cell-scroll position):
 *     DAT_005651b4    *col          (in/out)
 *     DAT_005651b8    *row          (in/out)
 *     DAT_0053e8ac     col_count    (in;  visible columns this frame)
 *     DAT_0056d67c     row_count    (in;  visible rows    this frame)
 *
 *   FUN_0047ed34 (anchor clamp — strategic-map / battlefield camera
 *                 anchor position, also written by entity-centering and
 *                 castle-table-driven re-centering):
 *     DAT_0055ce70    *x            (in/out)
 *     DAT_005678a0    *y            (in/out)
 *
 * Both functions are called from the render-phase tail
 * (DAT_005c9274 == 0 gate) and from a handful of other spots
 * (FUN_00429b1d gets called from multiple navigation handlers
 * after they mutate the scroll position; FUN_0047ed34 gets called
 * after the keyboard-arrow handler and the castle-table re-center).
 * The pure functions can be reused at every call site.
 */

#ifndef OPENLORDS2_ENGINE_CAMERA_H
#define OPENLORDS2_ENGINE_CAMERA_H

#include <stdint.h>

/* Strategic-map cell-scroll clamp.  Bounds are derived from the
 * retail strategic-map dimensions:
 *
 *   col ∈ [0, 0x40 - col_count]    (max map width = 64 cells)
 *   row ∈ [0, 0x80 - row_count]    (max map height = 128 cells)
 *
 * The upper-bound test in retail is written as
 *   `if (0x41 - col_count <= col) col = 0x40 - col_count;`
 * which is equivalent to `if (col > 0x40 - col_count) col = 0x40 - col_count;`
 * — a standard `clamp_max` but written as `>=` over a one-greater
 * sentinel.  Preserved verbatim for parity.
 *
 * Quirk #104: if `col_count` exceeds 0x40 (or `row_count` exceeds
 * 0x80) the upper-bound expression goes negative.  Retail's lower
 * clamp fires first, so a 0-clamped value can be re-set to a
 * negative value by the upper clamp on the same call.  Never
 * observed in practice (visible counts max around 0x10) but
 * documented and pinned in tests.
 */
void engine_camera_clamp_scroll(int *col, int *row,
                                 int col_count, int row_count);

/* Strategic-map / battlefield camera-anchor clamp.  Hardcoded bounds:
 *
 *   x ∈ [0, 0x41]    (65)
 *   y ∈ [0, 0x42]    (66)
 *
 * Asymmetric by one — bound on y is one greater than x.  No callable
 * basis for the asymmetry in the surrounding code; quirk #103.
 * Upper-bound test is written `if (0x41 < x) x = 0x41;` (strict less),
 * so 0x41 is INCLUSIVE.
 */
void engine_camera_clamp_anchor(int *x, int *y);

/* Strategic-map cell-scroll → back-buffer pixel-origin conversion.
 * Port of FUN_0042a15f @ 0x42a15f (206 B).
 *
 *   Retail global ↔ parameter mapping:
 *     map_ready    DAT_005533b0  (gate; 0 = no map → leave outputs alone)
 *     scroll_col   DAT_005651b4  (clamped cell-scroll col; gate only)
 *     scroll_row   DAT_005651b8  (clamped cell-scroll row; gate only)
 *     col_base     DAT_00553f04  (cell column used for the X pixel math)
 *     row_subpos   DAT_005681f4  (cell row; drives Y + the odd-row shift)
 *     x_step       DAT_00568220  (per-column pixel step)
 *     half_step    DAT_00567950  (tile half-width; staggered-row offset)
 *     x_origin     DAT_0052afdc  (X pixel origin)
 *     y_step       DAT_0053f65c  (per-row pixel step)
 *     y_origin     DAT_00553244  (Y pixel origin)
 *   Outputs:
 *     *out_scroll_x  _DAT_0050a4a8
 *     *out_scroll_y   DAT_0050a4ac
 *
 * Gate: writes the outputs only when map_ready != 0 AND the absolute
 * cell is on the map ((col_base+scroll_col) ∈ [0,0x40], (scroll_row+
 * row_subpos) ∈ [0,0x80]).  Odd row_subpos shifts X left by half_step.
 */
void engine_camera_compute_scroll_pixels(int map_ready,
                                         int scroll_col, int scroll_row,
                                         int col_base, int row_subpos,
                                         int x_step, int half_step,
                                         int x_origin,
                                         int y_step, int y_origin,
                                         int *out_scroll_x, int *out_scroll_y);

/* ── Cursor → strategic-map cell resolver ──────────────────────────────
 * Port of FUN_00429ba4 @ 0x429ba4 (878 B).  The INVERSE of
 * engine_camera_compute_scroll_pixels: given the screen cursor and the
 * live viewport geometry, it picks the staggered-row map cell under the
 * cursor, looks up its tile index, and reports whether the cursor is over
 * a valid map tile.  This is the producer of retail's DAT_005533b0
 * ("map_ready"/cursor-over-map) gate and DAT_00568974 (the hovered-tile
 * index) that the in-game input dispatcher (FUN_0042ff10) reads to route
 * map clicks (army-select / terrain-info).
 *
 *   Retail global ↔ field (inputs):
 *     cursor_x     DAT_004e65fc   screen cursor X
 *     cursor_y     DAT_004e65f4   screen cursor Y
 *     x_origin     DAT_0052afdc   map viewport X pixel origin
 *     x_step       DAT_00568220   per-column pixel step
 *     half_step    DAT_00567950   tile half-width (staggered-row offset)
 *     y_origin     DAT_00553244   map viewport Y pixel origin
 *     y_step       DAT_0053f65c   per-row pixel step
 *     col_count    DAT_0053e8ac   visible columns
 *     row_count    DAT_0056d67c   visible rows
 *     scroll_col   DAT_005651b4   cell-scroll column
 *     scroll_row   DAT_005651b8   cell-scroll row
 *     game_mode    DAT_0057c8fc   game-mode enum (mode 2 → +0x10 cursor-Y bias)
 *     tile_grid    &DAT_0055cea0  int32 tile-index grid, ROW STRIDE 0x41 ints
 *
 *   Retail global ↔ field (outputs — written ONLY when the cursor lies in
 *   the viewport band; on a band miss the struct is left untouched,
 *   mirroring retail's stale globals on an early return):
 *     cell_col     DAT_00553f04   picked cell column
 *     cell_row     DAT_005681f4   picked cell row
 *     cell_px_x    _DAT_00553ff8  cell back-buffer pixel X
 *     cell_px_y    _DAT_00553fe8  cell back-buffer pixel Y
 *     tile_index   DAT_00568974   tile index at the picked cell
 *     edge_row     DAT_0056d654   0 / 2(top) / 1(bottom) row-edge flag
 *     edge_col     DAT_0057cab8   0 / 2(left) / 1(right) col-edge (odd rows only)
 *
 * Returns 1 iff the cursor is over a valid map tile (tile_index <
 * 0xfff0000, signed compare per retail) — the value retail stores in
 * DAT_005533b0.  Returns 0 outside the viewport band or over an invalid
 * tile.  edge_row/edge_col are written only on the valid-tile (return 1)
 * path; the other outputs are written whenever the cursor is in the band.
 */
typedef struct {
    int            cursor_x, cursor_y;
    int            x_origin, x_step, half_step;
    int            y_origin, y_step;
    int            col_count, row_count;
    int            scroll_col, scroll_row;
    int            game_mode;
    const int32_t *tile_grid;   /* base &DAT_0055cea0, row stride 0x41 int32 */
} engine_camera_hover_in_t;

typedef struct {
    int     cell_col, cell_row;
    int     cell_px_x, cell_px_y;
    int32_t tile_index;
    int     edge_row, edge_col;
} engine_camera_hover_out_t;

int engine_camera_cursor_to_cell(const engine_camera_hover_in_t *in,
                                 engine_camera_hover_out_t *out);

#endif /* OPENLORDS2_ENGINE_CAMERA_H */
