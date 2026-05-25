/*
 * Unit tests for gfx_dispatch — the glyph dispatcher and clip setters.
 *
 * Tests fall into three groups:
 *
 *   1. clip_x  (FUN_0040464c) — table-driven exercise of all four
 *                                clip-mode outcomes plus stride math.
 *   2. clip_y  (FUN_0040477b) — same shape; plus the X→Y "skip"
 *                                propagation rule.
 *   3. glyph   (FUN_00402a14) — end-to-end dispatch, including the
 *                                default-font character-range tweaks,
 *                                source-pointer fixup after clip, and
 *                                the Y-skip blitter gate.
 *
 * Each test binds a guarded canvas via gfx_paint and stages a
 * synthetic font blob in-place so glyph-descriptor reads target known
 * bytes.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Fixtures
 * ────────────────────────────────────────────────────────────────── */

enum { CANVAS_W = 32, CANVAS_H = 16 };

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

/* Reset all dispatcher state to a known clean baseline so tests can
 * run in any order.  gfx_glyph / gfx_dispatch hold persistent globals;
 * leftover values from a previous test would otherwise leak in. */
static void dispatch_reset(void)
{
    gfx_dispatch_set_default_font(0);
    gfx_dispatch_set_alt_clip(0, 0, 0);
    gfx_dispatch_set_glyph_height_short(0);
    gfx_dispatch_set_clip_x_mode(0);
    gfx_glyph_set_color(0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(0, 0, 0);
    gfx_glyph_set_clip(0, 0, 0);
    gfx_glyph_set_color_modulation(0);
}

/* Build a 256-byte char-glyph table where char_code -> glyph index
 * (1-based, with 0 meaning "no glyph").  Most tests only need one
 * mapping populated. */
static void char_table_init(uint8_t *table, unsigned char ch, uint8_t idx)
{
    memset(table, 0, 256);
    table[ch] = idx;
}

/* Synthetic font blob.  We place a glyph descriptor starting at +8 (the
 * retail layout) and pixel data starting at some byte offset we encode
 * into the source-offset field.
 *
 * Descriptor layout (16 bytes per slot per the dispatcher's reads):
 *   [+0..+1] width   (uint16 LE)
 *   [+2..+3] height  (uint16 LE)
 *   [+4..+7] src_offset (uint32 LE, byte index into this same blob)
 *   [+8..+b] (unread)
 *   [+c]     flag_a
 *   [+d]     advance / initial row count
 *   [+e]     flag_b
 *   [+f]     (unread)
 *
 * Glyph 0's descriptor starts at byte 8 in the blob.  Glyph 1's would
 * start at byte 24 etc., but tests only ever populate glyph 0 (the
 * char-table maps the test char to idx 1, which translates to 0-based
 * glyph index 0).
 *
 * Pixel data follows the descriptor table — for a single glyph, place
 * it at byte 256 to leave plenty of headroom.
 */
enum { FONT_BLOB_LEN = 512, FONT_PIXEL_DATA_OFFSET = 256 };

static void font_blob_init_glyph0(uint8_t *blob,
                                  int w, int h,
                                  uint8_t advance,
                                  const uint8_t *pixels)
{
    memset(blob, 0xff, FONT_BLOB_LEN);
    /* Zero the descriptor region so unread bytes don't bleed into the
     * fields we DO read (defensive — the dispatcher only reads the
     * specific offsets called out above). */
    memset(blob + 8, 0, 16);

    blob[8]  = (uint8_t)(w & 0xff);
    blob[9]  = (uint8_t)((w >> 8) & 0xff);
    blob[10] = (uint8_t)(h & 0xff);
    blob[11] = (uint8_t)((h >> 8) & 0xff);
    blob[12] = (uint8_t)(FONT_PIXEL_DATA_OFFSET & 0xff);
    blob[13] = (uint8_t)((FONT_PIXEL_DATA_OFFSET >> 8) & 0xff);
    blob[14] = 0;
    blob[15] = 0;
    blob[8 + 0xc] = 0x11;          /* flag_a — observable via dispatch */
    blob[8 + 0xd] = advance;
    blob[8 + 0xe] = 0x22;          /* flag_b */

    if (pixels) {
        for (int i = 0; i < w * h; i++) {
            blob[FONT_PIXEL_DATA_OFFSET + i] = pixels[i];
        }
    }
}

/* ──────────────────────────────────────────────────────────────────
 * clip_x — horizontal clip setter (FUN_0040464c)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_dispatch_clip_x_no_clip_fully_inside(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    gfx_glyph_set_glyph(0, 5, 0);            /* glyph_w = 5 */
    gfx_glyph_set_dest(10, 0);               /* cursor_x = 10 */

    gfx_dispatch_clip_x(0, CANVAS_W);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 0);
    T_ASSERT_EQ_I(gfx_dispatch_left_skip(), 0);
    /* clipped_w = glyph_w = 5; src_pad = 0; dst_pad = canvas - 5 */
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 10);
    return 0;
}

int test_gfx_dispatch_clip_x_zero_width_skips(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    gfx_glyph_set_glyph(0, 0, 0);
    gfx_glyph_set_dest(5, 0);

    gfx_dispatch_clip_x(0, CANVAS_W);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 5);
    return 0;
}

int test_gfx_dispatch_clip_x_left_clip_shifts_cursor(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    /* glyph_w=8 at cursor_x=-3 → left edge of glyph at -3, right at 5.
     * Clip rect [0, 32) → left-clip with skip=3, cursor moves to 0. */
    gfx_glyph_set_glyph(0, 8, 0);
    gfx_glyph_set_dest(-3, 0);

    gfx_dispatch_clip_x(0, CANVAS_W);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 1);
    T_ASSERT_EQ_I(gfx_dispatch_left_skip(), 3);
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 0);
    return 0;
}

int test_gfx_dispatch_clip_x_fully_left_offscreen_skips(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    /* glyph_w=5 at cursor_x=-10 → right edge at -5; entirely left of
     * the clip rect.  Expect mode 5. */
    gfx_glyph_set_glyph(0, 5, 0);
    gfx_glyph_set_dest(-10, 0);

    gfx_dispatch_clip_x(0, CANVAS_W);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 5);
    return 0;
}

int test_gfx_dispatch_clip_x_right_clip_narrows_visible(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    /* glyph_w=8 at cursor_x=28 → right edge at 36, past 32.  Visible
     * portion is 32-28 = 4 cols. */
    gfx_glyph_set_glyph(0, 8, 0);
    gfx_glyph_set_dest(28, 0);

    gfx_dispatch_clip_x(0, CANVAS_W);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 2);
    T_ASSERT_EQ_I(gfx_dispatch_left_skip(), 0);
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 28);    /* unchanged */
    return 0;
}

int test_gfx_dispatch_clip_x_fully_right_offscreen_skips(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    gfx_glyph_set_glyph(0, 5, 0);
    gfx_glyph_set_dest(40, 0);                  /* past right edge */

    gfx_dispatch_clip_x(0, CANVAS_W);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 5);
    return 0;
}

int test_gfx_dispatch_clip_x_alt_clip_rect_used(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    /* glyph_w=4 at cursor_x=8.  Alt-clip [10, 20) → left-clip with
     * skip=2, cursor → 10. */
    gfx_glyph_set_glyph(0, 4, 0);
    gfx_glyph_set_dest(8, 0);

    gfx_dispatch_clip_x(10, 20);

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 1);
    T_ASSERT_EQ_I(gfx_dispatch_left_skip(), 2);
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 10);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * clip_y — vertical clip setter (FUN_0040477b)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_dispatch_clip_y_no_clip_fully_inside(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    gfx_dispatch_set_clip_x_mode(0);            /* X "no clip" */
    gfx_dispatch_set_glyph_height_short(8);
    gfx_glyph_set_dest(0, 4);

    gfx_dispatch_clip_y(0, CANVAS_H);

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 0);
    T_ASSERT_EQ_I(gfx_dispatch_top_skip(), 0);
    T_ASSERT_EQ_I(gfx_glyph_glyph_h(), 8);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 4);
    return 0;
}

int test_gfx_dispatch_clip_y_zero_height_skips(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    gfx_dispatch_set_glyph_height_short(0);
    gfx_glyph_set_dest(0, 0);

    gfx_dispatch_clip_y(0, CANVAS_H);

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 5);
    T_ASSERT_EQ_I(gfx_glyph_glyph_h(), 0);
    return 0;
}

int test_gfx_dispatch_clip_y_top_clip_shifts_cursor(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    /* h_short=6 at cursor_y=-2 → top-clip with skip=2; visible 4 rows. */
    gfx_dispatch_set_glyph_height_short(6);
    gfx_glyph_set_dest(0, -2);

    gfx_dispatch_clip_y(0, CANVAS_H);

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 3);
    T_ASSERT_EQ_I(gfx_dispatch_top_skip(), 2);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0);
    T_ASSERT_EQ_I(gfx_glyph_glyph_h(), 4);
    return 0;
}

int test_gfx_dispatch_clip_y_bottom_clip_narrows_visible(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    /* h_short=8 at cursor_y=12 → bottom-clip; visible 16-12=4 rows. */
    gfx_dispatch_set_glyph_height_short(8);
    gfx_glyph_set_dest(0, 12);

    gfx_dispatch_clip_y(0, CANVAS_H);

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 4);
    T_ASSERT_EQ_I(gfx_dispatch_top_skip(), 0);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 12);   /* unchanged */
    T_ASSERT_EQ_I(gfx_glyph_glyph_h(), 4);
    return 0;
}

int test_gfx_dispatch_clip_y_fully_below_skips(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    gfx_dispatch_set_glyph_height_short(4);
    gfx_glyph_set_dest(0, 30);

    gfx_dispatch_clip_y(0, CANVAS_H);

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 5);
    T_ASSERT_EQ_I(gfx_glyph_glyph_h(), 0);
    return 0;
}

int test_gfx_dispatch_clip_y_propagates_x_skip(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    /* X-clip already said "skip"; clip_y should write 5 even when its
     * own inputs would normally yield "no clip". */
    gfx_dispatch_set_clip_x_mode(5);
    gfx_dispatch_set_glyph_height_short(4);
    gfx_glyph_set_dest(0, 4);                   /* inside canvas */

    gfx_dispatch_clip_y(0, CANVAS_H);

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 5);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Dispatcher — end-to-end glyph (FUN_00402a14)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_dispatch_glyph_missing_char_returns_zero(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    uint8_t table[256];
    memset(blob, 0, sizeof blob);
    memset(table, 0, sizeof table);             /* every char unmapped */

    int adv = gfx_dispatch_glyph(blob, table, 'X');

    T_ASSERT_EQ_I(adv, 0);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0);
    return 0;
}

int test_gfx_dispatch_glyph_null_table_treats_as_missing(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    memset(blob, 0, sizeof blob);

    int adv = gfx_dispatch_glyph(blob, 0, 'X');

    T_ASSERT_EQ_I(adv, 0);
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0);
    return 0;
}

int test_gfx_dispatch_glyph_returns_glyph_w_plus_one(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[3 * 2] = { 1,2,3, 4,5,6 };
    font_blob_init_glyph0(blob, 3, 2, 2, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    int adv = gfx_dispatch_glyph(blob, table, 'A');

    T_ASSERT_EQ_I(adv, 4);                      /* 3 + 1 */
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0);
    return 0;
}

int test_gfx_dispatch_glyph_advances_cursor_y_by_advance_byte(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    /* 1×1 pixel glyph, advance byte = 5. */
    static const uint8_t pixels[1] = { 0x42 };
    font_blob_init_glyph0(blob, 1, 1, 5, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_glyph_set_dest(0, 3);
    gfx_dispatch_glyph(blob, table, 'A');

    /* cursor_y was 3, advance byte added 5 → expect 8. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 8);
    return 0;
}

int test_gfx_dispatch_glyph_default_font_tweaks_lowercase_range(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[1] = { 0x10 };
    font_blob_init_glyph0(blob, 1, 1, 4, pixels);

    uint8_t table[256];
    char_table_init(table, 'a', 1);             /* 0x61 — in [0x61..0x6d] */

    /* Register THIS blob as the default font. */
    gfx_dispatch_set_default_font(blob);

    gfx_glyph_set_dest(0, 0);
    gfx_dispatch_glyph(blob, table, 'a');

    /* Advance byte = 4; cursor_y starts at 0 → 0 + 4 = 4; -1 for the
     * [0x61..0x6d] tweak → 3. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 3);
    return 0;
}

int test_gfx_dispatch_glyph_default_font_tweaks_skipped_outside_range(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[1] = { 0x10 };
    font_blob_init_glyph0(blob, 1, 1, 4, pixels);

    uint8_t table[256];
    /* 0x70 'p' — outside [0x61..0x6d], outside [0x73..0x77]. */
    char_table_init(table, 'p', 1);

    gfx_dispatch_set_default_font(blob);

    gfx_glyph_set_dest(0, 0);
    gfx_dispatch_glyph(blob, table, 'p');

    /* No tweak — cursor_y = 4. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 4);
    return 0;
}

int test_gfx_dispatch_glyph_non_default_font_skips_tweaks(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[1] = { 0x10 };
    font_blob_init_glyph0(blob, 1, 1, 4, pixels);

    uint8_t table[256];
    char_table_init(table, 'a', 1);

    /* Default font is some OTHER blob — pass NULL and dispatch the
     * test blob; no tweak should apply. */
    gfx_dispatch_set_default_font(0);

    gfx_glyph_set_dest(0, 0);
    gfx_dispatch_glyph(blob, table, 'a');

    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 4);     /* no tweak */
    return 0;
}

int test_gfx_dispatch_glyph_default_font_high_byte_tweak(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[1] = { 0x10 };
    font_blob_init_glyph0(blob, 1, 1, 4, pixels);

    uint8_t table[256];
    char_table_init(table, 0x82, 1);            /* in [0x80..0x84] */

    gfx_dispatch_set_default_font(blob);

    gfx_glyph_set_dest(0, 0);
    gfx_dispatch_glyph(blob, table, 0x82);

    /* 0 + 4 - 1 = 3. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 3);
    return 0;
}

int test_gfx_dispatch_glyph_paints_pixels_unclipped(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    /* 2x2 glyph: { 1, 0,
     *              0, 2 }
     * Painted as silhouette (color modulation off) at cursor (4, 2)
     * after advance 0 (so cursor_y stays 2).  Expect 0x42 at
     * (4,2) and (5,3); zeros at (5,2) and (4,3). */
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[2 * 2] = { 1,0, 0,2 };
    font_blob_init_glyph0(blob, 2, 2, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_glyph_set_color(0x42);
    gfx_glyph_set_dest(4, 2);
    gfx_glyph_set_color_modulation(0);

    gfx_dispatch_glyph(blob, table, 'A');

    T_ASSERT_EQ_U(at(&c, 4, 2), 0x42);
    T_ASSERT_EQ_U(at(&c, 5, 2), 0x00);
    T_ASSERT_EQ_U(at(&c, 4, 3), 0x00);
    T_ASSERT_EQ_U(at(&c, 5, 3), 0x42);
    return 0;
}

int test_gfx_dispatch_glyph_y_off_canvas_suppresses_blit(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    /* Place glyph entirely below the canvas — Y-clip should fire skip
     * (mode 5) and no blitter should run.  Verify canvas is untouched. */
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[1] = { 0x10 };
    font_blob_init_glyph0(blob, 1, 1, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_glyph_set_color(0x77);
    gfx_glyph_set_dest(2, CANVAS_H + 5);        /* well below */

    gfx_dispatch_glyph(blob, table, 'A');

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 5);
    /* Spot-check a few pixels — all zero. */
    T_ASSERT_EQ_U(at(&c, 2, 0), 0);
    T_ASSERT_EQ_U(at(&c, 2, CANVAS_H - 1), 0);
    return 0;
}

int test_gfx_dispatch_glyph_x_off_canvas_suppresses_blit(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[1] = { 0x10 };
    font_blob_init_glyph0(blob, 1, 1, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_glyph_set_color(0x77);
    gfx_glyph_set_dest(CANVAS_W + 5, 2);

    gfx_dispatch_glyph(blob, table, 'A');

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 5);
    /* Y-clip also skips via propagation, so blitter doesn't run. */
    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 5);
    return 0;
}

int test_gfx_dispatch_glyph_left_clipped_paints_visible_columns(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    /* 4-wide glyph at cursor_x=-2 → left-clip with skip=2.  Visible
     * portion is rightmost 2 columns. */
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[4] = { 1, 2, 3, 4 };  /* 4x1 */
    font_blob_init_glyph0(blob, 4, 1, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_glyph_set_color(0x55);
    gfx_glyph_set_dest(-2, 0);
    gfx_glyph_set_color_modulation(0);

    gfx_dispatch_glyph(blob, table, 'A');

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 1);
    T_ASSERT_EQ_I(gfx_dispatch_left_skip(), 2);
    /* cursor moved to 0; 2 visible cols painted with color (non-zero
     * source). */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x55);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x55);
    return 0;
}

int test_gfx_dispatch_glyph_top_clipped_paints_visible_rows(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    /* 1x4 glyph (1 col, 4 rows) at cursor_y=-1 → top-clip, skip=1.
     * Visible 3 rows. */
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[4] = { 1, 2, 3, 4 };  /* 1x4 */
    font_blob_init_glyph0(blob, 1, 4, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_glyph_set_color(0x66);
    gfx_glyph_set_dest(0, -1);
    gfx_glyph_set_color_modulation(0);

    gfx_dispatch_glyph(blob, table, 'A');

    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 3);
    T_ASSERT_EQ_I(gfx_dispatch_top_skip(), 1);
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x66);
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x66);
    T_ASSERT_EQ_U(at(&c, 0, 2), 0x66);
    /* No fourth row. */
    T_ASSERT_EQ_U(at(&c, 0, 3), 0x00);
    return 0;
}

int test_gfx_dispatch_glyph_alt_clip_rect_used_when_enabled(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    /* Alt clip: x ∈ [10, 0x1de), y ∈ [0x18, 30).  Cursor at (5, 0x18)
     * with glyph_w=4 → left-clip (cursor < 10), skip = 5. */
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[4] = { 1, 2, 3, 4 };
    font_blob_init_glyph0(blob, 4, 1, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_dispatch_set_alt_clip(1, 10, 30);
    gfx_glyph_set_color(0x88);
    gfx_glyph_set_dest(5, 0x18);

    gfx_dispatch_glyph(blob, table, 'A');

    /* glyph_w=4, cursor=5; alt-clip left=10 → 10 > 5 AND 10 < 5+4? No,
     * 10 < 9 is false → entirely-left-of-clip → mode 5. */
    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 5);
    return 0;
}

int test_gfx_dispatch_glyph_alt_clip_inside_rect_draws(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    /* Cursor inside the alt rect → no-clip, draws normally. */
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[1] = { 1 };
    font_blob_init_glyph0(blob, 1, 1, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    /* Make the alt rect span most of the canvas so (5, 0x18) is well
     * inside and the right edge (0x1de) is far away.  The canvas is
     * only 32 wide; ALT_RIGHT = 0x1de = 478 is enormous so right-clip
     * never triggers from that side.  The dst pixel needs to land
     * inside our actual canvas — set cursor to (5, 0x18) — but our
     * canvas height is 16, so cursor_y 0x18=24 is below canvas. */
    /* Use a larger canvas just for this test by binding it; reuse
     * pixel buffer slot via gfx_paint_bind_canvas. */
    static uint8_t big_pixels[64 * 32];
    memset(big_pixels, 0, sizeof big_pixels);
    gfx_paint_bind_canvas(big_pixels, 64, 32);

    gfx_dispatch_set_alt_clip(1, 4, 30);
    gfx_glyph_set_color(0xaa);
    gfx_glyph_set_dest(10, 25);                 /* inside [4,478) × [24,30) */

    gfx_dispatch_glyph(blob, table, 'A');

    T_ASSERT_EQ_I(gfx_dispatch_clip_x_mode(), 0);
    T_ASSERT_EQ_I(gfx_dispatch_clip_y_mode(), 0);
    T_ASSERT_EQ_U(big_pixels[10 + 25 * 64], 0xaa);

    /* Rebind a small canvas to leave gfx_paint in a clean state for
     * the next test (defensive — every test calls canvas_init which
     * rebinds anyway). */
    return 0;
}

int test_gfx_dispatch_glyph_source_offset_baked_with_skips(void)
{
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    /* 4-col × 3-row glyph at cursor (-2, -1) → left-clip skip=2,
     * top-clip skip=1.  Visible 2 cols × 2 rows, painted with the
     * BOTTOM-RIGHT 2×2 of the source. */
    uint8_t blob[FONT_BLOB_LEN];
    static const uint8_t pixels[4 * 3] = {
        0x10, 0x11, 0x12, 0x13,        /* row 0 (skipped) */
        0x20, 0x21, 0x22, 0x23,        /* row 1 (visible — cols 2,3) */
        0x30, 0x31, 0x32, 0x33,        /* row 2 (visible) */
    };
    font_blob_init_glyph0(blob, 4, 3, 0, pixels);

    uint8_t table[256];
    char_table_init(table, 'A', 1);

    gfx_glyph_set_color(0x99);
    gfx_glyph_set_dest(-2, -1);
    gfx_glyph_set_color_modulation(0);

    gfx_dispatch_glyph(blob, table, 'A');

    /* All four expected visible pixels are non-zero in source, so they
     * paint with color 0x99 at the canvas origin (0,0..1,1). */
    T_ASSERT_EQ_U(at(&c, 0, 0), 0x99);
    T_ASSERT_EQ_U(at(&c, 1, 0), 0x99);
    T_ASSERT_EQ_U(at(&c, 0, 1), 0x99);
    T_ASSERT_EQ_U(at(&c, 1, 1), 0x99);
    return 0;
}

int test_gfx_dispatch_glyph_table_zero_at_high_byte(void)
{
    /* High-byte char_code (>= 0x80) with a zero table entry — still
     * "no glyph", just like the low-byte case.  This catches a sign-
     * extension regression in the byte table indexing. */
    canvas_t c; canvas_init(&c);
    dispatch_reset();

    uint8_t blob[FONT_BLOB_LEN];
    memset(blob, 0xff, sizeof blob);            /* would crash if read */
    uint8_t table[256];
    memset(table, 0, sizeof table);

    int adv = gfx_dispatch_glyph(blob, table, 0xff);

    T_ASSERT_EQ_I(adv, 0);
    return 0;
}
