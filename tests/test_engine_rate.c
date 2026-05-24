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
