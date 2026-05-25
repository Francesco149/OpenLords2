/*
 * OpenLords2 — localization-blob accessor cluster.
 *
 * Models the L2_ENG.DAT runtime blob (120,000 bytes, loaded by
 * WinMain into the .bss region rooted at retail address 0x591580).
 * The blob holds the engine's localized strings: language tag,
 * UI text, menu labels, in-game messages, etc.  Sections are
 * indexed via an offset table near the top of the blob; each
 * section contains one or more sub-strings separated by NUL bytes
 * and optionally prefixed by control bytes (< 0x20) that the
 * walker silently skips.
 *
 * Retail address map:
 *
 *   FUN_0040187a @ 0x40187a  (93 B) → localization_offset
 *   FUN_004018d7 @ 0x4018d7 (173 B) → localization_seek_skip
 *   FUN_004017bf @ 0x4017bf (187 B) → localization_read
 *   FUN_004016f3 @ 0x4016f3 (204 B) → localization_write
 *
 * Blob layout (as far as the accessors care):
 *
 *   bytes [0,   8)   — header (unread by this cluster)
 *   bytes [8,  ?)    — primary offset table; 4-byte stride, each
 *                      entry holds a 24-bit little-endian byte
 *                      offset from blob[0] to the section's start.
 *                      The 4th byte of each slot is unread.
 *
 *   bytes [0x1c, ?)  — secondary offset table used by the WRITE path
 *                      only.  4-byte stride, but the offset lives at
 *                      bytes +2..+3 of each slot and is read as a
 *                      16-bit BIG-endian value relative to byte 0x1c
 *                      of the blob (not 0).  See quirk #21: this is
 *                      asymmetric with the read-side table and the
 *                      write function has zero retail callers — most
 *                      likely dead-code from an editor build that did
 *                      not ship.
 *
 *   bytes [?,  120000) — section payloads.  Each section is a run of
 *                      printable bytes (>= 0x20) and NUL terminators.
 *                      Sub-string boundaries are detected by the
 *                      pattern "NUL preceded by a printable byte" —
 *                      see localization_seek_skip for the variant
 *                      that also accepts NUL-after-NUL boundaries.
 *
 * Walker pointer:
 *
 *   Retail keeps a single global walker pointer at DAT_005c9270.
 *   Some callers read it directly after a seek (e.g. 49caaa.c:72
 *   copies 0x10 bytes from the walker into a per-army field).  Our
 *   port surfaces the walker as `state->walker_offset` — a byte
 *   offset relative to the blob base, valid after any seek/read.
 *
 *   The same `DAT_005c9270` global is also repurposed by other
 *   subsystems (e.g. FUN_0042ab82 redirects it into the per-player
 *   strategic-state region) — we deliberately don't model that
 *   crossover here; it lives in a sister state struct.
 *
 * Bounds:
 *
 *   The read paths gate their walk on `walker < blob + 120001` —
 *   one byte past the end is treated as valid (retail off-by-one).
 *   The write path performs no bounds check whatsoever — see
 *   quirk #22.
 *
 * Pure C; no Win32 deps; no globals — all state held in
 * `localization_state_t`.
 */

#ifndef OPENLORDS2_LOCALIZATION_H
#define OPENLORDS2_LOCALIZATION_H

#include <stddef.h>

#define LOCALIZATION_BLOB_SIZE        120000
#define LOCALIZATION_READ_TABLE_OFFS  8        /* primary offset table */
#define LOCALIZATION_WRITE_TABLE_OFFS 0x1c     /* secondary table, write only */

typedef struct {
    /* Pointer to the loaded blob.  Retail's value is 0x591580 in
     * .bss after FUN_004af8d9 reads l2_eng.dat into it.  Tests
     * supply their own buffer. */
    unsigned char *blob;

    /* Byte offset of the walker pointer, relative to `blob`.
     * Updated by every seek/read call; readable directly so
     * downstream callers (mirror of retail's 49caaa.c:72) can
     * copy from `blob + walker_offset` after a seek. */
    size_t walker_offset;
} localization_state_t;

/* Port of FUN_0040187a.  Read a 24-bit little-endian byte offset
 * out of the primary offset table at `blob + 8 + index*4`.
 * Returns the raw offset value — the caller decides whether to
 * combine it with blob base or some other anchor. */
int localization_offset(const unsigned char *blob, int index);

/* Port of FUN_004018d7.  Seek the walker to section `index`, then
 * walk forward `n_strings` sub-string boundaries, then skip
 * leading control bytes (< 0x20) to land on the first printable
 * byte.  Boundary detector accepts NUL-preceded-by-NUL as a
 * valid boundary (see quirk #20).
 *
 * Side effect: updates `state->walker_offset`.  If the seek goes
 * out of bounds, the walker offset is set to the (out-of-range)
 * post-seek value but no walk is performed.
 *
 * Reads `state->blob` but never writes to it. */
void localization_seek_skip(localization_state_t *state,
                            int index, int n_strings);

/* Port of FUN_004017bf.  Equivalent to localization_seek_skip
 * followed by `memcpy(dst, walker, n_bytes)`, except:
 *   - Boundary detector requires the byte BEFORE the NUL to be
 *     >= 0x20 (does NOT accept NUL-preceded-by-NUL).
 *   - If the seek goes out of bounds, the entire walk + copy is
 *     skipped (walker offset still set, dst untouched).
 *   - Uses a local walker, so `state->walker_offset` is updated
 *     to the post-walk byte offset.
 *
 * `dst` may be any byte buffer of at least n_bytes capacity. */
void localization_read(localization_state_t *state, void *dst,
                       int index, int n_strings, int n_bytes);

/* Port of FUN_004016f3.  Write `n_bytes` bytes from `src` into the
 * blob at the location identified by:
 *
 *   - The SECONDARY offset table (bytes [0x1c, ?) of the blob),
 *     16-bit big-endian.
 *   - Then `n_strings` sub-string boundaries walked forward (same
 *     boundary detector as localization_read — requires prev byte
 *     >= 0x20).
 *   - Then control-byte (< 0x20) prefix skipped.
 *
 * Performs NO bounds check (see quirk #22 — the write function
 * has zero retail callers and is preserved as dead-but-faithful
 * code).
 *
 * Updates `state->walker_offset` to the post-skip destination
 * byte offset before writing. */
void localization_write(localization_state_t *state, const void *src,
                        int index, int n_strings, int n_bytes);

#endif /* OPENLORDS2_LOCALIZATION_H */
