# rl ui3 — what the round-2 scenarios newly reach, on retail's layout (2026-11-04)

Partition `ui3` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): the options menu's handlers,
the noble faces' message / alliance buttons, the army split (Divide, screen 0x11), the battle prompt's siege-assault
check, the Alt+digit .rec load, the pull-down menu close. 23 functions, 6030 B, all in `src/rl_ui3.c`; rows in
`tools/rhost/rh_hooks_rl_ui3.c` (23).

## Status (2026-11-04)

All 23 ported, every one with a row; 23 / 23 shadowed at 0 divergences over the whole corpus, hybrid PASS (§Evidence);
block coverage 186 / 251 by the corpus, 232 / 251 with exploration probes (0 divergences).

## What is where

| group | functions |
|---|---|
| options (router actions) | 0x434556 / 0x4345d0 / 0x434693 the Advanced toggles (farming DAT_0053f25c, foraging DAT_0053f260 + every realm's provision / plan, exploration DAT_0053f264; MP message 0x32), 0x4349a4 music (DAT_0053f218: the strategic / battle music, or the stream stop + cue release), 0x434a29 effects (DAT_0053f214: the ambient loop), 0x434a9a / 0x434ad5 the toggles of DAT_0053f20c / DAT_0053f248 (two buttons each), 0x434b10 Full screen, 0x434e9e / 0x434ec9 the speed arrows (*DAT_005cd410 ± DAT_0058fd64 within DAT_005c927c..DAT_005c9260), 0x434942 Start game help (WinHelpA), 0x43480c a help page (event 291) |
| the menu bar | 0x40df62 close the pull-down menu (0x40dd92's no-item path, 0x42ff10) |
| noble faces (screen 0x1a modes) | 0x4361da mode 2, 0x436229 mode 3 / 4 (by the local realm's +0x81 vs DAT_00553fbc), 0x4362ca mode 5 (no county picked) |
| the army split | 0x4378b3 Divide (the muster rows from the army, screen 0x11), 0x437d65 / 0x437e9e a row's arrows (param0 = the row; row 7 the siege engines move whole), 0x437afb OK / Cancel (the minimums, the cap, MP 0x2d), 0x437fd7 the commit (a new army record beside the old one; an optional march target) |
| other | 0x43bbe7 the battle prompt's siege assault (events 110 / 111 / 157, MP 0x45, else 0x496b9f), 0x4477e7 Alt+2..5: master*.rec / slave*.rec into the 400 × 56-B table DAT_0050d7c0, +0x36 state 2 → 1 |

## How the ports reach the rest

- Linked ports on retail's globals, by name: engine_army_economy_provision / _plan_realm, engine_map_query_nearest_feature_ring
  (it leaves the found cell in g_cell_cursor_col / _row, the mirrored DAT_00569570 / 78), engine_territory_place_entity
  + engine_territory_current_slot() (DAT_0052afe0, a mirror), engine_combat_recompute_condition, engine_path_field_fill /
  _trace / _copy_record, engine_territory_mark_full_screen_dirty, engine_map_scene_tick_frame (0x4050c0, the mapview
  table's binding), engine_army_ai_set_ranking_dirty (DAT_0056d6a0, a mirror), the dirty state's below_status_redraw
  (DAT_0053e9ac, a mirror).
- Other partitions by name: rl_43eda0 (glue), rl_427990 / rl_451f70 / rl_499aca / rl_499d7d (io), rl_4b28cd / rl_4b1dd5
  (misc), bf_477b2f / bf_499d97 (extern stubs), bf_496b9f (stance).
- Platform shim (`rl_io_plat.h`), added: WinHelpA (IAT 0x5cf518; rhost stubs it in rh_win.c).

## Globals

`src/rl_ui3_globals.{h,c}` + `tools/rhost/rh_rl_ui3_alias.h`: generated (`gen_skeleton.py --part-globals ui3`;
`dedup_globals.py`: none shared), then by hand: DAT_0050d7c0 widened to the 0x5780-B .rec table (byte_50D7F6 inside it),
DAT_0052afe0 dropped (a mirror: engine_territory_current_slot()), added DAT_0053f380 (0x258 B: the lords2.inf message
texts of diplomacy modes 2..4; IDA names only byte_53F1E0 + offset). Other partitions' globals declared locally
(rl_io_globals.h and rl_glue_globals.h conflict in one unit): DAT_0053f20c / 0053f218 / 0053f228 / 005689a4 (io),
DAT_004d89b8.. (render: the cap table DAT_004d8a0c), hWndMain DAT_004e5a00 (misc). The muster rows go through
rl_glue's g_rl_53f6a0 (rl_glue's MUSTER; rl_render aliases the same VA as g_rl_53f690 — one storage under rhost, two in
the standalone).

## Rows

HOOK_R where the port models retail's EAX on every path (compared): 0x437d65 / 0x437e9e (row 7's split-off count, the
last value the sum loop reads), 0x437fd7 (0, 84 × army, player << 7 or the new army). HOOK_V elsewhere: router
actions (entry-patched; nobody reads EAX), 0x40df62 (AL only), 0x4477e7, 0x437afb (its Cancel path returns
0x45240a's EAX). RH_SH_MEDIA on 0x434b10 (the repaint) and the three noble-face buttons (the map scene tick draws).

## Quirks (retail ground truth)

1. 0x4477e7's key 0 ("master.rec" / "slave.rec") is dead: its only caller, the WndProc 0x4b29be, passes 2..5 for
   Alt+2..5 (Alt+1 is the screenshot 0x408ea4). Any other key reads nothing but still sweeps the 400 entries' +0x36
   (2 → 1). A missing .rec file leaves the table as it was.
2. 0x437fd7: a split in place (no target county) adds 5 to +0x153 (signed byte) of BOTH halves, and 0x4378b3 refuses a
   Divide while +0x153 > 0 (event 0x95): the split cooldown.
3. 0x437afb: an empty half clears the router's param0 (DAT_0059154c = 0) and drops the split silently, after the screen
   already went back to the map (0); Cancel goes to screen 4 instead.
4. 0x437afb: with a target county the minimum per half is 0 (50 without) and the cap is DAT_004d8a0c[county +0x1c0]
   less the men of the county's +0x1bc army (no target: 5000). The cap table (2500, 150, 200, 200, 400, 600, 0, 50,
   75, 100, 125, 150) has a 0 at index 6.
5. 0x437d65 / 0x437e9e: row 7 (the siege engines, +0x196) moves whole, the troop rows one man per click; the row-7
   buttons exist only with 18 split buttons (DAT_0055321c: the army's +0x197 set), so a click there otherwise does
   nothing.
6. 0x437fd7: the target is a cell BYTE offset: `off / 8` → col `% 64`, row `/ 64` (C division: cdq, and 7, sar 3).
7. 0x434b10: with DAT_0053f228 (the Full screen option, engine.c's "depth class") 1 and a depth other than 8 bpp
   (DAT_0053f22c; 16 under rhost) it only says event 0x104; at 0 and ≤ 640 px wide (DAT_004e65a0) it says event 0x12b
   ONCE (DAT_005689a4) and only the next click flips the option and re-creates the display (0x4b28cd(1), 0x4b2922,
   0x451f70, DAT_004df290 raised around it).
8. 0x434a9a / 0x434ad5 each sit in two button tables (0x4ddca8 / 0x4dde70, 0x4ddcc0 / 0x4ddeb8).
9. 0x4378b3 reads the army record index from DAT_0052afbc (engine_mapview_globals.h names it g_mv_ti_county, "the
   county id"): here it indexes the 420-B records (g_per_territory_420).

## Seams left

- TODO(rl-link) — a port exists, not on retail's layout / not linked in rhost (retail through engine_retail_call):
  engine_event_queue_post 0x472bc5 (a port struct), text_input_init 0x402009 / _recompute_length 0x401f49 (a port
  struct), audio_cue_release 0x427d19 (audio.c), gfx_set_palette_full_range 0x4b2922 (gfx.c), io_read_file_at
  0x4af8d9 (io.c), spawn_pick_roster_slot 0x4a9f72 (a static in engine_combat.c).
- TODO(rl-extern) — unported: 0x426d11 (the music stream stop).
- 0x4b1de0 (empty; in no partition) is not called (0x40df62, 0x4362ca): no state.
- For the owners of other files: rl_glue's rl_40dd92 calls 0x40df62 through engine_retail_call (TODO(rl-extern)): it
  can call rl_40df62 now. engine.c:1345 still lists 0x4361da / 0x436229 / 0x4362ca as DEFERRED (the standalone's
  diplomacy modes 2..5).

## Path coverage (block level) and exploration

First-hit coverage of every basic-block start of the 23 functions (jump targets + conditional fall-throughs from
all_ida.asm: 251 blocks; `rhost.py coverage` with that list as its function table, retail only): **186 / 251 by the
corpus** (realm-tour, diplo-custom, field-orders, siege-defence). Exploration probes (rhbot: replay a scenario with
every ui3 row in shadow, poke retail memory at a frame, run on — poked state, NOT a pure replay; evidence for paths
the corpus never takes; the same probe retail-only with `--coverage` for the blocks) add 46: **232 / 251**, all at 0
divergences:
- 0x434b10 (6 → 14 / 15): DAT_0053f228 0 before the Full screen click (f2840: event 0x12b), + DAT_005689a4 1 (the
  display re-create), + DAT_0053f22c 8 (the toggle back). The re-create path creates DirectDraw surfaces, which the
  shadow does not roll back (rh_ddraw state `20005ab0 r3/p5`, the known limit, rl-glue.md): verified instead as
  replace-vs-pure on the identical poked run — `replace:rl_434b10` and no hook end on the same state_full and
  framebuffer chain (fb, 2910 frames), both probes.
- 0x4477e7 (22 → 27 / 31): the MP-host names (DAT_00553248 1) and two entries in state 2 (f3925).
- 0x4378b3 (21 → 23): +0x153 1 (refused, event 0x95); +0x195..0x197 set (18 buttons) before Divide (f5505).
- 0x437d65 / 0x437e9e (12 → 15 each): row 7 — the trace's own row-7 clicks (f6140 / f6150) act once the army has
  siege engines.
- 0x437afb (21 → 33 / 34): Cancel (a click at (351, 440), f6153); a kept half of 10 (event 0x94) / 0 (param0
  cleared); a target county DAT_0053f080 2 with tier 6 (cap 0: event 0x11a), 4 (its +0x1bc garrison army), 2 with a
  target cell (f6160).
- 0x437fd7 (24 → 32 / 34): the target path (cells (33, 48) and (0, 0)), the siege engines' transfer
  (row 7's split-off 2 at f6160), +0x199 set (0x4a80db).
- 0x43bbe7 (8 → 13 / 14): siege-defence f1452 with DAT_0055401c 3 (the assault 0x496b9f) and DAT_0052af9c 1 (event
  157), DAT_005533d0 0 (nothing).
Not covered: the MP arms (DAT_00553030: 0x434556 / 0x4345d0 / 0x434693's message 0x32 and screen restore, 0x437afb's
0x2d, 0x43bbe7's 0x45 — 8 blocks), the battlefield sub-state arms (DAT_0057c8fc 2: options during a battle —
0x4349a4's 0x477b2f, 0x434a29's 0x499d97, 0x434b10's screen 0x29 — 5 blocks), 0x437fd7's two failure returns (no free
cell within ring 5 / no free record), 0x4477e7's key 0 (dead: quirk 1 — 4 blocks).

## Evidence

- `rhost.py verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (59 scenarios, 2026-11-04 17:58–18:58, with
  the other rl partitions' 321 rows): **344/344 reached with 0 divergences; hybrid PASS; 0 failed runs**. The 23 ui3
  rows: 134 calls shadowed (state; pixels on the RH_SH_MEDIA rows), 0 divergences, 0 skipped; depth 2 for 0x43480c /
  0x43bbe7 / 0x4378b3 (nested in other rl rows) and 0x437fd7 (inside 0x437afb).
- Reach: realm-tour alone for 21 of them (the options tour, Alt+2..5, the split: 0x437e9e 76 calls, 0x437d65 11);
  0x436229 diplo-custom + realm-tour; 0x4362ca diplo-custom; 0x43bbe7 field-orders + siege-defence.
- A poisoned port (0x437e9e moving 2 men) diverges at once: 70 / 76 calls, `0053f724 r1/p2 00554040 r1/p2`; hybrid FAIL.
- Never exercised (ported from the asm, unverified): the arms listed under "Not covered" above.
