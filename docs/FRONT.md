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
- **Top blocker:** none for logic parity. Smacker movies are skipped in rhost (retail tolerates it).
<!-- status:end -->

## ★ Next micro-tasks

1. **Re-verification batch 4** (`rh_port.h` pattern): the economy UI side — trade / treasury / merchant,
   labor, diplomacy actions, send supplies — then renderers (replace + `fb` chain). Fix the remaining inlined
   distance callers (`0x46902a`, battlefield) when hooked (#235 pattern).
2. **Bot v3**: (a) take the field and order a simple all-out attack — any fought battle reaches the 3
   battlefield-end rows (`0x43be65`, `0x477dfc`, `0x478419`), maybe without a human; (b) longer games that
   keep counties past 1268 — events roll only for the human's counties (22 handlers unreached); (c) a
   bot-driven attack on an owned county; (d) ask an ally for help (pay-for-help popup); (e) defeat / victory.
3. **Tooling**: hooks for functions reached only through pointer tables (`PTR_FUN_004d6a50`: patch the slot);
   `rh_res_set` residue compared in shadow mode.
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
