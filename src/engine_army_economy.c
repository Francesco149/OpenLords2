/*
 * OpenLords2 — per-army stockpile valuation + budget→units helpers.
 *
 * Four tiny pure-arithmetic functions from the AI economy.  They put a gold
 * value on an army/realm's commodity stockpiles and convert a gold budget back
 * into purchasable unit counts:
 *
 *   FUN_0044e5bb → engine_army_economy_stock_a_value      (value of stock A)
 *   FUN_0044e7b4 → engine_army_economy_stockpile_value    (value of A+B+C)
 *   FUN_0044e603 → engine_army_economy_units_b            (gold → B units)
 *   FUN_0044e653 → engine_army_economy_units_c            (gold → C units)
 *
 * All four are deps of the per-army provisioning decision 0x44df5f, which
 * the army/country setup pass 0x49bd99 reaches; the valuations are also called
 * directly by the AI economy passes 0x4ace5e (build/upgrade decision vs
 * treasury) and 0x4a0aaa.
 *
 * The three stock fields live in the per-army record row (retail
 * DAT_0053f9b0, 0x300-byte stride):
 *   +0x250  stock A  (DAT_0053fc00)   priced at price_a
 *   +0x184  stock B  (DAT_0053fb34)   priced at price_b
 *   +0x180  stock C  (DAT_0053fb30)   priced at price_c
 *
 * The unit prices are retail config scalars (DAT_00553f60 / DAT_00567594 /
 * DAT_0057cb30) the defaults init 0x4983b7 sets to 5 / 10 / 6.  They live
 * outside the save schema, so this module owns them: module statics seeded
 * with those retail defaults, overridable via engine_army_economy_set_prices
 * for the eventual config-init port.
 */

#include "engine_army_economy.h"

#include <stddef.h>   /* NULL */
#include <stdint.h>

#include "engine_math.h"           /* div_ceil / percent_of (FUN_00404e03/d6b) */
#include "save_schema_globals.h"   /* s_slot_0053f25c / s_slot_0053f260 */

/* Per-army record row geometry (shared with engine_territory). */
#define ARMY_ROW_STRIDE   0x300
#define ARMY_STOCK_A      0x250   /* DAT_0053fc00 */
#define ARMY_STOCK_B      0x184   /* DAT_0053fb34 */
#define ARMY_STOCK_C      0x180   /* DAT_0053fb30 */
#define ARMY_PROD_RATE    0x201   /* DAT_0053fbb1 — census TYPE-0x02..0x0e count */
#define ARMY_PROD_FALLBACK 0x1a7  /* DAT_0053fb57 — fallback latch (engine_territory's retype flag) */

/* Extra fields the provisioning decision FUN_0044df5f reads/writes. */
#define ARMY_DISPLAY_IDX     0x11   /* DAT_0053f9c1 — derived byte = level*3 - 8 */
#define ARMY_FEED_COUNT      0x24   /* DAT_0053f9d4 — mouths to feed (div_ceil dividend) */
#define ARMY_PROVISION_SCALED 0xc4  /* DAT_0053fa74 — production limit_scaled source */
#define ARMY_SUPPLY_LEVEL    0x15d  /* DAT_0053fb0d — current ration/supply level */
#define ARMY_SUPPLY_LEVEL_PREV 0x15e /* DAT_0053fb0e — prior level (this turn's init source) */
#define ARMY_BC_SPLIT_PCT    0x15f  /* DAT_0053fb0f — % of net cost spent on commodity B */
#define ARMY_VAL_A           0x16c  /* DAT_0053fb1c — gold value of stock A */
#define ARMY_VAL_C           0x170  /* DAT_0053fb20 — gold value of bought C */
#define ARMY_VAL_B           0x174  /* DAT_0053fb24 — gold value of bought B */
#define ARMY_BUY_C           0x178  /* DAT_0053fb28 — commodity C units to buy (clamped) */
#define ARMY_BUY_B           0x17c  /* DAT_0053fb2c — commodity B units to buy (clamped) */
#define ARMY_PROD_STORED     0x188  /* DAT_0053fb38 — home production output stored */
#define ARMY_OVERHEAD_HI     0x198  /* DAT_0053fb48 — overhead term 1 */
#define ARMY_OVERHEAD_LO     0x19c  /* DAT_0053fb4c — overhead term 2 */
#define ARMY_PROVISION_RAW   0x224  /* DAT_0053fbd4 — production limit_raw / C-need base */

/* Fields the production-level planner FUN_0044d374 (+ its evaluators) read/write. */
#define ARMY_PLAN_GATE       0xb8   /* DAT_0053fa68 — byte: army plans production only if set */
#define ARMY_PLAN_REF_LEVEL  0xc4   /* DAT_0053fa74 — stored reference level (== ARMY_PROVISION_SCALED) */
#define ARMY_PLAN_BEST       0xc8   /* DAT_0053fa78 — best candidate level index (-1 if none) */
#define ARMY_PLAN_BEST2      0xcc   /* DAT_0053fa7c — best level index, 0-seeded variant */
#define ARMY_PROD_RATE_DENOM 0x202  /* DAT_0053fbb2 — capacity-ratio denominator (vs +0x201) */
#define ARMY_YIELD_BONUS     0x208  /* DAT_0053fbb8 — yield-bonus percentage (applied halved) */
#define ARMY_PLAN_NET        0x22c  /* DAT_0053fbdc — net-balance result (signed by phase) */
#define ARMY_PLAN_OUT        0x230  /* DAT_0053fbe0 — production_output at the reference level */
#define ARMY_PLAN_CAP        0x244  /* DAT_0053fbf4 — resource cap fed to the yield evaluators */
#define ARMY_PLAN_OUT_P4     0x248  /* DAT_0053fbf8 — phase-4 yield at the reference level */
#define ARMY_PLAN_OUT_STD    0x2fc  /* DAT_0053fcac — phase-2/3 yield at the reference level */

static uint8_t *g_army_records;

/* Retail 0x4983b7 defaults: A=5 (DAT_00553f60), B=10 (DAT_00567594),
 * C=6 (DAT_0057cb30). */
static int g_price_a = 5;
static int g_price_b = 10;
static int g_price_c = 6;

/* Retail 0x4983b7 production-solver tuning: scale=12 (DAT_0057c8e0),
 * max_level=10 (DAT_00552ffc), divisor_lo=2 (DAT_0057d34c),
 * divisor_hi=5 (DAT_005533bc). */
static int g_prod_scale      = 12;
static int g_prod_max_level  = 10;
static int g_prod_divisor_lo = 2;
static int g_prod_divisor_hi = 5;

/* Retail 0x4983b7 yield-evaluator divisors (mode-flag-set path):
 * divisor_std=10 (DAT_00553538), divisor_phase4=3 (DAT_00553218). */
static int g_yield_divisor_std    = 10;
static int g_yield_divisor_phase4 = 3;

void engine_army_economy_bind_army_records(uint8_t *records)
{
    g_army_records = records;
}

void engine_army_economy_set_prices(int price_a, int price_b, int price_c)
{
    g_price_a = price_a;
    g_price_b = price_b;
    g_price_c = price_c;
}

static int stock(int key, int off)
{
    return *(const int32_t *)(g_army_records + key * ARMY_ROW_STRIDE + off);
}

/* FUN_0044e5bb */
int engine_army_economy_stock_a_value(int key)
{
    if (g_army_records == NULL) return 0;

    int a = stock(key, ARMY_STOCK_A);
    return a > 0 ? a * g_price_a : 0;
}

/* FUN_0044e7b4 */
int engine_army_economy_stockpile_value(int key)
{
    if (g_army_records == NULL) return 0;

    int value = 0;
    int a = stock(key, ARMY_STOCK_A);
    if (a > 0) value  = a * g_price_a;
    int b = stock(key, ARMY_STOCK_B);
    if (b > 0) value += b * g_price_b;
    int c = stock(key, ARMY_STOCK_C);
    if (c > 0) value += c * g_price_c;
    return value;
}

/* Ceil-divide a positive gold budget into unit count at `price`; retail checks
 * cost < 1 first so the divisor is only hit for cost >= 1 (price always > 0
 * under the shipped config). */
static int units_for_cost(int cost, int price)
{
    if (cost < 1) return 0;

    int units = cost / price;
    if (cost % price != 0) units++;
    return units;
}

/* FUN_0044e603 — `key` ignored (retail takes it but never reads it). */
int engine_army_economy_units_b(int key, int cost)
{
    (void)key;
    return units_for_cost(cost, g_price_b);
}

/* FUN_0044e653 — `key` ignored. */
int engine_army_economy_units_c(int key, int cost)
{
    (void)key;
    return units_for_cost(cost, g_price_c);
}

void engine_army_economy_set_production_tuning(int scale, int max_level,
                                               int divisor_lo, int divisor_hi)
{
    g_prod_scale      = scale;
    g_prod_max_level  = max_level;
    g_prod_divisor_lo = divisor_lo;
    g_prod_divisor_hi = divisor_hi;
}

/* FUN_0044cfe1 */
int engine_army_economy_production_output(int key, int limit_scaled,
                                          int limit_raw)
{
    /* Retail bails before touching the record when either cap is non-positive. */
    if (limit_raw < 1 || limit_scaled < 1) return 0;
    if (g_army_records == NULL)            return 0;

    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
    int divisor = (s_slot_0053f25c == 0) ? g_prod_divisor_lo : g_prod_divisor_hi;
    unsigned rate = rowbase[ARMY_PROD_RATE];        /* read as unsigned byte */

    rowbase[ARMY_PROD_FALLBACK] = 0;

    /* Scan levels max_level..1 for the highest whose rate-scaled output fits
     * both caps.  `count` bounds the scan to max_level steps in lock-step with
     * the descending level, exactly mirroring the retail two-cursor loop. */
    int output = 0;
    int level  = g_prod_max_level;
    for (int count = 0;
         count < g_prod_max_level &&
             (output = (int)(rate * (unsigned)level),
              limit_raw < output ||
              limit_scaled < (g_prod_scale * output) / divisor);
         level--) {
        count++;
    }

    if (level < 1) {
        /* Rate-scaled output overflowed even at level 1: fall back to the
         * highest flat level (output == level) that fits both caps. */
        rowbase[ARMY_PROD_FALLBACK] = 1;
        level = g_prod_max_level;
        for (int count = 0; count < g_prod_max_level; count++) {
            output = level;
            if (level <= limit_raw &&
                (g_prod_scale * level) / divisor <= limit_scaled)
                break;
            level--;
        }
        if (level < 1) {
            rowbase[ARMY_PROD_FALLBACK] = 0;
            output = 0;
        }
    }

    return output;
}

/* --------------------------------------------------------------------- *
 *  Per-army provisioning decision — port of FUN_0044df5f (1628 B).
 *
 *  Reached from the army/country setup pass 0x49bd99 (and ~25 other call
 *  sites, almost always paired with 0x4485a5).  Given an army's mouths-
 *  to-feed count and a target supply order, it (1) picks a ration "supply
 *  level" the army can afford out of its stockpile value, then (2) splits the
 *  remaining gold cost between commodities B and C and clamps each buy order
 *  to what the army can hold.  When `phase`==4 the army's home production
 *  (engine_army_economy_production_output) is folded in first, reducing the
 *  commodity-C order one-for-one.  `phase` is the saved game-mode code
 *  (retail s_slot_0057c934); only the value 4 changes behavior.
 *
 *  The ration cost for a level L is `mult(L) * ceil(feed / divisor(L))`, from
 *  the 7-entry table below, plus a fixed overhead (two record fields summed)
 *  when the saved flag s_slot_0053f260 == 1.  The solver ratchets the level
 *  down (starting one above last turn's) until the cost fits, twice: first
 *  against the raw stockpile value, then against the gold value of the buy
 *  orders the first pass produced.
 * --------------------------------------------------------------------- */

/* Retail's supply-cost table (DAT_004d6738), pairs (divisor, mult) indexed by
 * level.  The real ladder is levels 0..5; the level clamp of 6 (below) lets the
 * index reach 6, which in the binary over-reads one entry into the adjacent
 * reciprocal-percentage table (100,50,33,...).  We reproduce that exact 7th
 * pair so the clamped range [0,6] matches retail's memory reads bit-for-bit. */
static const struct { int divisor, mult; } k_supply_table[7] = {
    { 1, 0 }, { 4, 1 }, { 2, 1 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 100, 50 },
};

/* Ration cost at `level`, plus the optional flat overhead. */
static int supply_cost(int feed, int level, int overhead)
{
    int cost = k_supply_table[level].mult
             * engine_math_div_ceil(feed, k_supply_table[level].divisor);
    if (s_slot_0053f260 == 1) cost += overhead;
    return cost;
}

static void clamp_supply_level(int8_t *level)
{
    if (*level > 6) *level = 6;
    if (*level < 0) *level = 0;
}

/* Recompute the commodity-B and -C buy orders for the settled `level`.  The net
 * gold cost is the ration cost minus what stock A already covers (floored at 0);
 * `split` percent of it buys B, the rest buys C, and each unit count is capped
 * by the army's holding limit (the STOCK_B / STOCK_C fields, set up by the
 * caller below). */
static void apply_buy_orders(uint8_t *row, int key, int level, int feed,
                             int overhead, int split)
{
    int net = supply_cost(feed, level, overhead)
            - engine_army_economy_stock_a_value(key);
    if (net < 1) net = 0;

    int b_gold  = engine_math_percent_of(net, split);
    int b_units = engine_army_economy_units_b(key, b_gold);
    int c_units = engine_army_economy_units_c(key, net - b_gold);

    int cap_b = *(int32_t *)(row + ARMY_STOCK_B);
    int cap_c = *(int32_t *)(row + ARMY_STOCK_C);
    *(int32_t *)(row + ARMY_BUY_B) = (cap_b < b_units) ? cap_b : b_units;
    *(int32_t *)(row + ARMY_BUY_C) = (cap_c < c_units) ? cap_c : c_units;
}

/* FUN_0044df5f */
void engine_army_economy_provision(int key, int phase)
{
    /* Retail dereferences the record block unconditionally; we mirror the
     * module's other entry points and no-op when it is unbound. */
    if (g_army_records == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    int8_t  *level = (int8_t *)(row + ARMY_SUPPLY_LEVEL);

    /* Mode 4 folds in this army's home production. */
    int prod = 0;
    if (phase == 4)
        prod = engine_army_economy_production_output(
                   key,
                   *(int32_t *)(row + ARMY_PROVISION_SCALED),
                   *(int32_t *)(row + ARMY_PROVISION_RAW));

    /* Commodity-C need = raw order minus home production; commodity-B cap =
     * a copy of stock A.  Both double as the holding caps in apply_buy_orders. */
    *(int32_t *)(row + ARMY_STOCK_C) =
        *(int32_t *)(row + ARMY_PROVISION_RAW) - prod;
    *(int32_t *)(row + ARMY_STOCK_B) = *(int32_t *)(row + ARMY_STOCK_A);

    int overhead = *(int32_t *)(row + ARMY_OVERHEAD_LO)
                 + *(int32_t *)(row + ARMY_OVERHEAD_HI);
    int feed     = *(int32_t *)(row + ARMY_FEED_COUNT);
    int split    = *(int8_t  *)(row + ARMY_BC_SPLIT_PCT);

    /* Pass 1: ration the supply level down (from one above last turn's) until
     * its cost fits the army's whole stockpile value.  The decrement runs at
     * the head of each step. */
    int budget = engine_army_economy_stockpile_value(key);
    *level = (int8_t)(*(int8_t *)(row + ARMY_SUPPLY_LEVEL_PREV) + 1);
    for (int i = 0; i < 6; i++) {
        if (*level > 0) (*level)--;
        if (supply_cost(feed, *level, overhead) <= budget) break;
    }
    clamp_supply_level(level);
    apply_buy_orders(row, key, *level, feed, overhead, split);

    /* Pass 2: the real budget is the gold the pass-1 orders actually cost
     * (plus stock A).  Ratchet again, this time decrementing at the tail. */
    int budget2 = *(int32_t *)(row + ARMY_BUY_B) * g_price_b
                + *(int32_t *)(row + ARMY_BUY_C) * g_price_c
                + engine_army_economy_stock_a_value(key);
    for (int i = 0; i < 6; i++) {
        if (supply_cost(feed, *level, overhead) <= budget2) break;
        if (*level > 0) (*level)--;
    }
    clamp_supply_level(level);
    apply_buy_orders(row, key, *level, feed, overhead, split);

    /* Stash the production output and the gold valuations of the final
     * holdings/orders, then the derived display index. */
    *(int32_t *)(row + ARMY_PROD_STORED) = prod;
    *(int32_t *)(row + ARMY_VAL_A) = *(int32_t *)(row + ARMY_STOCK_A) * g_price_a;
    *(int32_t *)(row + ARMY_VAL_C) = *(int32_t *)(row + ARMY_BUY_C)   * g_price_c;
    *(int32_t *)(row + ARMY_VAL_B) = *(int32_t *)(row + ARMY_BUY_B)   * g_price_b;
    *(int8_t  *)(row + ARMY_DISPLAY_IDX) = (int8_t)(*level * 3 - 8);
}

/* --------------------------------------------------------------------- *
 *  Per-army production-level planner — the 0x44d374 branch of 0x4485a5.
 *
 *  Three per-level output evaluators (production_output above, plus the two
 *  yield variants below) feed one selector.  Each evaluator answers "how much
 *  output can this army achieve at intensity level L, under a cap"; the
 *  selector sweeps L and records the best.
 * --------------------------------------------------------------------- */

void engine_army_economy_set_yield_divisors(int divisor_std, int divisor_phase4)
{
    g_yield_divisor_std    = divisor_std;
    g_yield_divisor_phase4 = divisor_phase4;
}

/* FUN_0044d281.  Scale a candidate value down to the army's resource-cell
 * capacity ratio: when the +0x201 numerator is below the +0x202 denominator,
 * multiply by (num/den) as a percentage; otherwise leave it.  Both fields are
 * unsigned bytes, so num < den implies den > 0 (percent_div's /0 guard is never
 * reached on this path). */
static int prod_capacity_clamp(int key, int value)
{
    const uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    unsigned num = row[ARMY_PROD_RATE];
    unsigned den = row[ARMY_PROD_RATE_DENOM];

    if (num < den) {
        int pct = engine_math_percent_div((int)num, (int)den);
        value = engine_math_percent_of(value, pct);
    }
    return value;
}

/* FUN_0044d303.  Scale a value up by the army's +0x208 yield-bonus percentage,
 * halved: returns value + value*(bonus/2)/100.  (Retail computes the bonus
 * through a `field < 1 ? field : field` branch whose two arms are identical —
 * a decompiler-flattened no-op clamp; we read the field directly.) */
static int prod_yield_bonus(int key, int value)
{
    int bonus = *(const int32_t *)(g_army_records + key * ARMY_ROW_STRIDE + ARMY_YIELD_BONUS);
    bonus /= 2;
    return engine_math_percent_of(value, bonus) + value;
}

/* FUN_0044d15a */
int engine_army_economy_yield_standard(int key, int level, int cap)
{
    if (g_army_records == NULL) return 0;

    int divisor = (s_slot_0053f25c == 0) ? g_prod_divisor_lo : g_yield_divisor_std;
    int capped  = prod_capacity_clamp(key, cap);

    int out = level * divisor;
    if (capped < out) out = capped;       /* strict < (cf. yield_phase4's <=) */
    out = prod_yield_bonus(key, out);
    if (out < 0) out = 0;
    return out;
}

/* FUN_0044d1e5 */
int engine_army_economy_yield_phase4(int key, int level, int cap)
{
    if (g_army_records == NULL) return 0;

    int divisor = (s_slot_0053f25c == 0) ? g_prod_divisor_lo : g_yield_divisor_phase4;
    int capped  = prod_capacity_clamp(key, cap);
    if (capped < 0) capped = 0;
    if (s_slot_0053f25c != 0) level /= 2;

    int out = level * divisor;
    if (capped <= out) out = capped;      /* <= (cf. yield_standard's <) */
    return out;
}

/* FUN_0044d374 */
void engine_army_economy_plan_production(int key, int phase)
{
    /* Retail dereferences the record block unconditionally; mirror the module's
     * no-op-when-unbound convention. */
    if (g_army_records == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    *(int32_t *)(row + ARMY_PLAN_NET) = 0;       /* cleared even for an ungated army */
    int count = *(int32_t *)(row + ARMY_FEED_COUNT);

    if (row[ARMY_PLAN_GATE] == 0) return;

    int buy_c = *(int32_t *)(row + ARMY_BUY_C);

    /* Sweep candidate intensity levels 0..count-1 for the one giving the most
     * output under the phase-selected evaluator.  best/best2 differ only when no
     * level beats the initial threshold (best stays -1, best2 stays 0). */
    int best_out = 1;
    int best = -1, best2 = 0;
    for (int level = 0; level < count; level++) {
        int out;
        if (phase == 1)
            out = engine_army_economy_production_output(
                      key, level, *(int32_t *)(row + ARMY_PROVISION_RAW) - buy_c);
        else if (phase == 4)
            out = engine_army_economy_yield_phase4(key, level, *(int32_t *)(row + ARMY_PLAN_CAP));
        else
            out = engine_army_economy_yield_standard(key, level, *(int32_t *)(row + ARMY_PLAN_CAP));

        if (best_out < out) { best2 = level; best = level; best_out = out; }
    }

    /* Evaluate the output at the army's stored reference level and stash it. */
    int ref = *(int32_t *)(row + ARMY_PLAN_REF_LEVEL);
    *(int32_t *)(row + ARMY_PLAN_OUT) =
        engine_army_economy_production_output(key, ref, *(int32_t *)(row + ARMY_PROVISION_RAW));
    if (phase == 4)
        *(int32_t *)(row + ARMY_PLAN_OUT_P4) =
            engine_army_economy_yield_phase4(key, ref, *(int32_t *)(row + ARMY_PLAN_CAP));
    if (phase == 2 || phase == 3)
        *(int32_t *)(row + ARMY_PLAN_OUT_STD) =
            engine_army_economy_yield_standard(key, ref, *(int32_t *)(row + ARMY_PLAN_CAP));

    /* Net balance: phase 1 nets the production output against the C-buy order,
     * phase 4 the phase-4 yield against it, every other phase just -buy_c. */
    if (phase == 1)
        *(int32_t *)(row + ARMY_PLAN_NET) = -*(int32_t *)(row + ARMY_PLAN_OUT) - buy_c;
    else if (phase == 4)
        *(int32_t *)(row + ARMY_PLAN_NET) = *(int32_t *)(row + ARMY_PLAN_OUT_P4) - buy_c;
    else
        *(int32_t *)(row + ARMY_PLAN_NET) = -buy_c;

    *(int32_t *)(row + ARMY_PLAN_BEST)  = best;
    *(int32_t *)(row + ARMY_PLAN_BEST2) = best2;
}

/* --------------------------------------------------------------------- *
 *  Secondary-settlement reserve pass — the 0x450e46 branch of 0x4485a5.
 *
 *  Gated on the realm's secondary-settlement flag (+0x1c3, the same byte
 *  engine_territory_stamp_secondary_keep reads).  Works the secondary
 *  settlement's three (value, capacity) reserve pairs:
 *    pair 0  value +0x1cc / capacity +0x1d8
 *    pair 1  value +0x1d0 / capacity +0x1dc
 *    pair 2  value +0x1d4 / capacity +0x1e0
 *  Field semantics (which commodity each pair is) are provisional.
 * --------------------------------------------------------------------- */

#define ARMY_SEC_FLAG      0x1c3   /* DAT_0053fb73 — realm has a secondary settlement */
#define ARMY_SEC_VAL0      0x1cc   /* DAT_0053fb7c — reserve pair 0 value */
#define ARMY_SEC_VAL1      0x1d0   /* DAT_0053fb80 — reserve pair 1 value */
#define ARMY_SEC_VAL2      0x1d4   /* DAT_0053fb84 — reserve pair 2 value */
#define ARMY_SEC_CAP0      0x1d8   /* DAT_0053fb88 — reserve pair 0 capacity */
#define ARMY_SEC_CAP1      0x1dc   /* DAT_0053fb8c — reserve pair 1 capacity */
#define ARMY_SEC_CAP2      0x1e0   /* DAT_0053fb90 — reserve pair 2 capacity */
#define ARMY_SEC_RATE      0xe8    /* DAT_0053fa98 — per-turn drain rate (turns divisor) */
#define ARMY_SEC_SEL       0xec    /* DAT_0053fa9c — selection sentinel, always -1 here */
#define ARMY_SEC_SURPLUS   0xf0    /* DAT_0053faa0 — pair-0 value when full, else 0 */
#define ARMY_SEC_TURNS     0x1a6   /* DAT_0053fb56 — ceil(pair0 / rate), or 100 */

/* FUN_00450fb4 */
int engine_army_economy_secondary_headroom(int key)
{
    if (g_army_records == NULL) return 0;

    const uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    if (row[ARMY_SEC_FLAG] == 0) return 0;

    /* Minimum remaining-capacity percentage across pairs 1 and 2.  Retail
     * computes pair-2's fill first, then pair-1's headroom, then keeps the
     * smaller of the two headrooms. */
    int fill2 = engine_math_percent_div(*(const int32_t *)(row + ARMY_SEC_VAL2),
                                        *(const int32_t *)(row + ARMY_SEC_CAP2));
    int head  = 100 - engine_math_percent_div(*(const int32_t *)(row + ARMY_SEC_VAL1),
                                              *(const int32_t *)(row + ARMY_SEC_CAP1));
    if (100 - fill2 < head) head = 100 - fill2;
    return head;
}

/* FUN_00450e46 */
void engine_army_economy_plan_secondary(int key)
{
    if (g_army_records == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    *(int32_t *)(row + ARMY_SEC_SEL)     = -1;
    *(int32_t *)(row + ARMY_SEC_SURPLUS) = 0;
    if (row[ARMY_SEC_FLAG] == 0) return;

    /* Retail recomputes pair-0's fill here and discards it — a dead
     * percent_div whose result is never stored or branched on (see
     * engine-quirks).  percent_div is side-effect-free, so we drop it. */

    int head = engine_army_economy_secondary_headroom(key);
    int surplus = (head < 100) ? 0 : *(int32_t *)(row + ARMY_SEC_VAL0);

    *(int32_t *)(row + ARMY_SEC_SEL)     = -1;     /* second write, faithful to retail */
    *(int32_t *)(row + ARMY_SEC_SURPLUS) = surplus;

    int rate = *(int32_t *)(row + ARMY_SEC_RATE);
    if (head < 100 || rate < 1) {
        row[ARMY_SEC_TURNS] = 100;
    } else {
        int val0 = *(int32_t *)(row + ARMY_SEC_VAL0);
        int turns = val0 / rate;
        if (val0 % rate != 0) turns++;
        row[ARMY_SEC_TURNS] = (uint8_t)(int8_t)turns;   /* retail narrows to signed char */
    }
}
