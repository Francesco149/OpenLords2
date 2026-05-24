/*
 * test_engine_delta.c — coverage for engine_compute_frame_delta,
 * the pure-C port of retail FUN_004b20ed (frame-delta generator with
 * 30 ms low-pass).
 *
 * Behaviour matrix:
 *
 *   delta < 30 ms (including zero, including negative)
 *       → out.delta_ms = 0, baseline HELD (next call sees larger delta)
 *   delta >= 30 ms
 *       → out.delta_ms = delta, baseline ADVANCED to now
 *
 * The negative-delta branch is a key divergence from
 * engine_compute_phase_ticks (which fires the base tick on negative
 * delta as a clock-wrap defence).  Here, negative deltas get
 * suppressed via the signed `< 0x1e` gate — wrapping `now` is
 * treated as "no time has passed" and the baseline holds.  Tested
 * explicitly below.
 */

#include "t.h"
#include "engine.h"

#include <string.h>

int test_engine_delta_below_threshold_holds_baseline(void)
{
    engine_delta_in_t  in  = { .now_ms = 1029, .last_frame_ms = 1000 };
    engine_delta_out_t out;
    memset(&out, 0xff, sizeof(out));

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 0);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 1000);   /* baseline held */
    return 0;
}

int test_engine_delta_at_threshold_advances_baseline(void)
{
    /* `iVar2 < 0x1e` — exactly 0x1e (30) is NOT below, so it advances. */
    engine_delta_in_t  in  = { .now_ms = 1030, .last_frame_ms = 1000 };
    engine_delta_out_t out;

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 30);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 1030);
    return 0;
}

int test_engine_delta_just_below_threshold_holds(void)
{
    /* 29 ms — boundary case on the other side. */
    engine_delta_in_t  in  = { .now_ms = 1029, .last_frame_ms = 1000 };
    engine_delta_out_t out;

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 0);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 1000);
    return 0;
}

int test_engine_delta_large_delta_advances(void)
{
    engine_delta_in_t  in  = { .now_ms = 5000, .last_frame_ms = 1000 };
    engine_delta_out_t out;

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 4000);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 5000);
    return 0;
}

int test_engine_delta_zero_delta_holds(void)
{
    /* now == last → delta 0 → suppressed (0 < 0x1e). */
    engine_delta_in_t  in  = { .now_ms = 1000, .last_frame_ms = 1000 };
    engine_delta_out_t out;

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 0);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 1000);
    return 0;
}

int test_engine_delta_negative_delta_clock_wrap_holds(void)
{
    /* now < last → unsigned subtraction underflows to ~4 billion,
     * which read as int32_t is a large negative.  negative < 0x1e
     * is true → suppress branch, baseline held.
     *
     * KEY DIVERGENCE from engine_compute_phase_ticks: the phase-tick
     * generator special-cases this as `delta < 0 → fire`, but this
     * function treats negative as "noise, hold baseline" because of
     * the signed-compare gate.  Both behaviours are retail-correct
     * for their respective consumers (sprite anims want a smooth
     * cadence; phase ticks want clock-wrap defence). */
    engine_delta_in_t  in  = { .now_ms = 100, .last_frame_ms = 1000 };
    engine_delta_out_t out;

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 0);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 1000);   /* held, not advanced */
    return 0;
}

int test_engine_delta_overflow_wrap_then_advance(void)
{
    /* `timeGetTime` wrapped past 0 — last just before wrap, now past
     * wrap.  The unsigned subtract gives the small positive ms
     * across the wrap; if that's >= 30 it advances normally.
     *   last = 0xFFFFFFF0, now = 0x00000020 → 0x30 = 48 ms. */
    engine_delta_in_t  in  = { .now_ms = 0x20, .last_frame_ms = 0xFFFFFFF0u };
    engine_delta_out_t out;

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 48);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 0x20);
    return 0;
}

int test_engine_delta_baseline_zero_first_call_advances(void)
{
    /* Boot path — baseline starts at 0, now is current `timeGetTime`
     * (any large value).  First call past 30 ms advances normally.
     * If the host has been up < 30 ms (impossible in practice but
     * required for the edge), suppress fires. */
    engine_delta_in_t  in  = { .now_ms = 12345, .last_frame_ms = 0 };
    engine_delta_out_t out;

    engine_compute_frame_delta(&in, &out);
    T_ASSERT_EQ_I(out.delta_ms, 12345);
    T_ASSERT_EQ_U(out.new_last_frame_ms, 12345);
    return 0;
}
