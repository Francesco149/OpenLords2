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

#endif /* OPENLORDS2_ENGINE_H */
