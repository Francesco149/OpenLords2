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
    /* production-level planner fields (FUN_0044d374 + evaluators) */
    PLAN_GATE       = 0xb8,
    PLAN_REF_LEVEL  = 0xc4,   /* == PROVISION_SCALED */
    PLAN_BEST       = 0xc8,
    PLAN_BEST2      = 0xcc,
    PROD_RATE_DENOM = 0x202,
    YIELD_BONUS     = 0x208,
    PLAN_NET        = 0x22c,
    PLAN_OUT        = 0x230,
    PLAN_CAP        = 0x244,
    PLAN_OUT_P4     = 0x248,
    PLAN_OUT_STD    = 0x2fc,
    /* secondary-settlement reserve pass (FUN_00450e46 / 0fb4) */
    SEC_RATE        = 0xe8,
    SEC_SEL         = 0xec,
    SEC_SURPLUS     = 0xf0,
    SEC_TURNS       = 0x1a6,
    SEC_FLAG        = 0x1c3,
    SEC_VAL0        = 0x1cc,
    SEC_VAL1        = 0x1d0,
    SEC_VAL2        = 0x1d4,
    SEC_CAP0        = 0x1d8,
    SEC_CAP1        = 0x1dc,
    SEC_CAP2        = 0x1e0,
    /* seasonal production turn pass (FUN_0044c8ae) */
    PROVISION_RAW_PREV = 0x228,
    PROVISION_DRAW     = 0x18c,
    PROVISION_INTEREST = 0x278,
    PROVISION_RATE_PCT = 0x1fc,
    SEASON_OUT         = 0x240,
    SEASON_TYPE        = 0x21b,
    SEASON_DELTA       = 0x24c,
    RATE_CLASS_DENOM   = 0x206,
    /* candidate evaluator/selector (FUN_0044da99 / dd4d) */
    CENSUS_HIGH     = 0x200,
    EVAL_REF        = 0xd0,
    EVAL_SEL        = 0xd4,
    EVAL_MAXLVL     = 0xd8,
    EVAL_NET        = 0x258,
    EVAL_CATEGORY   = 0x25c,
    EVAL_REVENUE    = 0x268,
    EVAL_COST       = 0x26c,
};

_Alignas(int32_t) static uint8_t g_army[ARMY_ROWS * ARMY_STRIDE];

static void army_reset(void)
{
    memset(g_army, 0, sizeof g_army);
    engine_army_economy_bind_army_records(g_army);
    engine_army_economy_set_prices(5, 10, 6);   /* retail FUN_004983b7 defaults */
    engine_army_economy_set_production_tuning(12, 10, 2, 5);
    engine_army_economy_set_yield_divisors(10, 3);  /* retail FUN_004983b7 defaults */
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

/* ── yield evaluators (FUN_0044d15a / d1e5) ──────────────────────────── */
/* defaults: divisor_lo 2 (mode 0), divisor_std 10, divisor_phase4 3.  With
 * +0x201/+0x202/+0x208 all zero, the capacity clamp and yield bonus are no-ops,
 * so the base value is level*divisor capped by `cap`. */

int test_army_economy_yield_standard_base(void)
{
    army_reset();
    /* level 4 × divisor_lo 2 = 8, cap 1000 doesn't bind, no bonus → 8. */
    T_ASSERT_EQ_I(engine_army_economy_yield_standard(2, 4, 1000), 8);
    return 0;
}

int test_army_economy_yield_standard_cap_binds(void)
{
    army_reset();
    /* level 10 × 2 = 20, cap 15 < 20 → 15. */
    T_ASSERT_EQ_I(engine_army_economy_yield_standard(2, 10, 15), 15);
    return 0;
}

int test_army_economy_yield_standard_capacity_ratio(void)
{
    army_reset();
    set_byte(2, PROD_RATE, 2);                  /* numerator */
    set_byte(2, PROD_RATE_DENOM, 4);            /* denominator → ratio 50% */
    /* cap 100 scaled by 2/4 = 50; level 40 × 2 = 80 > 50 → 50. */
    T_ASSERT_EQ_I(engine_army_economy_yield_standard(2, 40, 100), 50);
    return 0;
}

int test_army_economy_yield_standard_bonus(void)
{
    army_reset();
    set_stock(2, YIELD_BONUS, 40);              /* bonus 40 → halved 20% */
    /* base level 10 × 2 = 20; +20% = 20 + 4 = 24. */
    T_ASSERT_EQ_I(engine_army_economy_yield_standard(2, 10, 1000), 24);
    return 0;
}

int test_army_economy_yield_standard_mode_divisor(void)
{
    army_reset();
    s_slot_0053f25c = 0;                        /* divisor_lo 2 */
    T_ASSERT_EQ_I(engine_army_economy_yield_standard(2, 3, 1000), 6);
    s_slot_0053f25c = 1;                        /* divisor_std 10 */
    T_ASSERT_EQ_I(engine_army_economy_yield_standard(2, 3, 1000), 30);
    return 0;
}

int test_army_economy_yield_standard_unbound_is_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_yield_standard(2, 4, 1000), 0);
    return 0;
}

int test_army_economy_yield_phase4_base(void)
{
    army_reset();
    /* level 5 × divisor_lo 2 = 10, cap 100 doesn't bind → 10. */
    T_ASSERT_EQ_I(engine_army_economy_yield_phase4(2, 5, 100), 10);
    return 0;
}

int test_army_economy_yield_phase4_cap_binds(void)
{
    army_reset();
    /* level 5 × 2 = 10, cap 8 <= 10 → 8. */
    T_ASSERT_EQ_I(engine_army_economy_yield_phase4(2, 5, 8), 8);
    return 0;
}

int test_army_economy_yield_phase4_mode_halves_level(void)
{
    army_reset();
    s_slot_0053f25c = 1;                        /* divisor_phase4 3, level halved */
    /* level 6 → /2 = 3, × 3 = 9, cap large → 9. */
    T_ASSERT_EQ_I(engine_army_economy_yield_phase4(2, 6, 1000), 9);
    return 0;
}

int test_army_economy_yield_phase4_negative_cap_floored(void)
{
    army_reset();
    /* cap -5 floors to 0; 0 <= level*2 → 0. */
    T_ASSERT_EQ_I(engine_army_economy_yield_phase4(2, 5, -5), 0);
    return 0;
}

int test_army_economy_yield_phase4_unbound_is_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_yield_phase4(2, 5, 100), 0);
    return 0;
}

/* ── production-level planner (FUN_0044d374) ─────────────────────────── */

/* Default phase (evaluator = yield_standard).  count 5 → sweep levels 0..4,
 * yield = level*2 (no caps/bonus), best at level 4.  Reference level fed to
 * production_output gives 30 (rate 3, big caps); PLAN_NET = -buy_c. */
int test_army_economy_plan_default_phase(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_stock(2, FEED_COUNT, 5);
    set_stock(2, BUY_C, 7);
    set_stock(2, PLAN_CAP, 1000);
    set_stock(2, PLAN_REF_LEVEL, 1000);
    set_stock(2, PROVISION_RAW, 1000);
    set_byte(2, PROD_RATE, 3);

    engine_army_economy_plan_production(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_BEST), 4);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_BEST2), 4);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT), 30);         /* production_output(ref) */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_NET), -7);         /* -buy_c */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT_STD), 0);      /* not phase 2/3 */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT_P4), 0);       /* not phase 4 */
    return 0;
}

/* Phase 1 (evaluator = production_output).  rate 1, raw 10 → sweep output grows
 * stepwise with the scaled cap = level: levels >=6 admit output 1, >=12 admit 2,
 * so over levels 0..12 the best is level 12.  Reference (scaled 60, raw 10)
 * gives 10; PLAN_NET = -PLAN_OUT - buy_c. */
int test_army_economy_plan_phase1(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_stock(2, FEED_COUNT, 13);             /* sweep levels 0..12 */
    set_stock(2, BUY_C, 0);
    set_stock(2, PROVISION_RAW, 10);
    set_stock(2, PLAN_REF_LEVEL, 60);
    set_byte(2, PROD_RATE, 1);

    engine_army_economy_plan_production(2, 1);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_BEST), 12);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT), 10);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_NET), -10);        /* -PLAN_OUT - buy_c */
    return 0;
}

/* Phase 4 (evaluator = yield_phase4, stores PLAN_OUT_P4, nets it).  rate 0 so
 * production_output (PLAN_OUT) is 0; yields are level*2.  Reference level 3 →
 * yield_phase4 = 6; PLAN_NET = 6 - buy_c. */
int test_army_economy_plan_phase4(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_stock(2, FEED_COUNT, 5);              /* sweep 0..4, best level 4 */
    set_stock(2, BUY_C, 2);
    set_stock(2, PLAN_CAP, 1000);
    set_stock(2, PLAN_REF_LEVEL, 3);
    set_byte(2, PROD_RATE, 0);                /* PLAN_OUT → 0 */

    engine_army_economy_plan_production(2, 4);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_BEST), 4);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT), 0);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT_P4), 6);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_NET), 4);          /* PLAN_OUT_P4 - buy_c */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT_STD), 0);
    return 0;
}

/* Phase 2 writes PLAN_OUT_STD (yield_standard at the reference level) and nets
 * just -buy_c. */
int test_army_economy_plan_phase2_stores_std(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_stock(2, FEED_COUNT, 5);
    set_stock(2, BUY_C, 7);
    set_stock(2, PLAN_CAP, 1000);
    set_stock(2, PLAN_REF_LEVEL, 3);
    set_byte(2, PROD_RATE, 0);

    engine_army_economy_plan_production(2, 2);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT_STD), 6);      /* yield_standard(ref=3) = 6 */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_NET), -7);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT_P4), 0);
    return 0;
}

/* When no swept level beats the initial threshold (here count 1 → only level 0,
 * yielding 0), best stays -1 and best2 stays 0. */
int test_army_economy_plan_no_level_beats_threshold(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_stock(2, FEED_COUNT, 1);              /* only level 0 → yield 0 */
    set_stock(2, PLAN_CAP, 1000);

    engine_army_economy_plan_production(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_BEST), -1);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_BEST2), 0);
    return 0;
}

/* Gate byte +0xb8 clear → only PLAN_NET is cleared; the result slots are left
 * untouched. */
int test_army_economy_plan_gate_off_clears_net_only(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 0);
    set_stock(2, PLAN_NET, 1234);
    set_stock(2, PLAN_BEST, 55);
    set_stock(2, PLAN_OUT, 66);

    engine_army_economy_plan_production(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_NET), 0);          /* cleared */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_BEST), 55);        /* untouched */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT), 66);         /* untouched */
    return 0;
}

/* Writes confined to the keyed row. */
int test_army_economy_plan_keyed_by_row(void)
{
    army_reset();
    set_byte(4, PLAN_GATE, 1);
    set_stock(4, FEED_COUNT, 5);
    set_stock(4, PLAN_CAP, 1000);
    set_stock(4, PLAN_REF_LEVEL, 3);

    engine_army_economy_plan_production(4, 0);

    T_ASSERT_EQ_I(rec_i32(4, PLAN_BEST), 4);
    T_ASSERT_EQ_I(rec_i32(3, PLAN_BEST), 0);         /* neighbour rows untouched */
    T_ASSERT_EQ_I(rec_i32(5, PLAN_BEST), 0);
    return 0;
}

int test_army_economy_plan_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_plan_production(2, 0);
    return 0;
}

/* ── secondary-settlement reserve pass (FUN_00450e46 / 0fb4) ─────────── */

/* headroom = min(100 - fill%(pair1), 100 - fill%(pair2)).  pair1 50/100 → 50%
 * fill → 50 headroom; pair2 60/100 → 40 headroom → min = 40. */
int test_army_economy_secondary_headroom_min_of_two(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 1);
    set_stock(2, SEC_VAL1, 50);  set_stock(2, SEC_CAP1, 100);
    set_stock(2, SEC_VAL2, 60);  set_stock(2, SEC_CAP2, 100);
    T_ASSERT_EQ_I(engine_army_economy_secondary_headroom(2), 40);
    return 0;
}

int test_army_economy_secondary_headroom_gate_off_zero(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 0);
    set_stock(2, SEC_VAL1, 50);  set_stock(2, SEC_CAP1, 100);
    T_ASSERT_EQ_I(engine_army_economy_secondary_headroom(2), 0);
    return 0;
}

/* Zero capacity → percent_div guards to 0 → full headroom 100. */
int test_army_economy_secondary_headroom_zero_capacity(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 1);     /* all caps 0 → both fills 0 → 100 */
    T_ASSERT_EQ_I(engine_army_economy_secondary_headroom(2), 100);
    return 0;
}

int test_army_economy_secondary_headroom_unbound_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_secondary_headroom(2), 0);
    return 0;
}

/* Full reserves (headroom 100): surplus = pair-0 value, turns = ceil(val0/rate). */
int test_army_economy_plan_secondary_full_reserves(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 1);     /* caps 0 → headroom 100 */
    set_stock(2, SEC_VAL0, 50);
    set_stock(2, SEC_RATE, 10);
    engine_army_economy_plan_secondary(2);
    T_ASSERT_EQ_I(rec_i32(2, SEC_SEL), -1);
    T_ASSERT_EQ_I(rec_i32(2, SEC_SURPLUS), 50);
    T_ASSERT_EQ_I(rec_byte(2, SEC_TURNS), 5);   /* 50/10 exact */
    return 0;
}

/* ceil rounding: 55/10 → 6 turns. */
int test_army_economy_plan_secondary_turns_ceil(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 1);
    set_stock(2, SEC_VAL0, 55);
    set_stock(2, SEC_RATE, 10);
    engine_army_economy_plan_secondary(2);
    T_ASSERT_EQ_I(rec_byte(2, SEC_TURNS), 6);
    return 0;
}

/* Not full (headroom < 100): surplus forced to 0, turns flat 100. */
int test_army_economy_plan_secondary_partial(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 1);
    set_stock(2, SEC_VAL1, 50);  set_stock(2, SEC_CAP1, 100);   /* fill 50 → headroom 50 */
    set_stock(2, SEC_VAL0, 50);
    set_stock(2, SEC_RATE, 10);
    engine_army_economy_plan_secondary(2);
    T_ASSERT_EQ_I(rec_i32(2, SEC_SURPLUS), 0);
    T_ASSERT_EQ_I(rec_byte(2, SEC_TURNS), 100);
    return 0;
}

/* Full but rate < 1 → turns flat 100, surplus still set. */
int test_army_economy_plan_secondary_zero_rate(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 1);
    set_stock(2, SEC_VAL0, 50);
    set_stock(2, SEC_RATE, 0);
    engine_army_economy_plan_secondary(2);
    T_ASSERT_EQ_I(rec_i32(2, SEC_SURPLUS), 50);
    T_ASSERT_EQ_I(rec_byte(2, SEC_TURNS), 100);
    return 0;
}

/* Gate off: only sentinel +0xec and surplus +0xf0 are cleared; turns untouched. */
int test_army_economy_plan_secondary_gate_off(void)
{
    army_reset();
    set_byte(2, SEC_FLAG, 0);
    set_byte(2, SEC_TURNS, 0x55);
    set_stock(2, SEC_SURPLUS, 999);
    engine_army_economy_plan_secondary(2);
    T_ASSERT_EQ_I(rec_i32(2, SEC_SEL), -1);
    T_ASSERT_EQ_I(rec_i32(2, SEC_SURPLUS), 0);
    T_ASSERT_EQ_I(rec_byte(2, SEC_TURNS), 0x55);   /* untouched */
    return 0;
}

int test_army_economy_plan_secondary_keyed_by_row(void)
{
    army_reset();
    set_byte(4, SEC_FLAG, 1);
    set_stock(4, SEC_VAL0, 30);
    set_stock(4, SEC_RATE, 10);
    engine_army_economy_plan_secondary(4);
    T_ASSERT_EQ_I(rec_i32(4, SEC_SURPLUS), 30);
    T_ASSERT_EQ_I(rec_i32(3, SEC_SURPLUS), 0);     /* neighbour rows untouched */
    T_ASSERT_EQ_I(rec_i32(5, SEC_SURPLUS), 0);
    return 0;
}

int test_army_economy_plan_secondary_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_plan_secondary(2);
    return 0;
}

/* ── candidate evaluator (FUN_0044da99) ─────────────────────────────── */
/* All expected values cross-checked against a Python transcription of the
 * decompile (truncating C division throughout). */

/* census +0x200 == 0 → flat cost (budget, or budget/2 for budget>=6), no revenue. */
int test_army_economy_eval_simple_cost(void)
{
    army_reset();
    engine_army_economy_eval_candidate(2, 4, 0, 10, 0);   /* budget 4 < 6 */
    T_ASSERT_EQ_I(engine_army_economy_eval_cost(), 4);
    T_ASSERT_EQ_I(engine_army_economy_eval_revenue(), 0);
    engine_army_economy_eval_candidate(2, 10, 0, 10, 0);  /* budget 10 → /2 */
    T_ASSERT_EQ_I(engine_army_economy_eval_cost(), 5);
    return 0;
}

int test_army_economy_eval_zero_budget(void)
{
    army_reset();
    set_byte(2, CENSUS_HIGH, 1);
    engine_army_economy_eval_candidate(2, 0, 5, 10, 0);
    T_ASSERT_EQ_I(engine_army_economy_eval_cost(), 0);
    T_ASSERT_EQ_I(engine_army_economy_eval_revenue(), 0);
    return 0;
}

/* model path (census +0x200 != 0), category 10, budget 4. */
int test_army_economy_eval_model_low_level(void)
{
    army_reset();
    set_byte(2, CENSUS_HIGH, 1);
    engine_army_economy_eval_candidate(2, 4, 0, 10, 0);   /* fill 0 → cost 1, rev 0 */
    T_ASSERT_EQ_I(engine_army_economy_eval_cost(), 1);
    T_ASSERT_EQ_I(engine_army_economy_eval_revenue(), 0);
    return 0;
}

int test_army_economy_eval_model_full_fill(void)
{
    army_reset();
    set_byte(2, CENSUS_HIGH, 1);
    /* level 12, budget 4 → fill 100, over-fill bonus → rev 4, cost 0. */
    engine_army_economy_eval_candidate(2, 4, 12, 10, 0);
    T_ASSERT_EQ_I(engine_army_economy_eval_cost(), 0);
    T_ASSERT_EQ_I(engine_army_economy_eval_revenue(), 4);
    return 0;
}

int test_army_economy_eval_phase1_boosts_revenue(void)
{
    army_reset();
    set_byte(2, CENSUS_HIGH, 1);
    /* same as full-fill but phase 1 → revenue ×3/2 = 6. */
    engine_army_economy_eval_candidate(2, 4, 12, 10, 1);
    T_ASSERT_EQ_I(engine_army_economy_eval_revenue(), 6);
    return 0;
}

int test_army_economy_eval_rounds_revenue_up_to_one(void)
{
    army_reset();
    set_byte(2, CENSUS_HIGH, 1);
    /* level 1, budget 4 → tiny base but non-zero → revenue floored up to 1. */
    engine_army_economy_eval_candidate(2, 4, 1, 10, 0);
    T_ASSERT_EQ_I(engine_army_economy_eval_cost(), 1);
    T_ASSERT_EQ_I(engine_army_economy_eval_revenue(), 1);
    return 0;
}

int test_army_economy_eval_unbound_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_eval_candidate(2, 4, 12, 10, 0);
    T_ASSERT_EQ_I(engine_army_economy_eval_cost(), 0);
    T_ASSERT_EQ_I(engine_army_economy_eval_revenue(), 0);
    return 0;
}

/* ── candidate selector (FUN_0044dd4d) ──────────────────────────────── */

/* Simple-cost path: revenue 0, cost constant across levels → every net equal
 * and negative, so the selector lands on level 0 (least-negative == first). */
int test_army_economy_select_simple_path(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_stock(2, FEED_COUNT, 3);
    set_stock(2, STOCK_A, 10);
    set_stock(2, BUY_B, 2);                 /* budget 8 → cost 4 */
    /* CENSUS_HIGH 0 (default) → simple path; EVAL_REF 0 */

    engine_army_economy_select_candidate(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, EVAL_REVENUE), 0);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_COST), 4);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_NET), -6);    /* (0-4) - buy_b 2 */
    T_ASSERT_EQ_I(rec_i32(2, EVAL_SEL), 0);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_MAXLVL), 0);
    return 0;
}

/* Empty sweep (count 0): no level evaluated → selected stays -1, max stays the
 * 999999 sentinel; the reference re-eval still fills the figures. */
int test_army_economy_select_empty_sweep(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_stock(2, FEED_COUNT, 0);
    set_stock(2, STOCK_A, 10);
    set_stock(2, BUY_B, 2);

    engine_army_economy_select_candidate(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, EVAL_SEL), -1);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_MAXLVL), 999999);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_COST), 4);
    return 0;
}

/* Model path with a varying net: nets over levels 0..12 are
 * [-1,0,0,0,1,1,1,1,1,1,1,1,4] → first break-even at level 1 (selected),
 * max net at level 12.  Reference level 12 → revenue 4, cost 0. */
int test_army_economy_select_model_picks_first_breakeven(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 1);
    set_byte(2, CENSUS_HIGH, 1);
    set_stock(2, FEED_COUNT, 13);
    set_stock(2, STOCK_A, 4);
    set_stock(2, BUY_B, 0);                 /* budget 4 */
    set_stock(2, EVAL_CATEGORY, 10);
    set_stock(2, EVAL_REF, 12);

    engine_army_economy_select_candidate(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, EVAL_SEL), 1);         /* first net >= 0 */
    T_ASSERT_EQ_I(rec_i32(2, EVAL_MAXLVL), 12);     /* argmax net */
    T_ASSERT_EQ_I(rec_i32(2, EVAL_REVENUE), 4);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_COST), 0);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_NET), 4);         /* (4-0) - buy_b 0 */
    return 0;
}

/* Gate off → only EVAL_NET is cleared; the selection slots are untouched. */
int test_army_economy_select_gate_off(void)
{
    army_reset();
    set_byte(2, PLAN_GATE, 0);
    set_stock(2, EVAL_NET, 1234);
    set_stock(2, EVAL_SEL, 77);

    engine_army_economy_select_candidate(2, 0);

    T_ASSERT_EQ_I(rec_i32(2, EVAL_NET), 0);
    T_ASSERT_EQ_I(rec_i32(2, EVAL_SEL), 77);
    return 0;
}

int test_army_economy_select_keyed_by_row(void)
{
    army_reset();
    set_byte(4, PLAN_GATE, 1);
    set_stock(4, FEED_COUNT, 3);
    set_stock(4, STOCK_A, 10);
    set_stock(4, BUY_B, 2);

    engine_army_economy_select_candidate(4, 0);

    T_ASSERT_EQ_I(rec_i32(4, EVAL_COST), 4);
    T_ASSERT_EQ_I(rec_i32(3, EVAL_COST), 0);    /* neighbour rows untouched */
    T_ASSERT_EQ_I(rec_i32(5, EVAL_COST), 0);
    return 0;
}

int test_army_economy_select_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_select_candidate(2, 0);
    return 0;
}

/* --------------------------------------------------------------------- *
 *  Per-turn stock-A settle (FUN_0044d60d).  Tests use the simple
 *  eval_candidate path (CENSUS_HIGH == 0 → flat cost = budget/2, no
 *  revenue) so the settle's own rate/trade arithmetic is isolated, with
 *  PLAN_GATE == 0 so the trailing select_candidate is an early no-op.
 * --------------------------------------------------------------------- */
enum {
    STOCK_A_PREV     = 0x254,
    STOCK_A_DRAW     = 0x190,
    STOCK_A_REVENUE  = 0x260,
    STOCK_A_BALANCE  = 0x264,
    STOCK_A_RATE_AMT = 0x270,
    STOCK_A_TRADE_AMT = 0x274,
    STOCK_A_TRADE_RATE = 0x1fd,
};

/* Consumption draw + flat production cost (census 0, neutral model type 3, no
 * trade): stock falls by the draw, then by budget/2 of the post-draw level. */
int test_army_economy_settle_basic_consume(void)
{
    army_reset();
    s_slot_0056d5dc = 1;
    s_slot_0057c934 = 2; s_slot_0057c92c = 2;
    set_stock(1, STOCK_A, 100);
    set_stock(1, STOCK_A_DRAW, 20);
    g_army[1 * ARMY_STRIDE + SEASON_TYPE] = 3;     /* k_rate[3] = 0 */

    engine_army_economy_settle_stock_a();

    /* prev=100, draw→80, cost=40, rev=0 → stock 40 */
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A), 40);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_PREV), 100);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_REVENUE), 0);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_BALANCE), 40);   /* (100-40)+0-20 */
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_RATE_AMT), 0);
    return 0;
}

/* Positive model-type rate (type 2 → +5%) and positive trade rate (+10%) both
 * add to revenue. */
int test_army_economy_settle_positive_rates(void)
{
    army_reset();
    s_slot_0056d5dc = 1;
    s_slot_0057c934 = 2; s_slot_0057c92c = 2;
    set_stock(1, STOCK_A, 200);
    g_army[1 * ARMY_STRIDE + SEASON_TYPE]      = 2;    /* k_rate[2] = 5 */
    g_army[1 * ARMY_STRIDE + STOCK_A_TRADE_RATE] = 10; /* +10% trade */

    engine_army_economy_settle_stock_a();

    /* cost=100, model +5%→10, trade +10%→20 ; rev=30 → stock 200+30-100=130 */
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A), 130);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_TRADE_AMT), 20);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_RATE_AMT), 10);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_REVENUE), 30);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_BALANCE), 100);  /* (200-130)+30 */
    return 0;
}

/* Trade-rate sentinel 99 disables trade and zeroes the revenue + model rate. */
int test_army_economy_settle_trade_disabled(void)
{
    army_reset();
    s_slot_0056d5dc = 1;
    s_slot_0057c934 = 2; s_slot_0057c92c = 2;
    set_stock(1, STOCK_A, 100);
    g_army[1 * ARMY_STRIDE + SEASON_TYPE]      = 2;    /* would be +5% */
    g_army[1 * ARMY_STRIDE + STOCK_A_TRADE_RATE] = 'c'; /* 99 → disabled */

    engine_army_economy_settle_stock_a();

    /* cost=50, rev forced 0, rate forced 0 → stock 100-50=50 */
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A), 50);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_REVENUE), 0);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_RATE_AMT), 0);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_TRADE_AMT), 0);
    return 0;
}

/* Negative model + trade rates can drive the stock below zero → floored to 0,
 * the balance ledger recording the pre-floor shortfall. */
int test_army_economy_settle_negative_floor(void)
{
    army_reset();
    s_slot_0056d5dc = 1;
    s_slot_0057c934 = 2; s_slot_0057c92c = 2;
    set_stock(1, STOCK_A, 10);
    g_army[1 * ARMY_STRIDE + SEASON_TYPE]      = 1;     /* k_rate[1] = -10 */
    g_army[1 * ARMY_STRIDE + STOCK_A_TRADE_RATE] = (uint8_t)(int8_t)-50;

    engine_army_economy_settle_stock_a();

    /* cost=5; trade -50%→+5 cost=10; model -10%→-1→cost=11; stock 10-11=-1→0 */
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A), 0);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_RATE_AMT), -1);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_TRADE_AMT), 5);
    T_ASSERT_EQ_I(rec_i32(1, STOCK_A_BALANCE), -1);   /* pre-floor stock + rev(0) */
    return 0;
}

int test_army_economy_settle_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_settle_stock_a();
    engine_army_economy_bind_army_records(g_army);   /* restore for later tests */
    return 0;
}

/* --------------------------------------------------------------------- *
 *  Storage/supply distribution — FUN_0044c278 / c53b / c5fc.
 *  Expected values cross-checked against a Python transcription of the
 *  decompile (truncating C division throughout).
 * --------------------------------------------------------------------- */

enum {
    SCORE_TABLE   = 0x90,
    DIST_BUDGET   = 0xdc,
    DIST_SEL      = 0xe0,
    DIST_HEADROOM = 0xe4,
    DIST_OVERFLOW = 0x20c,
    DIST_TOPIDX   = 0x210,
    DIST_TURNS    = 0x214,
};

static int32_t g_roster[17 * 20];
static uint8_t g_cellgrid[64 * 64 * 8];

static void dist_reset(void)
{
    army_reset();
    memset(g_roster, 0, sizeof g_roster);
    memset(g_cellgrid, 0, sizeof g_cellgrid);
    engine_army_economy_bind_cell_roster(g_roster);
    engine_army_economy_bind_cells(g_cellgrid);
}

/* Place a cell in realm `key`'s roster slot.  `developed` picks a cell-grid
 * type byte above (25) or below (5) the 0x18 settlement-tier threshold; `fill`
 * is written into the i16 score sub-view.  The roster offset is unique and
 * non-zero so empty slots (offset 0) stay distinct. */
static void set_cell(int key, int slot, int developed, int fill)
{
    int off = (key * 20 + slot + 1) * 8;
    g_roster[key * 20 + slot] = off;
    g_cellgrid[off] = developed ? 25 : 5;
    *(int16_t *)(g_army + key * ARMY_STRIDE + SCORE_TABLE + slot * 2) = (int16_t)fill;
}

int test_army_economy_seed_fullest_cell_argmax(void)
{
    dist_reset();
    set_cell(2, 2, 1, 100);
    set_cell(2, 5, 1, 600);
    set_cell(2, 9, 1, 300);
    engine_army_economy_seed_fullest_cell(2);
    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 5);
    return 0;
}

int test_army_economy_seed_fullest_cell_default_99(void)
{
    dist_reset();
    set_cell(2, 3, 0, 700);   /* present but undeveloped → ignored */
    engine_army_economy_seed_fullest_cell(2);
    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 99);
    return 0;
}

int test_army_economy_pick_fullest_cell_default_0(void)
{
    dist_reset();
    set_cell(2, 3, 0, 700);   /* undeveloped */
    set_byte(2, 0, 0);
    *(int32_t *)(g_army + 2 * ARMY_STRIDE + DIST_TOPIDX) = 17;  /* dirty */
    engine_army_economy_pick_fullest_cell(2);
    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 0);
    return 0;
}

int test_army_economy_distribute_no_developed_cells(void)
{
    dist_reset();
    set_stock(2, DIST_BUDGET, 500);
    int ret = engine_army_economy_distribute_stock(2);

    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 99);
    T_ASSERT_EQ_I(rec_i32(2, DIST_HEADROOM), 0);
    T_ASSERT_EQ_I(rec_i32(2, DIST_OVERFLOW), 0);
    T_ASSERT_EQ_I(rec_i32(2, DIST_TURNS), 0);
    T_ASSERT_EQ_I(rec_i32(2, DIST_SEL), -1);
    T_ASSERT_EQ_I(ret, 2 * 0x100);    /* key*0x100 */
    return 0;
}

int test_army_economy_distribute_basic(void)
{
    dist_reset();
    set_cell(2, 2, 1, 100);
    set_cell(2, 5, 1, 600);
    set_cell(2, 9, 1, 300);
    set_stock(2, DIST_BUDGET, 500);
    int ret = engine_army_economy_distribute_stock(2);

    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 5);
    T_ASSERT_EQ_I(rec_i32(2, DIST_HEADROOM), 600);  /* 200+200+200 */
    T_ASSERT_EQ_I(rec_i32(2, DIST_OVERFLOW), 1);
    T_ASSERT_EQ_I(rec_i32(2, DIST_TURNS), 1);
    T_ASSERT_EQ_I(rec_i32(2, DIST_SEL), -1);
    T_ASSERT_EQ_I(ret, 1);
    return 0;
}

int test_army_economy_distribute_single_cell_ceil(void)
{
    dist_reset();
    set_cell(2, 4, 1, 700);
    set_stock(2, DIST_BUDGET, 50);
    int ret = engine_army_economy_distribute_stock(2);

    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 4);
    T_ASSERT_EQ_I(rec_i32(2, DIST_HEADROOM), 100);  /* 800-700 */
    T_ASSERT_EQ_I(rec_i32(2, DIST_TURNS), 2);       /* ceil(100/50) */
    T_ASSERT_EQ_I(ret, 2);
    return 0;
}

int test_army_economy_distribute_overflow_count(void)
{
    dist_reset();
    set_cell(2, 0, 1, 750);
    set_cell(2, 1, 1, 790);
    set_cell(2, 2, 1, 760);
    set_stock(2, DIST_BUDGET, 1000);
    int ret = engine_army_economy_distribute_stock(2);

    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 1);       /* fill 790 fullest */
    T_ASSERT_EQ_I(rec_i32(2, DIST_OVERFLOW), 3);     /* all three top out */
    T_ASSERT_EQ_I(rec_i32(2, DIST_TURNS), 1);        /* ceil((800-790)/200)=1 */
    T_ASSERT_EQ_I(ret, 2 * 0x100);                   /* remainder != 0 → key*0x100 */
    return 0;
}

int test_army_economy_distribute_zero_budget(void)
{
    dist_reset();
    set_cell(2, 3, 1, 200);
    set_stock(2, DIST_BUDGET, 0);
    int ret = engine_army_economy_distribute_stock(2);

    T_ASSERT_EQ_I(rec_i32(2, DIST_TOPIDX), 3);
    T_ASSERT_EQ_I(rec_i32(2, DIST_HEADROOM), 200);
    T_ASSERT_EQ_I(rec_i32(2, DIST_TURNS), 0);        /* budget 0 → turns untouched */
    T_ASSERT_EQ_I(ret, 600);                          /* headroom_top = 800-200 */
    return 0;
}

int test_army_economy_distribute_keyed_by_row(void)
{
    dist_reset();
    set_cell(4, 5, 1, 600);
    set_stock(4, DIST_BUDGET, 500);
    engine_army_economy_distribute_stock(4);

    T_ASSERT_EQ_I(rec_i32(4, DIST_TOPIDX), 5);
    T_ASSERT_EQ_I(rec_i32(3, DIST_HEADROOM), 0);   /* neighbour rows untouched */
    T_ASSERT_EQ_I(rec_i32(5, DIST_HEADROOM), 0);
    return 0;
}

int test_army_economy_distribute_unbound_noop(void)
{
    dist_reset();
    engine_army_economy_bind_cell_roster(NULL);
    T_ASSERT_EQ_I(engine_army_economy_distribute_stock(2), 0);

    dist_reset();
    engine_army_economy_bind_cells(NULL);
    T_ASSERT_EQ_I(engine_army_economy_distribute_stock(2), 0);

    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_distribute_stock(2), 0);
    return 0;
}

/* --------------------------------------------------------------------- *
 *  Per-realm resource-production planner — FUN_004485a5 / f318 / ef4e /
 *  f248 / f15b.  Expected values hand-traced against the decompile.
 * --------------------------------------------------------------------- */

enum {
    GROUP          = 0x05,
    RACE           = 0x290,
    PLAN_GATE_B    = 0xb8,        /* gate byte (== PLAN_GATE), doubles as sweep step */
    RES_PROD_A     = 0x280,
    RES_PROD_B     = 0x284,
    RES_CARRY0     = 0x288,
    RES_CARRY3     = 0x28c,
    SLOT2_JOBCOUNT = 0x118,       /* == job-slot record 7's count (0xc4 + 7*0xc) */
    RES_ACTIVE2    = 0x2c7,       /* == res-slot 2's flag_c */
    /* resource-slot record field offsets (within the 0x18-byte record) */
    RSF_TARGET     = 0x00,
    RSF_FLAG_A     = 0x01,
    RSF_FLAG_B     = 0x02,
    RSF_FLAG_C     = 0x03,
    RSF_CELL_IDX   = 0x04,
    RSF_BASE_PCT   = 0x08,
    RSF_CAP        = 0x0a,
    RSF_CURRENT    = 0x0c,
    RSF_PREV       = 0x10,
    RSF_QUOTA      = 0x14,
    /* job-slot record field offsets (within the 0xc-byte record) */
    JSF_COUNT      = 0x00,
    JSF_SEL        = 0x04,
    JSF_CAP        = 0x08,
    /* per-group resource-pool record field offsets (within the 0x160 record) */
    RP_B_FLAG0     = 0x00,
    RP_B_FLAG1     = 0x04,
    RP_C_FLAG0     = 0x08,
    RP_C_FLAG1     = 0x0c,
    RP_A_FLAG0     = 0x10,
    RP_A_FLAG1     = 0x14,
    RP_RACE_ACC    = 0x20,
    RP_STRIDE      = 0x160,
};

static int res_off(int slot, int f) { return 0x294 + slot * 0x18 + f; }
static int job_off(int rec, int f)  { return 0xc4 + rec * 0xc + f; }

static void set_i16(int key, int off, int16_t v)
{
    *(int16_t *)(g_army + key * ARMY_STRIDE + off) = v;
}

static uint8_t g_respool[4 * 0x160];

/* Bind a clean army block + resource-pool table; set the army count. */
static void resq_reset(void)
{
    army_reset();
    memset(g_respool, 0, sizeof g_respool);
    engine_army_economy_bind_resource_pool(g_respool);
    s_slot_0053f25c = 1;        /* default to the "real" supply-target path */
    s_slot_0056d5dc = 4;        /* army count (rows 1..4) */
}

static void set_respool(int group, int off, int32_t v)
{
    *(int32_t *)(g_respool + group * RP_STRIDE + off) = v;
}

/* ── supply target (FUN_0044f248) ───────────────────────────────────── */

int test_army_economy_supply_target_mode_flag_flat(void)
{
    resq_reset();
    s_slot_0053f25c = 0;                       /* mode flag clear → flat 80 */
    T_ASSERT_EQ_I(engine_army_economy_supply_target(3, 1, 5, 30), 0x50);
    T_ASSERT_EQ_I(engine_army_economy_supply_target(3, 1, 0, 30), 0x50);  /* even amount 0 */
    return 0;
}

int test_army_economy_supply_target_zero_amount(void)
{
    resq_reset();                              /* f25c = 1 */
    T_ASSERT_EQ_I(engine_army_economy_supply_target(3, 1, 0, 30), 0);
    return 0;
}

int test_army_economy_supply_target_base_plus_byte(void)
{
    resq_reset();
    set_byte(3, res_off(1, RSF_BASE_PCT), 10);  /* base byte */
    set_i16(3, res_off(1, RSF_CAP), 100);       /* cap >= amount → no scaling */
    /* 10 + 20 = 30, within [20,100] */
    T_ASSERT_EQ_I(engine_army_economy_supply_target(3, 1, 5, 20), 30);
    return 0;
}

int test_army_economy_supply_target_cap_scales_then_floors(void)
{
    resq_reset();
    set_byte(3, res_off(1, RSF_BASE_PCT), 10);
    set_i16(3, res_off(1, RSF_CAP), 2);         /* cap 2 < amount 5 → scale */
    /* pct = (2*100)/5 = 40; base = (20*40)/100 = 8; target = 10+8 = 18;
     * clamp to floor (original base_pct 20) → 20 */
    T_ASSERT_EQ_I(engine_army_economy_supply_target(3, 1, 5, 20), 20);
    return 0;
}

int test_army_economy_supply_target_clamps_to_100(void)
{
    resq_reset();
    set_byte(3, res_off(1, RSF_BASE_PCT), 100);
    set_i16(3, res_off(1, RSF_CAP), 100);
    T_ASSERT_EQ_I(engine_army_economy_supply_target(3, 1, 5, 20), 100);  /* 120 → 100 */
    return 0;
}

int test_army_economy_supply_target_unbound_is_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_supply_target(3, 1, 5, 20), 0);
    return 0;
}

/* ── supply pools (FUN_0044f15b) ────────────────────────────────────── */

/* Make army `key` qualify for group `group`'s slot-2 pool with race `race`. */
static void make_pool_member(int key, int group, int race)
{
    set_byte(key, GROUP, (uint8_t)group);
    set_byte(key, RES_ACTIVE2, 1);
    set_stock(key, SLOT2_JOBCOUNT, 5);          /* > 0 */
    set_byte(key, RACE, (uint8_t)race);
}

int test_army_economy_sum_pools_adds_race_columns(void)
{
    resq_reset();
    make_pool_member(1, 1, 0);                  /* race 0 → (6,10) */
    make_pool_member(2, 1, 2);                  /* race 2 → (3,10) */
    engine_army_economy_sum_supply_pools(1);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_a(), 6 + 3);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_b(), 10 + 10);
    return 0;
}

int test_army_economy_sum_pools_filters_non_members(void)
{
    resq_reset();
    make_pool_member(1, 1, 0);                  /* counts: (6,10) */
    make_pool_member(2, 2, 0);                  /* wrong group → excluded */
    make_pool_member(3, 1, 0);
    set_byte(3, RES_ACTIVE2, 0);                /* inactive → excluded */
    make_pool_member(4, 1, 0);
    set_stock(4, SLOT2_JOBCOUNT, 0);            /* no job count → excluded */
    engine_army_economy_sum_supply_pools(1);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_a(), 6);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_b(), 10);
    return 0;
}

int test_army_economy_sum_pools_floor_at_one(void)
{
    resq_reset();                               /* no members at all */
    engine_army_economy_sum_supply_pools(1);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_a(), 1);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_b(), 1);

    make_pool_member(1, 1, 4);                  /* race 4 → (13,0): B sum 0 floored */
    engine_army_economy_sum_supply_pools(1);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_a(), 13);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_b(), 1);
    return 0;
}

int test_army_economy_sum_pools_unbound_floors(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_sum_supply_pools(1);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_a(), 1);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_b(), 1);
    return 0;
}

/* ── resource quota (FUN_0044ef4e) ──────────────────────────────────── */

int test_army_economy_quota_gate_off(void)
{
    resq_reset();
    /* slot 1 flag_c clear → 0 */
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 1, 1, 0), 0);
    return 0;
}

int test_army_economy_quota_nonpool_flag_combo(void)
{
    resq_reset();
    set_byte(3, res_off(1, RSF_FLAG_C), 1);
    set_byte(3, res_off(1, RSF_FLAG_A), 1);
    set_byte(3, res_off(1, RSF_FLAG_B), 0);     /* a set, b clear → 999 */
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 1, 1, 0), 999);

    set_byte(3, res_off(1, RSF_FLAG_B), 1);     /* b set → 0 */
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 1, 1, 0), 0);

    set_byte(3, res_off(1, RSF_FLAG_A), 0);     /* a clear → 0 */
    set_byte(3, res_off(1, RSF_FLAG_B), 0);
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 1, 1, 0), 0);
    return 0;
}

int test_army_economy_quota_slot2_pool_share(void)
{
    resq_reset();
    set_byte(3, GROUP, 1);
    make_pool_member(3, 1, 0);                  /* sole member, race 0 → (6,10) */
    set_byte(3, RES_ACTIVE2, 1);               /* == res-slot-2 flag_c (top gate) */
    set_respool(1, RP_A_FLAG0, 100);
    set_respool(1, RP_B_FLAG0, 200);
    /* share_a = (100*6/6)/6 = 16; share_b = (200*10/10)/10 = 20; min = 16 */
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 2, 1, 0), 16);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_a(), 6);
    T_ASSERT_EQ_I(engine_army_economy_supply_pool_b(), 10);
    return 0;
}

int test_army_economy_quota_slot2_zero_race_column(void)
{
    resq_reset();
    make_pool_member(3, 1, 4);                  /* race 4 → (13,0): B column 0 */
    set_byte(3, RES_ACTIVE2, 1);
    set_respool(1, RP_A_FLAG0, 130);
    set_respool(1, RP_B_FLAG0, 999);            /* ignored: race_b == 0 → share_b stays 999 */
    /* share_a = (130*13/13)/13 = 10; share_b = 999; min = 10 */
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 2, 1, 0), 10);
    return 0;
}

int test_army_economy_quota_slot2_pool_unbound(void)
{
    resq_reset();
    make_pool_member(3, 1, 0);
    set_byte(3, RES_ACTIVE2, 1);
    engine_army_economy_bind_resource_pool(NULL);   /* demands read as 0 → share 0 */
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 2, 1, 0), 0);
    return 0;
}

int test_army_economy_quota_unbound_is_zero(void)
{
    engine_army_economy_bind_army_records(NULL);
    T_ASSERT_EQ_I(engine_army_economy_resource_quota(3, 1, 1, 0), 0);
    return 0;
}

/* ── plan resource (FUN_0044f318) ───────────────────────────────────── */

int test_army_economy_plan_resource_nonpool(void)
{
    resq_reset();
    s_slot_0053f25c = 0;                        /* supply_target → flat 80 */
    set_byte(3, GROUP, 1);
    set_byte(3, PLAN_GATE_B, 1);                /* gate set, step 1 */
    set_stock(3, FEED_COUNT, 3);
    set_stock(3, job_off(4, JSF_COUNT), 2);     /* stored level */
    set_byte(3, res_off(1, RSF_FLAG_C), 1);     /* slot-1 quota → 999 */
    set_byte(3, res_off(1, RSF_FLAG_A), 1);

    engine_army_economy_plan_resource(3, 1, 4, 0x0f, 1);

    /* target byte = supply_target(.,.,stored,.) = 80; quota = percent_of(2/1,80)=1 */
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_TARGET)), 80);
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_QUOTA)), 1);
    T_ASSERT_EQ_I(rec_i32(3, job_off(4, JSF_SEL)), -1);
    T_ASSERT_EQ_I(rec_i32(3, job_off(4, JSF_CAP)), 100000);  /* non-slot-2 sentinel */
    T_ASSERT_EQ_I(rec_i32(3, RES_PROD_A), 0);                /* slot-2-only, untouched */
    return 0;
}

int test_army_economy_plan_resource_group_zero_clears(void)
{
    resq_reset();
    set_stock(3, res_off(1, RSF_QUOTA), 777);   /* pre-existing junk */
    /* group (+0x05) stays 0 → clears and bails before the sweep */
    engine_army_economy_plan_resource(3, 1, 4, 0x0f, 1);
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_QUOTA)), 0);
    T_ASSERT_EQ_I(rec_i32(3, job_off(4, JSF_SEL)), -1);  /* up-front reset still runs */
    T_ASSERT_EQ_I(rec_i32(3, job_off(4, JSF_CAP)), 0);
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_TARGET)), 0);  /* sweep skipped */
    return 0;
}

int test_army_economy_plan_resource_quota_nonpositive_bails(void)
{
    resq_reset();
    set_byte(3, GROUP, 1);
    set_byte(3, PLAN_GATE_B, 1);
    /* slot-1 flags: flag_c set, flag_a clear → quota 0 → bail after clears */
    set_byte(3, res_off(1, RSF_FLAG_C), 1);
    engine_army_economy_plan_resource(3, 1, 4, 0x0f, 1);
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_QUOTA)), 0);
    T_ASSERT_EQ_I(rec_i32(3, job_off(4, JSF_SEL)), -1);
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_TARGET)), 0);
    return 0;
}

int test_army_economy_plan_resource_slot0_secondary_carry(void)
{
    resq_reset();
    set_byte(3, SEC_FLAG, 1);
    set_stock(3, SEC_VAL2, 555);                /* slot 0 carries SEC_VAL2 */
    engine_army_economy_plan_resource(3, 0, 6, 0x14, 1);   /* group 0 → bails, but carry runs first */
    T_ASSERT_EQ_I(rec_i32(3, RES_CARRY0), 555);
    return 0;
}

int test_army_economy_plan_resource_slot2_pool(void)
{
    resq_reset();
    s_slot_0053f25c = 0;                        /* supply_target → flat 80 */
    set_byte(3, GROUP, 1);
    set_byte(3, PLAN_GATE_B, 1);
    set_stock(3, FEED_COUNT, 2);
    make_pool_member(3, 1, 0);                  /* sole member, race 0 → (6,10) */
    set_stock(3, SLOT2_JOBCOUNT, 50);           /* stored level (== job-slot 7 count) */
    set_byte(3, RES_ACTIVE2, 1);               /* slot-2 flag_c */
    set_respool(1, RP_A_FLAG0, 100);            /* quota = 16 (> 0) */
    set_respool(1, RP_B_FLAG0, 200);

    engine_army_economy_plan_resource(3, 2, 7, 0x0f, 4);

    /* quota = percent_of(50/4, 80) = percent_of(12,80) = 9, capped at 16 → 9 */
    T_ASSERT_EQ_I(rec_i32(3, res_off(2, RSF_QUOTA)), 9);
    T_ASSERT_EQ_I(rec_byte(3, res_off(2, RSF_TARGET)), 80);
    T_ASSERT_EQ_I(rec_i32(3, RES_PROD_A), 6 * 9);   /* race col A × quota */
    T_ASSERT_EQ_I(rec_i32(3, RES_PROD_B), 10 * 9);  /* race col B × quota */
    T_ASSERT_EQ_I(rec_i32(3, job_off(7, JSF_CAP)), 0);  /* best level (sweep gave 0) */
    return 0;
}

int test_army_economy_plan_resource_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_plan_resource(3, 1, 4, 0x0f, 1);   /* no crash */
    return 0;
}

/* ── orchestrator (FUN_004485a5) ────────────────────────────────────── */

int test_army_economy_plan_realm_dispatches_all_branches(void)
{
    dist_reset();                               /* binds army + roster + cells */
    engine_army_economy_bind_resource_pool(g_respool);
    memset(g_respool, 0, sizeof g_respool);
    s_slot_0056d5dc = 4;
    /* group 0 everywhere → each plan_resource clears and bails, but the four
     * job-slot resets and every always-run clear still prove the dispatch. */

    engine_army_economy_plan_realm(3, 1);

    T_ASSERT_EQ_I(rec_i32(3, PLAN_NET), 0);          /* plan_production ran */
    T_ASSERT_EQ_I(rec_i32(3, EVAL_NET), 0);          /* select_candidate ran */
    T_ASSERT_EQ_I(rec_i32(3, DIST_SEL), -1);         /* distribute_stock ran */
    T_ASSERT_EQ_I(rec_i32(3, SEC_SEL), -1);          /* plan_secondary ran */
    T_ASSERT_EQ_I(rec_i32(3, job_off(4, JSF_SEL)), -1);  /* plan_resource slot 1 */
    T_ASSERT_EQ_I(rec_i32(3, job_off(5, JSF_SEL)), -1);  /* slot 3 */
    T_ASSERT_EQ_I(rec_i32(3, job_off(6, JSF_SEL)), -1);  /* slot 0 */
    T_ASSERT_EQ_I(rec_i32(3, job_off(7, JSF_SEL)), -1);  /* slot 2 */
    return 0;
}

int test_army_economy_plan_realm_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_plan_realm(3, 1);            /* no crash */
    return 0;
}

/* ── resource-delivery turn pass — FUN_0044e852 / ea92 / edc2 ───────── */

/* stamp_resource_cell (FUN_0044edc2). */
int test_army_economy_stamp_cell_type_and_tag(void)
{
    dist_reset();
    /* slot 1, live (flag_a set, flag_b clear), top tier (flag_c set), stalled. */
    set_byte(3, res_off(1, RSF_FLAG_A), 1);
    set_byte(3, res_off(1, RSF_FLAG_C), 1);
    set_stock(3, res_off(1, RSF_CELL_IDX), 100);
    set_stock(3, res_off(1, RSF_CURRENT), 5);
    set_stock(3, res_off(1, RSF_PREV), 5);          /* cur-prev = 0 < 1 → tag */

    engine_army_economy_stamp_resource_cell(3, 1);

    T_ASSERT_EQ_I(g_cellgrid[100], 1 + 1);          /* slot-1 base 1 + top-tier bump */
    T_ASSERT_EQ_I(g_cellgrid[103], 0x1e);           /* slot-1 stall tag */
    return 0;
}

int test_army_economy_stamp_cell_growth_skips_tag(void)
{
    dist_reset();
    set_byte(3, res_off(2, RSF_FLAG_A), 1);         /* slot 2, flag_c clear */
    set_stock(3, res_off(2, RSF_CELL_IDX), 200);
    set_stock(3, res_off(2, RSF_CURRENT), 10);
    set_stock(3, res_off(2, RSF_PREV), 5);          /* grew → no tag */
    g_cellgrid[203] = 0x77;                         /* sentinel must survive */

    engine_army_economy_stamp_resource_cell(3, 2);

    T_ASSERT_EQ_I(g_cellgrid[200], 7);              /* slot-2 base 7, no top-tier bump */
    T_ASSERT_EQ_I(g_cellgrid[203], 0x77);           /* +3 byte untouched */
    return 0;
}

int test_army_economy_stamp_cell_gated_off(void)
{
    dist_reset();
    set_stock(3, res_off(0, RSF_CELL_IDX), 300);
    g_cellgrid[300] = 0x55;
    /* flag_a clear → no stamp */
    engine_army_economy_stamp_resource_cell(3, 0);
    T_ASSERT_EQ_I(g_cellgrid[300], 0x55);

    /* flag_a set but flag_b set (depleting) → still gated off */
    set_byte(3, res_off(0, RSF_FLAG_A), 1);
    set_byte(3, res_off(0, RSF_FLAG_B), 1);
    engine_army_economy_stamp_resource_cell(3, 0);
    T_ASSERT_EQ_I(g_cellgrid[300], 0x55);
    return 0;
}

/* apply_resource_quota (FUN_0044ea92). */
int test_army_economy_apply_quota_nonpool_slot1(void)
{
    resq_reset();
    s_slot_0053f25c = 0;                            /* supply_target → flat 80 */
    set_byte(3, GROUP, 1);
    set_byte(3, res_off(1, RSF_FLAG_C), 1);         /* quota gate */
    set_byte(3, res_off(1, RSF_FLAG_A), 1);         /* → quota 999 */
    set_stock(3, job_off(4, JSF_COUNT), 10);        /* delivery level */
    set_stock(3, res_off(1, RSF_CURRENT), 5);       /* prior running total */

    engine_army_economy_apply_resource_quota(3, 1, 4, 0x0f, 1);

    /* amount = percent_of(10/1, 80) = 8 (≤ quota 999). */
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_PREV)), 5);     /* snapshot of old current */
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_CURRENT)), 13); /* 5 + 8 */
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_TARGET)), 80);
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_BASE_PCT)), 80);
    T_ASSERT_EQ_I(*(int32_t *)(g_respool + 1 * RP_STRIDE + RP_B_FLAG0), 8);
    T_ASSERT_EQ_I(*(int32_t *)(g_respool + 1 * RP_STRIDE + RP_A_FLAG0), 0);  /* B-only */
    return 0;
}

int test_army_economy_apply_quota_slot2_pool_debits(void)
{
    resq_reset();
    s_slot_0053f25c = 0;
    make_pool_member(3, 1, 0);                      /* group 1, race 0 → (6,10) */
    set_stock(3, SLOT2_JOBCOUNT, 40);               /* == job-slot 7 count (delivery level) */
    set_respool(1, RP_A_FLAG1, 600);               /* prior-turn demand (flag=1 column) */
    set_respool(1, RP_B_FLAG1, 200);

    engine_army_economy_apply_resource_quota(3, 2, 7, 0x0f, 4);

    /* quota = min(B 200/10, A 600/6) = min(20,100) = 20; amount = percent_of(40/4,80)=8. */
    T_ASSERT_EQ_I(rec_i32(3, res_off(2, RSF_CURRENT)), 8);
    T_ASSERT_EQ_I(*(int32_t *)(g_respool + 1 * RP_STRIDE + RP_RACE_ACC + 0 * 4), 8);
    T_ASSERT_EQ_I(*(int32_t *)(g_respool + 1 * RP_STRIDE + RP_A_FLAG0), -8 * 6);  /* debit race col A */
    T_ASSERT_EQ_I(*(int32_t *)(g_respool + 1 * RP_STRIDE + RP_B_FLAG0), -10 * 8); /* debit race col B */
    return 0;
}

int test_army_economy_apply_quota_group_zero_bails_after_snapshot(void)
{
    resq_reset();
    set_stock(3, res_off(1, RSF_CURRENT), 7);       /* group stays 0 */
    engine_army_economy_apply_resource_quota(3, 1, 4, 0x0f, 1);
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_PREV)), 7);     /* snapshot still ran */
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_CURRENT)), 7);  /* delivery skipped */
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_TARGET)), 0);
    return 0;
}

int test_army_economy_apply_quota_depletion_countdown(void)
{
    dist_reset();                                   /* cells bound for the expiry stamp */
    engine_army_economy_bind_resource_pool(g_respool);
    memset(g_respool, 0, sizeof g_respool);
    set_byte(3, res_off(1, RSF_FLAG_A), 1);
    set_byte(3, res_off(1, RSF_FLAG_B), 2);         /* depleting, 2 turns left */
    set_stock(3, res_off(1, RSF_CELL_IDX), 150);
    set_stock(3, res_off(1, RSF_CURRENT), 99);

    /* First tick: countdown 2 → 1, total zeroed, no stamp yet. */
    engine_army_economy_apply_resource_quota(3, 1, 4, 0x0f, 1);
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_FLAG_B)), 1);
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_CURRENT)), 0);
    T_ASSERT_EQ_I(rec_i32(3, res_off(1, RSF_PREV)), 0);
    T_ASSERT_EQ_I(g_cellgrid[150], 0);              /* not stamped */

    /* Second tick: 1 → 0 expires → stamp fires (flag_b now clear). */
    engine_army_economy_apply_resource_quota(3, 1, 4, 0x0f, 1);
    T_ASSERT_EQ_I(rec_byte(3, res_off(1, RSF_FLAG_B)), 0);
    T_ASSERT_EQ_I(g_cellgrid[150], 1);              /* slot-1 base type stamped */
    T_ASSERT_EQ_I(g_cellgrid[153], 0x1e);           /* stalled tag */
    return 0;
}

/* resource_pass (FUN_0044e852). */
static uint8_t g_pass_pool[6 * 0x160];

int test_army_economy_resource_pass_snapshot_and_caps(void)
{
    dist_reset();                                   /* army + cells */
    memset(g_pass_pool, 0, sizeof g_pass_pool);
    engine_army_economy_bind_resource_pool(g_pass_pool);
    s_slot_0056d5dc = 1;                            /* one realm, group 0 → no pool writes */

    /* Pre-load flag-0 accumulators for a couple of groups in the snapshot range. */
    *(int32_t *)(g_pass_pool + 2 * RP_STRIDE + RP_B_FLAG0) = 111;
    *(int32_t *)(g_pass_pool + 2 * RP_STRIDE + RP_C_FLAG0) = 222;
    *(int32_t *)(g_pass_pool + 2 * RP_STRIDE + RP_A_FLAG0) = 333;
    *(int32_t *)(g_pass_pool + 5 * RP_STRIDE + RP_B_FLAG0) = 44;

    /* Job-slot counts feed each slot's cap after planning. */
    set_stock(1, job_off(7, JSF_COUNT), 70);
    set_stock(1, job_off(4, JSF_COUNT), 40);
    set_stock(1, job_off(5, JSF_COUNT), 50);
    set_stock(1, job_off(6, JSF_COUNT), 60);

    engine_army_economy_resource_pass();

    /* Snapshot: flag-0 copied into the prior-turn flag-1 fields, groups 1..5. */
    T_ASSERT_EQ_I(*(int32_t *)(g_pass_pool + 2 * RP_STRIDE + RP_B_FLAG1), 111);
    T_ASSERT_EQ_I(*(int32_t *)(g_pass_pool + 2 * RP_STRIDE + RP_C_FLAG1), 222);
    T_ASSERT_EQ_I(*(int32_t *)(g_pass_pool + 2 * RP_STRIDE + RP_A_FLAG1), 333);
    T_ASSERT_EQ_I(*(int32_t *)(g_pass_pool + 5 * RP_STRIDE + RP_B_FLAG1), 44);

    /* Cap refresh: slot 2←job 7, 1←4, 3←5, 0←6. */
    T_ASSERT_EQ_I(*(int16_t *)(g_army + 1 * ARMY_STRIDE + res_off(2, RSF_CAP)), 70);
    T_ASSERT_EQ_I(*(int16_t *)(g_army + 1 * ARMY_STRIDE + res_off(1, RSF_CAP)), 40);
    T_ASSERT_EQ_I(*(int16_t *)(g_army + 1 * ARMY_STRIDE + res_off(3, RSF_CAP)), 50);
    T_ASSERT_EQ_I(*(int16_t *)(g_army + 1 * ARMY_STRIDE + res_off(0, RSF_CAP)), 60);
    return 0;
}

int test_army_economy_resource_pass_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_bind_resource_pool(NULL);
    engine_army_economy_bind_cells(NULL);
    engine_army_economy_apply_resource_quota(3, 1, 4, 0x0f, 1);  /* no crash */
    engine_army_economy_stamp_resource_cell(3, 1);               /* no crash */
    engine_army_economy_resource_pass();                         /* no crash */
    return 0;
}

/* ── seasonal production turn pass (FUN_0044c8ae) ───────────────────── */

/* rate_class (FUN_0044cf6f) — the q/denom → 2/3/7/0xb classifier. */
int test_army_economy_rate_class_boundaries(void)
{
    army_reset();
    /* non-positive inputs → 2 */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(0, 10), 2);
    T_ASSERT_EQ_I(engine_army_economy_rate_class(-5, 10), 2);
    T_ASSERT_EQ_I(engine_army_economy_rate_class(100, 0), 2);
    T_ASSERT_EQ_I(engine_army_economy_rate_class(100, -1), 2);
    /* q/denom in [0,0x29) → 3 */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(40, 1), 3);      /* 40 < 0x29 */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(0x28, 1), 3);    /* 40 == 0x28 */
    /* q/denom in [0x29,0x51) → 7 */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(0x29, 1), 7);    /* 41 */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(0x50, 1), 7);    /* 80 */
    /* q/denom >= 0x51 → 0xb */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(0x51, 1), 0xb);  /* 81 */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(1000, 1), 0xb);
    /* integer division: 0x52/2 = 0x29 → 7 */
    T_ASSERT_EQ_I(engine_army_economy_rate_class(0x52, 2), 7);
    return 0;
}

/* Season 1 with the evaluator gated to 0 (ref level 0): isolates the
 * front-half (snapshot / drawdown / clamp) and the fallback-denom branch. */
int test_army_economy_season1_fronthalf_fallback_clear(void)
{
    army_reset();
    set_stock(2, PROVISION_RAW, 1000);
    set_stock(2, PROVISION_DRAW, 150);
    set_byte(2, PROVISION_RATE_PCT, 0);          /* no interest */
    set_byte(2, PLAN_REF_LEVEL, 0);              /* production_output → 0 */
    set_byte(2, PROD_FALLBACK, 0);               /* denom = PROD_RATE */
    set_byte(2, PROD_RATE, 7);
    set_byte(2, SEASON_TYPE, 3);                 /* no model adjustment */

    engine_army_economy_season_update(2, 1, 2);

    T_ASSERT_EQ_I(rec_i32(2, PROVISION_RAW_PREV), 1000);   /* snapshot of old +0x224 */
    T_ASSERT_EQ_I(rec_i32(2, PROVISION_INTEREST), 0);
    T_ASSERT_EQ_I(rec_i32(2, SEASON_OUT), 0);              /* gated evaluator */
    T_ASSERT_EQ_I(rec_i32(2, PLAN_CAP), 0);               /* 0 * scale */
    T_ASSERT_EQ_I(rec_i32(2, SEASON_DELTA), 0);
    T_ASSERT_EQ_I(rec_i32(2, PROVISION_RAW), 850);        /* 1000 - 150 drawdown - 0 */
    T_ASSERT_EQ_I(rec_byte(2, PROD_RATE_DENOM), 7);       /* fallback clear → PROD_RATE */
    T_ASSERT_EQ_I(rec_byte(2, RATE_CLASS_DENOM), 7);
    return 0;
}

int test_army_economy_season1_fallback_set_forces_denom_one(void)
{
    army_reset();
    set_stock(2, PROVISION_RAW, 500);
    set_byte(2, PLAN_REF_LEVEL, 0);
    set_byte(2, PROD_FALLBACK, 1);               /* denom forced to 1 */
    set_byte(2, PROD_RATE, 9);

    engine_army_economy_season_update(2, 1, 2);

    T_ASSERT_EQ_I(rec_byte(2, PROD_RATE_DENOM), 1);
    T_ASSERT_EQ_I(rec_byte(2, RATE_CLASS_DENOM), 1);
    return 0;
}

/* Positive interest grows the running figure; the amount is stored at +0x278. */
int test_army_economy_season_interest_positive(void)
{
    army_reset();
    set_stock(2, PROVISION_RAW, 1000);
    set_stock(2, PROVISION_DRAW, 0);
    set_byte(2, PROVISION_RATE_PCT, 10);         /* +10% */
    set_byte(2, PLAN_REF_LEVEL, 0);              /* season-4 evaluator → 0 */

    engine_army_economy_season_update(2, 4, 2);  /* season 4: RAW += OUT_P4 (0) */

    T_ASSERT_EQ_I(rec_i32(2, PROVISION_INTEREST), 100);   /* 10% of 1000 */
    T_ASSERT_EQ_I(rec_i32(2, PROVISION_RAW), 1100);       /* grown, +0 production */
    return 0;
}

/* Negative interest draws down; a draw past 0 clamps the figure to 0. */
int test_army_economy_season_interest_negative_and_clamp(void)
{
    army_reset();
    set_stock(2, PROVISION_RAW, 80);
    set_stock(2, PROVISION_DRAW, 100);           /* 80 - 100 = -20 → clamp 0 before interest */
    set_byte(2, PROVISION_RATE_PCT, (uint8_t)(int8_t)-50);
    set_byte(2, PLAN_REF_LEVEL, 0);

    engine_army_economy_season_update(2, 4, 2);

    /* 80 - 100 = -20; interest computed on -20 → percent_of(-20,50) = -10,
     * stored, RAW -= (-10) → -10; then clamp(<0) → 0; + OUT_P4(0). */
    T_ASSERT_EQ_I(rec_i32(2, PROVISION_INTEREST), -10);
    T_ASSERT_EQ_I(rec_i32(2, PROVISION_RAW), 0);
    return 0;
}

/* Season 4 dispatches yield_phase4 and ADDS the result to the running figure.
 * Twin row proves the dispatched evaluator output without re-deriving it. */
int test_army_economy_season4_dispatch_adds(void)
{
    army_reset();
    /* test row 2 */
    set_stock(2, PROVISION_RAW, 2000);
    set_stock(2, PROVISION_DRAW, 0);
    set_byte(2, PROVISION_RATE_PCT, 0);
    set_byte(2, PLAN_REF_LEVEL, 50);
    set_stock(2, PLAN_CAP, 500);
    set_byte(2, PROD_RATE, 4);
    set_byte(2, PROD_RATE_DENOM, 4);
    set_stock(2, YIELD_BONUS, 0);
    set_byte(2, SEASON_TYPE, 3);                 /* no adjustment → OUT_P4 == yield */
    /* twin row 5 = identical pre-state */
    memcpy(g_army + 5 * ARMY_STRIDE, g_army + 2 * ARMY_STRIDE, ARMY_STRIDE);
    int expected = engine_army_economy_yield_phase4(5, 50, 500);

    engine_army_economy_season_update(2, 4, 2);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_OUT_P4), expected);
    T_ASSERT_EQ_I(rec_i32(2, SEASON_DELTA), 0);              /* type 3 → no change */
    T_ASSERT_EQ_I(rec_i32(2, PROVISION_RAW), 2000 + expected);
    return 0;
}

/* Season 2/3 dispatches yield_standard; model-type 2 scales the cap ×3/2. */
int test_army_economy_season2_type2_scales_cap(void)
{
    army_reset();
    set_byte(2, PLAN_REF_LEVEL, 50);
    set_stock(2, PLAN_CAP, 600);
    set_byte(2, PROD_RATE, 5);
    set_byte(2, PROD_RATE_DENOM, 5);
    set_stock(2, YIELD_BONUS, 0);
    set_byte(2, SEASON_TYPE, 2);                 /* *3/2 */
    memcpy(g_army + 5 * ARMY_STRIDE, g_army + 2 * ARMY_STRIDE, ARMY_STRIDE);
    int raw = engine_army_economy_yield_standard(5, 50, 600);

    engine_army_economy_season_update(2, 3, 2);

    T_ASSERT_EQ_I(rec_i32(2, PLAN_CAP), (raw * 3) / 2);
    T_ASSERT_EQ_I(rec_i32(2, SEASON_DELTA), (raw * 3) / 2 - raw);
    return 0;
}

/* Season 1 dispatches production_output; result × scale lands in PLAN_CAP and
 * the figure is drawn down by the output. */
int test_army_economy_season1_dispatch(void)
{
    army_reset();
    set_stock(2, PROVISION_RAW, 5000);
    set_stock(2, PROVISION_DRAW, 0);
    set_byte(2, PROVISION_RATE_PCT, 0);
    set_byte(2, PLAN_REF_LEVEL, 8);
    set_byte(2, PROD_RATE, 3);
    set_byte(2, SEASON_TYPE, 3);                 /* no adjustment */
    memcpy(g_army + 5 * ARMY_STRIDE, g_army + 2 * ARMY_STRIDE, ARMY_STRIDE);
    int out = engine_army_economy_production_output(5, 8, 5000);

    engine_army_economy_season_update(2, 1, 2);

    T_ASSERT_EQ_I(rec_i32(2, SEASON_OUT), out);
    T_ASSERT_EQ_I(rec_i32(2, PLAN_CAP), out * 12);          /* g_prod_scale default */
    T_ASSERT_EQ_I(rec_i32(2, PROVISION_RAW), 5000 - out);
    return 0;
}

/* The loop wrapper runs every realm 1..count with the saved season pair. */
int test_army_economy_season_pass_loops_count(void)
{
    army_reset();
    s_slot_0056d5dc = 3;
    s_slot_0057c934 = 1;     /* just-ended season */
    s_slot_0057c92c = 2;     /* next season */
    for (int k = 1; k <= 3; k++) {
        set_stock(k, PROVISION_RAW, 100 * k);
        set_byte(k, PLAN_REF_LEVEL, 0);
    }
    set_stock(4, PROVISION_RAW, 999);            /* beyond count — untouched */

    engine_army_economy_apply_season_pass();

    for (int k = 1; k <= 3; k++)
        T_ASSERT_EQ_I(rec_i32(k, PROVISION_RAW_PREV), 100 * k);   /* each snapshotted */
    T_ASSERT_EQ_I(rec_i32(4, PROVISION_RAW_PREV), 0);             /* not run */
    return 0;
}

int test_army_economy_season_update_unbound_noop(void)
{
    engine_army_economy_bind_army_records(NULL);
    engine_army_economy_season_update(2, 1, 2);   /* no crash */
    engine_army_economy_apply_season_pass();      /* count may be stale; no crash */
    return 0;
}
