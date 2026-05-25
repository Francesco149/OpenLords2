/*
 * OpenLords2 — text-line renderer (implementation).
 *
 * See gfx_text.h for the function map, state model, and quirk
 * narrative.  Each function matches its retail body statement-for-
 * statement; control flow and order-of-writes match the Ghidra decomp.
 */

#include "gfx_text.h"

#include "font_metrics.h"
#include "gfx_dispatch.h"
#include "gfx_glyph.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Renderer-private state.  Each block is grouped by which retail DAT_*
 * symbol it shadows.  All zero-initialized at startup — matches the
 * BSS layout retail relies on.
 * ────────────────────────────────────────────────────────────────── */

/* Render-mode flags. */
static int g_no_shadow;             /* DAT_005aea40 */
static int g_outline;               /* DAT_0058fe9c */
static int g_tight_shadow;          /* DAT_005aeb90 */
static int g_uppercase_highlight;   /* DAT_0058fe2c */
static int g_collapse_spaces;       /* DAT_005aeb7c */
static int g_language_tag;          /* DAT_004eac50 */

/* Substitution stream. */
#define GFX_TEXT_SUBSTITUTION_BUF_LEN 256
static uint8_t g_substitution_buf[GFX_TEXT_SUBSTITUTION_BUF_LEN]; /* DAT_005bb4d0 */
static int     g_substitution_idx;                                /* DAT_005bb4b4 */
static int     g_substitution_on;                                 /* DAT_005bb4c4 */
static int     g_collapse_active;                                 /* DAT_005c925a */

/* First-position hook. */
static int g_first_pos_target;      /* DAT_005bb4a8 */
static int g_first_pos_latch;       /* DAT_005aeb64 */
static int g_first_pos_x;           /* DAT_0058fd58 */
static int g_first_pos_y;           /* DAT_0058fd54 */

/* Running accumulators. */
static int g_line_x_acc;            /* DAT_005cd404 */
static int g_line_y_acc;            /* DAT_005cd4f8 */
static int g_char_counter;          /* DAT_0058fd78 */

/* ──────────────────────────────────────────────────────────────────
 * Render-mode setters
 * ────────────────────────────────────────────────────────────────── */

void gfx_text_set_no_shadow(int enabled)           { g_no_shadow = enabled; }
void gfx_text_set_outline(int enabled)             { g_outline = enabled; }
void gfx_text_set_tight_shadow(int enabled)        { g_tight_shadow = enabled; }
void gfx_text_set_uppercase_highlight(int enabled) { g_uppercase_highlight = enabled; }
void gfx_text_set_collapse_spaces(int enabled)     { g_collapse_spaces = enabled; }
void gfx_text_set_language_tag(int tag)            { g_language_tag = tag; }

/* ──────────────────────────────────────────────────────────────────
 * Substitution-stream setters
 * ────────────────────────────────────────────────────────────────── */

void gfx_text_load_substitution_stream(const char *bytes)
{
    if (!bytes) {
        g_substitution_on = 0;
        return;
    }
    int n = 0;
    while (bytes[n] != '\0' && n < GFX_TEXT_SUBSTITUTION_BUF_LEN - 1) {
        g_substitution_buf[n] = (uint8_t)bytes[n];
        n++;
    }
    g_substitution_buf[n] = 0;
    g_substitution_idx = 0;
}

void gfx_text_enable_substitution(int enabled)
{
    g_substitution_on = enabled;
}

int gfx_text_substitution_cursor(void) { return g_substitution_idx; }

/* ──────────────────────────────────────────────────────────────────
 * First-position hook
 * ────────────────────────────────────────────────────────────────── */

void gfx_text_set_first_pos_target(int target_char_index) { g_first_pos_target = target_char_index; }
void gfx_text_clear_first_pos_latch(void)                 { g_first_pos_latch = 0; }
int  gfx_text_first_pos_latched(void)                     { return g_first_pos_latch; }
int  gfx_text_first_pos_x(void)                           { return g_first_pos_x; }
int  gfx_text_first_pos_y(void)                           { return g_first_pos_y; }

/* ──────────────────────────────────────────────────────────────────
 * Running accumulators
 * ────────────────────────────────────────────────────────────────── */

void gfx_text_set_line_x_acc(int x) { g_line_x_acc = x; }
void gfx_text_set_line_y_acc(int y) { g_line_y_acc = y; }
int  gfx_text_line_x_acc(void)      { return g_line_x_acc; }
int  gfx_text_line_y_acc(void)      { return g_line_y_acc; }

int  gfx_text_char_counter(void)    { return g_char_counter; }
void gfx_text_set_char_counter(int n) { g_char_counter = n; }

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00402c16 — pull one byte from the substitution stream
 * ────────────────────────────────────────────────────────────────── */

static uint8_t substitution_next(void)
{
    uint8_t c = g_substitution_buf[g_substitution_idx];
    if (c == 0) {
        /* NUL → wrap cursor to 0 and yield space.  Subsequent reads
         * would restart from buf[0] (caller can reload the buffer to
         * change the substitution mid-render). */
        g_substitution_idx = 0;
        c = ' ';
    } else {
        g_substitution_idx++;
    }
    return c;
}

/* ──────────────────────────────────────────────────────────────────
 * Helpers for the three shadow / outline render passes
 * ────────────────────────────────────────────────────────────────── */

/* All three "shadow" / "outline" / "tight-shadow" paths share the
 * structure "set cursor_y; set color; dispatch".  Wrap that in one
 * inline helper to keep the dispatcher logic clean.  Cursor_x is
 * unchanged across passes — only cursor_y and color vary. */
static int dispatch_pass(int cursor_y,
                         uint8_t color,
                         const uint8_t *font_base,
                         const uint8_t *char_glyph_table,
                         uint8_t char_code)
{
    gfx_glyph_set_cursor_y(cursor_y);
    gfx_glyph_set_color(color);
    return gfx_dispatch_glyph(font_base, char_glyph_table, char_code);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00402637 — text-line renderer
 * ────────────────────────────────────────────────────────────────── */

void gfx_text_render_line(const uint8_t *buf,
                          int cursor_x,
                          int cursor_y,
                          const uint8_t *font_base,
                          const uint8_t *char_glyph_table,
                          uint8_t color)
{
    /* Shadow color pair — defaults to (0x10, 0x1f); flips to (0x36, 0x2c)
     * for language tags 0x1c / 0x1f.  Retail writes local_1c / local_20
     * once per call. */
    uint8_t shadow_color_a = 0x10;
    uint8_t shadow_color_b = 0x1f;
    if (g_language_tag == 0x1f || g_language_tag == 0x1c) {
        shadow_color_a = 0x36;
        shadow_color_b = 0x2c;
    }

    /* `effective_color` is the per-character foreground color — defaults
     * to the caller's `color`, but the uppercase-highlight path may
     * override to 1.  Mirrors retail's `local_8`. */
    uint8_t effective_color = color;

    /* Initial color write — retail emits `DAT_0057d3bc = 0x10` before
     * the loop.  The first dispatch overwrites it; the explicit write is
     * a leftover from an older render shape but harmless.  Preserved for
     * fidelity. */
    gfx_glyph_set_color(0x10);

    /* Initial cursor_x — retail `DAT_00591524 = param_2`.  Subsequent
     * advances are RELATIVE to whatever the dispatcher left in cursor_x
     * (clip can mutate it). */
    gfx_glyph_set_cursor_x(cursor_x);

    uint8_t ch = buf ? *buf : 0;

    /* `last_width` carries between iterations — used by the consecutive-
     * space-collapse rewind path to undo the previous advance.  Initial
     * value mirrors retail's `local_14` (left uninitialized but
     * conventionally 0 before any glyph is dispatched). */
    int last_width = 0;

    while (ch != 0) {
        uint8_t prev_ch = ch;

        /* '#' macro — pull next byte from the substitution stream when
         * armed.  Otherwise render as space. */
        if (ch == '#') {
            if (g_substitution_on == 1) {
                ch = substitution_next();
            } else {
                ch = ' ';
            }
        }

        /* Visual-space alias.  Retail emits this AFTER the substitution
         * branch — so a substitution that yields '_' will also be
         * normalized to ' '. */
        if (ch == '_') {
            ch = ' ';
        }

        /* Uppercase-highlight override.  Reset to caller's color on the
         * default branch — `local_8 = param_5` in retail.  Only active
         * when the highlight flag is set AND the char is A..Z. */
        if (g_uppercase_highlight != 0 && ch > 0x40 && ch < 0x5b) {
            effective_color = 1;
        } else {
            effective_color = color;
        }

        /* Printable-glyph block — gated on char >= 0x20.  Control chars
         * (< 0x20) drop through to the loop-advance / collapse logic
         * unchanged. */
        if (ch >= 0x20) {
            uint8_t glyph_idx = (uint8_t)(ch - 0x20);

            /* First-position hook — captured exactly once per render
             * when char_counter matches the target and the latch is
             * clear.  Tests against the PRE-DISPATCH accumulator
             * values (matches retail). */
            if (g_char_counter == g_first_pos_target && g_first_pos_latch == 0) {
                g_first_pos_x = g_line_x_acc;
                g_first_pos_y = g_line_y_acc;
                g_first_pos_latch = 1;
            }

            int width = 0;

            if (char_glyph_table != 0 && char_glyph_table[glyph_idx] == 0) {
                /* No glyph for this char — retail emits the fixed 4-pixel
                 * advance.  Dispatcher is NOT called. */
                width = 4;
            } else if (g_no_shadow == 0) {
                /* Shadow / outline / tight-shadow paths.  All three pre-
                 * write effective_color into the dispatcher's color
                 * slot.  Cursor_x is left at the running value; cursor_y
                 * varies per pass. */
                if (g_outline == 0) {
                    if (g_tight_shadow == 0) {
                        /* Drop-shadow path — 3 passes. */
                        dispatch_pass(cursor_y - 1, shadow_color_a,
                                      font_base, char_glyph_table, glyph_idx);
                        dispatch_pass(cursor_y + 1, shadow_color_b,
                                      font_base, char_glyph_table, glyph_idx);
                        width = dispatch_pass(cursor_y, effective_color,
                                              font_base, char_glyph_table, glyph_idx);
                    } else {
                        /* Tight-shadow path — 2 passes.  Retail also
                         * bumps DAT_00591524 by +1 for the shadow pass
                         * and undoes it before the foreground pass. */
                        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + 1);
                        dispatch_pass(cursor_y + 1, 0x3f,
                                      font_base, char_glyph_table, glyph_idx);
                        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() - 1);
                        width = dispatch_pass(cursor_y, effective_color,
                                              font_base, char_glyph_table, glyph_idx);
                    }
                } else {
                    /* Outline path — 3 passes with hard-coded colors. */
                    dispatch_pass(cursor_y - 1, 0x3f,
                                  font_base, char_glyph_table, glyph_idx);
                    dispatch_pass(cursor_y + 1, 0x26,
                                  font_base, char_glyph_table, glyph_idx);
                    width = dispatch_pass(cursor_y, effective_color,
                                          font_base, char_glyph_table, glyph_idx);
                }
            } else {
                /* Plain path — single pass at the baseline cursor_y. */
                width = dispatch_pass(cursor_y, effective_color,
                                      font_base, char_glyph_table, glyph_idx);
            }

            /* Advance the running cursors by the width the dispatcher
             * returned (or 4 for the no-glyph fallback).  Dispatcher
             * returns `glyph_w + 1` per char. */
            gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + width);
            g_line_x_acc += width;
            last_width = width;
        }

        /* Loop advance — pull the next byte either from the source
         * buffer or from the substitution stream, depending on whether
         * the stream cursor has been engaged.  Once the stream is
         * engaged (idx != 0), it stays the source until the NUL
         * terminator wraps the cursor back to 0. */
        if (g_substitution_idx == 0) {
            buf++;
            ch = *buf;
        } else {
            ch = substitution_next();
        }

        g_char_counter++;

        /* Consecutive-space collapse.  Triggered when:
         *   - collapse_active (DAT_005c925a) is clear,
         *   - collapse_spaces (DAT_005aeb7c) is armed,
         *   - the previous char equals the just-loaded next char,
         *   - the next char is ' ' or '_'.
         * Effect: rewind the running cursors by the last width.  Keeps
         * adjacent spaces from accumulating in word-wrap text. */
        if (g_collapse_active == 0 && g_collapse_spaces != 0
            && prev_ch == ch && (ch == ' ' || ch == '_')) {
            g_line_x_acc -= last_width;
            gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() - last_width);
        }
    }

    /* Per-line padding — adds 4 to the running X accumulator regardless
     * of whether anything was drawn.  Lets word-wrap callers chain
     * fragments without breaking layout. */
    g_line_x_acc += 4;

    /* End-of-call resets.  The substitution stream stays loaded — only
     * the cursor and the on-flag are cleared.  The collapse_active flag
     * is also cleared.  The dispatcher's alt_clip flag is disarmed
     * (rect coords stay intact for the next caller that re-enables). */
    g_substitution_on  = 0;
    g_substitution_idx = 0;
    g_collapse_active  = 0;
    gfx_dispatch_set_alt_clip_enabled(0);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_004025d7 — centered text-line renderer
 * ────────────────────────────────────────────────────────────────── */

void gfx_text_render_line_centered(const uint8_t *buf,
                                   int cursor_x,
                                   int cursor_y,
                                   int width,
                                   const uint8_t *font_base,
                                   const uint8_t *char_glyph_table,
                                   const unsigned char *char_glyph_table_metrics,
                                   uint8_t color)
{
    /* Pre-measure via font_metrics — retail calls FUN_004014f0 which is
     * our font_metrics_buffer_width_bounded.  The measurer needs its own
     * state struct; we instantiate a temporary one since retail also
     * threads the call through a shared global the renderer doesn't
     * read. */
    font_metrics_state_t mstate;
    memset(&mstate, 0, sizeof mstate);
    mstate.char_glyph_index = char_glyph_table_metrics;

    int text_width = font_metrics_buffer_width_bounded(&mstate, buf, font_base);
    int offset = (width - text_width) / 2;
    if (offset < 0) {
        offset = 0;
    }

    gfx_text_render_line(buf, cursor_x + offset, cursor_y,
                         font_base, char_glyph_table, color);
}
