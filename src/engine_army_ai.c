/*
 * OpenLords2 — per-realm AI economic-management layer.  See engine_army_ai.h.
 *
 * The AI "brain" the turn-advance engine runs once per turn for every realm an
 * AI player owns: reached from FUN_00448440 (the turn driver) → FUN_0049a990
 * (loop over players still in the game) → FUN_0049dd01 (per the player's realms)
 * → one of three personality dispatchers (FUN_004a4052/42e3/440f).  Each
 * dispatcher buys/sells resources to hit target stocks, sweeps the tax rate for
 * the best net, and assigns harvest/labour to feature cells, then re-plans the
 * realm's economy.
 *
 * Operates on the shared strategic globals directly (g_per_army_768 = the
 * per-realm record block, g_player_strategic_352 = the per-player block) plus
 * the cell roster / grid engine_territory owns, and calls into the deterministic
 * economy model (engine_army_economy_*), the territory cell mutators
 * (engine_territory_*), and the ranking accumulator (engine_ranking_*).
 */

#include <stdint.h>

#include "engine_army_ai.h"
#include "save.h"                  /* g_per_army_768, g_player_strategic_352 */
#include "save_schema_globals.h"   /* s_slot_0057c934 (event season), 0057c92c (map season) */
#include "engine_army_economy.h"   /* engine_army_economy_provision / _plan_realm */
#include "engine_territory.h"      /* recensus_reduce / grade_feature_cells / allocate_workforce */
#include "engine_ranking.h"        /* engine_ranking_recompute_player_total */
#include "engine_math.h"           /* engine_math_percent_of (FUN_00404d6b) */

#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])

#define I32(p)  (*(int32_t *)(p))

/* ── Per-realm record fields (offsets into g_per_army_768 row `key`) ── */
#define ARMY_OWNER          0x005   /* DAT_0053f9b5 — owning player (0 = independent) */
#define ARMY_APPROVAL       0x00c   /* DAT_0053f9bc — signed approval byte, capped 100 */
#define ARMY_FEED_COUNT     0x024   /* DAT_0053f9d4 — mouths to feed (tier denominator) */
/* Workforce target-ratio run (+0x130 .. +0x14c, eight i32) the AI seeds before
 * the workforce allocator (engine_territory_allocate_realm_workforce) runs. */
#define ARMY_WF_RATIO_0     0x130   /* DAT_0053fae0 */
#define ARMY_WF_RATIO_1     0x134   /* DAT_0053fae4 */
#define ARMY_WF_RATIO_2     0x138   /* DAT_0053fae8 */
#define ARMY_WF_RATIO_3     0x13c   /* DAT_0053faec */
#define ARMY_WF_RATIO_4     0x140   /* DAT_0053faf0 */
#define ARMY_WF_RATIO_5     0x144   /* DAT_0053faf4 */
#define ARMY_WF_RATIO_6     0x148   /* DAT_0053faf8 */
#define ARMY_WF_RATIO_7     0x14c   /* DAT_0053fafc */
#define ARMY_TIER_PROGRESS  0x194   /* DAT_0053fb44 — cumulative advancement (i32) */
#define ARMY_BUILD_DRAW_LO  0x1d0   /* DAT_0053fb80 — settlement build demand, pool +0x128 */
#define ARMY_BUILD_DRAW_HI  0x1d4   /* DAT_0053fb84 — settlement build demand, pool +0x130 */
#define ARMY_SETTLE_FLAG    0x1c3   /* DAT_0053fb73 — secondary settlement present */
#define ARMY_TIER_STEP      0x219   /* DAT_0053fbc9 — advancement tier byte (0..5) */

/* ── Per-player record fields (offsets into g_player_strategic_352 row p) ── */
#define STRAT_POOL_128      0x128   /* DAT_0057c028 — resource pool drawn by +0x1d0 */
#define STRAT_POOL_130      0x130   /* DAT_0057c030 — resource pool drawn by +0x1d4 */
#define STRAT_CASH          0x118   /* DAT_0057c018 — player treasury */
#define STRAT_BUY_LEDGER_A  0x104   /* DAT_0057c004 — buy expenditure accumulator A */
#define STRAT_BUY_LEDGER_B  0x108   /* DAT_0057c008 — buy expenditure accumulator B */
#define STRAT_SELL_LEDGER_A 0x10c   /* DAT_0057c00c — sell revenue accumulator A */
#define STRAT_SELL_LEDGER_B 0x110   /* DAT_0057c010 — sell revenue accumulator B */
#define ARMY_TREASURY       0x1f4   /* DAT_0053fba4 — independent realm's own gold */
#define ARMY_PROVISION_RAW  0x224   /* DAT_0053fbd4 — commodity slot 1 stock */
#define ARMY_STOCK_A        0x250   /* DAT_0053fc00 — commodity slot 2 stock */
#define ARMY_TRADE_GATE     0x1a4   /* DAT_0053fb54 — byte: realm may trade when set */
#define ARMY_RACE_IDX       0x1a5   /* DAT_0053fb55 — territory-420 index for price lookup */
#define ARMY_CENSUS_HIGH    0x200   /* DAT_0053fbb0 — census 0x0f..0x16 bucket */
#define ARMY_OVERHEAD_HI    0x198   /* DAT_0053fb48 — overhead term 1 */
#define ARMY_OVERHEAD_LO    0x19c   /* DAT_0053fb4c — overhead term 2 */
#define ARMY_STRENGTH_BAND  0x15e   /* DAT_0053fb0e — 0..5 strength band the sweep classifies */
#define ARMY_PROVISION_NET  0x15d   /* DAT_0053fb0d — provision net the sweep maximises */
#define ARMY_TAX_RATE       0x15f   /* DAT_0053fb0f — tax-rate % the sweep selects */
#define ARMY_DENSITY_VALUE  0x25c   /* DAT_0053fc0c — graded density value */

/* g_per_territory_420 field holding a per-territory price-adjustment percent
 * (retail DAT_0052f216 = base 0x52f0b0 + 0x166), indexed by an army's +0x1a5. */
#define TERR420_PRICE_ADJ   0x166

/* Commodity base price: the int at s_slot_005532a0[slot * 0x10] (retail
 * DAT_005532a0, a 16-entry × 0x10-byte price table). */
#define COMMODITY_PRICE(slot) (*(int32_t *)&s_slot_005532a0[(slot) * 0x10])

/* Port of FUN_0045158b (147 B).  Seed the realm's eight workforce target ratios
 * (+0x130 run) to the AI's fixed defaults {0x21, 0x32, 0x11, 0x28, 0xf, 0xf,
 * 0xf, 0xf} before the workforce allocator (FUN_0044f6e7) redistributes its
 * population against them.  Called by every personality dispatcher. */
void engine_army_ai_seed_workforce_ratios(int key)
{
    uint8_t *r = ARMY_REC(key);
    I32(r + ARMY_WF_RATIO_0) = 0x21;
    I32(r + ARMY_WF_RATIO_1) = 0x32;
    I32(r + ARMY_WF_RATIO_2) = 0x11;
    I32(r + ARMY_WF_RATIO_6) = 0xf;
    I32(r + ARMY_WF_RATIO_5) = 0xf;
    I32(r + ARMY_WF_RATIO_4) = 0xf;
    I32(r + ARMY_WF_RATIO_7) = 0xf;
    I32(r + ARMY_WF_RATIO_3) = 0x28;
}

/* Port of FUN_00428c42 (365 B).  Advance the realm's "advancement" progress by a
 * 0..5 tier step sized by how `amount` (a gold spend) compares to the realm's
 * mouths-to-feed/10 (+0x24): each whole multiple up to 5 is one tier.  The step
 * is clamped so the tier byte +0x219 never exceeds 5, then folded into +0x219,
 * the approval byte +0xc (capped at 100), and the cumulative +0x194.  No-op when
 * `amount` <= 0.  Reached from the slot-4 ("training") arm of the buy/sell
 * executor. */
void engine_army_ai_advance_tier(int amount, int key)
{
    if (amount <= 0) return;

    uint8_t *r = ARMY_REC(key);
    int unit = I32(r + ARMY_FEED_COUNT) / 10;

    int step;
    if      (amount < unit * 5) {
        if      (amount < unit * 4) {
            if      (amount < unit * 3) {
                if      (amount < unit * 2) step = (amount < unit) ? 0 : 1;
                else                        step = 2;
            } else                          step = 3;
        } else                              step = 4;
    } else                                  step = 5;

    int headroom = 5 - r[ARMY_TIER_STEP];
    if (headroom < step) step = headroom;
    if (step < 0)        step = 0;

    r[ARMY_TIER_STEP]   += (uint8_t)step;
    r[ARMY_APPROVAL]    += (uint8_t)step;
    I32(r + ARMY_TIER_PROGRESS) += step;
    if ((int8_t)r[ARMY_APPROVAL] > 99) r[ARMY_APPROVAL] = 100;   /* retail 'c' = 99 */
}

/* Port of FUN_00450ccd (377 B).  Settle a realm's secondary-settlement build
 * demands against its owner's two resource pools: when the realm has a secondary
 * settlement (+0x1c3) and a non-independent owner, consume min(pool, demand)
 * from the owner's pool +0x130 (demand +0x1d4) and pool +0x128 (demand +0x1d0),
 * reducing both pool and demand by the consumed amount.  No-op for independent
 * realms or those without a secondary settlement.  Run in the buy/sell
 * executor's re-plan tail. */
void engine_army_ai_consume_build_demand(int key)
{
    uint8_t *r = ARMY_REC(key);
    if (r[ARMY_SETTLE_FLAG] == 0) return;
    int owner = r[ARMY_OWNER];
    if (owner == 0) return;

    uint8_t *s = STRAT_REC(owner);

    int pool_hi = I32(s + STRAT_POOL_130);
    if (pool_hi < I32(r + ARMY_BUILD_DRAW_HI)) {
        I32(s + STRAT_POOL_130)   -= pool_hi;
        I32(r + ARMY_BUILD_DRAW_HI) -= pool_hi;
    } else {
        I32(s + STRAT_POOL_130)   -= I32(r + ARMY_BUILD_DRAW_HI);
        I32(r + ARMY_BUILD_DRAW_HI) = 0;
    }

    int pool_lo = I32(s + STRAT_POOL_128);
    if (pool_lo < I32(r + ARMY_BUILD_DRAW_LO)) {
        I32(s + STRAT_POOL_128)   -= pool_lo;
        I32(r + ARMY_BUILD_DRAW_LO) -= pool_lo;
    } else {
        I32(s + STRAT_POOL_128)   -= I32(r + ARMY_BUILD_DRAW_LO);
        I32(r + ARMY_BUILD_DRAW_LO) = 0;
    }
}

/* Resolve a commodity `slot`'s stock field to the pointer the trade executor
 * adjusts: slots 1/2 are per-realm army-record stocks; slots 6..0xe are the
 * owning player's strategic-block pools.  Slot 4 ("training") and any unknown
 * slot have no field (NULL) — slot 4 is funded straight into the advancement
 * tier instead. */
static int32_t *commodity_field(int slot, int player, int realm)
{
    uint8_t *a = ARMY_REC(realm);
    uint8_t *s = STRAT_REC(player);
    switch (slot) {
        case 1:    return (int32_t *)(a + ARMY_PROVISION_RAW);
        case 2:    return (int32_t *)(a + ARMY_STOCK_A);
        case 6:    return (int32_t *)(s + 0x120);
        case 7:    return (int32_t *)(s + STRAT_POOL_128);
        case 8:    return (int32_t *)(s + STRAT_POOL_130);
        case 9:    return (int32_t *)(s + 0x14c);
        case 0xa:  return (int32_t *)(s + 0x150);
        case 0xb:  return (int32_t *)(s + 0x144);
        case 0xc:  return (int32_t *)(s + 0x140);
        case 0xd:  return (int32_t *)(s + 0x148);
        case 0xe:  return (int32_t *)(s + 0x154);
        default:   return NULL;
    }
}

/* Port of FUN_004284ce (1858 B).  The AI's buy/sell transaction executor: move
 * `amount` units of commodity `slot` for realm `realm` owned by `player`
 * (0 = independent), then re-plan the realm's whole economy.  `amount` < 0
 * SELLS |amount| units at unit price `sell_price`, crediting the proceeds;
 * `amount` > 0 BUYS `amount` units at unit price `buy_price`, debiting the cost.
 * `flag` (retail param_1) is accepted to mirror the call ABI but is unused.
 *
 *  - Sell: when the realm is owned, require the stock field to cover the sale
 *    (else abort).  Reduce the stock, add price×qty to the treasury — the
 *    owner's cash (+0x118) plus its two revenue ledgers (+0x10c/+0x110), or the
 *    independent realm's own gold (+0x1f4).
 *  - Buy: when owned, require the treasury to cover cost = price×amount (else
 *    abort).  Add the stock (or, for slot 4, fund the advancement tier), debit
 *    the cost from cash (+0x118) into the two expenditure ledgers (+0x104/+0x108),
 *    or from the independent realm's gold.
 *
 * Then (only when amount != 0): resync the owner's cashflow total, settle the
 * realm's build demands, reduce a footprint when a slot-2 buy reshaped it, grade
 * its cells, and run the provision→plan→workforce→provision→plan re-plan chain
 * at the saved seasons.  No-op when amount == 0.  The downstream re-plan callees
 * each guard their own (separately-bound) state. */
void engine_army_ai_execute_trade(int flag, int amount, int slot,
                                  int buy_price, int sell_price,
                                  int player, int realm)
{
    (void)flag;
    if (amount == 0) return;

    uint8_t *a = ARMY_REC(realm);
    uint8_t *s = STRAT_REC(player);
    int32_t *fld = commodity_field(slot, player, realm);

    if (amount < 0) {
        int qty  = -amount;
        int gold = sell_price * qty;
        if (player != 0 && fld != NULL && *fld < qty) return;   /* can't cover sale */

        if (fld != NULL) *fld += amount;                        /* amount < 0 → reduce */
        if (player == 0) {
            I32(a + ARMY_TREASURY) += gold;
        } else {
            I32(s + STRAT_CASH)        += gold;
            I32(s + STRAT_SELL_LEDGER_B) += gold;
            I32(s + STRAT_SELL_LEDGER_A) += gold;
        }
    } else {
        int cost = buy_price * amount;
        if (player != 0 && I32(s + STRAT_CASH) < cost) return;   /* can't afford */

        if (slot == 4)        engine_army_ai_advance_tier(cost, realm);
        else if (fld != NULL) *fld += amount;

        if (player == 0) {
            I32(a + ARMY_TREASURY) -= cost;
        } else {
            I32(s + STRAT_CASH)        -= cost;
            I32(s + STRAT_BUY_LEDGER_B) += cost;
            I32(s + STRAT_BUY_LEDGER_A) += cost;
        }
    }

    engine_ranking_recompute_player_total(player);
    engine_army_ai_consume_build_demand(realm);
    if (slot == 2 && amount >= 0) engine_territory_recensus_reduce(realm);
    engine_territory_grade_feature_cells(realm);
    engine_army_economy_provision(realm, (int)s_slot_0057c934);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_economy_provision(realm, (int)s_slot_0057c934);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
}

/* Per-unit BUY price for commodity `slot` in `realm`: the base price marked up
 * by the realm's territory-420 price-adjustment percent (floor 1), or just the
 * base for slot 4 (the markup is 0).  *base_out receives the un-marked-up base
 * (the SELL price).  Shared by the three order helpers. */
static int trade_unit_price(int realm, int slot, int *base_out)
{
    uint8_t *a = ARMY_REC(realm);
    int base = COMMODITY_PRICE(slot);
    int adjpct = g_per_territory_420[a[ARMY_RACE_IDX] * 0x1a4 + TERR420_PRICE_ADJ];
    int adj = engine_math_percent_of(base, adjpct);
    if (adj < 1) adj = 1;
    if (slot == 4) adj = 0;
    *base_out = base;
    return adj + base;
}

/* Port of FUN_004a4a3f (211 B).  Issue a SELL order: dump `amount` units of
 * `slot` at the base price through the trade executor (flag 1, signed negative).
 * No affordability gate — selling is always allowed.  No-op unless the realm's
 * trade gate (+0x1a4) is set. */
void engine_army_ai_sell_order(int realm, int amount, int slot)
{
    uint8_t *a = ARMY_REC(realm);
    if (a[ARMY_TRADE_GATE] == 0) return;

    int base;
    int unit = trade_unit_price(realm, slot, &base);
    engine_army_ai_execute_trade(1, -amount, slot, unit, base, a[ARMY_OWNER], realm);
}

/* Port of FUN_004a4b12 (298 B).  Issue a BUY order for `amount` units of `slot`,
 * but only when the buyer can afford the whole order up front: the independent
 * realm's gold (+0x1f4) for player 0, else the owner's cash (+0x118).  No-op
 * unless the trade gate is set. */
void engine_army_ai_buy_order(int realm, int amount, int slot)
{
    uint8_t *a = ARMY_REC(realm);
    if (a[ARMY_TRADE_GATE] == 0) return;

    int player = a[ARMY_OWNER];
    int base;
    int unit = trade_unit_price(realm, slot, &base);
    int cost = unit * amount;

    int afford = (player == 0)
        ? (cost <= I32(a + ARMY_TREASURY))
        : (cost <= I32(STRAT_REC(player) + STRAT_CASH));
    if (afford)
        engine_army_ai_execute_trade(0, amount, slot, unit, base, player, realm);
}

/* Port of FUN_004a4c41 (311 B).  BUY as much of `amount` units of `slot` as the
 * buyer can afford, halving the order until it fits the treasury (or hits 0).
 * No-op unless the trade gate is set. */
void engine_army_ai_buy_order_scaled(int realm, int amount, int slot)
{
    uint8_t *a = ARMY_REC(realm);
    if (a[ARMY_TRADE_GATE] == 0) return;

    int player = a[ARMY_OWNER];
    int base;
    int unit = trade_unit_price(realm, slot, &base);
    int treasury = (player == 0)
        ? I32(a + ARMY_TREASURY)
        : I32(STRAT_REC(player) + STRAT_CASH);

    while (amount > 0) {
        if (unit * amount <= treasury) {
            engine_army_ai_execute_trade(0, amount, slot, unit, base, player, realm);
            return;
        }
        amount /= 2;
    }
}

/* Port of FUN_004a4694 (238 B).  Re-balance the realm's high-tier cell footprint
 * against a target `high_target`: when the graded density (+0x25c) is low (< 0xb)
 * and the realm is high-cell-rich but stock-poor, shed one high cell; when the
 * density is high (> 10) and the realm is below its high-cell target, add one.
 * Either way the band is rewritten via demolish_band/convert_or_demolish_band,
 * then the realm is re-censused and re-graded. */
void engine_army_ai_balance_high_cells(int realm, int high_target)
{
    uint8_t *a = ARMY_REC(realm);
    int high = a[ARMY_CENSUS_HIGH];

    if (I32(a + ARMY_DENSITY_VALUE) < 0xb) {
        if (high > 2 && I32(a + ARMY_STOCK_A) < 10) {
            engine_territory_demolish_band(realm, 0x13);
            engine_territory_convert_or_demolish_band(realm, 0x13, high - 1);
        }
    } else if (I32(a + ARMY_DENSITY_VALUE) > 10 && high < high_target) {
        engine_territory_demolish_band(realm, 0x13);
        engine_territory_convert_or_demolish_band(realm, 0x13, high + 1);
    }
    engine_territory_census_army_terrain(realm);
    engine_territory_grade_feature_cells(realm);
}

/* Port of FUN_004a4782 (701 B).  Classify the realm's economic strength into a
 * 0..5 band (+0x15e) from its appraisal value vs a cost basis (mouths-to-feed
 * +0x24, plus the two overhead terms when the global mode s_slot_0053f260 == 1),
 * then sweep the tax rate (+0x15f) to maximise the provision net (+0x15d):
 *   sweep == 0   → forward sweep 0..100
 *   sweep == 100 → backward sweep 100..0
 *   else         → fix the rate at `sweep`
 * Each trial calls the economy provisioner at the map season; the best rate is
 * left in +0x15f with a final provision run there. */
void engine_army_ai_tax_sweep(int realm, int sweep)
{
    uint8_t *a = ARMY_REC(realm);
    int appraisal = engine_army_economy_appraisal_value(realm);
    int stock_a   = engine_army_economy_stock_a_value(realm);

    int basis = I32(a + ARMY_FEED_COUNT);
    if (s_slot_0053f260 == 1)
        basis += I32(a + ARMY_OVERHEAD_LO) + I32(a + ARMY_OVERHEAD_HI);

    if      (appraisal < basis / 2)  a[ARMY_STRENGTH_BAND] = 0;
    else if (appraisal < basis)      a[ARMY_STRENGTH_BAND] = 1;
    else if (appraisal < basis * 2)  a[ARMY_STRENGTH_BAND] = 2;
    else if (basis * 3 < stock_a)    a[ARMY_STRENGTH_BAND] = 5;
    else if (basis * 2 < stock_a)    a[ARMY_STRENGTH_BAND] = 4;
    else if (basis * 8 < appraisal)  a[ARMY_STRENGTH_BAND] = 5;
    else if (basis * 6 < appraisal)  a[ARMY_STRENGTH_BAND] = 4;
    else                             a[ARMY_STRENGTH_BAND] = 3;

    int8_t best_net = 0;
    int best_rate = sweep;
    if (sweep == 0) {
        for (int rate = 0; rate < 0x65; rate++) {
            a[ARMY_TAX_RATE] = (uint8_t)rate;
            engine_army_economy_provision(realm, (int)s_slot_0057c92c);
            if (best_net < (int8_t)a[ARMY_PROVISION_NET]) {
                best_net = (int8_t)a[ARMY_PROVISION_NET];
                best_rate = rate;
            }
        }
        a[ARMY_TAX_RATE] = (uint8_t)best_rate;
    } else if (sweep == 100) {
        for (int rate = 100; rate >= 0; rate--) {
            a[ARMY_TAX_RATE] = (uint8_t)rate;
            engine_army_economy_provision(realm, (int)s_slot_0057c92c);
            if (best_net < (int8_t)a[ARMY_PROVISION_NET]) {
                best_net = (int8_t)a[ARMY_PROVISION_NET];
                best_rate = rate;
            }
            a[ARMY_TAX_RATE] = (uint8_t)best_rate;   /* retail resets in-loop (dead but faithful) */
        }
    } else {
        a[ARMY_TAX_RATE] = (uint8_t)sweep;
    }
    engine_army_economy_provision(realm, (int)s_slot_0057c92c);
}
