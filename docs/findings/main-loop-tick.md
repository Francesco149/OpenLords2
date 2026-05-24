# Main loop & per-tick dispatch

This document corrects an important misread from
[`winmain-and-bootstrap.md`](winmain-and-bootstrap.md): the
"per-tick driver" called from WinMain is *not* `FUN_004b8c3a`.
That's the **DPlay receive pump**.  The real sim+render driver
is `FUN_004b99c0`, reached via `FUN_0040e8bb` → `FUN_0040e890`.

All addresses are preferred-base (ImageBase `0x00400000`).

## Corrected call graph

```
0x0040eb4c  main loop body
   PeekMessage(&msg, ...)
   if (no message):
       FUN_004b8c3a()                      ← DPlay receive pump (NOT sim)
       if (DAT_004e65d0 == 0 && DAT_004e65a8 != 0):
           FUN_0040e8bb()                  ← surface-lost guard
              ↓ surface IsLost? → FUN_004b2730 + FUN_004b26b8 to restore
              ↓ FUN_0040e890()             ← thin wrapper
                 ↓ FUN_004b99c0()          ← THE sim+render driver
                 ↓ if DAT_0052af94 > 8: DAT_005c9274 = 1
       else:
           Sleep(200)                      ← idle/paused sleep
   else:
       GetMessage / TranslateMessage / DispatchMessage
   if (DAT_005aeb68 == 1):
       PostMessage(hwnd, WM_CLOSE, 0, 0)
       DAT_005aeb68 = 0
```

## `FUN_004b8c3a` — DPlay receive pump (not sim)

Gated by `DAT_00553030 != 0` (multiplayer-active flag).  In SP this
function is a no-op.  In MP:

```c
local_14   = GetTickCount();
local_8    = local_14 - _DAT_0053e998;   // ms since last MP pump
_DAT_0053e998 = local_14;
if (DAT_0053f05c < local_8) DAT_0053f05c = local_8;   // max-jitter tracker

loop:
  if (DAT_004e2a2c) {                              // IDirectPlay4 path
      rv = lpDP4->lpVtbl[0x64/4](lpDP4, ...);      // IDirectPlay4::Receive
  } else if (DAT_004e2a30) {                       // RGM_SIGS wrapper path
      rv = FUN_004b6b71(...);                      // (custom transport)
  } else return 0;

  switch (DPSYS message id at &DAT_00515c50):
    3   → FUN_0043e6a2(1, ...)                     // DPSYS_HOST
    5   → FUN_0043e6a2(2, ...)                     // DPSYS_SESSIONLOST?
    7   → FUN_0043e6a2(5, 0)
    0x21→ FUN_0043e6a2(6, 0)
    0x31→ FUN_0043e6a2(3, 0)
    0x101→FUN_0043e6a2(4, 0)
    0x102→FUN_0043e6a2(7, 0)
    0x103→FUN_0043e6a2(8, 0)
    other (application) → FUN_0043eac1(local_28);  // app-msg dispatch

  goto loop  (drain entire queue)
```

The interesting global: `DAT_0053f05c` holds the **maximum DPlay
pump-interval** observed across the session (jitter metric, never
read elsewhere as far as the chain has been walked).  Useful for
turbo-mode soak-testing: if our hooks introduce latency, this counter
will spike.

## `FUN_0040e8bb` — surface-lost guard

```c
if ((primary && primary->IsLost() == DDERR_SURFACELOST) ||
    (back    && back   ->IsLost() == DDERR_SURFACELOST)) {
    if (FUN_004b2730() == 0) {        // re-Restore primary + back
        FUN_004b26b8();               // log "OK :Paused." + InvalidateRect
        return false;                 // skip this frame
    }
}
if (DAT_005aeb68 == 3) DAT_005aeb68 = 1;   // (deferred-WM_CLOSE escalator)
// audio/save state machine via DAT_005c9274
if (DAT_005c9274 != 0) {
    if (DAT_00553030 == 0) { FUN_004978ad(); FUN_00497a34(0); }   // SP
    else                   { FUN_0049792e(); FUN_00497a34(1); }   // MP
}
if (DAT_005aeb68 == 1) return false;       // closing — skip
FUN_0040e890();                            // → FUN_004b99c0 (sim+render)
return true;
```

`vtable+0x60 = IDirectDrawSurface::IsLost`,
`vtable+0x6c = IDirectDrawSurface::Restore` (used by FUN_004b2730).
Constant `-0x7789FE3E = 0x887601C2 = DDERR_SURFACELOST`.

## `FUN_0040e890` — render-fast-skip dispatcher (43 B)

```c
FUN_004b99c0();                            // THE sim+render driver
if (DAT_0052af94 > 8) DAT_005c9274 = 1;    // engine asked to fast-skip render
return 0;
```

`DAT_0052af94` is the **engine state machine** (similar to OpenMare's
`g_engine_state`):

| Value | Likely meaning (deduced from FUN_004b99c0 branches)     |
|-------|---------------------------------------------------------|
| 3     | "ingame, time-of-day advancing" — drives `FUN_00499a1c(1)` |
| 10    | "save in progress" — sim-step loop breaks                |
| 0xb   | "load in progress" — sim-step loop returns               |
| > 8   | "render-fast-skip" — sets `DAT_005c9274`                 |

`DAT_005c9274` then gates the render-phase tail in `FUN_004b99c0`
(if non-zero, the FPS-counter + map redraw + finalisers are
skipped).

## `FUN_004b99c0` — the sim + render driver (2222 B)

The single biggest function on the per-frame hot path.  Shape:

```c
FUN_004b99c0(void) {
    // === pre-tick (always runs once per Win32-pump iteration) ===
    if (FullscreenForcedByDepth && DAT_005533d4 == 0 && DAT_0052af94 == 3) {
        DAT_005533d4 = 1;
        FUN_00472bc5(0, DAT_0057c8cc, 0x12a, 0,0,0,0,0);   // ingame welcome
    }
    if (DAT_0052af94 == 3) FUN_00499a1c(1);   // music/audio update
    FUN_004b0aaa();                           // (input poll?)
    FUN_00498458();
    _DAT_005bb4c8++;                          // wall-clock frame counter
    DAT_004ea128 = FUN_004b20ed();
    FUN_004bbc80();
    FUN_00481b9a();
    FUN_00482111();
    FUN_004782c5();
    FUN_00451f1a();

    // === variable-rate sim sub-steps ===
    local_c = FUN_004bb978();         // 0, 1 or 2 sim sub-steps for this iter
    local_8 = 0;
    while (true) {
        if (local_c <= local_8) {
            // === RENDER PHASE — runs once regardless ===
            FUN_0049a3e6();   FUN_004b1de0();   FUN_004b191e();
            ...
            (state-specific frame composition: ~30 sub-function calls,
             dispatched on DAT_004eac50 = "current screen ID")
            ...
            if (DAT_005c9274 == 0) {           // not fast-skipping
                FUN_00429b1d();
                FUN_0047ed34();
                FUN_00426529();
                FUN_004b27bf();                ← FPS counter
                FUN_004b12fb();
                FUN_004b0e03();
                // WM_CLOSE deferred-shutdown trigger (DAT_005aeb68 == 4)
            }
            return;
        }
        // === SIM SUB-STEP BODY ===
        DAT_0057c998++;                                          // master tick counter
        // 1000-tick / 0x82-tick / 10-tick scheduled events
        if ((&DAT_0057bf04)[DAT_0057c8cc * 0x160] != 0) {
            if (DAT_0057c998 % 1000 == 0) FUN_0043eda0(0xb, 0);
            ...
        }
        FUN_004472ce(DAT_0057c998);              // advance sim by 1 tick
        if (DAT_00553030 == 0) FUN_00404a46();   // SP-only sim step
        if (DAT_0052af94 == 10) break;           // saving — stop sub-step loop
        if (DAT_0052af94 == 0xb) return;         // loading — bail entirely
        // screen-state-specific extra updates
        if (DAT_0057c8fc == 0 && local_c != 0) {
            FUN_0040490d(); FUN_0049a010(); FUN_004650b0();
        } else if (DAT_0057c8fc == 2 && local_c != 0) { ... }
        local_8++;
    }
}
```

### Critical globals identified

| Address       | Meaning                                                       |
|---------------|---------------------------------------------------------------|
| `DAT_0057c998`| **Master sim tick counter** (mod-10 / 0x82 / 1000 schedulers) |
| `_DAT_005bb4c8`| Wall-clock frame counter (pre-tick, every Win32 iter)        |
| `DAT_005c9274`| "Fast-skip render" flag (DAT_0052af94>8)                      |
| `DAT_0052af94`| Engine state machine (3=ingame, 10=save, 0xb=load)            |
| `DAT_004eac50`| Current screen ID (0=menu, 0x10=menu2?, 0x28..0x2a=loading…)  |
| `DAT_0057c8fc`| Sub-state within current screen (0=normal, 2=alt UI)          |
| `_DAT_004e5b0c`| Last-tick `timeGetTime()` (used by `FUN_004bb978`)           |
| `_DAT_0053f230`| Game-speed slider (0=slow, 100=fast)                          |
| `_DAT_0053f234`| Menu/UI speed slider                                          |

## `FUN_004bb978` — sim time-step rate controller (614 B)

This is the function that decides how many sim sub-steps to run this
Win32-message-pump iteration.

```c
DVar2 = timeGetTime();
local_10 = 1;                                  // default: 1 sub-step
iVar3 = FUN_004bb8c0();                        // some "should sim be running?" gate
if (iVar3 == 0) { local_10 = 0; DVar1 = _DAT_004e5b0c; }
else {
    // auto-save 5-second window — return 0 inside it
    if (DAT_0056d8b4 != 0) {
        if (DVar2 - DAT_0056d8b4 < 5000) return 0;
        DAT_0056d8b4 = 0;
        if (DAT_00553030) FUN_0043eda0(0xf, 0);   // MP "auto-save begun" msg
    }
    local_8 = DVar2 - _DAT_004e5b0c;             // ms since last sim step
    if (local_8 < 0) local_8 = 10000;            // tick overflow → catch up
    local_14 = FUN_0043de70();                   // (?)
    if (local_14 == 0)        local_10 = 0;
    else if (local_14 < 0) {
        // fast-skip mode: 2 sub-steps until DAT_0057c998 ≥ 100
        local_10 = (DAT_0057c998 < 100) ? 1 : 2;
        local_14 = 0;
    }
    if (local_14 > 0x8c) local_14 = 0x8c;        // clamp
    DAT_004d215c = local_14;
    DAT_004d2164 = (DAT_004d2164 + 1) % 0xa0;
    if ((&DAT_004e3260)[DAT_004d2164 + (local_14/10)*0xa0] == 0) local_10 = 0;
    else {
        iVar3 = (100 - _DAT_0053f230) / 10;       // 0=fast .. 10=slow
        if (iVar3 < 10) {                         // rate-limited path
            if (screen-state forbids stepping) return 0;
            if (DAT_0053e910 != 0 && ...)        return 0;
            // primary rate gate:
            if ((iVar3 != 0 || DAT_00553030 == 0) && local_8 < iVar3*10 + 2) {
                local_10 = 0;                     // not enough wall time passed
                DVar1 = _DAT_004e5b0c;            // don't advance the marker
            }
        } else local_10 = 0;                       // speed slider = max-slow → freeze
    }
}
_DAT_004e5b0c = DVar1;
return local_10;
```

### Implications for turbo v2

The rate gate is `local_8 < iVar3*10 + 2`, where `local_8 = DVar2 -
_DAT_004e5b0c` is the **wall-clock gap since the last accepted sim
step**.  A `timeGetTime` virtualisation hook that advances faster
than real time will:

1. Make `local_8` always ≥ `iVar3*10 + 2`, so the gate never blocks.
2. Keep `_DAT_004e5b0c` updating on every iteration (set to `DVar2`).
3. Make sim sub-steps proceed at CPU-bound rate (1 per iter typical,
   2 when `local_14 < 0`).

The same `timeGetTime` hook also turbo-charges:
- `FUN_004b27bf` (FPS counter — `_DAT_004e59e0`, `DAT_004eaba0`).
- `FUN_004bbbe3` (UI animation rate, uses `_DAT_004e5b14`).
- `FUN_004b8c3a` (DPlay pump uses `GetTickCount`, *not* `timeGetTime`
  — so DPlay jitter tracking stays on real wall-clock).

→ **A clean win for turbo v2 is to virtualise only `timeGetTime`,
not `GetTickCount`**.  The split between the two APIs in retail is
fortuitous: sim+render speeds up, MP timing stays real.

## `FUN_004b27bf` — FPS counter (frame meter)

```c
DAT_004eada4++;                                // total frames this second
DVar1 = timeGetTime();
ms = DVar1 - _DAT_004e59e0;                    // ms since last render
_DAT_004e59e0 = DVar1;
if (DVar1 - DAT_004eaba0 > 1000) {             // rollover at 1s boundary
    DAT_004ea174 = DAT_004eada4 * 1000 / (DVar1 - DAT_004eaba0);  // = current FPS
    DAT_004eaba0 = timeGetTime();
    DAT_004eada4 = 0;
    DAT_004ea0f4 += DAT_004ea174;              // cumulative FPS
    DAT_004ea83c++;                            // cumulative seconds
}
return ms;
```

Frame-rate globals:

| Address       | Meaning                                          |
|---------------|--------------------------------------------------|
| `DAT_004eada4`| frame count this second                          |
| `DAT_004eaba0`| start-of-second timestamp (was: "base timeGetTime") |
| `_DAT_004e59e0`| last-render timestamp                            |
| `DAT_004ea174`| **current FPS** (refreshed once per wall-second) |
| `DAT_004ea0f4`| cumulative FPS for averaging                     |
| `DAT_004ea83c`| seconds elapsed                                  |

These were partly mis-labelled in [`winmain-and-bootstrap.md`](winmain-and-bootstrap.md)
— `DAT_004eaba0` is *not* "base timeGetTime captured at boot", it's
the FPS-counter's rolling 1-second window start.  Updating that doc
in the next pass.

## Open questions

1. **`FUN_004bb8c0`** — the "should sim be running?" gate.  240 B,
   not yet read.  Likely combines pause flag + dialog-modal flag +
   close-pending flag.  Need to read before porting `FUN_004bb978`.
2. **`FUN_0043de70`** — returns the sub-step count hint.  Negative
   for fast-skip mode.  Worth understanding before touching turbo.
3. **`DAT_004e3260[]` lookup table** — 0xa0 × 11 = 0x6e0 = 1760
   bytes of "should we step at this 0.1× slider × phase-counter
   bucket" flags.  Hand-crafted rate-shaping table?  Worth dumping.
4. **`FUN_004472ce`** (1046 B) — "advance sim by 1 tick".  Probably
   the dispatch root for everything that happens *in* the game
   world.  Not on the critical path for the next port slice but
   the moment we touch turbo for soak-testing this is the next
   stop.
5. **`FUN_00499a1c(1)`** — audio/music update called when
   `DAT_0052af94 == 3` (ingame).  Probably what wires Smacker /
   Miles into the per-tick path.

## Mapping to harness

**Turbo v2 — installed 2026-05-24.**  The Frida agent
(`tools/frida/openlords2-agent.js`) virtualises `winmm!timeGetTime`
and uses `FUN_004b99c0` (entry) as the per-frame bump anchor: each
entry advances `g_virtual_now_ms` by `--turbo-step-ms` (default 17).
The virtual clock is gated to the engine main thread (captured on
first `FUN_004b99c0` entry); DPlay's `GetTickCount` reads, the
DSound mixer, and the Smacker thread keep wall-clock so audio +
network timing aren't disrupted.

Engine FPS-counter globals (`DAT_004ea174` / `DAT_004ea0f4` /
`DAT_004ea83c`) are sampled on every cumulative-seconds rollover and
republished as `kind:'fps_sample'` events — turbo factor is now
visible end-to-end without speluncking memory.  Verified on retail
(2026-05-24 smoke run, 4 s wall time, `--turbo-step-ms 17`):
1993 engine cum-seconds vs 3 without turbo (~500×).

> 📍  Sources: `docs/decompiled/all.c` lines 7524 (FUN_0040e890),
> 7537 (FUN_0040e8bb), 88042 (FUN_004b8c3a), 88528 (FUN_004b99c0),
> 89413 (FUN_004bb978), 83824 (FUN_004b27bf).

## Port status (2026-05-24)

The three outer wrappers are now ported to `src/engine.{c,h}`:

| Retail                | OpenLords2                  | Status                       |
|-----------------------|-----------------------------|------------------------------|
| `FUN_0040e8bb`        | `engine_tick_guard`         | full — surface-lost guard, deferred-shutdown escalator (3→1), savepump dispatcher (SP arm only — MP arm waits on the `DAT_00553030` mirror), gate on `g_shutdown_request == 1` |
| `FUN_0040e890`        | `engine_tick_dispatch`      | full — calls `engine_tick`, latches `g_savepump_request = 1` on `g_engine_state > 8` |
| `FUN_004b99c0`        | `engine_tick`               | scaffold — bumps `g_engine_frame_counter` (retail `_DAT_005bb4c8`), runs the pre-tick block (today: `engine_tick_delta_step` + `engine_tick_phase_step` + `engine_tick_anim_cycle_step`), then the variable-rate sim sub-step loop driven by `engine_tick_rate_step` (today: always 0 sub-steps since scenario hint == 0), then calls `gfx_tick_present(0)` (retail `FUN_00452160(0)`).  Remaining pre-tick subfunctions all deferred — survey notes below |
| `FUN_004b20ed`        | `engine_compute_frame_delta` | full (pure function in `src/engine_delta.c`).  Wrapper `engine_tick_delta_step` reads `timeGetTime`, runs the pure function, writes baseline + delta back into file-static mirrors `g_frame_delta_last_ms` / `g_frame_delta_ms`.  Signed 30 ms low-pass — diverges from `engine_compute_phase_ticks` on the negative-delta branch (suppress vs fire), pinned in tests.  Downstream consumer of the delta (sprite-animation updaters e.g. `FUN_0040da1e`) unported, so the value lands in a write-only mirror today.  8 unit tests cover threshold + clock-wrap + overflow paths |
| `FUN_004bbc80`        | `engine_compute_phase_ticks` | full (pure function in `src/engine_phase.c`).  Wrapper `engine_tick_phase_step` in `engine.c` bundles file-static globals (`g_phase_*`) into the input struct + writes deltas back.  Sticky-flag semantics for the 1040 ms cadence (retail `_DAT_005aeb28`) live in the wrapper — pure function always SETs the flag on rollover, wrapper ORs into the live global rather than overwriting.  12 unit tests cover base + boundary + all-cycles + carry-through paths |
| `FUN_00451f1a`        | `engine_compute_anim_cycle` | full (pure function in `src/engine_anim_cycle.c`).  Wrapper `engine_tick_anim_cycle_step` consumes `g_phase_tick_80ms` (produced upstream by phase step) and bumps `g_anim_cycle_counter` mod 16.  Downstream consumer (`FUN_004071a0`, sprite-strip indexer) unported, so the counter spins with nothing reading it.  7 unit tests; defensive-but-unreachable lower clamp documented in engine-quirks #4 |
| `FUN_004bb978`        | `engine_compute_sim_sub_steps` | full (pure function in `src/engine_rate.c`).  Wrapper `engine_tick_rate_step` in `engine.c` bundles file-static globals (`g_sim_*`) into the input struct + writes deltas back.  LUT (`DAT_004e3260`) not extracted yet — wrapper passes `NULL` (treats as all-zeros) since today scenario hint == 0 prevents the LUT branch from executing.  24 unit tests cover every branch |
| `FUN_004b191e`        | `engine_compute_input_state` | full (pure function in `src/engine_input.c`).  Wrapper `engine_tick_input_step` in `engine.c` calls `GetCursorPos` + `timeGetTime`, bundles file-static `g_input_*` mirrors + the WndProc-mirrored `g_mouse_*` globals + `g_client_screen` into the input struct, calls the pure function, writes outputs back, then clears `g_mouse_dblclk` (retail's `DAT_004eada1 = 0` final statement).  Resolves screen-cursor + button-state into game-coord cursor + per-frame edge flags + 300 ms delayed-single-click events + double-click events.  22 unit tests cover the matrix.  Downstream consumers (UI dispatcher) unported, so the outputs land in write-only mirrors today |
| `FUN_004b27bf`        | `engine_compute_fps`         | full (pure function in `src/engine_fps.c`).  Wrapper `engine_tick_fps_step` reads `timeGetTime` twice (matches retail's 2-call shape — second call only used as the new window-start basis on rollover frames), bundles file-static `g_fps_*` mirrors into the input struct, runs the pure function, writes outputs back.  Lazy-initialises `g_fps_second_window_start_ms` on first call to skip the spurious boot-time rollover (retail's `FUN_004b2258` does the equivalent at line 83665 before the main loop spins).  Wired into `engine_tick`'s render-phase tail, gated on `g_savepump_request == 0` (always true today).  9 unit tests cover boundary + rollover + cumulative + clock-wrap branches.  Current FPS is write-only from our side (UI overlay unported); cumulative pair read by `FUN_004b2636` (session-diag, unported) and by main.c's shutdown summary as a smoke artifact (printed via four new public readers `engine_get_current_fps/cumulative_fps/cumulative_seconds/frame_counter`) |
| `FUN_004bb8c0`        | `engine_run_gate` (file-static) | stub returning 1 (matches retail — the function literally `return 1` in retail too).  No follow-up needed unless retail's debug build reveals a different body |
| `FUN_0043de70`        | `engine_scenario_rate_hint` (file-static) | outer arm only — `return 0` unless `g_engine_state == 3`; today always returns 0.  The full 1211-byte body (scenario-rate selector based on per-player tick budgets) lands when the player-faction data (`DAT_0057bf04`) + current-player mirror (`DAT_0057c8cc`) become available |
| `FUN_004b2730`        | `gfx_restore`               | full (prior slice) |
| `FUN_004b26b8`        | (skipped)                   | retail logs `"OK :Paused."` + InvalidateRects after a successful Restore-during-guard.  Deferred — `engine_tick_guard` comment flags it.  No behavioural delta for the smoke harness (we have no Smacker movie that would care about the invalidation) |
| `FUN_004978ad / 0049792e / 00497a34` | `savepump_begin_sp` / `savepump_step` | stubs.  Engine state can't transition above 3 today, so `g_savepump_request` never sets; dispatch arm never runs |

### Pre-tick subfunctions — surveyed, deferred

Five functions in the pre-tick block remain unported.  Each was
read end-to-end; the disposition column records *why* the port is
deferred and *what has to land first* to unblock it.  The order
matches retail's call order in `FUN_004b99c0`'s pre-tick prologue.

| Retail        | Size  | What it does                                            | Gate                                       | Blocked on                                                                                                                                                                                                                                                                          |
|---------------|-------|----------------------------------------------------------|--------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `FUN_004b0aaa` | 11 B  | Empty body — `return;`.  Likely a debug hook the ship build optimised out (the slot is preserved but the body is gone). | none                                       | nothing — the port is "do nothing".  When we land it, the function is a comment-only acknowledgement that the slot was surveyed.  Worth landing only if the future bisection workflow benefits from a verbatim call sequence; otherwise leave the surveyed-empty result in this doc. |
| `FUN_00498458` | 132 B | Menu state-machine refresh.  Dispatches to `FUN_004984dc` / `FUN_00480f8b` / `FUN_004afab9` based on the (`DAT_004eac50`, `DAT_0057c8fc`) pair. | `DAT_004eac50 == 0` AND `DAT_0057c8fc ∈ {0, 2}` | the menu subsystem.  Today neither global has a mirror in our port (no UI screen state, no sub-state) and the called subfunctions are unported.  Lands together with the menu screen port. |
| `FUN_00481b9a` | 1399 B | Castle/unit grid iterator — walks the 0x50-element table at `DAT_005544ac`, computing per-castle aggregates (worker counts at `DAT_0053f028` / `DAT_00553c58`, food at `DAT_00553f74`, etc.) and rasterising castle footprints into the 0x50×0x50 collision grid at `DAT_005440e2`. | `DAT_0057c8fc == 2`                        | castle table + collision grid.  Needs the castle.dat parser + ~10 game-state mirrors.  Largest pre-tick subfunction — natural slice with the castle subsystem. |
| `FUN_00482111` | 476 B | Smaller object-grid iterator — walks the 0x65-element table at `DAT_0057a108` (likely armies/units, separate from castles) and rasterises their footprints into the same `DAT_005440e2` collision grid. | `DAT_0057c8fc == 2`                        | army/unit table.  Sibling of `FUN_00481b9a`; lands with the unit subsystem.  Shares the collision-grid output with the castle iterator. |
| `FUN_004782c5` | 340 B | Load-screen timeout / state transition.  Counts up `DAT_00568470`; once past 5000 (~83 s at 60 Hz, but the units depend on how often this runs — probably a frame counter, not ms), advances the engine into `DAT_004eac50 = '\x13'` or `'\x2e'`. | `DAT_004eac50 == '+' (0x2b)` AND `DAT_0057c8fc == 2` | load screen state machine.  Today no producer transitions `DAT_004eac50` to `'+'`.  Lands together with the load-screen port (which itself depends on the savepump chain). |

The combined effect of all five being skipped: zero observable
delta from current behaviour, because each is gated on a screen
ID / sub-state that the drop-in cannot produce.  Future bisection
runs against a retail trace will see the same divergence — the
trace will show these functions being called and we will not, but
the global state we'd be reading is also all-zero, so the would-be
mutations are no-ops.

### Render-phase tail — surveyed + FPS counter ported (2026-05-24)

Six functions run at the tail of the render phase, after the screen-
dispatch ladder, gated on `DAT_005c9274 == 0` (g_savepump_request).

| Retail        | Size  | Disposition | Notes |
|---------------|-------|-------------|-------|
| `FUN_00429b1d` | 135 B | **Deferred — blocked on map subsystem.** Clamps the map-scroll offsets `DAT_005651b4` / `DAT_005651b8` against the map dimensions `DAT_0053e8ac` / `DAT_0056d67c`.  Trivial pure function; lands when the map subsystem provides the producers for the scroll values and the dimensions. |
| `FUN_0047ed34` | 103 B | **Deferred — blocked on UI subsystem.**  Clamps the UI-scroll offsets `DAT_0055ce70` / `DAT_005678a0` against constants 0x41 / 0x42.  Trivial pure function; lands when the UI subsystem provides the producers for the scroll values. |
| `FUN_00426529` | 11 B  | **Deferred — empty body.**  Same disposition as `FUN_004b0aaa` / `FUN_004b1de0` / `FUN_004b12fb` — ship-build-stripped debug hook.  No port — the slot stands surveyed. |
| `FUN_004b27bf` | 143 B | **Ported.**  FPS counter — see below. |
| `FUN_004b12fb` | 21 B  | **Deferred — `return 1` stub in retail.**  Likely a debug-build gate optimised out in the ship build.  No port — the slot stands surveyed.  If the stub becomes meaningful in some future debug path (e.g. ASLR-pinned save-state validator) it's a single-line landing. |
| `FUN_004b0e03` | 875 B | **Deferred — blocked on intro/Smacker screen.**  Palette-fade machine, gated on `DAT_004eac50 == '#' (0x23)`.  Drives a 12-step-per-RGB lerp from `DAT_004ea530..532` source triplet array → `DAT_004ea1b0..1b2` live palette, on a 20 ms cadence.  On convergence (or the per-screen frame-budget timer `DAT_004e65e0` running out), snaps the palette, transitions screen ID to `DAT_004e65c8`, schedules a deferred screen-change action via `DAT_0057d340 = 2`.  Calls `FUN_0042f58c` to upload the palette to the DAC.  Lands together with the intro/Smacker screen subsystem. |

After the tail, retail has the deferred-shutdown trigger gated on
`DAT_005aeb68 == 4` — that path is the sim-requested-shutdown branch
which is not yet reachable from anything our drop-in can produce
(`g_engine_state` never transitions above 0 today).

### Render-phase head between sim sub-step loop and screen-dispatch — surveyed (2026-05-24)

A single one-liner sits between the input-state resolver
(`FUN_004b191e`, ported) and the screen-dispatch ladder.  Survey
result:

| Retail        | Size  | Disposition | Notes |
|---------------|-------|-------------|-------|
| `FUN_0040f1a0` | 1117 B | **Deferred — itself a screen-state ladder.**  Gated on `DAT_0057d340 != 0` (the deferred-screen-change cookie).  Body is ~40 `else if (DAT_004eac50 == <id>)` branches dispatching into per-screen UI subsystems (FUN_004110b1, FUN_0041e7e1, FUN_00414819, …).  Every called subfunction is per-screen UI work, blocked on the corresponding screen subsystem.  Lands incrementally as each screen gets its UI port. |

### Render-phase head — surveyed (2026-05-24)

Three functions run at the head of the render phase (after the sim
sub-step loop, before the screen-dispatch).  Survey result:

| Retail        | Size  | Disposition | Notes |
|---------------|-------|-------------|-------|
| `FUN_0049a3e6` | 109 B | **Deferred — dead branch.**  Body is gated on `DAT_004eac50 == 0x24` (the screen-ID byte).  Cross-referencing `grep -h "DAT_004eac50 *=" ...` shows screen IDs 0x23 and 0x25 are written, but 0x24 is **never written anywhere in retail**.  The whole body is unreachable.  Likely an orphaned screen state from an earlier revision of the menu state machine, kept in the binary by the linker because the function is referenced through a vtable / function pointer chain that the compiler couldn't dead-strip.  Not ported — the test for `0x24` would never fire even if every called subfunction inside were ported. |
| `FUN_004b1de0` | 11 B  | **Deferred — empty body.**  Same disposition as `FUN_004b0aaa` from pre-tick: ship build's optimised-out debug hook.  No port — the slot stands surveyed in this doc. |
| `FUN_004b191e` | 981 B | **Ported** — see `engine_compute_input_state` in the port-status table above.  Substantive: cursor edge detection, dblclk consumption, 300 ms delayed-single-click, motion idle tracking.  All data dependencies satisfied by the existing WndProc mirrors (`g_mouse_buttons`, `g_mouse_dblclk`, `g_mouse_x/y`, `g_client_screen`). |

Engine-state mirrors landed in `engine.h`:

| Retail global  | OpenLords2          | Initial | Notes |
|----------------|---------------------|---------|-------|
| `DAT_0052af94` | `g_engine_state`    | 0       | menus.  Only `engine_tick_dispatch` reads it (>8 fast-skip latch); no writer yet |
| `DAT_005aeb68` | `g_shutdown_request`| 0       | WndProc writes 3 on WM_CLOSE / 1 on VK_ESCAPE.  Inline `DestroyWindow` still in WndProc until the sim that acks the deferred shutdown lands |
| `DAT_005c9274` | `g_savepump_request`| 0       | latched by `engine_tick_dispatch`; cleared by retail's full savepump (stubbed) |
| `_DAT_005bb4c8`| `g_engine_frame_counter` (file-static) | 0 | retail uses it as a frames-since-boot cookie in save metadata; no consumer in our port yet |

Main loop body now matches retail's shape:

```
PeekMessage / DispatchMessage drain
    ↓
engine_tick_guard()          (only fires while g_gfx_active)
    ↓
    ├─ gfx_surface_lost()?  → gfx_restore — if !ok, return 0
    ├─ shutdown_request == 3 → set to 1
    ├─ savepump_request      → SP savepump_step() (stub)
    ├─ shutdown_request == 1 → return 0
    ↓
engine_tick_dispatch()
    ↓
engine_tick() = frame_counter++ + gfx_tick_present(0)
    ↓
(g_engine_state > 8) ? g_savepump_request = 1 : ()
```

Behavioural delta vs the pre-slice direct-call shape:

- Surface-lost path now wired into the per-frame body — if a
  present-side Blt/Lock returns `DDERR_SURFACELOST`, the next loop
  iteration calls `gfx_restore` before attempting another present.
  Previously the latch was consulted only on `WM_ACTIVATEAPP`.
- Shutdown path unchanged in practice (WndProc inline `DestroyWindow`
  still wins the race), but the structural seam is now in place for
  the follow-up slice that removes the inline.
- Mouse input now resolved once per frame in the render-phase head
  (matches retail's call site for `FUN_004b191e`).  The WndProc-
  cached button + dblclk bitmasks get consumed each tick: dblclk
  bits clear, button-edge events latch into `g_input_*` mirrors,
  and the 300 ms delayed-single-click timer runs.  No downstream
  consumer yet, so the outputs are write-only.  The
  `g_input_cursor_cache_enabled` mirror defaults to 0 — retail's
  cursor-capture / DDraw exclusive-mode-fullscreen path would set
  it, but our windowed-mode harness never enters that state.

### Wiring: cross-TU mouse globals

Promoted from `static` in `main.c` so `engine.c` can `extern` them:

- `g_mouse_x`, `g_mouse_y` (int16) — WM_MOUSEMOVE-cached client-space
  cursor coords.  Read by `engine_tick_input_step` (which can
  optionally overwrite them with screen-space coords when
  `g_input_cursor_cache_enabled != 0`).
- `g_mouse_buttons` (uint8) — current button bitmask.
- `g_mouse_dblclk` (uint8) — pending dblclk bitmask, cleared by
  `engine_tick_input_step` after each tick.
- `g_client_screen` (RECT) — client rect in screen coords.  Used
  for the viewport-to-game-coord mapping.  `engine.c` forward-
  declares `struct tagRECT` (the Win32 layout `{ LONG left, top,
  right, bottom }`) and reads through a `const LONG *` cast to
  avoid pulling `windows.h` into headers.
