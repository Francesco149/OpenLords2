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

#include <windows.h>

/* Render dimensions — retail's DAT_004eb274 / DAT_004eac4c.  Constants
 * for now; once the .ini parser lands they read from L2.INI. */
#define GFX_RENDER_W  640
#define GFX_RENDER_H  480

/* Init DDraw against the given window.  When fullscreen != 0, takes
 * exclusive cooperative level + SetDisplayMode(640,480,8).  When 0,
 * stays in NORMAL coop level and skips the mode-set (windowed 8bpp
 * via a clipper).  Returns 1 on success, 0 on failure.  On failure
 * the partial state is rolled back. */
int  gfx_init(HWND hwnd, int fullscreen);

/* Release everything created by gfx_init in the reverse order. */
void gfx_shutdown(void);

#endif /* OPENLORDS2_GFX_H */
