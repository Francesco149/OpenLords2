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

The current Frida harness doesn't hook the per-tick path.  When
turbo v2 lands, the natural seat is `winmm!timeGetTime` —
virtualise to advance N× real time per call.  Bound the speedup
via a `--turbo-scale` flag (1.0 default = identity).

A complementary trace mode would log
`DAT_0057c998`/`_DAT_005bb4c8`/`DAT_004ea174` once per second so we
can verify the turbo factor visually instead of via "I think it
looked faster".  Same validate-against-perceived-behavior rule from
the OpenMare carry-over memory.

> 📍  Sources: `docs/decompiled/all.c` lines 7524 (FUN_0040e890),
> 7537 (FUN_0040e8bb), 88042 (FUN_004b8c3a), 88528 (FUN_004b99c0),
> 89413 (FUN_004bb978), 83824 (FUN_004b27bf).
