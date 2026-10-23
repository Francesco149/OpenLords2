# OpenLords2 methodology reset — audit and plan (2026-10-23)

Prepared by Claude (Opus 5.5) at the owner's request: "revamp this project to follow the same methodology
as ../openrecet, adapt and improve it", plus an exploration of LLM autoplay through the retail game.
Reference: `../openrecet/docs/audits/2026-10-13-methodology-reset.md` (the original reset; read its §2–§3
for the reasoning, not repeated here). **§8 is the checklist of things that need you.**

---

## TL;DR

1. **The verification foundation is now rhost**: retail `Lords2.exe` runs inside our own process,
   deterministically and headless (`tools/rhost/`). Built and proven today:
   - boot → title → Play Now → Winter 1268 map → two End turns, driven by a trace;
   - **0 differing dwords** between repeated runs (full-state hash identical — no masked slots at all);
   - **Windows and wine agree bit for bit**, in state *and* in every presented frame;
   - ~11k fps at the title, ~2–3k fps in game with per-frame hashing; a 3,700-frame scenario in < 2 s;
   - the per-function porting loop works: `rng_step` (FUN_00404a46 vs `src/engine_rng.c`) —
     **rhost shadow: 2,814 calls / 2 scenarios, 0 divergences; replace: corpus --check PASS**.
2. **Adapted and improved over openrecet / OpenSummoners** — Lords2 is a 2D, 8 bpp, message-driven game,
   so rhost fakes the whole display/sound/window layer in software instead of hooking the real one:
   no fullscreen ever, no display device (runs from the ssh session, no queue runner needed), no
   system-DLL pointers in retail memory (nothing to calibrate), and cross-host identical pixels.
3. **A control channel** lets a program step retail frame by frame, post input and read memory, and
   every input it posts is recorded as a replayable trace. It replaces the Frida probe daemon / MCP,
   is how scenarios get authored, and is the substrate for the autoplay bot.
4. **Autoplay** (`tools/bot/`): TypeSafe's **Jev** decision model (OpenRouter Decisions API —
   typed questions in, probabilities out, ~0.2 s and ~$0.00002 per request) plays the strategy layer
   through rhost; battles auto-resolve. Its games become deterministic scenarios that exercise far
   more situations than hand-made traces. Key: `openlords2-bot`, $3/month cap (work repo `infra keys`).
5. **Earlier "BYTE-EXACT" / "1:1" claims are UNVERIFIED** until re-checked under rhost: they came from
   the cross-process Frida drive, which needed anchors, pins and residual triage (PROGRESS sessions 2–24
   are largely that triage). The existing port is valuable logic; its claims are not evidence.
6. `../lords2-decomp` (byte-exact, 567 rows / 50 KB ≈ 5.9% of .text) is **archived read-only**, per
   the owner's 2026-10-13 decision for the sibling decomp repos.

---

## 1. Where things stood (before today)

**Port** (`src/`): 53k lines of C (106 files), 946 of 2,447 engine functions "touched" (48.5% of code
bytes, `docs/STATUS.md`); builds clean; host suite **2,311 tests: 2,294 pass, 0 fail, 17 skip**. Mostly
pure functions with input/output structs plus Win32 wrappers mirroring retail globals. The port boots,
renders title/menus/map, and runs the SP turn (PROGRESS 2026-06-10).

**Verification**: Frida drove retail cross-process on the Windows host (`tools/frida/openlords2-agent.js`,
6.5k lines; `frida_capture.py`), with anchor-segmented traces (`{"wait": "TITLE"}` + tick offsets),
`--turbo` virtual time on one side, and Trace Studio v3 (`.olv3` index/palette/state diffs) to compare.
The same friction openrecet measured: load seams, phase/RNG origins, "transient vs persistent residual
slots" (sessions 2, 3, 23, 24), harness races (quirk #213), input-scale calibration (quirk #215), and
`click_eligible` / `force_active` pokes needed to make clicks land at all.

**July** drifted to footage tooling (`film_*.py`, `camera_director.py`, `kite_*.py`, `melt_*.py`) and
an LLM-play MCP over the Frida daemon (`openlords2_mcp.py`, `play_game.py`) — the right idea (an AI
player) on a foundation that could not make it deterministic or fast.

**Decomp** (`../lords2-decomp`): 567 matched rows, 50,138 B; plateau; last match 2026-10-08; its
container idles (`sleep infinity`). Archived today (README/AGENTS banner, commit `5b78b9a` there).

## 2. What was built today (`tools/rhost/`, commits `03fd597`..)

| piece | what it does |
|---|---|
| `rhost.c` | maps Lords2.exe at 0x400000 in a .bss hole, resolves imports by policy, virtual clock, process identity constants, frame wrapper on every call site of FUN_004b99c0, probes, watchdog, RESULT line |
| `rh_ddraw.c` | software DirectDraw v1/v2: surfaces (flip chains, Blt/BltFast, color keys, Lock, GetDC), palettes, clippers; the "display" = primary memory + palette |
| `rh_dsound.c` | silent DirectSound with virtual play cursors; `timeSetEvent` callbacks fired on the main thread |
| `rh_win.c` | fake HWND, virtual message queue, trace player, human recorder, visible presenter (StretchDIBits) |
| `rh_heap.c`, `rh_handles.c`, `rh_files.c`, `rh_media.c` | fixed arena, fake handles (kernel + mmio), copy-on-write overlay with a merged FindFirstFile, fixed media pool |
| `rh_state.c`, `rhs.py`, `rh_shot.c` | state hash / dumps / shadow execution; PNG + exact `.idx` frames; frame hash chain |
| `rh_hooks.c` | shadow / replace registry (first row: `rng_step`) |
| `rh_bot.c`, `rhbot.py` | control channel + Python controller (+ an action-script CLI) |
| `rhost.py` | build / stage / setup / run / boot / det / corpus (bless/check) / verify / coverage / view / shots / record / diff, targets `win` and `wine` |
| `rh_smack.c` | deterministic Smacker (`--movies`): smackw32 mapped by rhost at a fixed base, imports bound to the virtual layers, its waveOut soundtrack on the virtual clock — the real boot sequence (intro, imptitle, credits) plays |

Found on the way: **the GOG patch put code past `.text`'s VirtualSize** (FUN_004cfb08, a timeGetTime-paced
animation counter called by FUN_004050c0). A loader that honours VirtualSize runs into zeros on the first
in-game frame; rhost now maps whole pages like Windows. (Also matters for IDA / Ghidra exports.)

### Measured (2026-10-23)

| check | result |
|---|---|
| `det boot -n 3` (Windows, wine) | 0 differing dwords; full hash identical |
| corpus (3 scenarios) Windows vs wine | identical `state_sem`, RNG words and `fb` chains |
| bot session → recorded trace → plain replay | identical hash (`bca1d82af927376c` @ f806) |
| `verify` (shadow rng_step) | 2,814 calls / 2 scenarios, 0 divergences; end states unchanged |
| `corpus --check --hook replace:rng_step` | PASS |
| speed | title ~11k fps (Win), 8–11k (wine); in game 2–3k fps with `--fb-hash` |
| `det boot -n 2 --movies` (12,000 f, three movies) | 0 dwords on both hosts; same hash on both |
| retail coverage of the corpus (`rhost.py coverage`) | 4 hand-made scenarios 37.1% of code bytes → 20 scenarios (with 15 bot games) 44.6% |

## 3. Methodology from here

**Porting loop (per function), the openrecet loop adapted:**
1. **Read** the IDA pseudocode + disasm (`/mnt/ephemeral/openlords2/decompiled-ida/`, `tools/ida/`;
   being produced). The Ghidra export (`docs/decompiled/all.c`) is secondary (drops x87 operands).
2. **Write** the C port. For functions already ported as pure in/out modules, write a retail-layout
   wrapper (read retail globals → call the port → write back), exactly like `rng_port` in `rh_hooks.c`.
3. **Verify** on identical state: `rhost.py verify` (shadow, per call) and `rhost.py corpus --check
   --hook replace:NAME` (hybrid end state + every presented frame).
4. **Commit with an evidence line**: `rhost shadow: N calls / K scenarios, 0 divergences; replace: corpus
   --check PASS` (or the divergence + why).

**Gates:** `det SCEN -n 3` (0 differing dwords), `corpus --check` on both targets (expected.json /
expected.wine.json), `verify`. A render port is proven by the `fb` chain under `replace:` (pure ≡ hybrid in
pixels on every frame) — a shadow cannot re-blit.

**Re-verification of the existing port.** ~900 touched functions carry PROGRESS / quirk claims made under
the Frida drive. Re-verify them by wrapping in batches (wrappers are mechanical: good subagent work), worst
first: the turn driver / AI brain, economy, combat resolver, then renderers via `replace` + `fb`.

**Scenarios grow from three sources:** human play (`rhost.py record`, a visible window on the Windows
desktop; record ≡ replay by construction), hand authoring (`rhbot.py` action scripts), and the autoplay bot.

**Product direction (decide after a pilot, as openrecet did):** the hybrid model — rhost + port = a drop-in
that runs port code and falls back to the user's own retail code for anything not yet ported (the
OpenRCT2/re3 path; OpenSummoners went this way on 2026-10-14) — versus keeping the standalone port with
rhost only as its oracle. Lords2's existing port is standalone-shaped (own data layout), so the pilot
should measure wrapper cost on one subsystem (suggest: the AI brain FSM, already ported and host-tested).

## 4. Autoplay (`tools/bot/`)

- **State**: `l2state.py` reads realms / counties / armies / date from retail memory (being built; VA map
  in `docs/findings/game-state-map.md`).
- **Decisions**: Jev (`~typesafe/jev-latest`, OpenRouter `POST /api/alpha/decisions`) — the state as JSON,
  typed questions (tax level = score, rations = score, labor focus = choice, raise army = noul, target
  county = choice …), answers with probabilities. Sampling from the probabilities (not argmax) gives
  varied games: exploration is the point. `typesafe/jev-router` (chat) is a fallback for free-form text.
- **Actions**: semantic actions compiled to mouse/key input (`docs/findings/ui-action-anchors.md`
  coordinates), checked against the state reader after each action; every input lands in the trace.
- **Battles**: always auto-resolve at first; the interactive battlefield is a later stage.
- **Output**: each game = `tests/scenarios/bot-<seed>-<n>/trace.jsonl` + a decision log; replays need
  no LLM and no network. Variety comes from sampling (the virtual-clock origin does not change Play Now's map).
- **Cost**: ~$0.0001 per decision request; a 20-turn game ≈ $0.005; the key is capped at $3/month
  (expires 2026-12-22).
- **Measured (same day)**: 15 bot games in the corpus (economy, drafts, marches, auto-resolved battles,
  trades, gifts, defeats and restarts), every one replaying bit-exactly on both hosts; retail coverage of
  the corpus 37.1% → 44.6% of code bytes for ~$0.12 of Jev and a few minutes of compute.

## 5. Legacy (keep for reference, don't extend)

- The Frida retail drive: `tools/frida/`, `frida_capture.py`, `probe_daemon.py` / `probe.py` /
  `stop_probe.py`, `openlords2_mcp.py`, `play_game.py`, `play_strategy.py`, `micro_bot.py`, the film/footage
  tools, anchor-segmented traces in `tools/traces/` (their click coordinates remain useful).
- Trace Studio v3's retail capture through Frida (`olv3_capture.py`). The **viewer stays** for human eyes;
  rhost's `.idx` frames (640×480 index plane + palette) are the natural input for a new retail side.
- The 2026-05 PLAN §7 "subagents forbidden" rule: superseded by openrecet's policy (delegate mechanical
  and search work; keep RE interpretation and parity adjudication).

## 6. Risks and unknowns (honest)

- **Movies are opt-in** (`--movies` / trace meta): the existing corpus was recorded without them (SmackOpen
  fails, retail skips the movie). Deterministic Smacker exists (`rh_smack.c`); movie scenarios are next.
- **Sound** is silent in replays; `record` / `--audible` mix DirectSound and movie audio to the real device
  (a side channel that cannot touch retail state).
- **The fake DirectDraw implements what retail uses today** (no stretch Blt, no dest color keys): an
  unimplemented path logs `UNIMPL` loudly rather than guessing. Battle screens are not yet exercised.
- **Multiplayer / SierraNW** are out of scope (DirectPlay stubbed).
- **Real-user timing**: rhost's clock is `step_ms` per frame (17 by default); the real game's frame rate
  is whatever the user's machine gives. Logic is time-driven through `timeGetTime`, so traces stay valid,
  but "feel" judgements still need the human.
- **Port-side determinism** (the standalone exe consuming the same traces with the same virtual clock)
  is not done; under the hybrid model it is not needed.

## 7. Where things are

| item | location |
|---|---|
| rhost | `tools/rhost/` (README), Windows `C:\ol2-rhost\`, wine prefix `/mnt/ephemeral/openlords2/rhost-wine/` |
| scenarios | `tests/scenarios/<name>/{trace.jsonl,expected.json,expected.wine.json}` |
| run results | `/mnt/ephemeral/openlords2/rhost-runs/` |
| IDA export | `/mnt/ephemeral/openlords2/decompiled-ida/` (tools in `tools/ida/`, Windows `C:\ol2-ida\`) |
| bot | `tools/bot/` (`llm.py`, `l2state.py`, …) |
| OpenRouter key | slot `openlords2-bot` on nix-work (`infra keys` in `/opt/src/work/infra`), $3/month |

## 8. Checklist for you

**P1 — decisions**
1. **Direction**: approve rhost as the verification foundation and the per-function loop (§3); approve a
   hybrid pilot on one already-ported subsystem before deciding hybrid vs standalone.
2. **Re-verification**: OK to treat the earlier "BYTE-EXACT organic" / "1:1" PROGRESS claims as unverified
   until rhost re-checks them?
3. **lords2-decomp**: archived read-only today (banner commit). Its podman container `lords2-decomp` (and
   `sotes-decomp`, `ys6-decomp`) still idle with `sleep infinity` — stop them, or leave?

**P2 — quick checks (≤5 min, optional)**
4. See rhost yourself, visible on your desktop (a normal 1280×960 window, never fullscreen; close it to end):
   `python3 tools/rhost/rhost.py record try-me` — play a few turns; the trace lands in
   `tests/scenarios/try-me/` and replays bit-exactly with `rhost.py run try-me`.
5. Smacker: is skipping the intro/event movies acceptable for the logic corpus for now?
