/*
 * OpenLords2 — per-category byte-bucket table (6 buckets × 16 slots)
 *                + 6-byte "first-non-duplicate" digest.
 *
 * Leaf cluster of 4 functions that together model a small bucketed
 * collection used during map-data load:
 *
 *   FUN_004290d0 @ 0x4290d0 (131 B) → engine_category_buckets_clear
 *   FUN_00429153 @ 0x429153  (96 B) → engine_category_buckets_append
 *   FUN_0042925e @ 0x42925e  (81 B) → engine_category_buckets_digest_contains
 *   FUN_004291b3 @ 0x4291b3 (171 B) → engine_category_buckets_refresh_digest
 *
 * Closed under callees — FUN_004291b3 calls FUN_0042925e, which is
 * the only inter-cluster call; the rest are leaves.
 *
 * Data layout (mirrors retail):
 *
 *   buckets[6][16] (DAT_00567970 — 96 B)
 *     For each of 6 categories (numbered 1..6 externally, indexed
 *     0..5 internally), an append-only list of up to 16 byte-sized
 *     entries.  Zero-byte slots are treated as empty / end-of-list.
 *
 *   digest[6] (DAT_00569518 — 6 B)
 *     A "first-non-duplicate" rollup, one byte per bucket.  Refresh
 *     walks each bucket and picks the first byte that isn't already
 *     present in the digest's prior entries — building up a unique
 *     per-bucket representative.  Default value when nothing
 *     qualifies is 0.
 *
 * Caller usage (retail): the map loader (0x467770, "l2_maps.dat")
 * scans 64x64 cells; for each cell whose flag-byte has bit 0x40 set,
 * it calls `append(bucket_id_in_1..6, byte_value)`.  After load
 * completes, the army-setup pipeline (around 0x427ed0) calls
 * refresh_digest() then uses the 6-byte digest to seed per-army
 * starting state.
 *
 * Pure C; no Win32 deps; state struct holds both tables explicitly.
 */

#ifndef OPENLORDS2_ENGINE_CATEGORY_BUCKETS_H
#define OPENLORDS2_ENGINE_CATEGORY_BUCKETS_H

#include <stdint.h>

/* Geometry — matches retail's hardcoded loop bounds. */
#define ENGINE_CATEGORY_BUCKETS_COUNT      6   /* 6 categories */
#define ENGINE_CATEGORY_BUCKETS_SLOTS     16   /* 16 entries per bucket */

/* All state read or written by the cluster. */
typedef struct {
    /* Bucketed entries.  buckets[i][j] is the j-th entry in bucket i.
     * A zero byte = empty / end-of-list (matches retail's "first 0
     * byte terminates" convention used by both append and refresh). */
    uint8_t buckets[ENGINE_CATEGORY_BUCKETS_COUNT]
                   [ENGINE_CATEGORY_BUCKETS_SLOTS];

    /* Per-bucket "first non-duplicate" digest.  Refreshed by
     * refresh_digest().  Each entry is one byte. */
    uint8_t digest[ENGINE_CATEGORY_BUCKETS_COUNT];
} engine_category_buckets_state_t;

/* Port of FUN_004290d0.  Zero out both `buckets` (all 96 B) and
 * `digest` (all 6 B).  Idempotent; safe to call repeatedly. */
void engine_category_buckets_clear(engine_category_buckets_state_t *s);

/* Port of FUN_00429153.  Append `value` to the bucket at 1-based
 * index `bucket_one_based` (i.e. retail's `param_2` argument).
 * Internally indexes into `buckets[bucket_one_based - 1]`.  Finds
 * the first zero-slot in that bucket and writes `value` there.  If
 * all 16 slots are non-zero, silently does nothing (retail behavior
 * — returns without writing past the 16-element bound).
 *
 * Caller MUST ensure bucket_one_based is in [1, 6].  Retail does
 * NOT bounds-check this; out-of-range values would read/write
 * adjacent globals.  Our port likewise does no bounds check —
 * preserved verbatim. */
void engine_category_buckets_append(engine_category_buckets_state_t *s,
                                    uint8_t value,
                                    int bucket_one_based);

/* Port of FUN_0042925e.  Return 1 iff `value` appears in any of
 * the 6 digest entries, else 0.  Used by refresh_digest to skip
 * past entries already present in the digest. */
int engine_category_buckets_digest_contains(
    const engine_category_buckets_state_t *s, uint8_t value);

/* Port of FUN_004291b3.  Rebuild the 6-byte `digest` from
 * `buckets`.  For each bucket index `i` in [0, 6):
 *
 *   1. Start at `value = buckets[i][0]`.
 *   2. While `value` is already in the digest AND fewer than 6
 *      probe attempts have happened:
 *      - read the next "every-other-byte" slot in the same bucket:
 *        buckets[i][offset + 2], advancing offset by 2 each time.
 *      - QUIRK: if the read byte is 0, switch the parity to ODD
 *        indices (offset = 1) — see engine-quirks.md.
 *   3. Write `value` to `digest[i]`.
 *
 * The "every-other-byte" probing and the "switch to odd parity on
 * zero" rules are preserved verbatim — they make refresh_digest's
 * scan irregular but byte-equivalent with retail.  See test cases
 * for the observable corner cases. */
void engine_category_buckets_refresh_digest(
    engine_category_buckets_state_t *s);

#endif /* OPENLORDS2_ENGINE_CATEGORY_BUCKETS_H */
