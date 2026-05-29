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
| Agent | `tools/frida/openlords2-agent.js` | `installMemoryWatch` (armed pre-resume), precise re-arm, `mem_access_batch`, `getMemWatchStatus` |
| Driver | `tools/mem_watch.py` | spawn + drive + rank trapped accesses → owning fn + port status |
| Capture | `tools/frida_capture.py` | `CaptureConfig.mem_watch{,_regions,_precise}` → `mem_watch.jsonl` |

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
```

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
   re-arm budget (`MEM_WATCH_REARM_CAP` = 8000) burns on neighbour reads before
   the write lands. Keep the watched region tight and check page heat first.
   - Reproduced live: watching the FPS counter `0x4ea83c` (a read-hot page)
     hit `n_neighbor=8001`, `n_hits=0` — exactly openrecet's `var_input_mask`
     result. Not a bug; the wrong kind of target.
3. HW write watchpoint is byte-granular but **crashed retail** in openrecet —
   not implemented here; revisit with single-thread arming + tick-freeze.

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
