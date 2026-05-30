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

#include "call_trace.h"
#include "engine_camera.h"
#include "engine_overlay_render.h"
#include "engine_screen_dispatch.h"
#include "engine_title_render.h"
#include "engine_menu_input.h"
#include "engine_turn.h"
#include "gfx.h"
#include "save.h"                 /* g_player_strategic_352 */
#include "save_schema_globals.h"  /* s_slot_00554004 */

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
 * today, so always 0.  Shared (declared extern in engine.h) so the
 * strategic-layer modules that branch on it — e.g. engine_npc_schedule
 * — read the single canonical flag rather than a private mirror. */
int g_sim_mp_active;
/* MP session sub-variant (retail DAT_00553248) and turn-save rotation
 * hold counter (retail DAT_00553260).  Both runtime-only, 0 in SP.
 * See engine.h for semantics; consumed by save_rotate_and_write. */
int g_sim_mp_variant;
int g_sim_save_rotate_hold;
/* Game-speed slider (0..100, retail _DAT_0053f230).  Default 100
 * (max speed) until the UI slider lands. */
static int g_sim_game_speed = 100;
/* Pause lock — non-zero freezes the sim.  retail DAT_0053f238.
 * Today no UI sets it, so always 0. */
static int g_sim_pause_lock;
/* Current screen ID.  retail DAT_004eac50 — single byte indexing the
 * per-screen tables.  Today nothing transitions screens, so 0.
 *
 * NOTE: gfx_text.c keeps an independent mirror of this same retail byte
 * (its `g_language_tag`, used to render title-screen text in a taller
 * box when the value is 0x1f/0x1c).  Both are 0 today since no producer
 * transitions screens, so they cannot yet desync — but when the
 * boot→title transition (0x42b780, sets 0x1f) lands it must update
 * BOTH.  See docs/findings/engine-quirks.md. */
static uint8_t g_sim_current_screen;

/* Demand-redraw cookie.  retail DAT_0057d340 — set to 1 or 2 by 50+
 * input/state handlers (incl. the menu state tick and WndProc on
 * WM_MOVE/WM_ACTIVATEAPP) whenever the screen must repaint; consumed and
 * cleared by the render-phase seam below (the menu render seam — see
 * docs/plans/main-menu-render-chips.md).  The forwarded value is a
 * redraw-mode hint that the 0x1f title arm ignores.  Owned here (the
 * render driver is the consumer); WndProc sets it via
 * engine_set_demand_redraw. */
static int g_demand_redraw;
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

/* ─── Sim screen sub-state mirror ───────────────────────────────────
 *
 * Retail DAT_0057c8fc.  0 = normal ingame, 2 = alt UI (combat
 * resolution / events screen).  Gates several sub-step body branches.
 * No producer in our port today — stays 0.  Lands when the screen-
 * state machine ports. */
static uint8_t g_sim_screen_substate;          /* DAT_0057c8fc */

/* "This player is human/active" gate.  Retail (&DAT_0057bf04)[player_index *
 * 0x160].  Indexed by current-player mirror DAT_0057c8cc.  The whole
 * scheduled-events block in the sub-step body is gated on this byte
 * being non-zero.  Today no producer — defaults to 0 → scheduled-events
 * block is skipped. */
static uint8_t g_sim_player_human_active;      /* DAT_0057bf04[curp] */

/* "Turn end pending" latch.  Retail DAT_0056d6a4.  Set by FUN_0049b8b0
 * (turn-timeout subsystem); consumed by the sub-step body's SP-only
 * branch to break out of the sub-step loop (forces local_c = 0).  No
 * producer in our port today. */
static int g_sim_turn_end_pending;             /* DAT_0056d6a4 */

/* The "turn progress" flag (DAT_00553fc8) and the turn-sync baseline
 * (_DAT_0056d658) now live in engine_turn.c (the turn module) so the
 * new-game scalar seeder can reach them; engine.c still owns the live
 * read/write, marshalling them through the engine_turn snapshot each
 * tick — see engine_turn.h.  The other end-of-turn mirrors below stay
 * engine.c-local (no cross-TU reader). */

/* ─── End-of-turn readiness mirrors (FUN_0049b762 state) ───────────
 *
 * Runtime-only globals consumed/written by engine_sim_turn_resolved.
 * None have a producer in the drop-in yet (the turn driver 0x49a010
 * and the timeout subsystem 0x49b8b0 are unported), so they sit at 0
 * and the SP path (mp_active == 0) is the only one exercised live.
 * NOT save-schema scalars — none are persisted. */
static int      g_sim_active_player_count;     /* DAT_0053f068 — # active
                                                *   players; retail's writer
                                                *   FUN_0043e330 computes this
                                                *   into the rate-controller
                                                *   hint snapshot, not a global,
                                                *   so this mirror stays 0 */
static int      g_sim_turn_timeout_reached;    /* DAT_00553018 — set by the
                                                *   2500 ms turn timer in the
                                                *   turn driver (retail 0x49a010) */
static int      g_sim_saved_turn_timeout;      /* DAT_0055ce88 — snapshot of the
                                                *   above taken when the sync
                                                *   handshake arms */
/* g_sim_turn_sync_baseline_ms (_DAT_0056d658) relocated to engine_turn.c. */
static int      g_sim_turn_ready_flags[10];    /* DAT_00515d60[10] — per-player
                                                *   turn-ack flags, cleared when
                                                *   the handshake arms */

/* ─── Double-LFSR RNG mirrors (FUN_00404a46 state) ──────────────────
 *
 * 31-bit Galois LFSR state + six masked-output mirrors.  Retail seeds
 * the state in FUN_004b2258 / InitInstance (lines 83635-83636); we
 * mirror that at .bss-init time via static initialisers so the LFSR
 * is in a non-degenerate state from the first call.
 *
 * Output mirrors are write-only from our side today — no consumer in
 * the drop-in yet.  Retail's bounded-bitmask sampler (FUN_00404b99,
 * unported) is the primary reader. */
static uint32_t g_rng_state_a    = 0x54657687u;   /* DAT_0058fe28 */
static uint32_t g_rng_state_b    = 0x72641663u;   /* DAT_0058fe08 */
static uint32_t g_rng_15bit_a;                    /* DAT_005aea58 */
static uint32_t g_rng_7bit_a;                     /* DAT_0058fd7c */
static uint32_t g_rng_3bit_a;                     /* _DAT_0058fd70 */
static uint32_t g_rng_15bit_b;                    /* _DAT_005aea5c */
static uint32_t g_rng_7bit_b;                     /* DAT_0058fd60 */
static uint32_t g_rng_3bit_b;                     /* _DAT_0058fd6c */

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

/* ─── FPS counter mirrors (FUN_004b27bf state) ─────────────────────
 *
 * Rolling 1-second FPS measurement.  `g_fps_second_window_start_ms`
 * is lazy-initialised on first call to dodge a spurious first-second
 * rollover — retail's InitInstance (FUN_004b2258 @ line 83665) does
 * `DAT_004eaba0 = timeGetTime();` before the main loop spins, but our
 * drop-in doesn't have an InitInstance hook point yet, so the wrapper
 * substitutes a 0-sentinel + first-call init.
 *
 * `g_fps_current` is write-only today — retail's on-screen FPS
 * overlay (UI subsystem, unported) is the reader.  Cumulative pair is
 * read by retail's session-diag (FUN_004b2636, unported), and also by
 * our smoke-print at shutdown so the harness can verify the counter
 * is alive. */
static uint32_t g_fps_last_render_ms;          /* _DAT_004e59e0 */
static uint32_t g_fps_second_window_start_ms;  /* DAT_004eaba0 (0 → lazy init) */
static uint32_t g_fps_frames_this_second;      /* DAT_004eada4 */
static uint32_t g_fps_current;                 /* DAT_004ea174 */
static uint32_t g_fps_cumulative_fps;          /* DAT_004ea0f4 */
static uint32_t g_fps_cumulative_seconds;      /* DAT_004ea83c */

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

/* ─── Strategic-map camera mirrors (FUN_00429b1d + FUN_0047ed34 state) ─
 *
 * Two pairs of integer state for two related clamps run unconditionally
 * at the head of retail's render-phase tail (decompile lines
 * 88725-88726, gated on DAT_005c9274 == 0).  No producer in our port
 * today — both pairs stay 0 — but the clamps are harmless idempotent
 * no-ops when the values are already in range, so we wire the wrappers
 * unconditionally to match retail's call shape.
 *
 * Scroll pair (FUN_00429b1d) — strategic-map cell-scroll position +
 * the per-frame visible-row/col counts that bound it.  Bounds are
 * variable: max valid scroll is `0x40 - col_count` / `0x80 - row_count`.
 * Writers in retail: keyboard arrow handler (FUN_0042xxxx camera-pan),
 * scrolled-into-view re-centering paths.  Today no writer.
 *
 * Anchor pair (FUN_0047ed34) — strategic-map / battlefield camera
 * anchor.  Hardcoded bounds [0, 0x41] / [0, 0x42] — independent of
 * the visible-cells count.  Writers: same keyboard-arrow handler,
 * entity-centering (FUN_0042xxxx), castle-table-driven re-centering
 * (decompile line 27137).  Today no writer.
 */
static int g_cam_scroll_col;           /* DAT_005651b4 */
static int g_cam_scroll_row;           /* DAT_005651b8 */
static int g_cam_visible_cols;         /* DAT_0053e8ac (col_count this frame) */
static int g_cam_visible_rows;         /* DAT_0056d67c (row_count this frame) */
static int g_cam_anchor_x;             /* DAT_0055ce70 */
static int g_cam_anchor_y;             /* DAT_005678a0 */

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

/* Win32 wrapper around engine_turn_end_check — the end-of-turn
 * readiness predicate (port of FUN_0049b762 @ 0x49b762).  Captures
 * timeGetTime + the live turn-state globals, runs the pure check,
 * writes the readiness count + handshake state back, and zeroes the
 * per-player ready flags when the handshake arms.  Returns 1 when the
 * turn may advance.
 *
 * No caller in the drop-in yet — retail's turn driver 0x49a010
 * (982 B, unported) is the sole caller (`if (resolved) 0x49ce51()`).
 * Dead-but-tested today: the pure core is exercised by the host suite. */
int engine_sim_turn_resolved(void)
{
    engine_turn_end_in_t  in  = {0};
    engine_turn_end_out_t out = {0};

    in.now_ms           = timeGetTime();
    in.mp_active        = g_sim_mp_active;
    in.active_count     = g_sim_active_player_count;
    in.turn_progress    = g_sim_turn_progress;
    in.timeout_reached  = g_sim_turn_timeout_reached;
    in.saved_timeout    = g_sim_saved_turn_timeout;
    in.sync_baseline_ms = g_sim_turn_sync_baseline_ms;
    in.players          = g_player_strategic_352;

    engine_turn_end_check(&in, &out);

    s_slot_00554004             = (uint32_t)out.not_ready_count;
    g_sim_turn_progress         = out.new_turn_progress;
    g_sim_turn_timeout_reached  = out.new_timeout_reached;
    g_sim_saved_turn_timeout    = out.saved_timeout;
    g_sim_turn_sync_baseline_ms = out.new_sync_baseline_ms;
    if (out.clear_ready_flags) {
        for (int i = 0; i < 10; i++)
            g_sim_turn_ready_flags[i] = 0;
    }

    return out.advance;
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

/* ─── title-menu input dispatch (FUN_004ba9c8 0x1f arm) ────────────
 *
 * Bridges the resolved input state (above) into engine_menu_input.
 * Retail runs the whole UI input dispatcher (FUN_004ba9c8) from inside
 * FUN_0042ff10 every frame; in the drop-in's scaffold tick we run just
 * the title-screen (0x1f) arm — the only screen whose render half is
 * ported — right after the input resolver, so a hover/click resolved
 * this frame updates the highlight + may transition the page (the
 * demand-redraw seam below repaints when it does).
 *
 * The module's engine-side hooks (demand-redraw cookie, quit latch) are
 * bound once here so the module stays unit-testable in isolation. */
static void menu_quit_hook(void) { g_shutdown_request = 1; }  /* DAT_005aeb68 = 1 */

static void engine_tick_menu_input_step(void)
{
    if (g_sim_current_screen != 0x1f)
        return;

    static int hooks_bound;
    if (!hooks_bound) {
        engine_menu_input_bind_redraw_fn(engine_set_demand_redraw);
        engine_menu_input_bind_quit_fn(menu_quit_hook);
        hooks_bound = 1;
    }

    engine_menu_click_in_t in;
    in.cursor_x          = g_input_game_x;            /* DAT_004e65fc */
    in.cursor_y          = g_input_game_y;            /* DAT_004e65f4 */
    in.lmb_newly_pressed = g_input_lmb_newly_pressed; /* DAT_004eafb4 */
    in.lmb_newly_released= g_input_lmb_newly_released; /* DAT_004e65d8 */
    in.lmb_held          = g_input_lmb;               /* DAT_004e65cc */
    in.lmb_dblclk        = g_input_lmb_dblclk;        /* DAT_004eabc5 */
    in.tick_320ms        = g_phase_tick_320ms;        /* DAT_0057d3c8 */
    in.repeat_tick       = 1;                         /* DAT_004ea128 != 0 —
                                                       * each title frame is a
                                                       * real input frame; the
                                                       * render rate-limiter
                                                       * caps the cadence the
                                                       * scroll auto-repeat
                                                       * advances at. */

    engine_menu_title_input(&in);
}

/* Page-background reload hook — the drop-in's gfx background load takes
 * its own DirectDraw lock, so it cannot run nested inside the canvas
 * lock the full-scene composite uses.  The title render step calls this
 * hook (bound by main.c to gfx_title_background_load) BEFORE locking the
 * canvas, on every demand-redraw frame.  Default no-op so engine.c stays
 * unit-testable without gfx. */
static void title_bg_reload_stub(int page) { (void)page; }
static engine_title_bg_reload_fn g_title_bg_reload_fn = title_bg_reload_stub;

/* ─── title-screen render step (demand-redraw seam + per-frame refresh) ─
 *
 * Retail runs two distinct repaints in the render phase of FUN_004b99c0:
 *
 *   line 36:  if (DAT_0057d340) { FUN_0040f1a0(DAT_0057d340); ...=0; }
 *             — the demand-redraw seam: a FULL screen-dispatch (the title
 *               arm composites background + panel + text + buttons) only
 *               when a state handler asked for a repaint.
 *   line 99:  FUN_004ba26e();
 *             — the per-frame UI refresh, run UNCONDITIONALLY every frame.
 *               Its 0x1f arm re-renders just the current page's buttons,
 *               so the hover highlight (set by the input dispatcher with
 *               NO demand-redraw request) follows the mouse smoothly.
 *
 * Both paint into the locked back buffer.  In the drop-in the page
 * background load takes its own DirectDraw lock, so on a full redraw we
 * reload the background BEFORE taking the canvas lock, then composite the
 * rest inside one lock; the per-frame button refresh takes its own short
 * lock.  Non-title screens keep the plain (unlocked) demand-redraw seam —
 * their per-screen renderers are unported stubs that draw nothing. */
static void engine_tick_title_render_step(void)
{
    if (g_sim_current_screen != 0x1f) {
        /* Other screens: generic demand-redraw seam.  The renderers are
         * stubs (engine_screen_dispatch's other-fn hook), so no canvas
         * lock is needed yet. */
        if (g_demand_redraw != 0) {
            engine_screen_dispatch(g_sim_current_screen,
                                   engine_title_render_page(),
                                   g_demand_redraw);
            g_demand_redraw = 0;
        }
        return;
    }

    int page = engine_title_render_page();

    /* Full-scene redraw requested (page transition, window move, surface
     * restore).  Reload the page background outside the canvas lock, then
     * composite panel + text + buttons inside it. */
    int full = (g_demand_redraw != 0);
    if (full) {
        g_title_bg_reload_fn(page);
        g_demand_redraw = 0;
    }

    if (gfx_lock_canvas()) {
        if (full)
            engine_screen_dispatch(0x1f, page, 2);
        else
            engine_title_render_refresh_buttons(page);
        gfx_unlock_canvas();
    }
}

/* Wrappers around engine_camera_clamp_scroll / engine_camera_clamp_anchor.
 * Both run in the render-phase tail before the FPS counter — same
 * order as retail (decompile lines 88725-88726).  Bundle the file-
 * static mirrors into pointer args, run the pure clamps, write back.
 *
 * Today there are no producers for any of these mirrors, so each
 * call is an idempotent no-op (everything stays 0).  Wiring the
 * structural seam now so future scroll-/anchor-mutating ports
 * (keyboard arrow handler, entity-centering, castle-table re-center)
 * can land without having to also wire the tail. */
static void engine_tick_camera_scroll_clamp(void)
{
    engine_camera_clamp_scroll(&g_cam_scroll_col, &g_cam_scroll_row,
                                g_cam_visible_cols, g_cam_visible_rows);
}

static void engine_tick_camera_anchor_clamp(void)
{
    engine_camera_clamp_anchor(&g_cam_anchor_x, &g_cam_anchor_y);
}

/* Win32 wrapper around engine_compute_fps — reads timeGetTime twice
 * to match retail's 2-call shape (the second call is what retail uses
 * as the new window-start basis on rollover frames), bundles into the
 * input struct, runs the pure function, writes outputs back.  Lazy-
 * initialises the window-start on first call to skip the spurious
 * boot-time rollover.
 *
 * Runs in the render-phase tail, gated on g_savepump_request == 0 —
 * same shape as retail. */
static void engine_tick_fps_step(void)
{
    engine_fps_in_t  in  = {0};
    engine_fps_out_t out = {0};

    in.now_ms          = timeGetTime();
    in.rollover_now_ms = timeGetTime();
    in.last_render_ms  = g_fps_last_render_ms;

    /* Lazy init — retail's InitInstance sets the window-start basis
     * before the main loop ever calls this; in our drop-in we just
     * substitute the first call's timestamp.  Same effect: the first
     * rollover happens 1 second after boot, not on frame 1. */
    if (g_fps_second_window_start_ms == 0) {
        g_fps_second_window_start_ms = in.now_ms;
    }
    in.second_window_start_ms = g_fps_second_window_start_ms;
    in.frames_this_second     = g_fps_frames_this_second;
    in.current_fps            = g_fps_current;
    in.cumulative_fps         = g_fps_cumulative_fps;
    in.cumulative_seconds     = g_fps_cumulative_seconds;

    engine_compute_fps(&in, &out);

    g_fps_last_render_ms          = out.new_last_render_ms;
    g_fps_second_window_start_ms  = out.new_second_window_start_ms;
    g_fps_frames_this_second      = out.new_frames_this_second;
    g_fps_current                 = out.new_current_fps;
    g_fps_cumulative_fps          = out.new_cumulative_fps;
    g_fps_cumulative_seconds      = out.new_cumulative_seconds;
    /* delta_ms discarded — retail does the same (FUN_004b99c0's
     * caller of FUN_004b27bf ignores the return value). */
}

/* Wrapper around engine_rng_step — bundles the two state mirrors,
 * runs the pure function, writes the new state + six masked outputs
 * back.  Called from the SP-only branch of the sub-step body (matches
 * retail FUN_004b99c0 @ line 88780, gated on DAT_00553030 == 0). */
static void engine_tick_rng_step(void)
{
    engine_rng_in_t  in  = { .state_a = g_rng_state_a,
                             .state_b = g_rng_state_b };
    engine_rng_out_t out = {0};

    engine_rng_step(&in, &out);

    g_rng_state_a = out.new_state_a;
    g_rng_state_b = out.new_state_b;
    g_rng_15bit_a = out.rng_15bit_a;
    g_rng_7bit_a  = out.rng_7bit_a;
    g_rng_3bit_a  = out.rng_3bit_a;
    g_rng_15bit_b = out.rng_15bit_b;
    g_rng_7bit_b  = out.rng_7bit_b;
    g_rng_3bit_b  = out.rng_3bit_b;
}

/* Public readers used by main.c's shutdown summary so the harness can
 * verify the FPS counter is alive without poking file-static state. */
uint32_t engine_get_current_fps(void)        { return g_fps_current; }
uint32_t engine_get_cumulative_fps(void)     { return g_fps_cumulative_fps; }
uint32_t engine_get_cumulative_seconds(void) { return g_fps_cumulative_seconds; }
uint32_t engine_get_frame_counter(void)      { return g_engine_frame_counter; }

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

    /* Push the four tier-rate flags into the overlay renderer's army
     * drawer.  Retail keeps these as separate globals
     * (`DAT_0058fd08`, `DAT_0057d3c8`, `DAT_0057d3ac`, `DAT_00591510`)
     * but they ARE the 640/320/160/80-ms phase ticks above — same
     * addresses, same producer (this function).  The army drawer's
     * `pick_rate_tier` indexes by `(cur - prev)` distance bin and
     * reads one of these four; the binding contract is:
     *
     *   distance <  10  → tier 0 → 640 ms tick (slowest cadence)
     *   distance < 0x19 → tier 1 → 320 ms tick
     *   distance < 0x32 → tier 2 → 160 ms tick
     *   distance ≥ 0x32 → tier 3 →  80 ms tick (fastest cadence)
     *
     * Small distance = slow-moving army → slow sprite cycle; large
     * distance = fast-moving army → fast cycle.  Closes engine
     * quirk #74's "driver lives elsewhere" follow-up. */
    engine_overlay_render_set_animation_rate_flags(out.tick_640ms,
                                                   out.tick_320ms,
                                                   out.tick_160ms,
                                                   out.tick_80ms);
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
        /* TODO(mp): port 0x43eda0(0xf, 0) — the MP "autosave
         * complete" broadcast.  Today MP isn't wired, so the flag
         * never fires (g_sim_mp_active == 0 short-circuits the
         * notification path inside the pure function too). */
    }

    return out.sub_steps;
}

void engine_set_demand_redraw(int mode)
{
    g_demand_redraw = mode;
}

/* ─── live title-screen producer hooks ──────────────────────────────
 *
 * The boot→title state transition (retail 0x42b780) is unported, so
 * these small setters let a bring-up producer put the engine into the
 * live title-screen state the render seam expects: screen byte 0x1f, a
 * page, the viewport game-coord extents (so engine_compute_input_state
 * maps the cursor 1:1 on the 640×480 menu), and an initial demand-redraw
 * so the first frame paints the full scene. */
void engine_set_current_screen(int screen)
{
    g_sim_current_screen = (uint8_t)screen;
}

int engine_current_screen(void)
{
    return g_sim_current_screen;
}

void engine_set_viewport_game_extents(int w, int h)
{
    g_input_viewport_game_w = w;   /* retail DAT_004eb274 (0x280) */
    g_input_viewport_game_h = h;   /* retail DAT_004eac4c (0x1e0) */
}

void engine_bind_title_bg_reload_fn(engine_title_bg_reload_fn fn)
{
    g_title_bg_reload_fn = fn ? fn : title_bg_reload_stub;
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

    /* Open this frame's call-trace window (no-op unless --call-trace).
     * Uses the same per-tick counter the agent stamps on retail, so the
     * two traces align 1:1.  This probe declares engine_tick == the
     * FUN_004b99c0 per-tick driver. */
    call_trace_begin_frame(g_engine_frame_counter);
    CALL_TRACE_ENTER_STUB(0x004b99c0);   /* engine_tick — scaffold body */

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
    int load_bailout = 0;
    for (int i = 0; i < sub_steps; i++) {
        (void)i;

        /* Skeleton of retail FUN_004b99c0's sub-step body @ lines
         * 88749-88803.  In SP mode (which is the only mode our
         * drop-in can produce today) the body collapses substantially:
         * retail's two biggest callees — FUN_004472ce (1046 B "sim
         * tick") and 0x43eda0 (637 B "scheduled events
         * dispatcher") — are BOTH gated entirely on DAT_00553030 != 0
         * (MP active) and become no-ops in SP.  The only real per-
         * step work in SP is:
         *   - master tick counter bump
         *   - FUN_00404a46 (LFSR advance — ported as engine_rng_step)
         *   - FUN_0049b8b0 (turn-timeout, unported)
         *   - screen-state-specific FUN_0040490d / FUN_0049a010 /
         *     0x4650b0 (sub-state 0) or the sub-state-2 chain
         *
         * Today sub_steps is always 0 because scenario_hint == 0
         * (engine state != 3), so this body is unreachable until the
         * ingame state machine lands.  Structural seam in place. */

        /* 1. Master sim tick counter.  retail DAT_0057c998. */
        g_sim_tick_counter++;

        /* 2. Scheduled events — gated on the per-player "human/active"
         *    flag.  In SP all the 0x43eda0 calls are no-ops (gated
         *    on DAT_00553030 != 0 inside), so we only mirror the SP
         *    branch's other side effects. */
        if (g_sim_player_human_active != 0) {
            if (g_sim_screen_substate == 2) {
                /* TODO(sim): MP sub-state 2 path.
                 *   - tick % 0x82 == 0    → 0x43eda0(0xc, 0)  // MP-only, no-op in SP
                 *   - else tick % 10 == 0 → 0x43eda0(10, 0)   // MP-only, no-op in SP
                 *   - else tick % 0x82 == 0x2d → local_c = FUN_00440666()
                 *     (FUN_00440666 is 383 B, unported — manipulates
                 *      the deferred-action selection).  Today the
                 *      sub_steps loop sees local_c as the rate-step
                 *      output (constant for the iter), so this write
                 *      would change loop termination mid-iter; faithful
                 *      port requires reworking the rate-step / loop
                 *      contract. */
            } else {
                /* SP path: 0x43eda0(10, 0) is MP-only no-op.
                 * The real work here is FUN_0049b8b0() (turn-timeout
                 * subsystem — 195 B, gated on DAT_00553fc8 != 0 &&
                 * DAT_00553248 != 0; both are 0 today so the function
                 * itself is a no-op anyway) and the turn-end-pending
                 * consumer. */
                /* TODO(sim): FUN_0049b8b0 turn-timeout subsystem. */

                if (g_sim_turn_end_pending) {
                    if (g_sim_turn_progress == 0) {
                        g_sim_turn_end_pending = 0;
                    }
                    /* retail also sets local_c = 0 here — same
                     * mid-iter loop-termination shape as the sub-
                     * state-2 path above.  Deferred until FUN_00440666
                     * etc land. */
                }
            }
        }

        /* 3. Sim tick — retail FUN_004472ce(DAT_0057c998).  Entirely
         *    MP-gated (DAT_00553030 != 0), so no-op in SP.  Body is
         *    the MP "process deferred actions due at this tick" walker
         *    over a 400-element table (DAT_0050d7c0 + i*0x38), with
         *    a per-action function-table dispatch via PTR_FUN_004d5c98
         *    indexed by the action's type byte.  Lands together with
         *    the MP subsystem. */
        /* TODO(mp): FUN_004472ce port — 1046 B MP-only deferred-action
         * processor. */

        /* 4. SP-only step — retail's `if (DAT_00553030 == 0) FUN_00404a46();`
         *    Now a real port: the LFSR advance. */
        if (g_sim_mp_active == 0) {
            engine_tick_rng_step();
        }

        /* 5. State-machine breaks — saving (10) breaks the loop;
         *    loading (0xb) returns from engine_tick entirely. */
        if (g_engine_state == 10) {
            break;
        }
        if (g_engine_state == 0xb) {
            load_bailout = 1;
            break;
        }

        /* 6. Screen-state-specific extras.  Both branches are
         *    deferred — all of their callees are blocked on the
         *    sim / UI / map subsystems.  Surveyed in
         *    main-loop-tick.md §"sub-step body screen-state extras". */
        if (g_sim_screen_substate == 0 && sub_steps != 0) {
            /* TODO(sim): FUN_0040490d (313 B), FUN_0049a010 (982 B),
             * 0x4650b0 (265 B). */
        } else if (g_sim_screen_substate == 2 && sub_steps != 0) {
            /* TODO(sim): 0x478952 (53 B) + per-screen chain
             * 0x488dfe (943 B), 0x4822ed (644 B),
             * 0x485bb1 (1592 B), 0x489401 (595 B),
             * 0x4859e5 (354 B), 0x477dfc (1225 B) when
             * DAT_004eac50 < 0x2b. */
        }
    }
    if (load_bailout) {
        /* Retail's loading branch returns from FUN_004b99c0 entirely
         * (skipping the render phase).  Same shape here. */
        return;
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

    /* Title-menu input dispatch — resolves the hover/click just computed
     * into a selected-button highlight + page transition while the title
     * screen is up.  No-op on every other screen (and dormant until a
     * producer sets g_sim_current_screen = 0x1f).  A transition here sets
     * the demand-redraw cookie the seam below consumes. */
    engine_tick_menu_input_step();

    /* Title-screen render step — the demand-redraw seam (retail
     * FUN_004b99c0 line 36) plus the per-frame button refresh (line 99,
     * FUN_004ba26e 0x1f arm), both painting into the locked back buffer.
     * On the title screen a full redraw fires on demand (page transition,
     * window move, surface restore) while the buttons repaint every frame
     * so the hover highlight tracks the mouse.  Dormant on every other
     * screen until a producer sets g_sim_current_screen = 0x1f (the
     * --title-live bring-up does, ahead of the unported boot→title
     * transition 0x42b780).  See docs/plans/main-menu-render-chips.md
     * chip 5 + docs/findings/title-menu-screens.md. */
    engine_tick_title_render_step();

    /* TODO(render): the remaining ~30 per-subsystem renders (the screen-0
     * in-game path 0x4050c0, the strategic-map / sprite renders, …)
     * still go between here and the tail block below; all blocked on the
     * respective UI / map / sprite subsystems.  Surveyed in
     * docs/findings/main-loop-tick.md. */

    /* Render-phase tail — retail gates the whole block on
     * `DAT_005c9274 == 0` (g_savepump_request).  Today's port:
     *
     *   FUN_00429b1d  ported.  Strategic-map scroll clamp (variable
     *                 bounds from visible col/row counts).  Idempotent
     *                 no-op today — no producer writes the scroll
     *                 position.
     *   FUN_0047ed34  ported.  Strategic / battlefield camera-anchor
     *                 clamp (hardcoded [0, 0x41] × [0, 0x42]).  Same
     *                 disposition — idempotent no-op today.
     *   FUN_00426529  surveyed: empty body (debug hook optimised out)
     *   FUN_004b27bf  ported.  Rolling 1-second FPS counter.  Bumps
     *                 frame counts, computes ms/frame, publishes
     *                 current FPS at second boundaries.
     *   FUN_004b12fb  surveyed: stub `return 1` in retail (likely
     *                 debug-build gate optimised out)
     *   FUN_004b0e03  surveyed: palette-fade machine, gated on screen
     *                 0x23 (Smacker/intro), blocked on intro screen
     *
     * After that retail has a deferred-shutdown trigger gated on
     * `DAT_005aeb68 == 4` — covered by g_shutdown_request handling
     * which today still lives inline in WndProc; the structural seam
     * comes when the sim that acks the shutdown lands. */
    if (g_savepump_request == 0) {
        engine_tick_camera_scroll_clamp();
        engine_tick_camera_anchor_clamp();
        engine_tick_fps_step();
    }

    gfx_tick_present(0);
}

void engine_tick_dispatch(void)
{
    CALL_TRACE_ENTER(0x0040e890);
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
    CALL_TRACE_ENTER(0x0040e8bb);
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
