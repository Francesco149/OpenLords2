/*
 * test_engine_fps.c — coverage for engine_compute_fps, the pure-C
 * port of retail FUN_004b27bf (rolling 1-second FPS counter).
 *
 * Behaviour matrix:
 *
 *   non-rollover frame
 *       → frames_this_second++
 *       → delta_ms = now - last_render
 *       → last_render advanced to now
 *       → current_fps / window_start / cumulative pair UNCHANGED
 *       → window_rolled_over = 0
 *
 *   rollover frame (now - window_start > 1000, SIGNED compare)
 *       → frames_this_second reset to 0
 *       → current_fps = frames * 1000 / elapsed
 *       → window_start := rollover_now_ms (NOT now_ms — retail's 2-call)
 *       → cumulative_fps += current_fps
 *       → cumulative_seconds++
 *       → window_rolled_over = 1
 *
 *   negative elapsed (timeGetTime wrap, debug-seeded future window_start)
 *       → suppress branch — `1000 < (int)elapsed` is false for negatives
 *       → behaves like a non-rollover frame
 */

#include "t.h"
#include "engine.h"

#include <string.h>

int test_engine_fps_basic_frame_bumps_count(void)
{
    /* First frame after a fresh window — frames_this_second was 0,
     * should become 1.  No rollover (only 16 ms into the window). */
    engine_fps_in_t in = {
        .now_ms                 = 1016,
        .rollover_now_ms        = 1016,
        .last_render_ms         = 1000,
        .second_window_start_ms = 1000,
        .frames_this_second     = 0,
        .current_fps            = 0,
        .cumulative_fps         = 0,
        .cumulative_seconds     = 0,
    };
    engine_fps_out_t out;
    memset(&out, 0xff, sizeof(out));

    engine_compute_fps(&in, &out);

    T_ASSERT_EQ_U(out.delta_ms,                  16);
    T_ASSERT_EQ_U(out.new_last_render_ms,        1016);
    T_ASSERT_EQ_U(out.new_frames_this_second,    1);
    T_ASSERT_EQ_U(out.new_second_window_start_ms,1000);  /* held */
    T_ASSERT_EQ_U(out.new_current_fps,           0);     /* held */
    T_ASSERT_EQ_U(out.new_cumulative_fps,        0);
    T_ASSERT_EQ_U(out.new_cumulative_seconds,    0);
    T_ASSERT_EQ_I(out.window_rolled_over,        0);
    return 0;
}

int test_engine_fps_delta_independent_of_window(void)
{
    /* The ms-since-last-render delta must use last_render, not
     * window_start — they're different baselines. */
    engine_fps_in_t in = {
        .now_ms                 = 2050,
        .rollover_now_ms        = 2050,
        .last_render_ms         = 2030,
        .second_window_start_ms = 1500,
        .frames_this_second     = 30,
    };
    engine_fps_out_t out;

    engine_compute_fps(&in, &out);
    T_ASSERT_EQ_U(out.delta_ms,           20);    /* 2050 - 2030 */
    T_ASSERT_EQ_U(out.new_last_render_ms, 2050);
    /* No rollover yet: 2050 - 1500 = 550 ms, under threshold. */
    T_ASSERT_EQ_I(out.window_rolled_over, 0);
    return 0;
}

int test_engine_fps_rollover_at_1001ms_publishes(void)
{
    /* Rollover gate is `1000 < elapsed` — STRICT — so 1001 fires but
     * exactly 1000 does not (covered by the boundary test below). */
    engine_fps_in_t in = {
        .now_ms                 = 2001,
        .rollover_now_ms        = 2002,        /* differs from now_ms — retail's 2-call */
        .last_render_ms         = 1985,
        .second_window_start_ms = 1000,
        .frames_this_second     = 59,
        .current_fps            = 0,
        .cumulative_fps         = 0,
        .cumulative_seconds     = 0,
    };
    engine_fps_out_t out;

    engine_compute_fps(&in, &out);

    /* 60 frames (59 + 1) over 1001 ms → 60 * 1000 / 1001 = 59 FPS. */
    T_ASSERT_EQ_U(out.new_current_fps,               59);
    /* Window resets to the SECOND timeGetTime call (rollover_now_ms),
     * matching retail's `DAT_004eaba0 = timeGetTime()` after the math. */
    T_ASSERT_EQ_U(out.new_second_window_start_ms,    2002);
    T_ASSERT_EQ_U(out.new_frames_this_second,        0);
    T_ASSERT_EQ_U(out.new_cumulative_fps,            59);
    T_ASSERT_EQ_U(out.new_cumulative_seconds,        1);
    T_ASSERT_EQ_I(out.window_rolled_over,            1);
    T_ASSERT_EQ_U(out.delta_ms,                      16);   /* still computed */
    T_ASSERT_EQ_U(out.new_last_render_ms,            2001);
    return 0;
}

int test_engine_fps_rollover_boundary_exactly_1000ms_does_not_fire(void)
{
    /* `1000 < elapsed` is false when elapsed == 1000 — so the
     * exactly-on boundary stays in the non-rollover branch. */
    engine_fps_in_t in = {
        .now_ms                 = 2000,
        .rollover_now_ms        = 2000,
        .last_render_ms         = 1990,
        .second_window_start_ms = 1000,
        .frames_this_second     = 59,
        .current_fps            = 33,             /* stale stale value */
        .cumulative_fps         = 100,
        .cumulative_seconds     = 1,
    };
    engine_fps_out_t out;

    engine_compute_fps(&in, &out);

    T_ASSERT_EQ_I(out.window_rolled_over,         0);
    T_ASSERT_EQ_U(out.new_frames_this_second,     60);     /* bumped */
    T_ASSERT_EQ_U(out.new_second_window_start_ms, 1000);   /* held */
    T_ASSERT_EQ_U(out.new_current_fps,            33);     /* held */
    T_ASSERT_EQ_U(out.new_cumulative_fps,         100);
    T_ASSERT_EQ_U(out.new_cumulative_seconds,     1);
    return 0;
}

int test_engine_fps_cumulative_accumulates_across_rollovers(void)
{
    /* Two back-to-back rollover frames — verify the cumulative pair
     * adds the *current* FPS each time, not the same FPS twice. */
    engine_fps_in_t in1 = {
        .now_ms                 = 1100,
        .rollover_now_ms        = 1100,
        .last_render_ms         = 1080,
        .second_window_start_ms = 0,
        .frames_this_second     = 49,             /* +1 = 50 */
        .cumulative_fps         = 0,
        .cumulative_seconds     = 0,
    };
    engine_fps_out_t out1;
    engine_compute_fps(&in1, &out1);
    /* 50 frames over 1100 ms → 50 * 1000 / 1100 = 45 FPS. */
    T_ASSERT_EQ_U(out1.new_current_fps,        45);
    T_ASSERT_EQ_U(out1.new_cumulative_fps,     45);
    T_ASSERT_EQ_U(out1.new_cumulative_seconds, 1);

    engine_fps_in_t in2 = {
        .now_ms                 = 2200,
        .rollover_now_ms        = 2200,
        .last_render_ms         = 2180,
        .second_window_start_ms = out1.new_second_window_start_ms,  /* 1100 */
        .frames_this_second     = 59,             /* +1 = 60 */
        .current_fps            = out1.new_current_fps,
        .cumulative_fps         = out1.new_cumulative_fps,
        .cumulative_seconds     = out1.new_cumulative_seconds,
    };
    engine_fps_out_t out2;
    engine_compute_fps(&in2, &out2);
    /* 60 frames over (2200 - 1100) = 1100 ms → 60 * 1000 / 1100 = 54 FPS. */
    T_ASSERT_EQ_U(out2.new_current_fps,        54);
    T_ASSERT_EQ_U(out2.new_cumulative_fps,     45 + 54);
    T_ASSERT_EQ_U(out2.new_cumulative_seconds, 2);
    return 0;
}

int test_engine_fps_negative_elapsed_clock_wrap_suppressed(void)
{
    /* `(int32_t)elapsed` reads as negative when timeGetTime wraps or
     * the window_start was debug-seeded into the future.  Signed
     * `1000 < (negative)` is false → no rollover, frame just bumps
     * the count.  Mirrors engine_compute_frame_delta's signed-gate
     * quirk; pinned here for FUN_004b27bf's variant. */
    engine_fps_in_t in = {
        .now_ms                 = 500,
        .rollover_now_ms        = 500,
        .last_render_ms         = 490,
        .second_window_start_ms = 5000,       /* "in the future" */
        .frames_this_second     = 10,
        .current_fps            = 60,
        .cumulative_fps         = 60,
        .cumulative_seconds     = 1,
    };
    engine_fps_out_t out;

    engine_compute_fps(&in, &out);

    T_ASSERT_EQ_I(out.window_rolled_over,         0);
    T_ASSERT_EQ_U(out.new_frames_this_second,     11);     /* bumped */
    T_ASSERT_EQ_U(out.new_second_window_start_ms, 5000);   /* held — future basis intact */
    T_ASSERT_EQ_U(out.new_current_fps,            60);     /* held */
    return 0;
}

int test_engine_fps_two_call_shape_window_basis(void)
{
    /* The pure function uses rollover_now_ms (NOT now_ms) for the
     * new window-start on rollover frames — matching retail's two
     * timeGetTime calls.  Verify the wrapper-side intent by passing
     * a deliberately-different rollover_now_ms. */
    engine_fps_in_t in = {
        .now_ms                 = 5000,
        .rollover_now_ms        = 5050,       /* +50 ms artificial drift */
        .last_render_ms         = 4980,
        .second_window_start_ms = 3000,
        .frames_this_second     = 100,
    };
    engine_fps_out_t out;

    engine_compute_fps(&in, &out);

    T_ASSERT_EQ_I(out.window_rolled_over,         1);
    T_ASSERT_EQ_U(out.new_second_window_start_ms, 5050); /* uses rollover_now_ms */
    /* But last_render uses the FIRST timestamp, not the second. */
    T_ASSERT_EQ_U(out.new_last_render_ms,         5000);
    return 0;
}

int test_engine_fps_first_frame_with_zero_baselines(void)
{
    /* Cold start: every input zero.  Without the wrapper's lazy
     * init the pure function would fire a rollover here
     * (now - 0 > 1000 when now > 1000).  With now == 0 too it doesn't
     * fire — exercises the boundary where everything is zero. */
    engine_fps_in_t in = {
        .now_ms                 = 0,
        .rollover_now_ms        = 0,
        .last_render_ms         = 0,
        .second_window_start_ms = 0,
    };
    engine_fps_out_t out;

    engine_compute_fps(&in, &out);

    T_ASSERT_EQ_U(out.delta_ms,                   0);
    T_ASSERT_EQ_U(out.new_frames_this_second,     1);
    T_ASSERT_EQ_I(out.window_rolled_over,         0);  /* 0 < 1000 elapsed → false */
    return 0;
}

int test_engine_fps_high_fps_3sec_window(void)
{
    /* Pathological case: 3 seconds of frames coalesced into a single
     * rollover (e.g. the harness paused for a sec then resumed).
     * Verify the math doesn't blow up — divisor is the FULL elapsed,
     * not 1000. */
    engine_fps_in_t in = {
        .now_ms                 = 4000,
        .rollover_now_ms        = 4000,
        .last_render_ms         = 3990,
        .second_window_start_ms = 1000,
        .frames_this_second     = 179,           /* +1 = 180 frames over 3000 ms = 60 FPS */
        .cumulative_fps         = 0,
        .cumulative_seconds     = 0,
    };
    engine_fps_out_t out;

    engine_compute_fps(&in, &out);

    T_ASSERT_EQ_U(out.new_current_fps,         60);     /* 180 * 1000 / 3000 */
    T_ASSERT_EQ_U(out.new_cumulative_seconds,  1);      /* still 1 — not 3 — the
                                                         * rollover-event counter is
                                                         * per-rollover, not per-second */
    return 0;
}
