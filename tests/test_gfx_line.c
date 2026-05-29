/*
 * Unit tests for gfx_line — the Bresenham + axis-rect primitives.
 *
 * Tests bind a host-allocated uint8_t canvas (with sentinel-padded
 * guards) and exercise each entry point against golden pixel patterns.
 *
 * The Bresenham variant in this port is the engine's own (see
 * gfx_line.c + quirk #38) — pixel distribution is non-uniform along
 * shallow/steep diagonals.  Goldens here were generated from the port
 * itself; future cross-checks against Frida-captured retail draws can
 * confirm bit-for-bit equivalence.
 */

#include "t.h"
#include "gfx_line.h"
#include "gfx_paint.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Fixture
 * ────────────────────────────────────────────────────────────────── */

enum { CW = 16, CH = 16 };

typedef struct {
    uint8_t guard_top   [CW];
    uint8_t pixels      [CW * CH];
    uint8_t guard_bot   [CW];
} canvas_t;

static void canvas_init(canvas_t *c)
{
    memset(c->guard_top, 0xee, sizeof c->guard_top);
    memset(c->pixels,    0x00, sizeof c->pixels);
    memset(c->guard_bot, 0xee, sizeof c->guard_bot);
    gfx_paint_bind_canvas(c->pixels, CW, CH);
}

static int canvas_guards_intact(const canvas_t *c)
{
    for (int i = 0; i < CW; i++) {
        if (c->guard_top[i] != 0xee) return 0;
        if (c->guard_bot[i] != 0xee) return 0;
    }
    return 1;
}

static uint8_t at(const canvas_t *c, int x, int y)
{
    return c->pixels[x + y * CW];
}

static int count_color(const canvas_t *c, uint8_t color)
{
    int n = 0;
    for (int i = 0; i < CW * CH; i++) if (c->pixels[i] == color) n++;
    return n;
}

/* Compare against a string-based golden pattern.  Each row is one
 * line of `CW` chars; '#' = colored pixel, '.' = zero.  Single color
 * only (suitable for solid lines and single-color rects). */
static int canvas_matches(const canvas_t *c, uint8_t color,
                          const char *const rows[CH])
{
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < CW; x++) {
            uint8_t got    = at(c, x, y);
            uint8_t expect = (rows[y][x] == '#') ? color : 0;
            if (got != expect) return 0;
        }
    }
    return 1;
}

/* ──────────────────────────────────────────────────────────────────
 * Pure-horizontal lines (FUN_00403a8f, y1==y2 branch)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_horizontal_paints_inclusive_range(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw(2, 2, 12, 2, 7);

    /* 11 pixels: x=2..12 at y=2. */
    T_ASSERT_EQ_I(count_color(&c, 7), 11);
    for (int x = 2; x <= 12; x++) T_ASSERT_EQ_U(at(&c, x, 2), 7);
    T_ASSERT_EQ_U(at(&c, 1, 2), 0);
    T_ASSERT_EQ_U(at(&c, 13, 2), 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_line_horizontal_endpoint_order_invariant(void)
{
    canvas_t c1; canvas_init(&c1);
    gfx_line_draw(2, 5, 10, 5, 9);

    canvas_t c2; canvas_init(&c2);
    gfx_line_draw(10, 5, 2, 5, 9);

    T_ASSERT_MEM_EQ(c1.pixels, c2.pixels, sizeof c1.pixels);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Pure-vertical lines (x1==x2 branch)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_vertical_paints_inclusive_range(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw(3, 1, 3, 10, 5);

    /* 10 pixels: y=1..10 at x=3. */
    T_ASSERT_EQ_I(count_color(&c, 5), 10);
    for (int y = 1; y <= 10; y++) T_ASSERT_EQ_U(at(&c, 3, y), 5);
    T_ASSERT_EQ_U(at(&c, 3, 0), 0);
    T_ASSERT_EQ_U(at(&c, 3, 11), 0);
    return 0;
}

int test_gfx_line_vertical_endpoint_order_invariant(void)
{
    canvas_t c1; canvas_init(&c1);
    gfx_line_draw(7, 1, 7, 9, 3);

    canvas_t c2; canvas_init(&c2);
    gfx_line_draw(7, 9, 7, 1, 3);

    T_ASSERT_MEM_EQ(c1.pixels, c2.pixels, sizeof c1.pixels);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Diagonal — pure 45° (|dx| == |dy|, falls into shallow branch)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_diagonal_45deg(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw(0, 0, 10, 10, 2);

    /* 11 pixels on the main diagonal. */
    T_ASSERT_EQ_I(count_color(&c, 2), 11);
    for (int i = 0; i <= 10; i++) T_ASSERT_EQ_U(at(&c, i, i), 2);
    /* Off-diagonal pixels stay zero. */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0);
    return 0;
}

int test_gfx_line_diagonal_antidiagonal_endpoint_order_invariant(void)
{
    /* (0,10) → (10,0): both lines fall into shallow (dx==dy) but with
     * a -1 y_step.  Check that reversing endpoints gives identical
     * output (it should — the setup re-sorts by x for shallow). */
    canvas_t c1; canvas_init(&c1);
    gfx_line_draw(0, 10, 10, 0, 4);
    canvas_t c2; canvas_init(&c2);
    gfx_line_draw(10, 0, 0, 10, 4);
    T_ASSERT_MEM_EQ(c1.pixels, c2.pixels, sizeof c1.pixels);
    /* And the diagonal actually hits the corners. */
    T_ASSERT_EQ_U(at(&c1, 0, 10), 4);
    T_ASSERT_EQ_U(at(&c1, 10, 0), 4);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Shallow line (|dx| > |dy|) — golden traced from the port
 *
 * The engine's Bresenham has a non-uniform pixel distribution along
 * shallow lines: y stays constant for several x values, then jumps.
 * The golden below was generated from this implementation; it locks
 * the exact distribution against regressions.
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_shallow_golden(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw(0, 0, 10, 5, 1);

    const char *const rows[CH] = {
        "####............",  /* y=0: x=0..3 */
        "....###.........",  /* y=1: x=4..6 */
        ".......#........",  /* y=2: x=7    */
        "........#.......",  /* y=3: x=8    */
        ".........#......",  /* y=4: x=9    */
        "..........#.....",  /* y=5: x=10   */
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    };
    T_ASSERT(canvas_matches(&c, 1, rows));
    /* Endpoints hit. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 1);
    T_ASSERT_EQ_U(at(&c, 10, 5), 1);
    /* Total = 11 pixels (x_end - x_start + 1). */
    T_ASSERT_EQ_I(count_color(&c, 1), 11);
    return 0;
}

int test_gfx_line_shallow_endpoint_order_invariant(void)
{
    canvas_t c1; canvas_init(&c1);
    gfx_line_draw(0, 0, 10, 5, 3);
    canvas_t c2; canvas_init(&c2);
    gfx_line_draw(10, 5, 0, 0, 3);
    T_ASSERT_MEM_EQ(c1.pixels, c2.pixels, sizeof c1.pixels);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Steep line (|dy| > |dx|) — golden
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_steep_golden(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw(0, 0, 5, 10, 1);

    const char *const rows[CH] = {
        "#...............",  /* y=0: x=0 */
        "#...............",  /* y=1: x=0 */
        "#...............",  /* y=2: x=0 */
        "#...............",  /* y=3: x=0 */
        ".#..............",  /* y=4: x=1 */
        ".#..............",  /* y=5: x=1 */
        ".#..............",  /* y=6: x=1 */
        "..#.............",  /* y=7: x=2 */
        "...#............",  /* y=8: x=3 */
        "....#...........",  /* y=9: x=4 */
        ".....#..........",  /* y=10: x=5 */
        "................",
        "................",
        "................",
        "................",
        "................",
    };
    T_ASSERT(canvas_matches(&c, 1, rows));
    T_ASSERT_EQ_U(at(&c, 0, 0), 1);
    T_ASSERT_EQ_U(at(&c, 5, 10), 1);
    T_ASSERT_EQ_I(count_color(&c, 1), 11);
    return 0;
}

int test_gfx_line_steep_endpoint_order_invariant(void)
{
    canvas_t c1; canvas_init(&c1);
    gfx_line_draw(0, 0, 5, 10, 6);
    canvas_t c2; canvas_init(&c2);
    gfx_line_draw(5, 10, 0, 0, 6);
    T_ASSERT_MEM_EQ(c1.pixels, c2.pixels, sizeof c1.pixels);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Degenerate line (single pixel)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_degenerate_paints_one_pixel(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw(5, 5, 5, 5, 9);
    T_ASSERT_EQ_I(count_color(&c, 9), 1);
    T_ASSERT_EQ_U(at(&c, 5, 5), 9);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Clipping — pixels outside the canvas drop silently
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_clips_against_canvas(void)
{
    canvas_t c; canvas_init(&c);
    /* Line that runs partly off the right edge. */
    gfx_line_draw(10, 5, 30, 5, 7);
    /* x=10..15 visible (canvas width=16); x=16..30 dropped. */
    T_ASSERT_EQ_I(count_color(&c, 7), 6);
    for (int x = 10; x <= 15; x++) T_ASSERT_EQ_U(at(&c, x, 5), 7);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_line_clips_negative_coords(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw(-5, 5, 10, 5, 7);
    /* Only x=0..10 visible. */
    T_ASSERT_EQ_I(count_color(&c, 7), 11);
    for (int x = 0; x <= 10; x++) T_ASSERT_EQ_U(at(&c, x, 5), 7);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Dotted line — gfx_line_draw_dotted (FUN_00403c49)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_dotted_horizontal(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw_dotted(2, 3, 12, 3, 4);
    /* x=2,4,6,8,10,12 painted (step +2 from x_start=2 inclusive). */
    int expected_x[] = { 2, 4, 6, 8, 10, 12 };
    for (size_t i = 0; i < sizeof expected_x / sizeof expected_x[0]; i++) {
        T_ASSERT_EQ_U(at(&c, expected_x[i], 3), 4);
    }
    T_ASSERT_EQ_I(count_color(&c, 4), 6);
    /* Gaps stay zero. */
    T_ASSERT_EQ_U(at(&c, 3, 3), 0);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0);
    return 0;
}

int test_gfx_line_dotted_vertical(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_draw_dotted(5, 1, 5, 9, 4);
    /* y=1,3,5,7,9 painted. */
    int expected_y[] = { 1, 3, 5, 7, 9 };
    for (size_t i = 0; i < sizeof expected_y / sizeof expected_y[0]; i++) {
        T_ASSERT_EQ_U(at(&c, 5, expected_y[i]), 4);
    }
    T_ASSERT_EQ_I(count_color(&c, 4), 5);
    return 0;
}

int test_gfx_line_dotted_diagonal_is_noop(void)
{
    canvas_t c; canvas_init(&c);
    /* Diagonal endpoints fall through both branches in retail —
     * silent no-op. */
    gfx_line_draw_dotted(0, 0, 10, 5, 4);
    T_ASSERT_EQ_I(count_color(&c, 4), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Solid rect outline — gfx_line_rect (FUN_00403cf4)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_rect_solid_paints_four_edges(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_rect(2, 2, 6, 5, 8);
    /* width=6 height=5 → right=7, bottom=6.  Perimeter = 2*(W+H) - 4
     * shared corners = 2*6 + 2*5 - 4 = 18 unique pixels. */
    int painted = count_color(&c, 8);
    T_ASSERT_EQ_I(painted, 18);
    /* Corner pixels. */
    T_ASSERT_EQ_U(at(&c, 2, 2), 8);
    T_ASSERT_EQ_U(at(&c, 7, 2), 8);
    T_ASSERT_EQ_U(at(&c, 2, 6), 8);
    T_ASSERT_EQ_U(at(&c, 7, 6), 8);
    /* Interior is empty. */
    for (int y = 3; y <= 5; y++)
        for (int x = 3; x <= 6; x++)
            T_ASSERT_EQ_U(at(&c, x, y), 0);
    return 0;
}

int test_gfx_line_rect_solid_clamps_oversize(void)
{
    canvas_t c; canvas_init(&c);
    /* Width 30 from x=2 → would extend past canvas_width=16.  Solid
     * clamp shrinks width to 14 → right edge at x=15 (last column).
     * Bottom clamp similarly: height 30 from y=2 → height=14, bottom
     * edge at y=15. */
    gfx_line_rect(2, 2, 30, 30, 6);
    T_ASSERT_EQ_U(at(&c, 2, 2), 6);
    T_ASSERT_EQ_U(at(&c, 15, 2), 6);
    T_ASSERT_EQ_U(at(&c, 2, 15), 6);
    T_ASSERT_EQ_U(at(&c, 15, 15), 6);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * 3D bevel rects — FUN_00403deb / FUN_00403ee4 / FUN_00403fdd
 *
 * Each draws 4 lines in this order: top (c_tr), right (c_tr), bottom
 * (c_bl), left (c_bl).  Corner ownership after the 4 sequential
 * paints (a later edge overwrites an earlier one at shared corners):
 *
 *   top-left     ← left   (c_bl)   [overwritten by left after top]
 *   top-right    ← right  (c_tr)   [overwritten by right after top]
 *   bottom-right ← bottom (c_bl)   [overwritten by bottom after right]
 *   bottom-left  ← left   (c_bl)
 *
 * So when c_tr != c_bl, only the TOP-RIGHT corner shows c_tr; the
 * other 3 corners show c_bl.
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_line_rect_bevel_dark_br_corner_overwrite(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_rect_bevel_dark_br(2, 2, 6, 5);

    /* top + right at 0x10; bottom + left at 0x1f.
     * Corner ownership: TR=0x10; TL, BR, BL = 0x1f. */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x1f);   /* TL — left overwrites top */
    T_ASSERT_EQ_U(at(&c, 7, 2), 0x10);   /* TR — right overwrites top */
    T_ASSERT_EQ_U(at(&c, 7, 6), 0x1f);   /* BR — bottom overwrites right */
    T_ASSERT_EQ_U(at(&c, 2, 6), 0x1f);   /* BL — left = bottom = 0x1f */

    /* Top edge interior @ 0x10. */
    for (int x = 3; x <= 6; x++) T_ASSERT_EQ_U(at(&c, x, 2), 0x10);
    /* Right edge interior @ 0x10. */
    for (int y = 3; y <= 5; y++) T_ASSERT_EQ_U(at(&c, 7, y), 0x10);
    /* Bottom edge interior @ 0x1f. */
    for (int x = 3; x <= 6; x++) T_ASSERT_EQ_U(at(&c, x, 6), 0x1f);
    /* Left edge interior @ 0x1f. */
    for (int y = 3; y <= 5; y++) T_ASSERT_EQ_U(at(&c, 2, y), 0x1f);
    return 0;
}

int test_gfx_line_rect_bevel_palette_b_uses_correct_colors(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_rect_bevel_palette_b(2, 2, 6, 5);
    /* TR=0x35; other corners = 0x28. */
    T_ASSERT_EQ_U(at(&c, 7, 2), 0x35);
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x28);
    T_ASSERT_EQ_U(at(&c, 2, 6), 0x28);
    T_ASSERT_EQ_U(at(&c, 7, 6), 0x28);
    /* Top + right interior @ 0x35; bottom + left interior @ 0x28. */
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x35);
    T_ASSERT_EQ_U(at(&c, 7, 4), 0x35);
    T_ASSERT_EQ_U(at(&c, 4, 6), 0x28);
    T_ASSERT_EQ_U(at(&c, 2, 4), 0x28);
    return 0;
}

int test_gfx_line_rect_bevel_dark_tl_inverts_palette(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_rect_bevel_dark_tl(2, 2, 6, 5);
    /* TR=0x1f; other corners = 0x10. */
    T_ASSERT_EQ_U(at(&c, 7, 2), 0x1f);
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x10);
    T_ASSERT_EQ_U(at(&c, 2, 6), 0x10);
    T_ASSERT_EQ_U(at(&c, 7, 6), 0x10);
    return 0;
}

/* The bevel `if/else if` clamp (quirk #39): when x=0, the right-clamp
 * branch is SKIPPED (the first branch matched: `x < 1`).  The
 * resulting wide rect's right edge falls past the canvas, but the
 * per-pixel clipper drops the off-canvas writes, so the visible
 * effect is just an unclipped right edge. */
int test_gfx_line_rect_bevel_x_zero_skips_right_clamp_per_quirk_39(void)
{
    canvas_t c; canvas_init(&c);
    /* x=0 satisfies `x < 1` — right clamp skipped.  Width=30 means
     * right edge at x=29, painted via clipped pixel writer (off-canvas
     * drops). */
    gfx_line_rect_bevel_dark_br(0, 0, 30, 30);
    /* Top-left corner (drawn LAST as the left edge in the 4-line
     * sequence) at color 0x1f. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x1f);
    /* Right edge is OFF-canvas → no pixels at x=15 (the rightmost
     * column) from the right-edge line, only from the top edge.  Top
     * spans x=0..29 visible portion x=0..15, all at 0x10 (then BL
     * corner overwrites x=0,y=15 to 0x1f). */
    T_ASSERT_EQ_U(at(&c, 15, 0), 0x10);
    /* No guard byte was clobbered — the off-canvas clipper held. */
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* The solid rect's clamp (sequential `if`s) DOES re-clamp the right
 * edge even when x was clamped — but since x was 0 we don't actually
 * exercise the difference here.  This test instead nails down that
 * the SOLID rect's clamp does NOT have the bevel's skip: with x=0 +
 * oversize width, the solid clamp shrinks width AND draws a real
 * right edge inside the canvas. */
int test_gfx_line_rect_solid_x_zero_still_clamps_right(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_rect(0, 0, 30, 30, 8);
    /* width shrunk to 16 → right edge at x=15. */
    T_ASSERT_EQ_U(at(&c, 15, 0), 8);
    T_ASSERT_EQ_U(at(&c, 15, 15), 8);
    /* Whole right column has the edge. */
    for (int y = 0; y <= 15; y++) T_ASSERT_EQ_U(at(&c, 15, y), 8);
    return 0;
}

/* ── gfx_line_fill_rect (FUN_0040437d) ──────────────────────────── */

int test_gfx_line_fill_rect_fills_interior(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_fill_rect(2, 3, 4, 2, 9);
    /* 4×2 = 8 solid pixels filled (unlike the 4-edge outline). */
    T_ASSERT_EQ_I(count_color(&c, 9), 8);
    for (int y = 3; y <= 4; y++)
        for (int x = 2; x <= 5; x++)
            T_ASSERT_EQ_U(at(&c, x, y), 9);
    /* Border neighbours untouched. */
    T_ASSERT_EQ_U(at(&c, 1, 3), 0);
    T_ASSERT_EQ_U(at(&c, 6, 3), 0);
    T_ASSERT_EQ_U(at(&c, 2, 2), 0);
    T_ASSERT_EQ_U(at(&c, 2, 5), 0);
    return 0;
}

int test_gfx_line_fill_rect_zero_or_negative_size_is_noop(void)
{
    canvas_t c; canvas_init(&c);
    gfx_line_fill_rect(2, 3, 0, 4, 9);    /* w == 0 */
    gfx_line_fill_rect(2, 3, 4, 0, 9);    /* h == 0 */
    gfx_line_fill_rect(2, 3, -5, 4, 9);   /* w < 0 */
    T_ASSERT_EQ_I(count_color(&c, 9), 0);
    return 0;
}

int test_gfx_line_fill_rect_clamps_oversize(void)
{
    canvas_t c; canvas_init(&c);
    /* Extends past the 16×16 canvas — clamps to fit. */
    gfx_line_fill_rect(14, 14, 8, 8, 7);
    /* x clamps to width 16-14 = 2 cols (14,15), y to 2 rows (14,15). */
    T_ASSERT_EQ_U(at(&c, 14, 14), 7);
    T_ASSERT_EQ_U(at(&c, 15, 15), 7);
    T_ASSERT_EQ_I(count_color(&c, 7), 4);
    return 0;
}
