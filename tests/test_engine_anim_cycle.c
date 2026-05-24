/*
 * test_engine_anim_cycle.c — coverage for engine_compute_anim_cycle,
 * the pure-C port of retail FUN_00451f1a (16-frame animation cycle
 * counter driven by the 80 ms phase tick).
 *
 * Behaviour matrix:
 *
 *   tick_80ms_fired == 0  → counter held (no advance)
 *   tick_80ms_fired != 0  → counter bumped:
 *       0..14  → +1
 *       15     → wraps to 0
 *
 * The unreachable lower clamp (`c < 0 → c = 0`) is documented in
 * engine-quirks but can't be reached from any normal call sequence
 * — tests don't exercise it because there's no input that triggers
 * it (the counter only ever bumps by 1 and the upper clamp catches
 * before it could overflow signed-int min, which would take ~2bn
 * tick-80ms-fires).
 */

#include "t.h"
#include "engine.h"

#include <string.h>

int test_engine_anim_cycle_no_tick_holds(void)
{
    engine_anim_cycle_in_t  in  = { .counter = 7, .tick_80ms_fired = 0 };
    engine_anim_cycle_out_t out;
    memset(&out, 0xff, sizeof(out));

    engine_compute_anim_cycle(&in, &out);
    T_ASSERT_EQ_I(out.new_counter, 7);
    return 0;
}

int test_engine_anim_cycle_tick_bumps(void)
{
    engine_anim_cycle_in_t  in  = { .counter = 7, .tick_80ms_fired = 1 };
    engine_anim_cycle_out_t out;

    engine_compute_anim_cycle(&in, &out);
    T_ASSERT_EQ_I(out.new_counter, 8);
    return 0;
}

int test_engine_anim_cycle_from_zero_bumps(void)
{
    engine_anim_cycle_in_t  in  = { .counter = 0, .tick_80ms_fired = 1 };
    engine_anim_cycle_out_t out;

    engine_compute_anim_cycle(&in, &out);
    T_ASSERT_EQ_I(out.new_counter, 1);
    return 0;
}

int test_engine_anim_cycle_wraps_at_15_to_0(void)
{
    /* Retail tests `0xf < c` AFTER the bump, so 14→15 doesn't wrap
     * but 15→16 → wraps to 0. */
    engine_anim_cycle_in_t  in  = { .counter = 15, .tick_80ms_fired = 1 };
    engine_anim_cycle_out_t out;

    engine_compute_anim_cycle(&in, &out);
    T_ASSERT_EQ_I(out.new_counter, 0);
    return 0;
}

int test_engine_anim_cycle_14_to_15_no_wrap(void)
{
    /* Boundary on the other side: 14→15, 0xf < 15 false → no wrap. */
    engine_anim_cycle_in_t  in  = { .counter = 14, .tick_80ms_fired = 1 };
    engine_anim_cycle_out_t out;

    engine_compute_anim_cycle(&in, &out);
    T_ASSERT_EQ_I(out.new_counter, 15);
    return 0;
}

int test_engine_anim_cycle_full_16_cycle(void)
{
    /* Drive 16 ticks and check we return to 0.  Belt-and-braces
     * confirmation that the cycle period is 16. */
    engine_anim_cycle_in_t  in  = { .counter = 0, .tick_80ms_fired = 1 };
    engine_anim_cycle_out_t out;

    for (int i = 0; i < 16; i++) {
        engine_compute_anim_cycle(&in, &out);
        in.counter = out.new_counter;
    }
    T_ASSERT_EQ_I(out.new_counter, 0);
    return 0;
}

int test_engine_anim_cycle_no_tick_with_max_counter(void)
{
    /* Counter at max but no tick — no advance, no wrap, just hold. */
    engine_anim_cycle_in_t  in  = { .counter = 15, .tick_80ms_fired = 0 };
    engine_anim_cycle_out_t out;

    engine_compute_anim_cycle(&in, &out);
    T_ASSERT_EQ_I(out.new_counter, 15);
    return 0;
}
