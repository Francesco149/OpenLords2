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
