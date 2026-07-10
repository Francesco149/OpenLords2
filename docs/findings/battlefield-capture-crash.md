# Battlefield capture "crash" — root-caused + fixed (the ingame gates)

> 2026-07-10. Resolves the long-standing blocker "CAPTURE + ATTACK-ORDER combat crashes
> frida" / "mass-setpos crashes" / "fast routs crash" that had staged-battle FOOTAGE
> written off as uncapturable (kite melt commit 29c598b limit #2; recommended numbers-
> overlay instead). It was **not** a fundamental frida instability — it was driving the
> live tactical sim against a **half-initialized ingame state**. Fixed by arming two gates.

## TL;DR
Before running a staged battle, poke **both**:

| VA | name | value | why |
|---|---|---|---|
| `0x0052af94` | `game_state_phase` (`DAT_0052af94`) | **3** (ingame) | sim sub-steps + action dispatcher gate on it |
| `0x00553030` | `master_switch` / `mp_active` (`DAT_00553030`) | **2** (≠0) | tactical per-tick action-dispatcher (`FUN_004472ce`) BAILS if 0 |

Helper: `kiting_demo.arm_ingame(d)` — call once after `Battle.start()`. Baked into
`kiting_demo.stage()`; wired into `film_battle.py` (both staged + natural paths).

## Two symptoms, one cause
A **bare menu-spawn** (probe daemon spawns `Lords2.exe` fresh → main menu) can muster +
deploy + RENDER tactical units via the callq chain (`F_MUSTER/F_SUBINIT/F_ENTER/F_DEPLOY`),
but the two gates are left at `phase=2, master=0`. Consequences:

1. **Units never act.** The per-tick action dispatcher `FUN_004472ce`
   (`src/engine_delayed_action.c:76` — `if master_switch==0: return`) drains nothing, so
   movement/combat commits never fire. Symptom seen: order registers (action byte → 5,
   then per-unit-move → 2 "walking") yet the unit's live position never changes and enemy
   HP stays flat across dozens of rounds. This is why every "melt/kite" repro on a fresh
   game showed **zero damage** — not a targeting/range problem, the sim simply wasn't
   ticking the tactical layer.
2. **The crashes.** When combat IS driven with these globals inconsistent vs a real ingame
   state, retail's per-tick sim sub-functions read half-initialized structures and deref
   **unmapped pointers** (the deferred-action table `DAT_0050d7c0`, player-struct internals
   behind `DAT_0057bf04`). The agent already documents this exact hazard as the reason it
   *freezes* the tick during diff vectors (`openlords2-agent.js:725-737`). Under a live
   film loop (capture + order + run) the race surfaces as the intermittent "crash frida".

Arming `phase=3 + master=2` gives a **consistent ingame state**: the sim ticks normally AND
the sub-functions see coherent structures, so nothing derefs garbage.

## Verified crash-free (tools/trigger_test.py, 2026-07-10, gates armed)
Every previously-blamed operation now survives a full staged fight with a capture every round:

| trigger | what it exercises | result |
|---|---|---|
| `shotd`  | async lock-free `captureFrameDirect` during run (film recipe) | ✅ no crash |
| `shots`  | in-tick synchronized capture | ✅ no crash |
| `frozen` | capture while sim paused (`0x53f238=-1`) | ✅ no crash |
| `setpos` | mass per-unit setpos each round (the kite step-back) | ✅ no crash |
| `pileon` | 8 archer stacks (the auto-calc instant-resolve threshold) | ✅ no crash |

Production proof: `film_battle.py --staged --enemy mace --n 24 --follow enemy` → 40–50
bright frames, enemy 24→6 while archers stay 24 (the melt), arrow volleys visible, clean
mp4, no crash. So **staged battle footage is capturable** — the numbers-overlay workaround
is no longer forced.

## Notes / gotchas
- Gates HOLD once set post-`start()` (the engine does not re-stomp them mid-battle, unlike
  the AI-level `+0x29` refresh). `arm_ingame` is cheap; re-call per run-window if paranoid.
- Re-staging on the SAME game boot works **after a battle ends naturally** (enemy wiped →
  screen 0x22/0x2e). Re-staging ON TOP of a still-running battle still fails ("deploy 0
  stacks") — end or reboot first (unchanged from before).
- The ~200-tick / ~20% auto-end of a one-sided fight (`0x2e`, kite melt limit #1) is
  UNCHANGED and orthogonal — that's an engine rule, not a harness bug. Kiting remains a
  time-limited curiosity, not a real strat.
