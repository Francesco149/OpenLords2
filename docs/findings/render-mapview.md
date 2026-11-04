# Render under rhost — the strategic map view (batch "mapview", 2026-11-04)

Follows docs/findings/render-under-rhost.md (the gfx layer on retail's layout, the media shadow). The screen
renderers of the strategic map — the map scene driver, its three passes, the minimap, the county panel — kept their
state in file-statics and their inputs in setter binds; now they run on retail's layout and each ported function has
an rhost row (`tools/rhost/rh_hooks_mapview.c`) whose shadow compares state AND every surface pixel.

## Result

- **64 rows** in `tools/rhost/rh_hooks_mapview.c`: the scene driver FUN_004050c0, the terrain pass (frame, two row
  iterators, drawers A / B, three empty-cell drawers, 24 unrolled blitters / strips over the three view modes), the
  feature walker (4), the overlay pass (frame, rows, army / city / label / dev overlay, FUN_004b4056), the minimap (3),
  the county panel (FUN_0040f7d3, the queue builder, 8 subdrawers, the End-turn strip) and the screen-4 terrain panel
  (FUN_0041b032 / 0041befe / 0041c208 / 0041c996 / 00409397). Every row RH_SH_MEDIA | RH_SH_SAMPLE.
- `RHOST_JOBS=3 rhost.py verify --target wine -j 3 --hooks '^mv_'` over the whole corpus (51 scenarios): **45 reached,
  0 divergences** — 442,524 calls compared on identical state AND identical pixels, 799,817,316 more run retail-only by
  sampled-out passes; 19 never called (§6). Four shadow passes (301 / 96 / 92 / 91 s), the hybrid 149 s.
- **Hybrid PASS**: the 64 rows replaced at once, `corpus --check` (state hash + every presented frame) on all 51; and
  **every row of every table at once (606: AI, combat, battle, events, economy, the battlefield partitions, rl, gfx,
  mapview)** — `corpus --check --fb-hash` PASS on all 51.
- The pixel compare is live for these rows: `--shadow-poison-px 100,100` on endturn-2 makes every shadowed call of
  `mv_blit_452820` / `mv_feature_tile` / `mv_overlay_army` diverge (142/142, 858/858, 170/170 —
  `px(100,100)@6005aa64 r00/p01 (1 px differ)`); without the poison 0.
- 16 port bugs fixed (§4) — read in the asm while porting, so every row then ran at 0 divergences on its first shadow;
  the blitters by construction (§2). One tool: `tools/gen_mapblit.py`.

## 1. State mapping

`src/engine_mapview_globals.h` / `.c` hold the retail globals the map modules share — one non-static definition per
retail VA (`g_mv_<what>`), aliased by `rh_hooks_mapview.c` (`A(name, 0xVA)`; rhost never links `*_globals.c`). The
modules shared ONE set of retail globals but each kept its own copy (the camera three times, the walker three times,
the error counter three times); now they read and write the same symbol. Widths from the asm: the cell latches
`DAT_0057d384` / `DAT_0057d374` and the dev toggles `DAT_005bb4a4/5` are bytes, everything else a dword, the bank
pointers retail DWORDs.

| group | globals |
|---|---|
| camera | x0 `DAT_0052afdc` (= `g_gfx_alt_clip_left`: the alt clip's left edge IS the map's), y0 `553244`, col/row start `5651b4/b8` (save slots), counts `53e8ac` / `56d67c`, steps `568220` / `53f65c` / `567950`, the feature y bias `57c97c` |
| walker | row `56d594`, col `56d598`, y accum `56957c`, cell `567954`, error count `58fea8` |
| modes | view `57cb18`, render `53f264` (save slots) |
| banks | terrain `568208` / `53e914` / `57d344` / `55409c` / `56898c`, features `553224` / `56d8bc`, overlay RLE `55ce5c` |
| terrain | cell county `57d384`, cell flags `57d374`, overhang source `5aeb6c`, county votes `52f020[33]` |
| features / overlay | rotation `522f7c`, size-cap county `57c8c4`, dev toggles `5bb4a4/5`, army flag tick `53e9a4`, walk-rate gates `58fd08` / `57d3c8` / `57d3ac` / `591510` |
| scene | counters `57d378` / `57d388`, redraw phase `57d380`, sub-phases `57d3a8 / 394 / 39c / 390`, `57d3a4 / 3a0 / 398 / 38c`, pacer timestamps `5ceffc` / `5ceff8` |
| minimap | terrain grid `569590` (`engine_minimap_globals.c`; the county grid `52aff0` is rh_hooks_economy.c's alias), food detail `553e60` |
| county panel | queues `53f690[4]` / `553fd0[4]`, counts `553c60` / `5440b4`, rows `553038` / `56d68c`, heights `53e970` / `53f04c`, last county `4e59b8`, strategic-ready `53f050`, End-turn key `4e59a4` (vs save slot `554004`) |

Port bindings (not retail globals), bound once under rhost (`mv_bind`): the tile grid `DAT_0055cea0` (retail indexes
the array; the standalone binds engine_map_init's), the descriptor table (default `g_overworld_map`), feature records
(default `g_per_territory_420`), army records (`g_per_army_768`), the path field's i16 grid `DAT_00500c30` (city size,
dev overlay), the palette tables (retail's own at `0x4d8108`: retail indexes them without a bound), the minimap colours
(`0x4d28f8`), the army label's font + char maps, the army drawer's log (`FUN_004afab9`), the scene's clock (retail's
`GetTickCount` import — retail reads it twice per tick), and the dirty rectangle: every pass marks through
`engine_territory_dirty_state()` (rh_hooks_ai.c's synced `s_dirty` — the owning port symbol of `FUN_004527a6`'s six
globals); the empty-cell redraw gate `DAT_0053e9ac` is that state's `below_status_redraw` (the terrain module reads it
through a bound `int *`). The county panel's environment (sheet pointer `DAT_005530c8`, the three fonts, char maps,
the localization walker over `DAT_00591580` / `DAT_005c9270`, the dirty state, the screen-2 workforce seams) is
filled per call.

## 2. The blitters — transliterated from the asm

The terrain pass draws through 24 fully unrolled blitters (the iso tile, its edge halves and the overhang strips, per
view mode). The port replayed tables extracted from Ghidra's decompile (`tools/extract_blit_table.py`,
`extract_drawb_*.py`) with a hand model of the strip loop that called its second branch "dead (col_y ≫ count)". The
asm shows it is not: `cmp ebx, ecx; jle` sends every row whose walker y (`DAT_0056957c`) is ≤ the row counter to the
second branch — the top rows of the map, where y is 0 — which draws the overhang rows without the scanline decrement.
`tools/gen_mapblit.py` now translates each blitter's IDA asm instruction by instruction into C
(`src/gen/mapblit_<va>.h`, derived, gitignored): registers as locals, `[esi+N]` / `[edi+N]` loads and stores, each
`cmp` paired with its jump, the globals mapped to the retail-layout symbols. Exact by construction (the instruction set
is 30 forms); every view-0 blitter then ran at 0 divergences on its first shadow.

## 3. Rows + evidence

Depth = the shadow pass that reached the row; `calls` = calls shadowed (state + pixels); `sampled` = calls a
sampled row let run retail-only in the shadow passes (the hybrid ran every one through the port).

| row | retail | port | depth | calls | sampled | scenarios |
|---|---|---|---:|---:|---:|---:|
| `mv_scene_tick` | FUN_004050c0 | `engine_map_scene_tick_frame` | 1 | 15382 | 570855 | 49/51 |
| `mv_terrain_frame` | FUN_0040526e | `engine_map_terrain_frame` | 1 | 6333 | 564522 | 49/51 |
| `mv_terrain_row_aligned` | FUN_00405ae9 | `engine_map_terrain_row_aligned` | 1 | 6438 | 7896870 | 49/51 |
| `mv_terrain_row_shifted` | FUN_00405c2f | `engine_map_terrain_row_shifted` | 1 | 12508 | 8455322 | 49/51 |
| `mv_terrain_drawer_a` | FUN_004063c1 | `engine_map_terrain_drawer_a` | 1 | 39013 | 142190533 | 49/51 |
| `mv_terrain_drawer_b` | FUN_00406673 | `engine_map_terrain_drawer_b` | 1 | 39013 | 142190533 | 49/51 |
| `mv_terrain_empty` | FUN_0042a7f1 | `engine_map_terrain_empty_cell` | 1 | 4251 | 4839653 | 12/51 |
| `mv_terrain_empty_left` | FUN_0042a8d1 | `engine_map_terrain_empty_left` | 1 | 1344 | 853973 | 12/51 |
| `mv_terrain_empty_right` | FUN_0042a9ab | `engine_map_terrain_empty_right` | 1 | 600 | 9251 | 6/51 |
| `mv_blit_452820` | FUN_00452820 | `engine_map_terrain_blit_452820` | 1 | 8242 | 130992170 | 49/51 |
| `mv_blit_45337b` | FUN_0045337b | `engine_map_terrain_blit_45337b` | 1 | 7733 | 8436993 | 49/51 |
| `mv_blit_4538f6` | FUN_004538f6 | `engine_map_terrain_blit_4538f6` | 1 | 7038 | 8441234 | 49/51 |
| `mv_blit_460501` | FUN_00460501 | `engine_map_terrain_blit_460501` | - | 0 |  | 0/51 |
| `mv_blit_46079d` | FUN_0046079d | `engine_map_terrain_blit_46079d` | - | 0 |  | 0/51 |
| `mv_blit_4608de` | FUN_004608de | `engine_map_terrain_blit_4608de` | - | 0 |  | 0/51 |
| `mv_blit_463b42` | FUN_00463b42 | `engine_map_terrain_blit_463b42` | - | 0 |  | 0/51 |
| `mv_blit_463c06` | FUN_00463c06 | `engine_map_terrain_blit_463c06` | - | 0 |  | 0/51 |
| `mv_blit_463c62` | FUN_00463c62 | `engine_map_terrain_blit_463c62` | - | 0 |  | 0/51 |
| `mv_strip_453e71` | FUN_00453e71 | `engine_map_terrain_strip_453e71` | 1 | 6521 | 14114959 | 49/51 |
| `mv_strip_455f04` | FUN_00455f04 | `engine_map_terrain_strip_455f04` | 1 | 5819 | 685270 | 49/51 |
| `mv_strip_456fd8` | FUN_00456fd8 | `engine_map_terrain_strip_456fd8` | 1 | 5927 | 898651 | 49/51 |
| `mv_strip_4580a9` | FUN_004580a9 | `engine_map_terrain_strip_4580a9` | 1 | 11722 | 5602171 | 49/51 |
| `mv_strip_459258` | FUN_00459258 | `engine_map_terrain_strip_459258` | 1 | 11832 | 4865267 | 49/51 |
| `mv_strip_460a1c` | FUN_00460a1c | `engine_map_terrain_strip_460a1c` | - | 0 |  | 0/51 |
| `mv_strip_4611ef` | FUN_004611ef | `engine_map_terrain_strip_4611ef` | - | 0 |  | 0/51 |
| `mv_strip_461623` | FUN_00461623 | `engine_map_terrain_strip_461623` | - | 0 |  | 0/51 |
| `mv_strip_461a54` | FUN_00461a54 | `engine_map_terrain_strip_461a54` | - | 0 |  | 0/51 |
| `mv_strip_461efb` | FUN_00461efb | `engine_map_terrain_strip_461efb` | - | 0 |  | 0/51 |
| `mv_strip_463cbe` | FUN_00463cbe | `engine_map_terrain_strip_463cbe` | - | 0 |  | 0/51 |
| `mv_strip_463e91` | FUN_00463e91 | `engine_map_terrain_strip_463e91` | - | 0 |  | 0/51 |
| `mv_strip_463fa5` | FUN_00463fa5 | `engine_map_terrain_strip_463fa5` | - | 0 |  | 0/51 |
| `mv_strip_4640b6` | FUN_004640b6 | `engine_map_terrain_strip_4640b6` | - | 0 |  | 0/51 |
| `mv_strip_464209` | FUN_00464209 | `engine_map_terrain_strip_464209` | - | 0 |  | 0/51 |
| `mv_feature_frame` | FUN_00405487 | `engine_map_render_frame` | 1 | 6333 | 564522 | 49/51 |
| `mv_feature_row0` | FUN_00405862 | `engine_map_render_row_phase0` | 1 | 6438 | 7896870 | 49/51 |
| `mv_feature_row1` | FUN_004059af | `engine_map_render_row_phase1` | 1 | 12508 | 8455322 | 49/51 |
| `mv_feature_tile` | FUN_00408438 | `engine_map_render_tile` | 1 | 51878 | 157102465 | 49/51 |
| `mv_overlay_frame` | FUN_00405602 | `engine_overlay_render_frame` | 1 | 6333 | 564522 | 49/51 |
| `mv_overlay_row0` | FUN_00405eb5 | `engine_overlay_render_row_phase0` | 1 | 6438 | 7896870 | 49/51 |
| `mv_overlay_row1` | FUN_00405fac | `engine_overlay_render_row_phase1` | 1 | 12508 | 8455322 | 49/51 |
| `mv_overlay_army` | FUN_004071a0 | `engine_overlay_render_tile_army` | 1 | 29527 | 7358187 | 49/51 |
| `mv_overlay_city` | FUN_004081a6 | `engine_overlay_render_tile_city` | 1 | 13303 | 127082 | 40/51 |
| `mv_overlay_debug` | FUN_00408c50 | `engine_overlay_render_debug_text` | 1 | 13398 | 118009981 | 49/51 |
| `mv_army_label` | FUN_00407f82 | `engine_overlay_render_army_label` | 1 | 320 | 1073 | 4/51 |
| `mv_fill4` | FUN_004b4056 | `engine_overlay_render_fill4` | 1 | 384 | 330 | 1/51 |
| `mv_minimap_render` | FUN_00410aa9 | `engine_minimap_render` | 1 | 5743 | 3328 | 49/51 |
| `mv_minimap_plot` | FUN_00410cbd | `engine_minimap_plot` | 1 | 1794 | 1547 | 38/51 |
| `mv_minimap_bands` | FUN_00451bba | `engine_minimap_compute_bands` | 1 | 890 | 657 | 17/51 |
| `mv_panel_draw` | FUN_0040f7d3 | `engine_hud_panel_draw` | 1 | 53199 | 1186412 | 51/51 |
| `mv_panel_queues` | FUN_0040fec1 | `engine_hud_panel_build_queues` | 1 | 3481 | 2823 | 48/51 |
| `mv_panel_q1_type1` | FUN_004100af | `engine_hud_panel_draw_q1_type1` | 1 | 3416 | 2374 | 48/51 |
| `mv_panel_q1_type0` | FUN_0041023a | `engine_hud_panel_draw_q1_type0` | 1 | 2500 | 2482 | 33/51 |
| `mv_panel_q1_type2` | FUN_004103c5 | `engine_hud_panel_draw_q1_type2` | - | 0 |  | 0/51 |
| `mv_panel_q2_type4` | FUN_00410502 | `engine_hud_panel_draw_q2_type4` | - | 0 |  | 0/51 |
| `mv_panel_q2_type5` | FUN_00410598 | `engine_hud_panel_draw_q2_type5` | 1 | 99 | 150 | 1/51 |
| `mv_panel_q2_type6` | FUN_0041062e | `engine_hud_panel_draw_q2_type6` | 1 | 3353 | 2022 | 48/51 |
| `mv_panel_q2_type7` | FUN_004106c4 | `engine_hud_panel_draw_q2_type7` | - | 0 |  | 0/51 |
| `mv_panel_q2_type3` | FUN_004107d1 | `engine_hud_panel_draw_q2_type3` | 1 | 1580 | 1610 | 19/51 |
| `mv_panel_endturn` | FUN_0041a734 | `engine_hud_panel_endturn` | 1 | 12557 | 572720 | 49/51 |
| `mv_ti_panel` | FUN_0041b032 | `engine_terrain_info_panel` | 1 | 80 |  | 17/51 |
| `mv_ti_simple` | FUN_0041befe | `engine_terrain_info_simple` | 2 | 41 |  | 17/51 |
| `mv_ti_text` | FUN_0041c208 | `engine_terrain_info_text` | 3 | 41 |  | 17/51 |
| `mv_ti_stamp` | FUN_0041c996 | `engine_terrain_info_stamp` | 3 | 3 |  | 3/51 |
| `mv_ti_parchment` | FUN_00409397 | `engine_terrain_info_parchment` | 1 | 4663 | 418 | 45/51 |

Rows at depth `-` are never called by the corpus (in the hybrid either); §6.

## 4. Port bugs found and fixed (retail ground truth: IDA pseudocode + asm, then the shadow)

| # | function | was | retail |
|---|---|---|---|
| 1 | FUN_00406673 overhang strip | the second strip branch modelled as dead; a render-mode-1 cell without flag 0x20 drew bank 0 / sprite 0 | the branch draws the map's top rows; render mode 1 skips the cell. Phase 3 kind 4 (`459258(…, 2)`) and phase 4 kind 3 (`4580a9(…, 2)`) were counted "skipped"; the source shift arg (`add esi, [ebp+arg_8]`) was missing |
| 2 | FUN_00406673 | `DAT_005aeb6c` never written; count > 200 silently skipped | writes the overhang source offset (Offset + 900 / 196 / 36 per view); count > 200 bumps `DAT_0058fea8` |
| 3 | FUN_004063c1 / 00406673 / 0042a7f1 | the descriptor staged into port locals; `DAT_005c9288`, `DAT_0057d384`, `DAT_0057d374` untouched; the first-paint bit cleared nowhere | stage `word_5C9258 / 5AEB70 / Offset / 591568 / 5BB478 / 57D3C0`, write the sprite index and the cell latches, clear descriptor +2 bit 0 and mark the 80×48 box (FUN_00452628) |
| 4 | FUN_00405ae9 / 00405c2f | no county votes | each drawn inner cell adds 1 (5 for flag 0x40) to `DAT_0052f020[descriptor +7]` |
| 5 | FUN_00405c2f | the edge empty cells folded into one generic drawer (phase 3 / 4) | two functions of their own, FUN_0042a8d1 / FUN_0042a9ab (now ported, with rows) |
| 6 | FUN_004050c0 | the votes, the error-counter reset and the current-county follow "deferred as inert" | `DAT_0058fea8 = 0`; votes seeded with 30 for the current county; after the passes the county with most votes becomes `DAT_0057c93c` (a SAVE SLOT) in view mode 0 — game state, not a camera detail |
| 7 | FUN_004050c0 | one injected `now` | the GOG pacer stub reads GetTickCount twice (the bound clock) and gates on `DAT_005ceffc == 0`, not a separate flag |
| 8 | FUN_00408438 | the cursor saved in locals; `DAT_005c9288` left at the record's sprite when there was no overlay; the palette lookup masked `& 0x7f` | saves to `DAT_00591508 / 504` (shared slot), writes 999 then the overlay index, indexes the palette without a bound (retail's bytes) |
| 9 | FUN_004081a6 city | no cursor save; size cap skipped when the cap county is 0 | saves to `DAT_00591508 / 504`; always reads county `DAT_0057c8c4`'s record (record 0 exists) |
| 10 | FUN_004071a0 army | a bad offset bumped `DAT_0058fea8` | logs `aErrTopItNoData` / `aErrTopItBadDat` / `…_0` through FUN_004afab9 and latches `DAT_005aeb68 = 1`; no count |
| 11 | FUN_00407f82 army label | the text drawn with the render map as the measure map; the cursor save in locals | the measure map `DAT_004d71d0`; `DAT_005aea40` around the label; the mark reads the live cursor after the label |
| 12 | FUN_00408c50 dev overlay | suffix "" | suffix " " (`asc_4D2098` / `asc_4D209C`), the cursor save slot |
| 13 | FUN_00405602 | the view-mode-1 strip clear wrote the bound canvas | FUN_004b4056 (ported: 4 pixels through Buf / xRight) |
| 14 | FUN_00451bba / 00410cbd minimap | record count clamped to 16; colours from a 48-byte table (an owner byte > 5 overflowed) | no clamp; one contiguous table (`DAT_004d28f8` + 8), retail's bytes |
| 15 | FUN_0040f7d3 panel | only the forced path; queues / rows / heights in a local struct; no right-half mark | the change-detection gates (`DAT_005530d0` 2 / count-down, `DAT_004e59b8`), the screen-2 workforce refresh (FUN_0045161e + 0x4120e0, seams), the screen exclusions, FUN_004523e6's mark, the retail globals |
| 16 | FUN_0041a734 End turn | sprite + label only | the `DAT_00554004` change gate, FUN_004527a6's 11×2 forced box, the menu redraw latch `DAT_004eb260` |

## 5. Quirks (retail ground truth) — for engine-quirks.md

- FUN_004063c1 draws nothing in view mode 1 (only views 0 and 2 have a ground blitter); the empty-cell drawers do.
- The overhang strips draw their rows above the map top through a second, un-decremented branch when the walker y
  (`DAT_0056957c`, 0 for the first two rows) is ≤ the row counter.
- FUN_004050c0 makes the county with the most visible cells current (`DAT_0057c93c`, a save slot), weighting
  flag-0x40 cells 5 and the current county 30 — scrolling the map changes the panel's county (view mode 0 only).
- FUN_00405862's extra left cell reads `DAT_0055ce9c[65 * row + col_start]` (column col_start - 1).
- The army drawer's 0x40 branch with descriptor +4 nibble ∉ {0, 2} draws the previously staged sprite (#70).

## 6. What remains

- **Never reached by the corpus** (19 rows; 0 calls in shadow and hybrid alike):
  - view mode 1 (the far zoom): FUN_00460501 / 0046079d / 004608de (tile + edges) and the strips FUN_00460a1c /
    004611ef / 00461623 / 00461a54 / 00461efb — view 1 shows for ~2 frames in bot3s-s406 only (`mv_fill4` reached
    there), with no empty cell and no overhang sprite on screen (drawer A draws nothing in view 1);
  - view mode 2 (the kingdom overview): the corpus never presses the minimap's magnifier. An ad-hoc probe (endturn-2
    + a click at (623, 144) = FUN_0043ab76 button 4 → FUN_00434fd5, kept out of the corpus) reaches
    FUN_00463b42 / 00463c06 / 00463c62 and the strips FUN_00463cbe / 004640b6 / 00464209: 378 calls shadowed (137 / 28 / 20 / 65 / 64 / 64), 0 divergences — and every other mapview row on the same run, 0 divergences; the 64 rows replaced: the same final state and frame-hash chain as the pure run (`fb=d30eacffd73b7965`). To repeat: append to endturn-2's trace a click at (623, 144) at f 3710 and 4100 (with hovers at the map edges between), frames 4600, in a scratch `tests/scenarios/<name>/` (not committed). The edge kind-2
    strips FUN_00463e91 / 00463fa5 stay unreached;
  - the county panel's q1 type 2 (FUN_004103c5), q2 type 4 (FUN_00410502) and type 7 (FUN_004106c4) — counties
    showing those resource columns.
  A bot / human game that opens the kingdom overview and scrolls it (and a far-zoom one) would cover them
  (docs/human-traces.md candidate).
- **Seams to retail (unported callees, through `rh_port_retail_enter/leave`)**: the county / army terrain panel
  0x41b19d and its rosters 0x41cb3a / 0x41d299 / 0x41da2f, the UI sound cue 0x426216, the `icon_tmp.pl8` read
  (retail's file reader at 0x4af8d9), the present 0x452160, the status log 0x4afab9, the screen-2 workforce redraw
  0x4120e0 and FUN_0045161e (ported in engine_workforce_hud.c, not linked: it drags the file layer). In the
  standalone the seams are NULL (no-op), as the port's behaviour was before.
- **No row**: FUN_0046a037 (the minimap grid loader: file I/O; retail loads), FUN_0043caf4 (the terrain-info opener:
  its hook-struct core is bound by engine.c, not on retail's layout yet).
- **The standalone** still composes through the setters (engine_screen_strat.c, main.c's seats); they now write the
  retail-layout globals, including save slots (view / render mode, col / row start). The county follow now runs in
  `engine_map_scene_render_body` (retail behaviour: the panel's county follows the view).
- **One retail global, two standalone symbols** (aliased to one VA under rhost; to unify when the owners meet):
  `DAT_0053f050` (`g_mv_strat_ready` / `g_bf_53f050`), `DAT_00568208` (`g_mv_terrain_blob0` / `g_bf_568208`),
  `DAT_00522f7c` (`g_mv_rotation` / `g_bf_522f7c`), `DAT_0053e9ac` (the dirty state's `below_status_redraw` /
  the terrain module's own gate / `g_bf_53e9ac`).
- **Shared-core wishes** (not changed): rh_hooks_gfx.c's `gfx_bind` and `LOC` are file-static, so
  rh_hooks_mapview.c repeats the three gfx bindings and the walker load / store — an exported `rh_gfx_bind_once()`
  / `rh_loc_load/store()` in rh_port.h would remove the copies. `rhost.py run --hook @FILE` passes the host path to
  the exe (unreadable under wine): inline the spec instead.
