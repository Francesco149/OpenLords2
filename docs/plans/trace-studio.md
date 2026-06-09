# Trace Studio — OpenLords2's port↔retail workhorse

**Status (2026-06-10):** Phase 1 IN PROGRESS. Modeled on
`../openrecet/tools/trace_studio/` (see its `docs/plans/trace-studio-v2.md`),
which is NOT battle-tested — we lift its proven structure and deliberately fix
the parts its own postmortems flagged. This doc is the canonical plan + status.

## Why

One tool that runs retail and the port side by side off the SAME input trace,
captures aligned frames on both, diffs them, and serves a scrub/compare UI —
so every port change is verified against retail in one command, and the user
can flag divergences by clicking on them instead of reading pixel-sample logs.
The standing loop: keep a LIVE session on a real trace, re-capture after every
port change (`--only port`, seconds), the user checks the served diff.

## What already exists (the substrate the studio drives)

- **One segtrace drives both sides** (TAS P5, commit 8ea811d):
  `{t,m,w,l}` input ops + `{wait:ANCHOR}` clock-rebase + `{capture:N,label}`
  anchor-relative captures. Port: `src/input_trace.c` via `--input-segtrace`
  + `--capture-to` (writes `<label>.bmp`). Retail: `tools/frida_capture.py
  --input-segtrace` (agent schedules captures, writes `frame_<tick>.bmp`).
- **Anchors**: retail emits BOOT/TITLE/NEW_GAME/LOADING_START/LOADING_END/
  MAP_FREEROAM (`--anchor-trace` → anchors.jsonl); port consumes TITLE/
  MAP_FREEROAM internally (`main.c current_tas_anchor`).
- **Determinism**: retail `--rng-seed` pins the 3 LFSRs to the canonical
  InitInstance seeds pre-ingame; the port's defaults ARE those seeds
  (engine.c:179, game_init.c:1198) — so seeded retail matches the port with no
  port-side flag. Known ground-truth traces: `tools/traces/grid_load.jsonl`
  (load→map, chrome pixel-1:1), `new_game_to_map.jsonl` (deterministic
  new-game, retail-only so far).
- **Speed**: `--skip-intro` (~200ms to menu) + `--turbo`; the port runs
  natively in seconds. Retail frames arrive over the frida socket (no 9p
  write amplification), 640×480 8bpp ≈ 308KB BMPs.

## Design — and where it deviates from openrecet (deliberately)

openrecet's own findings drive the deviations:

1. **One op-compiler, frozen engine parsers** (kills the "three segtrace
   parsers" trap, openrecet Phase-3 finding). The port C parser and the agent
   JS parser stay FROZEN at the primitive op set: `{t,m,w,l}`, `{wait}`,
   `{capture:N,label}`. ALL sugar lives in `model/ops.py`'s compiler, which
   lowers a studio trace to primitives both engines already speak:
   - `{caprange:[start,count], label, stride?}` → `count/stride` `{capture}`
     ops labeled `<label>/0000`, `<label>/0008`, … (anchor-relative, strided).
     Dense capture, overview cadence (D3), and drill are ALL compiler-level —
     zero engine changes, kept-sets identical by construction.
   - The compiled trace (`trace.compiled.jsonl`) is what BOTH sides replay;
     the studio trace (`trace.jsonl`) is what you edit.
2. **Label-keyed pairing, not ordinal pairing** (openrecet's highest-risk
   invariant was "both sides keep the same kept-count in the same order").
   Our `{capture}` op carries its label through both engines: the port names
   files by label; retail's `segtrace_capture_sched` messages give label→tick
   (now persisted as `captures.jsonl`). Pairing is by NAME — immune to
   kept-count drift; a one-sided label is surfaced as `missing`, not silently
   misaligned.
3. **No D1/D2 engine work.** Lords2 save-loads are one-tick state flips (no
   turbo load-pump frame flood) and frames are 10× smaller with no 9p in the
   retail path. If new-game scroll-spam spans ever flood a capture, the fix is
   compiler-level (don't emit captures there), not an engine gate.
4. **State parity is first-class (Phase 3).** Lords2 is turn-based: sim-state
   divergence matters as much as pixels, and we have the byte-exact save
   schema (`save_schema_table.h`). `{statedump:N,label}` will dump the scalar
   slots on both sides at capture instants (port: C-side table walk; retail:
   agent reads the same VAs) → per-label field diff in the UI. The manifest
   reserves the slot now.
5. **One coordinate space.** Captures are an ordered list of labels (trace
   order). No global-ordinal ↔ segment-frame ↔ absolute-frame algebra; groups
   (compiled ranges) give per-group videos for free.

### Session model

`runs/trace-studio/<session>/`:
```
trace.jsonl            # studio trace (editable; sugar ops allowed)
trace.compiled.jsonl   # compiler output — what both sides replayed
port/    frames/<label>.png  anchors.jsonl  captures.jsonl  log.txt
retail/  frames/<label>.png  anchors.jsonl  captures.jsonl  watch.jsonl  log…
diff/    <label>.png         # amplified per-label diff
group videos: <group>.port.mp4 / .retail.mp4 / .diff.mp4 (dense groups)
session.json           # manifest: captures[], groups[], anchors, verdict
```
Manifest schema `ol2-trace-studio-v1`: `captures[] = {label, group?, port:
{frame}, retail:{frame}, diff:{n_px, pct, mean, max} | "missing"}`, ordered;
`groups[] = {name, labels, videos{}}`; `anchors = {port:[{anchor,frame}],
retail:[…]}`; `verdict = {clean, n_diff, worst}`. Slots reserved: `state`,
`marks`.

### Workflows (CLI v1)

```
trace_studio capture --trace tools/traces/grid_load.jsonl [--session N]
              [--target both|port|retail] [--only port] [--remote HOST]
trace_studio serve [--session N] [--http-port 8780]
trace_studio lint --trace FILE
```
- `capture`: compile → drive port (run-openlords2.sh, local, ~s) + retail
  (frida_capture.py, remote) CONCURRENTLY → convert/rename to label-keyed
  PNGs → per-label diff + verdict → group videos → session.json.
- `--only port`: the fast loop — re-run just the port against cached retail.
- `serve`: session list + scrub UI + a Recapture(port) button so the loop
  runs from the browser.

### Web UI (Phase 2, lifted shapes from openrecet's user-confirmed SPA)

Preact+htm, no build step (vendor file copied). v1 components: SessionPicker,
CaptureStrip (label chips colored by diff severity — the filmstrip), Stage
(port|retail|diff for the selected label + A/B wipe + zoom), GroupVideo
(per-group mp4 scrub when dense), AnchorPanel (port vs retail anchor frames),
JobTray + Recapture. Marks/notes + state panel later.

### Engine/harness changes (small, v1)

- `src/input_trace.h`: `INPUT_TRACE_MAX_OPS` 512 → 8192 (compiled dense
  ranges are N ops). ~620KB static, fine.
- `src/main.c`: when segtrace is active, append anchor edges to
  `<capture-dir>/anchors.jsonl` and `{capture}` hits to `captures.jsonl`
  (symmetric, machine-readable; the port's label-named BMPs stay).
- `tools/frida_capture.py`: persist `segtrace_capture_sched` payloads to
  `captures.jsonl` (label→tick map for the renamer).

## Phases

- **Phase 1 — core pipeline + live session** ← NOW: package
  (`tools/trace_studio/`), compiler+lint, drive both, label pairing, pixel
  diff + verdict, manifest, CLI capture/lint; engine/harness changes above;
  validate on `grid_load.jsonl` (expect ≈ the known 91k-px residual map diff,
  chrome clean) and a strided map caprange. Commit per unit.
- **Phase 2 — serve + SPA**: server (ranged HTTP, session API, recapture
  job) + the web viewer; live session for the user to scrub. The
  llm-feed stays the ping channel; the studio is the workbench.
- **Phase 3 — state parity**: `{statedump}` both sides + field-diff panel
  (save-schema slot names). Then `{wait_until}` port-side when a trace needs
  it (new-game scrolls), port LOADING_*/NEW_GAME anchors as traces demand.
- **Phase 4 — record mode**: retail `--input-record` → studio trace
  (distill: collapse idle, snap to anchors); synth helpers (click macros).
- **Phase 5 — deeper integrations**: call-trace diff per label window,
  mem-watch hand-off, TAS determinism gate (double-run bit-identical check)
  in CI.

## Verification

- Phase 1: `grid_load` session — labels paired 1:1, chrome labels ~0 diff px,
  map label ≈ known residual; two port runs bit-identical per label;
  `--only port` reuses retail. Phase 2: user scrubs the served session,
  confirms the diff matches what the feed comparisons showed.

## Critical files

`src/input_trace.{c,h}` (primitive replay), `src/main.c` segtrace branch
(~2505-2575: post/step/capture; anchor fn 2463), `tools/frida_capture.py`
(segtrace lowering ~983-1050 analog, sched handler ~784, frames ~670),
`tools/frida/openlords2-agent.js` (segtraceTick ~2488, anchorTick ~2392,
captures ~3161), `tools/run-openlords2.sh`, openrecet reference:
`tools/trace_studio/` + `trace_studio_web/`.
