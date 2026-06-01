/*
 * Unit tests for engine_map_scene — the strategic-map body composition
 * driver (FUN_004050c0 / FUN_004cfb08 / FUN_004050f6).
 *
 * The cluster-frame calls in render_body need a fully-bound canvas + tile
 * state, which is exercised by the scene_render_parity harness, not here.
 * These tests pin down the real LOGIC the driver adds: the animation
 * sub-phase derivation (counter >> 1..4), the counter wraps, and the
 * 16 ms rate-limited tick.  derive_subphases() pushes the >>4 values into
 * the cluster modules; we read them back through the scene accessors.
 */

#include "t.h"
#include "engine_map_scene.h"

int test_engine_map_scene_reset_zeroes_counters_and_phases(void)
{
    engine_map_scene_set_anim_counters(0x40, 0x30);
    engine_map_scene_reset();
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_378(), 0);
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_388(), 0);
    T_ASSERT_EQ_I(engine_map_scene_redraw_phase(), 0);
    T_ASSERT_EQ_I(engine_map_scene_overlay_base_index(), 0);
    T_ASSERT_EQ_I(engine_map_scene_arrow_marker_index(), 0);
    return 0;
}

int test_engine_map_scene_set_counters_derives_subphases(void)
{
    /* overlay_base = c378>>4, arrow_marker = c388>>4 (the consumed pair). */
    engine_map_scene_set_anim_counters(0x70, 0x50);
    T_ASSERT_EQ_I(engine_map_scene_overlay_base_index(), 0x70 >> 4);  /* 7 */
    T_ASSERT_EQ_I(engine_map_scene_arrow_marker_index(), 0x50 >> 4);  /* 5 */
    return 0;
}

int test_engine_map_scene_subphases_shift_by_1_to_4(void)
{
    engine_map_scene_set_anim_counters(0x7e, 0x5a);
    T_ASSERT_EQ_I(engine_map_scene_subphase_378(1), 0x7e >> 1);
    T_ASSERT_EQ_I(engine_map_scene_subphase_378(2), 0x7e >> 2);
    T_ASSERT_EQ_I(engine_map_scene_subphase_378(3), 0x7e >> 3);
    T_ASSERT_EQ_I(engine_map_scene_subphase_378(4), 0x7e >> 4);
    T_ASSERT_EQ_I(engine_map_scene_subphase_388(1), 0x5a >> 1);
    T_ASSERT_EQ_I(engine_map_scene_subphase_388(2), 0x5a >> 2);
    T_ASSERT_EQ_I(engine_map_scene_subphase_388(3), 0x5a >> 3);
    T_ASSERT_EQ_I(engine_map_scene_subphase_388(4), 0x5a >> 4);
    /* Out-of-range shift → 0 (guard). */
    T_ASSERT_EQ_I(engine_map_scene_subphase_378(0), 0);
    T_ASSERT_EQ_I(engine_map_scene_subphase_378(5), 0);
    return 0;
}

int test_engine_map_scene_render_body_wraps_counter_378_at_0x80(void)
{
    /* FUN_004050f6 prologue: if (0x7f < c378) c378 = 0, before derivation.
     * advance_animation is render_body's prologue without the cluster draws. */
    engine_map_scene_reset();
    engine_map_scene_set_anim_counters(0x80, 0);   /* just over the limit */
    engine_map_scene_advance_animation();
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_378(), 0);
    T_ASSERT_EQ_I(engine_map_scene_overlay_base_index(), 0);
    /* 0x7f is the last in-range value (>>4 == 7). */
    engine_map_scene_set_anim_counters(0x7f, 0);
    engine_map_scene_advance_animation();
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_378(), 0x7f);
    T_ASSERT_EQ_I(engine_map_scene_overlay_base_index(), 7);
    return 0;
}

int test_engine_map_scene_render_body_wraps_counter_388_at_0x60(void)
{
    engine_map_scene_reset();
    engine_map_scene_set_anim_counters(0, 0x60);   /* just over the limit */
    engine_map_scene_advance_animation();
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_388(), 0);
    T_ASSERT_EQ_I(engine_map_scene_arrow_marker_index(), 0);
    /* 0x5f is the last in-range value (>>4 == 5). */
    engine_map_scene_set_anim_counters(0, 0x5f);
    engine_map_scene_advance_animation();
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_388(), 0x5f);
    T_ASSERT_EQ_I(engine_map_scene_arrow_marker_index(), 5);
    return 0;
}

int test_engine_map_scene_tick_first_call_bumps_both_counters(void)
{
    /* First tick (rate limiter "invalid") always bumps both counters. */
    engine_map_scene_reset();
    engine_map_scene_advance_frame_counters(1000);
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_378(), 1);
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_388(), 1);
    return 0;
}

int test_engine_map_scene_tick_within_16ms_holds_counters(void)
{
    engine_map_scene_reset();
    engine_map_scene_advance_frame_counters(1000);     /* counters → 1,1 */
    engine_map_scene_advance_frame_counters(1000 + 0xf); /* < 0x10 ms later → held */
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_378(), 1);
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_388(), 1);
    return 0;
}

int test_engine_map_scene_tick_after_16ms_bumps_counters(void)
{
    engine_map_scene_reset();
    engine_map_scene_advance_frame_counters(1000);        /* counters → 1,1 */
    engine_map_scene_advance_frame_counters(1000 + 0x10); /* >= 0x10 ms later → bump */
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_378(), 2);
    T_ASSERT_EQ_I(engine_map_scene_anim_counter_388(), 2);
    return 0;
}

int test_engine_map_scene_redraw_phase_cycles_0_to_3(void)
{
    /* DAT_0057d380 increments each tick, wrapping 3 → 0. */
    engine_map_scene_reset();
    int t = 1000;
    engine_map_scene_advance_frame_counters(t); T_ASSERT_EQ_I(engine_map_scene_redraw_phase(), 1); t += 0x10;
    engine_map_scene_advance_frame_counters(t); T_ASSERT_EQ_I(engine_map_scene_redraw_phase(), 2); t += 0x10;
    engine_map_scene_advance_frame_counters(t); T_ASSERT_EQ_I(engine_map_scene_redraw_phase(), 3); t += 0x10;
    engine_map_scene_advance_frame_counters(t); T_ASSERT_EQ_I(engine_map_scene_redraw_phase(), 0);
    return 0;
}
