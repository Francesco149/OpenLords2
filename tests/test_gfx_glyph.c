/*
 * Unit tests for gfx_glyph — the per-character glyph blitter.
 *
 * Tests bind a guarded canvas via gfx_paint (since gfx_glyph reads
 * the canvas through gfx_paint_canvas_*), stage a small synthetic
 * glyph blob, and exercise the three blitter entry points against
 * golden pixel patterns.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"

/* ──────────────────────────────────────────────────────────────────
 * Fixtures — canvas + glyph
 * ────────────────────────────────────────────────────────────────── */

enum { CANVAS_W = 16, CANVAS_H = 8 };

typedef struct {
    uint8_t guard_top[CANVAS_W];
    uint8_t pixels[CANVAS_W * CANVAS_H];
    uint8_t guard_bot[CANVAS_W];
} canvas_t;

static void canvas_init(canvas_t *c)
{
    for (int i = 0; i < (int)sizeof c->guard_top; i++) c->guard_top[i] = 0xee;
    for (int i = 0; i < (int)sizeof c->pixels;    i++) c->pixels[i]    = 0;
    for (int i = 0; i < (int)sizeof c->guard_bot; i++) c->guard_bot[i] = 0xee;
    gfx_paint_bind_canvas(c->pixels, CANVAS_W, CANVAS_H);
}

static uint8_t at(const canvas_t *c, int x, int y)
{
    return c->pixels[x + y * CANVAS_W];
}

static int canvas_guards_intact(const canvas_t *c)
{
    for (int i = 0; i < CANVAS_W; i++) {
        if (c->guard_top[i] != 0xee) return 0;
        if (c->guard_bot[i] != 0xee) return 0;
    }
    return 1;
}

/* A synthetic font blob.  Glyph at offset 8 is a 3×2 pattern:
 *
 *     1 0 7
 *     0 2 0
 *
 * Zero bytes are transparent in retail's palette convention.  The
 * leading 8 bytes are filler so glyph_offset == 8 exercises the
 * font_base + glyph_offset arithmetic.
 *
 * Padded to 32 bytes so the clipped-blit tests can refer to a wider
 * source row (glyph_w = 5) than the visible width (clipped_w = 3)
 * without reading past the array. */
static const uint8_t k_font_blob[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* filler */
    0x01, 0x00, 0x07, 0x05, 0x06,                    /* row 0 (extra cols 5,6) */
    0x00, 0x02, 0x00, 0x09, 0x0a,                    /* row 1 (extra cols 9,A) */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,        /* trailing filler */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/* For pure 3-wide tests (unclipped path), reuse k_font_blob but read
 * only 3 cols per row — but the unclipped retail path reads exactly
 * `glyph_w` cols with no stride pad.  Use a smaller blob where rows
 * are tight (3 bytes each) so col-advance hits the next row's data. */
static const uint8_t k_font_blob_tight[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* filler */
    0x01, 0x00, 0x07,                                /* row 0 (3 bytes) */
    0x00, 0x02, 0x00,                                /* row 1 (3 bytes) */
    0xff, 0xff,
};

/* ──────────────────────────────────────────────────────────────────
 * gfx_glyph_blit — FUN_004b41b7 (unclipped)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_glyph_blit_color_fill_skips_zeros(void)
{
    /* Default mode (color_modulation != 1): paint bound color for
     * each non-zero source byte; zeros leave dest at 0. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_color(0x42);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);   /* clipped_w=glyph_w; dst pad = W - 3 */
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit(k_font_blob_tight);

    /* row 0: src = 01 00 07 → dst at (2..4, 1) = 42 00 42 */
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x42);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0x42);
    /* row 1: src = 00 02 00 → dst at (2..4, 2) = 00 42 00 */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x42);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    /* Surrounding pixels untouched. */
    T_ASSERT_EQ_U(at(&c, 1, 1), 0);
    T_ASSERT_EQ_U(at(&c, 5, 1), 0);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0);
    T_ASSERT_EQ_U(at(&c, 2, 3), 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_glyph_blit_copy_byte_mode_preserves_per_pixel_color(void)
{
    /* color_modulation == 1: each non-zero source byte is copied
     * verbatim, NOT replaced by the bound color. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_color(0xff);                /* should be ignored */
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(1);

    gfx_glyph_blit(k_font_blob_tight);

    T_ASSERT_EQ_U(at(&c, 0, 0), 0x01);   /* source kept */
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x00);   /* transparent skipped */
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x07);
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x02);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_glyph_blit_writes_at_offset_glyph(void)
{
    /* Confirm dest cursor offset + dst_stride_pad land at the right
     * row.  Place glyph at (5, 3) on the 16-wide canvas. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_color(0xab);
    gfx_glyph_set_dest(5, 3);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit(k_font_blob_tight);

    T_ASSERT_EQ_U(at(&c, 5, 3), 0xab);
    T_ASSERT_EQ_U(at(&c, 6, 3), 0x00);
    T_ASSERT_EQ_U(at(&c, 7, 3), 0xab);
    T_ASSERT_EQ_U(at(&c, 5, 4), 0x00);
    T_ASSERT_EQ_U(at(&c, 6, 4), 0xab);
    T_ASSERT_EQ_U(at(&c, 7, 4), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_glyph_blit_preserves_underlying_pixels_under_zeros(void)
{
    /* If dest pixels are non-zero, the zero-source pixels must leave
     * them alone (silhouette overlay semantics). */
    canvas_t c; canvas_init(&c);
    /* Pre-fill the 3x2 destination block with 0x77. */
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++)
            c.pixels[(x) + (y) * CANVAS_W] = 0x77;

    gfx_glyph_set_color(0x33);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit(k_font_blob_tight);

    /* row 0: 33 (was 77, now 33), 77 (preserved by zero), 33 */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x33);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x77);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x33);
    /* row 1: 77, 33, 77 */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x77);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x33);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x77);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_glyph_blit — quirk: zero counters still run once
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_glyph_blit_zero_width_writes_one_pixel_per_row(void)
{
    /* The inner loop is `do {...} while (...)` so the first
     * iteration always runs, even at glyph_w == 0.  Faithful to
     * retail; see engine-quirks.md. */
    canvas_t c; canvas_init(&c);
    /* Place a non-zero source byte at the glyph_offset position so
     * the body's transparency test passes. */
    static const uint8_t blob[16] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x09,                                            /* glyph data */
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    gfx_glyph_set_color(0x44);
    gfx_glyph_set_dest(3, 2);
    gfx_glyph_set_glyph(8, 0, 1);             /* glyph_w == 0 */
    gfx_glyph_set_clip(0, 0, CANVAS_W);       /* dst pad = W - 0 = W */
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit(blob);

    /* One write at (3, 2) — the body ran once before the post-test
     * exited the inner loop. */
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x44);
    /* Adjacent pixels untouched. */
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 3), 0x00);
    return 0;
}

int test_gfx_glyph_blit_zero_height_writes_one_row(void)
{
    /* glyph_h == 0 still runs one outer iteration. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_color(0x55);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 3, 0);             /* glyph_h == 0 */
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit(k_font_blob_tight);

    /* One row of 3 pixels written. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x55);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x00);   /* transparent */
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x55);
    /* No second row. */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x00);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_glyph_blit — null canvas safety
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_glyph_blit_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_color(0x42);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, 0);
    gfx_glyph_set_color_modulation(0);
    /* Must not segfault. */
    gfx_glyph_blit(k_font_blob_tight);
    gfx_glyph_blit_left_clipped(k_font_blob);
    gfx_glyph_blit_right_clipped(k_font_blob);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * gfx_glyph_blit_left_clipped — FUN_004b425c (clipped)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_glyph_blit_left_clipped_writes_clipped_width(void)
{
    /* Source row is 5 bytes wide but visible width is 3 — source
     * pointer advances by src_stride_pad = 5 - 3 = 2 per row. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_color(0x42);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 5, 2);             /* glyph_w = 5 */
    gfx_glyph_set_clip(3, 2, CANVAS_W - 3);   /* clipped_w=3, src_pad=2 */
    gfx_glyph_set_color_modulation(1);        /* MUST be ignored — clipped is color-only */

    gfx_glyph_blit_left_clipped(k_font_blob);

    /* row 0: visible cols are first 3 of {01, 00, 07, 05, 06}
     * → dst (2..4, 1) = 42, 00, 42 (clipped variant is color-only,
     *   so 0x07 becomes 0x42 — NOT 0x07). */
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x42);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0x42);
    /* row 1: first 3 of {00, 02, 00, 09, 0A} → 00, 42, 00. */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x42);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    /* Source pad: confirm the 4th source col (0x05) was NOT written
     * past the right edge (5, 1). */
    T_ASSERT_EQ_U(at(&c, 5, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_glyph_blit_right_clipped_byte_identical_behavior(void)
{
    /* FUN_004b425c and FUN_004b42c5 are byte-identical (r2-verified).
     * This test renders the same state with both entry points and
     * asserts the canvases match. */
    canvas_t c1; canvas_init(&c1);
    gfx_glyph_set_color(0x77);
    gfx_glyph_set_dest(4, 2);
    gfx_glyph_set_glyph(8, 5, 2);
    gfx_glyph_set_clip(3, 2, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit_left_clipped(k_font_blob);

    uint8_t saved[CANVAS_W * CANVAS_H];
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) saved[i] = c1.pixels[i];

    canvas_t c2; canvas_init(&c2);   /* rebind to c2, identical state */
    gfx_glyph_set_color(0x77);
    gfx_glyph_set_dest(4, 2);
    gfx_glyph_set_glyph(8, 5, 2);
    gfx_glyph_set_clip(3, 2, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit_right_clipped(k_font_blob);

    T_ASSERT_MEM_EQ(c2.pixels, saved, CANVAS_W * CANVAS_H);
    return 0;
}

int test_gfx_glyph_blit_left_clipped_color_modulation_ignored(void)
{
    /* Even with color_modulation = 1, the clipped variant writes
     * `color` not the source byte (because the clipped retail body
     * has no modulation branch). */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_color(0x99);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 5, 2);
    gfx_glyph_set_clip(3, 2, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(1);

    gfx_glyph_blit_left_clipped(k_font_blob);

    /* Source[0,0] = 0x01 — if modulation were honored, dest would
     * be 0x01.  We expect 0x99 (color). */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x99);
    /* Source[2,0] = 0x07 — also expect 0x99, not 0x07. */
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x99);
    return 0;
}

int test_gfx_glyph_blit_left_clipped_zero_src_pad_equals_unclipped(void)
{
    /* When src_stride_pad == 0 AND clipped_w == glyph_w, the clipped
     * variant produces the same per-row output as the unclipped one
     * (modulo color-modulation; we use mode 0 to match). */
    canvas_t c1; canvas_init(&c1);
    gfx_glyph_set_color(0x42);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit(k_font_blob_tight);

    uint8_t unclipped[CANVAS_W * CANVAS_H];
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) unclipped[i] = c1.pixels[i];

    canvas_t c2; canvas_init(&c2);
    gfx_glyph_set_color(0x42);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_blit_left_clipped(k_font_blob_tight);

    T_ASSERT_MEM_EQ(c2.pixels, unclipped, CANVAS_W * CANVAS_H);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * State persistence between calls
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_glyph_state_persists_until_set(void)
{
    /* Set color once, run two blits — second blit should still use
     * the same color. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_color(0x66);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_glyph_set_color_modulation(0);

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_blit(k_font_blob_tight);
    gfx_glyph_set_dest(8, 0);
    gfx_glyph_blit(k_font_blob_tight);

    /* Both glyphs painted with 0x66. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x66);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x66);
    T_ASSERT_EQ_U(at(&c, 8, 0), 0x66);
    T_ASSERT_EQ_U(at(&c, 10, 0), 0x66);
    return 0;
}
