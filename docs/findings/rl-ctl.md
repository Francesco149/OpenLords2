# rl ctl — the in-game input dispatch, the title menus, the turn, the realm passes on retail's layout (2026-11-04)

Partition `ctl` of family `rl`, round 3 (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): the 0x42ff10..0x465485
band the corpus reaches — the in-game input dispatch 0x42ff10, the edge scroll, the title menus' actions, the armoury,
the army select / move handoff, the county panels' buttons, End Turn, the terrain info, the scenario hint, the MP
delayed actions, the turn advance, the coalitions, the realm development, the new-game baseline, the map camera, the
dirty-rect marks, the army state machine's march. 98 functions, 31763 B, all in `src/rl_ctl.c`; rows in
`tools/rhost/rh_hooks_rl_ctl.c` (98). Every one had an old port (round 3); none was on retail's layout AND linked
in rhost except the ones marked (a).

## Status (2026-11-04)

All 98 ported, every one with a row. `rhost.py verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (59
scenarios; every rl row of this tree: 442, 68.2 M shadowed calls): 441 / 442 at 0 divergences, all 98 ctl rows reached
(11.3 M calls); hybrid PASS (all 442 replaced, `corpus --check`: every state hash and presented frame). The one
diverging row is rl_46521f: 4 of 3.18 M calls (bot3cen-s203 / s207 / s210), and those two pass-1 runs end off the corpus
— the page-tracked shadow perturbing RETAIL's side, not the port (§Residue): under `--shadow-full` the same three
scenarios give 687 k calls, 0 divergences, every end state on the corpus (and 472 k more over 12 others with the residue
itself compared). Least reached (1 scenario): 0x433905, 0x4351c4, 0x435211, 0x435a0c, 0x43a8f2, 0x43a922, 0x43b04c,
0x43d309, 0x435a61, 0x434346, 0x432ee6, 0x432fab (1–5 calls). Never exercised (no MP in the corpus): every `MP_ON`
arm, 0x4472ce past its single-player exit, 0x43de70 past the engine-state / single-player tests.

## Routes

(a) = a wrapper over a linked old port that already works on retail's layout; (b) = a fresh port from the asm (the old
one a reading aid).

| route | functions |
|---|---|
| (a) engine_dirty_rect.c (the bound dirty state, a mirror) | 0x452306 0x4523e6 0x45240a 0x452436 0x452489 0x4524dc 0x452628 0x45270a 0x4527a6 (+ 0x43cc56 / 0x452775 thin (b) over them) |
| (a) engine_realm.c (static → extern) | 0x449d6e 0x44b4d0 0x44b391 0x44b302 0x44ba35 0x44bfd5 0x44c093; 0x44b042 (extern already) |
| (a) engine_army_economy.c | 0x44d281 0x44d303 (static → extern), 0x450fb4 |
| (a) engine_territory.c | 0x44c6c4 0x4651b9 |
| (b) | the other 76: the old ports live in unlinked modules (engine.c, engine_ingame_input.c, engine_menu_input.c, engine_army_select.c, game_init.c, engine_map_init.c, engine_delayed_action.c, engine_rate.c, engine_anim_cycle.c, engine_confirm_dialog.c, gfx.c, engine_turn.c), take port structs (engine_camera.c's edge scroll, engine_county_panels.c, engine_armory.c, engine_labor.c, engine_terrain_info.c, engine_title_render.c's handlers, engine_army_state.c's march handler: a port queue), post into a port queue (0x44ae51) or diverge (0x43278b / 0x432746, below) |

22 (a) + 76 (b); no host-skip (the present 0x452160 blits through the primary surface's own vtable: rhost's software
DirectDraw keeps it in the media pool, so a RH_SH_MEDIA shadow rolls it back).

## Old-port bugs (found reading against the asm)

1. 0x43278b (engine_territory_recenter_on_tile): clamps the scroll with zero view counts (`engine_camera_clamp_scroll(&sc,
   &sr, 0, 0)`) — right only at game init; retail's 0x429b1d reads the live view size. 0x432746 skips the staged tile
   DAT_0053f0dc (a state write retail makes).
2. 0x452160 (gfx.c's gfx_tick_present): drops two of retail's gates — the movie bypass (0x42e26f with DAT_004df28c)
   and DAT_0056d650 / DAT_00553008 ("dead until the cutscene paths land"); retail's EAX (0x451f70 returns it) unmodelled.

## Quirks (retail ground truth)

1. 0x452160: the dirty rect is copied into a local RECT and then never used — Blt / BltFast always get the global
   Point (the whole screen rect DAT_004ea180); the mark then counts down and the rect resets to (canvas − 1, canvas − 1,
   0, 0).
2. 0x452628 zeroes its caller's third pushed argument (callers push 3, it reads 2); 0x4527a6 / 0x452436 & co. clamp
   their argument slots in place.
3. 0x431f59: in modes other than 0 and 2 (DAT_0057c8fc) the step moves nothing yet still sets DAT_0057cac4 and runs
   the rate gate.
4. 0x42ff10: the mid view's map click (screen 0, DAT_0057cb18 == 2) returns 0x4350a1's EAX straight away, skipping
   the dispatch's tail (DAT_00565488 and the fresh-key latch DAT_004eabb4 are left as they are that frame).
5. 0x42ff10 screen 8: the three close arms each read their own copy of "vill_gd8.pl8" (0x4d5620 / 0x4d5630 / 0x4d5640).
6. 0x42ff10 screen 5: 0x439541 is called twice (once for != 1, again for == 2).
7. 0x43ce1a: an army's owner byte (+0) is compared SIGNED with the local realm (movsx); the county owner (+5) unsigned.
8. 0x43a5a9: the ration byte +0x15f is signed (movsx) through the clamp 0..100.
9. 0x46521f: the facing index +0x1b into DAT_004d6a78 is signed (movsx).
10. 0x434e1f: the confirm dialog's callback is a retail VA held in DAT_00552ff4 (0x43723a, 0x43b101 …): the port runs it
    through engine_retail_call (an entry-patched row still shadows it).
11. 0x46521f: a marching army that changes region before its executor step hands the executor 0x4ad6c0's leftover
    locals as its uninitialized waypoint (col = 17, row = army 150's county's owner) and step state (151, army 150's
    owner) — extends quirk #229 / #232 (engine_army_state.c models only the executor-to-executor case).

## Seams

- TODO(rl-link) (a linked port exists but takes a port struct / queue): 0x4ac3c6 demolish, 0x49b2a3 / 0x448819 /
  0x449889 / 0x4acbd4 / 0x44aa41 / 0x44ae3c / 0x4508de (the turn's queue-posting passes), 0x4050c0 (via
  engine_map_scene_tick_frame where verified), 0x41b032 terrain info panel, 0x412143 workforce render, 0x4aa90a muster
  init, 0x438ceb county grid, 0x43a379 rations input, 0x43a23f / 0x43a307 / 0x43aa83 / 0x43ab25 county steps,
  0x4110b1 / 0x4116fb / 0x41152f / 0x411b72 county panels, 0x435cef armoury slider, 0x43b412 send-supplies target,
  0x43b622 auto resolve, 0x4abb36 enter region, 0x465d28 the executor (rhost: bound, see §Residue).
- TODO(rl-extern) (unported, outside every partition): 0x4b7514, 0x4b743b, 0x43e8f4, 0x42df30, 0x40902b, 0x43e330 /
  0x43e433 / 0x43e50d (MP tick minima), the delayed-action handlers DAT_004d5c98[50], the confirm callback VA.
- Other partitions by name (stubs or ports): rl_429b1d / rl_429ba4 / rl_4298c1 / rl_40e3ee / rl_40e6f2 / rl_40e7e4 /
  rl_402009 / rl_401f49 / rl_401136 / rl_426d11 / rl_427d19 / rl_412b33 / rl_4180f6 / rl_4184c6 / rl_428471 (ui4),
  rl_472bc5 / rl_46d5fe / rl_46a037 / rl_467770 / rl_46f700 / rl_46ff43 (terr), rl_476768 / rl_476710 / rl_47685d /
  rl_4984dc / rl_497ced / rl_499a1c / rl_499034 / rl_499e5d / rl_499dc3 / rl_49bae9 / rl_49caaa / rl_49aa0e (turn),
  rl_4bbbe3 / rl_4b27bf / rl_4aef7e / rl_4af502 / rl_4af8d9 / rl_4ba9c8 / rl_4a8426 / rl_4a8d9c / rl_4a9252 / rl_4aabd8 /
  rl_4b3768 / rl_4b1de0 (host), rl_43eda0 / rl_4323fe / rl_43292d / rl_4329a4 / rl_437002 / rl_438990 / rl_438a91 /
  rl_439079 / rl_439122 / rl_4393eb / rl_432893 / rl_40deca / rl_40dd92 / rl_43b4cb / rl_451f8a / rl_4330c6 / rl_42e26f
  (glue), rl_427990 / rl_42d91b / rl_4263ad / rl_4aecf0 / rl_4978ad / rl_4b11ce / rl_433cc1 / rl_497a34 (io), rl_4120e0 /
  rl_41438c / rl_438e3b (render), rl_4350a1 / rl_434174 / rl_439541 / rl_43958a / rl_4399b0 / rl_439edb / rl_4b7250 /
  rl_4b3994 (ui2), rl_40df62 (ui3), rl_42b780 / rl_42b7f7 / rl_42b919 (skirmish), rl_4b1897 / rl_4b1dd5 / rl_4bbea7 /
  rl_4bbf15 (misc), bf_43bf07 / bf_43c57d / bf_43c2a9 (input), bf_rng_step.
- Replaced (other partitions' one-line seams, now rl_ctl's ports): 0x452160 in rl_io.c (rl_451f70 returns its EAX:
  modelled), rl_glue.c, rl_misc.c, rl_render.c, rl_ui2.c; 0x451fcc in rl_io.c, rl_render.c, rl_ui2.c; 0x4398f5 /
  0x448648 in rl_ui2.c.
- Verified linked ports by name: the turn's passes (engine_army_ai_run_turn, engine_realm_*, engine_army_economy_*,
  engine_territory_*, engine_ranking_*), engine_combat_spawn_unit, engine_sendsupplies_commit,
  engine_workforce_row_sprite_index, engine_title_render_setup_apply_defaults / _clamp_player_count.

## Residue: 0x46521f → 0x465d28

The executor 0x465d28 never initializes four locals (quirk #229); called from 0x46521f they lie 0x20..0x2c below the
handler's entry esp (dir, outcome code, waypoint col / row) and hold what the previous handler's executor left — or,
after a region change, the four locals of 0x4ad6c0, called last at the same depth (`sub esp, 0x10`): its loop counters
j = 151 and k = 17 (var_C is reused: the region byte, then k), army 150's owner (+0, movsx) and that army's county's
owner. The executor then reads those as its waypoint (quirk 11 above). rl_46521f models the four as rl_46521f_res[]
(in, and out: unchanged / 0x4ad6c0's / the executor's); its row loads them with rh_res_get and stores them with
rh_res_set (written in replace; not compared in the default shadow — see the rhost limit below), and binds the
executor as the linked engine_army_move_run with the four values in its globals (g_army_move_*) and the event queue
(EVQ) — what rh_hooks_combat.c's move_run row does. The model is verified with rh_res_put under `--shadow-full`
(below). engine_army_state.c (the old driver port) does not model the 0x4ad6c0 case.

**rhost limit (shared-file need).** A residue slot BELOW the hooked function's own frame is not retail's in the
page-tracked shadow: the retail side's first write to each page faults, and the exception dispatch pushes its frames on
retail's stack — right there. Two effects: (1) comparing the slots afterwards fails (rh_res_put: retail 0 where the stack
held 0x013fff88, 44 k of 128 k calls), so the row stores with rh_res_set; (2) retail's own executor, called after
0x46521f's first record write has faulted, reads the exception frame's bytes as its waypoint instead of the pure run's
residue — 4 of 3.18 M shadowed calls took another path (the army's substate +0x14c 2 vs the port's 0, +0x14a, +0x150,
the facing +0x1b), and since the shadow keeps retail's state those two runs (bot3cen-s203 / s210) left the corpus.
Under `--shadow-full` (no faults) both vanish: the residue compared exactly (rh_res_put, an experiment build: 472 k
calls / 12 scenarios, 0 divergences) and the three diverging scenarios clean (687 k calls, 0 divergences, end states on
the corpus); the hybrid pass (the port alone) is bit-exact on all 59. A shared fix: a row flag that shadows that row
with the full copy (no faults), so a residue row is compared like any other.

## Rows

All 98 have a row. HOOK_R where a caller reads EAX and the value is modelled (the dirty marks return their fourth
argument — 0x452306's last `mov eax, [ebp+arg_C]`). RH_SH_SAMPLE on the hot leaves (the dirty marks, 0x44d281 /
0x44d303 ~1.4 M / 0.9 M calls a corpus) and on the per-frame present 0x452160 (RH_SH_MEDIA: a pixel shadow every
frame was the corpus's slowest row; the hybrid pass runs every call). RH_SH_MEDIA on everything that draws or calls
retail code that does — not on the per-frame 0x42ff10 (it draws nothing itself; the hybrid pass checks every frame).
HOOK_SMK_V (rh_hook_smk.h) on 0x432b05 (plays lom.smk) and 0x43ad25 (stops screen 34's movie). Router actions and the
pointer-table handler 0x46521f (no call site) are entry-patched, HOOK_V. 0x452160 is HOOK_R (EAX modelled: 0x42e26f's,
the frame step, Point.bottom, the blit's HRESULT or the canvas height − 1).

## Globals

`src/rl_ctl_globals.{h,c}` + `tools/rhost/rh_rl_ctl_alias.h`: generated (`gen_skeleton.py --part-globals ctl`;
`dedup_globals.py`: none shared); by hand: Point DAT_004ea180 widened to a RECT (16 B), word_4DC428 to the menu
bar's 3 × 16-B title records (retail's bytes). DAT_0053e8d0 (the MP ticks, a per-realm array) is engine_bf_globals.h's
scalar g_bf_53e8d0 — 0x43de70 indexes it through AT (exact under rhost; the standalone needs it widened). The dirty-rect six
(DAT_0050d09c …, DAT_0053e9ac) are generated but never used: the ports go through the bound mirror. Other partitions'
globals declared locally (rl_glue / rl_io / rl_ui2 / rl_ui3 / rl_render headers conflict in one unit).
