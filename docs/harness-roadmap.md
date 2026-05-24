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
- [ ] Turbo v2: virtualised `timeGetTime` clock pinned to the
      main thread (so the engine *perceives* time moving faster
      rather than just spinning).
- [x] Smoke test: spawn → 5 s window → assert hooks installed →
      teardown.

## Phase 2 — Frame capture

- [ ] Hook `IDirectDraw{,7}::Lock` / `IDirectDrawSurface::Lock`
      to grab the back-buffer at every Flip/Blt.
- [ ] Serialize captured frames as top-down BGRA BMP under
      `runs/<scenario>/<ts>/frames/frame_NNNNN.bmp`.
- [ ] Differential frame-diff path: red-tint pixels that
      disagree between retail and our re-impl.

## Phase 3 — Input replay

- [ ] `DispatchMessage` hook records every `WM_KEY*` / `WM_MOUSE*`
      / `WM_CHAR` as a sparse JSONL trace.
- [ ] `--input-trace FILE` replay path: `SendMessageA` / 
      `PostMessageA` the recorded events back into the engine's
      main HWND at the right engine-frame offset.
- [ ] Side-by-side runs: same trace, retail vs ours, frame-diff
      the divergence.

## Phase 4 — Pure-function differential testing

- [ ] For each ported function, when feasible, cross-check its
      output against retail via Frida `NativeFunction` invocation
      with the engine suspended.  Catches arithmetic / hashing /
      table-lookup deltas without needing a scenario to surface
      them.

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
