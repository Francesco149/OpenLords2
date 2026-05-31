/*
 * Tests for engine_realm_advance_rating (FUN_0044baea) — the per-realm
 * approval-rating roll-forward run by the turn-advance engine.
 *
 * The pass reads/writes the per-army record block (g_per_army_768) and
 * reads the per-player strategic block (g_player_strategic_352) + the army
 * count / turn counter scalars; all are the canonical save globals (linked
 * from save.c), written directly by these tests.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_realm.h"
#include "save.h"
#include "save_schema_globals.h"

#define AREC(key)   (&g_per_army_768[(key) * 0x300])
#define PREC(slot)  (&g_player_strategic_352[(slot) * 0x160])
#define AI32(p, o)  (*(int32_t *)((p) + (o)))

/* One realm, owned by a human, turn 1; clean per-army + per-player state. */
static uint8_t *realm_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;       /* one realm */
    s_slot_00553240 = 1;       /* turn counter (divisor) ≥ 1 */
    return AREC(1);
}

/* Basic roll-forward: prev snapshot, three signed adjustments folded in,
 * display mirrors, per-turn delta clears, +0x2f4 decay, accumulate + average. */
int test_engine_realm_rating_rollforward(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 40;                     /* current rating */
    r[0x0e] = 3; r[0x10] = 2; r[0x11] = 1;   /* adjustment sources */
    r[0x05] = 1;                      /* owned by player 1 */
    PREC(1)[0x05] = 1;                /* player 1 is human */
    AI32(r, 0x24) = 100;             /* non-zero population */
    AI32(r, 0x2f4) = 20;             /* countdown */
    r[0x15] = r[0x17] = r[0x219] = 0xAA;     /* dirty the per-turn deltas */
    AI32(r, 0x194) = 0x1234;

    engine_realm_advance_rating();

    T_ASSERT_EQ_I(r[0x0d], 40);       /* previous = old current */
    T_ASSERT_EQ_I(r[0x0c], 46);       /* 40 + 3 + 2 + 1 */
    T_ASSERT_EQ_I(r[0x12], 3);        /* display mirrors */
    T_ASSERT_EQ_I(r[0x14], 2);
    T_ASSERT_EQ_I(r[0x13], 1);
    T_ASSERT_EQ_I(r[0x15], 0);        /* per-turn deltas cleared */
    T_ASSERT_EQ_I(r[0x17], 0);
    T_ASSERT_EQ_I(r[0x219], 0);
    T_ASSERT_EQ_I(AI32(r, 0x194), 0);
    T_ASSERT_EQ_I(AI32(r, 0x2f4), 15); /* 20 - 5 decay */
    T_ASSERT_EQ_I(AI32(r, 0x1c), 46);  /* lifetime sum */
    T_ASSERT_EQ_I(r[0x18], 46);        /* sum / turn(1) */
    return 0;
}

/* A zero-population, non-human-owned realm resets to 50, and (being unowned)
 * then takes the +5 nudge → 55 with delta 5. */
int test_engine_realm_rating_zero_pop_reset(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 40;
    r[0x05] = 0;                      /* unowned (owner slot 0) */
    AI32(r, 0x24) = 0;               /* zero population */
    /* PREC(0)[0x05] is 0 (reserved slot) → non-human → reset applies */

    engine_realm_advance_rating();

    T_ASSERT_EQ_I(r[0x0c], 55);       /* reset 50, then unowned nudge +5 */
    T_ASSERT_EQ_I(r[0x17], 5);        /* nudge delta overwrote the 0x32 reset */
    T_ASSERT_EQ_I(AI32(r, 0x1c), 55);
    T_ASSERT_EQ_I(r[0x18], 55);
    return 0;
}

/* The unowned-below-75 nudge fires at 70→75, but NOT at exactly 75. */
int test_engine_realm_rating_unowned_nudge_boundary(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 70;
    r[0x05] = 0;                      /* unowned */
    AI32(r, 0x24) = 100;             /* non-zero pop → no reset */
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 75);       /* 70 < 75 → +5 */
    T_ASSERT_EQ_I(r[0x17], 5);

    r = realm_reset();
    r[0x0c] = 75;
    r[0x05] = 0;
    AI32(r, 0x24) = 100;
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 75);       /* 75 < 75 false → no nudge */
    T_ASSERT_EQ_I(r[0x17], 0);
    return 0;
}

/* Signed 8-bit accumulation: an adjustment that overflows past +127 wraps
 * negative and is caught by the low clamp → 0; the high clamp pins >100. */
int test_engine_realm_rating_signed_clamp(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 100;
    r[0x0e] = 30;                     /* 100 + 30 = 130 → int8 wrap -126 */
    r[0x05] = 1; PREC(1)[0x05] = 1;   /* owned human → no reset/nudge */
    AI32(r, 0x24) = 100;
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 0);        /* wrapped negative → clamped to 0 */
    T_ASSERT_EQ_I(AI32(r, 0x1c), 0);

    r = realm_reset();
    r[0x0c] = 98;
    r[0x0e] = 5;                      /* 103 → high clamp */
    r[0x05] = 1; PREC(1)[0x05] = 1;
    AI32(r, 0x24) = 100;
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 100);
    return 0;
}

/* The running average divides the lifetime sum by the turn counter, and the
 * sum carries across turns. */
int test_engine_realm_rating_average_over_turns(void)
{
    uint8_t *r = realm_reset();
    s_slot_00553240 = 4;             /* turn 4 */
    r[0x0c] = 46;
    r[0x05] = 1; PREC(1)[0x05] = 1;
    AI32(r, 0x24) = 100;
    AI32(r, 0x1c) = 300;             /* prior accumulated sum */

    engine_realm_advance_rating();

    T_ASSERT_EQ_I(AI32(r, 0x1c), 346); /* 300 + 46 */
    T_ASSERT_EQ_I(r[0x18], 86);        /* 346 / 4 = 86 (trunc) */
    return 0;
}
