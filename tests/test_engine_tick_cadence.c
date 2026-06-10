/*
 * test_engine_tick_cadence.c — coverage for the FUN_0040490d /
 * FUN_0040489e cadence-counter pair (engine_tick_cadence.c).
 *
 * The retail semantics under test: each counter bumps by 1 per step
 * and resets to 0 when it EXCEEDS its limit — so a limit-3 counter
 * runs 0,1,2,3,0,… (period 4, i.e. period == limit+1 for every slot).
 */

#include "t.h"
#include "engine_tick_cadence.h"

#include <string.h>

static const int k_period[ENGINE_TICK_CADENCE_COUNT] = {
    4, 8, 16, 24, 32, 64, 128, 256, 384, 768,
};

int test_engine_tick_cadence_first_step_all_ones(void)
{
    engine_tick_cadence_t s;
    memset(&s, 0, sizeof(s));

    engine_tick_cadence_step(&s);
    for (int i = 0; i < ENGINE_TICK_CADENCE_COUNT; i++) {
        T_ASSERT_EQ_I(s.c[i], 1);
    }
    return 0;
}

int test_engine_tick_cadence_periods(void)
{
    engine_tick_cadence_t s;
    memset(&s, 0, sizeof(s));

    /* Step through two full periods of the slowest counter; at every
     * instant each counter must equal step-count mod its period. */
    for (int n = 1; n <= 768 * 2; n++) {
        engine_tick_cadence_step(&s);
        for (int i = 0; i < ENGINE_TICK_CADENCE_COUNT; i++) {
            T_ASSERT_EQ_I(s.c[i], n % k_period[i]);
        }
    }
    return 0;
}

int test_engine_tick_cadence_reset(void)
{
    engine_tick_cadence_t s;
    memset(&s, 0, sizeof(s));

    for (int n = 0; n < 100; n++) {
        engine_tick_cadence_step(&s);
    }
    engine_tick_cadence_reset(&s);
    for (int i = 0; i < ENGINE_TICK_CADENCE_COUNT; i++) {
        T_ASSERT_EQ_I(s.c[i], 0);
    }
    return 0;
}
