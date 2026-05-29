/*
 * Unit tests for gfx_caret — the text-input caret renderer
 * (FUN_0040acce).
 *
 * The caret reads its anchor from gfx_text (first_pos_x/y), the cursor
 * char + insert-mode from a text_input editor, the cursor-char width
 * from font_metrics, and paints with gfx_line_fill_rect onto the bound
 * gfx_paint canvas.  Fixtures stage all four.
 */

#include "t.h"
#include "gfx_caret.h"
#include "gfx_text.h"
#include "gfx_paint.h"
#include "text_input.h"
#include "font_metrics.h"

#include <string.h>

enum { CANVAS_W = 64, CANVAS_H = 48 };

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

static uint8_t at(const canvas_t *c, int x, int y) { return c->pixels[x + y * CANVAS_W]; }
static int any_painted(const canvas_t *c)
{
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) if (c->pixels[i]) return 1;
    return 0;
}

/* Char-table is 1-based: entry 'a' (0x61) = 2 → 0-based glyph 1;
 * others missing (0). */
static unsigned char g_char_table[256];
/* Font blob: glyph 1 width = 0*256 + 5 + 1 = 6. */
static unsigned char g_font[64];

static void fixtures_init(void)
{
    memset(g_char_table, 0, sizeof g_char_table);
    g_char_table[0x61] = 2;                 /* 'a' → 0-based glyph 1 */
    memset(g_font, 0xee, sizeof g_font);
    g_font[1 * 16 + 8] = 5; g_font[1 * 16 + 9] = 0;   /* width 6 */
}

/* Zero a font-metrics scratch state and bind the char-glyph table. */
static void fm_init(font_metrics_state_t *fm)
{
    memset(fm, 0, sizeof *fm);
    fm->char_glyph_index = g_char_table;
}

static void editor_with_a(text_input_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->buf[0] = 'a';
    s->cursor = 0;
    s->insert_mode = 0;        /* overwrite → underscore caret */
}

/* ──────────────────────────────────────────────────────────────── */

int test_gfx_caret_off_phase_draws_nothing(void)
{
    canvas_t c; canvas_init(&c);
    fixtures_init();
    text_input_state_t e; editor_with_a(&e);
    font_metrics_state_t fm; fm_init(&fm);

    gfx_caret_reset();
    gfx_text_set_first_pos_x(20);
    gfx_text_set_first_pos_y(10);

    /* First call → blink counter 1 (<= 8): measured but not drawn. */
    gfx_caret_render(&e, &fm, g_font, 0x3f);

    T_ASSERT_EQ_I(gfx_caret_blink(), 1);
    T_ASSERT(!any_painted(&c));
    /* The measure side effect still ran (cursor char 'a' → glyph 1). */
    T_ASSERT_EQ_I(fm.last_glyph_index, 1);
    return 0;
}

int test_gfx_caret_anchor_y_zero_skips_measure_and_draw(void)
{
    canvas_t c; canvas_init(&c);
    fixtures_init();
    text_input_state_t e; editor_with_a(&e);
    font_metrics_state_t fm; fm_init(&fm);
    fm.last_glyph_index = 0x55;     /* sentinel — must stay untouched */

    gfx_caret_reset();
    gfx_text_set_first_pos_x(20);
    gfx_text_set_first_pos_y(0);    /* anchor y == 0 → early return */

    gfx_caret_render(&e, &fm, g_font, 0x3f);

    T_ASSERT_EQ_I(gfx_caret_blink(), 1);   /* counter still advanced */
    T_ASSERT(!any_painted(&c));
    T_ASSERT_EQ_I(fm.last_glyph_index, 0x55);  /* measure NOT called */
    return 0;
}

int test_gfx_caret_visible_phase_draws_underscore(void)
{
    canvas_t c; canvas_init(&c);
    fixtures_init();
    text_input_state_t e; editor_with_a(&e);   /* overwrite mode */
    font_metrics_state_t fm; fm_init(&fm);

    /* Force the counter to 9 (first visible frame): set to 8, one
     * render advances to 9. */
    gfx_caret_reset();
    for (int i = 0; i < 8; i++) {
        gfx_text_set_first_pos_y(0);       /* keep these calls no-draw */
        gfx_caret_render(&e, &fm, g_font, 0x3f);
    }
    T_ASSERT_EQ_I(gfx_caret_blink(), 8);

    /* Now stage the real anchor and render: counter → 9 (> 8), draws. */
    gfx_text_set_first_pos_x(20);
    gfx_text_set_first_pos_y(10);
    gfx_caret_render(&e, &fm, g_font, 0x3f);
    T_ASSERT_EQ_I(gfx_caret_blink(), 9);

    /* Underscore: fill_rect(x-1, y+0xe, char_w+2, 2).  char_w('a')=6,
     * so a (6+2)=8-wide × 2-tall bar at (19, 24). */
    T_ASSERT_EQ_U(at(&c, 19, 24), 0x3f);
    T_ASSERT_EQ_U(at(&c, 26, 24), 0x3f);   /* x = 19 + 8 - 1 */
    T_ASSERT_EQ_U(at(&c, 19, 25), 0x3f);
    T_ASSERT_EQ_U(at(&c, 26, 25), 0x3f);
    /* One row above / below the bar is clear. */
    T_ASSERT_EQ_U(at(&c, 19, 23), 0x00);
    T_ASSERT_EQ_U(at(&c, 19, 26), 0x00);
    /* Just past the right edge is clear. */
    T_ASSERT_EQ_U(at(&c, 27, 24), 0x00);
    return 0;
}

int test_gfx_caret_blink_wraps_past_0x10(void)
{
    fixtures_init();
    text_input_state_t e; editor_with_a(&e);
    font_metrics_state_t fm; fm_init(&fm);

    gfx_caret_reset();
    gfx_text_set_first_pos_y(0);           /* no-draw calls */
    for (int i = 0; i < 0x10; i++) gfx_caret_render(&e, &fm, g_font, 0x3f);
    T_ASSERT_EQ_I(gfx_caret_blink(), 0x10);
    /* 17th call: counter would be 0x11 → wraps to 0. */
    gfx_caret_render(&e, &fm, g_font, 0x3f);
    T_ASSERT_EQ_I(gfx_caret_blink(), 0);
    return 0;
}

int test_gfx_caret_insert_mode_draws_box_outline(void)
{
    canvas_t c; canvas_init(&c);
    fixtures_init();
    text_input_state_t e; editor_with_a(&e);
    e.insert_mode = 1;                      /* insert → box outline */
    font_metrics_state_t fm; fm_init(&fm);

    gfx_caret_reset();
    gfx_text_set_first_pos_y(0);
    for (int i = 0; i < 8; i++) gfx_caret_render(&e, &fm, g_font, 0x3f);

    gfx_text_set_first_pos_x(20);
    gfx_text_set_first_pos_y(10);
    gfx_caret_render(&e, &fm, g_font, 0x20);

    /* Left vertical bar: fill_rect(x-2, y-2, 1, 0xf) → column 18,
     * rows 8..22. */
    T_ASSERT_EQ_U(at(&c, 18, 8),  0x20);
    T_ASSERT_EQ_U(at(&c, 18, 22), 0x20);
    /* Top stub: fill_rect(x-4, y-3, 2, 1) → (16..17, 7). */
    T_ASSERT_EQ_U(at(&c, 16, 7),  0x20);
    T_ASSERT_EQ_U(at(&c, 17, 7),  0x20);
    /* Bottom-right stub: fill_rect(x-1, y+0xe, 2, 1) → (19..20, 24). */
    T_ASSERT_EQ_U(at(&c, 19, 24), 0x20);
    T_ASSERT_EQ_U(at(&c, 20, 24), 0x20);
    return 0;
}
