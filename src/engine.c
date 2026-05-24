/*
 * Per-tick driver + outer wrappers.  See engine.h for the call-chain
 * and the retail-FUN_xxxxx ↔ engine_* mapping.
 *
 * The three functions here close a structural gap in the main loop:
 * before this port, main_loop_body() called gfx_tick_present(0)
 * directly with no surface-lost check, no engine-state dispatch, no
 * deferred-shutdown handling.  After this port the loop has the
 * same shape as retail's `FUN_0040eb4c → FUN_0040e8bb → FUN_0040e890
 * → FUN_004b99c0`, even though engine_tick itself is still a
 * scaffold (no sim, no per-subsystem render) — every future
 * subsystem port slots into engine_tick rather than into the loop
 * body, which is exactly where retail puts it too.
 */

#include "engine.h"

#include "gfx.h"

#include <stdint.h>

int g_engine_state;
int g_shutdown_request;
int g_savepump_request;

/* Wall-clock frame counter — mirror of retail _DAT_005bb4c8.  Bumped
 * unconditionally at the head of engine_tick (every Win32-pump
 * iteration, regardless of sim-step count).  No consumer yet; retail
 * uses it as a "frames since boot" cookie across save metadata and
 * the dev profiler. */
static uint32_t g_engine_frame_counter;

/* TODO(savepump): retail's save/load pump.
 *
 * The chain is:
 *   FUN_004978ad  — SP "begin save/load" hook
 *   FUN_0049792e  — MP "begin save/load" hook
 *   FUN_00497a34  — the pump step itself (SP=0, MP=1 arg)
 *
 * Together they drive level loading, savefile (de)serialisation, the
 * mid-game state-transition cinematics, and the menu→ingame /
 * ingame→menu screens.  ~10 KB of code that touches sim, audio,
 * sprite, UI, and persistence — too large for one slice.
 *
 * Engine behaviour is identical with these stubbed today because
 * g_savepump_request can only get set when g_engine_state > 8, and
 * no current code path can transition the engine state above 3
 * (the value we use when ingame).  When the save subsystem lands,
 * these stubs get replaced with the real chain. */
static void savepump_begin_sp(void) {}
static void savepump_step(int is_mp) { (void)is_mp; }
/* MP equivalent (port of FUN_0049792e) lands with the multiplayer
 * slice; until then engine_tick_guard never takes the MP branch. */

void engine_tick(void)
{
    /* Scaffold of retail FUN_004b99c0 @ 0x4b99c0.  The retail body
     * is ~50 calls — pre-tick block, variable-rate sim sub-step
     * loop, render phase tail.  Each will land in a follow-up
     * slice; see docs/findings/main-loop-tick.md for the per-
     * subfunction breakdown.
     *
     * Today we do only the two things that have no subsystem
     * dependencies:
     *   1. Bump the wall-clock frame counter (retail _DAT_005bb4c8).
     *   2. Call the rate-gated present (retail FUN_00452160(0)). */

    g_engine_frame_counter++;

    gfx_tick_present(0);
}

void engine_tick_dispatch(void)
{
    /* Port of retail FUN_0040e890 @ 0x40e890 (43 bytes).  Identical
     * shape: call the per-tick driver, then latch the save-pump
     * request if the engine asked to fast-skip the render. */

    engine_tick();

    if (g_engine_state > 8) {
        g_savepump_request = 1;
    }
}

int engine_tick_guard(void)
{
    /* Port of retail FUN_0040e8bb @ 0x40e8bb.  Same control flow,
     * one structural deviation noted inline.
     *
     * Retail polls IDirectDrawSurface::IsLost (vtable+0x60) on both
     * the primary and back surfaces every iteration; the call sets
     * the SURFACELOST flag if either reports DDERR_SURFACELOST.
     * Our gfx_surface_lost() is a *latch* — it stays sticky once any
     * present-side Blt or Lock returned DDERR_SURFACELOST, and
     * clears on a successful gfx_restore() (or gfx_init() reseat).
     * Same observable: the present path is the only producer of
     * fresh IsLost results in retail too, so a sticky latch from
     * the present path is equivalent to a per-iteration vtable poll
     * for our purposes.  Documented in gfx.c. */
    if (gfx_surface_lost()) {
        if (!gfx_restore()) {
            /* Retail also calls FUN_004b26b8 here (logs "OK :Paused."
             * + InvalidateRect(hwnd, NULL, TRUE)).  Skipping for
             * now — we don't have the structured log channel for
             * that string yet, and the InvalidateRect would
             * re-trigger WM_PAINT which our WndProc already handles
             * via Begin/EndPaint as a no-op.  Comment-only TODO
             * tracked here so this jumps out when the next gfx
             * slice lands. */
            return 0;
        }
    }

    /* WM_CLOSE arrived last frame; the sim had one frame to drain.
     * Escalate to "real" shutdown next iteration. */
    if (g_shutdown_request == 3) {
        g_shutdown_request = 1;
    }

    /* Save-pump dispatcher.  Retail branches on DAT_00553030 (the
     * multiplayer-active flag) — we don't have that mirror yet and
     * the drop-in is SP-only today, so always take the SP arm.  The
     * stubs are no-ops; identical behaviour. */
    if (g_savepump_request) {
        savepump_begin_sp();   /* TODO(mp): FUN_0049792e on MP arm */
        savepump_step(0);      /* TODO(mp): 1 on MP arm */
    }

    if (g_shutdown_request == 1) {
        return 0;
    }
    engine_tick_dispatch();
    return 1;
}
