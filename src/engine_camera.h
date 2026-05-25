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

#endif /* OPENLORDS2_ENGINE_CAMERA_H */
