/*
 * Unit tests for engine_category_buckets — 6x16 bucket table + 6-byte
 * "first non-duplicate" digest.
 */

#include "t.h"
#include "engine_category_buckets.h"
#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * clear
 * ────────────────────────────────────────────────────────────────── */

int test_engine_category_buckets_clear_zeros_both_tables(void) {
    engine_category_buckets_state_t s;
    memset(&s, 0xee, sizeof s);          /* poison */

    engine_category_buckets_clear(&s);

    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(s.digest[i], 0);
        for (int j = 0; j < 16; j++) {
            T_ASSERT_EQ_I(s.buckets[i][j], 0);
        }
    }
    return 0;
}

int test_engine_category_buckets_clear_is_idempotent(void) {
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    engine_category_buckets_clear(&s);   /* second call shouldn't crash */
    /* All zero. */
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(s.digest[i], 0);
    }
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * append
 * ────────────────────────────────────────────────────────────────── */

int test_engine_category_buckets_append_first_slot_of_empty_bucket(void) {
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);

    engine_category_buckets_append(&s, 42, /*bucket*/ 1);
    T_ASSERT_EQ_I(s.buckets[0][0], 42);
    T_ASSERT_EQ_I(s.buckets[0][1], 0);    /* still zero */
    return 0;
}

int test_engine_category_buckets_append_advances_past_filled_slots(void) {
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);

    engine_category_buckets_append(&s, 11, 1);
    engine_category_buckets_append(&s, 22, 1);
    engine_category_buckets_append(&s, 33, 1);
    T_ASSERT_EQ_I(s.buckets[0][0], 11);
    T_ASSERT_EQ_I(s.buckets[0][1], 22);
    T_ASSERT_EQ_I(s.buckets[0][2], 33);
    T_ASSERT_EQ_I(s.buckets[0][3], 0);
    return 0;
}

int test_engine_category_buckets_append_independent_buckets(void) {
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);

    engine_category_buckets_append(&s, 10, 1);
    engine_category_buckets_append(&s, 20, 3);
    engine_category_buckets_append(&s, 30, 6);

    T_ASSERT_EQ_I(s.buckets[0][0], 10);
    T_ASSERT_EQ_I(s.buckets[2][0], 20);
    T_ASSERT_EQ_I(s.buckets[5][0], 30);
    /* Other buckets untouched. */
    T_ASSERT_EQ_I(s.buckets[1][0], 0);
    T_ASSERT_EQ_I(s.buckets[3][0], 0);
    T_ASSERT_EQ_I(s.buckets[4][0], 0);
    return 0;
}

int test_engine_category_buckets_append_full_bucket_silently_drops(void) {
    /* Fill bucket 2 with 16 entries; the 17th append is silently
     * dropped (retail returns past the `0xf < local_8` check). */
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);

    for (int i = 0; i < 16; i++) {
        engine_category_buckets_append(&s, (uint8_t)(i + 1), 2);
    }
    /* Bucket 1 is fully populated 1..16. */
    for (int i = 0; i < 16; i++) {
        T_ASSERT_EQ_I(s.buckets[1][i], (uint8_t)(i + 1));
    }

    /* 17th append — should be silently dropped, bucket unchanged. */
    engine_category_buckets_append(&s, 99, 2);
    for (int i = 0; i < 16; i++) {
        T_ASSERT_EQ_I(s.buckets[1][i], (uint8_t)(i + 1));
    }
    return 0;
}

int test_engine_category_buckets_append_skips_zero_in_middle(void) {
    /* Retail's "find first zero" actually finds the first zero — even
     * if there's a non-zero entry AFTER it.  So a hole in the middle
     * gets refilled. */
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);

    s.buckets[0][0] = 1;
    s.buckets[0][1] = 0;    /* hole */
    s.buckets[0][2] = 3;
    s.buckets[0][3] = 4;

    engine_category_buckets_append(&s, 99, 1);
    T_ASSERT_EQ_I(s.buckets[0][1], 99);  /* fills the hole */
    T_ASSERT_EQ_I(s.buckets[0][2], 3);   /* untouched */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * digest_contains
 * ────────────────────────────────────────────────────────────────── */

int test_engine_category_buckets_digest_contains_found(void) {
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    s.digest[3] = 42;
    T_ASSERT_EQ_I(engine_category_buckets_digest_contains(&s, 42), 1);
    return 0;
}

int test_engine_category_buckets_digest_contains_not_found(void) {
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    s.digest[2] = 10;
    s.digest[4] = 20;
    T_ASSERT_EQ_I(engine_category_buckets_digest_contains(&s, 99), 0);
    return 0;
}

int test_engine_category_buckets_digest_contains_zero_matches_default(void) {
    /* The digest defaults to all zeros — so digest_contains(0) is
     * trivially true after init.  This is documented as part of the
     * refresh_digest probing logic; tests of refresh exercise it. */
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    T_ASSERT_EQ_I(engine_category_buckets_digest_contains(&s, 0), 1);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * refresh_digest
 * ────────────────────────────────────────────────────────────────── */

int test_engine_category_buckets_refresh_picks_bucket0_initial(void) {
    /* Setup: bucket 0 starts with byte 10.  digest defaults to all 0.
     * First iter: value = 10.  digest_contains(10)? No → write
     * digest[0] = 10.  Subsequent buckets get 0 (their bucket[0] is
     * 0, digest already contains 0, probes hit the 6-attempt cap). */
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    s.buckets[0][0] = 10;

    engine_category_buckets_refresh_digest(&s);
    T_ASSERT_EQ_I(s.digest[0], 10);
    /* Buckets 1..5 are all-zero, so digest[1..5] stays 0 (0 is in
     * the digest from bucket 0's pass, so the search probes 6 times
     * finding 0 each time, then caps out). */
    for (int i = 1; i < 6; i++) {
        T_ASSERT_EQ_I(s.digest[i], 0);
    }
    return 0;
}

int test_engine_category_buckets_refresh_skips_duplicate_initial(void) {
    /* bucket[0][0] = 10  (becomes digest[0])
     * bucket[1][0] = 10  (duplicate — probe further in bucket 1)
     * bucket[1][2] = 20  (first even-index non-duplicate after probe)
     *
     * Expected: digest[0] = 10, digest[1] = 20. */
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    s.buckets[0][0] = 10;
    s.buckets[1][0] = 10;
    s.buckets[1][2] = 20;

    engine_category_buckets_refresh_digest(&s);
    T_ASSERT_EQ_I(s.digest[0], 10);
    T_ASSERT_EQ_I(s.digest[1], 20);
    return 0;
}

int test_engine_category_buckets_refresh_probe_caps_at_6(void) {
    /* All entries in bucket 1 are duplicates of bucket 0's value.
     * The probe loop hits the 6-attempt cap and stops, leaving
     * digest[1] = the last value read (still 10).
     *
     * Layout: bucket 1's even-index slots [0, 2, 4, 6, 8, 10, 12] are
     * all 10; the 6-cap is reached at probes=6 (5 reads after the
     * initial), so digest[1] gets whatever's at slot 10 (= 10). */
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    s.buckets[0][0] = 10;
    for (int j = 0; j < 16; j += 2) {
        s.buckets[1][j] = 10;
    }

    engine_category_buckets_refresh_digest(&s);
    T_ASSERT_EQ_I(s.digest[0], 10);
    T_ASSERT_EQ_I(s.digest[1], 10);
    return 0;
}

int test_engine_category_buckets_refresh_zero_value_flips_to_odd_parity(void) {
    /* QUIRK #14 — when the every-other-byte probe reads a 0, the
     * offset is reset to 1 (odd parity) for the next probe.
     *
     * Setup: bucket 0 → digest[0] = 5.
     *        bucket 1 [0] = 5    (dup, probe further at slot 2)
     *        bucket 1 [2] = 0    (zero! → offset jumps from 2 to 1)
     *        bucket 1 [3] = 7    (next probe reads slot 1+2 = 3)
     *
     * Expected: digest[1] = 7 (the odd-parity-flipped probe wins). */
    engine_category_buckets_state_t s;
    engine_category_buckets_clear(&s);
    s.buckets[0][0] = 5;
    s.buckets[1][0] = 5;      /* duplicate of digest[0]; probe to slot 2 */
    s.buckets[1][2] = 0;      /* triggers odd-parity flip */
    s.buckets[1][3] = 7;      /* odd-slot read after flip */

    engine_category_buckets_refresh_digest(&s);
    T_ASSERT_EQ_I(s.digest[0], 5);
    T_ASSERT_EQ_I(s.digest[1], 7);
    return 0;
}
