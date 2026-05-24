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

#include <mmsystem.h>     /* timeGetTime — gfx.h transitively pulled in
                           * windows.h, but not mmsystem.h */
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

/* ─── Sim rate-controller mirrors (retail FUN_004bb978 state) ──────
 *
 * Each is a file-static mirror of the retail global the rate
 * controller reads/writes.  See engine_rate.c for the per-global
 * commentary; here we only hold the state.  None are exposed in
 * engine.h yet — no consumer outside engine_tick_rate_step. */

/* Last accepted sim-step timestamp (ms).  retail _DAT_004e5b0c. */
static uint32_t g_sim_last_step_ms;
/* Autosave-window start (ms, 0 = no window).  retail DAT_0056d8b4.
 * No producer in our drop-in yet — the autosave subsystem sets it
 * when it begins; the rate controller clears it 5000 ms later. */
static uint32_t g_sim_autosave_start_ms;
/* Master sim tick counter.  retail DAT_0057c998.  Bumped once per
 * sim sub-step body — today nothing produces sub-steps, so it stays
 * 0. */
static uint32_t g_sim_tick_counter;
/* Multiplayer-session active flag.  retail DAT_00553030.  SP only
 * today, so always 0. */
static int g_sim_mp_active;
/* Game-speed slider (0..100, retail _DAT_0053f230).  Default 100
 * (max speed) until the UI slider lands. */
static int g_sim_game_speed = 100;
/* Pause lock — non-zero freezes the sim.  retail DAT_0053f238.
 * Today no UI sets it, so always 0. */
static int g_sim_pause_lock;
/* Current screen ID.  retail DAT_004eac50 — single byte indexing the
 * per-screen tables.  Today nothing transitions screens, so 0. */
static uint8_t g_sim_current_screen;
/* Per-screen freeze table.  retail DAT_004e31b0[].  All zeros today;
 * the real bytes need extraction from retail's .data.  TODO. */
static uint8_t g_sim_screen_freeze_table[256];
/* Extended-pause flag (modal dialog etc.).  retail DAT_0053e910.
 * Today no producer. */
static int g_sim_extended_pause;
/* MP player skip table.  retail DAT_004e3240[], indexed by
 * DAT_005440a4.  Bytes + producers TODO. */
static uint8_t g_sim_mp_skip_table[256];
static uint8_t g_sim_mp_player_index;
/* Phase counter — advances each rate-step call that gets past the
 * scenario_hint == 0 gate.  retail DAT_004d2164.  Wraps at 0xa0. */
static int g_sim_phase;
/* Derived hint mirror.  retail DAT_004d215c.  Written by the rate
 * controller, no documented consumer in retail either — kept for
 * parity. */
static int g_sim_derived_hint;

/* ─── Animation-phase tick mirrors (FUN_004bbc80 state) ────────────
 *
 * One non-sticky flag per "did event X fire this call" cadence
 * (consumers re-read every frame, wrapper clears before refresh)
 * + one sticky flag at 1040 ms (consumers clear themselves).  Plus
 * the eight underlying counters that drive the cadence.
 *
 * All zero at boot, which is correct: the first call sees
 * `delta = now - 0` which is huge, fires the base tick, and from
 * there the counters spin up naturally. */

/* ─── Frame-delta mirrors (FUN_004b20ed state) ─────────────────────
 *
 * `_DAT_004e59ec` baseline timestamp + `DAT_004ea128` last-published
 * delta.  The baseline is read+written each call; the delta is
 * write-only from our side (downstream sprite-animation updaters
 * read it once it lands; none of those callers exist in the drop-in
 * yet so the delta is currently dropped).  Same 30 ms low-pass +
 * signed-gate semantics as retail. */
static uint32_t g_frame_delta_last_ms;         /* _DAT_004e59ec */
static int      g_frame_delta_ms;              /* DAT_004ea128 */

static uint32_t g_phase_last_base_tick_ms;     /* _DAT_004e5b10 */
static uint32_t g_phase_base_tick_counter;     /* DAT_005aeb2c */
static uint32_t g_phase_counter_2;             /* DAT_0058feb4 */
static uint32_t g_phase_counter_4;             /* DAT_0058feb0 */
static uint32_t g_phase_counter_8;             /* DAT_0058feb8 */
static uint32_t g_phase_counter_13;            /* DAT_005aea54 */
static uint32_t g_phase_counter_16;            /* DAT_005aea44 */
static uint32_t g_phase_counter_24;            /* DAT_005aea48 */
static uint32_t g_phase_counter_32;            /* DAT_005aea4c */

/* Live flags — each frame's animation consumers read these.  The
 * eight non-sticky flags are cleared inside the wrapper before the
 * pure function runs (matching retail's clear-at-entry); the sticky
 * one is OR-in only. */
static int g_phase_tick_20ms;                  /* DAT_0058fcb0 */
static int g_phase_tick_80ms;                  /* DAT_00591510 */
static int g_phase_tick_160ms;                 /* DAT_0057d3ac */
static int g_phase_tick_320ms;                 /* DAT_0057d3c8 */
static int g_phase_tick_640ms;                 /* DAT_0058fd08 */
static int g_phase_tick_1040ms_sticky;         /* _DAT_005aeb28 (sticky) */
static int g_phase_tick_1280ms;                /* DAT_005aeb3c */
static int g_phase_tick_1920ms;                /* DAT_005aeb38 */
static int g_phase_tick_2560ms;                /* DAT_005aeb30 */

/* Win32 wrapper around engine_compute_frame_delta — reads
 * timeGetTime, bundles into the input struct, calls the pure
 * function, writes the baseline + delta back into the file-static
 * mirrors.  No-op observable today: nothing consumes
 * g_frame_delta_ms yet.  The structural seam slots the producer
 * into pre-tick at retail's exact position. */
static void engine_tick_delta_step(void)
{
    engine_delta_in_t  in  = {0};
    engine_delta_out_t out = {0};

    in.now_ms        = timeGetTime();
    in.last_frame_ms = g_frame_delta_last_ms;

    engine_compute_frame_delta(&in, &out);

    g_frame_delta_last_ms = out.new_last_frame_ms;
    g_frame_delta_ms      = out.delta_ms;
}

static void engine_tick_phase_step(void)
{
    engine_phase_in_t  in  = {0};
    engine_phase_out_t out = {0};

    in.now_ms              = timeGetTime();
    in.last_base_tick_ms   = g_phase_last_base_tick_ms;
    in.base_tick_counter   = g_phase_base_tick_counter;
    in.counter_2           = g_phase_counter_2;
    in.counter_4           = g_phase_counter_4;
    in.counter_8           = g_phase_counter_8;
    in.counter_13          = g_phase_counter_13;
    in.counter_16          = g_phase_counter_16;
    in.counter_24          = g_phase_counter_24;
    in.counter_32          = g_phase_counter_32;

    engine_compute_phase_ticks(&in, &out);

    /* Clear-then-write for the 8 non-sticky flags (matches retail's
     * unconditional `DAT_xxx = 0` at function entry, then conditional
     * `DAT_xxx = 1` later). */
    g_phase_tick_20ms      = out.tick_20ms;
    g_phase_tick_80ms      = out.tick_80ms;
    g_phase_tick_160ms     = out.tick_160ms;
    g_phase_tick_320ms     = out.tick_320ms;
    g_phase_tick_640ms     = out.tick_640ms;
    g_phase_tick_1280ms    = out.tick_1280ms;
    g_phase_tick_1920ms    = out.tick_1920ms;
    g_phase_tick_2560ms    = out.tick_2560ms;
    /* OR-in for the sticky 1040 ms — retail does NOT clear this at
     * entry, so a fresh fire ORs with the previous (possibly
     * unconsumed) value.  Consumer clears it after reading. */
    g_phase_tick_1040ms_sticky |= out.tick_1040ms;

    /* Counter / timestamp writebacks. */
    g_phase_last_base_tick_ms  = out.new_last_base_tick_ms;
    g_phase_base_tick_counter  = out.new_base_tick_counter;
    g_phase_counter_2          = out.new_counter_2;
    g_phase_counter_4          = out.new_counter_4;
    g_phase_counter_8          = out.new_counter_8;
    g_phase_counter_13         = out.new_counter_13;
    g_phase_counter_16         = out.new_counter_16;
    g_phase_counter_24         = out.new_counter_24;
    g_phase_counter_32         = out.new_counter_32;
}

/* Stubs of the two helpers retail FUN_004bb978 calls inline.  Both
 * are placeholders for full ports; see findings doc for what they
 * really do.
 *
 *   FUN_004bb8c0 — 84 B, `return 1` in retail.  Truly a stub upstream
 *                  too; possibly a debug-build pause toggle that
 *                  shipped optimised out.
 *
 *   FUN_0043de70 — 1211 B, scenario-rate selector.  Returns 0 unless
 *                  the engine is in ingame state (DAT_0052af94 == 3).
 *                  Our g_engine_state is hardcoded to 0 today, so
 *                  the function effectively returns 0 → the rate
 *                  controller returns 0 sub-steps → no sim runs.
 *                  Full port lands when player-faction data
 *                  (DAT_0057bf04) + current-player mirror
 *                  (DAT_0057c8cc) are available. */
static int engine_run_gate(void)
{
    /* Port of FUN_004bb8c0. */
    return 1;
}

static int engine_scenario_rate_hint(void)
{
    /* Port of FUN_0043de70 — outermost branch only.  All inner
     * branches are guarded on DAT_0052af94 == 3 (ingame), which we
     * can't reach yet, so they're elided.  When ingame state
     * becomes producible, the full selector lands here. */
    if (g_engine_state == 3) {
        /* TODO(sim): full FUN_0043de70 body — selects ms-per-step
         * from the scenario's per-player tick budget + the current
         * sub-state.  Returns negative for fast-skip mode. */
        return 1;   /* harmless default — real selector returns a
                     * scenario-derived value */
    }
    return 0;
}

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

/* Win32 wrapper around engine_compute_sim_sub_steps — bundles the
 * file-static mirrors into an input struct, runs the pure function,
 * writes the deltas back, fires the autosave-done notification if
 * needed.  Returns the sub-step count.
 *
 * No-op for everything but the frame-counter + timeGetTime side
 * effects today: scenario_hint == 0 (engine state != 3) makes the
 * function return 0 every call.  When the sim lands and ingame state
 * becomes reachable, sub_steps will start producing 1 or 2 each
 * iteration on a slider-and-LUT-driven cadence. */
static int engine_tick_rate_step(void)
{
    engine_rate_in_t  in  = {0};
    engine_rate_out_t out = {0};

    in.now_ms             = timeGetTime();
    in.last_step_ms       = g_sim_last_step_ms;
    in.autosave_start_ms  = g_sim_autosave_start_ms;
    in.run_gate           = engine_run_gate();
    in.scenario_hint      = engine_scenario_rate_hint();
    in.mp_active          = g_sim_mp_active;
    in.sim_tick_counter   = g_sim_tick_counter;
    in.game_speed         = g_sim_game_speed;
    in.pause_lock         = g_sim_pause_lock;
    in.screen_freeze      = g_sim_screen_freeze_table[g_sim_current_screen];
    in.extended_pause     = g_sim_extended_pause;
    in.mp_skip_flag       = g_sim_mp_skip_table[g_sim_mp_player_index];
    in.phase              = g_sim_phase;
    in.rate_lut           = NULL;       /* TODO: extract DAT_004e3260 bytes from
                                         * retail.  NULL → treat as all-zeros,
                                         * which forces 0 sub-steps via the
                                         * LUT==0 branch.  Today the function
                                         * returns 0 before reaching the LUT
                                         * anyway (scenario_hint == 0). */

    engine_compute_sim_sub_steps(&in, &out);

    g_sim_last_step_ms        = out.new_last_step_ms;
    g_sim_autosave_start_ms   = out.new_autosave_start_ms;
    g_sim_phase               = out.new_phase;
    g_sim_derived_hint        = out.derived_hint;
    if (out.notify_autosave_done) {
        /* TODO(mp): port FUN_0043eda0(0xf, 0) — the MP "autosave
         * complete" broadcast.  Today MP isn't wired, so the flag
         * never fires (g_sim_mp_active == 0 short-circuits the
         * notification path inside the pure function too). */
    }

    return out.sub_steps;
}

void engine_tick(void)
{
    /* Scaffold of retail FUN_004b99c0 @ 0x4b99c0.  The retail body
     * is ~50 calls — pre-tick block, variable-rate sim sub-step
     * loop, render phase tail.  Each will land in a follow-up
     * slice; see docs/findings/main-loop-tick.md for the per-
     * subfunction breakdown.
     *
     * Today's scaffold has the structural skeleton in place:
     *   1. Bump the wall-clock frame counter (retail _DAT_005bb4c8).
     *   2. Pre-tick block — TODO (all retail subfunctions still
     *      unported: FUN_004b0aaa input poll, FUN_00499a1c music,
     *      FUN_00498458 unknown, FUN_004b20ed unknown, …).
     *   3. Variable-rate sim sub-step loop — driven by
     *      engine_tick_rate_step.  Currently produces 0 sub-steps
     *      because scenario_hint is hardcoded to "engine state != 3
     *      → 0", so the loop body never executes.
     *   4. Render phase — TODO (all retail subfunctions unported).
     *   5. Rate-gated present (retail FUN_00452160(0)) — runs once
     *      per call regardless of sub-step count. */

    g_engine_frame_counter++;

    /* Pre-tick block — retail does (in order): FUN_004b0aaa input
     * poll, FUN_00499a1c music (only when ingame), FUN_00498458,
     * FUN_004b20ed (← ported), FUN_004bbc80 (← ported), FUN_00481b9a,
     * FUN_00482111, FUN_004782c5, FUN_00451f1a.  Order between
     * the pure timing slices doesn't matter for behavior (each is
     * independent state update); we keep them in retail order so
     * future bisection against retail traces stays apples-to-apples. */
    engine_tick_delta_step();
    engine_tick_phase_step();
    /* TODO(sim): remaining pre-tick subfunctions. */

    int sub_steps = engine_tick_rate_step();
    for (int i = 0; i < sub_steps; i++) {
        /* TODO(sim): sub-step body.  Retail does:
         *   DAT_0057c998++;                          // master tick
         *   ... 1000/0x82/10-tick scheduled events ...
         *   FUN_004472ce(DAT_0057c998);              // advance sim
         *   if (SP) FUN_00404a46();                  // SP-only step
         *   if (g_engine_state == 10) break;         // saving
         *   if (g_engine_state == 0xb) return;       // loading
         *   ... screen-state-specific extra updates ...
         *
         * Until any of that lands, sub-step body is a no-op.  But
         * the loop is in place so when the sim does land, only the
         * body needs to fill in. */
        (void)i;
    }

    /* TODO(render): render-phase tail.  Retail dispatches ~30
     * subsystem renders here based on g_current_screen, then the
     * FPS counter / map redraw / finalisers (gated on
     * g_savepump_request == 0).  Until those land, the rate-gated
     * present is the only thing that paints. */

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
