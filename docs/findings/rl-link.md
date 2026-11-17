# The link layer: retail-layout code calling old ports (FRONT ★5, 2026-11-16)

`src/rl_link.{h,c}` (+ `rl_link_globals.{h,c}`). The TODO(rl-link) seams are rl_ functions that reach an OLD port
(an `engine_*` module) through `engine_retail_call(VA)`: the old port keeps retail globals in file-static mirrors,
takes the event queue as a struct, or draws through a bound environment, and until now only rhost's row wrappers
(`tools/rhost/rh_hooks_*.c`) supplied that. Under rhost the seam works (hybrid: the VA's entry patch; standalone: its
thunk → the row's wrapper), but the code that makes it work lived in rhost, not in the port.

## What moved into src
- **Mirror sync** (`rl_link_sync_in` / `_out`): the 27 mirrored retail globals (economy eval / supply scratch, the
  path distance, the cell cursor, the territory slot, the ranking cookie, the dirty rectangle `rl_link_dirty`, the
  path field's bookkeeping, the army-state walk), by port symbol (`g_rl_<va>` / `g_bf_<va>`; seven had none:
  `rl_link_globals.h`, aliased in `rh_hooks_ai.c`). Store-if-changed against the values at sync_in, as before.
- **Liveness** (`rl_link_port_enter` / `_leave`, `rl_link_retail_enter` / `_leave`): rhost's `ctx_port` and
  `rh_port_retail_enter` / `_leave` now call these — one live flag for the row machinery and the port.
- **Event queue** (`rl_link_evq_load` / `_store`): the ring is `g_misc_128` + `g_rl_568500` (two symbols; contiguous
  only under rhost's aliases), copied in two pieces.
- **Environments**: `rl_link_loc_load` / `_store` (the walker over `g_rl_591580` / `g_rl_5c9270`) and
  `rl_link_panel_env` (engine_hud_panel). rh_hooks_mapview.c's `PANEL` rows and terrain-info env use them; the
  panel's workforce callbacks now go port to port (`engine_workforce_compute_rows`, `rl_4120e0`) instead of retail VAs.

`rh_port_sync_in` / `_out`, `rh_evq_load` / `_store`, `rh_port_retail_enter` / `_leave` are one-line forwards.
Refactor alone: `verify` (wine; econ-tour, strat-tour, mp-leave-p1, realm-tour): 1068/1937 rows reached, 0
divergences, hybrid PASS.

## The pattern (per old-port entry)
`int rl_<va>(args) { OLD_PORT(<env bind>, <old port call>); return 0; }` — `rl_link_port_enter` (a live caller's
mirrors stored first), the env bound, the call, the walker stored, `rl_link_port_leave`. The VA's existing row
switches its CALL to `rl_<va>` (the row then verifies the src entry, env included); the seam sites call `rl_<va>`.
Callbacks an env hands the old port: an old port → call it directly; retail-layout code → hand over the walker only:
`rl_link_loc_store(); rl_<x>(); rl_link_loc_load();`. rl_ code runs with the mirrors LIVE (it reads / writes them by
name: `g_cell_cursor_col`, `rl_link_dirty` …), and its own calls into retail are bracketed by engine_retail_call's
binder; a `rl_link_retail_enter / _leave` around an rl_ callee reloads the mirrors over its writes (the first screens
verify: 5 rows diverging on 0x50a4bc..0x50d09c / 0x53e9ac / 0x569570 — fixed, see §Screens).
Pilot: `rl_40f7d3` (strat_draw_territory_panel) and `rl_41a734` (End-turn strip), 7 seam sites.

## Screens (2026-11-16) — `src/rl_link_screens.c`
rhost's `scr_bind` and the per-module context builders (tooltip state, noble faces, title editor / seats, county
panels, trade, armory, labor, treasury; the battlefield renderer's tables) moved to src: `rl_link_screens_bind` plus a
retail-layout entry `rl_<va>` for every one of the 104 rows of `rh_hooks_screens.c` (the rows now call them;
`rh_hooks_screens.c` keeps only the module-global aliases and the link-cut io / gfx bridges, which now call their rl_
ports). The env's retail callbacks (0x41e61d, title pages 6 / 8..13, 0x410c71, 0x49caaa, 0x46c4a3, 0x4997bf,
0x4afab9, the county flips, 0x43eda0, the armory's 0x4b11ce / 0x4996c8 / 0x452160 / 0x4b0ab5) call rl_ ports; the
county panels' map recomposite 0x4050c0 stays a seam (mapview family). An entry runs between `rl_link_port_enter` /
`_leave` (the dirty rectangle is a mirror) except the pure leaves (bf pixel ↔ cell helpers, the workforce row
arithmetic, the backdrop walk, the setup defaults, the minimap context bind). Five fixed tables had no port symbol:
`g_rl_4e3bf0 / 4e4030 / 4e4470 / 4e4520 / 4d2c88` (`rl_link_globals.h`, aliased in rh_hooks_ai.c). All 56 screens VAs'
seam sites call `rl_<va>` (rl_ctl / glue / host / io / mpmsg3 / mpnet / mpnet2 / render / skirmish / turn / ui2 / ui4).
Verify (wine, `--hooks '^scr_'`, 12 scenarios: the tours, title-setup, tooltips, field-orders, castle-labor,
mp-start-p1, skirmish-tour, grain-and-save): 99/104 rows reached, 0 divergences, hybrid PASS (5 never called in these 12:
bf_terrain16 / bf_overlay16 — the zoom-16 path —, bf_marker, bf_col_delta / bf_row_delta).

## Mapview (2026-11-16) — `src/rl_link_mapview.c`
rhost's `mv_bind` / `ti_env` moved to src: `rl_link_mapview_bind` (the tile grid `g_rl_55cea0`, the path field
`g_rl_500c30`, the palette / minimap colour tables, the army label's font + char maps, the log 0x4afab9, the clock =
`rl_plat_GetTickCount`, the dirty rectangle) and `rl_link_mapview_ti_env` (the terrain-info panel: its callbacks
0x41b19d / 0x41cb3a / 0x41d299 / 0x41da2f / 0x426216 / 0x4af8d9 / 0x452160 are rl_ ports, handed the walker only),
plus an `rl_<va>` entry for 58 of `rh_hooks_mapview.c`'s 64 rows (the rows call them). No entry: the empty-cell edges
0x42a8d1 / 0x42a9ab and the special stamp 0x41c996 (own rl_ ports), the parchment 0x409397 (the screens family's), the
panel pilots 0x40f7d3 / 0x41a734 (rl_link.c). The blit / strip kernels and the 4-byte fill are pure leaves (no
port_enter). Four fixed tables got port symbols: `g_rl_4d8108 / 4d28f8 / 4d2ec8 / 4d4230`. All seam sites of the five
VAs (0x4050c0 0x410aa9 0x410cbd 0x41b032 0x4b4056) call the entries.
Verify (wine, `--hooks '^mv_'`, 14 scenarios, before the logic family): 52/64 rows reached, 0 divergences, hybrid PASS (12 never called in these 14: blits 0x460501 / 0x46079d / 0x4608de, strips 0x460a1c / 0x4611ef / 0x461623 / 0x461a54 / 0x461efb / 0x463fa5, the army label, panel q1 type 2 / q2 type 5). Standalone (wine): 121/121 = pure retail.

## Logic (2026-11-16) — `src/rl_link_logic.c`
`rl_link_bind_once` = rhost's `rh_port_bind_once` body (the table / storage binds: army records, cell roster /
descriptors, territory records, the AI score table inside the county records, tile grid, path-field storage, the
dirty rectangle) + the combat / battle / economy seam binders, whose callbacks were retail VAs behind
`rh_port_retail_enter / _leave` and are now rl_ ports (0x426360 0x426216 0x427990 0x499aca 0x43eda0 0x42c64f 0x4788c6
0x47f474 0x423241 0x426d11 0x42d91b 0x416308 0x4b3714 0x435ebc / 0x4aa90a, bf_426120). An old port's callback into rl_
code hands over an OPEN EVENT QUEUE (`rl_link_evq_callout_enter / _leave`: stored before, reloaded after — the callee
may post into retail's ring; the mirrors stay live as for every rl_ callee). rhost's `rh_port_bind_once` keeps the
save-schema check, `rh_bf_bind_seams` (engine_retail_call's binder) and the tables' binds; `rh_combat / _battle /
_economy_bind_seams` are gone. 18 entries `rl_<va>` (EVQ where the old port takes a queue): 0x448819 0x449889 0x44aa41
0x44ae3c 0x4508de 0x4acbd4 0x46eddf 0x49a581 0x4ac3c6 0x437535 0x49b2a3 0x4abb36 0x4a8aab 0x43b622 0x477dfc 0x428007
0x43b412 0x4357a6; their rows and all seam sites call them. `g_rl_4d9278` (the result-movie names) is new.
**0x465d28 stayed a seam** (until 2026-11-17, §Left): engine_army_move_run's waypoint / direction locals are its
caller's stack residue (quirks #229 / #232).
`rl_link_bind_all` (logic + mapview + screens binds) runs from `rl_crt_start`: the port's start for a build without
rhost (under rhost: no-ops, rh_port_bind_once ran them).
**`rl_link_port_leave` now reloads for a live caller** (as `rl_link_retail_leave` does). The first standalone after the
switch had 47 / 121 scenarios off pure retail in state_sem only (frame / tick / RNG / every frame equal): one dword,
`g_gfx_char_counter` 0x58fd78 (bot-s1-t8: 39 extra characters dispatched at the f5627 click). Bisected (call sites
back to `engine_retail_call` by group) to rl_turn.c's direct `rl_49a581()` (the AI's per-tick pass). The seam path ended
in the binder's `rl_link_retail_leave` = a mirror reload for the rl_ caller; the direct entry ended in `port_leave` =
store-if-changed only, so a mirrored global the callee wrote through its retail symbol left the caller's mirror stale.
The hybrid did not see it: the function holding that call site has no row, so it runs retail there.
Verify (wine, every row of the mapview + ai / combat / battle / events / economy / state tables, 34 scenarios, after
the fix): 270/281 rows reached, 0 divergences, hybrid PASS, 0 failed runs. Standalone (wine): 121/121 = pure retail.

## Direct calls, the gfx entries, live from the start (2026-11-16, later)
- **Literal seams 82 → 3.** 69 `engine_retail_call(0xVA, …)` sites in src whose VA already had a retail-layout port
  (`rl_<va>`; their notes said TODO(rl-extern) / TODO(bf-link)) now call it — 39 VAs (MP session / link, io, the movie
  stop, the slider render, the gift / tribute, …) — plus 10 sites of the 7 new entries below. `rl_43fd2a` takes no argument (retail pushes none;
  the old seam passed the previous call's EAX). rl_host.c's `rcall0..2` are gone (0x41f4a1 → `rl_41f4a1`).
- **New entries**: gfx leaves in `rl_link_mapview.c` — `rl_40a567` (sprite opaque from blob), `rl_409934` /
  `rl_409c93` (the chrome panel twins), `rl_402f64` (decimal label), `rl_40cfd2` (engine_button_render_list: the
  misc sheet DAT_005530c8, system2.pl8, `rl_link_dirty`; the pressed mark = `rl_452775`, bound in
  `rl_link_mapview_bind`); logic EVQ entries `rl_4ab383` (combat aftermath) / `rl_43be65` (battlefield end) in
  `rl_link_logic.c`. Their rows (rh_hooks_gfx / _combat / _battle) call them; rh_hooks_gfx.c's retail pressed-mark
  seam (its own dirty copy) is gone.
- **The mirrors are live from the standalone's start** (`rl_crt_start`: `rl_link_port_enter()`, never left). Found by
  the switch: 20 MP sides off pure retail in state_sem, two dwords — the cell cursor 0x569570 / 0x569578. Bisected to
  the startup speed probe rl_40f10f calling `rl_4b209a` directly; `--watch-write` + `rhs.py`: the only store of the
  cursor written by rl_46c4a3 (a mirror write by name) was that seam's `rl_link_retail_enter` sync_out. Before the
  first seam the link layer was NOT live, so a mirror write by rl_ code was reloaded over by the next port_enter. Under
  rhost every rl_ row runs live (ctx_port); now the standalone does too.
- **Walker hand-off.** The first verify (12 scenarios) had 3 diverging rows: button_render_list / rl_4b99c0, 1 dword,
  the walker DAT_005c9270. `seam_pressed_mark` wrapped `rl_452775` in CALLOUT (store the walker, call, load), but
  `rl_40cfd2` (MV) never loads one: the store wrote a stale offset. engine_button.c holds no walker — the seam now
  calls `rl_452775` bare. Verify after (wine, 12 scenarios): 1501/1937 reached, 1 diverging — rl_439b52's EAX (r 0x11111111 /
  p 0: 0x412143's seal-draw leftover on the selected-county path; every caller drops it — row → HOOK_V; strat-tour /
  econ-tour / tooltips after: 973 reached, 0 divergences), hybrid PASS.
- Left in src: `engine_retail_call(0x465d28)` (below), the two CRT debug entries (rl_io_plat.c, ★4), and the
  dynamic-VA dispatches (retail tables of action / callback VAs: rl_glue's pull-down item action
  (0x40dd92) and the tile drawers' pick (0x42a8d1 / 0x42a9ab), rl_ctl's delayed actions 0x4d5c98 and DAT_00552ff4, rl_ui4's hot-spot action (the routers' hs_fire), rl_mpmsg's handler tables 0x4d57f0 /
  0x4d5980, rl_mpnet's CRT helpers) — the standalone reaches them through the entry thunks (the CRT helpers: ★4).

## Calls by retail VA (2026-11-17) — `src/rl_va.c`, `src/rl_va_map.c`
The dynamic-VA dispatches (retail code pointers held in data or set at run time) call `rl_va_call(va, n, …)`: a
sorted map VA → adapter, GENERATED by `tools/gen_rl_va_map.py` from rhost's row tables (every row whose adapter is
a plain `rl_<va>` / `bf_<va>` call on argument slots, HOOK_R / HOOK_V / HOOK_SMK*: the adapter is the row's — HOOK_R
returns the port's value, HOOK_V 0). 1516 VAs. The pre-commit hook regenerates it when the row tables change. A miss
goes to `engine_retail_call` (retail under rhost, the entry thunk in the standalone) and is logged once per VA
(`rl_va miss VA` in rhost.log). Sites: rl_mpmsg's handler tables 0x4d57f0 / 0x4d5980 (fallback of k_mp_build /
_recv), rl_ctl's delayed actions 0x4d5c98 and the confirm callback DAT_00552ff4, rl_ui4's hot-spot action (hs_fire),
rl_glue's pull-down item action (0x40dd92) and the tile drawers (0x42a8d1 / 0x42a9ab).
The first standalone run with the map (121/121 = pure retail) missed 21 VAs: bf_ rows (retail-layout, now taken), the
movie row 0x432b05 (HOOK_SMK_V, now taken), and the economy screens' hot-spot actions, served by OLD ports — they got
link entries in `rl_link_logic.c` (LG / EVQ; their rows call them): trade grid 0x435339 / 0x43543d / 0x4355db /
0x435541 / 0x435286 / 0x4352f2 / open 0x43530e, gift arrow / seal 0x436372 / 0x436408 (the editor struct over
retail's globals, as the row's helper did), county-action strip 0x43ae30, Send Supplies 0x43b27a / 0x43b1ca /
0x43b32a. Left off the map: the delayed actions 0x4479e9 (reads an uninitialized local) and 0x448339 (leaves its
three locals) — their rows model stack residue (rh_res_get / _set) that only rhost's real stack carries.

## Left (2026-11-17)
- ~~0x465d28~~ — done 2026-11-17: the executor's residue was already the caller's model (rl_46521f_res, quirk #238;
  rl_terr's handlers 2..4 keep it in g_army_move_*); only the binder (rhost's `ctl_move_run` constructor) lived in
  rhost. Now the link entry `rl_465d28_res(res)` (rl_link_logic.c, EVQ) — rl_46521f calls it. Verify (wine, 8
  scenarios, rows rl_46521f + move_run): 0 divergences, hybrid PASS; standalone 121/121 = pure retail, 0 rl_va
  misses. **No literal retail seam left in src but the two CRT debug entries (★4).**
- ~~0x4479e9 / 0x448339~~ — done 2026-11-17: the dispatcher models the handlers' shared slot (quirk #241, rl_ctl.c
  `da_run`) and calls both directly; standalone `--only '^mp-'` 39/39 = pure retail, 0 rl_va misses.
- rl_mpnet's CRT helpers (operator new, _splitpath, sprintf: literal VAs, ★4).

Product gap: rhost-only binds left are engine_retail_call's binder and bf's sleep (`rh_bf_bind_seams`) — a build
without rhost has no retail to call.
