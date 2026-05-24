/*
 * OpenLords2 — small string helpers (header).
 *
 * Port of the engine's four ad-hoc string-utility functions at
 *
 *     FUN_00401000  bounded substring search
 *     FUN_00401085  bounded character search
 *     FUN_00401136  bounded copy (no null terminator)
 *     FUN_004012a2  strip control / 8-bit-set bytes
 *
 * (FUN_004010da, the bounded memcmp consumed by FUN_00401000, is
 * inlined into strutil_find_substr — it has no other call sites
 * worth a separate export.  FUN_004c2090 is the MSVC CRT's optimised
 * strcat and not engine code — we use libc strcat where retail
 * called it.)
 *
 * These are reproduced verbatim from retail, including quirks:
 *
 *   - strutil_find_substr returns the 0-based offset of the match.
 *     "Not found" returns 0 — the SAME value as "found at offset 0".
 *     Retail callers (e.g. the .ini parser in FUN_0040ecc4) work
 *     around this with the convention that all real matches sit at
 *     offset > 0 (.ini files start with comments or headers, never
 *     directly with a key name).  We preserve the quirk so any
 *     future caller that depends on retail semantics sees them.
 *
 *   - strutil_find_char returns 1-based: 0 = not found, otherwise
 *     offset+1.  Different convention from strutil_find_substr.
 *
 *   - strutil_bounded_copy does NOT null-terminate.  It's a fixed-n
 *     memcpy; the caller writes a terminator if it needs one.
 *
 *   - strutil_strip_ctrl is a "find first byte outside [0x20, 0xa6]
 *     and null-terminate there" — used by the .ini parser to strip
 *     trailing CR/LF and any high-bit junk past the value.
 */

#ifndef OPENLORDS2_STRUTIL_H
#define OPENLORDS2_STRUTIL_H

#include <stddef.h>

/* Port of `FUN_00401000 @ 0x401000`.  Bounded substring search.
 * Returns the 0-based byte offset of the first occurrence of
 * `needle` inside `haystack` (limited to `max` bytes).  Returns 0
 * for "not found" *or* "found at offset 0" — see header comment. */
int  strutil_find_substr(const char *haystack, const char *needle, int max);

/* Port of `FUN_00401085 @ 0x401085`.  Bounded character search.
 * Returns offset+1 (1-based) of the first `c` in the first `max`
 * bytes of `str`, or 0 if not found. */
int  strutil_find_char(const char *str, char c, int max);

/* Port of `FUN_00401136 @ 0x401136`.  Bounded byte copy.  No null
 * terminator written. */
void strutil_bounded_copy(void *dst, const void *src, int n);

/* Port of `FUN_004012a2 @ 0x4012a2`.  Scans forward; the first byte
 * outside the inclusive range [0x20, 0xa6] gets replaced with '\0'.
 * Stops on the existing '\0' if encountered first.  Effective for
 * stripping trailing CR/LF/whitespace from .ini value lines. */
void strutil_strip_ctrl(unsigned char *str);

#endif /* OPENLORDS2_STRUTIL_H */
