# Hybrid batch 3 — army-state machine, battle exits / siege assault, realm events under rhost (2026-10-23)

Scope: `src/engine_army_state.c` (the per-tick army-state driver FUN_004650b0 and the turn driver's movement arming /
siege-build / raid / march planners), the battle path's unported exits (AUTO_RESOLVE button FUN_0043b622, battlefield
end FUN_0043be65 / FUN_00477dfc, the siege assault FUN_004a8aab — ported in `src/engine_combat.c`), and the season
pass's realm events (`src/engine_realm_events.c`) plus its remaining realm passes. Tables: `tools/rhost/rh_hooks_state.c`
(9), `rh_hooks_events.c` (32), `rh_hooks_battle.c` (6).

## Result

- **47 hooks added; 196 in all.** `rhost.py verify --target wine -j 8 --only .` over the 36-scenario corpus (33 +
  the bot v2 games bot2-s61-t28, bot2-s64-t28, bot2cen-s72-t30): 162/196 reached, 0 divergences, 34 never called;
  **hybrid pass PASS** (all 196 replaced, corpus bit-exact, state + every frame).
- Batch-3 rows reached by the corpus: 19 of 47. The other 22 event handlers + the 2 territory probes were shadowed
  under exploration (below); 4 battle rows wait for the battlefield.

| hook | VA | depth | calls | scenarios |
|---|---|---|---|---|
| state_run | 0x4650b0 | 1 | 307738 | 34/36 |
| state_arm_movement | 0x4a4f5b | 1 | 46925 | 33/36 |
| state_siege_build_step | 0x4a8507 | 2 | 88201 | 33/36 |
| state_scan_siege_built | 0x4a84ba | 1 | 589 | 33/36 |
| state_raid_assign_target | 0x4ac5ba | 2 | 16 | 2/36 |
| state_plan_raids | 0x4ac499 | 1 | 564 | 32/36 |
| state_plan_capital_marches | 0x429418 | 1 | 588 | 33/36 |
| state_plan_settle_marches | 0x4280e9 | 1 | 564 | 32/36 |
| state_find_settle_spot | 0x428007 | 1 | 114 | 34/36 |
| battle_auto_resolve | 0x43b622 | 1 | 32 | 23/36 |
| battle_siege_assault | 0x4a8aab | 1 | 1 | 1/36 (bot2-s64-t28) |
| battle_end | 0x43be65 | - | 0 | 0/36 |
| battle_end_tick | 0x477dfc | - | 0 | 0/36 |
| battle_pick_result_row | 0x478419 | - | 0 | 0/36 |
| battle_stage_siege_side | 0x4aa6ca | - | 0 | 0/36 |
| events_roll | 0x448819 | 1 | 601 | 34/36 |
| ev_134 / ev_13c | 0x44949b / 0x4497ad | 2 | 1 / 1 | 1/36 each |
| the other 22 handlers (ev_87 … ev_13d) | 0x448e12 … 0x449826 | - | 0 | 0/36 |
| events_claim_empty / events_vacate_owned | 0x44c77a / 0x44c80a | - | 0 | 0/36 |
| rank_snapshot_metrics | 0x4486a9 | 1 | 601 | 34/36 |
| realm_pay_upkeep | 0x4acbd4 | 1 | 601 | 34/36 |
| realm_coalition_pass | 0x44ae3c | 1 | 601 | 34/36 |
| terr_census_all | 0x469b51 | 1 | 601 | 34/36 |
| terr_allocate_all | 0x44f699 | 1 | 1202 | 34/36 |

`combat_resolve` / `combat_aftermath` moved from depth 1 to 2: the AUTO_RESOLVE button (now hooked) is their caller.
The bot's battles go through it — screen 0x12 is the pre-battle choice, the bot always auto-resolves; the per-frame
battlefield end check (FUN_00477dfc, DAT_0057c8fc == 2) only runs once "take the field" is chosen.

## What the shadow found

- **#232** — the army-state handlers call the move executor from two stack depths (states 1/2 reserve a local, 3/4
  do not), so its uninitialized waypoint (#229) is read 4 bytes apart: an idle stack after a marching one reads the
  marcher's outcome code / waypoint column. The executor now keeps all four locals as state (dir / code / wp col / row;
  the direction is written twice — also on the arrival exit, which the four-slot move_run shadow caught), the driver a
  residue window. Without the depth shift the `state_run` shadow diverges in 5 scenarios.
- **#233** — the raid planner's goal is the path-trace goal +0x16/+0x17; the port used +0x116/+0x117 (disassembly).
- **#234** — the coalition pass's group table 0x568240 and count DAT_00553ef4 were engine_realm.c file-statics: the
  `realm_coalition_pass` shadow diverged on every call (523/523). Now `src/engine_realm_globals.c`, aliased by rhost.
- **#235** — the bot v2 games exposed two more #226 scratch writers in batch-1/2 code: the capital-defender box scan
  FUN_0046d42c counts in the cell cursor DAT_00569570/78, and the AI's nearest-army scanners FUN_004673b8 / FUN_00467532
  go through the chebyshev helper (|dx| / |dy| in DAT_00591560 / DAT_0058fea0).
- Not a bug: the per-event handlers, the territory probes and the siege-build step match retail as ported.

## Exploration: the realm-event handlers

An event rolls only for a county whose owner has strategic +0x05 set (the human) after 1268; the bot's human holds at
most one of 4 counties, so 2 handlers fire in the corpus (ev_134 / ev_13c, bot2 games). 36 rhbot sessions replaying
bot games (30 with every event row shadowed, 6 with only the probes) with player 0's +0x05 poked to 1 (the unowned
counties roll too; poking AI players stalls the turn — they then count as humans) and the handler rows shadowed: all
24 handlers, 156 calls, and in isolation claim_empty 2 / vacate_owned 5 — 0 divergences.
Not corpus evidence (the pokes change the game); scratch script, not committed.

## Tooling

- `rh_res_set` (rh_port.h): residue a port models but retail's other frames may overwrite — written in replace mode,
  not compared in shadow (the state dwords still are). Up to 8 residue puts per call (was 4).
- Hooks need an E8 call site: the army-state handlers (FUN_0046521f / 00465486 / 00465622 / 00465761) are reached
  only through PTR_FUN_004d6a50 and are verified as part of `state_run`. Gap: no table-slot (pointer) hooks.
- `tools/rhost/build.sh`'s link-cut comment does not mention `engine_realm_globals.c` (aliased in rh_hooks_events.c),
  nor `g_engine_state` (engine.c, aliased in rh_hooks_battle.c): build.sh is the owner's file outside PORT_OBJS.
- Replace mode returns 0 from void rows; FUN_0043b622 / FUN_0043be65 return garbage that UI-handler wrappers pass up
  (`return sub_43B622();`). Harmless in the corpus (hybrid PASS); keep in mind if a button dispatcher reads it.
- Seams to retail (rh_hooks_battle.c): siege result FUN_0042c64f, return to map FUN_004788c6, casualty writeback
  FUN_0047f474, result panel FUN_00423241, stream stop FUN_00426d11, result movie FUN_0042d91b (called with retail's
  own name string from its 16-byte-stride tables), message FUN_0043eda0. None posts to rh_win's queue or opens files
  in the corpus (unreached); FUN_0042d91b loads a Smacker movie and FUN_004788c6 rebuilds the map — when the
  battlefield rows get calls, check the shadow's rh_files / rh_win roll-back covers them.

## Never called — what reaches them

| rows | reached only via |
|---|---|
| battle_end_tick, battle_pick_result_row, battle_end | the battlefield ("take the field" at the 0x12 choice) → FUN_00477dfc every frame, its end buttons → FUN_0043be65 — human-traces A1 or a bot that fights |
| battle_stage_siege_side | a siege assault fought on the battlefield (DAT_0053f284 / both garrisoned); bot2-s64-t28's assault took the quick path — A1 + A2 |
| 22 ev_* handlers, events_claim_empty / _vacate_owned | the human owning counties past 1268 (≈10 %/county/season) — a longer bot game that keeps its counties, A3 |
