/*
 * OpenLords2 — integer formatter + label-render wrappers (impl).
 *
 * See gfx_text_label.h for the function map, state model, and quirk
 * narrative.  Each function matches its retail body statement-for-
 * statement; control flow and order-of-writes match the Ghidra decomp
 * (with r2 disassembly cross-checks where Ghidra's loop shape was
 * ambiguous — see quirk #45).
 */

#include "gfx_text_label.h"

#include "gfx_dispatch.h"
#include "gfx_text.h"

/* ──────────────────────────────────────────────────────────────────
 * Shared 8000-byte format buffer (retail DAT_0058fda0).  Zero-
 * initialized at startup — matches retail's BSS layout.  Used by the
 * two formatters here and (eventually) by the save-name copy paths
 * (0x4afab9 / 0x4afeed) when those land.
 * ────────────────────────────────────────────────────────────────── */

enum { GFX_TEXT_LABEL_BUFFER_LEN = 8000 };
static uint8_t g_format_buffer[GFX_TEXT_LABEL_BUFFER_LEN];

uint8_t *gfx_text_label_buffer(void)        { return g_format_buffer; }
size_t   gfx_text_label_buffer_size(void)   { return GFX_TEXT_LABEL_BUFFER_LEN; }

/* ──────────────────────────────────────────────────────────────────
 * Shared prelude — zero first 100 bytes + emit optional sign byte
 * (used by both format_dec and format_hex).  Returns the digit-start
 * offset (past the sign byte if one was emitted).
 * ────────────────────────────────────────────────────────────────── */
static int format_zero_and_sign(int *value_ref, int offset, int with_plus)
{
    /* Retail zeros only the FIRST 100 bytes of the 8000-byte buffer.
     * Bytes 100+ retain prior caller state — quirk #43. */
    for (int i = 0; i < 100; i++) {
        g_format_buffer[i] = 0;
    }

    int v = *value_ref;
    if (v < 0) {
        v = -v;
        g_format_buffer[offset] = '-';
        offset++;
    } else if (with_plus != 0) {
        g_format_buffer[offset] = '+';
        offset++;
    }
    *value_ref = v;
    return offset;
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_004022bd — integer → decimal digits
 *
 * Magnitude → digit-count table:
 *   < 10        → 1 digit
 *   < 100       → 2 digits
 *   ...
 *   < 10^9      → 9 digits
 *   >= 10^9     → 0 digits (silent — see quirk #42)
 *
 * Digits written big-endian: highest at offset, lowest at offset+count-1.
 * ────────────────────────────────────────────────────────────────── */
void gfx_text_label_format_dec(int value, int offset, int with_plus)
{
    int v = value;
    offset = format_zero_and_sign(&v, offset, with_plus);

    int digit_count;
    if      (v <         10) digit_count = 1;
    else if (v <        100) digit_count = 2;
    else if (v <       1000) digit_count = 3;
    else if (v <      10000) digit_count = 4;
    else if (v <     100000) digit_count = 5;
    else if (v <    1000000) digit_count = 6;
    else if (v <   10000000) digit_count = 7;
    else if (v <  100000000) digit_count = 8;
    else if (v < 1000000000) digit_count = 9;
    else                     digit_count = 0;

    /* Retail emits the digits via repeated `% 10` / `/= 10`, writing
     * at buffer[offset + digit_count - 1] and decrementing.  The
     * `0x58fd9f + offset + local_8` indexing in Ghidra is the same
     * thing relative to buffer base 0x58fda0 — i.e. buffer[offset +
     * local_8 - 1]. */
    while (digit_count - 1 >= 0) {
        g_format_buffer[offset + digit_count - 1] = (uint8_t)((v % 10) + '0');
        v /= 10;
        digit_count--;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00402447 — integer → uppercase hex digits
 *
 * Magnitude → digit-count table:
 *   < 0x10       → 1 digit
 *   < 0x100      → 2 digits
 *   ...
 *   < 0x10000000 → 7 digits
 *   >= 0x10000000→ 0 digits (silent — see quirk #42)
 *
 * Retail's MSVC 4.20 emitted the per-nibble extraction as a sign-aware
 * abs trick (`(byte ^ sign) - sign & 0xf ^ sign) - sign`) that's
 * identical to `(byte) & 0xf` for non-negative ints — and the value
 * is non-negative inside the loop because the sign was stripped in the
 * prelude.  We use the plain `& 0xf` for readability; the rendered
 * bytes are identical.
 * ────────────────────────────────────────────────────────────────── */
void gfx_text_label_format_hex(int value, int offset, int with_plus)
{
    int v = value;
    offset = format_zero_and_sign(&v, offset, with_plus);

    int digit_count;
    if      (v < 0x10)        digit_count = 1;
    else if (v < 0x100)       digit_count = 2;
    else if (v < 0x1000)      digit_count = 3;
    else if (v < 0x10000)     digit_count = 4;
    else if (v < 0x100000)    digit_count = 5;
    else if (v < 0x1000000)   digit_count = 6;
    else if (v < 0x10000000)  digit_count = 7;
    else                      digit_count = 0;

    while (digit_count - 1 >= 0) {
        uint8_t nibble = (uint8_t)(v & 0xf);
        /* '0'..'9' for 0..9; 'A'..'F' for 10..15 (0x37 + 10 = 0x41). */
        g_format_buffer[offset + digit_count - 1] =
            (nibble < 10) ? (uint8_t)(nibble + '0') : (uint8_t)(nibble + 0x37);
        v >>= 4;
        digit_count--;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Shared prefix-overwrite helper.  All four render wrappers call
 * the formatter with offset=1, then optionally overwrite buffer[0]
 * with the per-call prefix char (quirk #41).
 * ────────────────────────────────────────────────────────────────── */
static void apply_prefix(char prefix)
{
    if (prefix != 0) {
        g_format_buffer[0] = (uint8_t)prefix;
    }
}

/* Suffix-append for the "for-loop" strlen pattern used by
 * FUN_00402f64 and FUN_00403015 — local_8 = (first-zero-index);
 * suffix overwrites the zero, then a final terminator goes one past
 * the last suffix byte.
 *
 * (The "do-while" variant used by FUN_004030c6 / FUN_00403190 has
 * a one-byte gap — see append_suffix_off_by_one + quirk #45.) */
static void append_suffix(const char *suffix)
{
    int dst = 0;
    while (g_format_buffer[dst] != 0) {
        dst++;
    }
    while (*suffix != 0) {
        g_format_buffer[dst] = (uint8_t)*suffix;
        dst++;
        suffix++;
    }
    g_format_buffer[dst] = 0;
}

/* Suffix-append for the do-while strlen pattern.  Walks one byte past
 * the first zero (leaving an embedded NUL between digits and suffix)
 * and then appends.  The renderer stops at the embedded NUL, so the
 * suffix is effectively invisible — faithful to retail quirk #45. */
static void append_suffix_off_by_one(const char *suffix)
{
    int dst = 0;
    /* do/while: read buf[dst]; if non-zero, advance to dst+1; if zero,
     * still advance to dst+1 then exit.  Equivalent to "scan to first
     * zero, then advance one more". */
    do {
        uint8_t b = g_format_buffer[dst];
        dst++;
        if (b == 0) break;
    } while (1);

    while (*suffix != 0) {
        g_format_buffer[dst] = (uint8_t)*suffix;
        dst++;
        suffix++;
    }
    g_format_buffer[dst] = 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00402f64 — render "<prefix?><dec(value)><suffix>"
 * ────────────────────────────────────────────────────────────────── */
void gfx_text_label_render_dec(int value,
                               char prefix,
                               const char *suffix,
                               int cursor_x,
                               int cursor_y,
                               const uint8_t *font_base,
                               const uint8_t *char_glyph_table,
                               uint8_t color)
{
    gfx_text_label_format_dec(value, 1, 0);
    apply_prefix(prefix);
    append_suffix(suffix);
    gfx_text_render_line(g_format_buffer, cursor_x, cursor_y,
                         font_base, char_glyph_table, color);
    /* Retail's `DAT_005aeb84 = 0;` — the renderer already clears
     * alt_clip_enabled in its epilogue, but retail emits the write
     * explicitly here too (harmless redundancy preserved). */
    gfx_dispatch_set_alt_clip_enabled(0);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403015 — render "<prefix?><hex(value)><suffix>"
 * ────────────────────────────────────────────────────────────────── */
void gfx_text_label_render_hex(int value,
                               char prefix,
                               const char *suffix,
                               int cursor_x,
                               int cursor_y,
                               const uint8_t *font_base,
                               const uint8_t *char_glyph_table,
                               uint8_t color)
{
    gfx_text_label_format_hex(value, 1, 0);
    apply_prefix(prefix);
    append_suffix(suffix);
    gfx_text_render_line(g_format_buffer, cursor_x, cursor_y,
                         font_base, char_glyph_table, color);
    gfx_dispatch_set_alt_clip_enabled(0);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_004030c6 — centered "<prefix?><dec(value)><suffix>"
 *
 * Uses the off-by-one strlen pattern (quirk #45), so the suffix is
 * effectively dropped at render time.  Retail callers pass single-
 * character suffixes (typically " "), so the visible loss is just
 * a trailing space.
 * ────────────────────────────────────────────────────────────────── */
void gfx_text_label_render_dec_centered(int value,
                                        char prefix,
                                        const char *suffix,
                                        int cursor_x,
                                        int cursor_y,
                                        int width,
                                        const uint8_t *font_base,
                                        const uint8_t *char_glyph_table,
                                        const unsigned char *char_glyph_table_metrics,
                                        uint8_t color)
{
    gfx_text_label_format_dec(value, 1, 0);
    apply_prefix(prefix);
    append_suffix_off_by_one(suffix);
    gfx_text_render_line_centered(g_format_buffer, cursor_x, cursor_y, width,
                                  font_base, char_glyph_table,
                                  char_glyph_table_metrics, color);
    gfx_dispatch_set_alt_clip_enabled(0);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00403190 — centered + signed-color
 *
 *   - When both `value` and `zero_companion` are zero, the entire
 *     render is skipped (no formatter call, no render).  Quirk #44.
 *   - The formatter is called with `with_plus = 1`, so positive
 *     values render with a leading '+' sign (and negatives with '-').
 *   - Foreground color is `color_positive` for value >= 0, else
 *     `color_negative`.
 *   - Suffix-append uses the same off-by-one pattern as
 *     render_dec_centered (quirk #45).
 * ────────────────────────────────────────────────────────────────── */
void gfx_text_label_render_dec_signed(int value,
                                      int zero_companion,
                                      char prefix,
                                      const char *suffix,
                                      int cursor_x,
                                      int cursor_y,
                                      int width,
                                      const uint8_t *font_base,
                                      const uint8_t *char_glyph_table,
                                      const unsigned char *char_glyph_table_metrics,
                                      uint8_t color_positive,
                                      uint8_t color_negative)
{
    if (value == 0 && zero_companion == 0) {
        return;
    }

    uint8_t color = (value < 0) ? color_negative : color_positive;

    gfx_text_label_format_dec(value, 1, 1);
    apply_prefix(prefix);
    append_suffix_off_by_one(suffix);
    gfx_text_render_line_centered(g_format_buffer, cursor_x, cursor_y, width,
                                  font_base, char_glyph_table,
                                  char_glyph_table_metrics, color);
    gfx_dispatch_set_alt_clip_enabled(0);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_00402e0c — compound "label + signed value"
 *
 * Two-half render: a localization-string label, then a decimal value
 * positioned past the label.  Sign-conditional label color + prefix-
 * char selection on the value render.  Wraps everything in a
 * line_x_acc reset/restore dance — the renderer mutates line_x_acc to
 * track per-glyph horizontal advance, and this compound wrapper reuses
 * that to position the value just past the label.
 *
 * Retail's gate-shape: the line_x_acc reset happens BEFORE the
 * early-return check, but the restore happens INSIDE the active
 * branch.  All-zero inputs therefore silently zero the caller's
 * accumulator — see quirk #46.  Faithful here.
 *
 * Mode/value decision tree (mirrors retail's if-else chain — the
 * `value < 1` retail branch is `value == 0` here since `value < 0`
 * was already handled):
 *
 *   mode == 2          → '@' prefix, color_positive  (value as-is)
 *   value <  0         → '-' prefix, color_negative  (value negated)
 *   value == 0         → '@' prefix, color_positive
 *   value >  0         → '+' prefix, color_positive
 *
 * The label render's color tracks the value's sign directly, NOT the
 * mode — so a (value < 0, mode == 2) call produces a negative-colored
 * label paired with a positive-colored value (quirk #47).
 * ────────────────────────────────────────────────────────────────── */
void gfx_text_label_render_labeled_signed(int value,
                                          int mode,
                                          const uint8_t *label,
                                          const char *suffix,
                                          int cursor_x,
                                          int cursor_y,
                                          const uint8_t *font_base,
                                          const uint8_t *char_glyph_table,
                                          uint8_t color_positive,
                                          uint8_t color_negative)
{
    int saved_x_acc = gfx_text_line_x_acc();
    gfx_text_set_line_x_acc(0);

    if (value != 0 || mode != 0) {
        /* Label color: by value sign — even under mode==2 (quirk #47). */
        uint8_t label_color = (value < 0) ? color_negative : color_positive;
        gfx_text_render_line(label, cursor_x, cursor_y,
                             font_base, char_glyph_table, label_color);

        /* Position value past the label.  line_x_acc was mutated by the
         * label render to the per-glyph total advance. */
        int value_x = cursor_x + gfx_text_line_x_acc();

        if (mode == 2) {
            gfx_text_label_render_dec(value, '@', suffix,
                                      value_x, cursor_y,
                                      font_base, char_glyph_table,
                                      color_positive);
        } else if (value < 0) {
            /* Negate so the formatter writes only digits; the '-' goes
             * into buffer[0] via the prefix overwrite (quirk #41).  This
             * keeps the '-' contiguous with the digits. */
            gfx_text_label_render_dec(-value, '-', suffix,
                                      value_x, cursor_y,
                                      font_base, char_glyph_table,
                                      color_negative);
        } else if (value == 0) {
            gfx_text_label_render_dec(value, '@', suffix,
                                      value_x, cursor_y,
                                      font_base, char_glyph_table,
                                      color_positive);
        } else {
            gfx_text_label_render_dec(value, '+', suffix,
                                      value_x, cursor_y,
                                      font_base, char_glyph_table,
                                      color_positive);
        }

        /* Restore (add to current).  After the value render, line_x_acc
         * = label_width + value_width; we leave it as that PLUS the
         * caller's saved value, so the caller observes their accumulator
         * incremented by the combined width of the compound render. */
        gfx_text_set_line_x_acc(gfx_text_line_x_acc() + saved_x_acc);
    }
    /* else: line_x_acc stays at 0 — caller's saved_x_acc is silently
     * dropped (quirk #46). */
}
