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
#include "engine_ranking.h"        /* engine_ranking_recompute_player_total (4487a9) */
#include "engine_territory.h"      /* engine_territory_census_army_terrain (469b8d) */
#include "save_schema_globals.h"   /* s_slot_0053f25c / s_slot_0053f260 / s_slot_0056d5dc */

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

/* Fields the candidate selector FUN_0044dd4d (+ evaluator FUN_0044da99) use. */
#define ARMY_CENSUS_HIGH     0x200  /* DAT_0053fbb0 — census TYPE 0x0f..0x16 count (model gate) */
#define ARMY_EVAL_REF        0xd0   /* DAT_0053fa80 — reference level for the final re-eval */
#define ARMY_EVAL_SEL        0xd4   /* DAT_0053fa84 — selected level */
#define ARMY_EVAL_MAXLVL     0xd8   /* DAT_0053fa88 — max-net level */
#define ARMY_EVAL_NET        0x258  /* DAT_0053fc08 — final net (revenue − cost − buy_b) */
#define ARMY_EVAL_CATEGORY   0x25c  /* DAT_0053fc0c — category code (10/0x14/0x1e/else) */
#define ARMY_EVAL_REVENUE    0x268  /* DAT_0053fc18 — final revenue */
#define ARMY_EVAL_COST       0x26c  /* DAT_0053fc1c — final cost */

/* Fields the storage/supply distribution FUN_0044c278 (+ FUN_0044c53b/c5fc) use.
 * The fill levels live in the i16 AI score sub-view at +0x90 (DAT_0053fa40). */
#define ARMY_SCORE_TABLE     0x90   /* DAT_0053fa40 — i16[20] developed-cell fill 0..800 */
#define ARMY_DIST_BUDGET     0xdc   /* DAT_0053fa8c — supply budget to distribute */
#define ARMY_DIST_SEL        0xe0   /* DAT_0053fa90 — selection sentinel, always -1 here */
#define ARMY_DIST_HEADROOM   0xe4   /* DAT_0053fa94 — summed capped headroom */
#define ARMY_DIST_OVERFLOW   0x20c  /* DAT_0053fbbc — count of cells topped out at 800 */
#define ARMY_DIST_TOPIDX     0x210  /* DAT_0053fbc0 — fullest developed-cell slot index */
#define ARMY_DIST_TURNS      0x214  /* DAT_0053fbc4 — turns to fill the fullest cell */
#define ROSTER_ROW_I32       20     /* DAT_0053ea00 row stride (0x50 B / 4) */

static uint8_t *g_army_records;
static const int32_t *g_cell_roster;   /* DAT_0053ea00 — 17 rows × 20 i32 */
static uint8_t *g_cells;               /* DAT_00522f90 — cell descriptor grid (resource pass stamps it) */

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

/* The candidate-evaluator scratch pair (retail globals DAT_0055cd6c /
 * DAT_005656f0).  FUN_0044da99 writes them; FUN_0044dd4d reads them right back;
 * the still-unported FUN_0044d60d is the third sharer.  Module-scope statics —
 * not save state — so they faithfully model the retail global handoff. */
static int g_eval_cost;       /* DAT_0055cd6c — the candidate's upkeep/cost */
static int g_eval_revenue;    /* DAT_005656f0 — the candidate's output/revenue */

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

/* FUN_0044e6a3 (158 B).  The "appraisal" valuation the AI tax-rate sweep
 * (FUN_004a4782) weighs a realm's gold needs against: stock A (+0x250) valued at
 * BOTH price A and price B, plus the provision-raw field (+0x224) valued at
 * price C — each term added only when its field is > 0.  Distinct from
 * engine_army_economy_stockpile_value (which prices the +0x250/+0x184/+0x180
 * stockpiles).  0 when the army-record block is unbound. */
int engine_army_economy_appraisal_value(int key)
{
    if (g_army_records == NULL) return 0;

    int value = 0;
    int a = stock(key, ARMY_STOCK_A);
    if (a > 0) value  = a * g_price_a;
    if (a > 0) value += a * g_price_b;
    int p = stock(key, ARMY_PROVISION_RAW);
    if (p > 0) value += p * g_price_c;
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

int engine_army_economy_prod_scale(void)
{
    return g_prod_scale;
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
 *  Per-army seasonal production turn pass — FUN_0044c8ae.
 *
 *  The turn-advance driver 0x448440 runs this between the dev-spread pass and
 *  the army recount.  Per army it advances the running "provision" figure
 *  (+0x224): snapshot, draw down a per-turn consumption (+0x18c), apply a
 *  signed interest rate (+0x1fc %), then fold in this season's production via
 *  the same three yield evaluators the planner above uses, adjust by the
 *  model-type byte (+0x21b), classify the scaled result and retype the realm's
 *  feature cells to that rating, and plan next season.  Field semantics beyond
 *  the running figure are provisional (the names track what each slot holds in
 *  this pass).  Reuses g_prod_scale (DAT_0057c8e0). */

#define ARMY_PROVISION_RAW_PREV  0x228  /* DAT_0053fbd8 — prior-turn +0x224 snapshot */
#define ARMY_PROVISION_DRAW      0x18c  /* DAT_0053fb3c — per-turn consumption drawn off +0x224 */
#define ARMY_SEASON_SCRATCH      0x234  /* DAT_0053fbe4 — cleared each turn, unread here */
#define ARMY_PROVISION_INTEREST  0x278  /* DAT_0053fc28 — interest amount applied to +0x224 */
#define ARMY_PROVISION_RATE_PCT  0x1fc  /* DAT_0053fbac — signed interest rate %, per turn */
#define ARMY_SEASON_OUT          0x240  /* DAT_0053fbf0 — season-1 production output */
#define ARMY_SEASON_TYPE         0x21b  /* DAT_0053fbcb — model-type byte (0/1/2/4/5 cases) */
#define ARMY_SEASON_DELTA        0x24c  /* DAT_0053fbfc — adjusted output − unadjusted yield */
#define ARMY_RATE_CLASS_DENOM    0x206  /* DAT_0053fbb6 — denominator fed to rate_class */

/* FUN_0044cf6f */
int engine_army_economy_rate_class(int q, int denom)
{
    if (q < 1)            return 2;
    if (denom < 1)        return 2;
    if (q / denom < 0x29) return 3;
    if (q / denom < 0x51) return 7;
    return 0xb;
}

/* FUN_0044c8ae per-army step. */
void engine_army_economy_season_update(int key, int season, int next_season)
{
    if (g_army_records == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    /* Carry last turn's running figure, then draw down the per-turn consume. */
    *(int32_t *)(row + ARMY_PROVISION_RAW_PREV) = *(int32_t *)(row + ARMY_PROVISION_RAW);
    *(int32_t *)(row + ARMY_PROVISION_RAW) -= *(int32_t *)(row + ARMY_PROVISION_DRAW);
    *(int32_t *)(row + ARMY_PLAN_OUT_P4)        = 0;
    *(int32_t *)(row + ARMY_SEASON_SCRATCH)     = 0;
    *(int32_t *)(row + ARMY_PROVISION_INTEREST) = 0;

    /* Signed interest rate: negative draws further down, positive grows. */
    signed char rate = (signed char)row[ARMY_PROVISION_RATE_PCT];
    if (rate < 0) {
        int amt = engine_math_percent_of(*(int32_t *)(row + ARMY_PROVISION_RAW), -rate);
        *(int32_t *)(row + ARMY_PROVISION_INTEREST) = amt;
        *(int32_t *)(row + ARMY_PROVISION_RAW) -= amt;
    } else if (rate > 0) {
        int amt = engine_math_percent_of(*(int32_t *)(row + ARMY_PROVISION_RAW), rate);
        *(int32_t *)(row + ARMY_PROVISION_INTEREST) = amt;
        *(int32_t *)(row + ARMY_PROVISION_RAW) += amt;
    }
    if (*(int32_t *)(row + ARMY_PROVISION_RAW) < 0)
        *(int32_t *)(row + ARMY_PROVISION_RAW) = 0;

    int ref  = *(int32_t *)(row + ARMY_PLAN_REF_LEVEL);
    int type = row[ARMY_SEASON_TYPE];
    int rating = 0;

    if (season == 1) {
        int raw = engine_army_economy_production_output(
                      key, ref, *(int32_t *)(row + ARMY_PROVISION_RAW));
        *(int32_t *)(row + ARMY_SEASON_OUT) = raw;
        if (type == 5) *(int32_t *)(row + ARMY_SEASON_OUT) /= 4;
        if (type == 0) *(int32_t *)(row + ARMY_SEASON_OUT) /= 2;
        if (type == 4) *(int32_t *)(row + ARMY_SEASON_OUT) /= 2;
        *(int32_t *)(row + ARMY_SEASON_DELTA) = *(int32_t *)(row + ARMY_SEASON_OUT) - raw;
        *(int32_t *)(row + ARMY_PLAN_CAP) = *(int32_t *)(row + ARMY_SEASON_OUT) * g_prod_scale;
        if (row[ARMY_PROD_FALLBACK] == 0) {
            row[ARMY_PROD_RATE_DENOM]  = row[ARMY_PROD_RATE];
            row[ARMY_RATE_CLASS_DENOM] = row[ARMY_PROD_RATE];
        } else {
            row[ARMY_PROD_RATE_DENOM]  = 1;
            row[ARMY_RATE_CLASS_DENOM] = 1;
        }
        *(int32_t *)(row + ARMY_PROVISION_RAW) -= *(int32_t *)(row + ARMY_SEASON_OUT);
        rating = engine_army_economy_rate_class(
                     *(int32_t *)(row + ARMY_PLAN_CAP), row[ARMY_RATE_CLASS_DENOM]);
    } else if (season == 2 || season == 3) {
        int raw = engine_army_economy_yield_standard(
                      key, ref, *(int32_t *)(row + ARMY_PLAN_CAP));
        *(int32_t *)(row + ARMY_PLAN_CAP) = raw;
        if (type == 5) *(int32_t *)(row + ARMY_PLAN_CAP) /= 2;
        if (type == 1) *(int32_t *)(row + ARMY_PLAN_CAP) /= 2;
        if (type == 2) *(int32_t *)(row + ARMY_PLAN_CAP) = (*(int32_t *)(row + ARMY_PLAN_CAP) * 3) / 2;
        *(int32_t *)(row + ARMY_SEASON_DELTA) = *(int32_t *)(row + ARMY_PLAN_CAP) - raw;
        rating = engine_army_economy_rate_class(
                     *(int32_t *)(row + ARMY_PLAN_CAP), row[ARMY_RATE_CLASS_DENOM]);
    } else if (season == 4) {
        int raw = engine_army_economy_yield_phase4(
                      key, ref, *(int32_t *)(row + ARMY_PLAN_CAP));
        *(int32_t *)(row + ARMY_PLAN_OUT_P4) = raw;
        /* The model-type adjustments here scale the CAP (+0x244), not the
         * yield result — a faithful retail quirk. */
        int cap = *(int32_t *)(row + ARMY_PLAN_CAP);
        if (type == 5) *(int32_t *)(row + ARMY_PLAN_OUT_P4) = cap / 4;
        if (type == 4) *(int32_t *)(row + ARMY_PLAN_OUT_P4) = cap / 2;
        if (type == 0) *(int32_t *)(row + ARMY_PLAN_OUT_P4) = cap / 2;
        if (type == 2) *(int32_t *)(row + ARMY_PLAN_OUT_P4) = (cap * 3) / 2;
        *(int32_t *)(row + ARMY_SEASON_DELTA) = *(int32_t *)(row + ARMY_PLAN_OUT_P4) - raw;
        *(int32_t *)(row + ARMY_PROVISION_RAW) += *(int32_t *)(row + ARMY_PLAN_OUT_P4);
        rating = engine_army_economy_rate_class(
                     *(int32_t *)(row + ARMY_PLAN_OUT_P4), row[ARMY_RATE_CLASS_DENOM]);
    }

    /* Rating → a 0..3 tier byte, then retype this realm's [2,0xe] feature
     * cells, and plan next season's production. */
    char tier = (rating < 3) ? (char)0 : (char)((rating - 3) / 4 + 1);
    engine_territory_retype_army_band(key, rating, tier, 2, 0xe);
    engine_army_economy_plan_production(key, next_season);
}

/* FUN_0044c8ae loop wrapper — the diff-harness entry point. */
void engine_army_economy_apply_season_pass(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++)
        engine_army_economy_season_update(key, (int)s_slot_0057c934, (int)s_slot_0057c92c);
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

/* --------------------------------------------------------------------- *
 *  Candidate cost/revenue evaluator + selector — the 0x44dd4d branch of
 *  0x4485a5.
 *
 *  FUN_0044da99 values one candidate at a given budget/intensity, writing a
 *  (cost, revenue) pair into the shared scratch globals; FUN_0044dd4d sweeps
 *  the candidate intensity over the army's feed-count, reads the pair back each
 *  step, and selects a level by the sign of the net (revenue − cost).  The
 *  per-category constants (weights 1/3/5/7, revenue bases 1400/900/500/200) are
 *  a designer table; the precise commodity semantics are provisional.
 * --------------------------------------------------------------------- */

int engine_army_economy_eval_cost(void)    { return g_eval_cost; }
int engine_army_economy_eval_revenue(void) { return g_eval_revenue; }
void engine_army_economy_set_eval_scratch(int cost, int revenue) { g_eval_cost = cost; g_eval_revenue = revenue; }

/* FUN_0044da99 */
void engine_army_economy_eval_candidate(int key, int budget, int level,
                                        int category, int phase)
{
    g_eval_cost    = 0;
    g_eval_revenue = 0;
    if (g_army_records == NULL) return;   /* guard: the model path reads a row byte */
    if (budget == 0) return;

    const uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    /* No settlement-tier (census +0x200) cells → a trivial flat cost, no revenue. */
    if (row[ARMY_CENSUS_HIGH] == 0) {
        g_eval_cost = (budget < 6) ? budget : budget / 2;
        return;
    }

    /* `fill` is the candidate's level expressed as a percent of 3× the budget,
     * capped at 200. */
    int fill = engine_math_percent_div(level, budget * 3);
    if (fill > 199) fill = 200;

    int weight;                            /* per-category cost weight */
    if      (category == 10)   weight = 1;
    else if (category == 0x14) weight = 3;
    else if (category == 0x1e) weight = 5;
    else                       weight = 7;

    /* Below a full fill, a falling curve adds to the cost weight (max +33 at
     * fill 0); the C truncating division on the negative dividend is preserved. */
    int curve = (fill < 100) ? -((fill - 100) / 3) : 0;

    int base;                              /* per-category revenue base */
    if      (category == 10)   base = 0x578;   /* 1400 */
    else if (category == 0x14) base = 900;
    else if (category == 0x1e) base = 500;
    else                       base = 200;

    base = engine_math_percent_of(base, fill);
    if (fill > 99) {                       /* small-budget over-fill bonus */
        if      (budget < 5)  base += 10000;
        else if (budget < 10) base += 5000;
        else if (budget < 25) base += 2000;
    }

    g_eval_cost = engine_math_bp_of(budget * 100, weight + curve);
    if (phase == 4) g_eval_cost = (g_eval_cost * 3) / 2;

    g_eval_revenue = engine_math_bp_of(budget, base);
    if (phase == 1) g_eval_revenue = (g_eval_revenue * 3) / 2;

    /* Round a non-zero potential up to at least 1 unit; if there is no revenue
     * potential at all but a cost weight remains, charge a token 1 cost. */
    if (g_eval_revenue == 0) {
        if (base == 0) {
            if (g_eval_cost == 0 && (weight + curve) != 0) g_eval_cost = 1;
        } else {
            g_eval_revenue = 1;
        }
    }

    if (budget < g_eval_cost) g_eval_cost = budget;   /* cost never exceeds the budget */
}

/* FUN_0044dd4d */
void engine_army_economy_select_candidate(int key, int phase)
{
    if (g_army_records == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    *(int32_t *)(row + ARMY_EVAL_NET) = 0;          /* cleared even for an ungated army */
    int count = *(int32_t *)(row + ARMY_FEED_COUNT);

    if (row[ARMY_PLAN_GATE] == 0) return;

    int buy_b    = *(int32_t *)(row + ARMY_BUY_B);
    int budget   = *(int32_t *)(row + ARMY_STOCK_A) - buy_b;
    int category = *(int32_t *)(row + ARMY_EVAL_CATEGORY);

    int neg_level = -1, nonneg_level = -1, max_level = 999999;
    int best_neg = -1000000, first_nonneg = -100000, best_net = -1000000;

    for (int level = 0; level < count; level++) {
        engine_army_economy_eval_candidate(key, budget, level, category, phase);
        int net = g_eval_revenue - g_eval_cost;
        if (net < 0 && best_neg < net)              { neg_level = level; best_neg = net; }
        if (net >= 0 && first_nonneg == -100000)    { nonneg_level = level; first_nonneg = net; }
        if (best_net < net)                         { max_level = level; best_net = net; }
    }

    /* Prefer the first level that breaks even; otherwise the least-negative one. */
    int selected = (nonneg_level < 0) ? neg_level : nonneg_level;

    /* Re-evaluate at the army's stored reference level for the recorded figures. */
    engine_army_economy_eval_candidate(key, budget,
                                       *(int32_t *)(row + ARMY_EVAL_REF), category, phase);
    *(int32_t *)(row + ARMY_EVAL_REVENUE) = g_eval_revenue;
    *(int32_t *)(row + ARMY_EVAL_COST)    = g_eval_cost;
    *(int32_t *)(row + ARMY_EVAL_NET)     = (g_eval_revenue - g_eval_cost) - buy_b;
    *(int32_t *)(row + ARMY_EVAL_SEL)     = selected;
    *(int32_t *)(row + ARMY_EVAL_MAXLVL)  = max_level;
}

/* --------------------------------------------------------------------- *
 *  Per-turn stock-A settle (FUN_0044d60d) — the third sharer of the
 *  g_eval_cost / g_eval_revenue scratch pair (the turn-advance engine
 *  0x448440 runs it once per realm).  It draws this turn's consumption off
 *  stock A (+0x250), books the base production cost/revenue (eval_candidate),
 *  layers on the model-type rate and the per-realm trade rate, applies the net
 *  to the stockpile (floored at 0), records the ledger fields, regrades the
 *  realm's feature cells, and re-plans next turn's candidate (select_candidate).
 * --------------------------------------------------------------------- */

/* Per-model-type stock-A rate (retail DAT_004d6560[0..5], i32), indexed by the
 * army's model-type byte (+0x21b, ARMY_SEASON_TYPE — the enum runs 0/1/2/4/5,
 * skipping 3, which is why slot 3 is 0).  Two unrelated tables sit back-to-back
 * at 0x4d6560; only these six entries belong to this lookup.  The model-type
 * byte is always 0..5, so the read is always in range. */
static const int32_t k_stock_a_rate[6] = { -2, -10, 5, 0, -5, -10 };

#define ARMY_STOCK_A_PREV      0x254  /* DAT_0053fc04 — prior-turn +0x250 snapshot     */
#define ARMY_STOCK_A_DRAW      0x190  /* DAT_0053fb40 — consumption drawn off +0x250   */
#define ARMY_STOCK_A_SCRATCH   0x27c  /* DAT_0053fc2c — cleared each turn, unread here */
#define ARMY_STOCK_A_REVENUE   0x260  /* DAT_0053fc10 — this turn's gross revenue      */
#define ARMY_STOCK_A_BALANCE   0x264  /* DAT_0053fc14 — net-change ledger              */
#define ARMY_STOCK_A_RATE_AMT  0x270  /* DAT_0053fc20 — model-rate amount applied      */
#define ARMY_STOCK_A_TRADE_AMT 0x274  /* DAT_0053fc24 — trade-rate amount applied      */
#define ARMY_STOCK_A_TRADE_RATE 0x1fd /* DAT_0053fbad — signed trade rate %, 99=off    */

/* Port of FUN_0044d60d. */
void engine_army_economy_settle_stock_a(void)
{
    if (g_army_records == NULL) return;

    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *row    = g_army_records + key * ARMY_ROW_STRIDE;
        int32_t *stockA = (int32_t *)(row + ARMY_STOCK_A);

        *(int32_t *)(row + ARMY_STOCK_A_PREV)    = *stockA;     /* snapshot */
        *(int32_t *)(row + ARMY_STOCK_A_SCRATCH) = 0;
        *stockA -= *(int32_t *)(row + ARMY_STOCK_A_DRAW);       /* consume */
        *(int32_t *)(row + ARMY_STOCK_A_BALANCE) = 0;
        *(int32_t *)(row + ARMY_STOCK_A_REVENUE) =
            *(int32_t *)(row + ARMY_STOCK_A_BALANCE);           /* = 0 */

        /* Base production cost/revenue for the current stock level
         * (writes the g_eval_cost / g_eval_revenue scratch pair). */
        engine_army_economy_eval_candidate(key, *stockA,
                                           *(int32_t *)(row + ARMY_EVAL_REF),
                                           *(int32_t *)(row + ARMY_EVAL_CATEGORY),
                                           (int)s_slot_0057c934);

        int rate_amt = engine_math_percent_of(*stockA,
                            k_stock_a_rate[row[ARMY_SEASON_TYPE]]);

        *(int32_t *)(row + ARMY_STOCK_A_TRADE_AMT) = 0;
        int trade = (int8_t)row[ARMY_STOCK_A_TRADE_RATE];
        if (trade == 'c') {                       /* 99 → trade disabled */
            rate_amt = 0;
            g_eval_revenue = 0;
        } else if (trade < 0) {                    /* net outgoing */
            int v = engine_math_percent_of(*stockA, -trade);
            *(int32_t *)(row + ARMY_STOCK_A_TRADE_AMT) = v;
            g_eval_cost += v;
        } else if (trade > 0) {                    /* net incoming */
            int v = engine_math_percent_of(*stockA, trade);
            *(int32_t *)(row + ARMY_STOCK_A_TRADE_AMT) = v;
            g_eval_revenue += v;
        }

        if (rate_amt < 0) g_eval_cost    -= rate_amt;
        else              g_eval_revenue += rate_amt;
        *(int32_t *)(row + ARMY_STOCK_A_RATE_AMT) = rate_amt;

        *stockA += g_eval_revenue - g_eval_cost;
        *(int32_t *)(row + ARMY_STOCK_A_REVENUE) = g_eval_revenue;
        *(int32_t *)(row + ARMY_STOCK_A_BALANCE) =
            ((*(int32_t *)(row + ARMY_STOCK_A_PREV) - *stockA) + g_eval_revenue)
            - *(int32_t *)(row + ARMY_STOCK_A_DRAW);
        if (*stockA < 0) {                         /* stockpile can't go negative */
            *(int32_t *)(row + ARMY_STOCK_A_BALANCE) = *stockA + g_eval_revenue;
            *stockA = 0;
        }

        engine_territory_grade_feature_cells(key);             /* FUN_0044d913 */
        engine_army_economy_select_candidate(key, (int)s_slot_0057c92c);
    }
}

/* --------------------------------------------------------------------- *
 *  Storage/supply distribution — the 0x44c278 branch of 0x4485a5.
 *
 *  Spreads a realm's per-turn supply budget (+0xdc) across its developed
 *  feature-cells, working outward from the fullest.  A cell is "developed" iff
 *  its roster entry (DAT_0053ea00) is non-zero AND the cell-grid type byte it
 *  indexes (DAT_00522f90) is > 0x18; its current fill 0..800 is the i16 in the
 *  AI score sub-view at +0x90 (DAT_0053fa40).  The pass does not mutate the
 *  fills — it only derives the headroom sum, overflow count, and fill-time
 *  estimate the rest of the economy planner reads.  Commodity semantics of the
 *  budget/fill are provisional.
 * --------------------------------------------------------------------- */

void engine_army_economy_bind_cell_roster(const int32_t *roster)
{
    g_cell_roster = roster;
}

void engine_army_economy_bind_cells(uint8_t *cells)
{
    g_cells = cells;
}

/* A realm slot is a developed feature-cell iff its roster offset is non-zero and
 * the cell-grid type byte at that offset exceeds 0x18 (settlement-tier). */
static int dist_developed(int key, int slot)
{
    int32_t off = g_cell_roster[key * ROSTER_ROW_I32 + slot];
    return off != 0 && g_cells[off] > 0x18;
}

/* Current fill 0..800 of a slot, read from the per-army i16 score sub-view. */
static int dist_fill(const uint8_t *row, int slot)
{
    return *(const int16_t *)(row + ARMY_SCORE_TABLE + slot * 2);
}

/* FUN_0044c5fc — fullest developed slot, default 99 (the no-cell gate sentinel). */
void engine_army_economy_seed_fullest_cell(int key)
{
    if (g_army_records == NULL || g_cell_roster == NULL || g_cells == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    int best = -1, sel = 99;
    for (int slot = 0; slot < 20; slot++) {
        if (!dist_developed(key, slot)) continue;
        int fill = dist_fill(row, slot);
        if (best < fill) { best = fill; sel = slot; }
    }
    *(int32_t *)(row + ARMY_DIST_TOPIDX) = sel;
}

/* FUN_0044c53b — fullest developed slot, default 0 (written up front, then the
 * argmax overwrites it incrementally). */
void engine_army_economy_pick_fullest_cell(int key)
{
    if (g_army_records == NULL || g_cell_roster == NULL || g_cells == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    *(int32_t *)(row + ARMY_DIST_TOPIDX) = 0;
    int best = -1;
    for (int slot = 0; slot < 20; slot++) {
        if (!dist_developed(key, slot)) continue;
        int fill = dist_fill(row, slot);
        if (best < fill) { best = fill; *(int32_t *)(row + ARMY_DIST_TOPIDX) = slot; }
    }
}

/* FUN_0044c278 — the distribution pass.  Return value is discarded by the lone
 * caller (0x4485a5); ported faithfully regardless. */
int engine_army_economy_distribute_stock(int key)
{
    if (g_army_records == NULL || g_cell_roster == NULL || g_cells == NULL) return 0;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;

    engine_army_economy_seed_fullest_cell(key);

    /* Summed headroom across developed cells, each capped at 200/turn. */
    int headroom_sum = 0;
    for (int slot = 0; slot < 20; slot++) {
        if (!dist_developed(key, slot)) continue;
        int h = 800 - dist_fill(row, slot);
        if (h > 200) h = 200;
        headroom_sum += h;
    }

    *(int32_t *)(row + ARMY_DIST_SEL)      = -1;
    *(int32_t *)(row + ARMY_DIST_HEADROOM) = headroom_sum;
    *(int32_t *)(row + ARMY_DIST_OVERFLOW) = 0;
    *(int32_t *)(row + ARMY_DIST_TURNS)    = 0;

    int ret = key * 0x100;

    if (*(int32_t *)(row + ARMY_DIST_TOPIDX) < 99) {
        engine_army_economy_pick_fullest_cell(key);

        /* Greedily deposit the budget ≤200 at a time around the cell ring from
         * the fullest, counting how many cells top out at the 800 cap (their
         * overflow is carried back into the running budget).  Fills are NOT
         * written back — only the overflow count is produced. */
        int remaining = *(int32_t *)(row + ARMY_DIST_BUDGET);
        int idx = *(int32_t *)(row + ARMY_DIST_TOPIDX);
        for (int step = 0; step < 20; step++) {
            if (dist_developed(key, idx)) {
                int deposited;
                if (remaining < 0xc9) { deposited = dist_fill(row, idx) + remaining; remaining = 0; }
                else                  { deposited = dist_fill(row, idx) + 200; remaining -= 200; }
                if (deposited > 799) {
                    remaining += deposited - 800;
                    *(int32_t *)(row + ARMY_DIST_OVERFLOW) += 1;
                }
                if (remaining < 1) break;
            }
            idx++;
            if (idx > 0x13) idx = 0;
        }

        /* Turns to fill the fullest cell at ≤200/turn from the full budget. */
        remaining = *(int32_t *)(row + ARMY_DIST_BUDGET);
        int headroom_top = 800 - dist_fill(row, *(int32_t *)(row + ARMY_DIST_TOPIDX));
        ret = headroom_top;
        if (remaining > 0) {
            if (remaining > 200) remaining = 200;
            *(int32_t *)(row + ARMY_DIST_TURNS) = headroom_top / remaining;
            int rem = headroom_top % remaining;
            ret = headroom_top / remaining;
            if (rem != 0) {
                ret = key * 0x100;
                *(int32_t *)(row + ARMY_DIST_TURNS) += 1;
            }
        }
    }
    return ret;
}

/* --------------------------------------------------------------------- *
 *  Per-realm resource-production planner — the FUN_004485a5 orchestrator
 *  and its 0x44f318 branch (with leaves 0x44f15b / 0x44ef4e / 0x44f248).
 *
 *  A realm has four "resource slots" (0..3), a 4-entry array of 0x18-byte
 *  records at +0x294; plan_resource computes each slot's production quota and
 *  supply target.  The slot-2 path additionally rations the realm's share of a
 *  per-group demand pool, weighted by a baked per-race multiplier table.
 * --------------------------------------------------------------------- */

/* Resource-slot record array (retail DAT_0053fc44, 0x18-byte stride). */
#define ARMY_RES_BASE       0x294   /* slot 0 record base */
#define RES_STRIDE          0x18
#define RES_TARGET_PCT      0x00    /* DAT_0053fc44 — byte, plan_resource writes */
#define RES_FLAG_A          0x01    /* DAT_0053fc45 — byte gate */
#define RES_FLAG_B          0x02    /* DAT_0053fc46 — byte gate / depletion countdown */
#define RES_FLAG_C          0x03    /* DAT_0053fc47 — byte gate (top gate) */
#define RES_CELL_IDX        0x04    /* DAT_0053fc48 — i32, byte offset of the slot's overworld cell */
#define RES_BASE_PCT        0x08    /* DAT_0053fc4c — signed byte, supply_target base */
#define RES_CAP             0x0a    /* DAT_0053fc4e — i16, supply_target cap */
#define RES_CURRENT         0x0c    /* DAT_0053fc50 — i32, running supplied amount */
#define RES_PREV            0x10    /* DAT_0053fc54 — i32, previous-turn supplied amount */
#define RES_QUOTA           0x14    /* DAT_0053fc58 — i32, plan_resource OUTPUT */

/* Eight 0xc-byte job-slot records at +0xc4 (retail DAT_0053fa74 — engine_
 * territory's WF_COUNTER, filled by FUN_0044f6e7).  plan_resource indexes by
 * rec_idx; the +0x00 assigned-count is the production level fed to supply_target,
 * +0x08 (the cap field in the setup phase) is reused as the best-level scratch. */
#define ARMY_JOBSLOT_BASE   0xc4
#define JOBSLOT_STRIDE      0xc
#define JOBSLOT_COUNT       0x00    /* DAT_0053fa74 — assigned count / production level */
#define JOBSLOT_SEL         0x04    /* DAT_0053fa78 — plan_resource sets -1 */
#define JOBSLOT_CAP         0x08    /* DAT_0053fa7c — best level (slot 2) / 100000 sentinel */

#define ARMY_GROUP          0x05    /* DAT_0053f9b5 — realm group id (resource-pool index) */
#define ARMY_RACE           0x290   /* DAT_0053fc40 — race id (race-table index) */
#define ARMY_RES_PROD_A     0x280   /* DAT_0053fc30 — slot-2 race-weighted output A */
#define ARMY_RES_PROD_B     0x284   /* DAT_0053fc34 — slot-2 race-weighted output B */
#define ARMY_RES_CARRY0     0x288   /* DAT_0053fc38 — slot-0 secondary carry (from SEC_VAL2) */
#define ARMY_RES_CARRY3     0x28c   /* DAT_0053fc3c — slot-3 secondary carry (from SEC_VAL1) */
#define ARMY_SLOT2_JOBCOUNT 0x118   /* DAT_0053fac8 — job-slot record 7's count (== +0xc4 + 7*0xc) */
#define ARMY_RES_ACTIVE2    0x2c7   /* DAT_0053fc77 — resource-slot-2 active flag (== res(2).flag_c) */

/* Per-group resource-demand pool (retail DAT_0057c020, 0x160-byte stride).  Three
 * commodity pairs (flag-0 accumulator / flag-1 prior-turn snapshot): B (slot 1),
 * C (slot 3) and A (slot 0).  Slot 2 instead accumulates per-race into +0x40 and
 * debits A/B by the race weights. */
#define RESPOOL_STRIDE      0x160
#define RESPOOL_B_FLAG0     0x00    /* DAT_0057c020 — commodity-B demand, flag 0 */
#define RESPOOL_B_FLAG1     0x04    /* DAT_0057c024 — commodity-B demand, flag !=0 */
#define RESPOOL_C_FLAG0     0x08    /* DAT_0057c028 — commodity-C demand, flag 0 */
#define RESPOOL_C_FLAG1     0x0c    /* DAT_0057c02c — commodity-C demand, flag !=0 */
#define RESPOOL_A_FLAG0     0x10    /* DAT_0057c030 — commodity-A demand, flag 0 */
#define RESPOOL_A_FLAG1     0x14    /* DAT_0057c034 — commodity-A demand, flag !=0 */
#define RESPOOL_RACE_ACC    0x20    /* DAT_0057c040 — slot-2 per-race accumulator (6× i32) */

/* Baked per-race production multipliers (retail DAT_0057c020's siblings
 * DAT_004d8990 / DAT_004d8994 — a 6-entry initialised-.data table, 8-byte
 * stride: column A at +0, column B at +4).  Column A pairs with supply pool A,
 * column B with pool B. */
static const int k_race_pool_a[6] = { 6, 4, 3, 6, 13, 4 };
static const int k_race_pool_b[6] = { 10, 4, 10, 3, 0, 18 };

static uint8_t *g_resource_pool;   /* DAT_0057c020 — per-group demand table (resource pass accumulates into it) */

static int g_supply_pool_a;   /* DAT_0057c904 — sum of race column A over the group */
static int g_supply_pool_b;   /* DAT_0056d628 — sum of race column B over the group */

void engine_army_economy_bind_resource_pool(uint8_t *pool)
{
    g_resource_pool = pool;
}

int engine_army_economy_supply_pool_a(void) { return g_supply_pool_a; }
int engine_army_economy_supply_pool_b(void) { return g_supply_pool_b; }
void engine_army_economy_set_supply_pools(int pool_a, int pool_b) { g_supply_pool_a = pool_a; g_supply_pool_b = pool_b; }

/* FUN_0044f15b */
void engine_army_economy_sum_supply_pools(int group)
{
    g_supply_pool_a = 0;
    g_supply_pool_b = 0;

    if (g_army_records != NULL) {
        for (int i = 1; i <= (int)s_slot_0056d5dc; i++) {
            const uint8_t *r = g_army_records + i * ARMY_ROW_STRIDE;
            if (r[ARMY_GROUP] == (unsigned)group &&
                r[ARMY_RES_ACTIVE2] != 0 &&
                *(const int32_t *)(r + ARMY_SLOT2_JOBCOUNT) > 0) {
                unsigned race = r[ARMY_RACE];
                g_supply_pool_a += k_race_pool_a[race];
                g_supply_pool_b += k_race_pool_b[race];
            }
        }
    }

    if (g_supply_pool_a < 1) g_supply_pool_a = 1;
    if (g_supply_pool_b < 1) g_supply_pool_b = 1;
}

/* The keyed resource-slot record. */
static uint8_t *res_slot(uint8_t *row, int slot)
{
    return row + ARMY_RES_BASE + slot * RES_STRIDE;
}

/* A per-group demand field, or 0 when the pool table is unbound. */
static int respool_demand(int group, int off)
{
    if (g_resource_pool == NULL) return 0;
    return *(const int32_t *)(g_resource_pool + group * RESPOOL_STRIDE + off);
}

/* A writable per-group demand field, or NULL when the pool table is unbound. */
static int32_t *respool_field(int group, int off)
{
    if (g_resource_pool == NULL) return NULL;
    return (int32_t *)(g_resource_pool + group * RESPOOL_STRIDE + off);
}

/* FUN_0044ef4e */
int engine_army_economy_resource_quota(int key, int slot, int group, int flag)
{
    if (g_army_records == NULL) return 0;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    uint8_t *rs  = res_slot(row, slot);

    if (rs[RES_FLAG_C] == 0) return 0;

    if (slot == 2) {
        /* This army's race-weighted share of the group's demand pool, the
         * smaller of the A- and B-commodity shares (each 999 if its race
         * multiplier is 0, i.e. the army does not draw that commodity). */
        engine_army_economy_sum_supply_pools(group);

        unsigned race = row[ARMY_RACE];
        int race_a = k_race_pool_a[race];
        int race_b = k_race_pool_b[race];

        int share_a = 999, share_b = 999;
        if (race_a != 0) {
            int demand = flag == 0 ? respool_demand(group, RESPOOL_A_FLAG0)
                                   : respool_demand(group, RESPOOL_A_FLAG1);
            share_a = (demand * race_a / g_supply_pool_a) / race_a;
        }
        if (race_b != 0) {
            int demand = flag == 0 ? respool_demand(group, RESPOOL_B_FLAG0)
                                   : respool_demand(group, RESPOOL_B_FLAG1);
            share_b = (demand * race_b / g_supply_pool_b) / race_b;
        }
        return share_b < share_a ? share_b : share_a;
    }

    /* Non-pool slots: a flat 999 only when flag_a is set and flag_b clear.
     * (Retail re-checks flag_c here — dead, since the top gate proved it set.) */
    if (rs[RES_FLAG_A] == 0) return 0;
    return rs[RES_FLAG_B] == 0 ? 999 : 0;
}

/* FUN_0044f248 */
int engine_army_economy_supply_target(int key, int slot, int amount, int base_pct)
{
    if (g_army_records == NULL) return 0;

    if (s_slot_0053f25c == 0) return 0x50;   /* mode flag clear → flat 80 */
    if (amount == 0)          return 0;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    uint8_t *rs  = res_slot(row, slot);

    int base_floor = base_pct;
    int cap        = *(const int16_t *)(rs + RES_CAP);
    if (cap < amount) {
        int pct = engine_math_percent_div(cap, amount);   /* cap/amount as percent */
        base_pct = engine_math_percent_of(base_pct, pct); /* scale base toward it */
    }

    int target = *(const int8_t *)(rs + RES_BASE_PCT) + base_pct;
    if (target > 100)        target = 100;
    if (target < base_floor) target = base_floor;
    return target;
}

/* The keyed job-slot record (8× 0xc-byte, retail WF_COUNTER). */
static uint8_t *job_slot(uint8_t *row, int rec_idx)
{
    return row + ARMY_JOBSLOT_BASE + rec_idx * JOBSLOT_STRIDE;
}

/* FUN_0044f318 */
void engine_army_economy_plan_resource(int key, int slot, int rec_idx,
                                       int target, int divisor)
{
    if (g_army_records == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    uint8_t *rs  = res_slot(row, slot);
    uint8_t *job = job_slot(row, rec_idx);

    *(int32_t *)(rs + RES_QUOTA)        = 0;
    *(int32_t *)(row + ARMY_RES_PROD_A) = 0;
    *(int32_t *)(row + ARMY_RES_PROD_B) = 0;

    /* Slot 0 / slot 3 carry one of the secondary settlement's reserve values
     * (0 when the realm has no secondary settlement). */
    if (slot == 0)
        *(int32_t *)(row + ARMY_RES_CARRY0) =
            row[ARMY_SEC_FLAG] == 0 ? 0 : *(int32_t *)(row + ARMY_SEC_VAL2);
    if (slot == 3)
        *(int32_t *)(row + ARMY_RES_CARRY3) =
            row[ARMY_SEC_FLAG] == 0 ? 0 : *(int32_t *)(row + ARMY_SEC_VAL1);

    *(int32_t *)(job + JOBSLOT_SEL) = -1;
    *(int32_t *)(job + JOBSLOT_CAP) = 0;

    int group = row[ARMY_GROUP];
    if (group == 0) return;

    int quota = engine_army_economy_resource_quota(key, slot, group, 0);
    if (quota <= 0 || row[ARMY_PLAN_GATE] == 0) return;

    int step = row[ARMY_PLAN_GATE];                 /* gate byte doubles as the sweep step */
    int feed = *(int32_t *)(row + ARMY_FEED_COUNT);

    /* Sweep production levels 0, step, 2·step … (each clamped to feed) for the
     * one whose capped supply value is highest. */
    int best_pct = -1, best_level = 0;
    int base_at_top = 0;                            /* supply_target at the final level */
    for (int level = 0; level < feed + step; level += step) {
        int clamped = level > feed ? feed : level;
        base_at_top = engine_army_economy_supply_target(key, slot, clamped, target);
        int pct = engine_math_percent_of(clamped / divisor, base_at_top);
        if (pct > quota) pct = quota;
        if (pct > best_pct) { best_pct = pct; best_level = clamped; }
    }

    /* Final quota: the supply value at this slot's stored job-slot count. */
    int stored = *(int32_t *)(job + JOBSLOT_COUNT);
    rs[RES_TARGET_PCT] = (uint8_t)(int8_t)engine_army_economy_supply_target(key, slot, stored, target);

    int pct = engine_math_percent_of(stored / divisor, base_at_top);
    if (pct > quota) pct = quota;
    *(int32_t *)(rs + RES_QUOTA) = pct;

    if (slot == 2) {
        unsigned race = row[ARMY_RACE];
        *(int32_t *)(row + ARMY_RES_PROD_A) = k_race_pool_a[race] * pct;
        *(int32_t *)(row + ARMY_RES_PROD_B) = k_race_pool_b[race] * pct;
        *(int32_t *)(job + JOBSLOT_CAP) = best_level;
    } else {
        *(int32_t *)(job + JOBSLOT_CAP) = 100000;
    }
}

/* FUN_004485a5 */
void engine_army_economy_plan_realm(int key, int phase)
{
    engine_territory_census_army_terrain(key);       /* FUN_00469b8d */
    engine_army_economy_distribute_stock(key);       /* FUN_0044c278 */
    engine_army_economy_plan_production(key, phase);  /* FUN_0044d374 */
    engine_army_economy_select_candidate(key, phase); /* FUN_0044dd4d */
    engine_army_economy_plan_resource(key, 1, 4, 0x0f, 1);   /* FUN_0044f318 */
    engine_army_economy_plan_resource(key, 3, 5, 0x0f, 2);
    engine_army_economy_plan_resource(key, 0, 6, 0x14, 1);
    engine_army_economy_plan_resource(key, 2, 7, 0x0f, 4);
    engine_army_economy_plan_secondary(key);         /* FUN_00450e46 */
}

/* --------------------------------------------------------------------- *
 *  Per-realm resource-slot delivery pass — the FUN_0044e852 dispatcher
 *  and its two leaves 0x44edc2 (cell stamp) / 0x44ea92 (quota apply).
 *
 *  Where plan_resource (0x44f318) projects each slot's quota for the turn,
 *  these run during turn-advance: apply_resource_quota actually delivers the
 *  slot's share into the realm's running total and the per-group demand pool,
 *  and stamp_resource_cell repaints the slot's overworld cell to reflect its
 *  current production state.
 * --------------------------------------------------------------------- */

/* FUN_0044edc2.  Stamp resource `slot`'s overworld cell (the descriptor at the
 * slot's +0x04 byte offset) with a per-slot type code, plus — when the slot did
 * not grow this turn — a per-slot value into the cell's +3 byte.  Gated on the
 * slot being live (flag_a set, flag_b clear). */
void engine_army_economy_stamp_resource_cell(int key, int slot)
{
    if (g_army_records == NULL || g_cells == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    uint8_t *rs  = res_slot(row, slot);

    if (rs[RES_FLAG_A] == 0 || rs[RES_FLAG_B] != 0) return;

    int cur  = *(const int32_t *)(rs + RES_CURRENT);
    int prev = *(const int32_t *)(rs + RES_PREV);
    int32_t cell_off = *(const int32_t *)(rs + RES_CELL_IDX);

    /* Base type code: a per-slot constant, bumped by 1 when the slot is at its
     * top tier (flag_c set). */
    int type = (rs[RES_FLAG_C] != 0) ? 1 : 0;
    switch (slot) {
        case 1: type += 1;    break;
        case 3: type += 4;    break;
        case 2: type += 7;    break;
        case 0: type += 0x0a; break;
    }
    g_cells[cell_off + 0] = (uint8_t)type;

    /* Stalled (no net growth this turn) → tag the cell's +3 byte. */
    if (cur - prev < 1) {
        int tag = 0;
        switch (slot) {
            case 1: tag = 0x1e; break;
            case 3: tag = 0;    break;
            case 0: tag = 0x14; break;
            case 2: tag = 10;   break;
        }
        g_cells[cell_off + 3] = (uint8_t)tag;
    }
}

/* FUN_0044ea92.  Deliver resource `slot`'s production for realm `key`: snapshot
 * the running total into +0x10, then (when the slot is live) credit this turn's
 * supplied amount — the smaller of the level-scaled supply target and the slot's
 * delivery quota — into the running total and the owning group's demand pool.
 * When the slot is depleting (flag_b > 0) it instead decays the countdown and
 * re-stamps the cell once it hits zero.  `target`/`divisor` are the per-slot
 * constants from the 0x44e852 call table; `rec_idx` selects the job-slot record
 * whose count is the delivery level. */
void engine_army_economy_apply_resource_quota(int key, int slot, int rec_idx,
                                              int target, int divisor)
{
    if (g_army_records == NULL) return;

    uint8_t *row = g_army_records + key * ARMY_ROW_STRIDE;
    uint8_t *rs  = res_slot(row, slot);

    *(int32_t *)(rs + RES_PREV) = *(const int32_t *)(rs + RES_CURRENT);

    if (rs[RES_FLAG_B] != 0) {
        /* Depleting: zero the running total and tick the countdown; re-stamp the
         * cell when it expires. */
        *(int32_t *)(rs + RES_PREV)    = 0;
        *(int32_t *)(rs + RES_CURRENT) = *(const int32_t *)(rs + RES_PREV);
        rs[RES_FLAG_B] = (uint8_t)(rs[RES_FLAG_B] - 1);
        if ((int8_t)rs[RES_FLAG_B] < 1)
            engine_army_economy_stamp_resource_cell(key, slot);
        return;
    }

    int group = row[ARMY_GROUP];
    if (group == 0) return;

    int quota = engine_army_economy_resource_quota(key, slot, group, 1);

    uint8_t *job = job_slot(row, rec_idx);
    int level = *(const int32_t *)(job + JOBSLOT_COUNT);

    rs[RES_TARGET_PCT] =
        (uint8_t)(int8_t)engine_army_economy_supply_target(key, slot, level, target);
    rs[RES_BASE_PCT] = rs[RES_TARGET_PCT];

    int amount = engine_math_percent_of(level / divisor, (int8_t)rs[RES_TARGET_PCT]);
    if (quota < amount) amount = quota;
    *(int32_t *)(rs + RES_CURRENT) += amount;

    if (slot == 1) {
        int32_t *p = respool_field(group, RESPOOL_B_FLAG0);
        if (p) *p += amount;
    } else if (slot == 3) {
        int32_t *p = respool_field(group, RESPOOL_C_FLAG0);
        if (p) *p += amount;
    } else if (slot == 0) {
        int32_t *p = respool_field(group, RESPOOL_A_FLAG0);
        if (p) *p += amount;
    } else if (slot == 2) {
        unsigned race = row[ARMY_RACE];
        int race_a = k_race_pool_a[race];
        int race_b = k_race_pool_b[race];
        int32_t *acc = respool_field(group, RESPOOL_RACE_ACC + (int)race * 4);
        int32_t *pa  = respool_field(group, RESPOOL_A_FLAG0);
        int32_t *pb  = respool_field(group, RESPOOL_B_FLAG0);
        if (acc) *acc += amount;
        if (pa)  *pa  -= amount * race_a;
        if (pb)  *pb  -= race_b * amount;
        engine_ranking_recompute_player_total(group);   /* FUN_004487a9 */
    }
}

/* FUN_0044e852.  The per-turn resource-delivery pass over every realm.  First
 * snapshot each group pool's three commodity accumulators into their prior-turn
 * companions, then for every realm 1..s_slot_0056d5dc: deliver slot 2 (the
 * race-weighted pool slot), then slots 1/3/0, stamping each slot's cell and
 * re-planning it; each slot's cap (+0x0a) is refreshed from its job-slot count
 * afterwards. */
void engine_army_economy_resource_pass(void)
{
    /* Snapshot flag-0 accumulators → flag-1 prior-turn fields, groups 1..5. */
    for (int g = 1; g < 6; g++) {
        int32_t *b0 = respool_field(g, RESPOOL_B_FLAG0);
        int32_t *c0 = respool_field(g, RESPOOL_C_FLAG0);
        int32_t *a0 = respool_field(g, RESPOOL_A_FLAG0);
        if (b0) *respool_field(g, RESPOOL_B_FLAG1) = *b0;
        if (c0) *respool_field(g, RESPOOL_C_FLAG1) = *c0;
        if (a0) *respool_field(g, RESPOOL_A_FLAG1) = *a0;
    }

    if (g_army_records == NULL) return;

    /* Slot 2 (race-weighted pool slot) for every realm. */
    for (int r = 1; r <= (int)s_slot_0056d5dc; r++) {
        engine_army_economy_apply_resource_quota(r, 2, 7, 0x0f, 4);
        engine_army_economy_stamp_resource_cell(r, 2);
        engine_army_economy_plan_resource(r, 2, 7, 0x0f, 4);

        uint8_t *row = g_army_records + r * ARMY_ROW_STRIDE;
        *(int16_t *)(res_slot(row, 2) + RES_CAP) =
            (int16_t)*(const int32_t *)(job_slot(row, 7) + JOBSLOT_COUNT);
    }

    /* Slots 1/3/0 for every realm (each cell stamped, then all four slots
     * re-planned). */
    for (int r = 1; r <= (int)s_slot_0056d5dc; r++) {
        engine_army_economy_apply_resource_quota(r, 1, 4, 0x0f, 1);
        engine_army_economy_apply_resource_quota(r, 3, 5, 0x0f, 2);
        engine_army_economy_apply_resource_quota(r, 0, 6, 0x14, 1);
        engine_army_economy_stamp_resource_cell(r, 1);
        engine_army_economy_stamp_resource_cell(r, 3);
        engine_army_economy_stamp_resource_cell(r, 0);
        engine_army_economy_plan_resource(r, 1, 4, 0x0f, 1);
        engine_army_economy_plan_resource(r, 3, 5, 0x0f, 2);
        engine_army_economy_plan_resource(r, 0, 6, 0x14, 1);
        engine_army_economy_plan_resource(r, 2, 7, 0x0f, 4);

        uint8_t *row = g_army_records + r * ARMY_ROW_STRIDE;
        *(int16_t *)(res_slot(row, 1) + RES_CAP) =
            (int16_t)*(const int32_t *)(job_slot(row, 4) + JOBSLOT_COUNT);
        *(int16_t *)(res_slot(row, 3) + RES_CAP) =
            (int16_t)*(const int32_t *)(job_slot(row, 5) + JOBSLOT_COUNT);
        *(int16_t *)(res_slot(row, 0) + RES_CAP) =
            (int16_t)*(const int32_t *)(job_slot(row, 6) + JOBSLOT_COUNT);
    }
}
