# DDraw frame capture

How the Frida harness grabs the retail engine's back buffer and turns
it into a viewable BMP — Phase 2 of `docs/harness-roadmap.md`.

This is much simpler than the analogous path in the sibling projects
because Lords II uses 8-bpp paletted DDraw 1.x surfaces in system
memory: there is no Direct3D bounce, no `CopyRects`, no
`CreateImageSurface` dance — the back is directly lockable, and the
palette is reachable via three known BSS pointers.  Compare
`openrecet`'s `captureBackbuffer()` (D3D8) which has to walk a
sysmem-bounce surface because its retail back buffer isn't lockable.

All addresses below are preferred ImageBase (`0x00400000`) and
resolved at hook time via `rva()`.

## What we read

Three engine globals, all populated by `FUN_0042ec91` (CreateSurface
chain) — see `ddraw-dsound-bringup.md`:

| Address       | Type                      | Meaning                  |
|---------------|---------------------------|--------------------------|
| `0x004eabd0`  | `IDirectDrawSurface*`     | primary surface          |
| `0x004eafac`  | `IDirectDrawSurface*`     | back surface (sys-mem)   |
| `0x004df288`  | `IDirectDrawPalette*`     | 256-entry palette        |

We don't hook `CreateSurface` to discover these — three pointer reads
is cheaper than vtable interception, and the engine never recreates
these mid-run.  We do gate every capture on all three being non-NULL
(early ticks before DDraw bring-up finishes will read zeros).

## Vtable slots used

| Interface             | Slot | Method        | Notes                       |
|-----------------------|------|---------------|-----------------------------|
| `IDirectDrawSurface`  | 25   | `Lock`        | byte offset `0x64`          |
| `IDirectDrawSurface`  | 32   | `Unlock`      | byte offset `0x80`          |
| `IDirectDrawPalette`  | 4    | `GetEntries`  | byte offset `0x10`          |

The vtable layout is documented in `ddraw-dsound-bringup.md`; these
positions are stable across DDraw 1..7.

`Lock` signature (stdcall):

    HRESULT Lock(this, LPRECT dest, LPDDSURFACEDESC desc,
                 DWORD flags, HANDLE evt)

We pass `dest=NULL` for the full surface, `flags = DDLOCK_WAIT |
DDLOCK_READONLY` so we don't contend with the engine and the driver
doesn't think we'll mutate the surface.

`GetEntries(this, flags=0, start=0, count=256, out)` writes 256
`PALETTEENTRY` records (4 bytes each: peRed, peGreen, peBlue,
peFlags) into the supplied buffer.

## DDSURFACEDESC layout (108 B, DDraw1)

Only the offsets we touch:

| Offset | Field        | Type       |
|--------|--------------|------------|
| `+0`   | `dwSize`     | `DWORD`    | must be set to `108` before Lock |
| `+8`   | `dwHeight`   | `DWORD`    |
| `+12`  | `dwWidth`    | `DWORD`    |
| `+16`  | `lPitch`     | `LONG`     | stride in bytes (≥ width)        |
| `+36`  | `lpSurface`  | `LPVOID`   | filled by Lock                   |

The buffer is `Memory.alloc(108)`; only `dwSize` needs to be set
pre-Lock (the rest are out-params).

## Capture loop

Triggered from `engine_tick.onLeave` whenever the engine's FPS
cumulative-seconds counter (`DAT_004ea83c`) rolls over.  At default
turbo step (17 ms/tick) that's ~500 captures per real second; the
`frame_capture_max` knob caps the run, and `frame_capture_warmup_secs`
skips the first N rollovers so the engine has time to render past the
uninitialised-back-buffer phase (default warmup = 5).

The captured payload sent over `send(json, ab)`:

    [u32 width][u32 height][u32 frame_idx]   header  (12 B)
    [256 × PALETTEENTRY = 1024 B]            palette
    [width * height bytes]                   tight-packed indices

The Python side (`tools/frida_capture.py:_write_paletted_bmp`)
unpacks this into a 24-bpp top-down BMP under `runs/.../frames/
frame_NNNNN.bmp`.  Same on-disk shape as
`src/gfx.c:gfx_capture_backbuffer` so output is interchangeable for
comparison diffs.

## Why not capture every Blt?

Two reasons.

1. **Cadence.**  In turbo mode the engine ticks ~500×/real-second.
   Catching every Blt would mean ~500 BMPs/second (hundreds of MB per
   real second on disk), and most of those are duplicates of the
   previous frame's content.  Riding the cum-secs heartbeat gives us
   ~1 capture per virtual second, naturally sparse.
2. **Simplicity.**  Hooking Blt means vtable-patching every surface
   the engine creates, plus identifying which Blts are "the present"
   versus internal back-buffer scratch.  Reading three globals and
   calling Lock from the engine_tick.onLeave context is half a page
   of code and has no surface-identity problem.

## Verified

Smoke run 2026-05-24 (10 s wall-clock, turbo on, warmup 10, max 8):

- `frame_capture_armed` ✓
- 8 BMPs landed under `runs/capture/<ts>/frames/`
- All 640×480, 24-bpp top-down, ~921 KB each
- Non-zero pixel coverage ~26%, 21–55 unique byte values per frame
- Mid-rows of late-stage frames showed real palette content (the
  Sierra logo screen's dark grays + browns), confirming the palette
  conversion + row-pitch unpack are correct.

## What the harness CLI looks like

    nix develop -c python3 tools/frida_capture.py \
        --turbo --frame-capture --frame-capture-max 8 \
        --frame-capture-warmup-secs 10 \
        --duration-ms 10000

…produces `runs/capture/<ts>/frames/frame_00000.bmp` …
`frame_00007.bmp` plus the usual `agent.log` + `run.json`.

Smoke equivalent (with assertion that ≥ 1 BMP lands):

    nix develop -c python3 tools/smoke-test.py \
        --turbo --frame-capture --duration-ms 6000
