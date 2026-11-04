# FRONT — the active front (hand-edited; resolved arcs move to PROGRESS.md)

<!-- status:begin -->
- **Phase 0 — methodology reset (2026-10-23):** rhost is the verification foundation
  (`docs/audits/2026-10-23-methodology-reset.md`). Built + proven: deterministic boot → new game → turns,
  Windows ≡ wine bit for bit, control channel, porting loop (`rng_step`), IDA export, game-state reader,
  **autoplay** (Jev plays through rhost: 15 bot games in the corpus; retail coverage 37.1% → 44.6%),
  deterministic Smacker (`--movies`: the real boot sequence plays).
  **Direction: hybrid** (owner, 2026-10-23) — port functions replace retail's inside rhost, verified one
  by one; the standalone build falls out once coverage is (near) complete.
  **Hybrid pilot done** (`docs/findings/hybrid-pilot-ai.md`): 88/89 AI-realm-turn functions verified against
  retail (shadow + replace, 33 scenarios, 0 divergences); 2 real gameplay bugs fixed (#227, #228).
  `verify` now runs in nesting passes + a hybrid pass; shadow is page-tracked (cheap for hot leaves).
  **Batches 2–3 done** (`docs/findings/hybrid-batch2-combat.md`, `hybrid-batch3.md`): battle resolver, army
  movement, army-state machine, battle exits + siege assault, realm events + season passes — **196 hooks,
  162 reached, 0 divergences** (36 scenarios), hybrid pass bit-exact; quirks #229–#235.
  **Bot v2** (Jev, ~$0.19): sieges, quick battles, mercenaries, garrisons, AI alliance offers, event popups →
  3 scenarios; coverage 46.4% → 48.8%.
  **Batch 4** (`hybrid-batch4.md`): the economy / UI-action side (merchant, diplomacy, send supplies, county
  actions) — 218 hooks; `econ-tour` scenario; #236. **Bot v3** (~$0.32): **fights on the battlefield**
  (`tools/bot/battle.py`), long custom games (events roll only for odd counties) → 11 scenarios; coverage
  49.1% → **63.5%**. Its games exposed a binding gap (the AI score table) and #237 — fixed, 0 divergences.
  Pointer-table hooks (entry patch + trampoline) landed.
  **2026-11-04 (overnight, parallel agents):** the **battlefield ported — 260/260** (`battlefield-sim-port.md`,
  `bf-<part>.md`): all 260 replaced at once, corpus --check PASS (51 scenarios); **render verifiable under rhost**
  (`render-under-rhost.md`: gfx layer on retail's layout, pixel shadow, 64 rows); ledger honest (FUN_-token
  pollution removed: 47.2% touched / 60.9% of bytes). rl partitions (io, glue) and render batches in flight.
- **Top blocker:** none for logic parity. Smacker movies are skipped in rhost (retail tolerates it). The
  standalone's UI is far behind the hybrid (engine_screen_dispatch: title + strategic map only) — the battlefield
  cannot be wired into the standalone until the battle prompt / battlefield screens' dispatch is.
<!-- status:end -->

## ★ Next micro-tasks

1. **Merge the in-flight branches** (rl io, rl glue, render mapview / screens) with `tools/bf/fix_tokens.py` + the
   ledger regen; cross-verify (`verify --hooks`, then all rows replaced at once: `corpus --check --hook replace:…`).
2. **The whole-corpus `verify`** (no --only / --hooks) → `docs/rhost-evidence.json` → STATUS runtime axis (~550 rows).
3. **Battlefield evidence for the ~45 unreached functions**: a scenario where the player assaults an AI-held castle
   on the field (the defender stance table), siege engines (3×3 units) on the field, catapults breaking a wall, fire
   on wood cells, orders 9 / 12–16 (human-traces §A2, or bot v4).
4. **The remaining reached-unported code**: the render partition (the 0x40f1a0 screen arms, terrain info, the MP
   HUD — ~34 KB, now verifiable) and engine.c's glue (106 ported functions not on retail's layout, 72 KB).
5. **Standalone**: retail-layout ports (bf_ / rl_) replace engine.c's glue; the screen dispatch grows the battle
   prompt + battlefield arms; then wire the combat cluster (engine.c sub-state 2 TODO: bf_478952, 0x40490d,
   bf_488dfe, bf_4822ed, bf_485bb1, bf_489401, bf_4859e5, engine_combat_battlefield_end_tick).
6. **Port build `-std=gnu11`** after an x87 sweep (openrecet's c11 long-double literal finding).
7. **Tooling found tonight**: the bot's battlefield "RMB attack" never gives an order (retail: LMB press + release with
   a selection; an RMB release deselects) — fix `tools/bot/battle.py`; `corpus --bless` refuses a run that ends in a
   game exit (File > Quit: +1.9 KB unreachable to the corpus); rhost runs out of fake handles (8192) after ~1000 frames
   on the Battle Master ratings screen (0x2e: a per-frame handle leak — retail's or rhost's?); the shadow's movie rows
   need rh_hook_smk.h (smackw32's heap) — a core rollback would retire it.

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
1. **Record human traces** for what the bot can't reproduce — `docs/human-traces.md` §A first: real
   battlefield tactics (formations, archers held back, castle walls, siege engines on the field, defending your
   castle; the lord captured), a late-game campaign to a victory, natural mouse dynamics:
   `python3 tools/rhost/rhost.py record NAME [--from SCEN]` (normal window, never fullscreen; close it to
   finish). The bot now covers simple field battles, sieges, events, alliance offers.

Resolved 2026-10-23: direction approved (audit §8 P1); lords2/sotes/ys6-decomp containers stopped.
