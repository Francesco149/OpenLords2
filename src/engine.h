#ifndef OPENLORDS2_ENGINE_H
#define OPENLORDS2_ENGINE_H

#include <stdint.h>

/*
 * Per-tick driver + its two outer wrappers.  Ported from retail's
 * main-loop chain:
 *
 *     main loop body
 *         ↓
 *     engine_tick_guard()        ← FUN_0040e8bb @ 0x40e8bb
 *         ↓
 *     engine_tick_dispatch()     ← FUN_0040e890 @ 0x40e890
 *         ↓
 *     engine_tick()              ← FUN_004b99c0 @ 0x4b99c0 (scaffold only)
 *
 * See docs/findings/main-loop-tick.md for the full retail
 * decompile and engine-state-machine cross-reference.
 *
 * Today engine_tick() is just a scaffold — wall-clock frame counter
 * + the variable-rate sim sub-step loop (computes 0 sub-steps until
 * the engine reaches ingame state) + the rate-gated present call
 * (gfx_tick_present).  Each of the ~40 subsystem calls inside
 * retail's FUN_004b99c0 lands in its own slice as the corresponding
 * subsystem (sim, music, input, UI…) gets ported.
 */

/* Engine state machine.  Mirror of retail DAT_0052af94.
 *
 *   0     menus / pre-game
 *   2     transitioning into ingame (set by save-pump tail)
 *   3     ingame, sim ticking
 *   10    save in progress    \
 *   0xb   load in progress    } > 8 → engine_tick_dispatch latches
 *   > 8   any "fast-skip render" state — gates the FPS counter / map
 *         redraw / finalisers out of engine_tick's render tail.
 */
extern int g_engine_state;

/* Deferred-shutdown latch.  Mirror of retail DAT_005aeb68.
 *
 *   0   normal
 *   1   shutdown requested (e.g. VK_ESCAPE outside ingame).
 *       engine_tick_guard skips the dispatch when this is set;
 *       retail's main-loop tail then PostMessages WM_CLOSE.
 *   3   WM_CLOSE received, drain sim one frame then escalate to 1.
 *   4   sim asked for self-shutdown.  Consumed inside engine_tick's
 *       render tail (subsystem-teardown branch).
 *
 * Today the loop body doesn't yet PostMessage WM_CLOSE on the 1
 * transition — the WndProc's WM_CLOSE/VK_ESCAPE handlers still call
 * DestroyWindow inline as the OS-side quit path.  Once the sim that
 * can ack the deferred shutdown is ported, that inline call goes
 * away and this latch drives the dance.
 */
extern int g_shutdown_request;

/* Save/load pump request.  Mirror of retail DAT_005c9274.  Set by
 * engine_tick_dispatch when g_engine_state > 8; consumed by
 * engine_tick_guard which dispatches the SP or MP save-pump.  Both
 * pump paths are stubs today (FUN_004978ad / FUN_0049792e /
 * FUN_00497a34 are the level-load chain — many KB across most
 * subsystems).  No behavioural difference until g_engine_state can
 * actually transition above 8, which today it can't. */
extern int g_savepump_request;

/* Per-tick driver — scaffold of retail FUN_004b99c0.  Public so
 * engine_tick_dispatch can call it without going through the guard. */
void engine_tick(void);

/* Render-fast-skip dispatcher — port of retail FUN_0040e890.
 *   1. call engine_tick
 *   2. if g_engine_state > 8, latch g_savepump_request = 1
 */
void engine_tick_dispatch(void);

/* Surface-lost guard + save-pump dispatcher + deferred-shutdown
 * escalator.  Port of retail FUN_0040e8bb.  Returns 1 if engine_tick
 * was dispatched, 0 if skipped (surface still lost after a Restore
 * attempt, or shutdown latched). */
int engine_tick_guard(void);

/* ─── sim sub-step rate controller (port of FUN_004bb978) ──────────
 *
 * Decides how many sim sub-steps to run on this Win32-pump iteration.
 * Returns 0, 1, or 2.  Pure function: all inputs explicit, all state
 * mutations returned in the output struct.  The Win32 wrapper inside
 * engine.c bundles the live globals into the input struct, calls
 * this, then writes the deltas back.
 *
 * Today every input except `now_ms` is a stub-fed constant (the
 * scenario hint comes from FUN_0043de70 which gates on
 * `g_engine_state == 3` — our engine state mirror is hardcoded to 0)
 * so the function returns 0 sub-steps unconditionally.  The structural
 * seam is in place for when subsystems land that can drive ingame
 * state, at which point this function starts producing the real
 * variable-rate cadence retail uses.
 */

/* Inputs — one per global retail FUN_004bb978 reads.  The
 * cross-references to retail DAT_* addresses are in engine.c next
 * to the implementation so the header stays slim. */
typedef struct {
    uint32_t now_ms;             /* timeGetTime() at call site */
    uint32_t last_step_ms;       /* last accepted sim-step timestamp */
    uint32_t autosave_start_ms;  /* autosave-window start (0 = none) */
    int      run_gate;           /* 0 = sim halted, else may step */
    int      scenario_hint;      /* signed: <0 fast-skip, 0 freeze, >0 ms-per-step */
    int      mp_active;          /* multiplayer-session flag */
    uint32_t sim_tick_counter;   /* master sim tick (fast-skip threshold) */
    int      game_speed;         /* 0 = slow … 100 = fast */
    int      pause_lock;         /* non-zero = paused */
    int      screen_freeze;      /* per-screen sim-freeze flag (0/1/2) */
    int      extended_pause;     /* extra pause condition (e.g. dialog) */
    int      mp_skip_flag;       /* per-MP-player skip flag */
    int      phase;              /* phase counter, 0..0x9f */
    const uint8_t *rate_lut;     /* retail DAT_004e3260, ≥(clamped_hint/10+1)*0xa0 bytes */
} engine_rate_in_t;

/* Outputs — caller writes these back into the live globals.  No
 * mutation happens inside the pure function. */
typedef struct {
    int      sub_steps;           /* return value: 0, 1, or 2 */
    uint32_t new_last_step_ms;    /* writeback to last_step_ms global */
    uint32_t new_autosave_start_ms; /* writeback (only cleared, never set) */
    int      new_phase;           /* writeback (always advances by 1 mod 0xa0
                                   *           when the function reaches the
                                   *           LUT-lookup branch) */
    int      derived_hint;        /* writeback (clamped local_14 — currently
                                   *           write-only in retail too) */
    int      notify_autosave_done;/* 1 if the MP "autosave done" notification
                                   *  needs to fire (retail FUN_0043eda0(0xf, 0)) */
} engine_rate_out_t;

void engine_compute_sim_sub_steps(const engine_rate_in_t *in,
                                  engine_rate_out_t *out);

/* ─── animation phase ticks (port of FUN_004bbc80) ─────────────────
 *
 * Maintains 9 "did event X fire this call" flags driven by
 * `timeGetTime`: a base 20 ms tick + 8 mod-N rollover flags gated
 * off it.  Downstream sprite/UI updaters consume the flags once per
 * frame to drive animation cadence.
 *
 * Pure function — every input explicit, every state mutation in
 * out.  See engine_phase.c for the per-flag commentary and retail
 * cross-references.
 */
typedef struct {
    uint32_t now_ms;             /* timeGetTime() at call site */
    uint32_t last_base_tick_ms;  /* timestamp of last accepted base tick */
    uint32_t base_tick_counter;  /* counts 20 ms ticks 0..3 toward the
                                  * 80 ms rollover */
    uint32_t counter_2;          /* 80 ms tick × 2  → 160 ms */
    uint32_t counter_4;          /* 80 ms tick × 4  → 320 ms */
    uint32_t counter_8;          /* 80 ms tick × 8  → 640 ms */
    uint32_t counter_13;         /* 80 ms tick × 13 → 1040 ms */
    uint32_t counter_16;         /* 80 ms tick × 16 → 1280 ms */
    uint32_t counter_24;         /* 80 ms tick × 24 → 1920 ms */
    uint32_t counter_32;         /* 80 ms tick × 32 → 2560 ms */
} engine_phase_in_t;

typedef struct {
    /* Did-fire flags (0 or 1).  Eight are non-sticky in retail (the
     * wrapper clears each on every call before reading the new
     * value); the 1040 ms flag is sticky (consumers clear it
     * themselves). */
    int tick_20ms;
    int tick_80ms;
    int tick_160ms;
    int tick_320ms;
    int tick_640ms;
    int tick_1040ms;
    int tick_1280ms;
    int tick_1920ms;
    int tick_2560ms;
    /* Writebacks. */
    uint32_t new_last_base_tick_ms;
    uint32_t new_base_tick_counter;
    uint32_t new_counter_2;
    uint32_t new_counter_4;
    uint32_t new_counter_8;
    uint32_t new_counter_13;
    uint32_t new_counter_16;
    uint32_t new_counter_24;
    uint32_t new_counter_32;
} engine_phase_out_t;

void engine_compute_phase_ticks(const engine_phase_in_t *in,
                                engine_phase_out_t *out);

/* ─── frame delta (port of FUN_004b20ed) ───────────────────────────
 *
 * Wall-clock delta-ms generator with a 30 ms low-pass: deltas below
 * 0x1e (including negatives from clock wrap or future-seeded
 * baselines — the gate is signed) report as 0 and hold the
 * baseline; otherwise advance the baseline and report the delta.
 * Downstream sprite-animation updaters consume the result.
 *
 * Pure function — every input explicit, every state mutation in
 * out.  See engine_delta.c for the per-field commentary and retail
 * cross-references.
 */
typedef struct {
    uint32_t now_ms;          /* timeGetTime() at call site */
    uint32_t last_frame_ms;   /* timestamp of last accepted frame */
} engine_delta_in_t;

typedef struct {
    int      delta_ms;            /* 0 if suppressed, else ms since last */
    uint32_t new_last_frame_ms;   /* baseline writeback (advanced or held) */
} engine_delta_out_t;

void engine_compute_frame_delta(const engine_delta_in_t *in,
                                engine_delta_out_t *out);

/* ─── 16-frame animation cycle (port of FUN_00451f1a) ──────────────
 *
 * Bumps a mod-16 counter once per 80 ms phase tick.  Downstream
 * sprite renderers use the counter as a frame index into a 16-frame
 * strip (full cycle is 16 × 80 ms = 1280 ms).  See engine_anim_cycle.c
 * for the per-field commentary and the unreachable-lower-clamp
 * engine quirk.
 */
typedef struct {
    int counter;          /* current value (always 0..15 in normal flow) */
    int tick_80ms_fired;  /* gate — consumes g_phase_tick_80ms output */
} engine_anim_cycle_in_t;

typedef struct {
    int new_counter;      /* counter writeback (0..15) */
} engine_anim_cycle_out_t;

void engine_compute_anim_cycle(const engine_anim_cycle_in_t *in,
                               engine_anim_cycle_out_t *out);

/* ─── mouse-input resolver (port of FUN_004b191e) ──────────────────
 *
 * Per-iteration mouse-state update.  Inputs: a screen-space cursor
 * position (retail calls GetCursorPos), wall-clock timestamp, the
 * WndProc-mirrored button + dblclk bitmasks, viewport bounds, and
 * the prior frame's resolved input state.  Outputs: motion event
 * flag, button-edge flags, 300 ms delayed-single-click events,
 * double-click events, cursor position in game-coord space, and
 * a small set of state-writeback fields the wrapper feeds back
 * into the file-static mirrors.
 *
 * Pure function — every input explicit, every state mutation in out.
 * See engine_input.c for the per-field commentary, helpers, and the
 * shared-300 ms-timer quirk between LMB and RMB.
 */
typedef struct {
    /* Win32-provided inputs (wrapper fills from GetCursorPos +
     * timeGetTime). */
    int      cursor_screen_x;
    int      cursor_screen_y;
    uint32_t now_ms;

    /* Mode flag.  retail DAT_0057cac4 — gates the per-frame cursor-
     * cache overwrite (cache_enabled==0 means the WM_MOUSEMOVE-cached
     * client-space coords carry through; ==1 means the screen-space
     * GetCursorPos result is stored instead). */
    int      cursor_cache_enabled;

    /* WndProc-mirrored event bitmasks.  bit 0 = LMB, bit 1 = RMB. */
    uint8_t  pending_dblclk;     /* g_mouse_dblclk (DAT_004eada1) */
    uint8_t  button_state;       /* g_mouse_buttons (DAT_004eabc2) */

    /* Viewport bounds — screen-space rectangle the cursor is
     * mapped against.  retail DAT_004ea180..18c (g_client_screen). */
    int      viewport_x0;
    int      viewport_y0;
    int      viewport_x1;
    int      viewport_y1;
    /* Viewport extents in game-coord space.  retail DAT_004eb274 / DAT_004eac4c. */
    int      viewport_game_w;
    int      viewport_game_h;

    /* Prior cached cursor (WM_MOUSEMOVE-tracked).  retail DAT_004eabbe/c0. */
    int16_t  cached_cursor_x;
    int16_t  cached_cursor_y;
    /* Prior game-coord cursor.  retail DAT_004e65fc / DAT_004e65f4. */
    int      prev_game_x;
    int      prev_game_y;
    /* Prior resolved LMB/RMB state (0 or 1).  retail DAT_004e65cc / DAT_004e8780. */
    int      prev_lmb;
    int      prev_rmb;

    /* Pending-click timer state.  retail DAT_004e65e8 / DAT_004e65c4. */
    int      lmb_timer_armed;
    int      rmb_timer_armed;
    /* Shared click-timer baseline.  retail _DAT_004e59f8 — both LMB
     * and RMB releases reset this; both expiries read it.  Quirk
     * preserved (see engine_input.c). */
    uint32_t click_timer_ms;
    /* Last-motion timestamp.  retail _DAT_004e59f4. */
    uint32_t motion_last_ms;
    /* Click-eligibility latch.  retail DAT_004e65f0.  Cleared on
     * release-into-click; presumably set elsewhere on press over
     * an interactive UI region (writer not yet ported).  Today our
     * wrapper hardcodes it to 0 — no UI yet — so the click-arming
     * branches stay inert. */
    int      click_eligible;
    /* Prior last-click coords.  retail DAT_004eac04 / DAT_004eac08.
     * Carried through unchanged on non-click frames; overwritten in
     * the click-armed release branches. */
    int      prev_last_click_x;
    int      prev_last_click_y;
} engine_input_in_t;

typedef struct {
    /* Per-frame raw cursor (always written). */
    int      raw_cursor_x;            /* DAT_004e6594 */
    int      raw_cursor_y;            /* DAT_004e6598 */

    /* Cached cursor — either freshly written from cursor_screen_x/y
     * (cursor_cache_enabled) or carried through unchanged. */
    int16_t  new_cached_cursor_x;     /* DAT_004eabbe */
    int16_t  new_cached_cursor_y;     /* DAT_004eabc0 */

    /* Snapshots of prior state for downstream consumers.  Retail
     * writes these unconditionally before computing the new state. */
    int      prev_game_x_snapshot;    /* DAT_004ea198 */
    int      prev_game_y_snapshot;    /* DAT_004ea190 */
    int      prev_lmb_snapshot;       /* DAT_004eabbc */
    int      prev_rmb_snapshot;       /* DAT_004eabc3 */

    /* Resolved game-coord cursor for this frame. */
    int      game_x;                  /* DAT_004e65fc */
    int      game_y;                  /* DAT_004e65f4 */

    /* Per-frame event flags (all 0 unless the corresponding edge
     * fired this frame). */
    int      mouse_event;             /* DAT_004ea4b0 — any mouse activity */
    int      button_changed;          /* _DAT_004eb264 — LMB or RMB edge */
    int      lmb_newly_pressed;       /* DAT_004eafb4 */
    int      lmb_newly_released;      /* DAT_004e65d8 */
    int      rmb_newly_pressed;       /* DAT_004eabe0 */
    int      rmb_newly_released;      /* DAT_004e6900 */
    int      lmb_click_fired;         /* DAT_004eabf0 — 300 ms expiry */
    int      rmb_click_fired;         /* DAT_004ea51c — 300 ms expiry */
    int      lmb_dblclk;              /* DAT_004eabc5 — from WndProc dblclk bit */
    int      rmb_dblclk;              /* DAT_004ea4b4 */

    /* Resolved LMB/RMB state (0 or 1) — cached for next frame. */
    int      new_lmb;                 /* DAT_004e65cc */
    int      new_rmb;                 /* DAT_004e8780 */

    /* Pending-click timer writebacks. */
    int      new_lmb_timer_armed;     /* DAT_004e65e8 */
    int      new_rmb_timer_armed;     /* DAT_004e65c4 */
    uint32_t new_click_timer_ms;      /* _DAT_004e59f8 */
    uint32_t new_motion_last_ms;      /* _DAT_004e59f4 */
    /* Idle ms since the last motion (0 on motion frames). */
    uint32_t motion_idle_ms;          /* _DAT_004eabfc */

    /* Last-click coordinates (in game-coord space).  Updated on
     * release-into-click; otherwise carried through. */
    int      last_click_x;            /* DAT_004eac04 */
    int      last_click_y;            /* DAT_004eac08 */

    /* Click-eligibility writeback (cleared on release-into-click). */
    int      new_click_eligible;      /* DAT_004e65f0 */
} engine_input_out_t;

void engine_compute_input_state(const engine_input_in_t *in,
                                engine_input_out_t *out);

/* ─── FPS counter (port of FUN_004b27bf) ───────────────────────────
 *
 * Rolling 1-second FPS measurement.  Bumps frames-this-second on every
 * call; once the window elapses (> 1000 ms), publishes the current FPS
 * and rolls the cumulative-FPS / cumulative-seconds counters used by
 * retail's session-diag (FUN_004b2636 — reports average FPS).
 *
 * Also returns ms since the previous frame (delta_ms) — retail's
 * caller discards this, but the value is computed regardless because
 * the baseline (`last_render_ms`) is unconditionally advanced on every
 * call.
 *
 * Pure function — every input explicit, every state mutation in out.
 * See engine_fps.c for the per-field commentary and the two preserved
 * quirks (signed rollover gate; two-timeGetTime-call shape).
 */
typedef struct {
    uint32_t now_ms;                  /* timeGetTime() at call site */
    uint32_t rollover_now_ms;         /* second timeGetTime() — used only
                                       * inside the rollover branch as the
                                       * new window-start basis (matches
                                       * retail's 2-call shape) */
    uint32_t last_render_ms;          /* _DAT_004e59e0 */
    uint32_t second_window_start_ms;  /* DAT_004eaba0 */
    uint32_t frames_this_second;      /* DAT_004eada4 */
    uint32_t current_fps;             /* DAT_004ea174 */
    uint32_t cumulative_fps;          /* DAT_004ea0f4 */
    uint32_t cumulative_seconds;      /* DAT_004ea83c */
} engine_fps_in_t;

typedef struct {
    uint32_t delta_ms;                    /* return value (ms since previous
                                           * frame) — retail discards it */
    uint32_t new_last_render_ms;
    uint32_t new_second_window_start_ms;
    uint32_t new_frames_this_second;
    uint32_t new_current_fps;
    uint32_t new_cumulative_fps;
    uint32_t new_cumulative_seconds;
    int      window_rolled_over;          /* 1 iff the 1-second window
                                           * rolled over this call; helps
                                           * tests + lets the wrapper note
                                           * which writebacks were the
                                           * "live" ones */
} engine_fps_out_t;

void engine_compute_fps(const engine_fps_in_t *in, engine_fps_out_t *out);

/* Read-only accessors for the FPS counter + frame counter mirrors.
 * Used by main.c at shutdown to print a smoke summary — the values
 * confirm that the per-tick driver is actually ticking and the FPS
 * counter is alive.  No setters; the wrappers in engine.c own writes. */
uint32_t engine_get_current_fps(void);
uint32_t engine_get_cumulative_fps(void);
uint32_t engine_get_cumulative_seconds(void);
uint32_t engine_get_frame_counter(void);

#endif /* OPENLORDS2_ENGINE_H */
