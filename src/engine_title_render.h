/*
 * OpenLords2 — title-scene / main-menu render cluster.
 *
 * Ports the main-menu render composition reached from `engine_tick`'s
 * render phase:
 *
 *   engine_tick (FUN_004b99c0) render phase, line 36:
 *     if (DAT_0057d340 != 0) { 0x40f1a0(DAT_0057d340); DAT_0057d340 = 0; }
 *       → 0x40f1a0  screen-dispatch on DAT_004eac50
 *           DAT_004eac50 == 0x1f → 0x41e7e1  title-scene dispatch
 *               0x41e61d   background + PL8 asset load   (NOT ported — long pole)
 *               DAT_005530f0 == 1 → FUN_0041ea14  MAIN-MENU root  ← this module
 *               FUN_0045240a   finalize           (already ported)
 *
 * See docs/plans/main-menu-render-chips.md for the full (corrected)
 * render tree and the long-pole dependency map.
 *
 * Functions ported here (rest on already-ported gfx primitives):
 *
 *   FUN_0041ea14 @ 0x41ea14 (143 B) → engine_title_render_main_menu
 *     The main-menu root.  Draws (in order): the gray stone panel, the
 *     title text (large font, with the uppercase-highlight render mode),
 *     the subtitle text (default font, no-shadow mode), then the four
 *     menu buttons.  All text is localization-blob entry 0xb, addressed
 *     by sub-string index.
 *
 *   FUN_0041eaa3 @ 0x41eaa3 (487 B) → engine_title_render_buttons
 *     The four menu buttons.  Each is a beveled rect
 *     (gfx_line_rect_bevel_palette_b) + a dirty-rect mark
 *     (engine_dirty_rect_mark_box_tiled) + a centered label
 *     (gfx_wrap_localized_line_centered).  The currently-selected button
 *     (`DAT_00553114`, 1..4) draws its label in highlight color 0xf9;
 *     the rest use 0x3f.  Bracketed by the no-shadow text mode.
 *
 * Deferred (the DDraw long pole — see plan):
 *
 *   0x409346 @ 0x409346 (gray stone panel) bottoms out in the PL8
 *   tile-blit family (0x4b446d / 4b44d2 / 4b43b1 / 4b49f7) and needs the
 *   panel asset (`DAT_004eabec`, loaded by 0x41e61d) and a locked
 *   DDraw surface.  Exposed here as a BOUND FUNCTION POINTER
 *   (engine_title_render_bind_panel_fn); the default is a no-op stub
 *   that bumps a call counter, so the composition above can be ported
 *   and tested for dispatch fidelity before the tile blitter lands.
 *   Same pattern as engine_overlay_render's deferred army drawer.
 *
 * State model: this module holds its own statics for the two fonts, the
 * glyph tables, the localization blob, the dirty-rect state, the panel
 * asset pointer, and the selected-button index.  All injected via the
 * binders below.  No Win32 deps — pure C, host-testable.
 *
 * Text-render globals (no_shadow / uppercase_highlight / language tag)
 * live in gfx_text.c; this module toggles them through the gfx_text
 * setters exactly as retail toggles DAT_005aea40 / DAT_0058fe2c around
 * the title and subtitle.  The taller title-font line box keys on the
 * gfx_text language tag being 0x1f (set via gfx_text_set_language_tag) —
 * that is the caller's responsibility, mirroring retail where it is the
 * ambient screen-state global.
 */

#ifndef OPENLORDS2_ENGINE_TITLE_RENDER_H
#define OPENLORDS2_ENGINE_TITLE_RENDER_H

#include <stdint.h>

#include "engine_dirty_rect.h"

/* ─────────────────────────────────────────────────────────────────
 * Bindings — install retail globals into this module's statics
 * ───────────────────────────────────────────────────────────────── */

/* Bind the two fonts.
 *
 *   title_font  retail `&DAT_005b2fa0` — the large title font.  Must be
 *               the SAME pointer registered as gfx_wrap's tall-font
 *               marker (gfx_wrap_set_tall_font_marker), which selects
 *               the 24-px line advance; this binder forwards it there.
 *   menu_font   retail `&DAT_005af8f0` — the default/menu font, also
 *               known as g_default_font (gfx_dispatch) / g_debug_label_font
 *               (engine_battlefield_render).  Used for the subtitle and
 *               all button labels.
 *
 * Pass NULL/NULL to suppress text (the per-line renderer's null-table
 * guard makes the render a no-op). */
void engine_title_render_bind_fonts(const uint8_t *title_font,
                                    const uint8_t *menu_font);

/* Bind the char→glyph index tables.
 *
 *   glyph_table          retail `&DAT_004d71f0` — render-side map (the
 *                        renderer subtracts 0x20 before indexing).
 *   glyph_table_metrics  retail `&DAT_004d71d0` — measure-side map (the
 *                        width pre-pass indexes it directly).  These are
 *                        the same fixed static table at a 0x20 offset.
 */
void engine_title_render_bind_glyph_tables(const uint8_t *glyph_table,
                                           const uint8_t *glyph_table_metrics);

/* Bind the localization blob (retail `DAT_00591580`, 120000 B, loaded
 * by config from l2_eng).  Menu strings are entry 0xb's sub-strings. */
void engine_title_render_bind_localization(unsigned char *blob);

/* Bind the dirty-rect state the button boxes mark.  Same singleton the
 * map / overlay passes bind. */
void engine_title_render_set_dirty_state(engine_dirty_rect_state_t *state);

/* Bind the gray-panel asset pointer (retail `DAT_004eabec`, populated by
 * the background loader 0x41e61d).  Forwarded as the first arg to the
 * bound panel fn. */
void engine_title_render_bind_panel_asset(const uint8_t *asset);

/* Set the selected-button index (retail `DAT_00553114`, 1..4).  Drives
 * which button label renders in highlight color 0xf9.  Default 1. */
void engine_title_render_set_selected_button(int index);

/* Panel-render hook — the deferred 0x409346.  Signature mirrors
 * retail: (asset, p2=0xa0, p3=10, p4=0x14, p5=0xf).  Pass NULL to reset
 * to the default no-op-plus-counter stub. */
typedef void (*engine_title_render_panel_fn)(const uint8_t *asset,
                                             int p2, int p3, int p4, int p5);
void engine_title_render_bind_panel_fn(engine_title_render_panel_fn fn);

/* ─────────────────────────────────────────────────────────────────
 * Instrumentation (test-only; not retail globals)
 * ───────────────────────────────────────────────────────────────── */

/* Number of times the bound panel fn was invoked.  Reset to 0 by
 * engine_title_render_reset_counters(). */
int  engine_title_render_panel_call_count(void);
void engine_title_render_reset_counters(void);

/* ─────────────────────────────────────────────────────────────────
 * Render functions
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_0041ea14.  Render the main menu: panel, title text,
 * subtitle text, and the four buttons. */
void engine_title_render_main_menu(void);

/* Port of FUN_0041eaa3.  Render the four menu buttons (bevel rect +
 * dirty mark + highlighted label). */
void engine_title_render_buttons(void);

#endif /* OPENLORDS2_ENGINE_TITLE_RENDER_H */
