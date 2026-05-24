/*
 * test_engine_phase.c — coverage for engine_compute_phase_ticks,
 * the pure-C port of retail FUN_004bbc80 (animation-phase tick
 * generator).
 *
 * Each test sets up an engine_phase_in_t with the case-specific
 * inputs, runs the function, and asserts on the flag pattern + the
 * counter / timestamp writebacks.
 *
 * The pure function never produces the "sticky" semantics of the
 * 1040 ms flag — that's a wrapper concern in engine.c.  Tests here
 * assert the flag is SET correctly on rollover; sticky-vs-fresh
 * is an integration concern not unit-tested here.
 */

#include "t.h"
#include "engine.h"

#include <string.h>

/* Fresh inputs — all zeros, now_ms = 0.  The first call from this
 * state fires the base tick (delta = 0 - 0 = 0; 0x13 < 0 is false,
 * 0 < 0 is false → neither branch fires).  So default_in is the
 * "exactly at boot, no time has passed" baseline. */
static void default_in(engine_phase_in_t *in)
{
    memset(in, 0, sizeof(*in));
}

/* ─── base tick threshold ──────────────────────────────────────── */

int test_engine_phase_no_time_passed_fires_nothing(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms = 0;
    in.last_base_tick_ms = 0;

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms, 0);
    T_ASSERT_EQ_I(out.tick_80ms, 0);
    T_ASSERT_EQ_U(out.new_base_tick_counter, 0);
    T_ASSERT_EQ_U(out.new_last_base_tick_ms, 0);
    return 0;
}

int test_engine_phase_19ms_below_threshold(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms = 19;                /* 0x13 < 19 is false → no fire */

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms, 0);
    T_ASSERT_EQ_U(out.new_last_base_tick_ms, 0);
    T_ASSERT_EQ_U(out.new_base_tick_counter, 0);
    return 0;
}

int test_engine_phase_20ms_fires_base_tick(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms = 20;                /* 0x13 < 20 → fire */

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms, 1);
    T_ASSERT_EQ_U(out.new_last_base_tick_ms, 20);
    T_ASSERT_EQ_U(out.new_base_tick_counter, 1);
    /* 80 ms tick fires when counter > 3, counter is 1 → no fire. */
    T_ASSERT_EQ_I(out.tick_80ms, 0);
    return 0;
}

int test_engine_phase_negative_delta_clock_wrap_fires_base(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms              = 100;
    in.last_base_tick_ms   = 1000;    /* now < last → negative delta */

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms, 1);
    T_ASSERT_EQ_U(out.new_last_base_tick_ms, 100);
    return 0;
}

/* ─── 80 ms gate ───────────────────────────────────────────────── */

int test_engine_phase_base_counter_3_no_80ms(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    /* Force base counter to bump from 3 to 4.  But retail tests
     * `if (3 < counter)` after the bump, so 4 > 3 → fires. */
    in.now_ms              = 20;
    in.base_tick_counter   = 3;

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms, 1);
    /* After bump from 3, counter = 4, 3 < 4 true → 80 ms fires,
     * counter resets to 0. */
    T_ASSERT_EQ_I(out.tick_80ms, 1);
    T_ASSERT_EQ_U(out.new_base_tick_counter, 0);
    return 0;
}

int test_engine_phase_base_counter_2_yields_3_no_80ms(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms              = 20;
    in.base_tick_counter   = 2;

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms, 1);
    /* After bump from 2, counter = 3, 3 < 3 false → 80 ms does not fire. */
    T_ASSERT_EQ_I(out.tick_80ms, 0);
    T_ASSERT_EQ_U(out.new_base_tick_counter, 3);
    return 0;
}

int test_engine_phase_no_20ms_means_no_80ms_even_with_full_counter(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms              = 10;          /* delta=10, no base fire */
    in.base_tick_counter   = 100;         /* already huge, but base didn't tick */

    engine_compute_phase_ticks(&in, &out);
    /* base didn't bump → out->new_base_tick_counter == 100; 3<100 true
     * → 80 ms DOES fire because retail tests *after* the bump
     * conditional, regardless of whether bump happened.  This catches
     * "stuck counter" from a previous frame's failure to consume. */
    T_ASSERT_EQ_I(out.tick_20ms, 0);
    T_ASSERT_EQ_I(out.tick_80ms, 1);
    return 0;
}

/* ─── mod counters — every-N rollover ──────────────────────────── */

int test_engine_phase_counter_2_rollover_at_2nd(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    /* Force 80 ms fire (base counter = 4 after bump). */
    in.now_ms              = 20;
    in.base_tick_counter   = 3;
    /* counter_2 = 0 → bump to 1, 1 < 1 false → no rollover. */

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_80ms, 1);
    T_ASSERT_EQ_I(out.tick_160ms, 0);
    T_ASSERT_EQ_U(out.new_counter_2, 1);

    /* Second call: counter_2=1 → bump to 2, 1 < 2 → rollover. */
    in.base_tick_counter = 3;
    in.counter_2         = 1;
    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_160ms, 1);
    T_ASSERT_EQ_U(out.new_counter_2, 0);
    return 0;
}

int test_engine_phase_all_long_cycles_at_max_rollover(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms              = 20;
    in.base_tick_counter   = 3;
    in.counter_2           = 1;     /* fires 160ms */
    in.counter_4           = 3;     /* fires 320ms */
    in.counter_8           = 7;     /* fires 640ms */
    in.counter_13          = 0xc;   /* fires 1040ms */
    in.counter_16          = 0xf;   /* fires 1280ms */
    in.counter_24          = 0x17;  /* fires 1920ms */
    in.counter_32          = 0x1f;  /* fires 2560ms */

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms,  1);
    T_ASSERT_EQ_I(out.tick_80ms,  1);
    T_ASSERT_EQ_I(out.tick_160ms, 1);
    T_ASSERT_EQ_I(out.tick_320ms, 1);
    T_ASSERT_EQ_I(out.tick_640ms, 1);
    T_ASSERT_EQ_I(out.tick_1040ms, 1);
    T_ASSERT_EQ_I(out.tick_1280ms, 1);
    T_ASSERT_EQ_I(out.tick_1920ms, 1);
    T_ASSERT_EQ_I(out.tick_2560ms, 1);
    /* All counters reset. */
    T_ASSERT_EQ_U(out.new_counter_2,  0);
    T_ASSERT_EQ_U(out.new_counter_4,  0);
    T_ASSERT_EQ_U(out.new_counter_8,  0);
    T_ASSERT_EQ_U(out.new_counter_13, 0);
    T_ASSERT_EQ_U(out.new_counter_16, 0);
    T_ASSERT_EQ_U(out.new_counter_24, 0);
    T_ASSERT_EQ_U(out.new_counter_32, 0);
    return 0;
}

int test_engine_phase_1040ms_off_by_one(void)
{
    /* The 13-cycle is the one that doesn't divide evenly into the
     * shorter cycles, so it's the most likely to drift if the
     * boundary arithmetic is off.  Check both sides. */
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms              = 20;
    in.base_tick_counter   = 3;
    in.counter_13          = 0xb;     /* 11; bump→12, 0xc < 12 false → no fire */

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_1040ms, 0);
    T_ASSERT_EQ_U(out.new_counter_13, 0xc);

    /* And the on-boundary case. */
    in.base_tick_counter = 3;
    in.counter_13        = 0xc;       /* bump→13, 0xc < 13 true → fire */
    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_1040ms, 1);
    T_ASSERT_EQ_U(out.new_counter_13, 0);
    return 0;
}

/* ─── carry-through ────────────────────────────────────────────── */

int test_engine_phase_no_fire_no_state_change(void)
{
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    in.now_ms              = 10;
    in.last_base_tick_ms   = 0;
    in.base_tick_counter   = 1;
    in.counter_4           = 2;

    engine_compute_phase_ticks(&in, &out);
    /* delta=10 → no base fire.  After init out copies in unchanged.
     * out->new_base_tick_counter=1, counter=1<=3 → no 80ms fire,
     * so the inner counters don't bump either. */
    T_ASSERT_EQ_U(out.new_last_base_tick_ms, 0);
    T_ASSERT_EQ_U(out.new_base_tick_counter, 1);
    T_ASSERT_EQ_U(out.new_counter_4, 2);
    return 0;
}

int test_engine_phase_default_clears_all_flags(void)
{
    /* The clear-at-entry semantic is part of the pure function's
     * contract — caller hands in any garbage for flags' previous
     * values and the pure function produces fresh ones. */
    engine_phase_in_t in;
    engine_phase_out_t out;
    default_in(&in);
    /* Set output to all 1s to verify the function clears. */
    memset(&out, 0xff, sizeof(out));
    in.now_ms = 19;     /* nothing fires */

    engine_compute_phase_ticks(&in, &out);
    T_ASSERT_EQ_I(out.tick_20ms, 0);
    T_ASSERT_EQ_I(out.tick_80ms, 0);
    T_ASSERT_EQ_I(out.tick_160ms, 0);
    T_ASSERT_EQ_I(out.tick_320ms, 0);
    T_ASSERT_EQ_I(out.tick_640ms, 0);
    T_ASSERT_EQ_I(out.tick_1040ms, 0);
    T_ASSERT_EQ_I(out.tick_1280ms, 0);
    T_ASSERT_EQ_I(out.tick_1920ms, 0);
    T_ASSERT_EQ_I(out.tick_2560ms, 0);
    return 0;
}
