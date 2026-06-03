/*
 * OpenLords2 — end-of-turn readiness check (interface).
 *
 * Pure-C port of retail FUN_0049b762 @ 0x49b762 (334 B): the
 * "is the turn fully resolved, may we advance?" predicate the
 * strategic-turn driver (retail 0x49a010, unported) polls once
 * per pump iteration — `if (engine_sim_turn_resolved()) advance;`.
 *
 * Two behaviours folded into one function:
 *
 *   1. Readiness scan.  Counts the human/active players (roster
 *      slots 1..5) still mid-turn — active byte set AND their
 *      ready-value (the int at slot+0) below the 999 "done" sentinel.
 *      The count lands in the save-schema scalar s_slot_00554004
 *      (retail DAT_00554004).  Non-zero → someone's still going →
 *      don't advance.
 *
 *   2. When everyone's done (count == 0):
 *        - single-player (mp_active == 0)  → advance now.
 *        - MP with exactly one active peer → advance now.
 *        - otherwise run the MP turn-sync handshake, a three-state
 *          machine on retail DAT_00553fc8 (g_sim_turn_progress):
 *            state 0 → arm: clear the 10 per-player ready flags,
 *                      snapshot the turn-timeout flag, set state 1,
 *                      arm the 10 s sync timer to fire immediately,
 *                      return "not yet".
 *            state 1 → still waiting → return "not yet".
 *            state 2 → peers acknowledged → reset to 0, advance.
 *
 * Pure function: every input explicit, every state mutation returned
 * in the output struct.  timeGetTime + the live globals are bundled
 * by the Win32 wrapper engine_sim_turn_resolved() in engine.c.
 *
 * Reference: docs/findings/main-loop-tick.md §"FUN_0049b762".
 */
#ifndef OPENLORDS2_ENGINE_TURN_H
#define OPENLORDS2_ENGINE_TURN_H

#include <stdint.h>

#include "engine_event_queue.h"   /* engine_event_queue_state_t */

typedef struct {
    uint32_t       now_ms;            /* timeGetTime() at call site */
    int            mp_active;         /* DAT_00553030 — MP session flag */
    int            active_count;      /* DAT_0053f068 — # active players */
    int            turn_progress;     /* DAT_00553fc8 — handshake state 0/1/2 */
    int            timeout_reached;   /* DAT_00553018 — turn-timer expired flag */
    int            saved_timeout;     /* DAT_0055ce88 — carry-through only */
    uint32_t       sync_baseline_ms;  /* _DAT_0056d658 — carry-through only */
    /* Per-player strategic records, retail 0x57bf00, stride 0x160.
     * Read fields: ready-value (int @ slot+0), active byte (@ slot+4).
     * Only slots 1..5 are scanned. */
    const uint8_t *players;
} engine_turn_end_in_t;

typedef struct {
    int      advance;             /* return value: 1 = advance turn, 0 = wait */
    int      not_ready_count;     /* writeback → DAT_00554004 (always written) */
    int      new_turn_progress;   /* writeback → DAT_00553fc8 */
    int      new_timeout_reached; /* writeback → DAT_00553018 */
    int      saved_timeout;       /* writeback → DAT_0055ce88 */
    uint32_t new_sync_baseline_ms;/* writeback → _DAT_0056d658 */
    int      clear_ready_flags;   /* 1 → wrapper zeroes the 10 ready flags
                                   *     (retail DAT_00515d60[0..9]) */
} engine_turn_end_out_t;

void engine_turn_end_check(const engine_turn_end_in_t *in,
                           engine_turn_end_out_t *out);

/* Calendar / season-rollover advance — the inline arithmetic block of
 * the turn-advance engine FUN_00448440 @ 0x448440 (it sits near the top
 * of the per-turn sequence, after the first two AI calls and before the
 * 28 economy / realm passes).  Operates in place on the save-schema
 * calendar slots:
 *
 *   - turn counter  s_slot_00553240 (entry 31)            ++ every turn
 *   - season triple s_slot_0057c934/00554470/0057c92c     (cur/prev/next)
 *       rotate: prev = cur; cur = next; next++ ; wrap next 1..4
 *   - on the season wrap point (the just-consumed `next` was 4) a YEAR
 *     rollover fires: s_slot_0055cd5c (entry 30) ++, the year triple
 *       s_slot_00553edc/0057c900/00553e74 (cur/prev/next) rotates the
 *       same way (no wrap), and the seasonal CD-audio probe
 *       io_path_probe_cd_audio (retail FUN_004aef7e) runs.
 *
 * The season/year *copy* slots (0057c990 / 00553228) are seed-only and
 * untouched here.  Mutates globals in place (not the pure-struct style
 * of engine_turn_end_check) because retail does — this is a save-state
 * step, like the engine_realm turn passes. */
void engine_turn_advance_calendar(void);

/* Empty per-turn hook — port of retail FUN_00428471 @ 0x428471, an
 * 11-byte stub (push/pop register-preserve prologue, then leave/ret)
 * with no body, called from the turn-advance engine 0x448440 between
 * the population-growth pass and the demolish/aftermath cluster.  A
 * compiled-out debug/profiling hook in the retail build; ported as a
 * no-op so the driver's call sequence stays complete and 0x428471 is
 * accounted for. */
void engine_turn_hook_noop(void);

/* ─── Full per-turn driver (assembled orchestrator) ─────────────────
 *
 * Host-side core of retail FUN_00448440 @ 0x448440 (261 B) — the per-turn
 * advance engine.  Runs the deterministic tail of the driver: the inline
 * calendar/season-rollover block followed by the 27 economy / realm /
 * territory passes, in retail's exact call order (see
 * docs/decompiled/by-address/448440.c).
 *
 * The two passes that head retail's sequence — the per-realm AI brain
 * (FUN_0049a990 = engine_army_ai_run_turn) and the RNG stir
 * (FUN_00404a46 = engine_rng_step) — are NOT run here.  They live in the
 * engine.c Win32 wrapper engine_turn_advance(), which owns the masked-RNG
 * global mirrors (g_rng_7bit_b/a) the stir produces and the AI consumes;
 * the wrapper runs AI + stir, then hands the freshly-stirred 7-bit bytes
 * to this function as rng_7bit_b / rng_7bit_a.  Splitting here keeps the
 * orchestrator host-testable / retail-diffable (engine.c is Win32-only,
 * excluded from the host suite and the diff .so) while the RNG-state
 * coupling stays where the live mirrors are.
 *
 * Retail threads the UI event queue, the MP flag, and the two cached RNG
 * bytes through file-scope globals; the ports thread them as explicit
 * params (queue posts go through evq; coalition/resign sweeps branch on
 * mp_active; roll-events + mood read the RNG bytes).  Pass the engine's
 * one canonical queue, g_sim_mp_active, and the post-stir RNG bytes.
 *
 * Mutates global save state in place (the passes are save-state steps).
 * Calendar parity through the full sequence is the retail-diff gate
 * (the `turn_driver` target reads back the 8 calendar scalars); the
 * per-pass economy effects hit the variable price-scalar wall and are
 * each verified by their own host tests / diff targets. */
void engine_turn_run_passes(engine_event_queue_state_t *evq,
                            uint32_t rng_7bit_b,
                            uint32_t rng_7bit_a,
                            int mp_active);

/* ─── Phase sequencer ───────────────────────────────────────────────
 *
 * The strategic turn runs as a 7-phase cycle (retail global DAT_00569584,
 * the save-schema scalar s_slot_00569584, values 1..7).  Each phase the
 * driver 0x49a010 (the SP per-tick screen-substate-0 turn driver, still
 * unported) does its phase-specific work, and when that phase resolves
 * it calls FUN_0049ce51 to step to the next phase.
 *
 * engine_turn_advance_phase — port of retail FUN_0049ce51 @ 0x49ce51
 * (109 B).  Steps the phase counter, wrapping 7→1; on the wrap (a fresh
 * full turn cycle beginning) it resets the per-realm turn roster via
 * engine_turn_reset_roster.  All state lives in save-schema scalars:
 *
 *   s_slot_0053f06c (prev phase)   ← s_slot_00569584 (current), saved first
 *   s_slot_00569584 (current)      ++ ; if > 7 → 1 (+ reset roster)
 *   s_slot_0053f678 (next phase)   = current + 1 ; if > 7 → 1  (lookahead)
 *   s_slot_0053f658 (phase tick)   = 0   (the per-phase pass counter the
 *                                          driver bumps each pass and gates
 *                                          its first-tick setup on == 1)
 *   s_slot_0057c944 (redraw cookie)= 1
 *
 * engine_turn_reset_roster — port of retail FUN_0049b6d3 @ 0x49b6d3
 * (143 B).  Called by the phase sequencer when the cycle wraps back to
 * phase 1.  Walks the per-realm strategic roster slots 1..5
 * (g_player_strategic_352, retail 0x57bf00, stride 0x160) and re-arms
 * each turn-progress field (int @ slot+0): an *active* realm (byte @
 * slot+4 != 0) is reset to 0 (mid-turn) and counted into the active
 * tally s_slot_00554004 (retail DAT_00554004); an *inactive* slot is
 * parked at the 999 "turn done" sentinel so the readiness scan
 * (engine_turn_end_check) skips it.  This is the producer that seeds the
 * exact field engine_turn_end_check consumes.
 *
 * Both mutate global save state in place (phase/roster are save-state),
 * like engine_turn_advance_calendar — not the pure-struct style of
 * engine_turn_end_check.
 *
 * Reference: docs/decompiled/by-address/{49ce51,49b6d3}.c. */
void engine_turn_advance_phase(void);
void engine_turn_reset_roster(void);

/* ─── Turn-handshake runtime state (canonical storage) ──────────────
 *
 * Retail DAT_00553fc8 / _DAT_0056d658.  NOT save-schema scalars — pure
 * runtime handshake state, defaulting to 0 at .bss-init and re-zeroed
 * at new-game by the scalar-seed phase of the initialiser
 * (engine_game_init_seed_scalars).  engine.c is the live reader/writer:
 * it marshals them into engine_turn_end_in_t each tick and writes the
 * result back.  Storage lives here, in the turn module, so both
 * engine.c and game_init.c reach them without either depending on the
 * other's translation unit.  No live producer drives them past 0 yet
 * (see the three-state handshake notes above). */
extern int      g_sim_turn_progress;         /* DAT_00553fc8 */
extern uint32_t g_sim_turn_sync_baseline_ms; /* _DAT_0056d658 */

#endif /* OPENLORDS2_ENGINE_TURN_H */
