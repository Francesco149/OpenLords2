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

/* Select which of retail's two palette builders gfx_load_palette
 * emulates (takes effect on the next call):
 *
 *   enable = 1 (default) → FUN_0042f306, the path retail runs on a
 *     full-color (non-8bpp) desktop, i.e. fullscreen-exclusive on every
 *     modern machine.  Loads the .256 into idx 1..254 (PC_NOCOLLAPSE),
 *     hard-codes idx 0 = black and idx 255 = white.  The asset's UI
 *     colors render as intended: dark-red title caps (idx 1) and the
 *     bright-red selected/hovered menu-button highlight (idx 0xf9).  This
 *     is what a real player sees — confirmed against a live fullscreen
 *     retail capture (idx 0xf9 = (252,0,0)) and on screen.
 *
 *   enable = 0 → FUN_0042f449, the 8bpp-desktop path that also runs under
 *     our --force-windowed harness (it forces DAT_0053f228 = 0): the
 *     Windows-reserved slots 0..9 and 246..255 are forced black, so the
 *     hover highlight is invisible.  Retained only for bit-identical
 *     regression against the old windowed retail groundtruths.
 *
 * See engine-quirks #115 and title-menu-screens.md. */
void gfx_set_palette_full_range(int enable);

/* Load a raw full-screen 640×480×8 background image (24-byte PL8
 * header + raw indexed pixels) straight into the back buffer.  Port
 * of the platform side of retail's FUN_00408fcb.  Returns 1 on
 * success, 0 on any failure (no back buffer, file read, Lock). */
int  gfx_load_background_image(const char *pl8_path);

/* Load the title/menu background (palette + full-screen backdrop) for
 * the given menu page (retail DAT_005530f0).  Port of FUN_0041e61d's
 * per-page asset selection; leaves the backdrop in the back buffer for
 * the caller to present.  Returns 1 if both palette and image loaded. */
int  gfx_title_background_load(int page);

/* Load the panel sprite sheet (panels2.pl8) into a persistent blob and
 * return it (or NULL on failure).  Port of retail's FUN_004b25b0 alloc +
 * the panels2.pl8 read.  The blob is the raw PL8 the panel renderer
 * indexes; bind it as engine_title_render's panel asset.  Freed by
 * gfx_shutdown. */
const uint8_t *gfx_load_panel_sheet(void);

/* Select-load one of the two system sheets into the single retail
 * DAT_005bb540 slot — port of FUN_00499a1c.  which 0 = system2.pl8
 * (title chrome, always reloads), which 1 = system.pl8 (in-game
 * chrome, sticky no-op when already resident — the per-tick ensure).
 * Returns the blob or NULL on read failure. */
const uint8_t *gfx_load_system_sheet_select(int which);

/* The resident system sheet (last loaded, either file) without disk
 * I/O — what retail readers see in DAT_005bb540.  NULL until loaded. */
const uint8_t *gfx_system_sheet_current(void);

/* Load the static system sprite sheet (system2.pl8 → retail DAT_005bb540,
 * loaded by FUN_00499a1c(0) / FUN_004af8d9 into a 0xdd18-byte buffer).
 * The raw PL8 the UI button-strip renderer (engine_button) indexes for
 * the gauntlet buttons + scroll arrows.  Bind it with
 * engine_title_render_bind_system_asset.  Cached; freed by gfx_shutdown.
 * Returns the blob or NULL. */
const uint8_t *gfx_load_system_sheet(void);

/* Load the mode-dependent misc sprite sheet (misc_sel.pl8 → retail
 * DAT_005530c8, loaded by FUN_00499034 into a 0x28488-byte buffer).
 * The setup-flow variant (pages 6..0xd); the button-strip renderer
 * (engine_button) indexes it for "kind < 0x18" buttons.  Bind it with
 * engine_title_render_bind_misc_asset.  Cached; freed by gfx_shutdown.
 * Returns the blob or NULL. */
const uint8_t *gfx_load_misc_sheet(void);

/* Load the chrome sprite sheet (panels.pl8 → retail DAT_0057d3d0, index
 * 10 of the master asset table, a 0x128e0-byte buffer).  The raised
 * bevel-frame renderer (gfx_panel_draw_bevel) indexes it for button caps
 * + the page-7 option tabs.  Distinct from panels2.pl8 (the gray
 * stone-panel sheet).  Cached; freed by gfx_shutdown.  Returns the blob
 * or NULL. */
const uint8_t *gfx_load_chrome_sheet(void);

/* Load the terrain-info icon sheet (icon_tmp.pl8) — the small terrain-type
 * pictures the screen-4 terrain-info panel stamps (engine_terrain_info_render).
 * Cached; freed by gfx_shutdown.  Returns the blob or NULL. */
const uint8_t *gfx_load_terrain_icon_sheet(void);

/* Load the county history-graph sheet (graphs.pl8) — the population/
 * happiness panel graphs (engine_county_graph_render).  Cached; freed by
 * gfx_shutdown.  Returns the blob or NULL. */
const uint8_t *gfx_load_graphs_sheet(void);

/* The active 8-bit RGB palette mirror (retail DAT_004ea1b0[256*3]) the
 * page-7 roster dim (gfx_paint_darken_rect) keys off.  Valid after
 * gfx_load_palette / gfx_init.  Returns a pointer to the 768-byte
 * table (never NULL once gfx is initialised). */
const uint8_t *gfx_palette_rgb(void);

/* Load the menu/default font (fntl2_14.pl8 → retail DAT_005af8f0) and
 * the large title font (fntl2_22.pl8 → retail DAT_005b2fa0) as raw PL8
 * blobs.  Each is the glyph sprite sheet the text renderer indexes via
 * blob+idx*16+8.  Bind them with engine_title_render_bind_fonts.
 * Cached; freed by gfx_shutdown.  Return the blob or NULL. */
const uint8_t *gfx_load_menu_font(void);
const uint8_t *gfx_load_title_font(void);
/* Territory-info panel fonts: fntl2_9.pl8 (name/number, DAT_005c9a90)
 * and font_10.pl8 (resource readouts, DAT_005aeba0). */
const uint8_t *gfx_load_panel_name_font(void);
const uint8_t *gfx_load_panel_resource_font(void);

/* Bind the locked back-buffer base pointer as the gfx_paint canvas so
 * the pure-C renderers (sprite / glyph / line / panel) paint into it.
 * Returns 1 on success (back buffer locked, pitch == 640), 0 otherwise.
 * Pair with gfx_unlock_canvas after the render pass; the caller then
 * presents (gfx_tick_present). */
int  gfx_lock_canvas(void);
void gfx_unlock_canvas(void);

/* Present the back buffer to the primary surface.  In windowed
 * mode the back is Blt'd into the on-screen client rectangle (the
 * clipper trims).  In fullscreen mode the back is BltFast'd to
 * primary at (0,0).  Port of the inner present from retail's
 * `FUN_00452160 @ 0x452160`.
 *
 * Unconditional — no rate gate, no dirty-rect check.  Used as the
 * blit primitive by gfx_tick_present and exposed for callers that
 * need to force a paint outside the engine's own cadence (e.g.
 * one-shot bring-up tests).  Steady-state callers should use
 * gfx_tick_present instead. */
void gfx_present(void);

/* Tick the rate-limited present accumulator.  Port of retail's
 * `FUN_00452160 @ 0x452160`.
 *
 * Adds a `timeGetTime`-delta to a 50-unit accumulator on every call.
 * Once the accumulator crosses the threshold, calls gfx_present
 * (only if something is marked dirty) and resets.  `force != 0`
 * pins the accumulator past the threshold so the present happens
 * this iteration regardless of timing — used by retail after
 * screen-state transitions to guarantee an immediate visible
 * update.  See docs/findings/render-rate-limiter.md. */
void gfx_tick_present(int force);

/* Set the sticky "force present next tick" latch — port of retail's
 * `DAT_005651ac` semantics.  Lower-level than gfx_tick_present(1):
 * the latch persists across calls until consumed by the next
 * present.  Used by code that wants to schedule a forced present
 * without immediately triggering it. */
void gfx_force_present_next(void);

/* Union (l,t,r,b) into the dirty-rect bounds + bump the dirty
 * count.  Port of retail's `FUN_00452306 @ 0x452306`.  Coordinates
 * are clamped to [0, GFX_RENDER_W-1] × [0, GFX_RENDER_H-1]. */
void gfx_dirty_rect_union(int l, int t, int r, int b);

/* Mark the full 640×480 screen dirty.  Port of retail's
 * `FUN_0045240a @ 0x45240a` — convenience wrapper for
 * `gfx_dirty_rect_union(0, 0, GFX_RENDER_W-1, GFX_RENDER_H-1)`. */
void gfx_mark_all_dirty(void);

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

/* Capture the back buffer as a raw Trace Studio v3 frame payload (the exact
 * byte layout the retail Frida agent emits): [u32 w][u32 h][u32 present]
 * [256×(R,G,B,flags)][w*h 8bpp indices].  Preserves the index plane verbatim
 * for the v3 viewer's INDEX-level diff.  Returns 1 on success, 0 on failure. */
int  gfx_capture_backbuffer_raw(const char *path, unsigned present);

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
