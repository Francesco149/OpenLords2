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
#include "text_input.h"

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

/* Current selected-button index (retail `DAT_00553114`).  The menu-input
 * hover hit-test writes it via the setter; exposed for the renderer's
 * highlight check and for tests. */
int  engine_title_render_selected_button(void);

/* Title/menu page selector (retail `DAT_005530f0`).  Page 1 = main menu;
 * pages 2..0xd route to the deferred page hook.  Written by the menu
 * state tick; read by the render seam (engine.c) to source the page it
 * forwards to engine_title_render_scene.  Default 0. */
void engine_title_render_set_page(int page);
int  engine_title_render_page(void);

/* Panel-render hook — the deferred 0x409346.  Signature mirrors
 * retail: (asset, p2=0xa0, p3=10, p4=0x14, p5=0xf).  Pass NULL to reset
 * to the default no-op-plus-counter stub. */
typedef void (*engine_title_render_panel_fn)(const uint8_t *asset,
                                             int p2, int p3, int p4, int p5);
void engine_title_render_bind_panel_fn(engine_title_render_panel_fn fn);

/* Background hook — the deferred FUN_0041e61d (photographic background +
 * PL8 asset load, the long pole).  Default is a no-op-plus-counter stub.
 * Called first by the title-scene dispatch. */
typedef void (*engine_title_render_background_fn)(void);
void engine_title_render_bind_background_fn(engine_title_render_background_fn fn);

/* Page-renderer hook for the menu pages OTHER than the main menu
 * (page 1, which routes to engine_title_render_main_menu).  The unported
 * page renderers (FUN_0041ec8a … FUN_0042150b for pages 2..0xd) are
 * funnelled through this single hook, which receives the page number.
 * Default is a no-op-plus-counter stub. */
typedef void (*engine_title_render_page_fn)(int page);
void engine_title_render_bind_page_fn(engine_title_render_page_fn fn);

/* ─────────────────────────────────────────────────────────────────
 * Instrumentation (test-only; not retail globals)
 * ───────────────────────────────────────────────────────────────── */

/* Number of times the bound panel fn was invoked.  Reset to 0 by
 * engine_title_render_reset_counters(). */
int  engine_title_render_panel_call_count(void);
int  engine_title_render_background_call_count(void);
/* Page number the page hook was last invoked with, or 0 if never. */
int  engine_title_render_last_page(void);
void engine_title_render_reset_counters(void);

/* Flags the title-scene dispatch sets after a successful 0x1f render
 * (retail DAT_005651ac / DAT_004eb260).  0 until a scene render runs. */
int  engine_title_render_scene_flag_a(void);   /* DAT_005651ac */
int  engine_title_render_scene_flag_b(void);   /* DAT_004eb260 */

/* DAT_004eb260 is a shared screen-render flag owned here; the screen
 * dispatcher (FUN_0040f1a0) clears it to 0 after every dispatch. */
void engine_title_render_set_scene_flag_b(int v);

/* ─────────────────────────────────────────────────────────────────
 * Render functions
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_0041ea14.  Render the main menu: panel, title text,
 * subtitle text, and the four buttons. */
void engine_title_render_main_menu(void);

/* Port of FUN_0041eaa3.  Render the four menu buttons (bevel rect +
 * dirty mark + highlighted label). */
void engine_title_render_buttons(void);

/* Port of FUN_0041ec8a.  Render the single-player "Your options" page
 * (page 2): panel + large-font title ("Your options") + five buttons. */
void engine_title_render_options(void);

/* Port of FUN_0041ece6.  Render the five "Your options" buttons. */
void engine_title_render_options_buttons(void);

/* Port of FUN_0041f592.  Render the "Play Now!" campaign-chooser page
 * (page 5): panel + large-font title ("Expansion pack installed,
 * choose:-") + two side-by-side buttons ("Original campaign" / "The new
 * campaign"). */
void engine_title_render_play_now(void);

/* Port of FUN_0041f5e8.  Render the two page-5 campaign buttons. */
void engine_title_render_play_now_buttons(void);

/* Port of FUN_0041ef57.  Render the "Choose your title and your shield"
 * setup page (page 4): panel + large-font title + the editable name box
 * (FUN_0041f321), the five-shield selector (FUN_0041f1dd), and — in the
 * tall layout — the Back/Continue button row (FUN_0041f01f).  Reads the
 * page-4 state bound via the setters below. */
void engine_title_render_choose_title(void);

/* Page-4 per-frame refresh (FUN_0041f321 standalone) — re-renders the
 * name box with the text shadow ON (the steady-state appearance, unlike
 * the full render which brackets it no-shadow).  Also routed via
 * engine_title_render_refresh_buttons(4). */
void engine_title_render_refresh_choose_title(void);

/* ─── page-4 ("Choose your title and your shield") state setters ──
 * Mirror the retail globals the page-4 render reads; bind before
 * rendering page 4 (e.g. from the bring-up / state-tick seam). */

/* DAT_00553030 / DAT_00553248 — gating.  (gate_a == 0 || gate_b != 0)
 * selects the tall panel + Back/Continue row. */
void engine_title_render_set_choose_title_gates(int gate_a, int gate_b);

/* DAT_0057c940 — Continue-button disable (non-zero → dimmed label). */
void engine_title_render_set_choose_title_continue_disabled(int disabled);

/* DAT_0057cb40[1..5] / DAT_0057c8cc / DAT_0057d354 / DAT_00568984 —
 * shield slot ids, selected id, flash flag, flash-off timer. */
void engine_title_render_set_choose_title_shields(const unsigned char slots[6],
                                                  int selected,
                                                  int flash,
                                                  int flash_timer);

/* DAT_0053f1e0 — the name/title text rendered in the box. */
void engine_title_render_set_choose_title_name(const uint8_t *name);

/* DAT_005cd550 editor (buffer + cursor + insert mode) read by the
 * caret; NULL → caret skipped (the textbox still renders). */
void engine_title_render_set_choose_title_editor(const text_input_state_t *editor);

/* Char→glyph metrics table for the caret's cursor-char width measure. */
void engine_title_render_bind_caret_metrics_table(const uint8_t *table);

/* Port of the 0x1f arm of FUN_004ba26e — the per-frame button refresh.
 * Re-renders just the current page's buttons (page 1 → buttons, page 2
 * → options_buttons) so the hover highlight follows the mouse without a
 * full-scene redraw.  Called every frame from the engine_tick render
 * phase while the title screen is up.  No-op for pages whose per-frame
 * refresher is not yet ported. */
void engine_title_render_refresh_buttons(int page);

/* Port of FUN_0041e7e1.  Title-scene dispatch: when `screen_state` is
 * 0x1f (the title screen), render the background, then the page named by
 * `page` (1 = main menu → engine_title_render_main_menu; 2 = "Your
 * options" → engine_title_render_options; 3..0xd → the page hook), then
 * the full-screen dirty finalize, and set the two scene flags.  A no-op
 * when `screen_state != 0x1f`. */
void engine_title_render_scene(int screen_state, int page);

#endif /* OPENLORDS2_ENGINE_TITLE_RENDER_H */
