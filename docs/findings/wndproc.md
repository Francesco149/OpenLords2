# WndProc — `FUN_004b29be @ 0x4b29be`

The class WndProc registered by `FUN_004b2258` (InitInstance).
2545 bytes of decompiled output but structurally a flat `switch
(uMsg)` with the four-band range tree that MSVC 4.20 emits for a
sparse case-table.  This document maps every handled message to
its retail behaviour, the globals it touches, and what the
drop-in needs to do to mirror it.

All addresses preferred-base (ImageBase `0x00400000`).

## Outer dispatch shape

```c
LRESULT FUN_004b29be(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    DWORD cached_mouse = CONCAT22(DAT_004eabc0, DAT_004eabbe);
    if (msg < 0x10)        { /* WM_NULL..WM_PAINT (0x0f) — see below */ }
    else if (msg < 0x1d)   { /* WM_CLOSE (0x10), WM_ACTIVATEAPP (0x1c) */ }
    else if (msg < 0x7f)   { /* WM_DISPLAYCHANGE (0x7e) */ }
    else if (msg < 0x201)  { /* WM_KEY*/WM_CHAR/WM_SYSCHAR/WM_MOUSEMOVE */ }
    else /* msg >= 0x201 */{ /* mouse buttons + palette */ }

    DAT_004eabc0 = (uint16_t)(cached_mouse >> 16);
    DAT_004eabbe = (uint16_t) cached_mouse;
    return DefWindowProcA(h, msg, wp, lp);
}
```

Every handler **falls through to `DefWindowProcA` after writing
`cached_mouse` back**.  The only `return` statements that bypass
the tail are:

| Path                       | Behaviour                                  |
|----------------------------|--------------------------------------------|
| `WM_PAINT` handled         | `return 1`                                 |
| `WM_KEYDOWN` VK_ESCAPE      | `return 0` (only when state is ingame)     |

So even WM_DESTROY hits `DefWindowProcA` at the bottom (which is
fine — DefWindowProcA is a no-op for WM_DESTROY).

`DAT_004eabbe/DAT_004eabc0` form a **last-mouse-coord cache**:
WM_MOUSEMOVE (`0x200`) sets `cached_mouse = lParam` (lo=x, hi=y);
all other messages preserve the prior value.  This cache is read
by the input/render path so per-frame logic always has a "current
cursor" position even when the cursor isn't moving.

## Messages handled

### WM_PAINT (`0x0f`) — Smacker-aware paint

```c
if (FUN_0042e26f()) {                 // DAT_00516a4c — Smacker movie active?
    FUN_0042e3ba(h);                  // BeginPaint + PatBlt(black) + EndPaint
                                      //   + SmackGoto for the current Smack handle
} else {
    BeginPaint(h, &ps); EndPaint(h, &ps);
    if (DAT_004e65a8 == 0) {          // not focused → repaint via FUN_00451f70
        FUN_00451f70();               // (full-screen redraw / surface flip)
    }
}
return 1;                             // skips DefWindowProcA tail
```

`FUN_0042e26f` is `return DAT_00516a4c != 0` — a Smacker
movie-active flag.  When playing a cutscene, paint is delegated to
the Smacker helper which black-fills the dirty region and resyncs
the movie to the engine clock.

### WM_CREATE (`0x01`) — no-op

Falls into the outer block but the `case 1:` does nothing except
re-cache `DAT_004eabc0/004eabbe`.

### WM_DESTROY (`0x02`) — full subsystem teardown

```c
DAT_004e5a00 = 0;          // clear cached main HWND
FUN_0042dfe2();            // close Smacker movie (SmackClose) + paletted cleanup
FUN_004b28cd(1);           // release `DAT_004df288` (a palette object) + FUN_0042eaa6
FUN_00425d35();            // DSound shutdown — Release primary buffer + ALL channels
FUN_004b7180(1);           // DPlay shutdown — Release IDirectPlay4 (DAT_004e2a2c)
PostQuitMessage(0);
```

This is *the* clean-shutdown chain.  Anything that holds a COM
pointer (DDraw, DSound, DPlay, Smacker) gets released here.  In
the drop-in we'll only run the pieces whose subsystems are
actually initialised — the chain naturally degrades because each
helper guards on its own non-null state flag.

### WM_MOVE (`0x03`) / WM_SIZE (`0x05`) — focus + client-rect cache

Both messages share a single arm (case 3, case 5 fall together):

```c
if (!IsIconic(h)) {
    FUN_004b26f8();        // log "OK :Unpaused." if foreground == DAT_004e5a00,
                           //   clear DAT_004e65d0
    DAT_004e65a8 = 1;      // focused
} else {
    FUN_004afab9(0x4df73c, 0, 0);   // log "OK :Minimizing."
    FUN_004b26b8();                 // log "OK :Paused." + InvalidateRect + clear DAT_004e65d0
}

if (DAT_0053f228 == 0) {                  // windowed mode
    GetClientRect(h, (LPRECT)&DAT_004ea180);
    ClientToScreen(h, (LPPOINT)&DAT_004ea180);     // top-left → screen
    ClientToScreen(h, (LPPOINT)&DAT_004ea188);     // bottom-right → screen
} else {                                  // fullscreen
    SetRect((LPRECT)&DAT_004ea180,
            0, 0, GetSystemMetrics(0), GetSystemMetrics(1));
}
FUN_004afab9(0x4df750, 0, 0);             // log "OK :Window moved."
DAT_0057d340 = 2;                         // render-dirty marker?

if (DAT_004df290 != 1) {                  // ???
    if (DAT_0052af94 == 0) {              // engine state == "menu"
        if (DAT_004eac50 == '"')          // 0x22 — specific menu screen
            FUN_0042e50d();               // Blt back→primary at offset DAT_004ea180,DAT_004ea184
    } else {
        FUN_00451f70();                   // full redraw
    }
}
```

`DAT_004ea180` is a **screen-space RECT** for the client area
(top-left at `[180..184]`, bottom-right at `[188..18c]`).  In
fullscreen it's pinned to `(0,0,SM_CXSCREEN,SM_CYSCREEN)` so the
blits can always assume "screen-space".  The blit at `FUN_0042e50d`
copies back→primary with the 640×480 source rect, destination
top-left at the cached client origin.

### WM_CLOSE (`0x10`) — deferred shutdown request

```c
DAT_005aeb68 = 3;
```

That's *all* it does — no `DestroyWindow`.  The actual close path
runs through the sim/render driver, which escalates `3 → 1 → 4`
across frames and eventually re-posts WM_CLOSE through the main
loop.  See `main-loop-tick.md` for the escalator.  This is why an
external `WM_CLOSE` (Alt+F4, taskbar close button) cleanly drains
the engine before the window dies, instead of yanking surfaces
out from under in-flight blits.

### WM_ACTIVATEAPP (`0x1c`) — focus/surface restore gate

```c
if (wp == 0 ||                              // becoming inactive
    GetForegroundWindow() != h ||           // someone else stole focus
    IsIconic(h))                            // minimized
{
    DAT_004e65a8 = 0;                       // not active
} else {
    DAT_004e65a8 = 1;                       // active
}

FUN_004afab9(DAT_004e65a8 ? 0x4df764       // "OK :Active."
                          : 0x4df774, 0, 0);// "OK :Not active."

if (DAT_004e65d0 != 0 && DAT_004e65a8 != 0) {
    // surface marked LOST + we just regained focus → try to restore
    if (FUN_004b2730() == 0) {              // both Restore() calls succeeded
        if (GetForegroundWindow() == h) {
            FUN_004afab9(0x4df788, 0, 0);   // "ERR:Re-initializing direct draw."
            if (FUN_004b2922()) {           // full DDraw bring-up
                FUN_004b26f8();             // "OK :Unpaused."
            }
        }
    } else {
        FUN_004b26f8();                     // "OK :Unpaused."
    }
}

if (DAT_004df290 != 1 && DAT_0052af94 != 0)
    FUN_00451f70();                         // redraw if not in menu
```

This is the **Alt-Tab restore path**.  When the user tabs away,
DDraw flags both surfaces as LOST (handled elsewhere).  When the
user tabs back, this branch sees `surface_lost && now_active` and
either Restores in place or re-runs the entire DDraw bring-up
(`FUN_004b2922`) from scratch.

### WM_DISPLAYCHANGE (`0x7e`) — cache new screen metrics

```c
_DAT_004eabd8   = wp;                  // wp = bits-per-pixel
DAT_004e65a0    = lp & 0xffff;         // SM_CXSCREEN equiv
DAT_004ea838    = lp >> 16;            // SM_CYSCREEN equiv
```

No follow-up — the cached values are read by the next paint/blit
to recentre.  `_DAT_004eabd8` is the bpp; the existing DDraw
bring-up reads it to gate the "should we force fullscreen?"
branch (`DAT_0053f228 = 1 if bpp != 8`).

### WM_KEYDOWN (`0x100`) — the input switch

```c
DAT_004eabb4 = 1;                      // "key pressed this frame" flag
switch (wp) {
  case 0x08:  FUN_00401c70();         break;  // VK_BACK
  case 0x0d:  FUN_00401c5b();                  // VK_RETURN
              FUN_0043600c();         break;
  case 0x11:  DAT_004df3a8 = 1;       break;  // VK_CONTROL — held flag
  case 0x1b:  if (DAT_0052af94 == 3) FUN_004343f8();    // VK_ESCAPE
              else                   DAT_005aeb68 = 1;  //   ingame=menu, else close
              return 0;
  case 0x23:  FUN_00401d11();         // VK_END
              FUN_004360f2();         break;
  case 0x24:  FUN_00401cfc();         break;  // VK_HOME
  case 0x25:  if (DAT_00553e68 > 0) DAT_00553e68--;     // VK_LEFT
              FUN_00401cbc();         break;
  case 0x27:  if (DAT_00553e68 < 0x50) DAT_00553e68++;  // VK_RIGHT (cap=80)
              FUN_00401cda();         break;
  case 0x2d:  FUN_00401ca3();         break;  // VK_INSERT
  case 0x2e:  FUN_00401c93();         break;  // VK_DELETE
  case 0x31..0x39: // '1'..'9' — speed/digit
              if (DAT_004df3a8 == 0) FUN_0043c910(wp);
              else                   FUN_0043c885(wp);  // Ctrl-modified
              break;
  case 0x63: case 0x67:               break;  // numpad 3/7 — no-op
  case 0x70:  WinHelpA(h, "l2help.hlp", 3, 1); break;  // VK_F1
  case 0x71:  DAT_00568960 = (DAT_00568960 + 1) % 3;   // VK_F2 — cycle 0→1→2→0
              if (DAT_004eac50 == '\t') DAT_00553e60 ^= 1;
              break;
  case 0x72:  if (DAT_0053f684 == 1) DAT_0055402c ^= 1; break;  // VK_F3
  case 0x73:  if (DAT_0053f684 == 1) {                          // VK_F4 — unit/army edit
                  if (DAT_0057c8fc == 0) _DAT_005402d4++;
                  else for (i=1; i<21; i++) (&DAT_0055461a)[i*0x1b0] += 1;
              }
              break;
  case 0x74:  // VK_F5 — recenter window to 640×480 (windowed only)
              if (DAT_0053f228 == 0) {
                  AdjustWindowRectEx(... 640, 480 ...);
                  SetWindowPos(h, NULL, (cx-640)/2, (cy-480)/2, ...);
              }
              break;
  case 0x75:  if (DAT_0053f684) FUN_0043eda0(0x13, 0); break;  // VK_F6
  case 0x76:  if (DAT_0053f684 == 1) FUN_0043eda0(0x1a, 0); break;  // VK_F7
  case 0x77:  FUN_0043eda0(0x12, 0); FUN_0043f7e3(); break;    // VK_F8
  case 0x78:  // VK_F9 — toggle (' ' screen + multiplayer)
              if (DAT_004eac50 == ' ' && DAT_00553030 != 0) DAT_0052eff8 ^= 1;
              break;
  case 0x7a:                          break;  // VK_F11 — reserved/no-op
  case 0x7b:  DAT_00553018 = 1;               // VK_F12
              if (DAT_004eac50 == '\t' && DAT_0057cb18 == 2)
                  DAT_00553018 = 0;
              break;
}
```

Key insights:

- `DAT_004eac50` is the **current screen ID** (one-byte enum).
  Observed values: `0x09 ('\t')`, `0x20 (' ')`, `0x22 ('"')`.
  Hot-keys behave differently per screen.
- `DAT_0053f684` is a "debug enabled" gate.  Real release builds
  probably ship with this set to 1 — F3/F4/F6/F7 only fire when
  it's 1.
- `DAT_004df3a8` is the **Ctrl held** flag, set on VK_CONTROL down,
  cleared on VK_CONTROL up in WM_KEYUP below.  Used as a modifier
  on the digit keys (`0x31..0x39`).
- `DAT_00553030` is the **multiplayer active** flag (matches
  `main-loop-tick.md`'s DPlay-pump gate).
- F5 (`0x74`) recentres the window to 640×480 — useful free
  feature for the drop-in if we want it.

### WM_KEYUP (`0x101`) — Ctrl release only

```c
if (wp == 0x11) DAT_004df3a8 = 0;
```

### WM_CHAR (`0x102`) — text input

```c
if (DAT_0057c8fc == 2 && DAT_0057a0cc == 0) {
    if (wp == 'h' || wp == 'H') FUN_0043c77a(0);
    else if (wp == 'v' || wp == 'V') FUN_0043c77a(1);
} else if (DAT_0057a0cc != 0 && DAT_0057a0cc < 5000) {
    DAT_0057a0cc = 5000;     // bump some timer to max
}
FUN_00401a20((BYTE)wp);     // generic char-input handler
```

`DAT_0057c8fc == 2` is the "alt UI" screen state from
`main-loop-tick.md`'s state table — H/V hotkeys (likely
horizontal/vertical view toggle) only fire there.

### WM_SYSCHAR (`0x105`) — Alt-key shortcut map

```c
switch (wp) {
  case 0x0d:                    break;  // Alt-Enter — no-op
  case 0x31: FUN_00408ea4(1);   break;  // Alt-1
  case 0x32: FUN_004477e7(2);   break;  // Alt-2
  case 0x33: FUN_004477e7(3);   break;  // Alt-3
  case 0x34: FUN_004477e7(4);   break;  // Alt-4
  case 0x35: FUN_004477e7(5);   break;  // Alt-5
  case 0x58: PostMessageA(h, WM_KEYDOWN, VK_ESCAPE, 0); break;  // Alt-X
}
```

Alt-Enter is *not* wired to fullscreen-toggle here — retail's
fullscreen is locked at startup.

### WM_MOUSEMOVE (`0x200`) — cache lParam

```c
cached_mouse = lp;          // overrides the default uVar7 cache
```

That's it.  The actual cursor-driven logic happens in the
per-frame sim/render loop, reading the cache.

### Mouse buttons (`0x201..0x206`)

```c
WM_LBUTTONDOWN   (0x201): DAT_004eabc2 |= 1;
WM_LBUTTONUP     (0x202): DAT_004eabc2 &= ~1;
WM_LBUTTONDBLCLK (0x203): DAT_004eada1 |= 1;
WM_RBUTTONDOWN   (0x204): DAT_004eabc2 |= 2;
WM_RBUTTONUP     (0x205): DAT_004eabc2 &= ~2;
WM_RBUTTONDBLCLK (0x206): DAT_004eada1 |= 2;
```

`DAT_004eabc2` is the **mouse-button-down bitmask** (bit 0 = LMB,
bit 1 = RMB).  `DAT_004eada1` is the **double-click latch** (set on
WM_*BUTTONDBLCLK, cleared by the consumer).  Both single bytes.

The class is registered with `CS_DBLCLKS` so the WM_*BUTTONDBLCLK
messages do fire (otherwise DBLCLK collapses into two
BUTTONDOWN/UP pairs).

### Palette messages (`0x30f` WM_PALETTECHANGED, `0x311` WM_QUERYNEWPALETTE)

```c
// WM_PALETTECHANGED: only act if not from our own window
// WM_QUERYNEWPALETTE: always
if (DAT_0053f228 == 0 && DAT_004eabd0 != NULL) {
    FUN_0042f1df(1);     // SetSystemPaletteUse + CreatePalette + primary->SetPalette
}
```

Only matters in **windowed 8bpp** (`DAT_0053f228 == 0`) — when
another app changes the system palette, the engine recreates its
own palette object and re-attaches it to the primary surface.  In
fullscreen, we own the palette outright via DDraw exclusive mode
and don't need to react.

`DAT_004eabd0` is the **primary IDirectDrawSurface**;
`DAT_004eafac` (referenced in `FUN_0042e50d`) is the back surface.

## Globals — full table

| Address          | Kind   | Meaning                                                |
|------------------|--------|--------------------------------------------------------|
| `DAT_004eabbe`   | uint16 | Last mouse x (lo half of cached LPARAM)                |
| `DAT_004eabc0`   | uint16 | Last mouse y (hi half of cached LPARAM)                |
| `DAT_004eabc2`   | uint8  | Mouse button bitmask (bit0=LMB, bit1=RMB)              |
| `DAT_004eada1`   | uint8  | Double-click latch (bit0=LMB-dbl, bit1=RMB-dbl)        |
| `DAT_004eabb4`   | uint?  | "Key was pressed this frame" flag (WM_KEYDOWN sets it) |
| `_DAT_004eabd8`  | DWORD  | Last WM_DISPLAYCHANGE bpp                              |
| `DAT_004e5a00`   | HWND   | Cached main HWND (cleared in WM_DESTROY)               |
| `DAT_004e65a8`   | int    | Window-active flag (focused & not iconic)              |
| `DAT_004e65d0`   | int    | Surface-LOST flag (set by render path, cleared here)   |
| `DAT_004e65a0`   | int    | Cached screen width (from WM_DISPLAYCHANGE)            |
| `DAT_004ea838`   | int    | Cached screen height (from WM_DISPLAYCHANGE)           |
| `DAT_004ea180`   | RECT   | Client area in screen coords (l,t,r,b at +0/+4/+8/+c)  |
| `DAT_0057d340`   | int    | Render-dirty marker (set to 2 after WM_MOVE/SIZE)      |
| `DAT_004df290`   | int    | ??? gate, suppresses redraw when ==1                   |
| `DAT_005aeb68`   | int    | Deferred-shutdown state (WM_CLOSE sets 3, escalator)   |
| `DAT_0052af94`   | int    | Engine state machine (3=ingame, 10=save, 0xb=load)     |
| `DAT_004eac50`   | char   | Current screen ID (0x09='\t', 0x20=' ', 0x22='"', …)   |
| `DAT_0053f228`   | int    | Fullscreen flag (1=fullscreen, 0=windowed)             |
| `DAT_0053f684`   | int    | "Debug features enabled" gate (F3/F4/F6/F7 hotkeys)    |
| `DAT_00553030`   | int    | Multiplayer active                                     |
| `DAT_004df3a8`   | int    | Ctrl held                                              |
| `DAT_00553e68`   | int    | Some 0..80 slider (volume? scrollrate?)                |
| `DAT_00568960`   | int    | F2-cycled 0/1/2 tri-state                              |
| `DAT_00553e60`   | int    | F2 toggle (only when screen='\t')                      |
| `DAT_0055402c`   | int    | F3 toggle (debug)                                      |
| `DAT_0052eff8`   | int    | F9 toggle (screen=' ' + MP)                            |
| `DAT_00553018`   | int    | F12 latch                                              |
| `DAT_0057a0cc`   | int    | Some timer countdown                                   |
| `DAT_0057c8fc`   | int    | Sub-screen state (0=normal, 2=alt UI)                  |
| `DAT_0057cb18`   | int    | Sub-state used by F12                                  |
| `DAT_004eabd0`   | *      | Primary IDirectDrawSurface                             |
| `DAT_004eafac`   | *      | Back   IDirectDrawSurface                              |
| `DAT_004e6974`   | *      | IDirectDraw                                            |
| `DAT_004df288`   | *      | IDirectDrawPalette                                     |
| `DAT_00516a4c`   | int    | Smacker-active flag (read by `FUN_0042e26f`)           |
| `DAT_004d4d58`   | *      | Smacker handle                                         |
| `DAT_004e2a2c`   | *      | IDirectPlay4                                           |
| `DAT_004e2a30`   | *      | Custom DPlay transport (RGM_SIGS)                      |
| `DAT_004d4760`   | *      | IDirectSound                                           |
| `DAT_00522b58`   | uint32 | DSound Release() return value (stashed for log/debug)  |

## Logged strings (`FUN_004afab9` first-arg = `printf`-style fmt)

| Address      | String                              | Where logged                     |
|--------------|-------------------------------------|----------------------------------|
| `0x4df648`   | `OK :Paused.`                       | `FUN_004b26b8` (minimize)        |
| `0x4df658`   | `OK :Unpaused.`                     | `FUN_004b26f8` (regain focus)    |
| `0x4df668`   | `ERR:Can't restore front buffer.`   | `FUN_004b2730` (DDERR_SURFACELOST)|
| `0x4df68c`   | `ERR:Can't restore back buffer.`    | `FUN_004b2730`                   |
| `0x4df73c`   | `OK :Minimizing.`                   | WM_MOVE/SIZE iconic branch       |
| `0x4df750`   | `OK :Window moved.`                 | WM_MOVE/SIZE tail                |
| `0x4df764`   | `OK :Active.`                       | WM_ACTIVATEAPP true              |
| `0x4df774`   | `OK :Not active.`                   | WM_ACTIVATEAPP false             |
| `0x4df788`   | `ERR:Re-initializing direct draw.`  | WM_ACTIVATEAPP surface-lost path |
| `0x4df7ac`   | `l2help.hlp`                        | VK_F1 → WinHelpA                 |

## Implications for the drop-in port

1. **Structural messages first.**  WM_PAINT (without Smacker),
   WM_DESTROY (PostQuitMessage only — no subsystem teardown until
   those subsystems exist), WM_MOVE/SIZE (client-rect cache,
   focus toggle, "OK :Window moved." log), WM_ACTIVATEAPP (focus
   toggle + log), WM_DISPLAYCHANGE (cached metrics), WM_CLOSE
   (set the deferred-shutdown flag; today our skeleton has no
   main-loop escalator, so we also DestroyWindow() inline as a
   fallback until the sim/render driver lands).

2. **Mouse-state mirror.**  Maintain `g_mouse_x/y/buttons/dblclk`
   in `src/main.c` exactly matching the four DAT_004eab* globals,
   so when we later port engine code that reads them the rename is
   one global-replace.

3. **Hotkeys deferred.**  Every WM_KEYDOWN case calls into a
   game-state function we haven't ported.  Skeleton port should
   handle only:
   - VK_ESCAPE → request shutdown (drop-in convenience — retail
     posts a menu first, but we have no menu).
   - VK_F1 → WinHelpA on `l2help.hlp` if it exists alongside the
     exe.  Match retail behaviour exactly (it's harmless and
     verifies the help file shipping).
   - VK_F5 → recentre to 640×480 (cheap, useful, matches retail).

4. **Globals as `static` in main.c for now.**  Once we port the
   sim/render driver and the screen-state code, they migrate to
   their natural homes — but we don't want a sprawling globals
   header before we have multiple TUs.

5. **Logger placeholder.**  Retail's `FUN_004afab9` writes to a
   logfile.  For the drop-in, route the same strings through
   `log_line(...)` so the smoke-test transcripts mirror retail's
   activity (and once we trace retail under Frida, we can diff).
