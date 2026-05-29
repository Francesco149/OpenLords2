/*
 * OpenLords2 — strategic-map camera-state clamps (implementation).
 *
 * Both functions are verbatim ports of the retail decompile.  Identical
 * branch shape, same boundary sentinels, same write order — so the diff
 * harness sees a single seam at the entry point.
 */

#include "engine_camera.h"

#include "call_trace.h"

void engine_camera_clamp_scroll(int *col, int *row,
                                 int col_count, int row_count)
{
    CALL_TRACE_ENTER(0x00429b1d);
    /* Port of FUN_00429b1d @ 0x429b1d (135 B).  Decompile lines
     * 17006-17024.
     *
     * Lower clamp fires FIRST.  Both axes independent.  Test order
     * (lower-then-upper per axis, col-then-row) matches retail
     * exactly — preserved so any future diff vs retail lines up. */

    if (*col < 0) {
        *col = 0;
    }
    if (*row < 0) {
        *row = 0;
    }

    if (0x41 - col_count <= *col) {
        *col = 0x40 - col_count;
    }
    if (0x81 - row_count <= *row) {
        *row = 0x80 - row_count;
    }
}

void engine_camera_clamp_anchor(int *x, int *y)
{
    /* Port of FUN_0047ed34 @ 0x47ed34 (103 B).  Decompile lines
     * 62798-62816.
     *
     * Constants 0x41 / 0x42 are hardcoded — see engine-quirks #103
     * for the asymmetry note. */

    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
    }

    if (0x41 < *x) {
        *x = 0x41;
    }
    if (0x42 < *y) {
        *y = 0x42;
    }
}

void engine_camera_compute_scroll_pixels(int map_ready,
                                         int scroll_col, int scroll_row,
                                         int col_base, int row_subpos,
                                         int x_step, int half_step,
                                         int x_origin,
                                         int y_step, int y_origin,
                                         int *out_scroll_x, int *out_scroll_y)
{
    CALL_TRACE_ENTER(0x0042a15f);
    /* Port of FUN_0042a15f @ 0x42a15f (206 B).  Converts the current
     * strategic-map cell-scroll position into the back-buffer pixel
     * origin (_DAT_0050a4a8 / DAT_0050a4ac), gated on the absolute cell
     * lying within the 64-wide × 128-tall map.
     *
     * The gate mixes axes the way retail does: the column bound tests
     * (col_base + scroll_col), the row bound tests (scroll_row +
     * row_subpos) — but the pixel math uses col_base / row_subpos only.
     * When the gate fails, retail leaves both outputs untouched (they
     * keep last frame's value); mirrored here by not writing.
     *
     * The odd-row half-step subtraction is the staggered/isometric map's
     * brick-laid row offset. */
    if (map_ready != 0
        && -1 < scroll_row + row_subpos
        && scroll_row + row_subpos < 0x81
        && -1 < col_base + scroll_col
        && col_base + scroll_col < 0x41) {
        int sx = (col_base * x_step - half_step) + x_origin;
        if ((row_subpos & 1) != 0) {
            sx -= half_step;
        }
        *out_scroll_x = sx;
        *out_scroll_y = y_step * row_subpos + y_origin;
    }
}
