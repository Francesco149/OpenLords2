/*
 * OpenLords2 — small math leaf helpers.
 *
 * A handful of single-purpose math functions in the 0x404xxx range
 * that show up in many call sites and behave consistently across
 * them.  All pure C, no globals read or written, no callees.
 *
 *   FUN_00404d6b @ 0x404d6b (43 B) → engine_math_percent_of
 *   FUN_00404d96 @ 0x404d96 (43 B) → engine_math_bp_of
 *   FUN_00404dc1 @ 0x404dc1 (66 B) → engine_math_percent_div
 *   FUN_00404e03 @ 0x404e03 (72 B) → engine_math_div_ceil
 *   FUN_00404e4b @ 0x404e4b (98 B) → engine_math_lookup_threshold
 *
 * (FUN_00404ead — Manhattan distance — shares its dx/dy globals with
 *  the Chebyshev distance helper in engine_path; it lives there as
 *  engine_path_manhattan_distance to keep the side-effect storage
 *  in one place.)
 */

#ifndef OPENLORDS2_ENGINE_MATH_H
#define OPENLORDS2_ENGINE_MATH_H

/* Port of FUN_00404d6b.  Returns (a * b) / 100 — "what's b percent
 * of a", or vice-versa.  Plain integer arithmetic; no special-case
 * for b == 0 (caller's responsibility).  Truncates toward zero. */
int engine_math_percent_of(int a, int b);

/* Port of FUN_00404d96.  Returns (a * b) / 10000 — "b basis points
 * of a".  Same shape as percent_of with a larger denominator.  Used
 * by the trade/economy code paths where percentages are stored in
 * 0..10000 to give two extra decimals of resolution. */
int engine_math_bp_of(int a, int b);

/* Port of FUN_00404dc1.  Returns (a * 100) / b — "express a as a
 * percent of b".  Guards against b == 0 by returning 0 (rather than
 * /0 trapping).  Truncates toward zero. */
int engine_math_percent_div(int a, int b);

/* Port of FUN_00404e03.  Returns ceil(a / b) (integer ceiling
 * division).  Guards against b == 0 by returning 0.
 *
 * Implementation note: retail uses `a / b + (a % b != 0 ? 1 : 0)`
 * rather than the more idiomatic `(a + b - 1) / b` — semantically
 * equivalent for positive a, b but differs on negatives.  We mirror
 * the retail form (preserving the negative-input behavior even
 * though no retail caller exercises it). */
int engine_math_div_ceil(int a, int b);

/* Port of FUN_00404e4b.  Walks a flat (key, value) i32 pair array
 * (length n pairs), and returns the first pair's `value` where the
 * pair's `key` is strictly greater than `query`.  If no pair has a
 * key > query, returns `fallback`.
 *
 * Layout: `table` is treated as `int[n * 2]` with keys at even
 * indices and values at odd indices (key0, val0, key1, val1, ...).
 *
 * Used by the engine's tiered cost / threshold lookup tables (e.g.
 * "if year < 1100 return 5; if year < 1200 return 10; ...; else
 * return 50").  Retail uses this for several different lookup
 * tables; the same code services all of them. */
int engine_math_lookup_threshold(int query, const int *table,
                                 int n, int fallback);

#endif /* OPENLORDS2_ENGINE_MATH_H */
