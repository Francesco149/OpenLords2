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
- touched: **1010** (41.3%) — tested 840, ported 170
- code bytes touched: **49.3%** (413,500 / 838,629 B)
- unported: **1437**
- orphan refs (in src/, not in function table): 7

## tested (840) — ported + host unit suite

| VA | name | size | src |
|----|------|-----:|-----|
| 0x401000 | FUN_00401000 | 133 | strutil.h |
| 0x401085 | FUN_00401085 | 85 | strutil.h |
| 0x4010da | FUN_004010da | 92 | strutil.c, strutil.h |
| 0x401136 | FUN_00401136 | 64 | engine_diplomacy_action_model.c, engine_event_queue.c, engine_event_queue.h (+5) |
| 0x401176 | FUN_00401176 | 58 | strutil.h |
| 0x4011b0 | FUN_004011b0 | 58 | strutil.h |
| 0x4011ea | FUN_004011ea | 92 | strutil.h |
| 0x401246 | FUN_00401246 | 92 | strutil.h |
| 0x4012a2 | FUN_004012a2 | 97 | strutil.h |
| 0x401303 | FUN_00401303 | 48 | strutil.h |
| 0x401333 | FUN_00401333 | 49 | strutil.h |
| 0x401364 | FUN_00401364 | 88 | strutil.h |
| 0x4013bc | FUN_004013bc | 89 | strutil.h |
| 0x401415 | FUN_00401415 | 219 | strutil.h |
| 0x4014f0 | FUN_004014f0 | 201 | font_metrics.h, gfx_text.c |
| 0x4015b9 | FUN_004015b9 | 173 | engine_title_render.c, font_metrics.c, font_metrics.h (+3) |
| 0x401666 | FUN_00401666 | 141 | strutil.h |
| 0x4016f3 | FUN_004016f3 | 204 | localization.c, localization.h |
| 0x4017bf | FUN_004017bf | 187 | localization.c, localization.h |
| 0x40187a | FUN_0040187a | 93 | localization.c, localization.h |
| 0x4018d7 | FUN_004018d7 | 173 | engine_hud_menubar.c, localization.c, localization.h |
| 0x401984 | FUN_00401984 | 156 | text_input.c, text_input.h |
| 0x401a20 | FUN_00401a20 | 543 | main.c, text_input.h |
| 0x401c3f | FUN_00401c3f | 28 | text_input.h |
| 0x401c70 | FUN_00401c70 | 35 | main.c, text_input.h |
| 0x401c93 | FUN_00401c93 | 16 | main.c, text_input.h |
| 0x401ca3 | FUN_00401ca3 | 25 | main.c, text_input.h |
| 0x401cbc | FUN_00401cbc | 30 | main.c, text_input.h |
| 0x401cda | FUN_00401cda | 34 | main.c, text_input.h |
| 0x401cfc | FUN_00401cfc | 21 | main.c, text_input.h |
| 0x401d11 | FUN_00401d11 | 21 | main.c, text_input.h |
| 0x401d26 | FUN_00401d26 | 162 | text_input.h |
| 0x401dc8 | FUN_00401dc8 | 64 | main.c, text_input.h |
| 0x401e49 | FUN_00401e49 | 135 | text_input.h |
| 0x401ed0 | FUN_00401ed0 | 121 | text_input.h |
| 0x401f49 | FUN_00401f49 | 70 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x401f8f | FUN_00401f8f | 122 | font_metrics.c, font_metrics.h, text_input.c (+1) |
| 0x402009 | FUN_00402009 | 165 | engine_title_render.c, main.c, text_input.h |
| 0x4020ae | FUN_004020ae | 94 | engine_diplomacy_action_model.c, save.h, text_input.h |
| 0x40210c | FUN_0040210c | 90 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4022bd | FUN_004022bd | 394 | gfx_text_label.c, gfx_text_label.h |
| 0x402447 | FUN_00402447 | 400 | gfx_text_label.c, gfx_text_label.h |
| 0x4025d7 | FUN_004025d7 | 96 | gfx_text.c, gfx_text.h |
| 0x402637 | FUN_00402637 | 989 | engine_county_panels.c, engine_noble_faces.c, engine_title_render.c (+5) |
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
| 0x4036f9 | FUN_004036f9 | 235 | font_metrics.h |
| 0x4037ee | FUN_004037ee | 347 | gfx_line.c, gfx_line.h |
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
| 0x40464c | FUN_0040464c | 303 | engine_battlefield_render.h, gfx_dispatch.c, gfx_dispatch.h (+2) |
| 0x40477b | FUN_0040477b | 291 | engine_battlefield_render.h, gfx_dispatch.c, gfx_dispatch.h (+1) |
| 0x40489e | FUN_0040489e | 111 | engine_tick_cadence.h |
| 0x40490d | FUN_0040490d | 313 | engine_tick_cadence.h |
| 0x404a46 | FUN_00404a46 | 230 | engine.c, engine.h, engine_bf.h (+5) |
| 0x404b2c | FUN_00404b2c | 109 | engine.h, engine_rng.c, engine_territory.c |
| 0x404b99 | FUN_00404b99 | 466 | engine.c, engine.h, engine_rng.c |
| 0x404d6b | FUN_00404d6b | 43 | engine_armory.c, engine_army_ai.c, engine_input.c (+5) |
| 0x404d96 | FUN_00404d96 | 43 | engine_math.h |
| 0x404dc1 | FUN_00404dc1 | 66 | engine_army_state.c, engine_bf_group.c, engine_combat.c (+6) |
| 0x404e03 | FUN_00404e03 | 72 | engine_army_economy.c, engine_math.h |
| 0x404e4b | FUN_00404e4b | 98 | engine_math.h |
| 0x404ead | FUN_00404ead | 159 | engine_army_ai.c, engine_bf_group.c, engine_combat.c (+2) |
| 0x404f4c | FUN_00404f4c | 180 | engine_army_ai.c, engine_path.h, engine_territory.c |
| 0x4050c0 | FUN_004050c0 | 53 | engine.c, engine_army_select.h, engine_county_panels.c (+11) |
| 0x4050f6 | FUN_004050f6 | 376 | engine_map_scene.c, engine_map_scene.h, main.c |
| 0x40526e | FUN_0040526e | 537 | engine_map_scene.c, engine_map_scene.h, engine_map_terrain.c (+1) |
| 0x405487 | FUN_00405487 | 379 | engine_map_render.c, engine_map_render.h, engine_map_scene.c (+3) |
| 0x405602 | FUN_00405602 | 608 | engine_map_scene.c, engine_map_scene.h, engine_overlay_render.c (+1) |
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
| 0x408fb0 | FUN_00408fb0 | 27 | engine_hud_menubar.h, gfx_panel.c, gfx_sprite_render.c (+1) |
| 0x409346 | FUN_00409346 | 81 | gfx_panel.c, gfx_panel.h |
| 0x409397 | FUN_00409397 | 73 | engine_armory.c, engine_county_panels.c, engine_labor.c (+1) |
| 0x4093e0 | FUN_004093e0 | 73 | engine_diplomacy_action.c, engine_noble_faces.c, engine_sendsupplies.c (+4) |
| 0x40946d | FUN_0040946d | 856 | gfx_panel.c, gfx_panel.h |
| 0x4097c5 | FUN_004097c5 | 367 | gfx_panel.c, gfx_panel.h |
| 0x409c93 | FUN_00409c93 | 374 | engine_armory.c, engine_county_panels.c, engine_sendsupplies.c (+1) |
| 0x409e09 | FUN_00409e09 | 368 | engine_hud_menubar.c, engine_hud_menubar.h |
| 0x40a01d | FUN_0040a01d | 261 | engine_minimap.c, engine_sendsupplies.c, gfx_dispatch.h (+2) |
| 0x40a21a | FUN_0040a21a | 433 | engine_county_panels.c, engine_hud_menubar.c, engine_hud_menubar.h (+5) |
| 0x40a3d0 | FUN_0040a3d0 | 402 | engine_armory.c, engine_terrain_info.c, gfx_sprite_render.c (+2) |
| 0x40a567 | FUN_0040a567 | 283 | engine_screen_strat.c, engine_screen_strat.h, gfx_glyph.c (+5) |
| 0x40a682 | FUN_0040a682 | 391 | engine_county_panels.c, engine_labor.c, engine_rankings.h (+4) |
| 0x40a80e | FUN_0040a80e | 413 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a9b0 | FUN_0040a9b0 | 439 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40acce | FUN_0040acce | 324 | engine_title_render.c, gfx_caret.c, gfx_caret.h (+2) |
| 0x40c5b0 | FUN_0040c5b0 | 373 | engine_hud_menubar.c, engine_hud_menubar.h |
| 0x40ccfa | FUN_0040ccfa | 94 | engine.c, engine_confirm_dialog.c, engine_confirm_dialog.h |
| 0x40cfd2 | FUN_0040cfd2 | 490 | engine.c, engine_button.c, engine_button.h (+12) |
| 0x40d1bc | FUN_0040d1bc | 181 | engine_armory.c, engine_diplomacy_action.c, engine_labor.c (+8) |
| 0x40da1e | FUN_0040da1e | 869 | engine.c, engine_confirm_dialog.h, engine_county_panels.h (+4) |
| 0x40deca | FUN_0040deca | 152 | engine_ingame_input.h |
| 0x40e3ee | FUN_0040e3ee | 505 | engine.c, engine_armory.c, engine_armory.h (+5) |
| 0x40e5e7 | FUN_0040e5e7 | 153 | engine_menu_input.c, engine_menu_input.h |
| 0x40e6f2 | FUN_0040e6f2 | 111 | engine.c, engine_army_select.c, engine_army_select.h (+1) |
| 0x40e7e4 | FUN_0040e7e4 | 87 | engine.c, engine_ingame_input.c, engine_ingame_input.h |
| 0x40e890 | FUN_0040e890 | 43 | engine.c, engine.h, main.c |
| 0x40e8bb | FUN_0040e8bb | 240 | engine.c, engine.h, main.c |
| 0x40e9ab | FUN_0040e9ab | 641 | config.h, main.c |
| 0x40ecc4 | FUN_0040ecc4 | 1099 | config.c, config.h, strutil.h |
| 0x40f1a0 | FUN_0040f1a0 | 1117 | engine.c, engine_armory.h, engine_diplomacy_action.h (+7) |
| 0x40f5fd | FUN_0040f5fd | 470 | engine.c, engine_hud_panel.h, engine_map_scene.h (+7) |
| 0x40f7d3 | FUN_0040f7d3 | 1774 | engine_hud_panel.c, engine_hud_panel.h, engine_screen_strat.c (+1) |
| 0x40fec1 | FUN_0040fec1 | 494 | engine_hud_panel.c, engine_hud_panel.h |
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
| 0x413155 | FUN_00413155 | 977 | engine_labor.c, engine_labor.h, engine_screen_strat.c (+1) |
| 0x413590 | FUN_00413590 | 1440 | engine_army_economy.h, engine_labor.c, engine_labor.h |
| 0x41438c | FUN_0041438c | 446 | engine.c, engine_ingame_input.c, engine_ingame_input.h |
| 0x4148e4 | FUN_004148e4 | 264 | engine_title_render.c, engine_title_render.h, main.c |
| 0x4149ec | FUN_004149ec | 1050 | engine_title_render.c, engine_title_render.h, gfx_sprite.c (+1) |
| 0x4156a7 | FUN_004156a7 | 627 | engine_county_panels.c, engine_county_panels.h, gfx.c |
| 0x4180f6 | FUN_004180f6 | 245 | engine_armory.h, engine_ingame_input.c |
| 0x419c78 | FUN_00419c78 | 682 | engine_hud_menubar.c, engine_hud_menubar.h, engine_labor.c (+3) |
| 0x41a734 | FUN_0041a734 | 272 | engine_hud_panel.c, engine_hud_panel.h, engine_screen_strat.c (+1) |
| 0x41a900 | FUN_0041a900 | 615 | engine_county_panels.c, engine_hud_menubar.c, engine_hud_menubar.h |
| 0x41ab67 | FUN_0041ab67 | 215 | engine_armory.c, engine_county_panels.c, engine_diplomacy_action.c (+6) |
| 0x41ac3e | FUN_0041ac3e | 87 | engine_armory.c, engine_hud_panel.c |
| 0x41ac95 | FUN_0041ac95 | 200 | engine_county_panels.h, engine_trade.c |
| 0x41b032 | FUN_0041b032 | 79 | engine.c, engine_army_select.c, engine_terrain_info.c (+1) |
| 0x41b19d | FUN_0041b19d | 3425 | engine_terrain_info.h |
| 0x41befe | FUN_0041befe | 778 | engine_terrain_info.c, engine_terrain_info.h |
| 0x41c208 | FUN_0041c208 | 1934 | engine_terrain_info.c, engine_terrain_info.h, gfx.c |
| 0x41c996 | FUN_0041c996 | 420 | engine_terrain_info.c |
| 0x41cb3a | FUN_0041cb3a | 1887 | engine_terrain_info.c, engine_terrain_info.h |
| 0x41d299 | FUN_0041d299 | 1942 | engine_terrain_info.c, engine_terrain_info.h |
| 0x41da2f | FUN_0041da2f | 1196 | engine_terrain_info.c, engine_terrain_info.h |
| 0x41e61d | FUN_0041e61d | 452 | engine_title_render.c, engine_title_render.h, gfx.c (+2) |
| 0x41e7e1 | FUN_0041e7e1 | 385 | engine_title_render.c, engine_title_render.h, main.c |
| 0x41ea14 | FUN_0041ea14 | 143 | engine_title_render.c, engine_title_render.h |
| 0x41eaa3 | FUN_0041eaa3 | 487 | engine_title_render.c, engine_title_render.h |
| 0x41ec8a | FUN_0041ec8a | 92 | engine_title_render.c, engine_title_render.h |
| 0x41ece6 | FUN_0041ece6 | 604 | engine_title_render.c, engine_title_render.h |
| 0x41ef57 | FUN_0041ef57 | 200 | engine_title_render.c, engine_title_render.h, main.c |
| 0x41f01f | FUN_0041f01f | 315 | engine_title_render.c, engine_title_render.h |
| 0x41f15a | FUN_0041f15a | 131 | engine_mp_render.c, engine_mp_render.h |
| 0x41f1dd | FUN_0041f1dd | 324 | engine_title_render.c, engine_title_render.h |
| 0x41f321 | FUN_0041f321 | 200 | engine_title_render.c, engine_title_render.h, gfx_caret.h (+4) |
| 0x41f592 | FUN_0041f592 | 86 | engine_title_render.c, engine_title_render.h |
| 0x41f5e8 | FUN_0041f5e8 | 223 | engine_title_render.c, engine_title_render.h |
| 0x41f6c7 | FUN_0041f6c7 | 179 | engine_title_render.c, engine_title_render.h |
| 0x41f77a | FUN_0041f77a | 243 | engine_title_render.c |
| 0x41f86d | FUN_0041f86d | 286 | engine_title_render.c, engine_title_render.h |
| 0x41f98b | FUN_0041f98b | 576 | engine_title_render.c, engine_title_render.h |
| 0x41fbcb | FUN_0041fbcb | 523 | engine_title_render.c, engine_title_render.h |
| 0x41fdd6 | FUN_0041fdd6 | 415 | engine_title_render.c, engine_title_render.h, main.c |
| 0x41ff75 | FUN_0041ff75 | 466 | engine_title_render.c |
| 0x42150b | FUN_0042150b | 508 | engine_title_render.h |
| 0x422c12 | FUN_00422c12 | 272 | engine_combat.c, engine_combat.h |
| 0x423241 | FUN_00423241 | 438 | engine_combat.c, engine_combat.h |
| 0x423b4f | FUN_00423b4f | 85 | engine_mp_render.c, engine_mp_render.h |
| 0x425d35 | FUN_00425d35 | 184 | audio.c, main.c |
| 0x426216 | FUN_00426216 | 185 | engine_army_move.c, engine_labor.c, engine_labor.h (+2) |
| 0x426d11 | FUN_00426d11 | 196 | audio.c, engine_combat.c, engine_combat.h |
| 0x427990 | FUN_00427990 | 779 | audio.h, engine.c, engine_combat.c (+8) |
| 0x427d19 | FUN_00427d19 | 89 | audio.c, audio.h, engine.c (+3) |
| 0x427ed0 | FUN_00427ed0 | 311 | engine_territory.c, game_init.c, game_init.h |
| 0x428007 | FUN_00428007 | 113 | engine_army_ai.c, engine_army_state.c, engine_army_state.h (+3) |
| 0x428078 | FUN_00428078 | 113 | engine_army_ai.c, engine_combat.c, game_init.c (+1) |
| 0x4280e9 | FUN_004280e9 | 904 | engine.c, engine_army_state.c, engine_army_state.h (+1) |
| 0x428471 | FUN_00428471 | 11 | engine_turn.c, engine_turn.h |
| 0x42847c | FUN_0042847c | 82 | game_init.c, game_init.h |
| 0x4284ce | FUN_004284ce | 1858 | engine_army_ai.c, engine_army_ai.h, engine_trade_model.c |
| 0x428c42 | FUN_00428c42 | 365 | engine_army_ai.c, engine_army_ai.h |
| 0x4290d0 | FUN_004290d0 | 131 | engine_category_buckets.c, engine_category_buckets.h, engine_territory.c (+1) |
| 0x429153 | FUN_00429153 | 96 | engine_category_buckets.h, engine_territory.c |
| 0x4291b3 | FUN_004291b3 | 171 | engine_category_buckets.h, engine_territory.c |
| 0x42925e | FUN_0042925e | 81 | engine_category_buckets.c, engine_category_buckets.h |
| 0x4292af | FUN_004292af | 361 | engine_army_ai.c, engine_army_ai.h, engine_army_state.c (+3) |
| 0x429418 | FUN_00429418 | 669 | engine.c, engine_army_state.c, engine_army_state.h |
| 0x4296b5 | FUN_004296b5 | 319 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x4298c1 | FUN_004298c1 | 604 | engine_map_init.c, engine_map_init.h, game_init.c (+1) |
| 0x429b1d | FUN_00429b1d | 135 | engine.c, engine_camera.c, engine_camera.h (+1) |
| 0x429ba4 | FUN_00429ba4 | 878 | engine.c, engine_camera.c, engine_camera.h (+3) |
| 0x42a15f | FUN_0042a15f | 206 | engine_camera.c, engine_camera.h |
| 0x42c64f | FUN_0042c64f | 1531 | engine_combat.c, engine_combat.h |
| 0x42d91b | FUN_0042d91b | 83 | engine_combat.c, engine_combat.h |
| 0x42dfe2 | FUN_0042dfe2 | 126 | main.c |
| 0x42e3ba | FUN_0042e3ba | 164 | main.c |
| 0x42ec91 | FUN_0042ec91 | 1207 | gfx.c, gfx.h, gfx_paint.h |
| 0x42f1df | FUN_0042f1df | 295 | gfx.c, main.c |
| 0x42ff10 | FUN_0042ff10 | 8140 | engine.c, engine_armory.c, engine_army_move_confirm.h (+8) |
| 0x431f59 | FUN_00431f59 | 712 | engine_camera.h |
| 0x432221 | FUN_00432221 | 477 | engine.c, engine_camera.h, engine_ingame_input.h |
| 0x4323fe | FUN_004323fe | 64 | engine_ingame_input.h |
| 0x432640 | FUN_00432640 | 262 | engine_ingame_input.c, engine_ingame_input.h |
| 0x432746 | FUN_00432746 | 69 | engine_territory.c, engine_territory.h, game_init.c |
| 0x43278b | FUN_0043278b | 223 | engine.c, engine_terrain_info.c, engine_terrain_info.h (+2) |
| 0x43292d | FUN_0043292d | 58 | engine_ingame_input.h |
| 0x432967 | FUN_00432967 | 61 | engine.c, engine_ingame_input.h |
| 0x4329ec | FUN_004329ec | 281 | engine_load_pump.c, engine_load_pump.h, engine_menu_input.c (+4) |
| 0x432b05 | FUN_00432b05 | 451 | engine_menu_input.c, engine_menu_input.h |
| 0x432cc8 | FUN_00432cc8 | 526 | engine_menu_input.c, main.c |
| 0x432ee6 | FUN_00432ee6 | 197 | engine_menu_input.c |
| 0x432fab | FUN_00432fab | 191 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433155 | FUN_00433155 | 569 | engine_menu_input.c |
| 0x433461 | FUN_00433461 | 208 | engine_menu_input.c |
| 0x4335f0 | FUN_004335f0 | 452 | engine_menu_input.c, main.c |
| 0x4337b4 | FUN_004337b4 | 337 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433905 | FUN_00433905 | 147 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433998 | FUN_00433998 | 139 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x433a23 | FUN_00433a23 | 77 | engine_menu_input.c, main.c |
| 0x433ba2 | FUN_00433ba2 | 287 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433cc1 | FUN_00433cc1 | 35 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433ce4 | FUN_00433ce4 | 217 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x43400b | FUN_0043400b | 361 | engine.c, engine_menu_input.c, engine_menu_input.h (+2) |
| 0x4342f3 | FUN_004342f3 | 21 | engine_menu_input.c, engine_menu_input.h |
| 0x434308 | FUN_00434308 | 62 | engine_menu_input.c |
| 0x434346 | FUN_00434346 | 178 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x4343f8 | FUN_004343f8 | 36 | main.c |
| 0x434e1f | FUN_00434e1f | 37 | engine_confirm_dialog.c, engine_confirm_dialog.h |
| 0x4350a1 | FUN_004350a1 | 232 | engine_ingame_input.h, engine_terrain_info.h |
| 0x4351c4 | FUN_004351c4 | 77 | engine.c, engine_ingame_input.c, engine_rankings.h (+2) |
| 0x43524e | FUN_0043524e | 45 | engine.c, engine_ingame_input.c |
| 0x4357a6 | FUN_004357a6 | 132 | engine.c, engine_ingame_input.c, engine_ingame_input.h (+4) |
| 0x435cef | FUN_00435cef | 456 | engine.c, engine_armory.c, engine_armory.h (+2) |
| 0x436408 | FUN_00436408 | 1015 | engine.c, engine_diplomacy.c, engine_diplomacy.h (+3) |
| 0x436d02 | FUN_00436d02 | 768 | engine_army_ai.c, engine_army_ai.h |
| 0x437002 | FUN_00437002 | 261 | engine_ingame_input.h |
| 0x4371ce | FUN_004371ce | 108 | engine_army_select.h |
| 0x43723a | FUN_0043723a | 245 | engine_army_select.c, engine_army_select.h |
| 0x437535 | FUN_00437535 | 390 | engine_combat.c, engine_combat.h, engine_map_query.c |
| 0x438681 | FUN_00438681 | 699 | engine_army_ai.c, engine_army_ai.h |
| 0x438990 | FUN_00438990 | 257 | engine_ingame_input.h |
| 0x438a91 | FUN_00438a91 | 59 | engine_ingame_input.h |
| 0x438ceb | FUN_00438ceb | 326 | engine.c, engine_county_panels.c, engine_county_panels.h (+1) |
| 0x438e3b | FUN_00438e3b | 574 | engine_ingame_input.h |
| 0x439079 | FUN_00439079 | 169 | engine_ingame_input.h |
| 0x439122 | FUN_00439122 | 537 | engine_ingame_input.h |
| 0x4393eb | FUN_004393eb | 342 | engine_ingame_input.c |
| 0x439df0 | FUN_00439df0 | 235 | engine_ingame_input.c |
| 0x43a123 | FUN_0043a123 | 145 | engine.c, engine_ingame_input.h, engine_labor.c (+1) |
| 0x43a1e9 | FUN_0043a1e9 | 86 | engine_county_panels.h |
| 0x43a23f | FUN_0043a23f | 115 | engine_county_panels.h |
| 0x43a2b2 | FUN_0043a2b2 | 85 | engine_county_panels.h |
| 0x43a307 | FUN_0043a307 | 114 | engine_county_panels.h |
| 0x43a379 | FUN_0043a379 | 560 | engine.c, engine_county_panels.c, engine_county_panels.h (+2) |
| 0x43a5a9 | FUN_0043a5a9 | 647 | engine.c, engine_county_panels.c, engine_county_panels.h |
| 0x43a846 | FUN_0043a846 | 172 | engine_county_panels.c, engine_county_panels.h |
| 0x43a8f2 | FUN_0043a8f2 | 48 | engine.c, engine_county_panels.c, engine_county_panels.h |
| 0x43a922 | FUN_0043a922 | 23 | engine_county_panels.c, engine_county_panels.h |
| 0x43a939 | FUN_0043a939 | 23 | engine_county_panels.c, engine_county_panels.h |
| 0x43aa32 | FUN_0043aa32 | 81 | engine.c, engine_county_panels.h |
| 0x43aa83 | FUN_0043aa83 | 82 | engine_county_panels.h |
| 0x43aad5 | FUN_0043aad5 | 80 | engine_county_panels.h |
| 0x43ab25 | FUN_0043ab25 | 81 | engine_county_panels.h |
| 0x43b412 | FUN_0043b412 | 185 | engine.c, engine_ingame_input.c, engine_ingame_input.h (+2) |
| 0x43b4cb | FUN_0043b4cb | 200 | engine_ingame_input.c |
| 0x43b622 | FUN_0043b622 | 95 | engine_combat.c, engine_combat.h |
| 0x43b917 | FUN_0043b917 | 115 | engine_combat.c, engine_combat.h |
| 0x43b98a | FUN_0043b98a | 23 | engine_combat.c |
| 0x43be65 | FUN_0043be65 | 129 | engine_combat.c, engine_combat.h |
| 0x43caf4 | FUN_0043caf4 | 354 | engine_ingame_input.h, engine_terrain_info.c, engine_terrain_info.h |
| 0x43cc56 | FUN_0043cc56 | 16 | engine_army_select.h, engine_ingame_input.h |
| 0x43ce1a | FUN_0043ce1a | 1263 | engine.c, engine_army_select.c, engine_army_select.h (+3) |
| 0x43d309 | FUN_0043d309 | 675 | engine_army_select.c, engine_army_select.h |
| 0x43de70 | FUN_0043de70 | 1211 | engine.c, engine.h, engine_rate.c |
| 0x43e330 | FUN_0043e330 | 259 | engine.c, engine.h, engine_rate.c (+1) |
| 0x43e433 | FUN_0043e433 | 218 | engine.h, engine_rate.c |
| 0x43e50d | FUN_0043e50d | 180 | engine.h, engine_rate.c |
| 0x43e9e2 | FUN_0043e9e2 | 223 | engine_roster.c, engine_roster.h, main.c |
| 0x43eda0 | FUN_0043eda0 | 637 | engine.c, engine_army_move_confirm.h, engine_army_select.c (+7) |
| 0x43f4b7 | FUN_0043f4b7 | 270 | engine.h, engine_state_hash.c, main.c |
| 0x43fd2a | FUN_0043fd2a | 413 | main.c |
| 0x43ffc3 | FUN_0043ffc3 | 117 | engine_state_hash.c |
| 0x440089 | FUN_00440089 | 81 | engine_battlefield_render.c |
| 0x44015a | FUN_0044015a | 96 | engine.h, engine_state_hash.c |
| 0x440231 | FUN_00440231 | 1077 | engine.h, engine_state_hash.c |
| 0x440dd2 | FUN_00440dd2 | 638 | engine.h, engine_state_hash.c |
| 0x4472ce | FUN_004472ce | 1046 | engine.c, engine_delayed_action.c, engine_delayed_action.h |
| 0x4479de | FUN_004479de | 11 | engine_handlers_da.h |
| 0x447ddb | FUN_00447ddb | 11 | engine_handlers_da.h |
| 0x4483f0 | FUN_004483f0 | 11 | engine_handlers_da.h |
| 0x4483fb | FUN_004483fb | 11 | engine_handlers_da.h |
| 0x448406 | FUN_00448406 | 11 | engine_handlers_da.h |
| 0x448411 | FUN_00448411 | 17 | engine_handlers_da.h |
| 0x448440 | FUN_00448440 | 261 | engine.c, engine.h, engine_army_ai.c (+8) |
| 0x448545 | FUN_00448545 | 96 | engine_combat.c, engine_realm.c, engine_realm.h (+1) |
| 0x4485a5 | FUN_004485a5 | 163 | engine_army_ai.c, engine_army_economy.c, engine_army_economy.h (+5) |
| 0x448648 | FUN_00448648 | 97 | engine_army_select.c, engine_army_select.h |
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
| 0x449d6e | FUN_00449d6e | 354 | engine_realm.c, engine_realm.h |
| 0x449ef3 | FUN_00449ef3 | 1991 | engine_realm.c, engine_realm.h, engine_territory.c |
| 0x44a6ba | FUN_0044a6ba | 903 | engine_realm.c, engine_realm.h |
| 0x44aa41 | FUN_0044aa41 | 1019 | engine_realm.c, engine_realm.h |
| 0x44ae3c | FUN_0044ae3c | 21 | engine_realm.c, engine_realm.h |
| 0x44ae51 | FUN_0044ae51 | 497 | engine_realm.c, engine_realm.h, engine_realm_globals.c |
| 0x44b042 | FUN_0044b042 | 704 | engine_realm.c, engine_realm.h, engine_realm_globals.c |
| 0x44b302 | FUN_0044b302 | 138 | engine_realm.c, engine_realm.h |
| 0x44b391 | FUN_0044b391 | 319 | engine_realm.c, engine_realm.h |
| 0x44b4d0 | FUN_0044b4d0 | 203 | engine_realm.c, engine_realm.h |
| 0x44b59b | FUN_0044b59b | 624 | engine_realm.c, engine_realm.h |
| 0x44b80b | FUN_0044b80b | 399 | engine_army_ai.c, engine_combat.c, engine_realm.c (+1) |
| 0x44b99a | FUN_0044b99a | 155 | engine_realm.c, engine_realm.h |
| 0x44ba35 | FUN_0044ba35 | 181 | engine_realm.c, engine_realm.h |
| 0x44baea | FUN_0044baea | 693 | engine_realm.c, engine_realm.h |
| 0x44bd9f | FUN_0044bd9f | 357 | engine_realm.c, engine_realm.h |
| 0x44bf04 | FUN_0044bf04 | 118 | engine_realm.c, engine_realm.h |
| 0x44bf7a | FUN_0044bf7a | 91 | engine_realm.c, engine_realm.h |
| 0x44bfd5 | FUN_0044bfd5 | 190 | engine_realm.c, engine_realm.h |
| 0x44c093 | FUN_0044c093 | 485 | engine_realm.c, engine_realm.h |
| 0x44c278 | FUN_0044c278 | 707 | engine_army_economy.c, engine_army_economy.h, engine_realm.c (+2) |
| 0x44c53b | FUN_0044c53b | 193 | engine_army_economy.c, engine_army_economy.h, engine_realm.c (+1) |
| 0x44c5fc | FUN_0044c5fc | 200 | engine_army_economy.c, engine_army_economy.h, engine_realm.c (+1) |
| 0x44c6c4 | FUN_0044c6c4 | 182 | engine_territory.c, engine_territory.h |
| 0x44c77a | FUN_0044c77a | 139 | engine_realm_events.c, engine_realm_events.h |
| 0x44c80a | FUN_0044c80a | 159 | engine_realm_events.c, engine_realm_events.h |
| 0x44c8ae | FUN_0044c8ae | 1729 | engine_army_economy.c, engine_army_economy.h |
| 0x44cf6f | FUN_0044cf6f | 114 | engine_army_economy.c, engine_army_economy.h |
| 0x44cfe1 | FUN_0044cfe1 | 377 | engine_army_economy.c, engine_army_economy.h |
| 0x44d15a | FUN_0044d15a | 139 | engine_army_economy.c, engine_army_economy.h |
| 0x44d1e5 | FUN_0044d1e5 | 156 | engine_army_economy.c, engine_army_economy.h |
| 0x44d281 | FUN_0044d281 | 130 | engine_army_economy.c |
| 0x44d303 | FUN_0044d303 | 113 | engine_army_economy.c |
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
| 0x450fb4 | FUN_00450fb4 | 173 | engine_army_ai.c, engine_army_economy.c, engine_army_economy.h |
| 0x451061 | FUN_00451061 | 239 | engine_realm.c, engine_realm.h |
| 0x451150 | FUN_00451150 | 936 | game_init.c, game_init.h |
| 0x4514f8 | FUN_004514f8 | 147 | game_init.c, game_init.h |
| 0x45158b | FUN_0045158b | 147 | engine_army_ai.c, engine_army_ai.h |
| 0x45161e | FUN_0045161e | 428 | engine_workforce_hud.h |
| 0x4517ca | FUN_004517ca | 107 | engine_labor.c, engine_workforce_hud.h |
| 0x4518a5 | FUN_004518a5 | 293 | engine_workforce_hud.h |
| 0x4519ca | FUN_004519ca | 147 | engine_workforce_hud.h |
| 0x451a5d | FUN_00451a5d | 349 | engine_workforce_hud.h |
| 0x451bba | FUN_00451bba | 864 | engine_minimap.c, engine_minimap.h |
| 0x451f1a | FUN_00451f1a | 81 | engine.c, engine.h, engine_anim_cycle.c |
| 0x451f70 | FUN_00451f70 | 26 | gfx.c, main.c |
| 0x451f8a | FUN_00451f8a | 66 | engine_load_pump.c, engine_load_pump.h, game_init.c (+1) |
| 0x451fcc | FUN_00451fcc | 385 | engine.c, engine_camera.h, engine_map_init.c (+2) |
| 0x452160 | FUN_00452160 | 422 | engine.c, engine_armory.c, engine_merchant.c (+4) |
| 0x452306 | FUN_00452306 | 224 | engine_dirty_rect.c, engine_dirty_rect.h, gfx.c (+1) |
| 0x4523e6 | FUN_004523e6 | 36 | engine_dirty_rect.h |
| 0x45240a | FUN_0045240a | 33 | engine.c, engine_armory.c, engine_combat.c (+16) |
| 0x452436 | FUN_00452436 | 83 | engine_dirty_rect.h |
| 0x452489 | FUN_00452489 | 83 | engine_dirty_rect.h |
| 0x4524dc | FUN_004524dc | 83 | engine_dirty_rect.h |
| 0x452582 | FUN_00452582 | 83 | engine_dirty_rect.h |
| 0x4525d5 | FUN_004525d5 | 83 | engine_dirty_rect.h |
| 0x452628 | FUN_00452628 | 56 | engine_dirty_rect.c, engine_dirty_rect.h |
| 0x4526b3 | FUN_004526b3 | 87 | engine_dirty_rect.c, engine_dirty_rect.h |
| 0x45270a | FUN_0045270a | 43 | engine_dirty_rect.h, engine_ingame_input.h, engine_workforce_hud.c (+1) |
| 0x452735 | FUN_00452735 | 53 | engine_dirty_rect.h |
| 0x4527a6 | FUN_004527a6 | 101 | engine.c, engine_armory.c, engine_battlefield_render.h (+10) |
| 0x4650b0 | FUN_004650b0 | 265 | engine.c, engine_army_state.c, engine_army_state.h (+3) |
| 0x4651b9 | FUN_004651b9 | 91 | engine.c, engine_territory.c, engine_territory.h |
| 0x465214 | FUN_00465214 | 11 | engine_army_state.c |
| 0x46521f | FUN_0046521f | 615 | engine_army_state.c, engine_combat.c, engine_combat.h |
| 0x465486 | FUN_00465486 | 412 | engine_army_state.c |
| 0x465622 | FUN_00465622 | 319 | engine_army_state.c |
| 0x465761 | FUN_00465761 | 319 | engine_army_state.c |
| 0x4658c1 | FUN_004658c1 | 1112 | engine_army_move.c, engine_army_move.h |
| 0x465d28 | FUN_00465d28 | 1441 | engine_army_move.c, engine_army_move.h, engine_army_state.c (+3) |
| 0x46634d | FUN_0046634d | 1002 | engine_army_move.c, engine_army_move.h |
| 0x46673c | FUN_0046673c | 343 | engine_army_move.c, engine_army_move.h |
| 0x466893 | FUN_00466893 | 900 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x466c3c | FUN_00466c3c | 328 | engine_army_move.c, engine_army_move.h |
| 0x466d84 | FUN_00466d84 | 995 | engine_army_move.c, engine_army_move.h |
| 0x467196 | FUN_00467196 | 422 | engine_army_ai.c, engine_army_move.c, engine_army_move.h |
| 0x46733c | FUN_0046733c | 124 | engine_army_ai.c, engine_army_move.c, engine_combat.c (+2) |
| 0x4673b8 | FUN_004673b8 | 378 | engine_army_ai.c |
| 0x467532 | FUN_00467532 | 426 | engine_army_ai.c |
| 0x4676e0 | FUN_004676e0 | 144 | engine_territory.c, engine_territory.h |
| 0x467770 | FUN_00467770 | 525 | engine_territory.c, engine_territory.h, engine_territory_globals.c (+4) |
| 0x46797d | FUN_0046797d | 185 | engine_map_init.c, engine_map_init.h |
| 0x467a36 | FUN_00467a36 | 624 | engine_territory.c, engine_territory.h |
| 0x467ca6 | FUN_00467ca6 | 385 | engine_territory.c, engine_territory.h |
| 0x467e2c | FUN_00467e2c | 127 | engine_realm.c, engine_realm.h |
| 0x467eb0 | FUN_00467eb0 | 126 | engine_army_ai.c, engine_combat.c, engine_realm.c (+1) |
| 0x467f2e | FUN_00467f2e | 163 | engine_army_ai.c |
| 0x467fd1 | FUN_00467fd1 | 336 | engine_territory.c, engine_territory.h |
| 0x468121 | FUN_00468121 | 331 | engine_territory.c, engine_territory.h |
| 0x46826c | FUN_0046826c | 221 | engine_army_ai.c, engine_territory.c, engine_territory.h |
| 0x46841c | FUN_0046841c | 170 | engine_army_ai.c |
| 0x4684c6 | FUN_004684c6 | 474 | engine_territory.c, engine_territory.h |
| 0x4686a0 | FUN_004686a0 | 159 | engine_army_move.c, engine_army_move.h, engine_combat.c (+1) |
| 0x46873f | FUN_0046873f | 931 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x468ae2 | FUN_00468ae2 | 351 | engine_army_move.c, engine_army_move.h, game_init.c |
| 0x468c41 | FUN_00468c41 | 270 | engine_territory.c, engine_territory.h |
| 0x468d4f | FUN_00468d4f | 274 | engine_territory.c, engine_territory.h, game_init.c |
| 0x468e61 | FUN_00468e61 | 457 | engine_territory.c, engine_territory.h |
| 0x46902a | FUN_0046902a | 499 | engine_territory.c, engine_territory.h |
| 0x46921d | FUN_0046921d | 138 | engine_army_move.c, engine_territory.c, engine_territory.h |
| 0x46942c | FUN_0046942c | 147 | engine_realm.c, engine_realm.h |
| 0x46958f | FUN_0046958f | 203 | engine_territory.c, engine_territory.h |
| 0x46965a | FUN_0046965a | 371 | engine_territory.c, engine_territory.h |
| 0x4697cd | FUN_004697cd | 192 | engine_territory.c, engine_territory.h |
| 0x46988d | FUN_0046988d | 274 | engine_territory.c, engine_territory.h |
| 0x469a9c | FUN_00469a9c | 176 | engine_realm.c, engine_realm.h |
| 0x469b51 | FUN_00469b51 | 60 | engine_territory.c, engine_territory.h |
| 0x469b8d | FUN_00469b8d | 404 | engine_army_economy.c, engine_territory.c, engine_territory.h (+1) |
| 0x469d21 | FUN_00469d21 | 237 | engine_territory.c, engine_territory.h |
| 0x469e0e | FUN_00469e0e | 72 | engine_army_move.c, engine_army_move.h, engine_army_move_confirm.c (+1) |
| 0x469e5b | FUN_00469e5b | 476 | engine_army_move.c, engine_army_move.h |
| 0x46a037 | FUN_0046a037 | 202 | engine_minimap.c, engine_minimap.h, game_init.c (+1) |
| 0x46ac22 | FUN_0046ac22 | 329 | engine_territory.c, engine_territory.h |
| 0x46c080 | FUN_0046c080 | 199 | engine_territory.c, engine_territory.h |
| 0x46c147 | FUN_0046c147 | 407 | engine_territory.c, engine_territory.h |
| 0x46c2de | FUN_0046c2de | 292 | engine_territory.c, engine_territory.h |
| 0x46c4a3 | FUN_0046c4a3 | 277 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x46c5b8 | FUN_0046c5b8 | 258 | engine_territory.c, engine_territory.h |
| 0x46c6ba | FUN_0046c6ba | 322 | engine_territory.c, engine_territory.h |
| 0x46cfbd | FUN_0046cfbd | 371 | engine_army_state.c, engine_map_query.c, engine_map_query.h (+2) |
| 0x46d130 | FUN_0046d130 | 371 | engine_map_query.c, engine_map_query.h, game_init.c (+1) |
| 0x46d2a3 | FUN_0046d2a3 | 301 | engine_map_query.c, engine_map_query.h |
| 0x46d42c | FUN_0046d42c | 456 | engine_combat.c, engine_combat.h |
| 0x46d5fe | FUN_0046d5fe | 502 | engine.c, engine_map_query.c, engine_map_query.h (+2) |
| 0x46d7f4 | FUN_0046d7f4 | 486 | engine_army_move.c, engine_realm_events.h, engine_territory.c (+1) |
| 0x46d9da | FUN_0046d9da | 113 | engine_territory.c, engine_territory.h |
| 0x46da4b | FUN_0046da4b | 426 | engine_territory.c, engine_territory.h, main.c |
| 0x46dbf5 | FUN_0046dbf5 | 319 | engine_territory.c, engine_territory.h, engine_territory_globals.c (+1) |
| 0x46dd34 | FUN_0046dd34 | 209 | engine_territory.c, engine_territory.h, main.c |
| 0x46de05 | FUN_0046de05 | 143 | engine_territory.c, engine_territory.h |
| 0x46de94 | FUN_0046de94 | 189 | engine_territory.c, engine_territory.h |
| 0x46df51 | FUN_0046df51 | 132 | engine_territory.c, engine_territory.h, engine_territory_globals.c |
| 0x46dfd5 | FUN_0046dfd5 | 146 | engine_combat.c, engine_territory.c, engine_territory.h (+1) |
| 0x46e067 | FUN_0046e067 | 317 | engine_territory.c, engine_territory.h |
| 0x46e1b0 | FUN_0046e1b0 | 792 | engine_army_ai.c, engine_combat.c, engine_territory.c (+2) |
| 0x46ea28 | FUN_0046ea28 | 57 | game_init.c, game_init.h |
| 0x46ea61 | FUN_0046ea61 | 57 | engine_combat.c, game_init.c |
| 0x46ead0 | FUN_0046ead0 | 57 | engine_army_state.c, engine_map_init.c, engine_realm_upkeep.c (+3) |
| 0x46eb09 | FUN_0046eb09 | 54 | game_init.c, game_init.h |
| 0x46ec6e | FUN_0046ec6e | 369 | engine_army_move.c, engine_combat.c, engine_realm_upkeep.c (+1) |
| 0x46eddf | FUN_0046eddf | 349 | engine_army_move.c, engine_combat.c, engine_realm_upkeep.c (+1) |
| 0x46f0a9 | FUN_0046f0a9 | 71 | engine_army_move.c, engine_army_state.c, engine_realm_upkeep.c (+1) |
| 0x46f1dc | FUN_0046f1dc | 67 | game_init.c, game_init.h |
| 0x46f2ae | FUN_0046f2ae | 67 | game_init.c, game_init.h |
| 0x46f3cd | FUN_0046f3cd | 78 | engine_map_init.c, game_init.c, game_init.h |
| 0x46f434 | FUN_0046f434 | 216 | engine_army_move.c, engine_path.c, engine_path.h |
| 0x46f68e | FUN_0046f68e | 114 | engine_army_select.c, engine_army_select.h, engine_territory.c |
| 0x46f700 | FUN_0046f700 | 2115 | engine_army_ai.c, engine_army_move.c, engine_path_field.c (+1) |
| 0x46ff43 | FUN_0046ff43 | 617 | engine.c, engine_strat_cost.c, engine_strat_cost.h (+1) |
| 0x4701ac | FUN_004701ac | 1228 | engine_army_ai.c, engine_path_field.c, engine_path_field.h |
| 0x470678 | FUN_00470678 | 326 | engine_army_move_confirm.c |
| 0x4707be | FUN_004707be | 274 | engine_army_ai.c, engine_path_field.c, engine_path_field.h |
| 0x4708d0 | FUN_004708d0 | 142 | engine_path_field.c, engine_path_field.h |
| 0x471c1f | FUN_00471c1f | 273 | engine_tile.c, engine_tile.h |
| 0x471d30 | FUN_00471d30 | 118 | engine_tile.c, engine_tile.h |
| 0x472392 | FUN_00472392 | 715 | engine_path.c, engine_path.h, engine_path_field.h |
| 0x47265d | FUN_0047265d | 536 | engine_path.h |
| 0x472875 | FUN_00472875 | 151 | engine_path.h |
| 0x472ad0 | FUN_00472ad0 | 16 | engine_event_queue.c, engine_event_queue.h, game_init.c |
| 0x472ae0 | FUN_00472ae0 | 96 | engine_event_queue.c, engine_event_queue.h |
| 0x472b40 | FUN_00472b40 | 133 | engine.c, engine_event_queue.c, engine_event_queue.h |
| 0x472bc5 | FUN_00472bc5 | 560 | engine.c, engine_army_ai.c, engine_army_move.c (+9) |
| 0x472df5 | FUN_00472df5 | 81 | engine_event_queue.c, engine_event_queue.h |
| 0x476710 | FUN_00476710 | 88 | engine_army_select.c, engine_army_select.h |
| 0x476768 | FUN_00476768 | 245 | engine.c, engine_army_select.h, engine_ingame_input.h (+2) |
| 0x47685d | FUN_0047685d | 512 | engine.c, engine_ingame_input.h |
| 0x476e95 | FUN_00476e95 | 421 | engine.c, engine_tooltip.h |
| 0x4770b8 | FUN_004770b8 | 121 | engine_tooltip.c, engine_tooltip.h, engine_workforce_hud.c (+1) |
| 0x477131 | FUN_00477131 | 280 | engine_tooltip.c, engine_tooltip.h |
| 0x477249 | FUN_00477249 | 109 | engine_tooltip.c, engine_tooltip.h |
| 0x4772b6 | FUN_004772b6 | 106 | engine_tooltip.c, engine_tooltip.h |
| 0x477320 | FUN_00477320 | 1082 | engine_tooltip.c, engine_tooltip.h |
| 0x4777aa | FUN_004777aa | 246 | engine_tooltip.c, engine_tooltip.h |
| 0x477dfc | FUN_00477dfc | 1225 | engine_combat.c, engine_combat.h |
| 0x478419 | FUN_00478419 | 177 | engine_combat.c, engine_combat.h |
| 0x4788c6 | FUN_004788c6 | 140 | engine_combat.c, engine_combat.h |
| 0x47ed34 | FUN_0047ed34 | 103 | engine.c, engine_camera.c, engine_camera.h |
| 0x47f474 | FUN_0047f474 | 761 | engine_combat.c, engine_combat.h |
| 0x48ee46 | FUN_0048ee46 | 87 | engine_tile.c, engine_tile.h |
| 0x4917dd | FUN_004917dd | 599 | engine_tile.c, engine_tile.h, save.c |
| 0x4921e5 | FUN_004921e5 | 451 | engine_tile.c, engine_tile.h |
| 0x497500 | FUN_00497500 | 211 | engine_minimap.c |
| 0x4976a1 | FUN_004976a1 | 472 | engine_load_pump.c, engine_load_pump.h, engine_map_init.c (+2) |
| 0x49792e | FUN_0049792e | 262 | engine.c, engine.h, game_init.c (+1) |
| 0x497a34 | FUN_00497a34 | 697 | engine.c, engine.h, main.c |
| 0x497ced | FUN_00497ced | 376 | engine_load_pump.c, engine_territory.c, engine_territory.h (+3) |
| 0x497e65 | FUN_00497e65 | 1025 | engine.c, engine.h, game_init.c (+1) |
| 0x498270 | FUN_00498270 | 327 | engine_map_init.c, engine_map_init.h, main.c |
| 0x4984dc | FUN_004984dc | 731 | engine.c, engine_ingame_input.c, engine_pl8.c (+4) |
| 0x499034 | FUN_00499034 | 197 | engine_menu_input.c, gfx.c, gfx.h |
| 0x4997bf | FUN_004997bf | 151 | engine_workforce_hud.c, engine_workforce_hud.h |
| 0x499a1c | FUN_00499a1c | 174 | engine.c, gfx.c, gfx.h (+1) |
| 0x499aca | FUN_00499aca | 487 | engine_combat.c, engine_combat.h, main.c |
| 0x499d7d | FUN_00499d7d | 26 | main.c |
| 0x499dc3 | FUN_00499dc3 | 154 | engine_menu_input.c, engine_menu_input.h, game_init.c (+2) |
| 0x499e5d | FUN_00499e5d | 421 | engine_menu_input.c, game_init.c, game_init.h (+1) |
| 0x49a010 | FUN_0049a010 | 982 | engine.c, engine_tick_cadence.h |
| 0x49a453 | FUN_0049a453 | 302 | game_init.c, game_init.h, save.c (+1) |
| 0x49a581 | FUN_0049a581 | 1039 | engine.c, engine_army_ai.c, engine_army_ai.h |
| 0x49a990 | FUN_0049a990 | 126 | engine_army_ai.c, engine_army_ai.h, engine_combat.c (+1) |
| 0x49aa0e | FUN_0049aa0e | 2197 | engine.c, engine_ranking.c, engine_ranking.h (+1) |
| 0x49b2a3 | FUN_0049b2a3 | 187 | engine_combat.c, engine_combat.h |
| 0x49b35e | FUN_0049b35e | 205 | game_init.c, game_init.h |
| 0x49b42b | FUN_0049b42b | 680 | engine_combat.c, engine_ranking.c, engine_ranking.h |
| 0x49b6d3 | FUN_0049b6d3 | 143 | engine_turn.c, engine_turn.h |
| 0x49b762 | FUN_0049b762 | 334 | engine.c, engine.h, engine_turn.c (+1) |
| 0x49bae9 | FUN_0049bae9 | 180 | engine_menu_input.c, engine_roster.c, engine_roster.h |
| 0x49bbe8 | FUN_0049bbe8 | 53 | engine_roster.c, engine_roster.h, engine_territory.c |
| 0x49bc1d | FUN_0049bc1d | 66 | engine_roster.c, engine_roster.h, engine_territory.c |
| 0x49bc5f | FUN_0049bc5f | 314 | engine_roster.c, engine_roster.h |
| 0x49bd99 | FUN_0049bd99 | 3063 | game_init.c, game_init.h |
| 0x49ca32 | FUN_0049ca32 | 46 | engine_roster.c, engine_roster.h, main.c |
| 0x49caaa | FUN_0049caaa | 935 | engine_menu_input.c, engine_npc_schedule.c, engine_npc_schedule.h (+3) |
| 0x49ce51 | FUN_0049ce51 | 109 | engine.c, engine_turn.c, engine_turn.h |
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
| 0x49f12f | FUN_0049f12f | 765 | engine_army_ai.c, engine_army_ai.h, engine_combat.h |
| 0x49f431 | FUN_0049f431 | 782 | engine_army_ai.c, engine_army_ai.h |
| 0x49f73f | FUN_0049f73f | 273 | engine_army_ai.c |
| 0x49f850 | FUN_0049f850 | 237 | engine_army_ai.c |
| 0x49f93d | FUN_0049f93d | 47 | engine_army_ai.c, engine_army_ai.h |
| 0x49f96c | FUN_0049f96c | 11 | engine_army_ai.c, engine_army_ai.h |
| 0x49f977 | FUN_0049f977 | 841 | engine_army_ai.c, engine_army_ai.h |
| 0x49fcc5 | FUN_0049fcc5 | 224 | engine_army_ai.c |
| 0x49fda5 | FUN_0049fda5 | 460 | engine_army_ai.c |
| 0x49ff71 | FUN_0049ff71 | 164 | engine_army_ai.c |
| 0x4a0015 | FUN_004a0015 | 469 | engine_army_ai.c, engine_army_ai.h, engine_combat.c |
| 0x4a01ea | FUN_004a01ea | 287 | engine_army_ai.c |
| 0x4a0309 | FUN_004a0309 | 233 | engine_army_ai.c, engine_army_ai.h |
| 0x4a03f2 | FUN_004a03f2 | 599 | engine_army_ai.c, engine_army_ai.h, engine_path.h |
| 0x4a0649 | FUN_004a0649 | 425 | engine_army_ai.c, engine_army_ai.h, engine_path.h |
| 0x4a07f2 | FUN_004a07f2 | 293 | engine_combat.c, engine_combat.h |
| 0x4a0917 | FUN_004a0917 | 403 | engine_combat.c, engine_combat.h |
| 0x4a0aaa | FUN_004a0aaa | 371 | engine_army_ai.c, engine_army_ai.h |
| 0x4a0c1d | FUN_004a0c1d | 1572 | engine_army_ai.c, engine_army_ai.h |
| 0x4a1241 | FUN_004a1241 | 357 | engine_diplomacy.c, engine_diplomacy.h |
| 0x4a13a6 | FUN_004a13a6 | 849 | engine_army_ai.c, engine_army_ai.h |
| 0x4a16f7 | FUN_004a16f7 | 125 | engine_army_move.h, engine_diplomacy.c, engine_diplomacy.h |
| 0x4a1774 | FUN_004a1774 | 211 | engine_army_ai.c, engine_diplomacy.c, engine_diplomacy.h |
| 0x4a1847 | FUN_004a1847 | 525 | engine.c, engine_diplomacy.c, engine_diplomacy.h |
| 0x4a1a54 | FUN_004a1a54 | 213 | engine_army_ai.c, engine_diplomacy.c, engine_diplomacy.h |
| 0x4a1b29 | FUN_004a1b29 | 298 | engine_army_ai.c |
| 0x4a1c53 | FUN_004a1c53 | 537 | game_init.c, game_init.h |
| 0x4a1e6c | FUN_004a1e6c | 117 | engine_noble_faces.c, engine_noble_faces.h, engine_roster.c (+2) |
| 0x4a1ee1 | FUN_004a1ee1 | 1520 | engine_combat.c, engine_diplomacy.c, engine_diplomacy.h |
| 0x4a24d1 | FUN_004a24d1 | 179 | engine_diplomacy.c, engine_diplomacy.h |
| 0x4a2584 | FUN_004a2584 | 157 | engine_army_ai.c, game_init.c, game_init.h |
| 0x4a2621 | FUN_004a2621 | 343 | engine.c, engine_diplomacy.c, engine_diplomacy.h (+2) |
| 0x4a277d | FUN_004a277d | 565 | engine_army_ai.c |
| 0x4a29b2 | FUN_004a29b2 | 804 | engine_army_ai.c |
| 0x4a2cd6 | FUN_004a2cd6 | 651 | engine_army_ai.c |
| 0x4a2f61 | FUN_004a2f61 | 405 | engine_army_ai.c |
| 0x4a30f6 | FUN_004a30f6 | 895 | engine_army_ai.c |
| 0x4a3475 | FUN_004a3475 | 220 | engine_army_ai.c |
| 0x4a3551 | FUN_004a3551 | 906 | engine_army_ai.c |
| 0x4a38db | FUN_004a38db | 908 | engine_army_ai.c |
| 0x4a3c67 | FUN_004a3c67 | 620 | engine_army_ai.c, engine_army_ai.h |
| 0x4a3ed3 | FUN_004a3ed3 | 383 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4052 | FUN_004a4052 | 657 | engine_army_ai.c, engine_army_ai.h |
| 0x4a42e3 | FUN_004a42e3 | 300 | engine_army_ai.c |
| 0x4a440f | FUN_004a440f | 645 | engine_army_ai.c |
| 0x4a4694 | FUN_004a4694 | 238 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4782 | FUN_004a4782 | 701 | engine_army_ai.c, engine_army_ai.h, engine_army_economy.c (+1) |
| 0x4a4a3f | FUN_004a4a3f | 211 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4b12 | FUN_004a4b12 | 298 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4c41 | FUN_004a4c41 | 311 | engine_army_ai.c, engine_army_ai.h |
| 0x4a4e3d | FUN_004a4e3d | 286 | engine_army_ai.c, engine_army_state.c, engine_army_state.h |
| 0x4a4f5b | FUN_004a4f5b | 168 | engine.c, engine_army_state.c, engine_army_state.h |
| 0x4a5003 | FUN_004a5003 | 171 | engine_combat.c, engine_combat.h |
| 0x4a50ae | FUN_004a50ae | 731 | engine_combat.c, engine_combat.h |
| 0x4a5389 | FUN_004a5389 | 214 | engine_combat.c, engine_combat.h |
| 0x4a545f | FUN_004a545f | 188 | engine_combat.c |
| 0x4a551b | FUN_004a551b | 332 | engine_combat.c, engine_combat.h |
| 0x4a5667 | FUN_004a5667 | 325 | engine_army_ai.c, engine_army_ai.h |
| 0x4a57ac | FUN_004a57ac | 328 | engine_army_ai.c |
| 0x4a58f4 | FUN_004a58f4 | 169 | engine_army_ai.c, engine_combat.c |
| 0x4a599d | FUN_004a599d | 386 | engine_army_ai.c |
| 0x4a5b1f | FUN_004a5b1f | 1003 | engine_army_ai.c |
| 0x4a5f0a | FUN_004a5f0a | 431 | engine_army_ai.c |
| 0x4a60b9 | FUN_004a60b9 | 439 | engine_army_ai.c |
| 0x4a6270 | FUN_004a6270 | 602 | engine_army_ai.c, engine_combat.c, engine_combat.h |
| 0x4a64ca | FUN_004a64ca | 217 | engine_army_ai.c |
| 0x4a65a3 | FUN_004a65a3 | 402 | engine_army_ai.c, engine_map_query.h |
| 0x4a6735 | FUN_004a6735 | 360 | engine_army_ai.c, engine_map_query.h |
| 0x4a689d | FUN_004a689d | 399 | engine_combat.c, engine_combat.h |
| 0x4a6a30 | FUN_004a6a30 | 568 | engine_combat.c, engine_combat.h |
| 0x4a6c68 | FUN_004a6c68 | 1264 | engine_army_move.c, engine_army_move.h, engine_combat.c (+2) |
| 0x4a7158 | FUN_004a7158 | 402 | engine_combat.c, engine_combat.h |
| 0x4a72fe | FUN_004a72fe | 1516 | engine_combat.c, engine_combat.h |
| 0x4a78ea | FUN_004a78ea | 94 | engine_combat.c, engine_combat.h |
| 0x4a79a3 | FUN_004a79a3 | 767 | engine_combat.c, engine_combat.h |
| 0x4a7ca2 | FUN_004a7ca2 | 269 | engine_combat.c, engine_combat.h |
| 0x4a7e0a | FUN_004a7e0a | 171 | engine_combat.c, engine_combat.h |
| 0x4a7eb5 | FUN_004a7eb5 | 550 | engine_combat.c, engine_combat.h |
| 0x4a80db | FUN_004a80db | 478 | engine_combat.c, engine_combat.h |
| 0x4a82b9 | FUN_004a82b9 | 365 | engine_army_state.c, engine_army_state.h |
| 0x4a8426 | FUN_004a8426 | 148 | engine_army_select.h |
| 0x4a84ba | FUN_004a84ba | 77 | engine_army_state.c, engine_army_state.h |
| 0x4a8507 | FUN_004a8507 | 1444 | engine_army_state.c, engine_army_state.h |
| 0x4a8aab | FUN_004a8aab | 753 | engine_army_state.h, engine_combat.c, engine_combat.h |
| 0x4a8d9c | FUN_004a8d9c | 111 | engine_army_select.c, engine_army_select.h |
| 0x4a8e0b | FUN_004a8e0b | 943 | engine.c, engine_army_move_confirm.c, engine_army_move_confirm.h |
| 0x4a91ba | FUN_004a91ba | 152 | engine_army_move_confirm.c, engine_army_move_confirm.h |
| 0x4a9252 | FUN_004a9252 | 288 | engine.c, engine_army_move_confirm.c, engine_army_move_confirm.h (+3) |
| 0x4a9372 | FUN_004a9372 | 669 | engine.c, engine_army_move_confirm.c, engine_army_move_confirm.h |
| 0x4a960f | FUN_004a960f | 334 | engine_army_move_confirm.c, engine_army_move_confirm.h |
| 0x4a975d | FUN_004a975d | 222 | engine_army_move_confirm.c, engine_army_move_confirm.h |
| 0x4a983b | FUN_004a983b | 195 | engine_army_move_confirm.c, engine_army_move_confirm.h |
| 0x4a98fe | FUN_004a98fe | 412 | engine_army_move_confirm.c, engine_army_move_confirm.h |
| 0x4a9a9a | FUN_004a9a9a | 1047 | engine.c, engine_armory.h, engine_combat.c (+2) |
| 0x4a9eb1 | FUN_004a9eb1 | 103 | engine_combat.c |
| 0x4a9f18 | FUN_004a9f18 | 90 | engine_combat.c |
| 0x4a9f72 | FUN_004a9f72 | 199 | engine_combat.c |
| 0x4aa039 | FUN_004aa039 | 328 | engine_army_ai.c, engine_combat.c, engine_realm_upkeep.c (+1) |
| 0x4aa181 | FUN_004aa181 | 1353 | engine_combat.c, engine_combat.h |
| 0x4aa6ca | FUN_004aa6ca | 469 | engine_combat.c, engine_combat.h |
| 0x4aa89f | FUN_004aa89f | 107 | engine_combat.c, engine_combat.h |
| 0x4aa90a | FUN_004aa90a | 374 | engine.c, engine_armory.c, engine_armory.h (+4) |
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
| 0x4ac7f3 | FUN_004ac7f3 | 273 | engine_combat.c |
| 0x4ac904 | FUN_004ac904 | 295 | game_init.c, game_init.h |
| 0x4aca2b | FUN_004aca2b | 297 | engine.c, engine_combat.c, engine_combat.h |
| 0x4acb54 | FUN_004acb54 | 128 | engine_combat.c |
| 0x4acbd4 | FUN_004acbd4 | 650 | engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ace5e | FUN_004ace5e | 650 | engine.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad0e8 | FUN_004ad0e8 | 132 | engine_realm_upkeep.c |
| 0x4ad16c | FUN_004ad16c | 196 | engine_realm_upkeep.c |
| 0x4ad230 | FUN_004ad230 | 230 | engine_realm_upkeep.c |
| 0x4ad316 | FUN_004ad316 | 132 | engine_realm_upkeep.c |
| 0x4ad39a | FUN_004ad39a | 140 | engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad426 | FUN_004ad426 | 111 | engine_combat.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad495 | FUN_004ad495 | 150 | engine_army_ai.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad52b | FUN_004ad52b | 405 | engine_combat.c, engine_realm_upkeep.c, engine_realm_upkeep.h |
| 0x4ad6c0 | FUN_004ad6c0 | 524 | engine_army_state.c, engine_combat.c, engine_realm.c (+2) |
| 0x4ad8cc | FUN_004ad8cc | 277 | engine_combat.c, engine_combat.h |
| 0x4ad9f0 | FUN_004ad9f0 | 774 | engine.c, engine_load_pump.c, engine_load_pump.h (+1) |
| 0x4adcf6 | FUN_004adcf6 | 413 | engine_load_pump.c, engine_load_pump.h |
| 0x4ade93 | FUN_004ade93 | 339 | save.c, save.h |
| 0x4adfe6 | FUN_004adfe6 | 377 | engine_load_pump.c, engine_load_pump.h, main.c (+2) |
| 0x4ae539 | FUN_004ae539 | 169 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4ae5e2 | FUN_004ae5e2 | 96 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4ae74d | FUN_004ae74d | 144 | game_init.c, game_init.h |
| 0x4ae7dd | FUN_004ae7dd | 204 | engine_realm.c, engine_realm.h, game_init.c (+2) |
| 0x4ae8a9 | FUN_004ae8a9 | 191 | game_init.c, game_init.h, save.c (+1) |
| 0x4ae968 | FUN_004ae968 | 264 | save.c, save.h |
| 0x4aea70 | FUN_004aea70 | 506 | save.c, save.h |
| 0x4aef7e | FUN_004aef7e | 408 | engine_menu_input.h, engine_turn.c, engine_turn.h (+2) |
| 0x4af502 | FUN_004af502 | 72 | engine_armory.c, engine_county_panels.c, engine_labor.c (+6) |
| 0x4af54a | FUN_004af54a | 112 | io.h, strutil.h |
| 0x4af5ba | FUN_004af5ba | 114 | strutil.h |
| 0x4af62c | FUN_004af62c | 73 | strutil.h |
| 0x4af675 | FUN_004af675 | 84 | engine_load_pump.c, strutil.h |
| 0x4af6c9 | FUN_004af6c9 | 67 | strutil.h |
| 0x4af767 | FUN_004af767 | 218 | engine_load_pump.c, engine_load_pump.h |
| 0x4af8d9 | FUN_004af8d9 | 381 | config.c, engine_armory.c, engine_county_panels.c (+11) |
| 0x4afab9 | FUN_004afab9 | 897 | engine_hud_menubar.h, gfx_panel.c |
| 0x4afeed | FUN_004afeed | 1214 | gfx_text_label.c, gfx_text_label.h |
| 0x4b13ed | FUN_004b13ed | 274 | gfx_paint.c, gfx_paint.h |
| 0x4b18f8 | FUN_004b18f8 | 38 | game_init.c |
| 0x4b191e | FUN_004b191e | 981 | engine.c, engine.h, engine_input.c (+2) |
| 0x4b1de0 | FUN_004b1de0 | 11 | engine.c, engine_armory.c, engine_confirm_dialog.c (+6) |
| 0x4b1deb | FUN_004b1deb | 92 | engine.c, engine_county_panels.c |
| 0x4b20ed | FUN_004b20ed | 68 | engine.c, engine.h, engine_delta.c |
| 0x4b2258 | FUN_004b2258 | 797 | audio.h, engine.c, engine.h (+2) |
| 0x4b2636 | FUN_004b2636 | 130 | engine.c, engine.h, engine_fps.c (+1) |
| 0x4b26f8 | FUN_004b26f8 | 56 | main.c |
| 0x4b2730 | FUN_004b2730 | 143 | gfx.c, gfx.h, main.c |
| 0x4b27bf | FUN_004b27bf | 143 | engine.c, engine.h, engine_camera.h (+2) |
| 0x4b28cd | FUN_004b28cd | 85 | gfx.c, main.c |
| 0x4b2922 | FUN_004b2922 | 156 | gfx.c, gfx.h, main.c |
| 0x4b29be | FUN_004b29be | 2545 | engine_minimap.c, main.c |
| 0x4b36c0 | FUN_004b36c0 | 84 | engine_confirm_dialog.c |
| 0x4b3768 | FUN_004b3768 | 84 | engine.c, engine_county_panels.h |
| 0x4b37bc | FUN_004b37bc | 388 | engine_terrain_info.c, engine_terrain_info.h |
| 0x4b3e51 | FUN_004b3e51 | 111 | engine_army_select.c |
| 0x4b3ec0 | FUN_004b3ec0 | 74 | engine_armory.c, engine_workforce_hud.c, engine_workforce_hud.h |
| 0x4b3f0a | FUN_004b3f0a | 74 | engine_armory.c, engine_armory.h, engine_workforce_hud.h |
| 0x4b3f54 | FUN_004b3f54 | 38 | gfx_paint.h |
| 0x4b3f7a | FUN_004b3f7a | 41 | gfx_paint.h |
| 0x4b3fa3 | FUN_004b3fa3 | 61 | gfx_paint.h |
| 0x4b4056 | FUN_004b4056 | 47 | engine_overlay_render.c |
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
| 0x4b495a | FUN_004b495a | 93 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b49b7 | FUN_004b49b7 | 64 | engine_battlefield_render.c, gfx_sprite.c, gfx_sprite.h |
| 0x4b49f7 | FUN_004b49f7 | 705 | gfx_sprite.c, gfx_sprite.h |
| 0x4b4cb8 | FUN_004b4cb8 | 1659 | gfx_sprite.c, gfx_sprite.h, gfx_sprite_render.c (+1) |
| 0x4b5333 | FUN_004b5333 | 2997 | gfx_sprite.c, gfx_sprite.h |
| 0x4b99c0 | FUN_004b99c0 | 2222 | call_trace.h, engine.c, engine.h (+11) |
| 0x4ba26e | FUN_004ba26e | 1882 | engine.c, engine_armory.c, engine_armory.h (+19) |
| 0x4ba9c8 | FUN_004ba9c8 | 3832 | engine.c, engine_confirm_dialog.h, engine_county_panels.h (+10) |
| 0x4bb8c0 | FUN_004bb8c0 | 84 | engine.c, engine_rate.c |
| 0x4bb978 | FUN_004bb978 | 614 | engine.c, engine.h, engine_rate.c |
| 0x4bbbe3 | FUN_004bbbe3 | 157 | engine_camera.h |
| 0x4bbc80 | FUN_004bbc80 | 465 | engine.c, engine.h, engine_phase.c |
| 0x4bc020 | FUN_004bc020 | 231 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc107 | FUN_004bc107 | 59 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc142 | FUN_004bc142 | 143 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc1d1 | FUN_004bc1d1 | 81 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc222 | FUN_004bc222 | 131 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc2a5 | FUN_004bc2a5 | 32 | engine_battlefield_render.h |
| 0x4bc2c5 | FUN_004bc2c5 | 32 | engine_battlefield_render.h |
| 0x4bc2e5 | FUN_004bc2e5 | 97 | engine_battlefield_render.h |
| 0x4bc346 | FUN_004bc346 | 102 | engine_battlefield_render.h |
| 0x4bc3ac | FUN_004bc3ac | 97 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x4bc40d | FUN_004bc40d | 100 | engine_battlefield_render.h |
| 0x4bc471 | FUN_004bc471 | 74 | engine_battlefield_render.h |
| 0x4bc4bb | FUN_004bc4bb | 74 | engine_battlefield_render.h |
| 0x4bc505 | FUN_004bc505 | 21 | engine_battlefield_render.h |
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

## ported (170) — reimplemented, no host test yet

| VA | name | size | src |
|----|------|-----:|-----|
| 0x4063c1 | FUN_004063c1 | 690 | engine_map_terrain.c, engine_map_terrain.h |
| 0x406673 | FUN_00406673 | 1351 | engine_map_terrain.c |
| 0x408fcb | FUN_00408fcb | 96 | engine_armory.c, engine_merchant.c, engine_rankings.c (+3) |
| 0x410a5d | FUN_00410a5d | 76 | engine_sendsupplies.c |
| 0x412b33 | FUN_00412b33 | 824 | engine.c, engine_labor.c, engine_labor.h (+1) |
| 0x412e6b | FUN_00412e6b | 746 | engine_labor.c, engine_labor.h |
| 0x413b30 | FUN_00413b30 | 1475 | engine_labor.c, engine_labor.h |
| 0x4140f3 | FUN_004140f3 | 301 | engine_labor.c, engine_labor.h |
| 0x414220 | FUN_00414220 | 31 | engine_labor.c, engine_labor.h |
| 0x41593b | FUN_0041593b | 673 | engine.c, engine_rankings.c, engine_rankings.h |
| 0x415bdc | FUN_00415bdc | 614 | engine_rankings.c, engine_rankings.h |
| 0x415e42 | FUN_00415e42 | 348 | engine_rankings.c, engine_rankings.h |
| 0x415fb7 | FUN_00415fb7 | 212 | engine.c, engine_merchant.c, engine_merchant.h |
| 0x416308 | FUN_00416308 | 87 | engine.c, engine_trade.c, engine_trade.h (+1) |
| 0x41635f | FUN_0041635f | 1478 | engine_trade.c, engine_trade.h |
| 0x416925 | FUN_00416925 | 974 | engine.c, engine_treasury.c, engine_treasury.h |
| 0x416cf3 | FUN_00416cf3 | 1275 | engine.c, engine_noble_faces.c, engine_noble_faces.h |
| 0x4171ee | FUN_004171ee | 1028 | engine_noble_faces.c, engine_noble_faces.h |
| 0x41789b | FUN_0041789b | 197 | engine.c, engine_diplomacy_action.c, engine_diplomacy_action.h |
| 0x417960 | FUN_00417960 | 399 | engine.c, engine_diplomacy_action.c, engine_diplomacy_action.h |
| 0x417ea7 | FUN_00417ea7 | 591 | engine.c, engine_armory.c, engine_armory.h |
| 0x4181eb | FUN_004181eb | 571 | engine.c, engine_armory.c, engine_armory.h |
| 0x418426 | FUN_00418426 | 160 | engine_armory.c |
| 0x4184c6 | FUN_004184c6 | 397 | engine.c, engine_armory.c, engine_armory.h |
| 0x418653 | FUN_00418653 | 2010 | engine.c, engine_armory.c, engine_armory.h |
| 0x418e2d | FUN_00418e2d | 408 | engine.c, engine_armory.c, engine_armory.h |
| 0x418fc5 | FUN_00418fc5 | 278 | engine.c, engine_armory.c, engine_armory.h |
| 0x4190db | FUN_004190db | 360 | engine.c, engine_armory.c, engine_armory.h |
| 0x419243 | FUN_00419243 | 110 | engine.c, engine_armory.c, engine_armory.h |
| 0x41ad5d | FUN_0041ad5d | 325 | engine.c, engine_sendsupplies.c, engine_sendsupplies.h |
| 0x41aea2 | FUN_0041aea2 | 369 | engine.c, engine_sendsupplies.c, engine_sendsupplies.h |
| 0x41dedb | FUN_0041dedb | 770 | engine_labor.c, engine_labor.h |
| 0x4238b8 | FUN_004238b8 | 285 | engine_bf_input.c |
| 0x4239d5 | FUN_004239d5 | 378 | engine_bf_input.c |
| 0x425b10 | FUN_00425b10 | 549 | audio.c, audio.h |
| 0x425ded | FUN_00425ded | 94 | audio.c, audio.h |
| 0x426529 | FUN_00426529 | 11 | engine.c |
| 0x427d72 | FUN_00427d72 | 340 | audio.c |
| 0x428daf | FUN_00428daf | 801 | engine_economy_globals.c, engine_trade.c, engine_trade.h (+1) |
| 0x42a7f1 | FUN_0042a7f1 | 224 | engine_map_terrain.c, engine_map_terrain.h, engine_screen_strat.c |
| 0x42e5c0 | FUN_0042e5c0 | 1249 | gfx.c, gfx.h |
| 0x42eaa6 | FUN_0042eaa6 | 214 | gfx.c |
| 0x42eb7c | FUN_0042eb7c | 277 | gfx.c, gfx.h |
| 0x42f306 | FUN_0042f306 | 323 | gfx.c, gfx.h |
| 0x42f449 | FUN_0042f449 | 323 | gfx.c, gfx.h |
| 0x435211 | FUN_00435211 | 61 | engine.c |
| 0x435286 | FUN_00435286 | 108 | engine_trade.h, engine_trade_model.c |
| 0x4352f2 | FUN_004352f2 | 28 | engine_trade.h, engine_trade_model.c |
| 0x43530e | FUN_0043530e | 43 | engine.c, engine_trade.c, engine_trade.h (+1) |
| 0x435339 | FUN_00435339 | 260 | engine_trade.h, engine_trade_model.c |
| 0x43543d | FUN_0043543d | 260 | engine_trade.h, engine_trade_model.c |
| 0x435541 | FUN_00435541 | 154 | engine_trade.h, engine_trade_model.c |
| 0x4355db | FUN_004355db | 152 | engine_trade.h, engine_trade_model.c |
| 0x435673 | FUN_00435673 | 296 | engine_trade.h, engine_trade_model.c |
| 0x43582a | FUN_0043582a | 134 | engine.c, engine_armory.c, engine_armory.h |
| 0x4358b0 | FUN_004358b0 | 138 | engine.c, engine_armory.c, engine_armory.h |
| 0x43593a | FUN_0043593a | 130 | engine_armory.c, engine_armory.h |
| 0x4359bc | FUN_004359bc | 80 | engine_armory.h |
| 0x435a0c | FUN_00435a0c | 85 | engine_armory.h |
| 0x435a61 | FUN_00435a61 | 135 | engine_armory.h |
| 0x435ae8 | FUN_00435ae8 | 101 | engine.c, engine_armory.h |
| 0x435b4d | FUN_00435b4d | 316 | engine.c, engine_armory.h |
| 0x435c89 | FUN_00435c89 | 54 | engine.c, engine_armory.h |
| 0x435cbf | FUN_00435cbf | 48 | engine.c, engine_armory.h |
| 0x435ebc | FUN_00435ebc | 336 | engine_armory.c, engine_armory.h, engine_sendsupplies.h (+1) |
| 0x43611b | FUN_0043611b | 38 | engine.c, engine_noble_faces.h, engine_sendsupplies_model.c |
| 0x436141 | FUN_00436141 | 74 | engine.c, engine_diplomacy_action.h, engine_noble_faces.c (+2) |
| 0x43631e | FUN_0043631e | 84 | engine_noble_faces.c, engine_noble_faces.h |
| 0x436372 | FUN_00436372 | 150 | engine.c, engine_diplomacy_action.c, engine_diplomacy_action.h (+1) |
| 0x436a88 | FUN_00436a88 | 154 | engine_sendsupplies_model.c |
| 0x4398f5 | FUN_004398f5 | 187 | engine_labor.c, engine_labor_region_grid.h |
| 0x43ac23 | FUN_0043ac23 | 258 | engine.c |
| 0x43ad25 | FUN_0043ad25 | 256 | engine.c |
| 0x43ae30 | FUN_0043ae30 | 540 | engine.c, engine_armory.h, engine_noble_faces.h (+3) |
| 0x43b04c | FUN_0043b04c | 181 | engine.c |
| 0x43b101 | FUN_0043b101 | 68 | engine.c |
| 0x43b145 | FUN_0043b145 | 133 | engine.c, engine_sendsupplies.h, engine_sendsupplies_model.c |
| 0x43b1ca | FUN_0043b1ca | 176 | engine_sendsupplies.h, engine_sendsupplies_model.c |
| 0x43b27a | FUN_0043b27a | 176 | engine_sendsupplies.h, engine_sendsupplies_model.c |
| 0x43b32a | FUN_0043b32a | 232 | engine.c, engine_sendsupplies.c, engine_sendsupplies.h (+1) |
| 0x43b9a1 | FUN_0043b9a1 | 136 | engine_bf_input.c |
| 0x43ba29 | FUN_0043ba29 | 208 | engine_bf_input.c |
| 0x43bb68 | FUN_0043bb68 | 111 | engine_bf_input.c |
| 0x43bbd7 | FUN_0043bbd7 | 16 | engine_bf_input.c |
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
| 0x440666 | FUN_00440666 | 383 | engine.c |
| 0x45183a | FUN_0045183a | 102 | engine_labor.c |
| 0x452820 | FUN_00452820 | 2907 | engine_map_terrain.c, engine_map_terrain.h |
| 0x45337b | FUN_0045337b | 1403 | engine_map_terrain.c, engine_map_terrain.h |
| 0x4538f6 | FUN_004538f6 | 1403 | engine_map_terrain.c, engine_map_terrain.h |
| 0x455f04 | FUN_00455f04 | 4308 | engine_map_terrain.c |
| 0x46eb3f | FUN_0046eb3f | 57 | engine_bf_unit.c |
| 0x46eb78 | FUN_0046eb78 | 54 | engine_bf_group.c |
| 0x46ebe4 | FUN_0046ebe4 | 138 | engine_bf_unit.c |
| 0x46ef3c | FUN_0046ef3c | 130 | engine_bf_group.c |
| 0x46efbe | FUN_0046efbe | 235 | engine_bf_group.c |
| 0x472e46 | FUN_00472e46 | 595 | engine.c |
| 0x47309e | FUN_0047309e | 10915 | engine.c |
| 0x476a5d | FUN_00476a5d | 74 | engine.c |
| 0x476aa7 | FUN_00476aa7 | 770 | engine.c |
| 0x476da9 | FUN_00476da9 | 120 | engine.c |
| 0x476e21 | FUN_00476e21 | 116 | engine.c |
| 0x4782c5 | FUN_004782c5 | 340 | engine.c |
| 0x4784ca | FUN_004784ca | 730 | engine_bf_input.c |
| 0x478952 | FUN_00478952 | 53 | engine_bf_group.c |
| 0x478987 | FUN_00478987 | 1412 | engine_bf_input.c |
| 0x478f0b | FUN_00478f0b | 392 | engine_bf_input.c |
| 0x479a71 | FUN_00479a71 | 231 | engine_bf_input.c |
| 0x479b58 | FUN_00479b58 | 415 | engine_bf_input.c |
| 0x479cf7 | FUN_00479cf7 | 277 | engine_bf_input.c |
| 0x47a76d | FUN_0047a76d | 167 | engine_bf_input.c |
| 0x47f76d | FUN_0047f76d | 458 | engine_bf_input.c |
| 0x47fc01 | FUN_0047fc01 | 522 | engine_bf_group.c |
| 0x4819cc | FUN_004819cc | 239 | engine_bf_group.c |
| 0x482111 | FUN_00482111 | 476 | engine.c |
| 0x4822ed | FUN_004822ed | 644 | engine_bf_unit.c |
| 0x485675 | FUN_00485675 | 492 | engine_bf_group.c |
| 0x485861 | FUN_00485861 | 388 | engine_bf_group.c |
| 0x4859e5 | FUN_004859e5 | 354 | engine_bf_group.c |
| 0x485bb1 | FUN_00485bb1 | 1592 | engine_bf_group.c |
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
| 0x48a672 | FUN_0048a672 | 359 | engine_bf_group.c |
| 0x491f17 | FUN_00491f17 | 359 | engine_bf_unit.c |
| 0x492c8b | FUN_00492c8b | 3793 | engine_bf_group.c |
| 0x49459a | FUN_0049459a | 878 | engine_bf_unit.c |
| 0x496f72 | FUN_00496f72 | 178 | engine_bf_group.c |
| 0x4978ad | FUN_004978ad | 129 | engine.c, engine.h |
| 0x498458 | FUN_00498458 | 132 | engine.c |
| 0x4996c8 | FUN_004996c8 | 244 | engine_armory.c |
| 0x499859 | FUN_00499859 | 451 | gfx.c |
| 0x499cb1 | FUN_00499cb1 | 204 | engine_bf_input.c |
| 0x49a3e6 | FUN_0049a3e6 | 109 | engine.c |
| 0x49b8b0 | FUN_0049b8b0 | 195 | engine.c |
| 0x4aabd8 | FUN_004aabd8 | 303 | engine_armory.c, engine_armory.h |
| 0x4ae310 | FUN_004ae310 | 553 | engine.c |
| 0x4b0aaa | FUN_004b0aaa | 11 | engine.c |
| 0x4b0ab5 | FUN_004b0ab5 | 212 | engine.c, engine_armory.c, engine_merchant.c (+4) |
| 0x4b0e03 | FUN_004b0e03 | 875 | engine.c |
| 0x4b11ce | FUN_004b11ce | 34 | engine_armory.c, engine_merchant.c, engine_rankings.c (+2) |
| 0x4b12fb | FUN_004b12fb | 21 | engine.c |
| 0x4b1867 | FUN_004b1867 | 48 | engine_screen_strat.c, engine_screen_strat.h |
| 0x4b1897 | FUN_004b1897 | 11 | engine_screen_strat.h |
| 0x4b18e3 | FUN_004b18e3 | 21 | engine.c |
| 0x4b25b0 | FUN_004b25b0 | 76 | gfx.c, gfx.h |
| 0x4b26b8 | FUN_004b26b8 | 64 | engine.c, gfx.c |
| 0x4b284e | FUN_004b284e | 122 | gfx.h |
| 0x4b35c1 | FUN_004b35c1 | 255 | engine.c |
| 0x4b3994 | FUN_004b3994 | 84 | engine.c |
| 0x4b3acd | FUN_004b3acd | 197 | engine.c |

