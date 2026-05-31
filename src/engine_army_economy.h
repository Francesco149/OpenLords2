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

#endif /* OPENLORDS2_ENGINE_ARMY_ECONOMY_H */
