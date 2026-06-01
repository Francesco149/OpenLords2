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
