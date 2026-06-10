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

## Findings log

- **2026-06-10 (Phase 1 shakeout) — retail load-camera flake.** One
  grid_load_dense retail run landed the post-load camera 60px right of the
  save's restored position (template match: dx=-60 dy=0, mean 0.00) and
  STAYED there (within-run stable, n=2 through n=60); two follow-up runs +
  the sparse session all restored exactly (CAMX/CAMY watch: boot 0,0 →
  title default 20,74 → save's 24,2 at the flip tick 647). The PORT was
  bit-identical across all runs (0 px run-vs-run). So retail's save-load
  camera has a ~1-in-N wall-clock-paced disturbance; the port↔retail gate
  must treat a one-off whole-map shift as a retail re-run signal, not a
  port regression. Standard kit when it recurs:
  `--watch CAMX=0x5651b4:i32 --watch CAMY=0x5651b8:i32`.
- **Parity baseline (grid_load / grid_load_dense):** every map label sits at
  92,897 px (30.24%) — the known residual (retail-only 256-colour popup +
  water anim phase + faint glyphs). Chrome-only labels would be ~0; the map
  labels carry the popup. This is the number to beat when the popup gets
  ported / the anim phase gets pinned.
- **2026-06-10 — load-path state divergence FENCED (grid_load_probe).**
  State parity (Phase 3) bisect with captures at MAP_FREEROAM+0/1/2/3/60:
  every diff (9/225 slots) is established INSIDE the load-flip tick and
  FROZEN after — the port's ingame tick is state-identical to retail's
  across 60 ticks on the loaded map. The residual, all load-tick-local:
  (a) slots 18/19: retail does exactly ONE more double-LFSR pull than the
  port — port = canonical+102, retail = canonical+103 advances, constant
  thereafter (so the menu+load consumes ~102 pulls and ingame map idle
  consumes none); (b) slot 74 `s_slot_0053f658` port=0 retail=1 —
  suspected 256c-popup-active flag (the popup is born at load; plausibly
  owns the extra pull too); (c) big blocks 0/3/4/5/11 hash-diff; (d) the
  camera flake (slot 16 = saved+1) recurs and is also load-tick-local.
  Next chips: mem-watch the RNG writer / call-trace the flip tick to name
  the 103rd pull site; per-block sub-hashing to localize the block diffs.
  Port `--rng-seed` (pre-ingame canonical pin, mirrors the agent) landed —
  proven by both sides reaching their values from canonical.
- **2026-06-10 (session 2) — the load-flip tick EXPLAINED and mostly
  PORTED; state residual 9 → 4 slots.**  The probe's whole flip-tick
  divergence decomposed into one retail engine-tick iteration the port
  wasn't running:  (1) the rate controller granted ONE sub-step at the
  flip (game-speed default 90 → 12 ms slider gate + LUT row 0; turbo
  starves it afterwards) — call-traced via `--call-trace-vas
  0x404a46,0x49a010,0x49d638`: exactly 3 calls all in the flip frame, the
  "103rd pull" = the SP per-sub-step stir at 0x4b9c4f;  (2) the sub-step
  ran the substate-0 trio (0x40490d cadence counters → 0x49a010 phase
  dispatch [slot 74's only incrementer; phase-1 tick-1 = rations+grant]
  → 0x4650b0 army-state driver);  (3) the tick's head posted the "Non
  256 colour display" warning (event 0x12a — L2.eng string 297; gated
  on `53f228==1 && 53f22c!=8`, one-shot 5533d4) and the render-phase
  consumer 0x472e46 parked its code in `DAT_0053e910` = the rate
  controller's extended-pause input → SIM FROZEN while the scroll is
  open (quirk #196) — retail's 60-tick freeze is deterministic, not
  wall-clock.  CORRECTION to the earlier finding: slots 18/19 are
  schema entries — the .sav CARRIES the LFSR state (+102 was the
  save's stored stream, not "menu+load pulls"); the port's load now
  re-seeds its RNG mirrors + event-ring struct from the loaded schema
  storage (two stale-mirror bugs).  Ported alongside: the load tail's
  deferred block-2/3 writers (turn-progress=1, FUN_0043e9e2 session
  re-arm, FUN_0049ca32 visibility bits).  Block 11 = the EVENT RING
  HEAD (g_misc_128 ↔ 0x568480), now mirrored both ways.  Residual
  after all of it (sub-hash-localized): block 0 cells 512..1279 band
  (retail run-dependent — anim/wall-clock class), block 2 player-5
  aux-name area, block 3 AI-record bands (+0xdc..+0x160 class, recs
  2..5), settled-only block 11 record 1 (retail posts a SECOND event
  mid-idle — poster unidentified), and the camera flake (hit 2 of 3
  retail runs today).  Pixel: clean-run baseline holds at 92,897 px =
  the scroll itself (render of 0x47309e = the remaining pixel chip).
- **2026-06-10 (session 2) — per-block sub-hashing landed.**  Both
  state dumpers emit `"sub": {slot: [16 range-hashes]}` per block
  (identical FNV-1a ranges, last range absorbs the remainder);
  statediff annotates differing blocks with `sub_diff` range indices.
  First use localized every remaining block residual (above).
- **2026-06-10 — new_game_to_map is config-state-dependent.** The studio
  run shows retail@map+780 inside a Berkshire COUNTY VIEW (banner + castle
  dialog) instead of the recorded clean map: the new-game config screen
  retains the LAST game's settings (persisted outside the trace), so the
  generated world changed since the 2026-06-01 ground truth, and the x=417
  scroll-dismiss over-spray now lands on the map → territory select (the
  exact trap the TAS notes warned about). The port (no entry scrolls yet)
  ends on the strategic map with a Kent panel → honest 68% diff. New-game
  parity needs: (a) config pinning (drive the config page in the trace, or
  poke the config globals), (b) the scroll-active global for a clean
  `{wait_until scroll==0}` (still unidentified), (c) port entry scrolls.
  Both engines DID reach MAP_FREEROAM within 5 ticks (563 vs 568) — the
  menu→newgame→map seam itself replays 1:1.

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
