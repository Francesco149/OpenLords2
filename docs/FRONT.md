# FRONT — the active front (hand-edited; resolved arcs move to PROGRESS.md)

<!-- status:begin -->
- **Phase 0 — methodology reset (2026-10-23):** rhost is the verification foundation
  (`docs/audits/2026-10-23-methodology-reset.md`). Built + proven: deterministic boot → new game → turns,
  Windows ≡ wine bit for bit, control channel, porting loop (`rng_step`), IDA export, game-state reader,
  **autoplay** (Jev plays through rhost: 15 bot games in the corpus; retail coverage 37.1% → 44.6%),
  deterministic Smacker (`--movies`: the real boot sequence plays).
  In flight: re-verification pilot (AI-turn subsystem).
- **Top blocker:** none for logic parity. Smacker movies are skipped in rhost (retail tolerates it).
<!-- status:end -->

## ★ Next micro-tasks

1. **Autoplay v2** (`tools/bot/README.md` §Not yet): play into defeat/victory screens; merchant, castle,
   diplomacy, supply actions; custom-game setups; steer toward longer games (coverage of later content).
   Bless the best-coverage games; track `rhost.py coverage` as the bot's metric.
2. **Re-verification batches**: rhost wrappers for the existing pure-function ports, worst-first (turn driver /
   AI brain FSM, economy, combat resolver `FUN_004aad07`); one row in `rh_hooks.c` each; `verify` evidence.
3. **Hybrid pilot** on one subsystem (suggest the AI brain FSM): measure wrapper cost and REPLACE coverage;
   decide hybrid vs standalone (audit §3).
4. **Movies in the corpus**: scenarios with `"movies": true` (rh_smack.c is deterministic) so the movie
   paths (intro → title, jail, victory/defeat) are exercised as a real user sees them.
5. **Port build `-std=gnu11`** after an x87 sweep (openrecet's c11 long-double literal finding).

## Phase 0 checklist
- [x] rhost core (fake DDraw / DSound / window / heap / handles / files), Windows + wine targets
- [x] determinism gate (0 dwords), corpus + bless/check, cross-host equality
- [x] control channel + `rhbot.py`; record mode (human play, with sound: `--audible` mixer)
- [x] porting loop proven (`rng_step`: shadow + replace)
- [x] `../lords2-decomp` archived read-only
- [x] IDA 9.4 export (`tools/ida/`, `/mnt/ephemeral/openlords2/decompiled-ida/`)
- [x] game-state reader (`tools/bot/l2state.py`, `docs/findings/game-state-map.md`)
- [x] autoplay bot v1 + bot-generated corpus (7 games)
- [x] Trace Studio v3 on rhost captures (`rhost.py view`); pre-commit rhost gate (wine)
- [ ] re-verify earlier "1:1" claims under rhost (batches)

## Human queue (ranked by what it unblocks)
1. **Record human traces** for what the bot can't reproduce — `docs/human-traces.md` §A first (interactive
   battles, sieges, a late-game campaign, the AI's diplomacy proposals, event decisions, natural mouse
   dynamics): `python3 tools/rhost/rhost.py record NAME [--from SCEN]` (normal window, never fullscreen;
   close it to finish).

Resolved 2026-10-23: direction approved (audit §8 P1); lords2/sotes/ys6-decomp containers stopped.
