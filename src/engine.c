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
#include "engine_load_pump.h"
#include "engine_turn.h"
#include "engine_army_ai.h"       /* engine_army_ai_run_turn (0049a990) */
#include "engine_army_state.h"    /* substate-0 trio: army-state driver + phase arms */
#include "engine_combat.h"        /* engine_combat_rotate_merc_offers (004aca2b) */
#include "engine_territory.h"     /* engine_territory_clear_size_state (004651b9) */
#include "engine_strat_cost.h"    /* engine_strat_cost_rebuild (0046ff43) */
#include "engine_ranking.h"       /* engine_ranking_recompute (0049aa0e) */
#include "engine_diplomacy.h"     /* engine_diplomacy_reconcile_treaties (004a1847) */
#include "engine_tick_cadence.h"  /* sub-step cadence counters (0040490d) */
#include "engine_map_terrain.h"   /* empty-cell redraw gate (scroll dismiss) */
#include "game_init.h"            /* g_anim_gate_53f0c4 (scroll dismiss) */
#include "gfx.h"
#include "gfx_panel.h"            /* scroll backdrop (004093e0) */
#include "gfx_wrap.h"             /* localized line/wrap draws (00402c5e/0040328e) */
#include "gfx_text.h"             /* raw name line + uppercase-highlight flag */
#include "gfx_sprite_render.h"    /* continue-seal draw (0040d1bc) */
#include "gfx_glyph_table.h"      /* char→glyph render/measure maps */
#include "audio.h"                /* audio_cue_release (00427d19) */
#include "localization.h"
#include "config.h"               /* config_localization_data (l2.eng blob) */
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
/* Game-speed slider (0..100, retail _DAT_0053f230).  Default 90
 * (0x5a) — retail's game-options initialiser FUN_004ae310 @ 0x4ae310
 * seeds exactly this, and the value is NOT a save-schema slot, so a
 * boot→load session plays at the boot default.  90 → slider gate
 * threshold (100-90)/10*10+2 = 12 ms between sub-steps; this is what
 * starves the sub-step loop during turbo idle on the retail seat
 * (grid_load_probe: exactly one sub-step at the load flip, zero
 * after).  The previous port default of 100 was a bug: threshold
 * 2 ms → a sub-step nearly every frame → runaway LFSR drift vs
 * retail.  Options-screen slider (0x434cbe binding, 10..100) lands
 * later. */
static int g_sim_game_speed = 90;
/* Help-hints option (retail DAT_0053f24c).  The same options-init
 * FUN_004ae310 seeds 1 (hints ON); the options-screen checkbox
 * (0x434787) toggles.  Not a schema slot.  Gates the per-frame
 * help-hint pump (0x476aa7) below. */
static int g_help_hints_enabled = 1;
/* Pause lock — non-zero freezes the sim.  retail DAT_0053f238.
 * Today no UI sets it, so always 0. */
static int g_sim_pause_lock;
/* Current screen ID.  retail DAT_004eac50 — single byte indexing the
 * per-screen tables.
 *
 * NOTE: gfx_text.c keeps a mirror of this same retail byte (the
 * shadow-pair discriminator — quirk #197).  engine_set_current_screen
 * forwards every write; ALL screen transitions must route through it. */
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

/* ─── Sim screen sub-state ──────────────────────────────────────────
 *
 * Retail DAT_0057c8fc — save-schema slot 14 (s_slot_0057c8fc), so the
 * canonical storage is the shared schema global and a load fills it
 * straight from the .sav.  0 = normal ingame, 2 = alt UI (combat
 * resolution / events screen).  Gates several sub-step body branches.
 * (Replaced the old file-static mirror, which a load could silently
 * desync from the schema slot.) */
#define g_sim_screen_substate   ((uint8_t)s_slot_0057c8fc)

/* "This player is human/active" gate.  Retail (&DAT_0057bf04)[player_index *
 * 0x160].  Both pieces are save-state (block 3 g_player_strategic_352 +
 * current-player slot 72 s_slot_0057c8cc), so derive on read — a load
 * route then needs no extra seeding. */
static uint8_t sim_player_human_active(void)
{
    /* Slot is 0..6 by construction; the &7 only bounds a corrupt save
     * (retail reads adjacent .data unchecked — personality_row-style
     * defensive clamp, same disposition). */
    return g_player_strategic_352[(s_slot_0057c8cc & 7) * 0x160 + 0x04];
}

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

    /* Retail order: FUN_004ba9c8 (the click router) first; when it does
     * not consume the click, FUN_0042ff10's per-state machine runs — of
     * which only the page-3 file-list grid selector (FUN_0043400b(1)) is
     * ported.  So a click on a save-list row that missed the Load/Cancel/
     * scroll strip selects that entry + loads its name into the filename
     * field, ready for the Load button → load-confirm pump. */
    if (engine_menu_title_input(&in) == 0)
        engine_menu_input_filelist_grid_step(&in);
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
        if (g_demand_redraw != 0) {
            if (g_sim_current_screen == 0) {
                /* Strategic map (FUN_0040f5fd): the ported arm composes
                 * into the locked back buffer like the title arm — present
                 * runs in the tick's gfx_tick_present tail.  No-op until
                 * the strat data path has populated the render arrays
                 * (engine_screen_strat_render gates on
                 * engine_pl8_data_ready), so this is dormant in the live
                 * loop until that path is wired. */
                if (gfx_lock_canvas()) {
                    engine_screen_dispatch(0, engine_title_render_page(),
                                           g_demand_redraw);
                    gfx_unlock_canvas();
                }
            } else {
                /* Other screens: generic demand-redraw seam.  The
                 * renderers are stubs (the other-fn hook), so no canvas
                 * lock is needed yet. */
                engine_screen_dispatch(g_sim_current_screen,
                                       engine_title_render_page(),
                                       g_demand_redraw);
            }
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
    /* Mirror the live double-LFSR state into its save home (entries 18/19).
     * Retail has ONE storage (DAT_0058fe28/0058fe08) read by both the stir
     * and the save; the port split it into this static + the s_slot mirror,
     * so reconcile here on every advance to keep the save in sync (see
     * project_shared_save_globals / new-game-save-parity #5). */
    s_slot_0058fe28 = g_rng_state_a;
    s_slot_0058fe08 = g_rng_state_b;
    g_rng_15bit_a = out.rng_15bit_a;
    g_rng_7bit_a  = out.rng_7bit_a;
    g_rng_3bit_a  = out.rng_3bit_a;
    g_rng_15bit_b = out.rng_15bit_b;
    g_rng_7bit_b  = out.rng_7bit_b;
    g_rng_3bit_b  = out.rng_3bit_b;
}

/* Read / write the live double-LFSR state (retail DAT_0058fe28/0058fe08).
 * The new-game seeder FUN_00497e65 (seat_shuffle) stirs this SAME global in
 * retail (3 FUN_00404a46 advances per loop iteration); the port threads its
 * draws through a local engine_rng_in_t, so game_init must load the global
 * in, run the seeder, and write the advanced state back — otherwise the
 * global stays at its InitInstance seed and the turn driver's stir (step 17)
 * starts from the wrong state, perturbing every RNG-driven turn-1 result
 * (realm standings/mood markers, the focus realm DAT_00554020, AI economy).
 * Writing back also refreshes the save mirror (entries 18/19). */
void engine_rng_global_get(uint32_t *state_a, uint32_t *state_b)
{
    if (state_a) *state_a = g_rng_state_a;
    if (state_b) *state_b = g_rng_state_b;
}

void engine_rng_global_set(uint32_t state_a, uint32_t state_b)
{
    g_rng_state_a   = state_a;
    g_rng_state_b   = state_b;
    s_slot_0058fe28 = state_a;
    s_slot_0058fe08 = state_b;
}

/* The engine's one canonical UI event queue — retail's global ring at
 * DAT_00568480 et al., which the turn passes post screen messages into
 * (and the in-game consumer 0x472e46, unported, drains).  Lazily inited
 * on the first turn advance so a fresh boot starts empty.  File-static:
 * the turn driver is the only producer wired live so far. */
static engine_event_queue_state_t g_turn_event_queue;
static int                        g_turn_event_queue_inited;

/* Win32 wrapper around the assembled per-turn driver (FUN_00448440).
 * See engine.h.  Runs retail's two head calls — the per-realm AI brain
 * (0049a990) and the RNG stir (00404a46, which refreshes the masked-RNG
 * mirrors g_rng_7bit_b/a) — then hands the freshly-stirred 7-bit bytes,
 * the engine's event queue, and the MP flag to engine_turn_run_passes
 * for the calendar block and the 27 economy/realm passes. */
void engine_turn_advance(void)
{
    if (!g_turn_event_queue_inited) {
        engine_event_queue_init_wrapper(&g_turn_event_queue);
        g_turn_event_queue_inited = 1;
    }

    engine_army_ai_run_turn();   /* #1  0049a990 — per-realm AI economic brain */
    engine_tick_rng_step();      /* #2  00404a46 — RNG stir → g_rng_7bit_b/a   */

    engine_turn_run_passes(&g_turn_event_queue,
                           g_rng_7bit_b, g_rng_7bit_a,
                           g_sim_mp_active);
}

/* ─── Screen-substate-0 strategic turn-phase dispatcher ─────────────
 *
 * Port of FUN_0049a010 @ 0x49a010 (982 B) — the per-sub-step driver of
 * the seven strategic turn phases, run as the middle call of the
 * sub-step trio (0x40490d → 0x49a010 → 0x4650b0) whenever the screen
 * sub-state is 0.  Bumps the per-phase pass counter (schema slot 74,
 * s_slot_0053f658) every pass, then dispatches on the current phase
 * (slot 73, s_slot_00569584); first-pass work is gated on the counter
 * == 1.  This is the slot-74 writer the grid_load_probe state bisect
 * fenced to retail's load-flip tick.
 *
 * Phase arms wired to their ported bodies:
 *   1  tick1 rations+grant(0) · tick2 independent-AI(0) · tick3 advance
 *   2  PARTIAL: scan-siege-built cadence live; entry pass 0x4a82b9
 *      (pending-garrison sweep + siege-cursor reset + 0x52afac clear)
 *      and the assault A/V trigger 0x4a8aab are TODO
 *   3  plan-capital-marches + arm-movement(4)
 *   5  plan-raids + arm-movement-owned(2,6)
 *   6  plan-settle-marches + arm-movement(3)
 *   7  PARTIAL: tick-counter resets + full turn rollover (turn driver,
 *      merc rotation, territory size clear, strat-cost rebuild, ranking,
 *      treaty reconcile, phase advance) live; the sub-screen closer
 *      0x43ad25 is sliced to its screen-id reset (per-screen closers
 *      0x4984dc/0x42df30 + repaint chain 0x40f1a0/0x4050c0/0x45240a/
 *      0x452160 are render-side TODO); the year-rollover scroll popup
 *      0x4b0cb4(0x24,1,0x5691f0) is TODO (popup-by-id system unported);
 *      the rollover wall-clock stamp 0x4e59d8 has no ported consumer
 *   4  (else) PARTIAL: the end-of-turn readiness check + phase advance
 *      are live; the per-tick AI brain FSM 0x49a581 (steps 1/6/9/0xb
 *      still unported), the turn-timer countdown block (0x5440c8 /
 *      0x53f26c / 0x568d9c + 0x43ac23 + the end-turn strip redraw
 *      0x41a734), the roster-999 10s re-poll, and the auto-advance
 *      block (0x55403c / 0x57cad0 / 0x553018) are TODO — all are
 *      wall-clock-paced UI/MP affordances, not sim state
 *
 * The phase-1 tick-3 arm stamps the phase-4 auto-advance baseline
 * (retail _DAT_0057cad0 = timeGetTime()) into a runtime mirror so the
 * TODO auto-advance block has its input when it lands. */
static uint32_t g_sim_phase4_baseline_ms;       /* _DAT_0057cad0 */
static int32_t  g_sim_player_ticks[10];         /* DAT_0053e8d0[0..9] */

/* The ten sub-step cadence counters (0x40490d/0x40489e state) — first
 * call of the substate-0 trio.  All .bss runtime, never saved. */
static engine_tick_cadence_t g_tick_cadence;

static void engine_turn_tick_substate0(void)
{
    CALL_TRACE_ENTER(0x0049a010);

    if (!g_turn_event_queue_inited) {
        engine_event_queue_init_wrapper(&g_turn_event_queue);
        g_turn_event_queue_inited = 1;
    }

    s_slot_0053f658 += 1;            /* per-phase pass counter (slot 74) */

    if (s_slot_00569584 == 7) {
        /* Turn rollover. */
        g_sim_tick_counter = 0;                       /* DAT_0057c998 */
        for (int i = 0; i < 10; i++)
            g_sim_player_ticks[i] = 0;                /* DAT_0053e8d0 */
        /* 0x43ad25 slice: force the strategic-map screen back (retail
         * also closes any open sub-screen first — TODO, see header). */
        engine_set_current_screen(0);                 /* DAT_004eac50 */
        /* TODO: _DAT_004e59d8 = now (rollover stamp, no consumer). */
        engine_turn_advance();                        /* FUN_00448440 */
        engine_combat_rotate_merc_offers();           /* FUN_004aca2b */
        engine_territory_clear_size_state();          /* FUN_004651b9 */
        engine_strat_cost_rebuild(g_overworld_map);   /* FUN_0046ff43 */
        engine_ranking_recompute(&g_turn_event_queue);/* FUN_0049aa0e */
        engine_diplomacy_reconcile_treaties();        /* FUN_004a1847 */
        engine_turn_advance_phase();                  /* FUN_0049ce51 */
        /* TODO: 0x4b0cb4(0x24, 1, 0x5691f0) — year-rollover scroll. */
    } else if (s_slot_00569584 == 5) {
        if (s_slot_0053f658 == 1) {
            engine_army_state_plan_raids();           /* FUN_004ac499 */
        }
        if (engine_army_state_arm_movement_owned(2, 6) == 0)  /* 4a4e3d */
            engine_turn_advance_phase();
    } else if (s_slot_00569584 == 2) {
        if (s_slot_0053f658 == 1) {
            /* TODO: 0x4a82b9 — phase-2 entry: pending-garrison sweep
             * over realms 1..16, siege-scan cursor reset
             * (s_slot_0057cad4 = 1), 0x52afac clear. */
        }
        /* Retail: if (DAT_00553030 == 0) DAT_0052afac = 1 — runtime
         * screen flag, producer TODO with the 0x4a82b9 port. */
        if (s_slot_0053f658 % 100 == 2) {
            if (engine_army_state_scan_siege_built() == 0) {  /* 4a84ba */
                engine_turn_advance_phase();
            } else {
                /* TODO: 0x5540 3c = 0 (turn-wait state) + 0x4a8aab
                 * (assault A/V trigger on the parked siege record). */
            }
        }
    } else if (s_slot_00569584 == 1) {
        if (s_slot_0053f658 == 1) {
            engine_army_ai_set_rations_and_grant(0);  /* FUN_0049d638 */
        } else if (s_slot_0053f658 == 2) {
            engine_army_ai_run_independent(0);        /* FUN_0049dfc6 */
        } else if (s_slot_0053f658 == 3) {
            engine_turn_advance_phase();              /* FUN_0049ce51 */
            g_sim_phase4_baseline_ms = timeGetTime(); /* _DAT_0057cad0 */
        }
    } else if (s_slot_00569584 == 6) {
        if (s_slot_0053f658 == 1) {
            engine_army_state_plan_settle_marches();  /* FUN_004280e9 */
        }
        if (engine_army_state_arm_movement(3) == 0)   /* FUN_004a4f5b */
            engine_turn_advance_phase();
    } else if (s_slot_00569584 == 3) {
        if (s_slot_0053f658 == 1) {
            engine_army_state_plan_capital_marches(); /* FUN_00429418 */
        }
        if (engine_army_state_arm_movement(4) == 0)   /* FUN_004a4f5b */
            engine_turn_advance_phase();
    } else {
        /* Phase 4 — the human player's interactive phase.
         * TODO (wall-clock UI/MP affordances, see header): the turn-timer
         * countdown, the roster-999 10-second re-poll, the auto-advance
         * block.  TODO (sim): the per-tick AI brain FSM 0x49a581. */
        if (engine_sim_turn_resolved() != 0)          /* FUN_0049b762 */
            engine_turn_advance_phase();
    }
}

/* ─── In-game event scroll: depth-warning post + consumer slice ─────
 *
 * Retail's first act inside the per-tick body FUN_004b99c0 is the
 * "Non 256 colour display" warning: ONCE per session, on the first
 * in-game tick with a full-color desktop, it posts event code 0x12a
 * into the global event ring:
 *
 *   if (DAT_0053f228 == 1 && DAT_0053f22c != 8 &&
 *       DAT_005533d4 == 0 && DAT_0052af94 == 3) {
 *       DAT_005533d4 = 1;
 *       FUN_00472bc5(0, DAT_0057c8cc, 0x12a, 0,0,0,0,0);
 *   }
 *
 * The render-phase consumer FUN_00472e46 then dequeues it, loads the
 * payload mirrors, and parks the event code in DAT_0053e910 — which is
 * EXACTLY the rate controller's extended_pause input: an open event
 * scroll freezes the SP sim (this is what froze retail's sim for the
 * whole 60-tick grid_load_probe window after exactly one sub-step).
 * The scroll's pixels come from the renderer below (0x47309e, type-0
 * arm ported — the load-route 92.9k-px map residual).
 *
 * Display globals: DAT_0053f228 (depth class) / DAT_0053f22c (bpp) are
 * options-init'd to 1/8 and boot-resolved from the desktop; the
 * drop-in targets modern full-color desktops (gfx.c's full-range
 * palette default), so the faithful defaults here are class 1 /
 * 32 bpp → the warning posts, matching retail-under-seat.  A true
 * 8bpp/legacy configuration (gfx_set_palette_full_range(0)) would set
 * bpp 8 and never warn — wire that coupling when a legacy seat needs
 * it.  None of these is a schema slot; only their consequence (the
 * ring write) is save-state. */
static int g_display_depth_class = 1;     /* DAT_0053f228 */
static int g_display_bpp         = 32;    /* DAT_0053f22c (boot-resolved) */
static int g_sim_depth_warned;            /* DAT_005533d4 (one-shot) */

/* Consumer payload mirrors (runtime .bss in retail) — read by the
 * scroll renderer below and the dismiss path. */
static int32_t g_popup_payload_a;         /* DAT_005440ac ← rec +0x0c */
static int32_t g_popup_payload_b;         /* DAT_005678ac ← rec +0x14 */
static uint8_t g_popup_flag6;             /* DAT_0055ce54 ← rec +0x12 */
static uint8_t g_popup_flag7;             /* DAT_0057c8ac ← rec +0x13 */
static uint8_t g_popup_type;              /* DAT_005440a4 ← rec +0x11 */
static int32_t g_popup_target;            /* DAT_00553ee0 ← rec +0x04 */

/* Open-scroll geometry + render bookkeeping (runtime .bss; written by
 * the renderer each frame, read by the dismiss hit-test when the
 * scroll-input chip lands). */
static int     g_popup_x;                 /* DAT_00553060 (px) */
static int     g_popup_y;                 /* DAT_00553064 (px) */
static int     g_popup_w;                 /* DAT_00553024 (px) */
static int     g_popup_h;                 /* DAT_00552ff8 (px) */
static int32_t g_popup_stash_code;        /* DAT_004f0374 — head stash; the
                                           * type-0xd/0xe arms re-speak from
                                           * it after their inner dismiss */
static int32_t g_popup_stash_payload;     /* DAT_004f0354 */
static int     g_popup_seal_committed_x;  /* DAT_005681dc ← seal x, per frame */
static int     g_popup_seal_committed_y;  /* DAT_0056950c ← seal y, per frame */
static int     g_popup_mp_substate;       /* DAT_0055cd78 — MP popup sub-state;
                                           * dismiss parks it at 2 */
static uint8_t g_screen27_saved_screen;   /* DAT_004f0350 — screen to restore
                                           * when leaving the 0x27 report */
static int     g_screen27_cookie = 0x14;  /* DAT_004f0358 — the hint-
                                           * pump cooldown.  Boot-armed
                                           * to 0x14 by the WinMain
                                           * hint reset (FUN_00476a5d
                                           * from 0x40e9ab); re-armed
                                           * by every screen-27
                                           * restore. */

/* Schema block 11 (g_misc_128, retail 0x568480) IS the first 128 bytes
 * of the live event ring — retail's producer/consumer mutate the ring
 * in place and the save schema captures its head (5⅓ records).  The
 * port's queue lives in a struct, so every ring mutation re-mirrors
 * the head into the save block (the same pattern as the LFSR slots
 * 18/19 in engine_rng_global_set). */
static void evq_sync_save_mirror(void)
{
    memcpy(g_misc_128, g_turn_event_queue.ring, sizeof(g_misc_128));
}

/* Load-route seeding — the inverse mirror.  save_load fills g_misc_128
 * from the .sav; the live queue must then see that ring head, or the
 * first consumer pass would read a stale slot 0.  write_idx/read_idx/
 * notify/cooldown are NOT schema slots (retail leaves them at their
 * boot values across a load), so only the ring bytes seed. */
void engine_event_queue_seed_from_save(void)
{
    if (!g_turn_event_queue_inited) {
        engine_event_queue_init_wrapper(&g_turn_event_queue);
        g_turn_event_queue_inited = 1;
    }
    memcpy(g_turn_event_queue.ring, g_misc_128, sizeof(g_misc_128));
}

/* The pre-tick depth-warning post (the FUN_004b99c0 head block above). */
static void engine_tick_depth_warning_step(void)
{
    if (g_display_depth_class == 1 && g_display_bpp != 8 &&
        g_sim_depth_warned == 0 && g_engine_state == 3) {
        g_sim_depth_warned = 1;
        if (!g_turn_event_queue_inited) {
            engine_event_queue_init_wrapper(&g_turn_event_queue);
            g_turn_event_queue_inited = 1;
        }
        /* The producer's filter table: has_data[i] = strategic +5 byte
         * (same construction as engine_army_ai's build_has_data). */
        uint8_t has_data[7];
        for (int i = 0; i < 6; i++)
            has_data[i] = g_player_strategic_352[i * 0x160 + 0x05];
        has_data[6] = 0;
        engine_event_queue_post(&g_turn_event_queue,
                                0, (int32_t)s_slot_0057c8cc,
                                0x12a, 0, 0, 0, 0, 0,
                                has_data, (int)s_slot_0057c8cc);
        evq_sync_save_mirror();
    }
}

/* ─── Event voice-over speak — port of FUN_004b35c1 @ 0x4b35c1 (255 B)
 *
 * Speaks the event's voice line: stops any in-flight cue
 * (audio_cue_release), raises the cue-active latch, and plays the
 * event's wav from one of three .rdata name tables:
 *
 *   codes 100..0xa9 → "S1xx_xx.wav" table @ 0x4df9b8 (stride 0x10)
 *   codes 0xaa..0xc5 → "ktxxx_x.wav" table @ 0x4e0458 (stride 0x100
 *                       per code + 0x10 per `variant` — the payload-a
 *                       mirror selects the line variant)
 *   codes 200..0x11c → "S2xx_xx.wav" table @ 0x4dfe18 (stride 0x10)
 *
 * Codes outside all three ranges (198/199, ≥ 0x11d — including the
 * 256-colour warning 0x12a) fall through silently: no latch write, no
 * cue release.  The cue-playback path (0x427990) is deferred
 * project-wide (audio.c), so the latch + release are the live state
 * effects; the audible part lands with the SFX subsystem (the name
 * tables image then). */
static void engine_popup_speak_event(int code, int variant)
{
    (void)variant;
    if (code >= 0xaa && code <= 0xc5) {
        g_combat_audio_latch = 1;             /* _DAT_005533f0 */
        audio_cue_release();                  /* FUN_00427d19 */
        /* TODO(sfx): 0x427990(0x4e0458 + (code-0xaa)*0x100
         *                              + variant*0x10, 1, 0). */
    } else if (code >= 200 && code <= 0x11c) {
        g_combat_audio_latch = 1;
        audio_cue_release();
        /* TODO(sfx): 0x427990(0x4dfe18 + (code-200)*0x10, 1, 0). */
    } else if (code >= 100 && code <= 0xa9) {
        g_combat_audio_latch = 1;
        audio_cue_release();
        /* TODO(sfx): 0x427990(0x4df9b8 + (code-100)*0x10, 1, 0). */
    }
}

/* ─── Screen-0x27 restore — port of FUN_00476e21 @ 0x476e21 (116 B)
 *
 * When the scroll being dismissed was opened over the 0x27 report
 * screen, restore the screen saved at entry (DAT_004f0350; 0x27 saved
 * → fall back to the strategic map), re-arm the report cookie, and
 * request a demand redraw (full when restoring screen 0x17, else
 * mode 2).  No-op on every other screen — including the load route's
 * screen 0. */
static void engine_popup_screen27_restore(void)
{
    if (g_sim_current_screen == 0x27) {
        int restored = g_screen27_saved_screen;
        if (g_screen27_saved_screen == 0x27)
            restored = 0;
        engine_set_current_screen(restored);
        g_screen27_cookie = 0x14;             /* DAT_004f0358 */
        if (g_sim_current_screen == 0x17)
            g_demand_redraw = 1;              /* DAT_0057d340 */
        else
            g_demand_redraw = 2;
    }
}

/* ─── Scroll dismiss — port of FUN_00476768 @ 0x476768 (245 B)
 *
 * Closes the open event scroll: unfreezes the SP sim (extended_pause
 * → 0), zeroes the consumer cooldown so the next pending event can
 * open next frame, marks the full screen dirty so the map repaints
 * under where the scroll was, and re-arms the empty-cell redraw gate.
 * Retail callers: the consumer's off-screen / MP-timed arms, the
 * scroll-input click chip (0x476aa7 — unported), and the renderer's
 * type-0xb/0xd/0xe arms. */
static void engine_popup_scroll_dismiss(void)
{
    CALL_TRACE_ENTER(0x00476768);

    int code = g_sim_extended_pause;
    if (code == 0xef || code == 0xee || code == 0x9f || code == 0x9e) {
        /* Game-end event class (won/lost) — hand off to the end-game
         * flow.  TODO(endgame): 0x4afab9(0x4d7094) log line, screen
         * (DAT_004eac50) = 0x26, 0x426d11(0) stream stop, main-loop
         * run-state DAT_005aeb68 = 3. */
    }
    if (code != 0xc2)
        audio_cue_release();                  /* FUN_00427d19 */

    gfx_mark_all_dirty();                     /* FUN_0045240a */
    engine_popup_screen27_restore();          /* FUN_00476e21 */

    if (!g_turn_event_queue_inited) {
        engine_event_queue_init_wrapper(&g_turn_event_queue);
        g_turn_event_queue_inited = 1;
    }
    g_turn_event_queue.consumer_cooldown = 0; /* DAT_0057c954 */
    g_sim_extended_pause = 0;                 /* DAT_0053e910 — unfreeze */
    g_popup_mp_substate  = 2;                 /* DAT_0055cd78 */
    engine_map_terrain_set_empty_redraw_gate(1); /* DAT_0053e9ac */
    g_combat_flag_530d0  = 1;                 /* DAT_005530d0 */

    if (g_anim_gate_53f0c4 == 0xb || g_anim_gate_53f0c4 == 10) {
        /* Battle-accept handoff parked by the renderer's type-0xe arm.
         * TODO(combat-ui): 0x497879 (scenario config-apply 0x499e5d +
         * screen 0x1c) + DAT_0057d340 = 1. */
    }
}

/* ─── Open-scroll renderer — port of FUN_0047309e @ 0x47309e (10,915 B)
 *
 * One arm per popup TYPE (the flag5 mirror).  The common shape: stash
 * the code/payload pair, arm the uppercase-highlight text flag, run
 * the per-type layout (a beveled parchment panel + title line + word-
 * wrapped body + the click-to-continue seal), then the shared tail
 * (commit the seal position, tile-mark the panel box, clear the
 * highlight flag).  Per-type one-shots key off exact consumer-
 * cooldown values (the cooldown counts 2000 → 1 once a scroll opens):
 * 2000 = first open frame (fanfare cues), 0x7c6/0x776/0x708 = speak /
 * camera-jump beats ~16-90 frames in.
 *
 * Ported arm: type 0 — generic event scroll (the load route's event
 * 0x12a "Non 256 colour display" posts with type 0; r2-verified
 * byte-exact against 0x4730d2..0x4732c5 + the tail 0x475ae8).  The
 * remaining arms are skeleton TODOs (bare-hex ranges in the arm
 * ladder below); each lands with its event producer.
 *
 * Drop-in deviations (structure-preserving):
 *   - assets fetch lazily (gfx_load_*) and the body paints inside a
 *     gfx canvas lock — retail's surface pointer is always live.
 *   - the tail's FUN_004527a6(x, y, w/16+1, h/16+1, 2) tile-mark is
 *     subsumed by gfx_unlock_canvas()'s mark-all-dirty until the
 *     per-frame map-scene tile renderer (0x4050c0) is wired into the
 *     live loop; the committed-seal copy IS mirrored. */
static void engine_popup_scroll_render(void)
{
    CALL_TRACE_ENTER(0x0047309e);

    g_popup_stash_code    = g_sim_extended_pause;  /* DAT_004f0374 */
    g_popup_stash_payload = g_popup_payload_a;     /* DAT_004f0354 */
    gfx_text_set_uppercase_highlight(1);           /* DAT_0058fe2c = 1 */

    const uint8_t *chrome     = gfx_load_chrome_sheet();  /* panels.pl8  */
    const uint8_t *sys_sheet  = gfx_system_sheet_current(); /* the live
                                  DAT_005bb540 slot — system.pl8 ingame
                                  (the tick-head ensure), system2 on the
                                  title flow */
    const uint8_t *title_font = gfx_load_title_font();    /* fntl2_22    */
    const uint8_t *menu_font  = gfx_load_menu_font();     /* fntl2_14    */
    size_t loc_size = 0;
    const uint8_t *locblob = config_localization_data(&loc_size);
    if (!chrome || !sys_sheet || !title_font || !menu_font || !locblob) {
        gfx_text_set_uppercase_highlight(0);
        return;
    }
    localization_state_t loc;
    loc.blob = (unsigned char *)locblob;
    loc.walker_offset = 0;

    int cooldown = g_turn_event_queue.consumer_cooldown; /* DAT_0057c954 */

    if (!gfx_lock_canvas()) {
        gfx_text_set_uppercase_highlight(0);
        return;
    }

    if (g_popup_type == 0) {
        /* ── Type 0: generic event scroll ──────────────────────── */
        if (cooldown == 2000 && g_sim_extended_pause > 0x71
                             && g_sim_extended_pause < 0x7f) {
            /* Territory-capture event class (0x72..0x7e): fanfare on
             * the first open frame. */
            g_combat_audio_latch = 0;          /* _DAT_005533f0 */
            /* TODO(sfx): 0x427990("ff_capt.wav", 1, 0) — cue playback
             * deferred project-wide (audio.c). */
        }
        if (cooldown == 0x776) {
            /* 90 frames in: speak the event voice-over (silent for
             * codes outside the voiced ranges — incl. 0x12a). */
            engine_popup_speak_event(g_sim_extended_pause,
                                     g_popup_payload_a);
        }
        g_popup_x = 0x20;  g_popup_y = 0xa0;   /* DAT_00553060/64 */
        g_popup_w = 0x1a0; g_popup_h = 0xe0;   /* DAT_00553024/0x552ff8 */
        gfx_panel_draw_bevel(chrome, g_popup_x, g_popup_y,
                             g_popup_w / 16, g_popup_h / 16);
        if (g_popup_flag6 != 0) {
            /* County-titled event: loc entry 100, sub = map-id*20 +
             * county index. */
            gfx_wrap_localized_line_centered(&loc, 100,
                (int)s_slot_0053f034 * 0x14 + g_popup_flag6,
                g_popup_x + 0x10, g_popup_y + 0x20, g_popup_w - 0x20,
                title_font, gfx_glyph_render_map(),
                gfx_glyph_measure_map(), 0x3f);
        } else if (g_popup_flag7 != 0) {
            /* Player-titled event: the player's name from the block-2
             * aux record (retail 0x553d54 = g_player_aux_44 + 4). */
            gfx_text_render_line(g_player_aux_44 + g_popup_flag7 * 0x2c + 4,
                                 g_popup_x + 0x20, g_popup_y + 0x20,
                                 title_font, gfx_glyph_render_map(), 0x3f);
        } else {
            /* Generic title: the event's own loc entry, sub 0. */
            gfx_wrap_localized_line_centered(&loc, g_sim_extended_pause, 0,
                g_popup_x + 0x10, g_popup_y + 0x20, g_popup_w - 0x20,
                title_font, gfx_glyph_render_map(),
                gfx_glyph_measure_map(), 0x3f);
        }
        /* Body: sub-string 1, word-wrapped inside the panel. */
        gfx_wrap_localized(&loc, g_sim_extended_pause, 1,
                           g_popup_x + 0x20, g_popup_y + 0x50,
                           g_popup_w - 0x40, 400, 0, 0,
                           menu_font, gfx_glyph_render_map(),
                           gfx_glyph_measure_map(), 0x3f);
        /* Click-to-continue seal at the bottom-right corner. */
        gfx_sprite_seal_draw(sys_sheet,
                             g_popup_x + g_popup_w - 0x30,
                             g_popup_h + g_popup_y - 0x30, 0);
    } else {
        /* TODO(popup-arms): types 0x10/0x11/0x12 (production/harvest
         * reports), 1/2 (capture w/ portrait), 3, 4 (tooltip-sized,
         * position-tracking), 5..9 (multi-line generic), 0x13 (table-
         * driven rect ladder), 10/0xb (portrait + accept), 0xc
         * (0x475e07), 0x14 (0x476488), 0xd/0xe (smk-playing victory /
         * ownership arms — call the dismiss inline), 0xf (event-
         * detail ladder).  Each lands with its event producer; the
         * shared tail below still runs so the state effects (seal
         * commit + highlight clear) stay faithful. */
    }

    /* ── Common tail (0x475ae8) ───────────────────────────────── */
    g_popup_seal_committed_x = gfx_sprite_seal_x();  /* DAT_005681dc */
    g_popup_seal_committed_y = gfx_sprite_seal_y();  /* DAT_0056950c */
    /* FUN_004527a6(x, y, w/16+1, h/16+1, 2) — tile-mark subsumed by
     * the unlock's mark-all-dirty (see header note). */
    gfx_unlock_canvas();
    gfx_text_set_uppercase_highlight(0);             /* DAT_0058fe2c = 0 */
}

/* ─── Help-hint pump + report opener ────────────────────────────────
 *
 * Retail shows a one-shot tutorial scroll the first time each screen
 * (or screen sub-mode) comes up in a session.  Two pieces:
 *
 *   FUN_00476da9 — open a help report: save the current screen into
 *   the restore slot (the dismiss's screen-0x27 restore reads it
 *   back), flip to screen 0x27, latch the per-code shown flag, zero
 *   the report scroll position, and POST the hint event (code = the
 *   hint id, type byte from the .rdata table 0x4d6ed8[code]).
 *
 *   FUN_00476aa7 — the per-frame pump (retail tick line 104, right
 *   before the scroll consumer): gated on the help-hints option +
 *   ingame state + the post-restore cooldown (DAT_004f0358 = 0x14,
 *   re-armed by the screen-27 restore — one hint per 20 frames).
 *   Strategic screen: view-mode-2 hint 0xce, then the 0xc8/0xc9/0xca
 *   welcome series (one per pass).  Combat sub-state: 0xd4 or
 *   0xd6/0xd7 by quick-resolve.  Then one ladder arm per screen id.
 *
 * The shown flags live at retail 0x4f0360..0x4f0373 — one byte per
 * hint code 0xc8..0xdb (the opener indexes (&0x4f0298)[code], which
 * lands in that window for every caller).  FUN_00476a5d resets the
 * 20 flags + arms the cooldown to 0x14; retail runs it ONCE from the
 * WinMain boot init (0x40e9ab) and again when the options checkbox
 * toggles (0x434787).  Because the cooldown only counts down while
 * INGAME, the boot arm means the first hint posts exactly 20 ingame
 * frames after a load/new-game flips state 3.  On the load route this
 * is the "mid-idle second event": the pump posts hint 0xc8 (first-
 * time strategic-screen help, type 7) into ring slot 1 while the
 * 256c warning still holds the scroll modal — the settled-state
 * block-11 record-1 residual. */
static uint8_t g_hint_shown[0x14];       /* 0x4f0360[code - 0xc8] */
static int     g_report_scroll_pos;      /* DAT_0052f004 */
static int     g_combat_flag_57a0f0;     /* DAT_0057a0f0 — combat-mode
                                          * gate read by the pump's
                                          * sub-state-2 arm; no port
                                          * writer yet (combat UI) */
static uint8_t g_sim_battle_subscreen;   /* DAT_00553f54 — battle sub-
                                          * screen id; 8 = the hint-
                                          * capable battle sub-mode.
                                          * No port writer yet. */

/* The hint-event TYPE table — image of retail .rdata 0x4d6ed8[code]
 * for the hint code range 0xc8..0xdb (r2: px @ 0x4d6fa0).  Type 7..9
 * select the multi-line generic scroll arms (3..5 body sub-strings),
 * 5/6 the shorter ones, 8 the portrait arm. */
static const uint8_t k_hint_event_type[0x14] = {
    /* 0xc8 */ 0x07, 0x08, 0x09, 0x05, 0x05, 0x05, 0x05, 0x08,
    /* 0xd0 */ 0x05, 0x08, 0x07, 0x05, 0x06, 0x05, 0x07, 0x05,
    /* 0xd8 */ 0x05, 0x09, 0x07, 0x05,
};

/* Open a help report — port of FUN_00476da9 @ 0x476da9 (120 B). */
static void engine_popup_open_report(int code)
{
    CALL_TRACE_ENTER(0x00476da9);
    if (g_sim_current_screen != 0x27) {
        g_screen27_saved_screen = g_sim_current_screen;  /* DAT_004f0350 */
        engine_set_current_screen(0x27);
        if (code >= 0xc8 && code < 0xc8 + 0x14)
            g_hint_shown[code - 0xc8] = 1;     /* (&DAT_004f0298)[code] */
        g_report_scroll_pos = 0;                         /* DAT_0052f004 */

        if (!g_turn_event_queue_inited) {
            engine_event_queue_init_wrapper(&g_turn_event_queue);
            g_turn_event_queue_inited = 1;
        }
        uint8_t has_data[7];
        for (int i = 0; i < 6; i++)
            has_data[i] = g_player_strategic_352[i * 0x160 + 0x05];
        has_data[6] = 0;
        uint8_t type = (code >= 0xc8 && code < 0xc8 + 0x14)
                           ? k_hint_event_type[code - 0xc8] : 0;
        engine_event_queue_post(&g_turn_event_queue,
                                0, (int32_t)s_slot_0057c8cc,
                                code, 0, type, 0, 0, 0,
                                has_data, (int)s_slot_0057c8cc);
        evq_sync_save_mirror();
    }
}

/* The per-frame hint pump — port of FUN_00476aa7 @ 0x476aa7 (770 B). */
static void engine_tick_help_hint_step(void)
{
    CALL_TRACE_ENTER(0x00476aa7);

    if (g_help_hints_enabled == 0 || g_engine_state != 3)
        return;
    if (g_screen27_cookie != 0) {
        g_screen27_cookie -= 1;                  /* DAT_004f0358 */
        return;
    }

    uint8_t scr = g_sim_current_screen;
    if (scr == 0 && g_sim_screen_substate == 0) {
        /* Strategic screen: the view-mode-2 hint, then the welcome
         * series — one hint per pump pass. */
        if (s_slot_0057cb18 == 2 && g_hint_shown[0xce - 0xc8] == 0) {
            engine_popup_open_report(0xce);
            return;
        }
        if (g_hint_shown[0xc8 - 0xc8] == 0) { engine_popup_open_report(0xc8); return; }
        if (g_hint_shown[0xc9 - 0xc8] == 0) { engine_popup_open_report(0xc9); return; }
        if (g_hint_shown[0xca - 0xc8] == 0) { engine_popup_open_report(0xca); return; }
    }
    if (scr == 0 && g_sim_screen_substate == 2) {
        /* Combat sub-state: the whole arm bails while the combat gate
         * holds; quick-resolve picks the hint pair. */
        if (g_combat_flag_57a0f0 != 0)
            return;
        if (s_slot_0053e8fc == 0) {
            if (g_hint_shown[0xd4 - 0xc8] == 0) {
                engine_popup_open_report(0xd4);
                return;
            }
        } else {
            if (g_hint_shown[0xd6 - 0xc8] == 0) {
                engine_popup_open_report(0xd6);
                return;
            }
            if (g_hint_shown[0xd7 - 0xc8] == 0) {
                engine_popup_open_report(0xd7);
                return;
            }
        }
    }
    if (scr == 0x17 && g_hint_shown[0xd1 - 0xc8] == 0) {
        engine_popup_open_report(0xd1);
    } else if (scr == 0xf && g_sim_battle_subscreen == 8 &&
               g_hint_shown[0xd0 - 0xc8] == 0) {
        engine_popup_open_report(0xd0);
    } else if (scr == 0x02 && g_hint_shown[0xcf - 0xc8] == 0) {
        engine_popup_open_report(0xcf);
    } else if (scr == 0x39 && g_hint_shown[0xda - 0xc8] == 0) {
        engine_popup_open_report(0xda);
    } else if (scr == 0x1b && g_hint_shown[0xd9 - 0xc8] == 0) {
        engine_popup_open_report(0xd9);
    } else if (scr == 0x10 && g_hint_shown[0xd2 - 0xc8] == 0) {
        engine_popup_open_report(0xd2);
    } else if (g_combat_flag_553210 != 0) {
        /* Consumed-and-cleared even when the hint was already shown
         * (retail clears it inside the condition). */
        g_combat_flag_553210 = 0;
        if (g_hint_shown[0xd3 - 0xc8] == 0)
            engine_popup_open_report(0xd3);
    }
}

/* Render-phase event-scroll consumer — port of FUN_00472e46 @ 0x472e46
 * (595 B).  Runs mid render-pump in retail (between the UI pumps and
 * the screen-render dispatch).  Dequeues at most one pending event per
 * cooldown window: parks its code in g_sim_extended_pause (the modal
 * sim freeze), mirrors the payload fields, zeros the consumed slot's
 * code (engine_event_queue_slot_zero — ring mutation → re-mirror) and
 * arms the 2000-frame cooldown.  In SP the cooldown is pinned at ≥ 1
 * while a scroll is open — modal until dismissed by input.  While a
 * scroll is open the renderer paints it every frame. */
static void engine_tick_event_popup_step(void)
{
    CALL_TRACE_ENTER(0x00472e46);

    uint8_t scr = g_sim_current_screen;
    int in_gate = (scr == 0 || scr == 0x27 || scr == 0x29 ||
                   (scr == 0xf && g_sim_battle_subscreen == 8));
    if (!in_gate) {
        if (g_sim_extended_pause != 0) {
            /* Auto-dismiss when the screen leaves the scroll-capable
             * set. */
            engine_popup_scroll_dismiss();           /* FUN_00476768 */
        }
        return;
    }
    if (g_sim_screen_substate == 2 && g_sim_extended_pause != 0) {
        /* Alt-UI sub-state arm: dismiss and skip the consumer pass. */
        engine_popup_scroll_dismiss();               /* FUN_00476768 */
        return;
    }

    if (!g_turn_event_queue_inited) {
        engine_event_queue_init_wrapper(&g_turn_event_queue);
        g_turn_event_queue_inited = 1;
    }
    engine_event_queue_state_t *q = &g_turn_event_queue;

    if (q->consumer_cooldown < 1) {
        q->consumer_cooldown = 0;
        engine_event_queue_record_t *slot = &q->ring[q->read_idx];
        g_sim_extended_pause = slot->code;          /* DAT_0053e910 */
        if (g_sim_extended_pause == 0) {
            /* Empty slot: chase the producer one step per frame. */
            if (q->read_idx == q->write_idx)
                return;                              /* fully drained */
            q->read_idx += 1;
            if (q->read_idx >= 0x32)
                q->read_idx = 0;
            return;
        }
        /* Live event — open it. */
        g_popup_payload_a = slot->payload32a;        /* DAT_005440ac */
        g_popup_payload_b = slot->payload32b;        /* DAT_005678ac */
        g_popup_flag6     = slot->flag6;             /* DAT_0055ce54 */
        g_popup_flag7     = slot->flag7;             /* DAT_0057c8ac */
        g_popup_type      = slot->flag5;             /* DAT_005440a4 */
        g_popup_target    = slot->target;            /* DAT_00553ee0 */
        engine_event_queue_slot_zero(q, q->read_idx);/* FUN_00472b40 */
        q->read_idx += 1;
        if (q->read_idx > 0x31)
            q->read_idx = 0;
        q->consumer_cooldown = 2000;
        evq_sync_save_mirror();                      /* ring mutated */
    } else {
        q->consumer_cooldown -= 1;
        if (g_sim_mp_active == 0 || g_popup_type == 0xe) {
            /* SP (or type-0xe broadcast): modal — pin the cooldown so
             * the scroll never times out; only input dismisses. */
            if (q->consumer_cooldown < 1)
                q->consumer_cooldown = 1;
        } else if (g_popup_type == 4) {
            if (q->consumer_cooldown < 1) {
                /* MP type-4 timed dismiss. */
                engine_popup_scroll_dismiss();       /* FUN_00476768 */
            }
        } else if (q->consumer_cooldown < 0x641) {
            /* MP general timed dismiss. */
            engine_popup_scroll_dismiss();           /* FUN_00476768 */
        }
    }

    if (g_sim_extended_pause != 0) {
        /* Draw the open event scroll every frame it stays open. */
        engine_popup_scroll_render();                /* FUN_0047309e */
    }
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
    in.rate_lut           = g_engine_rate_lut;   /* imaged DAT_004e3260 */

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
    /* gfx_text keeps its own mirror of the screen byte (retail reads
     * the live DAT_004eac50 at every text render to pick the shadow
     * color pair — title-class screens 0x1c/0x1f get the gray pair,
     * everything else the parchment pair).  Forward every write so
     * the two mirrors can't skew. */
    gfx_text_set_screen_byte(screen);
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
     *   2. Pre-tick block — TODO (remaining retail subfunctions:
     *      FUN_004b0aaa input poll, FUN_00498458 unknown,
     *      FUN_004b20ed unknown, …).  FUN_00499a1c(1) (in-game
     *      system-sheet ensure) is ported + wired below.
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

    /* Retail's first act in the per-tick body: the once-per-session
     * non-8bpp display warning post (see the block's header above). */
    engine_tick_depth_warning_step();

    /* Retail line 2: while ingame (state 3), ensure the IN-GAME system
     * sheet (system.pl8) is resident in the shared sheet slot —
     * FUN_00499a1c(1), sticky no-op after the first call.  The title
     * flow loads system2.pl8 into the same slot; the in-game UI (the
     * event-scroll seal, cursors) indexes the system.pl8 image. */
    if (g_engine_state == 3) {
        gfx_load_system_sheet_select(1);     /* FUN_00499a1c(1) */
    }

    /* Pre-tick block — retail does (in order): FUN_004b0aaa input
     * poll, FUN_00498458, FUN_004b20ed (← ported), FUN_004bbc80
     * (← ported), FUN_00481b9a,
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
         *   - screen-state-specific 0x40490d / 0x49a010 /
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
        if (sim_player_human_active() != 0) {
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
            /* The substate-0 trio, in retail order. */
            engine_tick_cadence_step(&g_tick_cadence);    /* 0x40490d */
            engine_turn_tick_substate0();                 /* 0x49a010 */
            engine_army_state_run(&g_turn_event_queue,    /* 0x4650b0 */
                                  g_sim_mp_active);
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

    /* Title-menu file-load confirm pump (retail FUN_004ad9f0, run from
     * inside the menu dispatcher FUN_004ba9c8).  Self-gates on the
     * page-3 Load request latch + its confirm timer; a no-op until the
     * Load button latches a request.  When the timer expires on the
     * title screen it performs the load (bound hook → save_load +
     * engine_map_post_load, which sets g_sim_current_screen = 0) and
     * transitions the engine to ingame, so the render step below paints
     * the strategic map this frame. */
    engine_load_pump_tick();

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

    /* Help-hint pump (retail FUN_00476aa7, tick line 104 — immediately
     * before the consumer).  First-time-per-session tutorial scrolls:
     * posts the per-screen hint event + flips to the 0x27 report
     * screen.  On the load route this posts hint 0xc8 (strategic-
     * screen welcome) ~20 frames after the flip — the "mid-idle
     * second event" the state oracle sees in ring record 1. */
    engine_tick_help_hint_step();

    /* In-game event-scroll consumer (retail FUN_00472e46).  Retail
     * frame order: the demand-redraw seam (0x40f1a0, line 36) and the
     * map-scene render (0x4050c0, line 42) run FIRST and the consumer
     * at line 105 — an open scroll paints OVER the freshly-rendered
     * frame.  Dequeues posted events into the modal scroll state —
     * opening one parks its code in extended_pause, freezing the SP
     * sim until dismissed — and renders the open scroll every frame.
     * No-op off the scroll-capable screens. */
    engine_tick_event_popup_step();

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
