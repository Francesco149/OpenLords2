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

/* Per-army record row geometry (shared with engine_territory). */
#define ARMY_ROW_STRIDE  0x300
#define ARMY_STOCK_A     0x250   /* DAT_0053fc00 */
#define ARMY_STOCK_B     0x184   /* DAT_0053fb34 */
#define ARMY_STOCK_C     0x180   /* DAT_0053fb30 */

static uint8_t *g_army_records;

/* Retail 0x4983b7 defaults: A=5 (DAT_00553f60), B=10 (DAT_00567594),
 * C=6 (DAT_0057cb30). */
static int g_price_a = 5;
static int g_price_b = 10;
static int g_price_c = 6;

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
