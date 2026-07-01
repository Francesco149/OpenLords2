/*
 * Unit tests for engine_confirm_dialog — the generic Yes/No modal
 * (screen 0x1e).  Port of FUN_0040e6f2 / FUN_0040ccfa / FUN_00434e1f.
 */

#include <string.h>

#include "t.h"
#include "engine_confirm_dialog.h"

static engine_menu_click_in_t at(int x, int y)
{
    engine_menu_click_in_t in;
    memset(&in, 0, sizeof in);
    in.cursor_x = x;
    in.cursor_y = y;
    return in;
}

static int g_fire_count;
static int g_last_answer;
static int g_last_prev_screen;

static void record_answer(void)
{
    g_fire_count++;
    g_last_answer      = engine_confirm_dialog_answer();
    g_last_prev_screen = engine_confirm_dialog_prev_screen();
}

static void reset(void)
{
    g_fire_count = 0;
    g_last_answer = -1;
    g_last_prev_screen = -1;
}

/* Drive the type-5 press-then-countdown animation to completion: an
 * initial press-edge frame arms it (engine_menu_input.c's pass 2), then
 * 20 more frames (pressed 0x14 counting down to 0) fire it in pass 1. */
static int click_and_settle(int x, int y)
{
    engine_menu_click_in_t press = at(x, y);
    press.lmb_newly_pressed = 1;
    int r = engine_confirm_dialog_input_step(&press);

    engine_menu_click_in_t hold = at(x, y);
    for (int i = 0; i < 20; i++) {
        r = engine_confirm_dialog_input_step(&hold) || r;
    }
    return r;
}

int test_confirm_dialog_open_sets_screen_and_redraw(void)
{
    reset();
    int screen = -1, redraw = -1;
    engine_confirm_dialog_open(4, 0xa0, 0xa0, /*prev_screen=*/0x10,
                               record_answer, &screen, &redraw);
    T_ASSERT_EQ_I(screen, ENGINE_CONFIRM_DIALOG_SCREEN);
    T_ASSERT_EQ_I(redraw, 2);
    T_ASSERT_EQ_I(engine_confirm_dialog_answer(), 0);
    T_ASSERT_EQ_I(engine_confirm_dialog_prev_screen(), 0x10);
    return 0;
}

int test_confirm_dialog_yes_button_fires_answer_1(void)
{
    reset();
    int screen, redraw;
    engine_confirm_dialog_open(7, 0xa0, 0xa0, 0x10, record_answer,
                               &screen, &redraw);
    /* Yes box: x in [0xa0+64, 0xa0+64+32), y in [0xa0+46, 0xa0+46+32). */
    int fired = click_and_settle(0xa0 + 64 + 4, 0xa0 + 46 + 4);
    T_ASSERT(fired != 0);
    T_ASSERT_EQ_I(g_fire_count, 1);
    T_ASSERT_EQ_I(g_last_answer, 1);
    T_ASSERT_EQ_I(g_last_prev_screen, 0x10);
    T_ASSERT_EQ_I(engine_confirm_dialog_answer(), 1);
    return 0;
}

int test_confirm_dialog_no_button_fires_answer_0(void)
{
    reset();
    int screen, redraw;
    engine_confirm_dialog_open(7, 0xa0, 0xa0, 0x10, record_answer,
                               &screen, &redraw);
    /* No box: x in [0xa0+112, 0xa0+112+32), y in [0xa0+50, 0xa0+50+32). */
    int fired = click_and_settle(0xa0 + 112 + 4, 0xa0 + 50 + 4);
    T_ASSERT(fired != 0);
    T_ASSERT_EQ_I(g_fire_count, 1);
    T_ASSERT_EQ_I(g_last_answer, 0);
    return 0;
}

int test_confirm_dialog_click_outside_buttons_does_not_fire(void)
{
    reset();
    int screen, redraw;
    engine_confirm_dialog_open(7, 0xa0, 0xa0, 0x10, record_answer,
                               &screen, &redraw);
    int fired = click_and_settle(0, 0);
    T_ASSERT_EQ_I(fired, 0);
    T_ASSERT_EQ_I(g_fire_count, 0);
    return 0;
}

int test_confirm_dialog_reopen_resets_button_animation_state(void)
{
    reset();
    int screen, redraw;
    /* Arm (but don't settle) the Yes button, then reopen — a fresh open
     * must not leave a stale countdown that fires later unexpectedly. */
    engine_confirm_dialog_open(7, 0xa0, 0xa0, 0x10, record_answer,
                               &screen, &redraw);
    engine_menu_click_in_t press = at(0xa0 + 64 + 4, 0xa0 + 46 + 4);
    press.lmb_newly_pressed = 1;
    engine_confirm_dialog_input_step(&press);

    engine_confirm_dialog_open(8, 0x60, 0x60, 0, record_answer,
                               &screen, &redraw);
    engine_menu_click_in_t idle = at(0, 0);
    for (int i = 0; i < 20; i++) {
        engine_confirm_dialog_input_step(&idle);
    }
    T_ASSERT_EQ_I(g_fire_count, 0);
    return 0;
}
