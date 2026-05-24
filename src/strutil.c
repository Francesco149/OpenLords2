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
