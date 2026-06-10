# trace_studio — usage + workflow notes

The port↔retail trace workhorse (plan + design rationale:
`docs/plans/trace-studio.md`). One studio trace drives BOTH engines to
label-keyed captured frames; the studio diffs them per label, builds group
scrub videos, and serves a compare UI with in-browser recapture.

## The standing loop

```
# 1. full capture (port local + retail on the frida host, concurrent)
nix develop -c python3 tools/trace_studio.py capture --trace grid_load_dense

# 2. open the studio
nix develop -c python3 tools/trace_studio.py serve --session grid_load_dense
#    → http://127.0.0.1:8780/?session=grid_load_dense

# 3. tweak the port → in the browser hit "⟳ port" (or:)
nix develop -c python3 tools/trace_studio.py capture --session grid_load_dense --only port
#    re-runs ONLY the port (~30 s incl. build) against the cached retail.

# 4. push the paired captures to the llm-feed (user ping channel)
nix develop -c python3 tools/trace_studio.py feed --session grid_load_dense --note "..."
```

Sessions live at `runs/trace-studio/<name>` (name defaults to the trace
stem; re-captures overwrite in place — ONE live session per trace).

## Authoring traces

Primitive ops (what the engines parse — FROZEN; see the plan doc):
`{"t","m","w","l"}` input · `{"wait":"ANCHOR"}` · `{"capture":N,"label"}` ·
`{"wait_until":{…}}` (retail-only). Studio sugar (compiled down by
`model/ops.py` — engines never see it):

```
{"caprange":[START,COUNT], "label":"grp", "stride":S}
```
→ one capture per kept frame labeled `grp.OOOO` (zero-padded offset from
START — an overview (stride 8) and a drill (stride 1) agree on shared
labels). `trace_studio lint --trace X` preflights (anchor vocab, port-gap
anchors, label charset/length/dups, op-count vs the port's 8192 cap,
boot-relative captures…); `compile` prints the lowered trace.

Anchor vocab: retail BOOT/TITLE/NEW_GAME/LOADING_START/LOADING_END/
MAP_FREEROAM; port TITLE/MAP_FREEROAM (lint warns when a trace waits on a
port-gap anchor). NB retail fires NEW_GAME on any state→3 flip (save-loads
too).

## What pairs with what

Captures pair by LABEL, not ordinal: the port writes `<label>.bmp` +
`captures.jsonl`; retail BMPs are named by a SEQUENTIAL capture index, and
`captures.jsonl` (label→tick, written from the agent's
`segtrace_capture_sched`) maps them — sorted unique ticks ↔ the file
sequence. RNG: retail runs with `--rng-seed` (canonical LFSR pin) by
default; the port's defaults ARE those seeds, so both sides agree without a
port flag.

## State parity (the sim oracle)

Every capture instant also snapshots the 225-entry save schema on BOTH
sides (port `src/state_dump.c` walks `g_save_schema`; the agent reads the
same slots by VA from `state_slots.json`, regenerated per capture from
`save_schema_table.h`). Scalars compare by value, big blocks by FNV-1a32
hash; the manifest's `captures[].state` lists differing slots with schema
symbol names and the UI StatePanel shows them for the selected capture.
STATE-CLEAN on a label = the sim states are byte-equal (blocks at
hash-resolution). Both sides run with the canonical pre-ingame RNG pin
(`--rng-seed`, port main.c + retail agent) so LFSR slots compare apples to
apples. To bisect WHEN a divergence is born, probe-capture consecutive
ticks (`{capture:0..3}` after an anchor) — see grid_load_probe.

### Sub-hash localization

Both dumpers also emit `"sub": {"<slot>": [16 range hashes]}` for every
block entry; a differing block's manifest entry carries
`sub_diff: [range indices]` — byte range `r` = `[r·size/16, (r+1)·size/16)`
(last range absorbs the remainder).  Map ranges to records by the block's
stride (e.g. block 3 stride 0x160: range 7 of 132 B ⇒ record 2 tail).

## Debug kit

- `--watch NAME=0xVA:type` (repeatable) → retail/watch.jsonl. Camera:
  `--watch CAMX=0x5651b4:i32 --watch CAMY=0x5651b8:i32`.
- `--keep-raw` keeps frames-raw BMPs; raws are auto-kept when a side's
  conversion doesn't cover the plan.
- Headless UI check (no jsdom in the shell): `node --check` each .mjs +
  curl every import path → 200; screenshot via
  `nix run nixpkgs#chromium -- --headless --screenshot=... --virtual-time-budget=8000 URL`.
- Serve in background via the Bash tool's run_in_background (inline `&`
  gets reaped); kill by port (`ss -ltnp`), not pkill (self-match).

## Known retail flakes (treat as re-run signals, not port regressions)

- **Load-camera off-by-60px** (~1-in-N): post-load camera lands offset but
  within-run stable; the port is bit-identical run-to-run. Watch the camera
  globals when it recurs. See the plan doc's findings log.

## Parity baselines (update when beaten)

- grid_load / grid_load_dense map labels: **92,897 px (30.24%)** — the
  retail-only 256-colour popup + water anim phase + faint glyphs.  The
  popup is the event scroll (consumer 0x472e46 ported for STATE; its
  renderer 0x47309e is the pixel chip).
- grid_load_probe STATE ribbon: **4 differing slots** (was 9 before the
  2026-06-10 session-2 ports): block 0 `sub_diff [2,3,4]` (cells
  512..1279; retail run-dependent), block 2 `[14]` (player-5 aux name),
  block 3 `[7,9,12,15]` (AI-record +0xdc..+0x160 bands), + the
  camera-flake slot 16 when it recurs; settled-only block 11 `[3,4,5]`
  (ring record 1 — retail's mid-idle second event).  LFSR 18/19, phase
  tick 74, blocks 4/5/11-at-flip: GREEN.
