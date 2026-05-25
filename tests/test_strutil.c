/*
 * test_strutil.c — unit tests for src/strutil.c.
 *
 * Every test below pins a specific behavioural claim made in
 * strutil.h about the engine quartet's quirks.  If retail changes
 * (say, we discover find_substr should have been 1-based), the
 * failure here is the early signal.
 */

#include "t.h"
#include "strutil.h"

#include <string.h>

/* ──────────────────────────── find_substr ────────────────────────── */

int test_strutil_find_substr_basic(void)
{
    const char hay[] = "[Config]\nWindowsDir=.\n";
    int off = strutil_find_substr(hay, "WindowsDir", sizeof hay);
    /* "[Config]\n" is 9 bytes, so "WindowsDir" starts at offset 9. */
    T_ASSERT_EQ_I(off, 9);
    return 0;
}

/* Quirk: "not found" returns 0 — the SAME sentinel as "found at
 * offset 0".  All retail callers know real .ini key matches sit
 * past offset 0 (the file starts with `[Section]` headers or
 * comments). */
int test_strutil_find_substr_not_found_returns_zero(void)
{
    const char hay[] = "[Config]\nWindowsDir=.\n";
    int off = strutil_find_substr(hay, "SomeKeyNotPresent", sizeof hay);
    T_ASSERT_EQ_I(off, 0);
    return 0;
}

/* Quirk: same return for "found at offset 0".  Pin the
 * not-distinguishable behaviour so anyone tempted to "fix" it sees
 * the test break and reads the comment in strutil.h. */
int test_strutil_find_substr_at_offset_zero_quirk(void)
{
    const char hay[] = "KEY=value";
    int off = strutil_find_substr(hay, "KEY", sizeof hay);
    T_ASSERT_EQ_I(off, 0);   /* indistinguishable from "not found" */
    return 0;
}

/* The `max` bound limits the search window.  A match past `max`
 * must not be returned. */
int test_strutil_find_substr_respects_bound(void)
{
    const char hay[] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAATARGET";
    /* "TARGET" begins at offset 30. */
    int off30 = strutil_find_substr(hay, "TARGET", 30);
    T_ASSERT_EQ_I(off30, 0);   /* not visible inside first 30 bytes */
    int off31 = strutil_find_substr(hay, "TARGET", 31);
    T_ASSERT_EQ_I(off31, 30);
    return 0;
}

/* The retail short-circuit "if first byte matches, then do bounded
 * memcmp" means a partial match like "WindXXX" at one offset does
 * not consume the 'W' for a real "Windows" match later. */
int test_strutil_find_substr_overlap_boundaries(void)
{
    const char hay[] = "WindyDayWindowsDir";
    int off = strutil_find_substr(hay, "WindowsDir", sizeof hay);
    T_ASSERT_EQ_I(off, 8);     /* skips the false start at 0 */
    return 0;
}

/* ──────────────────────────── find_char ──────────────────────────── */

int test_strutil_find_char_basic(void)
{
    const char s[] = "KEY=value";
    int rv = strutil_find_char(s, '=', sizeof s);
    /* '=' is at index 3; function returns 1-based offset = 4. */
    T_ASSERT_EQ_I(rv, 4);
    return 0;
}

/* Distinguishes "found at offset 0" (1) from "not found" (0).  The
 * 1-based return is the convention that lets the .ini parser do
 * `off + iVar2` without ambiguity. */
int test_strutil_find_char_at_offset_zero_one_based(void)
{
    const char s[] = "=value";
    int rv = strutil_find_char(s, '=', sizeof s);
    T_ASSERT_EQ_I(rv, 1);
    return 0;
}

int test_strutil_find_char_not_found_returns_zero(void)
{
    const char s[] = "no-equals-here";
    int rv = strutil_find_char(s, '=', sizeof s);
    T_ASSERT_EQ_I(rv, 0);
    return 0;
}

int test_strutil_find_char_respects_bound(void)
{
    const char s[] = "AAAAAAAAAA=";
    int rv5 = strutil_find_char(s, '=', 5);
    T_ASSERT_EQ_I(rv5, 0);     /* '=' at 10, search limited to 5 */
    int rv11 = strutil_find_char(s, '=', 11);
    T_ASSERT_EQ_I(rv11, 11);   /* '=' at 10, 1-based = 11 */
    return 0;
}

/* ──────────────────────────── bounded_copy ────────────────────────── */

/* Per strutil.h: NO null terminator written.  The caller is
 * responsible.  This test sets up a dst pre-filled with 0xff, copies
 * 5 bytes, and asserts the 6th byte is still 0xff. */
int test_strutil_bounded_copy_no_null_terminator(void)
{
    unsigned char src[] = "HELLO";
    unsigned char dst[8];
    memset(dst, 0xff, sizeof dst);
    strutil_bounded_copy(dst, src, 5);
    T_ASSERT_MEM_EQ(dst, "HELLO", 5);
    T_ASSERT_EQ_I(dst[5], 0xff);     /* unchanged — no auto-null */
    T_ASSERT_EQ_I(dst[6], 0xff);
    T_ASSERT_EQ_I(dst[7], 0xff);
    return 0;
}

int test_strutil_bounded_copy_zero_length(void)
{
    unsigned char src[] = "anything";
    unsigned char dst[8];
    memset(dst, 0xaa, sizeof dst);
    strutil_bounded_copy(dst, src, 0);
    /* No bytes written at all — destination unchanged. */
    for (int i = 0; i < 8; i++) T_ASSERT_EQ_I(dst[i], 0xaa);
    return 0;
}

/* ──────────────────────────── strip_ctrl ──────────────────────────── */

/* Stops at the first byte < 0x20 — covers CR/LF/TAB/NUL. */
int test_strutil_strip_ctrl_at_lf(void)
{
    unsigned char s[] = "value\n\rmore";
    strutil_strip_ctrl(s);
    T_ASSERT_EQ_S((char *)s, "value");
    return 0;
}

/* Stops at the first byte > 0xa6 — covers high-bit junk. */
int test_strutil_strip_ctrl_at_high_byte(void)
{
    unsigned char s[] = { 'v', 'a', 'l', 0xff, 'x', 0 };
    strutil_strip_ctrl(s);
    T_ASSERT_EQ_S((char *)s, "val");
    return 0;
}

/* Both range edges (0x20 SP, 0xa6 ¦) MUST be preserved.  A test
 * with chars at both boundaries should pass through, but 0x1f and
 * 0xa7 should trip. */
int test_strutil_strip_ctrl_range_edges(void)
{
    unsigned char s1[] = { 0x20, 0xa6, 'X', 0 };
    strutil_strip_ctrl(s1);
    /* 0x20 (space) and 0xa6 are both inside the range; 'X' (0x58)
     * is too; the trailing NUL terminates the loop normally. */
    T_ASSERT_EQ_I(s1[0], 0x20);
    T_ASSERT_EQ_I(s1[1], 0xa6);
    T_ASSERT_EQ_I(s1[2], 'X');
    T_ASSERT_EQ_I(s1[3], 0);

    unsigned char s2[] = { 'A', 0x1f, 'X', 0 };
    strutil_strip_ctrl(s2);
    T_ASSERT_EQ_S((char *)s2, "A");

    unsigned char s3[] = { 'A', 0xa7, 'X', 0 };
    strutil_strip_ctrl(s3);
    T_ASSERT_EQ_S((char *)s3, "A");
    return 0;
}

int test_strutil_strip_ctrl_empty_string(void)
{
    unsigned char s[] = "";
    strutil_strip_ctrl(s);
    T_ASSERT_EQ_S((char *)s, "");
    return 0;
}

/* ──────────────────────────── case conversion ────────────────────── */

int test_strutil_to_upper_char_lowercase_in_range(void)
{
    T_ASSERT_EQ_U(strutil_to_upper_char('a'), 'A');
    T_ASSERT_EQ_U(strutil_to_upper_char('z'), 'Z');
    T_ASSERT_EQ_U(strutil_to_upper_char('m'), 'M');
    return 0;
}

int test_strutil_to_upper_char_non_lowercase_returns_zero(void)
{
    /* QUIRK: out-of-range inputs return 0, NOT the input. */
    T_ASSERT_EQ_U(strutil_to_upper_char('A'), 0);   /* already uppercase */
    T_ASSERT_EQ_U(strutil_to_upper_char('0'), 0);
    T_ASSERT_EQ_U(strutil_to_upper_char(' '), 0);
    T_ASSERT_EQ_U(strutil_to_upper_char('`'), 0);   /* 0x60, just below 'a' */
    T_ASSERT_EQ_U(strutil_to_upper_char('{'), 0);   /* 0x7b, just above 'z' */
    return 0;
}

int test_strutil_to_lower_char_uppercase_in_range(void)
{
    T_ASSERT_EQ_U(strutil_to_lower_char('A'), 'a');
    T_ASSERT_EQ_U(strutil_to_lower_char('Z'), 'z');
    T_ASSERT_EQ_U(strutil_to_lower_char('M'), 'm');
    return 0;
}

int test_strutil_to_lower_char_non_uppercase_returns_zero(void)
{
    T_ASSERT_EQ_U(strutil_to_lower_char('a'), 0);
    T_ASSERT_EQ_U(strutil_to_lower_char('0'), 0);
    T_ASSERT_EQ_U(strutil_to_lower_char('@'), 0);   /* 0x40, just below 'A' */
    T_ASSERT_EQ_U(strutil_to_lower_char('['), 0);   /* 0x5b, just above 'Z' */
    return 0;
}

int test_strutil_to_upper_inplace_preserves_non_letters(void)
{
    /* Different from single-char: non-letters pass through unchanged
     * (the single-char variant would zero them). */
    unsigned char s[] = "Hello, World! 123";
    strutil_to_upper_inplace(s);
    T_ASSERT_EQ_S((char *)s, "HELLO, WORLD! 123");
    return 0;
}

int test_strutil_to_upper_inplace_all_lowercase(void)
{
    unsigned char s[] = "abcdef";
    strutil_to_upper_inplace(s);
    T_ASSERT_EQ_S((char *)s, "ABCDEF");
    return 0;
}

int test_strutil_to_upper_inplace_no_letters_unchanged(void)
{
    unsigned char s[] = "12345!@#";
    strutil_to_upper_inplace(s);
    T_ASSERT_EQ_S((char *)s, "12345!@#");
    return 0;
}

int test_strutil_to_lower_inplace_preserves_non_letters(void)
{
    unsigned char s[] = "Hello, World! 123";
    strutil_to_lower_inplace(s);
    T_ASSERT_EQ_S((char *)s, "hello, world! 123");
    return 0;
}

int test_strutil_to_lower_inplace_all_uppercase(void)
{
    unsigned char s[] = "ABCDEF";
    strutil_to_lower_inplace(s);
    T_ASSERT_EQ_S((char *)s, "abcdef");
    return 0;
}

/* ──────────────────────────── shift_left / shift_right ──────────── */

int test_strutil_shift_left_deletes_first_byte(void)
{
    unsigned char s[] = "ABCDE!";
    /* Delete s[0] ('A'), shifting BCDE down; end at &s[5] (the '!'). */
    strutil_shift_left(s, &s[5]);
    /* Expected: "BCDE" with '\0' at index 5 (was '!'). */
    T_ASSERT_EQ_I(s[0], 'B');
    T_ASSERT_EQ_I(s[1], 'C');
    T_ASSERT_EQ_I(s[2], 'D');
    T_ASSERT_EQ_I(s[3], 'E');
    T_ASSERT_EQ_I(s[4], '!');
    T_ASSERT_EQ_I(s[5], 0);
    return 0;
}

int test_strutil_shift_left_at_interior_offset(void)
{
    /* Delete s[2], range [s[2], s[5]]. */
    unsigned char s[] = "ABCDEF";   /* "ABCDEF\0" */
    strutil_shift_left(&s[2], &s[5]);
    /* Expected: "ABDEF" with '\0' at index 5 (was 'F'). */
    T_ASSERT_EQ_I(s[0], 'A');
    T_ASSERT_EQ_I(s[1], 'B');
    T_ASSERT_EQ_I(s[2], 'D');
    T_ASSERT_EQ_I(s[3], 'E');
    T_ASSERT_EQ_I(s[4], 'F');
    T_ASSERT_EQ_I(s[5], 0);
    return 0;
}

int test_strutil_shift_right_inserts_space(void)
{
    /* Insert at s[0], range [s[0], s[2]].  Buffer must have room for
     * one more byte (end[1] gets the new terminator). */
    unsigned char s[] = "AB" "\0\0\0";  /* "AB" + 3 extra bytes */
    strutil_shift_right(s, &s[2]);
    /* Expected: s[0] = 'A' (unchanged), s[1] = 'A' (shifted from
     * s[0]), s[2] = 'B' (shifted from s[1]), s[3] = 0 (new term). */
    T_ASSERT_EQ_I(s[0], 'A');
    T_ASSERT_EQ_I(s[1], 'A');
    T_ASSERT_EQ_I(s[2], 'B');
    T_ASSERT_EQ_I(s[3], 0);
    return 0;
}

int test_strutil_shift_right_interior_range(void)
{
    /* Shift right within "ABCDEF\0\0" — range [s[2], s[5]]. */
    unsigned char s[] = "ABCDEF" "\0";  /* "ABCDEF" + 1 extra byte */
    strutil_shift_right(&s[2], &s[5]);
    /* s[0..1] unchanged; s[2] retains 'C' (orig); s[3] = 'C' (from
     * orig s[2]); s[4] = 'D' (from orig s[3]); s[5] = 'E' (from orig
     * s[4]); s[6] = 0 (new terminator). */
    T_ASSERT_EQ_I(s[0], 'A');
    T_ASSERT_EQ_I(s[1], 'B');
    T_ASSERT_EQ_I(s[2], 'C');
    T_ASSERT_EQ_I(s[3], 'C');
    T_ASSERT_EQ_I(s[4], 'D');
    T_ASSERT_EQ_I(s[5], 'E');
    T_ASSERT_EQ_I(s[6], 0);
    return 0;
}

/* ──────────────────────────── strip_leading / trim_trailing ────── */

int test_strutil_strip_leading_spaces_basic(void)
{
    char s[] = "   hello";
    strutil_strip_leading_spaces(s);
    T_ASSERT_EQ_S(s, "hello");
    return 0;
}

int test_strutil_strip_leading_spaces_no_leading(void)
{
    char s[] = "hello   ";
    strutil_strip_leading_spaces(s);
    T_ASSERT_EQ_S(s, "hello   ");   /* trailing untouched */
    return 0;
}

int test_strutil_strip_leading_spaces_all_spaces(void)
{
    char s[] = "    ";
    strutil_strip_leading_spaces(s);
    T_ASSERT_EQ_S(s, "");
    return 0;
}

int test_strutil_strip_leading_spaces_empty(void)
{
    char s[] = "";
    strutil_strip_leading_spaces(s);
    T_ASSERT_EQ_S(s, "");
    return 0;
}

int test_strutil_trim_trailing_spaces_basic(void)
{
    char s[] = "hello   ";
    strutil_trim_trailing_spaces(s);
    T_ASSERT_EQ_S(s, "hello");
    return 0;
}

int test_strutil_trim_trailing_spaces_no_trailing(void)
{
    char s[] = "hello";
    strutil_trim_trailing_spaces(s);
    T_ASSERT_EQ_S(s, "hello");
    return 0;
}

int test_strutil_trim_trailing_spaces_preserves_leading(void)
{
    char s[] = "  hello  ";
    strutil_trim_trailing_spaces(s);
    T_ASSERT_EQ_S(s, "  hello");
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * strutil_decimal_atoi
 * ────────────────────────────────────────────────────────────────── */

int test_strutil_decimal_atoi_basic(void)
{
    T_ASSERT_EQ_I(strutil_decimal_atoi("123"), 123);
    return 0;
}

int test_strutil_decimal_atoi_single_digit(void)
{
    T_ASSERT_EQ_I(strutil_decimal_atoi("7"), 7);
    return 0;
}

int test_strutil_decimal_atoi_empty_returns_zero(void)
{
    T_ASSERT_EQ_I(strutil_decimal_atoi(""), 0);
    return 0;
}

int test_strutil_decimal_atoi_no_leading_digit_returns_zero(void)
{
    T_ASSERT_EQ_I(strutil_decimal_atoi("abc"), 0);
    return 0;
}

int test_strutil_decimal_atoi_stops_at_non_digit(void)
{
    T_ASSERT_EQ_I(strutil_decimal_atoi("42xyz"), 42);
    return 0;
}

int test_strutil_decimal_atoi_eight_digit_max(void)
{
    T_ASSERT_EQ_I(strutil_decimal_atoi("12345678"), 12345678);
    return 0;
}

int test_strutil_decimal_atoi_nine_plus_digits_truncates_to_eight(void)
{
    /* Retail would read past the powers-of-10 table; our port caps
     * at 8 digits.  "123456789" → reads "12345678", ignores '9'. */
    T_ASSERT_EQ_I(strutil_decimal_atoi("123456789"), 12345678);
    return 0;
}

int test_strutil_decimal_atoi_zero_string(void)
{
    T_ASSERT_EQ_I(strutil_decimal_atoi("0"), 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * strutil_collapse_double_spaces
 * ────────────────────────────────────────────────────────────────── */

int test_strutil_collapse_double_spaces_basic(void)
{
    char s[] = "ab  cd";
    strutil_collapse_double_spaces(s);
    T_ASSERT_EQ_S(s, "ab cd");
    return 0;
}

int test_strutil_collapse_double_spaces_three_run(void)
{
    char s[] = "a   b";
    strutil_collapse_double_spaces(s);
    T_ASSERT_EQ_S(s, "a b");
    return 0;
}

int test_strutil_collapse_double_spaces_no_doubles_unchanged(void)
{
    char s[] = "a b c d";
    strutil_collapse_double_spaces(s);
    T_ASSERT_EQ_S(s, "a b c d");
    return 0;
}

int test_strutil_collapse_double_spaces_only_spaces(void)
{
    char s[] = "     ";       /* 5 spaces */
    strutil_collapse_double_spaces(s);
    T_ASSERT_EQ_S(s, " ");
    return 0;
}

int test_strutil_collapse_double_spaces_multiple_runs(void)
{
    char s[] = "hello   world  again";
    strutil_collapse_double_spaces(s);
    T_ASSERT_EQ_S(s, "hello world again");
    return 0;
}

int test_strutil_collapse_double_spaces_empty_string(void)
{
    char s[] = "";
    strutil_collapse_double_spaces(s);
    T_ASSERT_EQ_S(s, "");
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Filename extension helpers (strutil_ext_save / _apply / _strip /
 * _append / _length).  Tests use the save+apply pair to verify the
 * scratch slot's hidden state behaves the same way retail's call
 * sites use it (extract from one filename, splice onto another).
 *
 * Tests run in registration order, so a save in one test leaks
 * into the next; each test that depends on a specific scratch
 * value calls `_save` first to pin the state.
 * ────────────────────────────────────────────────────────────────── */

int test_strutil_ext_save_basic(void)
{
    /* Save "pl8" from "Castle.pl8", then apply to a placeholder. */
    strutil_ext_save("Castle.pl8");
    char buf[] = "Other.???";
    strutil_ext_apply(buf);
    T_ASSERT_EQ_S(buf, "Other.pl8");
    return 0;
}

int test_strutil_ext_save_three_chars_only(void)
{
    /* Retail saves exactly 3 bytes past the '.', regardless of the
     * actual extension length.  "Hang.smk" → "smk" + NUL. */
    strutil_ext_save("Hang.smk");
    char buf[] = "X.XXX";
    strutil_ext_apply(buf);
    T_ASSERT_EQ_S(buf, "X.smk");
    return 0;
}

int test_strutil_ext_save_first_dot_wins(void)
{
    /* The walk stops at the FIRST '.', so a file with two dots
     * captures the extension AFTER the first dot — not the last. */
    strutil_ext_save("a.b.cd");
    char buf[] = "X.???";
    strutil_ext_apply(buf);
    /* First '.' is at index 1; next 3 bytes are 'b','.','c'. */
    T_ASSERT_EQ_S(buf, "X.b.c");
    return 0;
}

int test_strutil_ext_save_no_dot_is_noop(void)
{
    /* Pre-seed scratch with a known value, then save() on a
     * dotless filename — scratch should NOT be cleared. */
    strutil_ext_save("seed.abc");
    strutil_ext_save("nodothere");
    char buf[] = "Y.???";
    strutil_ext_apply(buf);
    T_ASSERT_EQ_S(buf, "Y.abc");
    return 0;
}

int test_strutil_ext_apply_no_dot_is_noop(void)
{
    strutil_ext_save("Foo.bar");
    char buf[] = "nodotherex";   /* 10 chars, no '.' */
    strutil_ext_apply(buf);
    T_ASSERT_EQ_S(buf, "nodotherex");
    return 0;
}

int test_strutil_ext_apply_truncates_after_three_chars(void)
{
    /* `_apply` writes 3 bytes after the '.' THEN a NUL — so it
     * truncates any longer pre-existing extension. */
    strutil_ext_save("X.abc");
    char buf[] = "File.longext";
    strutil_ext_apply(buf);
    T_ASSERT_EQ_S(buf, "File.abc");
    return 0;
}

int test_strutil_ext_strip_basic(void)
{
    char buf[] = "Castle.pl8";
    strutil_ext_strip(buf);
    T_ASSERT_EQ_S(buf, "Castle");
    return 0;
}

int test_strutil_ext_strip_first_dot_wins(void)
{
    /* Stops at FIRST '.' — same as save/apply. */
    char buf[] = "a.b.c";
    strutil_ext_strip(buf);
    T_ASSERT_EQ_S(buf, "a");
    return 0;
}

int test_strutil_ext_strip_no_dot_is_noop(void)
{
    char buf[] = "nodothere";
    strutil_ext_strip(buf);
    T_ASSERT_EQ_S(buf, "nodothere");
    return 0;
}

int test_strutil_ext_append_basic(void)
{
    /* Retail call pattern: strip then append re-attaches the saved
     * extension.  Tests both helpers as a pair. */
    strutil_ext_save("Source.wav");
    char buf[16] = "Castle";       /* 6 chars + slack */
    strutil_ext_append(buf);
    T_ASSERT_EQ_S(buf, "Castle.wav");
    return 0;
}

int test_strutil_ext_append_empty_string(void)
{
    /* Walking from index 0; '\0' is the first byte → '.' written
     * at index 0, scratch at 1..3, NUL at 4. */
    strutil_ext_save("X.smk");
    char buf[8] = "";
    strutil_ext_append(buf);
    T_ASSERT_EQ_S(buf, ".smk");
    return 0;
}

int test_strutil_ext_append_does_not_replace(void)
{
    /* Unlike `_apply`, `_append` works on the trailing NUL — even
     * if there's already a '.' in the path, the new ext is
     * appended after the whole thing. */
    strutil_ext_save("X.new");
    char buf[16] = "File.old";
    strutil_ext_append(buf);
    T_ASSERT_EQ_S(buf, "File.old.new");
    return 0;
}

int test_strutil_ext_length_basic(void)
{
    /* "Castle.pl8" — '.' at index 6, so length returns 7. */
    T_ASSERT_EQ_I((int)strutil_ext_length("Castle.pl8"), 7);
    return 0;
}

int test_strutil_ext_length_no_dot_returns_full_length_with_nul(void)
{
    /* "Castle" is 6 bytes + NUL — loop counts all 7. */
    T_ASSERT_EQ_I((int)strutil_ext_length("Castle"), 7);
    return 0;
}

int test_strutil_ext_length_empty_string(void)
{
    /* Empty string: counter increments to 1 before the NUL test
     * exits the loop.  Returns 1, not 0 (retail quirk). */
    T_ASSERT_EQ_I((int)strutil_ext_length(""), 1);
    return 0;
}

int test_strutil_ext_length_dot_at_start(void)
{
    /* "." → '.' at index 0 (1-based 1).  Trailing chars ignored. */
    T_ASSERT_EQ_I((int)strutil_ext_length(".hidden"), 1);
    return 0;
}

int test_strutil_ext_length_first_dot_wins(void)
{
    /* "a.b.c" — first '.' at index 1, returns 2. */
    T_ASSERT_EQ_I((int)strutil_ext_length("a.b.c"), 2);
    return 0;
}

int test_strutil_ext_save_apply_retail_call_pattern(void)
{
    /* Mirrors retail call pattern (e.g. FUN_004af312):
     *   1. ext_save(filename)
     *   2. ext_apply(other_buffer_with_placeholder_ext)
     * Demonstrates the scratch survives between calls. */
    strutil_ext_save("a2k_arch.pl8");
    char dest[] = "MyOwn.XYZ";
    strutil_ext_apply(dest);
    T_ASSERT_EQ_S(dest, "MyOwn.pl8");
    /* Subsequent _apply against another buffer uses the same
     * scratch — no re-save needed. */
    char dest2[] = "Second.???";
    strutil_ext_apply(dest2);
    T_ASSERT_EQ_S(dest2, "Second.pl8");
    return 0;
}
