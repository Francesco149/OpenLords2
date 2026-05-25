/*
 * OpenLords2 — small string helpers (port).
 *
 * Each function below is a faithful port of its retail counterpart;
 * see strutil.h for the address mapping and quirk notes.
 */

#include <string.h>

#include "strutil.h"

/* Port of `FUN_004010da @ 0x4010da`.  Bounded memcmp.  Returns 0 if
 * the first `n` bytes of `a` and `b` are equal, otherwise the 1-based
 * offset of the first mismatch.  Inlined in strutil_find_substr; not
 * exported on its own. */
static int bounded_memcmp_quirky(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return i + 1;
    }
    return 0;
}

int strutil_find_substr(const char *haystack, const char *needle, int max)
{
    if (!haystack || !needle) return 0;
    const size_t nlen = strlen(needle);
    for (int i = 0; i < max; i++) {
        /* Retail's first-byte short-circuit + bounded memcmp.  We
         * mirror it so the iteration count matches retail exactly. */
        if (haystack[i] == needle[0] &&
                bounded_memcmp_quirky(haystack + i, needle, (int)nlen) == 0) {
            return i;
        }
    }
    return 0;
}

int strutil_find_char(const char *str, char c, int max)
{
    if (!str) return 0;
    for (int i = 0; i < max; i++) {
        if (str[i] == c) return i + 1;
    }
    return 0;
}

void strutil_bounded_copy(void *dst, const void *src, int n)
{
    /* Faithful byte-for-byte loop — `memmove` would be slightly
     * faster, but retail's loop is byte-at-a-time and we match its
     * exact behaviour on overlapping regions (which is "undefined,
     * but in practice forward-copy"). */
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

void strutil_strip_ctrl(unsigned char *str)
{
    if (!str) return;
    while (*str) {
        if (*str < 0x20)      { *str = 0; return; }   /* CR / LF / TAB / NUL */
        if (*str > 0xa6)      { *str = 0; return; }   /* 8-bit junk */
        str++;
    }
    /* Already null-terminated at this point. */
}

/* ----- case conversion -------------------------------------------- */

unsigned int strutil_to_upper_char(unsigned char c)
{
    /* Retail: 0 unless c ∈ ('`', '{') exclusive — i.e. 'a' through 'z'.
     * Note the comparison `0x60 < c && c < 0x7b`. */
    if (0x60 < c && c < 0x7b) {
        return (unsigned int)(c - 0x20);
    }
    return 0;
}

unsigned int strutil_to_lower_char(unsigned char c)
{
    if (0x40 < c && c < 0x5b) {
        return (unsigned int)(c + 0x20);
    }
    return 0;
}

void strutil_to_upper_inplace(unsigned char *str)
{
    /* Different from the single-char variant: this one preserves
     * non-letter bytes (the single-char variant would zero them). */
    if (!str) return;
    for (; *str; str++) {
        unsigned char c = *str;
        if (0x60 < c && c < 0x7b) {
            *str = (unsigned char)(c - 0x20);
        }
    }
}

void strutil_to_lower_inplace(unsigned char *str)
{
    if (!str) return;
    for (; *str; str++) {
        unsigned char c = *str;
        if (0x40 < c && c < 0x5b) {
            *str = (unsigned char)(c + 0x20);
        }
    }
}

/* ----- string mutation -------------------------------------------- */

void strutil_shift_left(unsigned char *start, unsigned char *end)
{
    /* Walks p from start to end-1, copying p[1] into p[0].  After the
     * loop p == end; the terminator write at *end finalizes the
     * deletion. */
    unsigned char *p = start;
    for (; p < end; p++) {
        *p = p[1];
    }
    *p = 0;     /* *end = 0 */
}

void strutil_shift_right(unsigned char *start, unsigned char *end)
{
    /* Right-shift, iterating end→start+1 to avoid clobbering source.
     * end[1] is the new terminator slot. */
    end[1] = 0;
    for (; start < end; end--) {
        *end = end[-1];
    }
    /* On exit end == start; *start is untouched (retains its original
     * byte, which is now duplicated into *(start+1)).  Caller usually
     * overwrites *start with its inserted character. */
}

void strutil_strip_leading_spaces(char *str)
{
    /* Quadratic — for each leading space, shift the entire string
     * one byte left.  Retail does this verbatim; we mirror it. */
    if (!str) return;
    while (*str == ' ') {
        for (int i = 0; str[i] != '\0'; i++) {
            str[i] = str[i + 1];
        }
    }
}

void strutil_trim_trailing_spaces(char *str)
{
    if (!str) return;

    /* First pass: find the position of the null terminator. */
    int i = 0;
    while (str[i] != '\0') i++;

    /* Walk backward, replacing trailing spaces with '\0'.  Retail's
     * loop decrements i BEFORE checking, so on an all-spaces string
     * the final check reads str[-1].  We mirror that read literally —
     * see header for the OOB-read quirk note. */
    for (;;) {
        i--;
        if (str[i] != ' ') break;
        str[i] = '\0';
    }
}

/* ----- decimal parsing -------------------------------------------- */

int strutil_decimal_atoi(const char *str)
{
    /* Retail's powers-of-10 table at DAT_004d2030.  Only 8 entries —
     * matches retail exactly; see header for the 9+ digit quirk. */
    static const int powers_of_10[8] = {
        1, 10, 100, 1000, 10000, 100000, 1000000, 10000000
    };

    /* First pass: count leading digits (cap at 8 — see quirk note). */
    int n_digits = 0;
    const char *p = str;
    while (*p >= '0' && *p <= '9') {
        n_digits++;
        p++;
        if (n_digits == 8) break;
    }

    /* Second pass: walk back from rightmost-digit's power-of-10 entry. */
    int result = 0;
    p = str;
    while (n_digits != 0) {
        n_digits--;
        result += powers_of_10[n_digits] * (*p - '0');
        p++;
    }
    return result;
}

/* ----- whitespace collapse ---------------------------------------- */

void strutil_collapse_double_spaces(char *str)
{
    /* First pass: find the null terminator's position, capped at 64000.
     * Retail leaves `local_c` uninitialized if no null is found in
     * the first 64000 chars (UB).  We initialize to 0 explicitly so
     * the second pass is a no-op in that case — see header note. */
    int terminator_pos = 0;
    for (int i = 0; i <= 63999; i++) {
        if (str[i] == '\0') {
            terminator_pos = i;
            break;
        }
    }

    /* Second pass: collapse consecutive spaces.  `prev_was_space`
     * tracks whether the previous index was also a space. */
    int prev_was_space = 0;
    for (int i = 0; i < terminator_pos && str[i] != '\0'; i++) {
        if (str[i] == ' ') {
            if (prev_was_space) {
                /* Retail: back up local_8 by one then shift left.
                 * After the shift, the same index now holds what
                 * was the next char.  Loop's i++ moves us forward. */
                i--;
                strutil_shift_left((unsigned char *)(str + i),
                                   (unsigned char *)(str + terminator_pos));
            } else {
                prev_was_space = 1;
            }
        } else {
            prev_was_space = 0;
        }
    }
}
