# OpenLords2 progress log

Reverse-chronological journal of substantial changes.  Each entry is
a self-contained dispatch: what was done, what was learned, what the
next candidate milestones are.  Engine specifics belong in
`docs/findings/<subsystem>.md`; this log is the narrative thread.

---

## 2026-05-24 — Pure-function differential testing (Phase 4 of harness-roadmap)

Took the prior entry's "What's next" pointer — pure-function diff was
flagged as highest-leverage there ("catches arithmetic drift in our
ports before scenarios surface it").  Now landed end-to-end, covering
both ported pure functions: `engine_rng_step` and `engine_compute_fps`.

### Agent (`tools/frida/openlords2-agent.js`)

- New `ADDR` block: `fn_engine_rng_step` (`FUN_00404a46`),
  `fn_engine_compute_fps` (`FUN_004b27bf`), and every global the two
  functions read/write (8 for RNG, 6 for FPS — addresses cross-
  referenced to `src/engine_*.c`'s mirror tables).
- Refactored `installVirtualClockHook` → `installTimeGetTimeHook` +
  thin compat wrapper.  The new shape lets the diff path install the
  `timeGetTime` interceptor without arming turbo's virtual clock.
- New scripted-clock primitive: `g_diff_test_clock_queue` (FIFO of u32
  ms values).  The `timeGetTime` `onLeave` hook checks the queue head
  before the virtual-clock fallback.  Queue access is gated on
  `Process.getCurrentThreadId() === g_diff_test_caller_tid` so the
  engine main thread (still ticking during diffs) can't steal scripted
  values.
- New init opt `diff_test: bool` + two RPC entrypoints:
  - `runRetailRngStep(state_a, state_b)` — writes 2 inputs, calls
    retail, returns 8 outputs.
  - `runRetailComputeFps(now, rollover_now, last_render, window_start,
    frames, current_fps, cumulative_fps, cumulative_seconds)` — writes
    6 inputs, pushes 2 clock values, calls retail, returns 7 outputs
    (including return value as `delta_ms`).
- Both RPCs: snapshot all observable globals → write inputs → invoke
  via `NativeFunction` → readback → restore.  Restore is in `finally`
  so a thrown exception can't leave the engine perturbed.

### Orchestrator (`tools/diff_test.py`, new file)

- Builds `tests/build/libengine_diff.so` (new Makefile target,
  `-shared -fPIC`, no ASan since ASan-instrumented .so can't be
  `dlopen`'d from non-ASan Python).
- ctypes mirrors of `engine_rng_in_t` / `engine_rng_out_t` /
  `engine_fps_in_t` / `engine_fps_out_t` from `src/engine.h`.
- Vector generators: each starts with a fixed set of edge cases
  (retail seed pair, degenerate fixed points, all-ones, bit-30 set
  for RNG; rollover / non-rollover / strict-boundary / signed-wrap /
  long-window for FPS), then random fill to `--vectors N`.
- Per vector: invoke agent's RPC, invoke our port via ctypes, diff
  field-by-field.  Mismatch → recorded as failure with full vector +
  retail + port + diff list for the summary print.

### The engine-thread race + how it's handled

The engine main thread is still calling `FUN_004b27bf` every per-tick
driver iteration (≈200 Hz during boot).  Without protection, between
our retail call returning and our readback firing, the engine squeezes
in its own call and overwrites `_DAT_004e59e0` with wall-clock.

Two attempts at an on-target guard both **deadlocked**:

1. **`Interceptor.attach onEnter` spin** — spin while
   `g_diff_test_caller_tid !== -1`.  Frida's V8 isolate is single-
   threaded; the spin holds the isolate lock; the V8 RPC needs that
   lock to make progress; never clears caller_tid.
2. **`kernel32!SuspendThread` on the engine main thread** — Frida's V8
   heap and the engine share the process heap; suspending the engine
   while it holds an internal heap lock wedges the next V8 allocation.

Pragmatic resolution lives **on the Python side**: detect the race +
retry.  Strongest signal is `new_last_render_ms == in_now_ms` —
`FUN_004b27bf` unconditionally writes the first `timeGetTime` result
into that global, and our scripted clock always returns `in_now_ms` for
that read.  If readback differs, the engine raced us after the retail
call returned.  Retry up to 4× with 10 ms sleep between attempts so the
engine's intermediate calls land elsewhere.

RNG doesn't need retries — `FUN_00404a46` isn't on the per-frame hot
path today (its SP call sites are inside sim subsystems we haven't
unblocked), so the engine doesn't ambient-call it during diffs.

### Verified

Two stress runs against retail (via `cutestation.soy:27042`):

| Run                    | rng_step       | fps_step       | retries (rough)  |
|------------------------|----------------|----------------|------------------|
| `--vectors 12`         | 12/12          | 12/12          | ~0–1 / call      |
| `--vectors 200`        | 200/200        | 200/200        | a handful        |

This is the **first bit-exact validation of any ported function
against retail's actual code**.  All 14 baseline smoke checks still
pass (smoke run with `--turbo` exercises the refactored
`installVirtualClockHook` codepath and confirms the smoke-test's
load-bearing log-line greps still match).  Unit tests: 123/123
under ASan + UBSan, unchanged.

### Findings doc

[`docs/findings/pure-function-diff.md`](findings/pure-function-diff.md)
— full design: agent shape, the engine-thread race + why both guards
deadlocked, the host-side ctypes path, how to extend with a new
target, and what this catches / doesn't catch.

### What's next

The three threads from the prior entry remain, minus the one just
landed:

1. **Resume engine-port** inside `engine_tick`.  Pre-tick
   `FUN_00499a1c` (174 B, music/audio update) lands together with the
   audio subsystem; the screen-dispatch ladder unblocks per-screen as
   each subsystem ports.  Both gated on subsystems we haven't started.
2. **DDraw Blt-via-GDI seat** — short-circuit the "Blt invisible under
   DWM" quirk by intercepting `IDirectDrawSurface::Blt` and re-routing
   through GDI `StretchDIBits` (same path our `src/gfx.c` uses).
   Unblocks live recording sessions.  Roughly half a day's work.
3. **Save-file format** — `castles.dat` / `c_hist.dat` / `history.dat`
   parsers.  Lands as `docs/findings/save-file.md` + a host-side
   decoder TU; starts unlocking the simulation subsystem ports.
4. **More diff targets** — the diff machinery is now generic enough
   that any future pure-C port can be wired in with ~60 LOC of agent
   RPC + ~30 LOC of ctypes binding.  Next obvious candidates as they
   port: `engine_compute_anim_cycle` (FUN_00451f1a), the rate
   controller (FUN_004bb978), the bounded-bitmask RNG sampler
   (FUN_00404b99, calls into FUN_00404a46 with retries).

Top-down RE bias next time: option 2 unblocks recording-driven
scenarios, which then unblocks the per-screen subsystem ports
incrementally.  Option 3 unblocks the simulation subsystem chain in
parallel.  Pick whichever feels more interesting next session.

---

## 2026-05-24 — Input record + replay (Phase 3 of harness-roadmap)

Picked the "What's next" pointer from the previous entry: input replay
is the most productive non-engine slice while the screen-dispatch
ladder is still blocked on per-screen subsystems.  The machinery is
fully landed and tick-accurate against retail; a real interactive
recording session is gated on a separate host-display issue (see
"Limitations" below).

### Agent (`tools/frida/openlords2-agent.js`)

- Two new `ADDR` entries: `fn_wndproc` (`FUN_004b29be`) and
  `var_main_hwnd` (`DAT_004e5a00`).  Cached main-HWND is read directly
  from BSS rather than discovered via CreateWindowEx — the engine sets
  it during InitInstance and our drain runs only after engine_tick
  fires (i.e., after the main loop has started), so it's guaranteed
  non-null.
- New `g_tick_count` — 1-indexed engine_tick counter, bumped in
  `engine_tick.onEnter` at the top.  Used as both the recording
  timestamp and the replay schedule index.
- `installInputRecordHook()` attaches to `fn_wndproc.onEnter`, filters
  `args[1]` against the user-input WM_* set (KEY/CHAR/MOUSE — see
  `docs/findings/input-replay.md` for the full list), and emits one
  `kind:'input_event'` event per dispatched message with
  `{t, m, w, l, hwnd}`.
- `drainInputReplayForTick(tick)` posts queued events when their
  stamp ≤ current tick.  `PostMessageA` resolved lazily from
  user32.dll on first use.  Called from `engine_tick.onEnter` right
  after `g_tick_count++`, which puts the posted message in the main
  thread's queue *during* tick N → engine's `PeekMessage` picks it up
  before tick N+1.  Reverse of the recording: same gap between the
  same two ticks.
- Init RPC: two new opts (`input_record: bool`,
  `input_replay: array<{t,m,w,l}>`); ready event reports both.

### Driver (`tools/frida_capture.py`, `tools/smoke-test.py`)

- `CaptureConfig` grew `input_record: bool` and
  `input_replay_path: Path | None`.  The driver preloads the JSONL
  trace before spawn (parse failures surface immediately) and passes
  the parsed array inline via init opts — the Windows-side agent has
  no access to WSL filesystem paths.
- `_load_input_trace()` accepts comments (`#`) and blank lines so
  hand-authored synthetic traces stay readable.
- Two new `on_message` handlers: `input_event` writes a JSONL line to
  `<run_dir>/input_trace.jsonl` + logs to agent.log;
  `input_replay_post` increments a per-run counter for the smoke
  assertion.  Both are no-ops when their respective modes are off.
- New CLI flags `--input-record` / `--input-trace FILE` on both
  driver and smoke; smoke fails if `--input-trace` is set and zero
  events are posted by the end of the window.
- Bonus fix: smoke's "hide_window hooks installed" check was
  unconditional but the hide hooks correctly only install with
  `hide_window=true`.  Moved into the `not args.no_hide` arm so
  `--no-hide` smoke runs don't false-fail.

### Verified

Three smokes against retail (via `cutestation.soy:27042`):

1. `--input-record --turbo` (5 s, hidden window): WndProc hook armed,
   `input_trace.jsonl` created (empty — no events fired because
   hidden window has no focus to dispatch input to).  All 14
   baseline checks pass.
2. `--input-trace runs/synthetic/input_trace.jsonl --turbo` (4 s,
   then 6 s for diagnosis): synthetic trace of 4 events at ticks
   50/100/150/200.  Events 1..3 posted exactly at tick_now=50/100/150
   (`[replay] idx=0 t=50 tick_now=50 m=0x100 w=0x74 l=0x0`).  Event 4
   never fired — diagnosed: the 3rd event (VK_ESCAPE keydown at
   tick 150) hit retail's WndProc ESCAPE handler which sets
   `DAT_005aeb68 = 1` (deferred shutdown) when engine state isn't
   ingame.  Engine drained and exited cleanly within ~250 ticks; the
   smoke's cum_secs dropped from 2515 (baseline) to 2 (engine quit).
   This *validates* both the post mechanism and the engine's escape
   semantics — and is a property worth preserving in our port
   (VK_ESCAPE outside ingame state ≡ Alt-F4).
3. `--no-hide --no-force-windowed` (8 s): engine creates its main
   window (`WinLords2Class` "Lords2") but freezes before DSound init
   when forced into exclusive fullscreen.  Documented as a harness
   limitation in `input-replay.md` (not a regression — base
   `--force-windowed` smoke still gets ~117 fps).

### Limitations on real interactive recording

A live human-driven recording needs the engine's window to be both
visible AND responsive on the harness host.  Today's harness defaults
hit one of two walls:

- `--force-windowed` (default): window appears, but content is
  invisible under DWM (the documented `DDraw NORMAL Blt invisible
  under DWM` quirk).  Can't see what you're clicking.
- `--no-force-windowed`: engine attempts exclusive fullscreen on
  cutestation and hangs before DSound init (probably the desktop
  session isn't attached enough for exclusive mode-set).

Synthetic traces hand-authored from `wndproc.md`'s VK/WM table
round-trip end-to-end and prove the machinery works tick-accurately.
Real recording-from-a-live-session is gated on either a fullscreen-
capable Windows host or a re-impl menu (which uses GDI StretchDIBits
and renders correctly under DWM).

### What's next

The harness now has all three primitives needed for scenario-driven
differential testing: turbo (Phase 1), frame capture (Phase 2), input
replay (Phase 3).  Next slices, in roughly priority order:

1. **Resume engine-port** inside `engine_tick`.  Pre-tick
   `FUN_00499a1c` (174 B, music/audio update) lands together with the
   audio subsystem; the screen-dispatch ladder unblocks per-screen as
   we land each subsystem.  Both are blocked on subsystems we
   haven't started.
2. **Pure-function differential** (Phase 4 of harness-roadmap) — for
   each ported function, cross-check its output against retail via
   Frida `NativeFunction` invocation with the engine suspended.
   `engine_rng_step` (`FUN_00404a46`) and the FPS counter
   (`FUN_004b27bf`) are the obvious first targets: pure, small, and
   already have ASan-passing host-side ports.
3. **DDraw Blt-via-GDI seat** — short-circuit the "Blt invisible
   under DWM" quirk by intercepting `IDirectDrawSurface::Blt` and
   re-routing through `GDI StretchDIBits` (using the same path
   `src/gfx.c` uses for our re-impl).  Unblocks live recording
   sessions.  Probably half a day's work — vtable patch, lock the
   back, StretchDIBits to the HDC of the window we already track.
4. **Save-file format** — `castles.dat` / `c_hist.dat` /
   `history.dat` parsers.  Lands as a `docs/findings/save-file.md`
   plus a host-side decoder TU and starts to unlock the simulation
   subsystem ports.

Top-down RE bias next time: option 2 is the highest-leverage —
catches arithmetic drift in our ports before scenarios surface it, and
costs nothing relative to its protection.  Option 3 is the unlock
for option 1's recording-driven scenarios.

---

## 2026-05-24 — Sub-step body skeleton + double-LFSR RNG port (FUN_00404a46)

Continued the entry-chain port one slot deeper.  The sub-step body
(retail `FUN_004b99c0` lines 88749..88803, inside the
variable-rate sim loop) is now skeletoned in `engine_tick`, with
the SP-only LFSR advance landing as a real port and the rest
documented-as-deferred.

### Survey result — the biggest sub-step callees are MP-only

Reading retail's body end-to-end turned up a finding worth a
dedicated engine-quirks entry: the two functions that look like
"the heart of the sim tick" — `FUN_004472ce` (1046 B,
"advance sim by 1 tick") and `FUN_0043eda0` (637 B, "scheduled
events dispatcher") — are **both entirely MP-gated**.  Their
bodies are wrapped in `if (DAT_00553030 != 0)`, so in SP they
return immediately.

Implication: in SP mode (which is everything our drop-in can
produce today), the sub-step body's actual per-step work is:

- Master tick counter bump (`DAT_0057c998++`).
- LFSR advance (`FUN_00404a46`) — ported this session.
- Turn-end / autosave check (`FUN_0049b8b0`) — deferred, blocks on
  turn subsystem.
- State-machine breaks for save/load.
- Screen-state-specific extras (~11 functions, all blocked on the
  sim / combat / map subsystems).

That's it.  All other "sim work" happens in the pre-tick block
(anim phase, frame delta — already ported) or inside the screen-
dispatch ladder's per-screen handlers.  Documented as engine-quirk
#5.

### Port: `engine_rng_step` (`FUN_00404a46`, 230 B)

- **`src/engine_rng.c`** (new TU, pure C) — port of retail's
  double-LFSR step.  Two independent 32-bit Galois LFSRs with taps
  at bits 0 and 4; feedback injected into bit 30 (not bit 31).
  31 iterations per call.  Outputs six masked snapshots of the new
  state — 15/7/3 bits for each LFSR.  Pure function; every input
  + state mutation in the in/out structs.
- **`src/engine.c`** — wiring.  Eight new file-static mirrors
  (`g_rng_state_a/b` initialised to retail seeds `0x54657687` /
  `0x72641663` from FUN_004b2258 lines 83635-83636; six masked
  output mirrors).  Wrapper `engine_tick_rng_step` bundles + runs
  + writes back.  Wired into the sub-step body's SP-only branch
  (`g_sim_mp_active == 0`), matching retail's call site.
- **`src/engine.h`** — `engine_rng_in_t` / `engine_rng_out_t`
  structs and the pure-function declaration.

### Skeleton: sub-step body

- **`src/engine.c`** — `engine_tick`'s `for (i in 0..sub_steps)`
  loop body now has retail's structural shape.  Five new file-
  static mirrors land alongside (`g_sim_screen_substate`,
  `g_sim_player_human_active`, `g_sim_turn_end_pending`,
  `g_sim_turn_progress`, plus the eight RNG mirrors).  Each
  mirror's retail `DAT_*` cross-reference and producer status is
  documented inline.
- The `load_bailout` flag implements retail's "engine state 0xb →
  return from FUN_004b99c0 entirely" branch (skips the render
  phase tail).
- Everything else inside the sub-step body is a comment-only TODO
  with the retail subfunction address + size + blocker recorded.
- Today still produces 0 sub-steps every call (rate controller's
  scenario hint is 0 until engine state 3), so the loop body is
  unreached and observable behaviour is identical to the prior
  slice.

### Tests landed (9 new, 123/123 total under ASan + UBSan)

- All-zero state stays zero (degenerate LFSR fixed point).
- Seed=1 traces to a specific value (`0x08000001`).  Pinned as a
  regression — naive trace says "state=1 returns to 1" because it
  misses the secondary feedback at iter 28 when the walking bit
  re-hits position 4.  The test prevents future "small clean-up"
  from this not-actually-clean codegen.
- Bit 31 is always cleared after any call (the LFSR is effectively
  31-bit because feedback targets bit 30 and the shift is logical).
- State A and B are independent (zero-state-B stays 0 even when
  state-A evolves).
- Masked outputs match `new_state & mask` for all six fields.
- Reproducibility: same input → same output.
- Retail seed produces non-degenerate (non-zero, non-identity)
  output.
- Reference-comparison: against an independent inline `ref_advance`
  function, for four arbitrary seeds.
- Multi-call evolution: 10 consecutive calls from retail seed,
  each step changes state + cumulative 15-bit OR exceeds 0xff.

### Engine quirks

[`docs/findings/engine-quirks.md`](findings/engine-quirks.md) entry
#5 — the SP/MP asymmetry of the sub-step body.  The two biggest
"sim tick" functions are MP-only no-ops; SP's actual per-step work
collapses to LFSR + screen-state extras.

Two smaller LFSR oddities are documented inline in `engine_rng.c`
but not promoted to engine-quirks entries: the 31-bit-state-on-32-
bit-word shape (bit 31 always 0 after first call) and the seed=1
trivial trajectory.  Neither is load-bearing.

### Verified

- `make -C tests run` — 123/123 pass under ASan + UBSan
  (was 114/114; +9 RNG tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50 --gfx-init` — transcript
  identical to pre-slice (sub_steps == 0 → loop body never runs;
  observable behaviour unchanged).
- `tools/run-openlords2.sh --frames 5000 --gfx-init` —
  `engine_ticks=5000 current_fps=652 avg_fps=648 (over 7 s)`.
  Plausible numbers, no FPS regression from the new mirror
  bookkeeping.

### What's next

The sub-step body is now structurally accounted for (one ported,
eleven-plus deferred with blockers).  The render-phase head + tail
slots are similarly structured.  The remaining unblocked work
inside `engine_tick` is:

1. **Pre-tick FUN_00499a1c port** (174 B) — music/audio update,
   gated on `g_engine_state == 3`.  Body calls `FUN_004af502` +
   `FUN_004af8d9` + `FUN_004afab9` — each is the audio
   subsystem's PL8/song-bank loader, unported.  Lands together
   with the audio subsystem.
2. **Screen-dispatch ladder** — the bulk of `FUN_004b99c0`, ~30
   subsystem dispatches based on `DAT_004eac50`.  Each branch
   blocked on its respective screen subsystem; lands incrementally.
3. **Input replay** — Phase 3 of `docs/harness-roadmap.md`.
   Record `WM_KEY*` / `WM_MOUSE*` as JSONL, replay back through
   `SendMessageA`.  Standalone harness slice independent of the
   engine ports.

Top-down RE bias next time: the engine-port slots inside
`engine_tick` are nearly exhausted, modulo the per-screen
subsystems.  The most productive next slice is probably **input
replay** — gives us scenario reproducibility for when the screen-
dispatch ladder starts landing per-screen handlers.  After that
the engine ports unblock once the first screen subsystem (probably
the title screen / FUN_004afab9 audio update) lands.

---

## 2026-05-24 — Phase 2 frame capture (DDraw back surface → BMP)

Picked option 1 from the prior entry's "What's next": hook the retail
engine's DDraw back buffer and dump the contents as a BMP per virtual
second.  Pairs with the FPS sampler (riding the same cum-secs
heartbeat), gives us the first viewable artifacts from the harness
without yet having to port any renderer code.

### Agent changes (`tools/frida/openlords2-agent.js`)

- Three new `ADDR` entries pointing at the engine's DDraw surface +
  palette globals (`var_dd_primary` `0x004eabd0`, `var_dd_back`
  `0x004eafac`, `var_dd_palette` `0x004df288`).  These were already
  reverse-engineered in `docs/findings/ddraw-dsound-bringup.md`; the
  agent reads them directly rather than hooking
  `IDirectDraw::CreateSurface` to discover them — three pointer reads
  is cheaper than vtable interception, and the engine never
  recreates these mid-run.
- New `captureFrameOnce()` — lazy-builds three `NativeFunction`
  stubs (`IDirectDrawSurface::Lock` slot 25 / `Unlock` slot 32 /
  `IDirectDrawPalette::GetEntries` slot 4 — slot numbers documented
  in `ddraw-dsound-bringup.md`), Locks the back with
  `DDLOCK_WAIT | DDLOCK_READONLY`, reads the `DDSURFACEDESC`
  (width / height / `lPitch` / `lpSurface`), GetEntries-fills a 256-
  entry `PALETTEENTRY` scratch, row-copies pitched indices into a
  tight `width*height` buffer, and emits a `send({kind:'frame', …},
  binaryPayload)`.  Payload shape: `[u32 w][u32 h][u32 idx][1024
  palette][w*h pixels]`.  Unlock in `finally`.
- `engine_tick.onLeave` already detected cum-secs rollovers for the
  FPS-sample emit; threaded the frame-capture trigger through the
  same branch so capture cadence rides the existing heartbeat (one
  capture per virtual second).  Three new init opts:
  `frame_capture`, `frame_capture_max` (default unlimited from
  agent, 16 from Python), `frame_capture_warmup_secs` (default 5 —
  skip captures while the back surface is still uninitialised).
- DDraw vtable slots (`Lock=25 / Unlock=32 / GetEntries=4`) and
  `DDSURFACEDESC` layout (108 B, dwSize-prefixed, lpSurface at +36)
  documented inline so the next reader doesn't have to chase
  headers.

### Driver changes (`tools/frida_capture.py`, `tools/smoke-test.py`)

- New `kind:'frame'` handler in `on_message` — unpacks the payload
  header, slices off the palette + tight pixels, writes a 24-bpp
  top-down BMP via `_write_paletted_bmp` under `<run_dir>/frames/
  frame_NNNNN.bmp`.  Same on-disk shape as
  `src/gfx.c:gfx_capture_backbuffer` so retail-vs-our-port frames are
  trivially diffable.
- `CaptureConfig` + `CaptureResult` grew matching fields; CLI grew
  `--frame-capture` / `--frame-capture-max N` /
  `--frame-capture-warmup-secs N`.
- `smoke-test.py` grew `--frame-capture` / `--frame-capture-max`
  and two new checks: "frame_capture armed" (log marker) and "at
  least one frame BMP written" (filesystem check).

### Verified

Two retail smokes:

1. 6 s window, default warmup=4 → 4 BMPs captured but all-black.
   Pixel histogram showed only one unique byte (0) per frame.  The
   captures were landing during the engine's pre-render phase
   (engine had ticked through cum-secs 1..4 in ~5 ms wall-clock with
   turbo on, well before any render call).  Diagnosis: the back
   surface had been created (`var_dd_back` non-NULL → Lock
   succeeded), but never drawn to.

2. 10 s window, `--frame-capture-warmup-secs 10` → 8 BMPs at
   virtual seconds 11..18.  Pixel histograms now show 21–55 unique
   byte values per frame and ~26 % non-zero pixel coverage.  Mid-
   row RGB sample of the late frames returns dark grays/browns
   matching the Sierra logo screen palette — palette conversion +
   row-pitch tight-pack are correct.

This is the first behaviour-visible artifact from the harness — up
to now we've been emitting events + log lines.  The 26 % non-zero
coverage on the Sierra logo screen also matches expectations: most
of the screen is the black background; the logo bitmap occupies the
middle band.

### Engine quirk side note (not a new entry)

The pre-warmup all-black frames are a useful artifact themselves —
they confirm that Lords II's `FUN_004b99c0` driver runs *before*
the engine paints to the back surface for the first time.  That is,
the per-tick driver is structured "process input, advance sim, draw"
but the first ticks process input + sim only (because the title-
screen state machine hasn't loaded its bitmaps yet); drawing
doesn't start until cum-secs ≈ 5 with our turbo step.  Worth keeping
in mind when we start porting the screen-dispatch ladder — early
ticks see uninitialised render state.

### Findings doc

[`docs/findings/frame-capture.md`](findings/frame-capture.md) —
vtable slots, DDSURFACEDESC layout, the read-three-globals choice
vs. hooking CreateSurface, payload schema, warmup rationale.

### What's next

Same three threads as before, minus the one we just landed:

1. **Sub-step body** — still the largest single port unblock.
   With frame capture now in place, we have a way to verify visual
   behaviour matches retail before/after major sim changes.
2. **Screen-dispatch ladder** — incremental; lands as each screen
   subsystem ports.  Visual diff with frame capture is now
   actionable, so the "is this screen rendering correctly?" question
   has a clear answer.
3. **Input replay** — Phase 3 of the harness roadmap.  Record
   keyboard / mouse events as JSONL, replay back; combine with
   frame capture for side-by-side diffs of identical input streams.

Top-down RE bias still: sub-step body is the biggest unblock for
porting more of the engine.  Input replay is the next biggest
harness investment (lets us script scenarios reproducibly).

---

## 2026-05-24 — Turbo v2 (virtualised `timeGetTime`) + retail FPS sampler

First non-port harness slice in a few sessions.  Picked up the
turbo-v2 hook the FPS port had set up as the obvious next step
(see the prior entry's "What's next") and landed it end-to-end
plus the verification stream.

### Agent changes (`tools/frida/openlords2-agent.js`)

- New `ADDR` table at the top: `fn_engine_tick = 0x004b99c0`
  (retail's per-tick driver), `var_fps_current = 0x004ea174`,
  `var_fps_cum_sum = 0x004ea0f4`, `var_fps_cum_secs = 0x004ea83c`.
  Centralises the four engine addresses the new hooks touch so the
  next port-style address rev only changes one place.
- New `installVirtualClockHook()` — seeds `g_virtual_now_ms` from a
  one-shot real-`timeGetTime` read (avoids the 49.7-day-old-baseline
  trap the FPS counter would otherwise hit on the first window),
  then attaches `winmm!timeGetTime` with `Interceptor.attach`
  +`retval.replace` so the real syscall still runs (cheap on modern
  Windows, just a `QueryPerformanceCounter` shim) and we only
  overwrite the return on the main thread.  Side threads (DPlay,
  DSound mixer, Smacker) keep wall-clock.
- New `installEngineTickHook()` — always on, regardless of turbo.
  `onEnter`:
  - Captures `g_main_thread_id` the first time it sees the engine
    main thread (the virtual-clock gate needs this; without it the
    virtual clock would silently never apply).
  - Advances `g_virtual_now_ms` by `g_turbo_step_ms` when turbo is
    armed.  Bump happens BEFORE the engine reads `timeGetTime` later
    in the same iteration, so the FPS counter / frame-delta / rate
    gate all see the new virtual time within the same tick.
  `onLeave`:
  - Reads `DAT_004ea83c` (cum-seconds); if it changed since the last
    sample, also reads `DAT_004ea174` (current FPS) +
    `DAT_004ea0f4` (cum FPS sum) and emits a `kind:'fps_sample'`
    event.  Once-per-engine-cumulative-second cadence keeps the
    event stream sparse (~1 per virtual second).
- New init opt `turbo_step_ms` (default 17 — matches OpenMare's
  default for a 60 Hz-ish perceived clock under that harness; for
  Lords2's much faster per-tick driver this produces ~500× wall-
  clock speedup, see verification below).  `init` install order:
  always-on hooks first, then the conditional set; turbo block
  installs the virtual clock BEFORE the Sleep no-op so the engine
  never observes a real-clock baseline once turbo is armed.

### Driver changes (`tools/frida_capture.py`, `tools/smoke-test.py`)

- `CaptureConfig` grew `turbo_step_ms`; `CaptureResult` grew
  `fps_sample_count` + `last_current_fps` / `last_avg_fps` /
  `last_cum_seconds` so callers can summarise the turbo curve from
  the result struct without re-parsing the log.
- New `kind: 'fps_sample'` handler in `on_message` — logs a
  one-line `[fps] current=… avg=… cum_secs=… virt_ms=… t_ms=…`
  trace into `agent.log` and tallies into the counter dict.
- CLI: `--turbo-step-ms N` on `frida_capture.py`; smoke-test grew
  `--turbo` + `--turbo-step-ms` knobs plus four new check rows
  that the turbo path actually installed.

### Verified (4 s smoke window, retail Lords2.exe)

Two back-to-back smoke runs, same hide/silent/force-windowed
defaults, only `--turbo` toggled:

| Run            | fps_sample_count | last_cum_seconds | last_current_fps | last_avg_fps |
|----------------|------------------|------------------|------------------|--------------|
| `--turbo` off  | 3                | 3                | 566 743          | 534 152      |
| `--turbo` on   | 1993             | 1993             | 117              | 117          |

The off-baseline "current_fps = 566 743" is the engine's own
counter sampling itself at the CPU-bound iter rate against the
real wall-clock — the engine literally believes it's running at
half a megaframe per second.  Under turbo, virtual time advances
faster than wall-clock by a factor of ≈ 500×, so the counter
re-normalises to the actual frames-per-virtual-second the loop
achieves: 117 — a believable per-render-frame rate.

Both runs passed all 12 smoke checks (8 base + 4 turbo).

### Engine-quirk side note (not a new entry)

The off-baseline FPS read (566 743) is a useful reminder of why
turbo v2 specifically virtualises `timeGetTime` rather than just
nopping Sleep.  Turbo v1 alone makes the loop spin pointlessly:
`timeGetTime` still returns wall-clock so the FPS counter blows
up.  This is documented as the v1 trade-off comment in
`installTurboSleepHook` and the install-order comment in `init`.

### What's next

Three viable threads now that the harness can run the engine at
multiplied wall-clock speed:

1. **DDraw frame capture** — the Phase-2 roadmap item.  Hook
   `IDirectDrawSurface::Lock` on the primary, serialise back-buffer
   contents as BGRA BMP under
   `runs/<scenario>/<ts>/frames/frame_NNNNN.bmp`.  Pairs naturally
   with the FPS sampler — capture-on-rollover gives an automatic
   1-per-virtual-second screenshot stream.
2. **Sub-step body** — still the largest single port unblock.
   Now with turbo, soak-testing the eventual port becomes cheap.
3. **Screen-dispatch ladder** — incremental; lands as each screen
   subsystem ports.

Top-down RE bias still: option 1 is the smallest verifiable step
that unblocks the diff-render workflow.  Sub-step body is the
biggest unblock but also the biggest single chunk; better tackled
once we have frame capture to validate against.

---

## 2026-05-24 — Render-phase tail survey + FPS counter port (FUN_004b27bf)

Continued exiting the per-tick driver one slot at a time.  Surveyed
the small slot between the input-step and screen-dispatch ladder
(`FUN_0040f1a0` — itself another screen-state dispatcher, deferred),
then surveyed and ported the bottom-of-driver render-phase tail.

### Survey results

**Between input-step and screen-dispatch** (single slot):

| Retail        | Size   | Disposition                                                                                                                                                                                                                                                                                                                                                                                                                            |
|---------------|--------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FUN_0040f1a0` | 1117 B | **Deferred — screen-state dispatcher itself.**  Gated on `DAT_0057d340 != 0` (the deferred-screen-change cookie).  Body is ~40 `else if (DAT_004eac50 == <id>)` branches → per-screen UI subfunctions.  Each subfunction blocked on its corresponding screen subsystem.  Lands incrementally as each screen gets ported. |

**Render-phase tail** (six functions, gated on `g_savepump_request == 0`):

| Retail        | Size  | Disposition                                                                                                                                                                                                                |
|---------------|-------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FUN_00429b1d` | 135 B | **Deferred.**  Map-scroll-offset clamp.  Blocked on map subsystem (no producer of the scroll values + dimensions).                                                                                                          |
| `FUN_0047ed34` | 103 B | **Deferred.**  UI-scroll-offset clamp.  Blocked on UI subsystem.                                                                                                                                                            |
| `FUN_00426529` | 11 B  | **Deferred — empty body.**  Same disposition as the other debug-stripped stubs.                                                                                                                                              |
| `FUN_004b27bf` | 143 B | **Ported.**  Rolling 1-second FPS counter — see below.                                                                                                                                                                       |
| `FUN_004b12fb` | 21 B  | **Deferred — `return 1` stub.**  Likely debug-build gate optimised out.                                                                                                                                                      |
| `FUN_004b0e03` | 875 B | **Deferred.**  Palette-fade machine, gated on screen `0x23` (Smacker/intro).  20 ms cadence, 12-step-per-RGB lerp across 256 palette entries; blocked on intro-screen subsystem.                                              |

### Port: `engine_compute_fps` (`FUN_004b27bf`, 143 B)

- **`src/engine_fps.c`** (new TU, pure C, no Win32 deps) — port of
  the rolling 1-second FPS counter.  Inputs: now_ms,
  rollover_now_ms (a SECOND timeGetTime call retail uses only on
  rollover frames as the new window-start basis), last_render_ms,
  second_window_start_ms, frames_this_second, current_fps,
  cumulative_fps, cumulative_seconds.  Outputs: ms-since-last-render
  (delta_ms — retail's caller discards it but the math runs every
  call), new_last_render_ms, new_second_window_start_ms,
  new_frames_this_second, new_current_fps, new_cumulative_fps,
  new_cumulative_seconds, and a `window_rolled_over` test helper.
- **`src/engine.c`** — wiring.  6 new file-static mirrors
  (`g_fps_last_render_ms`, `g_fps_second_window_start_ms`,
  `g_fps_frames_this_second`, `g_fps_current`,
  `g_fps_cumulative_fps`, `g_fps_cumulative_seconds`).  Wrapper
  `engine_tick_fps_step` reads timeGetTime twice (matches retail's
  2-call shape), bundles + calls + writes back.  Lazy-init for
  `g_fps_second_window_start_ms` on first call: retail's
  `FUN_004b2258` does `DAT_004eaba0 = timeGetTime()` at line 83665
  before the main loop spins; we don't have an InitInstance hook
  point yet so we substitute first-call-init.  Without it, the
  first frame would see `now - 0 > 1000` and fire a bogus rollover.
  Wired into `engine_tick`'s render-phase tail, gated on
  `g_savepump_request == 0` (matches retail; always true today).
- **`src/main.c`** — shutdown smoke summary.  After `main loop
  exited after N frame(s)`, prints
  `engine_ticks=X current_fps=Y avg_fps=Z (over W s)` so the harness
  can confirm the per-tick driver is alive and the counter is
  computing plausible values.  Four new public engine readers:
  `engine_get_current_fps/cumulative_fps/cumulative_seconds/frame_counter`.

### Tests landed (9 new, 114/114 total under ASan + UBSan)

- Basic frame: count bumps, no rollover, delta_ms = now - last_render.
- Delta is computed against last_render (not window_start).
- Rollover fires at 1001 ms elapsed; current_fps published, cumulative
  pair advances, window_start becomes the SECOND timeGetTime value
  (rollover_now_ms), frames_this_second resets to 0.
- Strict-boundary: exactly 1000 ms does NOT fire (the gate is
  `1000 < elapsed`).
- Two back-to-back rollovers — cumulative pair adds CURRENT FPS each
  time, not stale.
- Negative elapsed (clock wrap or debug-seeded future window_start) →
  signed-compare suppresses the rollover branch, frame count just
  bumps.  Same quirk shape as `engine_compute_frame_delta`.
- Two-call shape: rollover_now_ms (not now_ms) becomes the new
  window-start basis, but last_render uses the FIRST call.
- All-zeros first-call boundary (now_ms == 0 too) — no rollover.
- 3-second window pathological case (harness pause + resume) — math
  divides by the FULL elapsed not 1000; cumulative_seconds bumps by
  1 not 3 (it counts rollover events, not actual wall-clock seconds).

### Verified

- `make -C tests run` — 114/114 pass under ASan + UBSan
  (was 105/105; +9 FPS tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice except for the new shutdown summary line
  (`engine_ticks=0 current_fps=0 avg_fps=0 (over 0 s)` — engine path
  not running without `--gfx-init`).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — `engine_ticks=50`
  confirms the per-tick driver is firing; FPS row reads 0 because
  50 frames is well under 1 second wall time.
- `tools/run-openlords2.sh --frames 5000 --gfx-init` —
  `engine_ticks=5000 current_fps=649 avg_fps=643 (over 7 s)`.
  Plausible numbers (Sleep(1) on Windows rounds to 1-2 ms → 500-1000
  FPS range).

### Engine quirks

No new entry — the two preserved quirks here (signed-rollover gate
shared with the frame-delta function; two-timeGetTime-call shape)
are documented inline in `engine_fps.c` and pinned by tests, not
load-bearing enough to need their own entries.  The first is a
shared shape with the frame-delta engine-quirk already documented;
the second is benign codegen drift on the order of microseconds.

### What's next

The render phase is now structurally split into three slices:

1. **Render-phase head — done** (input step + the deferred-screen
   dispatcher trampoline `FUN_0040f1a0` surveyed-as-deferred).
2. **Screen-dispatch ladder** — the bulk of `FUN_004b99c0`.  ~30
   subsystem calls + a long stream of phase functions
   (`FUN_00472a31`, `FUN_00419c78`, `FUN_0040c725`, …) interspersed
   with the `FUN_004b1cf3(N)` scoreboard dispatch.  This is where
   the bulk of per-frame UI/sim work runs.  Blocked on the
   corresponding subsystems; lands as those land.
3. **Render-phase tail — five-of-six deferred, FPS counter ported.**
   The remaining five are blocked on map/UI/intro subsystems.

The natural next slice is one of:

1. **Sub-step body** — retail's per-sim-tick block inside the
   variable-rate loop.  Blocked on the sim subsystem (`FUN_004472ce`
   itself is a major chunk).  Largest unblock and most productive
   for forward progress, but also the biggest single chunk.
2. **Turbo v2 in the Frida agent** — virtualise `timeGetTime` so
   the harness can drive faster-than-real-time soak runs.  Now
   that the FPS counter is alive, we have an immediate visual
   confirmation knob (current_fps should rise N× with the turbo
   scale).  Same shape as OpenMare's; small port.
3. **Engine state mirror** — write a producer that can transition
   `g_engine_state` to 3 (ingame).  Unblocks the rate controller's
   real scenario-hint path, the sub-step body's screen-state
   branches, the music-update gate (`FUN_00499a1c`), and the dead
   `engine_scenario_rate_hint` body.  Substantial — depends on
   menu→ingame transition which itself depends on the savepump
   chain.

Top-down RE bias says turbo v2 — the FPS counter just landed; it's
the smallest verifiable step that also gives us a knob for the next
soak-test cycle.  Lift in the harness, validate against the new
smoke summary.

---

## 2026-05-24 — Render-phase head survey + mouse-input resolver (FUN_004b191e)

First slice exiting the pre-tick block.  Surveyed all three retail
render-phase-head functions; ported the one substantive function;
documented the two trivial slots as deferred.

### Survey result

| Retail        | Size  | Disposition                                                                                                                                                |
|---------------|-------|------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FUN_0049a3e6` | 109 B | **Deferred — dead branch.**  Gated on `DAT_004eac50 == 0x24`.  Cross-grep of every site that *writes* to the screen-ID byte: 0x23 and 0x25 are produced; 0x24 is never written.  Function is unreachable in retail.  Likely an orphaned screen state from an earlier revision. |
| `FUN_004b1de0` | 11 B  | **Deferred — empty body.**  Twin of `FUN_004b0aaa` from pre-tick: ship-build-stripped debug hook.                                                            |
| `FUN_004b191e` | 981 B | **Ported.**  Mouse-input resolver — see below.                                                                                                              |

### Port: `engine_compute_input_state` (`FUN_004b191e`, 981 B)

- **`src/engine_input.c`** (new TU, pure C, no Win32 deps) — port of
  the per-frame mouse-state resolver.  Inputs: screen-space cursor,
  `timeGetTime` ms, WndProc-mirrored button + dblclk bitmasks,
  viewport bounds, prior frame's resolved input state.  Outputs:
  game-coord cursor, motion event flag, button-edge flags (newly
  pressed / newly released for LMB + RMB), 300 ms delayed-single-
  click events, double-click events, click coordinates.  Two retail
  helpers (`FUN_00404dc1`, `FUN_00404d6b`) inlined as static
  `percent_ratio` / `apply_percent` — they're a percent-ratio +
  percent-application pair used in the viewport→game-coord mapping.
- **`src/engine.c`** — wiring.  New file-static `g_input_*` mirrors
  for the 25 retail globals this function touches; `extern` decls
  for the 5 already-existing main.c-owned mirrors (`g_mouse_x/y`,
  `g_mouse_buttons`, `g_mouse_dblclk`, `g_client_screen`).  New
  wrapper `engine_tick_input_step` calls `GetCursorPos` +
  `timeGetTime`, bundles into the input struct, runs the pure
  function, writes outputs back, and clears `g_mouse_dblclk` to
  mirror retail's `DAT_004eada1 = 0` final statement.  Wired into
  `engine_tick`'s render-phase head, right after the sim sub-step
  loop (matches retail's call order in `FUN_004b99c0`).
- **`src/main.c`** — promoted `g_mouse_x/y`, `g_mouse_buttons`,
  `g_mouse_dblclk`, `g_client_screen` from `static` to non-static so
  `engine.c` can `extern` them.  Comments updated to point at
  `engine_tick_input_step` as the new consumer.

### Tests landed (22 new, 105/105 total under ASan + UBSan)

Core matrix (22):
- Raw cursor always written.
- Cache update gated on `cursor_cache_enabled` (both branches).
- Game-coord 1:1 mapping + scaled mapping + div-by-zero clamp.
- Default clears all 10 event flags.
- Dblclk bits → flags + matching pending-click timer cleared.
- Button bits decoded into `new_lmb` / `new_rmb`.
- LMB press edge → `lmb_newly_pressed` + `mouse_event` +
  `button_changed`; timer NOT armed.
- LMB release: `lmb_newly_released` ALWAYS set (the
  `(DAT_004e65d8 = 1, DAT_004e65f0 != 0)` comma quirk in retail);
  arm only when `click_eligible`.
- LMB release-with-eligibility: arms timer, clears eligibility,
  records click coords, advances `click_timer_ms`.
- RMB mirror of the LMB block.
- 300 ms expiry: at 301 ms fires + disarms; at exactly 300 ms does
  NOT fire (strict `300 <` boundary).
- Shared-baseline quirk: both LMB+RMB armed against the same
  `click_timer_ms` → both fire on the same call when the window
  expires.  Quirk preserved.
- Motion x-change / y-change set `mouse_event` + reset
  `motion_last_ms`; no-motion computes `motion_idle_ms`.
- `prev_*` snapshots always written.
- `last_click_x/y` carried through on non-click frames (this is
  why the input struct grew `prev_last_click_x/y`).

### Verified

- `make -C tests run` — 105/105 pass under ASan + UBSan (was 83/83;
  +22 input tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice (`OpenLords2 starting → main loop exited → OK :Not
  active.`).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### Engine quirks

No new entry — the shared-300 ms-timer baseline between LMB and RMB
is unusual but documented inline in `engine_input.c` and pinned by
test, not load-bearing enough to need an engine-quirks entry of its
own.  The dead branch in `FUN_0049a3e6` (gate value 0x24 never
written) is a structural finding documented in main-loop-tick.md,
not a behavioural quirk in code.

### What's next

The render phase has two more head slots and a tail:

1. **Render-phase head: rest of `FUN_004b99c0` between input step
   and the screen-dispatch.**  Need to survey what runs between
   `FUN_004b191e` and the big `else if (DAT_004eac50 == ...)`
   screen-dispatch ladder.  Probably small.
2. **Render-phase tail** — `FUN_00429b1d` / `FUN_0047ed34` /
   `FUN_00426529` / `FUN_004b27bf` (FPS counter) / `FUN_004b12fb` /
   `FUN_004b0e03` + the WM_CLOSE deferred-shutdown trigger.  Gated
   on `g_savepump_request == 0` which is always true today.
3. **Screen-dispatch ladder** — the 30+ `else if (DAT_004eac50 ==
   <id>)` branches that drive per-screen rendering.  Largest single
   chunk in the function; lands incrementally as the menu / load /
   ingame screens get their UI subsystems.

Top-down RE bias: option 1 (survey between input-step and screen-
dispatch) is the smallest verifiable step.  If it turns out to be
empty / one trivial slot, option 2 (FPS counter) is the next
productive lift — gives a visible smoke artifact even without the
render subsystem to feed it.

---

## 2026-05-24 — Frame delta + 16-frame anim cycle (FUN_004b20ed, FUN_00451f1a)

Two more pre-tick slices landed back-to-back, plus the survey
result for the five remaining pre-tick subfunctions.  Pre-tick
block is now fully accounted for: three ports landed, five
documented-as-deferred.

### Ports

- **`src/engine_delta.c`** (new TU, pure C) —
  `engine_compute_frame_delta(in, out)` is the port of retail
  `FUN_004b20ed @ 0x4b20ed` (68 B).  Wall-clock ms-since-last-frame
  generator with a 30 ms signed low-pass.  Suppresses both small
  positive deltas (< 30 ms) and negative deltas (clock wrap,
  debug-seeded future baseline) — divergent from the phase-tick
  generator which fires on negative.  Result lands in
  `g_frame_delta_ms` (retail `DAT_004ea128`); downstream
  sprite-animation updaters (e.g. `FUN_0040da1e`) consume it but
  none of those are ported yet, so the value is write-only today.
- **`src/engine_anim_cycle.c`** (new TU, pure C) —
  `engine_compute_anim_cycle(in, out)` is the port of retail
  `FUN_00451f1a @ 0x451f1a` (81 B).  16-frame counter that bumps
  once per 80 ms phase tick — full cycle 1280 ms.  First slice
  with an in-pure-function data dependency between two ports:
  consumes `g_phase_tick_80ms` produced by `engine_compute_phase_ticks`
  last commit.  Downstream consumer (`FUN_004071a0`, sprite-strip
  indexer reading the counter as a frame index into a 16-slot
  strip starting at `0x28`) unported, so the counter spins with
  no reader.
- **`src/engine.c`** — wiring.  Two new file-static wrappers
  (`engine_tick_delta_step`, `engine_tick_anim_cycle_step`) added
  + new mirrors (`g_frame_delta_last_ms`, `g_frame_delta_ms`,
  `g_anim_cycle_counter`).  `engine_tick`'s pre-tick block now
  invokes `delta_step → phase_step → anim_cycle_step` in that
  order; the dependency between phase and anim is commented inline
  since it's a real ordering constraint (other slices' order is
  cosmetic-only, retail-trace-bisection convenience).

### Tests landed (15 new, 83/83 total under ASan + UBSan)

Frame delta (8):
- Below threshold (29 ms) → 0, baseline held.
- At threshold (30 ms) → 30, baseline advances.
- Just below (29 ms) — boundary on the other side.
- Large delta (4000 ms) → 4000, advances.
- Zero delta → 0, baseline held.
- Negative delta (clock wrap) → 0, baseline held — KEY divergence
  from phase-tick's clock-wrap-fires behaviour, pinned here.
- 32-bit overflow at the wraparound seam → small-positive ms ⇒
  advances normally.
- Boot path (baseline = 0) → first call past 30 ms advances.

Anim cycle (7):
- No tick → counter held.
- Tick at counter 7 → 8.
- Tick at counter 0 → 1.
- Wrap at 15→0 (the retail `0xf < c` after-bump test).
- 14→15 no-wrap boundary on the other side.
- Full 16-tick cycle returns to 0.
- No tick with max counter holds.

### Engine-quirks entry #4

The 16-frame anim counter has a defensive lower clamp (`c < 0 →
c = 0`) that's provably unreachable from the only writer.  Counter
starts at 0 in .bss, only ever bumped by 1, only ever reset to 0
by the upper clamp.  Two wasted instructions on every pre-tick
iteration for a condition that physically can't occur.  Ported
faithfully for byte-shape parity.

### Pre-tick survey — completed

Five functions remain in the pre-tick block; all surveyed
end-to-end this session.  Disposition documented in
[`docs/findings/main-loop-tick.md`](findings/main-loop-tick.md)
§"Pre-tick subfunctions — surveyed, deferred":

- `FUN_004b0aaa` (11 B) — empty body in retail.  Nothing to port.
- `FUN_00498458` (132 B) — menu refresh; blocked on menu subsystem.
- `FUN_00481b9a` (1399 B) — castle grid iterator; blocked on castle
  table + 0x50×0x50 collision grid.  Largest pre-tick subfunction.
- `FUN_00482111` (476 B) — army/unit grid iterator (sibling of
  481b9a); blocked on the unit subsystem.
- `FUN_004782c5` (340 B) — load-screen timeout; blocked on the
  load-screen state machine.

Combined observable delta from skipping all five: zero — each is
gated on a screen ID / sub-state that the drop-in cannot produce.

### Verified

- `make -C tests run` — 83/83 pass under ASan + UBSan (was 68/68;
  +15 delta + anim-cycle tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice.
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### What's next

Pre-tick block is structurally complete (three ported, five
deferred with documented blockers).  The next natural slice should
exit the pre-tick block and move toward either:

1. **Sub-step body** — retail's per-sim-tick block: `DAT_0057c998`
   bump + mod-10/0x82/1000 scheduled events + `FUN_004472ce` (sim
   tick) + `FUN_00404a46` (SP-only step) + state-machine breaks for
   save/load.  Today produces 0 sub-steps so the body never runs;
   structural seam exists.  Substantive work blocked on the sim
   subsystem (`FUN_004472ce` itself is a major chunk).
2. **Render phase head** — `FUN_0049a3e6` / `FUN_004b1de0` /
   `FUN_004b191e` run before the screen-dispatch.  Likely tiny like
   the pre-tick block; survey first.
3. **Render phase tail** — `FUN_00429b1d` / `FUN_0047ed34` /
   `FUN_00426529` / `FUN_004b27bf` (FPS counter) / `FUN_004b12fb` /
   `FUN_004b0e03` + the WM_CLOSE deferred-shutdown trigger.  Gated
   on `g_savepump_request == 0` which is always true today.  FPS
   counter alone might be a useful smoke artifact even with no
   render subsystem to feed it.

Top-down RE bias says option 2 (survey-then-port the render-phase
head) — moves the entry-chain port forward by the smallest verifiable
step.  Sub-step body and the FPS counter are both productive but
further down the dependency graph.

---

## 2026-05-24 — Animation-phase tick generator (FUN_004bbc80)

Ported the 9-flag animation-cadence generator that retail invokes
in the pre-tick block.  Wires into `engine_tick`'s newly-created
pre-tick slot.  Same shape as the rate-controller slice that
landed just before: pure function in its own TU + Win32 wrapper in
engine.c + file-static globals + unit tests.

### Ports

- **`src/engine_phase.c`** (new TU, pure C, no Win32 deps) —
  `engine_compute_phase_ticks(in, out)` is the port of retail
  `FUN_004bbc80 @ 0x4bbc80` (465 B).  Maintains 9 "did event X
  fire" flags driven by `timeGetTime`: a base 20 ms tick + 8
  mod-N rollover flags (80 ms / 160 / 320 / 640 / 1040 / 1280 /
  1920 / 2560).
- **`src/engine.c`** — file-static mirrors for the 9 retail
  counters + 9 flags (`g_phase_last_base_tick_ms`,
  `g_phase_base_tick_counter`, `g_phase_counter_{2,4,8,13,16,24,32}`,
  `g_phase_tick_{20ms,80ms,160ms,320ms,640ms,1040ms_sticky,1280ms,1920ms,2560ms}`).
- **`engine_tick_phase_step`** (file-static, Win32 wrapper) —
  reads `timeGetTime`, bundles into input struct, calls pure
  function, writes deltas back.  Implements the sticky-vs-fresh
  semantics for the 1040 ms flag (OR-into-live, no clear)
  separately from the other 8 (clear-then-overwrite, matching
  retail's clear-at-entry pattern).
- **`engine_tick`** — pre-tick slot now invokes
  `engine_tick_phase_step` before `engine_tick_rate_step`.

### Tests landed (12, all under ASan + UBSan)

- No time passed → no flags fire.
- 19 ms (below threshold) → no flags fire.
- 20 ms (at threshold, `0x13 < 20`) → base fires, counter bumps.
- Negative delta (clock wrap / debug-seeded future) → base fires.
- Base counter 3 → 4 → 80 ms fires + resets.
- Base counter 2 → 3 → 80 ms does NOT fire (`3 < 3` false).
- Stale counter case: 20 ms didn't fire but base counter > 3 from
  a previous frame → 80 ms still fires (catches stuck counters
  from unconsumed state).
- counter_2 rollover: 0→1 no fire, 1→2 fires + resets.
- All long cycles at max — every counter at its threshold fires
  in one call.
- 1040 ms off-by-one: counter 0xb → 0xc no fire, 0xc → 0xd fires.
- No-fire-no-state-change: 10 ms delta + counter_4=2 → counters
  carry through unchanged.
- Default clears all flags — pure function overwrites any garbage
  the caller hands in for the flag outputs.

### Verified

- `make -C tests run` — 68/68 pass under ASan + UBSan (was 56/56;
  +12 phase tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice (phase ticks fire but nothing consumes them yet).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### What's next

- **Remaining pre-tick subfunctions** — `FUN_004b0aaa` (input
  poll), `FUN_00498458`, `FUN_004b20ed`, `FUN_00481b9a`,
  `FUN_00482111`, `FUN_004782c5`, `FUN_00451f1a`.  Each needs a
  brief survey before commit-to-port; some likely depend on
  subsystems that aren't there yet (input subsystem, sprite
  manager, etc.).
- **First sprite-blit primitive** — still the most user-visible
  outstanding piece.

---

## 2026-05-24 — Sim sub-step rate controller (FUN_004bb978)

Ported the variable-rate sim sub-step gate as a pure function +
Win32 wrapper, plus 24 unit tests covering every branch.  Closes
the second item from the previous entry's "What's next" (the
sub-step gate variant), and reshapes `engine_tick` into retail's
true skeleton: pre-tick block → variable-rate sub-step loop →
render phase tail.  Today the loop runs 0 sub-steps every iteration
(scenario hint == 0 because engine state isn't ingame yet), so
runtime behaviour is unchanged — but the structural seam is in
place, and each subsystem port will now slot directly into the
right phase.

### New + restructured

- **`src/engine_rate.c`** (new TU, pure C, no Win32 deps) —
  `engine_compute_sim_sub_steps(in, out)` is the port of retail
  `FUN_004bb978 @ 0x4bb978`.  Takes a full input struct (now_ms,
  last_step_ms, autosave window, run gate, scenario hint, MP flag,
  master sim tick, game speed, pause lock, per-screen freeze,
  extended pause, MP skip flag, phase counter, LUT pointer) and
  returns a deltas-only output struct (sub_steps + writebacks +
  notification flag).  Pure means the test harness compiles it
  under host gcc + ASan/UBSan without any stubs.
- **`src/engine.c`** — added file-static mirrors for the 12 retail
  globals the controller reads/writes (`g_sim_last_step_ms`,
  `g_sim_autosave_start_ms`, `g_sim_tick_counter`,
  `g_sim_mp_active`, `g_sim_game_speed`, `g_sim_pause_lock`,
  `g_sim_current_screen`, `g_sim_screen_freeze_table[256]`,
  `g_sim_extended_pause`, `g_sim_mp_skip_table[256]`,
  `g_sim_mp_player_index`, `g_sim_phase`, `g_sim_derived_hint`).
  All file-static — no exposure in `engine.h` until something
  outside engine.c needs to read them.
- **`engine_run_gate`** (file-static, port of retail
  `FUN_004bb8c0`) — returns 1.  That's what retail does too;
  literally `return 1` in the decomp.  Probably a debug-build
  toggle optimised out at ship.
- **`engine_scenario_rate_hint`** (file-static, port of retail
  `FUN_0043de70` outer arm only) — returns 0 unless
  `g_engine_state == 3` (ingame).  Today we can't reach ingame, so
  always 0.  Full 1211-byte body deferred until player-faction
  data + current-player mirror land.
- **`engine_tick_rate_step`** (file-static, Win32 wrapper) —
  reads `timeGetTime()`, bundles into the input struct, calls
  the pure function, writes deltas back.  Returns sub-step count.
- **`engine_tick`** restructured — now: bump frame counter →
  `[TODO: pre-tick block]` → `for (i in 0..sub_steps) { [TODO:
  sub-step body] }` → `[TODO: render phase]` → `gfx_tick_present(0)`.
  Today the loop never iterates (sub_steps always 0) so observable
  behaviour is identical, but the loop is in place for when sub-step
  body slices land.

### Tests landed (24, all under ASan + UBSan)

- gate=0 → 0 sub-steps, no state change.
- Autosave window active (< 5000 ms since start) → 0, no change.
- Autosave window boundary cases: 4999 ms blocks, 5000 ms expires.
- Autosave expiry in MP fires the notification flag.
- Scenario hint == 0 → 0 sub-steps.
- Fast-skip warm-up (negative hint, sim_tick < 100) → 1 sub-step.
- Fast-skip cruise (negative hint, sim_tick ≥ 100) → 2 sub-steps.
- Hint > 0x8c → clamped to 0x8c (also verifies the LUT OOB —
  the lookup index for hint=0x8c reaches 0x8c1, which is 480 B
  past the documented LUT end).
- LUT byte == 0 → 0 sub-steps.
- Pause lock → 0 sub-steps; phase + derived_hint still mutated.
- Screen freeze 2 blocks SP, passes MP.
- Screen freeze 1 (or any non-2 non-0) blocks both.
- Extended pause blocks SP; in MP, blocks if mp_skip_flag set,
  passes otherwise (MP at max speed → 1 sub-step, no advance).
- Slider max-slow (game_speed=0 → iVar3=10) blocks.
- Slider rate-gate boundary: 51 ms < threshold 52 ms → 0, 52 ms
  >= 52 → 1 sub-step + last_step_ms advances to now.
- MP at max speed (iVar3=0, mp_active=1) → 1 sub-step, no
  advance (uncapped).
- Phase counter wrap: 0x9f → 0 (next call), 0x9e → 0x9f.
- Time overflow sentinel: now < last_step → local_8 clamped to
  10000 → step accepted at any threshold.

### LUT OOB engine quirk

[`docs/findings/engine-quirks.md`](findings/engine-quirks.md) entry
#3 covers the LUT (`DAT_004e3260`) read past its documented
0x6e0-byte end — by up to 0x1ff bytes when scenario hint
reaches the `0x8c` clamp.  Same shape as the .ini parser's OOB: it
works in retail because adjacent .data is benign.  Our port takes
the LUT as a pointer; tests allocate a fresh buffer large enough
to cover the actual indices reached.  The drop-in passes `NULL`
(no LUT) since today the function returns 0 before reaching the
LUT branch.

### Verified

- `make -C tests run` — 56/56 pass under ASan + UBSan (was 32/32;
  +24 engine-rate tests).
- `make -C src` — both .exe targets clean, no warnings.
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  pre-slice (no scenario hint produced → 0 sub-steps every iter →
  the new loop is a no-op).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — full DDraw
  bring-up + present loop transcript identical to pre-slice.

### What's next

The pre-tick / sub-step-body / render-phase slots in `engine_tick`
are all open.  Per the top-down-RE memory, the right next slice is
the smallest one that unblocks the entry chain:

1. **Pre-tick block subfunctions** — `FUN_004b0aaa` (input poll),
   `FUN_00498458`, `FUN_004b20ed`, `FUN_004bbc80`, `FUN_00481b9a`,
   `FUN_00482111`, `FUN_004782c5`, `FUN_00451f1a`.  Most are
   unknown body — needs survey before commit-to-port.
2. **`FUN_004bbc80`** (465 B) — already decoded inline in
   `main-loop-tick.md`: it's a wall-clock-driven phase counter
   bank (mod-2/4/8/12/16/24 schedulers).  Pure timing, no
   subsystem deps — would slot in clean.  Probably the right next
   pre-tick port.
3. **LUT extraction** — pull `DAT_004e3260` bytes from retail's
   `.data` once a scenario consumer makes it worthwhile.  Today
   no consumer.
4. **First real drawing primitive** — sprite-blit helper.  Still
   the most user-visible next step independent of the per-tick
   chain.

---

## 2026-05-24 — Engine tick guard + dispatcher (FUN_0040e8bb + FUN_0040e890)

Ported the two small wrappers that bracket retail's per-tick driver
into a new `src/engine.{c,h}` module, plus a scaffold of
`FUN_004b99c0` itself.  Closes "main-loop driver" from the previous
entry's What's-next, but in slice form — the scaffold lets every
future subsystem port slot into `engine_tick()` rather than into
the bare main loop body, matching retail's shape.

### Ports

- **`src/engine.{c,h}`** — three functions + three engine-state
  mirrors.
  - `engine_tick_guard()` (port of `FUN_0040e8bb`) — surface-lost
    Restore-before-dispatch, deferred-shutdown 3→1 escalator,
    savepump dispatcher (SP arm only — MP arm waits on the
    `DAT_00553030` mirror), gate on `g_shutdown_request == 1`.
  - `engine_tick_dispatch()` (port of `FUN_0040e890`) — calls
    `engine_tick`, latches `g_savepump_request = 1` on
    `g_engine_state > 8`.
  - `engine_tick()` (scaffold of `FUN_004b99c0`) — bumps
    `g_engine_frame_counter` (retail `_DAT_005bb4c8`) then calls
    `gfx_tick_present(0)` (retail `FUN_00452160(0)`).  Every other
    call inside retail's 2222-byte body is deferred — the pre-tick
    block, the sim sub-step loop, and the ~30 render-phase
    subsystem dispatches each land in their own slice.
  - Mirrors: `g_engine_state` (DAT_0052af94), `g_shutdown_request`
    (DAT_005aeb68 — moved out of `main.c` since `engine_tick_guard`
    now owns its escalator), `g_savepump_request` (DAT_005c9274).
- **`src/main.c`** — `main_loop_body` now routes through
  `engine_tick_guard()` instead of calling `gfx_tick_present`
  directly.  The capture-every-N-frames sidecar moves with it.

### Structural deviations from retail

- Surface-lost detection is a *sticky latch* in our gfx layer
  (`gfx_surface_lost()`) rather than retail's per-iteration
  `IDirectDrawSurface::IsLost` poll on both surfaces.  The present
  path is the only producer of fresh IsLost results in retail too,
  so the latch-style is equivalent for our purposes.  Comment in
  `engine.c` documents the equivalence.
- `FUN_004b26b8`'s `"OK :Paused."` log + InvalidateRects after a
  successful Restore-during-guard is skipped for now.  No Smacker
  movie in flight that would care; the InvalidateRect would just
  re-fire our no-op WM_PAINT.  TODO flagged in the guard.
- WndProc's `WM_CLOSE` / `VK_ESCAPE` handlers still call
  `DestroyWindow` inline.  The structural seam to remove that is
  now in place (the guard's `shutdown_request == 1` early-return),
  but the actual removal waits on a sim that can ack the deferred
  shutdown — until then there's no producer of the "drain one
  frame" half of the dance, so dropping the inline would just
  block shutdown.

### Verified

- `make -C src` — both .exe targets build clean, no warnings.
- `make -C tests run` — 32/32 tests pass (no change; engine.c is
  Win32-coupled and not included in the test build).
- `tools/run-openlords2.sh --frames 50` — transcript identical to
  the pre-slice run (engine-state machine sees no transitions in a
  bootless smoke).
- `tools/run-openlords2.sh --frames 50 --gfx-init` — DDraw bring-up
  + present-loop transcript identical to the pre-slice version.
  `engine_tick_guard` is fully exercised (the gfx-init arm is the
  one that actually runs the dispatch).

### What's next

Same shortlist mostly survives, now with one of the items closed:

1. **First real drawing primitive** — sprite-blit helper.  Still
   the most user-visible next step.
2. **`FUN_004b99c0` body — first slice of subsystems.** The
   scaffold is up; the natural next layer inside it is either the
   pre-tick block (small, mostly state-machine reads:
   `FUN_004b0aaa` input poll, `FUN_00499a1c` music update,
   `FUN_004b20ed` ?, the wall-clock increment) or the sim
   sub-step gate (`FUN_004bb978` rate controller — pure decision
   function, would slot into the test harness).
3. **`FUN_004ae1b8`** (lords2.inf 1128-byte persistence load) —
   still deferred until at least one consumer chain decoder lands.
4. **Sim ack of the deferred shutdown** — small follow-on slice
   that removes the WM_CLOSE / VK_ESCAPE inline `DestroyWindow`
   workarounds in WndProc once there's a sim that can roll the
   shutdown_request 3→1 transition into a clean main-loop exit.

---

## 2026-05-24 — Unit test harness + caught real OOB

Stood up a host-gcc unit test harness under ASan + UBSan,
modelled on `/opt/src/openrecet/tests/`.  First-run found a real
out-of-bounds read in the just-landed config parser — exactly the
kind of bug the smoke tests miss (back-buffer captures look fine,
nothing crashes on Windows because the production buffer is
20000 zero-padded bytes that absorbs the OOB).

### Scope landed

- **`tests/Makefile`** — host gcc, `-fsanitize=address,undefined
  -fno-sanitize-recover=all`.  Builds a single
  `tests/build/run_tests` from the pure-C source under test
  (`src/strutil.c`, `src/config.c`) plus the test files.
- **`tests/t.h`** — tiny assertion macros (`T_FAIL`, `T_ASSERT`,
  `T_ASSERT_EQ_I`, `T_ASSERT_EQ_S`, `T_ASSERT_MEM_EQ`, `T_SKIP`).
  Mirrors openrecet's `t.h` verbatim — same return-code protocol
  (0 pass, 1 fail, 2 skip).
- **`tests/test_main.c`** — driver with X-macro test registry
  (each test name appears exactly once; registry can't drift
  from declarations).  Stubs `log_line` and `io_read_file_at` so
  src/config.c links without dragging in the Win32 surface.
- **`tests/test_strutil.c`** — 15 tests covering every quirk
  claimed in `strutil.h`: substring search's "0 = not-found OR
  found-at-0" ambiguity, char-search's 1-based return, bounded-
  copy's no-null-terminator guarantee, strip_ctrl's `[0x20,
  0xa6]` range edges.
- **`tests/test_config.c`** — 17 tests covering defaults match,
  per-key parse (CDRomDrives / CDROM / SoundChannels / PhysicalMem
  / VideoSpeed / CPU), all four CPU class branches (`P`, `p`,
  `>`, otherwise), the fast_machine threshold's `>` vs `>=`
  boundaries, missing-keys behaviour, and a real-shape sierra.ini
  round-trip.
- **README "Tests" section** — points new contributors at the
  harness.

### Bug caught

`config_parse_sierra_ini` was reading past the input buffer in two
places:

1. The `find_char` call in `seek_past_equals` used the full `max`
   as bound instead of `max - off` (where `buf + off` is the
   search start).  Retail does the same — out-of-bounds-past-key
   read of up to 20000 bytes — but harmless because retail's
   buffer is a 20000-byte malloc zero-padded after the file
   content.  Our pure-C parser receives buffers of arbitrary
   size, so clamping was needed.

2. More serious — `FUN_00401000`'s "0 = not-found OR found-at-0"
   quirk meant that when a key was absent but the buffer
   contained any other `=` (very common: WindowsDir=., CDROM=,
   etc.), the parser would set `off` to whatever value happened
   to follow the first `=`.  For the CPU= line, this then ran a
   20-byte dash-scan past random offsets, tripping ASan on test
   inputs and (in retail) silently overwriting fields with
   garbage.

Both fixed in `src/config.c` — `seek_past_equals` now
disambiguates the 0-quirk via `memcmp(buf, key, klen)` and clamps
the `find_char` bound to remaining bytes.  Comments flag both as
deliberate retail deviations.  Production smoke unchanged
(transcript identical pre/post fix on a real game-dir run).

### Refactor

To make the parser testable in isolation, two pure-C symbols are
now exported from `src/config.h`:

- `config_set_defaults(config_t *out)` — applies retail's
  defaults block.
- `config_parse_sierra_ini(const char *buf, int n, config_t *out)`
  — pure parser, no globals, no I/O.

`config_init()` is now `config_set_defaults(&g_config) +
load_l2_eng() + try_l2ini_to_sierra_ini() +
config_parse_sierra_ini(g_ini_scratch, ..., &g_config)` — same
behaviour as before, just exposing the seams.

### Verified

- `make -C tests run` — 32 tests, all green, no ASan/UBSan trips.
- `make -C src` — both .exe targets build clean.
- `tools/run-openlords2.sh --frames 50` — transcript identical
  to pre-test-harness run.

### What's next

Same shortlist as the previous entry — first real drawing
primitive, main-loop driver port, lords2.inf consumer chain.
Test harness now ready to receive coverage as each pure-C parser
or decoder lands.

---

## 2026-05-24 — WinMain pre-init slice (config + strutil)

Ported the WinMain pre-init chain: l2.eng localization load,
l2.ini → sierra.ini parse, and the small string-helper quartet
they depend on.  Closes item 3 from the previous progress entry's
"What's next".

### New findings doc

[`docs/findings/winmain-pre-init.md`](findings/winmain-pre-init.md)
maps `FUN_0040e9ab`'s pre-init calls in order, decodes
`FUN_0040ecc4`'s defaults-first then file-overrides pattern with
all six sierra.ini keys + the derived "fast machine" flag, and
catalogues the string-helper quartet (`FUN_00401000` substring
search, `FUN_00401085` char search, `FUN_00401136` bounded copy,
`FUN_004012a2` strip-ctrl).

### Ports

- **`src/strutil.{c,h}`** — the four engine string helpers, ported
  verbatim including their quirks: `strutil_find_substr` is 0-based
  with the "0 = not-found OR found-at-0" ambiguity (preserved —
  real .ini callsites never trigger it), `strutil_find_char` is
  1-based, `strutil_bounded_copy` doesn't null-terminate,
  `strutil_strip_ctrl` null-terminates at the first byte outside
  `[0x20, 0xa6]`.  Each function ≤ a dozen lines.
- **`src/config.{c,h}`** — `config_init()` walks the same lookup
  chain as retail: defaults → l2.ini (for WindowsDir) →
  `<WindowsDir>\sierra.ini` → fallback `sierra.ini` from CWD.  The
  asymmetric retail control flow (WindowsDir-missing skips the
  fallback that l2.ini-missing reaches via fall-through) is
  preserved with a comment flagging it as retail-as-shipped.  The
  six sierra.ini keys cook into a `config_t g_config` struct +
  the derived `fast_machine` flag.  l2.eng is loaded into a static
  120-KB buffer; future UI text consumers read it via
  `config_localization_data`.
- **`src/main.c`** — calls `config_init()` after `init_exe_dir`
  but before window creation, matching retail's
  `__getcwd → l2.eng → FUN_004ae1b8 → FUN_0040ecc4 → FUN_004b2258`
  ordering.

### Deferred

- **`FUN_004ae1b8`** (lords2.inf 1128-byte persistence load) — the
  load itself is trivial but the consumer chain (`FUN_004ae2bd`,
  `FUN_004ae2ab`, `FUN_004ae310`, `FUN_004aecf0`) hasn't been
  decoded.  Loading the bytes without consumers would be
  write-only state, so this lands when at least one consumer
  does.

### Verified

- Baseline smoke (`--frames 50`): clean.  Transcript shows
  `config: loaded l2.eng (100016 bytes)` and
  `Err:Could not read sierra.ini` (game-dir has no
  `sierra.ini`; L2.INI's `WindowsDir=.` triggers the `.\sierra.ini`
  attempt which also misses; defaults take effect).  Retail
  behavior matches: GOG installer doesn't lay down sierra.ini, the
  defaults are the load-bearing values.
- No regression in `--gfx-init` or smoke transcripts beyond the
  new pre-init log lines.

### What's next

1. **First real drawing primitive** — sprite-blit helper.  Visual
   diff against Frida-captured retail would close the loop on
   "what does the engine actually paint".
2. **Main-loop driver** — `FUN_004b99c0` (~2200 B).  Bottoms out
   in ~40 subfunctions across most subsystems; too large for one
   session.  Would need a multi-slice plan.
3. **`FUN_004ae1b8` (lords2.inf load)** — once a consumer chain
   decoder lands.

---

## 2026-05-24 — Render rate-limiter + dirty-rect tracker

Ported `FUN_00452160` (rate-gated present), `FUN_00452306`
(`dirty_rect_union`), and `FUN_0045240a` (`mark_all_dirty`) into
`src/gfx.c`.  Closes the punchlist item that's been carried since
the `gfx_present` slice landed ("we present every frame
unconditionally — needs the 50-tick gate").

New findings doc:
[`docs/findings/render-rate-limiter.md`](findings/render-rate-limiter.md)
covers the three functions + their shared global state in full
(accumulator, sticky force latch, dirty count, dirty bounds, Smacker
bypass, alternate-composition suppression gate + forced-redraw
insurance).  Per-function summary in the doc; the gist:

- The accumulator (`DAT_004eabe8`) is fed by `timeGetTime` deltas
  via the same source that ticks the FPS counter.  Threshold = 50.
- `force=1` and the sticky `DAT_005651ac` latch pin the accumulator
  past the threshold — used by screen-state transitions.
- Dirty-count gating (`DAT_0050d09c`) means presents that have no
  visual work are no-ops.
- Dirty-rect bounds (`DAT_0050a4bc/c0/c4/c8`) are accumulated for
  incremental-redraw consumers but the rate-limited present itself
  doesn't use them — it always blits the full back via
  `DAT_004ea180` (the visible client rect).  Bookkeeping is
  currently write-only in our drop-in.
- Smacker bypass and the cutscene-suppression gate are dead in
  steady state (both flags zero); ported as stubs/comments for
  when those subsystems land.

### Port

- **`gfx_tick_present(int force)`** in `src/gfx.c` — the rate-gated
  present.  Replaces the prior unconditional `gfx_present` call.
  `gfx_present` itself stays as the blit primitive (used by
  `gfx_tick_present` internally + available for one-shot bring-up
  tests).
- **`gfx_dirty_rect_union(int l, int t, int r, int b)`** — reproduces
  retail's `FUN_00452306` arithmetic verbatim, including the retail
  typo at the max_y clamp (compares against `width-1` but clamps to
  `height-1`; harmless because inputs are pre-bounded).
- **`gfx_mark_all_dirty()`** — `dirty_rect_union(0, 0, 639, 479)`.
- **`gfx_force_present_next()`** — sets the sticky latch, mirror of
  `DAT_005651ac = 1` callsites in retail.
- `gfx_init` ends with `gfx_mark_all_dirty()` (matches retail's
  bring-up tail).  `gfx_fill_back_index` also marks dirty after the
  fill so the next present is guaranteed to flush.
- `main.c` wires `gfx_tick_present(1)` once immediately after a
  successful `gfx_init` so frame-0 capture / show-window has visible
  content, then `gfx_tick_present(0)` per main-loop iteration.

### Verified

- Baseline smoke (`--frames 50`): clean.
- `--gfx-init` smoke (`--frames 50`): clean — full bring-up + clean
  shutdown.
- `--gfx-init --capture-to /tmp/openlords2-rate --capture-every 20`
  over `--frames 200`: 10 back-buffer captures each show
  `(128,0,0)` at centre (palette index 1 from `base01.256`).  Window
  captures show `(0,0,0)` because the run is hidden — DWM has
  nothing to composite when the window isn't shown.  Manual
  `--show-window` validation deferred to the user (same constraint
  as every other visual-correctness slice).

### What's next

Same as before, minus the rate-limiter (now done):

1. **First real drawing primitive** — sprite-blit helper so we can
   capture a recognisable image and diff against retail.  Next
   natural step now that the present cadence matches retail.
2. **Main-loop driver** — `FUN_004b99c0` (~2200 B).  Largest single
   port remaining.
3. **WinMain pre-init slice** — `l2.eng` localization load + l2.ini
   parse (`FUN_0040ecc4`) + Sierra config (`FUN_004ae1b8`).

---

## 2026-05-24 — Surface-restore wire-up

Ported retail's `FUN_004b2730 @ 0x4b2730` as `gfx_restore()`.  Closes
the WndProc TODO that's been carried since the WndProc port landed.

### Scope landed

- **`gfx_restore()`** in `src/gfx.c` — `IDirectDrawSurface::Restore`
  on primary then back.  Returns 1 on success, 0 on failure.  Logs
  retail's exact strings (`ERR:Can't restore front buffer.` /
  `ERR:Can't restore back buffer.`) at `0x4df668` / `0x4df68c`.
- **`gfx_surface_lost()`** accessor + internal latch.  Set in
  `gfx_present` whenever `BltFast` (fullscreen) or `Lock` (windowed)
  returns `DDERR_SURFACELOST`.  Cleared by a successful
  `gfx_restore` or a fresh `gfx_init`.
- **WndProc `WM_ACTIVATEAPP` escalator** in `src/main.c` — on focus
  regain with the latch set, call `gfx_restore()`.  On failure,
  escalate: `gfx_shutdown()` + `gfx_init()` (port of retail's outer
  `FUN_004b2922` wrapper around `FUN_004b2730`).  On either success
  path, clear `g_surface_lost` and mark `g_render_dirty = 2` so the
  next present repaints fully.

### Verified

Baseline + `--gfx-init` smoke runs both clean — no regressions.
The restore path itself isn't exercised by a smoke run (no real
focus-loss event under headless), so this slice is "wired but not
end-to-end tested".  Manual validation candidate: run with
`--show-window`, switch to another fullscreen app, switch back —
should see `OK :Active.` followed by either a clean restore or a
re-init sequence in the transcript.

### What's next

Same as before, minus surface-restore (now done):

1. **Render rate-limiter + dirty-rect tracker** —
   `FUN_00452160` / `FUN_00452306`.  Mid-size port; touches several
   undecoded globals around the dirty-rect accumulator, so worth a
   dedicated findings doc first.
2. **First real drawing primitive** — sprite-blit helper so we can
   capture a recognisable image and diff against retail.
3. **Main-loop driver** — `FUN_004b99c0` (~2200 B).  Largest single
   port remaining; what consumes the `g_shutdown_request` escalator
   and the per-tick rate gate.
4. **WinMain pre-init slice** — `l2.eng` localization load + l2.ini
   parse (`FUN_0040ecc4`) + Sierra config (`FUN_004ae1b8`).  Lots of
   global init touching `g_cd_drive`, `g_sound_channels`,
   `g_video_speed`, etc.  Self-contained, but pure setup — only
   becomes load-bearing once the consumers (SFX, video, screen-state
   code) land.

---

## 2026-05-24 — DSound bring-up (`src/audio.c`)

Ported retail's `FUN_00425b10 @ 0x425b10` (DSound init) into a new
`src/audio.c`.  Continues the WinMain entry-chain: retail calls this
*after* `FUN_004b2258` (InitInstance) returns, so it slots in after
`gfx_init` in our `WinMain`.

### Scope landed

- `audio_init(HWND)` — `DirectSoundCreate(NULL, &lpDS, NULL)` +
  `SetCooperativeLevel(hwnd, DSSCL_PRIORITY)` + `CreateSoundBuffer`
  primary (`DSBCAPS_PRIMARYBUFFER`) + `SetFormat(&wfx)` with retail's
  `FUN_00425ded` template: **11025 Hz stereo 8-bit PCM**.  All four
  error paths log retail's exact strings (`ERR:DS failed open`,
  `ERR:DS failed set co-op`, `ERR:DS failed setup primary buffer`,
  `ERR:DS failed to change primary buffer format`) at byte addresses
  `0x4d4768..0x4d47bc`; success arm logs `OK :DS Installed.` from
  `0x4d47ec`.
- `audio_shutdown()` — releases primary buffer + IDirectSound in
  reverse order, logs `OK :DS Released.` from `0x4d4800`.
  Idempotent — safe to call on a failed/skipped init.
- Voice slot zeroing (`DAT_00522b00[20]`, `DAT_00522b80[2]`,
  `DAT_00522b50[2]`) is left as a TODO — those slots are only
  meaningful once the SFX/music playback path exists.

### Default-off gating

`audio_init` is gated behind `--audio-init` in main.c, mirroring the
`--gfx-init` convention.  `SetCooperativeLevel(PRIORITY)` modifies
process-global DSound state and the headless smoke harness has no
way to validate output audibly — so default-off keeps
`tools/run-openlords2.sh` runs harmless.

### Verification

- Baseline smoke (`--frames 50`) clean — no change in transcript
  (only `OK :Active.` / `OK :Not active.`).
- `--audio-init` smoke clean — transcript shows
  `OK :DS Installed.` after `OK :Active.` and `OK :DS Released.`
  during shutdown, exits rc=0.
- `--gfx-init --audio-init` combined smoke clean — full DDraw
  bring-up + palette load + DSound install + clean teardown in one
  run.

### Other changes

- `src/Makefile` links `-ldsound`.
- `audio.c` includes `mmsystem.h` before `dsound.h` to pull in
  `WAVEFORMATEX` (mingw's `dsound.h` doesn't include it itself).

### What's next

The entry chain past WinMain is now ported end-to-end: window class
+ creation (`FUN_004b2258`) → DDraw bring-up (`FUN_004b2922` chain)
→ DSound bring-up (`FUN_00425b10`).  Remaining bring-up gaps:

1. **Render rate-limiter + dirty-rect tracker** — port
   `FUN_00452160` / `FUN_00452306` so we mirror retail's actual
   present cadence (currently we present every frame
   unconditionally).
2. **Surface-restore wire-up** — connect WndProc's
   `WM_ACTIVATEAPP` active-with-lost-surfaces branch to a real
   `IDirectDrawSurface::Restore` chain via gfx.c accessors.
3. **First real drawing primitive** — port one of the sprite-blit
   helpers so we can capture a recognisable image and diff against
   a Frida-captured retail screenshot.
4. **Main-loop driver** — `FUN_004b99c0` (sim+render driver, ~2200
   B).  This is what consumes `g_shutdown_request`'s 3→1→4
   escalator and what the per-tick `FUN_0040e8bb` / `FUN_004bb978`
   rate gate flows into.  Largest single port still ahead.

---

## 2026-05-24 — Windowed present via GDI (DWM workaround)

User flagged: visible window was black despite captures showing
red.  Confirmed via side-by-side back vs. GDI window capture
(both pipelines newly added in the same session):

```
frame_00100.bmp     (back via DDraw Lock):     mean=(128,0,0)
frame_00100-win.bmp (window via GDI BitBlt):   mean=(0,0,0)
```

The Blt was returning `DD_OK` but the visible surface never
updated.  Classic modern-Windows + DWM compositing gotcha:
DDraw NORMAL-mode Blts to primary go to the GPU's
front-buffer and bypass DWM's window backing stores, so
DWM re-composites the desktop from the unchanged backing
store and the user never sees the painted pixels.

### Fix

Routed the windowed-mode present through GDI's
`StretchDIBits` (Lock the back → build BITMAPINFO with the 256
palette entries inline → blit to `GetDC(hwnd)` → Unlock).  GDI
operations DO go through DWM's intercept layer, so the
window's backing store updates and the composited result is
visible.

Fullscreen path keeps `IDirectDrawSurface::BltFast` (exclusive
coop bypasses DWM entirely — no compositing mediation).

User confirms visual: window now shows solid red, matching
the captures.

### Why retail doesn't have this problem

`FUN_0042e5c0` auto-fullscreen-promotes any time the desktop
isn't 8bpp — i.e. always, on modern hardware.  Exclusive coop
level + SetDisplayMode puts retail's DDraw outside DWM
compositing, sidestepping the issue.  Our default-windowed
(`project_drop_in_defaults` memory) needs the GDI seat.

### Other changes

- `gfx_capture_window(path)` — GDI BitBlt of the window's
  client area to a 24bpp BMP.  Bypasses DDraw entirely, so it
  shows "what the user sees" rather than "what we drew".
  Smoke harness now emits both `frame_NNNNN.bmp` (back) and
  `frame_NNNNN-win.bmp` (visible) at every capture point.
- `WM_ERASEBKGND` returns 1 to suppress the class
  background-erase (BLACK_BRUSH would otherwise flash through
  during paint cycles).
- Memory recorded as `project_dwm_ddraw_normal.md` — this is
  the kind of gotcha that wastes an afternoon if you don't
  know it exists.

### What's next

Same as before: DSound bring-up, render rate-limiter, surface
restore, first real drawing primitive.  The visual-verify loop
is now closed end-to-end.

---

## 2026-05-24 — Back-buffer capture (autonomous visual diff)

User flagged that:

1. The smoke harness's CWD anchor was wrong — `init_exe_dir` was
   pinning CWD to the /tmp copy of the exe, so `base01.256`
   lookup failed.
2. We needed a screenshot mechanism so visual verification could
   happen without a human in the loop (parity with openrecet).

### Fixes landed

- **`init_exe_dir` now prefers `$OPENLORDS2_GAME_DIR`** (matches
  OpenMare's `init_game_dir` shape).  The flake's shellHook
  already propagates this across the WSL→Windows boundary as a
  native Windows path via `WSLENV/p`, so the .exe receives
  `C:\GOG Games\Lords of the Realm II` directly.  Falls back to
  the exe's parent dir when the env var is unset (production
  drop-in scenario).
- **Back buffer is now explicitly 8bpp.**  Without
  `DDPF_PALETTEINDEXED8` in the `CreateSurface` descriptor,
  DDraw inherits the primary's format — which on a 32bpp host
  is 32bpp.  An 8-bit palette can't attach to a 32bpp surface,
  so `SetPalette` returned `DDERR_INVALIDPIXELFORMAT`
  (`0x88760091`).  Always-explicit 8bpp on the back fixes this.
  The palette now attaches to the **back** in windowed mode
  (where the primary is 32bpp) and to the **primary** in
  fullscreen (where SetDisplayMode already made it 8bpp).
- **`gfx_capture_backbuffer(path)`** in `src/gfx.c` —
  `Lock(READONLY)` + per-pixel palette lookup via
  `g_palette_rgb` + top-down 24bpp BMP write.  Same spirit as
  openrecet's `capture_backbuffer` (which used D3D8 LockRect +
  32bpp), but pre-resolves palette → BGR so the BMP needs no
  palette section and viewers Just Work.
- **`--capture-to DIR --capture-every N`** flags in main.c.
  Implies `--gfx-init`.  Auto-creates the directory.  Linux
  paths (`/tmp/foo`) auto-translate to WSL UNC
  (`\\wsl.localhost\<WSL_DISTRO_NAME>\tmp\foo`) for the
  Windows-side process — propagated via `WSLENV`.

### Verified

Captured frames at `--capture-every 10` against an
unfilled-except-for-index-1 back buffer.  Every BMP is a
uniform `#800000` — palette index 1 from `base01.256`,
×4-expanded from its 6-bit VGA source.  Confirms end-to-end:

1. CWD anchoring works (768 bytes read from `base01.256`).
2. Palette load works (8bpp palette attached to back).
3. Back-buffer fill works (every pixel = idx 1).
4. Capture pipeline works (BMP shows the right colour).

### Why this matters

Visual verification now runs without a human in the loop.
When the next slice adds a real drawing primitive (a single
rectangle, a sprite blit, etc.), the smoke harness produces
BMPs the assistant reads back and confirms.  Bug-find loop
goes from "build → ship to user → wait" to "build → capture →
read pixel → iterate" in one session.

Adapted directly from openrecet's screenshot setup (see
`/opt/src/openrecet/src/main.c` `capture_backbuffer` and the
companion `--capture-to` / `--capture-every-ms` flags).  Same
pattern, swapped D3D8 → DDraw 8bpp, time-based → frame-based.

### What's next

1. **DSound bring-up** (`FUN_00425b10`).
2. **Render rate-limiter + dirty-rect tracker**
   (`FUN_00452160`/`FUN_00452306`).
3. **Surface-restore wire-up**.
4. **First real drawing primitive** — once we port one of the
   sprite-blit helpers (TBD which) we can capture a recognisable
   image and diff against a Frida-captured retail screenshot of
   the same content.

---

## 2026-05-24 — gfx_present + back-buffer fill

Added the per-frame back→primary present primitive and a simple
back-buffer fill so the existing palette load becomes visually
verifiable on the next manual run.

### Scope landed

- **`gfx_present()`** in `src/gfx.c` — port of the inner Blt /
  BltFast from retail's `FUN_00452160 @ 0x452160` (the
  "present" function).  Windowed: `primary->Blt(client_rect,
  back, NULL, DDBLT_WAIT, NULL)`.  Fullscreen:
  `primary->BltFast(0, 0, back, (0,0,640,480), DDBLTFAST_WAIT)`.
  Client rect is re-queried each call via
  `GetClientRect`+`ClientToScreen` so WM_MOVE/WM_SIZE don't need
  to push state into gfx.c yet (cheap query, no per-frame
  syscall hot path concern at this stage).
- **`gfx_fill_back_index(uint8_t)`** — 8-bit COLORFILL of the
  back buffer with a palette index.  For 8bpp DDraw surfaces,
  `DDBLTFX.dwFillColor` IS the palette index in the low byte —
  no RGB lookup needed.
- `gfx_init` primes the back buffer with index 1 at startup, so
  the next frame's present makes the visible window
  `base01.256[1]`'s colour (and stays black if no palette
  loaded — entry 0 is forced black).
- Main loop calls `gfx_present()` once per
  iteration when `--gfx-init` is active.  No rate limiter yet —
  retail's `FUN_00452160` gates on a 50-tick counter
  (`DAT_004eabe8`) and a dirty-rect tracker
  (`DAT_0050d09c`/`DAT_0050a4bc..c8`); both are deferred until
  the render driver lands.

### Verification

- Baseline smoke clean (50 frames).
- `--gfx-init` smoke clean (50 frames, present runs each frame
  against the hidden window, no errors).
- **Manual visual verification needed** on the next
  game-dir run: `--gfx-init --show-window` should display a
  solid colour (palette index 1 from `base01.256`) instead of
  black for the duration of the run.  If the window stays
  black, either the palette didn't load or the present isn't
  reaching the visible surface.

### What's next

Same as before plus:

1. **DSound bring-up** — port `FUN_00425b10` once human
   confirms gfx_present is visible.
2. **Render rate-limiter + dirty-rect tracker** — port
   `FUN_00452306` (dirty-rect accumulator) + the gating logic
   in `FUN_00452160` so we mirror retail's actual present
   cadence.  Right now we present every frame unconditionally.
3. **Surface-restore wire-up** still pending.

---

## 2026-05-24 — DDraw palette load (+ `src/io.c`)

DDraw bring-up confirmed visually on Windows by the user (`--gfx-init
--show-window` shows a brief black window, exits clean — matches
PROGRESS expectation).  Continued the chain with the palette load.

### Scope landed

- **`src/io.c`** — minimal file-IO helper `io_read_file_at(path,
  buf, len, offset)` that wraps `CreateFileA`+`SetFilePointer`+
  `ReadFile`+`CloseHandle`.  First-tier port of retail's
  `FUN_004af8d9`; the three-tier retry chain
  (`FUN_004af312`/`FUN_004af116`) is a TODO — we lean on
  `init_exe_dir` for the CWD anchor.
- **`gfx_load_palette(path)` in `src/gfx.c`** — reads 768 bytes
  (256 RGB triplets) from the given path, ×4-expands 6-bit VGA →
  8-bit RGB, builds a 256-entry `PALETTEENTRY` array with
  `PC_RESERVED` on entries 1..254 (entry 0 forced black, entry
  255 forced white per retail's boundary contract), calls
  `IDirectDraw::CreatePalette` + `IDirectDrawSurface::
  SetPalette` on the primary.  Releases any prior palette
  first so re-loads are clean.
- `gfx_init` now best-effort-loads `base01.256` after the
  initial Blt.  Failure is logged and skipped — primary still
  presents in the OS default palette.
- `gfx_shutdown` releases `g_palette` first in the cleanup
  chain.

### Simplifications from retail

- We don't preserve OS-reserved palette slots (0..9, 246..255)
  the way retail's `FUN_0042f449` does — that's only meaningful
  when running 8bpp on an 8bpp desktop, which today's hosts
  aren't.  In windowed mode on a 32bpp host DDraw handles the
  format conversion, so all 256 entries are ours.
- No `SetSystemPaletteUse` (windowed-8bpp-only concern).

### Verification

- **Baseline smoke** (no `--gfx-init`): 50 frames clean — only
  `OK :Active.` / `OK :Not active.` in the transcript.
- **`--gfx-init` from /tmp** (no `base01.256` reachable): full
  bring-up runs, palette load logs the graceful skip
  ("gfx_load_palette: read base01.256 got 0/768"), `OK :GFX
  DirectDraw enabled.` still fires, clean exit.
- **Success path live-verification** deferred — copying the exe
  to the game-dir via /mnt/c/ trips a Windows-side execute
  permission issue; needs human run on a native Windows shell
  to actually validate the colour load.  Code path is logically
  identical to the failure path with the success-arm taken;
  next manual run from the game dir is the ground-truth check.

### What's next

1. **Smoker bringup helper `FUN_00425b10`** — DSound init.  The
   harness already hooks `IDirectSound::SetCooperativeLevel`
   to demote PRIORITY→NORMAL; porting the bring-up gives us
   actual audio output paths.
2. **Surface-restore wire-up.**  Now that `gfx.c` owns
   primary/back/palette, expose accessors so WndProc's
   `WM_ACTIVATEAPP` active+lost arm can call
   `IDirectDrawSurface::Restore` and on failure
   `gfx_shutdown(); gfx_init()` — the `FUN_004b2922` outer
   call's equivalent.
3. **The flip helper `FUN_0045240a` → `FUN_00452306`.**  The
   per-frame present.  Once we know its exact `Blt` shape we
   can wire a "draw a recognisable pattern → present → confirm
   visually" sanity test in the smoke flow.

---

## 2026-05-24 — DDraw bring-up (`src/gfx.c`)

Ported the retail DDraw chain into a new `src/gfx.c`.

### Scope landed

- `gfx_init(HWND, fullscreen)` — `DirectDrawCreate` +
  `SetCooperativeLevel` (`NORMAL` for windowed, `EXCL|FULLSCREEN|
  ALLOWREBOOT` for fullscreen) + `SetDisplayMode(640,480,8)` for
  fullscreen only.
- Primary surface (`DDSCAPS_PRIMARYSURFACE`).
- Back buffer (system memory, `DDSCAPS_OFFSCREENPLAIN |
  DDSCAPS_SYSTEMMEMORY`, 640×480).
- Lock-probe on both surfaces (matches retail's
  `FUN_0042ec91` — caches `lPitch` for the render path's
  scanline-stride math).
- Clipper (windowed only — `IDirectDrawClipper` +
  `SetHWnd(hwnd)` + `IDirectDrawSurface::SetClipper`).
- Initial `Blt(COLORFILL=black, DDBLT_WAIT)`.
- `gfx_shutdown()` — releases everything in reverse order
  (clipper → back → primary → IDirectDraw).
- All status log lines mirror retail's exact wording —
  "OK :DD SetCooperative Level.", "OK :DD Kept resolution.",
  "OK :DD Set resolution.", "OK :DD Creating one Vram and one
  system surface.", "OK :DD Creating one Vram only.",
  "OK :DD Surfaces created and clipper attached.",
  "OK :DD Surface created.", "OK :GFX DirectDraw enabled." —
  so transcripts diff cleanly against a Frida-captured retail
  run.

### Scope deferred

- **Palette load.** Requires porting the file-read helper
  `FUN_004af8d9` (with its `FUN_004af116` /
  `FUN_004af312` retry layer for alternate game-dir search
  paths) first.  Without a palette the 8bpp primary is
  technically usable but renders against the OS's logical
  default palette — fine for bring-up smoke but not visually
  correct.
- **Smacker-aware paint.**  `FUN_0042e3ba` belongs in a
  Smacker-wrapper TU we don't have.
- **Surface-restore on focus regain.** WndProc's
  `WM_ACTIVATEAPP` path will call `IDirectDrawSurface::Restore`
  once `g_primary / g_back` are reachable from that side — the
  state mirror is already in place, just needs the helper.

### Default-off gating

`gfx_init` is gated behind `--gfx-init` in main.c.  `SetCooperative
Level` modifies process-global DDraw state and the headless smoke
harness has no way to validate the result visually — so default-off
keeps `tools/run-openlords2.sh` runs harmless and reserves the
DDraw path for an explicit `tools/run-openlords2.sh --gfx-init
--show-window`.

Baseline smoke (no flag) still passes — 50 frames clean, only
`OK :Active.` / `OK :Not active.` in the transcript.

With `--gfx-init` the full bring-up runs cleanly even against a
hidden window — six status lines + clean shutdown, no leaks
visible.  **Visual correctness needs a human run on a Windows
desktop** (per the validate-subjectively memory) — that's the
deferred item before this slice is "verified".

### Other changes

- `log_line` extern-visible (was static) so `gfx.c` can use the
  same logger.  No call-site change.
- `Makefile` links `-lddraw`.
- WndProc's `g_surface_lost` / `g_focused` / `g_render_dirty`
  mirrors from the previous slice are now reachable from
  `gfx.c`'s side — the surface-restore wire-up is one TODO away.

### What's next

1. **Palette load.**  Port `FUN_004af8d9` (open + lseek + read,
   with two retries on alternate paths) into a tiny `src/io.c`,
   then `FUN_004b0ab5` (×4 RGB expansion) and `FUN_0042f1df` →
   `FUN_0042f449` / `FUN_0042f306` (palette buffer construction
   + `IDirectDraw::CreatePalette` + `IDirectDrawSurface::
   SetPalette`).  At that point `--gfx-init` becomes visually
   correct enough to start diff-testing against retail.
2. **Visual verification on Windows.**  Manual `tools/
   run-openlords2.sh --gfx-init --show-window` from a Windows
   host — confirm the window appears, stays black for the run,
   exits clean.  Compare against a Frida-captured retail
   transcript of the same sequence.
3. **Surface-restore wire-up.** From WndProc's
   `WM_ACTIVATEAPP` active+lost path, call
   `IDirectDrawSurface::Restore(g_primary)` +
   `Restore(g_back)`; on failure, call `gfx_shutdown(); gfx_init()`
   (the equivalent of `FUN_004b2922`'s outer call).

---

## 2026-05-24 — WndProc port + findings

Continued top-down: ported retail's `FUN_004b29be @ 0x4b29be`
(WinLords2Class WndProc) into `src/main.c`, with a dedicated
findings doc.

### `docs/findings/wndproc.md`

Maps every message the retail WndProc handles to its retail
behaviour, call-chain, and the globals it reads/writes:

- WM_PAINT — Smacker-aware (delegates to `FUN_0042e3ba` if a
  movie is playing, else plain Begin/EndPaint + optional
  full-screen redraw when unfocused).
- WM_DESTROY — full subsystem teardown chain: close Smacker
  (`FUN_0042dfe2`), release palette (`FUN_004b28cd`), shut down
  DSound (`FUN_00425d35`), shut down DPlay (`FUN_004b7180`),
  then `PostQuitMessage(0)`.
- WM_MOVE / WM_SIZE — recompute the **screen-space client
  RECT** at `DAT_004ea180`+, log "OK :Window moved." / "OK
  :Minimizing." / "OK :Paused.", drive a redraw based on engine
  state.
- WM_CLOSE — sets `DAT_005aeb68 = 3` and returns.  The actual
  close is handled by the sim/render driver's escalator
  (`3 → 1 → 4`), so an external Alt+F4 cleanly drains the engine
  before the surfaces die.
- WM_ACTIVATEAPP — toggles the focus flag, logs "OK :Active." /
  "OK :Not active.", and on focus-regained-with-lost-surfaces
  runs `FUN_004b2730` (DDraw `Restore`) and on failure
  `FUN_004b2922` (full re-init).
- WM_DISPLAYCHANGE — caches new screen bpp + size.
- WM_KEYDOWN — a fat VK switch.  All 30-odd cases mapped to
  their `FUN_xxxxx` handlers (digits with Ctrl-modifier,
  F1/WinHelp, F2-cycle, F5/recentre, F7/F8 debug, ESC/menu, …).
- WM_CHAR / WM_SYSCHAR — text input + Alt-shortcut mappings
  (Alt-1, Alt-2..5, Alt-X = ESC).
- Mouse — coord cache at `DAT_004eabbe/004eabc0`, button bitmask
  `DAT_004eabc2`, dbl-click latch `DAT_004eada1`.
- WM_PALETTECHANGED / WM_QUERYNEWPALETTE — only act in
  windowed 8bpp; recreates the palette via `FUN_0042f1df`.

Also catalogued the 10 retail log strings the WndProc emits via
`FUN_004afab9` (`OK :Paused.`, `OK :Active.`, `ERR:Can't
restore front buffer.`, etc.) so the drop-in can mirror them
for differential transcripts later.

### Port landed in `src/main.c`

Structural messages fully ported — the drop-in now logs in
retail's exact wording ("OK :Active." / "OK :Not active." /
"OK :Window moved." / "OK :Minimizing." / "OK :Paused.")
through the same code paths.  Mouse-state mirror (`g_mouse_x`,
`g_mouse_y`, `g_mouse_buttons`, `g_mouse_dblclk`) and
focus/shutdown-request flags (`g_focused`, `g_surface_lost`,
`g_shutdown_request`) carry the same semantics as their
retail DAT_* counterparts — so when the sim/render driver
lands, the rename is global-replace.

Game-state hotkeys remain TODOs (each labelled with the retail
`FUN_xxxxx` it should call) — VK_ESCAPE/F1/F5/Ctrl are
honoured as drop-in conveniences.  WM_CLOSE both sets the
deferred-shutdown flag AND inlines `DestroyWindow` so we still
exit cleanly without the sim escalator that doesn't exist yet.

Smoke verified: 50 frames in <1 s under
`tools/run-openlords2.sh`, transcript shows
`OK :Active.` on window activation and `OK :Not active.` on
the synchronous deactivation during DestroyWindow.

### What's next

1. **DDraw bring-up** — port `FUN_004b284e` (DirectDrawCreate +
   SetCooperativeLevel + SetDisplayMode) and `FUN_0042ec91`
   (primary surface, back buffer, clipper, lock-probe) into a
   new `src/gfx.c`.  WndProc's surface-restore path can then
   call into real code.
2. **Turbo v2 in the Frida agent** — `timeGetTime`-only seat
   per the `main-loop-tick` finding.  Verify rise rate of
   `DAT_0057c998` against the scale.
3. Defer porting the per-screen hotkeys (F2/F3/F4/F6/F7/F8/F9
   etc.) until the screen-state machine + sim driver land —
   they all bottom out in game-state code that doesn't exist
   yet.

---

## 2026-05-24 — DDraw/DSound + main-loop findings + InitInstance port

Continued top-down chip-away from the entry point.  Three landings:

### Two new findings docs

- [`docs/findings/ddraw-dsound-bringup.md`](findings/ddraw-dsound-bringup.md)
  decodes the post-window init chain.  `FUN_004b2922` is just an
  orchestrator; the real work is `FUN_004b284e` →
  `FUN_0042e5c0` (`DirectDrawCreate` + `SetCooperativeLevel` +
  `SetDisplayMode`) and `FUN_0042ec91` (primary surface, back
  buffer, clipper, lock-probe).  `FUN_00425b10` is the DSound
  bring-up called separately from WinMain after `FUN_004b2258`
  returns: `DirectSoundCreate` + `SetCooperativeLevel(PRIORITY)`
  + `CreateSoundBuffer` primary + `SetFormat`.  All vtable
  offsets + log-prefix strings mapped, so the existing Frida
  hooks have a paper trail.

- [`docs/findings/main-loop-tick.md`](findings/main-loop-tick.md)
  corrects a misread from the first session:
  `FUN_004b8c3a` is the **DPlay receive pump**, not the
  per-tick driver.  The real sim+render path is `FUN_0040e8bb`
  (surface-lost guard) → `FUN_0040e890` → `FUN_004b99c0` (2222 B
  sim+render driver, with a variable sub-step loop fed by
  `FUN_004bb978`'s rate gate on `_DAT_0053f230` speed slider and
  `_DAT_004e5b0c` last-tick wall-clock).

  Key turbo-v2 insight: the rate gate depends on `timeGetTime` for
  sim+render+FPS+UI, but the DPlay pump uses `GetTickCount`.
  Virtualising **only** `timeGetTime` thus accelerates the
  client-side sim cleanly while leaving multiplayer wire timing
  on real wall-clock.  No need to do both APIs.

### `FUN_004b2258` (InitInstance) ported into `src/main.c`

Class registration and window creation now match retail:

- Class name `WinLords2Class` (so `FindWindowA` from outside the
  process resolves identically — matches what retail's class
  registration uses, see `s_WinLords2Class_004df5b0`).
- Title `Lords2` matches `s_Lords2_004df5c0`.
- `CS_DBLCLKS`, NULL cursor, `BLACK_BRUSH` background, NULL menu.
- `UpdateWindow` + `SetFocus` after create.

Deliberate deviations preserved:

- `WS_VISIBLE` never set inline — `--show-window` does ShowWindow
  separately so `--hidden` headless runs compose cleanly.
- Default style is `WS_OVERLAPPEDWINDOW` at 640×480 centred (the
  exact shape retail transforms to in `FUN_0042e5c0`'s now-dead-code
  8bpp-desktop windowed branch).  `--fullscreen` opts back into
  retail's WS_POPUP full-screen-sized popup.

Smoke verified: 50 frames in well under 8 s under
`tools/run-openlords2.sh`; class registers, window creates,
loop exits cleanly with rc=0.

### What's next

1. Port `FUN_004b29be` (the 2545-byte WndProc) into `src/main.c`'s
   `wndproc`.  Mostly state-mux on `param_2` (WM_*) → engine
   substate jumps.  Worth a dedicated findings doc.
2. Port the bring-up *body* of `FUN_004b2922` into a new
   `src/gfx.c` — DirectDrawCreate + SetCooperativeLevel + surface
   create + palette load.  First real DDraw code in the drop-in.
3. Add turbo v2 to the Frida agent now that we know the
   `timeGetTime`-only seat suffices.  Validate against retail
   behaviour: `DAT_0057c998` should rise N× faster on a chosen
   scale; FPS counter `DAT_004ea174` should be unaffected at the
   monitor cap but FPS-since-boot `DAT_004ea0f4 / DAT_004ea83c`
   should drift.
4. Read `FUN_004b29be` first (item 1) before doing the DDraw port,
   since several DDraw surface-restore paths post WM_PAINT /
   WM_ACTIVATE messages that the wndproc must already handle.

---

## 2026-05-24 — Harness verified end-to-end + first findings

Initial bootstrap session continued.

### Retail harness verified against Lords2.exe

Ran `tools/smoke-test.py --duration-ms 8000` against the real GOG
`Lords2.exe` via the cutestation.soy frida-server.  Results:

```
hwnd_count:        5        (1 game + 4 system: OLE, CIC, MSCTFIME)
hide_count:        2        (CreateWindowExA/WS_VISIBLE + SetWindowLongA)
ddraw_coop:        1        (0x13 EXCL|FULLSCREEN|ALLOWREBOOT → 0x0a NORMAL|ALLOWREBOOT)
SetDisplayMode:    1 blocked (640×480×8 — the classic 8-bit mode-set)
dsound_coop:       1        (0x2 PRIORITY → 0x1 NORMAL)
msgbox:            0
audio_mute:        0        (waveOutSetVolume not called yet in first 8s)
```

**First-run gotcha caught.**  The initial smoke run produced a
borderless black window flash on the host desktop despite all four
post-create show-path hooks being armed.  Root cause: Lords2.exe
passes `WS_VISIBLE` directly in `CreateWindowExA`'s `dwStyle` (style
`0x90080000` = `WS_POPUP|WS_VISIBLE|WS_SYSMENU`), so the window is
shown *during* the create call — by the time `onLeave` registers the
HWND, the display has already happened.  Added a pre-create
`WS_VISIBLE` strip in `CreateWindowEx` `onEnter` (commit `3c093ba`);
second run is fully invisible.

### First Ghidra-headless run completed

`tools/ghidra-headless.sh` decompiled the binary in single-digit
minutes: **2452 functions**, 3 MB `all.c`.

Documented the WinMain → InitInstance → main-loop chain in
[`docs/findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md):

- `entry @ 0x4c3370` — CRT bootstrap, calls `FUN_0040e9ab(hInstance)`
- `FUN_0040e9ab @ 0x40e9ab` — WinMain-equivalent.  641 B.  Hands off
  to `FUN_004b2258` for window+class setup, then drops into the
  PeekMessage/GetMessage main loop with a `Sleep(200)` paused-mode
  branch.
- `FUN_004b2258 @ 0x4b2258` — InitInstance-like.  Registers
  `WinLords2Class` (wndproc `FUN_004b29be`), creates the main window
  full-screen-sized at (0,0), then calls `FUN_004b2922` for what is
  likely DDraw/DSound bring-up.
- Default render size globals confirmed: `0x4eb250 = 640`,
  `0x4eb254 = 480`.

### What's next

1. Read `FUN_004b2922` to understand the DDraw/DSound bring-up chain
   — that's where the 640×480×8 mode-set lives.
2. Read `FUN_004b8c3a` (per-tick driver, called every main-loop
   iteration when no Win32 message is pending) — likely contains sim
   step + render dispatch.
3. Decide whether to add turbo v2 (virtual `timeGetTime`) to the
   harness now or wait until we have the frame counter located.
4. Begin porting `FUN_004b2258` to `src/main.c`'s
   `register_window_class` + `create_main_window` so the drop-in's
   window matches retail (class name, style bits, popup vs
   overlapped).

---

## 2026-05-24 — Bootstrap (initial project setup)

Project skeleton created from scratch.  Adapts the conventions from
the two sibling RE projects in `/opt/src/`:

- **openrecet** — first decomp project; established the
  Ghidra-headless / Frida-harness / co-author-trailer commits style.
- **OpenMare** — newer, more refined workflow; the Job-Object
  launcher pattern, single-TU C builds, MessageBox redirect hooks
  (both Frida-side and drop-in-side), and turbo v2 virtual clock
  all originate there.

### Target binary identification

`Lords2.exe` from the GOG release of *Lords of the Realm II*
(Impressions Games / Sierra On-Line, 1996; GOG patch 2.0.0.3).

```
file:    PE32 executable (GUI) Intel 80386
linker:  MSVC 4.20 (1996-era)
size:    1,031,680 bytes
sha256:  6fec043e59ed3d7ad5de5b313093b8e2ee2d7832ccc224705e010bfe164d004a
imports: KERNEL32, USER32, GDI32, DDRAW, smackw32, DSOUND, WINMM, DPLAYX
base:    0x00400000   image size: 0x001E9000   entry RVA: 0x000C3370
```

No DRM packer; Ghidra can analyze the binary directly.

Companion: `mapl2.exe` (253 KB) is the map editor — out of scope for
the drop-in but a useful side artifact for understanding the map
format.

### What landed

1. `flake.nix` — same shape as OpenMare's: Ghidra/radare2/cutter,
   frida-tools, mingw32 cross-compiler, Python env with
   pillow/numpy/skimage/opencv/frida-python/construct/rich, ffmpeg,
   imagemagick.  `OPENLORDS2_GAME_DIR` defaults to the GOG path.
2. `.gitignore`, `LICENSE` (MIT), `README.md`.
3. `tools/setup.sh` — symlinks `vendor/original/` → GOG install,
   hashes Lords2.exe + companion DLLs + L2.INI + Sierra.inf.
4. `docs/PLAN.md`, `docs/AGENT-WORKFLOW.md`, this log, and
   `docs/findings/engine-quirks.md` scaffold.

### Next candidates

1. Launcher wrapper (`tools/launcher/openlords2-launcher.c`) —
   Job-Object pattern from OpenMare's `openmare-launcher.c`.
   Guarantees the spawned `.exe` dies when the launcher does, no
   matter how the launcher dies.
2. Frida agent (`tools/frida/openlords2-agent.js`) — hidden window
   (`ShowWindow`/`SetWindowPos`/`SetWindowLong` multi-API), DDraw
   `SetCooperativeLevel` exclusive-fullscreen stripping, DSound
   volume clamps, MessageBox capture + IDOK return, turbo v2
   virtual clock.
3. Python driver (`tools/frida_capture.py`, `tools/smoke-test.py`) —
   port from OpenMare, swap target binary name.
4. Drop-in stub (`src/main.c`) — open a hidden window, install the
   MessageBox-to-stderr dev hook, cd into the game dir, exit
   cleanly on a `--frames N` budget.
5. Ghidra-headless decompile pipeline.

Deferred until the user is back: any test run that has potential to
disrupt the host desktop (fullscreen flip, audio output) — the goal
of phases 1-3 above is precisely to make subsequent runs
non-disruptive.
