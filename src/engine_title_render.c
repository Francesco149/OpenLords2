/*
 * OpenLords2 — title-scene / main-menu render cluster.
 *
 * Ports FUN_0041ea14 (main-menu root) and FUN_0041eaa3 (buttons).
 * See engine_title_render.h for the call topology and the deferred
 * panel hook, and docs/plans/main-menu-render-chips.md for the full
 * render tree.
 */

#include <stddef.h>

#include "engine_title_render.h"
#include "engine_dirty_rect.h"
#include "localization.h"
#include "gfx_line.h"
#include "gfx_text.h"
#include "gfx_wrap.h"

/* ─── bound retail globals ──────────────────────────────────────── */

static const uint8_t *g_title_font;     /* DAT_005b2fa0 */
static const uint8_t *g_menu_font;      /* DAT_005af8f0 */
static const uint8_t *g_glyph_table;    /* DAT_004d71f0 (render map) */
static const uint8_t *g_glyph_metrics;  /* DAT_004d71d0 (measure map) */

static localization_state_t g_loc;      /* over DAT_00591580 */

static engine_dirty_rect_state_t *g_dirty_state;

static const uint8_t *g_panel_asset;    /* DAT_004eabec */
static int g_selected_button = 1;       /* DAT_00553114 */

/* Title/menu page selector (retail DAT_005530f0).  Written by the menu
 * state tick (0x42ff10, unported) — page 1 = main menu — and read by
 * the render seam to pass to engine_title_render_scene.  0 until a tick
 * sets it, so the scene draws nothing visible until the state tick lands
 * (or a bring-up shortcut forces it). */
static int g_title_page;

/* ─── deferred panel hook (0x409346) ────────────────────────── */

static int g_panel_calls;

static void panel_stub(const uint8_t *asset, int p2, int p3, int p4, int p5)
{
    (void)asset; (void)p2; (void)p3; (void)p4; (void)p5;
    g_panel_calls++;
}

static engine_title_render_panel_fn g_panel_fn = panel_stub;

/* ─── deferred background hook (FUN_0041e61d) ───────────────────── */

static int g_background_calls;
static void background_stub(void) { g_background_calls++; }
static engine_title_render_background_fn g_background_fn = background_stub;

/* ─── deferred non-main-menu page hook (FUN_0041ec8a … 0042150b) ── */

static int g_last_page;
static void page_stub(int page) { g_last_page = page; }
static engine_title_render_page_fn g_page_fn = page_stub;

/* Scene flags set after a 0x1f render (DAT_005651ac / DAT_004eb260). */
static int g_scene_flag_a;
static int g_scene_flag_b;

/* ─── binders ───────────────────────────────────────────────────── */

void engine_title_render_bind_fonts(const uint8_t *title_font,
                                    const uint8_t *menu_font)
{
    g_title_font = title_font;
    g_menu_font  = menu_font;
    /* Retail's line-height discriminator is pointer-equality against
     * the title font (FUN_00402c5e's `font == &DAT_005b2fa0 ? 0x18 : 0x10`
     * shape, modeled in gfx_wrap as the tall-font marker).  Keep the two
     * in sync so the title renders at the 24-px advance. */
    gfx_wrap_set_tall_font_marker(title_font);
}

void engine_title_render_bind_glyph_tables(const uint8_t *glyph_table,
                                           const uint8_t *glyph_table_metrics)
{
    g_glyph_table   = glyph_table;
    g_glyph_metrics = glyph_table_metrics;
}

void engine_title_render_bind_localization(unsigned char *blob)
{
    g_loc.blob = blob;
    g_loc.walker_offset = 0;
}

void engine_title_render_set_dirty_state(engine_dirty_rect_state_t *state)
{
    g_dirty_state = state;
}

void engine_title_render_bind_panel_asset(const uint8_t *asset)
{
    g_panel_asset = asset;
}

void engine_title_render_set_selected_button(int index)
{
    g_selected_button = index;
}

int engine_title_render_selected_button(void) { return g_selected_button; }

void engine_title_render_set_page(int page) { g_title_page = page; }
int  engine_title_render_page(void)         { return g_title_page; }

void engine_title_render_bind_panel_fn(engine_title_render_panel_fn fn)
{
    g_panel_fn = fn ? fn : panel_stub;
}

void engine_title_render_bind_background_fn(engine_title_render_background_fn fn)
{
    g_background_fn = fn ? fn : background_stub;
}

void engine_title_render_bind_page_fn(engine_title_render_page_fn fn)
{
    g_page_fn = fn ? fn : page_stub;
}

int  engine_title_render_panel_call_count(void)      { return g_panel_calls; }
int  engine_title_render_background_call_count(void) { return g_background_calls; }
int  engine_title_render_last_page(void)             { return g_last_page; }
int  engine_title_render_scene_flag_a(void)          { return g_scene_flag_a; }
int  engine_title_render_scene_flag_b(void)          { return g_scene_flag_b; }
void engine_title_render_set_scene_flag_b(int v)     { g_scene_flag_b = v; }

void engine_title_render_reset_counters(void)
{
    g_panel_calls = 0;
    g_background_calls = 0;
    g_last_page = 0;
    g_scene_flag_a = 0;
    g_scene_flag_b = 0;
}

/* ─── render ────────────────────────────────────────────────────── */

/* One centered localization line — collapses retail's
 * FUN_00402c5e(0xb, skip, x, y, w, font, color) call shape. */
static void menu_text(int skip, int x, int y, int width,
                      const uint8_t *font, uint8_t color)
{
    gfx_wrap_localized_line_centered(&g_loc, 0xb, skip, x, y, width,
                                     font, g_glyph_table, g_glyph_metrics,
                                     color);
}

/* One menu button row — the shape shared verbatim by every title-page
 * button block (FUN_0041eaa3, FUN_0041ece6, …): a beveled fill rect at
 * the fixed x=0xe0 w=0xc0 h=0x18, a dirty-box mark (0xe tiles × 3), and a
 * centered label whose color is the highlight 0xf9 when this row is the
 * selected button (DAT_00553114), else 0x3f.  retail bracketing
 * (DAT_005aea40 no-shadow) is the caller's responsibility. */
static void render_menu_button(int y, int label_skip, int select_id)
{
    uint8_t color = (g_selected_button == select_id) ? 0xf9 : 0x3f;
    gfx_line_rect_bevel_palette_b(0xe0, y, 0xc0, 0x18);
    engine_dirty_rect_mark_box_tiled(g_dirty_state, 0xe0, y, 0xe, 3, 1);
    menu_text(label_skip, 0xe0, y + 5, 0xc0, g_menu_font, color);
}

/* Port of FUN_0041eaa3. */
void engine_title_render_buttons(void)
{
    /* Retail brackets the whole block with DAT_005aea40 = 1/0
     * (no-shadow render mode). */
    gfx_text_set_no_shadow(1);

    /* Four buttons.  retail layout: fixed x=0xe0, w=0xc0, h=0x18; the
     * rows step by 0x24 (0x5b, 0x7f, 0xa3, 0xc7).  The label uses
     * sub-string {2,3,0x2f,4}.  The selected button (DAT_00553114) gets
     * highlight color 0xf9. */
    static const struct { int y, label_skip, select_id; } buttons[4] = {
        { 0x5b, 2,    1 },
        { 0x7f, 3,    2 },
        { 0xa3, 0x2f, 3 },
        { 0xc7, 4,    4 },
    };
    for (int i = 0; i < 4; i++)
        render_menu_button(buttons[i].y, buttons[i].label_skip,
                           buttons[i].select_id);

    gfx_text_set_no_shadow(0);
}

/* Port of FUN_0041ece6 — the five "Your options" (page 2) buttons.  Same
 * shape as the main-menu block, one extra row (5 buttons, y step 0x24:
 * 0x5b/0x7f/0xa3/0xc7/0xeb), labels sub-string {6,7,0x13,8,9}. */
void engine_title_render_options_buttons(void)
{
    gfx_text_set_no_shadow(1);

    static const struct { int y, label_skip, select_id; } buttons[5] = {
        { 0x5b, 6,    1 },   /* "Play Now!"   */
        { 0x7f, 7,    2 },   /* "Load a game" */
        { 0xa3, 0x13, 3 },   /* "Skirmish!"   */
        { 0xc7, 8,    4 },   /* "Custom game" */
        { 0xeb, 9,    5 },   /* "Back."       */
    };
    for (int i = 0; i < 5; i++)
        render_menu_button(buttons[i].y, buttons[i].label_skip,
                           buttons[i].select_id);

    gfx_text_set_no_shadow(0);
}

/* Port of FUN_0041ea14. */
void engine_title_render_main_menu(void)
{
    /* Title text renders with the uppercase-highlight mode armed
     * (DAT_0058fe2c = 1), shadow on.  retail arms it before the panel
     * (the panel ignores it) and clears it after the title. */
    gfx_text_set_uppercase_highlight(1);

    /* Gray stone panel — deferred to the bound panel fn (0x409346).
     * retail args: (DAT_004eabec, 0xa0, 10, 0x14, 0xf). */
    g_panel_fn(g_panel_asset, 0xa0, 10, 0x14, 0xf);

    /* Title — large font, sub-string 0, centered in 0x180 at (0x80, 0x1e). */
    menu_text(0, 0x80, 0x1e, 0x180, g_title_font, 0x3f);

    gfx_text_set_uppercase_highlight(0);

    /* Subtitle — default font, no-shadow, sub-string 1, (0x80, 0x3a). */
    gfx_text_set_no_shadow(1);
    menu_text(1, 0x80, 0x3a, 0x180, g_menu_font, 0x3f);
    gfx_text_set_no_shadow(0);

    engine_title_render_buttons();
}

/* Port of FUN_0041ec8a — the single-player "Your options" page (page 2).
 * Same composition as the main menu minus the subtitle: panel + large-font
 * title ("Your options", sub-string 5), then the five option buttons.  The
 * panel geometry differs from page 1 (0xb0,10,0x12,0x12 vs 0xa0,10,0x14,0xf)
 * — narrower + taller for the extra button row. */
void engine_title_render_options(void)
{
    /* Title renders with uppercase-highlight armed (DAT_0058fe2c=1),
     * shadow on; cleared after the title.  retail arms it before the
     * panel (the panel ignores it). */
    gfx_text_set_uppercase_highlight(1);

    g_panel_fn(g_panel_asset, 0xb0, 10, 0x12, 0x12);

    /* Title — large font, sub-string 5, centered in 0x120 at (0xb0, 0x2d). */
    menu_text(5, 0xb0, 0x2d, 0x120, g_title_font, 0x3f);

    gfx_text_set_uppercase_highlight(0);

    engine_title_render_options_buttons();
}

/* Port of FUN_0041e7e1. */
void engine_title_render_scene(int screen_state, int page)
{
    if (screen_state != 0x1f) {
        return;
    }

    /* Background first (deferred — FUN_0041e61d long pole). */
    g_background_fn();

    /* Page dispatch.  Page 1 is the main menu; pages 2..0xd route to the
     * (still-unported) page renderers via the page hook. */
    if (page == 1) {
        engine_title_render_main_menu();
    } else if (page == 2) {
        engine_title_render_options();
    } else if (page >= 3 && page <= 0xd) {
        g_page_fn(page);
    }

    /* Full-screen dirty finalize + the two "scene rendered" flags. */
    engine_dirty_rect_mark_full_screen(g_dirty_state);
    g_scene_flag_a = 1;   /* DAT_005651ac */
    g_scene_flag_b = 1;   /* DAT_004eb260 */
}

/* Port of the 0x1f arm of FUN_004ba26e (the per-frame UI refresh
 * dispatcher, called unconditionally every frame from engine_tick's
 * render phase — decompile FUN_004b99c0 line 99).  For the title
 * screen it re-renders just the current page's buttons over the static
 * panel, so the hover highlight (DAT_00553114, updated by the input
 * dispatcher without a demand-redraw request) repaints every frame
 * without a full-scene redraw.
 *
 *   page 1 (main menu)      → FUN_0041eaa3 → engine_title_render_buttons
 *   page 2 ("Your options") → FUN_0041ece6 → engine_title_render_options_buttons
 *
 * Pages 3..0xd have their own per-frame refreshers in retail
 * (0x4149ec, 0x41f321, …) — still unported, so the live menu
 * only round-trips between pages 1 and 2 today; other pages just don't
 * get the cheap per-frame repaint until their refreshers land. */
void engine_title_render_refresh_buttons(int page)
{
    if (page == 1) {
        engine_title_render_buttons();
    } else if (page == 2) {
        engine_title_render_options_buttons();
    }
}
