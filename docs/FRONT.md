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
  **2026-11-07: the standalone runs** (`rhost --standalone`, `docs/findings/standalone-path.md`): only the exe's data
  sections mapped, `.text` INT3 + 1553 entry thunks, entry = the port of retail's start (`src/rl_crt.c`) → WinMain → window (`src/rl_main.c`),
  audit per run + `tools/standalone/prove_no_retail_code.py` (14/14 PASS). **Corpus (wine, 64): 64 run to the end with
  no retail code, 64 equal pure retail in frame / tick / RNG / every presented frame / state (CRT port 2026-11-08)** (bot-s16-t20 fixed: quirk #238); deterministic, Windows ≡ wine.
  Data image: `tools/data_ledger.py` → `docs/data-ledger.md` (99 KB from the exe; 78.6% of its non-zero bytes
  documented). Free-play capture: Desktop "Lords II capture" (`rhost.py play-*`).
  **2026-11-13: agent-driven play (`rd`, `tools/drive/`)** — openrecet's / the Kirby playbook's method on rhost:
  `rhost --live` (retail as a coroutine on its fixed stack), in-process savestates by full page compare (`rhost.py
  snap-check`: 128/128 PASS, end states = the blessed corpus), exploration search (wine 6.8k frames/s a session,
  Windows 13.7k, bunker 16.1k; `-j 8` across hosts 41k), screen text + hotspot recorders, exact in-session pictures,
  `rd cov` (corpus baseline 1527/2611 functions, 75.8%). Third host: bunker (`tools/bunker/`). Campaign 1 running.
- **Top blocker:** none for logic parity. The standalone's state = pure retail's (64/64, state_sem included) since the
  CRT port (2026-11-08, `docs/findings/crt-port.md`). Multiplayer stays unverifiable (DirectPlay / serial: rhost stubs it).
<!-- status:end -->

## ★ Next micro-tasks

0. **rd campaigns** — 2026-11-13 totals: 4 campaigns, 15 drivers (11 subagents, 4 API drivers for $9.12), 13 scenarios
   filed; corpus 22,673 → 23,255 basic blocks (62.9 % → 64.6 % of retail code bytes), 1,527 → 1,533 functions (lord_move
   0x4a599d and the F8 old-turn path among them); `rhost.py verify` on the first 5 new scenarios: 0 divergences, hybrid
   PASS. Remaining block gaps are mostly multiplayer, dead code, device behaviour or long setups (siege on our walls with
   AI knights; county captures; a won campaign) — `rd cov --blocks --fn X --disasm` per function.
   Campaign 1–2 (7 driver subagents): filed split-arm, castle-labor (+17 blocks),
   hotkeys (+167, 5 new functions), terrain-info (+91), piece-kinds (+55); ev-13d and split-arm drove covered code (the
   premise check `rd covered` came from that). **siege-knights (halve_garrison 0x4ad8cc) blocked**: AI knights take years
   (armour only from a smithy), an AI targets a garrisoned castle only under a vendetta (−30 attitude + 3 battles), and
   0x48d9ce arms the flag only when every AI unit left is a knight before any wall breach — a 30–40-season managed game
   (driver report in the 2026-11-13 session; furthest checkpoint run `siegeknights:t27`). Next: `rd seasons` makes long
   setups cheap; lord_move 0x4a599d and 0x48e774 are the other real function gaps; block gaps by `rd cov --blocks --fn`.
   Recipe: `tools/drive/README.md` §Campaign recipe. Tool work left: the boundary's host work leaves stack residue
   retail reads (field-orders differs by compiler: make the boundary residue-free, then re-bless); savestates cost ~9 %
   of search time (not worth optimizing yet).

1. ~~One residue model for the march executor~~ — done 2026-11-07 (quirks #238 / #239): the driver's residue window =
   the AI step 0x49a010's imprint (`engine_army_state_res_ai_step`), rl_4b99c0 calls the driver port directly (was an
   `engine_retail_call` seam: in the hybrid a seam runs RETAIL even when the VA has a row — only call sites are patched;
   the standalone thunks every entry); the battlefield's frame-pointer residue takes retail's frame-driver e0 under the
   port's frame guard. Full hybrid 64/64 (bot-s16-t20, field-orders failed), standalone 64/64. Fixed after (rh_hooks.c): replace mode
   now entry-patches every row too, so the 92 `engine_retail_call(VA)` seams whose VA has a row (of 144) run the port
   in the hybrid as in the standalone: full hybrid still 64/64.
2. **Host-level rows**: ui4's 0x42e5c0 / 0x42ec91 / 0x42f1df (RH_SH_HOST rows; they passed replace-only with temporary
   rows). Done 2026-11-07: WinMain 0x40e9ab / the window 0x4b2258 (rows `rh_hooks_rl_main.c`), the frame guard through
   `rl_frame_driver` (rhost's frame boundary).
3. ~~**The CRT port**~~ — done 2026-11-08 (`docs/findings/crt-port.md`): retail's MSVC 4.x debug CRT, 81 functions
   (`src/rl_crt.c`, rows `rh_hooks_rl_crt.c`) on retail's layout; shadow 81/81 0 divergences, full hybrid 64/64; the
   shim and `rl_crt_start` use it under rhost and standalone → **standalone 64/64 = pure retail incl. state_sem**.
   Next: the standalone joins `corpus --check` (the pre-commit gate); 21 unreached CRT seams (`_CrtDbgReport`,
   `_amsg_exit`, locale / TZ paths …) are standalone gaps (★4); then **`--live`**: a product exe on the real platform
   (real window / DirectDraw / DirectSound behind the same IAT slots).
4. **The standalone's static gaps** (`tools/standalone/prove_no_retail_code.py --gaps-only`): 213 retail entries the
   port calls with no thunk — the payload builders 0x4d57f0[100] (rl_glue.c:1414), the delayed-action handlers
   0x4d5c98[50] (rl_ctl.c:2547), 63 seams (the DLGPROCs 0x4b7f78 / 0x4b8335 / 0x4b8439 / 0x4b85b7, gfx_restore
   0x4b2730, 0x4b26b8, the MP save pump 0x49792e …). Each = a port + a row (most are the TODO(rl-link) seams, ★5).
5. **The TODO(rl-link) seams** (rl_ functions calling engine_retail_call where a linked old port takes a port struct /
   context: the event-queue posters 0x49b2a3 / 0x448819 / 0x449889 / 0x4acbd4 / 0x44aa41 / 0x44ae3c / 0x4508de, combat
   0x4ac3c6 / 0x4abb36 / 0x43b622, county panels 0x4110b1 / 0x41152f / 0x4116fb / 0x411b72, the screens' binders …;
   per-partition lists in docs/findings/rl-*.md): port them on retail's layout (they run retail under rhost today).
6. **Unreached code**: 939 functions / 172 KB no scenario reaches (`docs/findings/coverage-round3.md`): multiplayer
   (DirectPlay / serial — a fake transport in rhost would open it), dead code (zoom-1 / zoom-16 drawers, unwritten
   orders 0 / 7 / 13 / 15 / 16), AI knight garrisons, DirectDraw errors (a fault-injection switch for 0x42f60e's
   HRESULT table), the human queue (below).
7. **Tooling**: `tools/bf/dedup_globals.py` misses a duplicate whose two definitions differ in type / attributes
   (g_rl_4dc428, g_rl_4ea180, g_rl_4d8a58 fixed by hand at merge); whole-corpus `--hooks '^rl_'` verifies take ~2 h on a
   loaded box — the per-frame rows dominate (RH_SH_SAMPLE them).
8. **Data-image documentation** (`docs/data-ledger.md`, catalog `docs/data/catalog.tsv`): document each item with
   evidence (the bot exercises what it needs: a scenario that reads the item, a probe of its consumer); largest open:
   the string tables (every unreferenced non-zero byte is a string), 0x4e3260 (2244 B, 0x4bb978), 0x4d3200 (0x43c2a9), the
   draw tables 0x4e3bf4 / 0x4e4034, the menu / button records 0x4dc360..0x4de000. Goal: a port that loads nothing.
9. **Port build `-std=gnu11`** after an x87 sweep (openrecet's c11 long-double literal finding).

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
0. **Free play, whenever you have a moment:** double-click **"Lords II capture"** on cutestation's desktop and play
   (close the window to finish; the next session can Load your save). Nothing else to do — agents import the sessions
   (`rhost.py play-fetch`).
1. **Record human traces** for what the bot can't reproduce — `docs/human-traces.md` §A first: real
   battlefield tactics (formations, archers held back, castle walls, siege engines on the field, defending your
   castle; the lord captured), a late-game campaign to a victory, natural mouse dynamics:
   `python3 tools/rhost/rhost.py record NAME [--from SCEN]` (normal window, never fullscreen; close it to
   finish). The bot now covers simple field battles, sieges, events, alliance offers. Most wanted (coverage round 3,
   `docs/findings/coverage-round3.md`): an AI castle **defended by knights** and assaulted on the field (0x48e774), an
   AI **besieging you with knights** (halve_garrison 0x4ad8cc), an **ally that raises a new army** and marches its lord
   (0x4a599d), a long campaign with stone castles.
Resolved 2026-11-07 (owner): the standalone loads its DATA from the user's exe with proof no original code runs (as
OpenSummoners), toward a fully documented port that loads nothing from it; lean on the bot to exercise what the data
documentation needs; more targeted traces stay deferred (the free-play shortcut instead).

Resolved 2026-10-23: direction approved (audit §8 P1); lords2/sotes/ys6-decomp containers stopped.
