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
