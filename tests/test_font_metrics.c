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

/* ──────────────────────────────────────────────────────────────────
 * buffer_width_bounded — general renderer measurer
 * ────────────────────────────────────────────────────────────────── */

int test_font_metrics_buffer_width_bounded_sums_chars(void) {
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width_bounded(&s, (const unsigned char *)"ABC", font);
    /* "ABC" = 7 + 11 + 257 = 275. */
    T_ASSERT_EQ_I(w, 275);
    /* No pixel_overflow side effect — flag stays at default. */
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    return 0;
}

int test_font_metrics_buffer_width_bounded_caps_at_10000(void) {
    /* Build a non-null-terminated buffer of 12000 'A's; the bounded
     * helper should stop at 10000 chars rather than reading off the
     * end.  Each 'A' is 7 pixels → total = 7 * 10000 = 70000. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;

    /* Allocate +1 slot past the cap to confirm we don't read past
     * char index 9999 — set buf[10000] to a sentinel.  ASan would
     * catch an out-of-bounds read regardless. */
    static unsigned char big_buf[12001];
    memset(big_buf, 'A', 12000);
    big_buf[12000] = 0;
    int w = font_metrics_buffer_width_bounded(&s, big_buf, font);
    T_ASSERT_EQ_I(w, 7 * 10000);
    return 0;
}

int test_font_metrics_buffer_width_bounded_stops_at_terminator(void) {
    /* Terminator should win over the cap when it comes first. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_buffer_width_bounded(&s, (const unsigned char *)"AB", font);
    T_ASSERT_EQ_I(w, 18);
    return 0;
}

int test_font_metrics_buffer_width_bounded_missing_glyph_clears_cache(void) {
    /* Verify the unconditional-write-then-conditional-decrement
     * shape from FUN_004014f0 — a missing glyph clears
     * last_glyph_index (because the raw 1-based value is 0). */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    s.last_glyph_index = 42;
    /* "Z" alone: missing.  Width = 0; cache cleared. */
    int w = font_metrics_buffer_width_bounded(&s, (const unsigned char *)"Z", font);
    T_ASSERT_EQ_I(w, 0);
    T_ASSERT_EQ_I(s.last_glyph_index, 0);
    return 0;
}

int test_font_metrics_buffer_width_bounded_no_pixel_overflow_side_effect(void) {
    /* This helper must NOT touch pixel_overflow even on huge totals.
     * Drive a big total and pre-set pixel_overflow to verify. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    s.pixel_overflow = 1;
    font_metrics_buffer_width_bounded(&s, (const unsigned char *)"CCC", font);
    T_ASSERT_EQ_I(s.pixel_overflow, 1);   /* unchanged */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * word_width — next-word measurer (FUN_004036f9)
 * ────────────────────────────────────────────────────────────────── */

int test_font_metrics_word_width_basic_printables(void) {
    /* "AB" — sums to char A (7, glyph 0) + char B (11, glyph 1) = 18. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_word_width(&s, (const unsigned char *)"AB", font);
    T_ASSERT_EQ_I(w, 7 + 11);
    T_ASSERT_EQ_I(s.chars_consumed, 2);
    return 0;
}

int test_font_metrics_word_width_terminates_at_nul(void) {
    /* "A" → reads 'A' (+7), then NUL breaks out without
     * consuming the NUL. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_word_width(&s, (const unsigned char *)"A", font);
    T_ASSERT_EQ_I(w, 7);
    T_ASSERT_EQ_I(s.chars_consumed, 1);
    return 0;
}

int test_font_metrics_word_width_terminates_at_internal_space(void) {
    /* "AB CD" — after A, B come internal space → return.
     * chars_consumed is 2 (A+B were consumed; the terminating
     * space was NOT). */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_word_width(&s, (const unsigned char *)"AB CD", font);
    T_ASSERT_EQ_I(w, 7 + 11);
    T_ASSERT_EQ_I(s.chars_consumed, 2);
    return 0;
}

int test_font_metrics_word_width_leading_spaces_count_as_four(void) {
    /* "  A" — two leading spaces (+4 each), then A (+7). */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_word_width(&s, (const unsigned char *)"  A", font);
    T_ASSERT_EQ_I(w, 4 + 4 + 7);
    T_ASSERT_EQ_I(s.chars_consumed, 3);
    return 0;
}

int test_font_metrics_word_width_terminates_at_internal_dollar(void) {
    /* Quirk #24: '$' terminates a word after the first printable.
     * "A$B" — A printable, then $ terminates.  chars_consumed = 1. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_word_width(&s, (const unsigned char *)"A$B", font);
    T_ASSERT_EQ_I(w, 7);
    T_ASSERT_EQ_I(s.chars_consumed, 1);
    return 0;
}

int test_font_metrics_word_width_leading_dollar_is_skipped(void) {
    /* Quirk #24: leading '$' is silently consumed (no width, no
     * boundary).  "$$A" — consumes the two leading '$'s and the
     * 'A'.  Width = 7 (A's width); chars_consumed = 3. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_word_width(&s, (const unsigned char *)"$$A", font);
    T_ASSERT_EQ_I(w, 7);
    T_ASSERT_EQ_I(s.chars_consumed, 3);
    return 0;
}

int test_font_metrics_word_width_other_control_bytes_consumed_zero_width(void) {
    /* A control byte 0x01..0x1f matches none of the explicit
     * branches and falls through to chars_consumed++.  No width
     * contribution, no flag change.  Sequence "\x01\x02A":
     *   bytes 0x01, 0x02 each consume one but add zero width;
     *   'A' adds 7.  chars_consumed = 3. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    const unsigned char input[] = { 0x01, 0x02, 'A', 0 };
    int w = font_metrics_word_width(&s, input, font);
    T_ASSERT_EQ_I(w, 7);
    T_ASSERT_EQ_I(s.chars_consumed, 3);
    return 0;
}

int test_font_metrics_word_width_empty_input_returns_zero(void) {
    /* Pure NUL — no consumption, zero width. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    int w = font_metrics_word_width(&s, (const unsigned char *)"", font);
    T_ASSERT_EQ_I(w, 0);
    T_ASSERT_EQ_I(s.chars_consumed, 0);
    return 0;
}

int test_font_metrics_word_width_iteration_cap_at_1999(void) {
    /* Quirk #25: hard cap of 1999 iterations.  Build a 2100-byte
     * input of all 'A' bytes (no NUL within the cap).  Should
     * return after consuming exactly 1999 bytes. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;

    unsigned char *big = malloc(2100);
    memset(big, 'A', 2100);
    int w = font_metrics_word_width(&s, big, font);
    int consumed = s.chars_consumed;
    free(big);
    /* 1999 A's at width 7 = 13993.  Cap returns BEFORE adding the
     * 2000th char (when iterations=2000, 1999<iterations → return).
     * So chars_consumed reflects the bumps from iter 1..1999. */
    T_ASSERT_EQ_I(consumed, 1999);
    T_ASSERT_EQ_I(w, 1999 * 7);
    return 0;
}

int test_font_metrics_word_width_uses_char_width_cache(void) {
    /* Word measurement uses font_metrics_char_width internally for
     * printables — so the last_glyph_index cache should reflect the
     * last printable seen.  "AB" — last is 'B' (glyph 1). */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    font_metrics_word_width(&s, (const unsigned char *)"AB", font);
    T_ASSERT_EQ_I(s.last_glyph_index, 1);   /* B is 0-based glyph 1 */
    return 0;
}

int test_font_metrics_word_width_chars_consumed_reset_per_call(void) {
    /* chars_consumed must start at 0 each call — not accumulate
     * across calls. */
    font_metrics_state_t s = {0};
    unsigned char ctbl[256]; make_char_table(ctbl);
    unsigned char font[64];  make_font_blob(font);
    s.char_glyph_index = ctbl;
    s.chars_consumed = 99;   /* pre-poison */
    font_metrics_word_width(&s, (const unsigned char *)"A", font);
    T_ASSERT_EQ_I(s.chars_consumed, 1);
    return 0;
}
