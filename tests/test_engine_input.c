/*
 * test_engine_input.c — coverage for engine_compute_input_state, the
 * pure-C port of retail FUN_004b191e (mouse-input state resolver).
 *
 * Behaviour matrix covered:
 *   - raw cursor always written
 *   - cache update gated on cursor_cache_enabled
 *   - game-coord cursor via viewport scaling (and div-by-zero gate)
 *   - per-frame event flags cleared by default
 *   - dblclk bits propagate to out flags + clear pending-click timer
 *   - button_state bits → new_lmb / new_rmb
 *   - 300 ms timer expiry → click_fired + disarm
 *   - motion detection: x or y change → mouse_event = 1
 *   - motion_idle_ms computed from motion_last_ms when no motion
 *   - motion_last_ms reset to now_ms on motion
 *   - LMB press edge → lmb_newly_pressed, no timer arm
 *   - LMB release edge: always sets lmb_newly_released; arms timer
 *     and records click coords only if click_eligible
 *   - RMB edge mirror
 *   - shared 300 ms timer baseline between LMB and RMB
 *   - last_click_x/y carried through on non-click frame
 *   - prev_*_snapshot fields always written
 */

#include "t.h"
#include "engine.h"

#include <string.h>

/* Helper: zero an out struct, run the function, return the out. */
static engine_input_out_t run(const engine_input_in_t *in)
{
    engine_input_out_t out;
    memset(&out, 0xff, sizeof(out));   /* trash, so unset fields fail loudly */
    engine_compute_input_state(in, &out);
    return out;
}

/* Helper: a "default" input — viewport 0,0 → 100,100, game-coord
 * scale 100×100 (so screen and game coords map 1:1), no buttons,
 * no dblclks, click_eligible=0. */
static engine_input_in_t default_in(void)
{
    engine_input_in_t in = {0};
    in.viewport_x0     = 0;
    in.viewport_y0     = 0;
    in.viewport_x1     = 100;
    in.viewport_y1     = 100;
    in.viewport_game_w = 100;
    in.viewport_game_h = 100;
    in.now_ms          = 1000;
    return in;
}

int test_engine_input_raw_cursor_always_written(void)
{
    engine_input_in_t in = default_in();
    in.cursor_screen_x = 42;
    in.cursor_screen_y = 73;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.raw_cursor_x, 42);
    T_ASSERT_EQ_I(out.raw_cursor_y, 73);
    return 0;
}

int test_engine_input_cache_update_gated_on_flag(void)
{
    /* When cache_enabled=1, the screen cursor overwrites cache. */
    engine_input_in_t in = default_in();
    in.cursor_cache_enabled = 1;
    in.cursor_screen_x      = 30;
    in.cursor_screen_y      = 40;
    in.cached_cursor_x      = 999;     /* stale, should be overwritten */
    in.cached_cursor_y      = 999;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I((int)out.new_cached_cursor_x, 30);
    T_ASSERT_EQ_I((int)out.new_cached_cursor_y, 40);
    return 0;
}

int test_engine_input_cache_carries_through_when_disabled(void)
{
    /* When cache_enabled=0, cached_cursor_x/y carries through unchanged
     * (the WM_MOUSEMOVE-cached client-space coords). */
    engine_input_in_t in = default_in();
    in.cursor_cache_enabled = 0;
    in.cursor_screen_x      = 30;
    in.cursor_screen_y      = 40;
    in.cached_cursor_x      = 25;
    in.cached_cursor_y      = 35;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I((int)out.new_cached_cursor_x, 25);
    T_ASSERT_EQ_I((int)out.new_cached_cursor_y, 35);
    return 0;
}

int test_engine_input_game_coord_1to1_mapping(void)
{
    /* Default viewport is 100×100 → 100×100 game → game = cached. */
    engine_input_in_t in = default_in();
    in.cached_cursor_x = 50;
    in.cached_cursor_y = 25;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.game_x, 50);
    T_ASSERT_EQ_I(out.game_y, 25);
    return 0;
}

int test_engine_input_game_coord_scaled(void)
{
    /* Viewport 100×100 → game 200×100: game_x = 2*cached_x. */
    engine_input_in_t in = default_in();
    in.viewport_game_w = 200;
    in.viewport_game_h = 100;
    in.cached_cursor_x = 30;
    in.cached_cursor_y = 40;
    engine_input_out_t out = run(&in);
    /* scale_x = (200*100)/100 = 200; game_x = (30*200)/100 = 60 */
    T_ASSERT_EQ_I(out.game_x, 60);
    T_ASSERT_EQ_I(out.game_y, 40);
    return 0;
}

int test_engine_input_game_coord_div_by_zero_clamp(void)
{
    /* When viewport width is 0 (viewport_x1 == viewport_x0), percent_ratio
     * returns 0 → game_x = 0.  Same on y. */
    engine_input_in_t in = default_in();
    in.viewport_x0 = 50;
    in.viewport_x1 = 50;   /* width = 0 */
    in.cached_cursor_x = 30;
    in.cached_cursor_y = 40;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.game_x, 0);
    T_ASSERT_EQ_I(out.game_y, 40);   /* y still 1:1 */
    return 0;
}

int test_engine_input_default_clears_event_flags(void)
{
    /* Default input → no edges, no motion, no dblclks → all flags 0. */
    engine_input_in_t in = default_in();
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.mouse_event,        0);
    T_ASSERT_EQ_I(out.button_changed,     0);
    T_ASSERT_EQ_I(out.lmb_newly_pressed,  0);
    T_ASSERT_EQ_I(out.lmb_newly_released, 0);
    T_ASSERT_EQ_I(out.rmb_newly_pressed,  0);
    T_ASSERT_EQ_I(out.rmb_newly_released, 0);
    T_ASSERT_EQ_I(out.lmb_click_fired,    0);
    T_ASSERT_EQ_I(out.rmb_click_fired,    0);
    T_ASSERT_EQ_I(out.lmb_dblclk,         0);
    T_ASSERT_EQ_I(out.rmb_dblclk,         0);
    return 0;
}

int test_engine_input_dblclk_bit0_lmb(void)
{
    /* pending_dblclk bit 0 → lmb_dblclk = 1, clears LMB timer. */
    engine_input_in_t in = default_in();
    in.pending_dblclk   = 0x01;
    in.lmb_timer_armed  = 1;
    in.click_timer_ms   = 999;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_dblclk,         1);
    T_ASSERT_EQ_I(out.rmb_dblclk,         0);
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 0);
    return 0;
}

int test_engine_input_dblclk_bit1_rmb(void)
{
    /* pending_dblclk bit 1 → rmb_dblclk = 1, clears RMB timer. */
    engine_input_in_t in = default_in();
    in.pending_dblclk   = 0x02;
    in.rmb_timer_armed  = 1;
    in.click_timer_ms   = 999;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_dblclk,         0);
    T_ASSERT_EQ_I(out.rmb_dblclk,         1);
    T_ASSERT_EQ_I(out.new_rmb_timer_armed, 0);
    return 0;
}

int test_engine_input_button_state_decoded(void)
{
    /* button_state bit 0 → new_lmb=1; bit 1 → new_rmb=1. */
    engine_input_in_t in = default_in();
    in.button_state = 0x03;            /* both pressed */
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.new_lmb, 1);
    T_ASSERT_EQ_I(out.new_rmb, 1);
    return 0;
}

int test_engine_input_lmb_press_edge(void)
{
    /* prev_lmb=0, new_lmb=1 → lmb_newly_pressed, mouse_event, button_changed. */
    engine_input_in_t in = default_in();
    in.prev_lmb     = 0;
    in.button_state = 0x01;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_newly_pressed,  1);
    T_ASSERT_EQ_I(out.lmb_newly_released, 0);
    T_ASSERT_EQ_I(out.mouse_event,        1);
    T_ASSERT_EQ_I(out.button_changed,     1);
    /* Timer NOT armed on press — only on release-into-click. */
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 0);
    return 0;
}

int test_engine_input_lmb_release_always_sets_released_flag(void)
{
    /* prev_lmb=1, new_lmb=0 → lmb_newly_released always set, even when
     * click_eligible=0 (the (DAT_004e65d8 = 1, DAT_004e65f0 != 0) comma
     * pattern in retail). */
    engine_input_in_t in = default_in();
    in.prev_lmb        = 1;
    in.button_state    = 0;
    in.click_eligible  = 0;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_newly_released,  1);
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 0);   /* not armed */
    T_ASSERT_EQ_I(out.new_click_eligible,  0);
    return 0;
}

int test_engine_input_lmb_release_with_eligibility_arms_timer(void)
{
    /* prev_lmb=1, new_lmb=0, click_eligible=1 →
     *   lmb_newly_released = 1
     *   timer armed
     *   click_eligible cleared
     *   click_timer_ms set to now_ms
     *   last_click_x/y = game cursor */
    engine_input_in_t in = default_in();
    in.prev_lmb        = 1;
    in.button_state    = 0;
    in.click_eligible  = 1;
    in.now_ms          = 5000;
    in.cached_cursor_x = 40;
    in.cached_cursor_y = 30;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_newly_released,  1);
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 1);
    T_ASSERT_EQ_I(out.new_click_eligible,  0);
    T_ASSERT_EQ_I((int)out.new_click_timer_ms, 5000);
    T_ASSERT_EQ_I(out.last_click_x, 40);     /* 1:1 mapping with default viewport */
    T_ASSERT_EQ_I(out.last_click_y, 30);
    return 0;
}

int test_engine_input_rmb_release_with_eligibility_arms_rmb_timer(void)
{
    /* RMB block is a mirror of LMB block. */
    engine_input_in_t in = default_in();
    in.prev_rmb        = 1;
    in.button_state    = 0;
    in.click_eligible  = 1;
    in.now_ms          = 7000;
    in.cached_cursor_x = 10;
    in.cached_cursor_y = 20;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.rmb_newly_released,  1);
    T_ASSERT_EQ_I(out.new_rmb_timer_armed, 1);
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 0);   /* LMB timer unchanged */
    T_ASSERT_EQ_I(out.new_click_eligible,  0);
    T_ASSERT_EQ_I((int)out.new_click_timer_ms, 7000);
    T_ASSERT_EQ_I(out.last_click_x, 10);
    T_ASSERT_EQ_I(out.last_click_y, 20);
    return 0;
}

int test_engine_input_click_expiry_lmb_fires(void)
{
    /* LMB timer armed + > 300 ms since baseline → fires lmb_click_fired,
     * disarms timer.  Note: 300 ms boundary is `300 < delta` (strict),
     * so delta == 301 fires but 300 does not. */
    engine_input_in_t in = default_in();
    in.lmb_timer_armed = 1;
    in.click_timer_ms  = 1000;
    in.now_ms          = 1301;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_click_fired,     1);
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 0);
    return 0;
}

int test_engine_input_click_expiry_boundary_at_300_does_not_fire(void)
{
    /* delta == 300 → `300 < 300` is false → does NOT fire. */
    engine_input_in_t in = default_in();
    in.lmb_timer_armed = 1;
    in.click_timer_ms  = 1000;
    in.now_ms          = 1300;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_click_fired,     0);
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 1);   /* still armed */
    return 0;
}

int test_engine_input_shared_timer_baseline_quirk(void)
{
    /* Retail uses a single shared baseline (_DAT_004e59f8) for BOTH
     * LMB and RMB timer expiry.  If both are armed when the same
     * baseline elapses, both fire on the same call.  Quirk: arming
     * one disarms-then-rearms the SAME baseline shared by the other,
     * so this is an unlikely-but-possible state in retail. */
    engine_input_in_t in = default_in();
    in.lmb_timer_armed = 1;
    in.rmb_timer_armed = 1;
    in.click_timer_ms  = 1000;
    in.now_ms          = 1500;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.lmb_click_fired,     1);
    T_ASSERT_EQ_I(out.rmb_click_fired,     1);
    T_ASSERT_EQ_I(out.new_lmb_timer_armed, 0);
    T_ASSERT_EQ_I(out.new_rmb_timer_armed, 0);
    return 0;
}

int test_engine_input_motion_x_change_sets_event(void)
{
    /* prev_game_x != game_x → mouse_event = 1, motion_last_ms reset. */
    engine_input_in_t in = default_in();
    in.cached_cursor_x  = 50;          /* → game_x = 50 */
    in.prev_game_x      = 49;          /* differs by 1 */
    in.motion_last_ms   = 0;
    in.now_ms           = 5000;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.mouse_event,         1);
    T_ASSERT_EQ_I((int)out.new_motion_last_ms, 5000);
    T_ASSERT_EQ_I((int)out.motion_idle_ms,     0);
    return 0;
}

int test_engine_input_motion_y_change_sets_event(void)
{
    /* Same on Y. */
    engine_input_in_t in = default_in();
    in.cached_cursor_y = 70;
    in.prev_game_y     = 71;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.mouse_event, 1);
    return 0;
}

int test_engine_input_no_motion_computes_idle_ms(void)
{
    /* No motion → motion_idle_ms = now_ms - motion_last_ms;
     * motion_last_ms NOT updated. */
    engine_input_in_t in = default_in();
    in.prev_game_x    = 0;
    in.prev_game_y    = 0;
    in.cached_cursor_x = 0;
    in.cached_cursor_y = 0;
    in.motion_last_ms = 1000;
    in.now_ms         = 3500;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.mouse_event,             0);
    T_ASSERT_EQ_I((int)out.motion_idle_ms,     2500);
    T_ASSERT_EQ_I((int)out.new_motion_last_ms, 1000);   /* unchanged */
    return 0;
}

int test_engine_input_prev_snapshots_always_written(void)
{
    /* prev_game_x/y_snapshot, prev_lmb/rmb_snapshot mirror the input
     * prev_* fields unconditionally, regardless of any other state. */
    engine_input_in_t in = default_in();
    in.prev_game_x = 11;
    in.prev_game_y = 22;
    in.prev_lmb    = 1;
    in.prev_rmb    = 0;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.prev_game_x_snapshot, 11);
    T_ASSERT_EQ_I(out.prev_game_y_snapshot, 22);
    T_ASSERT_EQ_I(out.prev_lmb_snapshot,    1);
    T_ASSERT_EQ_I(out.prev_rmb_snapshot,    0);
    return 0;
}

int test_engine_input_last_click_carries_through_no_click(void)
{
    /* If neither click block fires this frame, last_click_x/y should
     * carry through unchanged (prev_last_click_*). */
    engine_input_in_t in = default_in();
    in.prev_last_click_x = 123;
    in.prev_last_click_y = 456;
    engine_input_out_t out = run(&in);
    T_ASSERT_EQ_I(out.last_click_x, 123);
    T_ASSERT_EQ_I(out.last_click_y, 456);
    return 0;
}
