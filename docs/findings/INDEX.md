# Findings index

Map of the per-subsystem RE writeups under `docs/findings/`. Each doc
captures the **why** behind a port: which Ghidra addresses cover which
behavior, calling conventions, globals touched, and the subtle behavior
the obvious port would miss. Findings are the *durable* output of a
session — they outlive the conversation context.

> New to the repo? Start at [`../STATUS.md`](../STATUS.md) for a
> 60-second orientation, then [`../PLAN.md`](../PLAN.md) for goal +
> phased roadmap. This index is the map of the deep dives.

## Bootstrap & main loop (PLAN §2)

| doc | covers |
|-----|--------|
| [winmain-pre-init.md](winmain-pre-init.md) | WinMain pre-init chain (earliest setup before the window) |
| [winmain-and-bootstrap.md](winmain-and-bootstrap.md) | WinMain & bootstrap chain — window creation, class registration |
| [wndproc.md](wndproc.md) | WndProc message handler (`FUN_004b29be`) |
| [main-loop-tick.md](main-loop-tick.md) | Main loop & per-tick dispatch |
| [per-realm-ai.md](per-realm-ai.md) | Per-realm AI economic-management subsystem (turn driver's first call, `FUN_0049a990`) |
| [combat-resolution.md](combat-resolution.md) | Strategic-map combat casualty resolver (`FUN_004aad07` + leaves) |
| [combat-battle-staging.md](combat-battle-staging.md) | Combat entry/routing (auto-calc `FUN_004a8aab` arms the defender buff; 0x13 quick-anim vs 0x12 interactive) + the two-outcomes-from-one-save staging recipe |
| [render-rate-limiter.md](render-rate-limiter.md) | Render rate-limiter + dirty-rect tracker |

## Graphics / DDraw (PLAN §3, §5)

| doc | covers |
|-----|--------|
| [ddraw-dsound-bringup.md](ddraw-dsound-bringup.md) | DDraw + DSound bring-up (surface create, cooperative level) |
| [frame-capture.md](frame-capture.md) | DDraw frame capture (lock hooks → BMP) |
| [map-render.md](map-render.md) | Strategic map-tile renderer — territory-sprite walk |
| [overlay-render.md](overlay-render.md) | Strategic-mode overlay renderer — units + cities pass |
| [battlefield-render.md](battlefield-render.md) | Combat-mode (battlefield) renderer — full draw-leaf survey |
| [combat-battlefield-input.md](combat-battlefield-input.md) | Interactive battlefield INPUT model — screens 0x28/0x29/0x2a, select/order (LMB select + RMB attack), entry/exit, Frida driving recipe |
| [combat-battlefield-ai.md](combat-battlefield-ai.md) | Tactical battlefield AI (sim driver, unit record, per-class behaviour) + exploit surface + kiting-bot spec |
| [main-menu-parity.md](main-menu-parity.md) | Main-menu render parity — capture (`--skip-intro`), reach, compare |
| [title-menu-screens.md](title-menu-screens.md) | Title page state machine (`DAT_005530f0`) + menu-navigation capture workflow |
| [game-start.md](game-start.md) | Start button → new-game init (`FUN_00497ced`) → ingame; entry chain, closure scope, scalar-wall resolution, phased port plan |

## Game state & save format (PLAN §4)

| doc | covers |
|-----|--------|
| [gameplay-and-screens.md](gameplay-and-screens.md) | How the game is played (strategy-guide synthesis for trace authoring) + the complete `DAT_004eac50` screen-ID map with per-screen renderer/string-group/port status |
| [save-format.md](save-format.md) | Save-file format (first map) |
| [save-schema-table.csv](save-schema-table.csv) | Save-record schema table (derived; feeds `tools/gen_save_schema.py`) |

## Harness / verification

| doc | covers |
|-----|--------|
| [in-process-retail-host.md](in-process-retail-host.md) | **rhost** (2026-10-23): retail in-process, deterministic; Lords2's runtime surface, the GOG patch code past .text, measurements |
| [game-state-map.md](game-state-map.md) | strategic game state VAs (realms, counties, armies, date) — the autoplay bot's reader |
| [hybrid-pilot-ai.md](hybrid-pilot-ai.md) | **hybrid pilot** (2026-10-23): the AI realm turn under rhost — 88/89 verified, bugs #227/#228, binding cost, blockers, recommendation |
| [hybrid-batch2-combat.md](hybrid-batch2-combat.md) | **hybrid batch 2** (2026-10-23): battle resolver + army movement under rhost — 50/59 reached, 0 divergences, hybrid 149 hooks PASS; #229–#231; shadow fidelity (host layers, stack residue) |
| [hybrid-batch3.md](hybrid-batch3.md) | **hybrid batch 3** (2026-10-23): army-state machine, battle exits / siege assault, realm events + season passes under rhost — 196 hooks, 162 reached over 36 scenarios, 0 divergences, hybrid PASS; #232–#235; event handlers shadowed by exploration |
| [hybrid-batch4.md](hybrid-batch4.md) | **hybrid batch 4** (2026-10-24): the economy / UI-action side under rhost — the trade grid, gift / message seals, Send Supplies + county-action tabs, keep build, caravan (models split from renders; entry-patched router actions) — 218 hooks, 201 reached over 43 scenarios, 0 divergences, hybrid PASS on the 37 blessed; #236; `econ-tour` |
| [battlefield-sim-port.md](battlefield-sim-port.md) | **the battlefield port** (2026-11-04): the tactical battle function by function on retail's layout — `bf_<va>` names, partitions (`tools/bf/parts.py`), pending stubs through `engine_retail_call`, generated `g_bf_<va>` globals, the porting loop |
| [bf-setup.md](bf-setup.md) | battlefield partition 'setup': entering the battlefield — terrain build, siege / custom maps, deployment + spawn, the per-frame cursor probe (50 functions); quirks |
| [bf-move.md](bf-move.md) | battlefield partition 'move': the move order FUN_0048f1dd, the path search, the 1×1 / 3×3 steps (26 functions); quirks |
| [bf-strike.md](bf-strike.md) | battlefield partition 'strike': the fire order, a projectile's flight tick (hits, damage), impact effects (22 functions); the projectile/effect record map; quirks |
| [bf-stance.md](bf-stance.md) | battlefield partition 'stance': the siege group AI — the defender table DAT_004d91d0 and attacker table DAT_004d91f8 handlers + helpers (28 functions); quirks, probe evidence |
| [bf-group.md](bf-group.md) | battlefield partition 'group': the group AI driver FUN_00489401, formation / re-target chain, interactive stances, the group aggregator, projectiles pass, fire pass (42 functions); quirks |
| [bf-unit.md](bf-unit.md) | battlefield partition 'unit': the per-unit driver, movement-state table DAT_004d9140, the animation sequencers, the troop band, terrain attrition (31 functions); stack-residue seams; quirks |
| [bf-action.md](bf-action.md) | battlefield partition 'action': the unit order table DAT_004d9170 (18 handlers) + helpers (36 functions); the siege orders' stack residue; quirks |
| [rl-io.md](rl-io.md) | rl partition 'io': the platform layer — asset / file loaders, WAV cues + music streams, movies, the debug log, sprite / palette loads, the debug-CRT free (70 functions); the platform shim through retail's imports / CRT; quirks |
| [render-under-rhost.md](render-under-rhost.md) | **render under rhost** (2026-11-04): the gfx layer on retail's layout (`src/gfx_globals.c`, 69 globals at their VAs; the canvas = retail's back buffer DAT_004ea1a8), the media (pixel) shadow `RH_SH_MEDIA`, hot-leaf sampling `RH_SH_SAMPLE`, the gfx / panel / text / wrap / menu-bar / button rows; the RLE 3-run spill, the rect's line order, the measurers' shared cache |
| [input-replay.md](input-replay.md) | Input recording & replay (JSONL trace format) |
| [pure-function-diff.md](pure-function-diff.md) | Pure-function differential testing vs retail |
| [ledger-provenance-audit.md](ledger-provenance-audit.md) | The FUN_-token gotcha — how the derived ledger over-counts, and `tools/audit_provenance.py` |

## Engine quirks

| doc | covers |
|-----|--------|
| [engine-quirks.md](engine-quirks.md) | Running log of load-bearing bugs, hand-rolled hashes, off-by-ones (#1–#104). Faithful-reproduce targets — cite in commits. |

---

*This index is hand-maintained. When you add a `findings/<subsystem>.md`,
add a row here (and a `PROGRESS.md` dispatch entry).*
