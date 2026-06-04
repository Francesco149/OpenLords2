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

#include "engine_event_queue.h"   /* engine_event_queue_state_t (proposal posting) */

/* Port of FUN_0045158b.  Seed the realm's eight workforce target ratios
 * (+0x130 run) to the AI defaults {0x21, 0x32, 0x11, 0x28, 0xf, 0xf, 0xf, 0xf}
 * before the workforce allocator redistributes population against them. */
void engine_army_ai_seed_workforce_ratios(int key);

/* Port of FUN_004502ca.  Toggle one of the realm's eight workforce slots (the
 * +0x130 run) and re-normalise the five ratio slots 3..7 (a percentage split
 * summing to 100) around the change.  `slot` selects which slot is toggled;
 * `mode` 1 enables it — it claims a share of pct[active-ratio-count]
 * (table 0x4d6768 {100,50,33,25,20}) and scales the five ratios down by the
 * remainder; `mode` 0 disables it — it scales the ratios back up by
 * 100/(100−wf[slot]).  Either way the five ratios are re-normalised so their
 * remainder-to-100 lands on the currently-largest ratio.  No-op when the slot is
 * already in the requested state.  Drivers: the workforce-allocation UI handlers
 * (FUN_00436e.. / FUN_0043d3.., dynamic slot) and the secondary-settlement
 * completion pass FUN_004508de, which always calls (realm, 3, 0).  The disable
 * branch divides by 100−wf[slot]; that is 0 only if the slot had previously
 * claimed the full 100% (enabled when no ratios were active) — a latent retail
 * div-by-zero the live drivers never reach. */
void engine_army_ai_rebalance_workforce(int key, int slot, int mode);

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

/* Port of FUN_0049d638.  Phase-1 AI init for one player slot: set each owned
 * realm's ration/tax setting (+0xb9) from its approval via a personality-keyed
 * ladder (player 0 uses a fixed independent ladder), then — for an active,
 * non-eliminated AI player whose level (+0x29) is set — credit a difficulty
 * cash grant and top up its realms' population / stock / provisions. */
void engine_army_ai_set_rations_and_grant(int player);

/* Ports of FUN_004a3c67 / FUN_004a3ed3 — the dispatch-code-0 / -1 managers for
 * INDEPENDENT realms (owner 0): self-funded balanced / compact variants of the
 * personality dispatchers, with no market reconcile (a provision shortfall is
 * met by crediting the realm's own gold pool +0x1f4). */
void engine_army_ai_dispatch_independent_balanced(int realm);
void engine_army_ai_dispatch_independent_compact(int realm);

/* Port of FUN_0049dfc6.  Per-realm AI driver for INDEPENDENT realms (sibling of
 * engine_army_ai_run_player, always called with player 0): develop empty cells
 * toward a census-scaled target, run each realm's dispatch code (+0x1fe → 0/1),
 * re-allocate the workforce. */
void engine_army_ai_run_independent(int player);

/* Port of FUN_0049d5e0.  Return the key of the first realm owned by `owner`
 * (1..count), or 0 if none — a generic owner-byte (+0x005) scan. */
int engine_army_ai_first_owned_realm(int owner);

/* Port of FUN_0049e1bf — AI brain step 4 (seed explicit buy requests).  Clears
 * the player's +0x70 buy-request run, then (for a high-level or prosperous AI)
 * tops the staple commodities to a floor and folds in each developed,
 * keep-bearing owned realm's build demand. */
void engine_army_ai_seed_buy_requests(int player);

/* Port of FUN_004a13a6 — AI brain step 0xd (diplomacy proposals).  For a #1-or-
 * unranked player, alternately broadcasts alliance (0xc1) proposals to every
 * in-game human slot then sends a war (0xc0) proposal to the weakest player
 * (s_slot_00522d90), once per per-tick cooldown, gated on a diplomacy-readiness
 * counter (+0x60).  Proposals are posted through `evq`. */
void engine_army_ai_propose_diplomacy(int player, engine_event_queue_state_t *evq);

/* Port of FUN_004a0c1d — AI brain step 2 (per-turn diplomacy decision): warm
 * relations toward AI players, maintain or betray the player's lord as betrayal
 * pressure accrues, and — when lordless and past year 1268 — seek/propose a new
 * alliance.  Notifications post through `evq`. */
void engine_army_ai_run_diplomacy(int player, engine_event_queue_state_t *evq);

/* Port of FUN_0049f96c — AI brain step 8.  A no-op in the shipped build. */
void engine_army_ai_step_noop(void);

/* Port of FUN_0049ed13.  Flag whether `player` can afford `realm`'s current
 * build order: clears the realm's affordability flag, runs the slot-2 resource
 * planner, then re-sets the flag iff the player's +0x130/+0x120 pools both cover
 * the current build tag's two costs. */
void engine_army_ai_mark_build_affordable(int player, int realm);

/* Port of FUN_0049e77d — AI brain step 0xc (per-turn build-order planning).
 * Rotates each owned realm's build tag through the personality build columns and
 * refreshes affordability, enables the realm job-bucket workforce gates (subject
 * to the secondary-settlement filter), then runs the workforce allocator and the
 * per-realm economy orchestrator for each owned realm. */
void engine_army_ai_run_build_orders(int player);

#endif /* OPENLORDS2_ENGINE_ARMY_AI_H */
