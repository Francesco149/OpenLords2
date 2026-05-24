# Harness roadmap

Goals for the test harness, in approximate dependency order.  Each
bullet's full design lands as `docs/findings/<subsystem>.md` once
implemented.

## Phase 1 — Non-disruptive boot of retail Lords2.exe

**Status:** in progress (bootstrap session 2026-05-24).

- [x] Launcher wrapper with Job-Object kill-on-close.
- [x] Frida agent: hidden-window multi-API hooks
      (`ShowWindow` / `SetWindowPos` / `SetWindowLong`).
- [x] MessageBox capture (always-on): redirect `MessageBox{A,W}`
      bodies to stderr with a loud banner, return IDOK so the
      engine doesn't block on a modal.
- [x] DDraw fullscreen suppression: rewrite
      `IDirectDraw{,7}::SetCooperativeLevel` to strip the
      `DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN` bits so the engine
      never takes over the desktop or changes resolution.
- [x] DSound mute: clamp `IDirectSoundBuffer::SetVolume` and
      `waveOutSetVolume` calls to silence.
- [x] Turbo v1: `kernel32!Sleep` → no-op for the main thread.
- [x] Turbo v2: virtualised `winmm!timeGetTime` clock pinned to
      the main thread.  Bumped by `--turbo-step-ms` (default 17)
      on every entry to retail's per-tick driver
      `FUN_004b99c0`.  Verified against retail (smoke 2026-05-24:
      1993 engine cumulative-seconds in 4 real seconds at the
      default step, vs 3 without turbo).
- [x] FPS-sample emitter (always on): read retail's FPS-counter
      globals (`DAT_004ea174` / `DAT_004ea0f4` / `DAT_004ea83c`)
      on every engine cumulative-seconds rollover and publish a
      `kind:'fps_sample'` event so the harness can plot the
      perceived-time curve without spelunking memory itself.
- [x] Smoke test: spawn → 5 s window → assert hooks installed →
      teardown.  Now also exercises the turbo path via
      `--turbo` / `--turbo-step-ms N`.

## Phase 2 — Frame capture

- [ ] Hook `IDirectDraw{,7}::Lock` / `IDirectDrawSurface::Lock`
      to grab the back-buffer at every Flip/Blt.
- [ ] Serialize captured frames as top-down BGRA BMP under
      `runs/<scenario>/<ts>/frames/frame_NNNNN.bmp`.
- [ ] Differential frame-diff path: red-tint pixels that
      disagree between retail and our re-impl.

## Phase 3 — Input replay

- [x] WndProc hook (preferred over `DispatchMessage` — args already on
      stack, and avoids round-tripping the engine's own internal
      `PostMessageA` re-entries) records every `WM_KEY*` /
      `WM_MOUSE*` / `WM_CHAR` as a sparse JSONL trace, stamped with
      `engine_tick` count.  See `docs/findings/input-replay.md`.
- [x] `--input-trace FILE` replay path: `PostMessageA` (not Send —
      avoids cross-thread deadlock since the drain runs on the engine
      main thread) the recorded events back into the engine's cached
      main HWND (`DAT_004e5a00`) at the right `engine_tick` count.
- [ ] Real interactive recording session: blocked on either a hostable
      fullscreen Windows session or a fix for the DWM-windowed
      visibility quirk.  Synthetic traces exercise the machinery
      tick-accurately; real recording is gated on the harness host.
- [ ] Side-by-side runs: same trace, retail vs ours, frame-diff
      the divergence.

## Phase 4 — Pure-function differential testing

- [x] For each ported function, when feasible, cross-check its
      output against retail via Frida `NativeFunction` invocation.
      Catches arithmetic / hashing / table-lookup deltas without
      needing a scenario to surface them.  Driver:
      `tools/diff_test.py` + agent-side `runRetail*` RPCs.  Currently
      covers `engine_rng_step` (FUN_00404a46), `engine_compute_fps`
      (FUN_004b27bf), `engine_compute_anim_cycle` (FUN_00451f1a),
      `engine_compute_frame_delta` (FUN_004b20ed), and
      `engine_compute_phase_ticks` (FUN_004bbc80) — 500/500 vectors
      pass on all five.
      See `docs/findings/pure-function-diff.md` for the engine-thread
      race + retry pattern (the engine main thread is still ticking
      during diffs; in-process spin and SuspendThread both deadlock,
      so the orchestrator detects races + retries instead).
      Phase_ticks added the *concurrent-execution* race mode (multi-
      core engine + V8 thread both in FUN_004bbc80 simultaneously) +
      the engine-clear race-pass signature for it.

## Phase 5 — TAS bot

- [ ] Drive a whole game session at inhuman speed via the trace +
      capture machinery, exercising entire game-state subsystems
      end-to-end in CI.

---

The above roughly mirrors the OpenMare harness roadmap.  The
engine specifics differ (DDraw 1.x style rather than DDraw 7,
DirectSound 3D vs DirectSound legacy, no MFC), but the shape of
the harness is the same: hidden window, turbo, mute, capture,
replay, diff.
