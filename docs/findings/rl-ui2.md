# rl ui2 — what the UI tours reach besides the skirmish, on retail's layout (2026-11-04)

Partition `ui2` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): the labour model, the kingdom
overview, the help / confirm-dialog actions, the save box and screenshots, the armoury's weapon pick, the county
field panel, the palette remap, the skirmish setup's actions and data load, the battlefield's group keys, the MP
session open. 76 functions, 17821 B, all in `src/rl_ui2.c`; rows in `tools/rhost/rh_hooks_rl_ui2.c` (76).

## Status (2026-11-04)

All 76 ported, every one with a row; 76 / 76 shadowed at 0 divergences (71 over the whole corpus, the 5
table-reached ones with `--hook-entry`: §Evidence); hybrid PASS.

## What is where

| group | functions |
|---|---|
| labour model (strat-tour: the drag-box worker moves) | 0x43958a box select, 0x4399b0 drop, 0x439b52 move, 0x439cc2 target side effects, 0x439f6a / 0x439edb rebalance, 0x44ff4a labour share, 0x450000 production shares, 0x450639 worker group on / off, 0x438bec / 0x438b02 a field's use (Farmland icons), 0x439541 drag end |
| kingdom overview (minimap view mode 2) | 0x434fa1 magnifier, 0x434fd5 open, 0x43505a close, 0x4350a1 county click, 0x41424a strip clear, 0x43ab76 overlay buttons, 0x46d421 (empty) |
| help / menus / dialogs | 0x41543f About, 0x4154ea Help Options, 0x434d33 / 0x434d60, 0x434787 / 0x4347c7 the two toggles, 0x43484a / 0x434888 / 0x4348c6 / 0x434904 How do I, 0x433dbd / 0x433deb New Game + answer, 0x4343f8 / 0x43441c Quit + answer, 0x401c5b, 0x43600c chat key |
| armoury / smithy | 0x43a950 weapon icons, 0x43a997 set the weapon |
| save box, screenshots, misc draws | 0x408ea4 / 0x408f1a shotN.lbm, 0x413526 saving animation, 0x414e06 file list, 0x434174 list click (the skirmish user map), 0x41ef42 |
| county field panel, palette | 0x41cb3a (terrain info kind 19), 0x4b14ff the system-colour remap (49 scenarios) |
| skirmish setup (title page 12 / 13) | 0x43d5b7 Go, 0x43d649 Back, 0x43d6a7 / 0x43d6c3 / 0x43d78b / 0x43d7a7 / 0x43d882 / 0x43d8b9 the custom army, 0x43d929 / 0x43d9cd the battle list, 0x43da9e / 0x43daf3 advantage, 0x43dbac difficulty, 0x43dc1d modes, 0x43dd83 side, 0x43ddf4 user.skr box, 0x498dcb / 0x498fb3 the skirmish data load, 0x4b3994 result cue |
| battlefield group keys | 0x43c77a WM_CHAR h / v, 0x43c885 Ctrl+1..9 store, 0x43c910 1..9 recall, 0x479276 / 0x47935b selection mask |
| MP session (custom-tour: Multiple players → DialogBoxParamA −1) | 0x4b7250, 0x4b7585, 0x43f352, 0x43f484, 0x43f24b, 0x4adcf6, 0x4af70c |

## How the ports reach the rest

- Linked ports on retail's globals, by name: the economy (engine_army_economy_provision / _plan_realm / _plan_resource
  / _stamp_resource_cell), the territory (engine_territory_set_cell_type / _census_all_realms /
  _allocate_realm_workforce / _grade_feature_cells), engine_math_percent_div / _of, the camera clamps (pointer
  arguments: `&s_slot_005651b4` …, the counts g_mv_col_count / g_mv_row_count), strutil_bounded_copy, the gfx layer
  (as rl_render.c: the localization walker loaded / stored around each wrap), engine_overlay_render_fill4 (0x4b4056).
- 0x46d5fe (the terrain under the cursor) through the pure engine_map_query_terrain_info: inputs from the cell grid,
  the county records and DAT_0056795c; its eight outputs stored where retail stores them (0x438b02).
- DAT_0053e9ac (the map-area redraw gate) is a MIRROR (rh_hooks_ai.c k_mirror_va): written through
  `engine_territory_dirty_state()->below_status_redraw`, never the alias.
- Other partitions by name: rl_4120e0 (render), rl_43eda0 / rl_4360f2 (glue), rl_426216 / rl_427990 / rl_4af767 /
  rl_4b11ce / rl_49915e / rl_499366 / rl_49964c / rl_4afab9 / rl_497a34 / rl_4b8c3a / rl_451f70 (io), rl_42c64f /
  rl_42c4e2 / rl_42c476 / rl_42b9c4 / rl_42ba40 / rl_42bf46 / rl_42ac0c / rl_42d4aa / rl_42d857 / rl_42c5ad
  (skirmish), bf_499cb1 / bf_478987 (input), bf_479e90 (action), bf_4778a0 (setup), bf_43f304 (extern).
- An 11-argument retail call (0x4bc020): engine_retail_call takes up to 8, so it calls a cdecl adapter (as rl_io.c's
  thiscall adapter) that makes the call — the bracket still applies.
- Platform shim (`rl_io_plat.h`), added: DialogBoxParamA (IAT 0x5cf52c), _filelength (CRT 0x4cf490).

## Globals

`src/rl_ui2_globals.{h,c}` + `tools/rhost/rh_rl_ui2_alias.h`: generated (`gen_skeleton.py --part-globals ui2`,
`dedup_globals.py`: none shared), then by hand: the workforce grid DAT_005540a0 widened to 200 B (8 x 25; IDA sized
it by byte_5540AA), the DPSESSIONDESC DAT_004e5ab0 folded into one 0x50-byte block, the MP preload states
DAT_00553d30..44 into one int[6]; added (owned by no header): DAT_0056795c (the cursor's cell offset — it falls
inside g_caravan_order's 8-row port array, retail's has 6 rows), DAT_00553f78, DAT_005651bc. Seven of rl_glue's
globals are declared locally (rl_glue_globals.h conflicts with rl_io / rl_render_globals.h in one unit).

## Rows

HOOK_R where a caller reads EAX (0x434174, 0x439541, 0x4399b0, 0x43f352, 0x4b7585, 0x4b7250, 0x4adcf6, 0x4af70c,
0x498fb3) and where the port models retail's EAX on every path (the labour model's leaves, 0x498dcb) — compared;
HOOK_V elsewhere (router actions, the dialogs' answer callbacks, the WndProc's key arms). RH_SH_MEDIA on every row
that draws or calls a drawer.

## Quirks (retail ground truth)

1. **strutil_bounded_copy (0x401136) is (src, dst, n)** in retail; the port's is (dst, src, n): 0x434174 copies the
   list's name INTO DAT_0053f5fc; 0x43c885 the selection mask into the group slot; 0x43c910 back.
2. 0x498dcb allocates its four skirmish blocks (sprites, misc, the minimap pair) anew on every call — each battle /
   mode change in the setup leaks the previous ones; 0x498fb3 leaks its block when the read fails.
3. 0x4b7585 (join path) stores the ADDRESS of its own by-value GUID argument in DAT_004e5b08 (a stack address in
   state, read by the 0x82 dialog's proc). Not reached (rhost has no DirectPlay).
4. 0x43d5b7 (MP) indexes the host-ready flags DAT_005533e0 by player: players ≥ 2 read past its two dwords into
   0x5533e8 (the army-state index, a port mirror) and beyond.
5. 0x44ff4a reads an uninitialized local (`var_8 = DAT_0053f9d4 − var_C`), a dead store overwritten at once.
6. 0x41cb3a: the composite lines reset the text cursor DAT_005cd404 to 0 and pass `cursor + 40` to each callee, which
   advances it; the first number of a line is drawn at x 40 either way.
7. 0x4350a1: a county missing from the overview map leaves the loops at col 65 / row 129: the scroll becomes
   (61, 116).
8. 0x43d9cd: a scroll past either end clamps the list top and returns without re-selecting the battle (no redraw).
9. 0x43daf3: the advantage bytes are clamped as UNSIGNED (a 0 − 1 wraps to 255 → 3, not 1).
10. 0x41424a (the view-mode-1 strip clear) is dead from its only caller: 0x434fd5 sets view mode 2 (0x451fcc stores
   DAT_0057cb18) right before it, and it draws only in mode 1.
11. 0x4b14ff remaps the 20 Windows system colours of the back buffer to the nearest of the live palette's 10..245
   (city-block RGB distance, the first minimum) — reached in 49 scenarios (after the palette fades).

## Seams left

- TODO(rl-link) — a port exists, not on retail's layout / not linked in rhost (retail through engine_retail_call):
  engine_workforce_row_sprite_index 0x4517ca, 0x4398f5 (engine_labor.c), 0x448648 (engine_army_select.c's leaf),
  engine_workforce_hud_render 0x412143, engine_map_init_set_camera 0x451fcc, the season loader 0x4984dc, gfx.c's
  present 0x452160, engine_confirm_dialog_open 0x40e6f2, engine_event_queue_post 0x472bc5 / _init 0x472ae0 (port
  structs), 0x476a5d (engine.c), text_input_init 0x402009 / _recompute_length 0x401f49, labor_panel_smithy 0x413155,
  labor_num_units 0x41ab67, engine_title_render_filelist 0x4148e4, gfx_load_misc_sheet 0x499034,
  gfx_load_system_sheet_select 0x499a1c, io_path_chdir_base 0x4af502, gfx_load_panel_sheet 0x4b25b0,
  io_read_file_at 0x4af8d9, engine_battlefield_render_bind_hex_grid_context 0x4bc020 / _minimap_context 0x4bc107,
  0x49792e.
- TODO(rl-extern) — unported: 0x4b74ce, 0x4b743b, 0x43e8f4, 0x4b7a9a, 0x43f43e, 0x43f01d, 0x43fed2, 0x43306a,
  0x4b7180, 0x40ec2c, the dialog procs 0x4b7f78 / 0x4b8335 / 0x4b8439 / 0x4b85b7 (passed by retail VA: the standalone
  would need its own).
- rl_misc.c calls 0x4b14ff through engine_retail_call (0x4b0cb4, 0x49a3e6): now rl_4b14ff.

## Standalone bug found

- `engine_workforce_hud.c` k_workforce_row_index (DAT_004d6780) has 8 entries; retail's table reads 10 —
  {5, 0, 1, 3, 6, 7, 8, 2, 4, 6} (exe bytes): the rebalance 0x439edb(c, 6) asks 0x4517ca for categories 0..9, so
  engine_workforce_row_sprite_index(c, 8 / 9) reads past the port's array (retail: rows 4 / 6).

## rhost limit hit (shared-file need)

- **A function with BOTH an E8 call site and a pointer-table caller** gets only its call sites patched
  (rh_hooks.c: `redirect_calls`, the entry patch only when there is none): the table's calls bypass the row.
  0x43d929 / 0x43d9cd / 0x43dc1d / 0x43dd83 (E8 only from the MP receiver 0x44622c; the setup page's button table
  calls them) and 0x43d5b7 (0x44678d) read "never called" in the corpus verify; 0x4343f8 (WndProc / the frame
  driver + the File menu's entry) sees only its key paths. Verified with `--extra=--hook-entry` (every installed row
  entry-patched): §Evidence. Fix: patch the entry too when the function's address is taken (functions_ida.tsv
  addr_taken), or always (the entry patch catches the call sites as well).

## Path coverage (block level) and exploration

First-hit coverage of every basic-block start of the 76 functions (jump targets + conditional fall-throughs from
all_ida.asm: 738 blocks; `rhost.py coverage` with that list as its function table, retail only, ~1 min for the
corpus): **512 / 738 blocks reached by the corpus**. Exploration probes (rhbot: replay a tour with every ui2 row in
shadow, poke retail memory at a frame, run on — poked state, NOT a pure replay; evidence for paths the corpus never
takes) add 46: **558 / 738**, all at 0 divergences:
- 0x450639's off arm + the remainder fix-up (5 → 20 / 24): county 2's shares poked (AE8 30 / AE4 70) before the
  Farmland click f4541. The on arm needs the census to set +0x204 (the cell feature it counts) — not found.
- 0x439f6a's fill arm (13 → 18 / 19): 60 idle workers for county 2 before the rebalance (f3514).
- 0x41cb3a (14 → 37 / 38, pixels compared): the season 1..4, Advanced farming, the stock / trend / change rows'
  signs, the 249 / 252 colours, poked before its one draw (f4719).
- 0x434174's copy + user-map load (11 → 13 / 17): another current name before the .skr list click (skirmish-tour
  f5342).
Not covered (beyond the MP arms below): 0x450639's on arm, 0x439cc2's rows 5 / 6 / 7 with their resource, 0x43441c's
yes arms (the answer byte is set in the click's own frame: a poke before it is overwritten), 0x433deb's skirmish arm,
0x41424a's draw (dead, quirk 10), 0x438b02's cue for uses 0 / 1 / 25.

## Evidence

- `rhost.py verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (54 scenarios, 2026-11-04 15:5x–17:10, with
  the other rl partitions' 210 rows): **281/286 reached with 0 divergences; hybrid PASS; 0 failed runs**. The 76 ui2
  rows: 71 reached, 10438 calls shadowed (state; pixels on the RH_SH_MEDIA rows), 0 divergences, 0 skipped; the 5
  never called are the table-reached ones (§rhost limit).
- `verify --extra=--hook-entry --hooks '^rl_(43d5b7|43d929|43d9cd|43dc1d|43dd83)$' --only skirmish-tour`: 5/5 reached,
  27 calls, 0 divergences (0x43d5b7: 1 call skipped — its battlefield entry grew the heap arena), hybrid PASS;
  `… --hooks '^rl_4343f8$' --only 'strat-tour|menus-tour|skirmish-tour'`: 2 calls (the menu's included), 0, PASS.
- Reach: 0x4b14ff 3207 calls / 49 scenarios; everything else 1–2 scenarios (strat-tour: the labour model, the overview,
  help, the armoury, the field panel, the group keys come from skirmish-tour; custom-tour: Multiple players, the
  custom setup). The custom army's ±/max/none (0x43d6c3 1563, 0x43d7a7 2425 calls) and the skirmish data load
  (0x498dcb 329, 0x498fb3 1316) are the hot ones.
- Never exercised (ported from the asm, unverified): every MP arm (DAT_00553030: 0x4399b0's message 37, 0x438b02's 34,
  0x433deb's 25, 0x43441c's session leave, 0x43600c's chat line, 0x43a950's 33, 0x43d5b7's ready gate, 0x43d649's
  leave, 0x434174's 93, 0x43c77a's 67, 0x4adcf6's wait loop), 0x4b7585 past "DP not created" (no DirectPlay under
  rhost: the caps, the join / host dialogs, Open, CreatePlayer) and 0x4b7250 past it (the hello loop), 0x498fb3's
  failure logs, 0x41cb3a's other seasons / Advanced farming rows as the corpus lacks them, 0x43441c's 4th try.

