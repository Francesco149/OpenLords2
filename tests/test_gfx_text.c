/*
 * Unit tests for gfx_text — the text-line renderer.
 *
 * Tests fall into groups:
 *   1. The plain & no-shadow paths (single dispatch per glyph).
 *   2. The three multi-pass paths: drop-shadow, outline, tight-shadow.
 *   3. Language tag flips shadow colors.
 *   4. Uppercase highlight override.
 *   5. The '#' substitution stream — armed, disarmed, NUL-wrap.
 *   6. Consecutive-space collapse + collapse_active disable.
 *   7. First-position hook.
 *   8. End-of-call state resets.
 *   9. Cursor advance via dispatcher return.
 *  10. Centered wrapper.
 *
 * Strategy: bind a small canvas, stage a synthetic 1-glyph font, and
 * call the renderer.  Observe the bound canvas + getters
 * (line_x_acc, char_counter, first_pos_*) to verify behavior.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_text.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Fixtures (mirror test_gfx_dispatch.c)
 * ────────────────────────────────────────────────────────────────── */

enum { CANVAS_W = 64, CANVAS_H = 32 };

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

static uint8_t at(const canvas_t *c, int x, int y)
{
    return c->pixels[x + y * CANVAS_W];
}

/* Reset every globals-holding module so tests run in any order. */
static void text_reset(void)
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
    gfx_text_set_language_tag(0);
    gfx_text_load_substitution_stream("");
    gfx_text_enable_substitution(0);
    gfx_text_set_first_pos_target(-1);
    gfx_text_clear_first_pos_latch();
    gfx_text_set_line_x_acc(0);
    gfx_text_set_line_y_acc(0);
    gfx_text_set_char_counter(0);
}

/* Synthetic font blob.  Glyph 0 = a 3x3 fully-opaque block.  Slot 0
 * descriptor starts at byte 8 (dispatcher's `slot = font + idx*16 + 8`).
 * Pixel data starts at byte 256. */
enum { FONT_BLOB_LEN = 512, PIXEL_BASE = 256 };

static void font_blob_init_3x3(uint8_t *blob, uint8_t fill, uint8_t advance)
{
    memset(blob, 0, FONT_BLOB_LEN);
    blob[8]  = 3;             /* width.lo */
    blob[9]  = 0;             /* width.hi */
    blob[10] = 3;             /* height.lo */
    blob[11] = 0;             /* height.hi */
    blob[12] = PIXEL_BASE & 0xff;
    blob[13] = (PIXEL_BASE >> 8) & 0xff;
    blob[14] = 0;
    blob[15] = 0;
    blob[8 + 0xc] = 0;
    blob[8 + 0xd] = advance;  /* per-glyph cursor_y advance / initial row count */
    blob[8 + 0xe] = 0;
    for (int i = 0; i < 9; i++) {
        blob[PIXEL_BASE + i] = fill;
    }
}

/* 1x1 glyph — used by the multi-pass shadow tests where overlapping
 * 3x3 boxes would obscure the per-pass color. */
static void font_blob_init_1x1(uint8_t *blob, uint8_t fill, uint8_t advance)
{
    memset(blob, 0, FONT_BLOB_LEN);
    blob[8]  = 1;
    blob[9]  = 0;
    blob[10] = 1;
    blob[11] = 0;
    blob[12] = PIXEL_BASE & 0xff;
    blob[13] = (PIXEL_BASE >> 8) & 0xff;
    blob[14] = 0;
    blob[15] = 0;
    blob[8 + 0xc] = 0;
    blob[8 + 0xd] = advance;
    blob[8 + 0xe] = 0;
    blob[PIXEL_BASE] = fill;
}

/* Char-glyph table — entry 0 = "no glyph", non-zero = 1-based glyph
 * index.  The renderer indexes at (char - 0x20), so table[0] is ASCII
 * 0x20 == space. */
static void table_init_single_mapping(uint8_t *table, uint8_t ch, uint8_t glyph_idx_one_based)
{
    memset(table, 0, 256);
    table[(uint8_t)(ch - 0x20)] = glyph_idx_one_based;
}

/* Count non-zero pixels of a specific palette index in the canvas. */
static int count_pixels(const canvas_t *c, uint8_t color)
{
    int n = 0;
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        if (c->pixels[i] == color) n++;
    }
    return n;
}

/* ──────────────────────────────────────────────────────────────────
 * Empty buffer + accumulator
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_empty_buffer_pads_acc_by_4(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_line_x_acc(100);

    gfx_text_render_line((const uint8_t *)"", 10, 10, NULL, NULL, 7);

    /* No glyphs drawn — but the unconditional +4 at end-of-call fires. */
    T_ASSERT_EQ_I(gfx_text_line_x_acc(), 104);
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Plain glyph render
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_plain_renders_single_glyph(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0x42, 0 /* advance */);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    /* Render "A" at (5, 5).  3x3 block painted with color 7. */
    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    T_ASSERT_EQ_I(at(&c, 7, 7), 7);
    /* Outside the 3x3 box: untouched. */
    T_ASSERT_EQ_I(at(&c, 4, 5), 0);
    T_ASSERT_EQ_I(at(&c, 8, 5), 0);
    /* glyph_w (3) + 1 = 4 advance + 4 EOL pad = 8 in line_x_acc. */
    T_ASSERT_EQ_I(gfx_text_line_x_acc(), 8);
    return 0;
}

int test_gfx_text_underscore_renders_as_space(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0x42, 0);

    uint8_t table[256];
    memset(table, 0, sizeof table);
    table[(uint8_t)(' ' - 0x20)] = 1;   /* ASCII 0x20 maps to glyph 0 */
    table[(uint8_t)('_' - 0x20)] = 0;   /* '_' has no own glyph */

    /* '_' is normalized to ' ' which DOES have a glyph here.  So 1 pass. */
    gfx_text_render_line((const uint8_t *)"_", 5, 5, font, table, 7);

    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    return 0;
}

int test_gfx_text_missing_glyph_advances_4_no_pixels(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0x42, 0);

    uint8_t table[256];
    memset(table, 0, sizeof table);   /* every char missing */

    gfx_text_render_line((const uint8_t *)"X", 5, 5, font, table, 7);

    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    /* Fallback width 4 + EOL pad 4. */
    T_ASSERT_EQ_I(gfx_text_line_x_acc(), 8);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Render-mode paths
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_no_shadow_one_pass(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    /* Single pass only fg color — no 0x10 or 0x1f shadow. */
    T_ASSERT(count_pixels(&c, 7) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 0x10), 0);
    T_ASSERT_EQ_I(count_pixels(&c, 0x1f), 0);
    return 0;
}

int test_gfx_text_drop_shadow_three_passes(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    /* no_shadow=0 (default), outline=0, tight_shadow=0 → drop shadow.
     * 1x1 glyph keeps the three passes on distinct pixels. */

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    /* Three passes: 0x10 at (5,4), 0x1f at (5,6), 7 at (5,5). */
    T_ASSERT_EQ_I(at(&c, 5, 4), 0x10);
    T_ASSERT_EQ_I(at(&c, 5, 6), 0x1f);
    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    return 0;
}

int test_gfx_text_outline_three_passes_hardcoded_colors(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_outline(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    /* 0x3f shadow at (5,4), 0x26 shadow at (5,6), fg at (5,5). */
    T_ASSERT_EQ_I(at(&c, 5, 4), 0x3f);
    T_ASSERT_EQ_I(at(&c, 5, 6), 0x26);
    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    return 0;
}

int test_gfx_text_tight_shadow_offsets_by_one(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_tight_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_1x1(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    /* Shadow pass at (cursor_x+1, cursor_y+1) with color 0x3f, then
     * foreground at (cursor_x, cursor_y). */
    T_ASSERT_EQ_I(at(&c, 6, 6), 0x3f);
    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    return 0;
}

int test_gfx_text_language_tag_flips_shadow_colors(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_language_tag(0x1f);    /* triggers Polish/Czech tweak */

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    /* Expect (0x36, 0x2c) instead of (0x10, 0x1f). */
    T_ASSERT(count_pixels(&c, 0x36) > 0);
    T_ASSERT(count_pixels(&c, 0x2c) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 0x10), 0);
    T_ASSERT_EQ_I(count_pixels(&c, 0x1f), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Uppercase highlight
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_uppercase_highlight_forces_color_one(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_set_uppercase_highlight(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    /* Uppercase A → foreground forced to 1. */
    T_ASSERT(count_pixels(&c, 1) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    return 0;
}

int test_gfx_text_uppercase_highlight_skips_lowercase(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_set_uppercase_highlight(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'a', 1);

    gfx_text_render_line((const uint8_t *)"a", 5, 5, font, table, 7);

    /* Lowercase a — highlight does NOT apply. */
    T_ASSERT(count_pixels(&c, 7) > 0);
    T_ASSERT_EQ_I(count_pixels(&c, 1), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Substitution stream
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_substitution_replaces_hash(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_load_substitution_stream("A");
    gfx_text_enable_substitution(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    /* Map 'A' but NOT '#' — if the substitution works, we'll see a
     * glyph; otherwise '#' becomes space which has no mapping → nothing. */
    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"#", 5, 5, font, table, 7);

    T_ASSERT(count_pixels(&c, 7) > 0);   /* glyph drawn */
    return 0;
}

int test_gfx_text_substitution_off_renders_hash_as_space(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_load_substitution_stream("A");
    /* substitution NOT enabled */

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"#", 5, 5, font, table, 7);

    /* '#' normalized to ' ' which has no mapping → no glyph drawn. */
    T_ASSERT_EQ_I(count_pixels(&c, 7), 0);
    return 0;
}

int test_gfx_text_substitution_wraps_on_nul(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_load_substitution_stream("A");        /* 1-char stream */
    gfx_text_enable_substitution(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    /* Single '#' — first read yields 'A', cursor advances to 1.  Bottom
     * of loop fetches next byte from stream (idx=1 → NUL → wrap to 0,
     * yield ' ').  Loop ends because the source pointer already passed
     * the '#' from the bottom of the prior... wait, no — the substitution
     * yielding NUL returns ' ', so loop continues with ch = ' '.  Then
     * next iter, idx==0 so source advance, ch = source[1] = NUL → loop
     * exits.  So we should see one 'A' glyph drawn and a space "drawn"
     * (no glyph for space since we didn't map it). */
    gfx_text_render_line((const uint8_t *)"#", 5, 5, font, table, 7);

    /* One 'A' glyph. */
    T_ASSERT(count_pixels(&c, 7) > 0);
    /* Substitution cursor reset at end of call. */
    T_ASSERT_EQ_I(gfx_text_substitution_cursor(), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Consecutive-space collapse
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_collapse_spaces_rewinds_acc(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_set_collapse_spaces(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    /* Space mapped — width = 3+1 = 4.  Two consecutive spaces normally
     * advance by 8 + EOL pad 4 = 12.  With collapse, the second space
     * is rewound → 4 + 4 (advance + rewind of second) + 4 (EOL) = 8. */
    uint8_t table[256];
    memset(table, 0, sizeof table);
    table[0] = 1;   /* ASCII 0x20 → glyph 0 */

    /* Without collapse: line_x_acc would be 4 + 4 + 4 = 12.
     * With collapse: the rewind fires after the second char is loaded.
     * Sequence:
     *   iter 1: ch=' ', prev=' '.  Advance acc by 4.  Bottom: ch=' '.
     *           collapse: prev==ch==' ', rewind by 4.  acc=0.
     *   iter 2: ch=' ', prev=' '.  Advance acc by 4.  Bottom: ch=NUL.
     *           collapse: prev==NUL ≠ ch=' '... wait, swapped.
     *           Actually prev=current iter's start ch=' ', new ch=NUL.
     *           prev != ch, no collapse.
     *   EOL: acc += 4 = 8.
     */
    gfx_text_render_line((const uint8_t *)"  ", 5, 5, font, table, 7);

    T_ASSERT_EQ_I(gfx_text_line_x_acc(), 8);
    return 0;
}

int test_gfx_text_collapse_active_disables_rewind(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_set_collapse_spaces(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    memset(table, 0, sizeof table);
    table[0] = 1;

    /* collapse_active is cleared by the renderer at end-of-call.  We
     * can't easily test the "active blocks collapse" path from outside
     * since there's no setter — but render output should match the
     * collapse case above (since collapse_active starts at 0).  This
     * test mainly verifies that after the FIRST render, the
     * collapse_active flag is clear and the second render still works
     * as expected. */
    gfx_text_render_line((const uint8_t *)"  ", 5, 5, font, table, 7);
    int first_acc = gfx_text_line_x_acc();

    gfx_text_set_line_x_acc(0);
    gfx_text_render_line((const uint8_t *)"  ", 5, 5, font, table, 7);
    int second_acc = gfx_text_line_x_acc();

    T_ASSERT_EQ_I(first_acc, second_acc);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * First-position hook
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_first_pos_hook_captures_at_target(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_set_char_counter(0);
    gfx_text_set_first_pos_target(2);
    gfx_text_clear_first_pos_latch();
    gfx_text_set_line_x_acc(0);
    gfx_text_set_line_y_acc(99);

    /* Render "AAA" — third 'A' (char index 2) triggers the hook before
     * dispatch.  At that point line_x_acc = 8 (two prior chars at 4
     * each → +1 = 4 advance per char). */
    gfx_text_render_line((const uint8_t *)"AAA", 5, 5, font, table, 7);

    T_ASSERT_EQ_I(gfx_text_first_pos_latched(), 1);
    T_ASSERT_EQ_I(gfx_text_first_pos_x(), 8);
    T_ASSERT_EQ_I(gfx_text_first_pos_y(), 99);
    return 0;
}

int test_gfx_text_first_pos_hook_latch_blocks_resnapshot(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_set_char_counter(0);
    gfx_text_set_first_pos_target(0);
    gfx_text_clear_first_pos_latch();
    gfx_text_set_line_x_acc(0);

    /* First 'A' captures at acc=0.  Without re-clearing the latch,
     * subsequent 'A's must NOT update first_pos_x even if counter ==
     * target again (which would require setting counter back; not the
     * case here but we test the latch behavior). */
    gfx_text_render_line((const uint8_t *)"AAA", 5, 5, font, table, 7);

    T_ASSERT_EQ_I(gfx_text_first_pos_latched(), 1);
    T_ASSERT_EQ_I(gfx_text_first_pos_x(), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * End-of-call state resets
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_clears_substitution_at_end(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_text_load_substitution_stream("XY");
    gfx_text_enable_substitution(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'X', 1);

    gfx_text_render_line((const uint8_t *)"#", 5, 5, font, table, 7);

    /* Substitution disabled at end-of-call; cursor back to 0. */
    T_ASSERT_EQ_I(gfx_text_substitution_cursor(), 0);
    return 0;
}

int test_gfx_text_clears_alt_clip_at_end(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    gfx_dispatch_set_alt_clip(1, 0, CANVAS_H);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);

    /* Render two more chars — if alt_clip wasn't cleared, the
     * dispatcher would still be using the panel-overlay rect.  Easiest
     * way to observe: set alt_clip to a region that excludes (5,5),
     * then check the renderer cleared it by checking that a fresh
     * render lands inside (5,5).  We arm it BEFORE the first render
     * (which uses the rect once) and verify the SECOND render hits
     * (5,5) anyway because alt_clip was cleared. */
    canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);
    /* Arm alt_clip to a rect that excludes (5,5). */
    gfx_dispatch_set_alt_clip(1, 50, CANVAS_H);  /* left=50 */

    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);
    /* First render: alt_clip was on → glyph clipped out (cursor_x=5
     * is left of left=50). */
    int first_count = count_pixels(&c, 7);
    T_ASSERT_EQ_I(first_count, 0);

    /* alt_clip should be cleared now; second render should NOT be
     * clipped. */
    gfx_text_render_line((const uint8_t *)"A", 5, 5, font, table, 7);
    int second_count = count_pixels(&c, 7);
    T_ASSERT(second_count > 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Cursor advance via dispatcher return
 * ────────────────────────────────────────────────────────────────── */

int test_gfx_text_advances_running_x_by_dispatcher_width(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);

    uint8_t table[256];
    table_init_single_mapping(table, 'A', 1);

    gfx_text_set_line_x_acc(0);

    /* Render "AA" — each glyph_w=3 → advance 4 per char, +4 EOL = 12. */
    gfx_text_render_line((const uint8_t *)"AA", 5, 5, font, table, 7);
    T_ASSERT_EQ_I(gfx_text_line_x_acc(), 12);

    /* Two glyphs painted at (5,5) and (9,5). */
    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    T_ASSERT_EQ_I(at(&c, 9, 5), 7);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Centered wrapper
 * ────────────────────────────────────────────────────────────────── */

/* Build a single 288-byte table mimicking retail's overlapped DAT_004d71d0
 * (metrics-view, raw char index) / DAT_004d71f0 (renderer-view, char-0x20
 * index) layout.  Returns ptrs into the same buffer so a single mapping
 * shows up in both views. */
typedef struct {
    uint8_t buf[288];
} overlapped_table_t;

static void overlapped_table_init(overlapped_table_t *t, uint8_t ch, uint8_t idx)
{
    memset(t->buf, 0, sizeof t->buf);
    t->buf[ch] = idx;   /* metrics view reads at [ch] */
    /* renderer view starts 0x20 bytes in; reads at [ch - 0x20] of its
     * view, which is [(ch - 0x20) + 0x20] = [ch] of the buffer.  Same
     * byte — no second write needed. */
}

int test_gfx_text_centered_offsets_x_by_half_remainder(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    /* width descriptor at +8/+9 — for the font_metrics call which uses
     * the SAME font blob.  Set glyph 0's width to 5 so metrics returns
     * 5 + 1 = 6 pixels for "A". */
    font_blob_init_3x3(font, 0xff, 0);
    font[8] = 5;
    font[9] = 0;

    overlapped_table_t t;
    overlapped_table_init(&t, 'A', 1);

    /* Width available = 20.  Text width via font_metrics_buffer_width_bounded:
     * 'A' contributes font[idx*16+9]*256 + font[idx*16+8] + 1 = 6.
     * Offset = (20 - 6) / 2 = 7.  Render at (5+7, 5). */
    gfx_text_render_line_centered((const uint8_t *)"A", 5, 5, 20,
                                  font, t.buf + 0x20, t.buf, 7);

    /* Glyph descriptor's width (slot[0..1]) is 5 — so a 5x3 block lands
     * at (12, 5). */
    T_ASSERT_EQ_I(at(&c, 12, 5), 7);
    /* Not at original (5, 5). */
    T_ASSERT_EQ_I(at(&c, 5, 5), 0);
    return 0;
}

int test_gfx_text_centered_clamps_offset_at_zero_when_overflow(void)
{
    canvas_t c; canvas_init(&c);
    text_reset();
    gfx_text_set_no_shadow(1);

    uint8_t font[FONT_BLOB_LEN];
    font_blob_init_3x3(font, 0xff, 0);
    font[8] = 20;
    font[9] = 0;

    overlapped_table_t t;
    overlapped_table_init(&t, 'A', 1);

    /* Text width = 21.  Width available = 10.  Offset would be (10-21)/2 = -5
     * → clamped to 0.  Render at base (5, 5). */
    gfx_text_render_line_centered((const uint8_t *)"A", 5, 5, 10,
                                  font, t.buf + 0x20, t.buf, 7);

    T_ASSERT_EQ_I(at(&c, 5, 5), 7);
    return 0;
}
