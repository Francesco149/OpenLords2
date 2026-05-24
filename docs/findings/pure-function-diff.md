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

| function name                | retail address    | port file                 | what it does                                  |
|------------------------------|-------------------|---------------------------|-----------------------------------------------|
| `engine_rng_step`            | `FUN_00404a46`    | `src/engine_rng.c`        | double-LFSR step (31 iters, 6 masked outputs) |
| `engine_compute_fps`         | `FUN_004b27bf`    | `src/engine_fps.c`        | rolling 1-sec FPS counter (2× `timeGetTime`)  |
| `engine_compute_anim_cycle`  | `FUN_00451f1a`    | `src/engine_anim_cycle.c` | 16-frame anim-cycle counter bump              |

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

Latest verified run: **200/200 pass on all three functions** (2026-05-24,
retail build via cutestation.soy).

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

## Host-side shared library

`tests/Makefile` grew a `diff` target that builds
`tests/build/libengine_diff.so` from `src/engine_rng.c` +
`src/engine_fps.c` with `-fPIC`, *without* ASan/UBSan (an
ASan-instrumented .so can't be `dlopen`'d from a non-ASan Python).

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
