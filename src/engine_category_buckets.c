/*
 * Per-category byte-bucket table — pure-C port of the FUN_004290d0
 * cluster.  See engine_category_buckets.h for the API doc and the
 * preserved-quirk callouts.
 */

#include "engine_category_buckets.h"

#include <string.h>

void engine_category_buckets_clear(engine_category_buckets_state_t *s)
{
    /* Retail does two explicit nested loops (6x16 zero + 6 zero)
     * rather than a memset.  Observable end state is identical. */
    for (int i = 0; i < ENGINE_CATEGORY_BUCKETS_COUNT; i++) {
        for (int j = 0; j < ENGINE_CATEGORY_BUCKETS_SLOTS; j++) {
            s->buckets[i][j] = 0;
        }
    }
    for (int i = 0; i < ENGINE_CATEGORY_BUCKETS_COUNT; i++) {
        s->digest[i] = 0;
    }
}

void engine_category_buckets_append(engine_category_buckets_state_t *s,
                                    uint8_t value,
                                    int bucket_one_based)
{
    /* Retail uses `param_2 - 1` as the row index; we mirror that. */
    int row = bucket_one_based - 1;

    /* Find the first zero slot in this bucket.  Loop bound: retail's
     * `0xf < local_8` — i.e. local_8 reaches 16 then returns without
     * writing.  We replicate the early-return shape. */
    for (int slot = 0; slot < ENGINE_CATEGORY_BUCKETS_SLOTS; slot++) {
        if (s->buckets[row][slot] == 0) {
            s->buckets[row][slot] = value;
            return;
        }
    }
    /* Bucket full — silent no-op. */
}

int engine_category_buckets_digest_contains(
    const engine_category_buckets_state_t *s, uint8_t value)
{
    /* Retail iterates 0..5 and breaks on match; returns 0 if never
     * matched.  Note the loop bound is `5 < local_8` — so it loops
     * while local_8 <= 5, i.e. 0..5 inclusive. */
    for (int i = 0; i < ENGINE_CATEGORY_BUCKETS_COUNT; i++) {
        if (s->digest[i] == value) {
            return 1;
        }
    }
    return 0;
}

void engine_category_buckets_refresh_digest(
    engine_category_buckets_state_t *s)
{
    /* For each bucket, walk its "every other byte" until we hit a
     * value not already in the digest (or we've made 6 probe
     * attempts). */
    for (int bucket = 0; bucket < ENGINE_CATEGORY_BUCKETS_COUNT; bucket++) {
        int    probes = 0;
        int    offset = 0;
        uint8_t value = s->buckets[bucket][0];

        /* While digest already contains `value` AND we have probe
         * attempts left (< 6 probes after increment) — keep
         * searching.  The loop conditional in retail is:
         *
         *   (iVar1 = FUN_0042925e(local_10),
         *    iVar1 != 0 && (local_c = local_c + 1, local_c < 6))
         *
         * which is "found AND (probes+1 < 6)" — i.e. up to 5
         * additional probes after the initial value. */
        while (engine_category_buckets_digest_contains(s, value)) {
            probes++;
            if (probes >= 6) {
                break;
            }

            /* Read the NEXT every-other-byte slot.  offset advances
             * by 2 each iter; the read index is `(offset + 2)`
             * which is the slot AFTER offset.  The initial probe
             * before the loop body was at slot 0; subsequent
             * probes are at slot 2, 4, 6, 8, ...
             *
             * Wait — the retail decomp reads index
             * `local_14 + 0x567972 + local_8 * 0x10` which is
             * `bucket_base + 2 + local_14`.  So when local_14 = 0
             * it reads bucket[2]; when local_14 = 2 it reads
             * bucket[4]; etc.  The local_14 += 2 happens AFTER the
             * read, so the read indices are: 2, 4, 6, ...
             *
             * For 6 probes (matches the < 6 cap), we'd read slots
             * 2, 4, 6, 8, 10, 12. */
            value = s->buckets[bucket][offset + 2];
            offset += 2;

            /* QUIRK: zero-byte fallback to odd-parity probing.
             * If the just-read value is 0, retail sets local_14 = 1
             * (not 0!) — so the NEXT iter reads slot (1 + 2) = 3,
             * then 5, 7, ... .  This flips the probing to odd
             * indices after the first zero is encountered.
             * Documented as quirk #14 in engine-quirks.md. */
            if (value == 0) {
                offset = 1;
            }
        }

        s->digest[bucket] = value;
    }
}
