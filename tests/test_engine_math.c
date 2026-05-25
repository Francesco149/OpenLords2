/*
 * Unit tests for engine_math — small leaf math helpers.
 */

#include "t.h"
#include "engine_math.h"

/* ──────────────────────────── percent_of ─────────────────────────── */

int test_engine_math_percent_of_basic(void) {
    T_ASSERT_EQ_I(engine_math_percent_of(200, 25), 50);   /* 25% of 200 */
    T_ASSERT_EQ_I(engine_math_percent_of(100, 100), 100);
    T_ASSERT_EQ_I(engine_math_percent_of(100, 0), 0);
    return 0;
}

int test_engine_math_percent_of_truncates(void) {
    /* (10 * 33) / 100 = 330 / 100 = 3.3 → truncates to 3. */
    T_ASSERT_EQ_I(engine_math_percent_of(10, 33), 3);
    return 0;
}

/* ──────────────────────────── bp_of ──────────────────────────────── */

int test_engine_math_bp_of_basic(void) {
    /* 1234 basis points (12.34%) of 10000 = 1234. */
    T_ASSERT_EQ_I(engine_math_bp_of(10000, 1234), 1234);
    /* 5000 bp (50%) of 200 = 100. */
    T_ASSERT_EQ_I(engine_math_bp_of(200, 5000), 100);
    /* 0 bp = 0. */
    T_ASSERT_EQ_I(engine_math_bp_of(99999, 0), 0);
    return 0;
}

/* ──────────────────────────── percent_div ────────────────────────── */

int test_engine_math_percent_div_basic(void) {
    /* 50 / 200 = 25%. */
    T_ASSERT_EQ_I(engine_math_percent_div(50, 200), 25);
    /* 100 / 100 = 100%. */
    T_ASSERT_EQ_I(engine_math_percent_div(100, 100), 100);
    return 0;
}

int test_engine_math_percent_div_divide_by_zero_returns_zero(void) {
    T_ASSERT_EQ_I(engine_math_percent_div(50, 0), 0);
    T_ASSERT_EQ_I(engine_math_percent_div(0, 0), 0);
    return 0;
}

/* ──────────────────────────── div_ceil ──────────────────────────── */

int test_engine_math_div_ceil_exact(void) {
    T_ASSERT_EQ_I(engine_math_div_ceil(10, 5), 2);
    T_ASSERT_EQ_I(engine_math_div_ceil(0, 5), 0);
    return 0;
}

int test_engine_math_div_ceil_remainder(void) {
    T_ASSERT_EQ_I(engine_math_div_ceil(11, 5), 3);
    T_ASSERT_EQ_I(engine_math_div_ceil(1, 5), 1);
    T_ASSERT_EQ_I(engine_math_div_ceil(9, 5), 2);
    return 0;
}

int test_engine_math_div_ceil_divide_by_zero(void) {
    T_ASSERT_EQ_I(engine_math_div_ceil(99, 0), 0);
    T_ASSERT_EQ_I(engine_math_div_ceil(0, 0), 0);
    return 0;
}

/* ──────────────────────────── lookup_threshold ──────────────────── */

int test_engine_math_lookup_threshold_first_pair_matches(void) {
    /* table: (10, 100), (20, 200), (30, 300).  query = 5 → first
     * key (10) > 5, return its value (100). */
    int table[] = { 10, 100, 20, 200, 30, 300 };
    T_ASSERT_EQ_I(engine_math_lookup_threshold(5, table, 3, 999), 100);
    return 0;
}

int test_engine_math_lookup_threshold_walks_to_second_pair(void) {
    /* query 15: skip (10) which is NOT > 15, but the table
     * indexes pair 1 (20) which IS > 15 → return 200. */
    int table[] = { 10, 100, 20, 200, 30, 300 };
    T_ASSERT_EQ_I(engine_math_lookup_threshold(15, table, 3, 999), 200);
    return 0;
}

int test_engine_math_lookup_threshold_no_match_returns_fallback(void) {
    /* query 999: no key is > 999, return fallback. */
    int table[] = { 10, 100, 20, 200, 30, 300 };
    T_ASSERT_EQ_I(engine_math_lookup_threshold(999, table, 3, 42), 42);
    return 0;
}

int test_engine_math_lookup_threshold_query_at_exact_key_walks_past(void) {
    /* query == key is NOT a match (test is `query < key`, strict). */
    int table[] = { 10, 100, 20, 200 };
    T_ASSERT_EQ_I(engine_math_lookup_threshold(10, table, 2, 0), 200);
    return 0;
}

int test_engine_math_lookup_threshold_empty_table_returns_fallback(void) {
    T_ASSERT_EQ_I(engine_math_lookup_threshold(5, NULL, 0, 77), 77);
    return 0;
}
