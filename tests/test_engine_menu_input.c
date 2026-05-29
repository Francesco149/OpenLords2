/*
 * Unit tests for engine_menu_input — title-menu hotspot hit-test +
 * page navigation (ports of FUN_0040e5e7, FUN_0040e3ee, FUN_00432b05,
 * FUN_00432cc8, and the pages-1/2 arm of FUN_004ba9c8).
 *
 * The page selector + selected-button live in engine_title_render; we
 * drive them through that module's public API and observe transitions.
 * Engine side-effects (demand-redraw, quit) are observed via the
 * module's default stubs (no real hook bound in tests).
 */

#include "t.h"
#include "engine_menu_input.h"
#include "engine_title_render.h"
#include "save_schema_globals.h"   /* s_slot_0053f640 / _0053f258 / _0055302c */

#include <string.h>

/* Page-5 ("Play Now!" campaign chooser) button centres — two side-by-side
 * boxes (left x∈[0x70,0x110), right x∈[0x170,0x210); y∈[0x76,0x8e)). */
enum { P5_LEFT_X = 0xc0, P5_RIGHT_X = 0x1d0, P5_Y = 0x82 };

/* Geometry shared by both pages: x in [0xe0, 0x1a0), rows step 0x24
 * starting at y=0x5b (height 0x18 → row N spans [0x5b+N*0x24, +0x18)). */
enum { BTN_X = 0x100 };   /* a column inside [0xe0,0x1a0) */
static int row_y(int n)  { return 0x5b + n * 0x24 + 8; }  /* mid-row */

/* A fresh, no-button-pressed input at a given cursor. */
static engine_menu_click_in_t at(int x, int y)
{
    engine_menu_click_in_t in;
    memset(&in, 0, sizeof in);
    in.cursor_x = x;
    in.cursor_y = y;
    return in;
}

static void mi_reset(void)
{
    engine_menu_input_bind_redraw_fn(0);   /* default stub */
    engine_menu_input_bind_quit_fn(0);
    engine_menu_input_set_sp_available(1);
    engine_menu_input_reset_counters();
    engine_title_render_set_page(1);
    engine_title_render_set_selected_button(1);
}

/* ── hover hit-test (FUN_0040e5e7) ──────────────────────────────── */

/* Build a tiny standalone table for the primitive tests. */
static void noop_action(int p) { (void)p; }
static const engine_menu_hotspot_t k_two[2] = {
    { 10, 20, 30, 40, noop_action, 1, 1 },
    { 50, 60, 70, 80, noop_action, 1, 2 },
};

int test_menu_hover_returns_1based_index(void)
{
    mi_reset();
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(0, 0, k_two, 2, 15, 25), 1);
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(0, 0, k_two, 2, 60, 70), 2);
    return 0;
}

int test_menu_hover_outside_returns_zero(void)
{
    mi_reset();
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(0, 0, k_two, 2, 5, 5), 0);
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(0, 0, k_two, 2, 100, 100), 0);
    return 0;
}

int test_menu_hover_box_is_half_open(void)
{
    mi_reset();
    /* x0,y0 inclusive; x1,y1 exclusive. */
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(0, 0, k_two, 2, 10, 20), 1);  /* corner in */
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(0, 0, k_two, 2, 30, 25), 0);  /* x1 out */
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(0, 0, k_two, 2, 15, 40), 0);  /* y1 out */
    return 0;
}

int test_menu_hover_applies_offset(void)
{
    mi_reset();
    /* With ox=100, the first box covers x in [110,130). */
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(100, 0, k_two, 2, 115, 25), 1);
    T_ASSERT_EQ_I(engine_menu_hotspot_hover(100, 0, k_two, 2, 15, 25), 0);
    return 0;
}

/* ── click hit-test (FUN_0040e3ee) ──────────────────────────────── */

static int g_fired_param;
static int g_fired_count;
static void record_action(int p) { g_fired_param = p; g_fired_count++; }
static const engine_menu_hotspot_t k_press[1] = {
    { 10, 20, 30, 40, record_action, 1, 7 },   /* type 1 = press */
};
static const engine_menu_hotspot_t k_release[1] = {
    { 10, 20, 30, 40, record_action, 3, 9 },   /* type 3 = release */
};

int test_menu_click_type1_fires_on_press(void)
{
    mi_reset();
    g_fired_count = 0; g_fired_param = -1;
    engine_menu_click_in_t in = at(15, 25);
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_hotspot_click(0, 0, k_press, 1, &in), 1);
    T_ASSERT_EQ_I(g_fired_count, 1);
    T_ASSERT_EQ_I(g_fired_param, 7);
    return 0;
}

int test_menu_click_type1_no_fire_without_press(void)
{
    mi_reset();
    g_fired_count = 0;
    engine_menu_click_in_t in = at(15, 25);   /* inside, but no press edge */
    T_ASSERT_EQ_I(engine_menu_hotspot_click(0, 0, k_press, 1, &in), 0);
    T_ASSERT_EQ_I(g_fired_count, 0);
    return 0;
}

int test_menu_click_outside_no_fire(void)
{
    mi_reset();
    g_fired_count = 0;
    engine_menu_click_in_t in = at(99, 99);
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_hotspot_click(0, 0, k_press, 1, &in), 0);
    T_ASSERT_EQ_I(g_fired_count, 0);
    return 0;
}

int test_menu_click_type3_needs_release_not_press(void)
{
    mi_reset();
    g_fired_count = 0;
    engine_menu_click_in_t in = at(15, 25);
    in.lmb_newly_pressed = 1;                 /* press alone: type 3 ignores */
    T_ASSERT_EQ_I(engine_menu_hotspot_click(0, 0, k_release, 1, &in), 0);
    T_ASSERT_EQ_I(g_fired_count, 0);
    in.lmb_newly_pressed = 0;
    in.lmb_newly_released = 1;                 /* release: fires */
    T_ASSERT_EQ_I(engine_menu_hotspot_click(0, 0, k_release, 1, &in), 1);
    T_ASSERT_EQ_I(g_fired_count, 1);
    T_ASSERT_EQ_I(g_fired_param, 9);
    return 0;
}

/* ── page-1 navigation (FUN_00432b05 via FUN_004ba9c8) ──────────── */

int test_menu_page1_single_player_goes_to_page2(void)
{
    mi_reset();
    engine_title_render_set_page(1);
    engine_menu_click_in_t in = at(BTN_X, row_y(0));   /* "Single player" */
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);    /* click consumed */
    T_ASSERT_EQ_I(engine_title_render_page(), 2);
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    return 0;
}

int test_menu_page1_single_player_sp_unavailable_goes_to_page10(void)
{
    mi_reset();
    engine_title_render_set_page(1);
    engine_menu_input_set_sp_available(0);             /* outside [1,2] */
    engine_menu_click_in_t in = at(BTN_X, row_y(0));
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 10);
    return 0;
}

int test_menu_page1_exit_requests_quit(void)
{
    mi_reset();
    engine_title_render_set_page(1);
    engine_menu_click_in_t in = at(BTN_X, row_y(3));   /* "Exit game" (btn 4) */
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_menu_input_quit_count(), 1);
    /* Exit does not change the page. */
    T_ASSERT_EQ_I(engine_title_render_page(), 1);
    return 0;
}

int test_menu_page1_lords_of_magic_is_release_triggered(void)
{
    mi_reset();
    engine_title_render_set_page(1);
    engine_menu_click_in_t in = at(BTN_X, row_y(2));   /* "Lords of Magic?" */
    in.lmb_newly_pressed = 1;                          /* press: type 3 ignores */
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    in.lmb_newly_pressed = 0;
    in.lmb_newly_released = 1;                          /* release: fires */
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    return 0;
}

/* ── page-2 navigation (FUN_00432cc8 via FUN_004ba9c8) ──────────── */

int test_menu_page2_back_returns_to_page1(void)
{
    mi_reset();
    engine_title_render_set_page(2);
    engine_menu_click_in_t in = at(BTN_X, row_y(4));   /* "Back." (btn 5) */
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 1);
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    return 0;
}

int test_menu_page2_play_now_goes_to_page5(void)
{
    mi_reset();
    engine_title_render_set_page(2);
    engine_menu_click_in_t in = at(BTN_X, row_y(0));   /* "Play Now!" */
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 5);
    return 0;
}

int test_menu_page2_load_goes_to_filelist_page3(void)
{
    mi_reset();
    engine_title_render_set_page(2);
    engine_menu_click_in_t in = at(BTN_X, row_y(1));   /* "Load a game" */
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 3);
    return 0;
}

/* ── page 5 ("Play Now!" campaign chooser, FUN_00433461) ────────── */

int test_menu_page5_original_campaign_goes_to_page4(void)
{
    mi_reset();
    engine_title_render_set_page(5);
    s_slot_0053f640 = 0xdead; s_slot_0053f258 = 0xdead; s_slot_0055302c = 0;
    engine_menu_click_in_t in = at(P5_LEFT_X, P5_Y);   /* "Original campaign" */
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 4);       /* → setup screen */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    T_ASSERT_EQ_U(s_slot_0053f640, 0);                  /* campaign 0 */
    T_ASSERT_EQ_U(s_slot_0053f258, 0);                  /* original */
    T_ASSERT_EQ_U(s_slot_0055302c, 1);                  /* full-game substate */
    return 0;
}

int test_menu_page5_new_campaign_goes_to_page4(void)
{
    mi_reset();
    engine_title_render_set_page(5);
    s_slot_0053f640 = 0xdead; s_slot_0053f258 = 0xdead;
    engine_menu_click_in_t in = at(P5_RIGHT_X, P5_Y);   /* "The new campaign" */
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 4);
    T_ASSERT_EQ_U(s_slot_0053f640, 1);                  /* campaign 1 */
    T_ASSERT_EQ_U(s_slot_0053f258, 2);                  /* new campaign */
    return 0;
}

int test_menu_page5_hover_updates_highlight(void)
{
    mi_reset();
    engine_title_render_set_page(5);
    engine_title_render_set_selected_button(0);
    /* Hover (no press) over the right button → selection 2. */
    engine_menu_click_in_t in = at(P5_RIGHT_X, P5_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 2);
    /* Move to the left button → selection 1. */
    engine_menu_click_in_t in2 = at(P5_LEFT_X, P5_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in2), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 1);
    /* Off both buttons → cleared. */
    engine_menu_click_in_t in3 = at(P5_LEFT_X, 0x200);
    T_ASSERT_EQ_I(engine_menu_title_input(&in3), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 0);
    return 0;
}

/* ── hover via the dispatcher updates the highlight ─────────────── */

int test_menu_title_input_updates_hover_highlight(void)
{
    mi_reset();
    engine_title_render_set_page(1);
    engine_title_render_set_selected_button(1);
    /* No button pressed → no click; hover over button 2 sets selection. */
    engine_menu_click_in_t in = at(BTN_X, row_y(1));
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 2);
    return 0;
}

int test_menu_title_input_hover_off_buttons_clears_highlight(void)
{
    mi_reset();
    engine_title_render_set_page(1);
    engine_title_render_set_selected_button(2);
    engine_menu_click_in_t in = at(0, 0);   /* nowhere near a button */
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 0);
    return 0;
}

int test_menu_title_input_click_does_not_refresh_hover(void)
{
    mi_reset();
    engine_title_render_set_page(1);
    engine_title_render_set_selected_button(3);   /* stale highlight */
    /* A consumed click returns before the hover refresh, so the
     * selected button is left as-is (retail returns 1 immediately). */
    engine_menu_click_in_t in = at(BTN_X, row_y(0));
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 3);
    return 0;
}

int test_menu_title_input_other_page_is_noop(void)
{
    mi_reset();
    engine_title_render_set_page(7);   /* not 1 or 2 */
    engine_menu_click_in_t in = at(BTN_X, row_y(0));
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_page(), 7);   /* unchanged */
    return 0;
}
