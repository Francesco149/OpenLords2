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

/* Per-seat fields the page-7 roster pass reads from the 0x160-stride
 * player record at retail DAT_0057bf05 (plus the separate enabled flag
 * DAT_005530e0).  Bound for seats 1..5 (index 0 unused). */
typedef struct {
    uint8_t type;          /* +0  DAT_0057bf05: 0 = open, else controller */
    uint8_t portrait_sel;  /* +2  DAT_0057bf07: portrait variant (type 0)  */
    uint8_t name_color;    /* +3  DAT_0057bf08: name text color            */
    uint8_t shield;        /* +5  DAT_0057bf0a: shield sprite group        */
    uint8_t enabled;       /* DAT_005530e0[seat]: 1 = active (else dimmed) */
} engine_title_setup_player_t;

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

/* Bind the static system sprite sheet (retail `DAT_005bb540`, the
 * system2.pl8 blob).  Source for the page-3 button strip's gauntlet
 * buttons + scroll arrows (engine_title_render_filelist_buttons). */
void engine_title_render_bind_system_asset(const uint8_t *asset);

/* Bind the mode-dependent misc sprite sheet (retail `DAT_005530c8`, the
 * misc_sel.pl8 blob for the setup-flow pages).  Source for pages
 * 6..0xd's "kind < 0x18" gauntlet buttons (engine_button_render_list). */
void engine_title_render_bind_misc_asset(const uint8_t *asset);

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

/* Port of FUN_004148e4 (title-screen variant) — the "Loading a game."
 * save/load-list frame (page 3): panel + large-font title (entry 0x28
 * sub `title_sub`) + the action-button bevel rect + three box outlines
 * + the file-list content (FUN_004149ec).  Page 3 passes title_sub 5. */
void engine_title_render_filelist(int title_sub);

/* Page-3 per-frame button strip — the 0x1f/page-3 arm of FUN_004ba26e:
 * refreshes the list content then paints the gauntlet action buttons +
 * scroll arrows (FUN_0040cfd2 over DAT_004ddd78).  Requires the system
 * sheet bound (engine_title_render_bind_system_asset). */
void engine_title_render_filelist_buttons(void);

/* The shared page-3 button-record table (DAT_004ddd78) — the same
 * 4-entry strip the renderer paints, handed to the input arm so the
 * hit-test's press-animation writes land on the next repaint.  *count
 * receives the entry count. */
struct engine_button_rec;
struct engine_button_rec *engine_title_render_filelist_button_table(int *count);

/* ─── page-3 file-list state setters ───────────────────────────── */

/* DAT_004e8790 entries (stride 0x41, null-terminated name then ext),
 * DAT_004eb25c count, DAT_004ea1a0 first-visible index. */
void engine_title_render_set_filelist(const uint8_t *entries, int entry_stride,
                                      int count, int first_visible);
/* DAT_004ea130 — the selected-name field above the grid (seeded into a
 * renderer-owned mutable buffer the editor tail streams into). */
void engine_title_render_set_filelist_topname(const uint8_t *name);
/* DAT_005cd550 editor behind the top field — the one shared title
 * editor; the WM_CHAR route targets it on page 3.  NULL → caret skipped. */
void engine_title_render_set_filelist_editor(text_input_state_t *editor);
/* DAT_0057d3c4 — bottom status-line gate (0 → no status line). */
void engine_title_render_set_filelist_show_status(int show);
/* DAT_004ea1a0 — the file-list first-visible index. */
int  engine_title_render_filelist_first_visible(void);
/* DAT_004ea130 — the current filename field text (the editor's stream-out
 * target). */
const uint8_t *engine_title_render_filelist_name(void);
/* Per-frame page-3 editor tail (FUN_004ba9c8 page-3 arm): enable the
 * shared editor, recompute its length, and stream it into the filename
 * field (FUN_0040210c, &DAT_004ea130, 0x40). */
void engine_title_render_filelist_name_tail(void);
/* Port of FUN_00434346 — scroll-arrow action: advance the first-visible
 * index by `delta` (±3) with the `mode` (param1) clamp. */
void engine_title_render_filelist_scroll(int delta, int mode);

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

/* DAT_0053f1e0 — the name/title text rendered in the box.  Copies the
 * seed into an owned mutable buffer (the page-4 input arm streams the
 * live editor text into it each frame). */
void engine_title_render_set_choose_title_name(const uint8_t *name);

/* The current name-box text (owned buffer); used by tests and the
 * input arm's copy-out destination. */
const uint8_t *engine_title_render_choose_title_name(void);

/* DAT_005cd550 editor (buffer + cursor + insert mode) read by the
 * caret and driven by the WM_CHAR keystroke route; NULL → caret
 * skipped (the textbox still renders) and the name-tail is a no-op. */
void engine_title_render_set_choose_title_editor(text_input_state_t *editor);

/* The bound page-4 editor (NULL if none); the host WndProc routes
 * WM_CHAR / edit-key events here. */
text_input_state_t *engine_title_render_choose_title_editor(void);

/* Per-frame tail of FUN_004ba9c8's page-4 arm: enable the editor
 * (DAT_005aeb78 = 1), recompute its length (FUN_00401f49), and copy
 * its text into the name box (FUN_0040210c, 0x1f chars).  Called from
 * engine_menu_title_input's page-4 arm. */
void engine_title_render_choose_title_name_tail(void);

/* Top-of-dispatch editor reset (FUN_004ba9c8 `DAT_005aeb78 = 0`):
 * disables the bound editor so the WM_CHAR route is gated off on every
 * screen but the live text field, which re-enables it via the tail. */
void engine_title_render_title_editor_disable(void);

/* Assign shield `slot` (1..5) to the current player — port of FUN_00432fab's
 * render-visible slot-ownership move (the strategic colour write is deferred
 * with the roster).  The flashing shield jumps to the clicked design; an
 * already-owned slot is a no-op. */
void engine_title_render_choose_title_assign_shield(int slot);

/* Owner of shield `slot` (0 == unowned). */
int engine_title_render_choose_title_slot_owner(int slot);

/* Whether the Back/Continue row is shown (gate_a == 0 || gate_b != 0) — the
 * input arm hit-tests 7 hotspots when true, else the 5 shields only. */
int engine_title_render_choose_title_buttons_shown(void);

/* Char→glyph metrics table for the caret's cursor-char width measure. */
void engine_title_render_bind_caret_metrics_table(const uint8_t *table);

/* Port of the 0x1f arm of FUN_004ba26e — the per-frame button refresh.
 * Re-renders just the current page's buttons (page 1 → buttons, page 2
 * → options_buttons) so the hover highlight follows the mouse without a
 * full-scene redraw.  Called every frame from the engine_tick render
 * phase while the title screen is up.  No-op for pages whose per-frame
 * refresher is not yet ported. */
void engine_title_render_refresh_buttons(int page);

/* ─── page-7 ("game setup") ──────────────────────────────────────
 * The custom-game setup screen: central carved panel + Cancel/Start/
 * Defaults, the country/map selector + scrollbar + preview, the player
 * roster, and the 12-entry option-tab grid.  Bind the state below, then
 * render via engine_title_render_setup_screen (routed from page 7). */

/* Bind the chrome sprite sheet (retail DAT_0057d3d0, panels.pl8) the
 * option-tab bevel frames draw from (gfx_load_chrome_sheet). */
void engine_title_render_bind_chrome_asset(const uint8_t *asset);

/* Bind the active palette mirror (retail DAT_004ea1b0 = gfx.c's
 * g_palette_rgb) used to dim disabled roster seats.  NULL → no dim. */
void engine_title_render_bind_palette(const uint8_t *pal_rgb);

/* Country/map selector state: the value-byte list (DAT_0050a460), the
 * scroll offset (DAT_0053e90c), the selected visible row 0..4
 * (DAT_0057cacc), and the total entry count (DAT_00554018). */
void engine_title_render_set_country_list(const uint8_t *values, int scroll,
                                          int selected_row, int count);

/* Country-selector input (ports of FUN_004337b4 / FUN_00433905, driven by
 * the page-7 hotspot tables).  _scroll moves the visible window by `dir`
 * (<0 up / >=0 down), clamped to [0, count-5], returning 1 iff it moved;
 * _select sets the selected visible row (0..4).  Both recompute the
 * selected-country id (s_slot_0053f034) and mark the country + tab sub-
 * passes dirty for the per-frame refresh.  The per-country map/preview
 * reload is deferred (see the .c). */
int  engine_title_render_country_scroll(int dir);
void engine_title_render_country_select(int row);

/* Mark a page-7 sub-pass dirty so the next refresh re-renders it
 * (DAT_0057c948 country list / DAT_0055ce94 option tabs). */
void engine_title_render_setup_mark_country_dirty(void);
void engine_title_render_setup_mark_tabs_dirty(void);

/* Port of the page-7 arm of FUN_004ba26e: re-render the country list and
 * option tabs when dirtied, then clear the flags.  Routed from
 * engine_title_render_refresh_buttons(7). */
void engine_title_render_refresh_setup(void);

/* The 128×128 preview buffer shown top-right (retail DAT_00569590). */
void engine_title_render_set_preview(const uint8_t *buf);

/* The 12 option-tab selected values in tab order (retail's permuted
 * scalars at 0x53f288..0x53f2b4, resolved by 0x433a70), plus the
 * tab currently being edited (-1 = none; that tab shows no bevel). */
void engine_title_render_set_tab_values(const int values[12], int active_tab);

/* The roster seats (index 1..5; index 0 unused) and the seat-name blob
 * base (retail DAT_00553d54; seat name = base + seat*0x2c). */
void engine_title_render_set_players(const engine_title_setup_player_t players[6],
                                     const uint8_t *names);

/* Port of FUN_0041f6c7.  Render the whole page-7 setup screen. */
void engine_title_render_setup_screen(void);

/* Set the page-9 option-detail dropdown state (retail DAT_0055306c /
 * DAT_00553fb4 / DAT_00553e5c, written by the page-7 option-tab click
 * FUN_00433998).  `tab` 0..11, `value_count` rows, `bookmark` underlay page. */
void engine_title_render_set_option_detail(int tab, int value_count,
                                           int bookmark);

/* The current page-9 dropdown tab / value count / underlay-page bookmark
 * (for tests + the page-9 input arm). */
int engine_title_render_option_detail_tab(void);
int engine_title_render_option_detail_count(void);
int engine_title_render_option_detail_bookmark(void);

/* The selected value for option tab 0..11 (retail's permuted option
 * scalars, held in tab order).  For tests + the page-9 pick writeback. */
int engine_title_render_tab_value(int tab);

/* Port of FUN_00433ce4 — page-9 dropdown hover hit-test.  Returns the
 * 1-based index of the value row under the game-space cursor (cx, cy), or
 * 0 if none.  The page-9 input arm publishes this as the highlight
 * (DAT_00553114), so the hovered row draws red. */
int engine_title_render_option_detail_hover(int cx, int cy);

/* Port of FUN_00433ba2 — write `value` (0-based) back to the currently
 * open option tab.  The roster/preview recompute (FUN_00433cc1) is
 * deferred; the page-7 value cap reflects the pick immediately. */
void engine_title_render_option_detail_pick(int value);

/* Port of FUN_004ae539 — reset all 12 option tabs to their defaults (the
 * page-7 Defaults button).  Tabs 10/11 take a multiplayer-specific default
 * keyed off g_sim_mp_active. */
void engine_title_render_setup_apply_defaults(void);

/* Port of FUN_004ae5e2 — clamp the player-count tab (tab 2) down to fit
 * `roster_size` (retail DAT_0053e8a4): <3→0, <4→1, <5→2, else 3. */
void engine_title_render_setup_clamp_player_count(int roster_size);

/* Port of FUN_0041fdd6.  Render the page-9 per-option detail dropdown over
 * the bookmarked page (routed from page 9 by engine_title_render_scene). */
void engine_title_render_option_detail(void);

/* Port of FUN_0041f98b / FUN_0041fbcb / FUN_0041f86d — the three setup
 * sub-passes (country list + scrollbar + preview / roster / option-tab
 * grid).  Exposed for the per-frame refresh + tests. */
void engine_title_render_setup_country_list(void);
void engine_title_render_setup_roster(void);
void engine_title_render_setup_tabs(void);

/* Port of FUN_0041e7e1.  Title-scene dispatch: when `screen_state` is
 * 0x1f (the title screen), render the background, then the page named by
 * `page` (1 = main menu → engine_title_render_main_menu; 2 = "Your
 * options" → engine_title_render_options; 3..0xd → the page hook), then
 * the full-screen dirty finalize, and set the two scene flags.  A no-op
 * when `screen_state != 0x1f`. */
void engine_title_render_scene(int screen_state, int page);

#endif /* OPENLORDS2_ENGINE_TITLE_RENDER_H */
