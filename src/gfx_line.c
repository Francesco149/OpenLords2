/*
 * OpenLords2 — line + axis-rect drawers (implementation).
 *
 * See gfx_line.h for the function map and quirk narratives.  The
 * Bresenham core and the rectangle wrappers each match their retail
 * body statement-for-statement; the only deviations are the leading-
 * underscore renaming and the `gfx_paint_canvas_*` accessors in place
 * of the bare DAT_004eb274 / DAT_004eac4c reads.
 */

#include "gfx_line.h"
#include "gfx_globals.h"

#include "gfx_paint.h"

/* ──────────────────────────────────────────────────────────────────
 * State: the nine Bresenham registers, retail globals (gfx_globals.h,
 * DAT_005aeb40..60 + DAT_0058fd5c).  Note: abs_dx / abs_dy are MUTATED
 * by the line-draw loop (decremented per iteration) — see quirk #38.
 * ────────────────────────────────────────────────────────────────── */

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_004037ee — Bresenham setup
 *
 * Writes the 8 endpoint/step slots used by the line drawer:
 *   - abs_dx, abs_dy:  absolute deltas (mutated during the draw loop).
 *   - x_start, y_start, x_end, y_end:  endpoints reordered so the
 *     loop runs from start → end in the positive direction of the
 *     DOMINANT axis (the one with the larger |delta|).
 *   - x_step, y_step:  +1 or -1 step sign for the non-dominant axis
 *     (and the dominant axis when the line is degenerate / pure-axis).
 *
 * Reordering rule:
 *   - For steep lines (|dy| > |dx|): the endpoint with the smaller y
 *     becomes start.
 *   - For shallow lines (|dx| >= |dy|): the endpoint with the smaller
 *     x becomes start.
 *
 * After reordering, the step-sign comparison fires on the non-dominant
 * axis only (the dominant axis is monotonically increasing by
 * construction): if x_end < x_start → x_step = -1; if y_end < y_start
 * → y_step = -1.
 * ────────────────────────────────────────────────────────────────── */
static void bres_setup(int x1, int y1, int x2, int y2)
{
    g_gfx_y_step = 1;
    g_gfx_x_step = 1;
    g_gfx_abs_dx = (x2 < x1) ? (x1 - x2) : (x2 - x1);
    g_gfx_abs_dy = (y2 < y1) ? (y1 - y2) : (y2 - y1);

    if (g_gfx_abs_dx < g_gfx_abs_dy) {
        /* Steep — sort by y. */
        if (y2 < y1) {
            g_gfx_y_start = y2;  g_gfx_y_end = y1;
            g_gfx_x_start = x2;  g_gfx_x_end = x1;
        } else {
            g_gfx_y_start = y1;  g_gfx_y_end = y2;
            g_gfx_x_start = x1;  g_gfx_x_end = x2;
        }
    } else if (x2 < x1) {
        /* Shallow, second endpoint to the left — swap. */
        g_gfx_y_start = y2;  g_gfx_y_end = y1;
        g_gfx_x_start = x2;  g_gfx_x_end = x1;
    } else {
        /* Shallow, already in order. */
        g_gfx_y_start = y1;  g_gfx_y_end = y2;
        g_gfx_x_start = x1;  g_gfx_x_end = x2;
    }

    if (g_gfx_x_end < g_gfx_x_start) g_gfx_x_step = -1;
    if (g_gfx_y_end < g_gfx_y_start) g_gfx_y_step = -1;
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403949 — Bresenham accumulator update
 *
 * Called once per pixel from inside the line-drawer loop, AFTER the
 * pixel paint and BEFORE the per-iteration decrement of the dominant
 * axis's abs delta.  Reads the current (mutated) abs_dx / abs_dy.
 *
 * mode == 0 (shallow):
 *   if err < 0:   err += 2 * abs_dy
 *   else (>= 0):  err += 2 * (abs_dy - abs_dx)
 *
 * mode == 1 (steep):
 *   if err <  0:  err += 2 * abs_dx
 *   else (>= 0):  err += 2 * (abs_dx - abs_dy)
 *
 * The decision in the OUTER loop ("did err cross zero this step?")
 * uses the post-update value to decide whether to step the
 * non-dominant axis.
 * ────────────────────────────────────────────────────────────────── */
static void bres_err_update(int steep_mode)
{
    if (steep_mode == 0) {
        int add = g_gfx_abs_dy;
        if (g_gfx_bres_err >= 0) {
            add = g_gfx_abs_dy - g_gfx_abs_dx;
        }
        g_gfx_bres_err += add * 2;
    } else if (g_gfx_bres_err < 0) {
        g_gfx_bres_err += g_gfx_abs_dx * 2;
    } else {
        g_gfx_bres_err += (g_gfx_abs_dx - g_gfx_abs_dy) * 2;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403a8f — solid Bresenham line
 *
 * Dispatches on endpoint shape:
 *   - Pure vertical (x1==x2) → step y from y_start to y_end.
 *   - Pure horizontal (y1==y2) → step x from x_start to x_end.
 *   - Steep (|dy| > |dx|) → iterate y; step x conditionally.
 *   - Shallow (|dx| >= |dy|) → iterate x; step y conditionally.
 *
 * Note the loop body for the diagonal cases:
 *   1. Paint current (x,y).
 *   2. Update bres_err using CURRENT abs_dx, abs_dy.
 *   3. Decrement the dominant-axis abs counter.
 *   4. If bres_err > 0: decrement the OTHER counter too, step the
 *      non-dominant axis by its step sign.
 *
 * abs_dx and abs_dy are decremented across the loop — they're not
 * the "remaining run/rise" you'd expect from textbook Bresenham; the
 * err update consults their current value each iteration.  See
 * engine-quirks.md #38.
 * ────────────────────────────────────────────────────────────────── */
void gfx_line_draw(int x1, int y1, int x2, int y2, uint8_t color)
{
    bres_setup(x1, y1, x2, y2);
    int local_y = g_gfx_y_start;
    int local_x = g_gfx_x_start;

    if (x2 == x1) {
        /* Pure vertical. */
        for (; local_y <= g_gfx_y_end; local_y++) {
            gfx_paint_pixel_clipped(x1, local_y, color);
        }
    } else if (y2 == y1) {
        /* Pure horizontal. */
        for (; local_x <= g_gfx_x_end; local_x++) {
            gfx_paint_pixel_clipped(local_x, y2, color);
        }
    } else if (g_gfx_abs_dx < g_gfx_abs_dy) {
        /* Steep — iterate over y. */
        g_gfx_bres_err = g_gfx_abs_dx * 2 - g_gfx_abs_dy;
        for (; local_y <= g_gfx_y_end; local_y++) {
            gfx_paint_pixel_clipped(local_x, local_y, color);
            bres_err_update(1);
            g_gfx_abs_dy -= 1;
            if (g_gfx_bres_err > 0) {
                g_gfx_abs_dx -= 1;
                local_x += g_gfx_x_step;
            }
        }
    } else {
        /* Shallow — iterate over x. */
        g_gfx_bres_err = g_gfx_abs_dy * 2 - g_gfx_abs_dx;
        for (; local_x <= g_gfx_x_end; local_x++) {
            gfx_paint_pixel_clipped(local_x, local_y, color);
            bres_err_update(0);
            g_gfx_abs_dx -= 1;
            if (g_gfx_bres_err > 0) {
                g_gfx_abs_dy -= 1;
                local_y += g_gfx_y_step;
            }
        }
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403c49 — dotted axis-only line
 *
 * Step is +2 on the dominant axis (so every other pixel is painted).
 * Only the pure-vertical and pure-horizontal cases are handled —
 * diagonal endpoints fall through both branches and paint NOTHING.
 * Retail never calls this with a diagonal pair; the code is shaped
 * for the axis-aligned path only.  See engine-quirks.md #40.
 *
 * The Bresenham setup is still called (writing the BSS slots), but
 * for axis-aligned lines its diagonal-related state is unused.
 * ────────────────────────────────────────────────────────────────── */
void gfx_line_draw_dotted(int x1, int y1, int x2, int y2, uint8_t color)
{
    bres_setup(x1, y1, x2, y2);
    int local_y = g_gfx_y_start;
    int local_x = g_gfx_x_start;

    if (x2 == x1) {
        for (; local_y <= g_gfx_y_end; local_y += 2) {
            gfx_paint_pixel_clipped(x1, local_y, color);
        }
    } else if (y2 == y1) {
        for (; local_x <= g_gfx_x_end; local_x += 2) {
            gfx_paint_pixel_clipped(local_x, y2, color);
        }
    }
    /* Diagonal endpoints: silent no-op. */
}

/* ──────────────────────────────────────────────────────────────────
 * Bounds-clamp helpers for the rect wrappers.
 *
 * Two flavors mirror the two retail patterns:
 *   - `clamp_solid` is the sequential `if`/`if` from FUN_00403cf4.
 *   - `clamp_bevel` is the `if`/`else if` from the 3 bevel rects.
 *
 * The difference: `clamp_bevel` skips the right-clamp when the left
 * edge was clamped, leaving the right edge un-clipped (the per-pixel
 * painter drops the off-canvas writes anyway, so the visible result
 * is the same — just wasted loop iterations).  See engine-quirks.md
 * #39.
 *
 * Both clamps follow the retail `< 1` test (catches negatives AND
 * leaves zero unchanged — equivalent to `< 0` with the `= 0`
 * assignment we use here, modulo the wasted compare).
 * ────────────────────────────────────────────────────────────────── */
static void clamp_solid(int *x, int *w, int *y, int *h)
{
    if (*x < 1) *x = 0;
    if (gfx_paint_canvas_width() <= *w + *x) {
        *w = gfx_paint_canvas_width() - *x;
    }
    if (*y < 1) *y = 0;
    if (gfx_paint_canvas_height() <= *h + *y) {
        *h = gfx_paint_canvas_height() - *y;
    }
}

static void clamp_bevel(int *x, int *w, int *y, int *h)
{
    if (*x < 1) {
        *x = 0;
    } else if (gfx_paint_canvas_width() <= *w + *x) {
        *w = gfx_paint_canvas_width() - *x;
    }
    if (*y < 1) {
        *y = 0;
    } else if (gfx_paint_canvas_height() <= *h + *y) {
        *h = gfx_paint_canvas_height() - *y;
    }
}

/* Stitch a rectangle from 4 lines.  `c_tr` colors the top + right
 * edges; `c_bl` colors the bottom + left edges.  When `c_tr == c_bl`
 * this draws a solid frame in a single color (FUN_00403cf4); when
 * they differ it's a diagonal bevel (the 3 bevel variants). */
static void draw_rect_4lines(int x, int y, int w, int h,
                             uint8_t c_tr, uint8_t c_bl)
{
    int right  = w - 1 + x;
    int bottom = h - 1 + y;
    gfx_line_draw(x,     y,      right, y,      c_tr);  /* top    */
    gfx_line_draw(right, y,      right, bottom, c_tr);  /* right  */
    gfx_line_draw(x,     bottom, right, bottom, c_bl);  /* bottom */
    gfx_line_draw(x,     y,      x,     bottom, c_bl);  /* left   */
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403cf4 — solid rectangle outline
 *
 * Retail order:
 *   FUN_00403a8f(x,        y,        right,    y,        color);
 *   FUN_00403a8f(x,        bottom,   right,    bottom,   color);
 *   FUN_00403a8f(x,        y,        x,        bottom,   color);
 *   FUN_00403a8f(right,    y,        right,    bottom,   color);
 *
 * The order matters beyond the pixels: the LAST line leaves its
 * endpoints in the Bresenham registers (DAT_005aeb48 / DAT_005aeb5c …),
 * which are retail state — drawing top→right→bottom→left (the bevel
 * order) left the bottom edge's x_end / x_start there instead of the
 * right edge's (rhost shadow, render-under-rhost.md).
 * ────────────────────────────────────────────────────────────────── */
void gfx_line_rect(int x, int y, int w, int h, uint8_t color)
{
    clamp_solid(&x, &w, &y, &h);
    int right  = w - 1 + x;
    int bottom = h - 1 + y;
    gfx_line_draw(x,     y,      right, y,      color);  /* top    */
    gfx_line_draw(x,     bottom, right, bottom, color);  /* bottom */
    gfx_line_draw(x,     y,      x,     bottom, color);  /* left   */
    gfx_line_draw(right, y,      right, bottom, color);  /* right  */
}

/* Port of FUN_0040437d @ 0x40437d (197 B).  Solid FILLED axis-aligned
 * rectangle — `h` stacked horizontal lines, one per row, each drawn
 * via gfx_line_draw (FUN_00403a8f) from (x,row) to (x+w-1,row).
 *
 * Retail guards the whole body on `w > 0 && h > 0`, then applies the
 * same solid-rect clamp the outline rect uses (negative x/y → 0;
 * right/bottom extents clamped to the canvas).  Used for the text-
 * input caret (FUN_0040acce). */
void gfx_line_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    if (w > 0 && h > 0) {
        clamp_solid(&x, &w, &y, &h);
        for (int row = y; row < h + y; row++)
            gfx_line_draw(x, row, w - 1 + x, row, color);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403deb — 3D bevel rect (dark BR, bright TR/TL)
 *
 * Retail edge order + colors:
 *   top    @ 0x10
 *   right  @ 0x10
 *   bottom @ 0x1f
 *   left   @ 0x1f
 *
 * top+right share 0x10 → "bright"; bottom+left share 0x1f → "dark".
 * Bounds-clamp uses the bevel `if/else if` pattern (quirk #39).
 * ────────────────────────────────────────────────────────────────── */
void gfx_line_rect_bevel_dark_br(int x, int y, int w, int h)
{
    clamp_bevel(&x, &w, &y, &h);
    draw_rect_4lines(x, y, w, h, 0x10, 0x1f);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403ee4 — 3D bevel rect, palette B
 *
 * Retail edge order + colors:
 *   top    @ 0x35
 *   right  @ 0x35
 *   bottom @ 0x28
 *   left   @ 0x28
 *
 * Same shape as bevel_dark_br with a different bevel color pair.
 * Same `if/else if` clamp (quirk #39).
 * ────────────────────────────────────────────────────────────────── */
void gfx_line_rect_bevel_palette_b(int x, int y, int w, int h)
{
    clamp_bevel(&x, &w, &y, &h);
    draw_rect_4lines(x, y, w, h, 0x35, 0x28);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403fdd — 3D bevel rect (dark TR/TL, bright BR)
 *
 * Retail edge order + colors:
 *   top    @ 0x1f
 *   right  @ 0x1f
 *   bottom @ 0x10
 *   left   @ 0x10
 *
 * Inverse of bevel_dark_br — same TR/BL diagonal split, swapped
 * colors.  Same `if/else if` clamp (quirk #39).
 * ────────────────────────────────────────────────────────────────── */
void gfx_line_rect_bevel_dark_tl(int x, int y, int w, int h)
{
    clamp_bevel(&x, &w, &y, &h);
    draw_rect_4lines(x, y, w, h, 0x1f, 0x10);
}
