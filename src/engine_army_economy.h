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

#endif /* OPENLORDS2_ENGINE_ARMY_ECONOMY_H */
