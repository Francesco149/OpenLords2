/*
 * Tests for the per-realm AI economic-management leaves (engine_army_ai):
 * FUN_0045158b seed-workforce-ratios, FUN_00428c42 advance-tier, and
 * FUN_00450ccd consume-build-demand.  All operate on the canonical save
 * globals (g_per_army_768 / g_player_strategic_352, linked from save.c),
 * written directly here.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_army_ai.h"
#include "engine_army_economy.h"   /* NULL the army-record bind to isolate the re-plan tail */
#include "engine_territory.h"      /* NULL the territory binds too */
#include "save.h"
#include "save_schema_globals.h"   /* s_slot_005532a0 price table, 0053f260 mode */

#define AREC(key)   (&g_per_army_768[(key) * 0x300])
#define PREC(slot)  (&g_player_strategic_352[(slot) * 0x160])
#define AI32(p, o)  (*(int32_t *)((p) + (o)))

static void ai_setup(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
}

/* seed_workforce_ratios writes the eight fixed AI defaults. */
int test_army_ai_seed_workforce_ratios(void)
{
    ai_setup();
    engine_army_ai_seed_workforce_ratios(3);
    uint8_t *r = AREC(3);
    T_ASSERT_EQ_I(AI32(r, 0x130), 0x21);
    T_ASSERT_EQ_I(AI32(r, 0x134), 0x32);
    T_ASSERT_EQ_I(AI32(r, 0x138), 0x11);
    T_ASSERT_EQ_I(AI32(r, 0x13c), 0x28);
    T_ASSERT_EQ_I(AI32(r, 0x140), 0xf);
    T_ASSERT_EQ_I(AI32(r, 0x144), 0xf);
    T_ASSERT_EQ_I(AI32(r, 0x148), 0xf);
    T_ASSERT_EQ_I(AI32(r, 0x14c), 0xf);
    return 0;
}

/* advance_tier: amount <= 0 is a no-op. */
int test_army_ai_advance_tier_nonpositive_noop(void)
{
    ai_setup();
    uint8_t *r = AREC(2);
    AI32(r, 0x24) = 100;
    engine_army_ai_advance_tier(0, 2);
    engine_army_ai_advance_tier(-5, 2);
    T_ASSERT_EQ_I(r[0x219], 0);
    T_ASSERT_EQ_I(r[0x0c], 0);
    T_ASSERT_EQ_I(AI32(r, 0x194), 0);
    return 0;
}

/* advance_tier: step sized by amount / (feed/10) multiples; here unit=10,
 * amount=25 → between unit*2(20) and unit*3(30) → step 2. */
int test_army_ai_advance_tier_step_by_multiple(void)
{
    ai_setup();
    uint8_t *r = AREC(2);
    AI32(r, 0x24) = 100;                /* feed → unit = 10 */
    engine_army_ai_advance_tier(25, 2);
    T_ASSERT_EQ_I(r[0x219], 2);         /* tier byte */
    T_ASSERT_EQ_I(r[0x0c], 2);          /* approval */
    T_ASSERT_EQ_I(AI32(r, 0x194), 2);   /* cumulative */
    return 0;
}

/* advance_tier: the tier byte is clamped so +0x219 never exceeds 5 — a step
 * that would overshoot is capped to the remaining headroom. */
int test_army_ai_advance_tier_clamps_to_five(void)
{
    ai_setup();
    uint8_t *r = AREC(2);
    AI32(r, 0x24) = 100;                /* unit = 10 */
    r[0x219] = 4;                       /* headroom 1 */
    engine_army_ai_advance_tier(50, 2); /* amount >= unit*5 → raw step 5 */
    T_ASSERT_EQ_I(r[0x219], 5);         /* 4 + min(5,1) */
    T_ASSERT_EQ_I(r[0x0c], 1);
    T_ASSERT_EQ_I(AI32(r, 0x194), 1);
    return 0;
}

/* advance_tier: approval caps at 100 (retail 'c' = 99 threshold). */
int test_army_ai_advance_tier_approval_caps_at_100(void)
{
    ai_setup();
    uint8_t *r = AREC(2);
    AI32(r, 0x24) = 100;                /* unit = 10 */
    r[0x0c] = 99;                       /* approval near cap */
    engine_army_ai_advance_tier(50, 2); /* step 5 → 99 + 5 = 104 → cap 100 */
    T_ASSERT_EQ_I((int8_t)r[0x0c], 100);
    return 0;
}

/* consume_build_demand: draw min(pool, demand) from each owner pool, reducing
 * both pool and demand. */
int test_army_ai_consume_build_demand(void)
{
    ai_setup();
    uint8_t *r = AREC(4);
    r[0x1c3] = 1;                       /* secondary settlement present */
    r[0x005] = 2;                       /* owner = player 2 */
    AI32(r, 0x1d4) = 30;                /* hi demand */
    AI32(r, 0x1d0) = 40;                /* lo demand */
    uint8_t *s = PREC(2);
    AI32(s, 0x130) = 50;                /* hi pool (>= demand) */
    AI32(s, 0x128) = 20;                /* lo pool (<  demand) */

    engine_army_ai_consume_build_demand(4);

    T_ASSERT_EQ_I(AI32(s, 0x130), 20);  /* 50 - 30 */
    T_ASSERT_EQ_I(AI32(r, 0x1d4), 0);   /* demand met */
    T_ASSERT_EQ_I(AI32(s, 0x128), 0);   /* pool drained */
    T_ASSERT_EQ_I(AI32(r, 0x1d0), 20);  /* 40 - 20 remaining */
    return 0;
}

/* rebalance_workforce: enabling a policy slot scales the five ratios down by the
 * remainder of pct[active-count] and folds the leftover onto the largest ratio.
 * Start {pol 0,0,0 | ratios 50,50,0,0,0}; active=2 → share=33, keep=67. */
int test_army_ai_rebalance_enable_distributes(void)
{
    ai_setup();
    uint8_t *r = AREC(3);
    AI32(r, 0x13c) = 50;   /* ratio slot 3 */
    AI32(r, 0x140) = 50;   /* ratio slot 4 */

    engine_army_ai_rebalance_workforce(3, 0, 1);

    T_ASSERT_EQ_I(AI32(r, 0x130), 33);  /* policy slot 0 claims share */
    T_ASSERT_EQ_I(AI32(r, 0x13c), 67);  /* slot 3: percent_of(67,50)=33, +34 fold */
    T_ASSERT_EQ_I(AI32(r, 0x140), 33);  /* slot 4: percent_of(67,50)=33 */
    T_ASSERT_EQ_I(AI32(r, 0x144), 0);
    T_ASSERT_EQ_I(AI32(r, 0x148), 0);
    T_ASSERT_EQ_I(AI32(r, 0x14c), 0);
    /* five ratios still sum to 100 */
    T_ASSERT_EQ_I(AI32(r,0x13c)+AI32(r,0x140)+AI32(r,0x144)+AI32(r,0x148)+AI32(r,0x14c), 100);
    return 0;
}

/* rebalance_workforce: disabling a policy slot scales the ratios back up by
 * 100/(100-share) and re-normalises.  Inverse of the enable vector above. */
int test_army_ai_rebalance_disable_restores(void)
{
    ai_setup();
    uint8_t *r = AREC(3);
    AI32(r, 0x130) = 33;   /* policy slot 0 active */
    AI32(r, 0x13c) = 67;   /* ratio slot 3 */
    AI32(r, 0x140) = 33;   /* ratio slot 4 */

    engine_army_ai_rebalance_workforce(3, 0, 0);

    T_ASSERT_EQ_I(AI32(r, 0x130), 0);   /* policy cleared */
    T_ASSERT_EQ_I(AI32(r, 0x13c), 51);  /* percent_div(67,67)=100, +(-49) fold */
    T_ASSERT_EQ_I(AI32(r, 0x140), 49);  /* percent_div(33,67)=49 */
    T_ASSERT_EQ_I(AI32(r,0x13c)+AI32(r,0x140)+AI32(r,0x144)+AI32(r,0x148)+AI32(r,0x14c), 100);
    return 0;
}

/* rebalance_workforce: no-op when the slot is already in the requested state
 * (enable an already-active slot, or disable an already-clear one). */
int test_army_ai_rebalance_noop_when_already_set(void)
{
    ai_setup();
    uint8_t *r = AREC(3);
    AI32(r, 0x130) = 33;   /* slot 0 already active */
    AI32(r, 0x13c) = 60;
    AI32(r, 0x140) = 40;

    engine_army_ai_rebalance_workforce(3, 0, 1);   /* enable an active slot → no-op */
    T_ASSERT_EQ_I(AI32(r, 0x130), 33);
    T_ASSERT_EQ_I(AI32(r, 0x13c), 60);
    T_ASSERT_EQ_I(AI32(r, 0x140), 40);

    engine_army_ai_rebalance_workforce(3, 1, 0);   /* disable a clear slot → no-op */
    T_ASSERT_EQ_I(AI32(r, 0x134), 0);
    T_ASSERT_EQ_I(AI32(r, 0x13c), 60);
    return 0;
}

/* rebalance_workforce: with all five ratios active the share table yields 0, so
 * enabling claims nothing and the ratios are unchanged after re-normalisation. */
int test_army_ai_rebalance_full_table_zero_share(void)
{
    ai_setup();
    uint8_t *r = AREC(3);
    AI32(r, 0x13c) = 40;
    AI32(r, 0x140) = 15;
    AI32(r, 0x144) = 15;
    AI32(r, 0x148) = 15;
    AI32(r, 0x14c) = 15;

    engine_army_ai_rebalance_workforce(3, 1, 1);

    T_ASSERT_EQ_I(AI32(r, 0x134), 0);   /* share[5] == 0 */
    T_ASSERT_EQ_I(AI32(r, 0x13c), 40);
    T_ASSERT_EQ_I(AI32(r, 0x140), 15);
    T_ASSERT_EQ_I(AI32(r, 0x14c), 15);
    return 0;
}

/* rebalance_workforce: the live secondary-settlement call (realm, slot 3, mode
 * 0) retires ratio slot 3 and spreads its share evenly across the other four. */
int test_army_ai_rebalance_settlement_retire_ratio3(void)
{
    ai_setup();
    uint8_t *r = AREC(5);
    AI32(r, 0x13c) = 40;   /* ratio slot 3 (retired) */
    AI32(r, 0x140) = 15;
    AI32(r, 0x144) = 15;
    AI32(r, 0x148) = 15;
    AI32(r, 0x14c) = 15;

    engine_army_ai_rebalance_workforce(5, 3, 0);

    T_ASSERT_EQ_I(AI32(r, 0x13c), 0);   /* slot cleared */
    T_ASSERT_EQ_I(AI32(r, 0x140), 25);  /* percent_div(15,60)=25 */
    T_ASSERT_EQ_I(AI32(r, 0x144), 25);
    T_ASSERT_EQ_I(AI32(r, 0x148), 25);  /* +0 fold (largest, but remainder 0) */
    T_ASSERT_EQ_I(AI32(r, 0x14c), 25);
    T_ASSERT_EQ_I(AI32(r,0x13c)+AI32(r,0x140)+AI32(r,0x144)+AI32(r,0x148)+AI32(r,0x14c), 100);
    return 0;
}

/* consume_build_demand: no-op for independent realms (owner 0) and realms with
 * no secondary settlement (+0x1c3 == 0). */
int test_army_ai_consume_build_demand_gates(void)
{
    ai_setup();
    uint8_t *r = AREC(4);
    r[0x005] = 2;
    AI32(r, 0x1d4) = 30;
    uint8_t *s = PREC(2);
    AI32(s, 0x130) = 50;

    r[0x1c3] = 0;                       /* no settlement → no-op */
    engine_army_ai_consume_build_demand(4);
    T_ASSERT_EQ_I(AI32(s, 0x130), 50);

    r[0x1c3] = 1;
    r[0x005] = 0;                       /* independent → no-op */
    engine_army_ai_consume_build_demand(4);
    T_ASSERT_EQ_I(AI32(s, 0x130), 50);
    return 0;
}

/* ── Buy/sell transaction executor (FUN_004284ce) ───────────────────
 * Isolate the transaction arithmetic from the re-plan tail by leaving the
 * separately-bound army-record / cell pointers NULL (every tail callee then
 * no-ops); recompute_player_total touches only +0x138, away from our checks.
 * Trade commodity slot 8 (strategic pool +0x130). */

static void trade_setup(void)
{
    ai_setup();
    engine_army_economy_bind_army_records(NULL);
    engine_territory_bind_army_records(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_cell_roster(NULL);
}

/* Buy: debit cost from cash (+0x118), credit both buy ledgers (+0x104/+0x108),
 * add the units to the slot-8 pool (+0x130). */
int test_army_ai_trade_buy(void)
{
    trade_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 100;               /* cash */
    AI32(s, 0x130) = 7;                 /* existing slot-8 stock */

    engine_army_ai_execute_trade(0, 5, 8, /*buy*/10, /*sell*/4, 2, 4);

    T_ASSERT_EQ_I(AI32(s, 0x130), 12);  /* 7 + 5 */
    T_ASSERT_EQ_I(AI32(s, 0x118), 50);  /* 100 - 5*10 */
    T_ASSERT_EQ_I(AI32(s, 0x108), 50);  /* buy ledger B */
    T_ASSERT_EQ_I(AI32(s, 0x104), 50);  /* buy ledger A */
    return 0;
}

/* Sell: reduce the slot-8 pool, credit cash + both sell ledgers (+0x10c/+0x110). */
int test_army_ai_trade_sell(void)
{
    trade_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 100;
    AI32(s, 0x130) = 10;

    engine_army_ai_execute_trade(0, -3, 8, /*buy*/10, /*sell*/4, 2, 4);

    T_ASSERT_EQ_I(AI32(s, 0x130), 7);   /* 10 - 3 */
    T_ASSERT_EQ_I(AI32(s, 0x118), 112); /* 100 + 3*4 */
    T_ASSERT_EQ_I(AI32(s, 0x110), 12);  /* sell ledger B */
    T_ASSERT_EQ_I(AI32(s, 0x10c), 12);  /* sell ledger A */
    return 0;
}

/* Buy aborts when the owner can't afford the cost — nothing changes. */
int test_army_ai_trade_buy_unaffordable_aborts(void)
{
    trade_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 10;                /* only 10 gold */
    AI32(s, 0x130) = 7;

    engine_army_ai_execute_trade(0, 5, 8, 10, 4, 2, 4);  /* cost 50 > 10 */

    T_ASSERT_EQ_I(AI32(s, 0x130), 7);   /* untouched */
    T_ASSERT_EQ_I(AI32(s, 0x118), 10);
    T_ASSERT_EQ_I(AI32(s, 0x108), 0);
    return 0;
}

/* Sell aborts when the owned realm lacks the stock to cover the sale. */
int test_army_ai_trade_sell_insufficient_aborts(void)
{
    trade_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 100;
    AI32(s, 0x130) = 2;                 /* only 2 in stock */

    engine_army_ai_execute_trade(0, -3, 8, 10, 4, 2, 4);  /* sell 3 > 2 */

    T_ASSERT_EQ_I(AI32(s, 0x130), 2);   /* untouched */
    T_ASSERT_EQ_I(AI32(s, 0x118), 100);
    return 0;
}

/* Independent realm (player 0): treasury is the realm's own gold (+0x1f4); no
 * affordability gate (the gate is owner-only). */
int test_army_ai_trade_independent_uses_realm_gold(void)
{
    trade_setup();
    uint8_t *r = AREC(4);
    AI32(r, 0x1f4) = 80;                /* realm treasury */

    engine_army_ai_execute_trade(0, 5, 8, 10, 4, 0, 4);  /* buy, player 0 */

    T_ASSERT_EQ_I(AI32(r, 0x1f4), 30);  /* 80 - 50 */
    T_ASSERT_EQ_I(AI32(PREC(0), 0x130), 5);  /* slot-8 pool of player 0 */
    return 0;
}

/* amount == 0 is a no-op (no transaction, no re-plan). */
int test_army_ai_trade_zero_noop(void)
{
    trade_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 100;
    AI32(s, 0x130) = 7;

    engine_army_ai_execute_trade(0, 0, 8, 10, 4, 2, 4);

    T_ASSERT_EQ_I(AI32(s, 0x130), 7);
    T_ASSERT_EQ_I(AI32(s, 0x118), 100);
    return 0;
}

/* ── Order helpers (FUN_004a4a3f / 4b12 / 4c41) ─────────────────────
 * Set the slot-8 base price to 4 and zero the territory price-adjustment so the
 * unit (buy) price floors to base+1 = 5 and the sell price is the base 4.  The
 * executor's re-plan tail stays a no-op (trade_setup NULLs its records). */

static void order_setup(void)
{
    trade_setup();
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);  /* adjpct 0 → adj floor 1 */
    *(int32_t *)&s_slot_005532a0[8 * 0x10] = 4;                  /* slot-8 base price */
    uint8_t *a = AREC(4);
    a[0x1a4] = 1;                                                /* trade gate open */
    a[0x005] = 2;                                                /* owner = player 2 */
    a[0x1a5] = 0;                                                /* race idx 0 */
}

/* sell_order: dump 3 units of slot 8 at the base price (4) → +12 cash; no gate
 * on stock beyond what the executor checks (owner has 10 in stock). */
int test_army_ai_sell_order(void)
{
    order_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 100;
    AI32(s, 0x130) = 10;

    engine_army_ai_sell_order(4, 3, 8);

    T_ASSERT_EQ_I(AI32(s, 0x130), 7);    /* 10 - 3 */
    T_ASSERT_EQ_I(AI32(s, 0x118), 112);  /* 100 + 3*4 */
    return 0;
}

/* buy_order: unit price 5, buy 3 → cost 15 affordable from 100. */
int test_army_ai_buy_order_affordable(void)
{
    order_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 100;
    AI32(s, 0x130) = 0;

    engine_army_ai_buy_order(4, 3, 8);

    T_ASSERT_EQ_I(AI32(s, 0x130), 3);
    T_ASSERT_EQ_I(AI32(s, 0x118), 85);   /* 100 - 15 */
    return 0;
}

/* buy_order: order unaffordable up front → nothing happens (no partial fill). */
int test_army_ai_buy_order_unaffordable_skips(void)
{
    order_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 10;                 /* < 15 */
    AI32(s, 0x130) = 0;

    engine_army_ai_buy_order(4, 3, 8);

    T_ASSERT_EQ_I(AI32(s, 0x130), 0);
    T_ASSERT_EQ_I(AI32(s, 0x118), 10);
    return 0;
}

/* buy_order_scaled: 3 @ 5 = 15 > 12 → halve to 1 @ 5 = 5 <= 12 → buy 1. */
int test_army_ai_buy_order_scaled_halves(void)
{
    order_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 12;
    AI32(s, 0x130) = 0;

    engine_army_ai_buy_order_scaled(4, 3, 8);

    T_ASSERT_EQ_I(AI32(s, 0x130), 1);    /* bought 1 */
    T_ASSERT_EQ_I(AI32(s, 0x118), 7);    /* 12 - 5 */
    return 0;
}

/* gate: a closed trade gate (+0x1a4 == 0) makes all order helpers no-ops. */
int test_army_ai_orders_gated(void)
{
    order_setup();
    uint8_t *s = PREC(2);
    AI32(s, 0x118) = 100;
    AI32(s, 0x130) = 10;
    AREC(4)[0x1a4] = 0;                  /* gate closed */

    engine_army_ai_buy_order(4, 3, 8);
    engine_army_ai_sell_order(4, 3, 8);
    engine_army_ai_buy_order_scaled(4, 3, 8);

    T_ASSERT_EQ_I(AI32(s, 0x130), 10);   /* untouched */
    T_ASSERT_EQ_I(AI32(s, 0x118), 100);
    return 0;
}

/* ── High-cell balancer (FUN_004a4694) ──────────────────────────────
 * Bind the territory module to the shared army block + a local cell roster/grid
 * so the demolish + census actually run.  Shed path: low density (+0x25c),
 * high-cell-rich (+0x200 > 2), stock-poor (+0x250 < 10) → demolish a high band. */

enum { BAL_CELLS = 0x40 * 0x40 * 8, BAL_ROWS = 17, BAL_NCOL = 20 };
static uint8_t  g_bal_cells[BAL_CELLS];
static int32_t  g_bal_roster[BAL_ROWS * BAL_NCOL];

int test_army_ai_balance_high_cells_sheds(void)
{
    ai_setup();
    memset(g_bal_cells, 0, sizeof g_bal_cells);
    memset(g_bal_roster, 0, sizeof g_bal_roster);
    engine_territory_bind_army_records(g_per_army_768);
    engine_territory_bind_cells(g_bal_cells);
    engine_territory_bind_cell_roster(g_bal_roster);
    engine_army_economy_bind_army_records(NULL);   /* grade's load path no-ops cleanly */

    /* three upper-high (0x14) cells in realm 4's roster */
    for (int c = 0; c < 3; c++) {
        int off = (4 * BAL_NCOL + c + 1) * 8;
        g_bal_cells[off] = 0x14;
        g_bal_roster[4 * BAL_NCOL + c] = off;
    }
    uint8_t *a = AREC(4);
    AI32(a, 0x25c) = 5;     /* density < 0xb */
    a[0x200] = 3;           /* HIGH census > 2 */
    AI32(a, 0x250) = 5;     /* stock A < 10 */
    a[0x205] = 20;          /* active count for any cursor wrap */

    engine_army_ai_balance_high_cells(4, 0);

    /* demolish_band(0x13) turned the 0x14 cells to 1 (then convert may re-touch
     * some); at minimum the first upper-high cell is no longer 0x14. */
    int off0 = (4 * BAL_NCOL + 0 + 1) * 8;
    T_ASSERT(g_bal_cells[off0] != 0x14);

    engine_territory_bind_army_records(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_cell_roster(NULL);
    return 0;
}

/* ── Resource orchestrator + dispatch/run bodies (FUN_0049e39b / 4a40xx /
 *    0049dd01 / 0049a990) ──────────────────────────────────────────
 * Personality 1 (table row 0) targets: commodity-8 stock 250, cash floor 1000.
 * The order/executor tail no-ops with econ/territory unbound. */

static void ai_turn_setup(void)
{
    order_setup();                  /* NULLs econ/territory, prices, gate on realm 4 */
    s_slot_0057c934 = 1;            /* not autumn (skip the season-4 rebuild) */
    s_slot_0057c92c = 1;
    s_slot_0053f25c = 1;
}

/* reconcile_resources: with no queued buy request, sell the slot-8 surplus over
 * the personality target (400 - 250 = 150) at the base price (4 → +600 cash). */
int test_army_ai_reconcile_sells_surplus(void)
{
    ai_turn_setup();
    uint8_t *s = PREC(2);
    s[0x007] = 1;                   /* personality 1 → target_8 = 250 */
    AI32(s, 0x130) = 400;           /* slot-8 pool over target */
    AI32(s, 0x118) = 50;            /* cash below the 1000 discretionary floor */

    engine_army_ai_reconcile_resources(2, 4);

    T_ASSERT_EQ_I(AI32(s, 0x130), 250);   /* sold the 150 surplus */
    T_ASSERT_EQ_I(AI32(s, 0x118), 650);   /* 50 + 150*4 */
    return 0;
}

/* reconcile_resources: the player's trade lock (+0x158) suppresses everything. */
int test_army_ai_reconcile_trade_lock_gates(void)
{
    ai_turn_setup();
    uint8_t *s = PREC(2);
    s[0x007] = 1;
    AI32(s, 0x130) = 400;
    AI32(s, 0x118) = 50;
    s[0x158] = 1;                   /* trade lock set */

    engine_army_ai_reconcile_resources(2, 4);

    T_ASSERT_EQ_I(AI32(s, 0x130), 400);   /* untouched */
    T_ASSERT_EQ_I(AI32(s, 0x118), 50);
    return 0;
}

/* run_player: latches the personality dispatch code (+0x1fe) and the dispatcher
 * tags the strategy field (+0x8).  Personality 4 → code 9 → expansionist → 0x28. */
int test_army_ai_run_player_dispatch_expansionist(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 5;            /* realm count */
    PREC(2)[0x007] = 4;             /* personality 4 → dispatch code 9 */
    AREC(5)[0x005] = 2;             /* realm 5 owned by player 2 */

    engine_army_ai_run_player(2);

    T_ASSERT_EQ_I(AREC(5)[0x1fe], 9);   /* dispatch code latched */
    T_ASSERT_EQ_I(AREC(5)[0x008], 0x28);/* expansionist strategy tag */
    return 0;
}

/* run_turn: process in-game players (active +0x4, not eliminated +0x5), skip the
 * rest.  Player 3 (personality 2 → code 1 → compact → tag 0x14) owns realm 5;
 * eliminated player 4 is skipped. */
int test_army_ai_run_turn_active_gate(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 5;
    uint8_t *p3 = PREC(3);
    p3[0x004] = 1; p3[0x005] = 0; p3[0x007] = 2;   /* active, personality 2 */
    uint8_t *p4 = PREC(4);
    p4[0x004] = 1; p4[0x005] = 1;                  /* eliminated → skipped */
    AREC(5)[0x005] = 3;                            /* realm 5 owned by player 3 */

    engine_army_ai_run_turn();

    T_ASSERT_EQ_I(AREC(5)[0x1fe], 1);    /* compact dispatch code */
    T_ASSERT_EQ_I(AREC(5)[0x008], 0x14); /* compact strategy tag */
    return 0;
}

/* ── Tax-rate sweep (FUN_004a4782) ──────────────────────────────────
 * Exercise the strength-band classifier + the fixed-rate path with the economy
 * appraisal bound (so appraisal_value / stock_a_value are real). */

int test_army_ai_tax_sweep_band_and_fixed_rate(void)
{
    ai_setup();
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_set_prices(5, 10, 6);
    s_slot_0053f260 = 0;                /* basis = feed only */

    uint8_t *a = AREC(4);
    AI32(a, 0x024) = 100;               /* feed → basis 100 */
    AI32(a, 0x250) = 5;                 /* stock A = 5 → appraisal = 5*(5+10) = 75 */
    AI32(a, 0x224) = 0;                 /* no provision-raw term */

    engine_army_ai_tax_sweep(4, 50);    /* fixed rate path */

    /* appraisal 75 vs basis 100: 75 < 100 and 75 >= 50 → band 1 */
    T_ASSERT_EQ_I(a[0x15e], 1);
    T_ASSERT_EQ_I(a[0x15f], 50);        /* rate fixed at 50 */

    engine_army_economy_bind_army_records(NULL);
    return 0;
}
