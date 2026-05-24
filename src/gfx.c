/*
 * OpenLords2 — DDraw bring-up (port of retail FUN_004b2922 chain).
 *
 * Call shape mirrored from `docs/findings/ddraw-dsound-bringup.md`:
 *
 *     gfx_init(hwnd, fs)
 *        ├─ FUN_0042e5c0  DirectDrawCreate + SetCooperativeLevel
 *        │                + (fs ? SetDisplayMode : window-style fixup)
 *        ├─ FUN_0042ec91  CreateSurface primary + back + clipper
 *        ├─ FUN_0042eb7c  initial Blt(COLORFILL=black, DDBLT_WAIT)
 *        └─ DAT_0057d340 = 2     ("ready to render" marker — recorded
 *                                 in main.c's g_render_dirty)
 *
 * Differences from retail held to a minimum and listed below:
 *
 *   1. We DON'T auto-promote to fullscreen on a non-8bpp desktop —
 *      retail does this so a single SetDisplayMode flips an LCD
 *      desktop into the engine's native palette.  We default to
 *      windowed and let the caller opt into fullscreen via the
 *      `fullscreen` arg.  Aligns with the project_drop_in_defaults
 *      memory.
 *
 *   2. The eight extra `SetWindowPos` calls retail uses to centre
 *      the windowed-mode frame against the work area are collapsed
 *      into one — main.c's `create_main_window` already centres the
 *      window when it's first created, and a redundant re-centre at
 *      DDraw init time isn't worth the duplication.
 *
 *   3. Palette load is deferred — gfx_init does NOT touch
 *      DAT_004df288 / base01.256 / FUN_004b0ab5.  See
 *      docs/findings/ddraw-dsound-bringup.md for the palette path.
 *      Without a palette, the primary surface in windowed 8bpp will
 *      render with the OS's logical default palette — fine for a
 *      bring-up smoke run but not visually correct.  Subsequent
 *      slice lands the palette.
 *
 * Vtable indices used (DX3-era DDraw — DDSURFACEDESC dwSize == 0x6c
 * matches DX3+):
 *
 *   IDirectDraw          +0x10  CreateClipper
 *                        +0x14  CreatePalette
 *                        +0x18  CreateSurface
 *                        +0x50  SetCooperativeLevel
 *                        +0x54  SetDisplayMode
 *   IDirectDrawSurface   +0x14  Blt
 *                        +0x60  IsLost
 *                        +0x64  Lock
 *                        +0x6c  Restore
 *                        +0x70  SetClipper
 *                        +0x7c  SetPalette
 *                        +0x80  Unlock
 *   IDirectDrawClipper   +0x20  SetHWnd
 *
 * The mingw32 ddraw.h exposes these via `IDirectDraw_Method(this,...)`
 * macros which call through `lpVtbl` for us — but our COM-aware code
 * is the only consumer, so we use the macros instead of raw vtbl
 * indexing.  Less brittle if mingw ever picks up a newer ddraw.h.
 */

#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0300        /* keep DDSURFACEDESC at 0x6c */
#define COBJMACROS

#include <windows.h>
#include <ddraw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "io.h"

/* Logger lives in main.c — declared here as a weak local prototype
 * so we don't drag dev_hooks.h into this TU. */
extern void log_line(const char *fmt, ...);

/* ---------------------------------------------------------------- *
 *  Global DDraw state mirrors retail's DAT_* globals one-for-one    *
 *  so future ports of code that reads them is a global-replace.     *
 * ---------------------------------------------------------------- */

static LPDIRECTDRAW         g_dd;             /* DAT_004e6974 */
static LPDIRECTDRAWSURFACE  g_primary;        /* DAT_004eabd0 */
static LPDIRECTDRAWSURFACE  g_back;           /* DAT_004eafac */
static LPDIRECTDRAWCLIPPER  g_clipper;        /* DAT_004ea12c */
static LPDIRECTDRAWPALETTE  g_palette;        /* DAT_004df288 */
static int                  g_back_pitch;     /* DAT_004ea1a8 */
static int                  g_primary_lockable; /* DAT_004df28c */
static int                  g_fullscreen;
static HWND                 g_hwnd;

/* Loaded palette RGB triplets (×4-expanded from 6-bit VGA to 8-bit).
 * Mirror of retail's DAT_004ea1b0[256*3] — held in this TU so the
 * surface-paint helpers (which read it for fallback fills) can
 * eventually share. */
static uint8_t g_palette_rgb[256 * 3];

/* ---------------------------------------------------------------- */

static int do_create_dd_and_coop(void);
static int do_create_surfaces(void);
static int do_initial_blt(void);

/* Black-fill helper (COLORFILL/Blt) for back + primary at startup.
 * Mirrors FUN_0042eb7c. */
static int blt_color_fill(LPDIRECTDRAWSURFACE surf, DWORD color, LPRECT dst);

/* ---------------------------------------------------------------- */

int gfx_init(HWND hwnd, int fullscreen)
{
    if (!hwnd) return 0;
    if (g_dd) {
        /* Already initialised — retail's bring-up is idempotent via
         * a separate `FUN_004b28cd(0)` teardown call.  We require an
         * explicit gfx_shutdown() before re-init. */
        log_line("gfx_init: already initialised, refusing re-entry");
        return 0;
    }

    g_hwnd       = hwnd;
    g_fullscreen = fullscreen;

    if (!do_create_dd_and_coop()) {
        gfx_shutdown();
        return 0;
    }

    if (!do_create_surfaces()) {
        gfx_shutdown();
        return 0;
    }

    if (!do_initial_blt()) {
        gfx_shutdown();
        return 0;
    }

    /* Palette load is best-effort — when running from /tmp (smoke
     * harness) base01.256 isn't reachable.  Continue without if it
     * fails; the primary still presents in the OS default palette. */
    if (!gfx_load_palette("base01.256")) {
        log_line("gfx_init: palette load skipped (base01.256 not "
                 "readable from CWD — drop-in needs to live next to "
                 "retail's data files)");
    }

    /* Bring-up test pattern — fill the back buffer with palette
     * index 1 so the next gfx_present makes the visible window
     * the colour of base01.256[1] (and stays black if no palette
     * was loaded — entry-0 boundary is forced black).  This is the
     * cheapest sanity test that the present pipeline + palette
     * upload are end-to-end correct. */
    gfx_fill_back_index(1);

    log_line("OK :GFX DirectDraw enabled.");
    return 1;
}

void gfx_shutdown(void)
{
    /* Reverse order of creation.  Mirrors FUN_004b28cd cleanup. */
    if (g_palette) {
        IDirectDrawPalette_Release(g_palette);
        g_palette = NULL;
    }
    if (g_clipper) {
        IDirectDrawClipper_Release(g_clipper);
        g_clipper = NULL;
    }
    if (g_back) {
        IDirectDrawSurface_Release(g_back);
        g_back = NULL;
    }
    if (g_primary) {
        IDirectDrawSurface_Release(g_primary);
        g_primary = NULL;
    }
    if (g_dd) {
        IDirectDraw_Release(g_dd);
        g_dd = NULL;
    }
    g_back_pitch = 0;
    g_primary_lockable = 0;
    g_hwnd = NULL;
}

/* ---------------------------------------------------------------- *
 *  Palette load — ports retail's `FUN_004af8d9("base01.256",        *
 *  &DAT_004ea8a0, 0x300, 0)` + `FUN_004b0ab5` (×4 expand) +         *
 *  `FUN_0042f1df`/`FUN_0042f449`/`FUN_0042f306` (build              *
 *  PALETTEENTRY[256] + CreatePalette + SetPalette).                 *
 *                                                                    *
 *  Simplifications relative to retail:                              *
 *                                                                    *
 *  - We don't preserve OS-reserved palette slots (0..9, 246..255).  *
 *    Retail does this to play nice with the desktop palette         *
 *    manager when running 8bpp on an 8bpp desktop.  In windowed     *
 *    mode on a 32bpp host (our typical drop-in scenario) DDraw      *
 *    handles the format conversion, so we're free to take all 256  *
 *    entries.  Entry 0 stays (0,0,0) and entry 255 stays            *
 *    (0xff,0xff,0xff) to match retail's "Windows reserved"          *
 *    boundary contract (the read at 0x4d50f0 / 0x4d512b matches).   *
 *  - No SetSystemPaletteUse — only matters in windowed 8bpp.        *
 * ---------------------------------------------------------------- */
int gfx_load_palette(const char *path)
{
    if (!g_dd || !g_primary) {
        log_line("gfx_load_palette: gfx not initialised");
        return 0;
    }

    uint8_t rgb[256 * 3];
    int n = io_read_file_at(path, rgb, sizeof(rgb), 0);
    if (n != (int)sizeof(rgb)) {
        log_line("gfx_load_palette: read %s got %d/%u",
                 path, n, (unsigned)sizeof(rgb));
        return 0;
    }

    /* ×4 expansion — retail palettes are stored as 6-bit VGA values
     * (range 0..63), so we shift left by 2 to get 8-bit RGB. */
    for (int i = 0; i < 256 * 3; i++) g_palette_rgb[i] = rgb[i] << 2;
    /* Boundary entries — match retail's reads. */
    g_palette_rgb[0] = g_palette_rgb[1] = g_palette_rgb[2] = 0x00;
    g_palette_rgb[255*3+0] = g_palette_rgb[255*3+1] = g_palette_rgb[255*3+2] = 0xff;

    PALETTEENTRY entries[256];
    for (int i = 0; i < 256; i++) {
        entries[i].peRed   = g_palette_rgb[i*3+0];
        entries[i].peGreen = g_palette_rgb[i*3+1];
        entries[i].peBlue  = g_palette_rgb[i*3+2];
        entries[i].peFlags = (i == 0 || i == 255) ? 0 : PC_RESERVED;
    }

    /* Replace any prior palette in-place (retail Releases first). */
    if (g_palette) {
        IDirectDrawPalette_Release(g_palette);
        g_palette = NULL;
    }
    HRESULT hr = IDirectDraw_CreatePalette(
        g_dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, entries, &g_palette, NULL);
    if (FAILED(hr) || !g_palette) {
        log_line("ERR:Palette create failed 0x%08lx", (unsigned long)hr);
        g_palette = NULL;
        return 0;
    }

    /* In fullscreen the primary IS 8bpp after SetDisplayMode, so we
     * attach the palette to it (matches retail FUN_0042f1df).  In
     * windowed mode the primary is the desktop's bit depth (32bpp
     * on any modern host), so an 8bpp palette can't legally attach
     * — DDERR_INVALIDPIXELFORMAT (0x88760091).  We bind the palette
     * to the 8bpp back buffer instead; DDraw applies it during the
     * back→primary format-conversion Blt in gfx_present.
     *
     * This is a deliberate divergence from retail: retail never
     * actually exercises the windowed-on-32bpp regime because its
     * auto-fullscreen-promote in FUN_0042e5c0 kicks in any time the
     * desktop is non-8bpp.  We default-windowed (drop_in_defaults
     * memory) and need this split to make palette work in our
     * default regime. */
    LPDIRECTDRAWSURFACE target = g_fullscreen ? g_primary : g_back;
    hr = IDirectDrawSurface_SetPalette(target, g_palette);
    if (FAILED(hr)) {
        log_line("ERR:Palette SetPalette failed 0x%08lx (target=%s)",
                 (unsigned long)hr,
                 g_fullscreen ? "primary" : "back");
        IDirectDrawPalette_Release(g_palette);
        g_palette = NULL;
        return 0;
    }
    log_line("OK :Palette installed (%s, %s).", path,
             g_fullscreen ? "primary" : "back");
    return 1;
}

/* ---------------------------------------------------------------- *
 *  Step 1 — `FUN_0042e5c0`                                           *
 *           DirectDrawCreate + SetCooperativeLevel                  *
 *           + (fullscreen: SetDisplayMode | windowed: style fixup)  *
 * ---------------------------------------------------------------- */
static int do_create_dd_and_coop(void)
{
    HRESULT hr = DirectDrawCreate(NULL, &g_dd, NULL);
    if (FAILED(hr) || !g_dd) {
        log_line("ERR:DD Create failed (0x%08lx)", (unsigned long)hr);
        g_dd = NULL;
        return 0;
    }

    DWORD coop = g_fullscreen
        ? (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT)
        :  DDSCL_NORMAL;
    hr = IDirectDraw_SetCooperativeLevel(g_dd, g_hwnd, coop);
    if (FAILED(hr)) {
        log_line("ERR:DD Not setCooperative Level : 0x%08lx",
                 (unsigned long)hr);
        return 0;
    }
    log_line("OK :DD SetCooperative Level.");

    if (g_fullscreen) {
        /* Apply retail's WS_POPUP|WS_VISIBLE style.  Strips the
         * window frame so DDraw owns the entire display. */
        LONG style = GetWindowLongA(g_hwnd, GWL_STYLE);
        SetWindowLongA(g_hwnd, GWL_STYLE,
                       (style & 0xff3bffff) | (LONG)0x90000000);

        hr = IDirectDraw_SetDisplayMode(g_dd,
                                        GFX_RENDER_W, GFX_RENDER_H, 8);
        if (FAILED(hr)) {
            log_line("ERR:DD SetMode failed err : 0x%08lx",
                     (unsigned long)hr);
            return 0;
        }
        log_line("OK :DD Set resolution.");
    } else {
        /* Windowed-mode style fixup — clear WS_POPUP, install
         * WS_OVERLAPPEDWINDOW = 0xcf0000.  Retail does this too, in
         * case the window was created as a fullscreen popup. */
        LONG style = GetWindowLongA(g_hwnd, GWL_STYLE);
        SetWindowLongA(g_hwnd, GWL_STYLE,
                       (style & 0x7fffffff) | (LONG)0xcf0000);
        log_line("OK :DD Kept resolution.");
    }

    /* Drain any messages the SetCooperativeLevel / SetDisplayMode
     * pair pushed — retail does this to keep WM_ACTIVATEAPP etc.
     * from arriving mid-surface-create. */
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE) &&
           GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 1;
}

/* ---------------------------------------------------------------- *
 *  Step 2 — `FUN_0042ec91`                                           *
 *           CreateSurface primary + back + clipper                  *
 * ---------------------------------------------------------------- */
static int do_create_surfaces(void)
{
    DDSURFACEDESC desc;

    /* Primary surface — always DDSD_CAPS + DDSCAPS_PRIMARYSURFACE. */
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize        = sizeof(desc);
    desc.dwFlags       = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    HRESULT hr = IDirectDraw_CreateSurface(g_dd, &desc, &g_primary, NULL);
    if (FAILED(hr) || !g_primary) {
        log_line(g_fullscreen
                 ? "ERR:DD failed Surface create (fullscreen) 0x%08lx"
                 : "ERR:DD failed Surface create (windowed) 0x%08lx",
                 (unsigned long)hr);
        return 0;
    }
    log_line(g_fullscreen ? "OK :DD Creating one Vram only."
                          : "OK :DD Creating one Vram and one system surface.");

    /* Lock-probe — retail uses this to log whether the primary
     * surface is lockable on this hardware, then immediately
     * unlocks.  We mirror the probe but don't log the result code. */
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(g_primary, NULL, &desc, DDLOCK_WAIT, NULL);
    if (SUCCEEDED(hr)) {
        g_primary_lockable = 1;
        IDirectDrawSurface_Unlock(g_primary, NULL);
    } else {
        g_primary_lockable = 0;
    }

    /* Offscreen back buffer in system memory — retail's
     * FUN_0042f148(w, h, 1) — DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH with
     * DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY.
     *
     * Retail relies on the primary's pixel format (auto-promoted to
     * 8bpp in fullscreen via SetDisplayMode) being inherited by the
     * back.  We default to windowed mode where the primary is the
     * desktop's 32bpp — without an explicit DDPF_PALETTEINDEXED8 in
     * the descriptor, the back also gets 32bpp and our 8-bit
     * palette can't attach (DDERR_INVALIDPIXELFORMAT 0x88760091).
     *
     * Always-explicit 8bpp here matches what retail logically wants
     * regardless of cooperative mode. */
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize         = sizeof(desc);
    desc.dwFlags        = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH
                        | DDSD_PIXELFORMAT;
    desc.dwWidth        = GFX_RENDER_W;
    desc.dwHeight       = GFX_RENDER_H;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize        = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags       = DDPF_RGB | DDPF_PALETTEINDEXED8;
    desc.ddpfPixelFormat.dwRGBBitCount = 8;

    hr = IDirectDraw_CreateSurface(g_dd, &desc, &g_back, NULL);
    if (FAILED(hr) || !g_back) {
        log_line("ERR:DD failed offscreen surface create 0x%08lx",
                 (unsigned long)hr);
        return 0;
    }

    /* Probe back-buffer pitch.  Retail's FUN_0042ec91 does this
     * Lock/Unlock dance specifically to read DDSURFACEDESC.lPitch
     * into DAT_004ea1a8 — the render path uses it for fast scanline
     * stride math without re-locking. */
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(g_back, NULL, &desc, DDLOCK_WAIT, NULL);
    if (SUCCEEDED(hr)) {
        g_back_pitch = (int)desc.lPitch;
        IDirectDrawSurface_Unlock(g_back, NULL);
    }

    if (!g_fullscreen) {
        /* Windowed-only: clipper.  Without one, blits to the primary
         * surface bleed across the desktop instead of being clipped
         * to our window's region. */
        hr = IDirectDraw_CreateClipper(g_dd, 0, &g_clipper, NULL);
        if (FAILED(hr) || !g_clipper) {
            log_line("ERR:DD failed clipper create 0x%08lx",
                     (unsigned long)hr);
            return 0;
        }
        hr = IDirectDrawClipper_SetHWnd(g_clipper, 0, g_hwnd);
        if (FAILED(hr)) {
            log_line("ERR:DD failed clipper SetHWnd 0x%08lx",
                     (unsigned long)hr);
            return 0;
        }
        hr = IDirectDrawSurface_SetClipper(g_primary, g_clipper);
        if (FAILED(hr)) {
            log_line("ERR:DD failed surface SetClipper 0x%08lx",
                     (unsigned long)hr);
            return 0;
        }
        log_line("OK :DD Surfaces created and clipper attached.");
    } else {
        log_line("OK :DD Surface created.");
    }
    return 1;
}

/* ---------------------------------------------------------------- *
 *  Step 3 — `FUN_0042eb7c`                                           *
 *           Initial color-fill (black) of back + primary            *
 * ---------------------------------------------------------------- */
static int do_initial_blt(void)
{
    UpdateWindow(g_hwnd);

    /* Retail Blt flags: 0x1000400 = DDBLT_COLORFILL | DDBLT_WAIT.
     * We use the named constants for clarity. */
    if (g_fullscreen) {
        if (!blt_color_fill(g_back, 0, NULL))     return 0;
        if (!blt_color_fill(g_primary, 0, NULL))  return 0;
    } else {
        /* Windowed: blit black into the on-screen client rect.
         * `g_client_screen` from main.c would be the strict mirror,
         * but at init time the engine just sets dest=NULL meaning
         * "full primary" — which the clipper will trim down to our
         * window region for us. */
        if (!blt_color_fill(g_primary, 0, NULL)) return 0;
    }
    return 1;
}

static int blt_color_fill(LPDIRECTDRAWSURFACE surf, DWORD color, LPRECT dst)
{
    DDBLTFX fx;
    ZeroMemory(&fx, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = color;

    HRESULT hr = IDirectDrawSurface_Blt(
        surf, dst, NULL, NULL,
        DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    if (FAILED(hr)) {
        log_line("ERR:DD initial Blt failed 0x%08lx",
                 (unsigned long)hr);
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- *
 *  Present + back-buffer fill — ports of the inner Blt /            *
 *  COLORFILL primitives the engine's render-tail (FUN_00452160)    *
 *  uses each frame.                                                  *
 * ---------------------------------------------------------------- */

void gfx_fill_back_index(uint8_t palette_index)
{
    if (!g_back) return;
    /* For 8-bit DDraw surfaces, DDBLTFX.dwFillColor IS the
     * palette index (low 8 bits).  No need to look up the RGB. */
    (void)blt_color_fill(g_back, palette_index, NULL);
}

/* ---------------------------------------------------------------- *
 *  Back-buffer capture — adapted from openrecet's                    *
 *  capture_backbuffer (which used D3D8 LockRect + 32bpp BMP).  We    *
 *  walk our 8bpp back through `g_palette_rgb` to produce a 24bpp    *
 *  BMP so it's directly viewable without dragging the palette       *
 *  along.  Top-down (negative biHeight) so row 0 is the top scan-   *
 *  line — matches what every viewer expects.                        *
 * ---------------------------------------------------------------- */
int gfx_capture_backbuffer(const char *path)
{
    if (!g_back || !path) return 0;

    DDSURFACEDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    HRESULT hr = IDirectDrawSurface_Lock(g_back, NULL, &desc,
                                         DDLOCK_WAIT | DDLOCK_READONLY,
                                         NULL);
    if (FAILED(hr) || !desc.lpSurface) {
        log_line("gfx_capture: Lock failed 0x%08lx", (unsigned long)hr);
        return 0;
    }

    const int w = (int)desc.dwWidth;
    const int h = (int)desc.dwHeight;
    const int pitch = (int)desc.lPitch;
    /* 24bpp BMP rows are padded to 4-byte boundaries. */
    const int row_bgr   = w * 3;
    const int row_pad   = (4 - (row_bgr & 3)) & 3;
    const int row_total = row_bgr + row_pad;
    const DWORD img_size  = (DWORD)(row_total * h);
    const DWORD file_size = 14 + 40 + img_size;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        log_line("gfx_capture: fopen(%s) failed", path);
        IDirectDrawSurface_Unlock(g_back, NULL);
        return 0;
    }

    /* BITMAPFILEHEADER */
    uint8_t fhdr[14] = {0};
    fhdr[0] = 'B';  fhdr[1] = 'M';
    fhdr[2]  = (uint8_t)(file_size);
    fhdr[3]  = (uint8_t)(file_size >> 8);
    fhdr[4]  = (uint8_t)(file_size >> 16);
    fhdr[5]  = (uint8_t)(file_size >> 24);
    fhdr[10] = 54;  /* bfOffBits = 14 + 40 */
    fwrite(fhdr, 1, 14, fp);

    /* BITMAPINFOHEADER — biHeight negative for top-down. */
    int32_t neg_h = -h;
    uint8_t ihdr[40] = {0};
    ihdr[0] = 40;
    ihdr[4] = (uint8_t)(w);
    ihdr[5] = (uint8_t)(w >> 8);
    ihdr[6] = (uint8_t)(w >> 16);
    ihdr[7] = (uint8_t)(w >> 24);
    ihdr[8]  = (uint8_t)(neg_h);
    ihdr[9]  = (uint8_t)((uint32_t)neg_h >> 8);
    ihdr[10] = (uint8_t)((uint32_t)neg_h >> 16);
    ihdr[11] = (uint8_t)((uint32_t)neg_h >> 24);
    ihdr[12] = 1;        /* biPlanes */
    ihdr[14] = 24;       /* biBitCount */
    ihdr[20] = (uint8_t)(img_size);
    ihdr[21] = (uint8_t)(img_size >> 8);
    ihdr[22] = (uint8_t)(img_size >> 16);
    ihdr[23] = (uint8_t)(img_size >> 24);
    fwrite(ihdr, 1, 40, fp);

    /* Pixel data — palette lookup per pixel, BGR byte order. */
    const uint8_t *base = (const uint8_t *)desc.lpSurface;
    uint8_t row[GFX_RENDER_W * 3 + 4];   /* +4 covers any padding */
    for (int y = 0; y < h; y++) {
        const uint8_t *src = base + y * pitch;
        uint8_t *dst = row;
        for (int x = 0; x < w; x++) {
            uint8_t idx = src[x];
            *dst++ = g_palette_rgb[idx * 3 + 2];   /* B */
            *dst++ = g_palette_rgb[idx * 3 + 1];   /* G */
            *dst++ = g_palette_rgb[idx * 3 + 0];   /* R */
        }
        /* Zero-fill the row padding. */
        for (int p = 0; p < row_pad; p++) *dst++ = 0;
        fwrite(row, 1, row_total, fp);
    }
    fclose(fp);

    IDirectDrawSurface_Unlock(g_back, NULL);
    return 1;
}

/* GDI BitBlt capture of the window's client area to a 24bpp BMP.
 * Independent of DDraw — reads whatever's currently composited at
 * the window's screen position.  Mostly a diagnostic, but useful
 * as the "ground truth what the user sees" comparison vs.
 * gfx_capture_backbuffer (the back buffer's content). */
int gfx_capture_window(const char *path)
{
    if (!g_hwnd || !path) return 0;

    RECT client;
    GetClientRect(g_hwnd, &client);
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return 0;

    HDC hdc_win = GetDC(g_hwnd);
    HDC hdc_mem = CreateCompatibleDC(hdc_win);
    HBITMAP bmp = CreateCompatibleBitmap(hdc_win, w, h);
    HGDIOBJ old = SelectObject(hdc_mem, bmp);
    BitBlt(hdc_mem, 0, 0, w, h, hdc_win, 0, 0, SRCCOPY);

    /* GetDIBits into a 24bpp buffer.  Padded row stride. */
    const int row_bgr   = w * 3;
    const int row_pad   = (4 - (row_bgr & 3)) & 3;
    const int row_total = row_bgr + row_pad;
    const DWORD img_size  = (DWORD)(row_total * h);
    const DWORD file_size = 14 + 40 + img_size;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biSizeImage   = img_size;

    uint8_t *pixels = (uint8_t *)malloc(img_size);
    int ok = 0;
    if (pixels) {
        if (GetDIBits(hdc_mem, bmp, 0, h, pixels, &bi, DIB_RGB_COLORS) > 0) {
            FILE *fp = fopen(path, "wb");
            if (fp) {
                uint8_t fhdr[14] = {0};
                fhdr[0] = 'B'; fhdr[1] = 'M';
                fhdr[2] = (uint8_t)(file_size);
                fhdr[3] = (uint8_t)(file_size >> 8);
                fhdr[4] = (uint8_t)(file_size >> 16);
                fhdr[5] = (uint8_t)(file_size >> 24);
                fhdr[10] = 54;
                fwrite(fhdr, 1, 14, fp);
                fwrite(&bi.bmiHeader, 1, 40, fp);
                fwrite(pixels, 1, img_size, fp);
                fclose(fp);
                ok = 1;
            }
        }
        free(pixels);
    }

    SelectObject(hdc_mem, old);
    DeleteObject(bmp);
    DeleteDC(hdc_mem);
    ReleaseDC(g_hwnd, hdc_win);
    return ok;
}

/* Windowed present via GDI's StretchDIBits.
 *
 * Modern Windows DWM intercepts DDraw NORMAL-mode Blts to primary
 * and never propagates them to the window's redirection-bitmap
 * backing store — the Blt returns DD_OK and the back buffer
 * legitimately holds the rendered pixels, but the visible window
 * stays at whatever DWM last composited (typically the
 * background-erase black).  Diagnosed empirically: the GDI BitBlt
 * capture of the window shows uniform (0,0,0) while
 * gfx_capture_backbuffer shows (128,0,0) for the same frame.
 *
 * Workaround: route the present through GDI, which DWM does
 * intercept and re-composite correctly.  Lock the back, hand the
 * 8bpp + palette bits to StretchDIBits aimed at the window DC,
 * Unlock.  This is the "manual present" pattern that pre-DDraw
 * Win16 games used and that DDraw NORMAL mode was supposed to
 * replace — but on modern Windows the GDI path is what actually
 * works for our windowed-on-DWM scenario.
 *
 * Retail Lords2.exe sidesteps the issue because its
 * FUN_0042e5c0 auto-fullscreen-promotes any time the desktop
 * isn't 8bpp — i.e. always, on modern hardware — taking exclusive
 * coop level + SetDisplayMode and bypassing DWM entirely.  We
 * default-windowed (project_drop_in_defaults memory) so we need
 * the GDI seat. */
static int present_via_gdi(void)
{
    DDSURFACEDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    HRESULT hr = IDirectDrawSurface_Lock(g_back, NULL, &desc,
                                         DDLOCK_WAIT | DDLOCK_READONLY,
                                         NULL);
    if (FAILED(hr) || !desc.lpSurface) {
        static unsigned diag_lock;
        if (diag_lock == 0 || (diag_lock % 500) == 0)
            log_line("gfx_present Lock hr=0x%08lx", (unsigned long)hr);
        diag_lock++;
        return 0;
    }

    /* StretchDIBits takes a BITMAPINFO with the 256 palette
     * entries inline.  Build it from g_palette_rgb.  Color
     * layout: bmiColors[i] is RGBQUAD = {B,G,R,reserved}. */
    struct { BITMAPINFOHEADER h; RGBQUAD colors[256]; } bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.h.biSize        = sizeof(bi.h);
    bi.h.biWidth       = (LONG)desc.dwWidth;
    bi.h.biHeight      = -(LONG)desc.dwHeight;   /* top-down */
    bi.h.biPlanes      = 1;
    bi.h.biBitCount    = 8;
    bi.h.biCompression = BI_RGB;
    bi.h.biClrUsed     = 256;
    for (int i = 0; i < 256; i++) {
        bi.colors[i].rgbRed      = g_palette_rgb[i * 3 + 0];
        bi.colors[i].rgbGreen    = g_palette_rgb[i * 3 + 1];
        bi.colors[i].rgbBlue     = g_palette_rgb[i * 3 + 2];
        bi.colors[i].rgbReserved = 0;
    }

    RECT client;
    GetClientRect(g_hwnd, &client);
    int dst_w = client.right  - client.left;
    int dst_h = client.bottom - client.top;

    HDC hdc = GetDC(g_hwnd);
    int rv = StretchDIBits(hdc,
                           0, 0, dst_w, dst_h,
                           0, 0, (int)desc.dwWidth, (int)desc.dwHeight,
                           desc.lpSurface,
                           (BITMAPINFO *)&bi,
                           DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, hdc);
    IDirectDrawSurface_Unlock(g_back, NULL);

    if (rv <= 0) {
        static unsigned diag_blit;
        if (diag_blit == 0 || (diag_blit % 500) == 0)
            log_line("gfx_present StretchDIBits rv=%d err=%lu",
                     rv, GetLastError());
        diag_blit++;
        return 0;
    }
    return 1;
}

void gfx_present(void)
{
    if (!g_primary || !g_back) return;

    if (g_fullscreen) {
        /* Fullscreen — primary IS the screen at 640×480.  BltFast
         * the whole back buffer to (0,0).  DDraw exclusive coop
         * bypasses DWM so this path works without GDI. */
        RECT src = { 0, 0, GFX_RENDER_W, GFX_RENDER_H };
        HRESULT hr = IDirectDrawSurface_BltFast(g_primary, 0, 0, g_back,
                                                &src, DDBLTFAST_WAIT);
        if (FAILED(hr)) {
            static unsigned diag_n_fs;
            if (diag_n_fs == 0 || (diag_n_fs % 500) == 0)
                log_line("gfx_present BltFast hr=0x%08lx",
                         (unsigned long)hr);
            diag_n_fs++;
        }
    } else {
        present_via_gdi();
    }
}
