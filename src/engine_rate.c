/*
 * Sim sub-step rate controller — pure-C port of retail FUN_004bb978
 * @ 0x4bb978 (614 bytes).
 *
 * Decides how many sim sub-steps to run on this Win32-pump iteration.
 * Every input is explicit (passed via engine_rate_in_t); every state
 * mutation is returned via engine_rate_out_t.  No global reads, no
 * timeGetTime, no Win32 anything — so the test harness compiles it
 * under host gcc + ASan/UBSan and exercises every branch.
 *
 * The Win32-coupled wrapper in engine.c (engine_tick_rate_step)
 * bundles the live globals into the input struct, calls this, then
 * writes the deltas back.
 *
 * Reference: docs/findings/main-loop-tick.md §`FUN_004bb978`.
 *
 * Retail global ↔ field mapping (mirrors are in engine.c):
 *
 *   _DAT_004e5b0c    in->last_step_ms / out->new_last_step_ms
 *   DAT_0056d8b4     in->autosave_start_ms / out->new_autosave_start_ms
 *   FUN_004bb8c0()   in->run_gate                    (today: returns 1)
 *   FUN_0043de70()   in->scenario_hint               (today: returns 0)
 *   DAT_00553030     in->mp_active                   (today: 0 — SP only)
 *   DAT_0057c998     in->sim_tick_counter
 *   _DAT_0053f230    in->game_speed
 *   DAT_0053f238     in->pause_lock
 *   DAT_004e31b0[]   in->screen_freeze (indexed by DAT_004eac50)
 *   DAT_0053e910     in->extended_pause
 *   DAT_004e3240[]   in->mp_skip_flag  (indexed by DAT_005440a4)
 *   DAT_004d2164     in->phase / out->new_phase
 *   DAT_004d215c     out->derived_hint               (write-only in retail too)
 *   DAT_004e3260[]   in->rate_lut (pointer; 11×0xa0 = 0x6e0 bytes
 *                                  documented, but the index can
 *                                  reach 14*0xa0+0x9f = 0x8df —
 *                                  481 B past the documented end.
 *                                  See engine-quirks for the OOB note.)
 *
 * Behaviour summary (the case matrix the tests cover):
 *
 *   run_gate == 0                                     → 0 sub-steps
 *   autosave window active (≤ 5000 ms since start)    → 0 sub-steps, no state change
 *   autosave window expired                           → clear it (+ notify if MP)
 *   scenario_hint == 0                                → 0 sub-steps
 *   scenario_hint < 0 + sim_tick < 100                → 1 sub-step (fast-skip warm-up)
 *   scenario_hint < 0 + sim_tick ≥ 100                → 2 sub-steps (fast-skip)
 *   scenario_hint > 0x8c                              → clamped to 0x8c
 *   LUT byte at (phase+1, hint/10) == 0               → 0 sub-steps
 *   pause_lock != 0                                   → 0 sub-steps
 *   screen_freeze == 2 + SP                           → 0 sub-steps, no state change
 *   screen_freeze != 0 && != 2                        → 0 sub-steps, no state change
 *   extended_pause + SP                               → 0 sub-steps, no state change
 *   extended_pause + MP + mp_skip_flag                → 0 sub-steps, no state change
 *   rate gate (slider-driven): now - last < hint*10+2 → 0 sub-steps, no advance
 *   rate gate passes                                  → 1 sub-step, advance last_step_ms
 *   MP at max speed (iVar3 == 0 && mp_active)         → 1 sub-step, no advance (uncapped)
 *
 * The state mutations DAT_004d2164 (phase) and DAT_004d215c
 * (derived hint) happen *before* the freeze/pause early-returns —
 * so even when the function returns 0, those two side-effects fire
 * provided we got past the scenario_hint == 0 check.  Tests verify
 * this ordering.
 */

#include "engine.h"

void engine_compute_sim_sub_steps(const engine_rate_in_t *in,
                                  engine_rate_out_t *out)
{
    /* Default outputs — no state change, 0 sub-steps.  Branches
     * below selectively overwrite.  Mirrors retail's locals-on-stack
     * pattern: `local_10` starts at 1 and DVar1 defaults to
     * `_DAT_004e5b0c` (no advance) unless a take-step branch sets it
     * to DVar2 (now). */
    out->sub_steps                = 1;
    out->new_last_step_ms         = in->last_step_ms;
    out->new_autosave_start_ms    = in->autosave_start_ms;
    out->new_phase                = in->phase;
    out->derived_hint             = 0;
    out->notify_autosave_done     = 0;

    int local_10 = 1;                    /* retail's local_10 — return value */
    uint32_t DVar1 = in->last_step_ms;   /* retail's DVar1 — writeback target */
    uint32_t DVar2 = in->now_ms;         /* retail's DVar2 — captured timeGetTime */

    if (in->run_gate == 0) {
        local_10 = 0;
        /* DVar1 stays at last_step_ms — no advance */
    } else {
        /* Autosave-window gate.  Retail special-cases this with two
         * early returns, neither of which writes back _DAT_004e5b0c
         * (we replicate that — the early `return` in retail bypasses
         * the function's tail `_DAT_004e5b0c = DVar1`). */
        if (in->autosave_start_ms != 0) {
            if ((int32_t)(DVar2 - in->autosave_start_ms) < 5000) {
                out->sub_steps = 0;
                /* No state writeback — return early. */
                return;
            }
            out->new_autosave_start_ms = 0;       /* expire the window */
            if (in->mp_active != 0) {
                out->notify_autosave_done = 1;    /* retail: FUN_0043eda0(0xf, 0) */
            }
        }

        int32_t local_8 = (int32_t)(DVar2 - in->last_step_ms);
        if (local_8 < 0) {
            /* `timeGetTime` overflow OR last_step_ms was in the
             * future (debug seeding).  Retail's catch-up sentinel. */
            local_8 = 10000;
        }

        int local_14 = in->scenario_hint;
        /* DVar1 reset here in retail (line 89449) — redundant since
         * we initialised it above; documented for parity. */
        DVar1 = in->last_step_ms;

        if (local_14 == 0) {
            local_10 = 0;
        } else {
            /* Fast-skip mode — negative hint encodes "run uncapped
             * until sim_tick ≥ 100, then 2 sub-steps". */
            if (local_14 < 0) {
                local_10 = (in->sim_tick_counter < 100) ? 1 : 2;
                local_14 = 0;
            }
            if (0x8c < local_14) {
                local_14 = 0x8c;
            }
            out->derived_hint = local_14;

            /* Phase counter advance — retail mod 0xa0 (160). */
            int new_phase = in->phase + 1;
            if (0x9f < new_phase) {
                new_phase = 0;
            }
            out->new_phase = new_phase;

            /* LUT lookup at [new_phase + (local_14/10) * 0xa0].
             * The 0xa0-stride table picks "should we step at this
             * 0.1× hint bucket × phase column" — non-zero means
             * "yes (proceed to rate gate)", zero means "no
             * (force-skip this iteration)".  TODO: extract the real
             * LUT bytes from retail (.data @ 0x4e3260, ≥0x6e0 bytes
             * documented but possibly OOB-read to 14×0xa0 = 0xc80 —
             * see engine-quirks).  Callers pass NULL to treat as
             * all-zeros (current drop-in). */
            uint8_t lut_byte = 0;
            if (in->rate_lut) {
                lut_byte = in->rate_lut[new_phase + (local_14 / 10) * 0xa0];
            }
            if (lut_byte == 0) {
                local_10 = 0;
            } else {
                /* Slider-driven rate gate.  `iVar3` is 0..9 where
                 * 0 = max speed, 9 = slow.  Anything ≥ 10 (slider
                 * pegged below 0) freezes the sim. */
                int iVar3 = (100 - in->game_speed) / 10;
                if (iVar3 < 10) {
                    if (in->pause_lock == 0) {
                        /* Per-screen freeze.  Value 2 only freezes
                         * in SP; any other non-zero freezes both. */
                        if (in->screen_freeze == 2) {
                            if (in->mp_active == 0) {
                                out->sub_steps = 0;
                                return;
                            }
                        } else if (in->screen_freeze != 0) {
                            out->sub_steps = 0;
                            return;
                        }
                        /* Extended-pause (e.g. modal dialog open).
                         * Freezes SP; in MP, also freezes if this
                         * player has their skip flag set. */
                        if (in->extended_pause != 0) {
                            if (in->mp_active == 0) {
                                out->sub_steps = 0;
                                return;
                            }
                            if (in->mp_skip_flag != 0) {
                                out->sub_steps = 0;
                                return;
                            }
                        }
                        /* Primary rate gate.  In MP at max speed
                         * (iVar3 == 0), step uncapped: skip the
                         * gate entirely, keep local_10 = 1, leave
                         * DVar1 at last_step_ms (no advance — so
                         * the next call sees the same baseline and
                         * also steps uncapped).
                         *
                         * Otherwise: tentatively advance DVar1 to
                         * now; if elapsed < hint*10+2, revert and
                         * skip the step. */
                        if (iVar3 != 0 || in->mp_active == 0) {
                            DVar1 = DVar2;        /* tentative advance */
                            if (local_8 < iVar3 * 10 + 2) {
                                local_10 = 0;
                                DVar1 = in->last_step_ms;  /* revert */
                            }
                        }
                        /* else: gate_condition false, no advance,
                         *        local_10 stays 1 (MP uncapped). */
                    } else {
                        local_10 = 0;            /* paused */
                    }
                } else {
                    local_10 = 0;                /* slider pegged slow */
                }
            }
        }
    }

    out->new_last_step_ms = DVar1;
    out->sub_steps        = local_10;
}
