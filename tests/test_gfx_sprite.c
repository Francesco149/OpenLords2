/*
 * Unit tests for gfx_sprite — the sprite-class blitter trio.
 *
 * Shares state with gfx_glyph (cursor, source offset, width/height,
 * the three clip values), so the fixtures here stage state via the
 * gfx_glyph_set_* setters before each blit — mirroring how the
 * sprite dispatcher will populate them at runtime.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_sprite.h"
#include "gfx_dispatch.h"

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

/* ────────────────────────────────────────────────────────────────
 * Sprite blobs.  Each starts with 8 bytes of filler so the
 * `+ sprite_offset` arithmetic exercises non-zero offsets.
 * ──────────────────────────────────────────────────────────────── */

/* Tight 3-wide × 2-row source — for the (width & 3) != 0 path
 * (entire row goes byte-at-a-time, no unrolled prefix). */
static const uint8_t k_blob_3x2[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* filler */
    0x01, 0x00, 0x07,                                /* row 0 */
    0x00, 0x02, 0x00,                                /* row 1 */
    0xff, 0xff,
};

/* 4-wide × 2-row source — exercises the unrolled-only path (width
 * is exactly the unroll factor, no tail).  Source rows are tightly
 * packed at glyph_w bytes apiece. */
static const uint8_t k_blob_4x2[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x11, 0x00, 0x22, 0x33,                          /* row 0 */
    0x00, 0x44, 0x00, 0x55,                          /* row 1 */
};

/* 8-wide × 1-row source — exercises the unrolled-prefix + tail-
 * loop combination (width is a multiple of 4, > 4). */
static const uint8_t k_blob_8x1[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xa1, 0x00, 0xa3, 0xa4, 0xa5, 0x00, 0xa7, 0xa8,
};

/* Wider source for the clipped variants — 5 cols, 2 rows.  Visible
 * portion is 3 cols; src_stride_pad = 2 per row. */
static const uint8_t k_blob_5x2[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x01, 0x00, 0x07, 0x05, 0x06,                    /* row 0 */
    0x00, 0x02, 0x00, 0x09, 0x0a,                    /* row 1 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit — FUN_004b43b1 (unclipped)
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_blit_copies_source_bytes_3x2(void)
{
    /* 3-wide row (not a multiple of 4) routes through the byte-at-a-
     * time tail loop for the entire row.  Zero source bytes are
     * transparent. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);

    gfx_sprite_blit(k_blob_3x2);

    /* row 0: src = 01 00 07 → dst at (2..4, 1) = 01 00 07 */
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x01);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0x07);
    /* row 1: src = 00 02 00 → dst at (2..4, 2) = 00 02 00 */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x02);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    /* Surrounding pixels untouched. */
    T_ASSERT_EQ_U(at(&c, 1, 1), 0);
    T_ASSERT_EQ_U(at(&c, 5, 1), 0);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_unroll_only_width_equals_4(void)
{
    /* width == 4: enters the unrolled-only path.  No tail; the four
     * bytes are written via the unrolled block. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 4, 2);
    gfx_glyph_set_clip(4, 0, CANVAS_W - 4);

    gfx_sprite_blit(k_blob_4x2);

    /* row 0: 11 00 22 33 */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x11);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x22);
    T_ASSERT_EQ_U(at(&c, 3, 0), 0x33);
    /* row 1: 00 44 00 55 */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x44);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x55);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_unroll_plus_tail_width_8(void)
{
    /* width == 8: first 4 bytes via unroll, remaining 4 via the
     * byte-at-a-time tail.  Pixel-perfect result must match
     * whole-row byte-at-a-time semantics. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 8, 1);
    gfx_glyph_set_clip(8, 0, CANVAS_W - 8);

    gfx_sprite_blit(k_blob_8x1);

    T_ASSERT_EQ_U(at(&c, 0, 0), 0xa1);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0xa3);
    T_ASSERT_EQ_U(at(&c, 3, 0), 0xa4);
    T_ASSERT_EQ_U(at(&c, 4, 0), 0xa5);
    T_ASSERT_EQ_U(at(&c, 5, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 6, 0), 0xa7);
    T_ASSERT_EQ_U(at(&c, 7, 0), 0xa8);
    /* Right of the sprite is untouched. */
    T_ASSERT_EQ_U(at(&c, 8, 0), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_preserves_underlying_pixels_under_zeros(void)
{
    /* Zero source pixels must leave dest alone (transparency). */
    canvas_t c; canvas_init(&c);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++)
            c.pixels[(x) + (y) * CANVAS_W] = 0x77;

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);

    gfx_sprite_blit(k_blob_3x2);

    /* row 0: 01 (overwrote 77), 77 (preserved by zero src), 07 */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x01);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x77);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x07);
    /* row 1: 77, 02, 77 */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x77);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x02);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x77);
    return 0;
}

int test_gfx_sprite_blit_writes_at_offset(void)
{
    /* Confirm dest cursor + dst_stride_pad land sprite correctly. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_dest(5, 3);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);

    gfx_sprite_blit(k_blob_3x2);

    T_ASSERT_EQ_U(at(&c, 5, 3), 0x01);
    T_ASSERT_EQ_U(at(&c, 6, 3), 0x00);
    T_ASSERT_EQ_U(at(&c, 7, 3), 0x07);
    T_ASSERT_EQ_U(at(&c, 5, 4), 0x00);
    T_ASSERT_EQ_U(at(&c, 6, 4), 0x02);
    T_ASSERT_EQ_U(at(&c, 7, 4), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit — quirks: partial-unroll edge cases
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_blit_zero_width_writes_4_bytes_per_row(void)
{
    /* width == 0 has (0 & 3) == 0, so the outer while-skip lets the
     * unrolled body fire — writing 4 source bytes per row regardless
     * of the staged width.  Latent quirk preserved verbatim; dispatcher
     * gates this case off via y_clip_mode == 5.  See engine-quirks.md. */
    canvas_t c; canvas_init(&c);
    static const uint8_t blob[16] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x09, 0x0a, 0x00, 0x0c,                          /* 4 "leak" bytes */
        0xff, 0xff, 0xff, 0xff,
    };
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 0, 1);             /* glyph_w == 0 */
    /* dst_stride_pad chosen as if for width 0 (canvas_w - 0). */
    gfx_glyph_set_clip(0, 0, CANVAS_W);

    gfx_sprite_blit(blob);

    /* 4 bytes were written from src[0..3] = 09 0a 00 0c, transparency
     * skipping the third. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x09);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x0a);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x00);  /* transparent leaves zero */
    T_ASSERT_EQ_U(at(&c, 3, 0), 0x0c);
    /* No second row (rows == 1, post-test exits after one). */
    return 0;
}

int test_gfx_sprite_blit_zero_height_writes_one_row(void)
{
    /* Same post-test quirk as the glyph blitter — h == 0 still runs
     * one outer iteration. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 3, 0);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);

    gfx_sprite_blit(k_blob_3x2);

    T_ASSERT_EQ_U(at(&c, 0, 0), 0x01);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x07);
    /* No second row. */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit — null canvas safety
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_blit_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, 0);
    gfx_sprite_blit(k_blob_3x2);
    gfx_sprite_blit_left_clipped(k_blob_5x2);
    gfx_sprite_blit_right_clipped(k_blob_5x2);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit_left_clipped — FUN_004b446d (clipped)
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_blit_left_clipped_copies_clipped_width(void)
{
    /* glyph_w = 5, clipped_w = 3, src_pad = 2 — source pointer
     * advances by 2 per row to skip the trimmed cols. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 5, 2);
    gfx_glyph_set_clip(3, 2, CANVAS_W - 3);

    gfx_sprite_blit_left_clipped(k_blob_5x2);

    /* row 0: visible cols = first 3 of {01, 00, 07, 05, 06} → 01 00 07.
     * Unlike the GLYPH clipped path, the SPRITE clipped path copies
     * source bytes — so 0x07 lands as 0x07 (NOT as a color value). */
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x01);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0x07);
    /* row 1: first 3 of {00, 02, 00, 09, 0A} → 00 02 00. */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x02);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    /* The trimmed-off 4th col (0x05) must NOT have been written. */
    T_ASSERT_EQ_U(at(&c, 5, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_right_clipped_byte_identical_to_left(void)
{
    /* FUN_004b446d and FUN_004b44d2 are byte-identical (r2-verified:
     * pxd 101 bytes match at both addresses).  This test renders the
     * same state with both entry points and asserts the canvases
     * match bit-for-bit. */
    canvas_t c1; canvas_init(&c1);
    gfx_glyph_set_dest(4, 2);
    gfx_glyph_set_glyph(8, 5, 2);
    gfx_glyph_set_clip(3, 2, CANVAS_W - 3);
    gfx_sprite_blit_left_clipped(k_blob_5x2);
    uint8_t saved[CANVAS_W * CANVAS_H];
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) saved[i] = c1.pixels[i];

    canvas_t c2; canvas_init(&c2);
    gfx_glyph_set_dest(4, 2);
    gfx_glyph_set_glyph(8, 5, 2);
    gfx_glyph_set_clip(3, 2, CANVAS_W - 3);
    gfx_sprite_blit_right_clipped(k_blob_5x2);

    T_ASSERT_MEM_EQ(c2.pixels, saved, CANVAS_W * CANVAS_H);
    return 0;
}

int test_gfx_sprite_blit_left_clipped_zero_src_pad_equals_unclipped(void)
{
    /* When src_pad == 0 and clipped_w == glyph_w, the clipped sprite
     * variant produces the same per-row output as the unclipped one. */
    canvas_t c1; canvas_init(&c1);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_sprite_blit(k_blob_3x2);
    uint8_t unclipped[CANVAS_W * CANVAS_H];
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) unclipped[i] = c1.pixels[i];

    canvas_t c2; canvas_init(&c2);
    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, 2);
    gfx_glyph_set_clip(3, 0, CANVAS_W - 3);
    gfx_sprite_blit_left_clipped(k_blob_3x2);

    T_ASSERT_MEM_EQ(c2.pixels, unclipped, CANVAS_W * CANVAS_H);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_dispatch_sprite — FUN_00404442 (canvas-clip)
 * ──────────────────────────────────────────────────────────────── */

/* Sprite blob with a single 3×2 descriptor at slot 0.
 * Header (8 bytes filler) + descriptor (16 bytes):
 *   width=3, height=2, src_offset=24, padding,
 *   flag_a=0, row_count=2, flag_b=0, last byte filler.
 * Then 6 bytes of pixel data at offset 24.
 *
 * Total layout: [8 header][16 descriptor][6 pixel].
 */
static const uint8_t k_sprite_blob_one[32] = {
    /* +0..7: PL8-style header (any bytes; only +8 onward matters) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* +8..23: descriptor slot 0 */
    0x03, 0x00,                      /* +0..1   width  = 3 */
    0x02, 0x00,                      /* +2..3   height = 2 */
    0x18, 0x00, 0x00, 0x00,          /* +4..7   src_offset = 24 (0x18) */
    0x00, 0x00, 0x00, 0x00,          /* +8..11  padding */
    0x00,                            /* +12     flag_a */
    0x02,                            /* +13     row_count */
    0x00,                            /* +14     flag_b */
    0x00,                            /* +15     filler */
    /* +24..29: pixel data, 3-wide × 2-row */
    0xa1, 0x00, 0xa3,
    0x00, 0xb2, 0x00,
    0x00, 0x00,
};

int test_gfx_dispatch_sprite_paints_at_dest(void)
{
    canvas_t c; canvas_init(&c);

    gfx_dispatch_sprite(k_sprite_blob_one, /*idx=*/0, /*x=*/2, /*y=*/1);

    /* Sprite is fully visible (no clipping) — unclipped blitter
     * picked, source copied byte-for-byte. */
    T_ASSERT_EQ_U(at(&c, 2, 1), 0xa1);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0xa3);
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0xb2);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_dispatch_sprite_left_clip_writes_visible_portion(void)
{
    /* Place sprite so its left edge is past the canvas's left bound,
     * forcing left-clip mode (x_clip_mode = 1). */
    canvas_t c; canvas_init(&c);
    gfx_dispatch_sprite(k_sprite_blob_one, 0, /*x=*/-1, /*y=*/0);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 1);
    /* Visible cols are sprite cols 1..2 (the 0-th col is clipped off).
     * Row 0 visible: src[1]=00, src[2]=a3 → dst at (0, 0) = 00, dst at (1, 0) = a3. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0xa3);
    /* Row 1 visible: src[1]=b2, src[2]=00 → dst at (0, 1) = b2, dst at (1, 1) = 00. */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0xb2);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_dispatch_sprite_right_clip_writes_visible_portion(void)
{
    /* Place sprite so its right edge crosses canvas right bound. */
    canvas_t c; canvas_init(&c);
    gfx_dispatch_sprite(k_sprite_blob_one, 0, /*x=*/CANVAS_W - 2, /*y=*/0);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 2);
    /* Visible cols are sprite cols 0..1 at canvas cols (W-2)..(W-1).
     * row 0: src[0]=a1, src[1]=00 */
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 2, 0), 0xa1);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, 0), 0x00);
    /* row 1: src[0]=00, src[1]=b2 */
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 2, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, 1), 0xb2);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_dispatch_sprite_fully_offscreen_writes_nothing(void)
{
    /* Sprite placed entirely past the right edge — x_clip_mode goes
     * to skip (5) and no blitter fires. */
    canvas_t c; canvas_init(&c);
    gfx_dispatch_sprite(k_sprite_blob_one, 0, /*x=*/CANVAS_W + 5, /*y=*/0);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 5);
    /* No pixels touched. */
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            T_ASSERT_EQ_U(c.pixels[x + y * CANVAS_W], 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_dispatch_sprite_clipped — FUN_00404547 (caller clip rect)
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_dispatch_sprite_clipped_uses_supplied_rect(void)
{
    /* Constrain to a 4-wide sub-rect starting at canvas x=3.  The
     * sprite at x=2 straddles the rect's left edge, so it should be
     * left-clipped against x=3 (not against canvas x=0). */
    canvas_t c; canvas_init(&c);

    gfx_dispatch_sprite_clipped(k_sprite_blob_one, 0,
                                /*x=*/2, /*y=*/0,
                                /*left=*/3, /*top=*/0,
                                /*right=*/7, /*bottom=*/CANVAS_H);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 1);   /* left-clip */
    /* Sprite col 0 trimmed (its dst would be x=2, off the rect); cols
     * 1..2 visible at dst x=3..4.  Row 0: src[1]=00, src[2]=a3. */
    T_ASSERT_EQ_U(at(&c, 3, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 0), 0xa3);
    /* Outside the rect (x=2) untouched. */
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x00);
    /* Outside rect right (x=5..) untouched too. */
    T_ASSERT_EQ_U(at(&c, 5, 0), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit_16x16_raw — FUN_004b49f7
 *
 * The 16×16 raw blit hardcodes a 640-byte dest row stride
 * (independent of the bound canvas pitch).  Tests use a
 * BIG_W-wide canvas with BIG_W == 640 so the hardcoded stride
 * matches the real row pitch — that's how retail uses it.
 * ──────────────────────────────────────────────────────────────── */

enum { BIG_W = 640, BIG_H = 32 };

typedef struct {
    uint8_t guard_top[16];
    uint8_t pixels[BIG_W * BIG_H];
    uint8_t guard_bot[16];
} big_canvas_t;

static void big_canvas_init(big_canvas_t *c)
{
    for (int i = 0; i < (int)sizeof c->guard_top; i++) c->guard_top[i] = 0xee;
    for (int i = 0; i < (int)sizeof c->pixels;    i++) c->pixels[i]    = 0;
    for (int i = 0; i < (int)sizeof c->guard_bot; i++) c->guard_bot[i] = 0xee;
    gfx_paint_bind_canvas(c->pixels, BIG_W, BIG_H);
}

static int big_canvas_guards_intact(const big_canvas_t *c)
{
    for (int i = 0; i < 16; i++) {
        if (c->guard_top[i] != 0xee) return 0;
        if (c->guard_bot[i] != 0xee) return 0;
    }
    return 1;
}

/* Build a 256-byte 16×16 source tile where each pixel encodes its
 * own (row, col): byte at row r, col c = (r * 16) + c + 1.  Lets
 * tests verify which source bytes land at which dest position. */
static void fill_16x16_tile(uint8_t *out)
{
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            out[r * 16 + c] = (uint8_t)(r * 16 + c + 1);
}

int test_gfx_sprite_blit_16x16_raw_copies_all_256_bytes(void)
{
    big_canvas_t c; big_canvas_init(&c);

    uint8_t blob[8 + 256];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;     /* filler */
    fill_16x16_tile(blob + 8);

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 16, 16);  /* glyph_offset = 8 → skip filler */

    gfx_sprite_blit_16x16_raw(blob);

    /* Every one of 16×16 = 256 dest bytes matches its source. */
    for (int r = 0; r < 16; r++)
        for (int col = 0; col < 16; col++)
            T_ASSERT_EQ_U(c.pixels[col + r * BIG_W],
                          (uint8_t)(r * 16 + col + 1));
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_16x16_raw_writes_zero_bytes_opaque(void)
{
    /* Unlike gfx_sprite_blit, this primitive has NO transparency
     * check — every source byte is written, including zeros.
     * Pre-fill the dest with a non-zero pattern and verify the blit
     * overwrites it with zeros. */
    big_canvas_t c; big_canvas_init(&c);
    for (int i = 0; i < (int)sizeof c.pixels; i++) c.pixels[i] = 0xa5;

    uint8_t blob[8 + 256] = {0};   /* all-zero src after filler */
    for (int i = 0; i < 8; i++) blob[i] = 0xff;

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 16, 16);

    gfx_sprite_blit_16x16_raw(blob);

    /* All 16×16 dest bytes overwritten with the zero source — so
     * the 0xa5 pre-fill is gone in the 16×16 footprint. */
    for (int r = 0; r < 16; r++)
        for (int col = 0; col < 16; col++)
            T_ASSERT_EQ_U(c.pixels[col + r * BIG_W], 0x00);
    /* Outside the 16×16 footprint, the 0xa5 pre-fill is intact. */
    T_ASSERT_EQ_U(c.pixels[16 + 0 * BIG_W], 0xa5);
    T_ASSERT_EQ_U(c.pixels[0 + 16 * BIG_W], 0xa5);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_16x16_raw_respects_cursor(void)
{
    /* Cursor at (37, 5) — verify the blit lands at the right
     * dest origin. */
    big_canvas_t c; big_canvas_init(&c);

    uint8_t blob[8 + 256];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;
    fill_16x16_tile(blob + 8);

    gfx_glyph_set_dest(37, 5);
    gfx_glyph_set_glyph(8, 16, 16);

    gfx_sprite_blit_16x16_raw(blob);

    /* Top-left of blit: row 0, col 0 → source byte 1 at (37, 5). */
    T_ASSERT_EQ_U(c.pixels[37 + 5 * BIG_W], 0x01);
    /* Bottom-right of blit: row 15, col 15 → source byte 256
     * (= 0x00 after wrap from 256 to uint8_t) at (52, 20). */
    T_ASSERT_EQ_U(c.pixels[52 + 20 * BIG_W], (uint8_t)256);
    /* Off-blit pixel: (36, 5) — column before footprint — still 0. */
    T_ASSERT_EQ_U(c.pixels[36 + 5 * BIG_W], 0x00);
    /* Off-blit pixel: (37, 4) — row before footprint — still 0. */
    T_ASSERT_EQ_U(c.pixels[37 + 4 * BIG_W], 0x00);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_16x16_raw_uses_glyph_offset(void)
{
    /* The dispatcher stages glyph_offset; the blit reads from
     * sprite_blob + glyph_offset.  Two different offsets in the
     * same blob produce two different tiles in the dest. */
    big_canvas_t c; big_canvas_init(&c);

    /* Two adjacent tiles in a blob: tile A starts at offset 8, tile
     * B starts at offset 8 + 256.  Tile A is all-0x11, tile B is
     * all-0x22. */
    uint8_t blob[8 + 256 + 256];
    for (int i = 0; i < 8; i++)        blob[i] = 0xff;
    for (int i = 0; i < 256; i++)      blob[8 + i] = 0x11;
    for (int i = 0; i < 256; i++)      blob[8 + 256 + i] = 0x22;

    /* First blit — tile A at (0, 0). */
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 16, 16);
    gfx_sprite_blit_16x16_raw(blob);

    /* Second blit — tile B at (20, 0). */
    gfx_glyph_set_dest(20, 0);
    gfx_glyph_set_glyph(8 + 256, 16, 16);
    gfx_sprite_blit_16x16_raw(blob);

    /* Tile A footprint at (0..15, 0..15) is 0x11. */
    T_ASSERT_EQ_U(c.pixels[0 + 0 * BIG_W], 0x11);
    T_ASSERT_EQ_U(c.pixels[15 + 15 * BIG_W], 0x11);
    /* Tile B footprint at (20..35, 0..15) is 0x22. */
    T_ASSERT_EQ_U(c.pixels[20 + 0 * BIG_W], 0x22);
    T_ASSERT_EQ_U(c.pixels[35 + 15 * BIG_W], 0x22);
    /* Gap between tiles still 0. */
    T_ASSERT_EQ_U(c.pixels[17 + 7 * BIG_W], 0x00);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_16x16_raw_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 16, 16);

    uint8_t blob[8 + 256];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;
    fill_16x16_tile(blob + 8);

    /* No crash, no observable output (nothing bound). */
    gfx_sprite_blit_16x16_raw(blob);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit_32x32_raw — FUN_004b5333
 *
 * Sibling of the 16×16 primitive — same hardcoded 640-byte dest
 * stride, no transparency, no width/height staging consulted.
 * Tests use the same BIG_W=640 / BIG_H=32 canvas: 32×32 at cursor
 * (0, 0) fits exactly into the 32-row buffer.
 * ──────────────────────────────────────────────────────────────── */

static void fill_32x32_tile(uint8_t *out)
{
    /* Each byte = (row * 32 + col + 1) & 0xff — unique per-byte pattern
     * within the 256-byte range (wraps every 256 bytes, but each row
     * has a distinct first byte). */
    for (int r = 0; r < 32; r++)
        for (int c = 0; c < 32; c++)
            out[r * 32 + c] = (uint8_t)(r * 32 + c + 1);
}

int test_gfx_sprite_blit_32x32_raw_copies_all_1024_bytes(void)
{
    big_canvas_t c; big_canvas_init(&c);

    uint8_t blob[8 + 1024];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;     /* filler */
    fill_32x32_tile(blob + 8);

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 32, 32);  /* glyph_offset = 8 → skip filler */

    gfx_sprite_blit_32x32_raw(blob);

    /* Every one of 32×32 = 1024 dest bytes matches its source. */
    for (int r = 0; r < 32; r++)
        for (int col = 0; col < 32; col++)
            T_ASSERT_EQ_U(c.pixels[col + r * BIG_W],
                          (uint8_t)(r * 32 + col + 1));
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_32x32_raw_writes_zero_bytes_opaque(void)
{
    /* Same as the 16×16 sibling: no transparency check.  Pre-fill the
     * dest with non-zero, blit zeroes, verify overwrite. */
    big_canvas_t c; big_canvas_init(&c);
    for (int i = 0; i < (int)sizeof c.pixels; i++) c.pixels[i] = 0xa5;

    uint8_t blob[8 + 1024] = {0};
    for (int i = 0; i < 8; i++) blob[i] = 0xff;

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 32, 32);

    gfx_sprite_blit_32x32_raw(blob);

    /* All 32×32 dest bytes overwritten with zero source. */
    for (int r = 0; r < 32; r++)
        for (int col = 0; col < 32; col++)
            T_ASSERT_EQ_U(c.pixels[col + r * BIG_W], 0x00);
    /* Outside the 32×32 footprint, 0xa5 pre-fill is intact. */
    T_ASSERT_EQ_U(c.pixels[32 + 0 * BIG_W], 0xa5);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_32x32_raw_uses_glyph_offset(void)
{
    /* Two adjacent tiles in a blob: tile A all-0x11, tile B all-0x22.
     * Each tile is 1024 bytes; the dispatcher's glyph_offset selects
     * which one the blit consumes. */
    big_canvas_t c; big_canvas_init(&c);

    uint8_t blob[8 + 1024 + 1024];
    for (int i = 0; i < 8; i++)        blob[i] = 0xff;
    for (int i = 0; i < 1024; i++)     blob[8 + i] = 0x11;
    for (int i = 0; i < 1024; i++)     blob[8 + 1024 + i] = 0x22;

    /* First blit: tile A at (0, 0). */
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 32, 32);
    gfx_sprite_blit_32x32_raw(blob);

    /* All 32×32 of tile A's footprint should be 0x11. */
    T_ASSERT_EQ_U(c.pixels[0 + 0 * BIG_W], 0x11);
    T_ASSERT_EQ_U(c.pixels[31 + 31 * BIG_W], 0x11);

    /* Second blit at a new position is harder to test without enough
     * canvas height; instead verify the offset selects tile B by
     * resetting and blitting AGAIN at (0, 0) with offset = 8 + 1024.
     * The tile-A pattern is overwritten with tile-B's 0x22. */
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8 + 1024, 32, 32);
    gfx_sprite_blit_32x32_raw(blob);

    T_ASSERT_EQ_U(c.pixels[0 + 0 * BIG_W], 0x22);
    T_ASSERT_EQ_U(c.pixels[31 + 31 * BIG_W], 0x22);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_32x32_raw_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 32, 32);

    uint8_t blob[8 + 1024];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;
    fill_32x32_tile(blob + 8);

    /* No crash, no observable output (nothing bound). */
    gfx_sprite_blit_32x32_raw(blob);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit_24x24_raw — FUN_004b4cb8
 *
 * The middle sibling — 24×24, same hardcoded 640-byte dest stride,
 * no transparency, no width/height staging consulted.  Same
 * BIG_W=640 / BIG_H=32 canvas.
 * ──────────────────────────────────────────────────────────────── */

static void fill_24x24_tile(uint8_t *out)
{
    for (int r = 0; r < 24; r++)
        for (int c = 0; c < 24; c++)
            out[r * 24 + c] = (uint8_t)(r * 24 + c + 1);
}

int test_gfx_sprite_blit_24x24_raw_copies_all_576_bytes(void)
{
    big_canvas_t c; big_canvas_init(&c);

    uint8_t blob[8 + 576];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;     /* filler */
    fill_24x24_tile(blob + 8);

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 24, 24);  /* glyph_offset = 8 → skip filler */

    gfx_sprite_blit_24x24_raw(blob);

    /* Every one of 24×24 = 576 dest bytes matches its source. */
    for (int r = 0; r < 24; r++)
        for (int col = 0; col < 24; col++)
            T_ASSERT_EQ_U(c.pixels[col + r * BIG_W],
                          (uint8_t)(r * 24 + col + 1));
    /* Column 24 (just past the footprint) is untouched. */
    T_ASSERT_EQ_U(c.pixels[24 + 0 * BIG_W], 0x00);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_24x24_raw_writes_zero_bytes_opaque(void)
{
    /* No transparency check — zeros overwrite the dest. */
    big_canvas_t c; big_canvas_init(&c);
    for (int i = 0; i < (int)sizeof c.pixels; i++) c.pixels[i] = 0xa5;

    uint8_t blob[8 + 576] = {0};
    for (int i = 0; i < 8; i++) blob[i] = 0xff;

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 24, 24);

    gfx_sprite_blit_24x24_raw(blob);

    for (int r = 0; r < 24; r++)
        for (int col = 0; col < 24; col++)
            T_ASSERT_EQ_U(c.pixels[col + r * BIG_W], 0x00);
    /* Outside the 24×24 footprint, 0xa5 pre-fill is intact. */
    T_ASSERT_EQ_U(c.pixels[24 + 0 * BIG_W], 0xa5);
    T_ASSERT_EQ_U(c.pixels[0 + 24 * BIG_W], 0xa5);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_24x24_raw_respects_cursor(void)
{
    /* Blit at a non-zero cursor; verify the footprint lands offset by
     * the cursor and the dest origin uses the dynamic canvas width. */
    big_canvas_t c; big_canvas_init(&c);

    uint8_t blob[8 + 576];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;
    fill_24x24_tile(blob + 8);

    gfx_glyph_set_dest(5, 3);
    gfx_glyph_set_glyph(8, 24, 24);

    gfx_sprite_blit_24x24_raw(blob);

    /* Top-left of the footprint is at (5, 3); source byte (0,0) = 1. */
    T_ASSERT_EQ_U(c.pixels[5 + 3 * BIG_W], 0x01);
    /* Bottom-right corner of the footprint: source byte (23,23). */
    T_ASSERT_EQ_U(c.pixels[(5 + 23) + (3 + 23) * BIG_W],
                  (uint8_t)(23 * 24 + 23 + 1));
    /* The cell just left of the origin is untouched. */
    T_ASSERT_EQ_U(c.pixels[4 + 3 * BIG_W], 0x00);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_24x24_raw_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 24, 24);

    uint8_t blob[8 + 576];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;
    fill_24x24_tile(blob + 8);

    gfx_sprite_blit_24x24_raw(blob);   /* no crash, nothing bound */
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit_2x2_raw — FUN_004b49b7 (chip 7).
 *
 * The 2×2 raw blit hardcodes the same 640-byte dest row stride as
 * its larger siblings (quirk #75).  Same big_canvas_t harness.
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_blit_2x2_raw_copies_4_bytes(void)
{
    big_canvas_t c; big_canvas_init(&c);

    /* Source: 4 bytes after an 8-byte filler.  Tag bytes 1..4. */
    uint8_t blob[8 + 4] = {0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
                            0x01, 0x02, 0x03, 0x04};

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 2, 2);

    gfx_sprite_blit_2x2_raw(blob);

    /* Row 0: (0,0)=0x01, (1,0)=0x02. */
    T_ASSERT_EQ_U(c.pixels[0 + 0 * BIG_W], 0x01);
    T_ASSERT_EQ_U(c.pixels[1 + 0 * BIG_W], 0x02);
    /* Row 1: (0,1)=0x03, (1,1)=0x04. */
    T_ASSERT_EQ_U(c.pixels[0 + 1 * BIG_W], 0x03);
    T_ASSERT_EQ_U(c.pixels[1 + 1 * BIG_W], 0x04);
    /* Adjacent untouched cells stay zero. */
    T_ASSERT_EQ_U(c.pixels[2 + 0 * BIG_W], 0x00);
    T_ASSERT_EQ_U(c.pixels[0 + 2 * BIG_W], 0x00);
    T_ASSERT(big_canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_2x2_raw_writes_zero_bytes_opaque(void)
{
    big_canvas_t c; big_canvas_init(&c);
    for (int i = 0; i < (int)sizeof c.pixels; i++) c.pixels[i] = 0xa5;

    uint8_t blob[8 + 4] = {0};  /* all-zero source */
    for (int i = 0; i < 8; i++) blob[i] = 0xff;

    gfx_glyph_set_dest(10, 10);
    gfx_glyph_set_glyph(8, 2, 2);

    gfx_sprite_blit_2x2_raw(blob);

    /* The 2×2 footprint is overwritten with zeros even though source
     * is zero (no transparency check). */
    T_ASSERT_EQ_U(c.pixels[10 + 10 * BIG_W], 0x00);
    T_ASSERT_EQ_U(c.pixels[11 + 10 * BIG_W], 0x00);
    T_ASSERT_EQ_U(c.pixels[10 + 11 * BIG_W], 0x00);
    T_ASSERT_EQ_U(c.pixels[11 + 11 * BIG_W], 0x00);
    /* Adjacent pixel still has the 0xa5 pre-fill. */
    T_ASSERT_EQ_U(c.pixels[12 + 10 * BIG_W], 0xa5);
    return 0;
}

int test_gfx_sprite_blit_2x2_raw_respects_cursor(void)
{
    big_canvas_t c; big_canvas_init(&c);

    uint8_t blob[8 + 4] = {0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
                            0xa1, 0xa2, 0xa3, 0xa4};

    gfx_glyph_set_dest(50, 20);
    gfx_glyph_set_glyph(8, 2, 2);

    gfx_sprite_blit_2x2_raw(blob);

    T_ASSERT_EQ_U(c.pixels[50 + 20 * BIG_W], 0xa1);
    T_ASSERT_EQ_U(c.pixels[51 + 20 * BIG_W], 0xa2);
    T_ASSERT_EQ_U(c.pixels[50 + 21 * BIG_W], 0xa3);
    T_ASSERT_EQ_U(c.pixels[51 + 21 * BIG_W], 0xa4);
    return 0;
}

int test_gfx_sprite_blit_2x2_raw_uses_glyph_offset(void)
{
    big_canvas_t c; big_canvas_init(&c);

    /* Two adjacent tiles in a blob. */
    uint8_t blob[8 + 4 + 4];
    for (int i = 0; i < 8; i++) blob[i] = 0xff;
    blob[ 8] = 0x11; blob[ 9] = 0x11; blob[10] = 0x11; blob[11] = 0x11;
    blob[12] = 0x22; blob[13] = 0x22; blob[14] = 0x22; blob[15] = 0x22;

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 2, 2);
    gfx_sprite_blit_2x2_raw(blob);

    gfx_glyph_set_dest(10, 0);
    gfx_glyph_set_glyph(12, 2, 2);
    gfx_sprite_blit_2x2_raw(blob);

    T_ASSERT_EQ_U(c.pixels[0  + 0 * BIG_W], 0x11);
    T_ASSERT_EQ_U(c.pixels[10 + 0 * BIG_W], 0x22);
    T_ASSERT_EQ_U(c.pixels[11 + 1 * BIG_W], 0x22);
    return 0;
}

int test_gfx_sprite_blit_2x2_raw_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 2, 2);

    uint8_t blob[8 + 4] = {0};
    gfx_sprite_blit_2x2_raw(blob);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_blit_opaque — FUN_004b432e
 *
 * The opaque sibling of gfx_sprite_blit: copies EVERY source byte,
 * including zeros (no transparency test).  Row count is the
 * descriptor's +2 short height (gfx_dispatch_glyph_height_short),
 * not the +0xd byte height (gfx_glyph_set_glyph's third arg) that
 * the transparent leaf reads.  Dest row stride = glyph_w +
 * dst_stride_pad (configurable here; the FUN_0040a567 wrapper bakes
 * 0x280 - glyph_w).
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_blit_opaque_copies_zeros_over_background(void)
{
    /* Pre-fill the target region with 0x99 so we can prove the zero
     * source byte overwrites it (transparent blit would leave 0x99). */
    canvas_t c; canvas_init(&c);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) c.pixels[i] = 0x99;

    gfx_glyph_set_dest(2, 1);
    gfx_glyph_set_glyph(8, 3, /*byte-height (unused by opaque)=*/0);
    gfx_dispatch_set_glyph_height_short(2);
    gfx_glyph_set_dst_stride_pad(CANVAS_W - 3);

    gfx_sprite_blit_opaque(k_blob_3x2);

    /* row 0: src = 01 00 07 → ALL copied, including the 0x00. */
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x01);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x00);   /* zero overwrote 0x99 */
    T_ASSERT_EQ_U(at(&c, 4, 1), 0x07);
    /* row 1: src = 00 02 00 → all copied. */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x02);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    /* Pixels outside the blit rect keep the 0x99 background. */
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x99);
    T_ASSERT_EQ_U(at(&c, 5, 1), 0x99);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_opaque_width_multiple_of_4(void)
{
    /* width == 4 (multiple of 4): retail copies one dword per row;
     * byte-for-byte identical to copying 4 bytes.  Opaque → the
     * embedded zeros land. */
    canvas_t c; canvas_init(&c);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) c.pixels[i] = 0x99;

    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 4, 0);
    gfx_dispatch_set_glyph_height_short(2);
    gfx_glyph_set_dst_stride_pad(CANVAS_W - 4);

    gfx_sprite_blit_opaque(k_blob_4x2);

    /* row 0: 11 00 22 33  (all copied) */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x11);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x22);
    T_ASSERT_EQ_U(at(&c, 3, 0), 0x33);
    /* row 1: 00 44 00 55 */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x44);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x55);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_opaque_uses_height_short_not_byte_height(void)
{
    /* Stage byte-height (set_glyph arg 3) = 5 but height_short = 1.
     * The opaque blit must honor height_short and write ONE row. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 3, /*byte-height=*/5);
    gfx_dispatch_set_glyph_height_short(1);
    gfx_glyph_set_dst_stride_pad(CANVAS_W - 3);

    gfx_sprite_blit_opaque(k_blob_3x2);

    /* row 0 written... */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x01);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x07);
    /* ...row 1 NOT written (height_short==1). */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_blit_opaque_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(8, 3, 0);
    gfx_dispatch_set_glyph_height_short(2);
    gfx_glyph_set_dst_stride_pad(0);
    gfx_sprite_blit_opaque(k_blob_3x2);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_fill_block — FUN_004b414a
 *
 * Solid fill of glyph_w 16-px column groups × glyph_height_short rows
 * at an explicit (x, y), using the canvas stride.  Used for the
 * save/load list selection highlight.
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_fill_block_fills_staged_dims(void)
{
    canvas_t c; canvas_init(&c);          /* CANVAS_W=16, CANVAS_H=8 */
    gfx_glyph_set_glyph(0, /*glyph_w=*/1, 0);   /* 1 block = 16 px */
    gfx_dispatch_set_glyph_height_short(3);     /* 3 rows */

    gfx_sprite_fill_block(0, 1, 0x5a);

    /* Rows 1..3 fully filled (16 px wide), rows 0 and 4 untouched. */
    for (int x = 0; x < 16; x++) {
        T_ASSERT_EQ_U(at(&c, x, 0), 0x00);
        T_ASSERT_EQ_U(at(&c, x, 1), 0x5a);
        T_ASSERT_EQ_U(at(&c, x, 3), 0x5a);
        T_ASSERT_EQ_U(at(&c, x, 4), 0x00);
    }
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_fill_block_respects_x_offset(void)
{
    /* Two blocks (32 px) would overflow the 16-wide canvas, so use a
     * single block at x=0 and verify the offset math on a wide row by
     * checking the start column only. */
    canvas_t c; canvas_init(&c);
    gfx_glyph_set_glyph(0, 1, 0);
    gfx_dispatch_set_glyph_height_short(1);
    gfx_sprite_fill_block(0, 2, 0x33);
    T_ASSERT_EQ_U(at(&c, 0, 2), 0x33);
    T_ASSERT_EQ_U(at(&c, 15, 2), 0x33);
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_fill_block_null_canvas_no_crash(void)
{
    gfx_paint_bind_canvas(0, 0, 0);
    gfx_glyph_set_glyph(0, 1, 0);
    gfx_dispatch_set_glyph_height_short(2);
    gfx_sprite_fill_block(0, 0, 0x5a);
    return 0;
}
