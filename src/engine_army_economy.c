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
