# Data self-check — the port's own copies of retail's data vs Lords2.exe

DERIVED by `tools/data_selfcheck.py` — never hand-edit. The port's definitions of aliased globals (initializers in `src/`, never linked under rhost / the standalone) compared with the exe at their VAs, byte by byte, and mapped onto the catalog rows (`docs/data/catalog.tsv`). Statuses: see the tool's docstring.

216 objects compiled, 1932 aliased names, 1932 defined in src/ as data.

| catalog bytes | all | non-zero in retail |
|---|---:|---:|
| match | 82967 | 46559 |
| uncovered | 464 | 27 |
| **total** | 83431 | 46586 |

## The composed image (`--compose`) vs the exe's mapped sections

| section | range | port-defined bytes | differ | differing spans (catalog row) |
|---|---|---:|---:|---|
| .rdata | 004d0000..004d2000 | 4512 | 27 | 004d0004 +4 k_pe_debug_directory, 004d000c +1 k_pe_debug_directory, 004d0010 +2 k_pe_debug_directory, 004d0019 +2 k_pe_debug_directory, 004d0020 +4 k_pe_debug_directory, 004d0028 +1 k_pe_debug_directory, 004d002d +1 k_pe_debug_directory, 004d0034 +3 k_pe_debug_directory, 004d003c +4 k_pe_debug_directory, 004d0044 +1 k_pe_debug_directory, 004d0048 +1 k_pe_debug_directory, 004d0050 +3 k_pe_debug_directory |
| .data | 004d2000..005cf000 | 984538 | 0 |  |

## Port definitions that differ from the exe (0)

## Overlapping definitions (195)

- 004d3158 `g_rl_4d3128` / `g_rl_4d3158`
- 004d6a8c `g_rl_4d6a78` / `g_rl_4d6a8c`
- 004d8af8 `g_rl_4d8a58` / `g_rl_4d8af8`
- 004dc560 `g_rl_4dc4d0` / `g_rl_4dc560`
- 004dc562 `g_rl_4dc4d0` / `g_rl_4dc562`
- 004dc578 `g_rl_4dc4d0` / `g_rl_4dc578`
- 004dc57a `g_rl_4dc4d0` / `g_rl_4dc57a`
- 004dc590 `g_rl_4dc4d0` / `g_rl_4dc590`
- 004dc592 `g_rl_4dc4d0` / `g_rl_4dc592`
- 004e65dc `g_tr_fl_selected` / `g_rl_4e65dc`
- 004e65f8 `g_gfx_menu_redraw_b` / `g_rl_4e65f8`
- 004ea130 `g_tr_fl_name` / `g_rl_4ea130`
- 004ea1a0 `g_tr_fl_first` / `g_rl_4ea1a0`
- 004ea1a8 `g_gfx_canvas_pixels` / `g_rl_4ea1a8`
- 004eabec `g_bf_4eabec` / `g_gfx_current_blob`
- 004eac50 `g_bf_4eac50` / `g_gfx_screen_byte`
- 004eac60 `g_gfx_darken_remap` / `g_rl_4eac60`
- 004eb25c `g_tr_fl_count` / `g_rl_4eb25c`
- 004eb260 `g_gfx_menu_redraw_a` / `g_rl_4eb260`
- 00500c30 `g_rl_500c2e` / `g_rl_500c30`
- 00522f78 `g_bf_522f78` / `g_bfr_type7_blob_high`
- 00522f7c `g_bf_522f7c` / `g_mv_rotation`
- 0052af94 `g_engine_state` / `g_bfr_engine_state`
- 0052afb4 `g_mv_ti_52afb4` / `g_rl_52afb4`
- 0052afbc `g_mv_ti_county` / `g_rl_52afbc`
- 0053e8d4 `g_bf_53e8d0` / `g_rl_53e8d4`
- 0053e8d8 `g_bf_53e8d0` / `g_rl_53e8d8`
- 0053e8dc `g_bf_53e8d0` / `g_rl_53e8dc`
- 0053e8e0 `g_bf_53e8d0` / `g_rl_53e8e0`
- 0053e8e4 `g_bf_53e8d0` / `g_rl_53e8e4`
- 0053e90c `g_tr_country_scroll` / `g_rl_53e90c`
- 0053e914 `g_mv_terrain_blob1` / `g_rl_53e914`
- 0053e918 `g_wf_villani1` / `g_rl_53e918`
- 0053e970 `g_mv_panel_height1` / `g_rl_53e970`
- 0053e9a0 `g_wf_hover_category` / `g_rl_53e9a0`
- 0053f04c `g_mv_panel_height2` / `g_rl_53f04c`
- 0053f050 `g_bf_53f050` / `g_mv_strat_ready`
- 0053f050 `g_bf_53f050` / `g_wf_panel_mode`
- 0053f1e0 `g_bf_53f1e0` / `g_tr_p4_name_buf`
- 0053f288 `g_tr_options` / `g_rl_53f288`
- 0053f690 `g_mv_panel_queue1` / `g_rl_53f690`
- 0053f6a0 `g_rl_53f690` / `g_rl_53f6a0`
- 0053f710 `g_rl_53f690` / `g_rl_53f710`
- 005440b0 `g_mv_ti_flags` / `g_rl_5440b0`
- 005440b4 `g_mv_panel_count2` / `g_rl_5440b4`
- 00553030 `g_sim_mp_active` / `g_tr_p4_gate_a`
- 00553040 `g_wf_hover_cells` / `g_rl_553040`
- 005530c8 `g_bf_5530c8` / `g_tr_misc_sheet`
- 005530c8 `g_bf_5530c8` / `g_bfr_creature_blob`
- 005530c8 `g_bf_5530c8` / `g_wf_icon_sheet`
- 005530f0 `g_tr_page` / `g_rl_5530f0`
- 00553224 `g_mv_feature_blob` / `g_rl_553224`
- 00553248 `g_tr_p4_gate_b` / `g_rl_553248`
- 00553250 `g_bf_553250` / `g_bfr_unit_key_blob`
- 005533c4 `g_wf_villani2` / `g_arm_torch`
- 005533c4 `g_wf_villani2` / `g_rl_5533c4`
- 00553c60 `g_mv_panel_count1` / `g_rl_553c60`
- 00553d2c `g_mv_ti_row` / `g_rl_553d2c`
- 00553e5c `g_tr_p9_bookmark` / `g_rl_553e5c`
- 00553e60 `g_mv_food_detail` / `g_rl_553e60`
- 00553f14 `g_bf_553f14` / `g_bfr_sel_x`
- 00553f18 `g_bf_553f14` / `g_bfr_sel_y`
- 00553f6c `g_bf_553f6c` / `g_bfr_type7_blob_low`
- 00553f84 `g_bf_553f84` / `g_bfr_sel_gate`
- 00553fd0 `g_mv_panel_queue2` / `g_rl_553fd0`
- 00554018 `g_tr_country_count` / `g_rl_554018`
- 0055409c `g_mv_terrain_blob3` / `g_rl_55409c`
- 005540a0 `g_wf_grid` / `g_rl_5540a0`
- 0055ce5c `g_mv_overlay_blob` / `g_rl_55ce5c`
- 0055ce94 `g_tr_tabs_dirty` / `g_rl_55ce94`
- 005651bc `g_mv_ti_derived` / `g_rl_5651bc`
- 00566514 `s_slot_00566514` / `g_bfr_slot_shift`
- 00566518 `g_bf_566518` / `g_bfr_marker_blob`
- 0056651c `s_slot_0056651c` / `g_bfr_slot_shift_sign`
- 00567950 `g_caravan_order` / `g_mv_x_half`
- 00567954 `g_caravan_order` / `g_mv_cell`
- 00567958 `g_caravan_order` / `s_slot_00567958`
- 0056795c `g_caravan_order` / `g_rl_56795c`
- 00567960 `g_caravan_order` / `s_slot_00567960`
- 00567964 `g_caravan_order` / `g_rl_567964`
- 00567970 `g_caravan_order` / `s_slot_00567970`
- 005679d0 `s_slot_00567970` / `g_arm_march_active`
- 00567d00 `s_slot_00567970` / `g_rl_567d00`
- 00567d04 `s_slot_00567970` / `g_rl_567d04`
- 00567d08 `s_slot_00567970` / `g_rl_567d08`
- 00567d0c `s_slot_00567970` / `g_combat_flag_567d0c`
- 00567d10 `s_slot_00567970` / `s_slot_00567d10`
- 005681cc `s_slot_00567970` / `g_rl_5681cc`
- 005681d0 `s_slot_00567970` / `g_mv_ti_owner`
- 005681d0 `s_slot_00567970` / `g_rl_5681d0`
- 005681d8 `s_slot_00567970` / `s_slot_005681d8`
- 005681dc `s_slot_00567970` / `g_rl_5681dc`
- 005681e0 `s_slot_00567970` / `g_mv_ti_head`
- 005681e0 `s_slot_00567970` / `g_rl_5681e0`
- 005681e8 `s_slot_00567970` / `g_rl_5681e8`
- 005681ec `s_slot_00567970` / `s_slot_005681ec`
- 005681f4 `s_slot_00567970` / `g_rl_5681f4`
- 005681f8 `s_slot_00567970` / `g_arm_march_land`
- 005681fc `s_slot_00567970` / `g_bf_5681fc`
- 00568200 `s_slot_00567970` / `g_bf_568200`
- 00568204 `s_slot_00567970` / `g_rl_568204`
- 00568208 `s_slot_00567970` / `g_bf_568208`
- 00568208 `s_slot_00567970` / `g_mv_terrain_blob0`
- 00568210 `s_slot_00567970` / `s_slot_00568210`
- 00568214 `s_slot_00567970` / `g_rl_568214`
- 00568218 `s_slot_00567970` / `s_slot_00568218`
- 00568218 `s_slot_00567970` / `g_bfr_marker_player`
- 00568220 `s_slot_00567970` / `g_mv_x_step`
- 00568224 `s_slot_00567970` / `g_bf_568224`
- 00568228 `s_slot_00567970` / `g_arm_march_target`
- 0056822c `s_slot_00567970` / `s_slot_0056822c`
- 00568234 `s_slot_00567970` / `s_slot_00568234`
- 00568240 `s_slot_00567970` / `g_coalition_tbl`
- 0056898c `g_mv_terrain_blob4` / `g_rl_56898c`
- 0056953a `g_rl_569530` / `g_rl_56953a`
- 00569543 `g_rl_569530` / `g_rl_569543`
- 00569544 `g_rl_569530` / `g_rl_569544`
- 0056954d `g_rl_569530` / `g_rl_56954d`
- 0056954e `g_rl_569530` / `g_rl_56954e`
- 00569557 `g_rl_569530` / `g_rl_569557`
- 00569558 `g_rl_569530` / `g_rl_569558`
- 00569561 `g_rl_569530` / `g_rl_569561`
- 00569562 `g_rl_569530` / `g_rl_569562`
- 0056956b `g_rl_569530` / `g_rl_56956b`
- 00569590 `g_minimap_type_grid` / `g_rl_569590`
- 0056d5b8 `g_arm_interior` / `g_rl_56d5b8`
- 0056d8bc `g_mv_feature_alt_blob` / `g_rl_56d8bc`
- 0057a0c0 `g_arm_troop` / `g_rl_57a0c0`
- 0057c8cc `s_slot_0057c8cc` / `g_tr_p4_selected_shield`
- 0057c8cc `s_slot_0057c8cc` / `g_bfr_late_player`
- 0057c908 `g_wf_y_origin` / `g_rl_57c908`
- 0057c940 `g_tr_p4_continue_disabled` / `g_rl_57c940`
- 0057c948 `g_tr_country_dirty` / `g_rl_57c948`
- 0057cacc `g_tr_country_sel` / `g_rl_57cacc`
- 0057cb2c `g_mv_ti_type` / `g_rl_57cb2c`
- 0057cb40 `g_tr_p4_shield_slots` / `g_rl_57cb40`
- 0057d344 `g_mv_terrain_blob2` / `g_rl_57d344`
- 0057d354 `g_tr_p4_shield_flash` / `g_rl_57d354`
- 0057d3ac `g_mv_rate_tier2` / `g_rl_57d3ac`
- 0057d3c0 `g_gfx_flag_b` / `g_rl_57d3c0`
- 0057d3c4 `g_tr_fl_show_status` / `g_rl_57d3c4`
- 0058fe04 `g_gfx_format_buffer` / `g_gfx_glyph_offset`
- 0058fe04 `g_gfx_format_buffer` / `g_rl_58fe04`
- 0058fe08 `g_gfx_format_buffer` / `s_slot_0058fe08`
- 0058fe10 `g_gfx_format_buffer` / `g_rl_58fe10`
- 0058fe18 `g_gfx_format_buffer` / `g_gfx_y_clip_mode`
- 0058fe1c `g_gfx_format_buffer` / `g_rl_58fe1c`
- 0058fe20 `g_gfx_format_buffer` / `g_rl_58fe20`
- 0058fe24 `g_gfx_format_buffer` / `g_rl_58fe24`
- 0058fe28 `g_gfx_format_buffer` / `s_slot_0058fe28`
- 0058fe2c `g_gfx_format_buffer` / `g_gfx_uppercase_highlight`
- 0058fe30 `g_gfx_format_buffer` / `g_rl_58fe30`
- 0058fe94 `g_gfx_format_buffer` / `g_rl_58fe94`
- 0058fe98 `g_gfx_format_buffer` / `g_gfx_right_visible_w`
- 0058fe9c `g_gfx_format_buffer` / `g_gfx_outline`
- 0058fea0 `g_gfx_format_buffer` / `g_bf_58fea0`
- 0058fea8 `g_gfx_format_buffer` / `g_mv_error_count`
- 0058fea8 `g_gfx_format_buffer` / `g_rl_58fea8`
- 0058feac `g_gfx_format_buffer` / `g_gfx_top_skip`
- 0058feb0 `g_gfx_format_buffer` / `g_rl_58feb0`
- 0058feb4 `g_gfx_format_buffer` / `g_rl_58feb4`
- 0058feb8 `g_gfx_format_buffer` / `g_rl_58feb8`
- 0058fec0 `g_gfx_format_buffer` / `g_rl_58fec0`
- 00591504 `g_gfx_format_buffer` / `g_gfx_saved_cursor_y`
- 00591508 `g_gfx_format_buffer` / `g_gfx_saved_cursor_x`
- 0059150c `g_gfx_format_buffer` / `g_gfx_src_stride_pad`
- 00591510 `g_gfx_format_buffer` / `g_mv_rate_tier3`
- 00591510 `g_gfx_format_buffer` / `g_bfr_marker_advance`
- 00591510 `g_gfx_format_buffer` / `g_rl_591510`
- 00591514 `g_gfx_format_buffer` / `g_gfx_clipped_w`
- 0059151c `g_gfx_format_buffer` / `g_bf_59151c`
- 00591524 `g_gfx_format_buffer` / `g_gfx_cursor_x`
- 00591524 `g_gfx_format_buffer` / `g_rl_591524`
- 00591528 `g_gfx_format_buffer` / `g_gfx_cursor_y`
- 00591528 `g_gfx_format_buffer` / `g_rl_591528`
- 0059153c `g_gfx_format_buffer` / `g_rl_59153c`
- 00591548 `g_gfx_format_buffer` / `g_tr_fl_flag`
- 00591548 `g_gfx_format_buffer` / `g_rl_591548`
- 0059154c `g_gfx_format_buffer` / `g_bf_59154c`
- 00591558 `g_gfx_format_buffer` / `g_rl_591558`
- 0059155c `g_gfx_format_buffer` / `g_rl_59155c`
- 00591560 `g_gfx_format_buffer` / `g_bf_591560`
- 00591564 `g_gfx_format_buffer` / `g_bf_591564`
- 00591568 `g_gfx_format_buffer` / `g_gfx_flag_a`
- 00591568 `g_gfx_format_buffer` / `g_rl_591568`
- 00591580 `g_gfx_format_buffer` / `g_rl_591580`
- 005aeb70 `g_gfx_glyph_height_short` / `g_rl_5aeb70`
- 005bb478 `g_gfx_glyph_h` / `g_rl_5bb478`
- 005bb4a5 `g_mv_dev_desc_value` / `g_bfr_debug_label`
- 005bb534 `g_gfx_substitution_buf` / `g_rl_5bb534`
- 005bb540 `g_gfx_substitution_buf` / `g_rl_5bb540`
- 005c9258 `g_gfx_glyph_w` / `g_rl_5c9258`
- 005c9288 `g_gfx_last_glyph_index` / `g_rl_5c9288`
- 005cf3d8 `g_rl_5cf3c4` / `g_rl_5cf3d8`
- 005cf5c4 `g_rl_5cf5b8` / `g_rl_5cf5c4`

## Catalog rows

`own-copy found in` = the port sections holding the most matched bytes; `uncovered, src names its VA` = source files that spell a VA inside the row (the port reads it through retail's image: it needs a definition).

| VA | size | name | port copy | own-copy found in | uncovered, src names its VA |
|---|---:|---|---|---|---|
| 004d0000 | 84 | k_pe_debug_directory | uncovered 84 |  |  |
| 004d0058 | 208 | k_dx_iids | match 208 |  |  |
| 004d0128 | 36 | k_crt_block_use_names | match 36 |  |  |
| 004d014c | 1800 | k_crt_dbgheap_msgs | match 1792, uncovered 8 |  | rl_crt.c rl_crt_data_globals.c rl_crt_data_globals.h rl_crt_dbgheap.c rl_io.c rl_io_globals.h rl_mpnet2_globals.h |
| 004d0854 | 60 | k_crt_assert_files_a | match 60 |  |  |
| 004d0890 | 12 | k_crt_start_scopetable | match 12 |  |  |
| 004d089c | 572 | k_crt_dbgrpt_msgs | match 572 |  |  |
| 004d0ad8 | 64 | k_crt_asctime_names | match 64 |  |  |
| 004d0b18 | 100 | k_crt_misc_asserts | match 100 |  |  |
| 004d0b7c | 148 | k_crt_output_data | match 148 |  |  |
| 004d0c10 | 120 | k_crt_eh_scopetables | match 120 |  |  |
| 004d0c88 | 48 | k_crt_assert_files_b | match 48 |  |  |
| 004d0cb8 | 720 | k_crt_rterr_msgs | match 720 |  |  |
| 004d0f88 | 12 | k_crt_assert_file_vsprintf | match 12 |  |  |
| 004d0f94 | 48 | k_crt_msgbox_imports | match 48 |  |  |
| 004d0fc4 | 268 | k_crt_assert_files_c | match 268 |  |  |
| 004d10d0 | 40 | k_eh_funcinfo_mp_resume | match 40 |  |  |
| 004d2000 | 48 | k_crt_init_tables | match 48 |  |  |
| 004d2030 | 32 | k_pow10 | match 32 |  |  |
| 004d2050 | 68 | k_log_overlay_army | match 68 |  |  |
| 004d2094 | 12 | s_label_space_suffixes | match 12 |  |  |
| 004d20a0 | 60 | k_iff_ilbm_header | match 60 |  |  |
| 004d20dc | 64 | k_dbg_quick_save_names | match 64 |  |  |
| 004d211c | 52 | k_screenshot_names | match 52 |  |  |
| 004d216c | 4 | g_bench_repaint_ms | match 4 |  |  |
| 004d2170 | 64 | k_dir8_offsets | match 64 |  |  |
| 004d21b0 | 28 | k_dbg_county_marker | match 28 |  |  |
| 004d21cc | 772 | k_log_gfx_blit_errors | match 768, uncovered 4 |  | engine_hud_menubar.c gfx_panel.c gfx_sprite_render.h rl_img_data_globals.c rl_img_data_globals.h rl_ui4.c rl_ui4_globals.h |
| 004d24d0 | 64 | k_lzh_p_len | match 64 |  |  |
| 004d2510 | 64 | k_lzh_p_code | match 64 |  |  |
| 004d2550 | 256 | k_lzh_d_code | match 256 |  |  |
| 004d2650 | 256 | k_lzh_d_len | match 256 |  |  |
| 004d2750 | 40 | k_btn_repeat_gate | match 40 |  |  |
| 004d2778 | 12 | s_numentry_suffixes | match 12 |  |  |
| 004d2784 | 7 | s_eng_file | match 7 |  |  |
| 004d278c | 30 | s_log_no_window | match 30 |  |  |
| 004d27ac | 10 | s_setup_wav | match 10 |  |  |
| 004d27b8 | 25 | s_log_partially_set | match 25 |  |  |
| 004d27d4 | 31 | s_log_ddraw_fail_a | match 31 |  |  |
| 004d27f4 | 31 | s_log_ddraw_fail_b | match 31 |  |  |
| 004d2814 | 22 | s_log_fully_set | match 22 |  |  |
| 004d282c | 11 | s_help_file | match 11 |  |  |
| 004d2838 | 192 | k_cfg_ini_keys | match 192 |  |  |
| 004d28f8 | 56 | k_minimap_colors | match 56 |  |  |
| 004d2950 | 36 | k_labor_task_cue | match 36 |  |  |
| 004d2974 | 44 | k_labor_task_icon | match 44 |  |  |
| 004d29a0 | 40 | k_labor_task_rows | match 40 |  |  |
| 004d29c8 | 24 | k_smithy_weapon_noun | match 24 |  |  |
| 004d29e0 | 24 | k_smithy_hearth_yoff | match 24 |  |  |
| 004d29f8 | 80 | k_county_graph_bucket | match 80 |  |  |
| 004d2a48 | 120 | k_county_graph_yscale | match 120 |  |  |
| 004d2ac0 | 96 | k_county_graph_barw | match 96 |  |  |
| 004d2b20 | 32 | k_rank_cat_x | match 32 |  |  |
| 004d2b40 | 24 | k_rank_slot_x | match 24 |  |  |
| 004d2b58 | 24 | k_rank_slot_player | match 24 |  |  |
| 004d2b70 | 280 | k_trade_good_tiles | match 280 |  |  |
| 004d2c88 | 96 | k_armory_interior_names | match 96 |  |  |
| 004d2ce8 | 160 | k_armory_tally_slots | match 160 |  |  |
| 004d2d88 | 72 | k_armory_rack_icons | match 72 |  |  |
| 004d2dd0 | 24 | k_castle_tier_pic | match 24 |  |  |
| 004d2de8 | 64 | k_castle_tier_sprite_a | match 64 |  |  |
| 004d2e28 | 60 | k_castle_tier_sprite_b | match 60 |  |  |
| 004d2e64 | 28 | k_castle_bar_marker_x | match 28 |  |  |
| 004d2e80 | 72 | k_screen_hide_turn_icon | match 72 |  |  |
| 004d2ec8 | 464 | k_terrain_feature_text | match 464 |  |  |
| 004d3098 | 144 | k_setup_tab_geom | match 144 |  |  |
| 004d3128 | 48 | k_setup_tab_value_base | match 48 |  |  |
| 004d3158 | 144 | k_setup_dropdown_geom | match 144 |  |  |
| 004d31f0 | 2856 | k_bf_roster_slots | match 2856 |  |  |
| 004d3d18 | 260 | k_lit_county_panels | match 176, uncovered 84 |  | engine_bf_input_globals.h rl_img_data_globals.c rl_img_data_globals.h rl_ui4.c rl_ui4_globals.h |
| 004d3e1c | 212 | k_lit_workforce_screens | match 208, uncovered 4 |  | rl_img_data_globals.c rl_img_data_globals.h rl_ui4.c rl_ui4_globals.h |
| 004d3ef0 | 240 | k_lit_kingdom_screens | match 240 |  |  |
| 004d3fe0 | 248 | k_lit_armoury_screens | match 204, uncovered 44 |  | rl_img_data_globals.c rl_img_data_globals.h rl_ui4.c rl_ui4_globals.h |
| 004d40d8 | 140 | k_lit_army_castle_screens | match 140 |  |  |
| 004d4164 | 104 | k_lit_menubar_debug | match 104 |  |  |
| 004d41cc | 204 | k_lit_county_boxes | match 204 |  |  |
| 004d4298 | 220 | k_lit_title_screens | match 220 |  |  |
| 004d4374 | 212 | k_lit_battle_setup_screens | match 212 |  |  |
| 004d4448 | 252 | k_lit_bf_hud_netdebug | match 252 |  |  |
| 004d4544 | 528 | k_lit_bf_debug_inspectors | match 526, uncovered 2 |  | rl_img_data_globals.c rl_img_data_globals.h rl_render.c rl_render_globals.h rl_skirmish.c rl_skirmish_globals.h rl_ui4.c rl_ui4_globals.h |
| 004d4754 | 8 | k_dsound_timer | match 8 |  |  |
| 004d475c | 4 | k_dsound_stream_chunks | match 4 |  |  |
| 004d4768 | 592 | k_log_dsound | match 592 |  |  |
| 004d49b8 | 16 | k_skirmish_set_base | match 16 |  |  |
| 004d49c8 | 40 | k_hiscore_default_scores | match 40 |  |  |
| 004d49f0 | 40 | k_hiscore_default_names | match 40 |  |  |
| 004d4a18 | 64 | k_siege_battle_flag_57cb48 | match 64 |  |  |
| 004d4a58 | 64 | k_siege_battle_holds | match 64 |  |  |
| 004d4a98 | 64 | k_siege_battle_flag_55307c | match 64 |  |  |
| 004d4ad8 | 64 | k_siege_battle_kit | match 64 |  |  |
| 004d4b18 | 64 | k_siege_battle_gate_breaks | match 64 |  |  |
| 004d4b58 | 64 | k_siege_battle_castle | match 64 |  |  |
| 004d4b98 | 48 | k_troop_type_cost | match 48 |  |  |
| 004d4bc8 | 224 | k_skirmish_battle_lord | match 224 |  |  |
| 004d4ca8 | 40 | k_skirmish_lord_banner | match 40 |  |  |
| 004d4cd0 | 128 | k_skirmish_file_names | match 128 |  |  |
| 004d4d50 | 8 | k_unref_u16_0123 | match 8 |  |  |
| 004d4d60 | 640 | k_movie_viewer_names | match 638, uncovered 2 |  | rl_img_data_globals.c rl_img_data_globals.h rl_ui2.c rl_ui2_globals.h rl_ui4.c |
| 004d4fe0 | 272 | k_log_smacker | match 272 |  |  |
| 004d50f0 | 64 | k_sys_palette_static | match 64 |  |  |
| 004d5130 | 920 | k_log_ddraw | match 920 |  |  |
| 004d54c8 | 64 | k_unref_right_panel_rects | match 64 |  |  |
| 004d5508 | 64 | k_hud_dirty_rects | match 64 |  |  |
| 004d5548 | 24 | k_title_slot_shield | match 24 |  |  |
| 004d5560 | 48 | k_option_tab_value_count | match 48 |  |  |
| 004d5590 | 80 | k_castle_tier_movies | match 80 |  |  |
| 004d55e0 | 64 | k_dbg_id_skip_lists | match 64 |  |  |
| 004d5620 | 448 | k_lit_menu_input | match 448 |  |  |
| 004d57e0 | 16 | k_mp_app_guid | match 16 |  |  |
| 004d57f0 | 400 | k_mp_send_builders | match 400 |  |  |
| 004d5980 | 400 | k_mp_recv_handlers | match 400 |  |  |
| 004d5b10 | 96 | k_mp_sync_ranges | match 80, uncovered 16 |  | rl_img_data_globals.c rl_img_data_globals.h rl_mpmsg.c rl_mpmsg3.c rl_mpmsg_globals.h |
| 004d5b70 | 32 | k_unref_sync_bases | match 32 |  |  |
| 004d5b90 | 256 | k_mp_packet_size | match 256 |  |  |
| 004d5c98 | 200 | k_delayed_action_handlers | match 200 |  |  |
| 004d5d60 | 456 | k_log_net_transport | match 453, uncovered 3 |  | rl_ctl_globals.h rl_glue_globals.h rl_mpmsg.c rl_mpmsg3.c rl_mpmsg3_globals.h rl_mpmsg_globals.h rl_ui4.c rl_ui4_globals.h |
| 004d5f28 | 64 | k_mp_turn_save_names | match 64 |  |  |
| 004d5f68 | 36 | k_lit_mp_checksum | match 36 |  |  |
| 004d5f8c | 120 | k_log_mp_newgame | match 120 |  |  |
| 004d6004 | 100 | k_lit_mp_save_msgs | match 100 |  |  |
| 004d6068 | 160 | k_mp_record_names | match 160 |  |  |
| 004d6108 | 512 | k_realm_event_deck | match 512 |  |  |
| 004d6308 | 160 | k_pop_birth_rate | match 160 |  |  |
| 004d63a8 | 24 | k_pop_death_health | match 24 |  |  |
| 004d63c0 | 24 | k_pop_death_season | match 24 |  |  |
| 004d63d8 | 208 | k_taxsetting_tally_modifier | match 208 |  |  |
| 004d64a8 | 120 | k_tax_approval_tbl | match 120 |  |  |
| 004d6520 | 40 | k_tax_band_threshold | match 40 |  |  |
| 004d6548 | 24 | k_tax_band_modifier | match 24 |  |  |
| 004d6560 | 24 | k_stock_a_rate | match 24 |  |  |
| 004d6578 | 448 | k_unref_pct_curve | match 448 |  |  |
| 004d6738 | 48 | k_supply_table | match 48 |  |  |
| 004d6768 | 24 | k_workforce_share_pct | match 24 |  |  |
| 004d6780 | 32 | k_workforce_row_sprite | match 32 |  |  |
| 004d67a0 | 104 | k_workforce_cell_colour | match 104 |  |  |
| 004d6808 | 40 | k_workforce_row_fill | match 40 |  |  |
| 004d6830 | 32 | k_workforce_cell_order | match 32 |  |  |
| 004d6850 | 16 | k_workforce_dual_order_a | match 16 |  |  |
| 004d6860 | 16 | k_workforce_dual_order_b | match 16 |  |  |
| 004d6870 | 480 | k_unref_records16 | match 480 |  |  |
| 004d6a50 | 24 | k_army_state_handlers | match 24 |  |  |
| 004d6a68 | 16 | k_army_state_handlers_x | match 16 |  |  |
| 004d6a78 | 64 | k_army_step_frame_march | match 64 |  |  |
| 004d6ab8 | 64 | k_army_step_frame_idle | match 64 |  |  |
| 004d6af8 | 240 | k_map_sheet_names | match 240 |  |  |
| 004d6be8 | 64 | k_terr_hex_nbr_odd | match 64 |  |  |
| 004d6c28 | 64 | k_terr_hex_nbr_even | match 64 |  |  |
| 004d6c68 | 48 | k_map_data_names | match 48 |  |  |
| 004d6c98 | 32 | k_dir8_cell_delta | match 32 |  |  |
| 004d6cb8 | 64 | k_dir8_dxdy | match 64 |  |  |
| 004d6cf8 | 400 | k_endgame_movie_names | match 400 |  |  |
| 004d6e88 | 48 | k_capture_movie_names | match 48 |  |  |
| 004d6eb8 | 96 | k_help_popup_rects | match 96 |  |  |
| 004d6f18 | 24 | k_help_para_count | match 24 |  |  |
| 004d6f30 | 112 | k_mp_msg_popup_rows | match 112 |  |  |
| 004d6fa0 | 24 | k_hint_report_panel | match 24 |  |  |
| 004d6fb8 | 72 | k_tooltip_mode | match 72 |  |  |
| 004d7000 | 148 | k_lit_event_popup | match 148 |  |  |
| 004d7094 | 32 | s_log_exited_game_flow | match 32 |  |  |
| 004d70b4 | 284 | k_unref_after_exit_log | match 284 |  |  |
| 004d71d0 | 256 | k_font_glyph_map | match 256 |  |  |
| 004d72d0 | 64 | k_unref_perm16x4 | match 64 |  |  |
| 004d7310 | 576 | k_terr_feature_rules | match 576 |  |  |
| 004d7550 | 120 | k_bf_rules_kind4 | match 119, uncovered 1 |  | engine_bf_globals.h engine_bf_setup.c rl_img_data_globals.c rl_img_data_globals.h rl_terr_globals.h |
| 004d75c8 | 72 | k_bf_rules_kind1 | match 71, uncovered 1 |  | engine_bf_globals.h engine_bf_setup.c rl_img_data_globals.c rl_img_data_globals.h |
| 004d7610 | 588 | k_bf_rules_water | match 575, uncovered 13 |  | engine_bf_globals.h engine_bf_setup.c rl_img_data_globals.c rl_img_data_globals.h |
| 004d7860 | 204 | k_bf_rules_kind12 | match 203, uncovered 1 |  | engine_bf_globals.h engine_bf_setup.c rl_img_data_globals.c rl_img_data_globals.h |
| 004d7930 | 588 | k_bf_rules_rubble | match 588 |  |  |
| 004d7b80 | 512 | k_bf_castle_tiles_stone | match 512 |  |  |
| 004d7d80 | 512 | k_bf_castle_tiles_other | match 512 |  |  |
| 004d7f80 | 352 | k_unref_after_castle_tiles | match 352 |  |  |
| 004d80e0 | 40 | k_terr_block_fill_order | match 40 |  |  |
| 004d8108 | 768 | k_map_unit_disp | match 768 |  |  |
| 004d8408 | 384 | k_unref_unit_disp8 | match 384 |  |  |
| 004d8588 | 32 | k_bf_dir8_cell_offset | match 32 |  |  |
| 004d85a8 | 64 | k_workforce_box_origins | match 64 |  |  |
| 004d85e8 | 56 | k_workforce_cell_offsets | match 56 |  |  |
| 004d8620 | 344 | k_unref_ramps20 | match 344 |  |  |
| 004d8778 | 408 | k_conscript_happy_cost | match 408 |  |  |
| 004d8910 | 64 | k_trade_default_price | match 64 |  |  |
| 004d8950 | 64 | k_unref_price_tail | match 64 |  |  |
| 004d8990 | 48 | k_weapon_cost | match 48 |  |  |
| 004d89c0 | 40 | k_castle_cost | match 40 |  |  |
| 004d89e8 | 40 | k_castle_work | match 40 |  |  |
| 004d8a10 | 20 | k_castle_garrison_cap | match 20 |  |  |
| 004d8a24 | 24 | k_castle_tax_boost | match 24 |  |  |
| 004d8a3c | 28 | k_castle_garrison_grant | match 28 |  |  |
| 004d8a58 | 960 | k_ai_personality | match 960 |  |  |
| 004d8e18 | 320 | k_scenario_presets_a | match 320 |  |  |
| 004d8f58 | 384 | k_scenario_presets_b | match 384 |  |  |
| 004d90d8 | 48 | k_unref_after_scenarios | match 48 |  |  |
| 004d9108 | 56 | k_bf_field_sheet_names | match 56 |  |  |
| 004d9140 | 48 | k_bf_move_state_handlers | match 48 |  |  |
| 004d9170 | 72 | k_bf_order_handlers | match 72 |  |  |
| 004d91b8 | 24 | k_bf_stance_field | match 24 |  |  |
| 004d91d0 | 40 | k_bf_stance_siege_a | match 40 |  |  |
| 004d91f8 | 48 | k_bf_stance_siege_d | match 48 |  |  |
| 004d9228 | 80 | k_bf_music_names | match 80 |  |  |
| 004d9278 | 768 | k_bf_end_movie_names | match 768 |  |  |
| 004d9578 | 96 | k_bf_marker_deploy_offsets | match 96 |  |  |
| 004d95d8 | 64 | k_bf_size_tier_thresholds | match 64 |  |  |
| 004d9618 | 64 | k_bf_engine_weight | match 64 |  |  |
| 004d9658 | 40 | k_bf_troops_per_unit | match 40 |  |  |
| 004d9680 | 40 | k_bf_tier_param_a | match 40 |  |  |
| 004d96a8 | 40 | k_bf_tier_param_b | match 40 |  |  |
| 004d96d0 | 224 | k_bf_class_spawn | match 224 |  |  |
| 004d97b0 | 80 | k_bf_missile_params | match 80 |  |  |
| 004d9800 | 112 | k_bf_band_thresholds | match 112 |  |  |
| 004d9870 | 48 | k_bf_field_spawn_order | match 48 |  |  |
| 004d98a0 | 32 | k_bf_siege_spawn_order | match 32 |  |  |
| 004d98c8 | 48 | k_bf_class_lead_priority | match 48 |  |  |
| 004d98f8 | 88 | k_bf_band_step | match 88 |  |  |
| 004d9950 | 88 | k_bf_effect4_frames | match 88 |  |  |
| 004d99a8 | 68 | k_bf_effect_frames | match 68 |  |  |
| 004d9a00 | 40 | k_bf_walk_phase_a | match 40 |  |  |
| 004d9a28 | 40 | k_bf_walk_phase_b | match 40 |  |  |
| 004d9a50 | 40 | k_bf_walk_phase_c | match 40 |  |  |
| 004d9a78 | 40 | k_bf_walk_phase_alt_a | match 40 |  |  |
| 004d9aa0 | 40 | k_bf_walk_phase_alt_b | match 40 |  |  |
| 004d9ac8 | 80 | k_bf_shot_frames_b | match 80 |  |  |
| 004d9b18 | 80 | k_bf_shot_frames_a | match 80 |  |  |
| 004d9b68 | 80 | k_bf_shot_frames_alt_b | match 80 |  |  |
| 004d9bb8 | 80 | k_bf_shot_frames_alt_a | match 80 |  |  |
| 004d9c08 | 40 | k_bf_engine7_overlay | match 40 |  |  |
| 004d9c30 | 64 | k_bf_facing_frames | match 64 |  |  |
| 004d9c90 | 96 | k_bf_edge_cells_straight | match 96 |  |  |
| 004d9cf0 | 160 | k_bf_edge_cells_diag | match 160 |  |  |
| 004d9d90 | 32 | k_bf_dock_far | match 32 |  |  |
| 004d9db0 | 32 | k_bf_dock_near | match 32 |  |  |
| 004d9dd0 | 72 | k_bf_gate_tiles | match 72 |  |  |
| 004d9e18 | 112 | k_bf_gate_breach_tiles | match 112 |  |  |
| 004d9e88 | 192 | k_lit_bf_setup | match 192 |  |  |
| 004d9f48 | 264 | k_sys_file_records | match 264 |  |  |
| 004da050 | 1280 | k_pl8_bank_table | match 1280 |  |  |
| 004da550 | 2400 | k_bf_sheet_records | match 2400 |  |  |
| 004daeb0 | 80 | k_misc_sheet_records | match 76, uncovered 4 |  | engine_bf_extern_globals.h engine_bf_globals.h rl_img_data_globals.c rl_img_data_globals.h rl_turn.c rl_turn_globals.h rl_ui2_globals.h |
| 004daf00 | 192 | k_fx_bank_names | match 192 |  |  |
| 004dafc0 | 272 | k_bf_fx_bank_names | match 272 |  |  |
| 004db0d0 | 2816 | k_unit_voice_names | match 2816 |  |  |
| 004dbbd0 | 32 | k_system_sheet_names | match 32 |  |  |
| 004dbbf8 | 32 | k_cfg_turn_time | match 32 |  |  |
| 004dbc18 | 20 | k_cfg_start_crowns | match 20 |  |  |
| 004dbc2c | 1092 | k_lit_strategic_loaders | match 1092 |  |  |
| 004dc070 | 96 | k_cfg_start_weapons | match 96 |  |  |
| 004dc0d0 | 64 | k_cfg_start_county | match 64 |  |  |
| 004dc110 | 112 | k_cfg_start_army | match 112 |  |  |
| 004dc180 | 80 | k_realm_personality_pref | match 80 |  |  |
| 004dc1d0 | 16 | k_realm_colour_pair | match 16 |  |  |
| 004dc1e0 | 80 | k_ai_crown_grant_hi | match 80 |  |  |
| 004dc230 | 80 | k_ai_crown_grant_lo | match 80 |  |  |
| 004dc280 | 16 | k_unref_4dc280 | match 16 |  |  |
| 004dc290 | 208 | k_turn_save_names | match 208 |  |  |
| 004dc360 | 48 | k_menu_items_1 | match 48 |  |  |
| 004dc390 | 60 | k_menu_items_2 | match 60 |  |  |
| 004dc3d0 | 84 | k_menu_items_3 | match 84 |  |  |
| 004dc428 | 48 | k_menubar_titles | match 48 |  |  |
| 004dc458 | 120 | k_menu_items_x | match 120 |  |  |
| 004dc4d0 | 72 | k_hs_county_panel | match 72 |  |  |
| 004dc518 | 24 | k_hs_county_panel_x | match 24 |  |  |
| 004dc530 | 48 | k_hs_county_panel_b | match 48 |  |  |
| 004dc560 | 72 | k_hs_army_panel | match 72 |  |  |
| 004dc5a8 | 72 | k_hs_army_panel_castle | match 72 |  |  |
| 004dc5f0 | 48 | k_hs_army_hover | match 48 |  |  |
| 004dc620 | 96 | k_hs_hud_topright | match 96 |  |  |
| 004dc680 | 144 | k_hs_hud_strip | match 144 |  |  |
| 004dc710 | 120 | k_hs_hud_bottomright | match 120 |  |  |
| 004dc788 | 96 | k_hs_x_4dc788 | match 96 |  |  |
| 004dc7e8 | 48 | k_hs_send_supplies | match 48 |  |  |
| 004dc818 | 120 | k_hs_castle_tiers | match 120 |  |  |
| 004dc890 | 168 | k_hs_rankings_tabs | match 168 |  |  |
| 004dc938 | 216 | k_hs_armory_view | match 216 |  |  |
| 004dca10 | 144 | k_hs_smithy | match 144 |  |  |
| 004dcaa0 | 120 | k_hs_x_4dcaa0 | match 120 |  |  |
| 004dcb18 | 48 | k_hs_siege_choice | match 48 |  |  |
| 004dcb48 | 96 | k_hs_title_main | match 96 |  |  |
| 004dcba8 | 120 | k_hs_title_options | match 120 |  |  |
| 004dcc20 | 168 | k_hs_title_choose | match 168 |  |  |
| 004dccc8 | 48 | k_hs_title_page6 | match 48 |  |  |
| 004dccf8 | 48 | k_hs_title_play_now | match 48 |  |  |
| 004dcd28 | 96 | k_hs_title_setup_btns | match 96 |  |  |
| 004dcd88 | 168 | k_hs_title_countries | match 168 |  |  |
| 004dce30 | 288 | k_hs_title_option_grid | match 288 |  |  |
| 004dcf50 | 480 | k_hs_skirmish_setup | match 480 |  |  |
| 004dd130 | 480 | k_hs_skirmish_setup_b | match 480 |  |  |
| 004dd310 | 48 | k_btn_confirm | match 40, uncovered 8 |  | engine_confirm_dialog.c engine_confirm_dialog.h rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dd340 | 72 | g_arm_overlay_buttons | match 72 |  |  |
| 004dd388 | 432 | k_btn_split | match 425, uncovered 7 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dd538 | 144 | k_btn_send_supplies | match 137, uncovered 7 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dd5c8 | 120 | k_btn_x_4dd5c8 | match 120 |  |  |
| 004dd640 | 24 | k_btn_castle_builder | match 24 |  |  |
| 004dd658 | 312 | k_btn_x_4dd658 | match 312 |  |  |
| 004dd790 | 48 | k_btn_tax | match 40, uncovered 8 |  | engine_county_panels.h rl_glue_globals.h rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dd7c0 | 48 | k_btn_rations | match 40, uncovered 8 |  | engine_county_panels.h rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dd7f0 | 24 | k_btn_x_4dd7f0 | match 24 |  |  |
| 004dd808 | 48 | k_btn_merchant | match 41, uncovered 7 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dd838 | 144 | g_trade_buttons | match 144 |  |  |
| 004dd8c8 | 96 | k_btn_assign | match 88, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dd928 | 24 | g_treasury_buttons | match 24 |  |  |
| 004dd940 | 144 | g_noble_buttons | match 144 |  |  |
| 004dd9d0 | 96 | k_btn_gift | match 88, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dda30 | 48 | k_btn_diplo_msg | match 40, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dda60 | 48 | k_btn_diplo_b | match 40, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dda90 | 48 | k_btn_popup_0a | match 48 |  |  |
| 004ddac0 | 48 | k_btn_popup_0b | match 48 |  |  |
| 004ddaf0 | 48 | k_btn_popup_fa | match 48 |  |  |
| 004ddb20 | 48 | k_btn_popup_fb | match 48 |  |  |
| 004ddb50 | 48 | k_btn_popup_11 | match 48 |  |  |
| 004ddb80 | 48 | k_btn_castle_seal | match 40, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h rl_turn_globals.h |
| 004ddbb0 | 48 | k_btn_battle_prompt | match 40, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004ddbe0 | 48 | k_btn_number_box | match 40, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004ddc10 | 96 | k_btn_adv_options | match 88, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004ddc70 | 72 | k_btn_sound_options | match 64, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h rl_ui3.c |
| 004ddcb8 | 48 | k_btn_display_options | match 40, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h rl_ui3.c |
| 004ddce8 | 144 | k_btn_help_options | match 136, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004ddd78 | 96 | k_btn_filelist | match 93, uncovered 3 |  | engine_title_render.c rl_cov3.c rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dddd8 | 48 | k_btn_page13_scroll | match 44, uncovered 4 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004dde08 | 264 | k_btn_x_4dde08 | match 264 |  |  |
| 004ddf10 | 144 | k_btn_siege_engines | match 137, uncovered 7 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004ddfa0 | 96 | k_btn_screen68 | match 88, uncovered 8 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004de000 | 1056 | k_btn_skirmish_custom | match 1049, uncovered 7 |  | rl_host.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h |
| 004de420 | 32 | k_x_4de420 | match 32 |  |  |
| 004de440 | 16 | k_siege_engine_work | match 16 |  |  |
| 004de450 | 672 | k_march_troop_sheets | match 672 |  |  |
| 004de6f0 | 32 | k_march_target_x | match 32 |  |  |
| 004de710 | 80 | k_casualty_survivors | match 80 |  |  |
| 004de760 | 48 | k_merc_men | match 48 |  |  |
| 004de790 | 16 | k_merc_class | match 16 |  |  |
| 004de7a0 | 15 | k_merc_period | match 15 |  |  |
| 004de7af | 13 | k_merc_dwell | match 13 |  |  |
| 004de7bc | 52 | k_merc_hire_cost | match 52 |  |  |
| 004de7f0 | 48 | k_merc_wage | match 48 |  |  |
| 004de820 | 80 | k_merc_band_count | match 80 |  |  |
| 004de870 | 160 | k_lit_combat_cues | match 160 |  |  |
| 004de960 | 1800 | g_save_schema | match 1800 |  |  |
| 004df068 | 36 | k_lit_load_pump | match 36 |  |  |
| 004df08c | 148 | k_log_mp_preload_wait | match 148 |  |  |
| 004df120 | 360 | k_lit_save_files | match 360 |  |  |
| 004df290 | 16 | g_disp_recreating | match 4, uncovered 12 |  | rl_io_globals.h rl_ui3_globals.h |
| 004df3ac | 252 | k_io_path_literals | match 252 |  |  |
| 004df4a8 | 224 | k_lit_debug_dumps | match 220, uncovered 4 |  | io_path.c rl_host_globals.h rl_img_data_globals.c rl_img_data_globals.h rl_io.c rl_io_globals.h |
| 004df588 | 37 | s_log_header | match 37 |  |  |
| 004df5b0 | 15 | s_wndclass | match 15 |  |  |
| 004df5c0 | 7 | s_wndtitle | match 7 |  |  |
| 004df5c8 | 15 | s_wndclass2 | match 15 |  |  |
| 004df5d8 | 480 | k_log_app_shell | match 475, uncovered 5 |  | gfx.h rl_host.c rl_host_globals.h rl_misc_globals.h rl_quit_globals.h rl_ui4.c rl_ui4_globals.h |
| 004df7b8 | 256 | k_voice_confirm | match 256 |  |  |
| 004df8b8 | 256 | k_voice_send_supplies | match 256 |  |  |
| 004df9b8 | 1120 | k_voice_events_1xx | match 1120 |  |  |
| 004dfe18 | 1600 | k_voice_events_2xx | match 1600 |  |  |
| 004e0458 | 7168 | k_voice_events_kt | match 7168 |  |  |
| 004e2058 | 272 | k_voice_cues_misc | match 272 |  |  |
| 004e2168 | 192 | k_voice_battle_result | match 188, uncovered 4 |  | rl_img_data_globals.c rl_img_data_globals.h rl_io_globals.h rl_ui2_globals.h |
| 004e2228 | 104 | k_event_narration_seq | match 104 |  |  |
| 004e2290 | 560 | k_voice_event_narration | match 560 |  |  |
| 004e24c0 | 12 | s_intro_movie | match 12 |  |  |
| 004e24cc | 620 | k_sigs_bootstrap_texts | match 620 |  |  |
| 004e2738 | 344 | k_sigs_dll_exports | match 344 |  |  |
| 004e2890 | 408 | k_sigs_debug_traces | match 408 |  |  |
| 004e2a38 | 16 | s_dp_session_name | match 16 |  |  |
| 004e2a64 | 616 | k_log_dplay_session | match 616 |  |  |
| 004e2ccc | 884 | k_log_dplay_io | match 884 |  |  |
| 004e3040 | 88 | k_log_wav_loader | match 88 |  |  |
| 004e3098 | 280 | k_screen_cursor | match 280 |  |  |
| 004e31b0 | 72 | k_screen_sim_gate | match 72 |  |  |
| 004e31f8 | 72 | k_screen_endturn_panel | match 72 |  |  |
| 004e3240 | 32 | k_popup_mp_sim_gate | match 32 |  |  |
| 004e3260 | 2400 | k_sim_duty_lut | match 2400 |  |  |
| 004e3bc8 | 40 | s_log_sprite_reload | match 40 |  |  |
| 004e3bf0 | 1088 | k_bf_anim_off_z16 | match 1088 |  |  |
| 004e4030 | 1088 | k_bf_anim_off_z32 | match 1088 |  |  |
| 004e4470 | 128 | k_bf_effect_jitter | match 128 |  |  |
| 004e44f0 | 48 | s_log_tilemap_no_data | match 48 |  |  |
| 004e4520 | 16 | s_bf_debug_cell_label | match 16 |  |  |
| 004e4580 | 24 | g_crt_dbgheap_vars | match 18, uncovered 6 |  | rl_crt.c rl_crt_data_globals.c rl_crt_data_globals.h rl_crt_dbgheap.c rl_io_globals.h |
| 004e4598 | 24 | k_crt_block_use_name_ptrs | match 24 |  |  |
| 004e45b0 | 16 | g_crt_nlg_destination | match 16 |  |  |
| 004e45c8 | 12 | g_crt_crt0dat_vars | match 12 |  |  |
| 004e45d8 | 56 | g_crt_dbgrpt_vars | match 56 |  |  |
| 004e4610 | 8 | g_crt_alloc_hook | match 8 |  |  |
| 004e4618 | 2068 | g_crt_sbh_region0 | match 2068 |  |  |
| 004e4e2c | 12 | g_crt_sbh_vars | match 12 |  |  |
| 004e4e38 | 8 | g_crt_pctype | match 8 |  |  |
| 004e4e40 | 514 | _ctype | match 514 |  |  |
| 004e5044 | 12 | g_crt_nls_vars | match 12 |  |  |
| 004e5050 | 368 | k_crt_errtable | match 368 |  |  |
| 004e51c0 | 152 | g_crt_tz_vars | match 152 |  |  |
| 004e5258 | 36 | g_crt_tz_dst_cache | match 36 |  |  |
| 004e52a4 | 12 | k_crt_nullstr_ptrs | match 12 |  |  |
| 004e52c8 | 8 | g_crt_eh_hooks | match 8 |  |  |
| 004e53f0 | 248 | k_crt_mbctype_tables | match 248 |  |  |
| 004e54e8 | 140 | g_crt_xcpt_act_tab | match 140 |  |  |
| 004e5580 | 8 | k_crt_badioinfo | match 8 |  |  |
| 004e5588 | 144 | k_crt_rterrs | match 144 |  |  |
| 004e5670 | 52 | k_crt_lpdays | match 52 |  |  |
| 004e56a8 | 56 | k_crt_days | match 56 |  |  |
| 004e56e0 | 640 | g_crt_iob | match 640 |  |  |
| 004e5970 | 24 | k_crt_cfltcvt_tab | match 24 |  |  |
