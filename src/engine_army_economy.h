/*
 * OpenLords2 — per-army stockpile valuation + budget→units helpers.
 * See engine_army_economy.c.
 *
 * A small cluster of pure arithmetic helpers the AI economy uses to put a
 * gold value on an army/realm's three commodity stockpiles and to convert a
 * gold budget back into purchasable unit counts.  Callees of the per-army
 * provisioning decision 0x44df5f (itself reached from the army/country
 * setup pass 0x49bd99) and of the AI economy passes at 0x4ace5e / 0x4a0aaa.
 */

#ifndef OPENLORDS2_ENGINE_ARMY_ECONOMY_H
#define OPENLORDS2_ENGINE_ARMY_ECONOMY_H

#include <stdint.h>

/* Bind the per-army record block (retail `DAT_0053f9b0` = save entry 4,
 * g_per_army_768 — 17 rows × 0x300 B, slot 0 reserved).  The valuation
 * helpers read three i32 stock fields out of the keyed row:
 *   +0x250  stock A  (retail DAT_0053fc00 — the "load numerator")
 *   +0x184  stock B  (retail DAT_0053fb34)
 *   +0x180  stock C  (retail DAT_0053fb30)
 * Same block engine_territory / engine_overlay_render bind their own views
 * of.  NULL → the keyed-row valuations return 0 (the cost→units helpers do
 * not touch the block and are always live). */
void engine_army_economy_bind_army_records(uint8_t *records);

/* Override the three commodity unit prices (retail config scalars set once by
 * the defaults init 0x4983b7 to A=5 / B=10 / C=6 — the values baked in as
 * this module's defaults).  Exists so the eventual config-init port can keep a
 * single source of truth; tests use it to exercise non-default prices. */
void engine_army_economy_set_prices(int price_a, int price_b, int price_c);

/* Port of FUN_0044e5bb (72 B).  Gold value of army `key`'s stock A: the
 * +0x250 field × price A, or 0 when the field is <= 0.  0 if the army-record
 * block is unbound. */
int engine_army_economy_stock_a_value(int key);

/* Port of FUN_0044e7b4 (158 B).  Combined gold value of army `key`'s three
 * stockpiles: +0x250×priceA + +0x184×priceB + +0x180×priceC, each term added
 * only when its field is > 0.  0 if the army-record block is unbound.  Note
 * the first term is identical to engine_army_economy_stock_a_value; retail
 * inlines it rather than calling, and so do we. */
int engine_army_economy_stockpile_value(int key);

/* Port of FUN_0044e6a3 (158 B).  The "appraisal" valuation the AI tax-rate
 * sweep (FUN_004a4782) uses: stock A (+0x250) valued at BOTH price A and
 * price B, plus provision-raw (+0x224) valued at price C — each term added
 * only when its field is > 0.  0 if the army-record block is unbound. */
int engine_army_economy_appraisal_value(int key);

/* Port of FUN_0044e603 (80 B).  Number of commodity-B units a gold budget
 * `cost` buys: ceil(cost / priceB), or 0 when cost <= 0.  `key` is accepted to
 * mirror the retail 2-arg call ABI but is ignored (the divisor is the global
 * price, not a per-army value). */
int engine_army_economy_units_b(int key, int cost);

/* Port of FUN_0044e653 (80 B).  As engine_army_economy_units_b but for
 * commodity C: ceil(cost / priceC).  `key` is likewise ignored. */
int engine_army_economy_units_c(int key, int cost);

/* Override the production-level solver's tuning constants (retail config
 * scalars set once by the defaults init 0x4983b7 — the values baked in as this
 * module's defaults):
 *   scale       DAT_0057c8e0 = 12  (gold weight applied to a candidate output)
 *   max_level   DAT_00552ffc = 10  (top of the level ladder / loop bound)
 *   divisor_lo  DAT_0057d34c = 2   (used when the mode flag s_slot_0053f25c==0)
 *   divisor_hi  DAT_005533bc = 5   (used when the mode flag != 0)
 * Same single-source-of-truth rationale as engine_army_economy_set_prices. */
void engine_army_economy_set_production_tuning(int scale, int max_level,
                                               int divisor_lo, int divisor_hi);

/* The live production scale (DAT_0057c8e0, g_prod_scale = 12 default).  The
 * labor grain panel multiplies per-field seed by it for the season-1 total
 * harvest readout (FUN_00413590). */
int engine_army_economy_prod_scale(void);

/* Port of FUN_0044cfe1 (377 B).  Solve army `key`'s sustainable production
 * output under two budget caps.  The army's per-resource-cell rate (record
 * +0x201, the census TYPE-0x02..0x0e bucket) times a candidate level (max_level
 * down to 1) is the output; the solver picks the highest level whose output
 * `q` satisfies both q <= limit_raw and (scale*q)/divisor <= limit_scaled, and
 * returns that output.  When even level 1 overflows, it falls back to returning
 * the highest flat level (not rate-scaled) that fits, latching the fallback
 * byte (record +0x1a7 — the same byte engine_territory reads as its retype
 * flag) to 1; if nothing fits at all it returns 0 and clears that byte.  The
 * divisor is divisor_lo or divisor_hi by the saved mode flag s_slot_0053f25c.
 * Returns 0 (no record touched) when either cap is < 1, or if the army-record
 * block is unbound. */
int engine_army_economy_production_output(int key, int limit_scaled,
                                          int limit_raw);

/* Port of FUN_0044df5f (1628 B).  The per-army provisioning decision reached
 * from the army/country setup pass 0x49bd99 (and ~25 other sites, almost always
 * paired with 0x4485a5).  For army `key` it settles a ration "supply level"
 * the army can afford out of its stockpile value, then splits the residual gold
 * cost between commodities B and C and writes clamped buy orders.  Reads/writes
 * many fields of the keyed record row; see engine_army_economy.c for the full
 * field map.  `phase` is the saved game-mode code (retail s_slot_0057c934) —
 * only the value 4 changes behavior, folding this army's home production
 * (engine_army_economy_production_output) into the commodity-C need.  No-ops
 * when the army-record block is unbound. */
void engine_army_economy_provision(int key, int phase);

/* ──────────────────────────────────────────────────────────────────
 * Per-army production-level planner — the 0x44d374 branch of the
 * per-realm economy pass 0x4485a5 (itself reached from 0x49bd99).
 *
 * Three per-"intensity-level" output evaluators feed one selector.  The
 * selector sweeps a candidate level L over [0, feed-count) for the army,
 * evaluates the achievable output at each L with the evaluator chosen by
 * `phase`, and records the best L and the output at the army's stored
 * reference level (+0xc4) into the record row, finishing with a net-balance
 * figure at +0x22c.  The three evaluators are:
 *   phase == 1            → engine_army_economy_production_output (cfe1)
 *   phase == 4            → engine_army_economy_yield_phase4      (d1e5)
 *   phase == 2/3/anything → engine_army_economy_yield_standard   (d15a)
 * ────────────────────────────────────────────────────────────────── */

/* Override the two extra production divisors the yield evaluators use on the
 * mode-flag-set path (retail config scalars set once by the defaults init
 * 0x4983b7 — the values baked in as this module's defaults):
 *   divisor_std    DAT_00553538 = 10  (engine_army_economy_yield_standard, s_slot_0053f25c != 0)
 *   divisor_phase4 DAT_00553218 = 3   (engine_army_economy_yield_phase4,    s_slot_0053f25c != 0)
 * The mode-flag-clear path of both uses the shared divisor_lo (DAT_0057d34c =
 * 2) from engine_army_economy_set_production_tuning.  Same single-source-of-
 * truth rationale as engine_army_economy_set_prices. */
void engine_army_economy_set_yield_divisors(int divisor_std, int divisor_phase4);

/* Port of FUN_0044d15a (139 B).  Output achievable at intensity `level` under a
 * resource `cap`, the phase-2/3/default evaluator: `level * divisor` (divisor =
 * divisor_lo or divisor_std by the saved mode flag s_slot_0053f25c), capped by
 * the army's capacity-scaled `cap` (record +0x201/+0x202 ratio), then scaled up
 * by the army's +0x208 yield-bonus percentage (halved), and floored at 0.  0 if
 * the army-record block is unbound. */
int engine_army_economy_yield_standard(int key, int level, int cap);

/* Port of FUN_0044d1e5 (156 B).  As yield_standard but the phase-4 evaluator:
 * divisor is divisor_lo or divisor_phase4; the capacity-scaled `cap` is floored
 * at 0 first; when the mode flag is set `level` is halved before scaling; and
 * the cap binds with `<=` (vs yield_standard's `<`).  No yield-bonus pass.  0 if
 * the army-record block is unbound. */
int engine_army_economy_yield_phase4(int key, int level, int cap);

/* Port of FUN_0044d374 (665 B).  Plan army `key`'s production level for the
 * given `phase`: gated on record byte +0xb8, sweep candidate intensity levels
 * 0..(+0x24 feed-count) with the phase-selected evaluator to find the best, then
 * write the output at the stored reference level (+0xc4) to +0x230 (and, for
 * phase 4 / phase 2-3, the matching yield to +0x248 / +0x2fc), the best/worst
 * level indices to +0xc8 / +0xcc, and a net-balance figure to +0x22c (signed by
 * phase).  +0x22c is cleared up front so an ungated army reads 0.  No-ops when
 * the army-record block is unbound. */
void engine_army_economy_plan_production(int key, int phase);

/* ──────────────────────────────────────────────────────────────────
 * Per-army seasonal production turn pass — FUN_0044c8ae, the per-army loop
 * the turn-advance driver 0x448440 runs between the dev-spread pass (0x44bf7a)
 * and the army recount (0x44d60d).  Reuses the three yield evaluators and the
 * production planner above.
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_0044cf6f (114 B).  Classify a scaled production figure `q`
 * against a per-army denominator `denom` into a 4-level rating: 2 when either
 * is < 1, else 3 / 7 / 0xb as q/denom falls in [0,0x29) / [0x29,0x51) /
 * [0x51,∞).  The lone caller is the seasonal pass below. */
int engine_army_economy_rate_class(int q, int denom);

/* Port of FUN_0044c8ae's per-army step.  For army `key` and the just-ended
 * `season` (1..4): snapshot the running provision figure (+0x224 → +0x228),
 * draw down the per-turn consumption (+0x18c) and apply the signed interest
 * rate (+0x1fc %, stored to +0x278), then add this season's production —
 * production_output for season 1, yield_standard for 2/3, yield_phase4 for 4 —
 * adjusted by the model-type byte +0x21b, recording the season output (+0x240/
 * +0x244/+0x248), its delta vs the unadjusted yield (+0x24c) and, on season 1,
 * the capacity-ratio pair (+0x202/+0x206) from the fallback latch +0x1a7.
 * Finally classify the result (engine_army_economy_rate_class), retype the
 * realm's band-[2,0xe] cells to that rating via engine_territory_retype_army_
 * band, and plan next season's production (engine_army_economy_plan_production,
 * `next_season`).  No-op when the army-record block is unbound. */
void engine_army_economy_season_update(int key, int season, int next_season);

/* Port of FUN_0044c8ae (1729 B).  The turn-pass loop wrapper: run
 * engine_army_economy_season_update over armies 1..s_slot_0056d5dc with the
 * saved season pair (just-ended s_slot_0057c934, next s_slot_0057c92c).  The
 * diff-harness entry point. */
void engine_army_economy_apply_season_pass(void);

/* ──────────────────────────────────────────────────────────────────
 * Secondary-settlement reserve pass — the 0x450e46 branch of 0x4485a5.
 * Both functions are gated on the realm's secondary-settlement flag (+0x1c3,
 * the byte engine_territory_stamp_secondary_keep also reads) and work three
 * (value, capacity) reserve pairs at +0x1cc.. / +0x1d8...  Pair→commodity
 * mapping is provisional.
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00450fb4 (173 B).  The minimum remaining-capacity percentage
 * across the secondary settlement's reserve pairs 1 and 2: min(100 −
 * fill%(+0x1d0/+0x1dc), 100 − fill%(+0x1d4/+0x1e0)), where fill% is value*100/
 * capacity (0 if the capacity is 0).  Returns 0 when the realm has no secondary
 * settlement, or the army-record block is unbound. */
int engine_army_economy_secondary_headroom(int key);

/* Port of FUN_00450e46 (366 B).  Plan the secondary settlement's reserve: clear
 * the selection sentinel +0xec (always -1) and surplus +0xf0 (0), and — when the
 * realm has a secondary settlement — set +0xf0 to pair-0's value (+0x1cc) if the
 * reserves are full (headroom == 100) else 0, and the turns counter +0x1a6 to
 * ceil(pair-0 / per-turn-rate +0xe8), or a flat 100 when the reserves are not
 * full or the rate is < 1.  No-op when the army-record block is unbound. */
void engine_army_economy_plan_secondary(int key);

/* ──────────────────────────────────────────────────────────────────
 * Candidate cost/revenue evaluator + selector — the 0x44dd4d branch of
 * 0x4485a5.  The evaluator writes a (cost, revenue) pair into a shared scratch
 * pair (retail globals DAT_0055cd6c / DAT_005656f0); the selector sweeps the
 * candidate intensity and picks a level by the net (revenue − cost).
 * ────────────────────────────────────────────────────────────────── */

/* The scratch pair the evaluator last wrote (retail DAT_0055cd6c /
 * DAT_005656f0).  Exposed for tests and for the eventual FUN_0044d60d port,
 * which is the third sharer of the same two globals. */
int engine_army_economy_eval_cost(void);
/* Set the eval scratch pair (retail DAT_0055cd6c / DAT_005656f0) — a host seam: the
 * in-process verifier (tools/rhost/rh_hooks_ai.c) loads retail's values before a port call. */
void engine_army_economy_set_eval_scratch(int cost, int revenue);
int engine_army_economy_eval_revenue(void);

/* Port of FUN_0044da99 (692 B).  Value a candidate for army `key` at a gold
 * `budget` and intensity `level`, writing its cost/revenue into the scratch
 * pair above.  With no settlement-tier cells (census +0x200 == 0) the cost is a
 * flat `budget` (or `budget/2` for budget >= 6) and revenue 0; otherwise a
 * per-`category` model (codes 10/0x14/0x1e/else select weight 1/3/5/7 and
 * revenue base 1400/900/500/200) scales both by the level's fill ratio, with a
 * small-budget over-fill bonus, phase-1/4 ×1.5 tweaks, a round-up-to-1 rule, and
 * a cap of the cost at the budget.  Both outputs are 0 when budget == 0 or the
 * army-record block is unbound. */
void engine_army_economy_eval_candidate(int key, int budget, int level,
                                        int category, int phase);

/* Port of FUN_0044dd4d (530 B).  Sweep candidate intensities 0..(+0x24
 * feed-count) for army `key`, evaluating each with engine_army_economy_eval_
 * candidate at budget = stock-A(+0x250) − buy-B(+0x17c) and the army's category
 * (+0x25c), and select a level: the first that breaks even (net = revenue −
 * cost >= 0), else the least-negative one.  Records the selected level (+0xd4),
 * the max-net level (+0xd8), and — from a re-eval at the stored reference level
 * (+0xd0) — the final revenue (+0x268), cost (+0x26c) and net (+0x258, less
 * buy-B).  Gated on byte +0xb8; +0x258 is cleared up front.  No-op when the
 * army-record block is unbound. */
void engine_army_economy_select_candidate(int key, int phase);

/* Port of FUN_0044d60d (774 B, void) — the per-turn stock-A settle, a callee of
 * the turn-advance engine (0x448440) and the third sharer of the g_eval_cost /
 * g_eval_revenue scratch pair (after eval_candidate / select_candidate).  For
 * every realm 1..s_slot_0056d5dc it:
 *   1. snapshots stock A (+0x250 → +0x254) and draws this turn's consumption
 *      (+0x190) off the stockpile;
 *   2. books the base production cost/revenue at the current level via
 *      engine_army_economy_eval_candidate (current season s_slot_0057c934);
 *   3. layers on the model-type rate (k_stock_a_rate[+0x21b], % of the stock)
 *      and the per-realm signed trade rate (+0x1fd; the sentinel 99 disables
 *      trade and zeroes the revenue), accumulating each into cost or revenue;
 *   4. applies the net (revenue − cost) to the stockpile, floored at 0, and
 *      records the revenue (+0x260) and net-change ledger (+0x264);
 *   5. regrades the realm's feature cells (engine_territory_grade_feature_cells,
 *      FUN_0044d913) and re-plans next turn (engine_army_economy_select_candidate
 *      at the map season s_slot_0057c92c).
 * No-op when the army-record block is unbound.  Touches only g_per_army_768 and
 * (through the regrade) the overworld; field semantics are provisional, offsets
 * + the rate table r2-verified. */
void engine_army_economy_settle_stock_a(void);

/* ──────────────────────────────────────────────────────────────────
 * Storage/supply distribution — the 0x44c278 branch of 0x4485a5.
 *
 * Distributes a realm's per-turn supply budget (record +0xdc) across its up-to-
 * 20 developed feature-cells, working from the fullest.  A cell is "developed"
 * iff its roster entry (retail DAT_0053ea00, the same per-key cell roster
 * engine_territory_rebuild_cell_roster fills) is non-zero AND the cell-grid type
 * byte it points at (retail DAT_00522f90) is > 0x18.  Each cell's current fill
 * 0..800 lives in the per-army record's i16 score sub-view at +0x90 (retail
 * DAT_0053fa40, the AI score table).
 *
 * These three need the cell roster and cell grid bound in addition to the army
 * records (bind below).  Commodity semantics of the budget/fill are provisional.
 * ────────────────────────────────────────────────────────────────── */

/* Bind the per-key cell roster (retail DAT_0053ea00 = save entry 12, 17 rows ×
 * 20 i32) and the 64×64×8 cell descriptor grid (retail DAT_00522f90).  The same
 * memory engine_territory binds; the development helpers read both read-only to
 * test the "developed feature-cell" predicate, while the resource pass stamps
 * cell type bytes through the grid.  NULL on either → the functions that need
 * them are no-ops. */
void engine_army_economy_bind_cell_roster(const int32_t *roster);
void engine_army_economy_bind_cells(uint8_t *cells);

/* Port of FUN_0044c5fc (200 B).  Record the realm's fullest developed-cell slot
 * index (argmax of the +0x90 fill) into +0x210, or 99 when the realm has no
 * developed cell.  The 99 sentinel is what gates the distribution in
 * engine_army_economy_distribute_stock.  No-op when the army records / roster /
 * cell grid are unbound. */
void engine_army_economy_seed_fullest_cell(int key);

/* Port of FUN_0044c53b (193 B).  As engine_army_economy_seed_fullest_cell but
 * the no-developed-cell default is 0, not 99.  Re-run inside distribute_stock
 * once the 99-gate has already proven a developed cell exists (so the default
 * never bites) — a faithful redundant recompute; see engine-quirks. */
void engine_army_economy_pick_fullest_cell(int key);

/* Port of FUN_0044c278 (707 B).  Plan the realm's supply distribution: seed the
 * fullest developed cell (+0x210), sum each developed cell's capped headroom
 * min(800 − fill, 200) into +0xe4, then — when a developed cell exists — greedily
 * walk the cells from the fullest, depositing the budget (+0xdc) ≤ 200 per cell
 * and counting how many top out at the 800 cap into +0x20c, and finally store a
 * turns-to-fill-the-fullest estimate ceil(headroom_top / min(budget, 200)) into
 * +0x214.  Also clears a selection sentinel +0xe0 to −1.  Returns an int the
 * lone caller (0x4485a5) discards; ported faithfully anyway.  No-op (returns 0)
 * when the army records / roster / cell grid are unbound. */
int engine_army_economy_distribute_stock(int key);

/* ──────────────────────────────────────────────────────────────────
 * Per-realm resource-production planner — the FUN_004485a5 orchestrator
 * (called per-realm from the army/country setup pass 0x49bd99, the new-game
 * country build 0x43a997, and ~40 other per-turn sites) and its 0x44f318
 * branch with the two leaves it rests on.
 *
 * Each realm has four "resource slots" (0..3) — a 4-entry array of 0x18-byte
 * records at record +0x294 (retail DAT_0053fc44).  Within a slot record:
 *   +0x00 byte   target_pct  (DAT_0053fc44)  plan_resource writes the supply
 *                                            target; +0x01/+0x02/+0x03 are the
 *   +0x01 byte   flag_a      (DAT_0053fc45)  three model gate flags resource_
 *   +0x02 byte   flag_b      (DAT_0053fc46)  quota reads
 *   +0x03 byte   flag_c      (DAT_0053fc47)  (also the top gate)
 *   +0x08 sbyte  base_pct    (DAT_0053fc4c)  supply_target's additive base
 *   +0x0a i16    cap         (DAT_0053fc4e)  supply_target's reference cap
 *   +0x14 i32    quota       (DAT_0053fc58)  plan_resource's OUTPUT
 * The four slots are planned by 0x4485a5 with (slot, rec_idx, target, divisor)
 * = (1,4,0xf,1), (3,5,0xf,2), (0,6,0x14,1), (2,7,0xf,4); `rec_idx` selects one
 * of the eight 0xc-byte job-slot records at +0xc4 (retail DAT_0053fa74 — the
 * same array engine_territory's FUN_0044f6e7 fills, WF_COUNTER) whose +0x00
 * assigned-count is the production level fed to supply_target.
 * ────────────────────────────────────────────────────────────────── */

/* Bind the per-group resource-demand pool table (retail DAT_0057c020 — a
 * runtime, zero-initialised .data table of 0x160-byte records indexed by the
 * realm's +0x05 group id).  resource_quota's slot-2 path reads four i32 demand
 * fields out of the keyed record: +0x00/+0x04 (commodity-B demand, flag 0 / !=0)
 * and +0x10/+0x14 (commodity-A demand).  NULL → the slot-2 path treats every
 * demand as 0 (so the quota collapses to 0); the other slots never touch it. */
void engine_army_economy_bind_resource_pool(uint8_t *pool);

/* The supply-pool totals resource_quota's slot-2 path last summed (retail
 * scalars DAT_0057c904 / DAT_0056d628).  Recomputed by every slot-2
 * resource_quota call via engine_army_economy_sum_supply_pools; exposed for
 * tests.  Each is floored at 1. */
int engine_army_economy_supply_pool_a(void);
int engine_army_economy_supply_pool_b(void);
/* Set both pools (retail DAT_0057c904 / DAT_0056d628) — the same host seam. */
void engine_army_economy_set_supply_pools(int pool_a, int pool_b);

/* Port of FUN_0044f15b (237 B).  Sum the two per-race production multipliers
 * (the baked 6-entry race table, retail DAT_004d8990 / DAT_004d8994) across
 * every other army (index 1..s_slot_0056d5dc) that shares `group` (record +0x05),
 * has its resource-slot-2 active flag set (record +0x2c7) and a positive slot-2
 * job count (record +0x118 = job-slot record 7's count), into the two supply-pool
 * totals above (each floored at 1).  No-op (leaves both pools at 1) when the
 * army-record block is unbound. */
void engine_army_economy_sum_supply_pools(int group);

/* Port of FUN_0044ef4e (525 B).  The per-army resource quota for resource `slot`
 * of `key`, gated on the slot's flag_c.  For slot 2 it sums the realm's supply
 * pools (sum_supply_pools(`group`)) and returns this army's race-weighted share
 * of the group demand pool (`flag` picks the 0 / !=0 demand column), the min of
 * the A- and B-commodity shares.  For other slots it returns 999 when the slot's
 * flag_a is set and flag_b clear, else 0.  Returns 0 when the army-record block
 * is unbound. */
int engine_army_economy_resource_quota(int key, int slot, int group, int flag);

/* Port of FUN_0044f248 (208 B).  The supply target percentage for resource
 * `slot` of `key` at production `amount`, starting from `base_pct`: returns a
 * flat 80 when the mode flag s_slot_0053f25c is clear, 0 when `amount` is 0,
 * otherwise the slot's stored base byte (+0x08, signed) plus `base_pct` (scaled
 * down by cap/amount when the slot cap +0x0a is below `amount`), clamped to
 * [base_pct, 100].  0 when the army-record block is unbound. */
int engine_army_economy_supply_target(int key, int slot, int amount, int base_pct);

/* Port of FUN_0044f318 (897 B).  Plan resource `slot` of realm `key`: clear the
 * slot's quota and the two slot-2 production scratch fields (+0x280/+0x284), seed
 * the secondary-settlement carry for slot 0/3 (+0x288/+0x28c), reset job-slot
 * record `rec_idx`, then — when the realm's group (+0x05) is set, its slot quota
 * is positive and it has job slots (gate byte +0xb8, which doubles as the sweep
 * step) — sweep production levels 0..feed (record +0x24) to find the one whose
 * `supply_target × percent(level/divisor)` is highest (capped by the quota),
 * store that slot's target byte and final quota, and for slot 2 also the
 * race-weighted production amounts (+0x280/+0x284) and the best level.  `target`
 * and `divisor` are the per-slot constants from the 0x4485a5 call table.  No-op
 * when the army-record block is unbound. */
void engine_army_economy_plan_resource(int key, int slot, int rec_idx,
                                       int target, int divisor);

/* Port of FUN_004485a5 (163 B).  The per-realm economy orchestrator: run the
 * terrain census (engine_territory_census_army_terrain), the storage/supply
 * distribution (distribute_stock), the production-level planner (plan_production)
 * and candidate selector (select_candidate) for `phase`, the four resource-slot
 * plans (plan_resource) with the fixed (slot, rec_idx, target, divisor) table,
 * and the secondary-settlement reserve pass (plan_secondary).  `phase` is the
 * saved game-mode code (retail DAT_0057c92c).  No-op when the army-record block
 * is unbound (each callee guards itself). */
void engine_army_economy_plan_realm(int key, int phase);

/* Port of FUN_0044edc2 (396 B).  Stamp resource `slot`'s overworld cell — the
 * descriptor at the slot's +0x04 byte offset into the bound cell grid — with a
 * per-slot type code (a per-slot constant {1→1,3→4,2→7,0→0xa} bumped by 1 when
 * the slot is at its top tier, flag_c set), and, when the slot did not grow this
 * turn (current +0x0c − previous +0x10 < 1), a per-slot value {1→0x1e,3→0,0→0x14,
 * 2→0xa} into the cell's +3 byte.  Gated on the slot being live (flag_a +0x01 set,
 * flag_b +0x02 clear).  No-op when the army-record or cell grid is unbound. */
void engine_army_economy_stamp_resource_cell(int key, int slot);

/* Port of FUN_0044ea92 (816 B).  Deliver resource `slot`'s production for realm
 * `key`: snapshot the running total (+0x0c) into the previous-turn field (+0x10),
 * then — when the slot is live (flag_b +0x02 clear) and the realm's group (+0x05)
 * is set — credit this turn's supplied amount (the smaller of the level-scaled
 * supply target and the delivery quota resource_quota(`group`,flag=1)) into the
 * running total and the owning group's demand pool.  Slot 1/3/0 add to the B/C/A
 * commodity accumulator; slot 2 adds to the per-race accumulator (+0x40) and
 * debits A/B by the race weights, then resyncs the owner's cashflow total
 * (engine_ranking_recompute_player_total).  When the slot is depleting (flag_b
 * > 0) it instead zeroes the running total, ticks the countdown and re-stamps the
 * cell once it expires.  `target`/`divisor` are the per-slot constants and
 * `rec_idx` the job-slot record selector from the 0x44e852 call table.  No-op
 * when the army-record block is unbound. */
void engine_army_economy_apply_resource_quota(int key, int slot, int rec_idx,
                                              int target, int divisor);

/* Port of FUN_0044e852 (576 B).  The per-turn resource-delivery pass: snapshot
 * each group pool's three commodity accumulators (B/C/A) into their prior-turn
 * companions (groups 1..5), then for every realm 1..s_slot_0056d5dc deliver slot
 * 2 (apply_resource_quota → stamp_resource_cell → plan_resource), followed by
 * slots 1/3/0 (all four re-planned).  Each slot's cap (+0x0a) is refreshed from
 * its job-slot count afterwards (slot 2←job 7, 1←4, 3←5, 0←6).  No-op when the
 * army-record block is unbound. */
void engine_army_economy_resource_pass(void);

#endif /* OPENLORDS2_ENGINE_ARMY_ECONOMY_H */
