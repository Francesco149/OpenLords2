# FRONT — the active front (hand-edited; resolved arcs move to PROGRESS.md)

<!-- status:begin -->
- **Phase 0 — methodology reset (2026-10-23):** rhost is the verification foundation
  (`docs/audits/2026-10-23-methodology-reset.md`). Built + proven: deterministic boot → new game → turns,
  Windows ≡ wine bit for bit, control channel, porting loop (`rng_step`). In flight: IDA export,
  game-state reader, autoplay bot (Jev).
- **Top blocker:** none for logic parity. Smacker movies are skipped in rhost (retail tolerates it).
<!-- status:end -->

## ★ Next micro-tasks

1. **Autoplay v1** (`tools/bot/`): state reader (`l2state.py`) → Jev questions per turn → actions compiled
   to clicks (`docs/findings/ui-action-anchors.md`), checked against state → End turn; battles auto-resolve.
   Output: `tests/scenarios/bot-*` traces + decision logs. Then a corpus of N games × M turns, blessed.
2. **Re-verification batches**: rhost wrappers for the existing pure-function ports, worst-first (turn driver /
   AI brain FSM, economy, combat resolver `FUN_004aad07`); one row in `rh_hooks.c` each; `verify` evidence.
3. **Hybrid pilot** on one subsystem (suggest the AI brain FSM): measure wrapper cost and REPLACE coverage;
   decide hybrid vs standalone (audit §3).
4. **rhost gaps**: deterministic Smacker (fixed-base smackw32, clock / heap / waveOut / threads redirected);
   a real mixer for `record` audio; Trace Studio v3 fed from rhost `.idx` frames.
5. **Port build `-std=gnu11`** after an x87 sweep (openrecet's c11 long-double literal finding).

## Phase 0 checklist
- [x] rhost core (fake DDraw / DSound / window / heap / handles / files), Windows + wine targets
- [x] determinism gate (0 dwords), corpus + bless/check, cross-host equality
- [x] control channel + `rhbot.py`; record mode (human play)
- [x] porting loop proven (`rng_step`: shadow + replace)
- [x] `../lords2-decomp` archived read-only
- [ ] IDA 9.4 export (`tools/ida/`) — in flight
- [ ] game-state reader (`tools/bot/l2state.py`, `docs/findings/game-state-map.md`) — in flight
- [ ] autoplay bot v1 + bot-generated corpus
- [ ] re-verify earlier "1:1" claims under rhost (batches)

## Human queue (ranked by what it unblocks)
1. **Approve the direction** (audit §8 P1): rhost foundation, per-function loop, hybrid pilot.
2. **Play a few turns in rhost** (visible window on your desktop, never fullscreen; close it to finish):
   `python3 tools/rhost/rhost.py record try-me` → `tests/scenarios/try-me/`. Look for: the game behaves like
   the real one (menus, map, turns); the window shows every frame. A human trace seeds the corpus.
3. lords2/sotes/ys6-decomp podman containers idle (`sleep infinity`): stop or keep?
