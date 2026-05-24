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

/* ─── 16-frame animation cycle mirror (FUN_00451f1a state) ─────────
 *
 * Single counter, bumped mod 16 each time the 80 ms phase tick fires.
 * Consumed by retail's sprite-strip renderer (FUN_004071a0) as a
 * frame index into a 16-contiguous-frame PL8 strip.  No consumer in
 * our drop-in yet (the sprite renderer is unported), so the counter
 * spins but nothing reads it. */
static int g_anim_cycle_counter;               /* DAT_0053e9a4 */

/* ─── Mouse-input resolver mirrors (FUN_004b191e state) ────────────
 *
 * The wrapper engine_tick_input_step bundles all this into the input
 * struct, calls engine_compute_input_state, writes outputs back.
 *
 * g_mouse_x / g_mouse_y / g_mouse_buttons / g_mouse_dblclk live in
 * main.c (owned by the WndProc that writes them); externed below.
 * g_client_screen too — same reason.
 *
 * Everything else lands here as file-static because engine_input is
 * the sole reader (and writer) outside of save/load.  The names
 * mirror retail's role rather than the raw DAT_* number, for
 * grep-readability across the wrapper + pure function. */
extern int16_t g_mouse_x;            /* retail DAT_004eabbe */
extern int16_t g_mouse_y;            /* retail DAT_004eabc0 */
extern uint8_t g_mouse_buttons;      /* retail DAT_004eabc2 */
extern uint8_t g_mouse_dblclk;       /* retail DAT_004eada1 */

/* Client RECT lives in main.c.  Don't pull windows.h in just for
 * the type — forward-declare the struct and extern by it.  Field
 * layout is fixed by the Win32 ABI: { LONG left, top, right, bottom }. */
struct tagRECT;
extern struct tagRECT g_client_screen;

static int g_input_raw_cursor_x;            /* DAT_004e6594 */
static int g_input_raw_cursor_y;            /* DAT_004e6598 */
static int g_input_prev_game_x_snap;        /* DAT_004ea198 */
static int g_input_prev_game_y_snap;        /* DAT_004ea190 */
static int g_input_prev_lmb_snap;           /* DAT_004eabbc */
static int g_input_prev_rmb_snap;           /* DAT_004eabc3 */
static int g_input_game_x;                  /* DAT_004e65fc */
static int g_input_game_y;                  /* DAT_004e65f4 */
static int g_input_lmb;                     /* DAT_004e65cc */
static int g_input_rmb;                     /* DAT_004e8780 */
static int g_input_mouse_event;             /* DAT_004ea4b0 */
static int g_input_button_changed;          /* _DAT_004eb264 */
static int g_input_lmb_newly_pressed;       /* DAT_004eafb4 */
static int g_input_lmb_newly_released;      /* DAT_004e65d8 */
static int g_input_rmb_newly_pressed;       /* DAT_004eabe0 */
static int g_input_rmb_newly_released;      /* DAT_004e6900 */
static int g_input_lmb_click_fired;         /* DAT_004eabf0 */
static int g_input_rmb_click_fired;         /* DAT_004ea51c */
static int g_input_lmb_dblclk;              /* DAT_004eabc5 */
static int g_input_rmb_dblclk;              /* DAT_004ea4b4 */
static int g_input_lmb_timer_armed;         /* DAT_004e65e8 */
static int g_input_rmb_timer_armed;         /* DAT_004e65c4 */
static uint32_t g_input_click_timer_ms;     /* _DAT_004e59f8 */
static uint32_t g_input_motion_last_ms;     /* _DAT_004e59f4 */
static uint32_t g_input_motion_idle_ms;     /* _DAT_004eabfc */
static int g_input_last_click_x;            /* DAT_004eac04 */
static int g_input_last_click_y;            /* DAT_004eac08 */
/* Viewport game-coord extents.  retail DAT_004eb274 / DAT_004eac4c.
 * Mirror the values the UI subsystem (unported) writes — 0 today,
 * which makes percent_ratio return 0 and pins game_x/y to 0.  When
 * the UI lands, those globals get producers and these mirrors take
 * the corresponding writebacks. */
static int g_input_viewport_game_w;         /* DAT_004eb274 */
static int g_input_viewport_game_h;         /* DAT_004eac4c */
/* Cache-enabled mode flag.  retail DAT_0057cac4.  Writer not yet
 * ported (lives in the cursor-capture path of the UI subsystem);
 * defaults to 0 = use WM_MOUSEMOVE-cached client-space coords. */
static int g_input_cursor_cache_enabled;    /* DAT_0057cac4 */
/* Click-eligibility latch.  retail DAT_004e65f0.  Set elsewhere
 * (on press over an interactive UI region — writer unported);
 * cleared by engine_compute_input_state on release-into-click. */
static int g_input_click_eligible;          /* DAT_004e65f0 */

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

/* Win32 wrapper around engine_compute_anim_cycle — bundles the
 * 80 ms phase tick + counter mirror into the input struct, runs
 * the pure function, writes the new counter back.  Must run AFTER
 * engine_tick_phase_step in the pre-tick block since it consumes
 * g_phase_tick_80ms (which the phase step produces).  Matches
 * retail's call order: FUN_004bbc80 (phase) then ... then
 * FUN_00451f1a (anim) at the tail of the pre-tick block. */
static void engine_tick_anim_cycle_step(void)
{
    engine_anim_cycle_in_t  in  = {0};
    engine_anim_cycle_out_t out = {0};

    in.counter         = g_anim_cycle_counter;
    in.tick_80ms_fired = g_phase_tick_80ms;

    engine_compute_anim_cycle(&in, &out);

    g_anim_cycle_counter = out.new_counter;
}

/* Win32 wrapper around engine_compute_input_state — reads
 * GetCursorPos + timeGetTime + the live globals, runs the pure
 * function, writes outputs back into the file-static mirrors, then
 * clears g_mouse_dblclk (matches retail's `DAT_004eada1 = 0` at the
 * end of FUN_004b191e).  Runs once per engine_tick iteration, at the
 * head of the render phase (right after the sim sub-step loop). */
static void engine_tick_input_step(void)
{
    POINT cursor;
    GetCursorPos(&cursor);

    /* g_client_screen is a RECT — { LONG left, top, right, bottom }.
     * Reach in through the forward-declared struct with a local cast
     * that knows the layout, so the rest of the file doesn't need
     * RECT. */
    const LONG *rect = (const LONG *)&g_client_screen;

    engine_input_in_t  in  = {0};
    engine_input_out_t out = {0};

    in.cursor_screen_x      = cursor.x;
    in.cursor_screen_y      = cursor.y;
    in.now_ms               = timeGetTime();
    in.cursor_cache_enabled = g_input_cursor_cache_enabled;
    in.pending_dblclk       = g_mouse_dblclk;
    in.button_state         = g_mouse_buttons;
    in.viewport_x0          = rect[0];   /* left */
    in.viewport_y0          = rect[1];   /* top */
    in.viewport_x1          = rect[2];   /* right */
    in.viewport_y1          = rect[3];   /* bottom */
    in.viewport_game_w      = g_input_viewport_game_w;
    in.viewport_game_h      = g_input_viewport_game_h;
    in.cached_cursor_x      = g_mouse_x;
    in.cached_cursor_y      = g_mouse_y;
    in.prev_game_x          = g_input_game_x;
    in.prev_game_y          = g_input_game_y;
    in.prev_lmb             = g_input_lmb;
    in.prev_rmb             = g_input_rmb;
    in.lmb_timer_armed      = g_input_lmb_timer_armed;
    in.rmb_timer_armed      = g_input_rmb_timer_armed;
    in.click_timer_ms       = g_input_click_timer_ms;
    in.motion_last_ms       = g_input_motion_last_ms;
    in.click_eligible       = g_input_click_eligible;
    in.prev_last_click_x    = g_input_last_click_x;
    in.prev_last_click_y    = g_input_last_click_y;

    engine_compute_input_state(&in, &out);

    g_input_raw_cursor_x       = out.raw_cursor_x;
    g_input_raw_cursor_y       = out.raw_cursor_y;
    g_mouse_x                  = out.new_cached_cursor_x;
    g_mouse_y                  = out.new_cached_cursor_y;
    g_input_prev_game_x_snap   = out.prev_game_x_snapshot;
    g_input_prev_game_y_snap   = out.prev_game_y_snapshot;
    g_input_prev_lmb_snap      = out.prev_lmb_snapshot;
    g_input_prev_rmb_snap      = out.prev_rmb_snapshot;
    g_input_game_x             = out.game_x;
    g_input_game_y             = out.game_y;
    g_input_mouse_event        = out.mouse_event;
    g_input_button_changed     = out.button_changed;
    g_input_lmb_newly_pressed  = out.lmb_newly_pressed;
    g_input_lmb_newly_released = out.lmb_newly_released;
    g_input_rmb_newly_pressed  = out.rmb_newly_pressed;
    g_input_rmb_newly_released = out.rmb_newly_released;
    g_input_lmb_click_fired    = out.lmb_click_fired;
    g_input_rmb_click_fired    = out.rmb_click_fired;
    g_input_lmb_dblclk         = out.lmb_dblclk;
    g_input_rmb_dblclk         = out.rmb_dblclk;
    g_input_lmb                = out.new_lmb;
    g_input_rmb                = out.new_rmb;
    g_input_lmb_timer_armed    = out.new_lmb_timer_armed;
    g_input_rmb_timer_armed    = out.new_rmb_timer_armed;
    g_input_click_timer_ms     = out.new_click_timer_ms;
    g_input_motion_last_ms     = out.new_motion_last_ms;
    g_input_motion_idle_ms     = out.motion_idle_ms;
    g_input_last_click_x       = out.last_click_x;
    g_input_last_click_y       = out.last_click_y;
    g_input_click_eligible     = out.new_click_eligible;

    /* Consume the pending dblclk bits — retail's final statement
     * `DAT_004eada1 = 0` (unconditional, regardless of whether dblclk
     * fired this frame). */
    g_mouse_dblclk = 0;
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
     * FUN_00482111, FUN_004782c5, FUN_00451f1a (← ported).  Order
     * between independent state updates doesn't matter for
     * behavior, but we keep them in retail order so future bisection
     * against retail traces stays apples-to-apples.  Note:
     * anim_cycle_step MUST run after phase_step because it consumes
     * g_phase_tick_80ms — retail has the same data dependency. */
    engine_tick_delta_step();
    engine_tick_phase_step();
    engine_tick_anim_cycle_step();
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

    /* Render-phase head — retail calls (in order):
     *   FUN_0049a3e6  surveyed: gated on DAT_004eac50 == 0x24, which
     *                 is never written anywhere in retail.  Dead
     *                 branch.  Not ported.  See findings doc.
     *   FUN_004b1de0  surveyed: empty body (`return;`).  Not ported —
     *                 same disposition as FUN_004b0aaa from pre-tick.
     *   FUN_004b191e  ported.  Mouse-input state resolver — runs
     *                 GetCursorPos, edge-detects the WndProc-mirrored
     *                 button bits, fires 300 ms delayed-single-click
     *                 events.  All outputs land in file-static
     *                 mirrors with no downstream consumer in the
     *                 drop-in yet (the UI dispatcher reads them in
     *                 retail). */
    engine_tick_input_step();

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
