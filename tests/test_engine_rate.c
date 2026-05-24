/*
 * test_engine_rate.c — coverage for engine_compute_sim_sub_steps,
 * the pure-C port of retail FUN_004bb978 (sim sub-step rate
 * controller).
 *
 * Each test sets up an engine_rate_in_t with the case-specific
 * inputs (defaults filled in by `default_in()`), runs the function,
 * and asserts on both the return value (sub_steps) and the writeback
 * struct.  No globals, no time mocking — the function is pure so
 * tests are deterministic.
 *
 * The retail-global ↔ field mapping lives in engine_rate.c next to
 * the implementation; cases here are named for the BEHAVIOUR they
 * cover, not the global they manipulate.
 */

#include "t.h"
#include "engine.h"

#include <string.h>

/* "Vanilla ingame" input — all gates open, sim should step at 1
 * per call once the rate gate allows.  Tests override individual
 * fields. */
static void default_in(engine_rate_in_t *in)
{
    memset(in, 0, sizeof(*in));
    in->now_ms             = 1000;
    in->last_step_ms       = 0;
    in->autosave_start_ms  = 0;        /* no autosave window */
    in->run_gate           = 1;
    in->scenario_hint      = 1;        /* "step at 1 ms-bucket" */
    in->mp_active          = 0;
    in->sim_tick_counter   = 0;
    in->game_speed         = 100;      /* max speed → iVar3 = 0 */
    in->pause_lock         = 0;
    in->screen_freeze      = 0;
    in->extended_pause     = 0;
    in->mp_skip_flag       = 0;
    in->phase              = 0;
    in->rate_lut           = NULL;     /* default: all-zeros → forces 0 */
}

/* Convenience — single-byte "yes-step" LUT so tests that want to
 * reach past the LUT==0 gate can do so without supplying 1760 bytes.
 *
 * Index used inside the function is `(phase+1) + (clamped_hint/10) * 0xa0`.
 * For phase=0, hint=1 → index 1 + 0 = 1.  So a 2-byte LUT [0,1]
 * suffices for the common "phase=0, hint=1" tests.
 *
 * For tests that exercise larger hints, a per-test LUT is built. */
static uint8_t k_lut_step_at_phase1[2] = { 0, 1 };

/* ─── run_gate ────────────────────────────────────────────────── */

int test_engine_rate_gate_zero_returns_zero(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.run_gate = 0;

    engine_compute_sim_sub_steps(&in, &out);

    T_ASSERT_EQ_I(out.sub_steps, 0);
    /* No state changes when gate is closed. */
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    T_ASSERT_EQ_U(out.new_autosave_start_ms, in.autosave_start_ms);
    T_ASSERT_EQ_I(out.new_phase, in.phase);
    T_ASSERT_EQ_I(out.derived_hint, 0);
    T_ASSERT_EQ_I(out.notify_autosave_done, 0);
    return 0;
}

/* ─── autosave window ───────────────────────────────────────────── */

int test_engine_rate_autosave_window_active_returns_zero_no_change(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.autosave_start_ms = 500;   /* started 500 ms ago (now=1000) */
    in.now_ms            = 1000;

    engine_compute_sim_sub_steps(&in, &out);

    T_ASSERT_EQ_I(out.sub_steps, 0);
    /* Crucial: window-active path is an early `return 0` in retail
     * that bypasses _DAT_004e5b0c writeback AND leaves the window
     * timestamp intact. */
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    T_ASSERT_EQ_U(out.new_autosave_start_ms, in.autosave_start_ms);
    T_ASSERT_EQ_I(out.new_phase, in.phase);
    T_ASSERT_EQ_I(out.notify_autosave_done, 0);
    return 0;
}

int test_engine_rate_autosave_window_boundary_4999ms_still_blocks(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.autosave_start_ms = 1;
    in.now_ms            = 4999;       /* exactly 4998 ms after start */

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    T_ASSERT_EQ_U(out.new_autosave_start_ms, 1);   /* still active */
    return 0;
}

int test_engine_rate_autosave_window_boundary_5000ms_expires(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.autosave_start_ms = 0xffffffff;     /* note: 0 means "no window" so
                                            * we need a non-zero start */
    in.now_ms            = 5000 - 1;        /* (now - start) = 5001 (uint wrap) ≥ 5000 */
    /* The comparison `(int)(now - start) < 5000` evaluates the diff
     * as signed.  With start = 0xffffffff and now = 4999,
     * now - start = 0x5000 = 5000.  Signed: 5000.  Not < 5000 →
     * window expires. */

    engine_compute_sim_sub_steps(&in, &out);
    /* Window cleared, MP-skip notification not fired (mp_active=0). */
    T_ASSERT_EQ_U(out.new_autosave_start_ms, 0);
    T_ASSERT_EQ_I(out.notify_autosave_done, 0);
    return 0;
}

int test_engine_rate_autosave_expiry_in_mp_fires_notification(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.autosave_start_ms = 100;
    in.now_ms            = 100 + 5000;     /* exactly at boundary, expires */
    in.mp_active         = 1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_U(out.new_autosave_start_ms, 0);
    T_ASSERT_EQ_I(out.notify_autosave_done, 1);
    return 0;
}

/* ─── scenario_hint ────────────────────────────────────────────── */

int test_engine_rate_scenario_hint_zero_returns_zero(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 0;
    in.last_step_ms  = 200;
    in.now_ms        = 1000;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    /* When scenario_hint == 0 the function reaches the
     * `_DAT_004e5b0c = DVar1` tail with DVar1 = in.last_step_ms.
     * So writeback IS performed but the value is unchanged. */
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    /* Phase NOT advanced — scenario_hint == 0 branches above the
     * phase-counter mutation. */
    T_ASSERT_EQ_I(out.new_phase, in.phase);
    T_ASSERT_EQ_I(out.derived_hint, 0);
    return 0;
}

int test_engine_rate_fast_skip_warmup_returns_one(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint    = -1;                   /* fast-skip */
    in.sim_tick_counter = 0;                    /* < 100 → warm-up */
    in.rate_lut         = k_lut_step_at_phase1; /* allow LUT-gate */

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 1);
    /* Phase did advance and was written back. */
    T_ASSERT_EQ_I(out.new_phase, 1);
    /* derived_hint set to 0 (negative path zeroes local_14). */
    T_ASSERT_EQ_I(out.derived_hint, 0);
    return 0;
}

int test_engine_rate_fast_skip_cruise_returns_two(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint    = -5;                   /* fast-skip */
    in.sim_tick_counter = 100;                  /* ≥ 100 → cruise */
    in.rate_lut         = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 2);
    return 0;
}

int test_engine_rate_hint_clamped_to_0x8c(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 0x1000;                  /* way over the clamp */
    /* Build a LUT big enough to hold the index for clamped hint=0x8c:
     *   index = (phase+1) + (0x8c/10) * 0xa0 = 1 + 14 * 0xa0 = 0x8c1.
     * That's outside the documented 11×0xa0 = 0x6e0 size — see the
     * engine-quirks OOB note.  Allocate the full extent so the test
     * is well-defined; assert the LUT byte the function reads is the
     * one at index 0x8c1. */
    static uint8_t lut_big[0x8c2];
    memset(lut_big, 0, sizeof(lut_big));
    lut_big[0x8c1] = 1;
    in.rate_lut = lut_big;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.derived_hint, 0x8c);
    /* hint > 0 → rate-gate kicks in.  At iVar3 = 0 (game_speed=100),
     * SP, the gate condition is true.  Then DVar1 = now (advance);
     * if local_8 < 2, revert + return 0.  local_8 = now - last = 1000.
     * 1000 >= 2 → step accepted. */
    T_ASSERT_EQ_I(out.sub_steps, 1);
    T_ASSERT_EQ_U(out.new_last_step_ms, in.now_ms);
    return 0;
}

/* ─── LUT gate ──────────────────────────────────────────────────── */

int test_engine_rate_lut_zero_blocks_step(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    /* rate_lut NULL → treated as all-zeros (drop-in default). */

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    /* Phase still advanced — phase update happens before LUT check. */
    T_ASSERT_EQ_I(out.new_phase, 1);
    T_ASSERT_EQ_I(out.derived_hint, 1);
    return 0;
}

/* ─── pause_lock ────────────────────────────────────────────────── */

int test_engine_rate_pause_lock_blocks_step(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.pause_lock    = 1;
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    /* Phase + derived_hint still updated (mutations before pause check). */
    T_ASSERT_EQ_I(out.new_phase, 1);
    T_ASSERT_EQ_I(out.derived_hint, 1);
    /* No advance — pause path stays at DVar1 = last_step_ms. */
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    return 0;
}

/* ─── screen freeze ─────────────────────────────────────────────── */

int test_engine_rate_screen_freeze_2_blocks_sp(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint  = 1;
    in.screen_freeze  = 2;
    in.mp_active      = 0;          /* SP */
    in.rate_lut       = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    /* Early return — no last_step_ms writeback.  But phase already
     * advanced (mutation precedes the early return). */
    T_ASSERT_EQ_I(out.new_phase, 1);
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    return 0;
}

int test_engine_rate_screen_freeze_2_passes_mp(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.screen_freeze = 2;
    in.mp_active     = 1;             /* freeze 2 only blocks SP */
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    /* In MP at max speed, gate_condition is false → no advance,
     * but local_10 stays 1 → step accepted. */
    T_ASSERT_EQ_I(out.sub_steps, 1);
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    return 0;
}

int test_engine_rate_screen_freeze_1_blocks_both(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.screen_freeze = 1;            /* non-2, non-0 → blocks both SP and MP */
    in.mp_active     = 1;
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    return 0;
}

/* ─── extended_pause ───────────────────────────────────────────── */

int test_engine_rate_extended_pause_sp_blocks(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint   = 1;
    in.extended_pause  = 1;
    in.mp_active       = 0;
    in.rate_lut        = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    return 0;
}

int test_engine_rate_extended_pause_mp_skip_flag_blocks(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint   = 1;
    in.extended_pause  = 1;
    in.mp_active       = 1;
    in.mp_skip_flag    = 1;          /* this player has skip flag set */
    in.rate_lut        = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    return 0;
}

int test_engine_rate_extended_pause_mp_no_skip_proceeds(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint   = 1;
    in.extended_pause  = 1;
    in.mp_active       = 1;
    in.mp_skip_flag    = 0;          /* no skip → MP at max speed proceeds */
    in.rate_lut        = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    /* In MP at max speed, gate skipped → step accepted, no advance. */
    T_ASSERT_EQ_I(out.sub_steps, 1);
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    return 0;
}

/* ─── slider-driven rate gate ───────────────────────────────────── */

int test_engine_rate_slider_max_slow_blocks(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.game_speed    = 0;            /* iVar3 = (100-0)/10 = 10 ≥ 10 → block */
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    return 0;
}

int test_engine_rate_slider_threshold_just_below_blocks(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.game_speed    = 50;           /* iVar3 = 5 — gate threshold = 5*10+2 = 52 */
    in.last_step_ms  = 0;
    in.now_ms        = 51;           /* local_8 = 51 < 52 → block, revert advance */
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 0);
    /* DVar1 was tentatively advanced to now then reverted. */
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    return 0;
}

int test_engine_rate_slider_threshold_at_passes(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.game_speed    = 50;           /* iVar3 = 5 — threshold = 52 */
    in.last_step_ms  = 0;
    in.now_ms        = 52;           /* local_8 = 52, NOT < 52 → step accepted */
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.sub_steps, 1);
    T_ASSERT_EQ_U(out.new_last_step_ms, in.now_ms);
    return 0;
}

int test_engine_rate_mp_max_speed_uncapped(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.game_speed    = 100;          /* iVar3 = 0 */
    in.mp_active     = 1;
    in.last_step_ms  = 0;
    in.now_ms        = 0;            /* local_8 = 0 — would normally block at threshold=2 */
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    /* Gate condition `(iVar3 != 0 || mp_active == 0)` is false →
     * comma-block never runs → local_10 stays 1, DVar1 stays at
     * last_step_ms. */
    T_ASSERT_EQ_I(out.sub_steps, 1);
    T_ASSERT_EQ_U(out.new_last_step_ms, in.last_step_ms);
    return 0;
}

/* ─── phase wrap ───────────────────────────────────────────────── */

int test_engine_rate_phase_wraps_at_0xa0(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.phase         = 0x9f;          /* will increment to 0xa0, then wrap to 0 */

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.new_phase, 0);
    return 0;
}

int test_engine_rate_phase_no_wrap_at_0x9e(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.phase         = 0x9e;          /* 0x9e + 1 = 0x9f — NOT > 0x9f */

    engine_compute_sim_sub_steps(&in, &out);
    T_ASSERT_EQ_I(out.new_phase, 0x9f);
    return 0;
}

/* ─── time-overflow sentinel ────────────────────────────────────── */

int test_engine_rate_time_overflow_local_8_clamps_to_10000(void)
{
    engine_rate_in_t  in;
    engine_rate_out_t out;
    default_in(&in);
    in.scenario_hint = 1;
    in.last_step_ms  = 1000;
    in.now_ms        = 500;          /* now < last_step → negative diff */
    in.game_speed    = 100;          /* iVar3 = 0, threshold = 2 — anything ≥ 2 passes */
    in.rate_lut      = k_lut_step_at_phase1;

    engine_compute_sim_sub_steps(&in, &out);
    /* local_8 clamped to 10000 ≥ 2 → step accepted. */
    T_ASSERT_EQ_I(out.sub_steps, 1);
    return 0;
}

/* ─── engine_compute_scenario_hint (port of FUN_0043de70) ──────────
 *
 * Per-branch coverage of the MP rate selector.  Helpers + ladders are
 * exercised through their callers.  Defaults: ingame-MP with slot 1
 * active + has-data + not disconnected; ally slots 2 and 3 active to
 * satisfy mode-2; tick counters at 0 (so my_ticks == peer_min by
 * default → catch-up branch via `lead < 1`).
 */

static void scenario_default_in(engine_scenario_hint_in_t *in)
{
    memset(in, 0, sizeof(*in));
    in->game_state_phase = 3;        /* ingame */
    in->mp_active        = 1;
    in->local_player_idx = 1;
    in->game_mode_speed  = 0;        /* coarse ladder */
    in->ally_a           = 2;
    in->ally_b           = 3;
    in->frame_count      = 0;
    in->max_stride       = 0;
    in->stride_sum       = 0;
    in->avg_stride       = 0xCAFEBABE;  /* canary — carries through unchanged */
    in->slow_frame_count = 0;
    in->base_delta       = 5;
    in->active_count     = 0xDEADBEEF;  /* canary — overwritten in normal path */
    in->player_active[1]       = 1; in->player_has_data[1] = 1;
    in->player_active[2]       = 1; in->player_has_data[2] = 1;
    in->player_active[3]       = 1; in->player_has_data[3] = 1;
}

int test_engine_scenario_hint_state_not_ingame_returns_zero(void)
{
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.game_state_phase = 0;     /* menu state */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 0);
    /* No globals written — every output equals carry-through. */
    T_ASSERT_EQ_U((uint32_t)out.new_active_count, 0xDEADBEEF);
    T_ASSERT_EQ_U((uint32_t)out.new_avg_stride,   0xCAFEBABE);
    return 0;
}

int test_engine_scenario_hint_sp_returns_one(void)
{
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.mp_active = 0;
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 1);
    /* SP path — no stride updates, no active_count write. */
    T_ASSERT_EQ_U((uint32_t)out.new_active_count, 0xDEADBEEF);
    return 0;
}

int test_engine_scenario_hint_local_player_inactive_returns_one(void)
{
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.player_active[1] = 0;       /* my slot inactive */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 1);
    return 0;
}

int test_engine_scenario_hint_invalid_slot_returns_one(void)
{
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.local_player_idx = 7;       /* out of range */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 1);
    return 0;
}

int test_engine_scenario_hint_single_active_player_returns_one(void)
{
    /* Only slot 1 (me) is active.  active_count = 1 → return 1.
     * Stride metrics not updated (we short-circuited before the ++). */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.player_active[2] = 0;
    in.player_has_data[2] = 0;
    in.player_active[3] = 0;
    in.player_has_data[3] = 0;
    in.frame_count = 100;
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 1);
    T_ASSERT_EQ_I(out.new_active_count, 1);
    T_ASSERT_EQ_I(out.new_frame_count, 100);  /* NOT bumped */
    return 0;
}

int test_engine_scenario_hint_catch_up_small_behind_returns_neg2(void)
{
    /* I'm 10 ticks behind a single peer.  behind < 0x14 → hint = -2.
     * Stride metrics NOT updated (catch-up branch). */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.player_ticks[1] = 0;
    in.player_ticks[2] = 10;       /* peer 10 ahead */
    in.frame_count = 50;
    in.stride_sum  = 1000;
    in.max_stride  = 25;
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, -2);
    /* frame_count++ happened (before lead check), but stride/max/avg unchanged. */
    T_ASSERT_EQ_I(out.new_frame_count, 51);
    T_ASSERT_EQ_I(out.new_max_stride, 25);
    T_ASSERT_EQ_I(out.new_stride_sum, 1000);
    T_ASSERT_EQ_U((uint32_t)out.new_avg_stride, 0xCAFEBABE);  /* carry-through */
    T_ASSERT_EQ_I(out.notify_slow_frame, 0);
    return 0;
}

int test_engine_scenario_hint_catch_up_ladder_boundaries(void)
{
    /* Sweep the -2..-10 ladder.  behind = peer_min_excl - my_ticks. */
    const struct { int behind; int expect; } cases[] = {
        {0,     -2},                      /* < 0x14 */
        {19,    -2},
        {20,    -3},                      /* >= 0x14 */
        {49,    -3},
        {50,    -4},                      /* >= 0x32 */
        {99,    -4},
        {100,   -5},                      /* >= 100 */
        {199,   -5},
        {200,   -6},                      /* >= 200 */
        {399,   -6},
        {400,   -7},                      /* >= 400 */
        {999,   -7},
        {1000,  -8},                      /* >= 1000 */
        {9999,  -8},
        {10000, -10},                     /* >= 10000 */
        {99999, -10},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        engine_scenario_hint_in_t  in;
        engine_scenario_hint_out_t out;
        scenario_default_in(&in);
        in.player_ticks[1] = 0;
        in.player_ticks[2] = cases[i].behind;
        in.player_ticks[3] = cases[i].behind;   /* same so min unchanged */
        engine_compute_scenario_hint(&in, &out);
        T_ASSERT_EQ_I(out.hint, cases[i].expect);
    }
    return 0;
}

int test_engine_scenario_hint_ahead_coarse_ladder(void)
{
    /* Sweep the coarse "ahead" ladder.  hint = base_delta + bucket. */
    const struct { int lead; int bucket; } cases[] = {
        {1,    0x14},                    /* < 10 → 20 */
        {9,    0x14},
        {10,   0x28},                    /* < 20 → 40 */
        {19,   0x28},
        {20,   0x3c},                    /* < 30 → 60 */
        {30,   0x50},                    /* < 40 → 80 */
        {40,   100},                     /* < 50 → 100 */
        {50,   0x6e},                    /* < 60 → 110 */
        {59,   0x6e},
        {60,   0x78},                    /* < 201 → 120 */
        {200,  0x78},
        {201,  0x82},                    /* < 401 → 130 */
        {400,  0x82},
        {401,  0x8c},                    /* >= 401 → 140 (ceiling) */
        {99999, 0x8c},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        engine_scenario_hint_in_t  in;
        engine_scenario_hint_out_t out;
        scenario_default_in(&in);
        in.game_mode_speed = 0;          /* coarse */
        in.base_delta = 5;
        in.player_ticks[1] = cases[i].lead;
        in.player_ticks[2] = 0;
        in.player_ticks[3] = 0;
        engine_compute_scenario_hint(&in, &out);
        T_ASSERT_EQ_I(out.hint, 5 + cases[i].bucket);
    }
    return 0;
}

int test_engine_scenario_hint_ahead_mode5_ladder(void)
{
    /* Sweep the fine-grained mode-5 ladder. */
    const struct { int lead; int bucket; } cases[] = {
        {1,    10},                      /* < 10 → 10 */
        {9,    10},
        {10,   0x14},                    /* < 30 → 20 */
        {29,   0x14},
        {30,   0x1e},                    /* < 50 → 30 */
        {50,   0x28},                    /* < 90 → 40 */
        {90,   0x32},                    /* < 101 → 50 */
        {101,  0x3c},                    /* < 130 → 60 */
        {130,  0x46},                    /* < 170 → 70 */
        {170,  0x50},                    /* < 210 → 80 */
        {210,  0x5a},                    /* < 310 → 90 */
        {310,  100},                     /* < 410 → 100 */
        {410,  0x6e},                    /* < 510 → 110 */
        {510,  0x78},                    /* < 610 → 120 */
        {610,  0x82},                    /* < 710 → 130 */
        {710,  0x8c},                    /* >= 710 → 140 */
        {99999, 0x8c},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        engine_scenario_hint_in_t  in;
        engine_scenario_hint_out_t out;
        scenario_default_in(&in);
        in.game_mode_speed = 5;          /* fine-grained */
        in.base_delta = 0;
        in.player_ticks[1] = cases[i].lead;
        in.player_ticks[2] = 0;
        in.player_ticks[3] = 0;
        engine_compute_scenario_hint(&in, &out);
        T_ASSERT_EQ_I(out.hint, cases[i].bucket);
    }
    return 0;
}

int test_engine_scenario_hint_ahead_throttle_writes_stride(void)
{
    /* Verify the throttle branch writes max_stride, stride_sum, avg_stride. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.base_delta = 0;
    in.frame_count = 99;       /* will become 100 after bump */
    in.stride_sum  = 200;
    in.max_stride  = 30;
    in.avg_stride  = 5;
    in.player_ticks[1] = 100;
    in.player_ticks[2] = 0;
    in.player_ticks[3] = 0;
    /* lead = 100, coarse ladder → bucket 0x78=120 (the [60..200] range
     * widens past the more-discrete 10-step buckets at the low end),
     * hint = 0+120 = 120. */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 120);
    T_ASSERT_EQ_I(out.new_frame_count, 100);
    T_ASSERT_EQ_I(out.new_max_stride, 100);             /* 30 < 100 → updated */
    T_ASSERT_EQ_I(out.new_stride_sum, 300);             /* 200 + 100 */
    T_ASSERT_EQ_I(out.new_avg_stride, 3);               /* 300 / 100 */
    T_ASSERT_EQ_I(out.notify_slow_frame, 0);
    T_ASSERT_EQ_I(out.new_slow_frame_count, 0);          /* hint < 0x8c → reset */
    return 0;
}

int test_engine_scenario_hint_max_stride_carries_through_when_not_exceeded(void)
{
    /* max_stride only updated when current lead exceeds it.  Smaller
     * leads carry through the existing value. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.max_stride = 999;
    in.player_ticks[1] = 50;
    in.player_ticks[2] = 0;
    in.player_ticks[3] = 0;
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.new_max_stride, 999);
    return 0;
}

int test_engine_scenario_hint_slow_frame_increments_at_140ms_cap(void)
{
    /* lead = 401 → coarse-ladder ceiling 140.  slow_frame_count bumps. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.slow_frame_count = 5;
    in.player_ticks[1] = 500;       /* lead 500 → ceiling */
    in.player_ticks[2] = 0;
    in.player_ticks[3] = 0;
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.new_slow_frame_count, 6);
    T_ASSERT_EQ_I(out.notify_slow_frame, 0);
    return 0;
}

int test_engine_scenario_hint_slow_frame_notify_on_hundred(void)
{
    /* slow_frame_count = 100 → bumps to 101, gate `100 < 101` → notify
     * fires, counter resets.  Diff vectors are constrained ≤ 99 to
     * skip this; tests cover it directly. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.slow_frame_count = 100;
    in.player_ticks[1] = 500;       /* ceiling lead */
    in.player_ticks[2] = 0;
    in.player_ticks[3] = 0;
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.notify_slow_frame, 1);
    T_ASSERT_EQ_I(out.new_slow_frame_count, 0);
    return 0;
}

int test_engine_scenario_hint_alone_in_mode2_uses_alone_ladder(void)
{
    /* mode-2 with only 1 active ally → alone-in-mode-2 path. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.game_mode_speed = 2;
    in.ally_b = -1;                 /* only ally_a (slot 2) eligible → count = 1 */
    in.base_delta = 10;
    in.player_ticks[1] = 100;
    in.player_ticks[2] = 50;
    in.player_ticks[3] = 50;        /* peer_min = 50, lead = 50 */
    /* lead 50 falls in [0x33..0x191) → bucket 0x1e = 30.  Floor at
     * base_delta*3 = 30, so 30 ≥ 30, no floor → hint = 30. */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 30);
    /* active_count NOT written in the alone path. */
    T_ASSERT_EQ_U((uint32_t)out.new_active_count, 0xDEADBEEF);
    return 0;
}

int test_engine_scenario_hint_alone_negative_lead_returns_minus_one(void)
{
    /* Alone-in-mode-2 + lead < 1 → -1 sentinel. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.game_mode_speed = 2;
    in.ally_b = -1;
    in.player_ticks[1] = 0;
    in.player_ticks[2] = 50;        /* peer ahead */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, -1);
    return 0;
}

int test_engine_scenario_hint_alone_floors_at_base_delta_times_three(void)
{
    /* Alone path with small lead — hint should floor at base_delta*3. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.game_mode_speed = 2;
    in.ally_b = -1;
    in.base_delta = 100;            /* floor = 300 */
    in.player_ticks[1] = 10;
    in.player_ticks[2] = 5;         /* lead = 5 → bucket 1, but floored to 300 */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, 300);
    return 0;
}

int test_engine_scenario_hint_mode2_two_allies_uses_normal_path(void)
{
    /* mode-2 with both allies active → normal-MP path (not alone). */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.game_mode_speed = 2;
    /* Already have slots 2 and 3 active as ally_a and ally_b → 2 allies */
    in.player_ticks[1] = 10;
    in.player_ticks[2] = 10;
    in.player_ticks[3] = 10;
    /* All equal → lead = 0 → catch-up branch with behind = 0 → -2 */
    engine_compute_scenario_hint(&in, &out);
    T_ASSERT_EQ_I(out.hint, -2);
    /* active_count IS written in the normal path. */
    T_ASSERT_EQ_I(out.new_active_count, 3);
    return 0;
}

int test_engine_scenario_hint_mode2_filter_excludes_non_allies(void)
{
    /* In mode 2, the min-tick search only considers ally slots.  Slot 4
     * is active but NOT an ally → its tick value (way ahead) is ignored
     * when computing peer_min for the lead/deficit branch decision. */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.game_mode_speed = 2;
    in.player_active[4]  = 1;
    in.player_has_data[4] = 1;
    in.player_ticks[1] = 100;
    in.player_ticks[2] = 100;       /* ally */
    in.player_ticks[3] = 100;       /* ally */
    in.player_ticks[4] = 0;         /* non-ally peer, but should be ignored
                                     * for the lead calc */
    engine_compute_scenario_hint(&in, &out);
    /* peer_min over ally slots only = 100, lead = 0 → catch-up (-2). */
    T_ASSERT_EQ_I(out.hint, -2);
    /* But active_count still counts slot 4 too (no ally filter on count). */
    T_ASSERT_EQ_I(out.new_active_count, 4);
    return 0;
}

int test_engine_scenario_hint_disconnected_player_excluded(void)
{
    /* Disconnected players don't count as active (skips both filters). */
    engine_scenario_hint_in_t  in;
    engine_scenario_hint_out_t out;
    scenario_default_in(&in);
    in.player_disconnected[2] = 1;   /* slot 2 disconnected */
    in.player_ticks[1] = 0;
    in.player_ticks[2] = 0;
    in.player_ticks[3] = 0;
    engine_compute_scenario_hint(&in, &out);
    /* active_count = 2 (slots 1 and 3 only). */
    T_ASSERT_EQ_I(out.new_active_count, 2);
    return 0;
}
