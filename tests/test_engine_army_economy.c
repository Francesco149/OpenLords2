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
