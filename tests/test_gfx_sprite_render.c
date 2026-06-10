/*
 * Unit tests for gfx_sprite_render — the five sibling-of-dispatcher
 * sprite-render wrappers.  Coverage matrix:
 *
 *   - raw buffer (no descriptor): pixels-at-dest, NULL early-out
 *   - caller blob: success, NULL blob → log + error 4,
 *     invalid src_offset (high + low) → log + error 4
 *   - global blob: bind/read, success, error gates
 *   - truncate_bottom: visible-rows shrunk, last row absent
 *   - truncate_top: source skipped, dst shifted, visible-rows shrunk
 *   - error latch sticky behavior (across calls)
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_sprite.h"
#include "gfx_dispatch.h"
#include "gfx_sprite_render.h"

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
 * Sprite blob fixtures — same descriptor layout as gfx_dispatch_sprite
 * tests, but with a few invalid-offset descriptors for the gate tests.
 * ──────────────────────────────────────────────────────────────── */

/* Single 3×2 descriptor at slot 0; src_offset = 24 (just past the
 * 24-byte header + descriptor block).
 *   Layout: [8 header][16 descriptor][6 pixel]
 */
static const uint8_t k_blob_one_3x2[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* header */
    /* descriptor slot 0 */
    0x03, 0x00, 0x02, 0x00,                            /* w=3, h=2 */
    0x18, 0x00, 0x00, 0x00,                            /* src_offset = 24 */
    0x00, 0x00, 0x00, 0x00,                            /* padding */
    0x00, 0x02, 0x00, 0x00,                            /* flag_a, row_count, flag_b, filler */
    /* pixel data */
    0xa1, 0x00, 0xa3,
    0x00, 0xb2, 0x00,
    0xff, 0xff,
};

/* Blob with TWO descriptors:
 *   slot 0: invalid (src_offset = 0) — should fail the "< 1" gate
 *   slot 1: invalid (src_offset = 0x4baf1) — should fail the "> 0x4baf0" gate
 */
static const uint8_t k_blob_two_invalid[64] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* header */
    /* slot 0: src_offset = 0 */
    0x03, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00,                            /* offset = 0 (too low) */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00,
    /* slot 1: src_offset = 0x4baf1 (too high) */
    0x03, 0x00, 0x02, 0x00,
    0xf1, 0xba, 0x04, 0x00,                            /* offset = 0x4baf1 */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00,
};

/* A tall 3×4 sprite for the truncate tests.  Visible h = 4. */
static const uint8_t k_blob_one_3x4[40] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x04, 0x00,                            /* w=3, h=4 */
    0x18, 0x00, 0x00, 0x00,                            /* src_offset = 24 */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00,
    /* pixel rows: each row is 3 bytes */
    0xa1, 0xa2, 0xa3,    /* row 0 */
    0xb1, 0xb2, 0xb3,    /* row 1 */
    0xc1, 0xc2, 0xc3,    /* row 2 */
    0xd1, 0xd2, 0xd3,    /* row 3 */
    0xff, 0xff, 0xff, 0xff,
};

/* ────────────────────────────────────────────────────────────────
 * Log hook recorder — tests install this to inspect the log calls.
 * ──────────────────────────────────────────────────────────────── */

#define LOG_CAP 8
static struct {
    uint32_t fmt_addr;
    int      idx_plus_one;
} g_log[LOG_CAP];
static int g_log_n;

static void log_recorder(uint32_t fmt_addr, int idx_plus_one)
{
    if (g_log_n < LOG_CAP) {
        g_log[g_log_n].fmt_addr     = fmt_addr;
        g_log[g_log_n].idx_plus_one = idx_plus_one;
        g_log_n++;
    }
}

static void reset_log_and_error(void)
{
    g_log_n = 0;
    gfx_sprite_render_set_log(log_recorder);
    gfx_sprite_render_clear_error();
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_render_raw — FUN_0040a01d
 * ──────────────────────────────────────────────────────────────── */

/* A raw buffer with 3-wide × 2-row data — densely packed. */
static const uint8_t k_raw_3x2[8] = {
    0x11, 0x00, 0x33,
    0x00, 0x55, 0x00,
    0xff, 0xff,
};

int test_gfx_sprite_render_raw_paints_at_dest(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_raw(k_raw_3x2, /*x=*/4, /*y=*/2, /*w=*/3, /*h=*/2);

    T_ASSERT_EQ_U(at(&c, 4, 2), 0x11);
    T_ASSERT_EQ_U(at(&c, 5, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 6, 2), 0x33);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x00);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x55);
    T_ASSERT_EQ_U(at(&c, 6, 3), 0x00);
    /* Surrounding pixels untouched. */
    T_ASSERT_EQ_U(at(&c, 3, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 7, 3), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    /* No error: error code stays at 0, log empty. */
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 0);
    T_ASSERT_EQ_I(g_log_n, 0);
    return 0;
}

int test_gfx_sprite_render_raw_null_buffer_no_op(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    /* Paint something first so we can detect a wrongful overwrite. */
    c.pixels[0] = 0x42;
    gfx_sprite_render_raw(/*buffer=*/0, /*x=*/0, /*y=*/0, /*w=*/3, /*h=*/2);
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x42);
    /* No error log either. */
    T_ASSERT_EQ_I(g_log_n, 0);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 0);
    return 0;
}

int test_gfx_sprite_render_raw_sets_last_index_zero(void)
{
    /* The raw wrapper writes the last-glyph-index global to 0
     * unconditionally.  Pre-poke it to a nonzero value and confirm
     * the wrapper clears it. */
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_dispatch_set_last_glyph_index(7);
    gfx_sprite_render_raw(k_raw_3x2, 0, 0, 3, 2);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_render_from_blob — FUN_0040a21a
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_render_from_blob_paints_at_dest(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_from_blob(k_blob_one_3x2, /*idx=*/0, /*x=*/2, /*y=*/1);

    T_ASSERT_EQ_U(at(&c, 2, 1), 0xa1);
    T_ASSERT_EQ_U(at(&c, 3, 1), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0xa3);
    T_ASSERT_EQ_U(at(&c, 2, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0xb2);
    T_ASSERT_EQ_U(at(&c, 4, 2), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 0);
    T_ASSERT_EQ_I(g_log_n, 0);
    /* last-glyph-index latched to the requested index. */
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0);
    return 0;
}

int test_gfx_sprite_render_from_blob_null_logs_and_latches(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_from_blob(/*blob=*/0, /*idx=*/2, 0, 0);
    /* No painting. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x00);
    /* Log + latch. */
    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_FROM_BLOB_NULL);
    T_ASSERT_EQ_I(g_log[0].idx_plus_one, 3);  /* idx + 1 */
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

int test_gfx_sprite_render_from_blob_offset_too_low_logs_and_latches(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    /* k_blob_two_invalid[0] has src_offset = 0 → triggers the "< 1" gate. */
    gfx_sprite_render_from_blob(k_blob_two_invalid, 0, 4, 2);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_LOW);
    T_ASSERT_EQ_I(g_log[0].idx_plus_one, 1);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    /* Nothing painted. */
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            T_ASSERT_EQ_U(c.pixels[x + y * CANVAS_W], 0);
    return 0;
}

int test_gfx_sprite_render_from_blob_offset_too_high_logs_and_latches(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    /* slot 1 has src_offset = 0x4baf1 → "> 0x4baf0" gate. */
    gfx_sprite_render_from_blob(k_blob_two_invalid, 1, 4, 2);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_HIGH);
    T_ASSERT_EQ_I(g_log[0].idx_plus_one, 2);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

int test_gfx_sprite_render_from_blob_invalid_still_stages_descriptor(void)
{
    /* The "high" and "low" gates fire AFTER the descriptor is staged.
     * Subsequent reads should see the staged state. */
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_from_blob(k_blob_two_invalid, 1, 4, 2);
    /* Slot 1 has w=3, h=4 in our fixture — but the actual h short bytes
     * are (0x02, 0x00) — h = 2.  Verify the staged glyph_w. */
    T_ASSERT_EQ_I(gfx_glyph_glyph_w(), 3);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_render — FUN_0040a682 (global blob)
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_render_global_paints_at_dest(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_one_3x2);
    gfx_sprite_render(/*idx=*/0, /*x=*/2, /*y=*/1);

    T_ASSERT_EQ_U(at(&c, 2, 1), 0xa1);
    T_ASSERT_EQ_U(at(&c, 4, 1), 0xa3);
    T_ASSERT_EQ_U(at(&c, 3, 2), 0xb2);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 0);
    T_ASSERT_EQ_I(g_log_n, 0);
    return 0;
}

int test_gfx_sprite_render_global_bind_round_trip(void)
{
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_one_3x2);
    T_ASSERT(gfx_sprite_render_blob() == k_blob_one_3x2);
    gfx_sprite_render_set_blob(0);
    T_ASSERT(gfx_sprite_render_blob() == 0);
    return 0;
}

int test_gfx_sprite_render_global_offset_too_low_logs(void)
{
    canvas_t c; canvas_init(&c); (void)c;
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_two_invalid);
    gfx_sprite_render(/*idx=*/0, /*x=*/0, /*y=*/0);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_LOW);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

int test_gfx_sprite_render_global_offset_too_high_logs(void)
{
    canvas_t c; canvas_init(&c); (void)c;
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_two_invalid);
    gfx_sprite_render(/*idx=*/1, /*x=*/0, /*y=*/0);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_HIGH);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_render_truncate_bottom — FUN_0040a80e
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_render_truncate_bottom_trims_last_row(void)
{
    /* h = 4, trim_rows = 1 → visible h = 3.  Bottom (4th) row is
     * absent in the painted output. */
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_one_3x4);
    gfx_sprite_render_truncate_bottom(/*idx=*/0, /*x=*/0, /*y=*/0,
                                      /*trim_rows=*/1);

    /* Rows 0..2 visible. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0xa1);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0xa3);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0xb2);
    T_ASSERT_EQ_U(at(&c, 0, 2), 0xc1);
    /* Row 3 (d1 d2 d3) must NOT be painted. */
    T_ASSERT_EQ_U(at(&c, 0, 3), 0x00);
    T_ASSERT_EQ_U(at(&c, 1, 3), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 3), 0x00);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_render_truncate_bottom_zero_trim_paints_all(void)
{
    /* trim_rows = 0 → identical behavior to gfx_sprite_render. */
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_one_3x4);
    gfx_sprite_render_truncate_bottom(0, 0, 0, /*trim_rows=*/0);

    T_ASSERT_EQ_U(at(&c, 0, 3), 0xd1);
    T_ASSERT_EQ_U(at(&c, 2, 3), 0xd3);
    return 0;
}

int test_gfx_sprite_render_truncate_bottom_error_gates(void)
{
    canvas_t c; canvas_init(&c); (void)c;
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_two_invalid);
    gfx_sprite_render_truncate_bottom(0, 0, 0, 1);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_TRUNCATE_BOTTOM_INDEX_TOO_LOW);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_render_truncate_top — FUN_0040a9b0
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_render_truncate_top_skips_first_row(void)
{
    /* h = 4, skip_rows = 1 → visible h = 3, source starts at row 1,
     * dst_y shifts to y + 1.  Net: rows 1..3 painted at y=1..3. */
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_one_3x4);
    gfx_sprite_render_truncate_top(/*idx=*/0, /*x=*/0, /*y=*/0,
                                   /*skip_rows=*/1);

    /* Row 0 (source's row 0 = a1 a2 a3) must NOT be painted. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x00);
    T_ASSERT_EQ_U(at(&c, 2, 0), 0x00);
    /* y=1 takes source row 1 (b1 b2 b3). */
    T_ASSERT_EQ_U(at(&c, 0, 1), 0xb1);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0xb2);
    T_ASSERT_EQ_U(at(&c, 2, 1), 0xb3);
    /* y=2: row 2. */
    T_ASSERT_EQ_U(at(&c, 0, 2), 0xc1);
    T_ASSERT_EQ_U(at(&c, 2, 2), 0xc3);
    /* y=3: row 3. */
    T_ASSERT_EQ_U(at(&c, 0, 3), 0xd1);
    T_ASSERT_EQ_U(at(&c, 2, 3), 0xd3);
    T_ASSERT(canvas_guards_intact(&c));
    return 0;
}

int test_gfx_sprite_render_truncate_top_zero_skip_paints_all(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_one_3x4);
    gfx_sprite_render_truncate_top(0, 0, 0, /*skip_rows=*/0);

    /* All 4 rows painted at y=0..3. */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0xa1);
    T_ASSERT_EQ_U(at(&c, 0, 3), 0xd1);
    return 0;
}

int test_gfx_sprite_render_truncate_top_error_gates(void)
{
    canvas_t c; canvas_init(&c); (void)c;
    reset_log_and_error();
    gfx_sprite_render_set_blob(k_blob_two_invalid);
    gfx_sprite_render_truncate_top(1, 0, 0, 1);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_TRUNCATE_TOP_INDEX_TOO_HIGH);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Error latch sticky behavior
 * ──────────────────────────────────────────────────────────────── */

int test_gfx_sprite_render_error_latch_sticky_until_cleared(void)
{
    canvas_t c; canvas_init(&c); (void)c;
    reset_log_and_error();
    /* Trip a gate. */
    gfx_sprite_render_from_blob(0, 0, 0, 0);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);

    /* A successful render does NOT clear the latch. */
    gfx_sprite_render_from_blob(k_blob_one_3x2, 0, 0, 0);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);

    /* Explicit clear. */
    gfx_sprite_render_clear_error();
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 0);
    return 0;
}

int test_gfx_sprite_render_log_hook_null_is_no_op(void)
{
    /* Default hook (NULL) must not crash on error.  Reset_log_and_error
     * installs the recorder; explicitly NULL it before the error. */
    canvas_t c; canvas_init(&c); (void)c;
    reset_log_and_error();
    gfx_sprite_render_set_log(0);
    gfx_sprite_render_from_blob(0, 0, 0, 0);
    /* Error latch still set. */
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_render_opaque_from_blob — FUN_0040a567
 *
 * Opaque sibling of gfx_sprite_render_from_blob.  Copies every source
 * byte (zeros included) and bakes dst_stride_pad = 0x280 - width, so
 * the dest row stride is a hardcoded 640 regardless of canvas pitch —
 * paint tests therefore run on a 640-wide canvas.  Same three
 * descriptor gates, using the OPAQUE_FROM_BLOB_* error addresses.
 * ──────────────────────────────────────────────────────────────── */

enum { OPQ_W = 640, OPQ_H = 4 };

int test_gfx_sprite_render_opaque_from_blob_paints_zeros_at_dest(void)
{
    static uint8_t px[OPQ_W * OPQ_H];
    for (int i = 0; i < OPQ_W * OPQ_H; i++) px[i] = 0x99;   /* background */
    gfx_paint_bind_canvas(px, OPQ_W, OPQ_H);
    reset_log_and_error();

    gfx_sprite_render_opaque_from_blob(k_blob_one_3x2, /*idx=*/0, /*x=*/2, /*y=*/1);

    /* row 0: a1 00 a3 — the zero overwrites the 0x99 background
     * (transparent variant would leave it 0x99). */
    T_ASSERT_EQ_U(px[2 + 1 * OPQ_W], 0xa1);
    T_ASSERT_EQ_U(px[3 + 1 * OPQ_W], 0x00);
    T_ASSERT_EQ_U(px[4 + 1 * OPQ_W], 0xa3);
    /* row 1: 00 b2 00 — landed 640 bytes (one row) down. */
    T_ASSERT_EQ_U(px[2 + 2 * OPQ_W], 0x00);
    T_ASSERT_EQ_U(px[3 + 2 * OPQ_W], 0xb2);
    T_ASSERT_EQ_U(px[4 + 2 * OPQ_W], 0x00);
    /* Untouched neighbor keeps the background. */
    T_ASSERT_EQ_U(px[1 + 1 * OPQ_W], 0x99);
    T_ASSERT_EQ_U(px[5 + 1 * OPQ_W], 0x99);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 0);
    T_ASSERT_EQ_I(g_log_n, 0);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0);
    return 0;
}

int test_gfx_sprite_render_opaque_from_blob_null_logs_and_latches(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_opaque_from_blob(/*blob=*/0, /*idx=*/2, 0, 0);

    T_ASSERT_EQ_U(at(&c, 0, 0), 0x00);
    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_OPAQUE_FROM_BLOB_NULL);
    T_ASSERT_EQ_I(g_log[0].idx_plus_one, 3);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

int test_gfx_sprite_render_opaque_from_blob_offset_too_low_logs(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_opaque_from_blob(k_blob_two_invalid, 0, 4, 2);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_OPAQUE_FROM_BLOB_INDEX_TOO_LOW);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            T_ASSERT_EQ_U(c.pixels[x + y * CANVAS_W], 0);
    return 0;
}

int test_gfx_sprite_render_opaque_from_blob_offset_too_high_logs(void)
{
    canvas_t c; canvas_init(&c);
    reset_log_and_error();
    gfx_sprite_render_opaque_from_blob(k_blob_two_invalid, 1, 4, 2);

    T_ASSERT_EQ_I(g_log_n, 1);
    T_ASSERT_EQ_U(g_log[0].fmt_addr,
                  (uint32_t)GFX_SPRITE_RENDER_ERR_OPAQUE_FROM_BLOB_INDEX_TOO_HIGH);
    T_ASSERT_EQ_U(gfx_sprite_render_last_error(), 4);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * gfx_sprite_seal_draw — FUN_0040d1bc
 *
 * The seal blits a FIXED 24×24 raw sprite (hardcoded 640-byte dest
 * stride — quirk #75), so these tests bind a 640-wide canvas like
 * the raw-blit tests in test_gfx_sprite.c.
 * ──────────────────────────────────────────────────────────────── */

enum { SEAL_W = 640, SEAL_H = 32 };

static uint8_t g_seal_canvas[SEAL_W * SEAL_H];

static void seal_canvas_init(void)
{
    memset(g_seal_canvas, 0, sizeof g_seal_canvas);
    gfx_paint_bind_canvas(g_seal_canvas, SEAL_W, SEAL_H);
}

/* Synthetic system sheet: 8-byte header + 0x34 descriptor slots +
 * two 24×24 pixel tiles.  Slot 0x33 points at tile A (every byte
 * 0x33), slot 0x10 at tile B (every byte 0x10). */
static uint8_t g_seal_sheet[8 + 0x34 * 16 + 2 * 576];

static void seal_sheet_init(void)
{
    memset(g_seal_sheet, 0, sizeof g_seal_sheet);
    uint32_t tile_a = 8 + 0x34 * 16;
    uint32_t tile_b = tile_a + 576;
    memset(g_seal_sheet + tile_a, 0x33, 576);
    memset(g_seal_sheet + tile_b, 0x10, 576);

    uint8_t *d33 = g_seal_sheet + 0x33 * 16 + 8;
    d33[0] = 24; d33[2] = 24;                     /* w, h_short */
    d33[4] = (uint8_t)(tile_a & 0xff);
    d33[5] = (uint8_t)(tile_a >> 8);
    d33[0xd] = 24;                                /* row count */

    uint8_t *d10 = g_seal_sheet + 0x10 * 16 + 8;
    d10[0] = 24; d10[2] = 24;
    d10[4] = (uint8_t)(tile_b & 0xff);
    d10[5] = (uint8_t)(tile_b >> 8);
    d10[0xd] = 24;
}

int test_gfx_sprite_seal_draw_mode0_blits_idx_0x33(void)
{
    seal_canvas_init();
    seal_sheet_init();

    gfx_sprite_seal_draw(g_seal_sheet, 12, 4, 0);

    T_ASSERT_EQ_I(gfx_sprite_seal_x(), 12);
    T_ASSERT_EQ_I(gfx_sprite_seal_y(), 4);
    T_ASSERT_EQ_I(gfx_sprite_seal_mode(), 0);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x33);
    /* 24×24 of tile A's bytes at (12, 4). */
    T_ASSERT_EQ_U(g_seal_canvas[12 + 4 * SEAL_W], 0x33);
    T_ASSERT_EQ_U(g_seal_canvas[12 + 23 + (4 + 23) * SEAL_W], 0x33);
    /* One past the extent stays clear. */
    T_ASSERT_EQ_U(g_seal_canvas[12 + 24 + 4 * SEAL_W], 0);
    T_ASSERT_EQ_U(g_seal_canvas[12 + (4 + 24) * SEAL_W], 0);
    return 0;
}

int test_gfx_sprite_seal_draw_mode_nonzero_uses_idx_0x10(void)
{
    seal_canvas_init();
    seal_sheet_init();

    gfx_sprite_seal_draw(g_seal_sheet, 0, 0, 2);

    T_ASSERT_EQ_I(gfx_sprite_seal_mode(), 2);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x10);
    T_ASSERT_EQ_U(g_seal_canvas[0], 0x10);
    return 0;
}

int test_gfx_sprite_seal_draw_null_sheet_sets_state_only(void)
{
    seal_canvas_init();

    gfx_sprite_seal_draw(0, 7, 9, 0);

    T_ASSERT_EQ_I(gfx_sprite_seal_x(), 7);
    T_ASSERT_EQ_I(gfx_sprite_seal_y(), 9);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x33);
    for (int i = 0; i < SEAL_W * SEAL_H; i++)
        T_ASSERT_EQ_U(g_seal_canvas[i], 0);
    return 0;
}
