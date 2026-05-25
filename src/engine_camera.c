/*
 * OpenLords2 — strategic-map camera-state clamps (implementation).
 *
 * Both functions are verbatim ports of the retail decompile.  Identical
 * branch shape, same boundary sentinels, same write order — so the diff
 * harness sees a single seam at the entry point.
 */

#include "engine_camera.h"

void engine_camera_clamp_scroll(int *col, int *row,
                                 int col_count, int row_count)
{
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
