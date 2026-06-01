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
| [render-rate-limiter.md](render-rate-limiter.md) | Render rate-limiter + dirty-rect tracker |

## Graphics / DDraw (PLAN §3, §5)

| doc | covers |
|-----|--------|
| [ddraw-dsound-bringup.md](ddraw-dsound-bringup.md) | DDraw + DSound bring-up (surface create, cooperative level) |
| [frame-capture.md](frame-capture.md) | DDraw frame capture (lock hooks → BMP) |
| [map-render.md](map-render.md) | Strategic map-tile renderer — territory-sprite walk |
| [overlay-render.md](overlay-render.md) | Strategic-mode overlay renderer — units + cities pass |
| [battlefield-render.md](battlefield-render.md) | Combat-mode (battlefield) renderer — full draw-leaf survey |
| [main-menu-parity.md](main-menu-parity.md) | Main-menu render parity — capture (`--skip-intro`), reach, compare |
| [title-menu-screens.md](title-menu-screens.md) | Title page state machine (`DAT_005530f0`) + menu-navigation capture workflow |
| [game-start.md](game-start.md) | Start button → new-game init (`FUN_00497ced`) → ingame; entry chain, closure scope, scalar-wall resolution, phased port plan |

## Game state & save format (PLAN §4)

| doc | covers |
|-----|--------|
| [save-format.md](save-format.md) | Save-file format (first map) |
| [save-schema-table.csv](save-schema-table.csv) | Save-record schema table (derived; feeds `tools/gen_save_schema.py`) |

## Harness / verification

| doc | covers |
|-----|--------|
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
