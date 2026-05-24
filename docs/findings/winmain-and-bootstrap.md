# WinMain & bootstrap chain

Initial map of Lords2.exe's boot sequence, from CRT entry to the main
message loop.  Derived from a Ghidra full-auto-analysis pass on the
GOG patch-2.0.0.3 binary (sha256 `6fec043e…` — see
[`PROGRESS.md`](../PROGRESS.md) for full hash).

All addresses are preferred-base (ImageBase `0x00400000`).

## Call chain

```
0x004c3370  entry                       (CRT _start, 494 B)
   ↓ heap_init, ioinit, initmbctable, setargv, setenvp, cinit
   ↓ GetStartupInfoA, GetModuleHandleA
0x0040e9ab  FUN_0040e9ab(HINSTANCE)     (WinMain-equivalent, 641 B)
   ↓ getcwd, FUN_004af8d9(s_l2_eng_, …)   (locale / lang setup, "l2_eng")
   ↓ FUN_004ae1b8()                       (?)
   ↓ FUN_0040ecc4()                       (1099 B — config? memory?)
0x004b2258  FUN_004b2258(HINSTANCE)     (InitInstance-like, 797 B)
   ↓ RegisterClassA("WinLords2Class", wndproc=FUN_004b29be)
   ↓ LoadIconA, LoadCursorA × N
   ↓ CreateWindowExA("WinLords2Class", "Lords2", WS_POPUP|WS_VISIBLE|WS_SYSMENU,
                     0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))
   ↓ UpdateWindow, SetFocus
   ↓ FUN_004b2922()                       (post-window init — likely DDraw/DSound bring-up)
   ↓
0x0040e9ab  back in WinMain
   ↓ FUN_00425b10(), FUN_004263ad, FUN_00426d11   (more init)
   ↓ FUN_004aef7e()                       (?)
   ↓ FUN_004b3571(0), FUN_004975d3, FUN_00497500
   ↓ FUN_0042d179, FUN_0040f10f, FUN_00476a5d, FUN_004978ad   (game-state init)
   ↓
   Main loop (LAB_0040eb4c):
     PeekMessageA(&msg, NULL, 0, 0, 0)
     if no message:
        FUN_004b8c3a()            ← per-tick driver (sim step? render?)
        if (DAT_004e65d0 == 0 && DAT_004e65a8 != 0) FUN_0040e8bb()
        else Sleep(200)           ← idle / paused sleep
     else:
        GetMessageA(&msg, …)
        TranslateMessage, DispatchMessage
     if (DAT_005aeb68 == 1) {     ← WM_CLOSE deferred?
        PostMessageA(g_main_hwnd, 0x10 /* WM_CLOSE */, 0, 0)
        DAT_005aeb68 = 0
     }
   ↓ WinHelpA(g_main_hwnd, "l2help.hlp", HELP_QUIT=2, 1)   (always called on exit)
   ↓ FUN_0049767d(), FUN_004b2636()    (teardown)
   ↓ return msg.wParam                  (WM_QUIT propagation)
```

## Notable globals

| Address       | Type | Meaning (best guess) |
|---------------|------|----------------------|
| `0x004e5a00`  | HWND | `g_main_hwnd` — main window, set in FUN_004b2258 |
| `0x004eaba0`  | u32  | base `timeGetTime()` value, captured in FUN_004b2258 |
| `0x004eb250`  | u32  | render width (`0x280 = 640`) |
| `0x004eb254`  | u32  | render height (`0x1e0 = 480`) |
| `0x004ea120`  | u32  | another width (640) — duplicated state |
| `0x004ea124`  | u32  | another height (480) |
| `0x004e6974`  | u32  | `g_directdraw_initialised` (compared to 0 before DDrawCreate) |
| `0x004e65a0`  | u32  | `GetSystemMetrics(SM_CXSCREEN)` cached at boot |
| `0x004ea838`  | u32  | `GetSystemMetrics(SM_CYSCREEN)` cached at boot |
| `0x00553c5c`  | u32  | `g_in_init` (1 during init, 0 once running) |
| `0x005aeb68`  | u8   | deferred-WM_CLOSE flag |
| `0x00553c5c`  | u32  | dup of g_in_init |

## Window creation specifics

```c
CreateWindowExA(
    0x40000,                              // WS_EX_APPWINDOW
    "WinLords2Class",                     // class
    "Lords2",                             // title
    0x90080000,                           // style = WS_POPUP|WS_VISIBLE|WS_SYSMENU
    0, 0,                                 // x, y
    GetSystemMetrics(SM_CXSCREEN),        // full-screen width
    GetSystemMetrics(SM_CYSCREEN),        // full-screen height
    NULL, NULL, hInstance, NULL);
```

Style `0x90080000` = `0x80000000 (WS_POPUP) | 0x10000000 (WS_VISIBLE) |
0x00080000 (WS_SYSMENU)`.  The `WS_VISIBLE` bit is the reason the
harness needs to strip it inside the `CreateWindowEx` `onEnter` — by
the time `onLeave` fires, the window is already shown.

The window class name `WinLords2Class` is what `Process.findModuleByName`
won't find, but `FindWindowA("WinLords2Class", NULL)` will resolve to
the main HWND from outside the process.

## Main loop characteristics

- **No fixed frame limiter.**  The loop calls `Sleep(200)` only when
  the message queue is empty AND `DAT_004e65d0 == 0 && DAT_004e65a8 != 0`
  (some kind of "paused" condition).  Otherwise it spins doing work in
  `FUN_004b8c3a()` per iteration.
- **`FUN_004b8c3a` is the per-tick driver.**  Likely contains the
  simulation step + render dispatch.  TBD.
- **`FUN_0040e8bb` is the alternate per-tick path.**  Called when
  `DAT_004e65d0 == 0 && DAT_004e65a8 != 0` evaluates false — likely
  the "actively running, not paused" path.
- The `0x10 = WM_CLOSE` PostMessage on flag `DAT_005aeb68` suggests
  the engine has a one-shot "close requested from somewhere outside
  the wndproc" defer mechanism.

## DDraw entry point

`DirectDrawCreate(0, &local_2b4, 0)` is called once, gated by
`if (DAT_004e6974 == 0)`.  The DDraw interface pointer is stored in
`local_2b4` (a local), suggesting it's then copied to a global inside
the same function.  The call site is around line 19137 in
`docs/decompiled/all.c` — find the enclosing function for full DDraw
bring-up details.

## Open questions for the next session

1. **What is `FUN_004b8c3a`?**  The per-tick driver — sim+render
   dispatch?  Find its callees and label.
2. **What is `FUN_0040e8bb`?**  The non-paused per-tick path.
3. **DDraw bring-up.**  Which function calls `DirectDrawCreate`?
   What surfaces does it create immediately?  What's the engine's
   primary/backbuffer convention?
4. **DPlay init.**  When does `dplayx.dll` get touched?  Lazy
   (only when "Play Multiplayer" is clicked) or eager?
5. **Smacker.**  Does the engine call into `smackw32.dll` at boot
   for the intro, or only on the menu screen?  Check call sites.

> 📍  Sources: `docs/decompiled/all.c` lines 7574 (FUN_0040e9ab),
> 83617 (FUN_004b2258), 94657 (entry).  Recreate with
> `./tools/ghidra-headless.sh` (idempotent, re-uses analysis).
