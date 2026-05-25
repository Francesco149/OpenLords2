/*
 * Unit tests for font_metrics — the per-char and per-buffer pixel-
 * width measurer.
 *
 * Uses hermetic char-table + font-blob fixtures: a 256-byte
 * char→glyph-index lookup and a small font blob with known widths.
 */

#include "t.h"
#include "font_metrics.h"
#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Fixtures
 * ────────────────────────────────────────────────────────────────── */

/* Char-table: 'A'=glyph 1, 'B'=glyph 2, 'C'=glyph 3, others=0
 * (missing).  Per retail convention, entries are 1-based glyph
 * indices, 0 = missing. */
static void make_char_table(unsigned char tbl[256])
{
    memset(tbl, 0, 256);
    tbl[(unsigned char)'A'] = 1;
    tbl[(unsigned char)'B'] = 2;
    tbl[(unsigned char)'C'] = 3;
}

/* Font blob: 4 glyphs, each 16 B.  We only need the +8 (lo) and
 * +9 (hi) bytes of each entry to set the width.
 *
 *   glyph 0 (idx 0): width = 0*256 +  6 + 1 = 7
 *   glyph 1 (idx 1): width = 0*256 + 10 + 1 = 11
 *   glyph 2 (idx 2): width = 1*256 +  0 + 1 = 257  (large)
 *   glyph 3 (idx 3): width = 0*256 +  3 + 1 = 4
 */
static void make_font_blob(unsigned char font[64])
{
    memset(font, 0xee, 64);   /* poison so off-by-one reads stand out */
    /* glyph 0 */ font[0*16 + 8] =  6; font[0*16 + 9] = 0;
    /* glyph 1 */ font[1*16 + 8] = 10; font[1*16 + 9] = 0;
    /* glyph 2 */ font[2*16 + 8] =  0; font[2*16 + 9] = 1;
    /* glyph 3 */ font[3*16 + 8] =  3; font[3*16 + 9] = 0;
}

/* ──────────────────────────────────────────────────────────────────
 * char_width — special cases
 * ────────────────────────────────────────────────────────────────── */

int test_font_metrics_char_width_null_char_returns_zero(void) {
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    /* Pre-poison cache to verify it's NOT updated for c=0. */
    s.last_glyph_index = 42;
    s.last_glyph_offset = 99;
    int w = font_metrics_char_width(&s, 0, font);
    T_ASSERT_EQ_I(w, 0);
    T_ASSERT_EQ_I(s.last_glyph_index, 42);
    T_ASSERT_EQ_I(s.last_glyph_offset, 99);
    return 0;
}

int test_font_metrics_char_width_space_returns_four(void) {
    /* Space is hardcoded to 4 — not table-driven. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    s.last_glyph_index = 42;
    int w = font_metrics_char_width(&s, ' ', font);
    T_ASSERT_EQ_I(w, 4);
    T_ASSERT_EQ_I(s.last_glyph_index, 42);  /* cache not updated */
    return 0;
}

int test_font_metrics_char_width_missing_returns_zero_clears_idx(void) {
    /* Missing glyph: returns 0, last_glyph_index := 0,
     * last_glyph_offset UNTOUCHED (per retail). */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    s.last_glyph_index = 42;
    s.last_glyph_offset = 99;
    int w = font_metrics_char_width(&s, 'Z', font);   /* not in table */
    T_ASSERT_EQ_I(w, 0);
    T_ASSERT_EQ_I(s.last_glyph_index, 0);
    T_ASSERT_EQ_I(s.last_glyph_offset, 99);  /* preserved */
    return 0;
}

int test_font_metrics_char_width_null_table_treats_as_missing(void) {
    font_metrics_state_t s = {0};
    unsigned char font[64]; make_font_blob(font);
    /* char_glyph_index is NULL — every char (except null/space)
     * should report 0 width with cache cleared. */
    s.last_glyph_index = 7;
    int w = font_metrics_char_width(&s, 'A', font);
    T_ASSERT_EQ_I(w, 0);
    T_ASSERT_EQ_I(s.last_glyph_index, 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * char_width — present glyphs
 * ────────────────────────────────────────────────────────────────── */

int test_font_metrics_char_width_glyph_one_byte_low(void) {
    /* 'A' → glyph 0 → width = 0*256 + 6 + 1 = 7. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_char_width(&s, 'A', font);
    T_ASSERT_EQ_I(w, 7);
    T_ASSERT_EQ_I(s.last_glyph_index, 0);
    T_ASSERT_EQ_I(s.last_glyph_offset, 8);
    return 0;
}

int test_font_metrics_char_width_glyph_combined_hi_lo(void) {
    /* 'C' → glyph 2 → width = 1*256 + 0 + 1 = 257. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_char_width(&s, 'C', font);
    T_ASSERT_EQ_I(w, 257);
    T_ASSERT_EQ_I(s.last_glyph_index, 2);
    T_ASSERT_EQ_I(s.last_glyph_offset, 2*16 + 8);
    return 0;
}

int test_font_metrics_char_width_cache_reflects_last_call(void) {
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    font_metrics_char_width(&s, 'A', font);
    T_ASSERT_EQ_I(s.last_glyph_index, 0);
    font_metrics_char_width(&s, 'B', font);
    T_ASSERT_EQ_I(s.last_glyph_index, 1);
    T_ASSERT_EQ_I(s.last_glyph_offset, 1*16 + 8);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * buffer_width
 * ────────────────────────────────────────────────────────────────── */

int test_font_metrics_buffer_width_sums_chars(void) {
    /* "ABC" widths: 7 + 11 + 257 = 275. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width(&s, (const unsigned char *)"ABC",
                                      font, /*max=*/9999);
    T_ASSERT_EQ_I(w, 275);
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    /* Cache holds the LAST char's glyph ('C' = glyph 2). */
    T_ASSERT_EQ_I(s.last_glyph_index, 2);
    return 0;
}

int test_font_metrics_buffer_width_empty_string_zero(void) {
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width(&s, (const unsigned char *)"",
                                      font, /*max=*/10);
    T_ASSERT_EQ_I(w, 0);
    /* max_pixel_width=10 <= total=0 is FALSE, so no overflow. */
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    return 0;
}

int test_font_metrics_buffer_width_spaces_only(void) {
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width(&s, (const unsigned char *)"    ",
                                      font, /*max=*/100);
    /* 4 spaces * 4px = 16. */
    T_ASSERT_EQ_I(w, 16);
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    return 0;
}

int test_font_metrics_buffer_width_missing_chars_count_zero(void) {
    /* "AZB" — 'Z' missing → 'A'(7) + 'Z'(0) + 'B'(11) = 18. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width(&s, (const unsigned char *)"AZB",
                                      font, /*max=*/100);
    T_ASSERT_EQ_I(w, 18);
    /* After 'Z', cache holds last_glyph_index=0 (cleared); then 'B'
     * (glyph 1) overwrites. */
    T_ASSERT_EQ_I(s.last_glyph_index, 1);
    return 0;
}

int test_font_metrics_buffer_width_overflow_at_threshold(void) {
    /* Retail uses `<=` — max_pixel_width <= total triggers overflow.
     * "AB" widths: 7 + 11 = 18.  max=18 should set pixel_overflow. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width(&s, (const unsigned char *)"AB",
                                      font, /*max=*/18);
    T_ASSERT_EQ_I(w, 18);
    T_ASSERT_EQ_I(s.pixel_overflow, 1);
    return 0;
}

int test_font_metrics_buffer_width_overflow_just_below_threshold(void) {
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width(&s, (const unsigned char *)"AB",
                                      font, /*max=*/19);
    T_ASSERT_EQ_I(w, 18);
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    return 0;
}

int test_font_metrics_buffer_width_overflow_clears_on_subsequent_call(void) {
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    /* Trigger overflow. */
    font_metrics_buffer_width(&s, (const unsigned char *)"AB", font, /*max=*/5);
    T_ASSERT_EQ_I(s.pixel_overflow, 1);
    /* Run again with generous max — overflow flag drops back to 0. */
    font_metrics_buffer_width(&s, (const unsigned char *)"A", font, /*max=*/100);
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    return 0;
}
