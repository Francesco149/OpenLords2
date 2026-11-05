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
  **2026-11-04/05 (overnight, parallel agents):** the **battlefield ported — 260/260** (+ its 4 seams); the rl
  partitions — rounds 1–2 (io / glue / render / misc / skirmish / ui2 / ui3: every function the corpus reached that had
  no port) and **round 3** (ui4 / ctl / terr / turn / host: the 400 reached functions whose OLD ports were never on
  retail's layout — 400 retail-layout ports, 95 of them wraps of linked old ports) + cov3 / quit: **1028 functions
  in 21 partitions, 0 pending** (`tools/bf/status.py`). Render verifiable under rhost (pixel shadow; mapview 64 +
  screens 104 rows). Coverage 63.5% → **75.8%** (64 scenarios: UI tours, battlefield orders, sieges, game exit, custom
  France "strong", the movie skip). **Whole-corpus verify (wine, 64 scenarios, 1474 rows): 1435 reached with 0
  divergences, 38 never called, all 1474 replaced at once: hybrid PASS, 0 failed runs** (the 1 divergence — rl_40e3ee,
  a movie-opening click: smackw32 not rolled back — fixed after: 1.57 M calls, 0 divergences). **Residual retail code
  under the full hybrid: 9 non-library functions / 3.7 KB** (WinMain, the window, the army-state handlers 2..4 and the
  frame guard — ported, no row —, two CRT helpers) + the statically linked CRT (79 / 22 KB); 316 / 125 KB before round 3.
  rhost: RH_SH_HOST (host-level rows: proven by the hybrid pass), RH_SH_FULL (the copy shadow for rows whose subtree
  reads stack residue the paged shadow's fault frames overwrite), smackw32 / DirectDraw rollbacks, handle reuse.
- **Top blocker:** none for logic parity. On the corpus the hybrid runs almost no retail code (above); the standalone
  needs the data-image build (★3, `docs/findings/standalone-path.md`). Multiplayer stays unverifiable
  (DirectPlay / serial: rhost stubs it).
<!-- status:end -->

## ★ Next micro-tasks

1. **One residue model for the march executor 0x465d28**: 0x46521f (ctl) passes its four uninitialized locals as stack
   residue (rl_46521f_res, rh_res_get / put); the army-state handlers 2..4 0x465486 / 0x465622 / 0x465761 (terr) run
   engine_army_move_run on its globals. Unify, then give the handlers RH_SH_FULL | RH_SH_SAMPLE rows (today: no row —
   verified under --shadow-full on terr's branch; with a row the hybrid pass fails with 46521f's, 0x465622 14 divergences).
2. **Host-level rows**: ui4's 0x42e5c0 / 0x42ec91 / 0x42f1df passed replace-only corpus --check with temporary rows —
   give them RH_SH_HOST rows; the frame guard 0x40e890 / 0x40e8bb needs a hook through rhost's frame wrapper; WinMain
   0x40e9ab and the window 0x4b2258 (plain code, no row) for the standalone.
3. **Standalone** (`docs/findings/standalone-path.md`): the data-image build — retail's .data/.rdata/.bss mapped at its
   VAs from the user's exe, every bf_ / rl_ / wrapped port, the code-pointer slot rewrite, the platform layer
   (`src/rl_io_plat.h`); report each scenario's first unported gap; then wire WinMain → the frame tick. Agents listed
   the per-access globals that only hold at retail's VAs (g_bf_542ce0, g_rl_4dc428, g_rl_53f2b8, g_bf_543fe0, the
   move-cost grid, the button / hotspot tables, DUP pairs g_tr_* / g_rl_*) — moot under the data image.
4. **The TODO(rl-link) seams** (rl_ functions calling engine_retail_call where a linked old port takes a port struct /
   context: the event-queue posters 0x49b2a3 / 0x448819 / 0x449889 / 0x4acbd4 / 0x44aa41 / 0x44ae3c / 0x4508de, combat
   0x4ac3c6 / 0x4abb36 / 0x43b622, county panels 0x4110b1 / 0x41152f / 0x4116fb / 0x411b72, the screens' binders …;
   per-partition lists in docs/findings/rl-*.md): port them on retail's layout (they run retail under rhost today).
5. **Unreached code**: 939 functions / 172 KB no scenario reaches (`docs/findings/coverage-round3.md`): multiplayer
   (DirectPlay / serial — a fake transport in rhost would open it), dead code (zoom-1 / zoom-16 drawers, unwritten
   orders 0 / 7 / 13 / 15 / 16), AI knight garrisons, DirectDraw errors (a fault-injection switch for 0x42f60e's
   HRESULT table), the human queue (below).
6. **Tooling**: `tools/bf/dedup_globals.py` misses a duplicate whose two definitions differ in type / attributes
   (g_rl_4dc428, g_rl_4ea180, g_rl_4d8a58 fixed by hand at merge); whole-corpus `--hooks '^rl_'` verifies take ~2 h on a
   loaded box — the per-frame rows dominate (RH_SH_SAMPLE them).
7. **Port build `-std=gnu11`** after an x87 sweep (openrecet's c11 long-double literal finding).

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
   finish). The bot now covers simple field battles, sieges, events, alliance offers. Most wanted (coverage round 3,
   `docs/findings/coverage-round3.md`): an AI castle **defended by knights** and assaulted on the field (0x48e774), an
   AI **besieging you with knights** (halve_garrison 0x4ad8cc), an **ally that raises a new army** and marches its lord
   (0x4a599d), a long campaign with stone castles.
2. **Review the standalone design** (`docs/findings/standalone-path.md`): retail's data image mapped at its VAs from
   your own Lords2.exe (read at startup, never shipped) + all-port code — how the hybrid becomes the standalone. A yes /
   no on that direction before the `standalone2` build starts.

Resolved 2026-10-23: direction approved (audit §8 P1); lords2/sotes/ys6-decomp containers stopped.
