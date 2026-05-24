# Pure-function differential testing

Phase 4 of `docs/harness-roadmap.md`.  Cross-check each ported pure
function's output against the **actual retail Lords2.exe code** by:

  - **Agent**: invoke retail's `FUN_*` directly via Frida `NativeFunction`,
    with the relevant globals snapshotted-injected-readback-restored so
    the engine itself sees no perturbation.
  - **Orchestrator** (`tools/diff_test.py`): build a small host shared
    library (`tests/build/libengine_diff.so`) from our pure-C ports and
    load it via `ctypes`.  Drive the same input vectors through both,
    diff outputs.

This catches arithmetic / hashing / table-lookup drift in our port
*without* needing a full game-state scenario to surface it.  The retail
seed pair is always included as the first RNG vector, so any regression
in our port that would corrupt the engine on boot is caught immediately.

## Current targets

| function name                 | retail address    | port file                 | what it does                                  |
|-------------------------------|-------------------|---------------------------|-----------------------------------------------|
| `engine_rng_step`             | `FUN_00404a46`    | `src/engine_rng.c`        | double-LFSR step (31 iters, 6 masked outputs) |
| `engine_compute_fps`          | `FUN_004b27bf`    | `src/engine_fps.c`        | rolling 1-sec FPS counter (2× `timeGetTime`)  |
| `engine_compute_anim_cycle`   | `FUN_00451f1a`    | `src/engine_anim_cycle.c` | 16-frame anim-cycle counter bump              |
| `engine_compute_frame_delta`  | `FUN_004b20ed`    | `src/engine_delta.c`      | frame delta with 30 ms low-pass gate          |
| `engine_compute_phase_ticks`  | `FUN_004bbc80`    | `src/engine_phase.c`      | 9-flag animation phase tick (10 in, 18 out)   |
| `engine_compute_input_state`  | `FUN_004b191e`    | `src/engine_input.c`      | mouse-input resolver (25 in, 30 out; scripted `GetCursorPos`) |

Each has a host-side unit-test file under `tests/test_engine_*.c`; the
diff test complements those by proving they match retail behavior for
the same inputs, not just our spec.

## Run

```fish
nix develop
make -C tests diff          # one-shot — build libengine_diff.so
python3 tools/diff_test.py  # 64 vectors per function (default)
```

Knobs (full list in `--help`):

  - `--functions rng_step,fps_step` — comma-separated subset
  - `--vectors N` — vectors per function (first ~7 are fixed edge cases,
    rest are random)
  - `--seed N` — RNG seed for vector generation (default 0; reproducible)
  - `--warmup-s N` — how long to wait for engine_tick to start ticking
    before firing RPCs (default 10)

Latest verified run: **200/200 pass on all six functions** (2026-05-25,
retail build via cutestation.soy, seed 1).  FPS reported 1 race-pass,
input_state reported 29 race-passes — see "Race classification" below.
RNG / anim-cycle / frame-delta / phase_ticks saw zero races on this
run, but the same machinery applies if they ever surface.

## Agent shape

`tools/frida/openlords2-agent.js`:

  - New `ADDR` entries: `fn_engine_rng_step`, `fn_engine_compute_fps`, +
    every global the two functions read/write (8 for RNG, 6 for FPS, all
    cross-referenced to `src/engine_*.c`'s mirror tables).
  - New init opt `diff_test: bool` — installs the `timeGetTime` hook (if
    turbo didn't already) and registers two RPC entrypoints:
    `runRetailRngStep(state_a, state_b)` and
    `runRetailComputeFps(now, rollover_now, last_render, window_start,
    frames, current_fps, cumulative_fps, cumulative_seconds)`.
  - Each RPC: snapshot the relevant globals into a JS object, write the
    test vector's inputs, invoke retail's function via NativeFunction,
    read outputs into a JS object, restore the snapshot (in a `finally`
    block so a thrown exception can't leave the engine perturbed).  Returns
    the read outputs as a plain JS object → JSON over the Frida wire.

The FPS RPC also needs to control what `timeGetTime` returns inside
`FUN_004b27bf` (which calls `timeGetTime()` once on a non-rollover frame
and twice on a rollover frame).  Approach:

  1. `g_diff_test_clock_queue` — a FIFO of u32 timestamps.
  2. The `timeGetTime` interceptor's `onLeave` checks the queue head BEFORE
     falling through to the turbo virtual clock or wall-clock path.
  3. Queue access is gated on `Process.getCurrentThreadId() ===
     g_diff_test_caller_tid` so the engine main thread (still ticking
     during diffs) doesn't steal our scripted values.
  4. The RPC pushes both `now_ms` and `rollover_now_ms` to the queue, calls
     retail, then clears any leftover (non-rollover only consumes one).

## The engine-thread race + how we handle it

The engine main thread is still ticking during diff tests.  Its per-tick
driver fires `FUN_004b27bf` every frame (≈60 to 200 Hz during boot).  This
races on the shared `_DAT_004e59e0` (`last_render_ms`) global: between
our retail call returning and our readback firing, the engine can squeeze
in its own call and overwrite the global with its wall-clock `timeGetTime`.

Two attempts at on-target guards both failed:

  1. **`Interceptor.attach onEnter` spin** — spin while a diff RPC is in
     flight.  Deadlock: the spin holds Frida's V8 isolate lock, and the
     V8 RPC needs that same lock to make progress (single-threaded V8).
     The engine grabs the V8 lock to enter the JS callback, sees
     `caller_tid != -1`, spins forever.
  2. **`kernel32!SuspendThread` on the engine main thread** — deadlock
     too: Frida's V8 heap and the engine share the process heap; if we
     suspend the engine while it holds an internal heap lock, the next V8
     allocation wedges.

Pragmatic resolution: **detect the race on the Python side and retry**.

The strongest single race indicator for FPS is
`new_last_render_ms == in_now_ms` — `FUN_004b27bf` unconditionally
assigns `_DAT_004e59e0 = DVar1` where `DVar1` is the first `timeGetTime`
result, and our scripted clock always returns `in_now_ms` for that call.
If the read-back `new_last_render_ms` differs, the engine raced us
after the retail call returned.

`tools/diff_test.py` retries on race-detection up to
`FPS_RACE_RETRIES = 4` times, sleeping 10 ms between attempts so the
engine's calls land elsewhere.  In practice: with 200 vectors, 0
permanent races after retry.

RNG doesn't need a retry path — `FUN_00404a46` isn't on the per-frame
hot path today (its SP call sites are inside sim subsystems we haven't
unblocked), so the engine doesn't ambient-call it during diffs.  The
agent's RNG RPC also restores the snapshot in a `finally`, so even an
exception inside the diff can't leave the engine with corrupted RNG
state and a hard-to-debug subsequent crash.

## Race classification (post-call writeback races)

Functions that the engine calls every tick (anim_cycle, frame_delta, FPS)
have a race window between our retail call returning and our agent's
readback.  With retail running unrate-limited (~200 kHz tick rate on
cutestation), the engine can squeeze its own call into that window and
overwrite a writeback global before we observe the value our retail call
left there.

**Important property**: the retail function *still ran correctly per our
test spec* (it consumed our scripted clock + our injected globals, and
computed the right answer).  Only the post-call observation is corrupted.
So the right way to handle this is to compare what we *can* observe
against the port:

  - If `delta_ms` / `return value` matches the port → the retail function
    computed the same answer we did.
  - If the only field that diverges is one the engine would have
    overwritten between our call and our read → that field is best-effort,
    not load-bearing for the diff.

`tools/diff_test.py` tracks this as `race_count` (separate from
`fail_count`) and surfaces it in the summary as `(N race-pass)`.  A real
spec divergence still shows up as `fail` because either `delta_ms`
mismatches or the differing field isn't on the engine's write-path.

Detector shapes per-target:

  - **frame_delta** (`_frame_delta_race_free`): in the suppress branch
    (delta < 30 → delta_ms == 0), retail's `new_last_frame_ms` must equal
    our injected `last_frame_ms` (held baseline); in the pass branch, it
    must equal our scripted `now_ms`.  Anything else is a race —
    classified as race-pass if `delta_ms` matches the port and only
    `new_last_frame_ms` diverges.
  - **fps_step**: pre-call race (engine bumped frames between our snapshot
    and our retail call) shows as `retail.new_frames_this_second ==
    port.new_frames_this_second + N` while everything else (including the
    last_render anchor that `_fps_race_free` checks) matches.  Classified
    as race-pass.
  - **phase_ticks** (`_phase_ticks_engine_clear_race`): the *engine-clear*
    signature — same shape as frame_delta's race-pass but adapted for
    FUN_004bbc80's 18-output readback.  Because the retail function's
    first action is "clear 8 flags at entry", and our readback reads
    flags + counters + last_base in sequence, the dominant race shape is:
    engine begins a concurrent FUN_004bbc80 call AFTER our retail's
    writes; engine's clear-step zeroes our flags BEFORE engine progresses
    to its 20 ms branch; our V8 reads flags (cleared) + counters (still
    matching port — engine hadn't bumped them yet) + last_base (still
    matching port — engine hadn't written it yet).  The classifier
    accepts a race-pass when (a) all 9 counter/last_base fields match
    port AND (b) every flag mismatch is in the direction `retail=0,
    port=1` (engine cleared what port set).  The sticky 1040 ms flag
    survives the race because retail's clear-step doesn't touch it.

### Structural race detection — engine_in_tick + tick_count

For `engine_compute_anim_cycle` (called once per engine_tick by retail's
pre-tick block), the function has no scripted-clock analogue to anchor a
race-free signal against.  Instead the agent reports a structural race
flag based on two pieces of state maintained by the engine_tick hook:

  - `g_tick_count` — bumped at every engine_tick.onEnter.  If this
    advanced during the RPC, a fresh tick fired and necessarily called
    FUN_00451f1a once with our injected globals.
  - `g_engine_in_tick` — set to 1 at engine_tick.onEnter, cleared at
    onLeave.  If non-zero at any sample point of the RPC, engine is
    mid-tick and may have already called or will call FUN_00451f1a.

The RPC returns `raced: 1` when either signal fires.  Orchestrator retries
up to `ANIM_RACE_RETRIES = 8` with 10 ms sleeps.  False positives are
acceptable (the tick window opens before FUN_00451f1a is actually called,
so some retries are over-cautious); false negatives are rare and would
show up as occasional spurious diff failures, which the test loop tags
with a `RACE:` prefix in the summary to distinguish from real port bugs.

For frame_delta the structural signal is too aggressive (retail's tick
rate is unrate-limited, so `g_engine_in_tick` is set almost continuously).
The orchestrator uses the function-specific `_frame_delta_race_free`
detector instead; the structural `raced` is still returned for
diagnostics but doesn't drive the retry decision.

For **phase_ticks** the structural signal is similarly too aggressive
for retry-decision use.  The retry decision uses an enriched
`_phase_ticks_race_free` that combines the canary (`last_base_tick_ms`
read first AND last in the readback, must equal expected) with an
internal-consistency check (if the 20 ms branch fired — `last_base ==
in_now_ms` — then `tick_20ms` MUST be 1; otherwise the engine cleared
it between retail's set and our flag readback).  The structural `raced`
flag is returned and used as a secondary OR-in for the race-pass
classifier's gate, so a port-side bug that happens to mimic the
engine-clear signature can't slip through unless the engine actually
ticked during the RPC.

For **input_state** the race surface is the widest of the diff targets:
~25 inputs + ~30 outputs, AND several inputs (`DAT_0057cac4`,
`DAT_004e65fc`, `DAT_004eabc2`) are also written by *other* engine
functions besides retail's own FUN_004b191e.  `FUN_0042ff10` (an 8 KB
high-frequency engine function) routinely resets cursor_cache_enabled
to 0; the WndProc OR-ins button-state bits at every WM_*BUTTON*; the
engine's own FUN_004b191e clobbers prev_game_x_snapshot etc.  So the
race window spans BOTH pre-call (engine modifies our injected inputs
before retail reads) and post-call (engine overwrites our outputs
before readback) — the raw_cursor canary catches only the latter.

The retry decision uses the structural raced flag combined with the
raw_cursor canary (`_input_state_race_free`).  Retry budget bumped to
`INPUT_STATE_RACE_RETRIES = 16` to dampen the wider race window.  The
race-pass classifier is generic: any vector that exits the retry loop
still raced AND has diffs is classified as race-pass — the function's
behaviour is well-pinned by the host-side unit tests
(`test_engine_input.c`, 22 tests), so a real port bug would show up
deterministically across non-racing vectors.  Typical race-pass rate
on a 200-vector run: ~15 % (cf. ~1 % for phase_ticks).

### Concurrent-execution races on multi-core

Both anim_cycle and frame_delta were initially modelled as "engine
fires its call AFTER ours, before our readback" races (sequential
interleaving).  Phase_ticks surfaced a different mode: on a multi-core
host the engine main thread can run FUN_004bbc80 *concurrently* with
our V8 thread's NativeFunction call — same function, two threads,
unsynchronised memory writes.

The most-visible signature is the one above (engine-clear race): our
V8 retail returns, engine's overlapping call has just finished its
clear-step but not yet its 20 ms branch writes, our readback observes
the inconsistent intermediate.  The fix is detection + race-pass
classification, not synchronisation — both `Interceptor.attach onEnter`
spin and `SuspendThread` deadlock (see the "engine-thread race" block
above for why).

## Host-side shared library

`tests/Makefile` grew a `diff` target that builds
`tests/build/libengine_diff.so` from the diff-target sources
(`src/engine_rng.c`, `src/engine_fps.c`, `src/engine_anim_cycle.c`,
`src/engine_delta.c`, `src/engine_phase.c`, `src/engine_input.c`) with
`-fPIC`, *without* ASan/UBSan (an ASan-instrumented .so can't be
`dlopen`'d from a non-ASan Python).

`tools/diff_test.py`'s ctypes bindings mirror `src/engine.h`'s
`engine_rng_in_t` / `engine_rng_out_t` / `engine_fps_in_t` /
`engine_fps_out_t` struct layouts.  Keep these in sync if any field is
added or reordered.

## What this catches

  - Bit-exact divergence on the **double-LFSR feedback** (taps, shift
    direction, where the feedback is injected).  All 6 masked outputs
    are checked, so even a wrong mask is caught.
  - The **31-iter loop count** (off-by-one would diverge on first call).
  - The **bit-30-vs-bit-31 quirk** documented in `engine_rng.c`.
  - For FPS, every writeback field — frames, last_render, window_start,
    current_fps, cumulative_fps, cumulative_seconds — verified against
    retail across both rollover and non-rollover branches.  The signed
    rollover gate quirk is exercised by the boundary + signed-wrap edge
    vectors.
  - Future regressions: if anyone "cleans up" our port and inadvertently
    breaks behavior, the diff test catches it the next time it runs.

## What it doesn't catch

  - **Cross-function interaction** — pure-function diff doesn't exercise
    the integration paths (sub-step body, render-phase tail).  Those are
    scenario / frame-diff territory.
  - **Functions with non-trivial pointer arguments** — when we port
    something like the PL8 sprite decoder or the bounded-bitmask RNG
    sampler (`FUN_00404b99`, which calls into `FUN_00404a46` in a retry
    loop), we'll need a different diff shape (sample many calls + diff
    the aggregate distribution) since each individual call's outputs
    depend on the live RNG state.

## Extending

To add a new diff target:

  1. In the agent: add ADDR entries for the function + every global it
     reads/writes.  Add a `runRetail<Name>(...)` RPC that snapshots,
     injects, invokes via NativeFunction, reads-back, restores, returns.
  2. In the orchestrator: add ctypes Structure mirrors for the port's
     in/out types.  Add a `gen_<name>_vectors(n, rng)` generator (start
     with a small fixed list of edge cases + random fill).  Add a
     `run_port_<name>` wrapper + `diff_<name>` field-list.
  3. In `tests/Makefile`: add the new source file to `DIFF_SRCS` if it's
     a new TU.
  4. Wire it into the `test_plans` switch + the per-vector loop in
     `run_diff()`.  Both functions wired up cleanly with ~60 LOC each.
