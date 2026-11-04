# rl render — the unported screen renders on retail's layout (2026-11-04)

Partition `render` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`; the gfx layer under it:
`render-under-rhost.md`): the screen dispatcher 0x40f1a0's remaining arms, the territory / county info panel, the
frame driver's HUD draws, the MP and battlefield debug HUDs, the movie / screen-transition tails, a few panels.
47 functions, 33103 B, all in `src/rl_render.c`; rows in `tools/rhost/rh_hooks_rl_render.c` (every one RH_SH_MEDIA:
state AND every surface pixel compared per call).

## Status (2026-11-04)

All 47 ported. `rhost.py verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (51 scenarios, with the io
partition's 70 rows): **117/117 reached with 0 divergences; hybrid PASS** — the 47 render rows: 17.87 M calls
shadowed (state AND every surface pixel, RH_SH_MEDIA, no sampling), 0 divergences; 0x4788c6's 13 calls are
"skipped" (its season-sheet load 0x4984dc grows the arena), the hybrid covers them. Pass 1 ~26 min (-j 3), the
hybrid 2 min. The per-frame HUD rows (14 of them, 1.23 M calls each) mostly take their gate's early return in the
corpus: see the block coverage below for what each draw path actually exercised. After the exploration fix (§ below)
the hybrid alone was re-run: `corpus --check --fb-hash --hook replace:<all 117 rl rows>`: PASS 51/51.

## How the ports draw

- **The gfx ports by name** (`gfx_line_*`, `gfx_panel_*`, `gfx_sprite_*`, `gfx_text_*`, `gfx_text_label_*`,
  `gfx_wrap_*`, `gfx_caret_render`, `gfx_paint_2x1`): they run on retail's globals (`gfx_globals.h`, aliased by
  `rh_hooks_gfx.c`). Retail's blobs by address: the fonts `&DAT_005af8f0` / `&DAT_005b2fa0` / `&DAT_005cbfb0`, the chrome
  sheet `&DAT_0057d3d0`, the seal sheet `&DAT_005bb540`, the char tables `DAT_004d71d0` (measure) / `+0x20` (render).
- **Three gfx ports take state structs** — the helpers at the top of `rl_render.c` load them from retail's globals
  and store them back: the localization walker (`localization_state_t` over the blob `DAT_00591580` and the pointer
  `DAT_005c9270`), the caret's editor + measurer cache (`DAT_005cd550` / `DAT_005bb4a8` / `DAT_005c9280`,
  `DAT_005c9288` / `DAT_0058fd88`), the dirty rectangle (FUN_004527a6's six globals: the MIRROR
  `engine_territory_dirty_state()`, rh_hooks_ai.c's `k_mirror_va`).
- **Binding** (`rh_hooks_rl_render.c`, a constructor): the gfx layer's three port-side bindings (default font, tall font,
  the sprite renderers' error log → retail 0x4afab9) are set here too — with `--hooks '^rl_'` the gfx table's rows
  (whose first call binds them) are not installed.
- **Globals**: `src/rl_render_globals.{h,c}` + `tools/rhost/rh_rl_render_alias.h`, generated
  (`gen_skeleton.py --part-globals render`) and hand-edited (marked "hand"): the block the partition reaches only
  through the gfx structs (the localization blob + walker, the char tables, the seal sheet, the editor buffer +
  insert flag), and two blocks grown to what the ports index (the workforce categories' 20-byte records at
  DAT_004d2b70, the per-player troop table DAT_0053f690 + 128 p up to g_per_army_768). Other partitions' globals are
  used through their headers (`rl_io_globals.h`, `engine_bf_input_globals.h`); the save block, `g_gfx_*`,
  `g_combat_*`, `g_engine_state`, `g_sim_mp_active`, `g_castle_tier`, `g_sim_turn_progress` through their owners; the
  screen byte / demand-redraw through `engine_current_screen()` / `engine_set_current_screen()` /
  `engine_set_demand_redraw()`; DAT_0056d6a0 (a mirror) through `engine_army_ai_set_ranking_dirty()`.

## Rows

HOOK_V everywhere a caller discards EAX (the screen dispatcher, the frame driver and the input dispatcher do);
HOOK_R for 0x438e3b (the input dispatcher tests it), 0x4788c6 (0x43be65 / 0x477dfc / 0x4782c5 return it on) and
0x4233f7 (0x434d77 stores only AL over it and returns it). 0x42e060 plays a movie at the title (0x42d91b): its row
is HOOK_SMK_V, a copy of rh_hooks_rl_io.c's HOOK_SMK (file-static there) — smackw32's writable pages and the live disk
handles' positions are put back before the port side, so its SmackOpen gets retail's heap block (DAT_004d4d58).

## What the shadow found (fixed)

| # | function | divergence | cause |
|---|---|---|---|
| 1 | FUN_0041a166 subtitles | `005c9270 r00591c06/p005a952b` (3156/3156, boot-movies) | the port's `localization_read` (0x4017bf) moves the state's walker; retail's walks a LOCAL pointer and leaves DAT_005c9270 alone — the helper stored it back. Now not stored. |
| 2 | FUN_0042e060 movie tail | `004d4d58 r190002c0/p19015e50` (4/6) | the port side's SmackOpen (0x42d91b → imptitle / credits.smk) got another smackw32 heap block: the row needed HOOK_SMK's host-layer rewind. |

## Path coverage (block level) and exploration

`rhost --coverage` arms a one-shot INT3 per listed address; listing every basic-block start of the 47 functions (jump
targets + conditional fall-throughs from all_ida.asm: 791 blocks) instead of the function entries gives block
coverage (a wrapper around rhost.py's coverage command; retail-only, ~3 min for the corpus):
**442 / 791 blocks reached by the corpus**. Per function (reached / blocks, + reached only by an exploration probe):

40c725 12/13 · 40cd58 3/6 · 410c71 1/1 · 410f4b 26/28 · 4120e0 1/1 · 412421 16/25 · 412795 3/26 +6 · 41298a 13/16 +3 ·
41438c 25/25 · 414819 1/1 · 414f68 10/13 · 41515c 7/10 · 4152ea 6/9 · 41608b 3/14 +11 · 417aef 1/1 · 417bd3 2/3 ·
417cef 7/7 · 4192b1 1/1 · 419354 10/16 · 419789 8/8 · 4198aa 8/8 · 41a166 7/45 · 41a639 12/15 · 41a844 3/13 +2 ·
41b19d 35/63 +25 · 41c996 7/11 · 41d299 18/41 · 41da2f 14/18 +1 · 41e1dd 11/17 · 41ff75 3/24 · 420316 3/6 +3 ·
421f14 19/24 · 4224e7 27/27 · 422d22 15/18 · 422ff1 13/13 · 423241 4/6 · 4233f7 5/6 · 423530 23/24 · 423739 23/29 ·
423ba4 3/48 +23 · 42476b 3/9 +6 · 4248a3 3/4 +1 · 424992 3/6 +2 · 42e060 10/33 · 438e3b 10/38 · 44098f 3/20 +15 ·
4788c6 1/1.

**Exploration probes** (rhbot: replay a scenario with the rows in shadow, poke retail memory at a frame, step —
poked state, NOT a pure replay; evidence for paths the corpus never takes): the workforce tooltip (0x41608b: the
500-frame countdown never runs out in the corpus), the MP chat line (0x420316), the MP checksum overlay (0x44098f),
the debug counters (0x4248a3), the MP network panel + lag light + message flag (0x423ba4 / 0x41a844 / 0x42476b, one
frame with DAT_00553030 = 1), the unit inspector (0x424992), the battlefield drag box's three clips (0x41298a), the
territory panel's kinds 2 / 3 / 4, a foreign county and the castle / growth variants (0x41b19d), the castle box
without a builder (0x41da2f) — all 0 divergences (state + pixels). The MP panel probe first CRASHED the port: an
`AT()` on the scalar `g_bf_53e8d0` (its value taken as the base) — fixed (`&g_bf_53e8d0`); no corpus path reaches it.
Still unexercised: the subtitles (0x41a166, non-English data only), the title movie chain past intro.smk and the
in-game transition arms of 0x42e060, the labour-panel click (0x438e3b), the MP chat log (0x41ff75), the county
drag box (0x412795 on screens 5 / 6), the siege row of 0x419354, the number box's modes (0x40cd58), the options rows'
alternatives (0x414f68 / 0x41515c / 0x4152ea), parts of the county people box (0x41d299).

## Lessons (for the other render partitions)

- `AT(T, sym, symva, va)` casts `sym` to a pointer: on a SCALAR symbol (`int32_t g_bf_53e8d0`) it takes the value as
  the base — write `AT(T, &sym, …)`. A generated scalar can still be the head of an indexed table.
- The gfx layer's `localization_read` (0x4017bf) moves its state's walker, retail's does not move DAT_005c9270: do not
  store the walker back after it (the wrap renderers DO move it — store after those).
- A port that calls a linked gfx renderer with a seam bound by another table (engine_button's pressed mark) runs that
  table's seam: check what state the seam assumes (here the gfx table's own dirty copy, not the mirror).
- Per-frame rows without sampling cost ~10 % of a verify pass on wine; block coverage (above) is the honest measure
  of what a 1.2 M-call row verified.

## Quirks (retail ground truth)

1. 0x414f68: the sound panel's fourth row shows "on" for DAT_0053f284 == 0 — the flag is the quick-anim
   SUPPRESSOR, so its on / off text is inverted against the other three rows.
2. 0x419354: the siege row's noun takes its count from the entry's FIRST dword even on the second-dword path (where
   that dword is 0): always the plural.
3. 0x41298a: the battlefield drag box clips right (x + w < 480) and top / bottom, never left — a drag from x < 0 keeps
   a negative x (the county one, 0x412795, clips x < 0).
4. 0x40c725: the menu box height is `21 n / 16 + 2` truncated to the WORD DAT_005aeb70 and re-read (movsx) for the box.
5. 0x417cef: stores byte_4EB260 = 0, calls the empty 0x4b1de0, then stores 2 (a dead store pair around a no-op).
6. 0x4b1de0 (called by 0x40cd58 / 0x414819 / 0x417cef / 0x4192b1 / 0x421f14 / 0x422d22 / 0x422ff1 / 0x423241) is an
   empty function; its calls are elided in the port.
7. 0x4224e7: a mode other than 0 / 1 / 2 returns an uninitialized EAX (no caller passes one).
8. 0x423ba4: the sync colour's first test is a chained boolean compare `((b44 == b3a) == b4e) == b4e` (0/1 results
   compared with a byte), not "all equal"; the realm ticks' colour is the realm's +8 byte only while its +4 byte is set
   (IDA prints these as the unk_57C064.. pairs: realm p's +4 / +8).
9. 0x41608b: the category tooltip's second number is N + max(1, N x the territory's +0x166 %) — except category 4,
   which adds 0.
10. 0x41b19d: the territory panel's ownership line index is 23 / 24 (own territory) or 25 / 24 (another's); a 26 the
   compiler stores first is always overwritten (dead store).
11. 0x42e060: its one argument is unused; the return screen is DAT_005169b0's LOW BYTE.
12. 0x41a166 (subtitles): compares the localization's string 300 with "English" — the English build never draws them;
   the corpus (English data) only exercises the compare.
13. 0x41d299: the population's colour is 249 below +0xd4, else 252 above +0xd8, else 63 (one call; IDA splits it).

## Seams left

- TODO(rl-link) — a port exists but is not on retail's layout / not linked in rhost (retail through
  `engine_retail_call`): text_input_recompute_length 0x401f49, text_input_copy_out 0x40210c, gfx_load_background_image
  0x408fcb, the chrome panel+fill 0x409397 (statics in engine_armory / county_panels / labor / terrain_info),
  engine_screen_strat_render 0x40f5fd, engine_minimap_render 0x410aa9, engine_minimap_plot 0x410cbd,
  engine_workforce_draw_all_rows 0x412666, engine_labor_render 0x412b33, engine_title_render_filelist_content
  0x4149ec, engine_hud_menubar_render_chrome 0x419c78 (the linked port takes its state as arguments),
  county_year_number 0x41a900, labor_num_units 0x41ab67, armory_unit_noun 0x41ac3e, labor_castle_block 0x41dedb,
  engine_map_init_build_cells 0x4298c1, min_tick_filtered 0x43e330, engine_state_hash 0x44015a,
  engine_map_init_set_camera 0x451fcc, gfx.c's present 0x452160, engine_territory_classify_feature_tiles 0x46dbf5 /
  _randomise_special_tiles 0x46dd34 (the linked ports take the rng), engine_popup_scroll_dismiss 0x476768,
  engine_pl8's season loader 0x4984dc, io_path_chdir_base 0x4af502, io_read_file_at 0x4af8d9, gfx.c's palette set
  0x4b0ab5, engine_screen_strat's clear 0x4b1867, gfx_load_panel_sheet 0x4b25b0, engine_workforce_hud
  restore / capture 0x4b3ec0 / 0x4b3f0a, engine_overlay_render's 4-byte fill 0x4b4056, engine_battlefield_render
  0x4bc1d1 / 0x4bc2a5 / 0x4bc2c5 / 0x4bc505.
- engine_button_render_list 0x40cfd2 (linked, retail-layout) is called through retail from 0x41da2f on purpose: its
  pressed-button mark seam (`engine_button_bind_pressed_mark`) is bound by rh_hooks_gfx.c to the gfx table's OWN copy
  of the dirty rectangle (bd_load / bd_store), not the mirror a port runs on — inside another port it would store a
  stale copy to retail's globals. Shared fix (rh_hooks_gfx.c): the seam should store / reload
  `engine_territory_dirty_state()` (rh_port_retail_enter does) when called from a port.
- TODO(rl-extern) — unported: 0x42563c (the MP checksum overlay's tail), 0x42e45e (the subtitle box), 0x4b743b
  (the MP session end).
- By name, still pending in their partitions (stubs → retail): glue rl_409429 / rl_40ae12 / rl_46c4a3 / rl_47703a /
  rl_4b1310; extern bf_43f304 / bf_4987b7.
- Linked ports called directly: engine_dirty_rect (mark_box_tiled, mark_tile_80x48_noclamp) on the mirror,
  engine_territory_mark_full_screen_dirty, engine_math_percent_of, engine_unit_absorb, localization_read, the rl_io
  ports (rl_4b11ce, rl_4263ad, rl_42d91b, rl_4996c8, rl_499aca, rl_499d7d, rl_4afab9), bf_4238b8 / bf_4239d5 /
  bf_480f8b, the platform shim's timeGetTime.
- Callers outside the partition that still reach these functions through retail: rl_io's 0x42dbc7 calls 0x41a166 /
  0x42e060 by VA (its TODO(rl-extern)) — they can call rl_41a166 / rl_42e060 now.
