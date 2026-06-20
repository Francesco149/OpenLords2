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

int engine_camera_cursor_to_cell(const engine_camera_hover_in_t *in,
                                 engine_camera_hover_out_t *out)
{
    CALL_TRACE_ENTER(0x00429ba4);
    /* Port of FUN_00429ba4 @ 0x429ba4 (878 B).  Verbatim branch shape.
     *
     * Outer viewport band tests (early-return 0 leaves *out untouched):
     *   X: x_origin <= cursor_x < x_step*col_count + x_origin
     *   Y: y_step + y_origin <= cursor_y
     *        < y_step*row_count + y_step + y_origin  ( = y_step*(row_count+1)+y_origin )
     * The top row [y_origin, y_origin+y_step) is intentionally EXCLUDED
     * (the HUD/offscreen header band), matching retail's lower-Y bound. */
    if (in->cursor_x < in->x_origin) {
        return 0;
    }
    if (in->cursor_x >= in->x_step * in->col_count + in->x_origin) {
        return 0;
    }
    if (in->cursor_y < in->y_step + in->y_origin) {
        return 0;
    }
    if (in->cursor_y >= in->y_step * in->row_count + in->y_step + in->y_origin) {
        return 0;
    }

    /* cursor-Y bias.  Retail has dead param_1 / screen sub-branches that
     * all yield 0 (a Ghidra-visible artifact); only game-mode 2 (the
     * campaign overview) biases the pick down by 0x10.  Collapsed. */
    int y_bias = (in->game_mode == 2) ? 0x10 : 0;

    int cursor_y_b = y_bias + in->cursor_y;
    int row_span   = cursor_y_b - (in->y_step + in->y_origin);  /* >= 0 */
    int cell_row   = row_span / in->y_step;                     /* DAT_005681f4 */

    int col_raw    = (in->cursor_x - in->x_origin) / in->half_step; /* uVar2 */
    int parity_cr  = (cell_row + col_raw) & 1;                  /* uVar3 */
    int parity_r   = cell_row & 1;                              /* uVar4 */
    int x_frac     = ((in->cursor_x - in->x_origin) % in->half_step) / 2; /* iVar5 */
    int y_frac     = row_span % in->y_step;                     /* iVar6 */
    int cell_col   = col_raw / 2;                               /* DAT_00553f04 */

    if (parity_cr == 0) {
        if (x_frac < y_frac) {
            cell_row += 1;
        } else if ((col_raw & 1) != 0 && parity_r != 0) {
            cell_col += 1;
        }
    } else if (parity_cr == 1) {
        if (in->y_step - 1 <= y_frac + x_frac) {
            cell_row += 1;
            if ((col_raw & 1) != 0 && parity_r == 0) {
                cell_col += 1;
            }
        }
    }

    int cell_px_x = cell_col * in->x_step + in->x_origin;       /* _DAT_00553ff8 */
    if ((cell_row & 1) != 0) {
        cell_px_x -= in->half_step;
    }
    int cell_px_y = in->y_step * cell_row + in->y_origin;       /* _DAT_00553fe8 */

    /* Tile-index lookup.  Retail byte offset
     *   (cell_col+scroll_col)*4 + (scroll_row+cell_row)*0x104
     * over &DAT_0055cea0 → int32 index with row stride 0x104/4 = 0x41. */
    int32_t tile = in->tile_grid[(cell_col + in->scroll_col) +
                                 (in->scroll_row + cell_row) * 0x41];

    out->cell_col    = cell_col;
    out->cell_row    = cell_row;
    out->cell_px_x   = cell_px_x;
    out->cell_px_y   = cell_px_y;
    out->tile_index  = tile;

    /* Signed compare, verbatim (0xfff0000 is a positive int literal; the
     * invalid-tile sentinel is >= 0xfff0000). */
    if (tile < 0xfff0000) {
        out->edge_row = 0;                                      /* DAT_0056d654 */
        out->edge_col = 0;                                      /* DAT_0057cab8 */
        if (cell_row == 0) {
            out->edge_row = 2;
        } else if (in->row_count <= cell_row) {
            out->edge_row = 1;
        }
        if ((cell_row & 1) != 0) {
            if (cell_col == 0) {
                out->edge_col = 2;
            } else if (in->col_count <= cell_col) {
                out->edge_col = 1;
            }
        }
        return 1;
    }
    return 0;
}
