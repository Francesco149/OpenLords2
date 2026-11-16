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

## Left (2026-11-16, after screens: 36 sites over 24 VAs) — by env family
- **mapview** (mv_bind / TI): 0x4050c0 0x410aa9 0x410cbd 0x41b032 0x4b4056.
- **logic, EVQ / combat seams** (rh_hooks_ai / combat / battle / events / economy / state): 0x428007 0x4357a6
  0x437535 0x43b412 0x43b622 0x448819 0x449889 0x44aa41 0x44ae3c 0x4508de 0x465d28 0x46eddf 0x477dfc 0x49a581
  0x49b2a3 0x4a8aab 0x4abb36 0x4ac3c6 0x4acbd4 — `rl_link_evq_load` + the combat / battle seam binds
  (`rh_combat_bind_seams` …) to move.

Product gap beyond the seams: `rh_port_bind_once` (the modules' runtime pointer binds) and the table binds still run
only under rhost; `rl_link` needs a `rl_link_bind_once` called from the port's start for a build without rhost.
