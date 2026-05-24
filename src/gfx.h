/*
 * OpenLords2 — DDraw bring-up (header).
 *
 * Port of the retail bring-up chain:
 *   FUN_004b2922 → FUN_004b284e → FUN_0042e5c0 + FUN_0042ec91
 *                                  + FUN_0042eb7c + FUN_004b0ab5
 *
 * Today: DirectDraw create + SetCooperativeLevel + (windowed: no
 * mode-set / fullscreen: 640×480×8 SetDisplayMode) + primary surface
 * + offscreen back buffer + windowed clipper + initial color-fill.
 *
 * NOT yet: palette load (base01.256), Smacker, surface-lost
 * restore.  Those land in subsequent slices.
 *
 * Gated behind --gfx-init in main.c by default — calling
 * SetCooperativeLevel modifies global desktop state, and on a host
 * without the Frida `force-windowed` hook installed, the fullscreen
 * path will actually flip the display.  Off-by-default keeps the
 * headless smoke runs harmless.
 *
 * Logged status lines mirror retail's exact strings (the "OK :DD …"
 * / "ERR:DD …" pairs at 0x4d51xx in Lords2.exe) so transcripts diff
 * cleanly against a Frida-captured retail run.
 */

#ifndef OPENLORDS2_GFX_H
#define OPENLORDS2_GFX_H

#include <stdint.h>
#include <windows.h>

/* Render dimensions — retail's DAT_004eb274 / DAT_004eac4c.  Constants
 * for now; once the .ini parser lands they read from L2.INI. */
#define GFX_RENDER_W  640
#define GFX_RENDER_H  480

/* Init DDraw against the given window.  When fullscreen != 0, takes
 * exclusive cooperative level + SetDisplayMode(640,480,8).  When 0,
 * stays in NORMAL coop level and skips the mode-set (windowed 8bpp
 * via a clipper).  Returns 1 on success, 0 on failure.  On failure
 * the partial state is rolled back.
 *
 * Also attempts to load `base01.256` (256 × 8-bit RGB triplets)
 * relative to CWD via io_read_file_at, expand to a full 256-entry
 * IDirectDrawPalette, and SetPalette on the primary.  Failure here
 * is non-fatal (logged + skipped) — the primary still presents in
 * the OS default palette. */
int  gfx_init(HWND hwnd, int fullscreen);

/* Release everything created by gfx_init in the reverse order. */
void gfx_shutdown(void);

/* Load palette from a 768-byte RGB triplet file (e.g. base01.256),
 * upload to a fresh IDirectDrawPalette, attach to the primary
 * surface.  Returns 1 on success, 0 on any failure (file open, size
 * mismatch, CreatePalette, SetPalette).  Called by gfx_init with
 * "base01.256" — additional palette swaps land via direct call. */
int  gfx_load_palette(const char *path);

/* Present the back buffer to the primary surface.  In windowed
 * mode the back is Blt'd into the on-screen client rectangle (the
 * clipper trims).  In fullscreen mode the back is BltFast'd to
 * primary at (0,0).  Port of the inner present from retail's
 * `FUN_00452160 @ 0x452160`. */
void gfx_present(void);

/* 8-bit color-fill the back buffer with a palette index.  Useful
 * for bring-up smoke tests — fills with a non-black index so the
 * present can be visually distinguished from "no palette loaded /
 * stuck on entry 0". */
void gfx_fill_back_index(uint8_t palette_index);

/* Capture the back buffer to a 24bpp BMP file at the given path
 * (resolves palette index → BGR via the loaded palette).  Returns
 * 1 on success, 0 on any failure (no back buffer, Lock failure,
 * file write failure).  Used by the smoke harness to make
 * autonomous visual diffs possible without a human in the loop —
 * matches openrecet's capture_backbuffer in spirit. */
int  gfx_capture_backbuffer(const char *path);

/* Capture the visible window's client area via GDI BitBlt (24bpp
 * BMP).  Bypasses DDraw entirely — useful as a cross-check against
 * gfx_capture_backbuffer to confirm that the back→primary Blt in
 * gfx_present is actually reaching the screen (DWM-composited or
 * not). */
int  gfx_capture_window(const char *path);

/* Call IDirectDrawSurface::Restore on the primary and back surfaces.
 * Returns 1 on success, 0 on failure.  Port of retail's
 * `FUN_004b2730 @ 0x4b2730`.  Caller decides what to do on failure
 * (typically: full gfx_shutdown + gfx_init, the outer FUN_004b2922
 * wrapper).
 *
 * Logs retail's exact failure strings ("ERR:Can't restore front
 * buffer." at 0x4df668 / "ERR:Can't restore back buffer." at
 * 0x4df68c). */
int  gfx_restore(void);

/* Read-back the "any present-side Blt/Lock returned DDERR_SURFACELOST
 * since last clear" flag.  WndProc consults this on WM_ACTIVATEAPP
 * focus-regain so it knows whether to call gfx_restore.  Cleared by
 * gfx_restore on success (or by a successful gfx_init reseating the
 * pointers). */
int  gfx_surface_lost(void);

#endif /* OPENLORDS2_GFX_H */
