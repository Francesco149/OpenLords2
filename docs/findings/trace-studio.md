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

- grid_load / grid_load_dense map labels: **273 px** (was 92,897) — the
  scroll renderer port (0x47309e, commit a96e7a3) closed the big residual;
  what's left is the cosmetic +3 sprite jitter (quirk #201) on-screen.
- grid_load_probe STATE ribbon: **1 differing slot** (9 → 4 → 1 over the
  2026-06-10 sessions): block 0 `sub_diff [2,3,4]` ONLY (records 512..1279
  at the true 8 B cell stride) — root-caused as the cosmetic +3
  sprite-variant re-roll (quirk #201), accepted.  Everything else GREEN.
- endturn_probe STATE: load byte-exact (flip0 = the 1-slot residual), then
  the turn RUNS; first real divergence = **slot 84 (AI brain cursor,
  FUN_0049a581) port=1 vs retail=4** + blocks 3/4/5 (player/army/territory)
  + retail's AI territory event scroll the port never posts.  The phase-4
  AI brain FSM is the open chip.  (The trace_studio port driver passes
  --turbo so the port's virtual clock matches retail's sub-step rate.)
