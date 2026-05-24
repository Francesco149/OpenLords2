/*
 * test_main.c — unit-test driver for the OpenLords2 pure-C modules.
 *
 *   make -C tests run             # run all tests
 *   ./build/run_tests             # ...or invoke directly
 *   ./build/run_tests strutil     # filter by name substring
 *
 * Test outcomes:
 *   0 pass, 1 fail (printed message), 2 skip (missing vendor data, etc.)
 *
 * Process exit is 0 iff zero failures.  Skips do not count as failures.
 *
 * Registration: add one X(name) line per test to TESTS below.  The
 * X-macro expands twice — once to emit the extern decl, once to
 * populate the dispatch table — so each test name appears exactly
 * once and the registry can't drift from the declarations.
 *
 * Stub block: src/config.c references `log_line` (defined in
 * main.c) and `io_read_file_at` (defined in io.c) at the top of the
 * file.  Tests don't exercise the I/O glue, but the symbols still
 * need to resolve at link time — so we provide tiny stubs below.
 */

#include "t.h"

#include <stdarg.h>
#include <stddef.h>

typedef int (*test_fn)(void);

struct test_case {
    const char *name;
    test_fn     fn;
};

/* ─── test list ────────────────────────────────────────────────────
 * Each X(n) declares `extern int test_##n(void);` and registers it
 * as {"n", test_##n}.  Group blank lines are cosmetic only.
 */
#define TESTS(X) \
    X(strutil_find_substr_basic) \
    X(strutil_find_substr_not_found_returns_zero) \
    X(strutil_find_substr_at_offset_zero_quirk) \
    X(strutil_find_substr_respects_bound) \
    X(strutil_find_substr_overlap_boundaries) \
    X(strutil_find_char_basic) \
    X(strutil_find_char_at_offset_zero_one_based) \
    X(strutil_find_char_not_found_returns_zero) \
    X(strutil_find_char_respects_bound) \
    X(strutil_bounded_copy_no_null_terminator) \
    X(strutil_bounded_copy_zero_length) \
    X(strutil_strip_ctrl_at_lf) \
    X(strutil_strip_ctrl_at_high_byte) \
    X(strutil_strip_ctrl_range_edges) \
    X(strutil_strip_ctrl_empty_string) \
    \
    X(config_defaults_match_retail) \
    X(config_parse_empty_keeps_defaults) \
    X(config_parse_cdrom_drives_letter) \
    X(config_parse_cdrom_speed) \
    X(config_parse_sound_channels) \
    X(config_parse_physical_mem) \
    X(config_parse_video_speed) \
    X(config_parse_cpu_class_pentium_upper_p) \
    X(config_parse_cpu_class_pentium_lower_p) \
    X(config_parse_cpu_class_pentium_gt_sigil) \
    X(config_parse_cpu_class_486_other) \
    X(config_parse_cpu_mhz_after_dash) \
    X(config_parse_fast_machine_threshold_both_satisfied) \
    X(config_parse_fast_machine_threshold_cd_alone_insufficient) \
    X(config_parse_fast_machine_threshold_cpu_alone_insufficient) \
    X(config_parse_missing_key_keeps_existing) \
    X(config_parse_real_sierra_ini_shape) \
    \
    X(engine_rate_gate_zero_returns_zero) \
    X(engine_rate_autosave_window_active_returns_zero_no_change) \
    X(engine_rate_autosave_window_boundary_4999ms_still_blocks) \
    X(engine_rate_autosave_window_boundary_5000ms_expires) \
    X(engine_rate_autosave_expiry_in_mp_fires_notification) \
    X(engine_rate_scenario_hint_zero_returns_zero) \
    X(engine_rate_fast_skip_warmup_returns_one) \
    X(engine_rate_fast_skip_cruise_returns_two) \
    X(engine_rate_hint_clamped_to_0x8c) \
    X(engine_rate_lut_zero_blocks_step) \
    X(engine_rate_pause_lock_blocks_step) \
    X(engine_rate_screen_freeze_2_blocks_sp) \
    X(engine_rate_screen_freeze_2_passes_mp) \
    X(engine_rate_screen_freeze_1_blocks_both) \
    X(engine_rate_extended_pause_sp_blocks) \
    X(engine_rate_extended_pause_mp_skip_flag_blocks) \
    X(engine_rate_extended_pause_mp_no_skip_proceeds) \
    X(engine_rate_slider_max_slow_blocks) \
    X(engine_rate_slider_threshold_just_below_blocks) \
    X(engine_rate_slider_threshold_at_passes) \
    X(engine_rate_mp_max_speed_uncapped) \
    X(engine_rate_phase_wraps_at_0xa0) \
    X(engine_rate_phase_no_wrap_at_0x9e) \
    X(engine_rate_time_overflow_local_8_clamps_to_10000) \
    \
    X(engine_phase_no_time_passed_fires_nothing) \
    X(engine_phase_19ms_below_threshold) \
    X(engine_phase_20ms_fires_base_tick) \
    X(engine_phase_negative_delta_clock_wrap_fires_base) \
    X(engine_phase_base_counter_3_no_80ms) \
    X(engine_phase_base_counter_2_yields_3_no_80ms) \
    X(engine_phase_no_20ms_means_no_80ms_even_with_full_counter) \
    X(engine_phase_counter_2_rollover_at_2nd) \
    X(engine_phase_all_long_cycles_at_max_rollover) \
    X(engine_phase_1040ms_off_by_one) \
    X(engine_phase_no_fire_no_state_change) \
    X(engine_phase_default_clears_all_flags) \
    \
    X(engine_delta_below_threshold_holds_baseline) \
    X(engine_delta_at_threshold_advances_baseline) \
    X(engine_delta_just_below_threshold_holds) \
    X(engine_delta_large_delta_advances) \
    X(engine_delta_zero_delta_holds) \
    X(engine_delta_negative_delta_clock_wrap_holds) \
    X(engine_delta_overflow_wrap_then_advance) \
    X(engine_delta_baseline_zero_first_call_advances) \
    \
    X(engine_anim_cycle_no_tick_holds) \
    X(engine_anim_cycle_tick_bumps) \
    X(engine_anim_cycle_from_zero_bumps) \
    X(engine_anim_cycle_wraps_at_15_to_0) \
    X(engine_anim_cycle_14_to_15_no_wrap) \
    X(engine_anim_cycle_full_16_cycle) \
    X(engine_anim_cycle_no_tick_with_max_counter) \
    \
    X(engine_input_raw_cursor_always_written) \
    X(engine_input_cache_update_gated_on_flag) \
    X(engine_input_cache_carries_through_when_disabled) \
    X(engine_input_game_coord_1to1_mapping) \
    X(engine_input_game_coord_scaled) \
    X(engine_input_game_coord_div_by_zero_clamp) \
    X(engine_input_default_clears_event_flags) \
    X(engine_input_dblclk_bit0_lmb) \
    X(engine_input_dblclk_bit1_rmb) \
    X(engine_input_button_state_decoded) \
    X(engine_input_lmb_press_edge) \
    X(engine_input_lmb_release_always_sets_released_flag) \
    X(engine_input_lmb_release_with_eligibility_arms_timer) \
    X(engine_input_rmb_release_with_eligibility_arms_rmb_timer) \
    X(engine_input_click_expiry_lmb_fires) \
    X(engine_input_click_expiry_boundary_at_300_does_not_fire) \
    X(engine_input_shared_timer_baseline_quirk) \
    X(engine_input_motion_x_change_sets_event) \
    X(engine_input_motion_y_change_sets_event) \
    X(engine_input_no_motion_computes_idle_ms) \
    X(engine_input_prev_snapshots_always_written) \
    X(engine_input_last_click_carries_through_no_click) \
    /* end */

/* Emit extern decls. */
#define X(n) extern int test_##n(void);
TESTS(X)
#undef X

/* Populate dispatch table. */
static const struct test_case g_tests[] = {
#define X(n) { #n, test_##n },
    TESTS(X)
#undef X
};
static const int g_n_tests = (int)(sizeof g_tests / sizeof g_tests[0]);

/* ─── stubs for src/config.c's externs ─────────────────────────────
 * Tests do NOT call config_init / load_l2_eng / try_l2ini_to_sierra_ini,
 * so the actual implementations of these are unused — but the linker
 * still resolves their references.  Provide silent stubs. */
void log_line(const char *fmt, ...)
{
    (void)fmt;
    /* Suppress logging in test runs; uncomment for debugging. */
    /* va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap);
     * fputc('\n', stderr); va_end(ap); */
}

int io_read_file_at(const char *path, void *buf, size_t len, long offset)
{
    (void)path; (void)buf; (void)len; (void)offset;
    return 0;   /* "file not found" — tests don't exercise this path */
}

/* ─── driver ────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *filter = (argc > 1) ? argv[1] : NULL;
    int ran = 0, passed = 0, failed = 0, skipped = 0;

    for (int i = 0; i < g_n_tests; i++) {
        const struct test_case *tc = &g_tests[i];
        if (filter && !strstr(tc->name, filter)) continue;

        fprintf(stderr, "test: %s\n", tc->name);
        int rv = tc->fn();
        ran++;
        if      (rv == 0) passed++;
        else if (rv == 1) failed++;
        else if (rv == 2) skipped++;
        else {
            fprintf(stderr, "  fail: test returned unexpected %d\n", rv);
            failed++;
        }
    }

    fprintf(stderr, "\n%d ran, %d passed, %d failed, %d skipped\n",
            ran, passed, failed, skipped);
    return failed ? 1 : 0;
}
