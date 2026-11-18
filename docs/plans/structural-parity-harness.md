# Structural-parity harness — call-graph diff, memory-watch, scenario state-forcing

> Plan, 2026-05-29.  Sub-plan of `docs/harness-roadmap.md` (extends it with
> Phases G/H/I/J).  Goal: methodically chase **structural parity with retail**
> — spot missing/unported calls per frame, find writers we can't locate by
> reading, and deterministically drive any rendering path without playing
> through.
>
> Ported from the machinery proven in `../openrecet`:
> - call-graph diff: `openrecet/src/call_trace.h` + agent emitter +
>   `tools/call_trace_diff.py` (their Phase E.1–E.3, annotation-driven).
> - memory-watch: `openrecet/tools/mem_watch.py` + `installMemoryWatch` RPC
>   (their Phase D.7).
> - scenario / state-forcing: their Phase A/B `input_trace` + `scenario-test.py`
>   + state-forcing RPCs.

## What OL2 already has (the foundation — do NOT rebuild)

The hard primitives openrecet's machinery rides on already exist here:

| Primitive | Where | Reuse for |
|-----------|-------|-----------|
| Per-tick frame anchor (retail) | agent `g_tick_count`, bumped in `engine_tick.onEnter` @ `FUN_004b99c0` | frame field of every retail trace event |
| Per-tick frame anchor (port) | `g_engine_frame_counter`, bumped in `engine_tick_driver` (engine.c), exposed via `engine_get_frame_counter()` | frame field of every port trace event |
| Engine-tick freeze | agent `Interceptor.replace(FUN_0040e890, no-op)` | safe state-forcing windows (install once / run) |
| Input replay | agent PostMessageA drain gated on `g_tick_count` → HWND `DAT_004e5a00`; port `--input-trace` | driving to a scene deterministically |
| Pure-fn diff | `tools/diff_test.py` + `runRetail*` RPCs (7 fns, 200/200) | the oracle pattern the I/O-diff tier reuses |
| Port ledger | `docs/port-ledger.json` (va → name/status/src) | VA→function→port-status cross-reference |
| Ghidra names | `docs/decompiled/functions.csv` | VA→name decoration in every diff report |
| Frida capture plumbing | `tools/frida_capture.py` (`CaptureConfig`, spawn/turbo/mute/kill) | every new driver imports it wholesale |

Both frame counters increment exactly once per `FUN_004b99c0` invocation, so
retail `g_tick_count` and port `g_engine_frame_counter` align **1:1** — the
same property openrecet relies on. No new frame-bracketing work needed (their
Phase E.1 is free here).

## What's missing (this plan builds these)

1. **Call-graph diff** (Phase G) — per-frame "which engine functions did each
   side call", diffed leaf-first. The core ask: spot missing/unported calls.
2. **Memory write/read intercept diff** (Phase H) — find writers of regions we
   can't locate by reading the decompile (indirect dispatch / dropped from
   Ghidra), so render-feeding state stops drawing stale/zero.
3. **Scenario reproduction + state-forcing** (Phase I) — deterministically
   trigger battlefield / world-map / overlay render paths without a play-through.
4. **Iterative parity loop** (Phase J) — glue: drive a render path → call-graph
   diff finds the deepest retail-only leaf → port it (or mem-watch if its writer
   is unfindable) → re-verify. The methodical bottom-up loop.

---

## Phase G — Call-graph diff (annotation-driven)

Mirrors openrecet Phase E.1–E.3. Chosen scheme: **explicit per-site VA
annotation** (`CALL_TRACE_ENTER(0xVA)`), not `-finstrument-functions`. Reasons
(carried from openrecet's audit): lossless and explicit (each traced site names
its engine VA at the source — no fragile auto-derived VA-equivalence map),
doubles as port↔engine documentation, and the `_STUB` variant surfaces
stubbed-but-wired bodies a pure call-count diff would treat as matching.

### G.1 — Port-side tracer (`src/call_trace.{c,h}`)

- Port `openrecet/src/call_trace.h` near-verbatim (rename guard to
  `OPENLORDS2_CALL_TRACE_H`). API: `call_trace_init_from_cli`,
  `call_trace_begin_frame`, `call_trace_end_frame`, `call_trace_shutdown`,
  `call_trace_enter(va, ret_addr, stub)`; macros `CALL_TRACE_ENTER(0xVA)` /
  `CALL_TRACE_ENTER_STUB(0xVA)`.
- Schema: `{"va": <ghidra_va>, "ret_va": <module-rel>, "frame": <N>, "stub"?: true}`.
- Wiring in `src/engine.c` `engine_tick_driver` (the `FUN_004b99c0` port): call
  `call_trace_begin_frame(g_engine_frame_counter)` right after the `++`, and
  `call_trace_end_frame()` before return. Mirror `--call-trace`/`--call-trace-frames`
  CLI through `src/main.c` (same shape as the existing `--input-trace` /
  `--capture` flags).
- Cost when off: one null-check on a static `FILE*` per probe. Output saturates
  on hot functions → always pair with `--call-trace-frames i,j,k`.
- New TU added to `src/Makefile` + `tests/Makefile` (host build).

### G.2 — Annotate ported functions with their engine VA

- Each ported function gets one `CALL_TRACE_ENTER(0xVA)` line at its top, naming
  the engine `FUN_` it implements. Most already record that VA in their
  docstring; `docs/port-ledger.json` carries `va`+`src` for every ported entry,
  so the annotation list is generated, not hand-found.
- New helper `tools/gen_call_trace_probes.py` (small): reads the ledger, lists
  `(src_file, function, va, status)` for ported entries lacking a probe, so the
  annotation pass is mechanical and auditable. (Insertion stays manual — one
  line each — to keep it a reviewable diff; the helper just produces the
  worklist.)
- Mark stub/partial bodies with `CALL_TRACE_ENTER_STUB(0xVA)` so count-parity
  doesn't mask an empty body.

### G.3 — Retail-side emitter (agent RPC + capture flag)

- Agent `installCallTrace(vas[])`: `Interceptor.attach` on each engine VA;
  `onEnter` pushes `{va, ret_va: this.returnAddress.sub(module.base),
  frame: g_tick_count}` into a per-tick buffer; flush one batched `send()` in
  `engine_tick.onLeave` (per-tick batching — per-call `send()` would saturate,
  same lesson as openrecet's d3d_trace). Gate buffering by a frame filter so a
  non-target scenario doesn't push megabytes.
- The VA set to attach = the union of annotated port VAs **and their static
  callees** (one hop out), so "retail called X, port didn't" surfaces unported
  neighbors, not just already-ported ones. Callee set comes from objdump
  `-d` static call extraction (openrecet ships `engine_function_vas.json` for
  this; generate the OL2 equivalent once via `tools/gen_call_trace_probes.py
  --static-callees`).
- `tools/frida_capture.py`: add `CaptureConfig.call_trace{,_vas,_frames}`;
  route `call_trace_batch` events → `<run_dir>/call_trace.jsonl`. Classifier-clean
  stdout (one JSON summary line; chatter → agent.log).

### G.4 — Diff orchestrator (`tools/call_trace_diff.py`)

- Port `openrecet/tools/call_trace_diff.py` near-verbatim. Only changes:
  `functions.csv` path (`docs/decompiled/functions.csv`), and the default frame
  anchors. Keep `--align-on-first 0xVA` (event-anchored alignment — robust to
  different load times / turbo factors between port and retail) and
  `--retail-frame-offset`.
- Output per frame: **overlap** (both called; `≈` if port body stubbed, `≠` if
  count mismatch), **retail-only** (= port-side gap → the work-list), **port-only**
  (= structural divergence or a mistagged VA). Decorated with Ghidra names.
- Exit 0 always — diagnostic, not a gate.

**Deliverables G**: `src/call_trace.{c,h}` wired + on by `--call-trace`;
`installCallTrace` RPC; `frida_capture.py` call-trace mode; `call_trace_diff.py`;
a first HOUSE-analog (battlefield-entry) frame diffed end-to-end.

---

## Phase H — Memory write/read intercept diff

Mirrors openrecet Phase D.7 (built + validated there). Finds the writer of a
render-feeding region we can't locate by reading the decompile.

### H.1 — Agent `installMemoryWatch(regions)` RPC

- Port openrecet's `installMemoryWatch` (`MemoryAccessMonitor.enable`), armed
  **pre-resume from `init`** so an init-time writer during scene bootstrap is
  trapped on its first write. `onAccess` → `{op, from: faulting-insn (module-rel),
  address, region, frame: g_tick_count}`, batched.
- **Precise mode** (default): re-arm on page-neighbor traps, record only
  in-region accesses (MemoryAccessMonitor is page-granular + op-blind). Carry
  openrecet's two hard-won lessons verbatim into the docstring:
  1. cold page (region written at a discrete event) → raw mode nails it; read-hot
     page → precise budget burns on neighbor reads before the write lands. Keep
     the region tight; check page heat first.
  2. HW write watchpoint (`Thread.setHardwareWatchpoint`) is the byte-granular
     fallback but **crashed retail** there — not shipped; revisit with
     single-thread arming + tick-freeze if a hot-page field must be pinned.

### H.2 — Driver (`tools/mem_watch.py`)

- Port near-verbatim. Imports `frida_capture` wholesale. Ranks trapped accesses
  by faulting VA, cross-references `docs/port-ledger.json` → owning function +
  port status → **the chip to port**. `port_candidates` (unported/stubbed/unmapped
  owners) surfaced at the top. Classifier-clean stdout.
- **OL2 adaptation**: openrecet drives past its intro video with `--auto-z-spam`;
  OL2 has no such video. Replace the drive-to-scene mechanism with
  `--input-trace <file>` (the existing PostMessageA replay) to reach the target
  scene, OR `--inject-state` (Phase I.2) to jump straight there. `--analyze-only`
  re-ranks an existing `mem_watch.jsonl` without re-driving.
- First targets: OL2 ledger entries of the "no writer in decompile" class that
  feed battlefield/map render (save-record / stage-state globals). Pin the exact
  stale field first via a G.4 call-trace + frame-capture diff, then point
  `mem_watch` at it (raw first — the bootstrap write is likely cold).

**Deliverables H**: `installMemoryWatch` RPC; `tools/mem_watch.py`; one
render-feeding writer identified and mapped to its chip.

---

## Phase I — Scenario reproduction + state-forcing

Mirrors openrecet Phase A/B. Deterministically trigger a chosen rendering path.

### I.1 — Determinism knobs (port side)

- `--rng-seed <n>` pins `engine_rng` (the `FUN_00404a46` port). Turbo virtual
  clock already exists. `--max-frames` already exists. `--capture-frames i,j,k`
  to sample only listed sim frames (filename `frame_<frame>.bmp`), matching the
  call-trace/mem-watch frame numbering.
- `--input-trace-replay` already lands (Phase 3). Confirm it pins
  `g_engine_frame_counter`-driven replay and a fixed step so the tick scheduler
  never stalls (openrecet learned this the hard way).

### I.2 — State-forcing (jump to a render path without playing)

- Agent `forceGameState(json)`: freeze the tick (`FUN_0040e890` no-op, install
  once), set the scene-selecting globals (`game_state_phase` and the
  battlefield/map/overlay state globals — same surface the pure-fn diff already
  poked for `sim_sub_steps`: `game_state_phase=3`, MP player tables), restore in
  a `finally`. This lets retail render an arbitrary scene for a golden frame.
- Port-side `--inject-state <json>`: set the same globals before the loop, so
  the port enters the identical render path. The two halves share one JSON
  scene-spec schema (versioned, `version: 1`).
- This is what "trigger certain rendering paths" needs: pick
  `{phase: battlefield, map: N, units: [...]}` → both sides render it → diff.

### I.3 — Scenario runner (`tools/scenario-test.py`)

- Port openrecet's runner. Discover `tests/scenarios/<name>/` (ship
  `scenario.yaml` + `trace.jsonl` / `state.json`; goldens are `--bless`-local and
  gitignored — vendor-texture redistribution risk, same as openrecet).
- `--target port|retail|both`. Runs each side with the right flags
  (port: `--input-trace-replay`/`--inject-state` + `--rng-seed` + `--capture-frames`;
  retail: `frida_capture` + input replay / `forceGameState`), **bit-exact** diffs
  captured BMPs vs per-target golden, emits a red-tint overlay PNG per mismatch
  and a `sidebyside.png` (ours|retail) for `--target both`.
- Frame capture itself = existing roadmap Phase 2 (DDraw `Lock`/`Blt` hook +
  the GDI StretchDIBits seat for the DWM-windowed quirk). That hook is a
  prerequisite for retail-side golden frames; port-side capture already writes
  BMPs via `--capture`.

**Deliverables I**: `forceGameState` RPC + `--inject-state`; scenario runner with
bit-exact diff + overlay + sidebyside; 2 starter scenarios (a battlefield-entry
and a world-map view).

---

## Phase J — Iterative parity loop (the payoff)

The methodology the user chose — bottom-up, leaf-first:

1. Pick a render path; drive it on both sides (Phase I scenario / state-forcing).
2. `call_trace_diff.py` for the captured frame → **retail-only** list = functions
   retail ran that the port didn't (deepest leaf first).
3. For each: either it's unported (→ port it, normal chip) or its writer is
   unfindable (→ `mem_watch.py` to locate the writer's VA → port that chip).
4. Re-run; the leaf drops out of retail-only. Repeat until retail-only is empty
   for that frame, then move up a level / to the next render path.
5. Optional `tools/regen-comparisons.py` HTML index (port openrecet's) for an
   at-a-glance multimodal regression board across all scenarios.

Stop condition per frame: retail-only empty, overlap all `=` (no `≈` stubs, no
`≠` count mismatches), frame-capture pixel-diff clean.

---

## Optional — DDraw-op trace (analog of openrecet d3d_trace)

openrecet diffs D3D8 device-call traces (`render_diff.py`) to catch
state-machine divergence (blend mode, transform). OL2 uses **DDraw 1.x**, so the
analog would trace `Blt`/`BltFast`/`Lock`/`SetPalette`/`SetEntries` + the engine's
internal blitter dispatch. **Deferred**: the engine-level call-trace (Phase G)
already shows *which* draw functions ran, and frame-capture pixel-diff (Phase 2)
shows the visual result — between them they cover the structural and visual
divergence classes. Build the DDraw-op trace only if a bug slips between those
two (a draw that runs with the right call graph but wrong DDraw args). Don't
pre-build.

---

## Sequencing

Recommended order (each phase independently useful; G unblocks the methodology):

1. **G** — call-graph diff. Highest leverage: turns "what's missing?" from a
   reading exercise into a per-frame work-list. ~1–1.5 sessions (most code ports
   near-verbatim; the annotation pass is the bulk).
2. **I** — scenario + state-forcing. Needed to *drive* the render paths G diffs.
   The retail frame-capture hook (roadmap Phase 2) is the one genuinely new
   piece. ~1.5–2 sessions.
3. **H** — memory-watch. Cheapest (near-verbatim port), but only pays off once G
   points at a specific "no-writer" gap. ~0.5 session.
4. **J** — iterative loop is ongoing once G+I+H exist.

Alternative: **I before G** if driving render paths is the immediate blocker
(can't diff a frame you can't reach). G and I are mutually reinforcing; H trails
both.

## Cross-cutting design notes (carried from openrecet's scars)

- **Classifier-clean stdout** on every new driver: one JSON summary line;
  debugger/agent chatter → log file only (Anthropic API Usage-Policy classifier
  trips on dense debugger text regardless of legitimacy).
- **Snapshot/restore in `finally`** for every RPC that mutates retail globals.
- **Install freezes/monitors once per run**, tear down once at end — cycling
  enable/disable triggers Frida-internal errors (OL2 already hit this in the
  sim_sub_steps freeze; the existing freeze code is the reference).
- **Per-tick batch** every JSONL emitter's `send()`; per-call saturates.
- **Schema versioning**: `version: 1` header line on each JSONL emitter.
- **Frida remote** = `$OPENLORDS2_FRIDA_REMOTE` (not loopback — WSL2 Hyper-V
  firewall), already the `frida_capture` default.
- **Always inside `nix develop`** for every command.
- **No mandatory retail dependency**: the whole harness is opt-in; default CI
  stays the host unit suite.

## Cross-references

- `../openrecet/src/call_trace.h` — port-side tracer source.
- `../openrecet/tools/call_trace_diff.py` — diff orchestrator source.
- `../openrecet/tools/mem_watch.py` + `docs/plans/d7-mem-watch.md` — mem-watch
  source + its build-log lessons.
- `../openrecet/docs/harness-roadmap.md` Phases D.7 / E.1–E.3 — origin design +
  rejected-alternative rationale (why annotation, not `-finstrument-functions`).
- `tools/diff_test.py`, `tools/frida/openlords2-agent.js` — OL2 reuse surface.
- `docs/harness-roadmap.md` — parent roadmap (this extends it with G/H/I/J).
</content>
</invoke>
