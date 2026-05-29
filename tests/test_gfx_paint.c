/*
 * Unit tests for gfx_paint — the bottom-of-stack pixel painters.
 *
 * Tests bind a host-allocated uint8_t canvas (padded with a known
 * sentinel byte on all sides) and exercise each painter against
 * golden coordinates.  The padding makes off-by-one writes
 * (especially the 2x1 clip quirk) visible without sanitizer noise.
 */

#include "t.h"
#include "gfx_paint.h"

/* ──────────────────────────────────────────────────────────────────
 * Fixtures
 * ────────────────────────────────────────────────────────────────── */

/* A canvas + a 1-row guard above, 1-row below, and 1-byte poison
 * either side of every row.  We bind the painter to the inner
 * `width * height` region and let bytes outside it stay at the
 * 0xee sentinel — any escape is then provable.
 *
 * Layout (for width=8, height=4):
 *   row -1 (8 sentinels)
 *   row  0 ......
 *   row  1 ......
 *   row  2 ......
 *   row  3 ......
 *   row  4 (8 sentinels)
 *
 * The inner canvas (rows 0..height-1) is contiguous so width is its
 * own pitch — exactly the retail assumption. */
enum { CANVAS_W = 16, CANVAS_H = 8 };

typedef struct {
    uint8_t guard_top   [CANVAS_W];
    uint8_t pixels      [CANVAS_W * CANVAS_H];
    uint8_t guard_bot   [CANVAS_W];
} canvas_t;

static void canvas_init(canvas_t *c)
{
    for (int i = 0; i < (int)sizeof c->guard_top; i++) c->guard_top[i] = 0xee;
    for (int i = 0; i < (int)sizeof c->pixels;    i++) c->pixels[i]    = 0;
    for (int i = 0; i < (int)sizeof c->guard_bot; i++) c->guard_bot[i] = 0xee;
    gfx_paint_bind_canvas(c->pixels, CANVAS_W, CANVAS_H);
}

static int canvas_guards_intact(const canvas_t *c)
{
    for (int i = 0; i < CANVAS_W; i++) {
        if (c->guard_top[i] != 0xee) return 0;
        if (c->guard_bot[i] != 0xee) return 0;
    }
    return 1;
}

static uint8_t at(const canvas_t *c, int x, int y)
{
    return c->pixels[x + y * CANVAS_W];
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_paint_pixel — FUN_004b3f54
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_paint_pixel_basic(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel(3, 2, 0x42);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x42);
    /* nothing else painted */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0);
    T_ASSERT_EQ_U(at(&c, 3, 3), 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_pixel_origin(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel(0, 0, 0x11);
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x11);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_pixel_corner(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel(CANVAS_W - 1, CANVAS_H - 1, 0xff);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, CANVAS_H - 1), 0xff);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_pixel_null_canvas_no_crash(void)
{
    /* Defensive: unbind, then call.  Should silently no-op. */
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_paint_pixel(3, 2, 0x42);     /* must not segfault */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_paint_2x1 — FUN_004b3f7a
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_paint_2x1_writes_both(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1(4, 3, 0x55);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x55);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x55);
    T_ASSERT_EQ_U(at(&c, 6, 3), 0);   /* untouched */
    T_ASSERT_EQ_U(at(&c, 3, 3), 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_2x1_overwrites_existing(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel(4, 3, 0x99);
    gfx_paint_pixel(5, 3, 0x77);
    gfx_paint_2x1(4, 3, 0x33);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x33);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x33);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_paint_2x1_under — FUN_004b3fa3
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_paint_2x1_under_writes_when_zero(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1_under(4, 3, 0x77);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x77);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x77);
    return 0;
}

int test_gfx_paint_2x1_under_skips_when_nonzero(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel(4, 3, 0x11);
    gfx_paint_pixel(5, 3, 0x22);
    gfx_paint_2x1_under(4, 3, 0x77);
    /* both already non-zero → both preserved */
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x11);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x22);
    return 0;
}

int test_gfx_paint_2x1_under_independent_pixel_test(void)
{
    /* Left pixel non-zero, right pixel zero — only the right pixel
     * should be written.  Each of the two checks is independent in
     * retail. */
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel(4, 3, 0x11);
    /* (5,3) is still 0 */
    gfx_paint_2x1_under(4, 3, 0x77);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x11);     /* preserved */
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x77);     /* filled in */
    return 0;
}

int test_gfx_paint_2x1_under_right_only_set(void)
{
    /* Mirror image: left=zero, right=nonzero. */
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel(5, 3, 0x22);
    gfx_paint_2x1_under(4, 3, 0x77);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x77);     /* filled in */
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x22);     /* preserved */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_paint_pixel_clipped — FUN_004039c7
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_paint_pixel_clipped_in_bounds(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel_clipped(5, 4, 0x42);
    T_ASSERT_EQ_U(at(&c, 5, 4), 0x42);
    return 0;
}

int test_gfx_paint_pixel_clipped_negative_x(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel_clipped(-1, 4, 0x42);
    /* whole canvas should still be zero */
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) T_ASSERT_EQ_U(c.pixels[i], 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_pixel_clipped_negative_y(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel_clipped(3, -1, 0x42);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) T_ASSERT_EQ_U(c.pixels[i], 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_pixel_clipped_x_at_width(void)
{
    /* x == width is OUT — retail bound is x < width. */
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel_clipped(CANVAS_W, 4, 0x42);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) T_ASSERT_EQ_U(c.pixels[i], 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_pixel_clipped_y_at_height(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel_clipped(3, CANVAS_H, 0x42);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) T_ASSERT_EQ_U(c.pixels[i], 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_pixel_clipped_x_at_width_minus_one(void)
{
    /* x == width - 1 is IN. */
    canvas_t c; canvas_init(&c);
    gfx_paint_pixel_clipped(CANVAS_W - 1, 3, 0xab);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, 3), 0xab);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_paint_2x1_clipped — FUN_00403a2c
 *
 * Faithful port: the bounds check is on (x, y) only.  The right
 * pixel at (x+1, y) is NOT separately tested — when x == width-1
 * the right pixel writes one column past the row.  In a contiguous
 * pitch=width canvas that lands on the first pixel of the NEXT row.
 * Tests pin this behavior down.
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_paint_2x1_clipped_in_bounds(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1_clipped(4, 3, 0x55);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x55);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x55);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_2x1_clipped_negative_x_drops(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1_clipped(-1, 3, 0x55);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) T_ASSERT_EQ_U(c.pixels[i], 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_2x1_clipped_x_at_width_drops(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1_clipped(CANVAS_W, 3, 0x55);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) T_ASSERT_EQ_U(c.pixels[i], 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_2x1_clipped_right_edge_wraps_to_next_row(void)
{
    /* x == width - 1 passes the clip check, the right pixel lands
     * at (width, y) which == (0, y+1) in a contiguous canvas.
     * Engine-quirks.md #26 — faithful port. */
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1_clipped(CANVAS_W - 1, 3, 0x55);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, 3), 0x55);   /* left pixel */
    T_ASSERT_EQ_U(at(&c, 0, 4), 0x55);              /* "right" pixel wraps */
    /* All other pixels untouched */
    T_ASSERT_EQ_U(at(&c, 0, 3), 0);
    T_ASSERT_EQ_U(at(&c, 1, 4), 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_2x1_clipped_bottom_right_corner_wraps_into_guard(void)
{
    /* x = width-1, y = height-1: left pixel is last pixel of
     * canvas; right pixel wraps into the guard row.  The guard
     * MUST receive the write (proving we faithfully reproduced the
     * unchecked second-pixel quirk). */
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1_clipped(CANVAS_W - 1, CANVAS_H - 1, 0x55);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, CANVAS_H - 1), 0x55);
    /* First byte of guard_bot is at pixels[CANVAS_W * CANVAS_H] —
     * i.e. one past the last pixel of the last row.  It started as
     * 0xee; the quirk should have stomped it to 0x55. */
    T_ASSERT_EQ_U(c.guard_bot[0], 0x55);
    /* Rest of guard_bot intact */
    for (int i = 1; i < CANVAS_W; i++) T_ASSERT_EQ_U(c.guard_bot[i], 0xee);
    /* Top guard intact */
    for (int i = 0; i < CANVAS_W; i++) T_ASSERT_EQ_U(c.guard_top[i], 0xee);
    return 0;
}

int test_gfx_paint_2x1_clipped_y_at_height_drops(void)
{
    canvas_t c; canvas_init(&c);
    gfx_paint_2x1_clipped(3, CANVAS_H, 0x55);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) T_ASSERT_EQ_U(c.pixels[i], 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * darken rect (FUN_004b13ed)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_paint_darken_rect_remaps_via_palette(void)
{
    canvas_t c; canvas_init(&c);
    /* Synthetic palette: entry 1 black (avg 0 → remap 0x3f), entry 2
     * white (avg 255 → 255>>3=31 → remap 0x3f-0x1f=0x20), entry 3 mid
     * 0x80 (0x80>>3=0x10 → remap 0x3f-0x10=0x2f). */
    uint8_t pal[256 * 3];
    for (int i = 0; i < 256 * 3; i++) pal[i] = 0;
    pal[2*3+0] = pal[2*3+1] = pal[2*3+2] = 0xff;
    pal[3*3+0] = pal[3*3+1] = pal[3*3+2] = 0x80;

    /* Seed a 2×2 block at (1,1) with source indices 1,2,3,1. */
    c.pixels[1 + 1 * CANVAS_W] = 1;
    c.pixels[2 + 1 * CANVAS_W] = 2;
    c.pixels[1 + 2 * CANVAS_W] = 3;
    c.pixels[2 + 2 * CANVAS_W] = 1;

    gfx_paint_darken_rect(pal, 1, 1, 2, 2);

    T_ASSERT_EQ_U(c.pixels[1 + 1 * CANVAS_W], 0x3f);   /* 1 → black  */
    T_ASSERT_EQ_U(c.pixels[2 + 1 * CANVAS_W], 0x20);   /* 2 → white  */
    T_ASSERT_EQ_U(c.pixels[1 + 2 * CANVAS_W], 0x2f);   /* 3 → gray   */
    T_ASSERT_EQ_U(c.pixels[2 + 2 * CANVAS_W], 0x3f);   /* 1 → black  */
    T_ASSERT_EQ_U(c.pixels[0], 0);                     /* outside rect */
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_paint_darken_rect_null_palette_no_crash(void)
{
    canvas_t c; canvas_init(&c);
    c.pixels[0] = 5;
    gfx_paint_darken_rect(NULL, 0, 0, 2, 2);
    T_ASSERT_EQ_U(c.pixels[0], 5);   /* unchanged */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * bind/unbind
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_paint_bind_then_rebind(void)
{
    canvas_t c1; canvas_init(&c1);
    gfx_paint_pixel(0, 0, 0xaa);
    T_ASSERT_EQ_U(at(&c1, 0, 0), 0xaa);

    canvas_t c2; canvas_init(&c2);   /* rebind to c2 */
    gfx_paint_pixel(0, 0, 0xbb);
    T_ASSERT_EQ_U(at(&c2, 0, 0), 0xbb);
    /* c1 unchanged by the c2 write */
    T_ASSERT_EQ_U(at(&c1, 0, 0), 0xaa);
    return 0;
}
