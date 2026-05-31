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
    /* provisioning-decision fields (FUN_0044df5f) */
    DISPLAY_IDX     = 0x11,
    FEED_COUNT      = 0x24,
    PROVISION_SCALED = 0xc4,
    SUPPLY_LEVEL    = 0x15d,
    SUPPLY_LEVEL_PREV = 0x15e,
    BC_SPLIT_PCT    = 0x15f,
    VAL_A           = 0x16c,
    VAL_C           = 0x170,
    VAL_B           = 0x174,
    BUY_C           = 0x178,
    BUY_B           = 0x17c,
    PROD_STORED     = 0x188,
    OVERHEAD_HI     = 0x198,
    OVERHEAD_LO     = 0x19c,
    PROVISION_RAW   = 0x224,
};

_Alignas(int32_t) static uint8_t g_army[ARMY_ROWS * ARMY_STRIDE];

static void army_reset(void)
{
    memset(g_army, 0, sizeof g_army);
    engine_army_economy_bind_army_records(g_army);
    engine_army_economy_set_prices(5, 10, 6);   /* retail FUN_004983b7 defaults */
    engine_army_economy_set_production_tuning(12, 10, 2, 5);
    s_slot_0053f25c = 0;                          /* divisor_lo path */
    s_slot_0053f260 = 0;                          /* no flat overhead */
}

static void set_stock(int key, int off, int32_t v)
{
    *(int32_t *)(g_army + key * ARMY_STRIDE + off) = v;
}

static int32_t rec_i32(int key, int off) { return *(int32_t *)(g_army + key * ARMY_STRIDE + off); }
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

/* ── provisioning decision (FUN_0044df5f) ───────────────────────────── */

/* Full two-pass scenario, phase != 4 (no home production):
 *   stockA=1, raw need=100, feed=40, split=50%, prev level=5 → init 6.
 *   supply-cost table (feed=40): L6=50 L5=120 L4=80 L3=40 L2=20 L1=10 L0=0.
 *   stockpile value = 1*5 + 1*10 (B<-A) + 100*6 = 615.
 *   pass1: init 6 → dec to 5, cost 120<=615 → level 5.
 *     net=120-stockA_value(5)=115; B-gold=50%*115=57 → ceil/10=6 units
 *     capped to STOCK_B(1)=1; C-gold rest 58 → ceil/6=10 units cap 100=10.
 *   pass2 budget = 1*10 + 10*6 + 5 = 75:
 *     L5 120>75 → 4 (80>75) → 3 (40<=75) → level 3.
 *     net=40-5=35; B-gold=17 → 2 units cap 1 =1; C rest 18 → 3 units =3.
 *   finals: VAL_A=1*5, VAL_B=1*10, VAL_C=3*6, display=3*3-8=1. */
int test_army_economy_provision_full_two_pass(void)
{
    army_reset();
    set_stock(2, STOCK_A, 1);
    set_stock(2, PROVISION_RAW, 100);
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + FEED_COUNT) = 40;
    set_byte(2, BC_SPLIT_PCT, 50);
    set_byte(2, SUPPLY_LEVEL_PREV, 5);

    engine_army_economy_provision(2, 0);

    T_ASSERT_EQ_I(rec_byte(2, SUPPLY_LEVEL), 3);
    T_ASSERT_EQ_I(rec_i32(2, BUY_B), 1);
    T_ASSERT_EQ_I(rec_i32(2, BUY_C), 3);
    T_ASSERT_EQ_I(rec_i32(2, VAL_A), 1 * 5);
    T_ASSERT_EQ_I(rec_i32(2, VAL_B), 1 * 10);
    T_ASSERT_EQ_I(rec_i32(2, VAL_C), 3 * 6);
    T_ASSERT_EQ_I(rec_i32(2, PROD_STORED), 0);
    T_ASSERT_EQ_I((int8_t)rec_byte(2, DISPLAY_IDX), 1);
    /* C-need and B-cap fields written up front. */
    T_ASSERT_EQ_I(rec_i32(2, STOCK_C), 100);
    T_ASSERT_EQ_I(rec_i32(2, STOCK_B), 1);
    return 0;
}

/* phase==4 folds home production into the commodity-C need and stores it.
 *   rate=3, scaled cap=1000, raw cap=100, divisor_lo path → output 30.
 *   STOCK_C = raw(100) - 30 = 70, PROD_STORED = 30. */
int test_army_economy_provision_mode4_folds_production(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 3);
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + PROVISION_SCALED) = 1000;
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + PROVISION_RAW)    = 100;

    engine_army_economy_provision(2, 4);

    T_ASSERT_EQ_I(rec_i32(2, PROD_STORED), 30);
    T_ASSERT_EQ_I(rec_i32(2, STOCK_C), 70);
    return 0;
}

/* phase != 4 leaves production untouched (PROD_STORED 0, STOCK_C == raw). */
int test_army_economy_provision_non_mode4_skips_production(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 3);
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + PROVISION_SCALED) = 1000;
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + PROVISION_RAW)    = 100;

    engine_army_economy_provision(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, PROD_STORED), 0);
    T_ASSERT_EQ_I(rec_i32(2, STOCK_C), 100);
    return 0;
}

/* The level clamp of 6 reaches the 7th table pair (100,50) — an over-read of
 * the real 6-level ladder into the adjacent reciprocal-% table.  prev=6 → init
 * 7 → dec to 6; feed=200 → cost = ceil(200/100)*50 = 100.  Huge stockpile makes
 * both passes settle at level 6, proving that pair is live.
 *   net=100, split=0 → all to C: ceil(100/6)=17 units, cap huge → BUY_C=17.
 *   display = 6*3-8 = 10. */
int test_army_economy_provision_level_six_over_read(void)
{
    army_reset();
    set_stock(2, PROVISION_RAW, 1000);    /* STOCK_C = 1000, big budget */
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + FEED_COUNT) = 200;
    set_byte(2, BC_SPLIT_PCT, 0);
    set_byte(2, SUPPLY_LEVEL_PREV, 6);

    engine_army_economy_provision(2, 0);

    T_ASSERT_EQ_I(rec_byte(2, SUPPLY_LEVEL), 6);
    T_ASSERT_EQ_I(rec_i32(2, BUY_C), 17);
    T_ASSERT_EQ_I(rec_i32(2, BUY_B), 0);
    T_ASSERT_EQ_I((int8_t)rec_byte(2, DISPLAY_IDX), 10);
    return 0;
}

/* The flat-overhead flag s_slot_0053f260 adds (OVERHEAD_LO+OVERHEAD_HI) to
 * every supply-cost, which can ratchet the settled level lower.  Same base as
 * the full scenario but with overhead 1000 and a tiny stockpile, the level
 * floors at 0 (cost 0+overhead vs budget). */
int test_army_economy_provision_overhead_flag(void)
{
    army_reset();
    s_slot_0053f260 = 1;
    set_stock(2, STOCK_A, 1);
    set_stock(2, PROVISION_RAW, 0);       /* STOCK_C = 0 → small budget */
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + FEED_COUNT) = 40;
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + OVERHEAD_LO) = 1000;
    set_byte(2, SUPPLY_LEVEL_PREV, 5);

    engine_army_economy_provision(2, 0);

    /* stockpile value = 1*5 + 1*10 + 0 = 15; even level-0 cost = 0+1000 > 15,
     * so pass-1 ratchets all the way to 0 and clamps there. */
    T_ASSERT_EQ_I(rec_byte(2, SUPPLY_LEVEL), 0);
    T_ASSERT_EQ_I((int8_t)rec_byte(2, DISPLAY_IDX), -8);   /* 0*3 - 8 */
    return 0;
}

/* Writes are confined to the keyed row. */
int test_army_economy_provision_keyed_by_row(void)
{
    army_reset();
    set_stock(4, STOCK_A, 2);
    set_stock(4, PROVISION_RAW, 50);
    set_byte(4, SUPPLY_LEVEL_PREV, 3);

    engine_army_economy_provision(4, 0);

    /* row 4 touched, neighbour rows untouched. */
    T_ASSERT_EQ_I(rec_i32(4, VAL_A), 2 * 5);
    T_ASSERT_EQ_I(rec_i32(3, VAL_A), 0);
    T_ASSERT_EQ_I(rec_i32(5, VAL_A), 0);
    return 0;
}

/* Unbound block → no-op, no crash. */
int test_army_economy_provision_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_provision(2, 4);
    return 0;
}
