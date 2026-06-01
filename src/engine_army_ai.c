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
