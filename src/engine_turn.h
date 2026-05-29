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

#endif /* OPENLORDS2_ENGINE_TURN_H */
