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
Callbacks an env hands the old port: an old port → call it directly; retail-layout code → bracket with
`rl_link_loc_store(); rl_link_retail_enter(); rl_<x>(); rl_link_retail_leave(); rl_link_loc_load();`.
Pilot: `rl_40f7d3` (strat_draw_territory_panel) and `rl_41a734` (End-turn strip), 7 seam sites.

## Left (144 sites over 81 VAs, 2026-11-16) — by env family
- **screens** (rh_hooks_screens.c `scr_bind`: title render, county panels, trade, treasury, noble faces, armory,
  labor, workforce HUD, MP render, battlefield render, tooltip): 0x409397 0x409c93 0x4110b1 0x41152f 0x4116fb 0x411b72
  0x411fde 0x412143 0x412666 0x4148e4 0x4149ec 0x416308 0x41635f 0x416925 0x416cf3 0x417ea7 0x4181eb 0x418426
  0x418653 0x418fc5 0x4190db 0x419243 0x41a900 0x41ab67 0x41ac3e 0x41dedb 0x41e7e1 0x41eaa3 0x41ece6 0x41f01f
  0x41f15a 0x41f1dd 0x41f321 0x41f5e8 0x41f6c7 0x41f86d 0x41f98b 0x41fbcb 0x423b4f 0x435cef 0x438ceb 0x43a23f
  0x43a307 0x43a379 0x43aa83 0x43ab25 0x4517ca 0x476e95 0x477249 0x4aa90a 0x4ae5e2 0x4b3ec0 0x4b3f0a 0x4bc107
  0x4bc142 0x4bc1d1. The bind block (fonts, sheets, the title / roster / editor seams, the battlefield tables) moves
  to `rl_link` as one `rl_link_screens_bind`; its retail callbacks (0x41e61d, the title pages, 0x410c71, 0x49caaa,
  0x46c4a3, 0x4997bf, 0x4afab9) all have rl_ ports. Its module-global aliases (`g_tr_*`, `g_bfr_*`, `g_arm_*` …)
  are rhost asm — a build without rhost keeps them in the modules' `_globals.c`, so they need no move.
- **mapview** (mv_bind / TI): 0x4050c0 0x410aa9 0x410cbd 0x41b032 0x4b4056.
- **logic, EVQ / combat seams** (rh_hooks_ai / combat / battle / events / economy / state): 0x428007 0x4357a6
  0x437535 0x43b412 0x43b622 0x448819 0x449889 0x44aa41 0x44ae3c 0x4508de 0x465d28 0x46eddf 0x477dfc 0x49a581
  0x49b2a3 0x4a8aab 0x4abb36 0x4ac3c6 0x4acbd4 — `rl_link_evq_load` + the combat / battle seam binds
  (`rh_combat_bind_seams` …) to move.

Product gap beyond the seams: `rh_port_bind_once` (the modules' runtime pointer binds) and the table binds still run
only under rhost; `rl_link` needs a `rl_link_bind_once` called from the port's start for a build without rhost.
