# Memory-access watch — find unported writers by trapping the write

> Landed 2026-05-29 (Phase H of `docs/plans/structural-parity-harness.md`).
> Ported from openrecet's D.7. Validated live against retail.

## What it does

Finds the **writer** of an engine memory region we can't locate by reading the
decompile (reached via indirect dispatch, or dropped from Ghidra). Frida's
`MemoryAccessMonitor` write-protects the region in retail; every access traps
with the faulting instruction's address → the owning engine function (via
`docs/port-ledger.json`) → the chip to port.

This is the unblock path when a render walker/draw reads a region whose filler
isn't ported, so it draws stale/zero: point mem_watch at that region, drive
retail into the scene, read off the writer VA.

## Pieces

| Piece | File | Role |
|-------|------|------|
| Agent | `tools/frida/openlords2-agent.js` | `installMemoryWatch` (pre-resume or bracketed via `installMemWatchBracket`), precise re-arm (configurable cap), snapshot poll (`memSnapshotPoll`), `mem_access_batch` / `mem_snapshot`, `getMemWatchStatus` |
| Driver | `tools/mem_watch.py` | spawn + drive + rank trapped accesses → owning fn + port status; `--arm-between` / `--snapshot-region` / `--rearm-cap` levers; `_snapshot_summary` |
| Capture | `tools/frida_capture.py` | `CaptureConfig.mem_watch{,_regions,_precise,_rearm_cap,_arm_between}` + `mem_snapshot_regions` → `mem_watch.jsonl` / `mem_snapshot.jsonl` |

## Usage

```sh
# boot/init-time writer (region written during bootstrap):
nix develop -c python3 tools/mem_watch.py \
    --run-dir runs/memwatch-foo --region 0xVA:SIZE:label

# writer that only fires in a scene — drive there first via an input trace:
nix develop -c python3 tools/mem_watch.py \
    --run-dir runs/memwatch-foo --region 0xVA:SIZE:label \
    --input-trace tests/scenarios/<scene>/trace.jsonl

# re-rank an existing capture without re-driving retail:
nix develop -c python3 tools/mem_watch.py --analyze-only \
    --run-dir runs/memwatch-foo --region 0xVA:SIZE:label

# read-hot page → snapshot-diff (no trap; reports the writer's tick):
nix develop -c python3 tools/mem_watch.py \
    --run-dir runs/memwatch-foo --snapshot-region 0xVA:SIZE:label \
    --input-segtrace tools/traces/<scene>.jsonl --rng-seed

# read-hot page → bracket the trap to a suspect call (+ optional bigger budget):
nix develop -c python3 tools/mem_watch.py \
    --run-dir runs/memwatch-foo --region 0xVA:SIZE:label \
    --arm-between 0xENTER:0xLEAVE --rearm-cap 60000 ...
```
See **Hot-page levers** below for when to reach for each.

Stdout is one JSON object (classifier-clean): ranked `writers` (faulting VA →
owning function + status + accessed offsets) and `port_candidates` (the subset
whose owner is unported/stubbed/unmapped — the chips to port).

## The two hard-won constraints (carried + reproduced)

`MemoryAccessMonitor` is **page-granular and op-blind**: it traps the first
access of each 4KiB page then disables that page, and can't tell reads from
writes at arm time.

1. **Cold page** (region written at a discrete event, no per-frame neighbour
   traffic) → raw or precise mode nails the writer: the first page access *is*
   the write. **This is the expected render-gap case.**
2. **Read-hot page** (region's 4KiB page is read every frame) → precise mode's
   re-arm budget (default 8000) burns on neighbour reads before the write lands.
   A tighter region does NOT help — the monitor protects the whole 4KiB page
   regardless of region size, so neighbour-read heat is unchanged. See the
   hot-page levers below.
   - Reproduced live: watching the FPS counter `0x4ea83c` (a read-hot page)
     hit `n_neighbor=8001`, `n_hits=0` — exactly openrecet's `var_input_mask`
     result. Not a bug; the wrong kind of target.
3. HW write watchpoint is byte-granular AND write-only (read heat irrelevant) —
   the correct trap for a write inside a read-hot page — but **crashed retail**
   in openrecet; not implemented here; revisit with single-thread arming +
   tick-freeze (lever 3 below).

## Hot-page levers (added 2026-06-02; the read-hot workarounds)

When a write lands inside a read-hot page, four levers (the agent + `mem_watch.py`
support 1/2/4; 3 is future work):

1. **Bracketed arm — `--arm-between ENTER_VA:LEAVE_VA`.**  Arm the monitor ONLY
   between `ENTER`'s `onEnter` and `LEAVE`'s `onLeave` (Ghidra VAs; `ENTER` may
   equal `LEAVE` to bracket exactly that call) instead of pre-resume.  The budget
   then competes with neighbour reads only DURING the suspect call.  Biggest win
   when the in-window neighbour count is under the budget.
2. **Snapshot-diff — `--snapshot-region VA:SIZE[:LABEL]`.**  Never traps: polls +
   `memcmp`s the region each tick and emits the changed bytes + tick.  Op-agnostic
   and immune to read heat.  Localises the writer's **tick** (then a call-trace
   over that tick pins the function).  **Misses an intra-tick transient** write
   that is overwritten before tick end — use the bracketed trap for those.
3. **HW write-watchpoint** (future) — byte-granular + write-only; the only trap
   that beats a genuinely hot page. Reserve for when you need the faulting insn.
4. **Raise the budget — `--rearm-cap N`.**  Cheap when a short window just
   overshoots; won't scale to a page read hundreds of thousands of times.

**Live validation (the `0x53fa1c` capital-cursor hunt, engine-quirks #174).**
Target: who writes per-army record 4's capital cursor `0x54061c` (page `0x540000`).
- Pre-resume precise: `n_neighbor=8001, n_hits=0` — budget exhausted (the original
  failure that triggered building these levers).
- `--arm-between 0x4676e0:0x4676e0` (bracket `setup_overworld`): armed/disarmed
  correctly, but still `neighbor=8001` in-window → exhausted.
- `--arm-between 0x468d4f:0x468d4f --rearm-cap 60000` (bracket `setup_capitals`
  itself): `neighbor=60001` — the page is read >60k times even inside that one
  function, so each re-arm just re-traps the next read.  Levers 1+4 can't win
  against a page this hot (a useful cap would be impractically large and slow).
- `--snapshot-region 0x54061c:8`: **change localised to tick 567** (the new-game
  init tick), offsets `+0/+1` (capital col/row) + `+4/+5` — zero page-fault cost.
  Lever 2 is the robust answer for this class; it confirmed the cursor is written
  during init (by the ported `setup_capitals`), independent of the static r2 read.

## Validation

- Live raw-mode trap → `0x004aecfc` mapped to owner `FUN_004aecf0` (unported)
  → surfaced as a `port_candidate`. Full arm→trap→rank→ledger-xref path. ✓
- Live precise-mode on a read-hot page hit the documented budget limit. ✓
- Ranking self-test: ported/tested owners classified correctly (not flagged as
  candidates). ✓

## Workflow for a real render gap (Phase J)

1. Drive port + retail into the scene; `call_trace_diff.py` + a frame-capture
   diff to pinpoint which draw reads stale/zero data.
2. Map that draw's read to the exact region VA (from the walker port's
   documented `DAT_`/record offsets).
3. `mem_watch.py --region <that VA>:<size>` (raw first — the bootstrap write is
   likely cold). Map the writer VA via `port_candidates` → port that chip.
