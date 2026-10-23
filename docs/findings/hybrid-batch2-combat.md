# Hybrid batch 2 — the battle resolver and army movement under rhost (2026-10-23)

Scope: `src/engine_combat.c` (the auto-resolve battle FUN_004aad07 and its callers / callees: sizing, dispatch,
contest, aftermath, ownership transfer, musters, sieges, occupation), `src/engine_army_move.c` (the per-tick
movement-step executor FUN_00465d28 and its classifier / evaluator / event handlers), and their unit / elimination
/ relation callees in engine_realm_upkeep / engine_ranking / engine_diplomacy. Wrappers:
`tools/rhost/rh_hooks_combat.c`; shared plumbing factored out of the pilot into `tools/rhost/rh_port.h`.

## Result

- **59 functions hooked, 50 reached, 0 divergences** (wine, `rhost.py verify --only .`: 522,472 shadowed calls over
  33 scenarios, nesting depth 1..6), and the **hybrid pass** — all 149 hooks of all tables in replace mode — reproduces
  the blessed corpus bit-exactly (state + every presented frame, 33/33). The AI pilot's 89 rows re-verified under the
  new wrappers: unchanged (88 reached, 0 divergences). Windows spot check (bot-s15-t20, bot-s32-t24): every hook
  shadowed → 0 divergences, end state = corpus; every hook replaced → corpus --check PASS.
- **Cost per function**: a table row + a one-line adapter, as in the pilot; `engine_army_move.c` linked in with two
  bridges (engine.c's screen byte / redraw mode) and one alias (engine_turn.c's DAT_00553fc8). The batch's real cost
  was the two shadow-fidelity problems below. Shadow ≈ 0.28 ms a call under wine, 0.12 ms on Windows; the whole
  verify (7 passes + hybrid) ≈ 4 min on wine -j12 — pass 2 (the executor's classifier / direction helper,
  ~150k calls each) dominates.

| hook | VA | depth | calls | scenarios |
|---|---|---|---|---|
| combat_resolve | 0x4aad07 | 1 | 32 | 23/33 |
| combat_clear_result | 0x4aa89f | 2 | 64 | 23/33 |
| combat_load_muster | 0x422c12 | - | 0 | 0/33 |
| combat_army_strength | 0x4ab2aa | 2 | 64 | 23/33 |
| combat_size_battle | 0x4a6a30 | 3 | 44 | 24/33 |
| combat_dispatch | 0x4a7158 | 6 | 4 | 4/33 |
| combat_aftermath | 0x4ab383 | 1 | 32 | 23/33 |
| combat_contest_region | 0x4a6c68 | 2 | 47 | 24/33 |
| combat_transfer_ownership | 0x4a72fe | 3 | 15 | 15/33 |
| combat_demolish_realm | 0x4ac3c6 | 3 | 26 | 24/33 |
| combat_transfer_army | 0x437535 | - | 0 | 0/33 |
| combat_recompute_condition | 0x4a80db | 2 | 32 | 23/33 |
| combat_halve_garrison | 0x4ad8cc | - | 0 | 0/33 |
| combat_merge_stack | 0x4aa181 | 3 | 4 | 4/33 |
| combat_capital_defender | 0x46d42c | - | 0 | 0/33 |
| combat_muster_defender | 0x4a50ae | 3 | 67 | 25/33 |
| combat_prime_muster | 0x4aaa80 | 4 | 173 | 30/33 |
| combat_raise_region_unit | 0x4a5003 | 3 | 5 | 5/33 |
| combat_garrison_realm | 0x4a07f2 | - | 0 | 0/33 |
| combat_deploy_cell | 0x4a689d | 3 | 33 | 5/33 |
| combat_place_garrison | 0x4a79a3 | 2 | 4 | 4/33 |
| combat_settlement_slot | 0x4a551b | 2 | 4 | 4/33 |
| combat_break_siege_link | 0x43b917 | 1 | 123 | 24/33 |
| combat_reinforce_keep | 0x4a78ea | 3 | 116 | 30/33 |
| combat_begin_siege | 0x4a7ca2 | - | 0 | 0/33 |
| combat_region_event_hi | 0x4686a0 | 2 | 116 | 30/33 |
| combat_enter_region | 0x4abb36 | 1 | 94 | 25/33 |
| combat_notify_occupation | 0x4abf77 | 2 | 94 | 25/33 |
| combat_settle_region | 0x4abd0f | 1 | 4337 | 2/33 |
| combat_spawn_rebellion | 0x4ac185 | 2 | 26 | 24/33 |
| combat_resign_player | 0x49cf20 | - | 0 | 0/33 |
| combat_mp_resign_sweep | 0x49b2a3 | 1 | 512 | 31/33 |
| combat_rotate_merc_offers | 0x4aca2b | 1 | 478 | 29/33 |
| unit_clear_contract | 0x4ac6fe | 2 | 87 | 26/33 |
| unit_disband | 0x4aa039 | 1 | 2 | 1/33 |
| unit_remove | 0x46f0a9 | 1 | 24 | 22/33 |
| unit_absorb | 0x4aba5a | 1 | 1195 | 29/33 |
| unit_map_unlink | 0x46ec6e | 2 | 87 | 26/33 |
| unit_map_link | 0x46eddf | 4 | 16349 | 30/33 |
| unit_recompute_wage | 0x4ad52b | 2 | 1348 | 29/33 |
| realm_refresh_wages | 0x4ad426 | 2 | 32 | 23/33 |
| rank_check_elimination | 0x49b42b | 2 | 1009 | 30/33 |
| diplo_battle_adjust | 0x4a1ee1 | 2 | 32 | 23/33 |
| move_run | 0x465d28 | 1 | 135034 | 30/33 |
| move_classify_step | 0x46634d | 2 | 149753 | 30/33 |
| move_eval_dir | 0x466893 | 3 | 16516 | 30/33 |
| move_classify_cell | 0x466c3c | 4 | 16516 | 30/33 |
| move_eval_occupied | 0x4658c1 | 5 | 244 | 30/33 |
| move_commit_step | 0x466d84 | 3 | 16349 | 30/33 |
| move_terrain_passable | 0x469e0e | 1 | 26 | 18/33 |
| move_decay_cell | 0x469e5b | 4 | 10 | 3/33 |
| move_blocked_branch | 0x46673c | 3 | 12 | 3/33 |
| move_special_tile | 0x468ae2 | - | 0 | 0/33 |
| move_region_event_lo | 0x46873f | - | 0 | 0/33 |
| move_combat_engage | 0x4296b5 | 2 | 47 | 24/33 |
| move_arrival | 0x467196 | 2 | 12 | 9/33 |
| path_direction_8way | 0x46f434 | 2 | 151175 | 30/33 |
| terr_mark_draw_box | 0x46e067 | 1 | 10035 | 31/33 |
| ai_run_turn_twin | 0x49df48 | 2 | 32 | 23/33 |


## What the shadow / binding found

- **#229** — FUN_00465d28 computes its step direction from two locals it never initializes on a disarmed pass;
  they hold the previous call's waypoint (often another army's), and "standing on it" stops this army. The
  port zero-initialized them. Now explicit state, moved through retail's stack residue by the wrapper.
- **#230** — the overrun of an idle hostile stack hits the victim's relation toward the mover; the port had the
  two owners swapped (read from the disassembly; the corpus never overruns).
- **#231** — the battle-setup outputs (role, sizes, owners, muster scratch, anim combatants) are save slots, and
  the battle screens are the engine's own screen byte / redraw mode / turn-progress; the port wrote private
  mirrors nothing read.

## Tooling: what the shadow needed (in `rh_port.h` / `rh_hooks_ai.c`, not `rh_state.c`)

| problem | symptom | fix |
|---|---|---|
| the port side calls back into retail code that uses rhost's HOST layers (FUN_00427990 loads a WAV cue and creates a DirectSound buffer): the shadow rolls back retail memory but not the media pool, buffer serials or the fake-handle sequence | 2 dwords differ in an arena `ds_buf` (media pointer, serial); the run ends off the blessed state | `rh_media_meta_*`, `rh_ds_meta_*`, `rh_handles_meta_*` (like `rh_heap_meta_*`); `rh_port_run` starts the port run from the host state retail's run started from and puts retail's back after |
| the wrapper's C frame sits where retail's callee frame would be: retail functions that read uninitialized locals see the wrapper's garbage, not what a pure run leaves there | 0 divergences reported, yet 14 of 33 scenarios end off the blessed state (bisected by `--hash-every 1` + `--dump-at` + `--trap 466116`) | naked entry stubs: retail runs AT the caller's esp again (`rh_call_at`), the C machinery and the port 256 KB below (below any hook machinery already running); the residue a port function models is moved with `rh_res_get` / `rh_res_put` (replace: written; shadow: compared) |

Both belong in `rh_state.c`'s shadow eventually (they now cover every port-hook table, not `rng_step`). Not
covered yet: `rh_win`'s virtual message queue and `rh_files`' overlay bookkeeping (no seam in this batch posts or
writes files); exception frames the page-tracked shadow's write faults push below a faulting retail function.

## Seams → retail

The executor's deeper handlers bind to their ports (arrival, settle-deposit / contest, region lo / hi, special
tile); what has no port binds to retail's own function, bracketed by `rh_port_retail_enter/leave` (store the
port's mirrors and an open event queue, reload after): the DirectSound footstep / alert / present calls
(0x426120 / 0x426216 / 0x426360), the WAV cue loader 0x427990 (ff_batl, s080_0x, ff_lose), the territory-captured
click 0x499aca, the game-over screen 0x43eda0(0x24, 0); the battle clocks read rhost's virtual timeGetTime.

## Never called by the corpus

9 rows: `combat_load_muster`, `combat_transfer_army`, `combat_halve_garrison`, `combat_capital_defender`,
`combat_garrison_realm`, `combat_begin_siege`, `combat_resign_player`, `move_special_tile`, `move_region_event_lo` —
what reaches each (battlefield, sieges, owned counties, an Options toggle, MP) is in `docs/human-traces.md` §A
(evidence for A1). Battlefield-only: `halve_garrison` and the aftermath's `render` / captured-loser branches.
Unported retail on the battle path (next): the AUTO_RESOLVE button FUN_0043b622, the battlefield end FUN_0043be65 /
FUN_00477dfc, the siege assault FUN_004a8aab, and the army-state machine FUN_004650b0 + its handlers (ported in
`engine_army_state.c`, not yet hooked).
