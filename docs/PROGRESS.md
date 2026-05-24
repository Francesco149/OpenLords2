# OpenLords2 progress log

Reverse-chronological journal of substantial changes.  Each entry is
a self-contained dispatch: what was done, what was learned, what the
next candidate milestones are.  Engine specifics belong in
`docs/findings/<subsystem>.md`; this log is the narrative thread.

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
