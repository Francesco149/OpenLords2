/*
 * Unit tests for engine_battlefield_render — combat-mode frame
 * driver + zoom-16 second-pass stub (chip 2 of the battlefield port).
 *
 * Coverage matrix:
 *
 *   - camera bind: frame driver stores (camera_x, camera_y) into
 *     retail globals readable via the accessors
 *   - dispatch shape at zoom 0x10: terrain-z16 + middle + overlay-z16
 *     + late + final, no zoom-32 passes
 *   - dispatch shape at zoom 0x20: terrain-z32 + middle + overlay-z32
 *     + late + final, no zoom-16 passes
 *   - dispatch shape at unknown zoom: middle + late + final only
 *   - force-redraw decrement on non-zero post-frame
 *   - force-redraw stays at 0 when already 0 (no underflow)
 *   - counters accumulate across frames; reset zeroes all
 *   - zoom-16 overlay pass IS dispatched even though its retail body
 *     is a no-op (counter increments to prove the dispatch fired)
 */

#include "t.h"

#include "engine_battlefield_render.h"

/* ────────────────────────────────────────────────────────────────
 * Helpers
 * ──────────────────────────────────────────────────────────────── */

static void scaffold_init(void)
{
    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_force_redraw(0);
}

/* ────────────────────────────────────────────────────────────────
 * Camera binding
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_frame_stores_camera(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(123, -456);

    T_ASSERT_EQ_I(engine_battlefield_render_camera_x(), 123);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_y(), -456);
    return 0;
}

int test_engine_battlefield_render_camera_updates_each_frame(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x20);

    engine_battlefield_render_frame(10, 20);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_x(), 10);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_y(), 20);

    engine_battlefield_render_frame(-7, 99);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_x(), -7);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_y(), 99);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Dispatch shape — zoom 0x10
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_zoom16_dispatches_z16_passes(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          1);
    return 0;
}

/* The zoom-16 overlay pass is FUN_004bcbd1 — a retail no-op body.
 * The dispatch itself MUST still fire, otherwise the renderer
 * cluster's call-graph diverges from retail. */
int test_engine_battlefield_render_zoom16_overlay_pass_is_dispatched(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 1);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Dispatch shape — zoom 0x20
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_zoom32_dispatches_z32_passes(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          1);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Dispatch shape — unknown zoom (neither 0x10 nor 0x20)
 *
 * The retail driver has no fall-through default in the if/else-if
 * chains: any other value skips both zoom-dispatched passes but
 * still runs middle, late, final and decrements force-redraw.
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_unknown_zoom_skips_zoom_passes(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          1);
    return 0;
}

int test_engine_battlefield_render_unknown_zoom_decrements_force_redraw(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x33);   /* not 0x10, not 0x20 */
    engine_battlefield_render_set_force_redraw(3);
    engine_battlefield_render_frame(0, 0);

    /* force-redraw decrement happens regardless of zoom dispatch. */
    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 2);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Force-redraw counter
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_force_redraw_decrements(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(5);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 4);

    engine_battlefield_render_frame(0, 0);
    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 2);
    return 0;
}

/* The zero floor — counter must NEVER decrement below zero.  Retail
 * uses an unsigned-ish `if (DAT != 0) DAT--`; a careless port could
 * underflow into negative territory. */
int test_engine_battlefield_render_force_redraw_zero_floor(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 0);
    return 0;
}

int test_engine_battlefield_render_force_redraw_one_to_zero(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(1);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 0);

    /* Next frame should hold at 0, not underflow. */
    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Counter accumulation + reset
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_counters_accumulate_across_frames(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);

    for (int i = 0; i < 7; i++) engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          7);

    /* Zoom-32 passes never fired. */
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    return 0;
}

int test_engine_battlefield_render_counters_reset_zeroes_all(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_frame(0, 0);
    engine_battlefield_render_frame(0, 0);

    /* Pre-reset: at least middle should be nonzero. */
    T_ASSERT(engine_battlefield_render_count_middle() > 0);
    T_ASSERT(engine_battlefield_render_count_terrain_zoom32() > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Mode-switch fidelity — switching zoom mid-stream picks the right
 * pair on the next frame, doesn't leak state.
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_switching_zoom_picks_correct_pair(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(0, 0);   /* +1 z16 terrain, +1 z16 overlay */

    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_frame(0, 0);   /* +1 z32 terrain, +1 z32 overlay */

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 1);

    /* Always-on passes fired twice. */
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         2);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           2);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          2);
    return 0;
}

/* Zoom accessor round-trip. */
int test_engine_battlefield_render_zoom_round_trip(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    T_ASSERT_EQ_I(engine_battlefield_render_zoom(), 0x10);

    engine_battlefield_render_set_zoom(0x20);
    T_ASSERT_EQ_I(engine_battlefield_render_zoom(), 0x20);

    engine_battlefield_render_set_zoom(0);
    T_ASSERT_EQ_I(engine_battlefield_render_zoom(), 0);
    return 0;
}
