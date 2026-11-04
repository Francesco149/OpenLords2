/*
 * OpenLords2 — title-menu file-load confirm pump (FUN_004ad9f0).
 * See engine_load_pump.h.
 *
 * Partial port: the single-player title-screen load path only.  Retail's
 * FUN_004ad9f0 has three request sources (the page-3 list Load button on
 * the title screen, and the in-game save/load dialog states '5'/'6') and
 * a multiplayer arm; we port the SP title-load that drives menu→ingame.
 */

#include <stddef.h>
#include <string.h>

#include "engine.h"
#include "engine_load_pump.h"
#include "engine_menu_input.h"
#include "engine_title_render.h"

/* Pump state (retail addresses in comments). */
static int  g_timer;              /* DAT_0057d3c4 — confirm countdown   */
static char g_inflight;           /* DAT_00591548 — load-in-progress latch */
static char g_fname[16];          /* DAT_004ead60 — built save filename  */

static int load_stub(const char *filename) { (void)filename; return 1; }
static engine_load_pump_load_fn g_load_fn = load_stub;

void engine_load_pump_bind_load_fn(engine_load_pump_load_fn fn)
{
    g_load_fn = fn ? fn : load_stub;
}

int         engine_load_pump_timer(void)    { return g_timer; }
const char *engine_load_pump_filename(void) { return g_fname; }

void engine_load_pump_reset(void)
{
    g_timer = 0;
    g_inflight = 0;
    g_fname[0] = '\0';
}

/* Build the save's filename from the page-3 selected-name field
 * (retail DAT_004ea130, our engine_title_render_filelist_name) plus the
 * single-player ".sav" extension (retail copies the 3-char ext from the
 * const at 0x4df070 into DAT_004eafa4, then FUN_004af675 appends
 * "." + ext + '\0').  MP families (.svb/.sva, 0x4df06c/0x4df068) are
 * deferred. */
static void build_filename(void)
{
    const char *base = (const char *)engine_title_render_filelist_name();
    size_t n = 0;
    /* Copy the base name up to its NUL, capped to the DOS 8.3 stem so
     * the ".sav" + NUL always fit the 16-byte buffer (retail's 12-byte
     * field copy + FUN_004af675 tail). */
    while (n < 8 && base && base[n] != '\0') {
        g_fname[n] = base[n];
        n++;
    }
    g_fname[n++] = '.';
    g_fname[n++] = 's';
    g_fname[n++] = 'a';
    g_fname[n++] = 'v';
    g_fname[n]   = '\0';
}

/* Engine-state transition into the in-game strategic map — the
 * single-player branch of FUN_004329ec @ 0x4329ec.  On the load path
 * retail enters with DAT_005aeb8c == 1, so the new-game initialiser
 * FUN_00497ced and the DAT_004eac50 = 0 reset are SKIPPED (the screen
 * state was already set to 0 by FUN_004976a1 / engine_map_post_load).
 * It then sets the engine phase to 3 (ingame), runs 0x451f8a (a
 * dirty-rect full mark + render-neutral UI flags — deferred), and raises
 * the demand-redraw cookie to 2.  The timing scalars (DAT_005440c8 /
 * _DAT_00568d9c) it also sets are deferred (non-deterministic). */
static void transition_to_ingame_sp(void)
{
    g_engine_state = 3;            /* DAT_0052af94 = 3 (ingame) */
    engine_set_demand_redraw(2);   /* DAT_0057d340 = 2 */
}

void engine_load_pump_tick(void)
{
    /* Block 1 — the confirm countdown (retail's first block).  Runs
     * before the request handler so the timer arms only on the NEXT
     * frame, matching retail's frame budget. */
    if (g_timer != 0) {
        g_timer--;
        if (g_timer == 0) {
            if (g_inflight == 0) {
                if (engine_current_screen() == 0x1f) {
                    /* SP title-load: deserialise + run the map setup via
                     * the bound hook (retail FUN_004adfe6 → FUN_004976a1).
                     * 0x4adcf6 is a no-op for SP (returns 1), so retail
                     * always takes the transition branch. */
                    g_inflight = (char)g_load_fn(g_fname);
                    transition_to_ingame_sp();
                }
                engine_set_demand_redraw(2);   /* DAT_0057d340 = 2 */
            } else {
                g_inflight = 0;
            }
        }
    }

    /* Block 2 — the Load request latch (retail's second block).  The
     * page-3 Load button sets the request (DAT_005cd41c = 100); arm the
     * ~150-frame confirm timer and build the filename.  The missing-file
     * retry (0x4af767) + the load sound (s040_01.wav) are deferred. */
    if (engine_menu_input_filelist_load_request() != 0) {
        engine_menu_input_set_filelist_load_request(0);   /* DAT_005cd41c = 0 */
        g_timer = 0x96;                                    /* 150 frames */
        build_filename();
    }
}
