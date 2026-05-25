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
    X(strutil_to_upper_char_lowercase_in_range) \
    X(strutil_to_upper_char_non_lowercase_returns_zero) \
    X(strutil_to_lower_char_uppercase_in_range) \
    X(strutil_to_lower_char_non_uppercase_returns_zero) \
    X(strutil_to_upper_inplace_preserves_non_letters) \
    X(strutil_to_upper_inplace_all_lowercase) \
    X(strutil_to_upper_inplace_no_letters_unchanged) \
    X(strutil_to_lower_inplace_preserves_non_letters) \
    X(strutil_to_lower_inplace_all_uppercase) \
    X(strutil_shift_left_deletes_first_byte) \
    X(strutil_shift_left_at_interior_offset) \
    X(strutil_shift_right_inserts_space) \
    X(strutil_shift_right_interior_range) \
    X(strutil_strip_leading_spaces_basic) \
    X(strutil_strip_leading_spaces_no_leading) \
    X(strutil_strip_leading_spaces_all_spaces) \
    X(strutil_strip_leading_spaces_empty) \
    X(strutil_trim_trailing_spaces_basic) \
    X(strutil_trim_trailing_spaces_no_trailing) \
    X(strutil_trim_trailing_spaces_preserves_leading) \
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
    X(engine_scenario_hint_state_not_ingame_returns_zero) \
    X(engine_scenario_hint_sp_returns_one) \
    X(engine_scenario_hint_local_player_inactive_returns_one) \
    X(engine_scenario_hint_invalid_slot_returns_one) \
    X(engine_scenario_hint_single_active_player_returns_one) \
    X(engine_scenario_hint_catch_up_small_behind_returns_neg2) \
    X(engine_scenario_hint_catch_up_ladder_boundaries) \
    X(engine_scenario_hint_ahead_coarse_ladder) \
    X(engine_scenario_hint_ahead_mode5_ladder) \
    X(engine_scenario_hint_ahead_throttle_writes_stride) \
    X(engine_scenario_hint_max_stride_carries_through_when_not_exceeded) \
    X(engine_scenario_hint_slow_frame_increments_at_140ms_cap) \
    X(engine_scenario_hint_slow_frame_notify_on_hundred) \
    X(engine_scenario_hint_alone_in_mode2_uses_alone_ladder) \
    X(engine_scenario_hint_alone_negative_lead_returns_minus_one) \
    X(engine_scenario_hint_alone_floors_at_base_delta_times_three) \
    X(engine_scenario_hint_mode2_two_allies_uses_normal_path) \
    X(engine_scenario_hint_mode2_filter_excludes_non_allies) \
    X(engine_scenario_hint_disconnected_player_excluded) \
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
    \
    X(engine_fps_basic_frame_bumps_count) \
    X(engine_fps_delta_independent_of_window) \
    X(engine_fps_rollover_at_1001ms_publishes) \
    X(engine_fps_rollover_boundary_exactly_1000ms_does_not_fire) \
    X(engine_fps_cumulative_accumulates_across_rollovers) \
    X(engine_fps_negative_elapsed_clock_wrap_suppressed) \
    X(engine_fps_two_call_shape_window_basis) \
    X(engine_fps_first_frame_with_zero_baselines) \
    X(engine_fps_high_fps_3sec_window) \
    \
    X(engine_rng_zero_state_stays_zero) \
    X(engine_rng_seed_one_traces_to_specific_value) \
    X(engine_rng_bit_31_always_cleared) \
    X(engine_rng_states_are_independent) \
    X(engine_rng_output_masks_match_state) \
    X(engine_rng_reproducibility) \
    X(engine_rng_retail_seed_produces_nonzero) \
    X(engine_rng_matches_reference) \
    X(engine_rng_multi_call_evolves) \
    \
    X(engine_state_hash_zeros_mode_other) \
    X(engine_state_hash_strategic_zeros) \
    X(engine_state_hash_strategic_loop1_per_army) \
    X(engine_state_hash_strategic_loop2_player_strategic) \
    X(engine_state_hash_strategic_loop3_per_territory_no_offset) \
    X(engine_state_hash_strategic_hash_offset_skips_per_army_first_5) \
    X(engine_state_hash_strategic_hash_offset_skips_player_strategic_first_6) \
    X(engine_state_hash_combat_basic) \
    X(engine_state_hash_quirk_bucket7_always_one) \
    X(engine_state_hash_bucket9_formula) \
    X(engine_state_hash_byte_sum_wraps_at_256) \
    X(engine_state_hash_bucket_sum_wraps) \
    X(engine_state_hash_slot_boundaries_per_army) \
    X(engine_state_hash_player_slot_irrelevant_to_computation) \
    X(engine_state_hash_output_writes_cover_all_strategic_slots) \
    X(engine_state_hash_combat_slot_boundaries) \
    \
    X(save_schema_table_shape) \
    X(save_schema_aliasing_preserved) \
    X(save_round_trip_retail_lastturn) \
    X(castles_init_blank_makes_zero_file) \
    X(castles_scenario_round_trip_byte13) \
    X(castles_scenario_independent_slots) \
    X(castles_read_fixup_flag_bit0) \
    X(castles_read_fixup_flag_bit1) \
    \
    X(engine_tile_push_into_empty_list_takes_slot_0) \
    X(engine_tile_push_skips_occupied_slots) \
    X(engine_tile_push_full_list_writes_last_only) \
    X(engine_tile_stage_scratch_copies_plus4_byte) \
    X(engine_tile_path_scratch_outer_ring_is_999) \
    X(engine_tile_path_scratch_flag10_marks_impassable) \
    X(engine_tile_neighbor_flags_orient0_center_plus4) \
    X(engine_tile_neighbor_flags_orient0_writes_diagonals) \
    X(engine_tile_neighbor_flags_orient0_horiz_neighbors) \
    X(engine_tile_neighbor_flags_orient2_vert_neighbors) \
    X(engine_tile_expand_unrecognized_anchor_is_noop) \
    X(engine_tile_expand_anchor_at_writes_footprint_glyphs) \
    X(engine_tile_expand_each_orient_glyphs) \
    X(engine_tile_expand_clears_low_bits_then_sets_bit2) \
    X(engine_tile_expand_center_marker_bit80) \
    X(engine_tile_expand_door_orient0_above_top_middle) \
    X(engine_tile_expand_door_orient4_below_bottom_middle) \
    X(engine_tile_expand_door_orient2_right_of_middle_right) \
    X(engine_tile_expand_door_orient6_left_of_middle_left) \
    X(engine_tile_expand_refreshes_scratches) \
    X(engine_tile_expand_inserts_into_footprint_list) \
    \
    X(engine_delayed_action_master_switch_off_is_noop) \
    X(engine_delayed_action_no_due_records_clears_queue) \
    X(engine_delayed_action_inactive_records_excluded) \
    X(engine_delayed_action_future_records_excluded) \
    X(engine_delayed_action_due_at_boundary_includes) \
    X(engine_delayed_action_in_order_dispatch_clears_active) \
    X(engine_delayed_action_dispatch_args_mirror_last_record) \
    X(engine_delayed_action_handler_called_per_dispatch) \
    X(engine_delayed_action_null_handler_treated_as_noop) \
    X(engine_delayed_action_mode2_with_one_record_skips_reorder) \
    X(engine_delayed_action_mode_other_skips_reorder) \
    X(engine_delayed_action_mp_reorder_pulls_relay_types_back) \
    X(engine_delayed_action_mp_reorder_local_player_relay_stays) \
    X(engine_delayed_action_mp_reorder_type_boundary_exclusive) \
    X(engine_delayed_action_mp_reorder_no_relays_is_in_order) \
    X(engine_delayed_action_mp_reorder_compaction_two_consecutive_holes) \
    X(engine_delayed_action_mp_reorder_hipri_preserves_record_order) \
    X(engine_delayed_action_dispatch_stops_at_first_empty_sentinel) \
    X(engine_delayed_action_record_padding_is_inert) \
    X(engine_delayed_action_record_size_layout) \
    \
    X(engine_dirty_rect_expand_sets_mark_to_1_from_zero) \
    X(engine_dirty_rect_expand_preserves_mark_at_2) \
    X(engine_dirty_rect_expand_widens_existing_box) \
    X(engine_dirty_rect_expand_does_not_shrink_existing_box) \
    X(engine_dirty_rect_expand_clamps_xmin_to_zero) \
    X(engine_dirty_rect_expand_clamps_ymin_to_zero) \
    X(engine_dirty_rect_expand_clamps_xmax_to_639) \
    X(engine_dirty_rect_quirk_ymax_threshold_uses_x_bound) \
    X(engine_dirty_rect_right_half_marks_right_half) \
    X(engine_dirty_rect_full_screen_marks_whole_screen) \
    X(engine_dirty_rect_below_status_bar_sets_redraw_flag) \
    X(engine_dirty_rect_below_status_bar_full_overrides_mark_to_2) \
    X(engine_dirty_rect_tile_32_box) \
    X(engine_dirty_rect_tile_48_box) \
    X(engine_dirty_rect_tile_64_box) \
    X(engine_dirty_rect_tile_80_box) \
    X(engine_dirty_rect_tile_96_box) \
    X(engine_dirty_rect_tile_200x160_box) \
    X(engine_dirty_rect_tile_64_negative_inputs_clamped) \
    X(engine_dirty_rect_tile_80x48_noclamp_skips_prelude) \
    X(engine_dirty_rect_box_tiled_pixel_origin_box) \
    X(engine_dirty_rect_box_tiled_force_full_escalates_mark) \
    X(engine_dirty_rect_box_tiled_clamps_negative_topleft) \
    X(engine_dirty_rect_box_tiled_force_zero_no_escalation) \
    X(engine_dirty_rect_box_tiled_escalation_overrides_preexisting) \
    \
    X(engine_event_queue_record_size_is_24) \
    X(engine_event_queue_capacity_is_50) \
    X(engine_event_queue_init_clears_indices_and_flags) \
    X(engine_event_queue_init_zeros_slot_0_fully) \
    X(engine_event_queue_init_quirk_slots_1_to_49_keep_non_code) \
    X(engine_event_queue_init_wrapper_matches_init) \
    X(engine_event_queue_post_broadcast_audience_publishes) \
    X(engine_event_queue_post_audience_matches_local_publishes) \
    X(engine_event_queue_post_audience_mismatches_local_skips) \
    X(engine_event_queue_post_target_zero_filter) \
    X(engine_event_queue_post_target_empty_filter) \
    X(engine_event_queue_post_scratch_updated_even_on_filter_fail) \
    X(engine_event_queue_post_wraps_write_idx) \
    X(engine_event_queue_publish_scratch_bumps_and_notifies) \
    X(engine_event_queue_publish_scratch_wraps_too) \
    X(engine_event_queue_publish_scratch_does_not_touch_other_slots) \
    X(engine_event_queue_post_pad16_stays_zero_through_publish) \
    X(engine_event_queue_slot_zero_via_consumer_idx_matches) \
    X(engine_event_queue_post_filter_target_populated_each_audience) \
    \
    X(engine_path_chebyshev_zero) \
    X(engine_path_chebyshev_x_dominant) \
    X(engine_path_chebyshev_y_dominant) \
    X(engine_path_chebyshev_negative_args_use_signed_subtraction) \
    X(engine_path_chebyshev_side_effect_preserved) \
    X(engine_path_manhattan_basic) \
    X(engine_path_manhattan_shares_side_effect_with_chebyshev) \
    X(engine_path_manhattan_zero_distance) \
    X(engine_path_direction_equal_is_8) \
    X(engine_path_direction_cardinals) \
    X(engine_path_direction_intercardinals) \
    X(engine_path_greedy_chebyshev_zero_returns_max_steps) \
    X(engine_path_greedy_adjacent_returns_max_steps) \
    X(engine_path_greedy_bad_start_cost_zero_returns_zero) \
    X(engine_path_greedy_bad_start_cost_wall_returns_zero) \
    X(engine_path_greedy_start_idx_recorded) \
    X(engine_path_greedy_horizontal_descent) \
    X(engine_path_greedy_stuck_returns_max_steps) \
    X(engine_path_greedy_stage_band_blocks_off_band_neighbor) \
    X(engine_path_greedy_stage_band_accepts_plus_one) \
    X(engine_path_greedy_tiebreak_prefers_direction_to_dest) \
    X(engine_path_nearest_finds_immediate_self_cell) \
    X(engine_path_nearest_skip_point_returns_zero) \
    X(engine_path_nearest_expands_to_radius_1) \
    X(engine_path_nearest_stage_mismatch_rejected) \
    X(engine_path_nearest_clamps_box_at_edges) \
    X(engine_path_nearest_quirk13_topleft_invisible) \
    X(engine_path_stash_copies_path_and_flags) \
    X(engine_path_stash_zero_step_count_only_sets_active) \
    \
    X(engine_handlers_da_install_wires_noop_slots) \
    X(engine_handlers_da_noop_increments_sentinel) \
    X(engine_handlers_da_noop_tolerates_null_user) \
    X(engine_handlers_da_noop_via_dispatcher_at_type_0) \
    X(engine_handlers_da_noop_via_dispatcher_at_each_type) \
    X(engine_handlers_da_counter_increments_once) \
    X(engine_handlers_da_counter_via_dispatcher) \
    X(engine_handlers_da_counter_ignores_dispatch_args) \
    X(engine_handlers_da_mixed_records_in_order) \
    X(engine_handlers_da_other_types_still_treated_as_noop) \
    \
    X(engine_category_buckets_clear_zeros_both_tables) \
    X(engine_category_buckets_clear_is_idempotent) \
    X(engine_category_buckets_append_first_slot_of_empty_bucket) \
    X(engine_category_buckets_append_advances_past_filled_slots) \
    X(engine_category_buckets_append_independent_buckets) \
    X(engine_category_buckets_append_full_bucket_silently_drops) \
    X(engine_category_buckets_append_skips_zero_in_middle) \
    X(engine_category_buckets_digest_contains_found) \
    X(engine_category_buckets_digest_contains_not_found) \
    X(engine_category_buckets_digest_contains_zero_matches_default) \
    X(engine_category_buckets_refresh_picks_bucket0_initial) \
    X(engine_category_buckets_refresh_skips_duplicate_initial) \
    X(engine_category_buckets_refresh_probe_caps_at_6) \
    X(engine_category_buckets_refresh_zero_value_flips_to_odd_parity) \
    \
    X(engine_math_percent_of_basic) \
    X(engine_math_percent_of_truncates) \
    X(engine_math_bp_of_basic) \
    X(engine_math_percent_div_basic) \
    X(engine_math_percent_div_divide_by_zero_returns_zero) \
    X(engine_math_div_ceil_exact) \
    X(engine_math_div_ceil_remainder) \
    X(engine_math_div_ceil_divide_by_zero) \
    X(engine_math_lookup_threshold_first_pair_matches) \
    X(engine_math_lookup_threshold_walks_to_second_pair) \
    X(engine_math_lookup_threshold_no_match_returns_fallback) \
    X(engine_math_lookup_threshold_query_at_exact_key_walks_past) \
    X(engine_math_lookup_threshold_empty_table_returns_fallback) \
    \
    X(text_input_init_copies_payload_into_buf0) \
    X(text_input_init_empty_payload_yields_length_zero) \
    X(text_input_init_zeroes_full_buffer) \
    X(text_input_init_propagates_initial_mode) \
    X(text_input_clear_sets_length_one_sentinel) \
    X(text_input_cursor_left_floor_at_zero) \
    X(text_input_cursor_right_caps_at_length) \
    X(text_input_cursor_home_and_end) \
    X(text_input_clamp_cursor_in_range_no_state) \
    X(text_input_clamp_cursor_past_length_bumps_to_length_plus_one) \
    X(text_input_clamp_cursor_at_length_no_clamp) \
    X(text_input_clamp_cursor_past_max_length_state_two) \
    X(text_input_clamp_cursor_pixel_overflow_sets_state_two) \
    X(text_input_clamp_cursor_negative_floored_to_zero) \
    X(text_input_clamp_cursor_resets_state_each_call) \
    X(text_input_insert_char_disabled_is_noop) \
    X(text_input_insert_char_overwrite_mode_replaces_in_place) \
    X(text_input_insert_char_insert_mode_shifts_right) \
    X(text_input_insert_char_insert_mode_full_buffer_no_op) \
    X(text_input_insert_char_clamp_state_two_blocks) \
    X(text_input_insert_char_at_end_extends_in_overwrite) \
    X(text_input_delete_char_removes_at_cursor) \
    X(text_input_delete_char_disabled_is_noop) \
    X(text_input_backspace_at_zero_is_noop) \
    X(text_input_backspace_deletes_prev_char) \
    X(text_input_strip_spaces_single_space) \
    X(text_input_strip_spaces_adjacent_keeps_one) \
    X(text_input_strip_spaces_no_spaces_unchanged) \
    X(text_input_recompute_length_basic) \
    X(text_input_recompute_length_empty) \
    X(text_input_toggle_insert_mode_flips) \
    X(text_input_toggle_insert_mode_xor_bit0) \
    X(text_input_probe_at_cursor_on_char_zero) \
    X(text_input_probe_at_cursor_on_terminator_is_one) \
    X(text_input_probe_at_cursor_decimal_lt_eight_is_one) \
    X(text_input_probe_at_cursor_decimal_ge_eight_is_two) \
    X(text_input_probe_at_cursor_decimal_without_clamp_state_one) \
    X(text_input_copy_out_truncates_at_max_chars) \
    X(text_input_copy_out_full_string_when_max_large) \
    X(text_input_copy_out_zero_max_writes_only_terminator) \
    X(text_input_copy_out_empty_buf_writes_only_terminator) \
    X(text_input_scenario_type_then_backspace) \
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
