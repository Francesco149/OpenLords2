/*
 * Unit tests for engine_title_render — main-menu render composition
 * (ports of FUN_0041ea14 root + FUN_0041eaa3 buttons).
 *
 * Strategy mirrors test_gfx_wrap.c: bind a canvas, a 1x1 font, an
 * all-glyph-1 char table, and a localization blob whose entry 0xb holds
 * the menu sub-strings, then drive the render entry points and observe
 * canvas pixels + the dirty-rect state + the deferred-panel hook.
 *
 * The 1x1 font renders one pixel of value == `color` per printable char
 * (color is used as a draw mask value, not the font fill), so the label
 * highlight color (0xf9 selected / 0x3f normal) shows up directly as
 * canvas pixels.  The bevel rect uses palette colors 0x35/0x28, distinct
 * from both, so 0xf9 pixels uniquely identify the selected label.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_text.h"
#include "gfx_wrap.h"
#include "localization.h"
#include "engine_dirty_rect.h"
#include "engine_title_render.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Fixtures
 * ────────────────────────────────────────────────────────────────── */

/* Full 640x480 backbuffer extent — page 5's right button sits at x=0x16e
 * w=0xa4 (right edge 0x212 = 530), past the old 512-wide canvas, so use the
 * real render extent to clip exactly as retail does. */
enum { CANVAS_W = 640, CANVAS_H = 480 };

/* Static so we don't blow the stack with the 300 KB canvas under ASan. */
static uint8_t g_canvas[CANVAS_W * CANVAS_H];

static void canvas_init(void)
{
    memset(g_canvas, 0x00, sizeof g_canvas);
    gfx_paint_bind_canvas(g_canvas, CANVAS_W, CANVAS_H);
}

/* Count pixels of `color` in row `y`. */
static int row_color_count(int y, uint8_t color)
{
    int n = 0;
    for (int x = 0; x < CANVAS_W; x++) {
        if (g_canvas[x + y * CANVAS_W] == color) n++;
    }
    return n;
}

/* Count pixels of `color` in row `y` within the half-open x range
 * [x0, x1).  Used by the page-5 chooser, whose two buttons share a row but
 * differ in x. */
static int row_color_count_x(int y, uint8_t color, int x0, int x1)
{
    int n = 0;
    for (int x = x0; x < x1 && x < CANVAS_W; x++) {
        if (g_canvas[x + y * CANVAS_W] == color) n++;
    }
    return n;
}

static int canvas_color_count(uint8_t color)
{
    int n = 0;
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        if (g_canvas[i] == color) n++;
    }
    return n;
}

/* 1x1 glyph @ slot 1 — identical layout to test_gfx_wrap.c's helper. */
enum { FONT_BLOB_LEN = 512, PIXEL_BASE = 256 };

static void font_blob_init_1x1(uint8_t *blob)
{
    memset(blob, 0, FONT_BLOB_LEN);
    blob[8]  = 1;     /* width.lo  */
    blob[9]  = 0;     /* width.hi  */
    blob[10] = 1;     /* height.lo */
    blob[11] = 0;     /* height.hi */
    blob[12] = PIXEL_BASE & 0xff;
    blob[13] = (PIXEL_BASE >> 8) & 0xff;
    blob[14] = 0;
    blob[15] = 0;
    blob[8 + 0xc] = 0;
    blob[8 + 0xd] = 0;
    blob[8 + 0xe] = 0;
    blob[PIXEL_BASE] = 0xff;   /* non-zero mask → draw with `color` */
}

/* All printable bytes → glyph 1; space/'$'/NUL → glyph 0 ("no glyph"). */
static void table_init(uint8_t *table)
{
    memset(table, 1, 256);
    table[0x20] = 0;
    table[0x24] = 0;
    table[0x00] = 0;
}

/* Build a localization blob whose entry `index` is a sequence of
 * NUL-terminated tokens (one per sub-string slot).  `tokens[i]` is the
 * i-th sub-string; empty strings ("") are valid boundary-only slots. */
/* Write one offset-table entry + its NUL-separated sub-strings at
 * `payload_off`, WITHOUT clearing the blob (so several entries can coexist). */
static void blob_add_entry(unsigned char *blob, int index,
                           const char *const *tokens, int n_tokens,
                           int payload_off)
{
    /* Offset-table entry `index` → 24-bit LE byte offset. */
    blob[8 + index * 4 + 0] = (unsigned char)(payload_off & 0xff);
    blob[8 + index * 4 + 1] = (unsigned char)((payload_off >> 8) & 0xff);
    blob[8 + index * 4 + 2] = (unsigned char)((payload_off >> 16) & 0xff);

    int p = payload_off;
    for (int i = 0; i < n_tokens; i++) {
        size_t len = strlen(tokens[i]);
        memcpy(blob + p, tokens[i], len);
        p += (int)len;
        blob[p++] = 0;   /* sub-string boundary */
    }
}

static void blob_init_entry(unsigned char *blob, int index,
                            const char *const *tokens, int n_tokens,
                            int payload_off)
{
    memset(blob, 0, LOCALIZATION_BLOB_SIZE + 16);
    blob_add_entry(blob, index, tokens, n_tokens, payload_off);
}

/* Menu localization layout (entry 0xb sub-strings):
 *   page 1 (main menu): title=0, subtitle=1, btn1=2, btn2=3, btn4=4, btn3=0x2f.
 *   page 2 ("Your options"): title=5, btn1=6, btn2=7, btn4=8, btn5=9, btn3=0x13.
 * Remaining slots are empty boundary-only. */
static const char *g_menu_tokens[48] = {
    "TT", "SS", "AA", "BB", "DD",             /* 0..4  (page-1) */
    "OO", "O1", "O2", "O4", "O5",             /* 5..9  (page-2 title + btns) */
    "", "", "", "", "", "", "", "", "",       /* 10..18 */
    "O3",                                     /* 19 (0x13) page-2 btn3 */
    "", "", "", "", "", "", "", "", "", "",   /* 20..29 */
    "", "", "", "", "", "", "", "", "", "",   /* 30..39 */
    "", "", "", "", "", "", "",               /* 40..46 */
    "CC"                                      /* 47 (0x2f) page-1 btn3 */
};

/* Page-5 ("Play Now!") string table (localization entry 0x27):
 *   sub 0 = title ("Expansion pack installed, choose:-")
 *   sub 4 = left button  ("Original campaign")
 *   sub 5 = right button ("The new campaign")
 * Title token is uppercase so the uppercase-highlight quirk forces it to
 * color 1, exactly as the page-1/2 title tokens do. */
static const char *g_play_now_tokens[6] = {
    "PT",          /* 0 — title         */
    "", "", "",    /* 1..3 — unused      */
    "P4",          /* 4 — left button   */
    "P5",          /* 5 — right button  */
};

static uint8_t g_font_title[FONT_BLOB_LEN];
static uint8_t g_font_menu[FONT_BLOB_LEN];
static uint8_t g_table[256];
static unsigned char g_blob[LOCALIZATION_BLOB_SIZE + 16];
static engine_dirty_rect_state_t g_dirty;

/* Reset every shared globals-holding module + bind this module's
 * fixtures.  Leaves selected-button at 1. */
static void title_reset(void)
{
    canvas_init();

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
    gfx_text_set_language_tag(0x1f);   /* title screen-state */
    gfx_text_load_substitution_stream("");
    gfx_text_enable_substitution(0);
    gfx_text_set_first_pos_target(-1);
    gfx_text_clear_first_pos_latch();
    gfx_text_set_line_x_acc(0);
    gfx_text_set_line_y_acc(0);
    gfx_text_set_char_counter(0);
    gfx_wrap_set_max_line_x(0);

    font_blob_init_1x1(g_font_title);
    font_blob_init_1x1(g_font_menu);
    table_init(g_table);
    blob_init_entry(g_blob, 0xb, g_menu_tokens, 48, 4096);
    blob_add_entry(g_blob, 0x27, g_play_now_tokens, 6, 8192);

    g_dirty.dirty_mark = 0;
    g_dirty.dirty_xmin = 0x7fff;
    g_dirty.dirty_xmax = -0x7fff;
    g_dirty.dirty_ymin = 0x7fff;
    g_dirty.dirty_ymax = -0x7fff;
    g_dirty.below_status_redraw = 0;

    engine_title_render_bind_fonts(g_font_title, g_font_menu);
    /* Render map indexed at ch-0x20, metrics map at ch. */
    engine_title_render_bind_glyph_tables(&g_table[0x20], &g_table[0]);
    engine_title_render_bind_localization(g_blob);
    engine_title_render_set_dirty_state(&g_dirty);
    engine_title_render_bind_panel_asset((const uint8_t *)"PANEL");
    engine_title_render_set_selected_button(1);
    engine_title_render_bind_panel_fn(0);   /* default stub */
    engine_title_render_reset_counters();
}

/* ──────────────────────────────────────────────────────────────────
 * Buttons (FUN_0041eaa3)
 * ────────────────────────────────────────────────────────────────── */

int test_title_buttons_marks_four_dirty_boxes(void)
{
    title_reset();
    engine_title_render_buttons();

    /* All four boxes share x = 0xe0, w = 0xe tiles → x range
     * [0xe0, 0xe0 + 0xe*16] = [224, 448].  Y spans the top of button 1
     * (0x5b) to the bottom of button 4 (0xc7 + 3*16 = 247). */
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    T_ASSERT_EQ_I(g_dirty.dirty_xmin, 0xe0);
    T_ASSERT_EQ_I(g_dirty.dirty_xmax, 0xe0 + 0xe * 16);
    T_ASSERT_EQ_I(g_dirty.dirty_ymin, 0x5b);
    T_ASSERT_EQ_I(g_dirty.dirty_ymax, 0xc7 + 3 * 16);
    return 0;
}

int test_title_buttons_draws_bevel_rects(void)
{
    title_reset();
    engine_title_render_buttons();

    /* Bevel rect uses palette colors 0x35 / 0x28; four rects → both
     * present. */
    T_ASSERT(canvas_color_count(0x35) > 0);
    T_ASSERT(canvas_color_count(0x28) > 0);
    return 0;
}

int test_title_buttons_highlights_selected(void)
{
    title_reset();
    engine_title_render_set_selected_button(2);
    engine_title_render_buttons();

    /* Button 2's label row is cursor_y = 0x7f + 5 = 0x84.  Only the
     * selected label uses highlight color 0xf9. */
    T_ASSERT(row_color_count(0x84, 0xf9) > 0);
    /* Button 1's label row (0x60) must NOT be highlighted. */
    T_ASSERT_EQ_I(row_color_count(0x60, 0xf9), 0);
    T_ASSERT(row_color_count(0x60, 0x3f) > 0);
    /* Exactly one button highlighted → 0xf9 confined to button 2 row. */
    T_ASSERT_EQ_I(canvas_color_count(0xf9), row_color_count(0x84, 0xf9));
    return 0;
}

int test_title_buttons_no_selection_all_normal(void)
{
    title_reset();
    engine_title_render_set_selected_button(0);   /* none in 1..4 */
    engine_title_render_buttons();

    T_ASSERT_EQ_I(canvas_color_count(0xf9), 0);
    T_ASSERT(canvas_color_count(0x3f) > 0);
    return 0;
}

int test_title_buttons_each_label_on_its_own_row(void)
{
    title_reset();
    engine_title_render_set_selected_button(0);   /* none → all 0x3f */
    engine_title_render_buttons();

    /* Labels sit at button_y + 5: 0x60, 0x84, 0xa8, 0xcc.  Confirms the
     * skip-0x2f button-3 label (sub-string 47) is reached, too. */
    T_ASSERT(row_color_count(0x60, 0x3f) > 0);   /* btn1 "AA" */
    T_ASSERT(row_color_count(0x84, 0x3f) > 0);   /* btn2 "BB" */
    T_ASSERT(row_color_count(0xa8, 0x3f) > 0);   /* btn3 "CC" (skip 0x2f) */
    T_ASSERT(row_color_count(0xc7 + 5, 0x3f) > 0); /* btn4 "DD" */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Main-menu root (FUN_0041ea14)
 * ────────────────────────────────────────────────────────────────── */

/* Capturing panel hook for the arg-fidelity test. */
static const uint8_t *g_cap_asset;
static int g_cap_p2, g_cap_p3, g_cap_p4, g_cap_p5, g_cap_calls;
static void capture_panel(const uint8_t *asset, int p2, int p3, int p4, int p5)
{
    g_cap_asset = asset;
    g_cap_p2 = p2; g_cap_p3 = p3; g_cap_p4 = p4; g_cap_p5 = p5;
    g_cap_calls++;
}

int test_title_main_menu_calls_panel_once_with_retail_args(void)
{
    title_reset();
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_main_menu();

    /* retail FUN_00409346(DAT_004eabec, 0xa0, 10, 0x14, 0xf). */
    T_ASSERT_EQ_I(g_cap_calls, 1);
    T_ASSERT(g_cap_asset == (const uint8_t *)"PANEL" ||
             (g_cap_asset != 0 && memcmp(g_cap_asset, "PANEL", 5) == 0));
    T_ASSERT_EQ_I(g_cap_p2, 0xa0);
    T_ASSERT_EQ_I(g_cap_p3, 10);
    T_ASSERT_EQ_I(g_cap_p4, 0x14);
    T_ASSERT_EQ_I(g_cap_p5, 0xf);
    return 0;
}

int test_title_main_menu_default_panel_stub_counts(void)
{
    title_reset();
    engine_title_render_main_menu();
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 1);
    return 0;
}

int test_title_main_menu_renders_title_subtitle_and_buttons(void)
{
    title_reset();   /* selected button defaults to 1 */
    engine_title_render_main_menu();

    /* Title at cursor_y 0x1e.  It renders with uppercase_highlight armed,
     * so retail forces the uppercase letters "TT" to color 1 (the
     * help-key-indicator quirk) — not the passed 0x3f.  Asserting color 1
     * here also confirms uppercase_highlight is armed for the title. */
    T_ASSERT(row_color_count(0x1e, 1) > 0);      /* title "TT" → color 1 */
    /* Subtitle at 0x3a: uppercase_highlight cleared, no-shadow on → 0x3f. */
    T_ASSERT(row_color_count(0x3a, 0x3f) > 0);   /* subtitle "SS" */
    /* Button 1 is the default selection → highlight color 0xf9. */
    T_ASSERT(row_color_count(0x60, 0xf9) > 0);   /* btn1 highlighted */
    /* Panel + dirty boxes also exercised. */
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 1);
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * "Your options" page (FUN_0041ec8a / FUN_0041ece6)
 * ────────────────────────────────────────────────────────────────── */

int test_title_options_calls_panel_with_retail_args(void)
{
    title_reset();
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_options();

    /* retail FUN_00409346(DAT_004eabec, 0xb0, 10, 0x12, 0x12). */
    T_ASSERT_EQ_I(g_cap_calls, 1);
    T_ASSERT_EQ_I(g_cap_p2, 0xb0);
    T_ASSERT_EQ_I(g_cap_p3, 10);
    T_ASSERT_EQ_I(g_cap_p4, 0x12);
    T_ASSERT_EQ_I(g_cap_p5, 0x12);
    return 0;
}

int test_title_options_buttons_marks_five_dirty_boxes(void)
{
    title_reset();
    engine_title_render_options_buttons();

    /* Five boxes share x = 0xe0, w = 0xe tiles → x range [224, 448].  Y
     * spans button 1 top (0x5b) to button 5 bottom (0xeb + 3*16 = 283). */
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    T_ASSERT_EQ_I(g_dirty.dirty_xmin, 0xe0);
    T_ASSERT_EQ_I(g_dirty.dirty_xmax, 0xe0 + 0xe * 16);
    T_ASSERT_EQ_I(g_dirty.dirty_ymin, 0x5b);
    T_ASSERT_EQ_I(g_dirty.dirty_ymax, 0xeb + 3 * 16);
    return 0;
}

int test_title_options_renders_title_and_five_buttons(void)
{
    title_reset();   /* selected button defaults to 1 */
    engine_title_render_options();

    /* Title "OO" at cursor_y 0x2d, uppercase_highlight armed → color 1. */
    T_ASSERT(row_color_count(0x2d, 1) > 0);
    /* Five button labels at button_y + 5: 0x60, 0x84, 0xa8, 0xcc, 0xf0. */
    T_ASSERT(row_color_count(0x60, 0xf9) > 0);   /* btn1 selected → highlight */
    T_ASSERT(row_color_count(0x84, 0x3f) > 0);   /* btn2 "O2" */
    T_ASSERT(row_color_count(0xa8, 0x3f) > 0);   /* btn3 "O3" (skip 0x13) */
    T_ASSERT(row_color_count(0xcc, 0x3f) > 0);   /* btn4 "O4" */
    T_ASSERT(row_color_count(0xf0, 0x3f) > 0);   /* btn5 "O5" */
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 1);
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    return 0;
}

int test_title_options_highlights_selected_fifth_button(void)
{
    title_reset();
    engine_title_render_set_selected_button(5);   /* "Back." */
    engine_title_render_options_buttons();

    /* Only button 5's label row (0xeb + 5 = 0xf0) uses highlight 0xf9. */
    T_ASSERT(row_color_count(0xf0, 0xf9) > 0);
    T_ASSERT_EQ_I(row_color_count(0x60, 0xf9), 0);   /* btn1 not highlighted */
    T_ASSERT(row_color_count(0x60, 0x3f) > 0);
    T_ASSERT_EQ_I(canvas_color_count(0xf9), row_color_count(0xf0, 0xf9));
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * "Play Now!" campaign chooser (page 5: FUN_0041f592 / FUN_0041f5e8)
 * ────────────────────────────────────────────────────────────────── */

int test_title_play_now_calls_panel_with_retail_args(void)
{
    title_reset();
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_play_now();

    /* retail FUN_00409346(DAT_004eabec, 0x40, 0x32, 0x20, 8). */
    T_ASSERT_EQ_I(g_cap_calls, 1);
    T_ASSERT_EQ_I(g_cap_p2, 0x40);
    T_ASSERT_EQ_I(g_cap_p3, 0x32);
    T_ASSERT_EQ_I(g_cap_p4, 0x20);
    T_ASSERT_EQ_I(g_cap_p5, 8);
    return 0;
}

int test_title_play_now_buttons_mark_one_dirty_box(void)
{
    title_reset();
    engine_title_render_play_now_buttons();

    /* Single mark FUN_004527a6(0x70, 0xd7, 0xe, 3, 1): pixel top-left
     * (0x70, 0xd7), tile-stride 0xe x 3 → box [0x70, 0x70+0xe*16] x
     * [0xd7, 0xd7+3*16]. */
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    T_ASSERT_EQ_I(g_dirty.dirty_xmin, 0x70);
    T_ASSERT_EQ_I(g_dirty.dirty_xmax, 0x70 + 0xe * 16);
    T_ASSERT_EQ_I(g_dirty.dirty_ymin, 0xd7);
    T_ASSERT_EQ_I(g_dirty.dirty_ymax, 0xd7 + 3 * 16);
    return 0;
}

int test_title_play_now_draws_two_bevel_rects(void)
{
    title_reset();
    engine_title_render_play_now_buttons();

    /* Bevel rect palette colors 0x35 / 0x28; two side-by-side rects → both. */
    T_ASSERT(canvas_color_count(0x35) > 0);
    T_ASSERT(canvas_color_count(0x28) > 0);
    return 0;
}

int test_title_play_now_renders_title_and_two_buttons(void)
{
    title_reset();   /* selected button defaults to 1 (left) */
    engine_title_render_play_now();

    /* Title "PT" at cursor_y 0x50, uppercase_highlight armed → color 1. */
    T_ASSERT(row_color_count(0x50, 1) > 0);

    /* Both labels share row 0x7a (= 0x74 button top? no — label y 0x7a).
     * Left button ("P4") is the default selection → highlight 0xf9 in the
     * left x-range; right button ("P5") → normal 0x3f in the right x-range. */
    T_ASSERT(row_color_count_x(0x7a, 0xf9, 0x6e, 0x6e + 0xa4) > 0);   /* left hi */
    T_ASSERT(row_color_count_x(0x7a, 0x3f, 0x16e, 0x16e + 0xa4) > 0); /* right normal */
    /* Only one label highlighted overall. */
    T_ASSERT_EQ_I(canvas_color_count(0xf9),
                  row_color_count_x(0x7a, 0xf9, 0x6e, 0x6e + 0xa4));

    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 1);
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    return 0;
}

int test_title_play_now_highlights_selected_right_button(void)
{
    title_reset();
    engine_title_render_set_selected_button(2);   /* right ("The new campaign") */
    engine_title_render_play_now_buttons();

    /* Highlight 0xf9 now in the RIGHT x-range; left is normal 0x3f. */
    T_ASSERT(row_color_count_x(0x7a, 0xf9, 0x16e, 0x16e + 0xa4) > 0);
    T_ASSERT_EQ_I(row_color_count_x(0x7a, 0xf9, 0x6e, 0x6e + 0xa4), 0);
    T_ASSERT(row_color_count_x(0x7a, 0x3f, 0x6e, 0x6e + 0xa4) > 0);
    T_ASSERT_EQ_I(canvas_color_count(0xf9),
                  row_color_count_x(0x7a, 0xf9, 0x16e, 0x16e + 0xa4));
    return 0;
}

int test_title_play_now_no_selection_all_normal(void)
{
    title_reset();
    engine_title_render_set_selected_button(0);   /* neither 1 nor 2 */
    engine_title_render_play_now_buttons();

    T_ASSERT_EQ_I(canvas_color_count(0xf9), 0);
    T_ASSERT(canvas_color_count(0x3f) > 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Title-scene dispatch (FUN_0041e7e1)
 * ────────────────────────────────────────────────────────────────── */

int test_title_scene_page1_renders_main_menu(void)
{
    title_reset();
    engine_title_render_scene(/*screen_state=*/0x1f, /*page=*/1);
    /* Background drawn, main menu (→ panel) drawn, page hook NOT used. */
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_last_page(), 0);
    /* Finalize expanded the dirty rect to the full screen. */
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    T_ASSERT_EQ_I(g_dirty.dirty_xmax, 0x27f);
    T_ASSERT_EQ_I(g_dirty.dirty_ymax, 0x1df);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_a(), 1);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_b(), 1);
    return 0;
}

int test_title_scene_wrong_screen_state_is_noop(void)
{
    title_reset();
    engine_title_render_scene(/*screen_state=*/0x10, /*page=*/1);
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 0);
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 0);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_a(), 0);
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 0);
    return 0;
}

int test_title_scene_other_page_routes_to_page_hook(void)
{
    title_reset();
    engine_title_render_scene(0x1f, /*page=*/3);
    /* Background drawn; page hook invoked with 3 (still-unported page);
     * neither the main menu nor the options page drawn. */
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_last_page(), 3);
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 0);
    /* Still finalizes + sets flags. */
    T_ASSERT_EQ_I(engine_title_render_scene_flag_a(), 1);
    return 0;
}

int test_title_scene_page2_routes_to_options(void)
{
    title_reset();
    engine_title_render_scene(0x1f, /*page=*/2);
    /* Background drawn; options page drawn (panel called once); the generic
     * page hook is NOT invoked for page 2. */
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_last_page(), 0);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_a(), 1);
    return 0;
}

int test_title_scene_page5_routes_to_play_now(void)
{
    title_reset();
    engine_title_render_scene(0x1f, /*page=*/5);
    /* Background drawn; "Play Now!" page drawn (panel called once); the
     * generic page hook is NOT invoked for page 5. */
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_last_page(), 0);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_a(), 1);
    return 0;
}

int test_title_scene_out_of_range_page_renders_nothing_but_finalizes(void)
{
    title_reset();
    engine_title_render_scene(0x1f, /*page=*/0xe);   /* > 0xd */
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_panel_call_count(), 0);
    T_ASSERT_EQ_I(engine_title_render_last_page(), 0);
    /* Finalize still runs (retail sets the flags unconditionally in 0x1f). */
    T_ASSERT_EQ_I(engine_title_render_scene_flag_b(), 1);
    return 0;
}

int test_title_page_selector_roundtrips(void)
{
    /* DAT_005530f0 — the seam (engine.c) sources the title page from here. */
    engine_title_render_set_page(1);
    T_ASSERT_EQ_I(engine_title_render_page(), 1);
    engine_title_render_set_page(0xc);
    T_ASSERT_EQ_I(engine_title_render_page(), 0xc);
    engine_title_render_set_page(0);
    T_ASSERT_EQ_I(engine_title_render_page(), 0);
    return 0;
}
