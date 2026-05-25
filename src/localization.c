/*
 * Localization-blob accessor cluster — pure-C port of FUN_0040187a,
 * FUN_004018d7, FUN_004017bf, FUN_004016f3.  See localization.h
 * for layout, calling conventions, and the catalog of quirks.
 */

#include "localization.h"

#include <string.h>

/* Bounds check used by the seek/read paths: the walker may sit one
 * byte past the end of the buffer (retail's `walker < (byte*)((int)
 * &DAT_005aea40 + 1)` — &DAT_005aea40 is exactly base+120000).  We
 * mirror that off-by-one — see quirk #22. */
static int walker_in_bounds(size_t walker_offset)
{
    return walker_offset < (size_t)LOCALIZATION_BLOB_SIZE + 1;
}

/* Walk `*walker_offset` forward `n_strings` sub-string boundaries.
 *
 * Boundary detector:
 *   accept_nul_prev != 0 → boundary iff *p == 0 && (p[-1] >= 0x20 || p[-1] == 0)
 *   accept_nul_prev == 0 → boundary iff *p == 0 && p[-1] >= 0x20
 *
 * Then advance past any leading control bytes (< 0x20) so the
 * walker lands on the first printable byte of the section.
 *
 * No bounds check beyond what's done at call sites — this helper
 * is meant to be called only after the caller has verified the
 * walker is in range.  Retail does the same. */
static void walk_skip(const unsigned char *blob, size_t *walker_offset,
                      int n_strings, int accept_nul_prev)
{
    size_t w = *walker_offset;
    while (n_strings > 0) {
        unsigned char here = blob[w];
        unsigned char prev = (w > 0) ? blob[w - 1] : 0;
        int boundary = (here == 0)
                       && (prev > 0x1f
                           || (accept_nul_prev && prev == 0));
        if (boundary) {
            n_strings--;
        }
        w++;
    }
    while (blob[w] < 0x20) {
        w++;
    }
    *walker_offset = w;
}

int localization_offset(const unsigned char *blob, int index)
{
    /* 24-bit little-endian at blob + 8 + index*4:
     *   low  = blob[8 + index*4 + 0]
     *   mid  = blob[8 + index*4 + 1]
     *   high = blob[8 + index*4 + 2]
     * 4th byte of the slot is unread. */
    size_t base = (size_t)LOCALIZATION_READ_TABLE_OFFS + (size_t)index * 4;
    unsigned int lo  = blob[base + 0];
    unsigned int mid = blob[base + 1];
    unsigned int hi  = blob[base + 2];
    return (int)((hi << 16) | (mid << 8) | lo);
}

void localization_seek_skip(localization_state_t *state,
                            int index, int n_strings)
{
    int offset = localization_offset(state->blob, index);
    state->walker_offset = (size_t)offset;

    if (!walker_in_bounds(state->walker_offset)) {
        return;   /* retail's `if (walker < base + 120001)` gate */
    }

    /* Quirk #20: this path accepts NUL-after-NUL as a boundary. */
    walk_skip(state->blob, &state->walker_offset, n_strings,
              /*accept_nul_prev=*/1);
}

void localization_read(localization_state_t *state, void *dst,
                       int index, int n_strings, int n_bytes)
{
    int offset = localization_offset(state->blob, index);
    size_t w = (size_t)offset;

    if (!walker_in_bounds(w)) {
        /* Retail leaves DAT_005c9270 alone in this branch — it was
         * a local pointer in FUN_004017bf, not a global write.
         * But our state mirror sees no update either: we do nothing. */
        return;
    }

    /* Quirk #20: read path does NOT accept NUL-after-NUL boundary. */
    walk_skip(state->blob, &w, n_strings, /*accept_nul_prev=*/0);

    /* Copy out `n_bytes` bytes starting at the walker. */
    unsigned char       *out = (unsigned char *)dst;
    const unsigned char *src = state->blob + w;
    for (int i = 0; i < n_bytes; i++) {
        out[i] = src[i];
    }

    state->walker_offset = w;
}

void localization_write(localization_state_t *state, const void *src,
                        int index, int n_strings, int n_bytes)
{
    /* Secondary offset table: base = blob + 0x1c, 4-byte stride,
     * but the offset itself is 16-bit BIG-endian at slot+2..+3.
     * (Retail: `&DAT_0059159c + ((byte*)&DAT_0059159e)[i*4]*0x100 +
     *           ((byte*)&DAT_0059159f)[i*4]`.)
     *
     * Quirk #21: asymmetric with the read-side table.  Likely
     * dead code (zero retail callers). */
    const unsigned char *table = state->blob + LOCALIZATION_WRITE_TABLE_OFFS;
    unsigned int hi = table[2 + index * 4];
    unsigned int lo = table[3 + index * 4];
    size_t w = (size_t)LOCALIZATION_WRITE_TABLE_OFFS + (hi << 8) + lo;

    /* Quirk #22: NO bounds check before the walk.  Mirror retail
     * exactly — a caller passing a deranged index can corrupt
     * memory past the blob end. */
    walk_skip(state->blob, &w, n_strings, /*accept_nul_prev=*/0);

    state->walker_offset = w;

    unsigned char       *dst    = state->blob + w;
    const unsigned char *source = (const unsigned char *)src;
    for (int i = 0; i < n_bytes; i++) {
        dst[i] = source[i];
    }
}
