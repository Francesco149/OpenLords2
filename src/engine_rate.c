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

/* ─── engine_compute_scenario_hint — port of FUN_0043de70 @ 0x43de70 ──
 *
 * Sub-step rate selector.  Retail flow recap:
 *
 *   1. engine state != 3 (ingame) → return 0.  Sub-step loop won't run.
 *   2. SP (mp_active == 0) → return 1.  Engine sub-steps freely.
 *   3. MP, local player slot inactive → return 1.  Same effect.
 *   4. MP, active.  Branch on count_active_allies:
 *      a. 2+ active allies (or not mode-2): "normal MP" path — compute
 *         my-lead-or-deficit vs slowest peer, pick from one of two
 *         ladders based on game_mode_speed (==5 fine, else coarse).
 *      b. <2 active allies in mode-2: "alone in mode-2" path — compute
 *         lead vs all-min, pick from a separate 4-step ladder floored
 *         at base_delta*3.
 *
 * The three retail helpers (FUN_0043e433, FUN_0043e330, FUN_0043e50d)
 * are tiny — inlined here as file-static helpers.
 */

/* Port of FUN_0043e433 @ 0x43e433 (218 B) — "few-allies" detector.
 * Returns 1 iff in mode-2 AND fewer than 2 of (ally_a, ally_b) are
 * active-eligible; else 0.  In any non-mode-2 mode the function
 * returns 0 outright (regardless of slot state).  Retail's iteration
 * walks slots 1..6 but the body's `slot == ally_a || slot == ally_b`
 * filter means only those two contribute. */
static int count_active_allies_low(const engine_scenario_hint_in_t *in)
{
    if (in->game_mode_speed != 2) {
        return 0;
    }
    int count = 0;
    for (int i = 1; i < 7; i++) {
        if (in->player_active[i] != 0 &&
            in->player_has_data[i] != 0 &&
            in->player_disconnected[i] == 0 &&
            (i == in->ally_a || i == in->ally_b)) {
            count++;
        }
    }
    return (count < 2) ? 1 : 0;
}

/* Port of FUN_0043e330 @ 0x43e330 (259 B) — min-tick + active-count
 * walker.  Resets active_count, then walks slots 1..6:
 *   - Every active-eligible slot bumps active_count.
 *   - Additionally, slots matching the (exclude_slot, mode-2 ally)
 *     filter contribute their player_ticks[i] to the running min.
 *
 * Returns sentinel 10000000 if no eligible slot is found (retail
 * relies on this as a "no peer to compare against" indicator —
 * `my_ticks - 10000000 = large negative = catch-up branch`). */
static int min_tick_filtered(const engine_scenario_hint_in_t *in,
                             int exclude_slot,
                             int *out_active_count)
{
    int min_tick = 10000000;
    int active_count = 0;
    for (int i = 1; i < 7; i++) {
        if (in->player_active[i] != 0 &&
            in->player_has_data[i] != 0 &&
            in->player_disconnected[i] == 0) {
            active_count++;
            if (i != exclude_slot &&
                (in->game_mode_speed != 2 ||
                 i == in->ally_a || i == in->ally_b)) {
                if (in->player_ticks[i] < min_tick) {
                    min_tick = in->player_ticks[i];
                }
            }
        }
    }
    *out_active_count = active_count;
    return min_tick;
}

/* Port of FUN_0043e50d @ 0x43e50d (180 B) — like min_tick_filtered
 * but without the exclude_slot OR the mode-2 ally filter.  Just the
 * unconditional min over all active-eligible slots.  Sentinel 10000000
 * for no-peer.  Used by the "alone-in-mode-2" path. */
static int min_tick_all(const engine_scenario_hint_in_t *in)
{
    int min_tick = 10000000;
    for (int i = 1; i < 7; i++) {
        if (in->player_active[i] != 0 &&
            in->player_has_data[i] != 0 &&
            in->player_disconnected[i] == 0) {
            if (in->player_ticks[i] < min_tick) {
                min_tick = in->player_ticks[i];
            }
        }
    }
    return min_tick;
}

/* "Ahead" ladders.  Both pick a ms-per-step bucket from `delta` (how
 * many ticks I'm ahead).  Retail's nested if/else chains are linearised
 * into ascending threshold ladders here — easier to audit. */
static int ahead_ladder_mode5(int delta)
{
    /* Fine-grained 13-step ladder.  Buckets (delta range → hint):
     *   [0..9]     → 10        [10..29]   → 20        [30..49]  → 30
     *   [50..89]   → 40        [90..100]  → 50        [101..129] → 60
     *   [130..169] → 70        [170..209] → 80        [210..309] → 90
     *   [310..409] → 100       [410..509] → 110       [510..609] → 120
     *   [610..709] → 130       [710..∞]   → 140 (cap) */
    if (delta >= 0x2c6) return 0x8c;   /* >= 710 → 140 */
    if (delta < 10)     return 10;
    if (delta < 0x1e)   return 0x14;
    if (delta < 0x32)   return 0x1e;
    if (delta < 0x5a)   return 0x28;
    if (delta < 0x65)   return 0x32;
    if (delta < 0x82)   return 0x3c;
    if (delta < 0xaa)   return 0x46;
    if (delta < 0xd2)   return 0x50;
    if (delta < 0x136)  return 0x5a;
    if (delta < 0x19a)  return 100;
    if (delta < 0x1fe)  return 0x6e;
    if (delta < 0x262)  return 0x78;
    return 0x82;                        /* [610..709] → 130 */
}

static int ahead_ladder_coarse(int delta)
{
    /* Coarse 8-step ladder.  Used for any game_mode_speed != 5.
     * Buckets (delta range → hint):
     *   [0..9]     → 20        [10..19]   → 40        [20..29]  → 60
     *   [30..39]   → 80        [40..49]   → 100       [50..59]  → 110
     *   [60..200]  → 120       [201..400] → 130       [401..∞]  → 140 (cap) */
    if (delta >= 0x191) return 0x8c;   /* >= 401 → 140 */
    if (delta >= 0xc9)  return 0x82;   /* [201..400] → 130 */
    if (delta >= 0x3c)  return 0x78;   /* [60..200] → 120 */
    if (delta < 10)     return 0x14;
    if (delta < 0x14)   return 0x28;
    if (delta < 0x1e)   return 0x3c;
    if (delta < 0x28)   return 0x50;
    if (delta < 0x32)   return 100;
    return 0x6e;                        /* [50..59] → 110 */
}

/* "Behind" ladder.  delta = peers_ahead_of_me (positive).  Returns the
 * negative fast-skip code that sim_sub_steps interprets. */
static int behind_ladder(int behind)
{
    if (behind < 0x14)    return -2;
    if (behind < 0x32)    return -3;
    if (behind < 100)     return -4;
    if (behind < 200)     return -5;
    if (behind < 400)     return -6;
    if (behind < 1000)    return -7;
    if (behind < 10000)   return -8;
    return -10;                         /* >= 10000 → -10 (deep stall) */
}

/* "Alone-in-mode-2" ladder.  4-step + sentinel for very-behind.
 * Returns 0xffffffff (-1) for "I'm not ahead at all" sentinel, else a
 * positive ms-per-step (floored at base_delta*3 by the caller). */
static int alone_ladder(int delta)
{
    if (delta < 1)        return -1;    /* sentinel, caller short-circuits */
    if (delta < 0x33)     return 1;
    if (delta < 0x191)    return 0x1e;
    if (delta < 0x321)    return 0x46;
    return 0x5a;
}

void engine_compute_scenario_hint(const engine_scenario_hint_in_t *in,
                                  engine_scenario_hint_out_t *out)
{
    /* Default outputs — carry through everything unchanged.  The
     * branches below overwrite selectively to match retail's per-path
     * side effects. */
    out->hint                 = 0;
    out->new_frame_count      = in->frame_count;
    out->new_max_stride       = in->max_stride;
    out->new_stride_sum       = in->stride_sum;
    out->new_avg_stride       = in->avg_stride;
    out->new_slow_frame_count = in->slow_frame_count;
    out->new_active_count     = in->active_count;
    out->notify_slow_frame    = 0;

    /* Gate 1: engine state must be ingame (==3).  Otherwise return 0
     * (the global remains, no other writes). */
    if (in->game_state_phase != 3) {
        return;
    }

    /* Gate 2: SP → trivial return 1.  No other writes. */
    if (in->mp_active == 0) {
        out->hint = 1;
        return;
    }

    /* Gate 3: MP but local player slot has no data → return 1.
     * Retail reads `(&DAT_0057bf04)[DAT_0057c8cc * 0x160]` — that's
     * the byte at slot's active-flag position. */
    int slot = in->local_player_idx;
    if (slot < 1 || slot > 6 || in->player_active[slot] == 0) {
        out->hint = 1;
        return;
    }

    /* Gate 4: alone-in-mode-2 path branches off FUN_0043e433.  Returns
     * 1 iff mode==2 and 0 or 1 active ally — i.e., the local player
     * has no MP peer to sync with through the mode-2 ally filter. */
    if (count_active_allies_low(in)) {
        /* Alone-in-mode-2 path.  Uses the unfiltered min-tick walker
         * (FUN_0043e50d) — doesn't write active_count.  Catch-up
         * sentinel (-1) returns early. */
        int peer_min = min_tick_all(in);
        int delta    = in->player_ticks[slot] - peer_min;

        int hint = alone_ladder(delta);
        if (hint == -1) {
            out->hint = -1;
            return;
        }
        /* Floor at base_delta * 3.  Retail compares as signed —
         * negative base_delta*3 would underfloor.  We mirror exactly. */
        if (hint < in->base_delta * 3) {
            hint = in->base_delta * 3;
        }
        out->hint = hint;
        return;
    }

    /* Normal MP path.  First call to min_tick_filtered sets the active
     * count via the side-channel out param.  Single-active short-
     * circuits to 1 (only me here, no point throttling). */
    int active_count = 0;
    int peer_min_inc = min_tick_filtered(in, 0, &active_count);
    out->new_active_count = active_count;

    if (active_count == 1) {
        out->hint = 1;
        return;
    }

    /* Bump frame_count BEFORE the lead/deficit branch (retail does the
     * same — and it's load-bearing for the avg_stride writeback gate). */
    out->new_frame_count = in->frame_count + 1;

    int my_ticks = in->player_ticks[slot];
    int lead     = my_ticks - peer_min_inc;

    if (lead < 1) {
        /* Catch-up: compute peers-ahead-of-me (with self excluded so a
         * dropped-out self doesn't skew the min) and pick from the
         * negative fast-skip ladder.  No writes to max/sum/avg/slow —
         * those carry through. */
        int active_excl = 0;
        int peer_min_excl = min_tick_filtered(in, slot, &active_excl);
        int behind = peer_min_excl - my_ticks;
        out->hint  = behind_ladder(behind);
        return;
    }

    /* Throttle: pick from the ahead ladder, then update stride
     * metrics + slow_frame_count book-keeping. */
    int hint = (in->game_mode_speed == 5) ? ahead_ladder_mode5(lead)
                                          : ahead_ladder_coarse(lead);

    if (in->max_stride < lead) {
        out->new_max_stride = lead;
    }
    out->new_stride_sum = in->stride_sum + lead;
    /* `if (DAT_00515b20 != 0)` post-bump — frame_count+1 is 0 only if
     * the addend overflowed (the diff orchestrator's vectors don't go
     * anywhere near INT_MAX, so we treat this as always-true in
     * practice; but the gate matches retail). */
    if (out->new_frame_count != 0) {
        out->new_avg_stride = out->new_stride_sum / out->new_frame_count;
    }

    if (hint < 0x8c) {
        /* Not at the 140 ms ceiling — reset the slow-frame run length. */
        out->new_slow_frame_count = 0;
    } else {
        /* Bumped slow frame.  Retail fires FUN_0043eda0(10, 0) when
         * the post-bump value exceeds 100 (gate is `100 < value`, so
         * a starting 100 → bumped to 101 → fires).  Diff vectors are
         * constrained `slow_frame_count ≤ 99` so this stays unfired
         * retail-side, but the port still computes the trigger so
         * unit tests cover it. */
        int new_slow = in->slow_frame_count + 1;
        if (new_slow > 100) {
            out->notify_slow_frame    = 1;
            out->new_slow_frame_count = 0;     /* retail resets after fire */
        } else {
            out->new_slow_frame_count = new_slow;
        }
    }

    /* Final hint = base_delta + ladder bucket.  base_delta is the
     * scenario's tick budget; adding it gives the "ms until I should
     * step again given my current lead". */
    out->hint = in->base_delta + hint;
}
