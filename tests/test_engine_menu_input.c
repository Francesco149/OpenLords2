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
#include "engine.h"                /* g_sim_mp_active (retail DAT_00553030) */
#include "save_schema_globals.h"   /* s_slot_0053f640 / _0053f258 / _0055302c */
#include "text_input.h"            /* page-4 name editor */
#include "engine_button.h"         /* page-3 button-record table */

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
    engine_title_render_set_choose_title_editor(0);   /* unbind page-4 editor */
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
    engine_title_render_set_page(6);   /* page with no dispatch arm yet */
    engine_menu_click_in_t in = at(BTN_X, row_y(0));
    in.lmb_newly_pressed = 1;
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_page(), 6);   /* unchanged */
    return 0;
}

/* ── page-7 ("game setup") input dispatch (FUN_004ba9c8 arm) ─────── */

/* A clean press at (x,y). */
static engine_menu_click_in_t press_at(int x, int y)
{
    engine_menu_click_in_t in = at(x, y);
    in.lmb_newly_pressed = 1;
    return in;
}

int test_menu_page7_cancel_returns_to_main_menu(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    /* Cancel box (165,181)-(241,236) → centre (203, 208). */
    engine_menu_click_in_t in = press_at(203, 208);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 1);
    return 0;
}

int test_menu_page7_start_fires_game_start_seam(void)
{
    mi_reset();
    engine_menu_input_bind_game_start_fn(0);     /* default counting stub */
    engine_title_render_set_page(7);
    s_slot_00553f98 = 1;                         /* 1 human */
    s_slot_0053e8a4 = 5;                         /* 5-seat roster → gate passes */
    /* Start box (243,181)-(319,236) → centre (281, 208). */
    engine_menu_click_in_t in = press_at(281, 208);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);     /* click consumed */
    T_ASSERT_EQ_I(engine_title_render_page(), 7);       /* Start does not change page */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    T_ASSERT_EQ_I(engine_menu_input_game_start_count(), 1);  /* seam fired once */
    T_ASSERT_EQ_I(engine_menu_input_game_start_mode(), 0);   /* custom game */
    return 0;
}

/* Start is gated on humans <= roster: with no roster seats the seam must NOT
 * fire (the click is still consumed, page unchanged). */
int test_menu_page7_start_blocked_when_roster_too_small(void)
{
    mi_reset();
    engine_menu_input_bind_game_start_fn(0);
    engine_title_render_set_page(7);
    s_slot_00553f98 = 2;                         /* 2 humans */
    s_slot_0053e8a4 = 1;                         /* only 1 roster seat → gate fails */
    engine_menu_click_in_t in = press_at(281, 208);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);     /* click consumed */
    T_ASSERT_EQ_I(engine_title_render_page(), 7);
    T_ASSERT_EQ_I(engine_menu_input_game_start_count(), 0);  /* seam did NOT fire */
    return 0;
}

int test_menu_page7_country_row_selects_and_sets_id(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    static const uint8_t ids[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    engine_title_render_set_country_list(ids, 0, 0, 8);
    s_slot_0053f034 = 0xbeef;
    /* Row 2 box (496,173)-(601,189) → centre (548, 181); param 3 → row 2. */
    engine_menu_click_in_t in = press_at(548, 181);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_U(s_slot_0053f034, 30);                 /* ids[0 + 2] */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    return 0;
}

int test_menu_page7_scrollbar_down_scrolls(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    static const uint8_t ids[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    engine_title_render_set_country_list(ids, 0, 0, 8);
    s_slot_0053f034 = 0xbeef;
    /* Down arrow (603,209)-(624,229) → centre (613, 219); type-2 fires on
     * the press edge.  scroll 0→1 → id = ids[1 + 0] = 20. */
    engine_menu_click_in_t in = press_at(613, 219);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_U(s_slot_0053f034, 20);
    return 0;
}

int test_menu_page7_option_tab_opens_page9(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    /* Tab 0 box (170,280)-(266,328) → centre (218, 304). */
    engine_menu_click_in_t in = press_at(218, 304);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 9);       /* option-detail page */
    /* Detail state published for the page-9 render: tab 0, 2 values. */
    T_ASSERT_EQ_I(engine_title_render_option_detail_tab(), 0);
    T_ASSERT_EQ_I(engine_title_render_option_detail_count(), 2);
    return 0;
}

/* Tab 2 (the player-count option) clamps its value count to one below the
 * live roster size when the roster has fewer than 5 seats. */
int test_menu_page7_tab2_count_clamps_to_roster(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    s_slot_0053e8a4 = 3;        /* DAT_0053e8a4 — 3-seat roster (< 5) */
    /* Tab 2 box (170,426)-(266,474) → centre (218, 450); param 3 → tab 2. */
    engine_menu_click_in_t in = press_at(218, 450);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_option_detail_tab(), 2);
    T_ASSERT_EQ_I(engine_title_render_option_detail_count(), 2);   /* roster-1 */

    /* With a full (>=5) roster the table count (4) stands. */
    mi_reset();
    engine_title_render_set_page(7);
    s_slot_0053e8a4 = 5;
    in = press_at(218, 450);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_option_detail_count(), 4);
    return 0;
}

int test_menu_page7_click_outside_hotspots_noop(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    /* (10,10) is outside every page-7 box. */
    engine_menu_click_in_t in = press_at(10, 10);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_page(), 7);
    return 0;
}

/* Defaults button (box (321,181)-(397,236) → centre (359,208)) resets the
 * edited option tabs back to the SP defaults and stays on page 7. */
int test_menu_page7_defaults_resets_option_values(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    g_sim_mp_active = 0;
    s_slot_0053e8a4 = 5;                 /* full roster → tab-2 default 3 stands */

    /* Scramble every tab away from its default first. */
    int vals[12] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    engine_title_render_set_tab_values(vals, -1);

    engine_menu_click_in_t in = press_at(359, 208);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);          /* consumed */
    T_ASSERT_EQ_I(engine_title_render_page(), 7);            /* still page 7 */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);

    static const int def_sp[12] = { 0, 0, 3, 0, 0, 0, 3, 2, 2, 1, 6, 1 };
    for (int t = 0; t < 12; t++) {
        T_ASSERT_EQ_I(engine_title_render_tab_value(t), def_sp[t]);
    }
    return 0;
}

/* ── page 9 (option-detail dropdown) input arm (FUN_004ba9c8 / 00433ce4 /
 *    00433a23) ─────────────────────────────────────────────────────── */

/* Open the tab-0 dropdown from page 7 (its panel anchors at (0xaa, 0x118)
 * with 2 value rows: row i box x∈[170,266), y∈[296+i·16, +16)). */
static void open_tab0_dropdown(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    engine_menu_click_in_t in = press_at(218, 304);   /* tab-0 box centre */
    (void)engine_menu_title_input(&in);
    /* now on page 9, tab 0, 2 values */
}

/* Hovering a dropdown value row (no click) publishes its 1-based index as
 * the highlight and consumes nothing; hovering off clears it to 0. */
int test_menu_page9_hover_highlights_row(void)
{
    open_tab0_dropdown();
    /* Row 1 (i=1) box y∈[312,328); x∈[170,266) → (218, 320). */
    engine_menu_click_in_t in = at(218, 320);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);          /* no click */
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 2); /* row 1 → 2 */
    T_ASSERT_EQ_I(engine_title_render_page(), 9);            /* still open */

    /* Off every row → highlight 0. */
    in = at(10, 10);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 0);
    return 0;
}

/* A press on a highlighted value row writes that value back to the edited
 * tab and closes the dropdown to the bookmarked page (7). */
int test_menu_page9_pick_writes_value_and_closes(void)
{
    open_tab0_dropdown();
    engine_menu_input_reset_counters();
    /* Press on row 1 (value index 1). */
    engine_menu_click_in_t in = press_at(218, 320);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);          /* consumed */
    T_ASSERT_EQ_I(engine_title_render_tab_value(0), 1);      /* hover-1 */
    T_ASSERT_EQ_I(engine_title_render_page(), 7);            /* closed → bookmark */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    return 0;
}

/* Clicking a *different* tab while the dropdown is open re-targets it
 * (re-publishes the detail state) without leaving page 9. */
int test_menu_page9_tab_click_retargets(void)
{
    open_tab0_dropdown();
    /* Tab 3 box (290,280)-(386,328) → centre (338,304); outside tab-0's
     * panel column [170,266), so the hover hit-test misses and the tab
     * table claims the click. */
    engine_menu_click_in_t in = press_at(338, 304);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 9);            /* still open */
    T_ASSERT_EQ_I(engine_title_render_option_detail_tab(), 3);
    return 0;
}

/* A press anywhere outside the value rows + tabs closes the dropdown. */
int test_menu_page9_click_outside_closes(void)
{
    open_tab0_dropdown();
    engine_menu_input_reset_counters();
    engine_menu_click_in_t in = press_at(10, 10);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 7);            /* back to bookmark */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    return 0;
}

/* Tab 2 (player count) grows *upward*: with 4 rows its panel anchors at
 * (0xaa, 0x1aa) but shifts up by (4-1)·16, so row 0 sits at the top and
 * the bottom row (3) abuts the tab.  The hover hit-test must agree. */
int test_menu_page9_tab2_grows_upward(void)
{
    mi_reset();
    engine_title_render_set_page(7);
    s_slot_0053e8a4 = 5;                          /* full roster → count 4 */
    engine_menu_click_in_t in = press_at(218, 450);  /* tab-2 box centre */
    (void)engine_menu_title_input(&in);
    T_ASSERT_EQ_I(engine_title_render_option_detail_tab(), 2);
    T_ASSERT_EQ_I(engine_title_render_option_detail_count(), 4);

    /* Row 0 top: ry = 0x1aa + 0*16 + 16 + (4-1)*-16 = 426+16-48 = 394 →
     * box [394,410) → (218, 400). */
    in = at(218, 400);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 1);

    /* Bottom row 3: ry = 394 + 3*16 = 442 → box [442,458) → (218, 450). */
    in = at(218, 450);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 4);
    return 0;
}

/* ── page 4 ("Choose your title and your shield") input (FUN_004ba9c8
 *    page-4 arm + FUN_00432ee6 shields + FUN_00433155 Back/Continue) ── */

/* Continue box (0x150,0xd7)-(0x210,0xf0) → centre (0x1b0, 0xe3). */
enum { P4_CONTINUE_X = 0x1b0, P4_CONTINUE_Y = 0xe3,
       P4_BACK_X = 0xd0, P4_BACK_Y = 0xe3 };

/* Reach page 4 the real way — page 2 → "Custom game" — so DAT_0057d320
 * (load-dir flag) = 0 and DAT_0055302c = 2 (custom, not skirmish/play-now),
 * the single-player Continue → page-7 path. */
static void reach_page4_custom(void)
{
    engine_title_render_set_page(2);
    engine_menu_click_in_t in = press_at(BTN_X, row_y(3));   /* Custom game */
    (void)engine_menu_title_input(&in);
    engine_title_render_set_choose_title_gates(0, 0);        /* buttons shown */
}

int test_menu_page4_continue_goes_to_page7(void)
{
    mi_reset();
    g_sim_mp_active = 0;
    reach_page4_custom();
    T_ASSERT_EQ_I(engine_title_render_page(), 4);

    engine_menu_click_in_t in = press_at(P4_CONTINUE_X, P4_CONTINUE_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 7);            /* → setup screen */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    return 0;
}

int test_menu_page4_back_returns_to_page1(void)
{
    mi_reset();
    g_sim_mp_active = 0;
    reach_page4_custom();

    engine_menu_click_in_t in = press_at(P4_BACK_X, P4_BACK_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 1);            /* → main menu */
    return 0;
}

int test_menu_page4_skirmish_continue_deferred(void)
{
    mi_reset();
    g_sim_mp_active = 0;
    /* Reach page 4 via "Skirmish!" (page-2 param 5 → DAT_0055302c = 3). */
    engine_title_render_set_page(2);
    engine_menu_click_in_t sk = press_at(BTN_X, row_y(2));   /* Skirmish! */
    (void)engine_menu_title_input(&sk);
    T_ASSERT_EQ_I(engine_title_render_page(), 4);
    engine_title_render_set_choose_title_gates(0, 0);

    /* Continue is consumed but stays on page 4 — the skirmish page-0xc
     * subsystem is deferred. */
    engine_menu_click_in_t in = press_at(P4_CONTINUE_X, P4_CONTINUE_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 4);
    return 0;
}

int test_menu_page4_shield_click_moves_selection(void)
{
    mi_reset();
    engine_title_render_set_page(4);
    engine_title_render_set_choose_title_gates(0, 0);
    /* Player 1 owns slot 1 (the seeded page-4 layout). */
    static const unsigned char slots[6] = { 0, 1, 0, 0, 0, 0 };
    engine_title_render_set_choose_title_shields(slots, /*selected=*/1,
                                                 /*flash=*/1, /*flash_timer=*/0);

    /* Shield 3 box (0x120,0x8c)-(0x160,0xd2) → centre (0x140, 0xaf). */
    engine_menu_click_in_t in = press_at(0x140, 0xaf);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_choose_title_slot_owner(1), 0);  /* released */
    T_ASSERT_EQ_I(engine_title_render_choose_title_slot_owner(3), 1);  /* claimed  */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);
    return 0;
}

int test_menu_page4_hover_highlights_continue(void)
{
    mi_reset();
    engine_title_render_set_page(4);
    engine_title_render_set_choose_title_gates(0, 0);
    engine_title_render_set_selected_button(0);
    /* Hover (no press) over Continue → 1-based index 7. */
    engine_menu_click_in_t in = at(P4_CONTINUE_X, P4_CONTINUE_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_selected_button(), 7);
    return 0;
}

int test_menu_page4_hidden_buttons_skip_button_clicks(void)
{
    mi_reset();
    g_sim_mp_active = 0;
    engine_title_render_set_page(4);
    /* Short MP layout: gate_a != 0 and gate_b == 0 → buttons hidden, so the
     * click pass tests only the 5 shields.  A click where Continue would be
     * hits nothing and the page is unchanged. */
    engine_title_render_set_choose_title_gates(1, 0);
    T_ASSERT_EQ_I(engine_title_render_choose_title_buttons_shown(), 0);

    engine_menu_click_in_t in = press_at(P4_CONTINUE_X, P4_CONTINUE_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(engine_title_render_page(), 4);            /* unchanged */

    /* Shields still hit-test in the short layout. */
    static const unsigned char slots[6] = { 0, 1, 0, 0, 0, 0 };
    engine_title_render_set_choose_title_shields(slots, 1, 1, 0);
    engine_menu_click_in_t s = press_at(0x140, 0xaf);        /* shield 3 */
    T_ASSERT_EQ_I(engine_menu_title_input(&s), 1);
    T_ASSERT_EQ_I(engine_title_render_choose_title_slot_owner(3), 1);
    return 0;
}

/* ── page-4 name editor: the per-frame tail enables the editor and
 *    streams its buffer into the rendered name box (the live link for
 *    the WM_CHAR keystroke route) ── */

/* Seed a page-4 editor the way retail's page-2→4 init does
 * (FUN_00402009(&name, 0x10, 0xc0, 0)) and put us on page 4 with the
 * Back/Continue layout. */
static text_input_state_t g_test_p4_ed;
static void bind_page4_editor(const char *seed)
{
    text_input_init(&g_test_p4_ed, seed, 0x10, 0xc0, 0);
    g_test_p4_ed.insert_mode = 0;            /* overwrite */
    engine_title_render_set_page(4);
    engine_title_render_set_choose_title_gates(0, 0);
    engine_title_render_set_choose_title_editor(&g_test_p4_ed);
}

int test_menu_page4_arm_enables_editor_and_copies_name(void)
{
    mi_reset();
    bind_page4_editor("ab");

    /* A hover frame (no click) runs the page-4 arm's editor tail:
     * enable → recompute → copy editor buffer into the name box. */
    engine_menu_click_in_t in = at(0, 0);       /* off every hotspot */
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 0);
    T_ASSERT_EQ_I(g_test_p4_ed.enabled, 1);     /* DAT_005aeb78 = 1 */
    T_ASSERT_EQ_I(g_test_p4_ed.length, 2);
    T_ASSERT(strcmp((const char *)engine_title_render_choose_title_name(),
                    "ab") == 0);
    return 0;
}

int test_menu_page4_typed_char_reaches_name_box(void)
{
    mi_reset();
    bind_page4_editor("ab");

    /* Frame 1 enables the editor (the WM_CHAR insert gate). */
    engine_menu_click_in_t in = at(0, 0);
    (void)engine_menu_title_input(&in);

    /* The WndProc route would call this on a keystroke; in overwrite
     * mode at cursor 0 it replaces 'a' with 'Z' (mode 0 → no case fold). */
    text_input_filtered_keypress(&g_test_p4_ed, 'Z');

    /* Frame 2's tail streams the edited buffer into the name box. */
    (void)engine_menu_title_input(&in);
    T_ASSERT(strcmp((const char *)engine_title_render_choose_title_name(),
                    "Zb") == 0);
    return 0;
}

int test_menu_page4_editor_disabled_off_page(void)
{
    mi_reset();
    bind_page4_editor("ab");

    /* Run the page-4 arm once to enable, then navigate to a non-editable
     * page: the top-of-dispatch reset clears the enable gate and the
     * page-1 arm never sets it back, so WM_CHAR inserts are dropped. */
    engine_menu_click_in_t in = at(0, 0);
    (void)engine_menu_title_input(&in);
    T_ASSERT_EQ_I(g_test_p4_ed.enabled, 1);

    engine_title_render_set_page(1);
    (void)engine_menu_title_input(&in);
    T_ASSERT_EQ_I(g_test_p4_ed.enabled, 0);     /* DAT_005aeb78 reset, not re-set */

    /* A keystroke now no-ops (insert_char gates on enabled). */
    text_input_filtered_keypress(&g_test_p4_ed, 'Z');
    T_ASSERT(strcmp((const char *)g_test_p4_ed.buf, "ab") == 0);
    return 0;
}

/* ── page 3 ("Loading a game." file list) — the button-record hit-test
 *    (FUN_0040da1e) over the Load / Cancel / scroll strip, plus the
 *    shared editor streaming into the filename field ── */

/* Centres of the four page-3 buttons at the strip origin (0x60, 10).  A
 * record's `kind` is its square side (Load/Cancel kind 0x20 = 32 px;
 * arrows kind 0x18 = 24 px), so the box is [x+ox, x+kind+ox). */
enum {
    P3_LOAD_X   = 0x1a0, P3_LOAD_Y     = 0x5a,
    P3_CANCEL_X = 0x1d0, P3_CANCEL_Y   = 0x5a,
    P3_ARROW_X  = 0x1ec, P3_SCRUP_Y    = 0xa6, P3_SCRDN_Y = 0xc6,
};

static void reset_filelist_buttons(void)
{
    int n = 0;
    engine_button_rec_t *r = engine_title_render_filelist_button_table(&n);
    for (int i = 0; i < n; i++) { r[i].pressed = 0; r[i].state = 0; r[i].pad_e = 0; }
}

int test_menu_page3_load_sets_request(void)
{
    mi_reset();
    reset_filelist_buttons();
    engine_title_render_set_page(3);

    engine_menu_click_in_t in = press_at(P3_LOAD_X, P3_LOAD_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_menu_input_filelist_load_request(), 100);  /* DAT_005cd41c */
    return 0;
}

/* End-to-end wiring of the page-3 grid selector through the exact
 * live-tick sequence (engine_tick_menu_input_step): run the click router
 * FUN_004ba9c8 (engine_menu_title_input); when it does NOT consume the
 * click, run the outer-dispatcher page-3 grid arm FUN_0042ff10 →
 * FUN_0043400b (engine_menu_input_filelist_grid_step).  Proves a save-row
 * click selects the entry + loads its name into the filename field, then
 * a Load-button click latches the request the load pump consumes. */
static int live_tick_menu_input(const engine_menu_click_in_t *in)
{
    if (engine_menu_title_input(in) == 0)
        return engine_menu_input_filelist_grid_step(in);
    return 1;   /* the click router consumed it */
}

static const char *g_p3it_names[4] = {"lastturn","old_turn","safeturn","test"};
static uint8_t g_p3it_entries[4 * 0x41];

int test_menu_page3_grid_click_selects_then_load_latches(void)
{
    mi_reset();
    reset_filelist_buttons();
    /* The live caller (engine_tick_menu_input_step) gates on screen 0x1f
     * before reaching grid_step, so the page-3 gate is the relevant one. */
    engine_title_render_set_page(3);              /* DAT_005530f0 */
    memset(g_p3it_entries, 0, sizeof g_p3it_entries);
    for (int e = 0; e < 4; e++)
        for (int j = 0; g_p3it_names[e][j]; j++)
            g_p3it_entries[e * 0x41 + j] = (uint8_t)g_p3it_names[e][j];
    engine_title_render_set_filelist(g_p3it_entries, 0x41, 4, 0);
    text_input_state_t ed;
    text_input_init(&ed, "", 8, 0xa0, 1);
    engine_title_render_set_filelist_editor(&ed);

    /* A click on grid row 0, col 1 ("old_turn") — misses the Load/Cancel/
     * scroll strip, so the click router returns 0 and the grid arm runs. */
    engine_menu_click_in_t row = press_at(0x100, 0x80);
    T_ASSERT_EQ_I(live_tick_menu_input(&row), 1);          /* grid consumed */
    T_ASSERT_EQ_I(engine_title_render_filelist_selected(), 1);
    T_ASSERT(strcmp((const char *)engine_title_render_filelist_name(),
                    "old_turn") == 0);

    /* Now a Load-button click latches the load request (DAT_005cd41c). */
    engine_menu_click_in_t load = press_at(P3_LOAD_X, P3_LOAD_Y);
    T_ASSERT_EQ_I(live_tick_menu_input(&load), 1);         /* router consumed */
    T_ASSERT_EQ_I(engine_menu_input_filelist_load_request(), 100);
    /* The filename the pump will build "<name>.sav" from is the clicked
     * entry — name_tail re-streamed the committed editor on the Load frame. */
    T_ASSERT(strcmp((const char *)engine_title_render_filelist_name(),
                    "old_turn") == 0);

    engine_title_render_set_filelist_editor(0);
    return 0;
}

int test_menu_page3_cancel_returns_to_bookmark(void)
{
    mi_reset();
    reset_filelist_buttons();
    /* Reach page 3 the real way (page-2 "Load a game") so the return-page
     * bookmark (DAT_00553e5c) is set to 2. */
    engine_title_render_set_page(2);
    engine_menu_click_in_t load = press_at(BTN_X, row_y(1));
    (void)engine_menu_title_input(&load);
    T_ASSERT_EQ_I(engine_title_render_page(), 3);

    engine_menu_click_in_t in = press_at(P3_CANCEL_X, P3_CANCEL_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_page(), 2);                  /* bookmark */
    T_ASSERT_EQ_I(engine_menu_input_last_redraw_mode(), 2);       /* DAT_0057d340 */
    return 0;
}

int test_menu_page3_scroll_down_advances_first_visible(void)
{
    mi_reset();
    reset_filelist_buttons();
    engine_title_render_set_page(3);
    engine_title_render_set_filelist(0, 0x41, 40, 0);   /* ≥30 entries → scrolls */

    engine_menu_click_in_t in = press_at(P3_ARROW_X, P3_SCRDN_Y);  /* +3 */
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_filelist_first_visible(), 3);
    return 0;
}

int test_menu_page3_scroll_up_clamps_at_zero(void)
{
    mi_reset();
    reset_filelist_buttons();
    engine_title_render_set_page(3);
    engine_title_render_set_filelist(0, 0x41, 40, 0);

    engine_menu_click_in_t in = press_at(P3_ARROW_X, P3_SCRUP_Y);  /* -3 */
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_filelist_first_visible(), 0);
    return 0;
}

int test_menu_page3_scroll_forced_zero_with_few_entries(void)
{
    mi_reset();
    reset_filelist_buttons();
    engine_title_render_set_page(3);
    engine_title_render_set_filelist(0, 0x41, 4, 0);    /* <30 → mode-1 forces 0 */

    engine_menu_click_in_t in = press_at(P3_ARROW_X, P3_SCRDN_Y);
    T_ASSERT_EQ_I(engine_menu_title_input(&in), 1);
    T_ASSERT_EQ_I(engine_title_render_filelist_first_visible(), 0);
    return 0;
}

/* The click flash: a pressed type-4 button latches pressed=3 / state=1,
 * and the per-frame countdown un-presses it three frames later. */
int test_menu_page3_click_flashes_then_settles(void)
{
    mi_reset();
    reset_filelist_buttons();
    engine_title_render_set_page(3);

    int n = 0;
    engine_button_rec_t *recs = engine_title_render_filelist_button_table(&n);

    engine_menu_click_in_t click = press_at(P3_LOAD_X, P3_LOAD_Y);
    (void)engine_menu_title_input(&click);
    T_ASSERT_EQ_I(recs[0].pressed, 3);   /* Load latched pressed */
    T_ASSERT_EQ_I(recs[0].state, 1);

    /* Three idle frames count the timer down to 0 (un-pressed). */
    engine_menu_click_in_t idle = at(0, 0);
    (void)engine_menu_title_input(&idle);   /* 3 → 2 */
    (void)engine_menu_title_input(&idle);   /* 2 → 1 */
    (void)engine_menu_title_input(&idle);   /* 1 → 0 */
    T_ASSERT_EQ_I(recs[0].pressed, 0);
    T_ASSERT_EQ_I(recs[0].state, 1);        /* settles on the released frame */
    return 0;
}

/* Holding a scroll arrow auto-repeats, gated by the accel table: the
 * first 7 held frames are the initial delay (no fire); the 8th fires. */
int test_menu_page3_scroll_autorepeat_gated_by_delay(void)
{
    mi_reset();
    reset_filelist_buttons();
    engine_title_render_set_page(3);
    engine_title_render_set_filelist(0, 0x41, 40, 0);

    engine_menu_click_in_t hold = at(P3_ARROW_X, P3_SCRDN_Y);
    hold.lmb_held    = 1;
    hold.repeat_tick = 1;     /* a real input frame */

    /* Frames 1..7 — counter < 8, no fire. */
    for (int f = 0; f < 7; f++) {
        (void)engine_menu_title_input(&hold);
        T_ASSERT_EQ_I(engine_title_render_filelist_first_visible(), 0);
    }
    /* Frame 8 — counter == 8, accel gate open → one +3 step fires. */
    (void)engine_menu_title_input(&hold);
    T_ASSERT_EQ_I(engine_title_render_filelist_first_visible(), 3);
    return 0;
}

/* The shared title editor streams into the page-3 filename field, and a
 * typed character flows through on the next frame — the page-3 twin of
 * the page-4 name-box route, proving the single-editor unification. */
static text_input_state_t g_test_fl_ed;
int test_menu_page3_editor_streams_into_filename(void)
{
    mi_reset();
    reset_filelist_buttons();
    text_input_init(&g_test_fl_ed, "save1", 0x40, 0xc0, 0);
    g_test_fl_ed.insert_mode = 0;                       /* overwrite */
    engine_title_render_set_page(3);
    engine_title_render_set_filelist_topname((const uint8_t *)"xxxxx");
    engine_title_render_set_filelist_editor(&g_test_fl_ed);

    /* Frame 1: the page-3 arm enables the editor and copies it out,
     * overwriting the seeded "xxxxx" field with the editor text. */
    engine_menu_click_in_t in = at(0, 0);              /* off every button */
    (void)engine_menu_title_input(&in);
    T_ASSERT_EQ_I(g_test_fl_ed.enabled, 1);            /* DAT_005aeb78 = 1 */
    T_ASSERT(strcmp((const char *)engine_title_render_filelist_name(),
                    "save1") == 0);

    /* A keystroke (overwrite at cursor 0: 'S' replaces 's') then frame 2
     * streams the edit into the filename field. */
    text_input_filtered_keypress(&g_test_fl_ed, 'S');
    (void)engine_menu_title_input(&in);
    T_ASSERT(strcmp((const char *)engine_title_render_filelist_name(),
                    "Save1") == 0);
    return 0;
}
