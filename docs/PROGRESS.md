# OpenLords2 progress log

Reverse-chronological journal of substantial changes.  Each entry is
a self-contained dispatch: what was done, what was learned, what the
next candidate milestones are.  Engine specifics belong in
`docs/findings/<subsystem>.md`; this log is the narrative thread.

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
