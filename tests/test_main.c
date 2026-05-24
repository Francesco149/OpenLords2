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
