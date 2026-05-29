/*
 * Unit tests for gfx_wrap — word-wrap + localization-string render
 * wrappers.
 *
 * Tests fall into groups:
 *   1. Single-line localized renders (gfx_wrap_localized_line).
 *   2. Single-line localized centered renders.
 *   3. Multi-line buffer word-wrap (gfx_wrap_buffer).
 *   4. Multi-line localized word-wrap with leading-space skip +
 *      two-stage pagination (gfx_wrap_localized).
 *   5. max_line_x tracking and reset semantics.
 *   6. Tall-font line-height switch.
 *   7. Out-of-bounds seek is a no-op.
 *
 * Strategy: bind a small canvas, stage a 1x1 font + a 256-byte
 * char-glyph table (with space mapped to "no glyph" so spaces
 * advance 4 px without drawing), build a tiny localization blob,
 * and call the wrap entry points.  Observe canvas pixels + getters
 * (line_x_acc, line_y_acc, max_line_x).
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_text.h"
#include "gfx_wrap.h"
#include "localization.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Fixtures
 * ────────────────────────────────────────────────────────────────── */

enum { CANVAS_W = 96, CANVAS_H = 96 };

typedef struct {
    uint8_t pixels[CANVAS_W * CANVAS_H];
} canvas_t;

static void canvas_init(canvas_t *c)
{
    memset(c->pixels, 0x00, sizeof c->pixels);
    gfx_paint_bind_canvas(c->pixels, CANVAS_W, CANVAS_H);
}

static uint8_t at(const canvas_t *c, int x, int y)
{
    return c->pixels[x + y * CANVAS_W];
}

static int count_pixels(const canvas_t *c, uint8_t color)
{
    int n = 0;
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        if (c->pixels[i] == color) n++;
    }
    return n;
}

/* Reset every shared globals-holding module so tests run in any
 * order.  Mirrors test_gfx_text.c::text_reset plus the gfx_wrap
 * module's two getters. */
static void wrap_reset(void)
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

    gfx_text_set_no_shadow(0);
    gfx_text_set_outline(0);
    gfx_text_set_tight_shadow(0);
    gfx_text_set_uppercase_highlight(0);
    gfx_text_set_collapse_spaces(0);
    gfx_text_set_collapse_active(0);
    gfx_text_set_language_tag(0);
    gfx_text_load_substitution_stream("");
    gfx_text_enable_substitution(0);
    gfx_text_set_first_pos_target(-1);
    gfx_text_clear_first_pos_latch();
    gfx_text_set_line_x_acc(0);
    gfx_text_set_line_y_acc(0);
    gfx_text_set_char_counter(0);

    gfx_wrap_set_max_line_x(0);
    gfx_wrap_set_tall_font_marker(0);
}

/* 1x1 glyph @ slot 0.  Width 1, height 1, advance 0.  Lets us
 * place known pixels at known x-coords without overlap. */
enum { FONT_BLOB_LEN = 512, PIXEL_BASE = 256 };

static void font_blob_init_1x1(uint8_t *blob, uint8_t fill)
{
    memset(blob, 0, FONT_BLOB_LEN);
    blob[8]  = 1;     /* width.lo */
    blob[9]  = 0;     /* width.hi */
    blob[10] = 1;     /* height.lo */
    blob[11] = 0;     /* height.hi */
    blob[12] = PIXEL_BASE & 0xff;
    blob[13] = (PIXEL_BASE >> 8) & 0xff;
    blob[14] = 0;
    blob[15] = 0;
    blob[8 + 0xc] = 0;
    blob[8 + 0xd] = 0;   /* per-glyph cursor_y advance */
    blob[8 + 0xe] = 0;
    blob[PIXEL_BASE] = fill;
}

/* Build a 256-byte char→glyph-index table.  Maps every byte to
 * glyph 1 EXCEPT space (0x20) and '$' (0x24) which stay at 0 ("no
 * glyph", fallback advance 4 px in the renderer and word_width's
 * "leading-space" / "leading-$" gates).
 *
 * Render-side view: pass &table[0x20] (renderer indexes at ch-0x20).
 * Metrics-side view: pass &table[0]   (metrics indexes at ch).
 *
 * With this setup, char_width returns 2 for any printable mapped
 * to glyph 1 (font[8]+1 = 1+1 = 2) and 4 for space (hardcoded). */
static void table_init_all_glyph1_except_space(uint8_t *table)
{
    memset(table, 1, 256);
    table[0x20] = 0;
    table[0x24] = 0;
    table[0x00] = 0;
}

/* Build a minimal localization blob:
 *   bytes [0,8)   header (zero-filled)
 *   bytes [8,12)  offset entry for index 0 — 24-bit LE = payload_off
 *   bytes [12,16) offset entry for index 1 — 24-bit LE = payload_off
 *   bytes [payload_off, ...) — caller-supplied payload bytes
 *
 * `payload` is a C string; it's copied verbatim INCLUDING the
 * terminating NUL.  Caller can chain multiple NUL-separated
 * sub-strings by including '\0' in the payload literal length.
 */
static void blob_init(unsigned char *blob, const char *payload, int payload_len,
                      int payload_off)
{
    memset(blob, 0, LOCALIZATION_BLOB_SIZE + 16);   /* +16 guard */
    blob[ 8] = (unsigned char)(payload_off & 0xff);
    blob[ 9] = (unsigned char)((payload_off >>  8) & 0xff);
    blob[10] = (unsigned char)((payload_off >> 16) & 0xff);
    blob[11] = 0;
    blob[12] = (unsigned char)(payload_off & 0xff);
    blob[13] = (unsigned char)((payload_off >>  8) & 0xff);
    blob[14] = (unsigned char)((payload_off >> 16) & 0xff);
    blob[15] = 0;
    for (int i = 0; i < payload_len; i++) {
        blob[payload_off + i] = (unsigned char)payload[i];
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Single-line localized render
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_wrap_localized_line_loads_and_renders(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Blob: "AB" at offset 32. */
    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    blob_init(blob, "AB", 3, 32);

    localization_state_t loc = { .blob = blob };

    gfx_wrap_localized_line(&loc, /*loc_index=*/0, /*skip_strings=*/0,
                            /*cursor_x=*/5, /*cursor_y=*/5,
                            font, &table[0x20], /*color=*/7);

    /* 'A' at (5,5), 'B' at (7,5). */
    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    T_ASSERT_EQ_I(at(&c, 7, 5), 7);
    return 0;
}

int test_gfx_wrap_localized_line_skip_strings_advances_walker(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Two sub-strings: "AB\0CD" at offset 32. */
    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    blob_init(blob, "AB\0CD", 6, 32);

    localization_state_t loc = { .blob = blob };

    /* Skip 1 sub-string → walker should advance past the NUL after "AB",
     * landing on "CD".  Render of CD lands at (5,5) and (7,5). */
    gfx_wrap_localized_line(&loc, 0, /*skip_strings=*/1, 5, 5,
                            font, &table[0x20], 7);

    /* Pixel pattern of "CD" — same shape as "AB". */
    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    T_ASSERT_EQ_I(at(&c, 7, 5), 7);
    /* The (3rd char) position should remain unpainted. */
    T_ASSERT_EQ_I(at(&c, 9, 5), 0);
    return 0;
}

int test_gfx_wrap_localized_line_out_of_bounds_is_noop(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Offset entry for index 0 points past the end of the 120000-byte
     * blob → seek_localized returns NULL → render is a no-op. */
    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    memset(blob, 0, sizeof blob);
    int bad_off = LOCALIZATION_BLOB_SIZE + 100;
    blob[ 8] = (unsigned char)(bad_off & 0xff);
    blob[ 9] = (unsigned char)((bad_off >>  8) & 0xff);
    blob[10] = (unsigned char)((bad_off >> 16) & 0xff);

    localization_state_t loc = { .blob = blob };

    gfx_wrap_localized_line(&loc, 0, 0, 5, 5, font, &table[0x20], 7);

    /* No pixels of foreground color emitted. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    return 0;
}

int test_gfx_wrap_localized_line_unbound_blob_is_noop(void)
{
    /* Unbound localization (blob == NULL) — must no-op, not deref NULL.
     * Exercised by the title render chain during bring-up before the
     * l2.eng blob is bound. */
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);
    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    localization_state_t loc = { .blob = 0 };

    /* Both the single-line and the centered entry points must survive. */
    gfx_wrap_localized_line(&loc, 0xb, 0, 5, 5, font, &table[0x20], 7);
    gfx_wrap_localized_line_centered(&loc, 0xb, 0, 0, 0, 0x180,
                                     font, &table[0x20], &table[0], 7);

    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    return 0;
}

int test_gfx_wrap_localized_line_clears_alt_clip(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    blob_init(blob, "A", 2, 32);

    localization_state_t loc = { .blob = blob };

    /* Pre-arm alt-clip; wrap call should leave it disarmed. */
    gfx_dispatch_set_alt_clip(1, 0, 0);

    gfx_wrap_localized_line(&loc, 0, 0, 5, 5, font, &table[0x20], 7);

    /* The dispatcher reads alt_clip_enabled during the next render —
     * easiest observable check: render a second char and verify the
     * pixel lands where the normal-clip path would put it (off the
     * panel-overlay rect).  Indirect, but matches retail.  We just
     * assert via re-render that nothing throws. */
    gfx_wrap_localized_line(&loc, 0, 0, 5, 7, font, &table[0x20], 7);
    T_ASSERT_EQ_I(at(&c, 5, 7), 7);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Single-line localized CENTERED render
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_wrap_localized_line_centered_offsets_x(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* "AB" at offset 32.  Text width = char_width('A') + char_width('B')
     * = 2 + 2 = 4.  In a 20-px box starting at x=10, offset = (20-4)/2
     * = 8.  So 'A' lands at (10+8, 5) = (18, 5). */
    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    blob_init(blob, "AB", 3, 32);

    localization_state_t loc = { .blob = blob };

    gfx_wrap_localized_line_centered(&loc, 0, 0,
                                     /*cursor_x=*/10, /*cursor_y=*/5,
                                     /*width=*/20,
                                     font, &table[0x20], &table[0], 7);

    T_ASSERT_EQ_I(at(&c, 18, 5), 7);
    T_ASSERT_EQ_I(at(&c, 20, 5), 7);
    /* Box edges: (10,5) and (29,5) should not be painted. */
    T_ASSERT_EQ_I(at(&c, 10, 5), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Multi-line buffer word-wrap (gfx_wrap_buffer / FUN_0040352f)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_wrap_buffer_wraps_at_pixel_budget(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* "AAAA BBBB" with budget 13 px:
     *   word_width("AAAA ...") = 4 * 2 = 8.  line_pixel = 8 (< 13).
     *   Copy 4 chars.  buf at " BBBB".
     *   word_width(" BBBB") = 4 (space) + 4 * 2 = 12.  line_pixel = 20 (>= 13).
     *   Render line 1 = "AAAA".
     *   Next line: word_width(" BBBB") = 12 (< 13).  Copy 5 chars " BBBB".
     *   Source NUL → terminate.  Render line 2 = " BBBB". */
    gfx_wrap_buffer((const uint8_t *)"AAAA BBBB",
                    /*cursor_x=*/5, /*cursor_y=*/5,
                    /*line_width=*/13,
                    font, &table[0x20], &table[0], 7);

    /* Line 1 at y=5: 'A's at x=5, 7, 9, 11. */
    T_ASSERT_EQ_I(at(&c,  5, 5), 7);
    T_ASSERT_EQ_I(at(&c,  7, 5), 7);
    T_ASSERT_EQ_I(at(&c,  9, 5), 7);
    T_ASSERT_EQ_I(at(&c, 11, 5), 7);
    T_ASSERT_EQ_I(at(&c, 13, 5), 0);
    /* Line 2 at y=21 (default line height = 16).  Line buffer is
     * " BBBB" with no leading-space skip — space takes the missing-
     * glyph 4-px advance path, then 'B's at x=9, 11, 13, 15. */
    T_ASSERT_EQ_I(at(&c,  9, 21), 7);
    T_ASSERT_EQ_I(at(&c, 11, 21), 7);
    T_ASSERT_EQ_I(at(&c, 13, 21), 7);
    T_ASSERT_EQ_I(at(&c, 15, 21), 7);
    /* No third line. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 8);
    return 0;
}

int test_gfx_wrap_buffer_advances_line_y_acc_per_line(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Two-line render — line_y_acc should advance by 16 * 2 = 32. */
    gfx_wrap_buffer((const uint8_t *)"AAAA BBBB",
                    5, 5, 13, font, &table[0x20], &table[0], 7);

    T_ASSERT_EQ_I(gfx_text_line_y_acc(), 32);
    return 0;
}

int test_gfx_wrap_buffer_does_not_reset_max_line_x(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Seed max_line_x to a known high value.  gfx_wrap_buffer should
     * NOT clear it (quirk #36 — only gfx_wrap_localized resets). */
    gfx_wrap_set_max_line_x(999);

    gfx_wrap_buffer((const uint8_t *)"A", 5, 5, 50,
                    font, &table[0x20], &table[0], 7);

    /* Single-char "A": line_x_acc = char_width(2) + EOL pad(4) = 6.
     * max should remain 999 since 999 > 6. */
    T_ASSERT_EQ_I(gfx_wrap_max_line_x(), 999);
    return 0;
}

int test_gfx_wrap_buffer_max_line_x_tracks_widest_line(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* From the wrap-at-pixel-budget test above:
     *   Line 1 "AAAA": line_x_acc = 4 * 2 + 4 = 12.
     *   Line 2 " BBBB": missing-glyph space(4) + 4 * 2 + 4 = 16.
     * max_line_x should be 16. */
    gfx_wrap_buffer((const uint8_t *)"AAAA BBBB",
                    5, 5, 13, font, &table[0x20], &table[0], 7);

    T_ASSERT_EQ_I(gfx_wrap_max_line_x(), 16);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Multi-line localized word-wrap (gfx_wrap_localized / FUN_0040328e)
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_wrap_localized_resets_max_line_x_at_top(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    blob_init(blob, "A", 2, 32);

    localization_state_t loc = { .blob = blob };

    /* Pre-seed max_line_x to a high value — gfx_wrap_localized resets
     * at top, so the post-call value reflects only this render. */
    gfx_wrap_set_max_line_x(999);

    gfx_wrap_localized(&loc, 0, 0, 5, 5,
                       /*first_page_width=*/50, /*first_page_lines=*/10,
                       /*next_page_x=*/5,        /*next_page_width=*/50,
                       font, &table[0x20], &table[0], 7);

    /* Single 'A': line_x_acc = 2 + 4 = 6.  Reset clears the 999 seed. */
    T_ASSERT_EQ_I(gfx_wrap_max_line_x(), 6);
    return 0;
}

int test_gfx_wrap_localized_suppresses_leading_spaces_on_wrap(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* "AAAA BBBB" through localization, budget 13 — should wrap.
     * On line 2, the leading space from " BBBB" must be SUPPRESSED
     * (this is the key behavior diff vs. gfx_wrap_buffer). */
    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    blob_init(blob, "AAAA BBBB", 10, 32);

    localization_state_t loc = { .blob = blob };

    gfx_wrap_localized(&loc, 0, 0,
                       /*cursor_x=*/5, /*cursor_y=*/5,
                       /*first_page_width=*/13, /*first_page_lines=*/10,
                       /*next_page_x=*/5,         /*next_page_width=*/13,
                       font, &table[0x20], &table[0], 7);

    /* Line 1 at y=5: 'A's at x=5, 7, 9, 11. */
    T_ASSERT_EQ_I(at(&c,  5, 5), 7);
    T_ASSERT_EQ_I(at(&c, 11, 5), 7);
    /* Line 2 at y=21: 'B's start at x=5 (NO leading space → no +4 gap).
     * 4 'B's: x=5, 7, 9, 11. */
    T_ASSERT_EQ_I(at(&c,  5, 21), 7);
    T_ASSERT_EQ_I(at(&c, 11, 21), 7);
    /* Position (9, 21) where gfx_wrap_buffer's first 'B' would have
     * landed — for gfx_wrap_localized it's the THIRD 'B' (x=9 is
     * column index 2 of the 4 'B's), so still painted but for a
     * different reason. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 8);
    /* line_x_acc for line 2 = 4 * 2 + 4 = 12 (no missing-glyph 4-px
     * space advance).  max_line_x = max(12, 12) = 12. */
    T_ASSERT_EQ_I(gfx_wrap_max_line_x(), 12);
    return 0;
}

int test_gfx_wrap_localized_two_stage_pagination(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Three-word source: "AA BB CC".
     *   word_width("AA ...") = 2*2 = 4
     *   word_width(" BB ...") = 4 + 2*2 = 8
     *   word_width(" CC")    = 4 + 2*2 = 8
     * With first_page_width = 9: line 1 fits "AA" only (4 < 9, then
     * 4+8 = 12 >= 9 → break).  Wait, but our suppress_leading-space
     * eats the leading ' ' of " BB"... let me check more carefully.
     *
     * Actually word_width MEASURES leading space as +4, so once
     * " BB" is measured, line_pixel jumps by 8, exceeding budget.
     * The bytes are not copied.  Then line 2 measures " BB" again:
     * line_pixel = 0 + 8 = 8 < 9.  Copy " BB" (leading space goes
     * through measurement but the BYTE is SKIPPED by leading_skip).
     * So line 2 = "BB".
     *
     * After first_page_lines=2 emitted, swap to next_page_x=30 and
     * next_page_width=50.  Line 3 ("CC") renders at x=30, y=37.
     */
    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    blob_init(blob, "AA BB CC", 9, 32);

    localization_state_t loc = { .blob = blob };

    gfx_wrap_localized(&loc, 0, 0,
                       /*cursor_x=*/5, /*cursor_y=*/5,
                       /*first_page_width=*/9, /*first_page_lines=*/2,
                       /*next_page_x=*/30,      /*next_page_width=*/50,
                       font, &table[0x20], &table[0], 7);

    /* Line 1 "AA" at (5, 5): pixels at (5,5), (7,5). */
    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    T_ASSERT_EQ_I(at(&c, 7, 5), 7);
    /* Line 2 "BB" at (5, 21): pixels at (5,21), (7,21). */
    T_ASSERT_EQ_I(at(&c, 5, 21), 7);
    T_ASSERT_EQ_I(at(&c, 7, 21), 7);
    /* Line 3 "CC" at (30, 37): pixels at (30,37), (32,37). */
    T_ASSERT_EQ_I(at(&c, 30, 37), 7);
    T_ASSERT_EQ_I(at(&c, 32, 37), 7);
    /* Total: 6 foreground pixels. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 6);
    return 0;
}

int test_gfx_wrap_localized_out_of_bounds_is_noop(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    static unsigned char blob[LOCALIZATION_BLOB_SIZE + 16];
    memset(blob, 0, sizeof blob);
    int bad_off = LOCALIZATION_BLOB_SIZE + 100;
    blob[ 8] = (unsigned char)(bad_off & 0xff);
    blob[ 9] = (unsigned char)((bad_off >>  8) & 0xff);
    blob[10] = (unsigned char)((bad_off >> 16) & 0xff);

    localization_state_t loc = { .blob = blob };

    gfx_wrap_set_max_line_x(999);

    gfx_wrap_localized(&loc, 0, 0, 5, 5, 50, 10, 5, 50,
                       font, &table[0x20], &table[0], 7);

    /* max_line_x is reset to 0 at the top BEFORE the bounds check —
     * matches retail's `DAT_005aeb88 = 0` at function entry. */
    T_ASSERT_EQ_I(gfx_wrap_max_line_x(), 0);
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Tall-font line-height switch
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_wrap_tall_font_uses_24px_line_height(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Register this exact font as the tall-font marker.  Subsequent
     * wrap calls with the same font_base pointer use 24 px line height. */
    gfx_wrap_set_tall_font_marker(font);

    gfx_wrap_buffer((const uint8_t *)"AAAA BBBB",
                    5, 5, 13, font, &table[0x20], &table[0], 7);

    /* Two lines.  line_y_acc = 24 * 2 = 48. */
    T_ASSERT_EQ_I(gfx_text_line_y_acc(), 48);
    /* Line 2 paints at y = 5 + 24 = 29 (not 21). */
    T_ASSERT_EQ_I(at(&c,  9, 29), 7);
    T_ASSERT_EQ_I(at(&c,  9, 21), 0);
    return 0;
}

int test_gfx_wrap_non_tall_font_uses_16px_line_height(void)
{
    canvas_t c; canvas_init(&c);
    wrap_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff);

    uint8_t other_font[FONT_BLOB_LEN];
    font_blob_init_1x1(other_font, 0xff);

    uint8_t table[256];
    table_init_all_glyph1_except_space(table);

    /* Register OTHER font as the tall-font marker; render with `font`,
     * which differs by address → 16 px line height. */
    gfx_wrap_set_tall_font_marker(other_font);

    gfx_wrap_buffer((const uint8_t *)"AAAA BBBB",
                    5, 5, 13, font, &table[0x20], &table[0], 7);

    T_ASSERT_EQ_I(gfx_text_line_y_acc(), 32);
    return 0;
}
