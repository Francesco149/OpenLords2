# OpenLords2 — port ledger

> **DERIVED FILE** — regenerate with `python3 tools/gen_port_ledger.py`.
> See `STATUS.md` for the headline.

Per-engine-function port status, derived from `functions.csv` (universe)
and `FUN_<va>` provenance references in `src/` (each port carries a
`Port of FUN_<va>` comment). A function is **tested** when its src module
has a matching `tests/test_<stem>.c`. This is the answer to
*"is FUN_x done?"* at a glance.

## Summary

- non-thunk engine functions: **2447** (of 2452 incl. thunks)
- touched: **1960** (80.1%) — tested 788, ported 1172
- code bytes touched: **85.6%** (717,933 / 838,629 B)
- unported: **487**
- orphan refs (in src/, not in function table): 7

## tested (788) — ported + host unit suite

| VA | name | size | src |
|----|------|-----:|-----|
| 0x401000 | FUN_00401000 | 133 | rl_ui4.c, strutil.h |
| 0x401085 | FUN_00401085 | 85 | rl_ui4.c, strutil.h |
| 0x4010da | FUN_004010da | 92 | rl_ui4.c, strutil.c, strutil.h |
| 0x401136 | FUN_00401136 | 64 | engine_diplomacy_action_model.c, engine_event_queue.c, engine_event_queue.h (+6) |
| 0x401176 | FUN_00401176 | 58 | strutil.h |
| 0x4011b0 | FUN_004011b0 | 58 | rl_ui4.c, strutil.h |
| 0x4011ea | FUN_004011ea | 92 | rl_ui4.c, strutil.h |
| 0x401246 | FUN_00401246 | 92 | rl_ui4.c, strutil.h |
| 0x4012a2 | FUN_004012a2 | 97 | rl_ui4.c, strutil.h |
| 0x401303 | FUN_00401303 | 48 | rl_ui4.c, strutil.h |
| 0x401333 | FUN_00401333 | 49 | rl_ui4.c, strutil.h |
| 0x401364 | FUN_00401364 | 88 | strutil.h |
| 0x4013bc | FUN_004013bc | 89 | rl_ui4.c, strutil.h |
| 0x401415 | FUN_00401415 | 219 | strutil.h |
| 0x4014f0 | FUN_004014f0 | 201 | font_metrics.h, gfx_text.c, rl_ui4.c |
| 0x4015b9 | FUN_004015b9 | 173 | engine_title_render.c, font_metrics.c, font_metrics.h (+5) |
| 0x401666 | FUN_00401666 | 141 | rl_ui4.c, strutil.h |
| 0x4016f3 | FUN_004016f3 | 204 | localization.c, localization.h |
| 0x4017bf | FUN_004017bf | 187 | localization.c, localization.h, rl_ui4.c |
| 0x40187a | FUN_0040187a | 93 | localization.c, localization.h, rl_ui4.c |
| 0x4018d7 | FUN_004018d7 | 173 | engine_hud_menubar.c, localization.c, localization.h (+1) |
| 0x401984 | FUN_00401984 | 156 | rl_ui4.c, text_input.c, text_input.h |
| 0x401a20 | FUN_00401a20 | 543 | main.c, rl_ui4.c, text_input.h |
| 0x401c3f | FUN_00401c3f | 28 | text_input.h |
| 0x401c70 | FUN_00401c70 | 35 | main.c, rl_ui4.c, text_input.h |
| 0x401c93 | FUN_00401c93 | 16 | main.c, rl_ui4.c, text_input.h |
| 0x401ca3 | FUN_00401ca3 | 25 | main.c, rl_ui4.c, text_input.h |
| 0x401cbc | FUN_00401cbc | 30 | main.c, rl_ui4.c, text_input.h |
| 0x401cda | FUN_00401cda | 34 | main.c, rl_ui4.c, text_input.h |
| 0x401cfc | FUN_00401cfc | 21 | main.c, rl_ui4.c, text_input.h |
| 0x401d11 | FUN_00401d11 | 21 | main.c, rl_ui4.c, text_input.h |
| 0x401d26 | FUN_00401d26 | 162 | rl_ui4.c, text_input.h |
| 0x401dc8 | FUN_00401dc8 | 64 | main.c, rl_ui4.c, text_input.h |
| 0x401e49 | FUN_00401e49 | 135 | text_input.h |
| 0x401ed0 | FUN_00401ed0 | 121 | text_input.h |
| 0x401f49 | FUN_00401f49 | 70 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+2) |
| 0x401f8f | FUN_00401f8f | 122 | font_metrics.c, font_metrics.h, rl_ui4.c (+2) |
| 0x402009 | FUN_00402009 | 165 | engine_title_render.c, main.c, rl_ui4.c (+1) |
| 0x4020ae | FUN_004020ae | 94 | engine_diplomacy_action_model.c, rl_link_logic.c, rl_ui4.c (+2) |
| 0x40210c | FUN_0040210c | 90 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+2) |
| 0x4022bd | FUN_004022bd | 394 | gfx_text_label.c, gfx_text_label.h |
| 0x402447 | FUN_00402447 | 400 | gfx_text_label.c, gfx_text_label.h |
| 0x4025d7 | FUN_004025d7 | 96 | gfx_text.c, gfx_text.h |
| 0x402637 | FUN_00402637 | 989 | engine_county_panels.c, engine_noble_faces.c, engine_treasury.c (+4) |
| 0x402a14 | FUN_00402a14 | 514 | gfx_dispatch.c, gfx_dispatch.h, gfx_glyph.h (+1) |
| 0x402c16 | FUN_00402c16 | 72 | gfx_text.c, gfx_text.h |
| 0x402c5e | FUN_00402c5e | 217 | engine_title_render.c, gfx_wrap.c, gfx_wrap.h |
| 0x402d37 | FUN_00402d37 | 213 | engine_confirm_dialog.h, engine_diplomacy_action.c, engine_hud_menubar.c (+6) |
| 0x402e0c | FUN_00402e0c | 344 | engine_county_panels.c, gfx_text_label.c, gfx_text_label.h |
| 0x402f64 | FUN_00402f64 | 177 | engine_sendsupplies.c, gfx_text_label.c, gfx_text_label.h |
| 0x403015 | FUN_00403015 | 177 | gfx_text_label.c, gfx_text_label.h |
| 0x4030c6 | FUN_004030c6 | 202 | gfx_text_label.c, gfx_text_label.h |
| 0x403190 | FUN_00403190 | 254 | gfx_text_label.c, gfx_text_label.h |
| 0x40328e | FUN_0040328e | 673 | engine_noble_faces.c, engine_sendsupplies.c, font_metrics.h (+2) |
| 0x40352f | FUN_0040352f | 458 | font_metrics.h, gfx_wrap.c, gfx_wrap.h |
| 0x4036f9 | FUN_004036f9 | 235 | font_metrics.h, gfx_wrap.c, rl_ui4.c |
| 0x4037ee | FUN_004037ee | 347 | gfx_line.c, gfx_line.h, rl_ui4.c |
| 0x403949 | FUN_00403949 | 126 | gfx_line.c, gfx_line.h |
| 0x4039c7 | FUN_004039c7 | 101 | engine_minimap.c, gfx_paint.h |
| 0x403a2c | FUN_00403a2c | 99 | gfx_paint.h |
| 0x403a8f | FUN_00403a8f | 442 | engine_rankings.c, engine_rankings.h, gfx_line.c (+1) |
| 0x403c49 | FUN_00403c49 | 171 | gfx_line.c, gfx_line.h, gfx_paint.h |
| 0x403cf4 | FUN_00403cf4 | 247 | engine_labor.c, engine_tooltip.c, gfx_line.c (+1) |
| 0x403deb | FUN_00403deb | 249 | engine_county_panels.c, engine_sendsupplies.c, engine_terrain_info.c (+4) |
| 0x403ee4 | FUN_00403ee4 | 249 | gfx_line.c, gfx_line.h |
| 0x403fdd | FUN_00403fdd | 249 | engine_hud_menubar.c, engine_hud_menubar.h, engine_trade.c (+2) |
| 0x40437d | FUN_0040437d | 197 | gfx_caret.c, gfx_caret.h, gfx_line.c (+1) |
| 0x404442 | FUN_00404442 | 256 | engine_hud_menubar.c, gfx_dispatch.c, gfx_dispatch.h |
| 0x404547 | FUN_00404547 | 256 | gfx_dispatch.c, gfx_dispatch.h |
| 0x40464c | FUN_0040464c | 303 | engine_battlefield_render.c, engine_battlefield_render.h, gfx_dispatch.c (+3) |
| 0x40477b | FUN_0040477b | 291 | engine_battlefield_render.c, engine_battlefield_render.h, gfx_dispatch.c (+2) |
| 0x40489e | FUN_0040489e | 111 | engine_tick_cadence.h, rl_ui4.c |
| 0x40490d | FUN_0040490d | 313 | engine_tick_cadence.h, rl_ui4.c |
| 0x404a46 | FUN_00404a46 | 230 | engine.c, engine.h, engine_bf.h (+4) |
| 0x404b2c | FUN_00404b2c | 109 | engine.h, engine_rng.c, engine_territory.c (+1) |
| 0x404d6b | FUN_00404d6b | 43 | engine_armory.c, engine_army_ai.c, engine_input.c (+6) |
| 0x404d96 | FUN_00404d96 | 43 | engine_math.h, rl_ui4.c |
| 0x404dc1 | FUN_00404dc1 | 66 | engine_army_state.c, engine_combat.c, engine_input.c (+6) |
| 0x404e03 | FUN_00404e03 | 72 | engine_army_economy.c, engine_math.h, rl_ui4.c |
| 0x404e4b | FUN_00404e4b | 98 | engine_math.h, rl_ui4.c |
| 0x404ead | FUN_00404ead | 159 | engine_army_ai.c, engine_combat.c, engine_math.h (+2) |
| 0x404f4c | FUN_00404f4c | 180 | engine_army_ai.c, engine_path.h, engine_territory.c (+1) |
| 0x4050c0 | FUN_004050c0 | 53 | engine.c, engine_army_select.h, engine_county_panels.c (+15) |
| 0x4050f6 | FUN_004050f6 | 376 | engine_map_scene.h, main.c |
| 0x40526e | FUN_0040526e | 537 | engine_map_scene.c, engine_map_scene.h, engine_map_terrain.c (+2) |
| 0x405487 | FUN_00405487 | 379 | engine_map_render.c, engine_map_render.h, engine_map_scene.c (+4) |
| 0x405602 | FUN_00405602 | 608 | engine_map_scene.c, engine_map_scene.h, engine_map_terrain.h (+3) |
| 0x405862 | FUN_00405862 | 333 | engine_map_render.c, engine_map_render.h |
| 0x4059af | FUN_004059af | 314 | engine_map_render.c, engine_map_render.h |
| 0x405ae9 | FUN_00405ae9 | 326 | engine_map_scene.h, engine_map_terrain.c, engine_map_terrain.h |
| 0x405c2f | FUN_00405c2f | 646 | engine_map_scene.h, engine_map_terrain.c, engine_map_terrain.h |
| 0x405eb5 | FUN_00405eb5 | 247 | engine_overlay_render.c, engine_overlay_render.h |
| 0x405fac | FUN_00405fac | 497 | engine_overlay_render.c, engine_overlay_render.h |
| 0x4071a0 | FUN_004071a0 | 3179 | engine.c, engine_anim_cycle.c, engine_overlay_render.c (+1) |
| 0x407f82 | FUN_00407f82 | 548 | engine_overlay_render.c, engine_overlay_render.h |
| 0x4081a6 | FUN_004081a6 | 658 | engine_overlay_render.c, engine_overlay_render.h |
| 0x408438 | FUN_00408438 | 2072 | engine_map_render.c, engine_map_render.h |
| 0x408c50 | FUN_00408c50 | 390 | engine_overlay_render.c, engine_overlay_render.h |
| 0x408fb0 | FUN_00408fb0 | 27 | engine_hud_menubar.h, gfx_globals.c, gfx_globals.h (+3) |
| 0x409346 | FUN_00409346 | 81 | gfx_panel.c, gfx_panel.h |
| 0x409397 | FUN_00409397 | 73 | engine_armory.c, engine_county_panels.c, engine_county_panels.h (+3) |
| 0x4093e0 | FUN_004093e0 | 73 | engine_diplomacy_action.c, engine_noble_faces.c, engine_sendsupplies.c (+4) |
| 0x40946d | FUN_0040946d | 856 | gfx_panel.c, gfx_panel.h |
| 0x4097c5 | FUN_004097c5 | 367 | engine_title_render.c, gfx_panel.c, gfx_panel.h |
| 0x409934 | FUN_00409934 | 863 | engine_county_panels.c, gfx_panel.c, gfx_panel.h |
| 0x409c93 | FUN_00409c93 | 374 | engine_armory.c, engine_county_panels.c, engine_sendsupplies.c (+4) |
| 0x409e09 | FUN_00409e09 | 368 | engine_hud_menubar.c, engine_hud_menubar.h |
| 0x40a01d | FUN_0040a01d | 261 | engine_minimap.c, engine_sendsupplies.c, gfx_dispatch.h (+2) |
| 0x40a21a | FUN_0040a21a | 433 | engine_county_panels.c, engine_hud_menubar.c, engine_hud_menubar.h (+6) |
| 0x40a3d0 | FUN_0040a3d0 | 402 | engine_armory.c, engine_terrain_info.c, gfx_sprite_render.c (+2) |
| 0x40a567 | FUN_0040a567 | 283 | engine_screen_strat.c, engine_screen_strat.h, gfx_glyph.c (+5) |
| 0x40a682 | FUN_0040a682 | 391 | engine_county_panels.c, engine_labor.c, engine_rankings.h (+4) |
| 0x40a80e | FUN_0040a80e | 413 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a9b0 | FUN_0040a9b0 | 439 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40acce | FUN_0040acce | 324 | engine_title_render.c, gfx_caret.c, gfx_caret.h (+2) |
| 0x40c5b0 | FUN_0040c5b0 | 373 | engine_hud_menubar.c, engine_hud_menubar.h, gfx_globals.h |
| 0x40ccfa | FUN_0040ccfa | 94 | engine.c, engine_confirm_dialog.c, engine_confirm_dialog.h (+1) |
| 0x40cfd2 | FUN_0040cfd2 | 490 | engine.c, engine_button.c, engine_button.h (+12) |
| 0x40d1bc | FUN_0040d1bc | 181 | engine_armory.c, engine_diplomacy_action.c, engine_labor.c (+8) |
| 0x40da1e | FUN_0040da1e | 869 | engine.c, engine_confirm_dialog.h, engine_county_panels.h (+5) |
| 0x40e3ee | FUN_0040e3ee | 505 | engine.c, engine_armory.c, engine_armory.h (+6) |
| 0x40e5e7 | FUN_0040e5e7 | 153 | engine_menu_input.c, engine_menu_input.h, rl_ui4.c |
| 0x40e6f2 | FUN_0040e6f2 | 111 | engine.c, engine_army_select.c, engine_army_select.h (+2) |
| 0x40e7e4 | FUN_0040e7e4 | 87 | engine.c, engine_ingame_input.c, engine_ingame_input.h (+1) |
| 0x40e890 | FUN_0040e890 | 43 | engine.c, engine.h, main.c (+1) |
| 0x40e8bb | FUN_0040e8bb | 240 | engine.c, engine.h, main.c (+1) |
| 0x40e9ab | FUN_0040e9ab | 641 | config.h, main.c |
| 0x40ecc4 | FUN_0040ecc4 | 1099 | config.c, config.h, rl_ui4.c (+1) |
| 0x40f1a0 | FUN_0040f1a0 | 1117 | engine.c, engine_armory.h, engine_diplomacy_action.h (+8) |
| 0x40f5fd | FUN_0040f5fd | 470 | engine.c, engine_hud_panel.h, engine_map_scene.h (+8) |
| 0x40f7d3 | FUN_0040f7d3 | 1774 | engine_hud_panel.c, engine_hud_panel.h, engine_mapview_globals.h (+2) |
| 0x40fec1 | FUN_0040fec1 | 494 | engine_hud_panel.c, engine_hud_panel.h, engine_mapview_globals.h |
| 0x4100af | FUN_004100af | 395 | engine_hud_panel.c, engine_hud_panel.h |
| 0x41023a | FUN_0041023a | 395 | engine_hud_panel.c |
| 0x4103c5 | FUN_004103c5 | 317 | engine_hud_panel.c |
| 0x410502 | FUN_00410502 | 150 | engine_hud_panel.c, engine_hud_panel.h |
| 0x410598 | FUN_00410598 | 150 | engine_hud_panel.c |
| 0x41062e | FUN_0041062e | 150 | engine_hud_panel.c |
| 0x4106c4 | FUN_004106c4 | 269 | engine_hud_panel.c |
| 0x4107d1 | FUN_004107d1 | 652 | engine_hud_panel.c |
| 0x410aa9 | FUN_00410aa9 | 456 | engine_minimap.c, engine_minimap.h, engine_screen_strat.c (+1) |
| 0x410cbd | FUN_00410cbd | 654 | engine_minimap.c, engine_minimap.h, engine_sendsupplies.c |
| 0x4110b1 | FUN_004110b1 | 1150 | engine_county_panels.c, engine_county_panels.h |
| 0x41152f | FUN_0041152f | 460 | engine_county_panels.c, engine_county_panels.h |
| 0x4116fb | FUN_004116fb | 1143 | engine_county_panels.c, engine_county_panels.h |
| 0x411b72 | FUN_00411b72 | 1132 | engine_county_panels.c, engine_county_panels.h |
| 0x411fde | FUN_00411fde | 258 | engine.c, engine_county_panels.h |
| 0x412143 | FUN_00412143 | 734 | engine.c, engine_army_select.h, engine_workforce_hud.h |
| 0x412666 | FUN_00412666 | 99 | engine_workforce_hud.h |
| 0x4126c9 | FUN_004126c9 | 204 | engine_workforce_hud.h |
| 0x413155 | FUN_00413155 | 977 | engine_labor.c, engine_labor.h, engine_screen_strat.c (+2) |
| 0x413590 | FUN_00413590 | 1440 | engine_army_economy.h, engine_labor.c, engine_labor.h (+1) |
| 0x4148e4 | FUN_004148e4 | 264 | engine_title_render.c, engine_title_render.h, main.c |
| 0x4149ec | FUN_004149ec | 1050 | engine_title_render.c, engine_title_render.h, gfx_sprite.c (+2) |
| 0x4156a7 | FUN_004156a7 | 627 | engine_county_panels.c, engine_county_panels.h, gfx.c |
| 0x4180f6 | FUN_004180f6 | 245 | engine_armory.h, engine_ingame_input.c, rl_ui4.c |
| 0x419c78 | FUN_00419c78 | 682 | engine_hud_menubar.c, engine_hud_menubar.h, engine_labor.c (+4) |
| 0x41a734 | FUN_0041a734 | 272 | engine_hud_panel.c, engine_hud_panel.h, engine_mapview_globals.h (+2) |
| 0x41a900 | FUN_0041a900 | 615 | engine_county_panels.c, engine_county_panels.h, engine_hud_menubar.c (+1) |
| 0x41ab67 | FUN_0041ab67 | 215 | engine_armory.c, engine_county_panels.c, engine_county_panels.h (+7) |
| 0x41ac3e | FUN_0041ac3e | 87 | engine_armory.c, engine_armory.h, engine_hud_panel.c |
| 0x41ac95 | FUN_0041ac95 | 200 | engine_county_panels.h, engine_trade.c |
| 0x41b032 | FUN_0041b032 | 79 | engine.c, engine_army_select.c, engine_mapview_globals.h (+2) |
| 0x41befe | FUN_0041befe | 778 | engine_terrain_info.c, engine_terrain_info.h |
| 0x41c208 | FUN_0041c208 | 1934 | engine_terrain_info.c, engine_terrain_info.h, gfx.c |
| 0x41c996 | FUN_0041c996 | 420 | engine_terrain_info.c, engine_terrain_info.h, rl_render.c |
| 0x41e61d | FUN_0041e61d | 452 | engine_title_render.c, engine_title_render.h, gfx.c (+3) |
| 0x41e7e1 | FUN_0041e7e1 | 385 | engine_title_render.c, engine_title_render.h, main.c |
| 0x41ea14 | FUN_0041ea14 | 143 | engine_title_render.c, engine_title_render.h |
| 0x41eaa3 | FUN_0041eaa3 | 487 | engine_title_render.c, engine_title_render.h |
| 0x41ec8a | FUN_0041ec8a | 92 | engine_title_render.c, engine_title_render.h |
| 0x41ece6 | FUN_0041ece6 | 604 | engine_title_render.c, engine_title_render.h |
| 0x41ef57 | FUN_0041ef57 | 200 | engine_title_render.c, engine_title_render.h, main.c |
| 0x41f01f | FUN_0041f01f | 315 | engine_title_render.c, engine_title_render.h |
| 0x41f15a | FUN_0041f15a | 131 | engine_mp_render.c, engine_mp_render.h, rl_link_screens.c |
| 0x41f1dd | FUN_0041f1dd | 324 | engine_title_render.c, engine_title_render.h |
| 0x41f321 | FUN_0041f321 | 200 | engine_title_render.c, engine_title_render.h, gfx_caret.h (+4) |
| 0x41f592 | FUN_0041f592 | 86 | engine_title_render.c, engine_title_render.h |
| 0x41f5e8 | FUN_0041f5e8 | 223 | engine_title_render.c, engine_title_render.h |
| 0x41f6c7 | FUN_0041f6c7 | 179 | engine_title_render.c, engine_title_render.h, rl_link_screens.c |
| 0x41f77a | FUN_0041f77a | 243 | engine_title_render.c, rl_mpnet.c |
| 0x41f86d | FUN_0041f86d | 286 | engine_title_render.c, engine_title_render.h, rl_link_screens.c |
| 0x41f98b | FUN_0041f98b | 576 | engine_title_render.c, engine_title_render.h |
| 0x41fbcb | FUN_0041fbcb | 523 | engine_title_render.c, engine_title_render.h |
| 0x41fdd6 | FUN_0041fdd6 | 415 | engine_title_render.c, engine_title_render.h, main.c (+1) |
| 0x422c12 | FUN_00422c12 | 272 | engine_combat.c, engine_combat.h |
| 0x423b4f | FUN_00423b4f | 85 | engine_mp_render.c, engine_mp_render.h |
| 0x426d11 | FUN_00426d11 | 196 | audio.c, engine_combat.c, engine_combat.h (+1) |
| 0x427d19 | FUN_00427d19 | 89 | audio.c, audio.h, engine.c (+4) |
| 0x427ed0 | FUN_00427ed0 | 311 | engine_territory.c, game_init.c, game_init.h (+1) |
| 0x428007 | FUN_00428007 | 113 | engine_army_ai.c, engine_army_state.c, engine_army_state.h (+4) |
| 0x428078 | FUN_00428078 | 113 | engine_army_ai.c, engine_combat.c, game_init.c (+2) |
| 0x4280e9 | FUN_004280e9 | 904 | engine.c, engine_army_state.c, engine_army_state.h (+1) |
| 0x428471 | FUN_00428471 | 11 | engine_turn.c, engine_turn.h, rl_ui4.c |
| 0x42847c | FUN_0042847c | 82 | game_init.c, game_init.h, rl_ui4.c |
| 0x4284ce | FUN_004284ce | 1858 | engine_army_ai.c, engine_army_ai.h, engine_trade_model.c |
| 0x428c42 | FUN_00428c42 | 365 | engine_army_ai.c, engine_army_ai.h |
| 0x4290d0 | FUN_004290d0 | 131 | engine_category_buckets.c, engine_category_buckets.h, engine_territory.c (+2) |
| 0x429153 | FUN_00429153 | 96 | engine_category_buckets.h, engine_territory.c, rl_ui4.c |
| 0x4291b3 | FUN_004291b3 | 171 | engine_category_buckets.h, engine_territory.c, rl_ui4.c |
| 0x42925e | FUN_0042925e | 81 | engine_category_buckets.c, engine_category_buckets.h, rl_ui4.c |
| 0x4292af | FUN_004292af | 361 | engine_army_ai.c, engine_army_ai.h, engine_army_state.c (+3) |
| 0x429418 | FUN_00429418 | 669 | engine.c, engine_army_state.c, engine_army_state.h |
| 0x4296b5 | FUN_004296b5 | 319 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x4298c1 | FUN_004298c1 | 604 | engine_map_init.c, engine_map_init.h, game_init.c (+2) |
| 0x429b1d | FUN_00429b1d | 135 | engine.c, engine_camera.c, engine_camera.h (+2) |
| 0x429ba4 | FUN_00429ba4 | 878 | engine.c, engine_camera.c, engine_camera.h (+4) |
| 0x42a15f | FUN_0042a15f | 206 | engine_camera.c, engine_camera.h, rl_ui4.c |
| 0x42dfe2 | FUN_0042dfe2 | 126 | main.c, rl_quit.c |
| 0x42ec91 | FUN_0042ec91 | 1207 | engine_mapview_globals.h, gfx.c, gfx.h (+3) |
| 0x42f1df | FUN_0042f1df | 295 | gfx.c, main.c, rl_ui4.c |
| 0x42ff10 | FUN_0042ff10 | 8140 | engine.c, engine_armory.c, engine_army_move_confirm.h (+9) |
| 0x431f59 | FUN_00431f59 | 712 | engine_camera.h, rl_ctl.c |
| 0x432221 | FUN_00432221 | 477 | engine.c, engine_camera.h, engine_ingame_input.h (+1) |
| 0x432640 | FUN_00432640 | 262 | engine_ingame_input.c, engine_ingame_input.h, rl_ctl.c |
| 0x432746 | FUN_00432746 | 69 | engine_territory.c, engine_territory.h, game_init.c (+1) |
| 0x43278b | FUN_0043278b | 223 | engine.c, engine_terrain_info.c, engine_terrain_info.h (+3) |
| 0x432967 | FUN_00432967 | 61 | engine.c, engine_ingame_input.h, rl_ctl.c |
| 0x4329ec | FUN_004329ec | 281 | engine_load_pump.c, engine_load_pump.h, engine_menu_input.c (+5) |
| 0x432b05 | FUN_00432b05 | 451 | engine_menu_input.c, engine_menu_input.h, rl_ctl.c |
| 0x432cc8 | FUN_00432cc8 | 526 | engine_menu_input.c, main.c, rl_ctl.c |
| 0x432ee6 | FUN_00432ee6 | 197 | engine_menu_input.c, rl_ctl.c |
| 0x432fab | FUN_00432fab | 191 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x433155 | FUN_00433155 | 569 | engine_menu_input.c, rl_ctl.c |
| 0x433461 | FUN_00433461 | 208 | engine_menu_input.c, rl_ctl.c |
| 0x4335f0 | FUN_004335f0 | 452 | engine_menu_input.c, main.c, rl_ctl.c |
| 0x4337b4 | FUN_004337b4 | 337 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x433905 | FUN_00433905 | 147 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x433998 | FUN_00433998 | 139 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+2) |
| 0x433a23 | FUN_00433a23 | 77 | engine_menu_input.c, main.c, rl_ctl.c |
| 0x433ba2 | FUN_00433ba2 | 287 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x433ce4 | FUN_00433ce4 | 217 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+2) |
| 0x43400b | FUN_0043400b | 361 | engine.c, engine_menu_input.c, engine_menu_input.h (+3) |
| 0x4342f3 | FUN_004342f3 | 21 | engine_menu_input.c, engine_menu_input.h, rl_ctl.c |
| 0x434308 | FUN_00434308 | 62 | engine_menu_input.c, rl_cov3.c |
| 0x434346 | FUN_00434346 | 178 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x434e1f | FUN_00434e1f | 37 | engine_confirm_dialog.c, engine_confirm_dialog.h, rl_ctl.c |
| 0x4351c4 | FUN_004351c4 | 77 | engine.c, engine_ingame_input.c, engine_rankings.h (+2) |
| 0x43524e | FUN_0043524e | 45 | engine.c, engine_ingame_input.c, rl_ctl.c |
| 0x4357a6 | FUN_004357a6 | 132 | engine.c, engine_ingame_input.c, engine_ingame_input.h (+4) |
| 0x435cef | FUN_00435cef | 456 | engine.c, engine_armory.c, engine_armory.h (+3) |
| 0x436408 | FUN_00436408 | 1015 | engine.c, engine_diplomacy.c, engine_diplomacy.h (+3) |
| 0x436d02 | FUN_00436d02 | 768 | engine_army_ai.c, engine_army_ai.h |
| 0x4371ce | FUN_004371ce | 108 | engine_army_select.h, rl_ctl.c |
| 0x43723a | FUN_0043723a | 245 | engine_army_select.c, engine_army_select.h, rl_ctl.c |
| 0x437535 | FUN_00437535 | 390 | engine_combat.c, engine_combat.h, engine_map_query.c |
| 0x438681 | FUN_00438681 | 699 | engine_army_ai.c, engine_army_ai.h |
| 0x438ceb | FUN_00438ceb | 326 | engine.c, engine_county_panels.c, engine_county_panels.h (+2) |
| 0x439df0 | FUN_00439df0 | 235 | engine_ingame_input.c, rl_ctl.c |
| 0x43a123 | FUN_0043a123 | 145 | engine.c, engine_ingame_input.h, engine_labor.c (+2) |
| 0x43a1e9 | FUN_0043a1e9 | 86 | engine_county_panels.h, rl_ctl.c |
| 0x43a23f | FUN_0043a23f | 115 | engine_county_panels.c, engine_county_panels.h |
| 0x43a2b2 | FUN_0043a2b2 | 85 | engine_county_panels.h, rl_ctl.c |
| 0x43a307 | FUN_0043a307 | 114 | engine_county_panels.c, engine_county_panels.h |
| 0x43a379 | FUN_0043a379 | 560 | engine.c, engine_county_panels.c, engine_county_panels.h (+2) |
| 0x43a5a9 | FUN_0043a5a9 | 647 | engine.c, engine_county_panels.c, engine_county_panels.h (+1) |
| 0x43a846 | FUN_0043a846 | 172 | engine_county_panels.c, engine_county_panels.h, rl_ctl.c |
| 0x43a8f2 | FUN_0043a8f2 | 48 | engine.c, engine_county_panels.c, engine_county_panels.h (+1) |
| 0x43a922 | FUN_0043a922 | 23 | engine_county_panels.c, engine_county_panels.h, rl_ctl.c |
| 0x43a939 | FUN_0043a939 | 23 | engine_county_panels.c, engine_county_panels.h, rl_ctl.c |
| 0x43aa32 | FUN_0043aa32 | 81 | engine.c, engine_county_panels.h, rl_ctl.c |
| 0x43aa83 | FUN_0043aa83 | 82 | engine_county_panels.c, engine_county_panels.h |
| 0x43aad5 | FUN_0043aad5 | 80 | engine_county_panels.h, rl_ctl.c |
| 0x43ab25 | FUN_0043ab25 | 81 | engine_county_panels.c, engine_county_panels.h |
| 0x43b412 | FUN_0043b412 | 185 | engine.c, engine_ingame_input.c, engine_ingame_input.h (+2) |
| 0x43b622 | FUN_0043b622 | 95 | engine_combat.c, engine_combat.h |
| 0x43b917 | FUN_0043b917 | 115 | engine_combat.c, engine_combat.h |
| 0x43be65 | FUN_0043be65 | 129 | engine_combat.c, engine_combat.h |
| 0x43caf4 | FUN_0043caf4 | 354 | engine_ingame_input.h, engine_terrain_info.c, engine_terrain_info.h (+1) |
| 0x43cc56 | FUN_0043cc56 | 16 | engine_army_select.h, engine_ingame_input.h, rl_ctl.c |
| 0x43ce1a | FUN_0043ce1a | 1263 | engine.c, engine_army_select.c, engine_army_select.h (+4) |
| 0x43d309 | FUN_0043d309 | 675 | engine_army_select.c, engine_army_select.h, rl_ctl.c |
| 0x43de70 | FUN_0043de70 | 1211 | engine.c, engine.h, engine_rate.c (+1) |
| 0x43e330 | FUN_0043e330 | 259 | engine.c, engine.h, engine_rate.c (+2) |
| 0x43e433 | FUN_0043e433 | 218 | engine.h, engine_rate.c, rl_mpmsg.c |
| 0x43e50d | FUN_0043e50d | 180 | engine.h, engine_rate.c, rl_mpmsg.c |
| 0x43e9e2 | FUN_0043e9e2 | 223 | engine_roster.c, engine_roster.h, main.c (+1) |
| 0x43ffc3 | FUN_0043ffc3 | 117 | engine_state_hash.c, rl_host.c |
| 0x44015a | FUN_0044015a | 96 | engine.h, engine_state_hash.c, rl_mpmsg.c |
| 0x440231 | FUN_00440231 | 1077 | engine.h, engine_state_hash.c, rl_mpmsg.c |
| 0x4472ce | FUN_004472ce | 1046 | engine.c, engine_delayed_action.c, engine_delayed_action.h (+1) |
| 0x4479de | FUN_004479de | 11 | engine_handlers_da.h, rl_mpmsg2.c |
| 0x447ddb | FUN_00447ddb | 11 | engine_handlers_da.h, rl_mpmsg3.c |
| 0x4483f0 | FUN_004483f0 | 11 | engine_handlers_da.h, rl_mpmsg3.c |
| 0x4483fb | FUN_004483fb | 11 | engine_handlers_da.h, rl_mpmsg3.c |
| 0x448406 | FUN_00448406 | 11 | engine_handlers_da.h, rl_mpmsg3.c |
| 0x448411 | FUN_00448411 | 17 | engine_handlers_da.h, rl_mpmsg3.c |
| 0x448440 | FUN_00448440 | 261 | engine.c, engine.h, engine_army_ai.c (+9) |
| 0x448545 | FUN_00448545 | 96 | engine_combat.c, engine_realm.c, engine_realm.h (+1) |
| 0x4485a5 | FUN_004485a5 | 163 | engine_army_ai.c, engine_army_economy.c, engine_army_economy.h (+5) |
| 0x448648 | FUN_00448648 | 97 | engine_army_select.c, engine_army_select.h, rl_ctl.c |
| 0x4486a9 | FUN_004486a9 | 256 | engine_ranking.c, engine_ranking.h |
| 0x4487a9 | FUN_004487a9 | 112 | engine_army_economy.c, engine_combat.c, engine_ranking.c (+1) |
| 0x448819 | FUN_00448819 | 1277 | engine_realm_events.c, engine_realm_events.h |
| 0x448d16 | FUN_00448d16 | 104 | engine_realm.c, engine_realm.h |
| 0x448e12 | FUN_00448e12 | 189 | engine_realm_events.c, engine_realm_events.h |
| 0x448ecf | FUN_00448ecf | 80 | engine_realm_events.c |
| 0x448f1f | FUN_00448f1f | 80 | engine_realm_events.c |
| 0x448f6f | FUN_00448f6f | 292 | engine_realm_events.c |
| 0x449093 | FUN_00449093 | 189 | engine_realm_events.c |
| 0x449150 | FUN_00449150 | 80 | engine_realm_events.c |
| 0x4491a0 | FUN_004491a0 | 80 | engine_realm_events.c |
| 0x4491f0 | FUN_004491f0 | 247 | engine_realm_events.c |
| 0x4492e7 | FUN_004492e7 | 99 | engine_realm_events.c |
| 0x44934a | FUN_0044934a | 66 | engine_realm_events.c |
| 0x44938c | FUN_0044938c | 60 | engine_realm_events.c |
| 0x4493c8 | FUN_004493c8 | 56 | engine_realm_events.c |
| 0x449400 | FUN_00449400 | 56 | engine_realm_events.c |
| 0x449438 | FUN_00449438 | 99 | engine_realm_events.c |
| 0x44949b | FUN_0044949b | 99 | engine_realm_events.c |
| 0x4494fe | FUN_004494fe | 53 | engine_realm_events.c |
| 0x449533 | FUN_00449533 | 66 | engine_realm_events.c |
| 0x449575 | FUN_00449575 | 154 | engine_realm_events.c |
| 0x44960f | FUN_0044960f | 121 | engine_realm_events.c |
| 0x449688 | FUN_00449688 | 133 | engine_realm_events.c |
| 0x44970d | FUN_0044970d | 80 | engine_realm_events.c |
| 0x44975d | FUN_0044975d | 80 | engine_realm_events.c |
| 0x4497ad | FUN_004497ad | 121 | engine_realm_events.c |
| 0x449826 | FUN_00449826 | 99 | engine_realm_events.c, engine_realm_events.h |
| 0x449889 | FUN_00449889 | 1253 | engine_realm.c, engine_realm.h |
| 0x449d6e | FUN_00449d6e | 354 | engine_realm.c, engine_realm.h, rl_ctl.c |
| 0x449ef3 | FUN_00449ef3 | 1991 | engine_realm.c, engine_realm.h, engine_territory.c |
| 0x44a6ba | FUN_0044a6ba | 903 | engine_realm.c, engine_realm.h |
| 0x44aa41 | FUN_0044aa41 | 1019 | engine_realm.c, engine_realm.h |
| 0x44ae3c | FUN_0044ae3c | 21 | engine_realm.c, engine_realm.h |
| 0x44ae51 | FUN_0044ae51 | 497 | engine_realm.c, engine_realm.h, engine_realm_globals.c (+1) |
| 0x44b042 | FUN_0044b042 | 704 | engine_realm.c, engine_realm.h, engine_realm_globals.c (+1) |
| 0x44b302 | FUN_0044b302 | 138 | engine_realm.c, engine_realm.h, rl_ctl.c |
| 0x44b391 | FUN_0044b391 | 319 | engine_realm.c, engine_realm.h, rl_ctl.c |
| 0x44b4d0 | FUN_0044b4d0 | 203 | engine_realm.c, engine_realm.h, rl_ctl.c |
| 0x44b59b | FUN_0044b59b | 624 | engine_realm.c, engine_realm.h |
| 0x44b80b | FUN_0044b80b | 399 | engine_army_ai.c, engine_combat.c, engine_realm.c (+1) |
| 0x44b99a | FUN_0044b99a | 155 | engine_realm.c, engine_realm.h |
| 0x44ba35 | FUN_0044ba35 | 181 | engine_realm.c, engine_realm.h, rl_ctl.c |
| 0x44baea | FUN_0044baea | 693 | engine_realm.c, engine_realm.h |
| 0x44bd9f | FUN_0044bd9f | 357 | engine_realm.c, engine_realm.h |
| 0x44bf04 | FUN_0044bf04 | 118 | engine_realm.c, engine_realm.h |
| 0x44bf7a | FUN_0044bf7a | 91 | engine_realm.c, engine_realm.h |
| 0x44bfd5 | FUN_0044bfd5 | 190 | engine_realm.c, engine_realm.h, rl_ctl.c |
| 0x44c093 | FUN_0044c093 | 485 | engine_realm.c, engine_realm.h, rl_ctl.c |
| 0x44c278 | FUN_0044c278 | 707 | engine_army_economy.c, engine_army_economy.h, engine_realm.c (+2) |
| 0x44c53b | FUN_0044c53b | 193 | engine_army_economy.c, engine_army_economy.h, engine_realm.c (+1) |
| 0x44c5fc | FUN_0044c5fc | 200 | engine_army_economy.c, engine_army_economy.h, engine_realm.c (+1) |
| 0x44c6c4 | FUN_0044c6c4 | 182 | engine_territory.c, engine_territory.h, rl_ctl.c |
| 0x44c77a | FUN_0044c77a | 139 | engine_realm_events.c, engine_realm_events.h |
| 0x44c80a | FUN_0044c80a | 159 | engine_realm_events.c, engine_realm_events.h |
| 0x44c8ae | FUN_0044c8ae | 1729 | engine_army_economy.c, engine_army_economy.h |
| 0x44cf6f | FUN_0044cf6f | 114 | engine_army_economy.c, engine_army_economy.h |
| 0x44cfe1 | FUN_0044cfe1 | 377 | engine_army_economy.c, engine_army_economy.h |
| 0x44d15a | FUN_0044d15a | 139 | engine_army_economy.c, engine_army_economy.h |
| 0x44d1e5 | FUN_0044d1e5 | 156 | engine_army_economy.c, engine_army_economy.h |
| 0x44d281 | FUN_0044d281 | 130 | engine_army_economy.c, rl_ctl.c |
| 0x44d303 | FUN_0044d303 | 113 | engine_army_economy.c, rl_ctl.c |
| 0x44d374 | FUN_0044d374 | 665 | engine_army_economy.c, engine_army_economy.h, engine_realm.c |
| 0x44d60d | FUN_0044d60d | 774 | engine_army_economy.c, engine_army_economy.h |
| 0x44d913 | FUN_0044d913 | 390 | engine_army_economy.c, engine_army_economy.h, engine_army_move.c (+4) |
| 0x44da99 | FUN_0044da99 | 692 | engine_army_economy.c, engine_army_economy.h |
| 0x44dd4d | FUN_0044dd4d | 530 | engine_army_economy.c, engine_army_economy.h, engine_realm.c |
| 0x44df5f | FUN_0044df5f | 1628 | engine_army_ai.c, engine_army_economy.c, engine_army_economy.h (+5) |
| 0x44e5bb | FUN_0044e5bb | 72 | engine_army_economy.c, engine_army_economy.h |
| 0x44e603 | FUN_0044e603 | 80 | engine_army_economy.c, engine_army_economy.h |
| 0x44e653 | FUN_0044e653 | 80 | engine_army_economy.c, engine_army_economy.h |
| 0x44e6a3 | FUN_0044e6a3 | 158 | engine_army_economy.c, engine_army_economy.h |
| 0x44e7b4 | FUN_0044e7b4 | 158 | engine_army_economy.c, engine_army_economy.h |
| 0x44e852 | FUN_0044e852 | 576 | engine_army_economy.c, engine_army_economy.h |
| 0x44ea92 | FUN_0044ea92 | 816 | engine_army_economy.c, engine_army_economy.h |
| 0x44edc2 | FUN_0044edc2 | 396 | engine_army_economy.c, engine_army_economy.h |
| 0x44ef4e | FUN_0044ef4e | 525 | engine_army_economy.c, engine_army_economy.h |
| 0x44f15b | FUN_0044f15b | 237 | engine_army_economy.c, engine_army_economy.h |
| 0x44f248 | FUN_0044f248 | 208 | engine_army_economy.c, engine_army_economy.h |
| 0x44f318 | FUN_0044f318 | 897 | engine_army_economy.c, engine_army_economy.h |
| 0x44f699 | FUN_0044f699 | 78 | engine_territory.c, engine_territory.h |
| 0x44f6e7 | FUN_0044f6e7 | 2147 | engine_army_ai.c, engine_army_economy.c, engine_army_economy.h (+6) |
| 0x4502ca | FUN_004502ca | 879 | engine_army_ai.c, engine_army_ai.h |
| 0x4508de | FUN_004508de | 1007 | engine_army_ai.h, engine_combat.c, engine_combat.h (+2) |
| 0x450ccd | FUN_00450ccd | 377 | engine_army_ai.c, engine_army_ai.h, engine_realm.c |
| 0x450e46 | FUN_00450e46 | 366 | engine_army_ai.c, engine_army_economy.c, engine_army_economy.h (+1) |
| 0x450fb4 | FUN_00450fb4 | 173 | engine_army_ai.c, engine_army_economy.c, engine_army_economy.h (+1) |
| 0x451061 | FUN_00451061 | 239 | engine_realm.c, engine_realm.h |
| 0x451150 | FUN_00451150 | 936 | game_init.c, game_init.h, rl_ctl.c |
| 0x4514f8 | FUN_004514f8 | 147 | game_init.c, game_init.h, rl_ctl.c |
| 0x45158b | FUN_0045158b | 147 | engine_army_ai.c, engine_army_ai.h |
| 0x45161e | FUN_0045161e | 428 | engine_hud_panel.c, engine_hud_panel.h, engine_workforce_hud.h (+1) |
| 0x4517ca | FUN_004517ca | 107 | engine_labor.c, engine_workforce_hud.h |
| 0x4518a5 | FUN_004518a5 | 293 | engine_workforce_hud.h |
| 0x4519ca | FUN_004519ca | 147 | engine_workforce_hud.h |
| 0x451a5d | FUN_00451a5d | 349 | engine_workforce_hud.h |
| 0x451bba | FUN_00451bba | 864 | engine_minimap.c, engine_minimap.h |
| 0x451f1a | FUN_00451f1a | 81 | engine.c, engine.h, engine_anim_cycle.c (+1) |
| 0x451fcc | FUN_00451fcc | 385 | engine.c, engine_camera.h, engine_map_init.c (+3) |
| 0x452160 | FUN_00452160 | 422 | engine.c, engine_armory.c, engine_armory.h (+6) |
| 0x452306 | FUN_00452306 | 224 | engine_dirty_rect.c, engine_dirty_rect.h, gfx.c (+2) |
| 0x4523e6 | FUN_004523e6 | 36 | engine_dirty_rect.h, rl_ctl.c |
| 0x45240a | FUN_0045240a | 33 | engine.c, engine_armory.c, engine_combat.c (+22) |
| 0x452436 | FUN_00452436 | 83 | engine_dirty_rect.h, engine_map_render.c, rl_ctl.c |
| 0x452489 | FUN_00452489 | 83 | engine_dirty_rect.h, rl_ctl.c |
| 0x4524dc | FUN_004524dc | 83 | engine_dirty_rect.h, rl_ctl.c |
| 0x452582 | FUN_00452582 | 83 | engine_dirty_rect.h |
| 0x4525d5 | FUN_004525d5 | 83 | engine_dirty_rect.h |
| 0x452628 | FUN_00452628 | 56 | engine_dirty_rect.c, engine_dirty_rect.h, engine_map_terrain.h (+1) |
| 0x4526b3 | FUN_004526b3 | 87 | engine_dirty_rect.c, engine_dirty_rect.h |
| 0x45270a | FUN_0045270a | 43 | engine_dirty_rect.h, engine_ingame_input.h, engine_workforce_hud.c (+2) |
| 0x452735 | FUN_00452735 | 53 | engine_dirty_rect.h |
| 0x452775 | FUN_00452775 | 49 | engine_button.c, engine_button.h, rl_ctl.c (+1) |
| 0x4527a6 | FUN_004527a6 | 101 | engine.c, engine_armory.c, engine_battlefield_render.c (+15) |
| 0x4650b0 | FUN_004650b0 | 265 | engine.c, engine_army_state.c, engine_army_state.h (+3) |
| 0x4651b9 | FUN_004651b9 | 91 | engine.c, engine_territory.c, engine_territory.h (+1) |
| 0x465214 | FUN_00465214 | 11 | engine_army_state.c |
| 0x46521f | FUN_0046521f | 615 | engine_army_state.c, engine_combat.c, engine_combat.h (+1) |
| 0x465486 | FUN_00465486 | 412 | engine_army_state.c, rl_terr.c |
| 0x465622 | FUN_00465622 | 319 | engine_army_state.c, rl_terr.c |
| 0x465761 | FUN_00465761 | 319 | engine_army_state.c, rl_terr.c |
| 0x4658c1 | FUN_004658c1 | 1112 | engine_army_move.c, engine_army_move.h |
| 0x465d28 | FUN_00465d28 | 1441 | engine_army_move.c, engine_army_move.h, engine_army_state.c (+3) |
| 0x46634d | FUN_0046634d | 1002 | engine_army_move.c, engine_army_move.h |
| 0x46673c | FUN_0046673c | 343 | engine_army_move.c, engine_army_move.h |
| 0x466893 | FUN_00466893 | 900 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x466c3c | FUN_00466c3c | 328 | engine_army_move.c, engine_army_move.h |
| 0x466d84 | FUN_00466d84 | 995 | engine_army_move.c, engine_army_move.h |
| 0x467196 | FUN_00467196 | 422 | engine_army_ai.c, engine_army_move.c, engine_army_move.h |
| 0x46733c | FUN_0046733c | 124 | engine_army_ai.c, engine_army_move.c, engine_combat.c (+3) |
| 0x4673b8 | FUN_004673b8 | 378 | engine_army_ai.c, rl_terr.c |
| 0x467532 | FUN_00467532 | 426 | engine_army_ai.c, rl_terr.c |
| 0x4676e0 | FUN_004676e0 | 144 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x467770 | FUN_00467770 | 525 | engine_territory.c, engine_territory.h, engine_territory_globals.c (+5) |
| 0x46797d | FUN_0046797d | 185 | engine_map_init.c, engine_map_init.h, rl_terr.c |
| 0x467a36 | FUN_00467a36 | 624 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x467ca6 | FUN_00467ca6 | 385 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x467e2c | FUN_00467e2c | 127 | engine_realm.c, engine_realm.h, rl_terr.c |
| 0x467eb0 | FUN_00467eb0 | 126 | engine_army_ai.c, engine_combat.c, engine_realm.c (+1) |
| 0x467f2e | FUN_00467f2e | 163 | engine_army_ai.c, rl_terr.c |
| 0x467fd1 | FUN_00467fd1 | 336 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x468121 | FUN_00468121 | 331 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46826c | FUN_0046826c | 221 | engine_army_ai.c, engine_territory.c, engine_territory.h |
| 0x46841c | FUN_0046841c | 170 | engine_army_ai.c, rl_terr.c |
| 0x4684c6 | FUN_004684c6 | 474 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x4686a0 | FUN_004686a0 | 159 | engine_army_move.c, engine_army_move.h, engine_combat.c (+1) |
| 0x46873f | FUN_0046873f | 931 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x468ae2 | FUN_00468ae2 | 351 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x468c41 | FUN_00468c41 | 270 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x468d4f | FUN_00468d4f | 274 | engine_territory.c, engine_territory.h, game_init.c (+1) |
| 0x468e61 | FUN_00468e61 | 457 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46902a | FUN_0046902a | 499 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46921d | FUN_0046921d | 138 | engine_army_move.c, engine_territory.c, engine_territory.h (+1) |
| 0x46942c | FUN_0046942c | 147 | engine_realm.c, engine_realm.h, rl_terr.c |
| 0x46958f | FUN_0046958f | 203 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46965a | FUN_0046965a | 371 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x4697cd | FUN_004697cd | 192 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46988d | FUN_0046988d | 274 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x469a9c | FUN_00469a9c | 176 | engine_realm.c, engine_realm.h, rl_terr.c |
| 0x469b51 | FUN_00469b51 | 60 | engine_territory.c, engine_territory.h |
| 0x469b8d | FUN_00469b8d | 404 | engine_army_economy.c, engine_territory.c, engine_territory.h (+1) |
| 0x469d21 | FUN_00469d21 | 237 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x469e0e | FUN_00469e0e | 72 | engine_army_move.c, engine_army_move.h, engine_army_move_confirm.c (+1) |
| 0x469e5b | FUN_00469e5b | 476 | engine_army_move.c, engine_army_move.h |
| 0x46a037 | FUN_0046a037 | 202 | engine_minimap.c, engine_minimap.h, game_init.c (+2) |
| 0x46ac22 | FUN_0046ac22 | 329 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46c080 | FUN_0046c080 | 199 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46c147 | FUN_0046c147 | 407 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46c2de | FUN_0046c2de | 292 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46c5b8 | FUN_0046c5b8 | 258 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46c6ba | FUN_0046c6ba | 322 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46cfbd | FUN_0046cfbd | 371 | engine_army_state.c, engine_map_query.c, engine_map_query.h (+3) |
| 0x46d130 | FUN_0046d130 | 371 | engine_map_query.c, engine_map_query.h, game_init.c (+2) |
| 0x46d2a3 | FUN_0046d2a3 | 301 | engine_map_query.c, engine_map_query.h, rl_terr.c |
| 0x46d42c | FUN_0046d42c | 456 | engine_combat.c, engine_combat.h |
| 0x46d5fe | FUN_0046d5fe | 502 | engine.c, engine_map_query.c, engine_map_query.h (+3) |
| 0x46d7f4 | FUN_0046d7f4 | 486 | engine_army_move.c, engine_realm_events.h, engine_territory.c (+2) |
| 0x46d9da | FUN_0046d9da | 113 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46da4b | FUN_0046da4b | 426 | engine_territory.c, engine_territory.h, main.c (+1) |
| 0x46dbf5 | FUN_0046dbf5 | 319 | engine_territory.c, engine_territory.h, engine_territory_globals.c (+2) |
| 0x46dd34 | FUN_0046dd34 | 209 | engine_territory.c, engine_territory.h, main.c (+1) |
| 0x46de05 | FUN_0046de05 | 143 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46de94 | FUN_0046de94 | 189 | engine_territory.c, engine_territory.h, rl_terr.c |
| 0x46df51 | FUN_0046df51 | 132 | engine_territory.c, engine_territory.h, engine_territory_globals.c (+1) |
| 0x46dfd5 | FUN_0046dfd5 | 146 | engine_combat.c, engine_territory.c, engine_territory.h (+2) |
| 0x46e067 | FUN_0046e067 | 317 | engine_territory.c, engine_territory.h |
| 0x46e1b0 | FUN_0046e1b0 | 792 | engine_army_ai.c, engine_combat.c, engine_territory.c (+3) |
| 0x46ea28 | FUN_0046ea28 | 57 | game_init.c, game_init.h, rl_terr.c |
| 0x46ea61 | FUN_0046ea61 | 57 | engine_combat.c, game_init.c, rl_terr.c |
| 0x46ead0 | FUN_0046ead0 | 57 | engine_army_state.c, engine_map_init.c, engine_realm_upkeep.c (+4) |
| 0x46eb09 | FUN_0046eb09 | 54 | game_init.c, game_init.h, rl_terr.c |
| 0x46ec6e | FUN_0046ec6e | 369 | engine_army_move.c, engine_combat.c, engine_realm_upkeep.c (+1) |
| 0x46eddf | FUN_0046eddf | 349 | engine_army_move.c, engine_combat.c, engine_realm_upkeep.c (+1) |
| 0x46f0a9 | FUN_0046f0a9 | 71 | engine_army_move.c, engine_army_state.c, engine_realm_upkeep.c (+1) |
| 0x46f1dc | FUN_0046f1dc | 67 | game_init.c, game_init.h, rl_terr.c |
| 0x46f2ae | FUN_0046f2ae | 67 | game_init.c, game_init.h, rl_terr.c |
| 0x46f3cd | FUN_0046f3cd | 78 | engine_map_init.c, game_init.c, game_init.h (+1) |
| 0x46f434 | FUN_0046f434 | 216 | engine_army_move.c, engine_path.c, engine_path.h |
| 0x46f68e | FUN_0046f68e | 114 | engine_army_select.c, engine_army_select.h, engine_territory.c (+1) |
| 0x46f700 | FUN_0046f700 | 2115 | engine_army_ai.c, engine_army_move.c, engine_path_field.c (+2) |
| 0x46ff43 | FUN_0046ff43 | 617 | engine.c, engine_strat_cost.c, engine_strat_cost.h (+2) |
| 0x4701ac | FUN_004701ac | 1228 | engine_army_ai.c, engine_path_field.c, engine_path_field.h (+1) |
| 0x470678 | FUN_00470678 | 326 | engine_army_move_confirm.c, rl_terr.c |
| 0x4707be | FUN_004707be | 274 | engine_army_ai.c, engine_path_field.c, engine_path_field.h (+1) |
| 0x4708d0 | FUN_004708d0 | 142 | engine_path_field.c, engine_path_field.h, rl_terr.c |
| 0x471c1f | FUN_00471c1f | 273 | engine_tile.c, engine_tile.h |
| 0x471d30 | FUN_00471d30 | 118 | engine_tile.c, engine_tile.h |
| 0x472392 | FUN_00472392 | 715 | engine_path.c, engine_path.h, engine_path_field.h (+1) |
| 0x47265d | FUN_0047265d | 536 | engine_path.h, rl_terr.c |
| 0x472875 | FUN_00472875 | 151 | engine_path.h, rl_terr.c |
| 0x472ad0 | FUN_00472ad0 | 16 | engine_event_queue.c, engine_event_queue.h, game_init.c (+1) |
| 0x472ae0 | FUN_00472ae0 | 96 | engine_event_queue.c, engine_event_queue.h, rl_link.c (+1) |
| 0x472b40 | FUN_00472b40 | 133 | engine.c, engine_event_queue.c, engine_event_queue.h (+1) |
| 0x472bc5 | FUN_00472bc5 | 560 | engine.c, engine_army_ai.c, engine_army_move.c (+11) |
| 0x472df5 | FUN_00472df5 | 81 | engine_event_queue.c, engine_event_queue.h |
| 0x476710 | FUN_00476710 | 88 | engine_army_select.c, engine_army_select.h, rl_turn.c |
| 0x476768 | FUN_00476768 | 245 | engine.c, engine_army_select.h, engine_ingame_input.h (+3) |
| 0x47685d | FUN_0047685d | 512 | engine.c, engine_ingame_input.h, rl_turn.c |
| 0x476e95 | FUN_00476e95 | 421 | engine.c, engine_tooltip.h |
| 0x4770b8 | FUN_004770b8 | 121 | engine_tooltip.c, engine_tooltip.h, engine_workforce_hud.c (+1) |
| 0x477131 | FUN_00477131 | 280 | engine_tooltip.c, engine_tooltip.h |
| 0x477249 | FUN_00477249 | 109 | engine_tooltip.c, engine_tooltip.h |
| 0x4772b6 | FUN_004772b6 | 106 | engine_tooltip.c, engine_tooltip.h |
| 0x477320 | FUN_00477320 | 1082 | engine_tooltip.c, engine_tooltip.h |
| 0x4777aa | FUN_004777aa | 246 | engine_tooltip.c, engine_tooltip.h |
| 0x477dfc | FUN_00477dfc | 1225 | engine_combat.c, engine_combat.h |
| 0x478419 | FUN_00478419 | 177 | engine_combat.c, engine_combat.h |
| 0x47ed34 | FUN_0047ed34 | 103 | engine.c, engine_camera.c, engine_camera.h (+1) |
| 0x48ee46 | FUN_0048ee46 | 87 | engine_tile.c, engine_tile.h |
| 0x4917dd | FUN_004917dd | 599 | engine_tile.c, engine_tile.h, rl_cov3.c (+1) |
| 0x4921e5 | FUN_004921e5 | 451 | engine_tile.c, engine_tile.h |
| 0x4976a1 | FUN_004976a1 | 472 | engine_load_pump.c, engine_load_pump.h, engine_map_init.c (+3) |
| 0x49792e | FUN_0049792e | 262 | engine.c, engine.h, game_init.c (+2) |
| 0x497ced | FUN_00497ced | 376 | engine_load_pump.c, engine_territory.c, engine_territory.h (+4) |
| 0x497e65 | FUN_00497e65 | 1025 | engine.c, engine.h, game_init.c (+2) |
| 0x498270 | FUN_00498270 | 327 | engine_map_init.c, engine_map_init.h, main.c (+1) |
| 0x4984dc | FUN_004984dc | 731 | engine.c, engine_ingame_input.c, engine_pl8.c (+6) |
| 0x499034 | FUN_00499034 | 197 | engine_menu_input.c, gfx.c, gfx.h (+1) |
| 0x4997bf | FUN_004997bf | 151 | engine_workforce_hud.c, engine_workforce_hud.h, rl_turn.c |
| 0x499a1c | FUN_00499a1c | 174 | engine.c, gfx.c, gfx.h (+2) |
| 0x499dc3 | FUN_00499dc3 | 154 | engine_menu_input.c, engine_menu_input.h, game_init.c (+3) |
| 0x499e5d | FUN_00499e5d | 421 | engine_menu_input.c, game_init.c, game_init.h (+2) |
| 0x49a010 | FUN_0049a010 | 982 | engine.c, engine_tick_cadence.h, rl_turn.c |
| 0x49a453 | FUN_0049a453 | 302 | game_init.c, game_init.h, rl_turn.c (+2) |
| 0x49a581 | FUN_0049a581 | 1039 | engine.c, engine_army_ai.c, engine_army_ai.h |
| 0x49a990 | FUN_0049a990 | 126 | engine_army_ai.c, engine_army_ai.h, engine_combat.c (+1) |
| 0x49aa0e | FUN_0049aa0e | 2197 | engine.c, engine_ranking.c, engine_ranking.h (+2) |
| 0x49b2a3 | FUN_0049b2a3 | 187 | engine_combat.c, engine_combat.h |
| 0x49b35e | FUN_0049b35e | 205 | game_init.c, game_init.h, rl_turn.c |
| 0x49b42b | FUN_0049b42b | 680 | engine_combat.c, engine_ranking.c, engine_ranking.h |
| 0x49b6d3 | FUN_0049b6d3 | 143 | engine_turn.c, engine_turn.h, rl_turn.c |
| 0x49b762 | FUN_0049b762 | 334 | engine.c, engine.h, engine_turn.c (+2) |
| 0x49bae9 | FUN_0049bae9 | 180 | engine_menu_input.c, engine_roster.c, engine_roster.h (+1) |
| 0x49bbe8 | FUN_0049bbe8 | 53 | engine_roster.c, engine_roster.h, engine_territory.c (+1) |
| 0x49bc1d | FUN_0049bc1d | 66 | engine_roster.c, engine_roster.h, engine_territory.c (+1) |
| 0x49bc5f | FUN_0049bc5f | 314 | engine_roster.c, engine_roster.h, rl_turn.c |
| 0x49bd99 | FUN_0049bd99 | 3063 | game_init.c, game_init.h, rl_turn.c |
| 0x49ca32 | FUN_0049ca32 | 46 | engine_roster.c, engine_roster.h, main.c (+1) |
| 0x49caaa | FUN_0049caaa | 935 | engine_menu_input.c, engine_npc_schedule.c, engine_npc_schedule.h (+5) |
| 0x49ce51 | FUN_0049ce51 | 109 | engine.c, engine_turn.c, engine_turn.h (+1) |
| 0x49cf20 | FUN_0049cf20 | 704 | engine_combat.c, engine_combat.h |
| 0x49d1e0 | FUN_0049d1e0 | 1024 | engine.c, engine_combat.c, engine_ranking.c (+1) |
| 0x49d5e0 | FUN_0049d5e0 | 88 | engine_army_ai.c, engine_army_ai.h |
| 0x49d638 | FUN_0049d638 | 1737 | engine.c, engine_army_ai.c, engine_army_ai.h |
| 0x49dd01 | FUN_0049dd01 | 583 | engine_army_ai.c, engine_army_ai.h, engine_territory.c (+1) |
| 0x49df48 | FUN_0049df48 | 126 | engine_combat.c |
| 0x49dfc6 | FUN_0049dfc6 | 505 | engine.c, engine_army_ai.c, engine_army_ai.h |
| 0x49e1bf | FUN_0049e1bf | 476 | engine_army_ai.c, engine_army_ai.h |
| 0x49e39b | FUN_0049e39b | 994 | engine_army_ai.c, engine_army_ai.h |
| 0x49e77d | FUN_0049e77d | 1430 | engine_army_ai.c, engine_army_ai.h |
| 0x49ed13 | FUN_0049ed13 | 180 | engine_army_ai.c, engine_army_ai.h |
| 0x49edc7 | FUN_0049edc7 | 872 | engine_army_ai.c, engine_army_ai.h |
| 0x49f12f | FUN_0049f12f | 765 | engine_army_ai.c, engine_army_ai.h, engine_combat.h (+1) |
| 0x49f431 | FUN_0049f431 | 782 | engine_army_ai.c, engine_army_ai.h, rl_turn.c |
| 0x49f73f | FUN_0049f73f | 273 | engine_army_ai.c, rl_turn.c |
| 0x49f850 | FUN_0049f850 | 237 | engine_army_ai.c, rl_turn.c |
| 0x49f93d | FUN_0049f93d | 47 | engine_army_ai.c, engine_army_ai.h |
| 0x49f96c | FUN_0049f96c | 11 | engine_army_ai.c, engine_army_ai.h |
| 0x49f977 | FUN_0049f977 | 841 | engine_army_ai.c, engine_army_ai.h |
| 0x49fcc5 | FUN_0049fcc5 | 224 | engine_army_ai.c, rl_turn.c |
| 0x49fda5 | FUN_0049fda5 | 460 | engine_army_ai.c, rl_turn.c |
| 0x49ff71 | FUN_0049ff71 | 164 | engine_army_ai.c, rl_cov3.c |
| 0x4a0015 | FUN_004a0015 | 469 | engine_army_ai.c, engine_army_ai.h, engine_combat.c |
| 0x4a01ea | FUN_004a01ea | 287 | engine_army_ai.c, rl_turn.c |
| 0x4a0309 | FUN_004a0309 | 233 | engine_army_ai.c, engine_army_ai.h |
| 0x4a03f2 | FUN_004a03f2 | 599 | engine_army_ai.c, engine_army_ai.h, engine_path.h |
| 0x4a0649 | FUN_004a0649 | 425 | engine_army_ai.c, engine_army_ai.h, engine_path.h (+1) |
| 0x4a07f2 | FUN_004a07f2 | 293 | engine_combat.c, engine_combat.h |
| 0x4a0917 | FUN_004a0917 | 403 | engine_combat.c, engine_combat.h |
| 0x4a0aaa | FUN_004a0aaa | 371 | engine_army_ai.c, engine_army_ai.h |
| 0x4a0c1d | FUN_004a0c1d | 1572 | engine_army_ai.c, engine_army_ai.h |
| 0x4a1241 | FUN_004a1241 | 357 | engine_diplomacy.c, engine_diplomacy.h, rl_turn.c |
| 0x4a13a6 | FUN_004a13a6 | 849 | engine_army_ai.c, engine_army_ai.h |
| 0x4a16f7 | FUN_004a16f7 | 125 | engine_army_move.h, engine_diplomacy.c, engine_diplomacy.h |
| 0x4a1774 | FUN_004a1774 | 211 | engine_army_ai.c, engine_diplomacy.c, engine_diplomacy.h (+1) |
| 0x4a1847 | FUN_004a1847 | 525 | engine.c, engine_diplomacy.c, engine_diplomacy.h (+1) |
| 0x4a1a54 | FUN_004a1a54 | 213 | engine_army_ai.c, engine_diplomacy.c, engine_diplomacy.h (+1) |
| 0x4a1b29 | FUN_004a1b29 | 298 | engine_army_ai.c, rl_cov3.c |
| 0x4a1c53 | FUN_004a1c53 | 537 | game_init.c, game_init.h, rl_turn.c |
| 0x4a1e6c | FUN_004a1e6c | 117 | engine_noble_faces.c, engine_noble_faces.h, engine_roster.c (+3) |
| 0x4a1ee1 | FUN_004a1ee1 | 1520 | engine_combat.c, engine_diplomacy.c, engine_diplomacy.h |
| 0x4a24d1 | FUN_004a24d1 | 179 | engine_diplomacy.c, engine_diplomacy.h, rl_turn.c |
| 0x4a2584 | FUN_004a2584 | 157 | engine_army_ai.c, game_init.c, game_init.h (+1) |
| 0x4a2621 | FUN_004a2621 | 343 | engine.c, engine_diplomacy.c, engine_diplomacy.h (+2) |
| 0x4a277d | FUN_004a277d | 565 | engine_army_ai.c, rl_turn.c |
| 0x4a29b2 | FUN_004a29b2 | 804 | engine_army_ai.c, rl_turn.c |
| 0x4a2cd6 | FUN_004a2cd6 | 651 | engine_army_ai.c, rl_turn.c |
| 0x4a2f61 | FUN_004a2f61 | 405 | engine_army_ai.c, rl_turn.c |
| 0x4a30f6 | FUN_004a30f6 | 895 | engine_army_ai.c, rl_turn.c |
| 0x4a3475 | FUN_004a3475 | 220 | engine_army_ai.c, rl_turn.c |
| 0x4a3551 | FUN_004a3551 | 906 | engine_army_ai.c, rl_cov3.c |
| 0x4a38db | FUN_004a38db | 908 | engine_army_ai.c, rl_turn.c |
| 0x4a3c67 | FUN_004a3c67 | 620 | engine_army_ai.c, engine_army_ai.h, rl_turn.c |
| 0x4a3ed3 | FUN_004a3ed3 | 383 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4052 | FUN_004a4052 | 657 | engine_army_ai.c, engine_army_ai.h, rl_turn.c |
| 0x4a42e3 | FUN_004a42e3 | 300 | engine_army_ai.c |
| 0x4a440f | FUN_004a440f | 645 | engine_army_ai.c, rl_turn.c |
| 0x4a4694 | FUN_004a4694 | 238 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4782 | FUN_004a4782 | 701 | engine_army_ai.c, engine_army_ai.h, engine_army_economy.c (+1) |
| 0x4a4a3f | FUN_004a4a3f | 211 | engine_army_ai.c, engine_army_ai.h, rl_turn.c |
| 0x4a4b12 | FUN_004a4b12 | 298 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4c41 | FUN_004a4c41 | 311 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4e3d | FUN_004a4e3d | 286 | engine_army_ai.c, engine_army_state.c, engine_army_state.h |
| 0x4a4f5b | FUN_004a4f5b | 168 | engine.c, engine_army_state.c, engine_army_state.h |
| 0x4a5003 | FUN_004a5003 | 171 | engine_combat.c, engine_combat.h |
| 0x4a50ae | FUN_004a50ae | 731 | engine_combat.c, engine_combat.h |
| 0x4a5389 | FUN_004a5389 | 214 | engine_combat.c, engine_combat.h |
| 0x4a545f | FUN_004a545f | 188 | engine_combat.c, rl_turn.c |
| 0x4a551b | FUN_004a551b | 332 | engine_combat.c, engine_combat.h |
| 0x4a5667 | FUN_004a5667 | 325 | engine_army_ai.c, engine_army_ai.h |
| 0x4a57ac | FUN_004a57ac | 328 | engine_army_ai.c, rl_host.c |
| 0x4a58f4 | FUN_004a58f4 | 169 | engine_army_ai.c, engine_combat.c, rl_host.c |
| 0x4a599d | FUN_004a599d | 386 | engine_army_ai.c |
| 0x4a5b1f | FUN_004a5b1f | 1003 | engine_army_ai.c, rl_host.c |
| 0x4a5f0a | FUN_004a5f0a | 431 | engine_army_ai.c, rl_host.c |
| 0x4a60b9 | FUN_004a60b9 | 439 | engine_army_ai.c, rl_host.c |
| 0x4a6270 | FUN_004a6270 | 602 | engine_army_ai.c, engine_combat.c, engine_combat.h (+1) |
| 0x4a64ca | FUN_004a64ca | 217 | engine_army_ai.c, rl_host.c |
| 0x4a65a3 | FUN_004a65a3 | 402 | engine_army_ai.c, engine_map_query.h, rl_host.c |
| 0x4a6735 | FUN_004a6735 | 360 | engine_army_ai.c, engine_map_query.h, rl_host.c |
| 0x4a689d | FUN_004a689d | 399 | engine_combat.c, engine_combat.h |
| 0x4a6a30 | FUN_004a6a30 | 568 | engine_combat.c, engine_combat.h |
| 0x4a6c68 | FUN_004a6c68 | 1264 | engine_army_move.c, engine_army_move.h, engine_combat.c (+2) |
| 0x4a7158 | FUN_004a7158 | 402 | engine_combat.c, engine_combat.h |
| 0x4a72fe | FUN_004a72fe | 1516 | engine_combat.c, engine_combat.h |
| 0x4a78ea | FUN_004a78ea | 94 | engine_combat.c, engine_combat.h |
| 0x4a79a3 | FUN_004a79a3 | 767 | engine_combat.c, engine_combat.h |
| 0x4a7ca2 | FUN_004a7ca2 | 269 | engine_combat.c, engine_combat.h |
| 0x4a7e0a | FUN_004a7e0a | 171 | engine_combat.c, engine_combat.h, rl_host.c |
| 0x4a7eb5 | FUN_004a7eb5 | 550 | engine_combat.c, engine_combat.h, rl_host.c |
| 0x4a80db | FUN_004a80db | 478 | engine_combat.c, engine_combat.h |
| 0x4a8426 | FUN_004a8426 | 148 | engine_army_select.h, rl_host.c |
| 0x4a84ba | FUN_004a84ba | 77 | engine_army_state.c, engine_army_state.h |
| 0x4a8507 | FUN_004a8507 | 1444 | engine_army_state.c, engine_army_state.h |
| 0x4a8aab | FUN_004a8aab | 753 | engine_army_state.h, engine_combat.c, engine_combat.h |
| 0x4a8d9c | FUN_004a8d9c | 111 | engine_army_select.c, engine_army_select.h, rl_host.c |
| 0x4a8e0b | FUN_004a8e0b | 943 | engine.c, engine_army_move_confirm.c, engine_army_move_confirm.h (+1) |
| 0x4a91ba | FUN_004a91ba | 152 | engine_army_move_confirm.c, engine_army_move_confirm.h, rl_host.c |
| 0x4a9252 | FUN_004a9252 | 288 | engine.c, engine_army_move_confirm.c, engine_army_move_confirm.h (+4) |
| 0x4a9372 | FUN_004a9372 | 669 | engine.c, engine_army_move_confirm.c, engine_army_move_confirm.h (+1) |
| 0x4a960f | FUN_004a960f | 334 | engine_army_move_confirm.c, engine_army_move_confirm.h, rl_host.c |
| 0x4a975d | FUN_004a975d | 222 | engine_army_move_confirm.c, engine_army_move_confirm.h, rl_host.c |
| 0x4a983b | FUN_004a983b | 195 | engine_army_move_confirm.c, engine_army_move_confirm.h, rl_host.c |
| 0x4a98fe | FUN_004a98fe | 412 | engine_army_move_confirm.c, engine_army_move_confirm.h, rl_host.c |
| 0x4a9a9a | FUN_004a9a9a | 1047 | engine.c, engine_armory.h, engine_combat.c (+2) |
| 0x4a9eb1 | FUN_004a9eb1 | 103 | engine_combat.c, rl_host.c |
| 0x4a9f18 | FUN_004a9f18 | 90 | engine_combat.c, rl_host.c |
| 0x4a9f72 | FUN_004a9f72 | 199 | engine_combat.c, rl_host.c |
| 0x4aa039 | FUN_004aa039 | 328 | engine_army_ai.c, engine_combat.c, engine_realm_upkeep.c (+1) |
| 0x4aa181 | FUN_004aa181 | 1353 | engine_combat.c, engine_combat.h |
| 0x4aa6ca | FUN_004aa6ca | 469 | engine_combat.c, engine_combat.h |
| 0x4aa89f | FUN_004aa89f | 107 | engine_combat.c, engine_combat.h |
| 0x4aa90a | FUN_004aa90a | 374 | engine.c, engine_armory.c, engine_armory.h (+5) |
| 0x4aaa80 | FUN_004aaa80 | 344 | engine_combat.c, engine_combat.h |
| 0x4aad07 | FUN_004aad07 | 1443 | engine_combat.c, engine_combat.h |
| 0x4ab2aa | FUN_004ab2aa | 217 | engine_combat.c, engine_combat.h |
| 0x4ab383 | FUN_004ab383 | 1751 | engine_combat.c, engine_combat.h, engine_diplomacy.h (+1) |
| 0x4aba5a | FUN_004aba5a | 220 | engine_combat.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4abb36 | FUN_004abb36 | 473 | engine_army_state.c, engine_combat.c, engine_combat.h |
| 0x4abd0f | FUN_004abd0f | 616 | engine_army_state.c, engine_combat.c, engine_combat.h |
| 0x4abf77 | FUN_004abf77 | 526 | engine_combat.c, engine_combat.h |
| 0x4ac185 | FUN_004ac185 | 577 | engine_combat.c, engine_combat.h, engine_realm.c (+1) |
| 0x4ac3c6 | FUN_004ac3c6 | 211 | engine_combat.c, engine_combat.h, engine_realm.h |
| 0x4ac499 | FUN_004ac499 | 289 | engine.c, engine_army_state.c, engine_army_state.h |
| 0x4ac5ba | FUN_004ac5ba | 324 | engine_army_state.c, engine_army_state.h |
| 0x4ac6fe | FUN_004ac6fe | 245 | engine_army_ai.c, engine_combat.c, engine_combat.h (+2) |
| 0x4ac7f3 | FUN_004ac7f3 | 273 | engine_combat.c, rl_host.c |
| 0x4ac904 | FUN_004ac904 | 295 | game_init.c, game_init.h, rl_host.c |
| 0x4aca2b | FUN_004aca2b | 297 | engine.c, engine_combat.c, engine_combat.h |
| 0x4acb54 | FUN_004acb54 | 128 | engine_combat.c, rl_host.c |
| 0x4acbd4 | FUN_004acbd4 | 650 | engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ace5e | FUN_004ace5e | 650 | engine.c, engine_realm_upkeep.c, engine_realm_upkeep.h (+1) |
| 0x4ad0e8 | FUN_004ad0e8 | 132 | engine_realm_upkeep.c, rl_host.c |
| 0x4ad16c | FUN_004ad16c | 196 | engine_realm_upkeep.c, rl_host.c |
| 0x4ad230 | FUN_004ad230 | 230 | engine_realm_upkeep.c, rl_host.c |
| 0x4ad316 | FUN_004ad316 | 132 | engine_realm_upkeep.c, rl_host.c |
| 0x4ad39a | FUN_004ad39a | 140 | engine_realm_upkeep.c, engine_realm_upkeep.h, rl_host.c |
| 0x4ad426 | FUN_004ad426 | 111 | engine_combat.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad495 | FUN_004ad495 | 150 | engine_army_ai.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad52b | FUN_004ad52b | 405 | engine_combat.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad6c0 | FUN_004ad6c0 | 524 | engine_army_state.c, engine_combat.c, engine_realm.c (+2) |
| 0x4ad8cc | FUN_004ad8cc | 277 | engine_combat.c, engine_combat.h |
| 0x4ad9f0 | FUN_004ad9f0 | 774 | engine.c, engine_load_pump.c, engine_load_pump.h (+2) |
| 0x4ade93 | FUN_004ade93 | 339 | rl_host.c, save.c, save.h |
| 0x4adfe6 | FUN_004adfe6 | 377 | engine_load_pump.c, engine_load_pump.h, main.c (+3) |
| 0x4ae539 | FUN_004ae539 | 169 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4ae5e2 | FUN_004ae5e2 | 96 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4ae74d | FUN_004ae74d | 144 | game_init.c, game_init.h, rl_host.c |
| 0x4ae7dd | FUN_004ae7dd | 204 | engine_realm.c, engine_realm.h, game_init.c (+2) |
| 0x4ae8a9 | FUN_004ae8a9 | 191 | game_init.c, game_init.h, rl_host.c (+2) |
| 0x4ae968 | FUN_004ae968 | 264 | rl_cov3.c, save.c, save.h |
| 0x4aea70 | FUN_004aea70 | 506 | rl_cov3.c, save.c, save.h |
| 0x4aef7e | FUN_004aef7e | 408 | engine_menu_input.h, engine_turn.c, engine_turn.h (+3) |
| 0x4af502 | FUN_004af502 | 72 | engine_armory.c, engine_county_panels.c, engine_labor.c (+7) |
| 0x4af54a | FUN_004af54a | 112 | io.h, rl_host.c, strutil.h |
| 0x4af5ba | FUN_004af5ba | 114 | strutil.h |
| 0x4af62c | FUN_004af62c | 73 | rl_host.c, strutil.h |
| 0x4af675 | FUN_004af675 | 84 | engine_load_pump.c, rl_host.c, strutil.h |
| 0x4af6c9 | FUN_004af6c9 | 67 | strutil.h |
| 0x4af8d9 | FUN_004af8d9 | 381 | config.c, engine_armory.c, engine_county_panels.c (+13) |
| 0x4afab9 | FUN_004afab9 | 897 | engine_battlefield_render.h, engine_hud_menubar.c, gfx_panel.c (+3) |
| 0x4b13ed | FUN_004b13ed | 274 | gfx_globals.c, gfx_globals.h, gfx_paint.c (+1) |
| 0x4b191e | FUN_004b191e | 981 | engine.c, engine.h, engine_input.c (+3) |
| 0x4b1de0 | FUN_004b1de0 | 11 | engine.c, engine_armory.c, engine_confirm_dialog.c (+7) |
| 0x4b1deb | FUN_004b1deb | 92 | engine.c, engine_county_panels.c, rl_host.c |
| 0x4b20ed | FUN_004b20ed | 68 | engine.c, engine.h, engine_delta.c (+1) |
| 0x4b2258 | FUN_004b2258 | 797 | audio.h, engine.c, engine.h (+3) |
| 0x4b2730 | FUN_004b2730 | 143 | gfx.c, gfx.h, main.c (+1) |
| 0x4b27bf | FUN_004b27bf | 143 | engine.c, engine.h, engine_camera.h (+3) |
| 0x4b2922 | FUN_004b2922 | 156 | gfx.c, gfx.h, main.c (+1) |
| 0x4b29be | FUN_004b29be | 2545 | main.c, rl_host.c |
| 0x4b3768 | FUN_004b3768 | 84 | engine.c, engine_county_panels.h, rl_host.c |
| 0x4b3e51 | FUN_004b3e51 | 111 | engine_army_select.c, rl_host.c |
| 0x4b3ec0 | FUN_004b3ec0 | 74 | engine_armory.c, engine_tooltip.c, engine_tooltip.h (+2) |
| 0x4b3f0a | FUN_004b3f0a | 74 | engine_armory.c, engine_armory.h, engine_tooltip.c (+3) |
| 0x4b3f54 | FUN_004b3f54 | 38 | gfx_paint.h |
| 0x4b3f7a | FUN_004b3f7a | 41 | gfx_paint.h |
| 0x4b3fa3 | FUN_004b3fa3 | 61 | gfx_paint.h |
| 0x4b4056 | FUN_004b4056 | 47 | engine_overlay_render.c, engine_overlay_render.h |
| 0x4b414a | FUN_004b414a | 109 | engine_labor.c, engine_tooltip.c, gfx_sprite.c (+1) |
| 0x4b41b7 | FUN_004b41b7 | 165 | gfx_glyph.h |
| 0x4b425c | FUN_004b425c | 105 | gfx_glyph.c, gfx_glyph.h |
| 0x4b42c5 | FUN_004b42c5 | 105 | gfx_glyph.c, gfx_glyph.h |
| 0x4b432e | FUN_004b432e | 131 | gfx_sprite.c, gfx_sprite.h, gfx_sprite_render.c |
| 0x4b43b1 | FUN_004b43b1 | 188 | gfx_sprite.c, gfx_sprite.h |
| 0x4b446d | FUN_004b446d | 101 | gfx_sprite.c, gfx_sprite.h |
| 0x4b44d2 | FUN_004b44d2 | 101 | gfx_sprite.c, gfx_sprite.h |
| 0x4b4537 | FUN_004b4537 | 733 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b4814 | FUN_004b4814 | 164 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b48b8 | FUN_004b48b8 | 162 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b495a | FUN_004b495a | 93 | gfx_sprite_rle.c, gfx_sprite_rle.h, rl_host.c |
| 0x4b49b7 | FUN_004b49b7 | 64 | gfx_sprite.c, gfx_sprite.h |
| 0x4b49f7 | FUN_004b49f7 | 705 | gfx_sprite.c, gfx_sprite.h |
| 0x4b4cb8 | FUN_004b4cb8 | 1659 | gfx_sprite.c, gfx_sprite.h, gfx_sprite_render.c (+1) |
| 0x4b5333 | FUN_004b5333 | 2997 | gfx_sprite.c, gfx_sprite.h |
| 0x4b99c0 | FUN_004b99c0 | 2222 | call_trace.h, engine.c, engine.h (+12) |
| 0x4ba26e | FUN_004ba26e | 1882 | engine.c, engine_armory.c, engine_armory.h (+20) |
| 0x4ba9c8 | FUN_004ba9c8 | 3832 | engine.c, engine_confirm_dialog.h, engine_county_panels.h (+12) |
| 0x4bb8c0 | FUN_004bb8c0 | 84 | engine.c, engine_rate.c, rl_host.c |
| 0x4bb978 | FUN_004bb978 | 614 | engine.c, engine.h, engine_rate.c (+1) |
| 0x4bbbe3 | FUN_004bbbe3 | 157 | engine_camera.h, rl_host.c |
| 0x4bbc80 | FUN_004bbc80 | 465 | engine.c, engine.h, engine_phase.c (+1) |
| 0x4bc020 | FUN_004bc020 | 231 | engine_battlefield_render.c, engine_battlefield_render.h, rl_host.c |
| 0x4bc107 | FUN_004bc107 | 59 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc142 | FUN_004bc142 | 143 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc1d1 | FUN_004bc1d1 | 81 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc222 | FUN_004bc222 | 131 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc2a5 | FUN_004bc2a5 | 32 | engine_battlefield_render.h |
| 0x4bc2c5 | FUN_004bc2c5 | 32 | engine_battlefield_render.h |
| 0x4bc2e5 | FUN_004bc2e5 | 97 | engine_battlefield_render.h |
| 0x4bc346 | FUN_004bc346 | 102 | engine_battlefield_render.h |
| 0x4bc3ac | FUN_004bc3ac | 97 | engine_battlefield_render.h |
| 0x4bc40d | FUN_004bc40d | 100 | engine_battlefield_render.h |
| 0x4bc471 | FUN_004bc471 | 74 | engine_battlefield_render.h |
| 0x4bc4bb | FUN_004bc4bb | 74 | engine_battlefield_render.h |
| 0x4bc505 | FUN_004bc505 | 21 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc51a | FUN_004bc51a | 994 | engine_battlefield_render.c, engine_battlefield_render.h, gfx_sprite.h |
| 0x4bc8fc | FUN_004bc8fc | 725 | engine_battlefield_render.c, engine_battlefield_render.h, gfx_sprite.h |
| 0x4bcbd1 | FUN_004bcbd1 | 11 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bcbdc | FUN_004bcbdc | 1187 | engine_battlefield_render.c, engine_battlefield_render.h, gfx_sprite.h |
| 0x4bd07f | FUN_004bd07f | 436 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bd233 | FUN_004bd233 | 290 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bd355 | FUN_004bd355 | 543 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bd574 | FUN_004bd574 | 485 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bd759 | FUN_004bd759 | 479 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bd938 | FUN_004bd938 | 346 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bda92 | FUN_004bda92 | 259 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bdb95 | FUN_004bdb95 | 156 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bdc31 | FUN_004bdc31 | 1087 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4be070 | FUN_004be070 | 719 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4be33f | FUN_004be33f | 416 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4be4df | FUN_004be4df | 735 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4be7be | FUN_004be7be | 763 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4beab9 | FUN_004beab9 | 1051 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4beed4 | FUN_004beed4 | 805 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bf1f9 | FUN_004bf1f9 | 433 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4cfb08 | FUN_004cfb08 | 64 | engine.c, engine_map_scene.c, engine_map_scene.h |

## ported (1172) — reimplemented, no host test yet

| VA | name | size | src |
|----|------|-----:|-----|
| 0x401c5b | FUN_00401c5b | 21 | rl_ui2.c |
| 0x405000 | FUN_00405000 | 180 | engine_bf_action.c |
| 0x4063c1 | FUN_004063c1 | 690 | engine_map_terrain.c, engine_map_terrain.h |
| 0x406673 | FUN_00406673 | 1351 | engine_map_terrain.c, engine_map_terrain.h |
| 0x408ea4 | FUN_00408ea4 | 118 | rl_ui2.c |
| 0x408f1a | FUN_00408f1a | 149 | rl_ui2.c |
| 0x408fcb | FUN_00408fcb | 96 | engine_armory.c, engine_merchant.c, engine_rankings.c (+4) |
| 0x40902b | FUN_0040902b | 148 | rl_ui4.c |
| 0x409429 | FUN_00409429 | 68 | rl_glue.c |
| 0x40ae12 | FUN_0040ae12 | 175 | rl_glue.c |
| 0x40c725 | FUN_0040c725 | 428 | rl_render.c |
| 0x40c8d1 | FUN_0040c8d1 | 87 | rl_glue.c |
| 0x40c928 | FUN_0040c928 | 68 | rl_glue.c |
| 0x40cd58 | FUN_0040cd58 | 433 | rl_render.c |
| 0x40dd92 | FUN_0040dd92 | 312 | rl_glue.c |
| 0x40deca | FUN_0040deca | 152 | rl_glue.c |
| 0x40df62 | FUN_0040df62 | 107 | rl_ui3.c |
| 0x40dfcd | FUN_0040dfcd | 61 | rl_glue.c |
| 0x40e00a | FUN_0040e00a | 143 | rl_glue.c |
| 0x40e099 | FUN_0040e099 | 149 | rl_glue.c |
| 0x40e761 | FUN_0040e761 | 131 | rl_glue.c |
| 0x40ec2c | FUN_0040ec2c | 152 | rl_mpnet2.c |
| 0x40f10f | FUN_0040f10f | 145 | rl_glue.c |
| 0x410a5d | FUN_00410a5d | 76 | engine_sendsupplies.c, rl_ui4.c |
| 0x410c71 | FUN_00410c71 | 76 | rl_render.c |
| 0x410f4b | FUN_00410f4b | 358 | rl_render.c |
| 0x4120e0 | FUN_004120e0 | 99 | rl_render.c |
| 0x412421 | FUN_00412421 | 581 | rl_render.c |
| 0x412795 | FUN_00412795 | 501 | rl_render.c |
| 0x41298a | FUN_0041298a | 425 | rl_render.c |
| 0x412b33 | FUN_00412b33 | 824 | engine.c, engine_labor.c, engine_labor.h (+2) |
| 0x412e6b | FUN_00412e6b | 746 | engine_labor.c, engine_labor.h, rl_ui4.c |
| 0x413526 | FUN_00413526 | 106 | rl_ui2.c |
| 0x413b30 | FUN_00413b30 | 1475 | engine_labor.c, engine_labor.h, rl_ui4.c |
| 0x4140f3 | FUN_004140f3 | 301 | engine_labor.c, engine_labor.h, rl_ui4.c |
| 0x414220 | FUN_00414220 | 31 | engine_labor.c, engine_labor.h, rl_ui4.c |
| 0x41423f | FUN_0041423f | 11 | rl_glue.c |
| 0x41424a | FUN_0041424a | 225 | rl_ui2.c |
| 0x41438c | FUN_0041438c | 446 | rl_render.c |
| 0x414819 | FUN_00414819 | 203 | rl_render.c |
| 0x414e06 | FUN_00414e06 | 354 | rl_ui2.c |
| 0x414f68 | FUN_00414f68 | 500 | rl_render.c |
| 0x41515c | FUN_0041515c | 398 | rl_render.c |
| 0x4152ea | FUN_004152ea | 341 | rl_render.c |
| 0x41543f | FUN_0041543f | 171 | rl_ui2.c |
| 0x4154ea | FUN_004154ea | 340 | rl_ui2.c |
| 0x41593b | FUN_0041593b | 673 | engine.c, engine_rankings.c, engine_rankings.h (+1) |
| 0x415bdc | FUN_00415bdc | 614 | engine_rankings.c, engine_rankings.h, rl_ui4.c |
| 0x415e42 | FUN_00415e42 | 348 | engine_rankings.c, engine_rankings.h, rl_ui4.c |
| 0x415fb7 | FUN_00415fb7 | 212 | engine.c, engine_merchant.c, engine_merchant.h (+1) |
| 0x41608b | FUN_0041608b | 637 | rl_render.c |
| 0x416308 | FUN_00416308 | 87 | engine.c, engine_trade.c, engine_trade.h (+1) |
| 0x41635f | FUN_0041635f | 1478 | engine_trade.c, engine_trade.h |
| 0x416925 | FUN_00416925 | 974 | engine.c, engine_treasury.c, engine_treasury.h |
| 0x416cf3 | FUN_00416cf3 | 1275 | engine.c, engine_noble_faces.c, engine_noble_faces.h |
| 0x4171ee | FUN_004171ee | 1028 | engine_noble_faces.c, engine_noble_faces.h |
| 0x41789b | FUN_0041789b | 197 | engine.c, engine_diplomacy_action.c, engine_diplomacy_action.h (+1) |
| 0x417960 | FUN_00417960 | 399 | engine.c, engine_diplomacy_action.c, engine_diplomacy_action.h (+1) |
| 0x417aef | FUN_00417aef | 228 | rl_render.c |
| 0x417bd3 | FUN_00417bd3 | 284 | rl_render.c |
| 0x417cef | FUN_00417cef | 440 | rl_render.c |
| 0x417ea7 | FUN_00417ea7 | 591 | engine.c, engine_armory.c, engine_armory.h |
| 0x4181eb | FUN_004181eb | 571 | engine.c, engine_armory.c, engine_armory.h |
| 0x418426 | FUN_00418426 | 160 | engine_armory.c, engine_armory.h |
| 0x4184c6 | FUN_004184c6 | 397 | engine.c, engine_armory.c, engine_armory.h (+1) |
| 0x418653 | FUN_00418653 | 2010 | engine.c, engine_armory.c, engine_armory.h |
| 0x418e2d | FUN_00418e2d | 408 | engine.c, engine_armory.c, engine_armory.h (+1) |
| 0x418fc5 | FUN_00418fc5 | 278 | engine.c, engine_armory.c, engine_armory.h |
| 0x4190db | FUN_004190db | 360 | engine.c, engine_armory.c, engine_armory.h |
| 0x419243 | FUN_00419243 | 110 | engine.c, engine_armory.c, engine_armory.h |
| 0x4192b1 | FUN_004192b1 | 163 | rl_render.c |
| 0x419354 | FUN_00419354 | 1077 | rl_render.c |
| 0x419789 | FUN_00419789 | 289 | rl_render.c |
| 0x4198aa | FUN_004198aa | 974 | rl_render.c |
| 0x41a166 | FUN_0041a166 | 1090 | rl_render.c |
| 0x41a639 | FUN_0041a639 | 251 | rl_render.c |
| 0x41a844 | FUN_0041a844 | 188 | rl_render.c |
| 0x41ad5d | FUN_0041ad5d | 325 | engine.c, engine_sendsupplies.c, engine_sendsupplies.h (+1) |
| 0x41aea2 | FUN_0041aea2 | 369 | engine.c, engine_sendsupplies.c, engine_sendsupplies.h (+1) |
| 0x41b013 | FUN_0041b013 | 31 | rl_io.c |
| 0x41b19d | FUN_0041b19d | 3425 | rl_render.c |
| 0x41cb3a | FUN_0041cb3a | 1887 | rl_ui2.c |
| 0x41d299 | FUN_0041d299 | 1942 | rl_render.c |
| 0x41da2f | FUN_0041da2f | 1196 | rl_render.c |
| 0x41dedb | FUN_0041dedb | 770 | engine_labor.c, engine_labor.h, rl_link_screens.c |
| 0x41e1dd | FUN_0041e1dd | 1011 | rl_render.c |
| 0x41e5d0 | FUN_0041e5d0 | 77 | rl_ui4.c |
| 0x41ef42 | FUN_0041ef42 | 21 | rl_ui2.c |
| 0x41f3e9 | FUN_0041f3e9 | 184 | rl_mpnet.c |
| 0x41f4a1 | FUN_0041f4a1 | 241 | rl_mpnet.c |
| 0x41ff75 | FUN_0041ff75 | 466 | rl_render.c |
| 0x420147 | FUN_00420147 | 463 | rl_skirmish.c |
| 0x420316 | FUN_00420316 | 274 | rl_render.c |
| 0x420428 | FUN_00420428 | 244 | rl_ui4.c |
| 0x42051c | FUN_0042051c | 276 | rl_skirmish.c |
| 0x420630 | FUN_00420630 | 403 | rl_mpnet2.c |
| 0x4207c3 | FUN_004207c3 | 510 | rl_skirmish.c |
| 0x4209c1 | FUN_004209c1 | 895 | rl_skirmish.c |
| 0x420d40 | FUN_00420d40 | 164 | rl_skirmish.c |
| 0x420de4 | FUN_00420de4 | 545 | rl_skirmish.c |
| 0x421005 | FUN_00421005 | 556 | rl_skirmish.c |
| 0x421231 | FUN_00421231 | 222 | rl_skirmish.c |
| 0x42130f | FUN_0042130f | 508 | rl_skirmish.c |
| 0x42150b | FUN_0042150b | 508 | rl_skirmish.c |
| 0x421707 | FUN_00421707 | 1538 | rl_skirmish.c |
| 0x421d09 | FUN_00421d09 | 523 | rl_skirmish.c |
| 0x421f14 | FUN_00421f14 | 1491 | rl_render.c |
| 0x4224e7 | FUN_004224e7 | 1835 | rl_render.c |
| 0x422d22 | FUN_00422d22 | 719 | rl_render.c |
| 0x422ff1 | FUN_00422ff1 | 592 | rl_render.c |
| 0x423241 | FUN_00423241 | 438 | rl_render.c |
| 0x4233f7 | FUN_004233f7 | 313 | rl_render.c |
| 0x423530 | FUN_00423530 | 521 | rl_render.c |
| 0x423739 | FUN_00423739 | 383 | rl_render.c |
| 0x4238b8 | FUN_004238b8 | 285 | engine_bf_input.c |
| 0x4239d5 | FUN_004239d5 | 378 | engine_bf_input.c |
| 0x423ba4 | FUN_00423ba4 | 3015 | rl_render.c |
| 0x42476b | FUN_0042476b | 138 | rl_render.c |
| 0x4247f5 | FUN_004247f5 | 174 | rl_ui4.c |
| 0x4248a3 | FUN_004248a3 | 239 | rl_render.c |
| 0x424992 | FUN_00424992 | 2434 | rl_render.c |
| 0x425314 | FUN_00425314 | 371 | rl_skirmish.c |
| 0x425487 | FUN_00425487 | 437 | rl_skirmish.c |
| 0x42563c | FUN_0042563c | 349 | rl_ui4.c |
| 0x425a6a | FUN_00425a6a | 158 | rl_ui4.c |
| 0x425b10 | FUN_00425b10 | 549 | audio.c, audio.h, rl_ui4.c |
| 0x425d35 | FUN_00425d35 | 184 | rl_quit.c |
| 0x425ded | FUN_00425ded | 94 | audio.c, audio.h, rl_ui4.c |
| 0x425e4b | FUN_00425e4b | 725 | rl_io.c |
| 0x426120 | FUN_00426120 | 246 | engine_bf_strike.c |
| 0x426216 | FUN_00426216 | 185 | rl_io.c |
| 0x4262cf | FUN_004262cf | 28 | engine_bf_strike.c |
| 0x4262eb | FUN_004262eb | 117 | rl_io.c |
| 0x426360 | FUN_00426360 | 77 | rl_io.c |
| 0x4263ad | FUN_004263ad | 255 | rl_io.c |
| 0x426529 | FUN_00426529 | 11 | rl_io.c |
| 0x426534 | FUN_00426534 | 1630 | rl_io.c |
| 0x426b92 | FUN_00426b92 | 383 | rl_io.c |
| 0x426dd5 | FUN_00426dd5 | 3003 | rl_io.c |
| 0x427990 | FUN_00427990 | 779 | rl_io.c |
| 0x427c9b | FUN_00427c9b | 126 | rl_io.c |
| 0x427d72 | FUN_00427d72 | 340 | rl_ui4.c |
| 0x428daf | FUN_00428daf | 801 | engine_economy_globals.c, engine_trade.c, engine_trade.h (+1) |
| 0x42a7f1 | FUN_0042a7f1 | 224 | engine_map_terrain.c, engine_map_terrain.h, engine_screen_strat.c |
| 0x42a8d1 | FUN_0042a8d1 | 218 | engine_map_terrain.c, engine_map_terrain.h, rl_glue.c |
| 0x42a9ab | FUN_0042a9ab | 218 | engine_map_terrain.c, engine_map_terrain.h, rl_glue.c |
| 0x42aa90 | FUN_0042aa90 | 133 | rl_skirmish.c |
| 0x42ab15 | FUN_0042ab15 | 109 | rl_skirmish.c |
| 0x42ab82 | FUN_0042ab82 | 138 | rl_skirmish.c |
| 0x42ac0c | FUN_0042ac0c | 2932 | rl_skirmish.c |
| 0x42b780 | FUN_0042b780 | 119 | rl_skirmish.c |
| 0x42b7f7 | FUN_0042b7f7 | 290 | rl_skirmish.c |
| 0x42b919 | FUN_0042b919 | 171 | rl_skirmish.c |
| 0x42b9c4 | FUN_0042b9c4 | 124 | rl_skirmish.c |
| 0x42ba40 | FUN_0042ba40 | 1286 | rl_skirmish.c |
| 0x42bf46 | FUN_0042bf46 | 935 | rl_skirmish.c |
| 0x42c476 | FUN_0042c476 | 108 | rl_skirmish.c |
| 0x42c4e2 | FUN_0042c4e2 | 203 | rl_skirmish.c |
| 0x42c5ad | FUN_0042c5ad | 81 | rl_skirmish.c |
| 0x42c5fe | FUN_0042c5fe | 81 | rl_skirmish.c |
| 0x42c64f | FUN_0042c64f | 1531 | rl_skirmish.c |
| 0x42cc4a | FUN_0042cc4a | 1327 | rl_skirmish.c |
| 0x42d179 | FUN_0042d179 | 157 | rl_glue.c |
| 0x42d216 | FUN_0042d216 | 198 | rl_skirmish.c |
| 0x42d2dc | FUN_0042d2dc | 173 | rl_skirmish.c |
| 0x42d389 | FUN_0042d389 | 130 | rl_skirmish.c |
| 0x42d40b | FUN_0042d40b | 159 | rl_mpnet2.c |
| 0x42d4aa | FUN_0042d4aa | 860 | rl_skirmish.c |
| 0x42d806 | FUN_0042d806 | 81 | engine_bf_setup.c |
| 0x42d857 | FUN_0042d857 | 179 | rl_skirmish.c |
| 0x42d91b | FUN_0042d91b | 83 | rl_io.c |
| 0x42da18 | FUN_0042da18 | 431 | rl_io.c |
| 0x42dbc7 | FUN_0042dbc7 | 868 | rl_io.c |
| 0x42df30 | FUN_0042df30 | 178 | rl_cov3.c |
| 0x42e060 | FUN_0042e060 | 527 | rl_render.c |
| 0x42e26f | FUN_0042e26f | 41 | rl_glue.c |
| 0x42e298 | FUN_0042e298 | 136 | rl_io.c |
| 0x42e320 | FUN_0042e320 | 66 | rl_io.c |
| 0x42e362 | FUN_0042e362 | 31 | rl_io.c |
| 0x42e3ba | FUN_0042e3ba | 164 | rl_ui4.c |
| 0x42e45e | FUN_0042e45e | 175 | rl_ui4.c |
| 0x42e50d | FUN_0042e50d | 179 | rl_ui4.c |
| 0x42e5c0 | FUN_0042e5c0 | 1249 | gfx.c, gfx.h, rl_ui4.c |
| 0x42eaa6 | FUN_0042eaa6 | 214 | gfx.c, rl_quit.c |
| 0x42eb7c | FUN_0042eb7c | 277 | gfx.c, gfx.h, rl_ui4.c |
| 0x42f148 | FUN_0042f148 | 151 | rl_glue.c |
| 0x42f306 | FUN_0042f306 | 323 | gfx.c, gfx.h, rl_ui4.c |
| 0x42f449 | FUN_0042f449 | 323 | gfx.c, gfx.h, rl_ui4.c |
| 0x42f58c | FUN_0042f58c | 130 | rl_io.c |
| 0x42f60e | FUN_0042f60e | 2298 | rl_ui4.c |
| 0x4323fe | FUN_004323fe | 64 | rl_glue.c |
| 0x432443 | FUN_00432443 | 247 | rl_glue.c |
| 0x43253a | FUN_0043253a | 262 | rl_glue.c |
| 0x432893 | FUN_00432893 | 154 | rl_glue.c |
| 0x43292d | FUN_0043292d | 58 | rl_glue.c |
| 0x4329a4 | FUN_004329a4 | 61 | rl_glue.c |
| 0x43306a | FUN_0043306a | 92 | rl_mpnet.c |
| 0x4330c6 | FUN_004330c6 | 143 | rl_glue.c |
| 0x43338e | FUN_0043338e | 195 | rl_mpnet2.c |
| 0x433551 | FUN_00433551 | 159 | rl_mpnet.c |
| 0x433a70 | FUN_00433a70 | 251 | rl_glue.c |
| 0x433cc1 | FUN_00433cc1 | 35 | rl_io.c |
| 0x433dbd | FUN_00433dbd | 46 | rl_ui2.c |
| 0x433deb | FUN_00433deb | 168 | rl_ui2.c |
| 0x433e93 | FUN_00433e93 | 182 | rl_glue.c |
| 0x433f49 | FUN_00433f49 | 194 | rl_glue.c |
| 0x434174 | FUN_00434174 | 383 | rl_ui2.c |
| 0x4343f8 | FUN_004343f8 | 36 | rl_ui2.c |
| 0x43441c | FUN_0043441c | 158 | rl_ui2.c |
| 0x4344f2 | FUN_004344f2 | 100 | rl_glue.c |
| 0x434556 | FUN_00434556 | 122 | rl_ui3.c |
| 0x4345d0 | FUN_004345d0 | 195 | rl_ui3.c |
| 0x434693 | FUN_00434693 | 122 | rl_ui3.c |
| 0x43470d | FUN_0043470d | 122 | rl_glue.c |
| 0x434787 | FUN_00434787 | 64 | rl_ui2.c |
| 0x4347c7 | FUN_004347c7 | 69 | rl_ui2.c |
| 0x43480c | FUN_0043480c | 62 | rl_ui3.c |
| 0x43484a | FUN_0043484a | 62 | rl_ui2.c |
| 0x434888 | FUN_00434888 | 62 | rl_ui2.c |
| 0x4348c6 | FUN_004348c6 | 62 | rl_ui2.c |
| 0x434904 | FUN_00434904 | 62 | rl_ui2.c |
| 0x434942 | FUN_00434942 | 42 | rl_ui3.c |
| 0x43496c | FUN_0043496c | 28 | rl_glue.c |
| 0x434988 | FUN_00434988 | 28 | rl_glue.c |
| 0x4349a4 | FUN_004349a4 | 133 | rl_ui3.c |
| 0x434a29 | FUN_00434a29 | 113 | rl_ui3.c |
| 0x434a9a | FUN_00434a9a | 59 | rl_ui3.c |
| 0x434ad5 | FUN_00434ad5 | 59 | rl_ui3.c |
| 0x434b10 | FUN_00434b10 | 281 | rl_ui3.c |
| 0x434cbe | FUN_00434cbe | 48 | rl_glue.c |
| 0x434cee | FUN_00434cee | 48 | rl_glue.c |
| 0x434d33 | FUN_00434d33 | 23 | rl_ui2.c |
| 0x434d60 | FUN_00434d60 | 23 | rl_ui2.c |
| 0x434e9e | FUN_00434e9e | 43 | rl_ui3.c |
| 0x434ec9 | FUN_00434ec9 | 48 | rl_ui3.c |
| 0x434fa1 | FUN_00434fa1 | 52 | rl_ui2.c |
| 0x434fd5 | FUN_00434fd5 | 133 | rl_ui2.c |
| 0x43505a | FUN_0043505a | 71 | rl_ui2.c |
| 0x4350a1 | FUN_004350a1 | 232 | rl_ui2.c |
| 0x435211 | FUN_00435211 | 61 | engine.c, rl_ctl.c |
| 0x435286 | FUN_00435286 | 108 | engine_trade.h, engine_trade_model.c |
| 0x4352f2 | FUN_004352f2 | 28 | engine_trade.h, engine_trade_model.c |
| 0x43530e | FUN_0043530e | 43 | engine.c, engine_trade.c, engine_trade.h (+1) |
| 0x435339 | FUN_00435339 | 260 | engine_trade.h, engine_trade_model.c |
| 0x43543d | FUN_0043543d | 260 | engine_trade.h, engine_trade_model.c |
| 0x435541 | FUN_00435541 | 154 | engine_trade.h, engine_trade_model.c |
| 0x4355db | FUN_004355db | 152 | engine_trade.h, engine_trade_model.c |
| 0x435673 | FUN_00435673 | 296 | engine_trade.h, engine_trade_model.c |
| 0x43582a | FUN_0043582a | 134 | engine.c, engine_armory.c, engine_armory.h (+1) |
| 0x4358b0 | FUN_004358b0 | 138 | engine.c, engine_armory.c, engine_armory.h (+1) |
| 0x43593a | FUN_0043593a | 130 | engine_armory.c, engine_armory.h, rl_ctl.c |
| 0x4359bc | FUN_004359bc | 80 | engine_armory.h, rl_ctl.c |
| 0x435a0c | FUN_00435a0c | 85 | engine_armory.h, rl_ctl.c |
| 0x435a61 | FUN_00435a61 | 135 | engine_armory.h, rl_ctl.c |
| 0x435ae8 | FUN_00435ae8 | 101 | engine.c, engine_armory.h, rl_ctl.c |
| 0x435b4d | FUN_00435b4d | 316 | engine.c, engine_armory.h, rl_ctl.c |
| 0x435c89 | FUN_00435c89 | 54 | engine.c, engine_armory.h, rl_ctl.c |
| 0x435cbf | FUN_00435cbf | 48 | engine.c, engine_armory.h, rl_ctl.c |
| 0x435ebc | FUN_00435ebc | 336 | engine_armory.c, engine_armory.h, engine_sendsupplies.h (+1) |
| 0x43600c | FUN_0043600c | 230 | rl_ui2.c |
| 0x4360f2 | FUN_004360f2 | 41 | rl_glue.c |
| 0x43611b | FUN_0043611b | 38 | engine.c, engine_noble_faces.h, engine_sendsupplies_model.c (+1) |
| 0x436141 | FUN_00436141 | 74 | engine.c, engine_diplomacy_action.h, engine_noble_faces.h (+1) |
| 0x43618b | FUN_0043618b | 79 | rl_glue.c |
| 0x4361da | FUN_004361da | 79 | rl_ui3.c |
| 0x436229 | FUN_00436229 | 161 | rl_ui3.c |
| 0x4362ca | FUN_004362ca | 84 | rl_ui3.c |
| 0x43631e | FUN_0043631e | 84 | rl_glue.c |
| 0x436372 | FUN_00436372 | 150 | engine.c, engine_diplomacy_action.c, engine_diplomacy_action.h (+1) |
| 0x4367ff | FUN_004367ff | 115 | rl_cov3.c |
| 0x436872 | FUN_00436872 | 139 | rl_glue.c |
| 0x43695d | FUN_0043695d | 96 | rl_mpnet2.c |
| 0x4369bd | FUN_004369bd | 203 | rl_glue.c |
| 0x436a88 | FUN_00436a88 | 154 | engine_sendsupplies_model.c, rl_ctl.c |
| 0x436b22 | FUN_00436b22 | 55 | rl_glue.c |
| 0x436b59 | FUN_00436b59 | 415 | rl_glue.c |
| 0x437002 | FUN_00437002 | 261 | rl_glue.c |
| 0x43733a | FUN_0043733a | 277 | rl_glue.c |
| 0x43744f | FUN_0043744f | 117 | rl_glue.c |
| 0x4374c4 | FUN_004374c4 | 113 | rl_glue.c |
| 0x4376bb | FUN_004376bb | 504 | rl_glue.c |
| 0x4378b3 | FUN_004378b3 | 584 | rl_ui3.c |
| 0x437afb | FUN_00437afb | 618 | rl_ui3.c |
| 0x437d65 | FUN_00437d65 | 313 | rl_ui3.c |
| 0x437e9e | FUN_00437e9e | 313 | rl_ui3.c |
| 0x437fd7 | FUN_00437fd7 | 1706 | rl_ui3.c |
| 0x438990 | FUN_00438990 | 257 | rl_glue.c |
| 0x438a91 | FUN_00438a91 | 59 | rl_glue.c |
| 0x438acc | FUN_00438acc | 54 | rl_glue.c |
| 0x438b02 | FUN_00438b02 | 234 | rl_ui2.c |
| 0x438bec | FUN_00438bec | 255 | rl_ui2.c |
| 0x438e3b | FUN_00438e3b | 574 | rl_render.c |
| 0x439079 | FUN_00439079 | 169 | rl_glue.c |
| 0x439122 | FUN_00439122 | 537 | rl_glue.c |
| 0x43933b | FUN_0043933b | 176 | rl_glue.c |
| 0x4393eb | FUN_004393eb | 342 | rl_glue.c |
| 0x439541 | FUN_00439541 | 73 | rl_ui2.c |
| 0x43958a | FUN_0043958a | 674 | rl_ui2.c |
| 0x4398f5 | FUN_004398f5 | 187 | engine_labor.c, engine_labor_region_grid.h, rl_ctl.c |
| 0x4399b0 | FUN_004399b0 | 418 | rl_ui2.c |
| 0x439b52 | FUN_00439b52 | 368 | rl_ui2.c |
| 0x439cc2 | FUN_00439cc2 | 302 | rl_ui2.c |
| 0x439edb | FUN_00439edb | 143 | rl_ui2.c |
| 0x439f6a | FUN_00439f6a | 441 | rl_ui2.c |
| 0x43a950 | FUN_0043a950 | 71 | rl_ui2.c |
| 0x43a997 | FUN_0043a997 | 155 | rl_ui2.c |
| 0x43ab76 | FUN_0043ab76 | 173 | rl_ui2.c |
| 0x43ac23 | FUN_0043ac23 | 258 | engine.c, rl_ctl.c |
| 0x43ad25 | FUN_0043ad25 | 256 | engine.c, rl_ctl.c |
| 0x43ae30 | FUN_0043ae30 | 540 | engine.c, engine_armory.h, engine_noble_faces.h (+3) |
| 0x43b04c | FUN_0043b04c | 181 | engine.c, rl_ctl.c |
| 0x43b101 | FUN_0043b101 | 68 | rl_ui4.c |
| 0x43b145 | FUN_0043b145 | 133 | engine.c, engine_sendsupplies.h, engine_sendsupplies_model.c |
| 0x43b1ca | FUN_0043b1ca | 176 | engine_sendsupplies.h, engine_sendsupplies_model.c |
| 0x43b27a | FUN_0043b27a | 176 | engine_sendsupplies.h, engine_sendsupplies_model.c |
| 0x43b32a | FUN_0043b32a | 232 | engine.c, engine_sendsupplies.c, engine_sendsupplies.h (+1) |
| 0x43b4cb | FUN_0043b4cb | 200 | rl_glue.c |
| 0x43b593 | FUN_0043b593 | 143 | rl_glue.c |
| 0x43b681 | FUN_0043b681 | 192 | rl_glue.c |
| 0x43b741 | FUN_0043b741 | 131 | rl_cov3.c |
| 0x43b7c4 | FUN_0043b7c4 | 132 | rl_glue.c |
| 0x43b848 | FUN_0043b848 | 207 | rl_glue.c |
| 0x43b98a | FUN_0043b98a | 23 | rl_glue.c |
| 0x43b9a1 | FUN_0043b9a1 | 136 | engine_bf_input.c |
| 0x43ba29 | FUN_0043ba29 | 208 | engine_bf_input.c |
| 0x43baf9 | FUN_0043baf9 | 111 | rl_ui4.c |
| 0x43bb68 | FUN_0043bb68 | 111 | engine_bf_input.c |
| 0x43bbd7 | FUN_0043bbd7 | 16 | engine_bf_input.c |
| 0x43bbe7 | FUN_0043bbe7 | 283 | rl_ui3.c |
| 0x43bd02 | FUN_0043bd02 | 101 | engine_bf_input.c |
| 0x43bd67 | FUN_0043bd67 | 102 | engine_bf_input.c |
| 0x43bdcd | FUN_0043bdcd | 152 | engine_bf_input.c |
| 0x43bf07 | FUN_0043bf07 | 827 | engine_bf_input.c |
| 0x43c247 | FUN_0043c247 | 98 | engine_bf_input.c |
| 0x43c2a9 | FUN_0043c2a9 | 541 | engine_bf_input.c |
| 0x43c4c6 | FUN_0043c4c6 | 150 | engine_bf_input.c |
| 0x43c55c | FUN_0043c55c | 33 | engine_bf_input.c |
| 0x43c57d | FUN_0043c57d | 183 | engine_bf_input.c |
| 0x43c634 | FUN_0043c634 | 326 | engine_bf_input.c |
| 0x43c77a | FUN_0043c77a | 267 | rl_ui2.c |
| 0x43c885 | FUN_0043c885 | 139 | rl_ui2.c |
| 0x43c910 | FUN_0043c910 | 344 | rl_ui2.c |
| 0x43d5b7 | FUN_0043d5b7 | 146 | rl_ui2.c |
| 0x43d649 | FUN_0043d649 | 94 | rl_ui2.c |
| 0x43d6a7 | FUN_0043d6a7 | 28 | rl_ui2.c |
| 0x43d6c3 | FUN_0043d6c3 | 200 | rl_ui2.c |
| 0x43d78b | FUN_0043d78b | 28 | rl_ui2.c |
| 0x43d7a7 | FUN_0043d7a7 | 219 | rl_ui2.c |
| 0x43d882 | FUN_0043d882 | 55 | rl_ui2.c |
| 0x43d8b9 | FUN_0043d8b9 | 74 | rl_ui2.c |
| 0x43d929 | FUN_0043d929 | 164 | rl_ui2.c |
| 0x43d9cd | FUN_0043d9cd | 209 | rl_ui2.c |
| 0x43da9e | FUN_0043da9e | 85 | rl_ui2.c |
| 0x43daf3 | FUN_0043daf3 | 185 | rl_ui2.c |
| 0x43dbac | FUN_0043dbac | 113 | rl_ui2.c |
| 0x43dc1d | FUN_0043dc1d | 358 | rl_ui2.c |
| 0x43dd83 | FUN_0043dd83 | 113 | rl_ui2.c |
| 0x43ddf4 | FUN_0043ddf4 | 114 | rl_ui2.c |
| 0x43e5c1 | FUN_0043e5c1 | 167 | rl_mpmsg.c |
| 0x43e668 | FUN_0043e668 | 58 | rl_ui4.c |
| 0x43e6a2 | FUN_0043e6a2 | 594 | rl_mpmsg3.c |
| 0x43e8f4 | FUN_0043e8f4 | 151 | rl_mpmsg3.c |
| 0x43e98b | FUN_0043e98b | 87 | rl_mpmsg.c |
| 0x43eac1 | FUN_0043eac1 | 735 | rl_mpmsg.c |
| 0x43eda0 | FUN_0043eda0 | 637 | rl_glue.c |
| 0x43f01d | FUN_0043f01d | 558 | rl_mpmsg.c |
| 0x43f24b | FUN_0043f24b | 185 | rl_ui2.c |
| 0x43f304 | FUN_0043f304 | 78 | engine_bf_extern.c |
| 0x43f352 | FUN_0043f352 | 236 | rl_ui2.c |
| 0x43f43e | FUN_0043f43e | 70 | rl_quit.c |
| 0x43f484 | FUN_0043f484 | 51 | rl_ui2.c |
| 0x43f4b7 | FUN_0043f4b7 | 270 | rl_mpmsg.c |
| 0x43f5c5 | FUN_0043f5c5 | 542 | rl_mpmsg3.c |
| 0x43f7e3 | FUN_0043f7e3 | 114 | rl_host.c |
| 0x43f855 | FUN_0043f855 | 170 | rl_mpmsg3.c |
| 0x43f8ff | FUN_0043f8ff | 58 | rl_mpmsg.c |
| 0x43fa1d | FUN_0043fa1d | 89 | rl_mpmsg3.c |
| 0x43faa4 | FUN_0043faa4 | 436 | rl_mpmsg3.c |
| 0x43fd2a | FUN_0043fd2a | 413 | rl_mpmsg.c |
| 0x43fed2 | FUN_0043fed2 | 54 | rl_mpmsg.c |
| 0x43ff08 | FUN_0043ff08 | 91 | rl_host.c |
| 0x43ff63 | FUN_0043ff63 | 96 | rl_host.c |
| 0x440038 | FUN_00440038 | 81 | rl_mpmsg.c |
| 0x4401ba | FUN_004401ba | 60 | rl_mpmsg.c |
| 0x4401f6 | FUN_004401f6 | 59 | rl_mpmsg.c |
| 0x440666 | FUN_00440666 | 383 | rl_glue.c |
| 0x4407e5 | FUN_004407e5 | 426 | rl_mpmsg.c |
| 0x44098f | FUN_0044098f | 595 | rl_render.c |
| 0x440dd2 | FUN_00440dd2 | 638 | rl_mpmsg.c |
| 0x441050 | FUN_00441050 | 11 | rl_mpmsg3.c |
| 0x44105b | FUN_0044105b | 11 | rl_mpmsg3.c |
| 0x441066 | FUN_00441066 | 87 | rl_mpmsg.c |
| 0x4410bd | FUN_004410bd | 349 | rl_mpmsg.c |
| 0x44121a | FUN_0044121a | 178 | rl_mpmsg.c |
| 0x4412cc | FUN_004412cc | 216 | rl_mpmsg.c |
| 0x4413a4 | FUN_004413a4 | 681 | rl_mpmsg.c |
| 0x44164d | FUN_0044164d | 696 | rl_mpmsg.c |
| 0x441905 | FUN_00441905 | 244 | rl_mpmsg.c |
| 0x4419f9 | FUN_004419f9 | 341 | rl_mpmsg.c |
| 0x441b4e | FUN_00441b4e | 56 | rl_mpmsg.c |
| 0x441b86 | FUN_00441b86 | 420 | rl_mpmsg.c |
| 0x441d2a | FUN_00441d2a | 245 | rl_mpmsg.c |
| 0x441e1f | FUN_00441e1f | 267 | rl_mpmsg.c |
| 0x441f2a | FUN_00441f2a | 38 | rl_mpmsg.c |
| 0x441f50 | FUN_00441f50 | 38 | rl_mpmsg.c |
| 0x441f76 | FUN_00441f76 | 158 | rl_mpmsg.c |
| 0x442014 | FUN_00442014 | 153 | rl_mpmsg.c |
| 0x4420ad | FUN_004420ad | 278 | rl_mpmsg.c |
| 0x4421c3 | FUN_004421c3 | 326 | rl_mpmsg.c |
| 0x442309 | FUN_00442309 | 88 | rl_mpmsg.c |
| 0x442361 | FUN_00442361 | 88 | rl_mpmsg.c |
| 0x4423b9 | FUN_004423b9 | 81 | rl_mpmsg.c |
| 0x44240a | FUN_0044240a | 62 | rl_mpmsg.c |
| 0x442448 | FUN_00442448 | 142 | rl_mpmsg.c |
| 0x4424d6 | FUN_004424d6 | 137 | rl_mpmsg.c |
| 0x44255f | FUN_0044255f | 26 | rl_mpmsg3.c |
| 0x442579 | FUN_00442579 | 70 | rl_mpmsg3.c |
| 0x4425bf | FUN_004425bf | 530 | rl_mpmsg.c |
| 0x4427d1 | FUN_004427d1 | 545 | rl_mpmsg.c |
| 0x4429f2 | FUN_004429f2 | 51 | rl_mpmsg.c |
| 0x442a25 | FUN_00442a25 | 90 | rl_mpmsg.c |
| 0x442a7f | FUN_00442a7f | 47 | rl_mpmsg3.c |
| 0x442aae | FUN_00442aae | 47 | rl_mpmsg3.c |
| 0x442add | FUN_00442add | 11 | rl_mpmsg3.c |
| 0x442ae8 | FUN_00442ae8 | 21 | rl_mpmsg3.c |
| 0x442afd | FUN_00442afd | 11 | rl_mpmsg.c |
| 0x442b08 | FUN_00442b08 | 16 | rl_mpmsg.c |
| 0x442b18 | FUN_00442b18 | 104 | rl_mpmsg3.c |
| 0x442b80 | FUN_00442b80 | 45 | rl_mpmsg3.c |
| 0x442bad | FUN_00442bad | 84 | rl_mpmsg.c |
| 0x442c01 | FUN_00442c01 | 62 | rl_mpmsg.c |
| 0x442c3f | FUN_00442c3f | 67 | rl_mpmsg.c |
| 0x442c82 | FUN_00442c82 | 55 | rl_mpmsg.c |
| 0x442cb9 | FUN_00442cb9 | 29 | rl_mpmsg.c |
| 0x442cd6 | FUN_00442cd6 | 42 | rl_mpmsg.c |
| 0x442d00 | FUN_00442d00 | 96 | rl_mpmsg3.c |
| 0x442d60 | FUN_00442d60 | 77 | rl_mpmsg3.c |
| 0x442dad | FUN_00442dad | 96 | rl_mpmsg3.c |
| 0x442e0d | FUN_00442e0d | 77 | rl_mpmsg3.c |
| 0x442e5a | FUN_00442e5a | 146 | rl_mpmsg3.c |
| 0x442eec | FUN_00442eec | 115 | rl_mpmsg3.c |
| 0x442f5f | FUN_00442f5f | 145 | rl_mpmsg3.c |
| 0x442ff0 | FUN_00442ff0 | 26 | rl_mpmsg3.c |
| 0x44300a | FUN_0044300a | 26 | rl_mpmsg3.c |
| 0x443024 | FUN_00443024 | 31 | rl_mpmsg3.c |
| 0x443043 | FUN_00443043 | 64 | rl_mpmsg3.c |
| 0x443083 | FUN_00443083 | 82 | rl_mpmsg3.c |
| 0x4430d5 | FUN_004430d5 | 110 | rl_mpmsg3.c |
| 0x443143 | FUN_00443143 | 172 | rl_mpmsg3.c |
| 0x4431ef | FUN_004431ef | 151 | rl_mpmsg2.c |
| 0x443286 | FUN_00443286 | 156 | rl_mpmsg2.c |
| 0x443322 | FUN_00443322 | 11 | rl_mpmsg3.c |
| 0x44332d | FUN_0044332d | 11 | rl_mpmsg3.c |
| 0x443338 | FUN_00443338 | 11 | rl_mpmsg3.c |
| 0x443343 | FUN_00443343 | 11 | rl_mpmsg3.c |
| 0x44334e | FUN_0044334e | 61 | rl_mpmsg3.c |
| 0x44338b | FUN_0044338b | 78 | rl_mpmsg3.c |
| 0x4433d9 | FUN_004433d9 | 66 | rl_mpmsg2.c |
| 0x44341b | FUN_0044341b | 113 | rl_mpmsg2.c |
| 0x44348c | FUN_0044348c | 95 | rl_mpmsg2.c |
| 0x4434eb | FUN_004434eb | 130 | rl_mpmsg2.c |
| 0x44356d | FUN_0044356d | 26 | rl_mpmsg3.c |
| 0x443587 | FUN_00443587 | 71 | rl_mpmsg3.c |
| 0x4435ce | FUN_004435ce | 109 | rl_mpmsg3.c |
| 0x44363b | FUN_0044363b | 128 | rl_mpmsg3.c |
| 0x4436bb | FUN_004436bb | 62 | rl_mpmsg3.c |
| 0x4436f9 | FUN_004436f9 | 79 | rl_mpmsg3.c |
| 0x443748 | FUN_00443748 | 98 | rl_mpmsg2.c |
| 0x4437aa | FUN_004437aa | 115 | rl_mpmsg2.c |
| 0x44381d | FUN_0044381d | 166 | rl_mpmsg2.c |
| 0x4438c3 | FUN_004438c3 | 205 | rl_mpmsg2.c |
| 0x443990 | FUN_00443990 | 157 | rl_mpmsg2.c |
| 0x443a2d | FUN_00443a2d | 168 | rl_mpmsg2.c |
| 0x443ad5 | FUN_00443ad5 | 184 | rl_mpmsg2.c |
| 0x443b8d | FUN_00443b8d | 162 | rl_mpmsg2.c |
| 0x443c2f | FUN_00443c2f | 119 | rl_mpmsg2.c |
| 0x443ca6 | FUN_00443ca6 | 100 | rl_mpmsg2.c |
| 0x443d0a | FUN_00443d0a | 176 | rl_mpmsg2.c |
| 0x443dba | FUN_00443dba | 151 | rl_mpmsg2.c |
| 0x443e51 | FUN_00443e51 | 184 | rl_mpmsg3.c |
| 0x443f09 | FUN_00443f09 | 162 | rl_mpmsg3.c |
| 0x443fab | FUN_00443fab | 100 | rl_mpmsg3.c |
| 0x44400f | FUN_0044400f | 81 | rl_mpmsg3.c |
| 0x444060 | FUN_00444060 | 160 | rl_mpmsg2.c |
| 0x444100 | FUN_00444100 | 145 | rl_mpmsg2.c |
| 0x444191 | FUN_00444191 | 11 | rl_mpmsg2.c |
| 0x44419c | FUN_0044419c | 59 | rl_mpmsg2.c |
| 0x4441d7 | FUN_004441d7 | 169 | rl_mpmsg2.c |
| 0x444280 | FUN_00444280 | 198 | rl_mpmsg2.c |
| 0x444346 | FUN_00444346 | 100 | rl_mpmsg3.c |
| 0x4443aa | FUN_004443aa | 81 | rl_mpmsg3.c |
| 0x4443fb | FUN_004443fb | 26 | rl_mpmsg2.c |
| 0x444415 | FUN_00444415 | 100 | rl_mpmsg2.c |
| 0x444479 | FUN_00444479 | 119 | rl_mpmsg3.c |
| 0x4444f0 | FUN_004444f0 | 100 | rl_mpmsg3.c |
| 0x444554 | FUN_00444554 | 119 | rl_mpmsg3.c |
| 0x4445cb | FUN_004445cb | 100 | rl_mpmsg3.c |
| 0x44462f | FUN_0044462f | 119 | rl_mpmsg3.c |
| 0x4446a6 | FUN_004446a6 | 100 | rl_mpmsg3.c |
| 0x44470a | FUN_0044470a | 82 | rl_mpmsg3.c |
| 0x44475c | FUN_0044475c | 79 | rl_mpmsg3.c |
| 0x4447ab | FUN_004447ab | 100 | rl_mpmsg3.c |
| 0x44480f | FUN_0044480f | 81 | rl_mpmsg3.c |
| 0x444860 | FUN_00444860 | 100 | rl_mpmsg3.c |
| 0x4448c4 | FUN_004448c4 | 81 | rl_mpmsg3.c |
| 0x444915 | FUN_00444915 | 51 | rl_mpmsg3.c |
| 0x444948 | FUN_00444948 | 88 | rl_mpmsg3.c |
| 0x4449a0 | FUN_004449a0 | 81 | rl_mpmsg2.c |
| 0x4449f1 | FUN_004449f1 | 62 | rl_mpmsg2.c |
| 0x444a2f | FUN_00444a2f | 108 | rl_mpmsg2.c |
| 0x444a9b | FUN_00444a9b | 108 | rl_mpmsg2.c |
| 0x444b07 | FUN_00444b07 | 16 | rl_mpmsg3.c |
| 0x444b17 | FUN_00444b17 | 16 | rl_mpmsg3.c |
| 0x444b27 | FUN_00444b27 | 169 | rl_mpmsg3.c |
| 0x444bd0 | FUN_00444bd0 | 150 | rl_mpmsg3.c |
| 0x444c66 | FUN_00444c66 | 79 | rl_mpmsg3.c |
| 0x444cb5 | FUN_00444cb5 | 79 | rl_mpmsg3.c |
| 0x444d04 | FUN_00444d04 | 100 | rl_mpmsg2.c |
| 0x444d68 | FUN_00444d68 | 81 | rl_mpmsg2.c |
| 0x444db9 | FUN_00444db9 | 67 | rl_mpmsg3.c |
| 0x444dfc | FUN_00444dfc | 67 | rl_mpmsg3.c |
| 0x444e3f | FUN_00444e3f | 343 | rl_mpmsg3.c |
| 0x444f96 | FUN_00444f96 | 205 | rl_mpmsg3.c |
| 0x445063 | FUN_00445063 | 361 | rl_mpmsg3.c |
| 0x4451cc | FUN_004451cc | 471 | rl_mpmsg3.c |
| 0x4453a3 | FUN_004453a3 | 100 | rl_mpmsg3.c |
| 0x445407 | FUN_00445407 | 81 | rl_mpmsg3.c |
| 0x445458 | FUN_00445458 | 81 | rl_mpmsg3.c |
| 0x4454a9 | FUN_004454a9 | 62 | rl_mpmsg3.c |
| 0x4454e7 | FUN_004454e7 | 198 | rl_mpmsg3.c |
| 0x4455ad | FUN_004455ad | 129 | rl_mpmsg3.c |
| 0x44562e | FUN_0044562e | 147 | rl_mpmsg3.c |
| 0x4456c1 | FUN_004456c1 | 224 | rl_mpmsg3.c |
| 0x4457a1 | FUN_004457a1 | 176 | rl_mpmsg3.c |
| 0x445851 | FUN_00445851 | 148 | rl_mpmsg3.c |
| 0x4458e5 | FUN_004458e5 | 123 | rl_mpmsg2.c |
| 0x445960 | FUN_00445960 | 825 | rl_mpmsg2.c |
| 0x445c99 | FUN_00445c99 | 138 | rl_mpmsg2.c |
| 0x445d23 | FUN_00445d23 | 116 | rl_mpmsg2.c |
| 0x445d97 | FUN_00445d97 | 119 | rl_mpmsg3.c |
| 0x445e0e | FUN_00445e0e | 97 | rl_mpmsg3.c |
| 0x445e6f | FUN_00445e6f | 56 | rl_mpmsg2.c |
| 0x445ea7 | FUN_00445ea7 | 156 | rl_mpmsg2.c |
| 0x445f43 | FUN_00445f43 | 61 | rl_mpmsg2.c |
| 0x445f80 | FUN_00445f80 | 190 | rl_mpmsg2.c |
| 0x44603e | FUN_0044603e | 11 | rl_mpmsg3.c |
| 0x446049 | FUN_00446049 | 11 | rl_mpmsg3.c |
| 0x446054 | FUN_00446054 | 11 | rl_mpmsg3.c |
| 0x44605f | FUN_0044605f | 11 | rl_mpmsg3.c |
| 0x44606a | FUN_0044606a | 55 | rl_mpmsg3.c |
| 0x4460a1 | FUN_004460a1 | 55 | rl_mpmsg3.c |
| 0x4460d8 | FUN_004460d8 | 11 | rl_mpmsg3.c |
| 0x4460e3 | FUN_004460e3 | 21 | rl_mpmsg3.c |
| 0x4460f8 | FUN_004460f8 | 121 | rl_mpmsg3.c |
| 0x446171 | FUN_00446171 | 131 | rl_mpmsg3.c |
| 0x4461f4 | FUN_004461f4 | 56 | rl_mpmsg3.c |
| 0x44622c | FUN_0044622c | 153 | rl_mpmsg3.c |
| 0x4462c5 | FUN_004462c5 | 26 | rl_mpmsg2.c |
| 0x4462df | FUN_004462df | 130 | rl_mpmsg2.c |
| 0x44637e | FUN_0044637e | 11 | rl_mpmsg3.c |
| 0x446389 | FUN_00446389 | 80 | rl_mpmsg3.c |
| 0x4463d9 | FUN_004463d9 | 267 | rl_mpmsg3.c |
| 0x4464e4 | FUN_004464e4 | 415 | rl_mpmsg3.c |
| 0x446683 | FUN_00446683 | 266 | rl_mpmsg3.c |
| 0x44678d | FUN_0044678d | 281 | rl_mpmsg3.c |
| 0x4468a6 | FUN_004468a6 | 16 | rl_mpmsg3.c |
| 0x4468b6 | FUN_004468b6 | 16 | rl_mpmsg3.c |
| 0x4468c6 | FUN_004468c6 | 45 | rl_mpmsg3.c |
| 0x4468f3 | FUN_004468f3 | 55 | rl_mpmsg3.c |
| 0x44692a | FUN_0044692a | 11 | rl_mpmsg3.c |
| 0x446935 | FUN_00446935 | 42 | rl_mpmsg3.c |
| 0x44695f | FUN_0044695f | 36 | rl_mpmsg3.c |
| 0x446983 | FUN_00446983 | 36 | rl_mpmsg3.c |
| 0x4469a7 | FUN_004469a7 | 11 | rl_mpmsg3.c |
| 0x4469b2 | FUN_004469b2 | 11 | rl_mpmsg3.c |
| 0x4469bd | FUN_004469bd | 47 | rl_mpmsg3.c |
| 0x4469ec | FUN_004469ec | 204 | rl_mpmsg3.c |
| 0x446ab8 | FUN_00446ab8 | 87 | rl_mpmsg3.c |
| 0x446b0f | FUN_00446b0f | 145 | rl_mpmsg3.c |
| 0x446ba0 | FUN_00446ba0 | 26 | rl_mpmsg3.c |
| 0x446bba | FUN_00446bba | 50 | rl_mpmsg3.c |
| 0x446bec | FUN_00446bec | 51 | rl_mpmsg3.c |
| 0x446c1f | FUN_00446c1f | 66 | rl_mpmsg3.c |
| 0x446c61 | FUN_00446c61 | 237 | rl_mpmsg3.c |
| 0x446d4e | FUN_00446d4e | 211 | rl_mpmsg3.c |
| 0x446e21 | FUN_00446e21 | 229 | rl_mpmsg2.c |
| 0x446f06 | FUN_00446f06 | 126 | rl_host.c |
| 0x446f84 | FUN_00446f84 | 162 | rl_mpmsg2.c |
| 0x447026 | FUN_00447026 | 158 | rl_mpmsg2.c |
| 0x4470c4 | FUN_004470c4 | 114 | rl_mpmsg3.c |
| 0x447136 | FUN_00447136 | 408 | rl_mpmsg2.c |
| 0x4476e4 | FUN_004476e4 | 92 | rl_mpmsg2.c |
| 0x447740 | FUN_00447740 | 92 | rl_mpmsg2.c |
| 0x4477e7 | FUN_004477e7 | 503 | rl_ui3.c |
| 0x4479e9 | FUN_004479e9 | 112 | rl_mpmsg2.c |
| 0x447a59 | FUN_00447a59 | 108 | rl_mpmsg3.c |
| 0x447ac5 | FUN_00447ac5 | 108 | rl_mpmsg3.c |
| 0x447b31 | FUN_00447b31 | 31 | rl_mpmsg3.c |
| 0x447b50 | FUN_00447b50 | 31 | rl_mpmsg3.c |
| 0x447b6f | FUN_00447b6f | 31 | rl_mpmsg3.c |
| 0x447b8e | FUN_00447b8e | 64 | rl_mpmsg2.c |
| 0x447bce | FUN_00447bce | 25 | rl_mpmsg3.c |
| 0x447be7 | FUN_00447be7 | 25 | rl_mpmsg3.c |
| 0x447c00 | FUN_00447c00 | 43 | rl_mpmsg3.c |
| 0x447c2b | FUN_00447c2b | 61 | rl_mpmsg3.c |
| 0x447c68 | FUN_00447c68 | 49 | rl_mpmsg2.c |
| 0x447c99 | FUN_00447c99 | 53 | rl_mpmsg2.c |
| 0x447cce | FUN_00447cce | 31 | rl_mpmsg3.c |
| 0x447ced | FUN_00447ced | 53 | rl_mpmsg3.c |
| 0x447d22 | FUN_00447d22 | 31 | rl_mpmsg2.c |
| 0x447d41 | FUN_00447d41 | 49 | rl_mpmsg3.c |
| 0x447d72 | FUN_00447d72 | 43 | rl_mpmsg3.c |
| 0x447d9d | FUN_00447d9d | 25 | rl_mpmsg3.c |
| 0x447db6 | FUN_00447db6 | 37 | rl_mpmsg2.c |
| 0x447de6 | FUN_00447de6 | 37 | rl_mpmsg3.c |
| 0x447e0b | FUN_00447e0b | 313 | rl_mpmsg3.c |
| 0x447f44 | FUN_00447f44 | 31 | rl_mpmsg3.c |
| 0x447f63 | FUN_00447f63 | 31 | rl_mpmsg3.c |
| 0x447f82 | FUN_00447f82 | 31 | rl_mpmsg3.c |
| 0x447fa1 | FUN_00447fa1 | 41 | rl_mpmsg3.c |
| 0x447fca | FUN_00447fca | 37 | rl_mpmsg3.c |
| 0x447fef | FUN_00447fef | 31 | rl_mpmsg3.c |
| 0x44800e | FUN_0044800e | 25 | rl_mpmsg2.c |
| 0x448027 | FUN_00448027 | 16 | rl_mpmsg3.c |
| 0x448037 | FUN_00448037 | 63 | rl_mpmsg3.c |
| 0x448076 | FUN_00448076 | 193 | rl_mpmsg3.c |
| 0x448137 | FUN_00448137 | 269 | rl_mpmsg3.c |
| 0x448244 | FUN_00448244 | 25 | rl_mpmsg3.c |
| 0x44825d | FUN_0044825d | 25 | rl_mpmsg3.c |
| 0x448276 | FUN_00448276 | 25 | rl_mpmsg3.c |
| 0x44828f | FUN_0044828f | 16 | rl_mpmsg3.c |
| 0x44829f | FUN_0044829f | 31 | rl_mpmsg3.c |
| 0x4482be | FUN_004482be | 43 | rl_mpmsg3.c |
| 0x4482e9 | FUN_004482e9 | 31 | rl_mpmsg3.c |
| 0x448308 | FUN_00448308 | 49 | rl_mpmsg3.c |
| 0x448339 | FUN_00448339 | 152 | rl_mpmsg2.c |
| 0x4483d1 | FUN_004483d1 | 31 | rl_mpmsg3.c |
| 0x448422 | FUN_00448422 | 26 | rl_mpmsg2.c |
| 0x448d7e | FUN_00448d7e | 148 | rl_glue.c |
| 0x44ff4a | FUN_0044ff4a | 182 | rl_ui2.c |
| 0x450000 | FUN_00450000 | 714 | rl_ui2.c |
| 0x450639 | FUN_00450639 | 677 | rl_ui2.c |
| 0x45183a | FUN_0045183a | 102 | engine_labor.c, rl_ctl.c |
| 0x451f70 | FUN_00451f70 | 26 | rl_io.c |
| 0x451f8a | FUN_00451f8a | 66 | rl_glue.c |
| 0x45242b | FUN_0045242b | 11 | rl_misc.c |
| 0x45280b | FUN_0045280b | 11 | rl_io.c |
| 0x452820 | FUN_00452820 | 2907 | engine_map_terrain.c, blit_table_452820.h |
| 0x45337b | FUN_0045337b | 1403 | engine_map_terrain.c, blit_table_45337b.h |
| 0x4538f6 | FUN_004538f6 | 1403 | engine_map_terrain.c, blit_table_4538f6.h |
| 0x453e71 | FUN_00453e71 | 8339 | engine_map_terrain.c, drawb_table_453e71.h |
| 0x455f04 | FUN_00455f04 | 4308 | engine_map_terrain.c, drawb_stripb_455f04.h |
| 0x456fd8 | FUN_00456fd8 | 4305 | engine_map_terrain.c, drawb_stripb_456fd8.h |
| 0x4580a9 | FUN_004580a9 | 4527 | engine_map_terrain.c, drawb_stripb_4580a9.h |
| 0x459258 | FUN_00459258 | 4377 | engine_map_terrain.c, drawb_stripb_459258.h |
| 0x460501 | FUN_00460501 | 668 | engine_map_terrain.c |
| 0x46079d | FUN_0046079d | 321 | engine_map_terrain.c |
| 0x4608de | FUN_004608de | 318 | engine_map_terrain.c |
| 0x460a1c | FUN_00460a1c | 2003 | engine_map_terrain.c |
| 0x4611ef | FUN_004611ef | 1076 | engine_map_terrain.c |
| 0x461623 | FUN_00461623 | 1073 | engine_map_terrain.c |
| 0x461a54 | FUN_00461a54 | 1191 | engine_map_terrain.c |
| 0x461efb | FUN_00461efb | 1145 | engine_map_terrain.c |
| 0x463b42 | FUN_00463b42 | 196 | engine_map_terrain.c, blit_table_463b42.h |
| 0x463c06 | FUN_00463c06 | 92 | engine_map_terrain.c, blit_table_463c06.h |
| 0x463c62 | FUN_00463c62 | 92 | engine_map_terrain.c, blit_table_463c62.h |
| 0x463cbe | FUN_00463cbe | 467 | engine_map_terrain.c, drawb_table_463cbe.h |
| 0x463e91 | FUN_00463e91 | 276 | engine_map_terrain.c, drawb_table_463e91.h |
| 0x463fa5 | FUN_00463fa5 | 273 | engine_map_terrain.c, drawb_table_463fa5.h |
| 0x4640b6 | FUN_004640b6 | 339 | engine_map_terrain.c, drawb_stripb_4640b6.h |
| 0x464209 | FUN_00464209 | 345 | engine_map_terrain.c, drawb_stripb_464209.h |
| 0x4692a7 | FUN_004692a7 | 201 | rl_mpmsg3.c |
| 0x469370 | FUN_00469370 | 188 | rl_mpmsg3.c |
| 0x46a101 | FUN_0046a101 | 165 | rl_io.c |
| 0x46c40c | FUN_0046c40c | 57 | engine_bf_setup.c |
| 0x46c4a3 | FUN_0046c4a3 | 277 | rl_glue.c |
| 0x46c7fc | FUN_0046c7fc | 182 | engine_bf_setup.c |
| 0x46d421 | FUN_0046d421 | 11 | rl_ui2.c |
| 0x46e4c8 | FUN_0046e4c8 | 582 | engine_bf_setup.c |
| 0x46e70e | FUN_0046e70e | 89 | engine_bf_setup.c |
| 0x46e767 | FUN_0046e767 | 531 | engine_bf_strike.c |
| 0x46e97a | FUN_0046e97a | 174 | engine_bf_action.c |
| 0x46ea9a | FUN_0046ea9a | 54 | rl_io.c |
| 0x46eb3f | FUN_0046eb3f | 57 | engine_bf_unit.c |
| 0x46eb78 | FUN_0046eb78 | 54 | engine_bf_strike.c |
| 0x46ebae | FUN_0046ebae | 54 | engine_bf_action.c |
| 0x46ebe4 | FUN_0046ebe4 | 138 | engine_bf_unit.c |
| 0x46ef3c | FUN_0046ef3c | 130 | engine_bf_group.c |
| 0x46efbe | FUN_0046efbe | 235 | engine_bf_strike.c |
| 0x46f0f0 | FUN_0046f0f0 | 78 | engine_bf_setup.c |
| 0x46f13e | FUN_0046f13e | 79 | engine_bf_setup.c |
| 0x46f18d | FUN_0046f18d | 79 | engine_bf_setup.c |
| 0x46f21f | FUN_0046f21f | 72 | rl_io.c |
| 0x46f267 | FUN_0046f267 | 71 | rl_io.c |
| 0x46f2f1 | FUN_0046f2f1 | 220 | rl_mpnet.c |
| 0x46f525 | FUN_0046f525 | 336 | engine_bf_strike.c |
| 0x47095e | FUN_0047095e | 1940 | engine_bf_move.c |
| 0x4710f2 | FUN_004710f2 | 1574 | engine_bf_move.c |
| 0x471718 | FUN_00471718 | 1287 | engine_bf_move.c |
| 0x471da6 | FUN_00471da6 | 415 | engine_bf_action.c |
| 0x471f45 | FUN_00471f45 | 738 | engine_bf_move.c |
| 0x472227 | FUN_00472227 | 363 | engine_bf_move.c |
| 0x472986 | FUN_00472986 | 63 | rl_glue.c |
| 0x472a31 | FUN_00472a31 | 152 | rl_glue.c |
| 0x472e46 | FUN_00472e46 | 595 | engine.c, rl_terr.c |
| 0x47309e | FUN_0047309e | 10915 | engine.c, rl_turn.c |
| 0x475b41 | FUN_00475b41 | 562 | rl_glue.c |
| 0x475d73 | FUN_00475d73 | 148 | rl_glue.c |
| 0x475e07 | FUN_00475e07 | 1665 | rl_mpnet.c |
| 0x476488 | FUN_00476488 | 648 | rl_mpnet2.c |
| 0x476a5d | FUN_00476a5d | 74 | engine.c, rl_turn.c |
| 0x476aa7 | FUN_00476aa7 | 770 | engine.c, rl_turn.c |
| 0x476da9 | FUN_00476da9 | 120 | engine.c, rl_turn.c |
| 0x476e21 | FUN_00476e21 | 116 | engine.c, rl_turn.c |
| 0x47703a | FUN_0047703a | 126 | rl_glue.c |
| 0x4778a0 | FUN_004778a0 | 655 | engine_bf_setup.c |
| 0x477b2f | FUN_00477b2f | 346 | engine_bf_extern.c |
| 0x477c89 | FUN_00477c89 | 371 | engine_bf_setup.c |
| 0x4782c5 | FUN_004782c5 | 340 | rl_glue.c |
| 0x4784ca | FUN_004784ca | 730 | engine_bf_input.c |
| 0x4787a4 | FUN_004787a4 | 290 | engine_bf_setup.c |
| 0x4788c6 | FUN_004788c6 | 140 | rl_render.c |
| 0x478952 | FUN_00478952 | 53 | engine_bf_group.c |
| 0x478987 | FUN_00478987 | 1412 | engine_bf_input.c |
| 0x478f0b | FUN_00478f0b | 392 | engine_bf_input.c |
| 0x479276 | FUN_00479276 | 229 | rl_ui2.c |
| 0x47935b | FUN_0047935b | 280 | rl_ui2.c |
| 0x479473 | FUN_00479473 | 229 | engine_bf_setup.c |
| 0x479558 | FUN_00479558 | 480 | rl_mpmsg3.c |
| 0x479738 | FUN_00479738 | 522 | rl_mpmsg3.c |
| 0x479a71 | FUN_00479a71 | 231 | engine_bf_input.c |
| 0x479b58 | FUN_00479b58 | 415 | engine_bf_input.c |
| 0x479cf7 | FUN_00479cf7 | 277 | engine_bf_input.c |
| 0x479e0c | FUN_00479e0c | 132 | rl_mpnet2.c |
| 0x479e90 | FUN_00479e90 | 2269 | engine_bf_action.c |
| 0x47a76d | FUN_0047a76d | 167 | engine_bf_input.c |
| 0x47a814 | FUN_0047a814 | 655 | engine_bf_action.c |
| 0x47aaa3 | FUN_0047aaa3 | 3599 | engine_bf_setup.c |
| 0x47b8b2 | FUN_0047b8b2 | 3080 | engine_bf_setup.c |
| 0x47c4ba | FUN_0047c4ba | 2567 | engine_bf_setup.c |
| 0x47cec1 | FUN_0047cec1 | 1459 | engine_bf_setup.c |
| 0x47d474 | FUN_0047d474 | 68 | engine_bf_setup.c |
| 0x47d4b8 | FUN_0047d4b8 | 140 | engine_bf_setup.c |
| 0x47d5ec | FUN_0047d5ec | 196 | engine_bf_setup.c |
| 0x47d6b0 | FUN_0047d6b0 | 78 | engine_bf_setup.c |
| 0x47d6fe | FUN_0047d6fe | 158 | engine_bf_setup.c |
| 0x47d79c | FUN_0047d79c | 122 | engine_bf_setup.c |
| 0x47d816 | FUN_0047d816 | 574 | engine_bf_setup.c |
| 0x47da54 | FUN_0047da54 | 584 | engine_bf_strike.c |
| 0x47dc9c | FUN_0047dc9c | 50 | engine_bf_setup.c |
| 0x47dcce | FUN_0047dcce | 184 | engine_bf_setup.c |
| 0x47dd86 | FUN_0047dd86 | 602 | engine_bf_action.c |
| 0x47dfe0 | FUN_0047dfe0 | 508 | engine_bf_strike.c |
| 0x47e1dc | FUN_0047e1dc | 84 | engine_bf_setup.c |
| 0x47e230 | FUN_0047e230 | 51 | engine_bf_setup.c |
| 0x47e263 | FUN_0047e263 | 292 | engine_bf_setup.c |
| 0x47e387 | FUN_0047e387 | 422 | engine_bf_setup.c |
| 0x47e52d | FUN_0047e52d | 315 | engine_bf_setup.c |
| 0x47e668 | FUN_0047e668 | 330 | engine_bf_setup.c |
| 0x47e7b2 | FUN_0047e7b2 | 367 | engine_bf_setup.c |
| 0x47e926 | FUN_0047e926 | 284 | engine_bf_setup.c |
| 0x47ebe8 | FUN_0047ebe8 | 332 | rl_mpmsg3.c |
| 0x47ed9b | FUN_0047ed9b | 590 | engine_bf_setup.c |
| 0x47efee | FUN_0047efee | 1087 | engine_bf_setup.c |
| 0x47f42d | FUN_0047f42d | 71 | engine_bf_setup.c |
| 0x47f474 | FUN_0047f474 | 761 | rl_glue.c |
| 0x47f76d | FUN_0047f76d | 458 | engine_bf_input.c |
| 0x47f937 | FUN_0047f937 | 714 | rl_mpnet.c |
| 0x47fc01 | FUN_0047fc01 | 522 | engine_bf_group.c |
| 0x47fea7 | FUN_0047fea7 | 849 | engine_bf_setup.c |
| 0x4801f8 | FUN_004801f8 | 929 | engine_bf_setup.c |
| 0x480599 | FUN_00480599 | 201 | engine_bf_setup.c |
| 0x480662 | FUN_00480662 | 2252 | engine_bf_setup.c |
| 0x480f2e | FUN_00480f2e | 93 | engine_bf_setup.c |
| 0x480f8b | FUN_00480f8b | 1200 | engine_bf_setup.c |
| 0x48143b | FUN_0048143b | 611 | engine_bf_action.c |
| 0x48169e | FUN_0048169e | 91 | engine_bf_setup.c |
| 0x4816f9 | FUN_004816f9 | 383 | engine_bf_setup.c |
| 0x481878 | FUN_00481878 | 142 | engine_bf_setup.c |
| 0x481906 | FUN_00481906 | 198 | engine_bf_setup.c |
| 0x4819cc | FUN_004819cc | 239 | engine_bf_group.c |
| 0x481abb | FUN_00481abb | 223 | engine_bf_unit.c |
| 0x481b9a | FUN_00481b9a | 1399 | rl_glue.c |
| 0x482111 | FUN_00482111 | 476 | rl_glue.c |
| 0x4822ed | FUN_004822ed | 644 | engine_bf_unit.c |
| 0x4825d1 | FUN_004825d1 | 223 | engine_bf_unit.c |
| 0x4826b0 | FUN_004826b0 | 217 | engine_bf_unit.c |
| 0x482789 | FUN_00482789 | 217 | engine_bf_unit.c |
| 0x482862 | FUN_00482862 | 217 | engine_bf_unit.c |
| 0x48293b | FUN_0048293b | 217 | engine_bf_unit.c |
| 0x482a14 | FUN_00482a14 | 217 | engine_bf_unit.c |
| 0x482aed | FUN_00482aed | 217 | engine_bf_unit.c |
| 0x482bc6 | FUN_00482bc6 | 226 | engine_bf_unit.c |
| 0x482ca8 | FUN_00482ca8 | 226 | engine_bf_unit.c |
| 0x482d8a | FUN_00482d8a | 226 | engine_bf_unit.c |
| 0x482e6c | FUN_00482e6c | 282 | engine_bf_unit.c |
| 0x482f86 | FUN_00482f86 | 11 | engine_bf_action.c |
| 0x482f91 | FUN_00482f91 | 344 | engine_bf_action.c |
| 0x4830e9 | FUN_004830e9 | 101 | engine_bf_action.c |
| 0x48314e | FUN_0048314e | 138 | engine_bf_action.c |
| 0x4831d8 | FUN_004831d8 | 274 | engine_bf_action.c |
| 0x4832ea | FUN_004832ea | 77 | engine_bf_action.c |
| 0x483337 | FUN_00483337 | 1873 | engine_bf_strike.c |
| 0x483a88 | FUN_00483a88 | 601 | engine_bf_action.c |
| 0x483ce1 | FUN_00483ce1 | 372 | engine_bf_action.c |
| 0x483e55 | FUN_00483e55 | 396 | engine_bf_action.c |
| 0x483fe1 | FUN_00483fe1 | 741 | engine_bf_action.c |
| 0x4842c6 | FUN_004842c6 | 184 | engine_bf_action.c |
| 0x48437e | FUN_0048437e | 62 | engine_bf_action.c |
| 0x4843bc | FUN_004843bc | 1595 | engine_bf_action.c |
| 0x4849fc | FUN_004849fc | 16 | engine_bf_action.c |
| 0x484a0c | FUN_00484a0c | 381 | engine_bf_action.c |
| 0x484b89 | FUN_00484b89 | 101 | engine_bf_action.c |
| 0x484bee | FUN_00484bee | 11 | engine_bf_action.c |
| 0x484bf9 | FUN_00484bf9 | 2340 | engine_bf_action.c |
| 0x48551d | FUN_0048551d | 344 | engine_bf_strike.c |
| 0x485675 | FUN_00485675 | 492 | engine_bf_strike.c |
| 0x485861 | FUN_00485861 | 388 | engine_bf_strike.c |
| 0x4859e5 | FUN_004859e5 | 354 | engine_bf_group.c |
| 0x485b47 | FUN_00485b47 | 106 | engine_bf_strike.c |
| 0x485bb1 | FUN_00485bb1 | 1592 | engine_bf_group.c |
| 0x4861e9 | FUN_004861e9 | 16 | engine_bf_unit.c |
| 0x4861f9 | FUN_004861f9 | 16 | engine_bf_unit.c |
| 0x486209 | FUN_00486209 | 16 | engine_bf_unit.c |
| 0x486219 | FUN_00486219 | 16 | engine_bf_unit.c |
| 0x486229 | FUN_00486229 | 16 | engine_bf_unit.c |
| 0x486239 | FUN_00486239 | 16 | engine_bf_unit.c |
| 0x486249 | FUN_00486249 | 1411 | engine_bf_unit.c |
| 0x486d83 | FUN_00486d83 | 684 | engine_bf_unit.c |
| 0x4872ae | FUN_004872ae | 841 | engine_bf_unit.c |
| 0x487908 | FUN_00487908 | 494 | engine_bf_unit.c |
| 0x487ce4 | FUN_00487ce4 | 456 | engine_bf_unit.c |
| 0x48804a | FUN_0048804a | 502 | engine_bf_unit.c |
| 0x488436 | FUN_00488436 | 861 | engine_bf_unit.c |
| 0x488793 | FUN_00488793 | 459 | engine_bf_unit.c |
| 0x48895e | FUN_0048895e | 763 | engine_bf_unit.c |
| 0x488dfe | FUN_00488dfe | 943 | engine_bf_group.c |
| 0x4891ad | FUN_004891ad | 596 | engine_bf_group.c |
| 0x489401 | FUN_00489401 | 595 | engine_bf_group.c |
| 0x489654 | FUN_00489654 | 186 | engine_bf_group.c |
| 0x48970e | FUN_0048970e | 138 | engine_bf_group.c |
| 0x489798 | FUN_00489798 | 254 | engine_bf_group.c |
| 0x489896 | FUN_00489896 | 235 | engine_bf_group.c |
| 0x489981 | FUN_00489981 | 225 | engine_bf_group.c |
| 0x489a62 | FUN_00489a62 | 299 | engine_bf_group.c |
| 0x489b8d | FUN_00489b8d | 1035 | engine_bf_group.c |
| 0x489f9d | FUN_00489f9d | 556 | engine_bf_group.c |
| 0x48a1c9 | FUN_0048a1c9 | 453 | engine_bf_group.c |
| 0x48a38e | FUN_0048a38e | 735 | engine_bf_group.c |
| 0x48a672 | FUN_0048a672 | 359 | engine_bf_action.c |
| 0x48a7d9 | FUN_0048a7d9 | 494 | engine_bf_action.c |
| 0x48a9c7 | FUN_0048a9c7 | 11 | engine_bf_group.c |
| 0x48a9d2 | FUN_0048a9d2 | 768 | engine_bf_group.c |
| 0x48acd2 | FUN_0048acd2 | 857 | engine_bf_group.c |
| 0x48b02b | FUN_0048b02b | 848 | engine_bf_group.c |
| 0x48b37b | FUN_0048b37b | 11 | engine_bf_group.c |
| 0x48b386 | FUN_0048b386 | 182 | engine_bf_group.c |
| 0x48b546 | FUN_0048b546 | 182 | engine_bf_group.c |
| 0x48b5fc | FUN_0048b5fc | 512 | engine_bf_group.c |
| 0x48b7fc | FUN_0048b7fc | 453 | engine_bf_group.c |
| 0x48b9c1 | FUN_0048b9c1 | 259 | engine_bf_group.c |
| 0x48bac4 | FUN_0048bac4 | 286 | engine_bf_group.c |
| 0x48bbe2 | FUN_0048bbe2 | 281 | engine_bf_group.c |
| 0x48bcfb | FUN_0048bcfb | 466 | engine_bf_group.c |
| 0x48becd | FUN_0048becd | 300 | engine_bf_group.c |
| 0x48bff9 | FUN_0048bff9 | 118 | engine_bf_group.c |
| 0x48c06f | FUN_0048c06f | 330 | engine_bf_group.c |
| 0x48c363 | FUN_0048c363 | 155 | engine_bf_group.c |
| 0x48c3fe | FUN_0048c3fe | 191 | engine_bf_group.c |
| 0x48c4bd | FUN_0048c4bd | 178 | engine_bf_group.c |
| 0x48c56f | FUN_0048c56f | 832 | engine_bf_group.c |
| 0x48c8af | FUN_0048c8af | 197 | engine_bf_group.c |
| 0x48c974 | FUN_0048c974 | 283 | engine_bf_group.c |
| 0x48cba9 | FUN_0048cba9 | 314 | engine_bf_stance.c |
| 0x48cce3 | FUN_0048cce3 | 314 | engine_bf_stance.c |
| 0x48ce1d | FUN_0048ce1d | 100 | engine_bf_stance.c |
| 0x48ce81 | FUN_0048ce81 | 402 | engine_bf_stance.c |
| 0x48d013 | FUN_0048d013 | 347 | engine_bf_stance.c |
| 0x48d16e | FUN_0048d16e | 676 | engine_bf_stance.c |
| 0x48d412 | FUN_0048d412 | 746 | engine_bf_stance.c |
| 0x48d6fc | FUN_0048d6fc | 722 | engine_bf_stance.c |
| 0x48d9ce | FUN_0048d9ce | 438 | engine_bf_stance.c |
| 0x48db84 | FUN_0048db84 | 574 | engine_bf_stance.c |
| 0x48ddc7 | FUN_0048ddc7 | 500 | engine_bf_stance.c |
| 0x48dfbb | FUN_0048dfbb | 220 | engine_bf_stance.c |
| 0x48e097 | FUN_0048e097 | 413 | engine_bf_stance.c |
| 0x48e234 | FUN_0048e234 | 148 | engine_bf_stance.c |
| 0x48e2c8 | FUN_0048e2c8 | 212 | engine_bf_stance.c |
| 0x48e39c | FUN_0048e39c | 304 | engine_bf_stance.c |
| 0x48e4cc | FUN_0048e4cc | 680 | engine_bf_stance.c |
| 0x48e774 | FUN_0048e774 | 324 | engine_bf_stance.c |
| 0x48e8b8 | FUN_0048e8b8 | 477 | engine_bf_stance.c |
| 0x48ea95 | FUN_0048ea95 | 348 | engine_bf_stance.c |
| 0x48ebf1 | FUN_0048ebf1 | 184 | engine_bf_stance.c |
| 0x48ed95 | FUN_0048ed95 | 177 | engine_bf_stance.c |
| 0x48eefd | FUN_0048eefd | 72 | engine_bf_setup.c |
| 0x48ef45 | FUN_0048ef45 | 308 | engine_bf_group.c |
| 0x48f079 | FUN_0048f079 | 356 | engine_bf_move.c |
| 0x48f1dd | FUN_0048f1dd | 3704 | engine_bf_move.c |
| 0x49005f | FUN_0049005f | 1165 | engine_bf_move.c |
| 0x4904ec | FUN_004904ec | 298 | engine_bf_move.c |
| 0x490616 | FUN_00490616 | 1033 | engine_bf_move.c |
| 0x490a44 | FUN_00490a44 | 528 | engine_bf_move.c |
| 0x490c59 | FUN_00490c59 | 804 | engine_bf_move.c |
| 0x490fa2 | FUN_00490fa2 | 411 | engine_bf_move.c |
| 0x491147 | FUN_00491147 | 411 | engine_bf_move.c |
| 0x4912ec | FUN_004912ec | 422 | engine_bf_move.c |
| 0x491492 | FUN_00491492 | 838 | engine_bf_move.c |
| 0x491a34 | FUN_00491a34 | 235 | engine_bf_move.c |
| 0x491b1f | FUN_00491b1f | 969 | engine_bf_move.c |
| 0x491f17 | FUN_00491f17 | 359 | engine_bf_move.c |
| 0x49207e | FUN_0049207e | 359 | engine_bf_move.c |
| 0x4926fb | FUN_004926fb | 255 | engine_bf_action.c |
| 0x4927fa | FUN_004927fa | 169 | engine_bf_action.c |
| 0x4928a3 | FUN_004928a3 | 19 | engine_bf_move.c |
| 0x492c8b | FUN_00492c8b | 3793 | engine_bf_strike.c |
| 0x493b61 | FUN_00493b61 | 344 | engine_bf_strike.c |
| 0x493cb9 | FUN_00493cb9 | 956 | engine_bf_strike.c |
| 0x494075 | FUN_00494075 | 176 | engine_bf_strike.c |
| 0x494125 | FUN_00494125 | 160 | engine_bf_strike.c |
| 0x4941c5 | FUN_004941c5 | 160 | engine_bf_strike.c |
| 0x494265 | FUN_00494265 | 821 | engine_bf_strike.c |
| 0x49459a | FUN_0049459a | 878 | engine_bf_unit.c |
| 0x494908 | FUN_00494908 | 1024 | engine_bf_action.c |
| 0x494d08 | FUN_00494d08 | 241 | engine_bf_move.c |
| 0x494df9 | FUN_00494df9 | 70 | engine_bf_move.c |
| 0x494e3f | FUN_00494e3f | 1694 | engine_bf_action.c |
| 0x4954dd | FUN_004954dd | 495 | engine_bf_action.c |
| 0x4956cc | FUN_004956cc | 650 | engine_bf_strike.c |
| 0x495956 | FUN_00495956 | 747 | engine_bf_stance.c |
| 0x495c41 | FUN_00495c41 | 739 | engine_bf_stance.c |
| 0x495f3d | FUN_00495f3d | 536 | engine_bf_stance.c |
| 0x496155 | FUN_00496155 | 374 | engine_bf_move.c |
| 0x496566 | FUN_00496566 | 514 | engine_bf_stance.c |
| 0x496768 | FUN_00496768 | 487 | engine_bf_action.c |
| 0x49694f | FUN_0049694f | 592 | engine_bf_action.c |
| 0x496b9f | FUN_00496b9f | 471 | engine_bf_stance.c |
| 0x496d76 | FUN_00496d76 | 508 | engine_bf_stance.c |
| 0x496f72 | FUN_00496f72 | 178 | engine_bf_strike.c |
| 0x497024 | FUN_00497024 | 378 | engine_bf_setup.c |
| 0x49719e | FUN_0049719e | 169 | engine_bf_setup.c |
| 0x497247 | FUN_00497247 | 178 | engine_bf_setup.c |
| 0x4972f9 | FUN_004972f9 | 318 | engine_bf_move.c |
| 0x497437 | FUN_00497437 | 191 | engine_bf_action.c |
| 0x497500 | FUN_00497500 | 211 | rl_io.c |
| 0x4975d3 | FUN_004975d3 | 170 | rl_io.c |
| 0x49767d | FUN_0049767d | 36 | rl_quit.c |
| 0x497879 | FUN_00497879 | 52 | rl_misc.c |
| 0x4978ad | FUN_004978ad | 129 | rl_io.c |
| 0x497a34 | FUN_00497a34 | 697 | rl_io.c |
| 0x4983b7 | FUN_004983b7 | 161 | rl_io.c |
| 0x498458 | FUN_00498458 | 132 | rl_glue.c |
| 0x4987b7 | FUN_004987b7 | 1536 | engine_bf_extern.c |
| 0x498dcb | FUN_00498dcb | 488 | rl_ui2.c |
| 0x498fb3 | FUN_00498fb3 | 129 | rl_ui2.c |
| 0x4990f9 | FUN_004990f9 | 101 | rl_io.c |
| 0x49915e | FUN_0049915e | 259 | rl_io.c |
| 0x499261 | FUN_00499261 | 261 | rl_io.c |
| 0x499366 | FUN_00499366 | 691 | rl_io.c |
| 0x499619 | FUN_00499619 | 51 | rl_io.c |
| 0x49964c | FUN_0049964c | 124 | rl_io.c |
| 0x4996c8 | FUN_004996c8 | 244 | rl_io.c |
| 0x499859 | FUN_00499859 | 451 | gfx.c, rl_turn.c |
| 0x499aca | FUN_00499aca | 487 | rl_io.c |
| 0x499cb1 | FUN_00499cb1 | 204 | engine_bf_input.c |
| 0x499d7d | FUN_00499d7d | 26 | rl_io.c |
| 0x499d97 | FUN_00499d97 | 26 | engine_bf_extern.c |
| 0x49a3e6 | FUN_0049a3e6 | 109 | rl_misc.c |
| 0x49b8b0 | FUN_0049b8b0 | 195 | rl_glue.c |
| 0x49b973 | FUN_0049b973 | 374 | rl_mpnet.c |
| 0x49bb9d | FUN_0049bb9d | 75 | rl_ui4.c |
| 0x49c995 | FUN_0049c995 | 157 | rl_io.c |
| 0x49ca60 | FUN_0049ca60 | 74 | rl_mpnet.c |
| 0x4a82b9 | FUN_004a82b9 | 365 | rl_glue.c |
| 0x4aabd8 | FUN_004aabd8 | 303 | engine_armory.c, engine_armory.h, rl_host.c |
| 0x4adcf6 | FUN_004adcf6 | 413 | rl_ui2.c |
| 0x4ae15f | FUN_004ae15f | 89 | rl_quit.c |
| 0x4ae1b8 | FUN_004ae1b8 | 243 | rl_io.c |
| 0x4ae2ab | FUN_004ae2ab | 18 | rl_io.c |
| 0x4ae2bd | FUN_004ae2bd | 83 | rl_io.c |
| 0x4ae310 | FUN_004ae310 | 553 | engine.c, rl_host.c |
| 0x4aecf0 | FUN_004aecf0 | 334 | rl_io.c |
| 0x4aee3e | FUN_004aee3e | 320 | rl_io.c |
| 0x4af116 | FUN_004af116 | 508 | rl_io.c |
| 0x4af312 | FUN_004af312 | 496 | rl_io.c |
| 0x4af70c | FUN_004af70c | 91 | rl_ui2.c |
| 0x4af767 | FUN_004af767 | 218 | rl_io.c |
| 0x4af841 | FUN_004af841 | 152 | rl_io.c |
| 0x4afe3a | FUN_004afe3a | 179 | rl_io.c |
| 0x4b049b | FUN_004b049b | 103 | rl_io.c |
| 0x4b0aaa | FUN_004b0aaa | 11 | rl_io.c |
| 0x4b0ab5 | FUN_004b0ab5 | 212 | engine.c, engine_armory.c, engine_armory.h (+6) |
| 0x4b0cb4 | FUN_004b0cb4 | 314 | rl_misc.c |
| 0x4b0e03 | FUN_004b0e03 | 875 | rl_misc.c |
| 0x4b11ce | FUN_004b11ce | 34 | rl_io.c |
| 0x4b12fb | FUN_004b12fb | 21 | rl_misc.c |
| 0x4b1310 | FUN_004b1310 | 221 | rl_glue.c |
| 0x4b14ff | FUN_004b14ff | 872 | rl_ui2.c |
| 0x4b1867 | FUN_004b1867 | 48 | engine_screen_strat.c, engine_screen_strat.h, rl_host.c |
| 0x4b1897 | FUN_004b1897 | 11 | rl_misc.c |
| 0x4b18e3 | FUN_004b18e3 | 21 | engine.c, rl_host.c |
| 0x4b18f8 | FUN_004b18f8 | 38 | rl_misc.c |
| 0x4b1cf3 | FUN_004b1cf3 | 226 | rl_misc.c |
| 0x4b1dd5 | FUN_004b1dd5 | 11 | rl_misc.c |
| 0x4b209a | FUN_004b209a | 83 | rl_misc.c |
| 0x4b2234 | FUN_004b2234 | 36 | rl_ui4.c |
| 0x4b2575 | FUN_004b2575 | 59 | rl_io.c |
| 0x4b25b0 | FUN_004b25b0 | 76 | gfx.c, gfx.h, rl_host.c |
| 0x4b25fc | FUN_004b25fc | 58 | rl_misc.c |
| 0x4b2636 | FUN_004b2636 | 130 | rl_quit.c |
| 0x4b26b8 | FUN_004b26b8 | 64 | rl_ui4.c |
| 0x4b26f8 | FUN_004b26f8 | 56 | rl_misc.c |
| 0x4b284e | FUN_004b284e | 122 | gfx.h, rl_host.c |
| 0x4b28cd | FUN_004b28cd | 85 | rl_misc.c |
| 0x4b3571 | FUN_004b3571 | 80 | rl_misc.c |
| 0x4b35c1 | FUN_004b35c1 | 255 | engine.c, rl_host.c |
| 0x4b36c0 | FUN_004b36c0 | 84 | rl_io.c |
| 0x4b3714 | FUN_004b3714 | 84 | rl_io.c |
| 0x4b37bc | FUN_004b37bc | 388 | rl_io.c |
| 0x4b3940 | FUN_004b3940 | 84 | rl_io.c |
| 0x4b3994 | FUN_004b3994 | 84 | rl_ui2.c |
| 0x4b3acd | FUN_004b3acd | 197 | rl_io.c |
| 0x4b3b92 | FUN_004b3b92 | 89 | rl_io.c |
| 0x4b3d5d | FUN_004b3d5d | 112 | rl_mpnet.c |
| 0x4b3dcd | FUN_004b3dcd | 132 | engine_bf_move.c |
| 0x4b61b0 | FUN_004b61b0 | 314 | rl_mpnet.c |
| 0x4b62ea | FUN_004b62ea | 225 | rl_mpnet.c |
| 0x4b63d0 | FUN_004b63d0 | 206 | rl_mpnet.c |
| 0x4b64a3 | FUN_004b64a3 | 53 | rl_mpnet2.c |
| 0x4b64d8 | FUN_004b64d8 | 626 | rl_mpnet.c |
| 0x4b6754 | FUN_004b6754 | 700 | rl_mpnet.c |
| 0x4b6a15 | FUN_004b6a15 | 161 | rl_mpnet2.c |
| 0x4b6ab6 | FUN_004b6ab6 | 47 | rl_mpnet2.c |
| 0x4b6ae5 | FUN_004b6ae5 | 140 | rl_mpnet.c |
| 0x4b6b71 | FUN_004b6b71 | 227 | rl_mpnet.c |
| 0x4b6c59 | FUN_004b6c59 | 41 | rl_mpnet.c |
| 0x4b6c82 | FUN_004b6c82 | 45 | rl_mpnet.c |
| 0x4b6ccb | FUN_004b6ccb | 35 | rl_mpnet.c |
| 0x4b6cee | FUN_004b6cee | 43 | rl_mpnet.c |
| 0x4b6d19 | FUN_004b6d19 | 43 | rl_mpnet.c |
| 0x4b6d44 | FUN_004b6d44 | 129 | rl_mpnet.c |
| 0x4b6dc5 | FUN_004b6dc5 | 33 | rl_mpnet.c |
| 0x4b6de6 | FUN_004b6de6 | 48 | rl_mpnet.c |
| 0x4b6e1b | FUN_004b6e1b | 28 | rl_mpnet.c |
| 0x4b6e8e | FUN_004b6e8e | 105 | rl_mpnet.c |
| 0x4b6ef7 | FUN_004b6ef7 | 320 | rl_mpnet2.c |
| 0x4b7052 | FUN_004b7052 | 288 | rl_mpnet.c |
| 0x4b7180 | FUN_004b7180 | 208 | rl_quit.c |
| 0x4b7250 | FUN_004b7250 | 491 | rl_ui2.c |
| 0x4b743b | FUN_004b743b | 147 | rl_mpnet2.c |
| 0x4b74ce | FUN_004b74ce | 70 | rl_ui4.c |
| 0x4b7514 | FUN_004b7514 | 87 | rl_mpnet.c |
| 0x4b756b | FUN_004b756b | 26 | rl_mpnet2.c |
| 0x4b7585 | FUN_004b7585 | 1113 | rl_ui2.c |
| 0x4b79e3 | FUN_004b79e3 | 153 | rl_mpnet2.c |
| 0x4b7a7c | FUN_004b7a7c | 30 | rl_mpnet2.c |
| 0x4b7a9a | FUN_004b7a9a | 1207 | rl_mpnet.c |
| 0x4b7f78 | FUN_004b7f78 | 710 | rl_mpnet.c |
| 0x4b8243 | FUN_004b8243 | 124 | rl_mpnet2.c |
| 0x4b82bf | FUN_004b82bf | 118 | rl_mpnet2.c |
| 0x4b8335 | FUN_004b8335 | 250 | rl_mpnet2.c |
| 0x4b8439 | FUN_004b8439 | 372 | rl_mpnet2.c |
| 0x4b85b7 | FUN_004b85b7 | 1483 | rl_mpnet2.c |
| 0x4b8b87 | FUN_004b8b87 | 179 | rl_mpnet2.c |
| 0x4b8c3a | FUN_004b8c3a | 1202 | rl_io.c |
| 0x4b90fb | FUN_004b90fb | 394 | rl_mpnet.c |
| 0x4b92a0 | FUN_004b92a0 | 57 | rl_mpnet2.c |
| 0x4b92e0 | FUN_004b92e0 | 730 | rl_io.c |
| 0x4b95ba | FUN_004b95ba | 152 | rl_io.c |
| 0x4b9652 | FUN_004b9652 | 423 | rl_io.c |
| 0x4b97f9 | FUN_004b97f9 | 86 | rl_io.c |
| 0x4b984f | FUN_004b984f | 361 | rl_io.c |
| 0x4bbe51 | FUN_004bbe51 | 86 | rl_misc.c |
| 0x4bbea7 | FUN_004bbea7 | 110 | rl_misc.c |
| 0x4bbf15 | FUN_004bbf15 | 116 | rl_misc.c |
| 0x4bbf89 | FUN_004bbf89 | 73 | rl_mpnet.c |
| 0x4bbfd2 | FUN_004bbfd2 | 73 | rl_mpnet.c |
| 0x4bf620 | _strlen | 123 | rl_crt.c |
| 0x4bf6a0 | __cinit | 66 | rl_crt.c |
| 0x4bf6f0 | _exit | 27 | rl_crt.c |
| 0x4bf770 | doexit | 251 | rl_crt.c |
| 0x4bf870 | __initterm | 49 | rl_crt.c |
| 0x4bfa00 | _malloc | 40 | rl_crt.c |
| 0x4bfa30 | __malloc_dbg | 46 | rl_crt.c |
| 0x4bfa60 | __nh_malloc | 38 | rl_crt.c |
| 0x4bfa90 | __nh_malloc_dbg | 101 | rl_crt.c |
| 0x4bfb30 | __heap_alloc_dbg | 818 | rl_crt.c |
| 0x4bfea0 | __calloc_dbg | 110 | rl_crt.c |
| 0x4c0580 | FUN_004c0580 | 25 | rl_io.c |
| 0x4c05a0 | __free_dbg | 1057 | rl_io.c |
| 0x4c09f0 | __msize_dbg | 358 | rl_crt.c |
| 0x4c0c60 | _CheckBytes | 140 | rl_crt.c |
| 0x4c1070 | __CrtSetDbgFlag | 48 | rl_crt.c |
| 0x4c1110 | __CrtIsValidPointer | 92 | rl_crt.c |
| 0x4c1170 | __CrtIsValidHeapPointer | 182 | rl_crt.c |
| 0x4c1b10 | __getcwd | 43 | rl_crt.c |
| 0x4c1b40 | __getdcwd | 312 | rl_crt.c |
| 0x4c1cf0 | __getdrive | 98 | rl_crt.c |
| 0x4c2080 | FUN_004c2080 | 7 | rl_crt.c |
| 0x4c2170 | _strcmp | 129 | rl_crt.c |
| 0x4c2200 | _memset | 88 | rl_crt.c |
| 0x4c2260 | FID_conflict:__wrename | 96 | rl_crt.c |
| 0x4c22c0 | FID_conflict:_remove | 92 | rl_crt.c |
| 0x4c2340 | _asctime | 345 | rl_crt.c |
| 0x4c24a0 | store_dt | 63 | rl_crt.c |
| 0x4c24e0 | _localtime | 597 | rl_crt.c |
| 0x4c2740 | _time | 390 | rl_crt.c |
| 0x4c28d0 | _strncpy | 254 | rl_crt.c |
| 0x4c29d0 | operator_delete | 162 | rl_crt.c |
| 0x4c2a80 | _sprintf | 236 | rl_crt.c |
| 0x4c2fb0 | __splitpath | 578 | rl_crt.c |
| 0x4c3200 | operator_new | 30 | rl_crt.c |
| 0x4c3370 | entry | 494 | rl_crt.c |
| 0x4c3760 | __heap_alloc_base | 99 | rl_crt.c |
| 0x4c4070 | FUN_004c4070 | 21 | rl_crt.c |
| 0x4c4370 | __free_base | 105 | rl_crt.c |
| 0x4c4480 | __heap_init | 93 | rl_crt.c |
| 0x4c45b0 | ___sbh_new_region | 508 | rl_crt.c |
| 0x4c49d0 | ___sbh_find_block | 162 | rl_crt.c |
| 0x4c4a80 | ___sbh_free_block | 136 | rl_crt.c |
| 0x4c4b10 | ___sbh_alloc_block | 1207 | rl_crt.c |
| 0x4c4fe0 | ___sbh_alloc_block_from_page | 763 | rl_crt.c |
| 0x4c57f0 | _toupper | 313 | rl_crt.c |
| 0x4c5930 | __dosmaperr | 177 | rl_crt.c |
| 0x4c5a30 | ___tzset | 35 | rl_crt.c |
| 0x4c5a60 | __tzset | 861 | rl_crt.c |
| 0x4c5dc0 | __isindst | 859 | rl_crt.c |
| 0x4c6130 | cvtdate | 515 | rl_crt.c |
| 0x4c6340 | _gmtime | 487 | rl_crt.c |
| 0x4c6530 | ___loctotime_t | 274 | rl_crt.c |
| 0x4c68f0 | __output | 3177 | rl_crt.c |
| 0x4c7680 | write_char | 117 | rl_crt.c |
| 0x4c7700 | write_multi_char | 75 | rl_crt.c |
| 0x4c7750 | write_string | 87 | rl_crt.c |
| 0x4c77b0 | get_int_arg | 30 | rl_crt.c |
| 0x4c8870 | __mbsnbcpy | 277 | rl_crt.c |
| 0x4c8990 | __setmbcp | 815 | rl_crt.c |
| 0x4c8cc0 | getSystemCP | 121 | rl_crt.c |
| 0x4c8e90 | ___initmbctable | 21 | rl_crt.c |
| 0x4c9260 | __ismbblead | 32 | rl_crt.c |
| 0x4c92f0 | x_ismbbtype | 110 | rl_crt.c |
| 0x4c9360 | __setenvp | 318 | rl_crt.c |
| 0x4c94a0 | __setargv | 204 | rl_crt.c |
| 0x4c9570 | parse_cmdline | 958 | rl_crt.c |
| 0x4c9bf0 | ___crtGetEnvironmentStringsA | 602 | rl_crt.c |
| 0x4c9e50 | __ioinit | 808 | rl_crt.c |
| 0x4cbfc0 | _getenv | 227 | rl_crt.c |
| 0x4cc0b0 | __write | 744 | rl_crt.c |
| 0x4cc3b0 | __lseek | 285 | rl_crt.c |
| 0x4cc5e0 | ___initstdio | 338 | rl_crt.c |
| 0x4cc740 | ___endstdio | 36 | rl_crt.c |
| 0x4cc930 | $E2 | 21 | rl_crt.c |
| 0x4cc950 | $E1 | 45 | rl_crt.c |
| 0x4cca20 | FUN_004cca20 | 28 | rl_crt.c |
| 0x4cccc0 | __alloc_osfhnd | 333 | rl_crt.c |
| 0x4cce10 | __set_osfhnd | 234 | rl_crt.c |
| 0x4ccf10 | __free_osfhnd | 263 | rl_crt.c |
| 0x4cd030 | __get_osfhandle | 118 | rl_crt.c |
| 0x4cd270 | _fflush | 126 | rl_crt.c |
| 0x4cd2f0 | __flush | 186 | rl_crt.c |
| 0x4cd3b0 | __flushall | 26 | rl_crt.c |
| 0x4cd3d0 | flsall | 247 | rl_crt.c |
| 0x4cd4f0 | __onexit | 181 | rl_crt.c |
| 0x4cd5b0 | _atexit | 48 | rl_crt.c |
| 0x4cd5e0 | ___onexitinit | 85 | rl_crt.c |
| 0x4ce640 | __close | 267 | rl_crt.c |
| 0x4cea00 | __open | 67 | rl_crt.c |
| 0x4cea50 | __sopen | 1355 | rl_crt.c |
| 0x4ceff0 | __read | 1156 | rl_crt.c |
| 0x4cf490 | __filelength | 190 | rl_crt.c |
| 0x4cf550 | __chdir | 226 | rl_crt.c |
| 0x4cf8f0 | __mbctoupper | 188 | rl_crt.c |

