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

/* ----- case conversion -------------------------------------------- */

/* Port of `FUN_00401176 @ 0x401176`.  Single-char to-upper.
 * Returns param-0x20 iff param ∈ ['a', 'z'], else 0.
 * Note: out-of-range inputs return 0, NOT param unchanged. */
unsigned int strutil_to_upper_char(unsigned char c);

/* Port of `FUN_004011b0 @ 0x4011b0`.  Single-char to-lower.
 * Returns param+0x20 iff param ∈ ['A', 'Z'], else 0.
 * Same out-of-range-returns-zero quirk as the upper variant. */
unsigned int strutil_to_lower_char(unsigned char c);

/* Port of `FUN_004011ea @ 0x4011ea`.  In-place to-upper across a
 * null-terminated string.  Each char in ['a','z'] is converted to
 * the corresponding uppercase; other chars pass through unchanged.
 * (Note: the whole-string variant is DIFFERENT from the single-char
 * variant — it preserves non-letter bytes, while strutil_to_upper_char
 * zeros them.  Both behaviors are preserved verbatim.) */
void strutil_to_upper_inplace(unsigned char *str);

/* Port of `FUN_00401246 @ 0x401246`.  In-place to-lower mirror of
 * the above; chars in ['A','Z'] become lowercase, others pass
 * through unchanged. */
void strutil_to_lower_inplace(unsigned char *str);

/* ----- string mutation -------------------------------------------- */

/* Port of `FUN_00401303 @ 0x401303`.  Left-shift the slice
 * [start, end] by one byte: deletes the character at *start, shifts
 * (end - start) bytes leftward, and writes '\0' at *end.
 * Effectively `memmove(start, start+1, end - start); *end = '\0';`. */
void strutil_shift_left(unsigned char *start, unsigned char *end);

/* Port of `FUN_00401333 @ 0x401333`.  Right-shift the slice
 * [start, end] by one byte: writes '\0' at end[1] (extends by one),
 * shifts (end - start) bytes rightward.  Leaves the byte at *start
 * with its original value (effectively duplicated into start[1]) —
 * caller typically overwrites *start immediately after. */
void strutil_shift_right(unsigned char *start, unsigned char *end);

/* Port of `FUN_00401364 @ 0x401364`.  Strip ALL leading spaces from
 * `str` in place.  Operates as: while *str == ' ', shift the entire
 * string one byte left.  Quadratic in the number of leading spaces;
 * preserved verbatim from retail. */
void strutil_strip_leading_spaces(char *str);

/* Port of `FUN_004013bc @ 0x4013bc`.  Trim trailing spaces from
 * `str` in place by replacing them with '\0' from the end backward.
 * QUIRK: if the string is all-spaces, retail's decrement-then-test
 * loop reads one byte before the start (`str[-1]`) on the exit
 * check.  Caller-invisible iff the byte before isn't ' '.  Preserved
 * verbatim — callers in retail are documented as always passing
 * pointers to interior offsets of larger buffers, so the OOB read
 * never goes past the surrounding allocation. */
void strutil_trim_trailing_spaces(char *str);

/* Port of `FUN_00401666 @ 0x401666`.  Parses leading run of ASCII
 * decimal digits (0x30..0x39) as a base-10 integer.  Returns the
 * integer value; consumes characters only up to (but not including)
 * the first non-digit.
 *
 * Retail implementation walks a powers-of-10 table at DAT_004d2030
 * with one entry per digit position:
 *     [1, 10, 100, 1000, 10000, 100000, 1000000, 10000000]
 * — only 8 entries.  If the input has 9+ digits, retail reads off
 * the end of the table into the adjacent .data string "ERR:_top_it
 * not data..." and multiplies digits by garbage.  Our port caps
 * the digit count at 8 explicitly to avoid the UB; inputs with
 * 9+ digits silently truncate to the first 8 (instead of doing
 * the corrupted multiply).
 *
 * Sign / whitespace / leading-zero handling: none.  Pure leading
 * digit run only.  Empty input or no leading digit returns 0.
 *
 * No callees, no globals read or written beyond the const table. */
int strutil_decimal_atoi(const char *str);

/* Port of `FUN_00401415 @ 0x401415`.  Collapses runs of two or more
 * consecutive spaces into a single space, in place.  Two passes:
 *
 *   1. Scan forward to find the null terminator's position (cap at
 *      64000 chars).
 *   2. Walk again from the start; for each space that follows
 *      another space, shift the remainder of the buffer one byte
 *      left (calling strutil_shift_left), then re-process the
 *      same position (which now holds the next char).
 *
 * Retail quirk: if the buffer has no null in its first 64000
 * bytes, retail falls through to the second pass with an
 * uninitialized `local_c` (read from the stack).  Our port
 * initializes the terminator position to 0 in that case, making
 * the second pass a no-op — different from retail's UB but
 * defensible.  Callers must null-terminate inputs.
 *
 * Only adjacent spaces are collapsed; isolated single spaces are
 * preserved.  Leading and trailing spaces collapse to one. */
void strutil_collapse_double_spaces(char *str);

#endif /* OPENLORDS2_STRUTIL_H */
