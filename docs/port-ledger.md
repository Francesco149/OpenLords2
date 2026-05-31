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
- touched: **431** (17.6%) — tested 395, ported 36
- code bytes touched: **18.2%** (152,465 / 838,629 B)
- unported: **2016**
- orphan refs (in src/, not in function table): 4

## tested (395) — ported + host unit suite

| VA | name | size | src |
|----|------|-----:|-----|
| 0x401000 | FUN_00401000 | 133 | strutil.h |
| 0x401085 | FUN_00401085 | 85 | strutil.h |
| 0x4010da | FUN_004010da | 92 | strutil.c, strutil.h |
| 0x401136 | FUN_00401136 | 64 | engine_event_queue.c, engine_event_queue.h, engine_npc_schedule.c (+2) |
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
| 0x4018d7 | FUN_004018d7 | 173 | localization.c, localization.h |
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
| 0x402009 | FUN_00402009 | 165 | main.c, text_input.h |
| 0x4020ae | FUN_004020ae | 94 | save.h, text_input.h |
| 0x40210c | FUN_0040210c | 90 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4022bd | FUN_004022bd | 394 | gfx_text_label.c, gfx_text_label.h |
| 0x402447 | FUN_00402447 | 400 | gfx_text_label.c, gfx_text_label.h |
| 0x4025d7 | FUN_004025d7 | 96 | gfx_text.c, gfx_text.h |
| 0x402637 | FUN_00402637 | 989 | engine_title_render.c, gfx_text.c, gfx_text.h (+2) |
| 0x402a14 | FUN_00402a14 | 514 | gfx_dispatch.c, gfx_dispatch.h, gfx_glyph.h (+1) |
| 0x402c16 | FUN_00402c16 | 72 | gfx_text.c, gfx_text.h |
| 0x402c5e | FUN_00402c5e | 217 | engine_title_render.c, gfx_wrap.c, gfx_wrap.h |
| 0x402d37 | FUN_00402d37 | 213 | gfx_wrap.c, gfx_wrap.h |
| 0x402e0c | FUN_00402e0c | 344 | gfx_text_label.c, gfx_text_label.h |
| 0x402f64 | FUN_00402f64 | 177 | gfx_text_label.c, gfx_text_label.h |
| 0x403015 | FUN_00403015 | 177 | gfx_text_label.c, gfx_text_label.h |
| 0x4030c6 | FUN_004030c6 | 202 | gfx_text_label.c, gfx_text_label.h |
| 0x403190 | FUN_00403190 | 254 | gfx_text_label.c, gfx_text_label.h |
| 0x40328e | FUN_0040328e | 673 | font_metrics.h, gfx_wrap.c, gfx_wrap.h |
| 0x40352f | FUN_0040352f | 458 | font_metrics.h, gfx_wrap.c, gfx_wrap.h |
| 0x4036f9 | FUN_004036f9 | 235 | font_metrics.h |
| 0x4037ee | FUN_004037ee | 347 | gfx_line.c, gfx_line.h |
| 0x403949 | FUN_00403949 | 126 | gfx_line.c, gfx_line.h |
| 0x4039c7 | FUN_004039c7 | 101 | gfx_paint.h |
| 0x403a2c | FUN_00403a2c | 99 | gfx_paint.h |
| 0x403a8f | FUN_00403a8f | 442 | gfx_line.c, gfx_line.h |
| 0x403c49 | FUN_00403c49 | 171 | gfx_line.c, gfx_line.h, gfx_paint.h |
| 0x403cf4 | FUN_00403cf4 | 247 | gfx_line.c, gfx_line.h |
| 0x403deb | FUN_00403deb | 249 | gfx_line.c, gfx_line.h |
| 0x403ee4 | FUN_00403ee4 | 249 | gfx_line.c, gfx_line.h |
| 0x403fdd | FUN_00403fdd | 249 | gfx_line.c, gfx_line.h |
| 0x40437d | FUN_0040437d | 197 | gfx_caret.c, gfx_caret.h, gfx_line.c (+1) |
| 0x404442 | FUN_00404442 | 256 | gfx_dispatch.c, gfx_dispatch.h |
| 0x404547 | FUN_00404547 | 256 | gfx_dispatch.c, gfx_dispatch.h |
| 0x40464c | FUN_0040464c | 303 | engine_battlefield_render.h, gfx_dispatch.c, gfx_dispatch.h (+2) |
| 0x40477b | FUN_0040477b | 291 | engine_battlefield_render.h, gfx_dispatch.c, gfx_dispatch.h (+1) |
| 0x404a46 | FUN_00404a46 | 230 | engine.c, engine.h, engine_menu_input.c (+2) |
| 0x404b2c | FUN_00404b2c | 109 | engine.h, engine_rng.c, engine_territory.c |
| 0x404b99 | FUN_00404b99 | 466 | engine.c, engine.h, engine_rng.c |
| 0x404d6b | FUN_00404d6b | 43 | engine_input.c, engine_math.c, engine_math.h (+2) |
| 0x404d96 | FUN_00404d96 | 43 | engine_math.h |
| 0x404dc1 | FUN_00404dc1 | 66 | engine_input.c, engine_math.h, engine_title_render.c |
| 0x404e03 | FUN_00404e03 | 72 | engine_army_economy.c, engine_math.h |
| 0x404e4b | FUN_00404e4b | 98 | engine_math.h |
| 0x404ead | FUN_00404ead | 159 | engine_math.h, engine_path.h |
| 0x404f4c | FUN_00404f4c | 180 | engine_path.h, engine_territory.c |
| 0x405487 | FUN_00405487 | 379 | engine_map_render.c, engine_map_render.h, engine_overlay_render.h |
| 0x405602 | FUN_00405602 | 608 | engine_overlay_render.c, engine_overlay_render.h |
| 0x405862 | FUN_00405862 | 333 | engine_map_render.c, engine_map_render.h |
| 0x4059af | FUN_004059af | 314 | engine_map_render.c, engine_map_render.h |
| 0x405eb5 | FUN_00405eb5 | 247 | engine_overlay_render.c, engine_overlay_render.h |
| 0x405fac | FUN_00405fac | 497 | engine_overlay_render.c, engine_overlay_render.h |
| 0x4071a0 | FUN_004071a0 | 3179 | engine.c, engine_anim_cycle.c, engine_overlay_render.c (+1) |
| 0x407f82 | FUN_00407f82 | 548 | engine_overlay_render.c, engine_overlay_render.h |
| 0x4081a6 | FUN_004081a6 | 658 | engine_overlay_render.c, engine_overlay_render.h |
| 0x408438 | FUN_00408438 | 2072 | engine_map_render.c, engine_map_render.h |
| 0x408c50 | FUN_00408c50 | 390 | engine_overlay_render.c, engine_overlay_render.h |
| 0x408fb0 | FUN_00408fb0 | 27 | gfx_panel.c, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x409346 | FUN_00409346 | 81 | gfx_panel.c, gfx_panel.h |
| 0x4093e0 | FUN_004093e0 | 73 | gfx_panel.c, gfx_panel.h |
| 0x40946d | FUN_0040946d | 856 | gfx_panel.c, gfx_panel.h |
| 0x4097c5 | FUN_004097c5 | 367 | gfx_panel.c, gfx_panel.h |
| 0x40a01d | FUN_0040a01d | 261 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a21a | FUN_0040a21a | 433 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h (+1) |
| 0x40a3d0 | FUN_0040a3d0 | 402 | gfx_sprite_render.c, gfx_sprite_render.h, gfx_sprite_rle.h |
| 0x40a567 | FUN_0040a567 | 283 | gfx_glyph.c, gfx_glyph.h, gfx_sprite.c (+3) |
| 0x40a682 | FUN_0040a682 | 391 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a80e | FUN_0040a80e | 413 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a9b0 | FUN_0040a9b0 | 439 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40acce | FUN_0040acce | 324 | engine_title_render.c, gfx_caret.c, gfx_caret.h (+2) |
| 0x40cfd2 | FUN_0040cfd2 | 490 | engine_button.c, engine_button.h, engine_menu_input.h (+3) |
| 0x40da1e | FUN_0040da1e | 869 | engine_delta.c, engine_menu_input.c, engine_menu_input.h (+1) |
| 0x40e3ee | FUN_0040e3ee | 505 | engine_button.h, engine_menu_input.c, engine_menu_input.h |
| 0x40e5e7 | FUN_0040e5e7 | 153 | engine_menu_input.c, engine_menu_input.h |
| 0x40e890 | FUN_0040e890 | 43 | engine.c, engine.h, main.c |
| 0x40e8bb | FUN_0040e8bb | 240 | engine.c, engine.h, main.c |
| 0x40e9ab | FUN_0040e9ab | 641 | config.h, main.c |
| 0x40ecc4 | FUN_0040ecc4 | 1099 | config.c, config.h, strutil.h |
| 0x40f1a0 | FUN_0040f1a0 | 1117 | engine.c, engine_screen_dispatch.c, engine_screen_dispatch.h (+1) |
| 0x40f5fd | FUN_0040f5fd | 470 | engine_screen_dispatch.c |
| 0x4148e4 | FUN_004148e4 | 264 | engine_title_render.c, engine_title_render.h, main.c |
| 0x4149ec | FUN_004149ec | 1050 | engine_title_render.c, engine_title_render.h, gfx_sprite.c (+1) |
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
| 0x423b4f | FUN_00423b4f | 85 | engine_mp_render.c, engine_mp_render.h |
| 0x425d35 | FUN_00425d35 | 184 | audio.c, main.c |
| 0x427ed0 | FUN_00427ed0 | 311 | engine_map_query.c, engine_territory.c, game_init.c (+1) |
| 0x428007 | FUN_00428007 | 113 | game_init.c, game_init.h |
| 0x428078 | FUN_00428078 | 113 | game_init.c, game_init.h |
| 0x42847c | FUN_0042847c | 82 | game_init.c, game_init.h |
| 0x4290d0 | FUN_004290d0 | 131 | engine_category_buckets.c, engine_category_buckets.h, engine_territory.c |
| 0x429153 | FUN_00429153 | 96 | engine_category_buckets.h, engine_territory.c |
| 0x4291b3 | FUN_004291b3 | 171 | engine_category_buckets.h, engine_territory.c |
| 0x42925e | FUN_0042925e | 81 | engine_category_buckets.c, engine_category_buckets.h |
| 0x429b1d | FUN_00429b1d | 135 | engine.c, engine_camera.c, engine_camera.h (+1) |
| 0x42a15f | FUN_0042a15f | 206 | engine_camera.c, engine_camera.h |
| 0x42dfe2 | FUN_0042dfe2 | 126 | main.c |
| 0x42e3ba | FUN_0042e3ba | 164 | main.c |
| 0x42ec91 | FUN_0042ec91 | 1207 | gfx.c, gfx.h, gfx_paint.h |
| 0x42f1df | FUN_0042f1df | 295 | gfx.c, main.c |
| 0x432746 | FUN_00432746 | 69 | engine_territory.c, engine_territory.h |
| 0x43278b | FUN_0043278b | 223 | engine_territory.c, engine_territory.h |
| 0x432b05 | FUN_00432b05 | 451 | engine_menu_input.c, engine_menu_input.h |
| 0x432cc8 | FUN_00432cc8 | 526 | engine_menu_input.c, main.c |
| 0x432ee6 | FUN_00432ee6 | 197 | engine_menu_input.c |
| 0x432fab | FUN_00432fab | 191 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433155 | FUN_00433155 | 569 | engine_menu_input.c |
| 0x433461 | FUN_00433461 | 208 | engine_menu_input.c |
| 0x4335f0 | FUN_004335f0 | 452 | engine_menu_input.c |
| 0x4337b4 | FUN_004337b4 | 337 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433905 | FUN_00433905 | 147 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433998 | FUN_00433998 | 139 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x433a23 | FUN_00433a23 | 77 | engine_menu_input.c, main.c |
| 0x433ba2 | FUN_00433ba2 | 287 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433cc1 | FUN_00433cc1 | 35 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x433ce4 | FUN_00433ce4 | 217 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4342f3 | FUN_004342f3 | 21 | engine_menu_input.c, engine_menu_input.h |
| 0x434308 | FUN_00434308 | 62 | engine_menu_input.c |
| 0x434346 | FUN_00434346 | 178 | engine_menu_input.c, engine_title_render.c, engine_title_render.h |
| 0x4343f8 | FUN_004343f8 | 36 | main.c |
| 0x43de70 | FUN_0043de70 | 1211 | engine.c, engine.h, engine_rate.c |
| 0x43e330 | FUN_0043e330 | 259 | engine.c, engine.h, engine_rate.c (+1) |
| 0x43e433 | FUN_0043e433 | 218 | engine.h, engine_rate.c |
| 0x43e50d | FUN_0043e50d | 180 | engine.h, engine_rate.c |
| 0x43f4b7 | FUN_0043f4b7 | 270 | engine.h, engine_state_hash.c |
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
| 0x4485a5 | FUN_004485a5 | 163 | engine_army_economy.c, engine_army_economy.h |
| 0x44c278 | FUN_0044c278 | 707 | engine_army_economy.c, engine_army_economy.h |
| 0x44c53b | FUN_0044c53b | 193 | engine_army_economy.c, engine_army_economy.h |
| 0x44c5fc | FUN_0044c5fc | 200 | engine_army_economy.c, engine_army_economy.h |
| 0x44cfe1 | FUN_0044cfe1 | 377 | engine_army_economy.c, engine_army_economy.h |
| 0x44d15a | FUN_0044d15a | 139 | engine_army_economy.c, engine_army_economy.h |
| 0x44d1e5 | FUN_0044d1e5 | 156 | engine_army_economy.c, engine_army_economy.h |
| 0x44d281 | FUN_0044d281 | 130 | engine_army_economy.c |
| 0x44d303 | FUN_0044d303 | 113 | engine_army_economy.c |
| 0x44d374 | FUN_0044d374 | 665 | engine_army_economy.c, engine_army_economy.h |
| 0x44d60d | FUN_0044d60d | 774 | engine_army_economy.c, engine_army_economy.h |
| 0x44d913 | FUN_0044d913 | 390 | engine_territory.c, engine_territory.h |
| 0x44da99 | FUN_0044da99 | 692 | engine_army_economy.c, engine_army_economy.h |
| 0x44dd4d | FUN_0044dd4d | 530 | engine_army_economy.c, engine_army_economy.h |
| 0x44df5f | FUN_0044df5f | 1628 | engine_army_economy.c, engine_army_economy.h |
| 0x44e5bb | FUN_0044e5bb | 72 | engine_army_economy.c, engine_army_economy.h |
| 0x44e603 | FUN_0044e603 | 80 | engine_army_economy.c, engine_army_economy.h |
| 0x44e653 | FUN_0044e653 | 80 | engine_army_economy.c, engine_army_economy.h |
| 0x44e7b4 | FUN_0044e7b4 | 158 | engine_army_economy.c, engine_army_economy.h |
| 0x44ef4e | FUN_0044ef4e | 525 | engine_army_economy.c, engine_army_economy.h |
| 0x44f15b | FUN_0044f15b | 237 | engine_army_economy.c, engine_army_economy.h |
| 0x44f248 | FUN_0044f248 | 208 | engine_army_economy.c, engine_army_economy.h |
| 0x44f318 | FUN_0044f318 | 897 | engine_army_economy.c, engine_army_economy.h |
| 0x44f6e7 | FUN_0044f6e7 | 2147 | engine_army_economy.c, engine_army_economy.h, engine_territory.c (+1) |
| 0x450e46 | FUN_00450e46 | 366 | engine_army_economy.c, engine_army_economy.h |
| 0x450fb4 | FUN_00450fb4 | 173 | engine_army_economy.c, engine_army_economy.h |
| 0x451f1a | FUN_00451f1a | 81 | engine.c, engine.h, engine_anim_cycle.c |
| 0x451f70 | FUN_00451f70 | 26 | gfx.c, main.c |
| 0x452160 | FUN_00452160 | 422 | engine.c, gfx.c, gfx.h (+1) |
| 0x452306 | FUN_00452306 | 224 | engine_dirty_rect.c, engine_dirty_rect.h, gfx.c (+1) |
| 0x4523e6 | FUN_004523e6 | 36 | engine_dirty_rect.h |
| 0x45240a | FUN_0045240a | 33 | engine_dirty_rect.h, engine_territory.c, engine_title_render.h (+3) |
| 0x452436 | FUN_00452436 | 83 | engine_dirty_rect.h |
| 0x452489 | FUN_00452489 | 83 | engine_dirty_rect.h |
| 0x4524dc | FUN_004524dc | 83 | engine_dirty_rect.h |
| 0x452582 | FUN_00452582 | 83 | engine_dirty_rect.h |
| 0x4525d5 | FUN_004525d5 | 83 | engine_dirty_rect.h |
| 0x452628 | FUN_00452628 | 56 | engine_dirty_rect.c, engine_dirty_rect.h |
| 0x4526b3 | FUN_004526b3 | 87 | engine_dirty_rect.c, engine_dirty_rect.h |
| 0x45270a | FUN_0045270a | 43 | engine_dirty_rect.h, save.h |
| 0x452735 | FUN_00452735 | 53 | engine_dirty_rect.h |
| 0x4527a6 | FUN_004527a6 | 101 | engine_battlefield_render.h, engine_button.c, engine_button.h (+2) |
| 0x4651b9 | FUN_004651b9 | 91 | engine_territory.c, engine_territory.h |
| 0x46733c | FUN_0046733c | 124 | engine_map_query.c, engine_map_query.h |
| 0x4676e0 | FUN_004676e0 | 144 | engine_territory.c, engine_territory.h |
| 0x467770 | FUN_00467770 | 525 | engine_territory.c, engine_territory.h, engine_title_render.c |
| 0x467a36 | FUN_00467a36 | 624 | engine_territory.c, engine_territory.h |
| 0x467ca6 | FUN_00467ca6 | 385 | engine_territory.c, engine_territory.h |
| 0x467fd1 | FUN_00467fd1 | 336 | engine_territory.c, engine_territory.h |
| 0x468121 | FUN_00468121 | 331 | engine_territory.c, engine_territory.h |
| 0x46826c | FUN_0046826c | 221 | engine_territory.c, engine_territory.h |
| 0x468c41 | FUN_00468c41 | 270 | engine_territory.c, engine_territory.h |
| 0x468d4f | FUN_00468d4f | 274 | engine_territory.c, engine_territory.h |
| 0x468e61 | FUN_00468e61 | 457 | engine_territory.c, engine_territory.h |
| 0x46902a | FUN_0046902a | 499 | engine_territory.c, engine_territory.h |
| 0x469b8d | FUN_00469b8d | 404 | engine_army_economy.c, engine_territory.c, engine_territory.h |
| 0x469d21 | FUN_00469d21 | 237 | engine_territory.c, engine_territory.h |
| 0x46ac22 | FUN_0046ac22 | 329 | engine_territory.c, engine_territory.h |
| 0x46c080 | FUN_0046c080 | 199 | engine_territory.c, engine_territory.h |
| 0x46c147 | FUN_0046c147 | 407 | engine_territory.c, engine_territory.h |
| 0x46c2de | FUN_0046c2de | 292 | engine_territory.c, engine_territory.h |
| 0x46c4a3 | FUN_0046c4a3 | 277 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x46c5b8 | FUN_0046c5b8 | 258 | engine_territory.c, engine_territory.h |
| 0x46cfbd | FUN_0046cfbd | 371 | engine_map_query.c, engine_map_query.h, game_init.c (+1) |
| 0x46d130 | FUN_0046d130 | 371 | engine_map_query.c, engine_map_query.h, game_init.c (+1) |
| 0x46d2a3 | FUN_0046d2a3 | 301 | engine_map_query.c, engine_map_query.h |
| 0x46d7f4 | FUN_0046d7f4 | 486 | engine_territory.c, engine_territory.h |
| 0x46d9da | FUN_0046d9da | 113 | engine_territory.c, engine_territory.h |
| 0x46da4b | FUN_0046da4b | 426 | engine_territory.c, engine_territory.h |
| 0x46dbf5 | FUN_0046dbf5 | 319 | engine_territory.c, engine_territory.h |
| 0x46dd34 | FUN_0046dd34 | 209 | engine_territory.c, engine_territory.h |
| 0x46de05 | FUN_0046de05 | 143 | engine_territory.c, engine_territory.h |
| 0x46de94 | FUN_0046de94 | 189 | engine_territory.c, engine_territory.h |
| 0x46df51 | FUN_0046df51 | 132 | engine_territory.c, engine_territory.h |
| 0x46dfd5 | FUN_0046dfd5 | 146 | engine_territory.c, engine_territory.h |
| 0x46e067 | FUN_0046e067 | 317 | engine_territory.c, engine_territory.h |
| 0x46e1b0 | FUN_0046e1b0 | 792 | engine_territory.c, engine_territory.h, game_init.c |
| 0x46ea28 | FUN_0046ea28 | 57 | game_init.c, game_init.h |
| 0x46ea61 | FUN_0046ea61 | 57 | game_init.c |
| 0x46ead0 | FUN_0046ead0 | 57 | engine_territory.c, game_init.c, game_init.h |
| 0x46eb09 | FUN_0046eb09 | 54 | game_init.c, game_init.h |
| 0x46f1dc | FUN_0046f1dc | 67 | game_init.c, game_init.h |
| 0x46f2ae | FUN_0046f2ae | 67 | game_init.c, game_init.h |
| 0x46f3cd | FUN_0046f3cd | 78 | game_init.c, game_init.h |
| 0x46f434 | FUN_0046f434 | 216 | engine_path.c, engine_path.h |
| 0x46ff43 | FUN_0046ff43 | 617 | engine_strat_cost.c, engine_strat_cost.h |
| 0x471c1f | FUN_00471c1f | 273 | engine_tile.c, engine_tile.h |
| 0x471d30 | FUN_00471d30 | 118 | engine_tile.c, engine_tile.h |
| 0x472392 | FUN_00472392 | 715 | engine_path.c, engine_path.h |
| 0x47265d | FUN_0047265d | 536 | engine_path.h |
| 0x472875 | FUN_00472875 | 151 | engine_path.h |
| 0x472ad0 | FUN_00472ad0 | 16 | engine_event_queue.c, engine_event_queue.h |
| 0x472ae0 | FUN_00472ae0 | 96 | engine_event_queue.c, engine_event_queue.h |
| 0x472b40 | FUN_00472b40 | 133 | engine_event_queue.c, engine_event_queue.h |
| 0x472bc5 | FUN_00472bc5 | 560 | engine_event_queue.c, engine_event_queue.h, engine_ranking.h |
| 0x472df5 | FUN_00472df5 | 81 | engine_event_queue.c, engine_event_queue.h |
| 0x47ed34 | FUN_0047ed34 | 103 | engine.c, engine_camera.c, engine_camera.h |
| 0x48ee46 | FUN_0048ee46 | 87 | engine_tile.c, engine_tile.h |
| 0x4917dd | FUN_004917dd | 599 | engine_tile.c, engine_tile.h, save.c |
| 0x4921e5 | FUN_004921e5 | 451 | engine_tile.c, engine_tile.h |
| 0x497ced | FUN_00497ced | 376 | engine_territory.c, engine_territory.h |
| 0x497e65 | FUN_00497e65 | 1025 | game_init.c, game_init.h |
| 0x499034 | FUN_00499034 | 197 | engine_menu_input.c, gfx.c, gfx.h |
| 0x49a453 | FUN_0049a453 | 302 | save.c, save.h |
| 0x49aa0e | FUN_0049aa0e | 2197 | engine_ranking.c, engine_ranking.h |
| 0x49b35e | FUN_0049b35e | 205 | game_init.c, game_init.h |
| 0x49b762 | FUN_0049b762 | 334 | engine.c, engine.h, engine_turn.c (+1) |
| 0x49bae9 | FUN_0049bae9 | 180 | engine_menu_input.c, engine_roster.c, engine_roster.h |
| 0x49bbe8 | FUN_0049bbe8 | 53 | engine_roster.c, engine_roster.h, engine_territory.c |
| 0x49bc1d | FUN_0049bc1d | 66 | engine_roster.c, engine_roster.h, engine_territory.c |
| 0x49bc5f | FUN_0049bc5f | 314 | engine_roster.c, engine_roster.h |
| 0x49caaa | FUN_0049caaa | 935 | engine_menu_input.c, engine_npc_schedule.c, engine_npc_schedule.h (+3) |
| 0x4a1c53 | FUN_004a1c53 | 537 | game_init.c, game_init.h |
| 0x4a1e6c | FUN_004a1e6c | 117 | engine_roster.c, engine_roster.h, game_init.c |
| 0x4a2584 | FUN_004a2584 | 157 | game_init.c, game_init.h |
| 0x4ac904 | FUN_004ac904 | 295 | game_init.c, game_init.h |
| 0x4ade93 | FUN_004ade93 | 339 | save.c, save.h |
| 0x4adfe6 | FUN_004adfe6 | 377 | save.c, save.h |
| 0x4ae539 | FUN_004ae539 | 169 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4ae5e2 | FUN_004ae5e2 | 96 | engine_menu_input.c, engine_title_render.c, engine_title_render.h (+1) |
| 0x4ae74d | FUN_004ae74d | 144 | game_init.c, game_init.h |
| 0x4ae8a9 | FUN_004ae8a9 | 191 | save.c, save.h |
| 0x4ae968 | FUN_004ae968 | 264 | save.c, save.h |
| 0x4aea70 | FUN_004aea70 | 506 | save.c, save.h |
| 0x4af502 | FUN_004af502 | 72 | io_path.c, io_path.h |
| 0x4af54a | FUN_004af54a | 112 | io.h, strutil.h |
| 0x4af5ba | FUN_004af5ba | 114 | strutil.h |
| 0x4af62c | FUN_004af62c | 73 | strutil.h |
| 0x4af675 | FUN_004af675 | 84 | strutil.h |
| 0x4af6c9 | FUN_004af6c9 | 67 | strutil.h |
| 0x4af8d9 | FUN_004af8d9 | 381 | config.c, gfx.c, gfx.h (+3) |
| 0x4afab9 | FUN_004afab9 | 897 | gfx_panel.c |
| 0x4afeed | FUN_004afeed | 1214 | gfx_text_label.c, gfx_text_label.h |
| 0x4b13ed | FUN_004b13ed | 274 | gfx_paint.c, gfx_paint.h |
| 0x4b191e | FUN_004b191e | 981 | engine.c, engine.h, engine_input.c (+2) |
| 0x4b20ed | FUN_004b20ed | 68 | engine.c, engine.h, engine_delta.c |
| 0x4b2258 | FUN_004b2258 | 797 | audio.h, engine.c, engine.h (+2) |
| 0x4b2636 | FUN_004b2636 | 130 | engine.c, engine.h, engine_fps.c (+1) |
| 0x4b26f8 | FUN_004b26f8 | 56 | main.c |
| 0x4b2730 | FUN_004b2730 | 143 | gfx.c, gfx.h, main.c |
| 0x4b27bf | FUN_004b27bf | 143 | engine.c, engine.h, engine_camera.h (+2) |
| 0x4b28cd | FUN_004b28cd | 85 | gfx.c, main.c |
| 0x4b2922 | FUN_004b2922 | 156 | gfx.c, gfx.h, main.c |
| 0x4b29be | FUN_004b29be | 2545 | main.c |
| 0x4b3f54 | FUN_004b3f54 | 38 | gfx_paint.h |
| 0x4b3f7a | FUN_004b3f7a | 41 | gfx_paint.h |
| 0x4b3fa3 | FUN_004b3fa3 | 61 | gfx_paint.h |
| 0x4b4056 | FUN_004b4056 | 47 | engine_overlay_render.c |
| 0x4b414a | FUN_004b414a | 109 | gfx_sprite.c, gfx_sprite.h |
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
| 0x4b4cb8 | FUN_004b4cb8 | 1659 | gfx_sprite.c, gfx_sprite.h |
| 0x4b5333 | FUN_004b5333 | 2997 | gfx_sprite.c, gfx_sprite.h |
| 0x4b99c0 | FUN_004b99c0 | 2222 | call_trace.h, engine.c, engine.h (+5) |
| 0x4ba26e | FUN_004ba26e | 1882 | engine.c, engine_button.c, engine_button.h (+3) |
| 0x4ba9c8 | FUN_004ba9c8 | 3832 | engine.c, engine_menu_input.c, engine_menu_input.h (+3) |
| 0x4bb8c0 | FUN_004bb8c0 | 84 | engine.c, engine_rate.c |
| 0x4bb978 | FUN_004bb978 | 614 | engine.c, engine.h, engine_rate.c |
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

## ported (36) — reimplemented, no host test yet

| VA | name | size | src |
|----|------|-----:|-----|
| 0x40490d | FUN_0040490d | 313 | engine.c |
| 0x408fcb | FUN_00408fcb | 96 | gfx.c, gfx.h |
| 0x425b10 | FUN_00425b10 | 549 | audio.c, audio.h |
| 0x425ded | FUN_00425ded | 94 | audio.c, audio.h |
| 0x426529 | FUN_00426529 | 11 | engine.c |
| 0x426d11 | FUN_00426d11 | 196 | audio.c |
| 0x427990 | FUN_00427990 | 779 | audio.h |
| 0x427d19 | FUN_00427d19 | 89 | audio.c, audio.h |
| 0x427d72 | FUN_00427d72 | 340 | audio.c |
| 0x42e5c0 | FUN_0042e5c0 | 1249 | gfx.c, gfx.h |
| 0x42eaa6 | FUN_0042eaa6 | 214 | gfx.c |
| 0x42eb7c | FUN_0042eb7c | 277 | gfx.c, gfx.h |
| 0x42f306 | FUN_0042f306 | 323 | gfx.c, gfx.h |
| 0x42f449 | FUN_0042f449 | 323 | gfx.c, gfx.h |
| 0x42ff10 | FUN_0042ff10 | 8140 | engine.c |
| 0x440666 | FUN_00440666 | 383 | engine.c |
| 0x4782c5 | FUN_004782c5 | 340 | engine.c |
| 0x481b9a | FUN_00481b9a | 1399 | engine.c |
| 0x482111 | FUN_00482111 | 476 | engine.c |
| 0x4978ad | FUN_004978ad | 129 | engine.c, engine.h |
| 0x49792e | FUN_0049792e | 262 | engine.c, engine.h |
| 0x497a34 | FUN_00497a34 | 697 | engine.c, engine.h |
| 0x498458 | FUN_00498458 | 132 | engine.c |
| 0x499a1c | FUN_00499a1c | 174 | engine.c, gfx.c, gfx.h |
| 0x49a010 | FUN_0049a010 | 982 | engine.c |
| 0x49a3e6 | FUN_0049a3e6 | 109 | engine.c |
| 0x49b8b0 | FUN_0049b8b0 | 195 | engine.c |
| 0x4b0aaa | FUN_004b0aaa | 11 | engine.c |
| 0x4b0ab5 | FUN_004b0ab5 | 212 | gfx.c, gfx.h |
| 0x4b0e03 | FUN_004b0e03 | 875 | engine.c |
| 0x4b11ce | FUN_004b11ce | 34 | gfx.c |
| 0x4b12fb | FUN_004b12fb | 21 | engine.c |
| 0x4b1de0 | FUN_004b1de0 | 11 | engine.c |
| 0x4b25b0 | FUN_004b25b0 | 76 | gfx.c, gfx.h |
| 0x4b26b8 | FUN_004b26b8 | 64 | engine.c, gfx.c |
| 0x4b284e | FUN_004b284e | 122 | gfx.h |

