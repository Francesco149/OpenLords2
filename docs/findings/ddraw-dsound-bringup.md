# DDraw + DSound bring-up

The video and audio subsystem initialisation chain, called once from
`FUN_004b2258` (InitInstance) → `FUN_004b2922` (gfx) and once from
`FUN_0040e9ab` (WinMain) → `FUN_00425b10` (sfx).  These are the calls
the Frida harness's `force-windowed` / `dsound-coop` / `mute` hooks
intercept.

All addresses are preferred-base (ImageBase `0x00400000`).

## Decoded log-prefix convention

The engine writes a one-line status to its log file on every init
step.  Strings are interned at `0x4d47..` (DSound) and `0x4d51..-0x4d54..`
(DDraw).  Prefix `ERR:` = failure path; `OK :` = success.  The
harness can sniff these via `FUN_004afab9(fmt, 0, arg)` for a cheap
oracle of what the engine is doing.

## DDraw bring-up — `FUN_004b2922` → `FUN_004b284e` → `FUN_0042e5c0` + `FUN_0042ec91`

```
FUN_004b2922 @ 0x4b2922       (156 B — orchestrator)
   ↓ FUN_004b28cd(0)          teardown any prior DDraw state
   ↓ FUN_004b284e             two-step init, returns 0/1
   │   ↓ FUN_0042e5c0         DirectDrawCreate + SetCooperativeLevel
   │   │                      + SetDisplayMode  (1249 B)
   │   ↓ FUN_0042ec91         CreateSurface (primary + back + clipper)
   │                          + palette buffer locking  (1207 B)
   ↓ FUN_0042eb7c             initial Blt (DDBLT_COLORFILL|DDBLT_WAIT)
   ↓ FUN_004af8d9("base01.256", &DAT_004ea8a0, 0x300, 0)
   │                          read 768 B (= 256 × RGB) palette
   ↓ FUN_004b0ab5(0x4ea8a0)   apply palette
   ↓ FUN_0045240a             flip / present (33 B — tiny helper)
   ↓ DAT_0057d340 = 2         state-machine: "running"
```

### `FUN_0042e5c0` — DirectDraw create & mode-set

```c
hdc       = GetDC(NULL);
desktop_bpp = GetDeviceCaps(hdc, BITSPIXEL);            // 0x18
desktop_w   = GetDeviceCaps(hdc, HORZRES);              // 0x0c
desktop_h   = GetDeviceCaps(hdc, VERTRES);              // 0x0e
DAT_0053f22c = desktop_w * desktop_h;                   // ??? stored, but only as bpp-marker below
if (desktop_bpp == -1) DAT_0053f22c = 16;
ReleaseDC(NULL, hdc);

// "fullscreen flag" auto-promote: if the desktop isn't 8bpp, we must
// take exclusive fullscreen to flip the bit-depth.
if (DAT_0053f228 == 0 && DAT_0053f22c != 8) DAT_0053f228 = 1;
if (DAT_0053f228 != 0) GetPaletteEntries(NULL, 0, 256, DAT_00515d90);

DAT_004e65a0 = GetSystemMetrics(SM_CXSCREEN);
DAT_004ea838 = GetSystemMetrics(SM_CYSCREEN);

// idempotent: re-uses cached IDirectDraw* if already created
if (DAT_004e6974 == NULL)
    rv = DirectDrawCreate(NULL, &lpDD, NULL);

if (DAT_004e6974 == NULL) {
    if (DAT_0053f228 == 0)
        rv = lpDD->lpVtbl->SetCooperativeLevel(lpDD, g_main_hwnd, 8);    // DDSCL_NORMAL
    else
        rv = lpDD->lpVtbl->SetCooperativeLevel(lpDD, g_main_hwnd, 0x13); // EXCL|FULLSCREEN|ALLOWREBOOT
    // "OK :DD SetCooperative Level." / "ERR:DD Not setCooperative Level :"
}

if (DAT_0053f228 == 0) {
    // WINDOWED PATH — strip WS_POPUP, add overlapped frame, resize/center
    style = GetWindowLong(hwnd, GWL_STYLE);
    SetWindowLong(hwnd, GWL_STYLE, (style & 0x7fffffff) | 0xcf0000);
    //                              ^^^^^^^^^^^ ^^^^^^^^
    //                              clears WS_POPUP   WS_CAPTION|WS_BORDER|WS_DLGFRAME|
    //                                                WS_SYSMENU|WS_THICKFRAME|
    //                                                WS_MINIMIZEBOX|WS_MAXIMIZEBOX
    SetRect(&r, 0, 0, DAT_004eb274, DAT_004eac4c);   // render w, h
    AdjustWindowRectEx(&r, style, has_menu, exstyle);
    // center against desktop using cached SM_CXSCREEN/SM_CYSCREEN
    DAT_004eabb8 = (DAT_004e65a0 - 640) / 2;
    DAT_004eabb0 = (DAT_004ea838 - 480) / 2;
    SetWindowPos(hwnd, NULL, 0, 0, w, h, 0x16);      // SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE
    SetWindowPos(hwnd, HWND_NOTOPMOST, …, 0x13);     // SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE
    SystemParametersInfoA(SPI_GETWORKAREA=0x30, …);
    // clip top-left to workarea, then SetWindowPos to the centred coords
    // "OK :DD Kept resolution."
} else {
    // FULLSCREEN PATH — strip frame, set WS_POPUP|WS_VISIBLE, mode-set
    style = GetWindowLong(hwnd, GWL_STYLE);
    SetWindowLong(hwnd, GWL_STYLE, (style & 0xff3bffff) | 0x90000000);
    //                                                    ^ WS_POPUP|WS_VISIBLE
    rv = lpDD->lpVtbl->SetDisplayMode(lpDD, render_w, render_h, 8);  // vtbl+0x54
    // "OK :DD Set resolution." / "ERR:DD SetMode failed err :"
}

// drain pending messages, then cache IDirectDraw* globally
while (PeekMessage(&msg, NULL, 0, 0, 0)) { GetMessage…TranslateMessage…DispatchMessage }
DAT_004e6974 = lpDD;
```

**Notable:** the GOG-default path on a modern desktop **always**
auto-promotes to fullscreen because the host is 32bpp.  The 8bpp
windowed path is essentially dead code for current users.

### `FUN_0042ec91` — surface + clipper creation

Splits on `DAT_0053f228` again, but the *shape* is the same in both
branches.  Net effect:

| Global         | Meaning                                                   |
|----------------|-----------------------------------------------------------|
| `DAT_004e6974` | `IDirectDraw*`                                            |
| `DAT_004eabd0` | primary surface (`IDirectDrawSurface*`)                   |
| `DAT_004eafac` | offscreen / back buffer (`IDirectDrawSurface*`)           |
| `DAT_004ea12c` | clipper (`IDirectDrawClipper*`, windowed only)            |
| `DAT_004df288` | palette (`IDirectDrawPalette*`)                           |
| `DAT_004ea1a8` | `lPitch` of back buffer (cached from `Lock` then `Unlock`)|
| `DAT_004df28c` | "primary surface lockable" flag (set if `Lock` succeeded) |

Vtable offsets used:

- `+0x08` Release (every interface)
- `+0x10` `IDirectDraw::CreateClipper`
- `+0x14` `IDirectDraw::CreatePalette` (used by `FUN_0042f1df`)
- `+0x18` `IDirectDraw::CreateSurface`
- `+0x20` `IDirectDrawClipper::SetHWnd`
- `+0x50` `IDirectDraw::SetCooperativeLevel`
- `+0x54` `IDirectDraw::SetDisplayMode`
- `+0x70` `IDirectDrawSurface::SetClipper`
- `+0x7c` `IDirectDrawSurface::SetPalette`
- `+0x18` (on palette) `IDirectDrawPalette::SetEntries`
- `+0x14` (on surface) `IDirectDrawSurface::Blt` (used for COLORFILL)

The `0x6c`-byte struct memset-zeroed at start is `DDSURFACEDESC2`
(`dwSize=0x6c`).  Field `+4 = dwFlags = 1 = DDSD_CAPS`, field
`+0x6c-0x4c = 0x200 = DDSCAPS_PRIMARYSURFACE`.

After CreateSurface, the code does `Lock(NULL, &desc, 1=DDLOCK_WAIT, NULL)`
to probe lockability, immediately `Unlock`s, and uses the
`lPitch`-via-FUN_0042f60e mapping to translate the lock error code
into a numeric code logged via `0x4d53ac` / `0x4d52cc`.

The "primary surface lockable but not going to" log line is exactly
what it says — the engine *can* lock the primary on this hardware but
chooses to always render via the back buffer + Blt-flip path.

### Palette setup — `FUN_0042f1df`, `FUN_0042f306`, `FUN_0042f449`

Called from `FUN_004b0ab5`.  Creates a 256-entry
`IDirectDrawPalette` with flags `0x44 = DDPCAPS_8BIT |
DDPCAPS_ALLOW256`, stores it at `DAT_004df288`, attaches it to the
primary surface via `SetPalette`.

The fullscreen branch uses `DAT_005165b0` (BGRA quadruplets) as the
palette buffer; the windowed branch uses `DAT_00515d90`.  The 8-bit
RGB triplets at `DAT_004ea8a0` (loaded from `base01.256`) are
expanded into the BGRA buffer with `peFlags=PC_RESERVED (4)` for
entries 1-254 and `peFlags=0` for the boundary entries (0, 255 and
sometimes 10/0xf6 — the Windows reserved palette slots).

## DSound bring-up — `FUN_00425b10`

Called from WinMain (`FUN_0040e9ab`) *after* `FUN_004b2258` returns.

```c
if (DAT_004d4760 == NULL) {
    rv = DirectSoundCreate(NULL, &lpDS, NULL);
    if (rv != 0) {
        FUN_00427d72(rv);                              // hresult → code
        // "ERR:DS failed open <code>"
        return 0;
    }
    FUN_00425ded();                                    // (post-create init?)
    desc.dwSize          = 0x14;                       // DSBUFFERDESC.dwSize
    desc.dwFlags         = 1;                          // DSBCAPS_PRIMARYBUFFER
    desc.dwBufferBytes   = 0;
    desc.lpwfxFormat     = NULL;
    rv = lpDS->lpVtbl->SetCooperativeLevel(lpDS, g_main_hwnd, 2);  // DSSCL_PRIORITY
    // "ERR:DS failed set co-op <code>"
    rv = lpDS->lpVtbl->CreateSoundBuffer(lpDS, &desc, &primary, NULL);
    // "ERR:DS failed setup primary buffer <code>"
    rv = primary->lpVtbl->SetFormat(primary, &wfx_template);
    // "ERR:DS failed to change primary buffer format <code>"
    // zero out 0x14 voice slots, 2 stream slots, 2 (?) slots
    // "OK :DS Installed."
}
```

Vtable offsets used:

| Iface              | Offset | Method                                |
|--------------------|--------|---------------------------------------|
| `IDirectSound`     | `+0x0C`| `CreateSoundBuffer`                   |
| `IDirectSound`     | `+0x18`| `SetCooperativeLevel`                 |
| `IDirectSoundBuffer`| `+0x38`| `SetFormat`                          |

Globals:

| Address       | Meaning                                          |
|---------------|--------------------------------------------------|
| `DAT_004d4760`| `IDirectSound*`                                  |
| `DAT_00522af0`| primary `IDirectSoundBuffer*`                    |
| `DAT_00522b58`| last HRESULT (sticky during init)                |
| `DAT_00522b90`| `WAVEFORMATEX` template (filled by SetFormat?)   |
| `DAT_00522b00`| 20-slot voice-state array (zeroed at end)        |
| `DAT_00522b50`| 2-slot ??? array (zeroed at end)                 |
| `DAT_00522b80`| 2-slot ??? array (zeroed at end)                 |

The 2-step "create primary + SetFormat" sequence is the standard DX5
pattern for getting a usable primary mixing buffer that doesn't lock
the format to the desktop's 8/11/22.05kHz default.

## Mapping to the Frida harness

The current `tools/frida/openlords2-agent.js` hooks observed:

- `ddraw!IDirectDraw::SetCooperativeLevel` → strip `EXCL|FULLSCREEN`,
  keep `NORMAL|ALLOWREBOOT`.  Lives at `lpDD->lpVtbl[0x50/4]` —
  vtable-indexed inline-hook.
- `ddraw!IDirectDraw::SetDisplayMode` → block 8bpp mode-set.  Lives
  at vtable `+0x54`.
- `dsound!IDirectSound::SetCooperativeLevel` → demote
  `PRIORITY (2)` → `NORMAL (1)`.  Lives at vtable `+0x18`.

All three already match the byte-level patterns in this section, so
the harness validation in PROGRESS.md is consistent with the
decompile.  The `force-windowed` path in the eventual drop-in should
follow the *windowed* branch of `FUN_0042e5c0` exactly — particularly
the SetWindowLong style transformation, since that's the bit that
makes the window survive alt-tab cleanly.

## Open questions for the next session

1. **`FUN_0045240a` (33 B)** — present/flip helper.  What does it
   actually do?  Probably `Blt(primary, NULL, back, NULL, …)` or the
   `Flip` method depending on which path took.  Worth fully decoding
   because it's called from ~50 sites — every UI screen draws via it.
2. **`FUN_00425ded`** — DSound post-create init.  What state does it
   set up that's not in the obvious `CreateSoundBuffer` path?
3. **`FUN_004af8d9(filename, buf, max, off)`** — the universal
   file-read helper.  Tier-1 (open / lseek / read / close at the
   current CWD) is ported as `io_read_file_at` in `src/io.c`;
   tiers 2 and 3 are the chdir-fallback retry chain documented
   below.
4. **`DAT_0053f22c`** is initialised to `w * h` then overwritten with
   bpp under one branch — looks like a Ghidra/MSVC artefact where
   the same stack slot was reused.  Investigate when porting.

## Asset-load IO helper family — `0x4af116..0x4afeed`

A self-contained cluster of file-handling helpers wraps every
disk-touching call in the engine.  All address-band reads/writes
go through this family; the tier-1 single-open shape is ported
(`src/io.c::io_read_file_at` + the extension helpers in
`src/strutil.c`); the multi-tier retry chain and the file-write /
existence helpers are deferred until they have a consumer.

```
FUN_004af116    508 B    tier-3 chdir into ABSOLUTE-path subdir   DEFERRED
FUN_004af312    496 B    tier-2 chdir into base-relative subdir   DEFERRED
FUN_004af502     72 B    chdir back to base (DAT_0058fe30)        DEFERRED (no consumer)
FUN_004af54a    112 B    save first '.' extension to scratch      PORTED  strutil_ext_save
FUN_004af5ba    114 B    apply scratch into first-'.' slot        PORTED  strutil_ext_apply
FUN_004af62c     73 B    strip first-'.' extension                PORTED  strutil_ext_strip
FUN_004af675     84 B    append '.' + scratch + NUL after NUL     PORTED  strutil_ext_append
FUN_004af6c9     67 B    1-based offset of first '.' (or len)     PORTED  strutil_ext_length
FUN_004af70c     91 B    file length (uses af502 then __open)     DEFERRED (no consumer)
FUN_004af767    218 B    file-exists (uses retry chain)           DEFERRED
FUN_004af841    152 B    file-exists with tier indicator (0/1/2)  DEFERRED
FUN_004af8d9    381 B    universal read with 3-tier retry         PORTED  io_read_file_at (tier 1)
FUN_004afa56     99 B    file write (create + truncate + write)   DEFERRED
FUN_004afab9    897 B    status.txt logger (formats + writes)     DEFERRED
FUN_004afe3a    179 B    status.txt low-level writer              DEFERRED
FUN_004afeed   1214 B    .ini parser? (largest sibling)           SURVEY PENDING
```

### Tier-2 retry — `FUN_004af312` (base-relative chdir)

Extracted extension (uppercased via `strutil_to_upper_inplace`)
is matched against `{ "PL8", "256", "WAV", "SMK" }`.  If matched,
the working buffer at `DAT_005cd420` is seeded with the base path
`DAT_0058fe30` (cwd captured at WinMain), then `FUN_004c2090`
(MSVC strcat) appends one of:

| Extension | Suffix appended       | RVA of suffix         |
|-----------|-----------------------|-----------------------|
| `PL8`     | `\pl8`                | `0x4df46c`            |
| `256`     | `\pl8`                | `0x4df478`            |
| `WAV`     | `\wav`                | `0x4df484`            |
| `SMK`     | `\smk_low`            | `0x4df490` (`DAT_0053f5f4 == 1`) |
| `SMK`     | `\smk_high`           | `0x4df49c` (otherwise) |

Then `__chdir(&DAT_005cd420)`.  Subsequent file open succeeds if
the asset lives in `<base>\pl8\` / `<base>\wav\` / `<base>\smk_*\`.

### Tier-3 retry — `FUN_004af116` (absolute-path chdir with drive substitution)

Same extension match (`{ "PL8", "256", "WAV", "SMK" }`).  Zeroes
40 bytes at `DAT_005cd490`, then `FUN_00401136`
(`strutil_bounded_copy`) copies 30 bytes of one of these
placeholder absolute paths:

| Extension | Placeholder path            | RVA of path |
|-----------|-----------------------------|-------------|
| `PL8`     | `c:.\pl8`                   | `0x4df3f4`  |
| `256`     | `c:.\pl8`                   | `0x4df408`  |
| `WAV`     | `c:.\wav`                   | `0x4df41c`  |
| `SMK`     | `c:.\smk_high` (default)    | `0x4df430`  |
| `SMK`     | `c:.\smk_low` (`DAT_0053f5f4 == 1`) | `0x4df444` |

Then `DAT_005cd490 = DAT_0053f200` overwrites the first 4 bytes
of the buffer.  `DAT_0053f200` is the install drive prefix
captured at boot (4 bytes — typically something like `"C:"` + the
drive letter substitution).  So the literal `c:` in the .rdata
strings is a placeholder pattern that gets rewritten with the
detected install drive at runtime.

### Why tier-2/3 are deferred

Today's drop-in install layout is FLAT — `vendor/original/` is one
directory with `Castle.pl8`, `base01.256`, `Intro.smk`, etc. all
co-located.  `init_exe_dir` chdirs there at startup, so tier-1
finds every asset on the first try.  The retry chain would only
fire if assets were partitioned into `pl8/`, `wav/`, `smk_low/`
etc. subdirs — which retail's CD-install layout did but the GOG
re-pack does not.

Landing tier-2/tier-3 today would:
1. Add ~1 KB of subdirectory chdir / extension-routing code
   that no test exercises (io.c is Win32-only).
2. Need a producer to populate `DAT_0058fe30` (cwd-at-boot
   capture) and `DAT_0053f200` (install drive prefix) — both
   trivial but they have to land somewhere.
3. Have no observable effect on either of today's GOG install
   nor any test scenario.

The tier-2/3 helpers land naturally alongside whichever future
chip first encounters a "file not found at CWD" path — likely the
intro Smacker subsystem, since `.smk` files in retail's tree-style
install would otherwise sit one directory away.

> 📍  Sources: `docs/decompiled/all.c` lines 81693 (FUN_004af116),
> 81765 (FUN_004af312), 81836 (FUN_004af502), 81852-81962
> (extension helper quintet), 82047 (FUN_004af8d9), 7592
> (WinMain cwd-capture into `DAT_0058fe30`).  RVA-to-string map
> verified via `r2 -q -c 'px 192 @ 0x4df3e0' vendor/original/Lords2.exe`.

> 📍  Sources: `docs/decompiled/all.c` lines 83849 (FUN_004b284e),
> 83895 (FUN_004b2922), 19095 (FUN_0042e5c0), 19273 (FUN_0042ec91),
> 19238 (FUN_0042eb7c), 15217 (FUN_00425b10).
