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

0. **Multiplayer under rhost (2026-11-14, `docs/findings/multiplayer-rhost.md`, PROGRESS 2026-11-14)** — both transports
   faked (SIGS, DirectPlay), each side's trace replays alone; `mpbot.py` authors live. **25 MP side traces**
   (mp-start / play / war / duel / dp / leave / trio / skirmish / siege / five), Windows ≡ wine. **Ported + verified on
   MP state: mpmsg 54, mpmsg2 58, mpnet 35, mpmsg3 83, mpnet2 23** (0 pending) + 12 CRT functions + the old seal ports'
   MP arms. 2026-11-14 evening: the two last MP divergences were harness (multiplayer-rhost.md §The last two): a shadowed
   CloseHandle of the game-start event (rh_handles.c defers it) and a nested row's sync_in dropping live port mirrors
   (rh_hooks_ai.c ctx_port). **Standalone: 105/105 scenarios = pure retail, all 25 MP sides included** (2026-11-14 late):
   mp-trio-p3 = quirk #240 (0x475e07's uninitialized [ebp-8] = the map scene's render_tile return address, never 3);
   mp-trio-p1 (fb only, the lobby's first frame f53108: name colour) = the roster row's seat snapshot was taken before
   0x49caaa reassigns the seats (rh_hooks_screens.c reloads after the prep seam); Windows standalone 105/105 too.
   **`verify --only '^mp-'` (wine, 2026-11-15, `--wine-timeout 18000`): 1541/1829 rows reached, 0 divergences, hybrid
   PASS (25/25)**; its 1 failed run (mp-war-p2 shadow pass 3 off the corpus state_sem) was harness: the row rl_4b1310
   (greys the back buffer in place) lacked RH_SH_MEDIA, so the shadow greyed it twice (fixed; mp-war-p2 with the row
   shadowed ends = corpus, 4 calls, 0 divergences). **2026-11-15: `mp-labor`** (p1 double-clicks
   the Town Center's workforce grid → message 38, toggles three task sites on the map → 58; End turn): 38 / 58 ported
   (rl_4436bb / 4436f9 / 444915 / 444948): shadow 8 calls, 0 divergences, hybrid PASS, standalone 2/2 = pure retail,
   det 0 dwords, Windows ≡ wine. **`mp-skrpick`** (a second .skr staged by `"saves": ["game:USER.SKR=user2.skr"]`,
   picked in the MP skirmish setup → 93 → the peer's 94): 93 / 94 ported, 0 divergences, hybrid PASS, standalone =
   pure retail. Census (rl-mpmsg3.md): 52 / 53 / 89 are dead (their senders have no reference).
   **`mp-desync`** (2026-11-15; mpbot `feed` re-plays mp-duel live to its battle, p2 takes the field; rh_net's live
   wire forge `pK forge 12 10 1` gives p1 one hash message of p2's with sum [0] ^ 1): the snapshot restore 0x43f5c5 →
   16 both ways, 91 → ported 0x43f5c5 / 0x47ebe8 / 0x43f855 / 16 / 91 (7 rows: 0 divergences, hybrid PASS, standalone
   2/2 = pure retail, det 0, Windows ≡ wine). The standalone first failed on fb only: rl_47ebe8 set DAT_0053e9ac through
   the g_bf_ alias, which the s_dirty mirror's sync_out overwrote — the shadow did not see it (tooling gap: a port write
   to an alias whose VA a mirror owns should be flagged); rl_444b17 / rl_448076 had the same write, fixed.
   **81 is dead** (the join's "shield" = DAT_005c9a74, always 1 in the GOG exe). **90 is unreachable by lag**: retail's
   tick pacing never lets a side run 50 ticks past its view of a peer (multiplayer-rhost.md §Desync; mpbot `pK lag MS`).
   **`mp-price`** (2026-11-15; setup County status strong → AI pops > 1000; p1 compliments the Baron, allies (179), asks
   him to attack until 188 → the type-10 popup's yes → 70): builder / receiver 0x4454e7 / 0x4455ad + action 40 0x4482be
   ported (shadow 0 divergences, hybrid PASS, standalone 2/2 = pure retail, det 0, Windows ≡ wine).
   **`mp-badpkt`** (2026-11-15; forge on mp-start: id → 98, size + 1, bad checksum): 0x43eac1's corrupt / wrong-size /
   bad-CRC arms (rl_43eac1 shadowed, standalone 2/2 = pure retail, det 0, Windows ≡ wine). **19 / 26 dead** (F6 / F7
   behind DAT_0053f684, only ever 0: rl-mpmsg3.md), forced resend dead (DAT_0055ce68 never set).
   **`mp-forge1` / `mp-forge2`** (id-forged carriers: the checksum covers the payload only): receivers 19 / 26 / 27 / 52 / 97,
   actions 24 / 46, the late-action path 0x4470c4 → 90 reached in retail. **Every MP message id now has a port** (47
   functions, fe9a756 / ec746c4: 10 rows reached, 0 divergences, hybrid PASS; 37 dead-code rows unverified — no carrier);
   standalone 39/39 MP sides = pure retail; static gaps 107 → 66. **2026-11-16: the last 24 delayed-action handlers ported** (table 0x4d5c98 complete,
   rl-mpmsg3.md; dead but for 25 / 35, reachable by forging 53 / 68): static gaps 66 → 44. ★5 closed (2026-11-17); ★4 closed (2026-11-17): 0 gaps.
0b. **rd campaigns** — **campaign 6 (2026-11-14, API drivers):** ai-recall filed (sub_48B7FC 0x48b7fc first time: an Equal
   field skirmish after a Play Now → New Game trip — a cold skirmish leaves byte_553080 zero, so the recall never fires;
   +12 blocks); ai-surrender blocked (probe: no skirmish attacker is knights-only; the battle ends when a side has 0 men
   of class < 7, so halve_garrison needs AI knights in a campaign siege after all); siege-knights blocked a third time (run
   `siegeknights3`: Baron at −30 / vendetta 2 by turn 17, but our county starves before a knight army comes). Its tool
   gaps fixed (2026-11-14): `rd seasons --feed` (county upkeep: rations, the grain / meat slider, tax — 12 seasons from
   custom-strong: happiness 100, herd 229 → 251), `--until` inside turn processing runs on input-free to the next
   screen that wants input (`--at-once` = old stop), `@raise_more` / `@raise_continue` follow the mercenary box
   (ui.json `alt`: {EXPR} coordinates). Campaign 6 total $7.52. **Attempt 4** (`siegeknights4`, API, $1.46, blocked):
   vendetta stage 3 at turn 29; the Baron's knight army A26 (417 men, 27 knights) launched at us turn 46 but vanished
   at (35,26) crossing realm 2's land; his other knight armies target realm 3. It found two bugs of ours, fixed: `--feed`
   counted grain sacks as heads (a sack feeds 6) → quarter rations, health 0; `Live.prog` reused ids round-robin
   (250), so a long `seasons --until` was overwritten by a later --watch program (a false "held") — pinned now, and
   sweep compiles --where once. `--feed` now buys grain too: won2 + 20 seasons peaks at 1158 people, starves ~turn 60
   (no sowing). Next for siege-knights: a start beside the armour AI (no third realm's land between). **campaign 5 (2026-11-14, 4 API drivers, $13.03):** knight-garrison filed (sub_48E774 0x48e774 —
   AI siege-defender knights, the Baron's Germany garrison, custom-strong + 4 seasons; +27 blocks), ai-sally filed
   (sub_48C3FE 0x48c3fe → sub_496D76 0x496d76: a skirmish siege, a docked tower = a breach record, our archers' hits give
   the AI swords a target; +41 blocks; attempt 1 named the condition); **siege-knights blocked again** (probe
   `docs/findings/probes/siege-knights.md`: the Baron's knight armies never came south; next: a start beside the armour
   AI, attitude < −10 by insults only). Still unreached: 0x4ad8cc halve_garrison, 0x485861. `rhost.py verify` on the 7
   campaign-3/4 scenarios: 1314/1557 rows reached, 0 divergences, hybrid PASS. Fixed: live / snap-test sessions ran
   retail's stack 16 bytes low (rd verify MISMATCH on any run through siege-castles' battle; 0ae40e4); the battle
   residue model's 0x4b99c0 e0 was a constant gone stale by 8 (frame_wrap's frame grew) — now retail's chain constant
   minus frame_wrap's measured frame, and the port's WinMain / 0x40e8bb / 0x40e890 have retail's frames: **field-orders'
   long-standing hybrid / standalone FAIL is gone — full hybrid 78/78; standalone 78/78 = pure retail** since the F8 old-turn
   reload 0x43f7e3 + its clears 0x43ff63 / 0x446f06 / 0x43ff08 / 0x43ffc3 were ported (rl_host.c, 2026-11-14).
   2026-11-13 totals: 4 campaigns, 15 drivers (11 subagents, 4 API drivers for $9.12), 13 scenarios
   filed; corpus 22,673 → 23,255 basic blocks (62.9 % → 64.6 % of retail code bytes), 1,527 → 1,533 functions (lord_move
   0x4a599d and the F8 old-turn path among them); `rhost.py verify` on the first 5 new scenarios: 0 divergences, hybrid
   PASS. Remaining block gaps are mostly multiplayer, dead code, device behaviour or long setups (county captures; a won campaign) — `rd cov --blocks --fn X --disasm` per function.
   Campaign 1–2 (7 driver subagents): filed split-arm, castle-labor (+17 blocks),
   hotkeys (+167, 5 new functions), terrain-info (+91), piece-kinds (+55); ev-13d and split-arm drove covered code (the
   premise check `rd covered` came from that). **siege-knights (halve_garrison 0x4ad8cc) blocked**: AI knights take years
   (armour only from a smithy), an AI targets a garrisoned castle only under a vendetta (−30 attitude + 3 battles), and
   0x48d9ce arms the flag only when every AI unit left is a knight before any wall breach — a 30–40-season managed game
   (driver report in the 2026-11-13 session; furthest checkpoint run `siegeknights:t27`). Next: `rd seasons` makes long
   setups cheap; lord_move 0x4a599d now has its row (rl_4a599d, 2026-11-14) and 0x48e774 is reached; block gaps by `rd cov --blocks --fn`.
   Recipe: `tools/drive/README.md` §Campaign recipe. Tool work left: savestates cost ~9 % of search time (not worth
   optimizing yet).

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
   **2026-11-17: the standalone is playable** — Desktop shortcut **"OpenLords2 (standalone)"** (`win/play-standalone.cmd` →
   `play.ps1 -Standalone`): the free-play launcher with `--standalone` (realtime, visible window, sound, movies, input
   recorded; same carry / sessions as "Lords II capture"). `play-fetch` replays each standalone session in PURE retail
   (wine) and compares frame / RNG / state_sem: human play = parity evidence. `play-test --standalone` (Windows, 2 ×
   1500 frames): audit OK (2000 thunks, other 0, identical 0), both = pure retail. Still rhost's software platform
   (GDI window, waveOut mixer, virtual clock paced to wall time), not real DirectDraw / DirectSound — the real-API
   product (OpenSummoners' `-DRH_PRODUCT`) only if the owner wants fullscreen / a single exe.
4. ~~**The standalone's static gaps**~~ — **0 left (2026-11-17)**: the last 23 (all CRT) + their unported closure, 67
   functions, ported (src/rl_crt_{dbg,dbgheap,env}.c; crt-port.md §Gap ports) and verified by **synthetic carriers**
   (`rhost.py probe`, `tests/probes/*.probe`, rhost README: a row's wrapper called with chosen arguments at a frame
   boundary, shadowed — evidence for code no scenario reaches): 83 rows / 4 files, 0 divergences (wine). No
   engine_retail_call left in the CRT; `prove_no_retail_code.py --gaps-only` 0 gaps; standalone 121/121 = pure retail.
   Unverified by shadow: _amsg_exit / _exit / abort / terminate (never return: hybrid only), __wtomb_environ (faults on
   _wenviron = NULL in both). The 37 MP dead-code rows: probed too (2026-11-17, `tests/probes/mp-dead-{host,guest}.probe`, rl-mpmsg3.md:
   0 divergences); all 6 probe files PASS on Windows ≡ wine. **The standalone joins the gate** (2026-11-17): `corpus --check --standalone` runs every scenario a
   second time under `--standalone` (no gap, = the bless in frame / tick / RNG / fb / state_sem); the pre-commit hook
   adds it when `src/` changes (wine: 121/121 = pure retail, 8 min for both halves). Next: `--live` (★3).
5. **The TODO(rl-link) seams** (rl_ functions reaching an OLD port through engine_retail_call; the old port's mirrors /
   event queue / environment came only from rhost's row wrappers) — `docs/findings/rl-link.md`. 2026-11-16: the link
   layer `src/rl_link.c`, the pilot, the **screens** (`rl_link_screens.c`, 104 entries), **mapview**
   (`rl_link_mapview.c`: mv_bind / ti_env moved, 58 entries) and **logic** families (`rl_link_logic.c`:
   `rl_link_bind_once` = rh_port_bind_once's binds + the combat / battle / economy callbacks as rl_ ports, 18 EVQ
   entries) — every listed seam site calls its `rl_<va>` but 0x465d28 (engine_army_move_run: caller-stack residue, only
   rhost models it). `rl_link_bind_all` runs from `rl_crt_start`. Fixed on the way: `rl_link_port_leave` reloads the
   mirrors for a live caller (47 standalone scenarios were off in g_gfx_char_counter). Verify (wine, mapview + logic tables, 34 scenarios): 270/281 reached, 0 divergences, hybrid PASS; standalone
   121/121 = pure retail. **2026-11-16 later:** literal seams 82 → 3 (direct `rl_<va>` calls; 7 new entries: gfx
   0x40a567 / 0x409934 / 0x409c93 / 0x402f64, button list 0x40cfd2, combat 0x4ab383 / 0x43be65 — their rows call them);
   no `TODO(rl-link)` left; the link layer is live from `rl_crt_start` (a mirror write before the first seam was lost:
   20 MP sides off). Standalone 121/121 = pure retail; verify (wine, 12 scenarios): 1501/1937 reached, 0 divergences after the walker / rl_439b52-EAX fixes, hybrid PASS.
   **2026-11-17: calls by retail VA** (`rl_va_call`, map `src/rl_va_map.c` generated from the row tables by
   `tools/gen_rl_va_map.py`, 1516 VAs; pre-commit regenerates it): the 7 dynamic dispatch sites switched; 13 new link
   entries for the economy screens' hot-spot actions (trade / gift / county action / Send Supplies). Standalone 121/121 =
   pure retail, misses only 0x4479e9 / 0x448339 (stack-residue rows); verify (wine, 8 scenarios): 162/327 reached, 0
   divergences, hybrid PASS. **2026-11-17 later: 0x4479e9 / 0x448339 done** — the dispatcher models the handlers'
   shared frame slot (quirk #241: 8 at a pass's start, then each handler's leftover); verify `--only '^mp-'` 6/6 rows,
   0 divergences, hybrid PASS; standalone 39/39 MP = pure retail, 0 rl_va misses. **0x465d28 done** (its residue was
   already rl_46521f's model; the binder moved into src: `rl_465d28_res`): standalone 121/121 = pure retail, 0 rl_va
   misses. ★5 closed (rl_mpnet's CRT helpers: ★4, done) — rl-link.md §Left.
6. **Unreached code**: 939 functions / 172 KB no scenario reaches (`docs/findings/coverage-round3.md`): multiplayer
   (DirectPlay / serial — a fake transport in rhost would open it), dead code (zoom-1 / zoom-16 drawers, unwritten
   orders 0 / 7 / 13 / 15 / 16), AI knight garrisons, DirectDraw errors (a fault-injection switch for 0x42f60e's
   HRESULT table), the human queue (below).
7. **Tooling** — 2026-11-17: `tools/bf/dedup_globals.py` read no definition with a trailing comment (every hand-edited one —
   how g_rl_4dc428 / 4ea180 / 4d8a58 slipped through): fixed, any g_bf_/g_rl_ definition it can't parse is now an error,
   `--check` = report only. Verify cost: the shadowed calls over every wine verify log (949 M) — 79 rows average ≥ 8k a
   run (77 %); the 7 EMPTY per-frame functions (rl_4b0aaa / 41423f / 426529 / 4b1dd5 / 4b1de0 / 45242b / 4b12fb, ~10 %)
   now RH_SH_SAMPLE (shadowed calls −85 %, 0 divergences, hybrid PASS). The rest stay full on purpose: input dispatch
   (rl_42ff10 / 4b191e / 4ba9c8 / 4472ce / 47685d), clocks with rare arms (rl_4b27bf / 40490d / 4bbc80), logic leaves
   (bf_*, eco_*), gated per-frame redraws (rl_render.c) — sampling by (caller, screen) would skip their rare arms.
   Next if verify time still hurts: sample per-frame rows except on frames with an input event or a state change.
8. **Data-image documentation** (`docs/data-ledger.md`, catalog `docs/data/catalog.tsv`): **2026-11-17: .rdata / .data
   100 % of non-zero bytes documented** (397 rows; all sections 85.0 %, the rest PE headers / imports / resources) —
   `docs/findings/data-catalog.md` (a port defect fixed, retail OOB reads, dead data, 17 port names to correct). Next
   toward "a port that loads nothing": **2026-11-17: the port's own copies checked** (`tools/data_selfcheck.py` →
   `docs/data-selfcheck.md`, data-catalog.md §The port's own copies): 1768 aliased definitions, 0 differ (10 tables
   without initializers filled, 4 old-port initializers → retail's 0); catalog non-zero bytes 35,252 matched, 3,035 under
   another port name, **8,299 uncovered** (140 rows, worklist in the doc). Next: definitions for the uncovered rows
   (CRT tables first: rl_crt*.c read them by VA), then an image built from the port's copies, compared with the exe's
   (same tool), then the standalone maps no exe section.
9. ~~**Port build `-std=gnu11`**~~ — done 2026-11-17: retail has no x87 code (IDA export: 0 FP mnemonics), the port
   none either; all 213 src modules compile to identical code under c11 / gnu11, rhost.exe byte-identical; src / tests
   Makefiles → gnu11.

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
   **New (2026-11-17): "OpenLords2 (standalone)"** beside it = the same, played by the PORT (no retail code). Every
   standalone session is checked against pure retail at import — the most valuable play now. Look for: anything that
   behaves differently from the original game (it should not), or a "GAP retail VA" stop (the console window says it).
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
