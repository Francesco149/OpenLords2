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

/* Port of FUN_004a4a3f.  Issue a SELL order: dump `amount` units of `slot` at
 * the base price (no affordability gate).  No-op unless +0x1a4 is set. */
void engine_army_ai_sell_order(int realm, int amount, int slot);

/* Port of FUN_004a4b12.  Issue a BUY order for `amount` units of `slot` only
 * when the whole order is affordable (realm gold for independents, owner cash
 * otherwise).  No-op unless +0x1a4 is set. */
void engine_army_ai_buy_order(int realm, int amount, int slot);

/* Port of FUN_004a4c41.  BUY as much of `amount` units of `slot` as the treasury
 * affords, halving the order until it fits (or hits 0).  No-op unless +0x1a4
 * is set. */
void engine_army_ai_buy_order_scaled(int realm, int amount, int slot);

/* Port of FUN_004a4694.  Re-balance the realm's high-tier cell footprint toward
 * `high_target` (shed one when density is low + stock-poor, add one when density
 * is high + below target), then re-census and re-grade. */
void engine_army_ai_balance_high_cells(int realm, int high_target);

/* Port of FUN_004a4782.  Classify the realm's strength band (+0x15e) from its
 * appraisal vs cost basis, then sweep the tax rate (+0x15f) to maximise the
 * provision net (+0x15d): sweep 0 → forward 0..100, 100 → backward 100..0, else
 * fix at `sweep`.  Leaves the best rate set with a final provision run. */
void engine_army_ai_tax_sweep(int realm, int sweep);

/* Port of FUN_0049e39b.  Reconcile a realm's three pooled commodities (8/6/7)
 * toward the owning `player`'s personality targets (buy a queued request, else
 * sell the surplus), then fund one discretionary buy when cash clears the
 * personality threshold.  No-op unless the realm's trade gate (+0x1a4) is set
 * and the player's trade lock (+0x158) is clear. */
void engine_army_ai_reconcile_resources(int player, int realm);

/* Ports of FUN_004a4052 / 004a42e3 / 004a440f — the three AI personality
 * dispatchers (codes 0 / 1 / 9): each reconciles resources, tops up stock, seeds
 * the workforce, sweeps tax, reshapes the cell footprint, and re-plans. */
void engine_army_ai_dispatch_balanced(int realm);
void engine_army_ai_dispatch_compact(int realm);
void engine_army_ai_dispatch_expansionist(int realm);

/* Port of FUN_0049dd01.  Run the per-realm AI for every realm owned by `player`:
 * develop empty cells toward a population-scaled target, latch the personality
 * dispatch code (+0x1fe), run the matching dispatcher, re-allocate workforce. */
void engine_army_ai_run_player(int player);

/* Port of FUN_0049a990.  The turn driver's first call: run the per-realm AI for
 * every player slot 1..5 still in the game (strategic +0x4 set, +0x5 clear). */
void engine_army_ai_run_turn(void);

#endif /* OPENLORDS2_ENGINE_ARMY_AI_H */
