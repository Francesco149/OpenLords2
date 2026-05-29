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
- touched: **283** (11.6%) — tested 253, ported 30
- code bytes touched: **10.5%** (88,275 / 838,629 B)
- unported: **2164**
- orphan refs (in src/, not in function table): 3

## tested (253) — ported + host unit suite

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
| 0x4015b9 | FUN_004015b9 | 173 | font_metrics.c, font_metrics.h, text_input.h |
| 0x401666 | FUN_00401666 | 141 | strutil.h |
| 0x4016f3 | FUN_004016f3 | 204 | localization.c, localization.h |
| 0x4017bf | FUN_004017bf | 187 | localization.c, localization.h |
| 0x40187a | FUN_0040187a | 93 | localization.c, localization.h |
| 0x4018d7 | FUN_004018d7 | 173 | localization.c, localization.h |
| 0x401984 | FUN_00401984 | 156 | text_input.c, text_input.h |
| 0x401a20 | FUN_00401a20 | 543 | text_input.h |
| 0x401c3f | FUN_00401c3f | 28 | text_input.h |
| 0x401c70 | FUN_00401c70 | 35 | text_input.h |
| 0x401c93 | FUN_00401c93 | 16 | text_input.h |
| 0x401ca3 | FUN_00401ca3 | 25 | text_input.h |
| 0x401cbc | FUN_00401cbc | 30 | text_input.h |
| 0x401cda | FUN_00401cda | 34 | text_input.h |
| 0x401cfc | FUN_00401cfc | 21 | text_input.h |
| 0x401d11 | FUN_00401d11 | 21 | text_input.h |
| 0x401d26 | FUN_00401d26 | 162 | text_input.h |
| 0x401dc8 | FUN_00401dc8 | 64 | text_input.h |
| 0x401e49 | FUN_00401e49 | 135 | text_input.h |
| 0x401ed0 | FUN_00401ed0 | 121 | text_input.h |
| 0x401f49 | FUN_00401f49 | 70 | text_input.h |
| 0x401f8f | FUN_00401f8f | 122 | font_metrics.c, font_metrics.h, text_input.c (+1) |
| 0x402009 | FUN_00402009 | 165 | text_input.h |
| 0x4020ae | FUN_004020ae | 94 | save.h, text_input.h |
| 0x40210c | FUN_0040210c | 90 | text_input.h |
| 0x4022bd | FUN_004022bd | 394 | gfx_text_label.c, gfx_text_label.h |
| 0x402447 | FUN_00402447 | 400 | gfx_text_label.c, gfx_text_label.h |
| 0x4025d7 | FUN_004025d7 | 96 | gfx_text.c, gfx_text.h |
| 0x402637 | FUN_00402637 | 989 | gfx_text.c, gfx_text.h, gfx_wrap.c (+1) |
| 0x402a14 | FUN_00402a14 | 514 | gfx_dispatch.c, gfx_dispatch.h, gfx_glyph.h |
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
| 0x404442 | FUN_00404442 | 256 | gfx_dispatch.c, gfx_dispatch.h |
| 0x404547 | FUN_00404547 | 256 | gfx_dispatch.c, gfx_dispatch.h |
| 0x40464c | FUN_0040464c | 303 | engine_battlefield_render.h, gfx_dispatch.c, gfx_dispatch.h (+1) |
| 0x40477b | FUN_0040477b | 291 | engine_battlefield_render.h, gfx_dispatch.c, gfx_dispatch.h |
| 0x404a46 | FUN_00404a46 | 230 | engine.c, engine.h, engine_rng.c |
| 0x404b99 | FUN_00404b99 | 466 | engine.c, engine.h, engine_rng.c |
| 0x404d6b | FUN_00404d6b | 43 | engine_input.c, engine_math.c, engine_math.h |
| 0x404d96 | FUN_00404d96 | 43 | engine_math.h |
| 0x404dc1 | FUN_00404dc1 | 66 | engine_input.c, engine_math.h |
| 0x404e03 | FUN_00404e03 | 72 | engine_math.h |
| 0x404e4b | FUN_00404e4b | 98 | engine_math.h |
| 0x404ead | FUN_00404ead | 159 | engine_math.h, engine_path.h |
| 0x404f4c | FUN_00404f4c | 180 | engine_path.h |
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
| 0x408fb0 | FUN_00408fb0 | 27 | gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a01d | FUN_0040a01d | 261 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a21a | FUN_0040a21a | 433 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h (+1) |
| 0x40a3d0 | FUN_0040a3d0 | 402 | gfx_sprite_render.c, gfx_sprite_render.h, gfx_sprite_rle.h |
| 0x40a682 | FUN_0040a682 | 391 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a80e | FUN_0040a80e | 413 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40a9b0 | FUN_0040a9b0 | 439 | gfx_dispatch.h, gfx_sprite_render.c, gfx_sprite_render.h |
| 0x40da1e | FUN_0040da1e | 869 | engine_delta.c |
| 0x40e890 | FUN_0040e890 | 43 | engine.c, engine.h, main.c |
| 0x40e8bb | FUN_0040e8bb | 240 | engine.c, engine.h, main.c |
| 0x40e9ab | FUN_0040e9ab | 641 | config.h, main.c |
| 0x40ecc4 | FUN_0040ecc4 | 1099 | config.c, config.h, strutil.h |
| 0x41ea14 | FUN_0041ea14 | 143 | engine_title_render.c, engine_title_render.h |
| 0x41eaa3 | FUN_0041eaa3 | 487 | engine_title_render.c, engine_title_render.h |
| 0x41f15a | FUN_0041f15a | 131 | engine_mp_render.c, engine_mp_render.h |
| 0x423b4f | FUN_00423b4f | 85 | engine_mp_render.c, engine_mp_render.h |
| 0x425d35 | FUN_00425d35 | 184 | main.c |
| 0x4290d0 | FUN_004290d0 | 131 | engine_category_buckets.c, engine_category_buckets.h |
| 0x429153 | FUN_00429153 | 96 | engine_category_buckets.h |
| 0x4291b3 | FUN_004291b3 | 171 | engine_category_buckets.h |
| 0x42925e | FUN_0042925e | 81 | engine_category_buckets.c, engine_category_buckets.h |
| 0x429b1d | FUN_00429b1d | 135 | engine.c, engine_camera.c, engine_camera.h |
| 0x42a15f | FUN_0042a15f | 206 | engine_camera.c, engine_camera.h |
| 0x42dfe2 | FUN_0042dfe2 | 126 | main.c |
| 0x42e3ba | FUN_0042e3ba | 164 | main.c |
| 0x42ec91 | FUN_0042ec91 | 1207 | gfx.c, gfx.h, gfx_paint.h |
| 0x42f1df | FUN_0042f1df | 295 | gfx.c, main.c |
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
| 0x451f1a | FUN_00451f1a | 81 | engine.c, engine.h, engine_anim_cycle.c |
| 0x451f70 | FUN_00451f70 | 26 | main.c |
| 0x452160 | FUN_00452160 | 422 | engine.c, gfx.c, gfx.h (+1) |
| 0x452306 | FUN_00452306 | 224 | engine_dirty_rect.c, engine_dirty_rect.h, gfx.c (+1) |
| 0x4523e6 | FUN_004523e6 | 36 | engine_dirty_rect.h |
| 0x45240a | FUN_0045240a | 33 | engine_dirty_rect.h, engine_title_render.h, gfx.c (+2) |
| 0x452436 | FUN_00452436 | 83 | engine_dirty_rect.h |
| 0x452489 | FUN_00452489 | 83 | engine_dirty_rect.h |
| 0x4524dc | FUN_004524dc | 83 | engine_dirty_rect.h |
| 0x452582 | FUN_00452582 | 83 | engine_dirty_rect.h |
| 0x4525d5 | FUN_004525d5 | 83 | engine_dirty_rect.h |
| 0x452628 | FUN_00452628 | 56 | engine_dirty_rect.c, engine_dirty_rect.h |
| 0x4526b3 | FUN_004526b3 | 87 | engine_dirty_rect.c, engine_dirty_rect.h |
| 0x45270a | FUN_0045270a | 43 | engine_dirty_rect.h, save.h |
| 0x452735 | FUN_00452735 | 53 | engine_dirty_rect.h |
| 0x4527a6 | FUN_004527a6 | 101 | engine_battlefield_render.h, engine_dirty_rect.h |
| 0x4651b9 | FUN_004651b9 | 91 | engine_territory.c, engine_territory.h |
| 0x46c4a3 | FUN_0046c4a3 | 277 | engine_battlefield_render.c, engine_battlefield_render.h |
| 0x46d2a3 | FUN_0046d2a3 | 301 | engine_map_query.c, engine_map_query.h |
| 0x46f434 | FUN_0046f434 | 216 | engine_path.c, engine_path.h |
| 0x471c1f | FUN_00471c1f | 273 | engine_tile.c, engine_tile.h |
| 0x471d30 | FUN_00471d30 | 118 | engine_tile.c, engine_tile.h |
| 0x472392 | FUN_00472392 | 715 | engine_path.c, engine_path.h |
| 0x47265d | FUN_0047265d | 536 | engine_path.h |
| 0x472875 | FUN_00472875 | 151 | engine_path.h |
| 0x472ad0 | FUN_00472ad0 | 16 | engine_event_queue.c, engine_event_queue.h |
| 0x472ae0 | FUN_00472ae0 | 96 | engine_event_queue.c, engine_event_queue.h |
| 0x472b40 | FUN_00472b40 | 133 | engine_event_queue.c, engine_event_queue.h |
| 0x472bc5 | FUN_00472bc5 | 560 | engine_event_queue.c, engine_event_queue.h |
| 0x472df5 | FUN_00472df5 | 81 | engine_event_queue.c, engine_event_queue.h |
| 0x47ed34 | FUN_0047ed34 | 103 | engine.c, engine_camera.c, engine_camera.h |
| 0x48ee46 | FUN_0048ee46 | 87 | engine_tile.c, engine_tile.h |
| 0x4917dd | FUN_004917dd | 599 | engine_tile.c, engine_tile.h, save.c |
| 0x4921e5 | FUN_004921e5 | 451 | engine_tile.c, engine_tile.h |
| 0x49a453 | FUN_0049a453 | 302 | save.c, save.h |
| 0x49b762 | FUN_0049b762 | 334 | engine.c, engine.h, engine_turn.c (+1) |
| 0x49bae9 | FUN_0049bae9 | 180 | engine_roster.c, engine_roster.h |
| 0x49bbe8 | FUN_0049bbe8 | 53 | engine_roster.c, engine_roster.h |
| 0x49bc1d | FUN_0049bc1d | 66 | engine_roster.c, engine_roster.h |
| 0x49bc5f | FUN_0049bc5f | 314 | engine_roster.c, engine_roster.h |
| 0x49caaa | FUN_0049caaa | 935 | engine_npc_schedule.c, engine_npc_schedule.h |
| 0x4ade93 | FUN_004ade93 | 339 | save.c, save.h |
| 0x4adfe6 | FUN_004adfe6 | 377 | save.c, save.h |
| 0x4ae8a9 | FUN_004ae8a9 | 191 | save.c, save.h |
| 0x4ae968 | FUN_004ae968 | 264 | save.c, save.h |
| 0x4aea70 | FUN_004aea70 | 506 | save.c, save.h |
| 0x4af54a | FUN_004af54a | 112 | io.h, strutil.h |
| 0x4af5ba | FUN_004af5ba | 114 | strutil.h |
| 0x4af62c | FUN_004af62c | 73 | strutil.h |
| 0x4af675 | FUN_004af675 | 84 | strutil.h |
| 0x4af6c9 | FUN_004af6c9 | 67 | strutil.h |
| 0x4af8d9 | FUN_004af8d9 | 381 | config.c, gfx.c, io.c (+2) |
| 0x4afeed | FUN_004afeed | 1214 | gfx_text_label.c, gfx_text_label.h |
| 0x4b191e | FUN_004b191e | 981 | engine.c, engine.h, engine_input.c (+1) |
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
| 0x4b41b7 | FUN_004b41b7 | 165 | gfx_glyph.h |
| 0x4b425c | FUN_004b425c | 105 | gfx_glyph.c, gfx_glyph.h |
| 0x4b42c5 | FUN_004b42c5 | 105 | gfx_glyph.c, gfx_glyph.h |
| 0x4b43b1 | FUN_004b43b1 | 188 | gfx_sprite.c, gfx_sprite.h |
| 0x4b446d | FUN_004b446d | 101 | gfx_sprite.c, gfx_sprite.h |
| 0x4b44d2 | FUN_004b44d2 | 101 | gfx_sprite.c, gfx_sprite.h |
| 0x4b4537 | FUN_004b4537 | 733 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b4814 | FUN_004b4814 | 164 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b48b8 | FUN_004b48b8 | 162 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b495a | FUN_004b495a | 93 | gfx_sprite_rle.c, gfx_sprite_rle.h |
| 0x4b49b7 | FUN_004b49b7 | 64 | engine_battlefield_render.c, gfx_sprite.c, gfx_sprite.h |
| 0x4b49f7 | FUN_004b49f7 | 705 | gfx_sprite.c, gfx_sprite.h |
| 0x4b5333 | FUN_004b5333 | 2997 | gfx_sprite.c, gfx_sprite.h |
| 0x4b99c0 | FUN_004b99c0 | 2222 | call_trace.h, engine.c, engine.h (+4) |
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

## ported (30) — reimplemented, no host test yet

| VA | name | size | src |
|----|------|-----:|-----|
| 0x40490d | FUN_0040490d | 313 | engine.c |
| 0x425b10 | FUN_00425b10 | 549 | audio.c, audio.h |
| 0x425ded | FUN_00425ded | 94 | audio.c, audio.h |
| 0x426529 | FUN_00426529 | 11 | engine.c |
| 0x426d11 | FUN_00426d11 | 196 | audio.c |
| 0x427d72 | FUN_00427d72 | 340 | audio.c |
| 0x42e5c0 | FUN_0042e5c0 | 1249 | gfx.c, gfx.h |
| 0x42eaa6 | FUN_0042eaa6 | 214 | gfx.c |
| 0x42eb7c | FUN_0042eb7c | 277 | gfx.c, gfx.h |
| 0x42f306 | FUN_0042f306 | 323 | gfx.c |
| 0x42f449 | FUN_0042f449 | 323 | gfx.c |
| 0x440666 | FUN_00440666 | 383 | engine.c |
| 0x4782c5 | FUN_004782c5 | 340 | engine.c |
| 0x481b9a | FUN_00481b9a | 1399 | engine.c |
| 0x482111 | FUN_00482111 | 476 | engine.c |
| 0x4978ad | FUN_004978ad | 129 | engine.c, engine.h |
| 0x49792e | FUN_0049792e | 262 | engine.c, engine.h |
| 0x497a34 | FUN_00497a34 | 697 | engine.c, engine.h |
| 0x498458 | FUN_00498458 | 132 | engine.c |
| 0x499a1c | FUN_00499a1c | 174 | engine.c |
| 0x49a010 | FUN_0049a010 | 982 | engine.c |
| 0x49a3e6 | FUN_0049a3e6 | 109 | engine.c |
| 0x49b8b0 | FUN_0049b8b0 | 195 | engine.c |
| 0x4b0aaa | FUN_004b0aaa | 11 | engine.c |
| 0x4b0ab5 | FUN_004b0ab5 | 212 | gfx.c, gfx.h |
| 0x4b0e03 | FUN_004b0e03 | 875 | engine.c |
| 0x4b12fb | FUN_004b12fb | 21 | engine.c |
| 0x4b1de0 | FUN_004b1de0 | 11 | engine.c |
| 0x4b26b8 | FUN_004b26b8 | 64 | engine.c, gfx.c |
| 0x4b284e | FUN_004b284e | 122 | gfx.h |

