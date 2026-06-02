/*
 * Host-side stubs for libengine_diff.so (tools/diff_test.py).
 *
 * The diff shared library links the host-clean source set (DIFF_SRCS =
 * SRCS_UNDER_TEST) but NOT the Win32-only translation units (engine.c, io.c,
 * main.c).  A handful of globals and helpers those TUs would define are
 * referenced by the linked sources; the host test binary supplies equivalent
 * stubs scattered across its test_*.c files.  This TU collects the same stubs
 * for the .so so ctypes can dlopen it (eager symbol resolution).
 *
 * These are inert: the diff targets exercise pure/stateful engine functions
 * that read these globals as plain state (the orchestrator injects whatever it
 * needs through the real globals) and never drive logging or file I/O.
 */

#include <stddef.h>

/* engine.c runtime sim flags (retail DAT_00553030 etc.) — plain state. */
int g_sim_mp_active;
int g_sim_mp_variant;
int g_sim_save_rotate_hold;

/* engine.c engine-phase scalar (retail DAT_0052af94); game_init.c /
 * engine_load_pump.c (both linked here) write it to 3 (in-game).  Inert for
 * the diff targets, which never read it. */
int g_engine_state;

/* engine.c screen / demand-redraw accessors that game_init.c / engine_load_pump.c
 * call when transitioning into the in-game map.  Mirrors the inert stubs the
 * test binary keeps in test_engine_load_pump.c — plain state, never observed by
 * the diff targets. */
static int g_stub_screen;
static int g_stub_redraw;
int  engine_current_screen(void)        { return g_stub_screen; }
void engine_set_current_screen(int s)   { g_stub_screen = s; }
void engine_set_demand_redraw(int mode) { g_stub_redraw = mode; }

/* main.c logging sink — suppressed, as in the test binary. */
void log_line(const char *fmt, ...)
{
    (void)fmt;
}

/* io.c file reader — "not found" (diff targets never hit real file I/O). */
int io_read_file_at(const char *path, void *buf, size_t len, long offset)
{
    (void)path; (void)buf; (void)len; (void)offset;
    return 0;
}
