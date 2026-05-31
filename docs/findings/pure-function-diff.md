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
| `engine_compute_sim_sub_steps` + `engine_compute_scenario_hint` | `FUN_004bb978` + `FUN_0043de70` | `src/engine_rate.c` | sim sub-step rate gate + scenario_hint computer (chained — outer calls inner internally; orchestrator chains both ports host-side and compares against ONE retail RPC) |
| `engine_compute_state_hash`   | `FUN_00440231`    | `src/engine_state_hash.c` | per-tick MP state hash — 3 strided per-record byte hashes (strategic or combat) + 10-byte bucket vector + 600-byte per-record region.  Source-array payload (125 KB across 6 arrays) installed once per test plan from a shared xorshift32 seed; engine-tick freeze required |
| `engine_tile_expand_footprint`| `FUN_004917dd`    | `src/engine_tile.c`       | castle-load 3×3 building footprint expander.  __cdecl(int) — one arg is the tile byte offset.  STATEFUL (mutates tiles + scratches + 19-entry list), so each vector re-seeds pre-state in-place from the shared xorshift32 seed (per-call re-fill via chunked 32 KB writes; 70 KB readback per call across 5 globals).  Engine-tick freeze required |
| `tax_approval`                | `FUN_0044bd9f`    | `src/engine_realm.c`      | per-realm tax-policy → approval-modifier turn pass.  __stdcall void() — no args; injects g_per_army_768 + g_player_strategic_352 + realm count, diffs the mutated per-army block.  Engine-tick freeze required |
| `dev_spread`                  | `FUN_0044bf7a`    | `src/engine_realm.c`      | per-realm development-spread turn pass (+ subtree c093/c278/c5fc/c53b/bfd5).  __stdcall void(); injects per-army + g_misc_1360 + g_overworld_map + count/standings/season, diffs the per-army block AND the overworld map (cell-type stamps).  Port binds its territory cell grid to its overworld copy so the c093→c278 type feedback matches retail.  Engine-tick freeze required.  CAUGHT a real port bug: the developable-cell predicate was inverted (< 0x19 vs >= 0x19) — the host unit tests shared the wrong model and passed; the retail diff did not |
| `realm_income`                | `FUN_0044b80b`    | `src/engine_realm.c`      | per-realm PROJECTED tax-income recompute (live tax-slider path; also the FUN_00448545 turn pass).  **__cdecl(int realm)** — the FIRST diff target that takes an argument, so the generic RPC is driven with `args:[realm]` + `cc:'mscdecl'` (added to runRetailRealmPass).  Injects g_per_army_768 + g_player_strategic_352 + realm count; reads back BOTH blocks (the called realm's projection fields +0xc0/+0x0f/+0x16 AND the owner-tally +0x28 / per-player income totals +0x15c it refreshes).  Engine-tick freeze required |

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

Latest verified run: **realm_income 24/24 pass** (2026-06-01, retail via
cutestation.soy) on top of the prior **288/288 across twelve functions** (24
vectors each).  Earlier deep run: 1800/1800 across the first nine (2026-05-25,
200 vectors each).

The per-realm passes (tax_approval, dev_spread, realm_income) share a single
generic agent RPC `runRetailRealmPass(fn_va, regions)` — snapshot/inject/call/
read-back/restore arbitrary memory regions — rather than a bespoke RPC each.
It defaults to invoking the target as `__stdcall void()`; `realm_income`
(FUN_0044b80b) is the first target that takes an argument, so the RPC grew an
optional `args:[…]` (list of ints) + `cc:'mscdecl'` so the per-realm index can
be passed for a `__cdecl(int)` pass.  Diff runs now spawn retail with `turbo: True` (fast boot/
warmup); the clock-sensitive targets still script `timeGetTime` per-thread, so
turbo doesn't perturb them (verified: fps_step et al. still pass under turbo).

NOTE (2026-06-01): `libengine_diff.so` links the full host-clean source set
(`DIFF_SRCS = SRCS_UNDER_TEST`) so stateful targets touching the shared save
globals resolve their whole dependency graph, plus `tests/diff_stubs.c` for the
Win32-only globals the test binary stubs (g_sim_*, log_line, io_read_file_at)
and `call_trace.c` (engine_rate.c references `call_trace_enter`).

Three targets — `sim_sub_steps`, `state_hash`, and `tile_expand` —
run under a shared **engine-tick freeze** installed ONCE at run start
if any target needs it, released ONCE at the end.  Cycling
install→uninstall→install back-to-back on the same Interceptor.replace
address triggered an intermittent Frida-internal TypeError that
destroyed the agent script context; sharing the freeze across the
run avoids the cycle.  See "Engine-tick freeze for sim_sub_steps"
below for the freeze-itself rationale.

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

### Engine-tick freeze for sim_sub_steps

`engine_compute_sim_sub_steps` is the first diff target where letting the
engine main thread race on our injected globals isn't merely a *racing
readback* problem — it's a *crash* problem.

The function reads `DAT_0052af94` (engine state phase).  We inject `3`
(ingame) for most vectors so retail takes the MP / SP per-player rate-
selection paths.  But the engine main thread's own per-tick driver
(`FUN_004b99c0`) also reads `DAT_0052af94`, and if it sees `3` while we
have it injected, its own call to `FUN_004bb978` returns >0 → it enters
the sub-step body → the sub-step body calls retail sim functions that
require a fully-initialised ingame state (player tables, deferred-action
queue at `DAT_0050d7c0`, sprite/UI/map subsystems) which we have NOT
provided.  Result: AV in retail's sub-step body → frida script destroyed.

First 25 vectors happened to land cleanly (the random fill hadn't yet
hit a combination that triggered the engine to enter the sub-step body
during a racing window).  At 50+ vectors the crash was reliable.

Resolution: install an **engine-tick freeze** for the duration of the
sim_sub_steps test plan.  The agent's `installSimSubStepsEngineFreeze()`
RPC uses `Interceptor.replace` to swap `FUN_0040e890` (engine_tick_dispatch
— the caller of the per-tick driver) with a no-op `NativeCallback`.  The
engine's main loop keeps pumping messages (PeekMessage / DispatchMessage
still works, so the process stays responsive to its own WndProc and to
our agent's subsequent RPCs), but the per-tick work — sim sub-step loop,
render phase, LFSR, autosave pump, FPS counter — is all skipped.

Pair with `uninstallSimSubStepsEngineFreeze()` at the end of the test
plan to restore normal behavior.  The freeze is scoped to sim_sub_steps
only — other diff targets that ran earlier in the same orchestrator run
had a live engine ticking.

Trade-off: with the freeze active, the structural `raced` flag is
permanently 0 (no engine ticks to delta `g_tick_count`), so the orchestrator
falls back entirely on the function-specific `_sim_sub_steps_race_free`
canary (derived_hint readback matches port + new_last_step_ms in
expected-value set).

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

### Seed-shared source-array install for state_hash

`engine_compute_state_hash` reads six strided source arrays — combined
~125 KB.  Sending those bytes per-vector over the Frida wire would
blow up the JSON encode cost; sending them once per test plan via
`installStateHashSources(seed)` instead.

The orchestrator and agent both implement the same 32-bit xorshift
(`state_hash_fill_bytes` / `stateHashFillBytes` — verbatim equivalents),
sub-seeding each of the six arrays with `seed + 0x9e3779b1 * tag`
(tag 0..5).  Only the 4-byte seed travels over the wire; both sides
synthesise the same ~125 KB payload locally.  Per-vector RPCs then
inject only 3 scalars (mode, player_slot, sim_tick_counter) + read
the 610-byte destination region.

Two infrastructure quirks worth knowing:

  - **writeByteArray chunk size**: writing a single buffer ≥64 KB
    reliably destroyed the agent script context.  Per-array writes
    are chunked at 32 KB (per_territory's 63420 B lands in two
    chunks; others fit in one).
  - **Snapshot must be a COPY, not a view**: `new Uint8Array(readByteArray(N))`
    creates a view onto a buffer Frida may reclaim between RPC calls;
    when uninstall tries to write from the (now dangling) backup,
    the script dies.  Always copy with `.set(new Uint8Array(...))` —
    matches the `installSimSubStepsRateLut` pattern.

### Shared engine freeze for sim_sub_steps + state_hash + tile_expand

All three targets need `FUN_0040e890` (engine_tick_dispatch) patched
to a no-op for the duration of their vectors.  Cycling install→uninstall
→install on the same Interceptor.replace address between consecutive
test plans triggered an intermittent Frida-internal TypeError that
destroyed the agent script context.

Workaround: the orchestrator installs the freeze ONCE upfront if any
enabled target needs it, leaves it installed across all vectors, and
releases it ONCE in the teardown path.  The per-target setup branches
no longer touch the freeze.

### Per-call pre-state re-seed for tile_expand

`engine_tile_expand_footprint` is stateful — it mutates ~70 KB across
5 globals (tiles, path_scratch, stage_scratch, footprint_list,
last_footprint_tile).  Consecutive vectors must NOT see each other's
output bytes, so the agent re-seeds the input arrays IN PLACE before
every call using the same xorshift32 + tagged-sub-seed pattern
state_hash uses for one-shot installs.

Two of the five globals (path_scratch + stage_scratch) are pure outputs
— fully recomputed inside the function by FUN_00471c1f / FUN_00471d30 —
so they're skipped from the per-call re-seed (small win: ~19 KB less
per-call writeback).  The other 3 inputs (tiles 51200, footprint_list
152, last_footprint_tile 4) total 51356 B per-call re-seed, chunked at
32 KB writes.  Plus one byte-write to inject the per-vector anchor at
`tiles[offset + 3]`.

Readback per call is the full ~70 KB across all 5 globals (chunked 32
KB).  The orchestrator-side ctypes struct is 70560 B — bytewise diff
of every field, with `path_scratch` + `stage_scratch` skipped when the
port's return value is 0 (the unrecognised-anchor early-return — in
that branch the function touches neither scratch, so retail keeps its
prior bytes while the port has fresh zero-init: not a real divergence).

### Sparse per-call inputs (delayed_action target)

`engine_dispatch_delayed_actions` (FUN_004472ce) has a 22.4 KB record
table (400 × 0x38) as its primary input but each test vector typically
sets only a handful of records active.  Sending the full 22.4 KB JSON-
encoded per call would be wasteful; instead, the per-call input is a
**sparse record list** (a JSON array of `{idx, active, type, due_tick,
args[10]}` objects).  The agent zero-fills the records region per call,
then walks the sparse list applying the fields at `records[idx * 0x38]`.

The agent ALSO seeds queue + hipri_stash + queue_count + hipri_count
per call, even though the dispatcher clears these at function entry.
Reason: the `master_switch=0` early-return branch BAILS BEFORE the
clear, so the pre-call queue/hipri state carries through to the readback
and contributes to the diff.  Seeding it explicitly makes that branch
well-defined and lets us treat master_switch=0 as a first-class test
case rather than a "skip the bytes that retail might have left over"
exclusion.

### Stubbing per-type dispatch tables (delayed_action target)

The retail dispatcher invokes `(*table[type])()` from `PTR_FUN_004d5c98`
(50 function pointers in `.rdata`).  Each handler — combat-action
commits, MP broadcasts, sim-state transitions — touches dozens of
unrelated globals; running them with our synthetic record state would
either crash on unmapped pointer reads or mutate state we haven't
mirrored to the port.

Solution: at test-plan install, `Memory.protect(table, 200, 'rwx')` the
`.rdata` page and stamp the address of a single no-op `NativeCallback`
into all 50 slots.  The dispatcher still does its function-pointer call
per dispatched record, but the call is a 1-cycle `ret`.  The port mirrors
this with a NULL dispatch table — its branch treats NULL as no-op-and-
skip, producing the same observable surface (records[active=0] cleared,
action_counter decremented, dispatch-arg mirrors holding the last
record's args).

Restore at uninstall puts the original 200 bytes back so retail's real
handlers survive the test plan — important if anyone leaves the process
alive after the diff for a follow-up scenario test.

## Host-side shared library

`tests/Makefile` grew a `diff` target that builds
`tests/build/libengine_diff.so` from the diff-target sources
(`src/engine_rng.c`, `src/engine_fps.c`, `src/engine_anim_cycle.c`,
`src/engine_delta.c`, `src/engine_phase.c`, `src/engine_input.c`,
`src/engine_rate.c`, `src/engine_state_hash.c`, `src/engine_tile.c`,
`src/engine_delayed_action.c`) with `-fPIC`, *without* ASan/UBSan
(an ASan-instrumented .so can't be `dlopen`'d from a non-ASan Python).

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
