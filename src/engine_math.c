/*
 * Small math leaf helpers — pure-C port of the FUN_00404d6b cluster.
 * See engine_math.h for the API doc and retail address map.
 */

#include "engine_math.h"

int engine_math_percent_of(int a, int b)
{
    return (a * b) / 100;
}

int engine_math_bp_of(int a, int b)
{
    return (a * b) / 10000;
}

int engine_math_percent_div(int a, int b)
{
    if (b == 0) return 0;
    return (a * 100) / b;
}

int engine_math_div_ceil(int a, int b)
{
    if (b == 0) return 0;
    int q = a / b;
    if (a % b != 0) q += 1;
    return q;
}

int engine_math_lookup_threshold(int query, const int *table,
                                 int n, int fallback)
{
    /* Retail walks `param_3` pairs (the third arg, here `n`).  Each
     * iter reads `iVar2 = *param_2` (the key) and `piVar1 =
     * param_2 + 1` (the value pointer), then advances param_2 by
     * +2 (one pair).  Loop exits when `param_1 < iVar2` (query is
     * strictly less than this key), returning `*piVar1`.  If the
     * loop completes all n iterations, returns the fallback. */
    for (int i = 0; i < n; i++) {
        int key = table[i * 2 + 0];
        int val = table[i * 2 + 1];
        if (query < key) {
            return val;
        }
    }
    return fallback;
}
