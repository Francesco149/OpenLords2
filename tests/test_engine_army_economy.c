/*
 * Unit tests for engine_army_economy — per-army stockpile valuation +
 * budget→units helpers (ports of FUN_0044e5bb / e7b4 / e603 / e653).
 */

#include "t.h"
#include "engine_army_economy.h"
#include "save_schema_globals.h"   /* s_slot_0053f25c — mode flag */

#include <string.h>

enum {
    ARMY_ROWS     = 17,
    ARMY_STRIDE   = 0x300,
    STOCK_A       = 0x250,
    STOCK_B       = 0x184,
    STOCK_C       = 0x180,
    PROD_RATE     = 0x201,
    PROD_FALLBACK = 0x1a7,
};

_Alignas(int32_t) static uint8_t g_army[ARMY_ROWS * ARMY_STRIDE];

static void army_reset(void)
{
    memset(g_army, 0, sizeof g_army);
    engine_army_economy_bind_army_records(g_army);
    engine_army_economy_set_prices(5, 10, 6);   /* retail FUN_004983b7 defaults */
    engine_army_economy_set_production_tuning(12, 10, 2, 5);
    s_slot_0053f25c = 0;                          /* divisor_lo path */
}

static void set_stock(int key, int off, int32_t v)
{
    *(int32_t *)(g_army + key * ARMY_STRIDE + off) = v;
}

static uint8_t rec_byte(int key, int off) { return g_army[key * ARMY_STRIDE + off]; }
static void set_byte(int key, int off, uint8_t v) { g_army[key * ARMY_STRIDE + off] = v; }

/* ── stock A value (FUN_0044e5bb) ───────────────────────────────────── */

int test_army_economy_stock_a_value_multiplies_by_price(void)
{
    army_reset();
    set_stock(3, STOCK_A, 7);
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(3), 7 * 5);
    return 0;
}

int test_army_economy_stock_a_value_zero_when_nonpositive(void)
{
    army_reset();
    set_stock(3, STOCK_A, 0);
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(3), 0);
    set_stock(3, STOCK_A, -4);                  /* 0 < field is false → 0 */
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(3), 0);
    return 0;
}

int test_army_economy_stock_a_value_ignores_other_fields(void)
{
    army_reset();
    set_stock(3, STOCK_A, 2);
    set_stock(3, STOCK_B, 100);                 /* must not leak in */
    set_stock(3, STOCK_C, 100);
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(3), 2 * 5);
    return 0;
}

int test_army_economy_stock_a_value_keyed_by_row(void)
{
    army_reset();
    set_stock(1, STOCK_A, 4);
    set_stock(9, STOCK_A, 11);
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(1), 4 * 5);
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(9), 11 * 5);
    return 0;
}

int test_army_economy_stock_a_value_unbound_is_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(3), 0);
    return 0;
}

/* ── total stockpile value (FUN_0044e7b4) ───────────────────────────── */

int test_army_economy_stockpile_value_sums_three_terms(void)
{
    army_reset();
    set_stock(4, STOCK_A, 3);                   /* 3*5  = 15 */
    set_stock(4, STOCK_B, 2);                   /* 2*10 = 20 */
    set_stock(4, STOCK_C, 5);                   /* 5*6  = 30 */
    T_ASSERT_EQ_I(engine_army_economy_stockpile_value(4), 15 + 20 + 30);
    return 0;
}

int test_army_economy_stockpile_value_skips_nonpositive_terms(void)
{
    army_reset();
    set_stock(4, STOCK_A, 3);                   /* 15 */
    set_stock(4, STOCK_B, 0);                   /* skipped */
    set_stock(4, STOCK_C, -9);                  /* skipped */
    T_ASSERT_EQ_I(engine_army_economy_stockpile_value(4), 15);
    return 0;
}

int test_army_economy_stockpile_value_matches_stock_a_when_bc_zero(void)
{
    army_reset();
    set_stock(4, STOCK_A, 8);
    T_ASSERT_EQ_I(engine_army_economy_stockpile_value(4),
                  engine_army_economy_stock_a_value(4));
    return 0;
}

int test_army_economy_stockpile_value_unbound_is_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_stockpile_value(4), 0);
    return 0;
}

/* ── budget → units (FUN_0044e603 / e653) ───────────────────────────── */

int test_army_economy_units_b_ceil_divides_by_price(void)
{
    army_reset();
    T_ASSERT_EQ_I(engine_army_economy_units_b(0, 20), 2);   /* 20/10 exact   */
    T_ASSERT_EQ_I(engine_army_economy_units_b(0, 21), 3);   /* 21/10 → ceil  */
    T_ASSERT_EQ_I(engine_army_economy_units_b(0, 1),  1);   /* 1/10  → 1     */
    return 0;
}

int test_army_economy_units_c_ceil_divides_by_price(void)
{
    army_reset();
    T_ASSERT_EQ_I(engine_army_economy_units_c(0, 12), 2);   /* 12/6 exact */
    T_ASSERT_EQ_I(engine_army_economy_units_c(0, 13), 3);   /* 13/6 ceil  */
    return 0;
}

int test_army_economy_units_zero_when_cost_nonpositive(void)
{
    army_reset();
    T_ASSERT_EQ_I(engine_army_economy_units_b(0, 0),   0);
    T_ASSERT_EQ_I(engine_army_economy_units_b(0, -5),  0);
    T_ASSERT_EQ_I(engine_army_economy_units_c(0, 0),   0);
    T_ASSERT_EQ_I(engine_army_economy_units_c(0, -5),  0);
    return 0;
}

int test_army_economy_units_ignore_key_arg(void)
{
    army_reset();
    T_ASSERT_EQ_I(engine_army_economy_units_b(0,  37),
                  engine_army_economy_units_b(99, 37));
    T_ASSERT_EQ_I(engine_army_economy_units_c(0,  37),
                  engine_army_economy_units_c(99, 37));
    return 0;
}

int test_army_economy_units_track_price_override(void)
{
    army_reset();
    engine_army_economy_set_prices(5, 7, 3);
    T_ASSERT_EQ_I(engine_army_economy_units_b(0, 14), 2);   /* 14/7 */
    T_ASSERT_EQ_I(engine_army_economy_units_b(0, 15), 3);   /* ceil */
    T_ASSERT_EQ_I(engine_army_economy_units_c(0, 9),  3);   /* 9/3  */
    T_ASSERT_EQ_I(engine_army_economy_units_c(0, 10), 4);   /* ceil */
    return 0;
}

int test_army_economy_value_tracks_price_override(void)
{
    army_reset();
    engine_army_economy_set_prices(2, 3, 4);
    set_stock(6, STOCK_A, 10);                  /* 10*2 = 20 */
    set_stock(6, STOCK_B, 10);                  /* 10*3 = 30 */
    set_stock(6, STOCK_C, 10);                  /* 10*4 = 40 */
    T_ASSERT_EQ_I(engine_army_economy_stock_a_value(6), 20);
    T_ASSERT_EQ_I(engine_army_economy_stockpile_value(6), 20 + 30 + 40);
    return 0;
}

/* ── production-level solver (FUN_0044cfe1) ──────────────────────────── */
/* defaults: scale 12, max_level 10, divisor 2 (mode flag 0).  output = rate
 * (record +0x201) × level; pick the highest level (10..1) with output<=raw and
 * (12*output)/2 = 6*output <= scaled. */

int test_army_economy_production_picks_highest_fitting_level(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 3);                  /* rate 3 */
    /* raw 100 admits 3L<=100 (L<=33); scaled 100 admits 18L<=100 (L<=5).
     * Highest fitting level = 5 → output 15. */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 100, 100), 15);
    T_ASSERT_EQ_I(rec_byte(2, PROD_FALLBACK), 0);   /* no fallback */
    return 0;
}

int test_army_economy_production_raw_cap_binds(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 3);
    /* scaled effectively unbounded; raw 16 admits 3L<=16 → L<=5 → output 15. */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 100000, 16), 15);
    T_ASSERT_EQ_I(rec_byte(2, PROD_FALLBACK), 0);
    return 0;
}

int test_army_economy_production_scaled_cap_binds(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 3);
    /* raw unbounded; scaled 72 admits 18L<=72 → L<=4 → output 12. */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 72, 100000), 12);
    T_ASSERT_EQ_I(rec_byte(2, PROD_FALLBACK), 0);
    return 0;
}

int test_army_economy_production_falls_back_to_flat_level(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 200);                /* rate so big every 200L overflows raw */
    /* raw 50: 200L<=50 fails for all L>=1 → fallback to flat level: level<=50 &&
     * 6*level<=10000 → level 10. output 10, fallback latched. */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 10000, 50), 10);
    T_ASSERT_EQ_I(rec_byte(2, PROD_FALLBACK), 1);
    return 0;
}

int test_army_economy_production_nothing_fits_returns_zero(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 200);
    /* raw 5 too small for any rate-scaled output AND for any flat level
     * (level<=5 ok but 6*level<=1 fails for level>=1) → 0, fallback cleared. */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 1, 5), 0);
    T_ASSERT_EQ_I(rec_byte(2, PROD_FALLBACK), 0);
    return 0;
}

int test_army_economy_production_zero_rate_returns_zero(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 0);                  /* output = 0 fits immediately */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 100, 100), 0);
    T_ASSERT_EQ_I(rec_byte(2, PROD_FALLBACK), 0);
    return 0;
}

int test_army_economy_production_nonpositive_cap_leaves_record(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 3);
    set_byte(2, PROD_FALLBACK, 0xaa);           /* sentinel must survive */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 0, 100), 0);
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 100, 0), 0);
    T_ASSERT_EQ_I(rec_byte(2, PROD_FALLBACK), 0xaa);
    return 0;
}

int test_army_economy_production_unbound_is_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 100, 100), 0);
    return 0;
}

int test_army_economy_production_mode_flag_selects_divisor(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 3);
    /* mode flag 0 → divisor 2: 18L<=54 → L<=3 → output 9. */
    s_slot_0053f25c = 0;
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 54, 100000), 9);
    /* mode flag 1 → divisor 5: (36L)/5<=54 → 36L<=270 → L<=7 → output 21. */
    s_slot_0053f25c = 1;
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 54, 100000), 21);
    return 0;
}

int test_army_economy_production_tuning_override(void)
{
    army_reset();
    engine_army_economy_set_production_tuning(12, 3, 2, 5);   /* cap level ladder at 3 */
    set_byte(2, PROD_RATE, 1);
    /* large caps → highest level is the ladder top 3 → output 3. */
    T_ASSERT_EQ_I(engine_army_economy_production_output(2, 100000, 100000), 3);
    return 0;
}
