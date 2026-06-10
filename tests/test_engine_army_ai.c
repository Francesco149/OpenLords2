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
#include "engine_event_queue.h"    /* diplomacy proposals post through the event queue */
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

/* set_rations_and_grant, player 0: independent realms use the fixed 8-step
 * approval → ration (+0xb9) ladder.  Pass 2 is skipped (strat slot 0 inactive). */
int test_army_ai_rations_independent_ladder(void)
{
    ai_setup();
    s_slot_0056d5dc = 7;
    AREC(1)[0x0c] = 10;   /* <20  → 0  */
    AREC(2)[0x0c] = 45;   /* <50  → 2  */
    AREC(3)[0x0c] = 55;   /* <60  → 3  */
    AREC(4)[0x0c] = 60;   /* <70  → 4  */
    AREC(5)[0x0c] = 75;   /* <80  → 6  */
    AREC(6)[0x0c] = 85;   /* <90  → 8  */
    AREC(7)[0x0c] = 95;   /* else → 12 */

    engine_army_ai_set_rations_and_grant(0);

    T_ASSERT_EQ_I(AREC(1)[0xb9], 0);
    T_ASSERT_EQ_I(AREC(2)[0xb9], 2);
    T_ASSERT_EQ_I(AREC(3)[0xb9], 3);
    T_ASSERT_EQ_I(AREC(4)[0xb9], 4);
    T_ASSERT_EQ_I(AREC(5)[0xb9], 6);
    T_ASSERT_EQ_I(AREC(6)[0xb9], 8);
    T_ASSERT_EQ_I(AREC(7)[0xb9], 12);
    return 0;
}

/* set_rations_and_grant, AI player with personality 1 → selector 2 ladder.
 * AI level left 0 so the grant pass returns early and only rations are set. */
int test_army_ai_rations_selector2(void)
{
    ai_setup();
    s_slot_0056d5dc = 6;
    PREC(2)[0x007] = 1;   /* personality 1 → +4 selector == 2 */
    PREC(2)[0x029] = 0;   /* AI level 0 → grant pass skipped */
    for (int r = 1; r <= 6; r++) AREC(r)[0x005] = 2;
    AREC(1)[0x0c] = 55;   /* <60 → 0  */
    AREC(2)[0x0c] = 65;   /* <70 → 1  */
    AREC(3)[0x0c] = 75;   /* <80 → 2  */
    AREC(4)[0x0c] = 85;   /* <90 → 3  */
    AREC(5)[0x0c] = 93;   /* <95 → 8  */
    AREC(6)[0x0c] = 99;   /* else → 10 */

    engine_army_ai_set_rations_and_grant(2);

    T_ASSERT_EQ_I(AREC(1)[0xb9], 0);
    T_ASSERT_EQ_I(AREC(2)[0xb9], 1);
    T_ASSERT_EQ_I(AREC(3)[0xb9], 2);
    T_ASSERT_EQ_I(AREC(4)[0xb9], 3);
    T_ASSERT_EQ_I(AREC(5)[0xb9], 8);
    T_ASSERT_EQ_I(AREC(6)[0xb9], 10);
    return 0;
}

/* set_rations_and_grant, AI player with personality 4 → selector 1 ladder. */
int test_army_ai_rations_selector1(void)
{
    ai_setup();
    s_slot_0056d5dc = 5;
    PREC(3)[0x007] = 4;   /* personality 4 → +4 selector == 1 */
    PREC(3)[0x029] = 0;
    for (int r = 1; r <= 5; r++) AREC(r)[0x005] = 3;
    AREC(1)[0x0c] = 20;   /* <30 → 0  */
    AREC(2)[0x0c] = 40;   /* <50 → 1  */
    AREC(3)[0x0c] = 60;   /* <65 → 3  */
    AREC(4)[0x0c] = 75;   /* <80 → 7  */
    AREC(5)[0x0c] = 90;   /* else → 12 */

    engine_army_ai_set_rations_and_grant(3);

    T_ASSERT_EQ_I(AREC(1)[0xb9], 0);
    T_ASSERT_EQ_I(AREC(2)[0xb9], 1);
    T_ASSERT_EQ_I(AREC(3)[0xb9], 3);
    T_ASSERT_EQ_I(AREC(4)[0xb9], 7);
    T_ASSERT_EQ_I(AREC(5)[0xb9], 12);
    return 0;
}

/* Grant pass, AI level 4 (>= 3 → DC1E0 table; 3..4 → level<5 step set).
 * personality 2, difficulty 3: cash += DC1E0[pers2][diff3] = 1400; per-realm
 * steps pop=30, stock=6, prov=60 applied only above the thresholds. */
int test_army_ai_grant_level4_topups(void)
{
    ai_setup();
    s_slot_0056d5dc = 2;
    s_slot_0053f23c = 3;                /* difficulty */
    uint8_t *s = PREC(3);
    s[0x004] = 1;                       /* active */
    s[0x007] = 2;                       /* personality 2 */
    s[0x029] = 4;                       /* AI level 4 */
    AI32(s, 0x118) = 1000;              /* treasury */

    uint8_t *a1 = AREC(1);              /* above every threshold → topped up */
    a1[0x005] = 3;
    AI32(a1, 0x024) = 100;
    AI32(a1, 0x250) = 50;
    AI32(a1, 0x224) = 100;
    uint8_t *a2 = AREC(2);              /* exactly at thresholds → untouched */
    a2[0x005] = 3;
    AI32(a2, 0x024) = 20;
    AI32(a2, 0x250) = 10;
    AI32(a2, 0x224) = 50;

    engine_army_ai_set_rations_and_grant(3);

    T_ASSERT_EQ_I(AI32(s, 0x118), 1000 + 1400);
    T_ASSERT_EQ_I(AI32(a1, 0x024), 130);   /* +30 */
    T_ASSERT_EQ_I(AI32(a1, 0x030), 30);    /* mirror +30 */
    T_ASSERT_EQ_I(AI32(a1, 0x250), 56);    /* +6  */
    T_ASSERT_EQ_I(AI32(a1, 0x224), 160);   /* +60 */
    T_ASSERT_EQ_I(AI32(a2, 0x024), 20);    /* unchanged */
    T_ASSERT_EQ_I(AI32(a2, 0x030), 0);
    T_ASSERT_EQ_I(AI32(a2, 0x250), 10);
    T_ASSERT_EQ_I(AI32(a2, 0x224), 50);
    return 0;
}

/* Grant pass, AI level 2 (< 3 → DC230 table; bigger step set pop=40/stock=10/
 * prov=80).  personality 1, difficulty 2: cash += DC230[pers1][diff2] = 250. */
int test_army_ai_grant_level2_easy(void)
{
    ai_setup();
    s_slot_0056d5dc = 1;
    s_slot_0053f23c = 2;
    uint8_t *s = PREC(4);
    s[0x004] = 1;
    s[0x007] = 1;                       /* personality 1 */
    s[0x029] = 2;                       /* AI level 2 */
    AI32(s, 0x118) = 0;

    uint8_t *a = AREC(1);
    a[0x005] = 4;
    AI32(a, 0x024) = 100;
    AI32(a, 0x250) = 20;
    AI32(a, 0x224) = 100;

    engine_army_ai_set_rations_and_grant(4);

    T_ASSERT_EQ_I(AI32(s, 0x118), 250);
    T_ASSERT_EQ_I(AI32(a, 0x024), 140);    /* +40 */
    T_ASSERT_EQ_I(AI32(a, 0x030), 40);
    T_ASSERT_EQ_I(AI32(a, 0x250), 30);     /* +10 */
    T_ASSERT_EQ_I(AI32(a, 0x224), 180);    /* +80 */
    return 0;
}

/* Grant pass, AI level 5: still draws the DC1E0 cash grant, but every per-realm
 * step is 0 (level >= 5 falls through both step branches). */
int test_army_ai_grant_level5_cash_only(void)
{
    ai_setup();
    s_slot_0056d5dc = 1;
    s_slot_0053f23c = 2;
    uint8_t *s = PREC(2);
    s[0x004] = 1;
    s[0x007] = 1;                       /* personality 1 */
    s[0x029] = 5;                       /* AI level 5 */
    AI32(s, 0x118) = 0;

    uint8_t *a = AREC(1);
    a[0x005] = 2;
    AI32(a, 0x024) = 100;
    AI32(a, 0x250) = 50;
    AI32(a, 0x224) = 100;

    engine_army_ai_set_rations_and_grant(2);

    T_ASSERT_EQ_I(AI32(s, 0x118), 700);    /* DC1E0[pers1][diff2] = 700 */
    T_ASSERT_EQ_I(AI32(a, 0x024), 100);    /* no topup */
    T_ASSERT_EQ_I(AI32(a, 0x030), 0);
    T_ASSERT_EQ_I(AI32(a, 0x250), 50);
    T_ASSERT_EQ_I(AI32(a, 0x224), 100);
    return 0;
}

/* Grant pass is fully gated: an inactive player, an eliminated player, or one
 * with AI level 0 gets no cash and no topups. */
int test_army_ai_grant_gated(void)
{
    ai_setup();
    s_slot_0056d5dc = 1;
    s_slot_0053f23c = 3;
    AREC(1)[0x005] = 2;
    AI32(AREC(1), 0x024) = 100;

    uint8_t *s = PREC(2);
    s[0x007] = 2;
    s[0x029] = 4;                       /* level set, but ... */

    s[0x004] = 0;                       /* inactive → no grant */
    engine_army_ai_set_rations_and_grant(2);
    T_ASSERT_EQ_I(AI32(s, 0x118), 0);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x024), 100);

    s[0x004] = 1; s[0x005] = 1;         /* active but eliminated → no grant */
    engine_army_ai_set_rations_and_grant(2);
    T_ASSERT_EQ_I(AI32(s, 0x118), 0);

    s[0x005] = 0; s[0x029] = 0;         /* active, alive, level 0 → no grant */
    engine_army_ai_set_rations_and_grant(2);
    T_ASSERT_EQ_I(AI32(s, 0x118), 0);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x024), 100);
    return 0;
}

/* Independent dispatch code 0 (balanced): a provision shortfall is met by
 * crediting the realm's OWN gold pool (+0x1f4) by 100, and the strategy tag
 * (+0x008) is cleared.  ai_turn_setup NULLs the heavy binds and skips season 4. */
int test_army_ai_dispatch_indep_balanced_selffunds(void)
{
    ai_turn_setup();
    uint8_t *a = AREC(3);
    AI32(a, 0x224) = 50;                /* provisions < 100 */
    AI32(a, 0x1f4) = 0;                 /* treasury */
    a[0x008] = 0x32;                    /* pre-set strategy tag */

    engine_army_ai_dispatch_independent_balanced(3);

    T_ASSERT_EQ_I(AI32(a, 0x1f4), 100); /* +100 self-fund */
    T_ASSERT_EQ_I(a[0x008], 0);         /* tag cleared */
    return 0;
}

/* Independent balanced: adequate provisions (>= 100) → no self-fund. */
int test_army_ai_dispatch_indep_balanced_no_selffund(void)
{
    ai_turn_setup();
    uint8_t *a = AREC(3);
    AI32(a, 0x224) = 100;              /* not < 100 */
    AI32(a, 0x1f4) = 500;

    engine_army_ai_dispatch_independent_balanced(3);

    T_ASSERT_EQ_I(AI32(a, 0x1f4), 500); /* unchanged */
    T_ASSERT_EQ_I(a[0x008], 0);
    return 0;
}

/* Independent dispatch code 1 (compact): clears the strategy tag but, unlike
 * the balanced variant, never self-funds the treasury. */
int test_army_ai_dispatch_indep_compact_no_selffund(void)
{
    ai_turn_setup();
    uint8_t *a = AREC(2);
    AI32(a, 0x224) = 50;               /* low provisions */
    AI32(a, 0x1f4) = 300;
    a[0x008] = 0x14;

    engine_army_ai_dispatch_independent_compact(2);

    T_ASSERT_EQ_I(AI32(a, 0x1f4), 300); /* compact has no self-fund path */
    T_ASSERT_EQ_I(a[0x008], 0);
    return 0;
}

/* run_independent: clears the +0x1b0 review flag and routes by dispatch code
 * (+0x1fe) for owner-0 realms only — code 0 self-funds, code 1 does not, and a
 * realm owned by another player is skipped entirely. */
int test_army_ai_run_independent_routes(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 3;

    uint8_t *a1 = AREC(1);            /* independent, code 0, low provisions */
    a1[0x005] = 0; a1[0x1fe] = 0;
    AI32(a1, 0x224) = 50; AI32(a1, 0x1f4) = 0; a1[0x1b0] = 1;

    uint8_t *a2 = AREC(2);            /* independent, code 1 */
    a2[0x005] = 0; a2[0x1fe] = 1;
    AI32(a2, 0x224) = 50; AI32(a2, 0x1f4) = 0; a2[0x1b0] = 1;

    uint8_t *a3 = AREC(3);           /* owned by player 2 → skipped */
    a3[0x005] = 2; a3[0x1b0] = 1;

    engine_army_ai_run_independent(0);

    T_ASSERT_EQ_I(a1[0x1b0], 0);       /* review flag cleared */
    T_ASSERT_EQ_I(a2[0x1b0], 0);
    T_ASSERT_EQ_I(a3[0x1b0], 1);       /* non-owned realm untouched */
    T_ASSERT_EQ_I(AI32(a1, 0x1f4), 100);/* code 0 self-funded */
    T_ASSERT_EQ_I(AI32(a2, 0x1f4), 0);  /* code 1 did not */
    T_ASSERT_EQ_I(a1[0x008], 0);        /* both ran a dispatcher → tag 0 */
    T_ASSERT_EQ_I(a2[0x008], 0);
    return 0;
}

/* ── FUN_004a13a6 / FUN_0049f96c — AI brain steps 0xd / 8 ───────────────── */

/* Per-player strategic field offsets used by the diplomacy step. */
#define P_RANK    0x02b
#define P_HUMAN   0x005
#define P_PERS    0x007
#define P_GAUGE   0x060
#define P_LORD    0x081
#define P_TICK    0x0e9
#define P_PHASE   0x0ea
#define P_MOOD    0x159

/* Common setup for a diplomacy-step test: zero the strategic block + queue,
 * make `player` a #1-ranked AI whose proposal cooldown is one tick from firing. */
static void diplo_setup(engine_event_queue_state_t *evq, int player)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    engine_event_queue_init(evq);
    s_slot_00522d90 = 0;
    s_slot_0057c8cc = 0;

    uint8_t *s = PREC(player);
    s[P_RANK] = 1;          /* leader → passes the +0x2b < 2 gate */
    s[P_TICK] = 7;          /* one ++ away from firing at 8       */
}

/* Phase 0: once the gauge tops 0x28, broadcast a 0xc1 alliance proposal to each
 * in-game human slot, flip the phase to 1, reset the tick. */
int test_army_ai_propose_alliance_broadcast(void)
{
    engine_event_queue_state_t evq;
    diplo_setup(&evq, 2);
    uint8_t *s = PREC(2);
    s[P_GAUGE] = 0x28;                 /* > 0x27 */
    s[P_PERS]  = 2;
    s[P_MOOD]  = 4;                    /* variant = (4−4) + 2·4 = 8 */

    PREC(3)[0x004] = 1; PREC(3)[P_HUMAN] = 1;   /* eligible recipient */
    PREC(1)[0x004] = 1; PREC(1)[P_HUMAN] = 0;   /* AI → skipped       */
    PREC(4)[0x004] = 0; PREC(4)[P_HUMAN] = 1;   /* not in game → skipped */
    s_slot_0057c8cc = 3;               /* local player = 3 → its post publishes */

    engine_army_ai_propose_diplomacy(2, &evq);

    T_ASSERT_EQ_I(s[P_PHASE], 1);      /* flipped to war-proposal phase */
    T_ASSERT_EQ_I(s[P_TICK], 0);       /* cooldown reset */
    T_ASSERT_EQ_I(s[P_MOOD], 0);       /* 4 → 5 wraps to 0 */
    T_ASSERT_EQ_I(evq.write_idx, 1);   /* exactly one record published */
    T_ASSERT_EQ_I(evq.ring[0].target, 2);
    T_ASSERT_EQ_I(evq.ring[0].audience, 3);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xc1);
    T_ASSERT_EQ_I(evq.ring[0].payload32a, 8);
    return 0;
}

/* Phase 1: once the gauge tops 0x1c, send a 0xc0 war proposal to the weakest
 * player (s_slot_00522d90), flip the phase back to 0, reset the tick. */
int test_army_ai_propose_war_targeted(void)
{
    engine_event_queue_state_t evq;
    diplo_setup(&evq, 2);
    uint8_t *s = PREC(2);
    s[P_RANK]  = 0;                    /* unranked still passes < 2 */
    s[P_PHASE] = 1;
    s[P_GAUGE] = 0x1c;                 /* > 0x1b */
    s[P_PERS]  = 3;
    s[P_MOOD]  = 2;                    /* variant = (2−4) + 3·4 = 10 */
    s[P_LORD]  = 0;                    /* lord != target */

    s_slot_00522d90 = 4;               /* weakest player */
    PREC(4)[P_HUMAN] = 1;              /* target is a human */
    s_slot_0057c8cc = 4;               /* local player = 4 → the post publishes */

    engine_army_ai_propose_diplomacy(2, &evq);

    T_ASSERT_EQ_I(s[P_PHASE], 0);      /* flipped back */
    T_ASSERT_EQ_I(s[P_TICK], 0);
    T_ASSERT_EQ_I(s[P_MOOD], 3);       /* 2 → 3 (no wrap) */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].target, 2);
    T_ASSERT_EQ_I(evq.ring[0].audience, 4);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xc0);
    T_ASSERT_EQ_I(evq.ring[0].payload32a, 10);
    return 0;
}

/* Gate: a player ranked 2 or worse (+0x2b >= 2) issues nothing. */
int test_army_ai_propose_rank_gate(void)
{
    engine_event_queue_state_t evq;
    diplo_setup(&evq, 2);
    uint8_t *s = PREC(2);
    s[P_RANK]  = 2;
    s[P_GAUGE] = 0x40;

    engine_army_ai_propose_diplomacy(2, &evq);

    T_ASSERT_EQ_I(s[P_TICK], 7);       /* untouched */
    T_ASSERT_EQ_I(s[P_PHASE], 0);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* Gate: phase 0 below the 0x28 gauge does not even tick the cooldown. */
int test_army_ai_propose_gauge_gate(void)
{
    engine_event_queue_state_t evq;
    diplo_setup(&evq, 2);
    uint8_t *s = PREC(2);
    s[P_GAUGE] = 0x27;                 /* not > 0x27 */

    engine_army_ai_propose_diplomacy(2, &evq);

    T_ASSERT_EQ_I(s[P_TICK], 7);       /* gauge fails before the ++ */
    T_ASSERT_EQ_I(s[P_PHASE], 0);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* Cooldown: phase 0 above the gauge ticks the counter but holds until it hits
 * 8 — nothing is posted on the earlier ticks. */
int test_army_ai_propose_cooldown_holds(void)
{
    engine_event_queue_state_t evq;
    diplo_setup(&evq, 2);
    uint8_t *s = PREC(2);
    s[P_GAUGE] = 0x28;
    s[P_TICK]  = 5;                    /* 5 → 6, still < 8 */

    engine_army_ai_propose_diplomacy(2, &evq);

    T_ASSERT_EQ_I(s[P_TICK], 6);       /* incremented, not reset */
    T_ASSERT_EQ_I(s[P_PHASE], 0);      /* no flip yet */
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* Gate: phase 1 against a target that is already this AI's lord does nothing —
 * and leaves the cooldown untouched (the lord check precedes the ++). */
int test_army_ai_propose_war_lord_gate(void)
{
    engine_event_queue_state_t evq;
    diplo_setup(&evq, 2);
    uint8_t *s = PREC(2);
    s[P_PHASE] = 1;
    s[P_GAUGE] = 0x1c;
    s[P_LORD]  = 4;

    s_slot_00522d90 = 4;
    PREC(4)[P_HUMAN] = 1;

    engine_army_ai_propose_diplomacy(2, &evq);

    T_ASSERT_EQ_I(s[P_TICK], 7);       /* lord == target → bail before ++ */
    T_ASSERT_EQ_I(s[P_PHASE], 1);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* FUN_0049f96c — step 8 is a no-op; touches nothing and returns. */
int test_army_ai_step_noop(void)
{
    engine_army_ai_step_noop();
    return 0;
}

/* ── FUN_0049d5e0 / FUN_0049e1bf — first-owned-realm + brain step 4 ──────── */

/* first_owned_realm: returns the lowest-keyed realm owned by the player, 0 when
 * none. */
int test_army_ai_first_owned_realm(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 4;
    AREC(1)[0x005] = 3;
    AREC(2)[0x005] = 2;
    AREC(3)[0x005] = 2;
    AREC(4)[0x005] = 0;

    T_ASSERT_EQ_I(engine_army_ai_first_owned_realm(2), 2);   /* first match */
    T_ASSERT_EQ_I(engine_army_ai_first_owned_realm(3), 1);
    T_ASSERT_EQ_I(engine_army_ai_first_owned_realm(5), 0);   /* owns none */
    return 0;
}

/* seed_buy_requests: a high-level AI (+0x29 > 1) always seeds.  Clears the
 * +0x70 run, floors the staples from short pools, and folds each developed
 * keep-bearing owned realm's build demand into +0x70 (from +0x1d4) and +0x7c
 * (from +0x1d0). */
int test_army_ai_seed_buy_requests_highlevel(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 2;
    uint8_t *s = PREC(2);
    s[0x029] = 2;                       /* AI level > 1 → always seeds */
    AI32(s, 0x70) = 7; AI32(s, 0x74) = 7; AI32(s, 0x78) = 7; AI32(s, 0x7c) = 7;
    AI32(s, 0x120) = 0;                /* pool short → floor +0x74 = 0x32 */
    AI32(s, 0x130) = 0;                /* pool short → floor +0x70 = 100  */

    uint8_t *a1 = AREC(1);             /* eligible: owned, keep, settle */
    a1[0x005] = 2; a1[0x1c0] = 1; a1[0x1c3] = 1;
    AI32(a1, 0x1d4) = 5; AI32(a1, 0x1d0) = 9;
    uint8_t *a2 = AREC(2);             /* owned but no keep → skipped */
    a2[0x005] = 2; a2[0x1c0] = 0; a2[0x1c3] = 1;
    AI32(a2, 0x1d4) = 100; AI32(a2, 0x1d0) = 100;

    engine_army_ai_seed_buy_requests(2);

    T_ASSERT_EQ_I(AI32(s, 0x74), 0x32);    /* commodity-6 floored */
    T_ASSERT_EQ_I(AI32(s, 0x78), 0);       /* +0x78 cleared, untouched after */
    T_ASSERT_EQ_I(AI32(s, 0x70), 100 + 5); /* floor 100 + realm-1 +0x1d4 */
    T_ASSERT_EQ_I(AI32(s, 0x7c), 9);       /* realm-1 +0x1d0 */
    return 0;
}

/* seed_buy_requests: a low-level AI with no prosperous realm clears the run but
 * seeds nothing further (the approval/morale gate fails). */
int test_army_ai_seed_buy_requests_gate_fails(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 1;
    uint8_t *s = PREC(2);
    s[0x029] = 1;                      /* not above level 1 */
    AI32(s, 0x70) = 7; AI32(s, 0x74) = 7; AI32(s, 0x78) = 7; AI32(s, 0x7c) = 7;
    AI32(s, 0x120) = 0; AI32(s, 0x130) = 0;

    uint8_t *a1 = AREC(1);             /* owned but approval too low */
    a1[0x005] = 2; a1[0x00c] = 14; a1[0x00b] = 30;
    a1[0x1c0] = 1; a1[0x1c3] = 1; AI32(a1, 0x1d4) = 5; AI32(a1, 0x1d0) = 9;

    engine_army_ai_seed_buy_requests(2);

    T_ASSERT_EQ_I(AI32(s, 0x70), 0);   /* run cleared */
    T_ASSERT_EQ_I(AI32(s, 0x74), 0);   /* staples NOT floored (gate failed) */
    T_ASSERT_EQ_I(AI32(s, 0x7c), 0);
    return 0;
}

/* ── FUN_004a0c1d — AI brain step 2 (per-turn diplomacy decision) ────────── */

#define DREL(p, o)  PREC(p)[0x84 + (o) * 0x10]   /* relation p→o (i8) */
#define DPRS(p, o)  PREC(p)[0x86 + (o) * 0x10]   /* contact/pressure p→o (u8) */

/* Pass 1 warms the relation toward each AI slot (+5 == 0) by 1, clamped to +30;
 * human slots (+5 != 0) are skipped.  Isolated by leaving the player lordless
 * and pinning the year below the ally-seek gate. */
int test_army_ai_diplomacy_warm_relations(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    engine_event_queue_init(&evq);
    s_slot_00553edc = 0;                  /* year gate fails → pass 3 skipped */

    PREC(2)[0x081] = 0;                   /* no lord → pass 2 skipped */
    PREC(3)[0x005] = 0;                   /* AI → warmed */
    PREC(5)[0x005] = 0;                   /* AI → warmed (already at cap) */
    PREC(1)[0x005] = 1;                   /* human → skipped */
    PREC(2)[0x005] = 1;                   /* keep self out of the warm loop */
    DREL(2, 3) = 5;
    DREL(2, 5) = 0x1e;                    /* at +30 cap */
    DREL(2, 1) = 5;

    engine_army_ai_run_diplomacy(2, &evq);

    T_ASSERT_EQ_I((int8_t)DREL(2, 3), 6);     /* warmed +1 */
    T_ASSERT_EQ_I((int8_t)DREL(2, 5), 0x1e);  /* clamped at +30 */
    T_ASSERT_EQ_I((int8_t)DREL(2, 1), 5);     /* human → untouched */
    return 0;
}

/* Pass 2: a lord that has dropped out of the game (+4 == 0) has its treaty
 * cleared outright and the step returns. */
int test_army_ai_diplomacy_dead_lord_clears(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    engine_event_queue_init(&evq);

    PREC(2)[0x081] = 3;                   /* lord = 3 */
    PREC(2)[0x085 + 3 * 0x10] = 1;        /* treaty toward 3 */
    PREC(2)[0x01e] = 9;
    PREC(3)[0x004] = 0;                   /* lord inactive */

    engine_army_ai_run_diplomacy(2, &evq);

    T_ASSERT_EQ_I(PREC(2)[0x081], 0);             /* lord cleared */
    T_ASSERT_EQ_I(PREC(2)[0x085 + 3 * 0x10], 0);  /* treaty cleared */
    T_ASSERT_EQ_I(PREC(2)[0x01e], 0);
    return 0;
}

/* Pass 2: betrayal pressure from a soured relation tops personality 1's
 * threshold (5) → clear treaty + post a 0xb5 notification. */
int test_army_ai_diplomacy_betrayal_fires(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    engine_event_queue_init(&evq);
    s_slot_00553edc = 0;                  /* year gate fails → pass 3 skipped */
    s_slot_00554004 = 5;                  /* not a thinning field (no +25) */
    s_slot_00553d24 = 0;                  /* lord is not the leader (no bonus) */
    s_slot_0057c8cc = 3;                  /* local player = lord → 0xb5 publishes */

    PREC(2)[0x004] = 1; PREC(2)[0x005] = 0; PREC(2)[0x007] = 1;  /* AI, personality 1 */
    PREC(2)[0x081] = 3;                   /* lord = 3 */
    PREC(2)[0x085 + 3 * 0x10] = 1;        /* treaty toward 3 */
    PREC(3)[0x004] = 1; PREC(3)[0x005] = 0;  /* lord active + AI */
    DREL(2, 3) = (uint8_t)(-20);          /* < -10 even after pass-1 warm (-19) */

    engine_army_ai_run_diplomacy(2, &evq);

    T_ASSERT_EQ_I(PREC(2)[0x081], 0);     /* treaty cleared on betrayal */
    T_ASSERT_EQ_I(evq.write_idx, 1);      /* one notification published */
    T_ASSERT_EQ_I(evq.ring[0].code, 0xb5);
    T_ASSERT_EQ_I(evq.ring[0].audience, 3);
    return 0;
}

/* Pass 2: pressure below threshold leaves the bond intact and posts nothing. */
int test_army_ai_diplomacy_no_betrayal_below_threshold(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    engine_event_queue_init(&evq);
    s_slot_00553edc = 0;
    s_slot_00554004 = 5;
    s_slot_00553d24 = 0;

    PREC(2)[0x004] = 1; PREC(2)[0x005] = 0; PREC(2)[0x007] = 4;  /* personality 4: thr 20 */
    PREC(2)[0x081] = 3;
    PREC(3)[0x004] = 1; PREC(3)[0x005] = 0;
    DREL(2, 3) = (uint8_t)(-20);          /* +10 pressure < 20 threshold */

    engine_army_ai_run_diplomacy(2, &evq);

    T_ASSERT_EQ_I(PREC(2)[0x081], 3);     /* lord kept */
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* Pass 3: a lordless player past the year gate, whose personality cooldown
 * elapses, bonds directly with an AI candidate (engine_diplomacy_form_alliance). */
int test_army_ai_diplomacy_forms_alliance_with_ai(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    engine_event_queue_init(&evq);
    s_slot_00553edc = 0x500;              /* > 0x4f4 → year gate passes */

    PREC(2)[0x007] = 1;                   /* personality 1: ally period 12 */
    PREC(2)[0x081] = 0;                   /* lordless */
    PREC(2)[0x02b] = 2;                   /* not the leader */
    PREC(2)[0x0ec] = 11;                  /* +1 → 12, period 12 elapses */
    PREC(3)[0x004] = 1; PREC(3)[0x005] = 0; PREC(3)[0x02b] = 2;  /* eligible AI candidate */

    engine_army_ai_run_diplomacy(2, &evq);

    T_ASSERT_EQ_I(PREC(2)[0x080], 3);     /* candidate latched */
    T_ASSERT_EQ_I(PREC(2)[0x081], 3);     /* mutual bond formed */
    T_ASSERT_EQ_I(PREC(3)[0x081], 2);
    T_ASSERT_EQ_I(PREC(2)[0x0ec], 0);     /* cooldown reset */
    return 0;
}

/* Pass 3: a human candidate gets a 0xb4 proposal instead, latching the +0x1c
 * pending gate. */
int test_army_ai_diplomacy_proposes_to_human(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    engine_event_queue_init(&evq);
    s_slot_00553edc = 0x500;
    s_slot_0057c8cc = 3;                  /* local = candidate → proposal publishes */

    PREC(2)[0x007] = 1;
    PREC(2)[0x081] = 0;
    PREC(2)[0x02b] = 2;
    PREC(2)[0x0ec] = 11;
    PREC(3)[0x004] = 1; PREC(3)[0x005] = 1; PREC(3)[0x02b] = 2; PREC(3)[0x081] = 0;  /* human, lordless */

    engine_army_ai_run_diplomacy(2, &evq);

    T_ASSERT_EQ_I(PREC(2)[0x01c], 1);     /* pending gate latched */
    T_ASSERT_EQ_I(PREC(2)[0x081], 0);     /* no direct bond with a human */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xb4);
    T_ASSERT_EQ_I(evq.ring[0].audience, 3);
    return 0;
}

/* seed_buy_requests: the prosperity gate passes when the first owned realm has
 * approval > 14 AND morale > 19 — then the staples floor even for a level-1 AI. */
int test_army_ai_seed_buy_requests_prosperity_gate(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 1;
    uint8_t *s = PREC(2);
    s[0x029] = 0;
    AI32(s, 0x120) = 0; AI32(s, 0x130) = 200;   /* only +0x74 should floor */

    uint8_t *a1 = AREC(1);
    a1[0x005] = 2; a1[0x00c] = 15; a1[0x00b] = 20;   /* approval 15>14, morale 20>19 */
    a1[0x1c0] = 0;                                     /* no keep → no build fold */

    engine_army_ai_seed_buy_requests(2);

    T_ASSERT_EQ_I(AI32(s, 0x74), 0x32);   /* commodity-6 floored */
    T_ASSERT_EQ_I(AI32(s, 0x70), 0);      /* +0x130 not short → not floored */
    return 0;
}

/* ── Build-order planning (FUN_0049ed13 / FUN_0049e77d, brain step 0xc) ──
 * ai_turn_setup NULLs the econ/territory binds, so plan_resource /
 * allocate_realm_workforce / plan_realm all no-op — these exercise the pure
 * bookkeeping (tag rotation, affordability flag, gate enabling). */

/* mark_build_affordable: build tag 2 costs {3,10} against pools +0x130/+0x120;
 * both pools cover → the realm's +0x2c5 affordability flag is set. */
int test_army_ai_mark_build_affordable_sets_flag(void)
{
    ai_turn_setup();
    uint8_t *s = PREC(2);
    uint8_t *a = AREC(1);
    a[0x290] = 2;                       /* build tag 2 → cost {3, 10} */
    AI32(s, 0x130) = 3; AI32(s, 0x120) = 10;   /* exactly cover both */

    engine_army_ai_mark_build_affordable(2, 1);

    T_ASSERT_EQ_I(a[0x2c5], 1);
    return 0;
}

/* mark_build_affordable: the flag clears when either pool falls short (here the
 * +0x120 pool is one under the tag-2 cost of 10). */
int test_army_ai_mark_build_affordable_clears_when_short(void)
{
    ai_turn_setup();
    uint8_t *s = PREC(2);
    uint8_t *a = AREC(1);
    a[0x2c5] = 1;                       /* stale flag → must be cleared */
    a[0x290] = 2;                       /* cost {3, 10} */
    AI32(s, 0x130) = 100; AI32(s, 0x120) = 9;  /* +0x120 short */

    engine_army_ai_mark_build_affordable(2, 1);

    T_ASSERT_EQ_I(a[0x2c5], 0);
    return 0;
}

/* run_build_orders: rotation counter 1 + personality 1 selects build column
 * +0x54 (tag 1); the counter advances to 2. */
int test_army_ai_build_orders_rotates_tag(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 1;
    uint8_t *s = PREC(2);
    s[0x007] = 1;                       /* personality 1 → columns {0,1,4,2,5,2} */
    AI32(s, 0x06c) = 1;                 /* rotation counter 1 → column +0x54 */
    AREC(1)[0x005] = 2;                 /* realm 1 owned by player 2 */

    engine_army_ai_run_build_orders(2);

    T_ASSERT_EQ_I(AREC(1)[0x290], 1);   /* personality-1 column 1 = tag 1 */
    T_ASSERT_EQ_I(AI32(s, 0x06c), 2);   /* counter advanced */
    return 0;
}

/* run_build_orders: rotation counter 9 reads the last column (+0x64, tag 2 for
 * personality 1) and wraps back to 0. */
int test_army_ai_build_orders_rotation_wraps(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 1;
    uint8_t *s = PREC(2);
    s[0x007] = 1;
    AI32(s, 0x06c) = 9;                 /* column +0x64 → tag 2; then wrap */
    AREC(1)[0x005] = 2;

    engine_army_ai_run_build_orders(2);

    T_ASSERT_EQ_I(AREC(1)[0x290], 2);
    T_ASSERT_EQ_I(AI32(s, 0x06c), 0);   /* wrapped */
    return 0;
}

/* run_build_orders: with no secondary settlement, a present + unlocked bucket
 * lights its workforce gate, a locked bucket does not, and the realm's plan flag
 * is set. */
int test_army_ai_build_orders_enables_gates(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 1;
    PREC(2)[0x007] = 1;
    uint8_t *a = AREC(1);
    a[0x005] = 2;
    a[0x1c3] = 0;                       /* no secondary settlement → no filter */
    /* bucket 0: present + unlocked → gate lights */
    a[0 * 0x18 + 0x295] = 1; a[0 * 0x18 + 0x296] = 0;
    /* bucket 1: present but locked → gate stays clear */
    a[1 * 0x18 + 0x295] = 1; a[1 * 0x18 + 0x296] = 1;
    /* bucket 2: absent → gate stays clear */
    a[2 * 0x18 + 0x295] = 0; a[2 * 0x18 + 0x296] = 0;
    a[0 * 0x18 + 0x297] = 9;            /* stale enable → cleared first */

    engine_army_ai_run_build_orders(2);

    T_ASSERT_EQ_I(a[0 * 0x18 + 0x297], 1);
    T_ASSERT_EQ_I(a[1 * 0x18 + 0x297], 0);
    T_ASSERT_EQ_I(a[2 * 0x18 + 0x297], 0);
    T_ASSERT_EQ_I(a[0x1b0], 1);         /* plan settled */
    return 0;
}

/* run_build_orders: with a secondary settlement (+0x1c3) the per-bucket filter
 * drops buckets 1/2 outright, gates bucket 0 on build-draw-hi (+0x1d4 > 0) and
 * bucket 3 on build-draw-lo (+0x1d0 > 0). */
int test_army_ai_build_orders_settle_filter(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 1;
    PREC(2)[0x007] = 1;
    uint8_t *a = AREC(1);
    a[0x005] = 2;
    a[0x1c3] = 1;                       /* secondary settlement present */
    for (int slot = 0; slot < 4; slot++) {
        a[slot * 0x18 + 0x295] = 1;     /* all present + unlocked */
        a[slot * 0x18 + 0x296] = 0;
    }
    AI32(a, 0x1d4) = 5;                 /* draw-hi > 0 → bucket 0 allowed */
    AI32(a, 0x1d0) = 0;                 /* draw-lo == 0 → bucket 3 blocked */

    engine_army_ai_run_build_orders(2);

    T_ASSERT_EQ_I(a[0 * 0x18 + 0x297], 1);   /* bucket 0: draw-hi > 0 */
    T_ASSERT_EQ_I(a[1 * 0x18 + 0x297], 0);   /* bucket 1: always dropped */
    T_ASSERT_EQ_I(a[2 * 0x18 + 0x297], 0);   /* bucket 2: always dropped */
    T_ASSERT_EQ_I(a[3 * 0x18 + 0x297], 0);   /* bucket 3: draw-lo == 0 */
    return 0;
}

/* run_build_orders: realms owned by another player are skipped entirely — no tag
 * write, no gate change. */
int test_army_ai_build_orders_skips_unowned(void)
{
    ai_turn_setup();
    s_slot_0056d5dc = 2;
    PREC(2)[0x007] = 1;
    AREC(1)[0x005] = 3;                 /* owned by player 3, not 2 */
    AREC(1)[0x290] = 0x77;              /* sentinel — must stay untouched */

    engine_army_ai_run_build_orders(2);

    T_ASSERT_EQ_I(AREC(1)[0x290], 0x77);
    T_ASSERT_EQ_I(AREC(1)[0x1b0], 0);   /* plan flag not set for unowned realm */
    return 0;
}

/* ── Muster-realm scoring (FUN_004a0aaa) ──────────────────────────────────
 * Stores only realm indices, so these verify the selection.  Bind the econ
 * records (ai_turn_setup NULLs them) and pin prices so stockpile_value is
 * deterministic: stock A (+0x250) × price_a drives the rich/thin fork. */

/* The flush realm (provision×4 < stockpile) wins the +0xe5 muster slot; the thin
 * realm (stockpile < provision) wins the inverse +0xe6 slot. */
int test_army_ai_pick_muster_rich_vs_weak(void)
{
    ai_turn_setup();
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_set_prices(5, 10, 6);
    s_slot_0056d5dc = 2;

    uint8_t *a1 = AREC(1);              /* flush: base 4 → rich 5, weak 2 */
    a1[0x005] = 2; a1[0x00c] = 0x40;   /* approval neutral */
    AI32(a1, 0x024) = 1000;            /* provision → base (1000/500)*2 = 4 */
    AI32(a1, 0x250) = 1000;            /* stock A 1000×5 = 5000 > 4×1000 */

    uint8_t *a2 = AREC(2);             /* thin: base 4 → rich 2, weak 6 */
    a2[0x005] = 2; a2[0x00c] = 0x40;
    AI32(a2, 0x024) = 1000;
    AI32(a2, 0x250) = 0;               /* stockpile 0 < provision */

    engine_army_ai_pick_muster_realms(2);

    T_ASSERT_EQ_I(PREC(2)[0x0e5], 1);  /* flush realm = muster source */
    T_ASSERT_EQ_I(PREC(2)[0x0e6], 2);  /* thin realm = inverse score */

    engine_army_economy_bind_army_records(NULL);
    return 0;
}

/* A high-approval realm (+1) outscores a neutral one in both slots. */
int test_army_ai_pick_muster_approval_boost(void)
{
    ai_turn_setup();
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_set_prices(5, 10, 6);
    s_slot_0056d5dc = 2;

    /* both in the no-fork zone (provision ≤ stockpile ≤ 4×provision). */
    uint8_t *a1 = AREC(1);
    a1[0x005] = 2; a1[0x00c] = 0x60;   /* approval > 0x50 → +1 */
    AI32(a1, 0x024) = 1000; AI32(a1, 0x250) = 400;   /* 2000, in [1000,4000] */
    uint8_t *a2 = AREC(2);
    a2[0x005] = 2; a2[0x00c] = 0x40;   /* approval neutral */
    AI32(a2, 0x024) = 1000; AI32(a2, 0x250) = 400;

    engine_army_ai_pick_muster_realms(2);

    T_ASSERT_EQ_I(PREC(2)[0x0e5], 1);  /* boosted realm wins both */
    T_ASSERT_EQ_I(PREC(2)[0x0e6], 1);

    engine_army_economy_bind_army_records(NULL);
    return 0;
}

/* Equal scores resolve to the higher realm index (retail's <= update). */
int test_army_ai_pick_muster_tie_higher_index(void)
{
    ai_turn_setup();
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_set_prices(5, 10, 6);
    s_slot_0056d5dc = 2;

    for (int k = 1; k <= 2; k++) {
        uint8_t *a = AREC(k);
        a[0x005] = 2; a[0x00c] = 0x40;
        AI32(a, 0x024) = 1000; AI32(a, 0x250) = 400;   /* identical, no fork */
    }

    engine_army_ai_pick_muster_realms(2);

    T_ASSERT_EQ_I(PREC(2)[0x0e5], 2);
    T_ASSERT_EQ_I(PREC(2)[0x0e6], 2);

    engine_army_economy_bind_army_records(NULL);
    return 0;
}

/* Realms owned by another player are not scored. */
int test_army_ai_pick_muster_skips_unowned(void)
{
    ai_turn_setup();
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_set_prices(5, 10, 6);
    s_slot_0056d5dc = 2;

    PREC(2)[0x0e5] = 0x33;             /* sentinels */
    PREC(2)[0x0e6] = 0x33;
    uint8_t *a1 = AREC(1);             /* owned by player 3 */
    a1[0x005] = 3; a1[0x00c] = 0x40; AI32(a1, 0x024) = 1000; AI32(a1, 0x250) = 400;

    engine_army_ai_pick_muster_realms(2);

    T_ASSERT_EQ_I(PREC(2)[0x0e5], 0);  /* no owned realm → realm 0 recorded */
    T_ASSERT_EQ_I(PREC(2)[0x0e6], 0);

    engine_army_economy_bind_army_records(NULL);
    return 0;
}

/* ── Attack-target selection (FUN_004a0649) ───────────────────────────────
 * Needs the realm records clean; should_engage reads the player's strategic
 * record (lord +0x81 must not be the target owner). */

/* The nearest adjacent enemy realm is chosen (lowest king-move score). */
int test_army_ai_pick_attack_target_nearest(void)
{
    ai_turn_setup();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;

    AREC(1)[0x005] = 2; AREC(1)[0x06c] = 10; AREC(1)[0x06d] = 10;  /* own realm + home */
    AREC(2)[0x005] = 2;                                            /* own → skipped */
    uint8_t *t = AREC(3);                                          /* enemy target */
    t[0x005] = 4; t[0x06c] = 12; t[0x06d] = 10;                   /* dist 2 */
    t[0x05c] = 1;                                                  /* borders realm 1 (player 2) */

    T_ASSERT_EQ_I(engine_army_ai_pick_attack_target(2, 1), 3);
    return 0;
}

/* A richer, defended target can outscore a closer poor one (wealth bonus beats
 * distance). */
int test_army_ai_pick_attack_target_prefers_rich(void)
{
    ai_turn_setup();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 4;

    AREC(1)[0x005] = 2; AREC(1)[0x06c] = 10; AREC(1)[0x06d] = 10;

    uint8_t *near = AREC(3);                       /* close + poor → score 2 */
    near[0x005] = 4; near[0x06c] = 12; near[0x06d] = 10; near[0x05c] = 1;

    uint8_t *rich = AREC(4);                       /* far but rich → score 10−0x14−0xa = −20 */
    rich[0x005] = 4; rich[0x06c] = 20; rich[0x06d] = 10; rich[0x05c] = 1;
    AI32(rich, 0x250) = 0xc9;                      /* stock ≥ 0xc9 → −0x14 */
    AI32(rich, 0x224) = 0x3e9;                     /* provision ≥ 0x3e9 → −0xa */

    T_ASSERT_EQ_I(engine_army_ai_pick_attack_target(2, 1), 4);
    return 0;
}

/* An enemy realm with no player-owned neighbour is not a target → 0. */
int test_army_ai_pick_attack_target_requires_border(void)
{
    ai_turn_setup();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;

    AREC(1)[0x005] = 2; AREC(1)[0x06c] = 10; AREC(1)[0x06d] = 10;
    uint8_t *t = AREC(3);
    t[0x005] = 4; t[0x06c] = 12; t[0x06d] = 10;    /* enemy, near — but no +0x5c border */

    T_ASSERT_EQ_I(engine_army_ai_pick_attack_target(2, 1), 0);
    return 0;
}

/* ── select_target (FUN_004a03f2): stores the chosen realm in +0x4b ──────── */

/* forced_owner mode: only realms owned by that owner are considered (the
 * engage/border checks are skipped); the winner lands in +0x4b. */
int test_army_ai_select_target_forced_owner(void)
{
    ai_turn_setup();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;

    AREC(1)[0x06c] = 10; AREC(1)[0x06d] = 10;       /* from-realm capital */
    AREC(2)[0x005] = 4; AREC(2)[0x06c] = 12; AREC(2)[0x06d] = 10;  /* owner 4 */
    AREC(3)[0x005] = 7;                             /* owner 7 → excluded */

    int ret = engine_army_ai_select_target(2, 1, 0, 4);
    T_ASSERT_EQ_I(ret, 1);
    T_ASSERT_EQ_I(PREC(2)[0x04b], 2);
    return 0;
}

/* gate_owner mode: with the gate set, only realms owned by the stored gate +0x48
 * (and engageable + bordering) qualify. */
int test_army_ai_select_target_owner_gate(void)
{
    ai_turn_setup();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 4;
    PREC(2)[0x081] = 0;                 /* no lord → should_engage allows enemies */
    PREC(2)[0x048] = 5;                 /* gate owner = 5 */

    AREC(1)[0x005] = 2; AREC(1)[0x06c] = 10; AREC(1)[0x06d] = 10;  /* player's own border realm */
    uint8_t *g = AREC(3);              /* owner 5 → passes gate */
    g[0x005] = 5; g[0x06c] = 12; g[0x06d] = 10; g[0x05c] = 1;
    uint8_t *o = AREC(4);              /* owner 6 → fails gate */
    o[0x005] = 6; o[0x06c] = 11; o[0x06d] = 10; o[0x05c] = 1;

    int ret = engine_army_ai_select_target(2, 1, 1, 0);
    T_ASSERT_EQ_I(ret, 1);
    T_ASSERT_EQ_I(PREC(2)[0x04b], 3);
    return 0;
}

/* No eligible realm (enemy present but not bordering) → +0x4b cleared, returns 0. */
int test_army_ai_select_target_none(void)
{
    ai_turn_setup();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;
    PREC(2)[0x081] = 0;
    PREC(2)[0x04b] = 0x55;             /* sentinel — must be overwritten with 0 */

    AREC(1)[0x005] = 2; AREC(1)[0x06c] = 10; AREC(1)[0x06d] = 10;
    AREC(3)[0x005] = 4; AREC(3)[0x06c] = 12; AREC(3)[0x06d] = 10;  /* enemy, no +0x5c border */

    int ret = engine_army_ai_select_target(2, 1, 0, 0);
    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(PREC(2)[0x04b], 0);
    return 0;
}

/* ── most_hostile_player (FUN_004a0309) ───────────────────────────────────── */

/* A pinned war target (+0xeb) overrides the relation scan. */
int test_army_ai_most_hostile_pinned(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    PREC(2)[0x0eb] = 4;
    T_ASSERT_EQ_I(engine_army_ai_most_hostile_player(2), 4);
    return 0;
}

/* Without a pin, the active slot the player's relation is most negative toward
 * (below −10) wins; a boundary −10 does not qualify. */
int test_army_ai_most_hostile_scan(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    uint8_t *s = PREC(2);
    PREC(1)[0x04] = 1; PREC(3)[0x04] = 1; PREC(4)[0x04] = 1; PREC(5)[0x04] = 1;
    s[0x84 + 1 * 0x10] = (uint8_t)(int8_t)-10;   /* boundary → excluded */
    s[0x84 + 3 * 0x10] = (uint8_t)(int8_t)-15;
    s[0x84 + 4 * 0x10] = (uint8_t)(int8_t)-20;   /* most negative → chosen */
    s[0x84 + 5 * 0x10] = 0;

    T_ASSERT_EQ_I(engine_army_ai_most_hostile_player(2), 4);
    return 0;
}

/* An inactive slot is skipped even if it is the most hated. */
int test_army_ai_most_hostile_skips_inactive(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    uint8_t *s = PREC(2);
    PREC(3)[0x04] = 0;                            /* inactive */
    s[0x84 + 3 * 0x10] = (uint8_t)(int8_t)-30;    /* hated hardest but out of game */
    PREC(4)[0x04] = 1;
    s[0x84 + 4 * 0x10] = (uint8_t)(int8_t)-12;    /* active, below −10 → chosen */

    T_ASSERT_EQ_I(engine_army_ai_most_hostile_player(2), 4);
    return 0;
}

/* No slot hated below −10 → 0. */
int test_army_ai_most_hostile_none(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    PREC(3)[0x04] = 1; PREC(4)[0x04] = 1;
    PREC(2)[0x84 + 3 * 0x10] = (uint8_t)(int8_t)-8;
    T_ASSERT_EQ_I(engine_army_ai_most_hostile_player(2), 0);
    return 0;
}

/* ── launch_attack (FUN_004a0015), decision/gate logic ────────────────────
 * The full spawn path is exercised in test_engine_combat.c; these cover the
 * cooldown tick and the early-out gates (no enemy / un-prosperous muster). */

/* While the attack cooldown (+0x15a) is set, the step only ticks it down. */
int test_army_ai_launch_attack_cooldown_ticks(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    PREC(2)[0x15a] = 3;
    engine_army_ai_launch_attack(2);
    T_ASSERT_EQ_I(PREC(2)[0x15a], 2);
    return 0;
}

/* Cooldown ready but no hostile enemy → no-op (cooldown stays 0, not reloaded). */
int test_army_ai_launch_attack_no_enemy(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    PREC(2)[0x15a] = 0;                /* ready */
    /* no pin, no relation below −10 → most_hostile returns 0 */
    engine_army_ai_launch_attack(2);
    T_ASSERT_EQ_I(PREC(2)[0x15a], 0);
    return 0;
}

/* Target selected but the muster realm is not prosperous (provision < 400) → the
 * attack is gated before the raise: select_target still ran (+0x4b set) but the
 * cooldown is not reloaded. */
int test_army_ai_launch_attack_unprosperous_gates(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;
    PREC(2)[0x15a] = 0;
    PREC(2)[0x0eb] = 4;               /* pinned enemy 4 */
    PREC(2)[0x0e5] = 1;               /* muster realm 1 */

    AREC(1)[0x005] = 2;               /* muster realm */
    AI32(AREC(1), 0x024) = 399;       /* provision < 400 → gate blocks */
    AREC(1)[0x00c] = 30;              /* approval fine */
    AREC(3)[0x005] = 4;               /* an enemy-owned realm select_target can pick */
    AREC(3)[0x06c] = 5; AREC(3)[0x06d] = 5;

    engine_army_ai_launch_attack(2);
    T_ASSERT_EQ_I(PREC(2)[0x04b], 3);  /* select_target ran */
    T_ASSERT_EQ_I(PREC(2)[0x15a], 0);  /* but cooldown not reloaded */
    return 0;
}

/* ── Per-tick AI brain dispatcher (FUN_0049a581) ──────────────────────────
 * These cover the dispatcher's own FSM bookkeeping — the round-robin cursor,
 * first-tick init, the human/parked gates, the per-tick state advance, and the
 * terminal park.  Every realm here sits on a stub state (7) or state 0/17 so
 * only the dispatcher's logic runs; the step bodies have their own tests. */

/* A realm on a stub state: dispatch is a no-op, the state advances by one, and
 * the round-robin cursor steps to the next slot. */
int test_army_ai_per_tick_advances_stub_state(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    engine_event_queue_init(&evq);
    PREC(2)[0x04] = 1;                       /* in-game */
    AI32(PREC(2), 0x00) = 7;                 /* stub step 7 */
    s_slot_0057c944 = 2;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(AI32(PREC(2), 0x00), 8);   /* state advanced */
    T_ASSERT_EQ_I((int)s_slot_0057c944, 3);  /* cursor stepped */
    return 0;
}

/* The round-robin skips inactive slots and wraps the cursor 5→1. */
int test_army_ai_per_tick_round_robin_skips_inactive(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    engine_event_queue_init(&evq);
    PREC(5)[0x04] = 1;                       /* only slot 5 is in-game */
    AI32(PREC(5), 0x00) = 7;
    s_slot_0057c944 = 4;                     /* starts before slot 5 */

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(AI32(PREC(5), 0x00), 8);   /* slot 5 advanced */
    T_ASSERT_EQ_I((int)s_slot_0057c944, 1);  /* 4→5, then 6 wraps to 1 */
    return 0;
}

/* No in-game realm: the cursor sweeps all five (wrapping back to its start) and
 * the call returns without touching any FSM state. */
int test_army_ai_per_tick_no_active_realm(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    engine_event_queue_init(&evq);
    s_slot_0057c944 = 1;                     /* all slots inactive */

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I((int)s_slot_0057c944, 1);  /* +5 increments, wrapping → 1 */
    return 0;
}

/* A human-owned realm (+0x05 != 0, quirk #190) is skipped: no dispatch, no
 * state advance — but the round-robin cursor still steps. */
int test_army_ai_per_tick_human_skipped(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    engine_event_queue_init(&evq);
    PREC(2)[0x04] = 1;                       /* in-game */
    PREC(2)[0x05] = 1;                       /* human-owned */
    AI32(PREC(2), 0x00) = 7;                 /* non-zero state → no init */
    s_slot_0057c944 = 2;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(AI32(PREC(2), 0x00), 7);   /* state untouched */
    T_ASSERT_EQ_I((int)s_slot_0057c944, 3);  /* cursor still stepped */
    return 0;
}

/* First-tick init (state 0): runs the elimination/aggregate init, clears the
 * proposal gate (+0x1c), sets state := 1, then step 1 runs (empty inbox here)
 * and the tail increment advances it to 2. */
int test_army_ai_per_tick_init_sets_state(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    engine_event_queue_init(&evq);
    s_slot_0056d5dc = 0;                     /* no realms → init is headless */
    PREC(2)[0x04] = 1;                       /* in-game */
    PREC(2)[0x1c] = 9;                       /* proposal gate set — init clears it */
    AI32(PREC(2), 0x00) = 0;                 /* state 0 → init */
    s_slot_0057c944 = 2;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(AI32(PREC(2), 0x00), 2);   /* 0 → init → 1 → step 1 → 2 */
    T_ASSERT_EQ_I(PREC(2)[0x1c], 0);         /* proposal gate cleared */
    return 0;
}

/* Terminal park: once the state crosses the per-slot threshold (sel*2+15) and
 * no owned army movement is pending, the realm is parked at 999 (then ++ to
 * 0x3e8 by the tail increment). */
int test_army_ai_per_tick_terminal_parks(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);   /* no pending movement */
    engine_event_queue_init(&evq);
    PREC(1)[0x04] = 1;                       /* in-game */
    AI32(PREC(1), 0x00) = 17;                /* == threshold 1*2+15 */
    s_slot_0057c944 = 1;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(AI32(PREC(1), 0x00), 0x3e8);  /* parked at 999, then ++ */
    return 0;
}

/* ── Brain step 1: diplomacy-inbox processor (FUN_004a277d + handlers) ─────
 * These drive the dispatcher on a realm sitting at state 1, so case 0x1 runs
 * the real inbox processor.  The cursor starts on the realm so the round-robin
 * selects it (sel = cursor, then cursor steps).  Relation lives at +0x84 +
 * other*0x10; the per-pair best-offer at +0x8c; the mood-cycle at +0x159. */

/* Stage one diplomacy message into receiver `rcv`'s inbox slot `i`. */
static void inbox_set(int rcv, int i, uint8_t sender, uint8_t type,
                      uint8_t param, int32_t value)
{
    uint8_t *e = &s_slot_0053f0f0[rcv * 0x28 + i * 8];
    e[0] = sender;
    e[1] = type;
    e[2] = param;
    *(int32_t *)(e + 4) = value;
}

/* An empty inbox: the processor drains nothing, resets the per-pair +0x90
 * scratch bytes, and the FSM advances 1 → 2. */
int test_army_ai_step1_empty_inbox_clears(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_0053f0f0, 0, sizeof s_slot_0053f0f0);
    engine_event_queue_init(&evq);
    PREC(2)[0x04] = 1;                       /* in-game */
    AI32(PREC(2), 0x00) = 1;                 /* state 1 → step 1 */
    PREC(2)[0x90 + 3 * 0x10] = 7;            /* a stale +0x90 scratch byte */
    s_slot_0057c944 = 2;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(PREC(2)[0x90 + 3 * 0x10], 0);  /* scratch reset */
    T_ASSERT_EQ_I(AI32(PREC(2), 0x00), 2);       /* state advanced */
    return 0;
}

/* type 0 — a generous offer (over the personality band) warms the relation by
 * +10, latches the per-pair best-offer, bumps the mood-cycle, and drains the
 * inbox slot. */
int test_army_ai_step1_offer_warms_and_posts(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_0053f0f0, 0, sizeof s_slot_0053f0f0);
    engine_event_queue_init(&evq);
    PREC(1)[0x04] = 1;                       /* p1 in-game */
    PREC(1)[0x07] = 1;                       /* personality 1 → offer band 100 */
    AI32(PREC(1), 0x00) = 1;                 /* state 1 */
    PREC(2)[0x05] = 1;                       /* sender active (type-0 gate) */
    inbox_set(1, 0, /*sender*/2, /*type*/0, /*param*/0, /*value*/1000);
    s_slot_0057c944 = 1;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I((int8_t)PREC(1)[0x84 + 2 * 0x10], 10);  /* relation +10 */
    T_ASSERT_EQ_I(AI32(PREC(1), 0x8c + 2 * 0x10), 1000);  /* best-offer latched */
    T_ASSERT_EQ_I(PREC(1)[0x159], 1);                     /* mood-cycle bumped */
    T_ASSERT_EQ_I(s_slot_0053f0f0[1 * 0x28 + 0], 0);      /* inbox slot drained */
    T_ASSERT_EQ_I(AI32(PREC(1), 0x00), 2);                /* state advanced */
    return 0;
}

/* type 2 — a notified treaty break: clears p1's lord bond to p2, sours the
 * relation by 20, posts (mood-cycle bumps). */
int test_army_ai_step1_break_clears_treaty(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_0053f0f0, 0, sizeof s_slot_0053f0f0);
    engine_event_queue_init(&evq);
    PREC(1)[0x04] = 1;                       /* p1 in-game */
    PREC(1)[0x81] = 2;                       /* p1's lord is p2 */
    AI32(PREC(1), 0x00) = 1;                 /* state 1 */
    PREC(2)[0x05] = 1;                       /* sender active (type-2 gate) */
    inbox_set(1, 0, /*sender*/2, /*type*/2, 0, 0);
    s_slot_0057c944 = 1;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(PREC(1)[0x81], 0);                       /* lord bond cleared */
    T_ASSERT_EQ_I((int8_t)PREC(1)[0x84 + 2 * 0x10], -20);  /* relation -20 */
    T_ASSERT_EQ_I(PREC(1)[0x159], 1);                      /* mood-cycle bumped */
    return 0;
}

/* type 3 — an alliance proposal accepted on a warm relation (>= 11, no RNG
 * roll): the relation gains +4 and a mutual lord bond is forged. */
int test_army_ai_step1_alliance_accept_forges(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_0053f0f0, 0, sizeof s_slot_0053f0f0);
    engine_event_queue_init(&evq);
    PREC(1)[0x04] = 1;                       /* p1 in-game */
    AI32(PREC(1), 0x00) = 1;                 /* state 1 */
    PREC(1)[0x84 + 2 * 0x10] = 20;           /* warm relation → accept */
    s_slot_00554004 = 5;                     /* plenty of players left */
    inbox_set(1, 0, /*sender*/2, /*type*/3, 0, 0);
    s_slot_0057c944 = 1;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I((int8_t)PREC(1)[0x84 + 2 * 0x10], 24);  /* relation +4 */
    T_ASSERT_EQ_I(PREC(1)[0x81], 2);                      /* p2 forged as lord */
    T_ASSERT_EQ_I(PREC(2)[0x81], 1);                      /* mutual bond */
    return 0;
}

/* Loop control: a self-addressed slot is skipped (not dispatched), and the scan
 * stops at the first empty slot — a message queued *after* a gap is never
 * processed. */
int test_army_ai_step1_skips_self_and_stops_at_gap(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_0053f0f0, 0, sizeof s_slot_0053f0f0);
    engine_event_queue_init(&evq);
    PREC(1)[0x04] = 1;                       /* p1 in-game */
    PREC(1)[0x07] = 1;                       /* personality 1 */
    AI32(PREC(1), 0x00) = 1;                 /* state 1 */
    PREC(2)[0x05] = 1;                       /* senders active */
    PREC(3)[0x05] = 1;
    inbox_set(1, 0, /*sender*/1, /*type*/0, 0, 1000);   /* self → skipped */
    inbox_set(1, 1, /*sender*/2, /*type*/0, 0, 1000);   /* processed */
    /* slot 2 left empty → break */
    inbox_set(1, 3, /*sender*/3, /*type*/0, 0, 1000);   /* after gap → ignored */
    s_slot_0057c944 = 1;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I((int8_t)PREC(1)[0x84 + 2 * 0x10], 10); /* from-2 handler ran */
    T_ASSERT_EQ_I((int8_t)PREC(1)[0x84 + 1 * 0x10], 0);  /* self never dispatched */
    T_ASSERT_EQ_I((int8_t)PREC(1)[0x84 + 3 * 0x10], 0);  /* post-gap never reached */
    T_ASSERT_EQ_I(PREC(1)[0x159], 1);                    /* exactly one post */
    return 0;
}

/* ── Brain step 9: offensive-campaign planner (FUN_0049f977) ────────────────
 * Occupant ("army") record accessor — g_per_territory_420, stride 0x1a4. */
#define OREC(i)  (&g_per_territory_420[(i) * 0x1a4])

/* No campaign and no pinned enemy: step 9 just ticks the patience counter
 * (+0x45) and returns before selecting any target. */
int test_army_ai_campaign_patience_ticks(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    uint8_t *s = PREC(2);
    s[0x007] = 1;                       /* personality 1 → period 3 */

    engine_army_ai_run_campaign(2);

    T_ASSERT_EQ_I(s[0x045], 1);         /* patience ticked (1 < period 3 → returned) */
    T_ASSERT_EQ_I(s[0x04b], 0);         /* no attack target selected */
    return 0;
}

/* A committed campaign target (+0xe8) that turns out to be the player's own
 * realm is dropped by the validator; with no fresh target available the call
 * returns leaving the campaign target cleared. */
int test_army_ai_campaign_validate_drops_self_target(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    uint8_t *s = PREC(2);
    s[0x007] = 1;
    s[0x0e8] = 3;                       /* committed target realm 3 */
    s[0x081] = 5;                       /* has a lord (so the no-lord drop is not the cause) */
    s[0x0e5] = 1;                       /* muster realm */
    AREC(3)[0x005] = 2;                 /* realm 3 is owned by SELF → validator drops it */

    engine_army_ai_run_campaign(2);

    T_ASSERT_EQ_I(s[0x0e8], 0);         /* self-owned target dropped */
    return 0;
}

/* Committed campaign against a lean muster realm: step 9 redirects the best
 * existing unit (pick_best_unit) and stamps it with an advance order (2, since
 * the target owner is not the player's lord).  With no flagged owned cell the
 * deploy finder falls back to the target realm's capital. */
int test_army_ai_campaign_deploys_via_pick_unit(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_overworld_map, 0, sizeof g_overworld_map);

    uint8_t *s = PREC(2);
    s[0x007] = 1;                       /* personality 1 → budget 750 */
    s[0x0e8] = 3;                       /* committed target realm 3 */
    s[0x081] = 5;                       /* lord = realm 5 (≠ target owner, ≠ self) */
    s[0x0e5] = 1;                       /* muster realm 1 */

    AREC(3)[0x005] = 4;                 /* realm 3 owned by player 4 */
    AREC(3)[0x06c] = 7;                 /* capital col (owned-cell fallback) */
    AREC(3)[0x06d] = 8;                 /* capital row */
    AI32(AREC(1), 0x024) = 0;           /* lean muster realm → budget(375) ≥ feed → pick path */

    uint8_t *c = OREC(1);
    c[0x000] = 2;                       /* unit owned by player 2 */
    c[0x010] = 3;                       /* home region == target → pick score 300 */
    c[0x150] = 1;                       /* placed (eligible) */
    AI32(c, 0x168) = 250;              /* head count ≥ 200 */

    engine_army_ai_run_campaign(2);

    T_ASSERT_EQ_I(s[0x0e8], 0);         /* campaign launched → target cleared */
    T_ASSERT_EQ_I(s[0x04b], 3);         /* attack target committed */
    T_ASSERT_EQ_I(OREC(1)[0x01a], 2);   /* order 2 (target owner ≠ lord) */
    T_ASSERT_EQ_I(OREC(1)[0x151], 3);   /* attack-target realm recorded */
    T_ASSERT_EQ_I(OREC(1)[0x150], 0);   /* placed cleared → redeploys */
    T_ASSERT_EQ_I(OREC(1)[0x19b], 3);   /* campaign target stamped */
    T_ASSERT_EQ_I(OREC(1)[0x016], 7);   /* dest = capital fallback (no 0x40 cell) */
    T_ASSERT_EQ_I(OREC(1)[0x017], 8);
    return 0;
}

/* Committed campaign against a lord-owned but defended realm (kept by the
 * validator): the muster realm holds a keep with a pending garrison, so the
 * deploy finder uses the settled-cell scan (flag 0x80 + cropland terrain), and
 * the order is the lord-directed move code 6. */
int test_army_ai_campaign_deploys_finds_settled_cell(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_overworld_map, 0, sizeof g_overworld_map);

    uint8_t *s = PREC(2);
    s[0x007] = 1;
    s[0x0e8] = 3;                       /* committed target realm 3 */
    s[0x081] = 5;                       /* lord = realm 5 */
    s[0x0e5] = 1;                       /* muster realm 1 */

    AREC(3)[0x005] = 5;                 /* realm 3 owned by the LORD (5) */
    AI32(AREC(3), 0x19c) = 1;          /* defended (overhead-lo ≠ 0) → validator keeps it */
    AREC(3)[0x1c0] = 1;                 /* keep level set */
    AI32(AREC(3), 0x1bc) = 9;          /* pending garrison ≠ 0 → settled-cell finder */
    AI32(AREC(1), 0x024) = 0;           /* lean muster realm → pick path */

    uint8_t *c = OREC(1);
    c[0x000] = 2; c[0x010] = 3; c[0x150] = 1; AI32(c, 0x168) = 250;

    /* one owned settled cell for region 3 at (col 20, row 5): offset
     * (5*64 + 20)*8 = 2720 — flag 0x80, terrain 0x16 (in the 0x15..0x19 window). */
    int ofs = (5 * 64 + 20) * 8;
    g_overworld_map[ofs + 7] = 3;       /* owning region */
    g_overworld_map[ofs + 1] = 0x80;    /* region-event flag */
    g_overworld_map[ofs + 0] = 0x16;    /* cropland-class terrain */

    engine_army_ai_run_campaign(2);

    T_ASSERT_EQ_I(s[0x0e8], 0);         /* launched */
    T_ASSERT_EQ_I(OREC(1)[0x01a], 6);   /* lord-directed move order */
    T_ASSERT_EQ_I(OREC(1)[0x016], 20);  /* dest = the settled cell */
    T_ASSERT_EQ_I(OREC(1)[0x017], 5);
    return 0;
}

/* Muster-branch selection: when the muster realm can bear the force budget the
 * planner raises a fresh force, but an easy-AI-unfunded personality below the
 * force-credit floor (+0x138 < personality +0x68) bails before mustering, so the
 * campaign target survives and no order is stamped. */
int test_army_ai_campaign_muster_force_floor_gates(void)
{
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);

    uint8_t *s = PREC(2);
    s[0x007] = 1;                       /* personality 1: not a funding (3/4) cheat → fund=0 */
    s[0x029] = 5;                       /* AI level ≥ 2 → fund_campaign returns 0 outright */
    s[0x0e8] = 3;                       /* committed target realm 3 */
    s[0x081] = 5;
    s[0x0e5] = 1;                       /* muster realm 1 */
    AI32(s, 0x138) = 50;               /* force credit below personality-1 floor (100) */

    AREC(3)[0x005] = 4;                 /* target owned by player 4 (validator keeps it) */
    AI32(AREC(1), 0x024) = 400;         /* feed 400 > budget 375 → muster branch */

    engine_army_ai_run_campaign(2);

    T_ASSERT_EQ_I(s[0x0e8], 3);         /* bailed before mustering → target NOT cleared */
    T_ASSERT_EQ_I(OREC(1)[0x01a], 0);   /* no deployment order stamped */
    return 0;
}

/* Dispatcher wiring: a realm parked on FSM state 9 routes through run_campaign
 * (here it just ticks patience), then the dispatcher advances the state by one
 * and steps the round-robin cursor. */
int test_army_ai_per_tick_step9_dispatches(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    engine_event_queue_init(&evq);
    PREC(2)[0x04] = 1;                  /* in-game */
    PREC(2)[0x07] = 1;                  /* personality 1 */
    AI32(PREC(2), 0x00) = 9;            /* FSM state 9 → step 9 */
    s_slot_0057c944 = 2;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(AI32(PREC(2), 0x00), 10);  /* state advanced past step 9 */
    T_ASSERT_EQ_I(PREC(2)[0x45], 1);         /* run_campaign ticked patience */
    T_ASSERT_EQ_I((int)s_slot_0057c944, 3);  /* cursor stepped */
    return 0;
}

/* ── Brain step 0xb: army-movement planner (FUN_004a5667) ───────────────────
 * These drive the public entry engine_army_ai_run_movement, which dispatches
 * each owned army to its order handler then runs the cost-field pathfinder.
 * OREC (the occupant accessor) is defined with the step-9 tests above. */

/* Dispatcher wiring: a realm parked on FSM state 0xb routes through
 * run_movement (a no-op with no owned armies), then the state advances. */
int test_army_ai_per_tick_step0xb_dispatches(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    engine_event_queue_init(&evq);
    PREC(2)[0x04] = 1;                       /* in-game */
    AI32(PREC(2), 0x00) = 0xb;               /* FSM state 0xb → step 0xb */
    s_slot_0057c944 = 2;

    engine_army_ai_run_per_tick(&evq, 0, 0);

    T_ASSERT_EQ_I(AI32(PREC(2), 0x00), 0xc);   /* state advanced past step 0xb */
    T_ASSERT_EQ_I((int)s_slot_0057c944, 3);    /* cursor stepped */
    return 0;
}

/* The order gate resets an unknown order code to 2 (advance) and reports "no
 * destination set" (return 0), so the pathfinder is skipped this tick. */
int test_army_ai_movement_unknown_order_resets(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    engine_event_queue_init(&evq);

    uint8_t *c = OREC(1);
    c[0x008] = 1;                            /* active */
    c[0x000] = 2;                            /* owned by realm 2 */
    c[0x01a] = 0x20;                         /* unknown order */

    engine_army_ai_run_movement(2, &evq, 0);

    T_ASSERT_EQ_I(OREC(1)[0x01a], 2);        /* reset to advance */
    T_ASSERT_EQ_I(OREC(1)[0x14c], 0);        /* path-OK flag never set (dispatch returned 0) */
    return 0;
}

/* Order 7 (attack-march): an un-held army is pointed at its target realm's
 * deploy cell.  With no deployable overworld cell for the target, the combat
 * deploy finder falls back to that realm's capital cell. */
int test_army_ai_movement_attack_march_deploys(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    engine_event_queue_init(&evq);

    uint8_t *c = OREC(1);
    c[0x008] = 1; c[0x000] = 2;              /* active, owned by realm 2 */
    c[0x00a] = 5; c[0x00b] = 5;              /* current seat */
    c[0x01a] = 7;                            /* order: attack-march */
    c[0x199] = 0;                            /* not held */
    c[0x151] = 3;                            /* target realm 3 */

    AREC(3)[0x06c] = 9; AREC(3)[0x06d] = 4;  /* capital cell (deploy fallback) */

    engine_army_ai_run_movement(2, &evq, 0);

    T_ASSERT_EQ_I(OREC(1)[0x016], 9);        /* dest col = capital */
    T_ASSERT_EQ_I(OREC(1)[0x017], 4);        /* dest row = capital */
    T_ASSERT_EQ_I(OREC(1)[0x150], 0);        /* placed cleared */
    return 0;
}

/* Order 2 (advance): a hostile army within 6 tiles is intercepted — the army's
 * destination is set to the enemy's cell. */
int test_army_ai_movement_advance_engages_enemy(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    engine_event_queue_init(&evq);

    uint8_t *c = OREC(1);                    /* our army */
    c[0x008] = 1; c[0x000] = 2;              /* active, realm 2 */
    c[0x00a] = 10; c[0x00b] = 10;            /* seat (10,10) */
    c[0x01a] = 2;                            /* order: advance */
    c[0x199] = 0;                            /* not held */

    uint8_t *e = OREC(2);                    /* enemy army within 6 tiles */
    e[0x008] = 1; e[0x000] = 4;              /* active, realm 4 (enemy) */
    e[0x00a] = 12; e[0x00b] = 11;           /* (12,11): chebyshev dist 2 */
    e[0x198] = 0;                            /* idle (engageable) */
    e[0x01a] = 0;                            /* not order 2 → engage condition holds */
    /* realm 2 has no lord (+0x81 = 0) ≠ enemy owner 4 → engage allowed */

    engine_army_ai_run_movement(2, &evq, 0);

    T_ASSERT_EQ_I(OREC(1)[0x016], 12);       /* dest = enemy col */
    T_ASSERT_EQ_I(OREC(1)[0x017], 11);       /* dest = enemy row */
    T_ASSERT_EQ_I(OREC(1)[0x150], 0);        /* placed cleared */
    return 0;
}

/* Order 5 (siege): when the target realm is already ours the handler reports
 * "nothing to do" (return 0) and leaves the order code untouched. */
int test_army_ai_movement_siege_target_owned_noop(void)
{
    engine_event_queue_state_t evq;
    ai_turn_setup();
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    engine_event_queue_init(&evq);

    uint8_t *c = OREC(1);
    c[0x008] = 1; c[0x000] = 2;              /* active, realm 2 */
    c[0x01a] = 5;                            /* order: siege */
    c[0x151] = 3;                            /* target realm 3 */
    AREC(3)[0x005] = 2;                      /* target already owned by realm 2 */

    engine_army_ai_run_movement(2, &evq, 0);

    T_ASSERT_EQ_I(OREC(1)[0x01a], 5);        /* order untouched (returned 0) */
    T_ASSERT_EQ_I(OREC(1)[0x14c], 0);        /* no path set */
    return 0;
}
