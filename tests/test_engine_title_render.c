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
    engine_title_render_scene(0x1f, /*page=*/6);
    /* Background drawn; page hook invoked with 6 (still-unported page);
     * neither the main menu nor the options page drawn. */
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_last_page(), 6);
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

/* ──────────────────────────────────────────────────────────────────
 * Page 4 — "Choose your title and your shield" (FUN_0041ef57 +
 * FUN_0041f321 / FUN_0041f1dd / FUN_0041f01f).
 *
 * The name-box frame (sprite 0xcc) and the five shields (0xcd/0xcf/…
 * + ring 0xd7) blit from the bound panel asset, so the page-4 tests
 * swap in a real sprite blob with valid descriptors for those indices.
 * Each touched sprite is a 4×4 block of the marker byte 0x77; tests
 * assert the marker lands at the retail blit positions.
 * ────────────────────────────────────────────────────────────────── */

#define P4_MARK 0x77
static uint8_t g_p4_blob[8 + 0xd8 * 16 + 256];

static void p4_blob_init(void)
{
    memset(g_p4_blob, 0, sizeof g_p4_blob);
    const int pix_off = 8 + 0xd8 * 16;
    for (int i = 0; i < 64; i++) g_p4_blob[pix_off + i] = P4_MARK;
    /* Touched sprite indices: box 0xcc, shields 0xcd/0xcf/0xd1/0xd3/0xd5,
     * ring 0xd7.  Give each a valid 4×4 descriptor. */
    const int idxs[] = {0xcc, 0xcd, 0xcf, 0xd1, 0xd3, 0xd5, 0xd7};
    for (unsigned k = 0; k < sizeof idxs / sizeof idxs[0]; k++) {
        uint8_t *slot = g_p4_blob + idxs[k] * 16 + 8;
        slot[0] = 4;  slot[1] = 0;       /* width  = 4 */
        slot[2] = 4;  slot[3] = 0;       /* height short = 4 (opaque rows) */
        slot[4] = (uint8_t)(pix_off & 0xff);
        slot[5] = (uint8_t)((pix_off >> 8) & 0xff);
        slot[6] = (uint8_t)((pix_off >> 16) & 0xff);
        slot[7] = (uint8_t)((pix_off >> 24) & 0xff);
        slot[0xd] = 4;                   /* row_count = 4 (transparent rows) */
    }
}

static text_input_state_t g_p4_test_editor;

static void p4_setup(void)
{
    title_reset();
    p4_blob_init();
    engine_title_render_bind_panel_asset(g_p4_blob);

    static const unsigned char slots[6] = {0, 1, 0, 0, 0, 0};
    engine_title_render_set_choose_title_gates(0, 0);
    engine_title_render_set_choose_title_continue_disabled(0);
    engine_title_render_set_choose_title_shields(slots, 1, 1, 0);

    static const uint8_t name[] = "aaa";
    memset(&g_p4_test_editor, 0, sizeof g_p4_test_editor);
    g_p4_test_editor.buf[0] = 'a';
    engine_title_render_set_choose_title_name(name);
    engine_title_render_set_choose_title_editor(&g_p4_test_editor);
    engine_title_render_bind_caret_metrics_table(&g_table[0]);
}

static int marker_at(int x, int y) { return g_canvas[x + y * CANVAS_W] == P4_MARK; }

int test_title_choose_title_panel_tall_when_gate_a_zero(void)
{
    p4_setup();
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_choose_title();

    /* FUN_00409346(asset, 0x50, 10, 0x1e, 0x10) — tall layout. */
    T_ASSERT_EQ_I(g_cap_calls, 1);
    T_ASSERT_EQ_I(g_cap_p2, 0x50);
    T_ASSERT_EQ_I(g_cap_p3, 10);
    T_ASSERT_EQ_I(g_cap_p4, 0x1e);
    T_ASSERT_EQ_I(g_cap_p5, 0x10);
    return 0;
}

int test_title_choose_title_panel_short_when_gated_off(void)
{
    p4_setup();
    engine_title_render_set_choose_title_gates(1, 0);   /* a!=0 && b==0 → short */
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_choose_title();

    T_ASSERT_EQ_I(g_cap_p5, 0xe);   /* short panel */
    /* No Back/Continue button row → no bevel-rect palette colors. */
    T_ASSERT_EQ_I(canvas_color_count(0x35), 0);
    T_ASSERT_EQ_I(canvas_color_count(0x28), 0);
    return 0;
}

int test_title_choose_title_name_box_blits_frame(void)
{
    p4_setup();
    engine_title_render_choose_title();
    /* Opaque box sprite 0xcc at (0xd0, 0x48), 4×4. */
    T_ASSERT(marker_at(0xd0, 0x48));
    T_ASSERT(marker_at(0xd3, 0x4b));
    return 0;
}

int test_title_choose_title_shields_blit_five_positions(void)
{
    p4_setup();
    engine_title_render_choose_title();
    /* Five shields at x = (i-1)*0x58 + 0x70, y = 0x8c. */
    for (int i = 1; i <= 5; i++) {
        int x = (i - 1) * 0x58 + 0x70;
        T_ASSERT(marker_at(x, 0x8c));
    }
    /* Selected slot 1 (flash on, timer 0) also blits the ring 0xd7 at
     * (0x74, 0x70). */
    T_ASSERT(marker_at(0x74, 0x70));
    return 0;
}

int test_title_choose_title_selected_flash_off_skips_ring(void)
{
    p4_setup();
    /* flash_timer != 0 → the selected slot's ring + sprite are skipped. */
    static const unsigned char slots[6] = {0, 1, 0, 0, 0, 0};
    engine_title_render_set_choose_title_shields(slots, 1, 1, /*timer=*/5);
    engine_title_render_choose_title();
    T_ASSERT(!marker_at(0x74, 0x70));      /* no ring */
    T_ASSERT(!marker_at(0x70, 0x8c));      /* slot 1 sprite skipped too */
    /* Other (empty) slots still draw. */
    T_ASSERT(marker_at(0xc8, 0x8c));
    return 0;
}

int test_title_choose_title_buttons_back_highlight(void)
{
    p4_setup();
    engine_title_render_set_selected_button(6);   /* Back is select id 6 */
    engine_title_render_choose_title();
    /* Back label highlight 0xf9 in the left button x-range at row 0xdc. */
    T_ASSERT(row_color_count_x(0xdc, 0xf9, 0x70, 0x70 + 0xc0) > 0);
    /* Continue not highlighted. */
    T_ASSERT_EQ_I(row_color_count_x(0xdc, 0xf9, 0x150, 0x150 + 0xc0), 0);
    return 0;
}

int test_title_choose_title_continue_dim_when_disabled(void)
{
    p4_setup();
    engine_title_render_set_choose_title_continue_disabled(1);  /* → 0x20 */
    engine_title_render_set_selected_button(0);                 /* neutral */
    engine_title_render_choose_title();
    /* Continue label drawn dim (0x20) in the right button x-range. */
    T_ASSERT(row_color_count_x(0xdc, 0x20, 0x150, 0x150 + 0xc0) > 0);
    return 0;
}

int test_title_choose_title_buttons_two_bevel_rects(void)
{
    p4_setup();
    engine_title_render_choose_title();
    /* Back + Continue bevel rects → palette-b colors present. */
    T_ASSERT(canvas_color_count(0x35) > 0);
    T_ASSERT(canvas_color_count(0x28) > 0);
    return 0;
}

int test_title_choose_title_scene_routes_page_4(void)
{
    p4_setup();
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_set_page(4);
    engine_title_render_scene(0x1f, 4);
    /* The page-4 renderer ran (panel with the page-4 args). */
    T_ASSERT_EQ_I(g_cap_calls, 1);
    T_ASSERT_EQ_I(g_cap_p2, 0x50);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Page 3 — "Loading a game." save/load list (FUN_004148e4 +
 * FUN_004149ec, title variant).  The frame draws via gfx_panel_draw /
 * gfx_panel_fill_interior, which read 16×16 tile descriptors from the
 * bound asset — so use a blob with EVERY sprite index valid (a 4×4
 * tile pointing at one pixel block).  Entry names render with the 1×1
 * test font.
 * ────────────────────────────────────────────────────────────────── */

static uint8_t g_fl_blob[8 + 0x100 * 16 + 256];

static void fl_blob_init(void)
{
    memset(g_fl_blob, 0, sizeof g_fl_blob);
    const int pix_off = 8 + 0x100 * 16;
    for (int i = 0; i < 64; i++) g_fl_blob[pix_off + i] = 0x44;
    for (int idx = 0; idx < 0x100; idx++) {
        uint8_t *slot = g_fl_blob + idx * 16 + 8;
        slot[0] = 4; slot[2] = 4;                 /* w=4, h_short=4 */
        slot[4] = (uint8_t)(pix_off & 0xff);
        slot[5] = (uint8_t)((pix_off >> 8) & 0xff);
        slot[6] = (uint8_t)((pix_off >> 16) & 0xff);
        slot[0xd] = 4;                             /* row_count */
    }
}

static const char *g_fl_test_names[4] = {"lastturn", "old_turn", "safeturn", "test"};
static uint8_t g_fl_test_entries[4 * 0x41];
static const uint8_t g_fl_test_top[] = "lastturn";

static void fl_setup(void)
{
    title_reset();
    fl_blob_init();
    engine_title_render_bind_panel_asset(g_fl_blob);
    memset(g_fl_test_entries, 0, sizeof g_fl_test_entries);
    for (int e = 0; e < 4; e++) {
        int j = 0;
        for (; g_fl_test_names[e][j]; j++)
            g_fl_test_entries[e * 0x41 + j] = (uint8_t)g_fl_test_names[e][j];
    }
    engine_title_render_set_filelist(g_fl_test_entries, 0x41, 4, 0);
    engine_title_render_set_filelist_topname(g_fl_test_top);
    engine_title_render_set_filelist_editor(0);
    engine_title_render_set_filelist_show_status(0);
}

int test_title_filelist_panel_and_title(void)
{
    fl_setup();
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_filelist(5);

    /* FUN_00409346(asset, 0x60, 10, 0x1c, 0x15). */
    T_ASSERT_EQ_I(g_cap_calls, 1);
    T_ASSERT_EQ_I(g_cap_p2, 0x60);
    T_ASSERT_EQ_I(g_cap_p3, 10);
    T_ASSERT_EQ_I(g_cap_p4, 0x1c);
    T_ASSERT_EQ_I(g_cap_p5, 0x15);
    /* Action-button bevel rect (palette-b colors) present. */
    T_ASSERT(canvas_color_count(0x35) > 0);
    T_ASSERT(canvas_color_count(0x28) > 0);
    return 0;
}

int test_title_filelist_renders_grid_text(void)
{
    fl_setup();
    engine_title_render_filelist(5);
    /* Row 0 of the grid is at y = 0x76; the 1×1 font renders the entry
     * names there in color 0x3f.  Count only within the grid interior
     * [0x80, 0x1d0) so the 0x3f box outline edges (x 0x78 / 0x1d8) don't
     * confound the result. */
    T_ASSERT(row_color_count_x(0x76, 0x3f, 0x80, 0x1d0) > 0);
    /* Row 1 (y = 0x86) carries the 4th entry ("test"). */
    T_ASSERT(row_color_count_x(0x86, 0x3f, 0x80, 0x1d0) > 0);
    return 0;
}

int test_title_filelist_empty_no_grid_text(void)
{
    fl_setup();
    engine_title_render_set_filelist(g_fl_test_entries, 0x41, 0, 0);  /* count 0 */
    engine_title_render_filelist(5);
    /* No entries → no filename glyphs in the grid interior (box-outline
     * edges at x 0x78 / 0x1d8 are excluded by the x range). */
    T_ASSERT_EQ_I(row_color_count_x(0x76, 0x3f, 0x80, 0x1d0), 0);
    T_ASSERT_EQ_I(row_color_count_x(0x86, 0x3f, 0x80, 0x1d0), 0);
    return 0;
}

int test_title_filelist_scene_routes_page_3(void)
{
    fl_setup();
    g_cap_calls = 0;
    engine_title_render_bind_panel_fn(capture_panel);
    engine_title_render_set_page(3);
    engine_title_render_scene(0x1f, 3);
    T_ASSERT_EQ_I(g_cap_calls, 1);
    T_ASSERT_EQ_I(g_cap_p2, 0x60);   /* page-3 panel args */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Page 7 — game-setup screen (FUN_0041f6c7 + subs)
 * ────────────────────────────────────────────────────────────────── */

/* A minimal valid chrome PL8 atlas (mirrors test_gfx_panel's atlas):
 * indices 0..0xf4 each a 16×16 tile of marker (i+1).  Big enough for the
 * mode-1 bevel (+0xcc shift) the option tabs draw. */
enum { P7_MAX_IDX = 0xf5 };
static uint8_t g_p7_chrome[8 + (P7_MAX_IDX + 1) * 0x10 + (P7_MAX_IDX + 1) * 256];

static void p7_chrome_build(void)
{
    memset(g_p7_chrome, 0, sizeof g_p7_chrome);
    int pix_base = 8 + (P7_MAX_IDX + 1) * 0x10;
    for (int i = 0; i <= P7_MAX_IDX; i++) {
        uint8_t *d = g_p7_chrome + 8 + i * 0x10;
        int off = pix_base + i * 256;
        d[0] = 0x10; d[2] = 0x10;
        d[4] = off & 0xff; d[5] = (off >> 8) & 0xff;
        d[6] = (off >> 16) & 0xff; d[7] = (off >> 24) & 0xff;
        d[0xd] = 0x10;
        memset(g_p7_chrome + off, (uint8_t)(i + 1), 256);
    }
}

/* Page-7 localization: entry 0x65 country names, 0x66 tab headers,
 * 0x67 tab values — all single printable token "C"/"H"/"V" per slot so
 * the 1×1 font emits one pixel of the call's color per sub-string. */
static const char *g_p7_e65[8] = { "C","C","C","C","C","C","C","C" };
static const char *g_p7_e66[12] = {
    "H","H","H","H","H","H","H","H","H","H","H","H" };
static const char *g_p7_e67[64];
static engine_title_setup_player_t g_p7_seats[6];
static uint8_t g_p7_names_blob[6 * 0x2c];

static void p7_setup(void)
{
    title_reset();
    for (int i = 0; i < 64; i++) g_p7_e67[i] = "V";
    blob_add_entry(g_blob, 0x65, g_p7_e65, 8, 9000);
    blob_add_entry(g_blob, 0x66, g_p7_e66, 12, 9200);
    blob_add_entry(g_blob, 0x67, g_p7_e67, 64, 9600);
    p7_chrome_build();
    engine_title_render_bind_chrome_asset(g_p7_chrome);
    engine_title_render_bind_palette(NULL);
    engine_title_render_set_preview(NULL);
}

/* Country list: five row-fill rects at x=0x1f0 w=0x69; the selected row
 * is bright (0x3f) fill, the rest dark (0x20).  Plus the dirty mark. */
int test_title_setup_country_list_fills_and_selection(void)
{
    p7_setup();
    /* count 8, scroll 0, select visible row 2. */
    engine_title_render_set_country_list((const uint8_t *)"\0\1\2\3\4\5\6\7",
                                         0, 2, 8);
    engine_title_render_setup_country_list();

    /* Row 2 (y = 2*0x10 + 0x8d = 0xad) interior is the bright fill 0x3f. */
    T_ASSERT(row_color_count_x(0xad, 0x3f, 0x1f0, 0x1f0 + 0x69) > 0);
    /* Row 0 (y = 0x8d) is the dark fill 0x20. */
    T_ASSERT(row_color_count_x(0x8d, 0x20, 0x1f0, 0x1f0 + 0x69) > 0);
    /* The list dirty box is marked at (0x1e0, 0). */
    T_ASSERT_EQ_I(g_dirty.dirty_mark, 1);
    T_ASSERT_EQ_I(g_dirty.dirty_xmin, 0x1e0);
    return 0;
}

/* Scrollbar: with scroll 0 the top filler is empty and the thumb sits at
 * the track top (y = 0xa3); scrolled down, a top filler appears above it. */
int test_title_setup_country_list_scrollbar_tracks_scroll(void)
{
    p7_setup();
    engine_title_render_set_country_list((const uint8_t *)"\0\1\2\3\4\5\6\7",
                                         0, 0, 8);
    engine_title_render_setup_country_list();
    /* scroll 0 → no top filler (0x20) above the thumb start row 0xa3. */
    int top0 = 0;
    for (int y = 0xa3; y < 0xa3 + 0x2c; y++)
        top0 += row_color_count_x(y, 0x3f, 0x25c, 0x25c + 0x14);
    T_ASSERT(top0 > 0);   /* thumb (0x3f) present */

    p7_setup();
    engine_title_render_set_country_list((const uint8_t *)"\0\1\2\3\4\5\6\7",
                                         3, 0, 8);  /* scrolled to 3/8 */
    engine_title_render_setup_country_list();
    /* scroll 3/8 of 0x2c ≈ 16 px of 0x20 top filler at the track top. */
    T_ASSERT(row_color_count_x(0xa3, 0x20, 0x25c, 0x25c + 0x14) > 0);
    return 0;
}

/* Roster: an open seat with no portrait is skipped (no name pixels at its
 * row), an occupied seat draws its centered name. */
int test_title_setup_roster_draws_occupied_skips_empty(void)
{
    p7_setup();
    memset(g_p7_seats, 0, sizeof g_p7_seats);
    memset(g_p7_names_blob, 0, sizeof g_p7_names_blob);
    /* Seat 1: human (type 0, portrait_sel != 0 → drawn). Name "NN". */
    g_p7_seats[1] = (engine_title_setup_player_t){ 0, 1, 0x3f, 1, 1 };
    memcpy(g_p7_names_blob + 1 * 0x2c, "NN", 2);
    /* Seat 2: open, no portrait → skipped (no row). */
    g_p7_seats[2] = (engine_title_setup_player_t){ 0, 0, 0x3f, 1, 1 };
    /* Seat 3: AI (type 2) → drawn as the SECOND visible row. */
    g_p7_seats[3] = (engine_title_setup_player_t){ 2, 0, 0x3f, 2, 1 };
    memcpy(g_p7_names_blob + 3 * 0x2c, "AA", 2);

    engine_title_render_set_players(g_p7_seats, g_p7_names_blob);
    engine_title_render_setup_roster();

    /* Row 0 name baseline y = 0x50; row 1 (next drawn seat) y = 0x5e+0x50. */
    T_ASSERT(row_color_count_x(0x50, 0x3f, 0, 0xa0) > 0);          /* seat 1 */
    T_ASSERT(row_color_count_x(0x5e + 0x50, 0x3f, 0, 0xa0) > 0);   /* seat 3 */
    /* A third drawn row would be at 2*0x5e+0x50 = 0xfc — none (seat 2
     * skipped, only two seats drew). */
    T_ASSERT_EQ_I(row_color_count_x(2 * 0x5e + 0x50, 0x3f, 0, 0xa0), 0);
    return 0;
}

/* Option tabs: every non-active tab draws its red (0xf9) value label; the
 * active tab draws no value label. */
int test_title_setup_tabs_value_label_skips_active(void)
{
    p7_setup();
    int vals[12] = { 0 };
    engine_title_render_set_tab_values(vals, /*active=*/4);
    engine_title_render_setup_tabs();

    /* Tab 4 geometry: x=0x122, value label at y = 0x161 + 0x10 = 0x171.
     * No 0xf9 pixels there (active tab → value label skipped). */
    T_ASSERT_EQ_I(row_color_count_x(0x171, 0xf9, 0x122, 0x122 + 0x60), 0);
    /* Tab 0 (non-active): value label at y = 0x118 + 0x10 = 0x128. */
    T_ASSERT(row_color_count_x(0x128, 0xf9, 0xaa, 0xaa + 0x60) > 0);
    return 0;
}

/* Scene dispatch routes page 7 to the setup renderer, NOT the page hook. */
int test_title_setup_scene_routes_page_7(void)
{
    p7_setup();
    engine_title_render_set_country_list((const uint8_t *)"\0\1\2\3\4\5\6\7",
                                         0, 0, 8);
    engine_title_render_set_players(g_p7_seats, g_p7_names_blob);
    int vals[12] = { 0 };
    engine_title_render_set_tab_values(vals, -1);
    engine_title_render_reset_counters();        /* clears last_page */
    engine_title_render_scene(0x1f, 7);
    /* Page hook NOT invoked (last_page stays 0) → routed to setup_screen. */
    T_ASSERT_EQ_I(engine_title_render_last_page(), 0);
    /* And the country-list fills landed on the canvas. */
    T_ASSERT(canvas_color_count(0x20) > 0);
    return 0;
}
