# Render screens under rhost — the screen renderers on retail's layout (2026-11-04)

Batch "screens": the screen renderers ABOVE the gfx layer (`docs/findings/render-under-rhost.md`) — the battlefield
renderer, the title / menu pages, the tooltip, the county panels, the noble faces, the armory, the trade grid, the MP
overlays, the workforce HUD, the treasury panel, the labor castle block, the backdrop walk — moved onto retail's own globals and verified function by function against retail on
identical state AND identical pixels (`RH_SH_MEDIA`).  Table: `tools/rhost/rh_hooks_screens.c` (rows `scr_*`).

## Result

`RHOST_JOBS=3 rhost.py verify --target wine -j 3 --hooks '^scr_'` over the whole corpus (51 scenarios):
**104 rows, 94 reached — 6,690,569 calls (+1,075,638 sampled) / 51 scenarios, 0 divergences; replace: every row at
once, corpus --check PASS (state + every presented frame; wine).**  10 rows are never called by the corpus (§7).
28 port bugs fixed (§4): 27 found reading the port against the asm / pseudocode before a run, 1 by the shadow
(the armory overlay's halved number); one table-side bug (the text walker across a seam) found by the shadow.

## 1. State mapping

Three patterns, by how the port held its state before:

- **File-statics that mirror retail globals** (the battlefield renderer, the title pages, the armory, the noble
  faces, the trade grid) → one non-static global per retail global in `src/<module>_globals.c`, retail widths from
  the asm (dwords; the shield slots / debug flag bytes; the visible list int16 pairs), declared in the module header
  and aliased at its VA in the table (`A(name, 0xVA)`; build.sh never links `*_globals.c`).  The binders the host
  tests and the standalone use now write those globals.
  - `engine_battlefield_render_globals.c` (52): the renderer's own block DAT_004e5b18..DAT_004e6588 + DAT_00568994
    (no other retail function references it: data_xrefs), and the other subsystems' globals it reads (the sheets
    DAT_00566518 / DAT_00553250 / DAT_00553f6c / DAT_00522f78 / DAT_005530c8, the selection marker DAT_00553f14/18/84,
    the slot shift DAT_00566514 / 0056651c, the debug flag DAT_005bb4a5, DAT_0052af94, DAT_0057c8cc).
  - `engine_title_render_globals.c` (22): the hovered button DAT_00553114, the page DAT_005530f0, the scene flag
    DAT_005651ac, the page-4 gates / shields / name DAT_00553030 / 00553248 / 0057c940 / 0057cb40 / 0057c8cc /
    0057d354 / 00568984 / 0053f1e0, the 12 setup options DAT_0053f288..2b4 (retail's scalar order; the port kept them
    in tab order — `k_tab_slot` is 0x433a70's switch), the misc sheet DAT_005530c8, the country list
    DAT_0053e90c / 0057cacc / 00554018, the two redraw gates DAT_0057c948 / 0055ce94, the dropdown DAT_0055306c /
    00553fb4 / 00553e5c.  The menu-bar latch DAT_004eb260 is `g_gfx_menu_redraw_a` (gfx_globals).
  - `engine_armory_globals.c` (18): the conscription / march state DAT_0056d65c … DAT_0057c8e4, the sheet pointers
    DAT_0056d5b8 / 005533c4 / 0057a0c0 (retail's FUN_004996c8 allocates them; the standalone on first use), the
    overlay strip DAT_004dd340.
  - `engine_noble_faces_globals.c` (2): the action buttons DAT_004dd940, their count DAT_00553ee8.
  - `engine_trade_globals.c` (2): the arrow widgets DAT_004dd838, their count DAT_00553f58.
  - `engine_treasury_globals.c` (2): the seal button DAT_004dd928, its count DAT_00568468.
  - `engine_workforce_hud_globals.c` (8): the icon grid DAT_005540a0, the icon sheet DAT_005530c8, the hovered
    category / cells DAT_0053e9a0 / DAT_00553040, the tab DAT_0053f050, the origin DAT_0057c908, the villani banks
    DAT_0053e918 / DAT_005533c4.
- **Pure in/out ports with a context struct** (the tooltip, the county panels, the noble faces, the trade grid, the
  armory) keep their API (engine.c builds the struct); the table's adapter loads the struct from retail's globals
  before the call and stores it back (store-if-changed) after it.  The tooltip's state is DAT_004eac00 / 004eb268 /
  004eb270 / 004ea830 + the 192×40 backdrop DAT_004e6980; the classifier's inputs the cursor, the panel sub-mode
  DAT_0057a0c4, the county owner byte, the labor / production row lists (dword arrays DAT_0053f690 / DAT_00553fd0 —
  the port had them as bytes).
- **Retail data the port bound by pointer** (fixed tables: the anim offsets DAT_004e3bf0 / 004e4030 / 004e4470, the
  entity records DAT_00554480, the effect chain DAT_0057a100, the marker slots DAT_0052f0b2, the visibility masks
  DAT_0057bfee, the minimap owner / slot tables, the country values DAT_0050a460, the click grid DAT_00542ce0) →
  bound once to retail's addresses in `scr_bind()`.  A sheet retail reads from a pointer variable at call time
  (DAT_004eabec, DAT_005530c8) is read at call time: the title sheet / the faces scratch / the trade icons are
  `g_gfx_current_blob` unless the standalone bound its own.

**The dirty rectangle** is engine_territory's bound state (`engine_territory_dirty_state()`: rh_hooks_ai.c's s_dirty,
synced around every row and every seam), so a mark the port makes and one retail code it calls makes land in the
same place; the battlefield's force-redraw counter DAT_0053e9ac is that state's `below_status_redraw`.
**The localized-text walker** DAT_005c9270: the adapter's `localization_state_t` over retail's blob, loaded at the row,
stored after — and stored / reloaded around every seam into retail (`scr_retail_enter/leave`, a depth count for a
hybrid row nested in a seam): the first run found the title scene's walker one page behind (below, §4).

## 2. Seams (unported callees → retail, bracketed by `scr_retail_enter/leave`)

| port | retail callee | why |
|---|---|---|
| title scene FUN_0041e7e1 | FUN_0041e61d (background + PL8 load), the page renderers 0x41f3e9 (6) / 41f77a (8) / 41fdd6 (9) / 420428 (10) / 420630 (11) / 42051c (12) / 42150b (13) | pages 1 / 2 / 3 / 4 / 5 / 7 are the ports; the rest are not on retail's layout yet (9 the dropdown) or unported |
| setup page FUN_0041f98b / 0041fbcb | 0x410c71 (the preview blit), 0x49caaa (the seat setup: ported in engine_realm_personality.c, not linked) | |
| county panels | FUN_004050c0 (the map recomposite), the flips 0x43a8f2 / 43a939 / 43a922 / 43a846, 0x43eda0 (MP) | |
| battlefield selection marker | 0x46c4a3 (the strategic-map mark) | |
| armory open render | 0x4b11ce, 0x4996c8, 0x452160, 0x4b0ab5 | palette / teardown / input tick |
| workforce HUD render FUN_00412143 | 0x4997bf (the villani buffers DAT_0053e918 / 005533c4) | retail allocates in its own heap; bound via `engine_workforce_hud_bind_retail` |
| io.c / io_path.c / gfx.c / engine_screen_strat.c bridges | FUN_004af8d9, FUN_004af502, FUN_00408fcb (the backdrop loader), FUN_004b0ab5, FUN_00419c78 (the menu bar) | the standalone's shims; not linked |
| error logs | FUN_004afab9 | the sprite renderers' (gfx_bind's twin) and the per-entity draw's "tile map show" |

## 3. Rows

Depth = the verify nesting pass the row is shadowed in; sampled = an `RH_SH_SAMPLE` row's calls retail ran unshadowed (the hot
leaves; `--shadow-all` shadows every one), not in `calls`; the hybrid pass runs every call through the port.

| row | retail | port | depth | calls | sampled | div | scenarios |
|---|---|---|---:|---:|---:|---:|---:|
| `scr_tooltip_tick` | FUN_00476e95 | `engine_tooltip_tick` | 1 | 1232565 |  | 0 | 51/51 |
| `scr_tooltip_arm` | FUN_00477131 | `engine_tooltip_arm` | 2 | 1005929 |  | 0 | 51/51 |
| `scr_tooltip_clear` | FUN_004770b8 | `engine_tooltip_clear` | 2 | 226636 |  | 0 | 47/51 |
| `scr_tooltip_clamp` | FUN_00477249 | `engine_tooltip_clamp` | 1 | 967 |  | 0 | 47/51 |
| `scr_tooltip_classify` | FUN_004772b6 | `engine_tooltip_classify` | 3 | 9539 |  | 0 | 51/51 |
| `scr_tooltip_strategic` | FUN_00477320 | `engine_tooltip_classify_strategic` | 4 | 5369 |  | 0 | 49/51 |
| `scr_tooltip_battle` | FUN_004777aa | `engine_tooltip_classify_battle` | 4 | 516 |  | 0 | 13/51 |
| `scr_backdrop_capture` | FUN_004b3f0a | `engine_workforce_hud_capture_backdrop` | 1 | 11 |  | 0 | 5/51 |
| `scr_backdrop_restore` | FUN_004b3ec0 | `engine_workforce_hud_restore_backdrop` | 1 | 980 |  | 0 | 47/51 |
| `scr_mp_indicator` | FUN_0041f15a | `engine_mp_render_player_indicator` | 1 | 1232565 |  | 0 | 51/51 |
| `scr_mp_status` | FUN_00423b4f | `engine_mp_render_status_line` | 1 | 3688 |  | 0 | 13/51 |
| `scr_noble_render` | FUN_00416cf3 | `engine_noble_faces_render` | 1 | 360 |  | 0 | 37/51 |
| `scr_noble_face` | FUN_004171ee | `engine_noble_faces_draw_face` | 2 | 984 |  | 0 | 37/51 |
| `scr_title_scene` | FUN_0041e7e1 | `engine_title_render_scene` | 1 | 259 |  | 0 | 51/51 |
| `scr_title_main` | FUN_0041ea14 | `engine_title_render_main_menu` | 2 | 112 |  | 0 | 51/51 |
| `scr_title_buttons` | FUN_0041eaa3 | `engine_title_render_buttons` | 1 | 30245 |  | 0 | 51/51 |
| `scr_title_options` | FUN_0041ec8a | `engine_title_render_options` | 2 | 49 |  | 0 | 49/51 |
| `scr_title_options_buttons` | FUN_0041ece6 | `engine_title_render_options_buttons` | 1 | 5194 |  | 0 | 49/51 |
| `scr_title_choose` | FUN_0041ef57 | `engine_title_render_choose_title` | 2 | 49 |  | 0 | 49/51 |
| `scr_title_choose_buttons` | FUN_0041f01f | `engine_title_render_choose_title_buttons` | 1 | 13644 |  | 0 | 49/51 |
| `scr_title_shields` | FUN_0041f1dd | `engine_title_render_choose_title_shields` | 1 | 13644 |  | 0 | 49/51 |
| `scr_title_name_box` | FUN_0041f321 | `engine_title_render_choose_title_name_box` | 1 | 13644 |  | 0 | 49/51 |
| `scr_title_play_now` | FUN_0041f592 | `engine_title_render_play_now` | 2 | 40 |  | 0 | 40/51 |
| `scr_title_play_now_buttons` | FUN_0041f5e8 | `engine_title_render_play_now_buttons` | 1 | 12240 |  | 0 | 40/51 |
| `scr_title_defaults` | FUN_004ae539 | `engine_title_render_setup_apply_defaults` | 1 | 51 |  | 0 | 51/51 |
| `scr_title_clamp_players` | FUN_004ae5e2 | `engine_title_render_setup_clamp_player_count` | 1 | 56 |  | 0 | 51/51 |
| `scr_county_tax` | FUN_0041152f | `engine_county_panel_tax` | 2 | 1892 |  | 0 | 44/51 |
| `scr_county_rations` | FUN_00411b72 | `engine_county_panel_rations` | 2 | 1083 |  | 0 | 44/51 |
| `scr_county_slider` | FUN_00411fde | `engine_county_ration_slider_render` | 1 | 26844 |  | 0 | 44/51 |
| `scr_county_delta` | FUN_0041ac95 | `engine_county_delta_label` | 3 | 5950 |  | 0 | 44/51 |
| `scr_county_year` | FUN_0041a900 | `engine_county_year_number` | 1 | 12189 |  | 0 | 49/51 |
| `scr_county_units` | FUN_0041ab67 | `engine_county_num_units` | 1 | 19837 |  | 0 | 49/51 |
| `scr_county_chrome_panel` | FUN_00409397 | `engine_county_chrome_panel` | 1 | 341 |  | 0 | 27/51 |
| `scr_county_tax_up` | FUN_0043aa83 | `engine_county_tax_step_render` | 1 | 883 |  | 0 | 44/51 |
| `scr_county_tax_down` | FUN_0043ab25 | `engine_county_tax_step_render` | 1 | 645 |  | 0 | 44/51 |
| `scr_county_wanted_up` | FUN_0043a23f | `engine_county_wanted_step_render` | 1 | 103 |  | 0 | 38/51 |
| `scr_county_wanted_down` | FUN_0043a307 | `engine_county_wanted_step_render` | 1 | 133 |  | 0 | 36/51 |
| `scr_bf_frame` | FUN_004bc142 | `engine_battlefield_render_frame` | 1 | 187164 |  | 0 | 13/51 |
| `scr_bf_terrain16` | FUN_004bc8fc | `engine_battlefield_render_terrain_zoom16` | - | 0 |  | 0 | 0/51 |
| `scr_bf_terrain32` | FUN_004bcbdc | `engine_battlefield_render_terrain_zoom32` | 2 | 187164 |  | 0 | 13/51 |
| `scr_bf_overlay16` | FUN_004bcbd1 | `engine_battlefield_render_overlay_zoom16` | - | 0 |  | 0 | 0/51 |
| `scr_bf_overlay32` | FUN_004bd07f | `engine_battlefield_render_overlay_zoom32` | 2 | 187164 |  | 0 | 13/51 |
| `scr_bf_middle` | FUN_004bd355 | `engine_battlefield_render_middle` | 2 | 187164 |  | 0 | 13/51 |
| `scr_bf_build_list` | FUN_004bd938 | `engine_battlefield_render_build_list` | 3 | 187164 |  | 0 | 13/51 |
| `scr_bf_sort_list` | FUN_004bda92 | `engine_battlefield_render_sort_list` | 3 | 187164 |  | 0 | 13/51 |
| `scr_bf_render_list` | FUN_004bdb95 | `engine_battlefield_render_render_list` | 3 | 187164 |  | 0 | 13/51 |
| `scr_bf_entity` | FUN_004bdc31 | `engine_battlefield_render_entity` | 4 | 1800 | 660941 | 0 | 13/51 |
| `scr_bf_type6` | FUN_004be4df | `engine_battlefield_render_entity_type6` | - | 0 |  | 0 | 0/51 |
| `scr_bf_type7` | FUN_004be7be | `engine_battlefield_render_entity_type7` | - | 0 |  | 0 | 0/51 |
| `scr_bf_type9e` | FUN_004beab9 | `engine_battlefield_render_entity_type9e` | - | 0 |  | 0 | 0/51 |
| `scr_bf_marker` | FUN_004bd574 | `engine_battlefield_render_player_marker` | 3 | 18118 |  | 0 | 2/51 |
| `scr_bf_unit_key` | FUN_004bd759 | `engine_battlefield_render_unit_key` | - | 0 |  | 0 | 0/51 |
| `scr_bf_effect` | FUN_004beed4 | `engine_battlefield_render_effect_chain` | 3 | 307 | 151748 | 0 | 5/51 |
| `scr_bf_late` | FUN_004bd233 | `engine_battlefield_render_late` | 2 | 187164 |  | 0 | 13/51 |
| `scr_bf_creature` | FUN_004be070 | `engine_battlefield_render_creature` | 3 | 1721 | 17436 | 0 | 13/51 |
| `scr_bf_selection` | FUN_004be33f | `engine_battlefield_render_selection_marker` | - | 0 |  | 0 | 0/51 |
| `scr_bf_final` | FUN_004bf1f9 | `engine_battlefield_render_final` | 2 | 187164 |  | 0 | 13/51 |
| `scr_bf_minimap` | FUN_004bc51a | `engine_battlefield_render_minimap_strip` | 2 | 187202 |  | 0 | 13/51 |
| `scr_bf_scroll` | FUN_004bc1d1 | `engine_battlefield_render_minimap_scroll` | 1 | 187202 |  | 0 | 13/51 |
| `scr_bf_bind_minimap` | FUN_004bc107 | `engine_battlefield_render_bind_minimap_context` | 1 | 38 |  | 0 | 13/51 |
| `scr_bf_in_grid` | FUN_004bc222 | `engine_battlefield_render_screen_in_grid` | 1 | 2680 | 186644 | 0 | 13/51 |
| `scr_bf_px_col` | FUN_004bc2a5 | `engine_battlefield_render_pixel_to_col` | 1 | 2717 | 29401 | 0 | 13/51 |
| `scr_bf_px_row` | FUN_004bc2c5 | `engine_battlefield_render_pixel_to_row` | 1 | 2717 | 29401 | 0 | 13/51 |
| `scr_bf_col_right` | FUN_004bc2e5 | `engine_battlefield_render_pixel_to_col_right_biased` | 1 | 336 |  | 0 | 13/51 |
| `scr_bf_col_left` | FUN_004bc346 | `engine_battlefield_render_pixel_to_col_left_biased` | 1 | 336 |  | 0 | 13/51 |
| `scr_bf_row_bottom` | FUN_004bc3ac | `engine_battlefield_render_pixel_to_row_bottom_biased` | 1 | 336 |  | 0 | 13/51 |
| `scr_bf_row_top` | FUN_004bc40d | `engine_battlefield_render_pixel_to_row_top_biased` | 1 | 336 |  | 0 | 13/51 |
| `scr_bf_col_delta` | FUN_004bc471 | `engine_battlefield_render_col_delta` | - | 0 |  | 0 | 0/51 |
| `scr_bf_row_delta` | FUN_004bc4bb | `engine_battlefield_render_row_delta` | - | 0 |  | 0 | 0/51 |
| `scr_bf_zoom` | FUN_004bc505 | `engine_battlefield_render_get_zoom` | 1 | 655 | 67 | 0 | 13/51 |
| `scr_trade_render` | FUN_00416308 | `engine_trade_render` | 1 | 243 |  | 0 | 36/51 |
| `scr_trade_content` | FUN_0041635f | `engine_trade_content` | 1 | 59334 |  | 0 | 36/51 |
| `scr_arm_counts` | FUN_004181eb | `engine_armory_counts_render` | 2 | 630 |  | 0 | 44/51 |
| `scr_arm_racks` | FUN_00418426 | `engine_armory_racks` | 2 | 630 |  | 0 | 44/51 |
| `scr_arm_refresh` | FUN_00419243 | `engine_armory_refresh` | 1 | 4828 |  | 0 | 44/51 |
| `scr_arm_march_restore` | FUN_00418fc5 | `engine_armory_march_restore` | 1 | 4828 |  | 0 | 44/51 |
| `scr_arm_march_step` | FUN_004190db | `engine_armory_march_step` | 1 | 4828 |  | 0 | 44/51 |
| `scr_arm_muster` | FUN_004aa90a | `engine_armory_muster_init` | 1 | 586 |  | 0 | 44/51 |
| `scr_arm_unit_noun` | FUN_0041ac3e | `engine_armory_unit_noun` | 1 | 2571 |  | 0 | 41/51 |
| `scr_arm_overlay` | FUN_00418653 | `engine_armory_overlay_render` | 1 | 1843 |  | 0 | 44/51 |
| `scr_arm_conscript` | FUN_00435ebc | `engine_armory_conscript_update` | 1 | 293 |  | 0 | 44/51 |
| `scr_arm_slider` | FUN_00435cef | `engine_armory_slider_click` | 1 | 26693 |  | 0 | 44/51 |
| `scr_arm_render` | FUN_00417ea7 | `engine_armory_render` | 1 | 630 |  | 0 | 44/51 |
| `scr_title_setup` | FUN_0041f6c7 | `engine_title_render_setup_screen` | 2 | 9 |  | 0 | 9/51 |
| `scr_title_tabs` | FUN_0041f86d | `engine_title_render_setup_tabs` | 1 | 1404 |  | 0 | 9/51 |
| `scr_title_country` | FUN_0041f98b | `engine_title_render_setup_country_list` | 1 | 1404 |  | 0 | 9/51 |
| `scr_title_roster` | FUN_0041fbcb | `engine_title_render_setup_roster` | 3 | 9 |  | 0 | 9/51 |
| `scr_county_grid` | FUN_00438ceb | `engine_county_action_grid_click` | 1 | 582435 |  | 0 | 49/51 |
| `scr_county_rations_in` | FUN_0043a379 | `engine_county_rations_click` | 1 | 26583 |  | 0 | 44/51 |
| `scr_county_population` | FUN_004110b1 | `engine_county_panel_population` | 2 | 1 |  | 0 | 1/51 |
| `scr_county_happiness` | FUN_004116fb | `engine_county_panel_happiness` | 2 | 1 |  | 0 | 1/51 |
| `scr_county_graph` | FUN_004156a7 | `engine_county_graph_render` | 3 | 2 |  | 0 | 1/51 |
| `scr_labor_castle` | FUN_0041dedb | `engine_labor_castle_block` | 1 | 20 |  | 0 | 9/51 |
| `scr_title_filelist` | FUN_004148e4 | `engine_title_render_filelist` | - | 0 |  | 0 | 0/51 |
| `scr_title_filelist_content` | FUN_004149ec | `engine_title_render_filelist_content` | 1 | 440 |  | 0 | 1/51 |
| `scr_wf_sprite_index` | FUN_004517ca | `engine_workforce_row_sprite_index` | 2 | 32 |  | 0 | 1/51 |
| `scr_wf_fill_linear` | FUN_004519ca | `engine_workforce_fill_row_linear` | 3 | 4 |  | 0 | 1/51 |
| `scr_wf_fill_dual` | FUN_00451a5d | `engine_workforce_fill_row_dual` | 3 | 4 |  | 0 | 1/51 |
| `scr_wf_compute_row` | FUN_004518a5 | `engine_workforce_compute_row` | 2 | 32 |  | 0 | 1/51 |
| `scr_wf_compute_rows` | FUN_0045161e | `engine_workforce_compute_rows` | 1 | 2 |  | 0 | 1/51 |
| `scr_wf_draw_row` | FUN_004126c9 | `engine_workforce_draw_row` | 2 | 32 |  | 0 | 1/51 |
| `scr_wf_draw_all` | FUN_00412666 | `engine_workforce_draw_all_rows` | 1 | 2 |  | 0 | 1/51 |
| `scr_wf_render` | FUN_00412143 | `engine_workforce_hud_render` | 1 | 2 |  | 0 | 1/51 |
| `scr_treasury` | FUN_00416925 | `engine_treasury_panel` | 1 | 1 |  | 0 | 1/51 |

## 4. What the shadow / the asm found (fixed in the port)

| # | function | what was wrong | found by |
|---|---|---|---|
| 1 | FUN_004b3f0a / 004b3ec0 backdrop walk | the port took (x, y, w, h) and a FULL stride; retail reads the rectangle from the gfx globals (cursor, DAT_005c9258 dwords, DAT_005aeb70 rows, a do-while) and adds a RESIDUAL stride after each row — the workforce HUD's call passed 0xa0 as a full stride (rows 160 bytes apart instead of 640) | asm |
| 2 | FUN_00477131 / 004770b8 / 00476e95 tooltip | the arm / clear did not stage the backdrop rectangle in the gfx globals (the capture took explicit args); the box render staged the fill through set_dest / set_glyph (writing the cursor, the glyph offset and row count retail leaves alone); all three FUN_004527a6(x, y, 0xe, 4, 1) marks skipped | asm |
| 3 | FUN_004777aa battle-screen tooltips | "deferred": mode-2 screens returned tip 0 — ported (tips 23..30) | read |
| 4 | FUN_00477320 strategic tooltips | the labor / production kind lists typed bytes; retail's DAT_0053f690 / DAT_00553fd0 are dwords | asm |
| 5 | FUN_0041f15a MP indicator | drew from its own bound "menu asset"; retail's is the current sheet DAT_004eabec; timeGetTime is read only inside the gate | read |
| 6 | FUN_00416cf3 noble faces | loaded faces.pl8 into a private buffer and rebound the sprite blob; retail loads into the buffer DAT_004eabec points at; the FUN_0045240a mark omitted; the action count / button table file-statics | read |
| 7 | FUN_0041f321 title name box | the latch fallback did not set DAT_005aeb64 = 1; the caret's measure ran on a private font-metrics cache (now the measurers' DAT_005c9288 / DAT_0058fd88) | asm |
| 8 | FUN_0041ea14 … 0041f592 title pages | the panel / shields / name box drew from a sheet bound once; retail reads DAT_004eabec at the call (the background loader refills it) | read |
| 9 | FUN_004ae539 / 004ae5e2 setup defaults | the 12 options lived in tab order; retail's are the scalars DAT_0053f288..2b4 (0x433a70's permutation) | asm |
| 10 | FUN_0041f98b / 0041f86d / 0041f6c7 setup page | the sub-renderers drew unconditionally (their DAT_0057c948 / DAT_0055ce94 gates hoisted to a caller); the tabs' no-shadow per tab; the preview a raw blit of a bound buffer (retail FUN_00410c71); the roster skipped FUN_0049caaa | asm |
| 11 | FUN_0041152f / 00411b72 / 004110b1 county panels | opened with gfx_panel_draw (FUN_00409346, the plain border) instead of FUN_00409397 (the chrome border, mode 0, + the chrome interior) and without FUN_004050c0 (the map recomposite) and FUN_0045240a; the trailing DAT_004eb260 = 1 dropped; the icons through the dispatcher 0x404442 instead of FUN_0040a21a | asm |
| 12 | FUN_0041a900 / 0041ab67 | partial inline replicas (mode 3 only; a fixed noun group) → full ports, shared by the county / trade / armory screens | asm |
| 13 | FUN_0043aa83 / 0043ab25 / 0043a23f / 0043a307 steps | the panel re-render and DAT_005530d0 / DAT_00567d0c / DAT_004eb260 left to the caller → whole-function variants | asm |
| 14 | FUN_0043a379 rations input | DAT_00591550 written only on a change (retail: on every hit) | asm |
| 15 | FUN_004bcbdc / 004bc8fc / 004bd07f / 004bd233 / 004bd355 battlefield passes | the cell walk kept its cursor in locals and set DAT_00591524 / DAT_00591528 only before a blit: retail's walk IS the cursor (its end value is state); DAT_005c9288 staged only when a sheet matched (retail: before the select) | asm |
| 16 | FUN_004be4df type-6 pre-call | drew a port-only bound sheet; retail's is the record's +0x04 sheet | asm |
| 17 | FUN_004beed4 effect chain | the camera offset used the frame camera DAT_004e657c / 80; retail's DAT_0055ce70 / DAT_005678a0 | asm |
| 18 | FUN_004bf1f9 debug labels | summed from the record's +0 and sign-extended; retail sums from +0x12 and the sum is an unsigned byte | asm |
| 19 | FUN_004bc020 hex-grid binder | set the row wrap to 0 and skipped DAT_004e6550 (retail: wrap = stride·(row_stride − cols), zoom²) | read |
| 20 | FUN_004bdc31 per-entity draw | the no-sheet error log FUN_004afab9("ERR: tile map show", 0, type + 1) replaced by a counter; the force-redraw counter a private static (retail DAT_0053e9ac) | read |
| 21 | FUN_0041635f / 00416308 trade | the ale gift's delta label (FUN_0041ac95) "deferred"; the two FUN_004527a6 marks, the FUN_0045240a mark and DAT_004eb260 omitted; a private icon sheet | asm |
| 22 | FUN_004181eb / 00418653 / 00419243 / 004190db / 004aa90a armory | the tally skipped the hired mercenary contingent and its 8×6 marks; the overlay opened with FUN_00409346 (not 00409397) and without FUN_0045240a / DAT_004eb260; the torch / march marks skipped; the muster init left DAT_0057c8e4; the slider left DAT_0057d340 = 2 to the caller | asm |
| 23 | FUN_00417ea7 armory open render | reordered (the seal after the sheet loads, the palette as one load at the end), private sheets, the strips in a private buffer (retail: DAT_004eabec), no FUN_004b11ce / 004996c8 / 00452160 / 004b0ab5 → retail's order with the retail-only steps as bound hooks | asm |
| 24 | FUN_00418653 armory overlay | the change-cost line's second number was the cost halved; retail's is the army SIZE halved (dword_568DC8 = the unit +8, not +0xc) | **shadow**: `0058fd78 r…9f/p…a0 px(437,251)` (one glyph more), 3/34 calls in 2 scenarios |
| 25 | FUN_0041152f / 00411b72 / 004110b1 / 004116fb county panels | read the county record before the map recomposite FUN_004050c0, which can re-pick the current county DAT_0057c93c (its last statement) — retail reads it afresh after | asm |
| 26 | FUN_004148e4 / 004149ec save-file list | the name field drawn only when non-empty; the latch fallback without DAT_005aeb64 = 1; the highlight staged through set_glyph (zeroing the glyph offset / rows); the FUN_0045240a mark and DAT_004eb260 omitted; the status line always sub 2 (retail: 4 while DAT_00591548, 3 off the title / 0x35 screens); no in-game (16, 144 / chrome) variant | asm |
| 27 | FUN_00451a5d / 004126c9 / 00412143 workforce HUD | the dual fill's "indicator" cells went to a private array; retail writes DAT_005540aa = the grid ten cells on (they overwrite the row's cells 10..24 and show); the draw used the opaque blit FUN_0040a567 (retail FUN_0040a21a) and ignored the hovered-category highlight DAT_0053e9a0 / DAT_00553040; the render skipped the standings head (villtops.pl8 + three lines), the FUN_0045270a mark, drew the feature icons opaque, the seal with no sheet, and kept private banks / scratch (retail: DAT_0053e918 / DAT_005533c4 allocated by 0x4997bf, the vill sheets + the backdrop in DAT_004eabec's buffer) | asm |
| 28 | FUN_00416925 treasury | the FUN_0045240a mark omitted; the widget table / count file-statics (DAT_004dd928 / DAT_00568468) | asm |

The table-side bug the first shadow found (not port logic): **the title scene's walker** — `scr_title_scene` diverged
at f865 in the nine custom-game scenarios (`005c9270 r00598706/p00592047`): page 7 rendered through a seam into
retail, which advanced DAT_005c9270, then the adapter's `loc_store` wrote its stale copy back.  The seams now store
the walker before and reload it after (`scr_retail_enter/leave`).

## 5. Quirks (retail ground truth) — for engine-quirks.md

- FUN_004beed4 (the effect chain): a link whose descriptor fails the `0 < Offset <= 310000` gate returns WITHOUT
  restoring the cursor — an earlier link's position stays in DAT_00591524 / DAT_00591528.
- FUN_004be33f (the selection marker) never restores the cursor either; FUN_004bdc31 / 4be4df / 4be7be leave their
  two centering locals uninitialized at a zoom other than 0x10 / 0x20 (the stack residue).
- FUN_004bf1f9 (the debug labels) sums 0x19e record bytes from +0x12 through 0x440089, which returns the sum in AL
  with EAX's upper bytes zero (the loop's last `mov eax, count` left 0): an UNSIGNED byte, 0..255.
- FUN_00477131 / 004770b8 (the tooltip) stage the backdrop as a glyph (cursor = the box, DAT_005c9258 = 0x30 dwords,
  DAT_005aeb70 = 0x28 rows) and pass FUN_004b3f0a / 004b3ec0 the RESIDUAL stride 0x1c0 (added after the row's
  `rep movsd`), not the pitch; the row count is a do-while.
- FUN_00477320's two dynamic row lists (DAT_0053f690 labor kinds, DAT_00553fd0 production kinds) are dword arrays.
- The armory's interior-sheet name is `&DAT_004d2c88[16 * strat+0x0a]`, unclamped.
- FUN_00451a5d (the workforce dual fill, both counts < 13) writes its "indicator" cells to DAT_005540aa[25·cat + k]
  — the icon grid DAT_005540a0 ten cells on: they overwrite the row's cells 10..24, and the draw shows them.
- FUN_00418653 (the armory change overlay): "N … half N …" — the second number is the army SIZE halved, not the
  cost.

## 6. Shared files

- `tools/rhost/rh_hooks.h`: `T(RH_HOOKS_GFX) \` + a new line `T(RH_HOOKS_SCREENS)`.
- `tools/rhost/build.sh`: two PORT_OBJS lines, `engine_tooltip engine_workforce_hud engine_mp_render engine_noble_faces
  engine_title_render engine_county_panels \` and `engine_battlefield_render engine_trade engine_armory engine_labor
  engine_treasury \`.
- `tests/Makefile`: `engine_title_render_globals.c`, `engine_battlefield_render_globals.c`,
  `engine_workforce_hud_globals.c` in SRCS_UNDER_TEST.
- Bridges defined in the table (a merge could collide with another batch's): `io_read_file_at`,
  `io_path_chdir_base`, `engine_map_scene_tick`, `engine_screen_strat_render_menubar`, `gfx_load_background_image`,
  `gfx_load_palette`, `gfx_lock_canvas`, `gfx_unlock_canvas`, and the alias `g_sim_mp_active` (0x553030).

## 7. What remains

- **Never reached by the corpus** (rows in the table, 0 calls; read against the asm): the zoom-16 battlefield
  passes FUN_004bc8fc / 004bcbd1 (every corpus battle runs at zoom 32), the type-6 / 7 / 9e entity leaves, the
  unit-key letters FUN_004bd759, the selection marker FUN_004be33f, the cell deltas FUN_004bc471 / 004bc4bb.  They
  need a battle that uses them (zoom 16, siege engines / catapults, a selection) — a human trace or a poke probe.
  And the title file list's wrapper FUN_004148e4: its one caller 0x41ef42 is not reached (its content FUN_004149ec is:
  440 calls, 1 scenario).
- **No row (the generic body copies 10 argument slots)**: FUN_004bc020, the hex-grid binder (11 arguments) — ported
  and read against the asm; `rh_port.h` would need 12 slots.
- **Not converted**: the file list's grid pick FUN_0043400b (1 scenario: it re-inits the title editor FUN_00402009
  on retail's DAT_005cd550 — the editor is not on retail's layout; `rh_hooks_gfx.c`'s caret loads one char of it),
  the option dropdown FUN_0041fdd6 (page 9; 0 scenarios: the seam), the setup page's input arms (0x4337b4 / 433905 /
  433998 / 433ba2 / 433ce4 / 434346 / 432fab; 0), the workforce allocator FUN_004997bf (a seam: the port mallocs,
  retail allocates in its own heap), the armory assign overlay FUN_004184c6 / 00418e2d and the march trigger
  FUN_004aabd8 (0), the labor screen FUN_00412b33 and its panels other than the castle block (0), the rankings
  FUN_0041593b (0), the MP setup page FUN_0041f77a and the other setup-flow pages (seams).
- **Per-frame refresh arms** (FUN_004ba26e's cases: the noble faces' / trade's / county panels' button strips) are
  arms of one retail function, not functions — they get a row when FUN_004ba26e is ported whole.
- **engine_bf_input.c** still calls retail for the four quantizers (0x4bc2e5 / 4bc346 / 4bc3ac / 4bc40d) — the ports
  are linked and verified now; that file is the bf batch's.
