/*
 * OpenLords2 — top-level screen-state render dispatch (FUN_0040f1a0).
 * See engine_screen_dispatch.h.
 *
 * Partial port: of the ~40 screen-state arms in retail FUN_0040f1a0,
 * only the title screen (0x1f → engine_title_render_scene) is ported.
 * The remaining arms (screen-states '5','6','\x1c','E','.','/', … — the
 * in-game / dialog / setup screens whose renderers FUN_004148xx …
 * FUN_0040f5fd are still unported) are funnelled through a single bound
 * hook so the dispatch is structurally complete and ready to grow one
 * arm at a time.
 */

#include <stddef.h>

#include "engine_screen_dispatch.h"
#include "engine_title_render.h"

static int g_last_other_state;
static int g_last_other_redraw;

static void other_stub(int screen_state, int redraw_mode)
{
    g_last_other_state  = screen_state;
    g_last_other_redraw = redraw_mode;
}
static engine_screen_dispatch_other_fn g_other_fn = other_stub;

void engine_screen_dispatch_bind_other_fn(engine_screen_dispatch_other_fn fn)
{
    g_other_fn = fn ? fn : other_stub;
}

int  engine_screen_dispatch_last_other_state(void)  { return g_last_other_state; }
int  engine_screen_dispatch_last_other_redraw(void) { return g_last_other_redraw; }
void engine_screen_dispatch_reset(void)
{
    g_last_other_state  = 0;
    g_last_other_redraw = 0;
}

void engine_screen_dispatch(int screen_state, int page, int redraw_mode)
{
    if (screen_state == 0x1f) {
        /* Title screen — the page sub-dispatch lives in the scene. */
        engine_title_render_scene(screen_state, page);
    } else {
        /* All other screens: not yet ported individually. */
        g_other_fn(screen_state, redraw_mode);
    }

    /* Retail clears DAT_004eb260 unconditionally at the tail. */
    engine_title_render_set_scene_flag_b(0);
}
