/*
 * Unit tests for gfx_text_label — the int-formatter + label-render
 * cluster.
 *
 * Three groups:
 *   1. Formatter tests — exercise format_dec / format_hex by peeking
 *      at the shared buffer.  No rendering involved.
 *   2. Render-wrapper tests — bind a canvas + synthetic font, call
 *      the render entry points, observe the assembled buffer (via
 *      `gfx_text_label_buffer()`) and the resulting pixel counts.
 *   3. Quirk witnesses — assertions that pin down quirks #41–#45
 *      (prefix-overwrite, large-value silence, embedded NUL, etc.).
 */

#include "t.h"
#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_paint.h"
#include "gfx_text.h"
#include "gfx_text_label.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Test fixture — synthetic font + small canvas (mirrors test_gfx_text.c)
 * ────────────────────────────────────────────────────────────────── */

enum { CANVAS_W = 128, CANVAS_H = 32 };

typedef struct {
    uint8_t guard_top[CANVAS_W];
    uint8_t pixels[CANVAS_W * CANVAS_H];
    uint8_t guard_bot[CANVAS_W];
} canvas_t;

static void canvas_init(canvas_t *c)
{
    memset(c->guard_top, 0xee, sizeof c->guard_top);
    memset(c->pixels,    0x00, sizeof c->pixels);
    memset(c->guard_bot, 0xee, sizeof c->guard_bot);
    gfx_paint_bind_canvas(c->pixels, CANVAS_W, CANVAS_H);
}

static int count_pixels(const canvas_t *c, uint8_t color)
{
    int n = 0;
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        if (c->pixels[i] == color) n++;
    }
    return n;
}

enum { FONT_BLOB_LEN = 512, PIXEL_BASE = 256 };

/* Single-mapping font where ASCII char `ch` paints a 1x1 pixel.
 * Other chars map to "no glyph" (table entry 0 → renderer advances
 * cursor by 4 px but paints nothing). */
static void font_blob_init_1x1(uint8_t *blob, uint8_t fill, uint8_t advance)
{
    memset(blob, 0, FONT_BLOB_LEN);
    blob[8]  = 1; blob[9]  = 0;   /* width  = 1 */
    blob[10] = 1; blob[11] = 0;   /* height = 1 */
    blob[12] = PIXEL_BASE & 0xff;
    blob[13] = (PIXEL_BASE >> 8) & 0xff;
    blob[14] = 0;
    blob[15] = 0;
    blob[8 + 0xc] = 0;
    blob[8 + 0xd] = advance;
    blob[8 + 0xe] = 0;
    blob[PIXEL_BASE] = fill;
}

/* Map a single ASCII char to glyph index 1.  Table is 0x20-offset. */
static void table_init_single_mapping(uint8_t *table, uint8_t ch, uint8_t glyph_idx)
{
    memset(table, 0, 256);
    table[(uint8_t)(ch - 0x20)] = glyph_idx;
}

/* Reset every globals-holding module so tests run in any order.
 * Mirrors test_gfx_text.c's text_reset(). */
static void label_reset(void)
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

    gfx_text_set_no_shadow(1);   /* simple 1-pass path */
    gfx_text_set_outline(0);
    gfx_text_set_tight_shadow(0);
    gfx_text_set_uppercase_highlight(0);
    gfx_text_set_collapse_spaces(0);
    gfx_text_set_screen_byte(0);
    gfx_text_load_substitution_stream("");
    gfx_text_enable_substitution(0);
    gfx_text_set_first_pos_target(-1);
    gfx_text_clear_first_pos_latch();
    gfx_text_set_line_x_acc(0);
    gfx_text_set_line_y_acc(0);
    gfx_text_set_char_counter(0);

    /* Pre-stuff a few "post-100" bytes so we can verify the formatter
     * doesn't reach past byte 99 (quirk #43). */
    uint8_t *buf = gfx_text_label_buffer();
    for (size_t i = 0; i < gfx_text_label_buffer_size(); i++) {
        buf[i] = 0;
    }
}

/* Read assembled buffer as a C string for assertion purposes. */
static const char *buf_as_string(void)
{
    return (const char *)gfx_text_label_buffer();
}

/* ──────────────────────────────────────────────────────────────────
 * Group 1 — Decimal formatter (FUN_004022bd)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_label_format_dec_zero(void)
{
    label_reset();
    gfx_text_label_format_dec(0, 0, 0);
    /* "0\0..." — single digit, no sign. */
    T_ASSERT_EQ_S(buf_as_string(), "0");
    return 0;
}

int test_gfx_text_label_format_dec_single_digit(void)
{
    label_reset();
    gfx_text_label_format_dec(7, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "7");
    return 0;
}

int test_gfx_text_label_format_dec_multi_digit(void)
{
    label_reset();
    gfx_text_label_format_dec(12345, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "12345");
    return 0;
}

int test_gfx_text_label_format_dec_negative(void)
{
    label_reset();
    gfx_text_label_format_dec(-42, 0, 0);
    /* '-' at [0]; digits at [1..2]. */
    T_ASSERT_EQ_S(buf_as_string(), "-42");
    return 0;
}

int test_gfx_text_label_format_dec_with_plus(void)
{
    label_reset();
    gfx_text_label_format_dec(42, 0, 1);
    /* '+' at [0]; digits at [1..2]. */
    T_ASSERT_EQ_S(buf_as_string(), "+42");
    return 0;
}

int test_gfx_text_label_format_dec_with_plus_zero(void)
{
    label_reset();
    gfx_text_label_format_dec(0, 0, 1);
    /* '+' is emitted for non-negative values when with_plus=1, INCLUDING
     * zero (since `0 < 0` is false → else-if branch fires). */
    T_ASSERT_EQ_S(buf_as_string(), "+0");
    return 0;
}

int test_gfx_text_label_format_dec_offset(void)
{
    label_reset();
    /* offset=1 leaves buffer[0] for the caller-supplied prefix char. */
    gfx_text_label_format_dec(42, 1, 0);
    T_ASSERT_EQ_U(gfx_text_label_buffer()[0], 0);  /* untouched */
    T_ASSERT_EQ_U(gfx_text_label_buffer()[1], '4');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[2], '2');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[3], 0);  /* zeroed */
    return 0;
}

/* Quirk #42 witness — values >= 10^9 emit ZERO digits. */
int test_gfx_text_label_format_dec_large_value_silent(void)
{
    label_reset();
    gfx_text_label_format_dec(1000000000, 0, 0);
    /* Sign-free, no digits — buffer is all zeros. */
    T_ASSERT_EQ_U(gfx_text_label_buffer()[0], 0);
    T_ASSERT_EQ_U(gfx_text_label_buffer()[1], 0);
    T_ASSERT_EQ_S(buf_as_string(), "");
    return 0;
}

/* Quirk #42 witness — large negative emits sign but no digits. */
int test_gfx_text_label_format_dec_large_negative_silent(void)
{
    label_reset();
    gfx_text_label_format_dec(-1000000000, 0, 0);
    /* '-' at [0], no digits. */
    T_ASSERT_EQ_S(buf_as_string(), "-");
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Group 2 — Hex formatter (FUN_00402447)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_label_format_hex_zero(void)
{
    label_reset();
    gfx_text_label_format_hex(0, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "0");
    return 0;
}

int test_gfx_text_label_format_hex_low_nibble(void)
{
    label_reset();
    gfx_text_label_format_hex(0xa, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "A");
    return 0;
}

int test_gfx_text_label_format_hex_multi_digit(void)
{
    label_reset();
    gfx_text_label_format_hex(0x1a2b, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "1A2B");
    return 0;
}

int test_gfx_text_label_format_hex_uppercase_digits(void)
{
    label_reset();
    gfx_text_label_format_hex(0xff, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "FF");
    return 0;
}

int test_gfx_text_label_format_hex_negative(void)
{
    label_reset();
    gfx_text_label_format_hex(-0x42, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "-42");
    return 0;
}

/* Quirk #42 witness — values >= 0x10000000 emit ZERO digits. */
int test_gfx_text_label_format_hex_large_value_silent(void)
{
    label_reset();
    gfx_text_label_format_hex(0x10000000, 0, 0);
    T_ASSERT_EQ_S(buf_as_string(), "");
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Group 3 — Buffer pre-zero scope (quirk #43)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_label_format_only_zeros_first_100_bytes(void)
{
    label_reset();
    /* Pre-stuff bytes 50, 99, 100, 200 with sentinels. */
    uint8_t *buf = gfx_text_label_buffer();
    buf[50]  = 0xaa;
    buf[99]  = 0xbb;
    buf[100] = 0xcc;
    buf[200] = 0xdd;

    gfx_text_label_format_dec(42, 0, 0);

    /* First 100 bytes are zeroed (except where digits were written). */
    T_ASSERT_EQ_U(buf[50],  0);
    T_ASSERT_EQ_U(buf[99],  0);
    /* Bytes 100+ are UNTOUCHED — quirk #43. */
    T_ASSERT_EQ_U(buf[100], 0xcc);
    T_ASSERT_EQ_U(buf[200], 0xdd);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Group 4 — Render wrappers (assembled buffer + pixel observation)
 * ────────────────────────────────────────────────────────────────── */

/* Verify render_dec assembles "<prefix><digits><suffix>" in the buffer. */
int test_gfx_text_label_render_dec_assembles_buffer(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; table_init_single_mapping(table, 'X', 1);

    gfx_text_label_render_dec(42, ' ', " gold", 5, 5, font, table, 7);

    /* Buffer should be " 42 gold\0". */
    T_ASSERT_EQ_S(buf_as_string(), " 42 gold");
    return 0;
}

/* Quirk #41 witness — `prefix` OVERWRITES buffer[0] (it doesn't
 * prepend).  With offset=1, the formatter wrote digits at [1..],
 * leaving [0] as 0.  The prefix is dropped INTO [0]. */
int test_gfx_text_label_render_dec_prefix_overwrites_byte_0(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; table_init_single_mapping(table, 'X', 1);

    gfx_text_label_render_dec(99, '@', "", 0, 0, font, table, 7);

    /* '@' at [0], '9' '9' at [1..2], terminator at [3]. */
    T_ASSERT_EQ_U(gfx_text_label_buffer()[0], '@');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[1], '9');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[2], '9');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[3], 0);
    return 0;
}

/* Zero prefix → buffer[0] stays as the formatter left it (0). */
int test_gfx_text_label_render_dec_zero_prefix_skips_overwrite(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; table_init_single_mapping(table, 'X', 1);

    /* Stuff buffer[0] with a sentinel first — formatter zeros first
     * 100 bytes so it should be cleared, but the no-prefix path
     * shouldn't restore the sentinel. */
    gfx_text_label_render_dec(7, 0, "", 0, 0, font, table, 7);
    T_ASSERT_EQ_U(gfx_text_label_buffer()[0], 0);
    T_ASSERT_EQ_U(gfx_text_label_buffer()[1], '7');
    return 0;
}

/* Verify render_hex assembles uppercase hex digits. */
int test_gfx_text_label_render_hex_assembles_buffer(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; table_init_single_mapping(table, 'X', 1);

    gfx_text_label_render_hex(0xab, ' ', " id", 0, 0, font, table, 7);

    T_ASSERT_EQ_S(buf_as_string(), " AB id");
    return 0;
}

/* Sanity: render path actually paints something on the canvas.
 * Use a single-char mapping for ' ' (space → glyph 1) so we get
 * visible pixels per character. */
int test_gfx_text_label_render_dec_paints_pixels(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 9, 0);
    uint8_t table[256];
    /* Every char maps to glyph 1 — every byte in the buffer paints. */
    memset(table, 1, 256);

    gfx_text_label_render_dec(123, ' ', "", 0, 0, font, table, 9);
    /* Buffer is " 123\0" — 4 characters render. */
    T_ASSERT_EQ_I(count_pixels(&c, 9), 4);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Group 5 — Centered variants + quirk #45 (embedded-NUL)
 * ────────────────────────────────────────────────────────────────── */

/* Quirk #45 witness — the centered render's do/while strlen leaves
 * an embedded NUL between the digits and the suffix.  The renderer
 * stops at the NUL, so the suffix is invisible.  We verify by
 * inspecting the assembled buffer directly. */
int test_gfx_text_label_render_dec_centered_embeds_nul_per_quirk_45(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; table_init_single_mapping(table, 'X', 1);

    gfx_text_label_render_dec_centered(42, ' ', "XY", 0, 0, 100,
                                       font, table, table, 7);

    /* Buffer layout: " 42\0XY\0" — suffix sits one byte past the
     * NUL terminator left by the formatter.  C-string view stops at
     * the embedded NUL. */
    uint8_t *buf = gfx_text_label_buffer();
    T_ASSERT_EQ_U(buf[0], ' ');
    T_ASSERT_EQ_U(buf[1], '4');
    T_ASSERT_EQ_U(buf[2], '2');
    T_ASSERT_EQ_U(buf[3], 0);     /* the embedded NUL — quirk #45 */
    T_ASSERT_EQ_U(buf[4], 'X');
    T_ASSERT_EQ_U(buf[5], 'Y');
    T_ASSERT_EQ_U(buf[6], 0);
    /* `strcmp` view truncates at the embedded NUL. */
    T_ASSERT_EQ_S(buf_as_string(), " 42");
    return 0;
}

/* In contrast, the (non-centered) render_dec wrapper uses the for-
 * loop strlen and DOES NOT leak a NUL between digits and suffix. */
int test_gfx_text_label_render_dec_no_embedded_nul(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; table_init_single_mapping(table, 'X', 1);

    gfx_text_label_render_dec(42, ' ', "XY", 0, 0, font, table, 7);

    /* Buffer layout: " 42XY\0" — suffix contiguous, no embedded NUL. */
    T_ASSERT_EQ_S(buf_as_string(), " 42XY");
    return 0;
}

/* Quirk #44 witness — signed wrapper skips render when both inputs
 * are zero.  We verify by leaving a sentinel in the buffer that the
 * formatter would overwrite if invoked. */
int test_gfx_text_label_render_dec_signed_skips_when_both_zero(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; table_init_single_mapping(table, 'X', 1);

    /* Stash a sentinel that the formatter would otherwise zero. */
    uint8_t *buf = gfx_text_label_buffer();
    buf[0] = 0x5a;
    buf[1] = 0xa5;

    gfx_text_label_render_dec_signed(0, 0, ' ', "", 0, 0, 100,
                                     font, table, table, 7, 8);

    /* Sentinel survives → formatter was not invoked. */
    T_ASSERT_EQ_U(buf[0], 0x5a);
    T_ASSERT_EQ_U(buf[1], 0xa5);
    /* Nothing painted. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    T_ASSERT_EQ_I(count_pixels(&c, 8), 0);
    return 0;
}

/* Signed wrapper renders when value != 0 even if zero_companion == 0. */
int test_gfx_text_label_render_dec_signed_renders_when_value_nonzero(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256];
    /* All chars paint (color picked from the positive/negative arg). */
    memset(table, 1, 256);

    gfx_text_label_render_dec_signed(5, 0, ' ', "", 0, 0, 100,
                                     font, table, table, 7, 8);

    /* Buffer should be " +5\0" — with_plus=1, sign at [1], digit at [2]. */
    T_ASSERT_EQ_S(buf_as_string(), " +5");
    /* Positive → color_positive (7) used. */
    T_ASSERT(count_pixels(&c, 7) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 8), 0);
    return 0;
}

/* Signed wrapper picks color_negative when value < 0. */
int test_gfx_text_label_render_dec_signed_negative_picks_color_neg(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    gfx_text_label_render_dec_signed(-3, 0, ' ', "", 0, 0, 100,
                                     font, table, table, 7, 8);

    /* Buffer: " -3\0". */
    T_ASSERT_EQ_S(buf_as_string(), " -3");
    /* Negative → color_negative (8). */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    T_ASSERT(count_pixels(&c, 8) > 0);
    return 0;
}

/* Signed wrapper renders when zero_companion is non-zero even if
 * value == 0 — the sentinel pair test. */
int test_gfx_text_label_render_dec_signed_renders_when_companion_nonzero(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    gfx_text_label_render_dec_signed(0, 1, ' ', "", 0, 0, 100,
                                     font, table, table, 7, 8);

    /* Value 0 with with_plus=1 → " +0\0". */
    T_ASSERT_EQ_S(buf_as_string(), " +0");
    T_ASSERT(count_pixels(&c, 7) > 0);  /* color_positive */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Group 6 — Compound "label + signed value" (FUN_00402e0c)
 * ────────────────────────────────────────────────────────────────── */

/* Positive value → '+' prefix on the value buffer; positive color. */
int test_gfx_text_label_render_labeled_signed_positive(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    static const uint8_t label[] = "TAX:";
    gfx_text_label_render_labeled_signed(7, 0, label, "", 0, 0,
                                         font, table, 7, 8);

    /* After the render, the format buffer holds the value half: "+7\0". */
    T_ASSERT_EQ_S(buf_as_string(), "+7");
    /* Only positive color used — both label and value share color_positive. */
    T_ASSERT(count_pixels(&c, 7) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 8), 0);
    return 0;
}

/* Zero value with mode==1 → renders (gate is "value != 0 OR mode != 0"),
 * '@' prefix per the value==0 branch. */
int test_gfx_text_label_render_labeled_signed_zero_value_mode_1(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    static const uint8_t label[] = "X:";
    gfx_text_label_render_labeled_signed(0, 1, label, "", 0, 0,
                                         font, table, 7, 8);

    /* value==0, mode != 2, value < 1 branch → '@' prefix, digits "0". */
    T_ASSERT_EQ_S(buf_as_string(), "@0");
    return 0;
}

/* Negative value → '-' prefix (via negate + prefix-overwrite),
 * negative color for both label and value. */
int test_gfx_text_label_render_labeled_signed_negative(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    static const uint8_t label[] = "X:";
    gfx_text_label_render_labeled_signed(-42, 0, label, "", 0, 0,
                                         font, table, 7, 8);

    /* Value rendered as "-42" (contiguous: prefix '-' overwrites
     * buffer[0], digits at [1..2]). */
    T_ASSERT_EQ_S(buf_as_string(), "-42");
    /* Both halves use negative color — only color 8 paints. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    T_ASSERT(count_pixels(&c, 8) > 0);
    return 0;
}

/* mode == 2 with positive value → '@' prefix, positive color. */
int test_gfx_text_label_render_labeled_signed_mode_2_positive(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    static const uint8_t label[] = "X:";
    gfx_text_label_render_labeled_signed(5, 2, label, "", 0, 0,
                                         font, table, 7, 8);

    /* mode==2 takes precedence over sign — '@' prefix used. */
    T_ASSERT_EQ_S(buf_as_string(), "@5");
    /* Positive color (mode==2 → color_positive). */
    T_ASSERT(count_pixels(&c, 7) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 8), 0);
    return 0;
}

/* Both inputs zero → no render at all.  Sentinel pre-stuff in the
 * buffer survives. */
int test_gfx_text_label_render_labeled_signed_skips_when_both_zero(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    uint8_t *buf = gfx_text_label_buffer();
    buf[0] = 0x5a;
    buf[1] = 0xa5;

    static const uint8_t label[] = "X:";
    gfx_text_label_render_labeled_signed(0, 0, label, "", 0, 0,
                                         font, table, 7, 8);

    /* Sentinel survives → no formatter / no render path was entered. */
    T_ASSERT_EQ_U(buf[0], 0x5a);
    T_ASSERT_EQ_U(buf[1], 0xa5);
    /* Canvas untouched. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    T_ASSERT_EQ_I(count_pixels(&c, 8), 0);
    return 0;
}

/* Quirk #46 witness — when both inputs are zero, the function resets
 * line_x_acc to 0 but never restores it.  The caller's running x
 * accumulator is silently clobbered. */
int test_gfx_text_label_render_labeled_signed_clobbers_x_acc_on_zero(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    gfx_text_set_line_x_acc(42);   /* caller's running advance */

    static const uint8_t label[] = "X:";
    gfx_text_label_render_labeled_signed(0, 0, label, "", 0, 0,
                                         font, table, 7, 8);

    /* All-zero gate → line_x_acc was reset to 0 BEFORE the gate check
     * but the restore lives INSIDE the (skipped) gate branch — so the
     * caller's 42 is lost.  Quirk #46. */
    T_ASSERT_EQ_I(gfx_text_line_x_acc(), 0);
    return 0;
}

/* Non-zero path preserves and increments line_x_acc.  After the
 * render: line_x_acc = saved + (label_width + value_width). */
int test_gfx_text_label_render_labeled_signed_increments_x_acc(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    /* Empty label so width contribution is just the value's. */
    static const uint8_t empty_label[] = "";
    gfx_text_set_line_x_acc(100);

    gfx_text_label_render_labeled_signed(1, 0, empty_label, "", 0, 0,
                                         font, table, 7, 8);

    /* Render happened; line_x_acc was reset to 0 then the value render
     * advanced it by the value's per-glyph width.  Final value =
     * value_width + saved (100).  We don't assert exact value-width
     * here (font-fixture-dependent) but the saved-100 must be present. */
    T_ASSERT(gfx_text_line_x_acc() >= 100);
    /* And strictly greater — value advanced the accumulator by at
     * least one glyph (the '+' or the digit). */
    T_ASSERT(gfx_text_line_x_acc() > 100);
    return 0;
}

/* Quirk #47 witness — mode==2 with negative value: label colors by
 * value sign (color_negative), value renders with '@' prefix in
 * color_positive.  Mismatched colors.  Also the assembled value buffer
 * contains a hidden '-' byte at offset 1 (the formatter still emits
 * its own sign byte for the negative value). */
int test_gfx_text_label_render_labeled_signed_mode_2_negative_quirk_47(void)
{
    canvas_t c; canvas_init(&c);
    label_reset();

    uint8_t font[FONT_BLOB_LEN]; font_blob_init_1x1(font, 7, 0);
    uint8_t table[256]; memset(table, 1, 256);

    /* Empty label so only value pixels land on the canvas — that lets
     * us assert the value color in isolation. */
    static const uint8_t empty_label[] = "";
    gfx_text_label_render_labeled_signed(-3, 2, empty_label, "", 0, 0,
                                         font, table, 7, 8);

    /* Value buffer: '@' at [0], '-' at [1] (formatter sign byte),
     * '3' at [2], terminator at [3]. */
    T_ASSERT_EQ_U(gfx_text_label_buffer()[0], '@');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[1], '-');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[2], '3');
    T_ASSERT_EQ_U(gfx_text_label_buffer()[3], 0);
    /* Value rendered in color_positive (mode==2 wins) — not
     * color_negative even though value < 0. */
    T_ASSERT(count_pixels(&c, 7) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 8), 0);
    return 0;
}
