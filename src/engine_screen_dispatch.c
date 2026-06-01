/*
 * OpenLords2 — top-level screen-state render dispatch (FUN_0040f1a0).
 * See engine_screen_dispatch.h.
 *
 * Partial port: of the ~40 screen-state arms in retail FUN_0040f1a0,
 * the title screen (0x1f → engine_title_render_scene) and the in-game
 * strategic map (0 → FUN_0040f5fd, via the bound strat hook →
 * engine_screen_strat_render) are ported.  The remaining arms
 * (screen-states '5','6','\x1c','E','.','/', … — the dialog / setup
 * screens whose renderers FUN_004148xx … are still unported) are
 * funnelled through a single bound hook so the dispatch is structurally
 * complete and ready to grow one arm at a time.
 */

#include <stddef.h>

#include "engine_screen_dispatch.h"
#include "engine_title_render.h"

static int g_last_other_state;
static int g_last_other_redraw;
static int g_last_strat_redraw;

static void other_stub(int screen_state, int redraw_mode)
{
    g_last_other_state  = screen_state;
    g_last_other_redraw = redraw_mode;
}
static engine_screen_dispatch_other_fn g_other_fn = other_stub;

static void strat_stub(int redraw_mode)
{
    g_last_strat_redraw = redraw_mode;
}
static engine_screen_dispatch_strat_fn g_strat_fn = strat_stub;

void engine_screen_dispatch_bind_other_fn(engine_screen_dispatch_other_fn fn)
{
    g_other_fn = fn ? fn : other_stub;
}

void engine_screen_dispatch_bind_strat_fn(engine_screen_dispatch_strat_fn fn)
{
    g_strat_fn = fn ? fn : strat_stub;
}

int  engine_screen_dispatch_last_other_state(void)  { return g_last_other_state; }
int  engine_screen_dispatch_last_other_redraw(void) { return g_last_other_redraw; }
int  engine_screen_dispatch_last_strat_redraw(void) { return g_last_strat_redraw; }
void engine_screen_dispatch_reset(void)
{
    g_last_other_state  = 0;
    g_last_other_redraw = 0;
    g_last_strat_redraw = 0;
}

void engine_screen_dispatch(int screen_state, int page, int redraw_mode)
{
    if (screen_state == 0x1f) {
        /* Title screen — the page sub-dispatch lives in the scene. */
        engine_title_render_scene(screen_state, page);
    } else if (screen_state == 0) {
        /* Strategic map (FUN_0040f5fd) — the in-game overworld arm. */
        g_strat_fn(redraw_mode);
    } else {
        /* All other screens: not yet ported individually. */
        g_other_fn(screen_state, redraw_mode);
    }

    /* Retail clears DAT_004eb260 unconditionally at the tail. */
    engine_title_render_set_scene_flag_b(0);
}
