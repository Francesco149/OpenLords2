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
