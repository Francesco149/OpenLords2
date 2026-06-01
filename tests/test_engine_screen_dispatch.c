/*
 * Unit tests for engine_screen_dispatch — the top-level screen-state
 * render dispatch (partial port of FUN_0040f1a0: title arm + hook).
 */

#include "t.h"
#include "engine_dirty_rect.h"
#include "engine_title_render.h"
#include "engine_screen_dispatch.h"

static engine_dirty_rect_state_t g_dirty;

static int g_other_state, g_other_redraw, g_other_calls;
static void other_capture(int state, int redraw)
{
    g_other_state = state; g_other_redraw = redraw; g_other_calls++;
}

static int g_strat_redraw, g_strat_calls;
static void strat_capture(int redraw)
{
    g_strat_redraw = redraw; g_strat_calls++;
}

static void disp_reset(void)
{
    engine_screen_dispatch_reset();
    engine_title_render_reset_counters();

    /* The title scene's finalize marks this dirty state full-screen. */
    g_dirty.dirty_mark = 0;
    g_dirty.dirty_xmin = 0x7fff; g_dirty.dirty_xmax = -0x7fff;
    g_dirty.dirty_ymin = 0x7fff; g_dirty.dirty_ymax = -0x7fff;
    g_dirty.below_status_redraw = 0;
    engine_title_render_set_dirty_state(&g_dirty);

    /* Default no-op hooks (background / page / other). */
    engine_title_render_bind_background_fn(0);
    engine_title_render_bind_page_fn(0);
    engine_screen_dispatch_bind_other_fn(0);
    engine_screen_dispatch_bind_strat_fn(0);

    g_other_state = 0; g_other_redraw = 0; g_other_calls = 0;
    g_strat_redraw = 0; g_strat_calls = 0;
}

/* Title screen routes to the scene; we use a still-unported page (3) so
 * the scene exercises its page hook (no font/canvas setup needed).  Pages
 * 1/2/5 are now ported renderers, so they would draw rather than hook. */
int test_dispatch_title_routes_to_scene(void)
{
    disp_reset();
    engine_screen_dispatch(/*screen_state=*/0x1f, /*page=*/6, /*redraw=*/2);

    /* Scene ran: background drawn, page hook saw page 6, flag_a set. */
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 1);
    T_ASSERT_EQ_I(engine_title_render_last_page(), 6);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_a(), 1);
    /* The other-screen hook was NOT used. */
    T_ASSERT_EQ_I(engine_screen_dispatch_last_other_state(), 0);
    return 0;
}

int test_dispatch_clears_scene_flag_b_after_title(void)
{
    disp_reset();
    engine_screen_dispatch(0x1f, 6, 2);   /* page 6 → hook (no real render) */
    /* Scene set flag_b=1; the dispatcher's tail clears it to 0. */
    T_ASSERT_EQ_I(engine_title_render_scene_flag_b(), 0);
    /* flag_a stays set (only flag_b is cleared by the dispatcher). */
    T_ASSERT_EQ_I(engine_title_render_scene_flag_a(), 1);
    return 0;
}

int test_dispatch_other_screen_routes_to_hook(void)
{
    disp_reset();
    engine_screen_dispatch_bind_other_fn(other_capture);
    engine_screen_dispatch(/*screen_state=*/0x10, /*page=*/1, /*redraw=*/3);

    /* Hook got the screen-state + redraw-mode; the title scene did NOT run. */
    T_ASSERT_EQ_I(g_other_calls, 1);
    T_ASSERT_EQ_I(g_other_state, 0x10);
    T_ASSERT_EQ_I(g_other_redraw, 3);
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 0);
    return 0;
}

int test_dispatch_other_screen_still_clears_flag_b(void)
{
    disp_reset();
    /* Pre-set flag_b to confirm the unconditional tail clear. */
    engine_title_render_set_scene_flag_b(1);
    engine_screen_dispatch(0x10, 1, 0);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_b(), 0);
    return 0;
}

/* Screen-state 0 (the in-game strategic map, FUN_0040f5fd) routes to its
 * dedicated strat hook with the redraw-mode arg — NOT the generic
 * other-screen hook, and not the title scene. */
int test_dispatch_strat_screen_routes_to_strat_hook(void)
{
    disp_reset();
    engine_screen_dispatch_bind_other_fn(other_capture);
    engine_screen_dispatch_bind_strat_fn(strat_capture);
    engine_screen_dispatch(/*screen_state=*/0, /*page=*/1, /*redraw=*/2);

    /* Strat hook got the redraw-mode; the other hook + title scene did NOT run. */
    T_ASSERT_EQ_I(g_strat_calls, 1);
    T_ASSERT_EQ_I(g_strat_redraw, 2);
    T_ASSERT_EQ_I(g_other_calls, 0);
    T_ASSERT_EQ_I(engine_title_render_background_call_count(), 0);
    return 0;
}

/* With the default (recording) strat stub bound, screen-0 dispatch updates
 * the last-strat-redraw instrumentation. */
int test_dispatch_strat_screen_default_stub_records_redraw(void)
{
    disp_reset();                          /* binds the default strat stub */
    engine_screen_dispatch(/*screen_state=*/0, /*page=*/0, /*redraw=*/3);
    T_ASSERT_EQ_I(engine_screen_dispatch_last_strat_redraw(), 3);
    return 0;
}

/* The strat arm also runs the dispatcher's unconditional flag_b tail clear. */
int test_dispatch_strat_screen_still_clears_flag_b(void)
{
    disp_reset();
    engine_title_render_set_scene_flag_b(1);
    engine_screen_dispatch(0, 1, 1);
    T_ASSERT_EQ_I(engine_title_render_scene_flag_b(), 0);
    return 0;
}
