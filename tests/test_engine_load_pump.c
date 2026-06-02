/*
 * Unit tests for engine_load_pump — the title-menu file-load confirm
 * pump (SP path of FUN_004ad9f0).  Drives the latch → confirm-timer →
 * load + ingame-transition sequence with a fake load hook, asserting the
 * filename build and the engine-state writes the live loop depends on.
 */

#include "t.h"
#include "engine_load_pump.h"
#include "engine_menu_input.h"
#include "engine_title_render.h"

/* ── Stubs for the engine.c symbols the pump touches (engine.c is the
 *    Win32 live driver — not in the host-test link). ── */
int  g_engine_state;
static int g_stub_screen;
static int g_stub_redraw;
int  engine_current_screen(void)        { return g_stub_screen; }
void engine_set_current_screen(int s)   { g_stub_screen = s; }
void engine_set_demand_redraw(int mode) { g_stub_redraw = mode; }

/* ── Fake load hook: records the filename the pump hands it. ── */
static int   g_load_calls;
static char  g_load_seen[32];
static int   g_load_rc;
static int fake_load(const char *filename)
{
    g_load_calls++;
    size_t i = 0;
    for (; filename[i] && i < sizeof g_load_seen - 1; i++)
        g_load_seen[i] = filename[i];
    g_load_seen[i] = '\0';
    return g_load_rc;
}

static void pump_reset(void)
{
    engine_load_pump_reset();
    engine_load_pump_bind_load_fn(fake_load);
    engine_menu_input_set_filelist_load_request(0);
    g_engine_state = 0;
    g_stub_screen  = 0x1f;          /* title screen */
    g_stub_redraw  = 0;
    g_load_calls   = 0;
    g_load_seen[0] = '\0';
    g_load_rc      = 0;             /* load succeeds */
}

/* A latched request arms the ~150-frame confirm timer (and clears the
 * latch) on the first tick, building the save filename from the selected
 * name field + the ".sav" extension. */
int test_load_pump_request_arms_timer_and_builds_name(void)
{
    pump_reset();
    engine_title_render_set_filelist_topname((const uint8_t *)"lastturn");
    engine_menu_input_set_filelist_load_request(100);

    engine_load_pump_tick();

    T_ASSERT_EQ_I(engine_load_pump_timer(), 0x96);
    T_ASSERT_EQ_I(engine_menu_input_filelist_load_request(), 0);
    T_ASSERT_EQ_S(engine_load_pump_filename(), "lastturn.sav");
    /* Not loaded yet — the timer must expire first. */
    T_ASSERT_EQ_I(g_load_calls, 0);
    return 0;
}

/* When the confirm timer expires on the title screen, the pump invokes
 * the load hook with the built filename and transitions the engine into
 * the in-game state (g_engine_state = 3, demand-redraw = 2). */
int test_load_pump_timer_expiry_loads_and_transitions(void)
{
    pump_reset();
    engine_title_render_set_filelist_topname((const uint8_t *)"lastturn");
    engine_menu_input_set_filelist_load_request(100);

    engine_load_pump_tick();                 /* arms timer = 0x96 */
    int guard = 0;
    while (engine_load_pump_timer() != 0 && guard++ < 1000)
        engine_load_pump_tick();

    T_ASSERT_EQ_I(g_load_calls, 1);
    T_ASSERT_EQ_S(g_load_seen, "lastturn.sav");
    T_ASSERT_EQ_I(g_engine_state, 3);        /* DAT_0052af94 = 3 (ingame) */
    T_ASSERT_EQ_I(g_stub_redraw, 2);         /* DAT_0057d340 = 2 */
    return 0;
}

/* The 8.3 stem is capped at 8 chars before the extension is appended. */
int test_load_pump_filename_caps_stem_to_eight(void)
{
    pump_reset();
    engine_title_render_set_filelist_topname((const uint8_t *)"abcdefghij");
    engine_menu_input_set_filelist_load_request(100);

    engine_load_pump_tick();

    T_ASSERT_EQ_S(engine_load_pump_filename(), "abcdefgh.sav");
    return 0;
}

/* With no request latched, the pump is an inert no-op (no timer, no
 * load, no state change) — safe to call every frame. */
int test_load_pump_idle_is_noop(void)
{
    pump_reset();

    for (int i = 0; i < 200; i++)
        engine_load_pump_tick();

    T_ASSERT_EQ_I(engine_load_pump_timer(), 0);
    T_ASSERT_EQ_I(g_load_calls, 0);
    T_ASSERT_EQ_I(g_engine_state, 0);
    return 0;
}
