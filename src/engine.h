#ifndef OPENLORDS2_ENGINE_H
#define OPENLORDS2_ENGINE_H

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
 * + the rate-gated present call (gfx_tick_present).  Each of the
 * ~40 subsystem calls inside retail's FUN_004b99c0 lands in its own
 * slice as the corresponding subsystem (sim, music, input, UI…)
 * gets ported.
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

#endif /* OPENLORDS2_ENGINE_H */
