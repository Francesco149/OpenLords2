/*
 * Unit tests for gfx_sprite_rle — the RLE (second) sprite-class blitter
 * family + the gfx_sprite_render_rle_from_blob wrapper.
 *
 * Most tests go through the wrapper so dispatcher state (cursor,
 * descriptor, clip output, top_skip) is staged uniformly.  A handful
 * of tests stage state manually to exercise the blitters in isolation
 * for opcode coverage.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_sprite_rle.h"
#include "gfx_sprite_render.h"

enum { CANVAS_W = 24, CANVAS_H = 8 };

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

/* Clear sticky test state.  Successive tests share the gfx_dispatch
 * statics, so explicit clears here keep each test self-contained. */
static void rle_test_reset(void)
{
    gfx_sprite_render_clear_error();
    gfx_sprite_render_set_log(0);
    gfx_sprite_render_set_blob(0);
    gfx_dispatch_set_alt_clip(0, 0, 0);
}

/* ────────────────────────────────────────────────────────────────
 * Sprite blobs.  Each starts with 8 bytes of filler so the
 * `+ sprite_offset` arithmetic exercises non-zero offsets.
 * ──────────────────────────────────────────────────────────────── */

/* 4-wide × 2-row RLE sprite at descriptor index 0.
 * Decoded image:
 *   row 0:  11 22 .. ..       (literal-2, then skip-2)
 *   row 1:  .. 33 44 55       (skip-1, then literal-3)
 *
 * Layout:
 *   [0..7]   filler (PL8-style header)
 *   [8..23]  16-byte descriptor:
 *               +0..1   width        = 4
 *               +2..3   height_short = 2
 *               +4..7   src_offset   = 24
 *               +8..11  padding
 *               +12     flag_a       = 0
 *               +13     row_count    = 2
 *               +14     flag_b       = 0
 *               +15     filler
 *   [24..]   RLE stream:
 *               row 0:  02 11 22 00 02
 *               row 1:  00 01 03 33 44 55
 */
static const uint8_t k_rle_4x2[64] = {
    /* 0..7: header */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    /* 8..23: descriptor */
    0x04, 0x00,                                   /* width = 4 */
    0x02, 0x00,                                   /* height_short = 2 */
    0x18, 0x00, 0x00, 0x00,                       /* src_offset = 24 */
    0x00, 0x00, 0x00, 0x00,                       /* padding */
    0x00,                                         /* flag_a */
    0x02,                                         /* row_count */
    0x00,                                         /* flag_b */
    0x00,                                         /* filler */
    /* 24..28: row 0 RLE */
    0x02, 0x11, 0x22,                             /* literal-2: 11 22 */
    0x00, 0x02,                                   /* skip-2 */
    /* 29..34: row 1 RLE */
    0x00, 0x01,                                   /* skip-1 */
    0x03, 0x33, 0x44, 0x55,                       /* literal-3: 33 44 55 */
};

/* 16-wide × 1-row RLE sprite exercising literal opcode 0x10 (16-byte
 * literal — its own dedicated retail switch arm).
 *
 *   row 0:  10 c0 c1 c2 c3 c4 c5 c6 c7 c8 c9 ca cb cc cd ce cf
 *           → 16 literal bytes c0..cf.
 */
static const uint8_t k_rle_16x1[48] = {
    /* 0..7: header */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    /* 8..23: descriptor */
    0x10, 0x00,                                   /* width = 16 */
    0x01, 0x00,                                   /* height_short = 1 */
    0x18, 0x00, 0x00, 0x00,                       /* src_offset = 24 */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00,
    /* 24..40: row 0 — opcode 0x10 followed by 16 data bytes */
    0x10,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
};

/* 8-wide × 3-row sprite for top-skip tests.
 *   row 0: 08 a0 a1 a2 a3 a4 a5 a6 a7   (8 literals — skipped if top_skip≥1)
 *   row 1: 08 b0 b1 b2 b3 b4 b5 b6 b7
 *   row 2: 08 c0 c1 c2 c3 c4 c5 c6 c7
 */
static const uint8_t k_rle_8x3[64] = {
    /* 0..7: header */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 8..23: descriptor */
    0x08, 0x00,                                   /* width = 8 */
    0x03, 0x00,                                   /* height_short = 3 */
    0x18, 0x00, 0x00, 0x00,                       /* src_offset = 24 */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00,
    /* 24..32: row 0 */
    0x08, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    /* 33..41: row 1 */
    0x08, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    /* 42..50: row 2 */
    0x08, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
};

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_render_rle_from_blob — FUN_0040a3d0
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_rle_render_basic_decode(void)
{
    canvas_t c; canvas_init(&c); rle_test_reset();

    gfx_sprite_render_rle_from_blob(k_rle_4x2, /*idx=*/0, /*x=*/2, /*y=*/1);

    /* row 0: 11 22 .. ..  at (2..5, 1) */
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x11);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x22);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 5, 1), 0x00);
    /* row 1: .. 33 44 55  at (2..5, 2) */
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x33);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x44);
    T_ASSERT_EQ_U(at(&c, 5, 2), 0x55);
    /* Surrounding pixels untouched. */
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 6, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_rle_render_16byte_literal(void)
{
    /* Opcode 0x10 — its own dedicated switch arm.  16 source bytes
     * land verbatim at (0..15, 0). */
    canvas_t c; canvas_init(&c); rle_test_reset();

    gfx_sprite_render_rle_from_blob(k_rle_16x1, 0, /*x=*/0, /*y=*/0);

    for (int i = 0; i < 16; i++) {
        T_ASSERT_EQ_U(at(&c, i, 0), (uint8_t)(0xc0 + i));
    }
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

/* Recorder pattern matching test_gfx_sprite_render.c — module-static
 * state captures (fmt_addr, index) tuples. */

static int      g_log_count;
static uint32_t g_log_fmts[8];
static int      g_log_indices[8];

static void test_log_recorder(uint32_t fmt_addr, int sprite_index_plus_one)
{
    if (g_log_count < (int)(sizeof g_log_fmts / sizeof g_log_fmts[0])) {
        g_log_fmts[g_log_count]    = fmt_addr;
        g_log_indices[g_log_count] = sprite_index_plus_one;
    }
    g_log_count++;
}

static void install_recorder(void)
{
    g_log_count = 0;
    gfx_sprite_render_set_log(test_log_recorder);
}

int test_gfx_sprite_rle_render_null_blob_logs_and_latches(void)
{
    canvas_t c; canvas_init(&c); rle_test_reset();
    install_recorder();

    gfx_sprite_render_rle_from_blob(/*blob=*/0, /*idx=*/4, /*x=*/0, /*y=*/0);

    T_ASSERT_EQ_I(g_log_count, 1);
    T_ASSERT_EQ_U(g_log_fmts[0], GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_NULL);
    T_ASSERT_EQ_I(g_log_indices[0], 5);   /* index + 1 */
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    /* No pixels touched. */
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            T_ASSERT_EQ_U(c.pixels[x + y * CANVAS_W], 0x00);
    return 0;
}

int test_gfx_sprite_rle_render_offset_too_low_logs(void)
{
    /* Patch a copy of k_rle_4x2 to have src_offset = 0 — triggers the
     * `< 1` reject. */
    canvas_t c; canvas_init(&c); rle_test_reset();
    install_recorder();

    uint8_t bad[64];
    memcpy(bad, k_rle_4x2, sizeof bad);
    bad[12] = 0; bad[13] = 0; bad[14] = 0; bad[15] = 0;   /* src_offset = 0 */

    gfx_sprite_render_rle_from_blob(bad, /*idx=*/0, 2, 1);

    T_ASSERT_EQ_I(g_log_count, 1);
    T_ASSERT_EQ_U(g_log_fmts[0], GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_INDEX_TOO_LOW);
    T_ASSERT_EQ_I(g_log_indices[0], 1);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

int test_gfx_sprite_rle_render_offset_too_high_logs(void)
{
    canvas_t c; canvas_init(&c); rle_test_reset();
    install_recorder();

    uint8_t bad[64];
    memcpy(bad, k_rle_4x2, sizeof bad);
    /* Set src_offset = 0x4baf1 (one past the max accepted 0x4baf0). */
    bad[12] = 0xf1; bad[13] = 0xba; bad[14] = 0x04; bad[15] = 0x00;

    gfx_sprite_render_rle_from_blob(bad, 0, 2, 1);

    T_ASSERT_EQ_I(g_log_count, 1);
    T_ASSERT_EQ_U(g_log_fmts[0], GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_INDEX_TOO_HIGH);
    T_ASSERT_EQ_I(g_log_indices[0], 1);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

int test_gfx_sprite_rle_render_left_clip(void)
{
    /* Sprite at x = -2 → first 2 pixels of each row clipped off.
     * Visible portion lands at dst x = 0..1. */
    canvas_t c; canvas_init(&c); rle_test_reset();

    gfx_sprite_render_rle_from_blob(k_rle_4x2, 0, /*x=*/-2, /*y=*/0);

    /* Decoded row 0: 11 22 .. ..  → visible (cols 2..3) = .. ..  →
     * dst (0..1, 0) = 00, 00. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x00);
    /* Decoded row 1: .. 33 44 55  → visible (cols 2..3) = 44 55  →
     * dst (0..1, 1) = 44, 55. */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x44);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x55);
    /* Pixels in canvas col 2 must NOT have been clobbered — they'd
     * land outside the sprite's visible portion. */
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_rle_render_right_clip(void)
{
    /* Sprite at x = CANVAS_W - 2 → only first 2 pixels of each row
     * land on canvas. */
    canvas_t c; canvas_init(&c); rle_test_reset();

    gfx_sprite_render_rle_from_blob(k_rle_4x2, 0, /*x=*/CANVAS_W - 2, /*y=*/0);

    /* row 0: 11 22 .. ..  → visible cols 0..1 = 11 22 land at
     * (W-2, 0)..(W-1, 0). */
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 2, 0), 0x11);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, 0), 0x22);
    /* row 1: .. 33 44 55  → visible cols 0..1 = .. 33  → dst
     * (W-2, 1) untouched, (W-1, 1) = 33. */
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 2, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, CANVAS_W - 1, 1), 0x33);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_rle_render_top_skip(void)
{
    /* Place 3-row sprite at y = -1 → top_skip = 1 → row 0 walked but
     * not rendered.  Rows 1..2 land at dst y = 0..1. */
    canvas_t c; canvas_init(&c); rle_test_reset();

    gfx_sprite_render_rle_from_blob(k_rle_8x3, 0, /*x=*/0, /*y=*/-1);

    /* Row 0 (a0..a7) must NOT appear anywhere on canvas. */
    for (int x = 0; x < 8; x++) {
        T_ASSERT(at(&c, x, 0) != (uint8_t)(0xa0 + x));
    }
    /* Row 1 (b0..b7) at dst y = 0. */
    for (int x = 0; x < 8; x++) {
        T_ASSERT_EQ_U(at(&c, x, 0), (uint8_t)(0xb0 + x));
    }
    /* Row 2 (c0..c7) at dst y = 1. */
    for (int x = 0; x < 8; x++) {
        T_ASSERT_EQ_U(at(&c, x, 1), (uint8_t)(0xc0 + x));
    }
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_rle_render_fully_offscreen_no_op(void)
{
    canvas_t c; canvas_init(&c); rle_test_reset();

    /* Far past the right edge — x_clip_mode = 5 → y_clip propagates to
     * 5 → no blitter fires. */
    gfx_sprite_render_rle_from_blob(k_rle_4x2, 0, /*x=*/CANVAS_W + 10, 0);

    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            T_ASSERT_EQ_U(c.pixels[x + y * CANVAS_W], 0x00);
    /* Error latch untouched (only descriptor-validity gates set it). */
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Direct blitter tests — bypass the wrapper to confirm staging.
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_rle_blit_zero_top_skip_helper_is_no_op(void)
{
    /* When top_skip is zero, rle_skip_top_rows should walk zero rows
     * and the blitter renders starting from row 0.  Stage via the
     * dispatcher path: dispatch_sprite_rle does clip_x/clip_y → with
     * cursor_y=0 and bottom=CANVAS_H, top_skip stays zero. */
    canvas_t c; canvas_init(&c); rle_test_reset();

    gfx_sprite_render_rle_from_blob(k_rle_4x2, 0, 0, 0);

    T_ASSERT_EQ_I(gfx_dispatch_top_skip(), 0);
    /* Row 0: 11 22 .. .. at (0..3, 0). */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x11);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x22);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_rle_sets_last_glyph_index(void)
{
    canvas_t c; canvas_init(&c); rle_test_reset();

    /* Pre-set last_glyph_index to a sentinel; the wrapper must
     * overwrite it (with the supplied sprite_index = 0). */
    gfx_dispatch_set_last_glyph_index(99);
    gfx_sprite_render_rle_from_blob(k_rle_4x2, /*idx=*/0, 0, 0);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0);
    return 0;
}

int test_gfx_sprite_rle_blit_unclipped_direct_4x2(void)
{
    /* Drive the unclipped RLE blitter directly with manually staged
     * dispatcher state — no wrapper, no clip setters. */
    canvas_t c; canvas_init(&c); rle_test_reset();

    /* Stage state: cursor, glyph descriptor, clip outputs as if
     * clip_x/clip_y had run with no clipping.  top_skip stays 0
     * (initialised by a do-nothing canvas-fit clip via the wrapper
     * call on a previous test invocation would leave it set — but
     * rle_test_reset doesn't clear g_top_skip.  Run a no-clip
     * clip_y here to drive it to 0 deterministically. */
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(/*src_offset=*/24, /*sprite_w=*/4, /*row_count=*/2);
    gfx_glyph_set_clip(/*clipped_w=*/4, /*src_pad=*/0, /*dst_pad=*/CANVAS_W - 4);
    /* Zero top_skip via clip_y: with cursor_y=0 and h_short=2, clip_y
     * sets top_skip=0 (no top clip). */
    gfx_dispatch_set_glyph_height_short(2);
    gfx_dispatch_set_clip_x_mode(0);
    gfx_dispatch_clip_y(0, CANVAS_H);
    /* clip_y rewrites glyph_h with visible-row count.  Re-stage if
     * needed — visible rows == 2, same as row_count above, so the
     * setter wrote 2. */

    gfx_sprite_rle_blit(k_rle_4x2);

    /* Row 0: 11 22 .. .. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x11);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x22);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 0), 0x00);
    /* Row 1: .. 33 44 55 */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x33);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0x44);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x55);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_rle_blit_null_canvas_no_crash(void)
{
    rle_test_reset();
    gfx_paint_bind_canvas(0, 0, 0);
    /* All three blitters must short-circuit when the canvas pointer
     * is NULL — same shape as the gfx_sprite_blit unbound-canvas
     * guard. */
    gfx_sprite_rle_blit(k_rle_4x2);
    gfx_sprite_rle_blit_left_clipped(k_rle_4x2);
    gfx_sprite_rle_blit_right_clipped(k_rle_4x2);
    return 0;
}
