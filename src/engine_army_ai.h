/*
 * OpenLords2 — per-realm AI economic-management layer.
 *
 * The AI "brain" run once per turn for each realm an AI player owns, reached
 * from the turn driver FUN_00448440 → FUN_0049a990 → FUN_0049dd01 → a
 * personality dispatcher.  This module is being ported bottom-up; the
 * declarations below are the pieces landed so far.  All operate on the shared
 * strategic globals (g_per_army_768 / g_player_strategic_352) and assume the
 * caller has set them up; none guard a NULL block (the globals are static
 * arrays).
 */

#ifndef OPENLORDS2_ENGINE_ARMY_AI_H
#define OPENLORDS2_ENGINE_ARMY_AI_H

/* Port of FUN_0045158b.  Seed the realm's eight workforce target ratios
 * (+0x130 run) to the AI defaults {0x21, 0x32, 0x11, 0x28, 0xf, 0xf, 0xf, 0xf}
 * before the workforce allocator redistributes population against them. */
void engine_army_ai_seed_workforce_ratios(int key);

/* Port of FUN_00428c42.  Advance the realm's advancement progress by a 0..5 tier
 * step sized by `amount` (a gold spend) relative to mouths-to-feed/10, clamped
 * so the tier byte +0x219 never exceeds 5, folding the step into +0x219, the
 * approval byte +0xc (capped at 100) and the cumulative +0x194.  No-op when
 * `amount` <= 0. */
void engine_army_ai_advance_tier(int amount, int key);

/* Port of FUN_00450ccd.  Consume a realm's secondary-settlement build demands
 * (+0x1d4 / +0x1d0) from its owner's resource pools (+0x130 / +0x128), capped at
 * pool balance.  No-op for independent realms or those with no secondary
 * settlement (+0x1c3 == 0). */
void engine_army_ai_consume_build_demand(int key);

/* Port of FUN_004284ce.  The AI buy/sell transaction executor: move `amount`
 * units of commodity `slot` for `realm` owned by `player` (0 = independent),
 * then re-plan the realm's economy.  `amount` < 0 sells |amount| units at
 * `sell_price` (credit), `amount` > 0 buys at `buy_price` (debit); slot 4 funds
 * the advancement tier instead of a stock.  Owned realms abort when they can't
 * cover the sale stock / purchase cost.  `flag` mirrors the retail ABI but is
 * unused.  No-op when `amount` == 0.  Commodity slots: 1/2 are army-record
 * stocks (+0x224/+0x250), 6..0xe the owner's strategic pools; cash + ledger
 * fields live in the strategic block (+0x118 / +0x104..+0x110) or, for
 * independent realms, the realm's own gold (+0x1f4). */
void engine_army_ai_execute_trade(int flag, int amount, int slot,
                                  int buy_price, int sell_price,
                                  int player, int realm);

#endif /* OPENLORDS2_ENGINE_ARMY_AI_H */
