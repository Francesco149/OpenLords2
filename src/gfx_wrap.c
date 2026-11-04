/*
 * OpenLords2 — word-wrap renderer + localization-string render wrappers.
 *
 * See gfx_wrap.h for the function map, state model, and quirk
 * narratives.  Each function below matches its retail body
 * statement-for-statement; control flow and order-of-writes match
 * the Ghidra decomp.
 */

#include "gfx_wrap.h"
#include "gfx_globals.h"

#include "font_metrics.h"
#include "gfx_dispatch.h"
#include "gfx_text.h"
#include "localization.h"

#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * State: retail globals (gfx_globals.h) — the max line x, and the two
 * intermediate line buffers DAT_005c92a0 (2000 bytes, used by
 * gfx_wrap_localized) and DAT_005aea60 (200 bytes, used by
 * gfx_wrap_buffer).  Not re-entrant.  The tall-font marker is the
 * port's binding: retail compares the font argument against the
 * address of its tall font blob (&DAT_005b2fa0).
 * ────────────────────────────────────────────────────────────────── */

static const uint8_t *g_tall_font_marker;    /* == &DAT_005b2fa0 in retail */

int  gfx_wrap_max_line_x(void)             { return g_gfx_max_line_x; }
void gfx_wrap_set_max_line_x(int v)        { g_gfx_max_line_x = v; }
void gfx_wrap_set_tall_font_marker(const uint8_t *m) { g_tall_font_marker = m; }

/* ──────────────────────────────────────────────────────────────────
 * Shared helpers
 * ────────────────────────────────────────────────────────────────── */

/* Per-line Y advance — 24 px for the tall-font marker, 16 px else.
 * Applied to both the per-call local cursor and the global
 * `gfx_text_line_y_acc` after every line.  Mirrors the
 * `param_9 == &DAT_005b2fa0 ? 0x18 : 0x10` shape in retail. */
static int line_height_for(const uint8_t *font_base)
{
    return (g_tall_font_marker != 0 && font_base == g_tall_font_marker)
           ? 0x18 : 0x10;
}

/* Arm the per-line render: substitution_on = 1, line_x_acc = 0,
 * collapse_active = 1.  Retail writes these THREE registers
 * immediately before every call to FUN_00402637 from within the
 * wrap functions; the per-line renderer clears them at its epilogue.
 *
 * `collapse_active = 1` keeps the per-line renderer's consecutive-
 * space collapse path disarmed — wrap is already chopping at word
 * boundaries, so the inner collapse would mis-fire on spaces that
 * survived the leading-space filter. */
static void arm_line(void)
{
    gfx_text_enable_substitution(1);
    gfx_text_set_line_x_acc(0);
    gfx_text_set_collapse_active(1);
}

/* Update the wrap-module's max_line_x with the line that was just
 * rendered.  Retail: `if (DAT_005aeb88 < DAT_005cd404) DAT_005aeb88
 * = DAT_005cd404;`. */
static void track_max_line_x(void)
{
    int line_x = gfx_text_line_x_acc();
    if (g_gfx_max_line_x < line_x) {
        g_gfx_max_line_x = line_x;
    }
}

/* Advance both the local Y cursor (returned) and the global
 * line_y_acc by the per-line Y step.  Retail does both writes
 * inline; pulling them into one helper keeps the wrap bodies
 * readable. */
/* Retail's word measurer (FUN_004036f9) and the char measurer it calls
 * (FUN_004015b9) cache into SHARED globals — the consumed-byte count
 * DAT_005c9a7c, the last glyph index DAT_005c9288 (the dispatcher's
 * too) and its offset DAT_0058fd88 — so each measurement runs on them
 * (rhost shadow; render-under-rhost.md). */
static int word_width(font_metrics_state_t *m, const uint8_t *p, const uint8_t *font)
{
    m->last_glyph_index  = g_gfx_last_glyph_index;
    m->last_glyph_offset = g_gfx_last_glyph_offset;
    m->chars_consumed    = g_gfx_word_chars;
    int w = font_metrics_word_width(m, p, font);
    g_gfx_last_glyph_index  = m->last_glyph_index;
    g_gfx_last_glyph_offset = m->last_glyph_offset;
    g_gfx_word_chars        = m->chars_consumed;
    return w;
}

static int advance_y(int local_y, const uint8_t *font_base)
{
    int step = line_height_for(font_base);
    gfx_text_set_line_y_acc(gfx_text_line_y_acc() + step);
    return local_y + step;
}

/* Seek the localization walker to the start of sub-string
 * `loc_index` then advance past `skip_strings` NUL boundaries and
 * skip leading control bytes.  Returns a pointer into `loc->blob`,
 * or NULL when the initial offset is out of bounds.  Mirrors the
 * top of FUN_00402d37 / FUN_00402c5e / FUN_0040328e. */
static const uint8_t *seek_localized(localization_state_t *loc,
                                     int loc_index,
                                     int skip_strings)
{
    /* Unbound localization (blob not yet loaded) — no string.  Retail
     * never reaches this (the l2.eng blob is loaded at boot before any
     * localized text renders), but the drop-in can invoke the render
     * chain during bring-up before the blob is bound; guard it the same
     * defensive way gfx_paint guards an unbound canvas. */
    if (loc->blob == 0) {
        return 0;
    }

    /* `localization_seek_skip` already implements the seek + skip +
     * leading-control-skip path with the NUL-after-NUL boundary
     * mode (quirk #20).  When the initial offset is out of bounds
     * it sets walker_offset and returns without walking; we mirror
     * that as a NULL return here so callers can short-circuit. */
    int offset = localization_offset(loc->blob, loc_index);
    if ((size_t)offset >= (size_t)LOCALIZATION_BLOB_SIZE + 1) {
        loc->walker_offset = (size_t)offset;
        return 0;
    }
    localization_seek_skip(loc, loc_index, skip_strings);
    return loc->blob + loc->walker_offset;
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00402d37 — single-line localized render
 * ────────────────────────────────────────────────────────────────── */

void gfx_wrap_localized_line(localization_state_t *loc,
                             int loc_index,
                             int skip_strings,
                             int cursor_x,
                             int cursor_y,
                             const uint8_t *font_base,
                             const uint8_t *char_glyph_table,
                             uint8_t color)
{
    const uint8_t *walker = seek_localized(loc, loc_index, skip_strings);
    if (walker == 0) {
        return;
    }
    gfx_text_render_line(walker, cursor_x, cursor_y,
                         font_base, char_glyph_table, color);
    /* Retail's `DAT_005aeb84 = 0;` at the end.  The per-line
     * renderer already clears it in its epilogue, but retail writes
     * it explicitly here too — preserved for fidelity (and as a
     * safety net if the renderer ever short-circuits). */
    gfx_dispatch_set_alt_clip_enabled(0);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00402c5e — single-line localized centered render
 * ────────────────────────────────────────────────────────────────── */

void gfx_wrap_localized_line_centered(localization_state_t *loc,
                                      int loc_index,
                                      int skip_strings,
                                      int cursor_x,
                                      int cursor_y,
                                      int width,
                                      const uint8_t *font_base,
                                      const uint8_t *char_glyph_table,
                                      const unsigned char *char_glyph_table_metrics,
                                      uint8_t color)
{
    const uint8_t *walker = seek_localized(loc, loc_index, skip_strings);
    if (walker == 0) {
        return;
    }
    gfx_text_render_line_centered(walker, cursor_x, cursor_y, width,
                                  font_base, char_glyph_table,
                                  char_glyph_table_metrics, color);
    gfx_dispatch_set_alt_clip_enabled(0);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040352f — multi-line buffer word-wrap
 *
 * Inner loop structure (preserved verbatim from retail):
 *   - For each line (cap 99):
 *       1. Zero the 200-byte intermediate buffer.
 *       2. While the accumulated pixel width is < line_width:
 *          - Measure the NEXT word's width via word_width.  This
 *            also writes `chars_consumed` into the font_metrics
 *            state.
 *          - If accumulated width is still < line_width AFTER the
 *            measurement: copy `chars_consumed` bytes from the
 *            source into the line buffer, advance the source.
 *            If the source byte AT the new walker position is NUL,
 *            terminate after this line (bVar2 = false).
 *          - Else if accumulated width is exactly 0 (first measure
 *            returned a word wider than the budget): no progress
 *            possible — terminate.
 *       3. Arm the per-line render (substitution on, line_x_acc=0,
 *          collapse_active=1) and call gfx_text_render_line.
 *       4. Track max_line_x against line_x_acc.
 *       5. Advance local cursor_y and global line_y_acc by the
 *          per-font line height.
 *
 * Quirks preserved:
 *   - Hard cap of 99 lines (the 100-iteration safety limit).
 *   - max_line_x is NOT reset at the top (caller-owned reset).
 *   - No leading-space suppression — spaces at the start of a
 *     wrapped line are kept (quirk #37).
 *   - The "first measured word doesn't fit AND nothing is in the
 *     line yet" termination condition matches retail's
 *     `local_c == 0` branch.
 * ────────────────────────────────────────────────────────────────── */

void gfx_wrap_buffer(const uint8_t *buf,
                     int cursor_x,
                     int cursor_y,
                     int line_width,
                     const uint8_t *font_base,
                     const uint8_t *char_glyph_table,
                     const unsigned char *char_glyph_table_metrics,
                     uint8_t color)
{
    font_metrics_state_t mstate;
    memset(&mstate, 0, sizeof mstate);
    mstate.char_glyph_index = char_glyph_table_metrics;

    int continue_outer = 1;
    int local_y = cursor_y;
    int safety = 0;

    while (continue_outer && (++safety < 100)) {
        /* Zero the 200-byte intermediate buffer.  Retail walks
         * 0..199 unconditionally. */
        memset(g_gfx_wrap_small_buf, 0, GFX_WRAP_SMALL_LINE_BUF_LEN);

        int line_pixel = 0;
        int line_cursor = 0;

        while (continue_outer && line_pixel < line_width) {
            int word_w = word_width(&mstate, buf, font_base);
            line_pixel += word_w;

            if (line_pixel < line_width) {
                /* Word fits — copy `chars_consumed` bytes. */
                for (int i = 0; i < mstate.chars_consumed; i++) {
                    uint8_t b = *buf++;
                    if (line_cursor < GFX_WRAP_SMALL_LINE_BUF_LEN) {
                        g_gfx_wrap_small_buf[line_cursor] = b;
                    }
                    line_cursor++;
                }
                /* Source-end detection — the byte AT the new walker
                 * position is the NUL terminator. */
                if (*buf == 0) {
                    continue_outer = 0;
                }
            } else if (line_pixel == 0) {
                /* word_width returned 0 — exhausted source or
                 * pathological input (a single char wider than the
                 * whole line budget).  Terminate. */
                continue_outer = 0;
            }
        }

        /* Per-line arm + render. */
        arm_line();
        gfx_text_render_line(g_gfx_wrap_small_buf, cursor_x, local_y,
                             font_base, char_glyph_table, color);

        track_max_line_x();
        local_y = advance_y(local_y, font_base);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040328e — multi-line localized word-wrap with
 *                        two-stage pagination + leading-space skip
 * ────────────────────────────────────────────────────────────────── */

void gfx_wrap_localized(localization_state_t *loc,
                        int loc_index,
                        int skip_strings,
                        int cursor_x,
                        int cursor_y,
                        int first_page_width,
                        int first_page_lines,
                        int next_page_x,
                        int next_page_width,
                        const uint8_t *font_base,
                        const uint8_t *char_glyph_table,
                        const unsigned char *char_glyph_table_metrics,
                        uint8_t color)
{
    /* Top-of-call: reset BOTH the wrap module's max_line_x AND the
     * dispatcher's alt-clip flag.  Retail emits the writes in this
     * order. */
    g_gfx_max_line_x = 0;
    gfx_dispatch_set_alt_clip_enabled(0);

    const uint8_t *walker = seek_localized(loc, loc_index, skip_strings);
    if (walker == 0) {
        return;
    }

    font_metrics_state_t mstate;
    memset(&mstate, 0, sizeof mstate);
    mstate.char_glyph_index = char_glyph_table_metrics;

    int continue_outer = 1;
    int lines_emitted = 0;
    int local_y = cursor_y;
    int safety = 0;

    int active_x     = cursor_x;
    int active_width = first_page_width;

    while (continue_outer && (++safety < 100)) {
        /* Zero the 2000-byte intermediate buffer. */
        memset(g_gfx_wrap_large_buf, 0, GFX_WRAP_LARGE_LINE_BUF_LEN);

        int line_pixel  = 0;
        int line_cursor = 0;
        /* Leading-space skip — armed at the start of every line,
         * cleared after the first non-space byte is copied.
         * Suppresses spaces at the start of wrapped lines so a soft
         * wrap doesn't visibly indent the continuation. */
        int leading_skip = 1;

        while (continue_outer && line_pixel < active_width) {
            int word_w = word_width(&mstate, walker, font_base);
            line_pixel += word_w;

            if (line_pixel < active_width) {
                for (int i = 0; i < mstate.chars_consumed; i++) {
                    uint8_t b = *walker++;
                    if (!leading_skip || b != 0x20) {
                        if (line_cursor < GFX_WRAP_LARGE_LINE_BUF_LEN) {
                            g_gfx_wrap_large_buf[line_cursor] = b;
                        }
                        line_cursor++;
                        leading_skip = 0;
                    }
                }
                if (*walker == 0) {
                    continue_outer = 0;
                }
            } else if (line_pixel == 0) {
                continue_outer = 0;
            }
        }

        arm_line();
        gfx_text_render_line(g_gfx_wrap_large_buf, active_x, local_y,
                             font_base, char_glyph_table, color);

        track_max_line_x();
        lines_emitted++;
        local_y = advance_y(local_y, font_base);

        /* Two-stage pagination — once `lines_emitted` reaches
         * `first_page_lines`, swap to the continuation's cursor_x
         * and pixel budget.  Retail uses `<=` so the swap fires
         * AFTER the param_6'th line (i.e. lines [1..first_page_lines]
         * use the first-page width, lines [first_page_lines+1..]
         * use the continuation). */
        if (first_page_lines <= lines_emitted) {
            active_x     = next_page_x;
            active_width = next_page_width;
        }
    }

    /* Mirror retail's walker-state write — keep loc->walker_offset
     * updated to the post-wrap position so callers can inspect
     * residual bytes if needed (matches the retail global). */
    loc->walker_offset = (size_t)(walker - loc->blob);
}
