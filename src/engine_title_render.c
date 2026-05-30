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
#include "engine.h"                /* g_sim_mp_active (retail DAT_00553030) */
#include "save_schema_globals.h"   /* s_slot_0053f034 (selected-country id) */
#include "engine_button.h"
#include "localization.h"
#include "gfx_line.h"
#include "gfx_text.h"
#include "gfx_wrap.h"
#include "gfx_sprite_render.h"
#include "gfx_sprite.h"
#include "gfx_panel.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_caret.h"
#include "gfx_paint.h"
#include "engine_math.h"
#include "font_metrics.h"
#include "text_input.h"

/* ─── bound retail globals ──────────────────────────────────────── */

static const uint8_t *g_title_font;     /* DAT_005b2fa0 */
static const uint8_t *g_menu_font;      /* DAT_005af8f0 */
static const uint8_t *g_glyph_table;    /* DAT_004d71f0 (render map) */
static const uint8_t *g_glyph_metrics;  /* DAT_004d71d0 (measure map) */

static localization_state_t g_loc;      /* over DAT_00591580 */

static engine_dirty_rect_state_t *g_dirty_state;

static const uint8_t *g_panel_asset;    /* DAT_004eabec */
static const uint8_t *g_system_asset;   /* DAT_005bb540 — system2.pl8 */
static const uint8_t *g_misc_asset;     /* DAT_005530c8 — misc_sel.pl8 */
static int g_selected_button = 1;       /* DAT_00553114 */

/* ─── page-7 (game-setup) state ─────────────────────────────────── */

static const uint8_t *g_chrome_asset;       /* DAT_0057d3d0 — panels.pl8 */
static const uint8_t *g_palette_rgb;        /* DAT_004ea1b0 — for darken */

/* Country/map selector (FUN_0041f98b): a value-byte list + scroll +
 * selected visible row (0..4) + total count, plus the 128×128 preview
 * buffer (DAT_00569590) shown top-right. */
static const uint8_t *g_p7_country_values;  /* DAT_0050a460 */
static int g_p7_country_scroll;             /* DAT_0053e90c */
static int g_p7_country_sel;                /* DAT_0057cacc */
static int g_p7_country_count;              /* DAT_00554018 */
static const uint8_t *g_p7_preview;         /* DAT_00569590 */

/* The two page-7 sub-pass dirty flags (DAT_0057c948 = country list,
 * DAT_0055ce94 = option tabs).  On the full-scene render the subs draw
 * unconditionally (setup_screen clears these to 0 afterward); these gate
 * only the per-frame refresh (engine_title_render_refresh_setup), which
 * re-renders just the sub a page-7 input action dirtied. */
static int g_p7_country_dirty;              /* DAT_0057c948 */
static int g_p7_tabs_dirty;                 /* DAT_0055ce94 */

/* Option-tab selected values, in tab order 0..11 (0x433a70 output —
 * retail reads 12 permuted scalars at 0x53f288..0x53f2b4; we bind the
 * resolved values in tab order so the permutation lives in the caller). */
static int g_p7_tab_values[12];
static int g_p7_active_tab = -1;            /* the tab being edited; -1 none */

/* Roster (FUN_0041fbcb): per-seat fields the render reads from the
 * 0x160-stride player record at DAT_0057bf05.  Seats 1..5 (index 0
 * unused).  `enabled` is retail's separate DAT_005530e0[seat]. */
static engine_title_setup_player_t g_p7_players[6];
static const uint8_t *g_p7_names;           /* DAT_00553d54 (seat·0x2c) */

/* Option-tab geometry (retail DAT_004d3098, 12×{x, bevel_y, header_y}):
 * a 4-column × 3-row grid.  Header text at (x, header_y); the bevel
 * frame + value label at (x, bevel_y). */
static const int g_p7_tab_geom[12][3] = {
    { 0xaa, 0x118, 0x0f6 }, { 0xaa, 0x161, 0x151 }, { 0xaa, 0x1aa, 0x19a },
    { 0x122, 0x118, 0x0f6 }, { 0x122, 0x161, 0x151 }, { 0x122, 0x1aa, 0x19a },
    { 0x19a, 0x118, 0x0f6 }, { 0x19a, 0x161, 0x151 }, { 0x19a, 0x1aa, 0x19a },
    { 0x212, 0x118, 0x0f6 }, { 0x212, 0x161, 0x151 }, { 0x212, 0x1aa, 0x19a },
};

/* Per-tab entry-0x67 value sub-string base (retail DAT_004d3128): the
 * displayed value is sub-string [base + selected_value]. */
static const int g_p7_tab_value_base[12] = {
    0, 2, 5, 9, 0xb, 0xf, 0x13, 0x19, 0x1d, 0x22, 0x25, 0x2c,
};

/* Page-9 dropdown-panel anchor (retail DAT_004d3158, 12×{x, y, rows}).
 * The third column is the precomputed row count (value_count + 2), which
 * FUN_0041fdd6 recomputes from the live count, so only x/y are kept. */
static const int g_p9_panel_geom[12][2] = {
    { 0xaa,  0x118 }, { 0xaa,  0x161 }, { 0xaa,  0x1aa },
    { 0x122, 0x118 }, { 0x122, 0x151 }, { 0x122, 0x17a },
    { 0x19a, 0x118 }, { 0x19a, 0x151 }, { 0x19a, 0x16a },
    { 0x212, 0x118 }, { 0x212, 0x131 }, { 0x212, 0x1aa },
};

/* Page-9 (per-option detail dropdown) state.  Set by the page-7 option-tab
 * click (FUN_00433998 → menu_action_p7_tab); read by the page-9 render.
 *   g_p9_tab         retail DAT_0055306c — the tab being edited (0..11)
 *   g_p9_value_count retail DAT_00553fb4 — its number of value rows
 *   g_p9_bookmark    retail DAT_00553e5c — the page to redraw underneath */
static int g_p9_tab         = 0;
static int g_p9_value_count = 0;
static int g_p9_bookmark    = 7;

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

/* ─── page-4 ("Choose your title and your shield") bound state ──── */

/* Gating (DAT_00553030 / DAT_00553248).  When (gate_a == 0 || gate_b
 * != 0) the panel is one tile taller and the Back/Continue button row
 * is drawn.  For the Custom-game entry both are 0 → condition true. */
static int g_p4_gate_a;          /* DAT_00553030 */
static int g_p4_gate_b;          /* DAT_00553248 */

/* Continue-button enable inverse (DAT_0057c940): 0 → label normal
 * (0x3f), non-zero → dimmed (0x20).  (A valid name leaves it 0.) */
static int g_p4_continue_disabled;   /* DAT_0057c940 */

/* Shield selector state.  slots[1..5] (DAT_0057cb40) hold per-slot
 * shield ids (0 = the slot's default design); selected (DAT_0057c8cc)
 * is the chosen shield id; flash (DAT_0057d354) + flash_timer
 * (DAT_00568984) drive the selected-slot highlight-ring blink. */
static unsigned char g_p4_shield_slots[6];   /* DAT_0057cb40 */
static int g_p4_selected_shield;             /* DAT_0057c8cc */
static int g_p4_shield_flash;                /* DAT_0057d354 */
static int g_p4_flash_timer;                 /* DAT_00568984 */

/* The name/title text rendered in the box (DAT_0053f1e0). */
static const uint8_t *g_p4_name;

/* The live editor (DAT_005cd550 buffer + cursor + insert mode), read
 * by the caret.  NULL → the caret is skipped (textbox still draws). */
static const text_input_state_t *g_p4_editor;

/* Font-metrics scratch for the caret's cursor-char width measure. */
static font_metrics_state_t g_p4_fm;

/* ─── page-3 ("Loading a game." save/load list) bound state ─────── */

/* File-list entries (retail DAT_004e8790, 0x41-byte records — a
 * null-terminated name then the extension); count (DAT_004eb25c) and
 * first-visible index (DAT_004ea1a0).  Names render in a 3-column grid. */
static const uint8_t *g_fl_entries;
static int            g_fl_entry_stride = 0x41;
static int            g_fl_count;
static int            g_fl_first_visible;

/* The selected-name field shown above the grid (DAT_004ea130). */
static const uint8_t *g_fl_topname;

/* The editor behind the top field (caret); NULL → caret skipped. */
static const text_input_state_t *g_fl_editor;

/* Bottom status-line gate (DAT_0057d3c4): 0 → no status line drawn. */
static int g_fl_show_status;

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

void engine_title_render_bind_system_asset(const uint8_t *asset)
{
    g_system_asset = asset;
}

void engine_title_render_bind_misc_asset(const uint8_t *asset)
{
    g_misc_asset = asset;
}

void engine_title_render_set_selected_button(int index)
{
    g_selected_button = index;
}

int engine_title_render_selected_button(void) { return g_selected_button; }

void engine_title_render_set_page(int page) { g_title_page = page; }
int  engine_title_render_page(void)         { return g_title_page; }

/* ─── page-4 state binders ──────────────────────────────────────── */

void engine_title_render_set_choose_title_gates(int gate_a, int gate_b)
{
    g_p4_gate_a = gate_a;
    g_p4_gate_b = gate_b;
}

void engine_title_render_set_choose_title_continue_disabled(int disabled)
{
    g_p4_continue_disabled = disabled;
}

void engine_title_render_set_choose_title_shields(const unsigned char slots[6],
                                                  int selected,
                                                  int flash,
                                                  int flash_timer)
{
    for (int i = 0; i < 6; i++) g_p4_shield_slots[i] = slots ? slots[i] : 0;
    g_p4_selected_shield = selected;
    g_p4_shield_flash    = flash;
    g_p4_flash_timer     = flash_timer;
}

void engine_title_render_set_choose_title_name(const uint8_t *name)
{
    g_p4_name = name;
}

void engine_title_render_set_choose_title_editor(const text_input_state_t *editor)
{
    g_p4_editor = editor;
}

/* Bind the char→glyph metrics table the caret's width measure walks
 * (retail FUN_004015b9 reads DAT_004d71d0).  Mirrors the table already
 * bound for the centered-text measurer. */
void engine_title_render_bind_caret_metrics_table(const uint8_t *table)
{
    g_p4_fm.char_glyph_index = table;
}

/* ─── page-3 file-list state binders ────────────────────────────── */

void engine_title_render_set_filelist(const uint8_t *entries, int entry_stride,
                                      int count, int first_visible)
{
    g_fl_entries       = entries;
    g_fl_entry_stride  = entry_stride ? entry_stride : 0x41;
    g_fl_count         = count;
    g_fl_first_visible = first_visible;
}

void engine_title_render_set_filelist_topname(const uint8_t *name)
{
    g_fl_topname = name;
}

void engine_title_render_set_filelist_editor(const text_input_state_t *editor)
{
    g_fl_editor = editor;
}

void engine_title_render_set_filelist_show_status(int show)
{
    g_fl_show_status = show;
}

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
 * FUN_00402c5e(entry, skip, x, y, w, font, color) call shape.  The
 * main-menu / options pages all draw from entry 0xb; later pages (e.g.
 * the "Play Now!" campaign chooser, page 5) draw from a different entry,
 * so the entry is a parameter. */
static void menu_text_entry(int loc_entry, int skip, int x, int y, int width,
                            const uint8_t *font, uint8_t color)
{
    gfx_wrap_localized_line_centered(&g_loc, loc_entry, skip, x, y, width,
                                     font, g_glyph_table, g_glyph_metrics,
                                     color);
}

/* Entry-0xb convenience wrapper (the main-menu / options string table). */
static void menu_text(int skip, int x, int y, int width,
                      const uint8_t *font, uint8_t color)
{
    menu_text_entry(0xb, skip, x, y, width, font, color);
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

/* Port of FUN_0041f5e8 — the two "Play Now!" campaign-chooser buttons
 * (page 5).  Unlike the vertically-stacked main-menu/options buttons,
 * these are two side-by-side boxes: a left button at x=0x6e and a right
 * button at x=0x16e, each w=0xa4 h=0x18, labels from localization entry
 * 0x27 (sub 4 "Original campaign" / sub 5 "The new campaign").  The
 * selected button (DAT_00553114, 1=left / 2=right) draws its label in the
 * 0xf9 highlight; the rest use 0x3f.  A single dirty-box mark covers the
 * row.  Bracketed by the no-shadow text mode (DAT_005aea40). */
void engine_title_render_play_now_buttons(void)
{
    gfx_text_set_no_shadow(1);

    /* Left button — "Original campaign" (entry 0x27 sub 4), select id 1. */
    uint8_t col1 = (g_selected_button == 1) ? 0xf9 : 0x3f;
    gfx_line_rect_bevel_palette_b(0x6e, 0x74, 0xa4, 0x18);
    menu_text_entry(0x27, 4, 0x70, 0x7a, 0xa0, g_menu_font, col1);

    /* Right button — "The new campaign" (entry 0x27 sub 5), select id 2. */
    uint8_t col2 = (g_selected_button == 2) ? 0xf9 : 0x3f;
    gfx_line_rect_bevel_palette_b(0x16e, 0x74, 0xa4, 0x18);
    menu_text_entry(0x27, 5, 0x170, 0x7a, 0xa0, g_menu_font, col2);

    /* One dirty-box mark for the whole row (retail FUN_004527a6). */
    engine_dirty_rect_mark_box_tiled(g_dirty_state, 0x70, 0xd7, 0xe, 3, 1);

    gfx_text_set_no_shadow(0);
}

/* Port of FUN_0041f592 — the "Play Now!" campaign-chooser page (page 5),
 * reached from "Your options" → Play Now! when the expansion pack is
 * installed.  Panel + large-font title ("Expansion pack installed,
 * choose:-", entry 0x27 sub 0) + the two campaign buttons.  Unlike the
 * earlier pages the panel is drawn BEFORE the uppercase-highlight mode is
 * armed (the panel ignores it either way); no subtitle. */
void engine_title_render_play_now(void)
{
    /* Gray stone panel — deferred panel hook.  retail args
     * (DAT_004eabec, 0x40, 0x32, 0x20, 8). */
    g_panel_fn(g_panel_asset, 0x40, 0x32, 0x20, 8);

    /* Title — large font, entry 0x27 sub 0, centered in 0x200 at
     * (0x40, 0x50), uppercase-highlight armed (DAT_0058fe2c=1). */
    gfx_text_set_uppercase_highlight(1);
    menu_text_entry(0x27, 0, 0x40, 0x50, 0x200, g_title_font, 0x3f);
    gfx_text_set_uppercase_highlight(0);

    engine_title_render_play_now_buttons();
}

/* ─── page 4: "Choose your title and your shield" ───────────────── */

/* Port of FUN_0041f321 — the editable name/title box.  Draws the
 * opaque box-frame sprite (panels2 index 0xcc), renders the current
 * name text inside it, then the (blinking) caret, and marks the box
 * dirty.  The caret-anchor bookkeeping mirrors retail: clear the
 * first-pos latch, render with the editor cursor as the capture
 * target, and — if the render never latched (cursor past the text) —
 * fall back to the line's end X; then shift the caret to the box
 * origin and pin its Y at 0x52. */
static void engine_title_render_choose_title_name_box(void)
{
    /* Caret-anchor reset (DAT_005aeb64 / DAT_0058fd58 / DAT_0058fd78 /
     * DAT_005c925a / DAT_005cd404). */
    gfx_text_clear_first_pos_latch();
    gfx_text_set_first_pos_x(0);
    gfx_text_set_char_counter(0);
    gfx_text_set_collapse_active(1);
    gfx_text_set_line_x_acc(0);
    if (g_p4_editor) {
        gfx_text_set_first_pos_target(g_p4_editor->cursor);
    }

    /* Box frame — opaque sprite 0xcc at (0xd0, 0x48). */
    gfx_sprite_render_opaque_from_blob(g_panel_asset, 0xcc, 0xd0, 0x48);

    /* Name text — menu font, color 0x3f, at (0xd6, 0x50). */
    if (g_p4_name) {
        gfx_text_render_line(g_p4_name, 0xd6, 0x50, g_menu_font,
                             g_glyph_table, 0x3f);
    }

    /* Caret anchor: if the render didn't latch a first-pos, use the
     * line's end X (DAT_005cd404); then shift to the box origin and
     * pin Y. */
    if (gfx_text_first_pos_latched() == 0) {
        gfx_text_set_first_pos_x(gfx_text_line_x_acc());
    }
    gfx_text_set_first_pos_x(gfx_text_first_pos_x() + 0xd6);
    gfx_text_set_first_pos_y(0x52);

    /* Caret (FUN_0040acce).  Off in a single static render (blink → 1). */
    if (g_p4_editor) {
        gfx_caret_render(g_p4_editor, &g_p4_fm, g_menu_font, 0x3f);
    }

    engine_dirty_rect_mark_box_tiled(g_dirty_state, 0xd0, 0x48, 0x16, 3, 1);
}

/* Port of FUN_0041f1dd — the five-shield selector row.  Each slot
 * draws a shield sprite at x = (i-1)*0x58 + 0x70, y = 0x8c.  An empty
 * slot (id 0) and the selected-flashing slot use the i*2+0xcb sprite;
 * any other filled slot uses i*2+0xcc.  The selected slot, while
 * flashing and outside the blink-off window, also gets the highlight
 * ring (sprite 0xd7) behind it at (..+0x74, 0x70). */
static void engine_title_render_choose_title_shields(void)
{
    for (int i = 1; i < 6; i++) {
        int x = (i - 1) * 0x58 + 0x70;
        if (g_p4_shield_slots[i] == 0) {
            gfx_sprite_render_from_blob(g_panel_asset, i * 2 + 0xcb, x, 0x8c);
        } else if (g_p4_shield_slots[i] == g_p4_selected_shield
                   && g_p4_shield_flash != 0) {
            if (g_p4_flash_timer == 0) {
                gfx_sprite_render_from_blob(g_panel_asset, 0xd7,
                                            (i - 1) * 0x58 + 0x74, 0x70);
                gfx_sprite_render_from_blob(g_panel_asset, i * 2 + 0xcb,
                                            x, 0x8c);
            }
        } else {
            gfx_sprite_render_from_blob(g_panel_asset, i * 2 + 0xcc, x, 0x8c);
        }
    }
}

/* Port of FUN_0041f01f — the Back / Continue button row, drawn only
 * when the panel is in its tall layout (gate_a == 0 || gate_b != 0).
 * Back is select id 6, Continue id 7 (DAT_00553114 highlight).
 * Continue dims to 0x20 when disabled (DAT_0057c940 != 0), else 0x3f. */
static void engine_title_render_choose_title_buttons(void)
{
    if (!(g_p4_gate_a == 0 || g_p4_gate_b != 0)) {
        return;
    }

    gfx_text_set_no_shadow(1);

    /* Back. (entry 0xb sub 9), select id 6. */
    uint8_t back_col = (g_selected_button == 6) ? 0xf9 : 0x3f;
    gfx_line_rect_bevel_palette_b(0x70, 0xd7, 0xc0, 0x18);
    engine_dirty_rect_mark_box_tiled(g_dirty_state, 0x70, 0xd7, 0xe, 3, 1);
    menu_text(9, 0x70, 0xdc, 0xc0, g_menu_font, back_col);

    /* Continue (entry 0xb sub 0xb), select id 7. */
    uint8_t cont_col;
    if (g_selected_button == 7)            cont_col = 0xf9;
    else if (g_p4_continue_disabled == 0)  cont_col = 0x3f;
    else                                   cont_col = 0x20;
    gfx_line_rect_bevel_palette_b(0x150, 0xd7, 0xc0, 0x18);
    engine_dirty_rect_mark_box_tiled(g_dirty_state, 0x150, 0xd7, 0xe, 3, 1);
    menu_text(0xb, 0x150, 0xdc, 0xc0, g_menu_font, cont_col);

    gfx_text_set_no_shadow(0);
}

/* Port of FUN_0041ef57 — the page-4 root.  Panel (one tile taller in
 * the Back/Continue layout) + large-font title (entry 0xb sub 10,
 * uppercase-highlight armed) + the name box + shields (both inside the
 * no-shadow bracket) + the conditional button row. */
void engine_title_render_choose_title(void)
{
    int tall = (g_p4_gate_a == 0 || g_p4_gate_b != 0);

    gfx_text_set_uppercase_highlight(1);
    g_panel_fn(g_panel_asset, 0x50, 10, 0x1e, tall ? 0x10 : 0xe);
    menu_text(10, 0x50, 0x23, 0x1e0, g_title_font, 0x3f);
    gfx_text_set_uppercase_highlight(0);

    gfx_text_set_no_shadow(1);
    engine_title_render_choose_title_name_box();
    engine_title_render_choose_title_shields();
    gfx_text_set_no_shadow(0);

    if (tall) {
        engine_title_render_choose_title_buttons();
    }
}

/* ─── page 3: "Loading a game." save/load list ──────────────────── */

/* Port of FUN_004149ec (mode 1 — the title-screen variant called by
 * FUN_004148e4).  Draws: the selected-name field (tiled inset + name
 * text + caret), the file-list grid (3 columns, 0x10-row step, the
 * selected entry on a 96×16 highlight bar), and the bottom box inset.
 * `selected` is the entry index to highlight (999 = none, matching the
 * draw-pass call).  base = (0x60, 10). */
static void engine_title_render_filelist_content(int selected)
{
    const int bx = 0x60, by = 10;   /* retail local_1c / local_20 */

    /* Top-field caret-anchor reset. */
    gfx_text_clear_first_pos_latch();
    gfx_text_set_first_pos_x(0);
    gfx_text_set_char_counter(0);
    gfx_text_set_first_pos_y(by + 0x48);
    gfx_text_set_collapse_active(1);
    gfx_text_set_line_x_acc(0);

    /* Two tiled-border strips lining the selected-name field. */
    gfx_panel_fill_interior(g_panel_asset, bx + 0x1c, by + 0x42, 10, 1);
    gfx_panel_fill_interior(g_panel_asset, bx + 0x1c, by + 0x4c, 10, 1);

    /* Selected-name text. */
    if (g_fl_topname) {
        gfx_text_render_line(g_fl_topname, bx + 0x20, by + 0x48, g_menu_font,
                             g_glyph_table, 0x3f);
    }
    if (gfx_text_first_pos_latched() == 0) {
        gfx_text_set_first_pos_x(gfx_text_line_x_acc());
    }
    gfx_text_set_first_pos_x(gfx_text_first_pos_x() + bx + 0x20);
    gfx_text_set_first_pos_y(by + 0x48);
    if (g_fl_editor) {
        gfx_caret_render(g_fl_editor, &g_p4_fm, g_menu_font, 0x3f);
    }
    engine_dirty_rect_mark_box_tiled(g_dirty_state, bx + 0x18, by + 0x40, 0xb, 3, 1);

    /* The list-box tiled border. */
    gfx_panel_fill_interior(g_panel_asset, bx + 0x1e, by + 0x6a, 0x15, 10);
    engine_dirty_rect_mark_box_tiled(g_dirty_state, bx + 0x1c, by + 0x68, 0x16, 0xb, 1);

    /* File-list grid — 3 columns (step 0x78), rows step 0x10. */
    int lx = bx + 0x20;
    int ly = by + 0x6c;
    int col = 0, shown = 0;
    for (int i = g_fl_first_visible; i < g_fl_count; i++) {
        const uint8_t *name = g_fl_entries
            ? g_fl_entries + i * g_fl_entry_stride : 0;
        if (i == selected) {
            /* Highlight bar: 6 column-groups (96 px) × 0x10 rows. */
            gfx_glyph_set_glyph(0, 6, 0);
            gfx_dispatch_set_glyph_height_short(0x10);
            gfx_sprite_fill_block(lx - 2, ly - 1, 0x3f);
            if (name) gfx_text_render_line(name, lx, ly, g_menu_font,
                                           g_glyph_table, 0x20);
        } else if (name) {
            gfx_text_render_line(name, lx, ly, g_menu_font, g_glyph_table, 0x3f);
        }
        if (col == 2) { lx = bx + 0x20; ly += 0x10; col = -1; }
        else          { lx += 0x78; }
        if (0x1c < shown) break;
        col++; shown++;
    }

    /* Bottom-box tiled border. */
    gfx_panel_fill_interior(g_panel_asset, bx + 0x20, by + 0x118, 0x15, 1);
    gfx_text_set_line_x_acc(0);

    /* Bottom status line — only when armed (retail DAT_0057d3c4 != 0). */
    if (g_fl_show_status) {
        gfx_wrap_localized_line(&g_loc, 0x28, 2, bx + 0x20, by + 0x11a,
                                g_menu_font, g_glyph_table, 0x3f);
    }
}

/* Port of FUN_004148e4 (the save/load-list frame, mode-1 / title
 * variant).  Panel + clip-reset + large-font title (entry 0x28 sub
 * `title_sub`) + the action-button bevel rect + three box outlines +
 * the list content.  Page 3 ("Loading a game.") passes title_sub 5. */
void engine_title_render_filelist(int title_sub)
{
    g_panel_fn(g_panel_asset, 0x60, 10, 0x1c, 0x15);
    menu_text_entry(0x28, title_sub, 0x60, 0x22, 0x1c0, g_title_font, 0x3f);
    gfx_line_rect_bevel_palette_b(0x70, 0x42, 400, 0x100);
    gfx_line_rect(0x78, 0x4a, 0xc0, 0x20, 0x3f);
    gfx_line_rect(0x78, 0x72, 0x160, 0xa4, 0x3f);
    gfx_line_rect(0x78, 0x11e, 0x180, 0x1c, 0x3f);
    engine_title_render_filelist_content(999);
}

/* Page-3 button strip — retail DAT_004ddd78, the table the per-frame
 * overlay paints with FUN_0040cfd2(0x60, 10, &DAT_004ddd78, 4).  Four
 * buttons, all edge-type 4 (release-triggered), all reading the static
 * system2.pl8 sheet (kind >= 0x18):
 *
 *   - the two gauntlet action buttons (Load / Cancel), kind 0x20
 *     (32×32 bevels), sprites 0x1d / 0x1f;
 *   - the scroll up / down arrows, kind 0x18 (24×24), sprites 0x23 /
 *     0x25.
 *
 * Verbatim from Lords2.exe @ 0x4ddd78 (r2 dump).  The action/param
 * fields drive the input hit-test, not the render; they are kept for a
 * faithful layout but ignored by engine_button_render_list. */
static const engine_button_rec_t k_filelist_buttons[4] = {
    { 0x130, 0x40, 0x1d, 0x20, 0x004342f3, 0, 0, 0, 4,  0, 0 },
    { 0x160, 0x40, 0x1f, 0x20, 0x00434308, 0, 0, 0, 4,  0, 0 },
    { 0x180, 0x90, 0x23, 0x18, 0x00434346, 0, 0, 0, 4, -3, 1 },
    { 0x180, 0xb0, 0x25, 0x18, 0x00434346, 0, 0, 0, 4,  3, 1 },
};

/* Port of the page-3 (DAT_005530f0 == 3) arm of the per-frame UI
 * refresh dispatcher (FUN_004ba26e):
 *
 *     FUN_004149ec(DAT_004e65dc, 1);              // list-content refresh
 *     FUN_0040cfd2(0x60, 10, &DAT_004ddd78, 4);   // button strip
 *
 * The list-content refresh repaints the selected-name field + grid the
 * same way the full-scene draw did (idempotent for a settled frame);
 * `selected` 999 means no highlight, matching the draw pass.  The
 * button strip is the part the full-scene draw omits — the gauntlet
 * buttons + scroll arrows over the action-button bevel.  base = (0x60,
 * 10). */
void engine_title_render_filelist_buttons(void)
{
    engine_title_render_filelist_content(999);
    engine_button_render_list(0x60, 10, k_filelist_buttons, 4,
                              /*misc=*/NULL, g_system_asset, g_dirty_state);
}

/* Page-4 per-frame refresh — retail's title-update ladder calls
 * FUN_0041f321 standalone every tick (NOT inside FUN_0041ef57's
 * no-shadow bracket), so the steady-state name box is redrawn with the
 * text shadow ON (DAT_005aea40 == 0).  This is what a captured frame
 * shows after the page settles — distinct from the full-scene render,
 * where the name text is bracketed no-shadow.  Re-blits the box frame +
 * name text (shadowed) + caret over the static panel. */
void engine_title_render_refresh_choose_title(void)
{
    gfx_text_set_no_shadow(0);
    engine_title_render_choose_title_name_box();
}

/* ─────────────────────────────────────────────────────────────────
 * Page 7 — the custom-game setup screen
 * ───────────────────────────────────────────────────────────────── */

void engine_title_render_bind_chrome_asset(const uint8_t *asset)
{
    g_chrome_asset = asset;
}

void engine_title_render_bind_palette(const uint8_t *pal_rgb)
{
    g_palette_rgb = pal_rgb;
}

void engine_title_render_set_country_list(const uint8_t *values, int scroll,
                                          int selected_row, int count)
{
    g_p7_country_values = values;
    g_p7_country_scroll = scroll;
    g_p7_country_sel    = selected_row;
    g_p7_country_count  = count;
}

void engine_title_render_setup_mark_country_dirty(void) { g_p7_country_dirty = 1; }
void engine_title_render_setup_mark_tabs_dirty(void)    { g_p7_tabs_dirty = 1; }

/* Port of FUN_004337b4 — the country-selector scrollbar.  `dir` < 0 scrolls
 * up (toward 0, the up-arrow's DAT_0059154c = -1), `dir` >= 0 scrolls down
 * (toward count-5, the down-arrow's param = +1).  Retail clamps the new
 * scroll to [0, count-5] and, on a move, recomputes the selected-country id
 * (s_slot_0053f034 = country_values[scroll + sel]) and marks both sub-passes
 * dirty.  Returns 1 iff the scroll actually moved (the caller raises the
 * demand-redraw flag only then, mirroring retail's per-branch DAT_0057d340).
 *
 * The per-country reload it also runs — stage load 0x4c (FUN_0043eda0),
 * country-data binds (FUN_0046a037 / FUN_00467770), camera reset
 * (FUN_004298c1), and the AI-roster/preview recompute (FUN_004ae5e2 /
 * FUN_00433cc1) — is deferred until those subsystems land; the scroll
 * offset, selected id, and dirty flags are faithful, so the visible list
 * scroll + scrollbar thumb track exactly. */
int engine_title_render_country_scroll(int dir)
{
    if (dir < 0) {
        if (g_p7_country_scroll <= 0) {
            return 0;
        }
        g_p7_country_scroll--;
    } else {
        if (g_p7_country_scroll >= g_p7_country_count - 5) {
            return 0;
        }
        g_p7_country_scroll++;
    }
    if (g_p7_country_values) {
        s_slot_0053f034 =
            g_p7_country_values[g_p7_country_scroll + g_p7_country_sel];
    }
    g_p7_country_dirty = 1;
    g_p7_tabs_dirty    = 1;
    return 1;
}

/* Port of FUN_00433905 — select the visible country row (0..4, the clicked
 * row's DAT_0059154c minus 1).  Sets the selected visible row, recomputes
 * the selected-country id (country_values[scroll + row]), and marks both
 * sub-passes dirty.  The per-country reload is deferred (see above). */
void engine_title_render_country_select(int row)
{
    g_p7_country_sel = row;
    if (g_p7_country_values) {
        s_slot_0053f034 = g_p7_country_values[g_p7_country_scroll + row];
    }
    g_p7_country_dirty = 1;
    g_p7_tabs_dirty    = 1;
}

void engine_title_render_set_preview(const uint8_t *buf)
{
    g_p7_preview = buf;
}

void engine_title_render_set_tab_values(const int values[12], int active_tab)
{
    for (int i = 0; i < 12; i++) {
        g_p7_tab_values[i] = values ? values[i] : 0;
    }
    g_p7_active_tab = active_tab;
}

/* Set the page-9 option-detail state (retail DAT_0055306c / DAT_00553fb4 /
 * DAT_00553e5c, written by FUN_00433998).  `tab` is the option being
 * edited (0..11), `value_count` its number of selectable values, and
 * `bookmark` the page the dropdown overlays (7 = the page-7 setup screen). */
void engine_title_render_set_option_detail(int tab, int value_count,
                                           int bookmark)
{
    g_p9_tab         = tab;
    g_p9_value_count = value_count;
    g_p9_bookmark    = bookmark;
}

int engine_title_render_option_detail_tab(void)   { return g_p9_tab; }
int engine_title_render_option_detail_count(void) { return g_p9_value_count; }
int engine_title_render_option_detail_bookmark(void) { return g_p9_bookmark; }

int engine_title_render_tab_value(int tab)
{
    return (tab >= 0 && tab < 12) ? g_p7_tab_values[tab] : 0;
}

/* Port of FUN_00433ce4 — the page-9 dropdown hover hit-test.  Walks the
 * `value_count` rows of the open tab's panel and returns the 1-based index
 * of the row under (cx, cy), or 0 if none.  The row boxes are exactly the
 * text-row geometry the dropdown render stamps: column [x, x+0x60), each
 * row 16px tall starting at panel_y + i·16 + 16, with tab 2 (player count)
 * shifted up by (count-1)·16 because its panel grows upward (quirks #121).
 * Cursor coords are the game-space cursor (retail DAT_004e65fc/DAT_004e65f4),
 * the same space the panel geometry is expressed in. */
int engine_title_render_option_detail_hover(int cx, int cy)
{
    int tab   = g_p9_tab;
    int count = g_p9_value_count;
    int x     = g_p9_panel_geom[tab][0];

    for (int i = 0; i < count; i++) {
        int ry = g_p9_panel_geom[tab][1] + i * 0x10 + 0x10;
        if (tab == 2) {
            ry += (count - 1) * -0x10;
        }
        if (x <= cx && cx < x + 0x60 && ry <= cy && cy < ry + 0x10) {
            return i + 1;
        }
    }
    return 0;
}

/* Port of FUN_00433ba2 (+ the 0x433cc1 tail) — write the picked value back
 * to the open tab's option.  Retail keeps the 12 option values as permuted
 * scalars (DAT_0053f288..0053f2b4); the drop-in holds them in tab order in
 * g_p7_tab_values, so the writeback is a direct index store — the page-7
 * value cap (sub-string base[tab] + value) then shows the new pick once the
 * dropdown closes.
 *
 * Deferred: retail's recompute tail (0x433cc1) also recomputes DAT_0053f268
 * from the tab-2 player count and rebuilds the AI seat roster + map preview
 * (0x49caaa, 935 B); that roster subsystem isn't ported, so picking a value
 * updates the visible cap but does not yet re-seat the roster. */
void engine_title_render_option_detail_pick(int value)
{
    if (g_p9_tab >= 0 && g_p9_tab < 12) {
        g_p7_tab_values[g_p9_tab] = value;
    }
    /* TODO(roster): 0x433cc1 → DAT_0053f268 recompute + 0x49caaa
     * roster/preview rebuild (deferred with the MP/roster subsystem). */
}

/* Port of FUN_004ae539 — reset the 12 option tabs to their default values
 * (the Defaults button).  Retail writes the permuted scalars
 * DAT_0053f288..0053f2b4; the drop-in holds the values in tab order in
 * g_p7_tab_values, so the defaults are mapped through FUN_00433ba2's
 * tab→scalar permutation:
 *
 *   tab  0→53f29c  1→53f2a4  2→53f288  3→53f2a0  4→53f290  5→53f298
 *        6→53f2b0  7→53f294  8→53f2a8  9→53f2ac 10→53f28c 11→53f2b4
 *
 * Tab 10 (game length) and tab 11 (a single-player-only toggle) take a
 * different default in a multiplayer setup (retail keys both off
 * DAT_00553030 == 0).  The resulting tab-order vector for single player is
 * {0,0,3,0,0,0,3,2,2,1,6,1} — identical to the captured page-7 seed in
 * main.c, confirming this is the setup screen's defaults source. */
void engine_title_render_setup_apply_defaults(void)
{
    g_p7_tab_values[0]  = 0;   /* DAT_0053f29c */
    g_p7_tab_values[1]  = 0;   /* DAT_0053f2a4 */
    g_p7_tab_values[2]  = 3;   /* DAT_0053f288 — player count */
    g_p7_tab_values[3]  = 0;   /* DAT_0053f2a0 */
    g_p7_tab_values[4]  = 0;   /* DAT_0053f290 */
    g_p7_tab_values[5]  = 0;   /* DAT_0053f298 */
    g_p7_tab_values[6]  = 3;   /* DAT_0053f2b0 */
    g_p7_tab_values[7]  = 2;   /* DAT_0053f294 */
    g_p7_tab_values[8]  = 2;   /* DAT_0053f2a8 */
    g_p7_tab_values[9]  = 1;   /* DAT_0053f2ac */
    g_p7_tab_values[10] = (g_sim_mp_active == 0) ? 6 : 3;  /* DAT_0053f28c */
    g_p7_tab_values[11] = (g_sim_mp_active == 0) ? 1 : 0;  /* DAT_0053f2b4 */
}

/* Port of FUN_004ae5e2 — clamp the player-count tab (tab 2 / DAT_0053f288)
 * down to fit the available roster size.  Retail calls this right after the
 * defaults reset with DAT_0053e8a4 (the live roster size); the value caps at
 * one of 0..3 so the default "3" never exceeds a small custom roster. */
void engine_title_render_setup_clamp_player_count(int roster_size)
{
    int v;
    if (roster_size < 3)      v = 0;
    else if (roster_size < 4) v = 1;
    else if (roster_size < 5) v = 2;
    else                      v = 3;
    g_p7_tab_values[2] = v;
}

void engine_title_render_set_players(const engine_title_setup_player_t players[6],
                                     const uint8_t *names)
{
    for (int i = 0; i < 6; i++) {
        if (players) {
            g_p7_players[i] = players[i];
        } else {
            g_p7_players[i] = (engine_title_setup_player_t){ 0, 0, 0, 0, 0 };
        }
    }
    g_p7_names = names;
}

/* Port of FUN_0041f98b — the country/map selector list: the parchment
 * frame (misc sprite 0x10), the 128×128 preview buffer top-right, five
 * value rows (selected row inverted), and the computed scrollbar.
 *
 * The preview blit is 0x410c71(0, …): at param 0 the shield
 * compositor (0x410cbd) skips its entire per-pixel loop — only a
 * base raw blit of DAT_00569590 + a non-visual title/rank state
 * recompute (0x451bba) remain — so the page-7 coat-of-arms/map
 * preview is exactly gfx_sprite_render_raw of the bound buffer.  The
 * recompute is deferred (no pixels, and its lord-record inputs are not
 * yet modelled). */
void engine_title_render_setup_country_list(void)
{
    /* Parchment frame for the selector + preview (misc sprite 0x10). */
    gfx_sprite_render_from_blob(g_misc_asset, 0x10, 0x1f0, 9);

    /* Map / coat-of-arms preview (0x410c71(0, 0x1f0, 9) → raw blit
     * inset by (-2, +3)). */
    if (g_p7_preview) {
        gfx_sprite_render_raw(g_p7_preview, 0x1f0 - 2, 9 + 3, 0x80, 0x80);
    }

    gfx_text_set_no_shadow(1);
    for (int row = 0; row < 5; row++) {
        unsigned v = g_p7_country_values
                   ? g_p7_country_values[g_p7_country_scroll + row] : 0;
        uint8_t color;
        if (row == g_p7_country_sel) {
            color = 0x20;                       /* dark text on bright fill */
            gfx_line_fill_rect(0x1f0, row * 0x10 + 0x8d, 0x69, 0x10, 0x3f);
        } else {
            color = 0x3f;                       /* bright text on dark fill */
            gfx_line_fill_rect(0x1f0, row * 0x10 + 0x8d, 0x69, 0x10, 0x20);
        }
        gfx_wrap_localized_line(&g_loc, 0x65, (int)v, 0x1f2,
                                row * 0x10 + 0x8e, g_menu_font,
                                g_glyph_table, color);
    }
    gfx_text_set_no_shadow(0);

    engine_dirty_rect_mark_box_tiled(g_dirty_state, 0x1e0, 0, 10, 0xf, 1);

    /* Scrollbar: a 0x2c-px track split into top filler / thumb / bottom
     * filler, sized by the visible window's position in the full list
     * (retail FUN_00404dc1 = percent_div, FUN_00404d6b = percent_of). */
    int top = engine_math_percent_of(0x2c,
                  engine_math_percent_div(g_p7_country_scroll, g_p7_country_count));
    int mid = engine_math_percent_of(0x2c,
                  engine_math_percent_div(5, g_p7_country_count));
    int bot = engine_math_percent_of(0x2c,
                  engine_math_percent_div(g_p7_country_count - g_p7_country_scroll - 5,
                                          g_p7_country_count));
    mid = mid + (((0x2c - top) - mid) - bot);
    if (top != 0) gfx_line_fill_rect(0x25c, 0xa3, 0x14, top, 0x20);
    if (mid != 0) gfx_line_fill_rect(0x25c, top + 0xa3, 0x14, mid, 0x3f);
    if (bot != 0) gfx_line_fill_rect(0x25c, mid + top + 0xa3, 0x14, bot, 0x20);
}

/* Port of FUN_0041fbcb — the player roster down the left edge: per
 * occupied seat a shield (misc sprite seat·shield), a portrait, and the
 * centered seat name, with disabled seats dimmed.  FUN_0049caaa (the AI
 * name/seat assignment) is a non-visual state setup — the render reads
 * its result, which we bind via engine_title_render_set_players, rather
 * than recomputing it here. */
void engine_title_render_setup_roster(void)
{
    int row = 0;
    for (int seat = 1; seat <= 5; seat++) {
        const engine_title_setup_player_t *p = &g_p7_players[seat];

        int portrait_idx;
        if (p->type == 0) {
            if (p->portrait_sel == 0) {
                continue;                       /* empty seat → skip */
            }
            portrait_idx = p->portrait_sel + 9;
        } else {
            portrait_idx = 0xe;
        }

        /* Skip a seat-1+ human-controlled (type 1) slot's draw, but still
         * skip the row advance (retail's LAB_0041fc67 gate). */
        if (seat >= 2 && p->type == 1) {
            continue;
        }

        int ry = row * 0x5e;
        gfx_sprite_render_from_blob(g_misc_asset, p->shield * 2 - 2, 10, ry + 6);
        gfx_sprite_render_from_blob(g_misc_asset, portrait_idx, 0x54, ry + 10);
        const uint8_t *name = g_p7_names ? g_p7_names + seat * 0x2c : NULL;
        gfx_text_render_line_centered(name, 0, ry + 0x50, 0xa0, g_menu_font,
                                      g_glyph_table, g_glyph_metrics,
                                      p->name_color);
        if (p->type != 0 && p->enabled != 1) {
            gfx_paint_darken_rect(g_palette_rgb, 8, ry + 6, 0x91, 0x5a);
        }
        row++;
    }
    engine_dirty_rect_mark_box_tiled(g_dirty_state, 0, 0, 10, 0x1e, 1);
}

/* Port of FUN_0041f86d — the 12-entry option-tab strip.  Each tab gets
 * a wrapped header (entry 0x66 sub `tab`); every tab except the one
 * being edited (`active`) also gets a raised bevel frame and a centered
 * red value label (entry 0x67 sub base+selected). */
void engine_title_render_setup_tabs(void)
{
    gfx_text_set_no_shadow(1);
    for (int t = 0; t < 12; t++) {
        int x  = g_p7_tab_geom[t][0];
        int by = g_p7_tab_geom[t][1];
        int hy = g_p7_tab_geom[t][2];

        gfx_wrap_localized(&g_loc, 0x66, t, x, hy, 100, 100, 0, 0,
                           g_menu_font, g_glyph_table, g_glyph_metrics, 0x3f);

        if (g_p7_active_tab != t) {
            gfx_panel_draw_bevel(g_chrome_asset, x, by, 6, 3);
            int sub = g_p7_tab_value_base[t] + g_p7_tab_values[t];
            gfx_wrap_localized_line_centered(&g_loc, 0x67, sub, x + 1,
                                             by + 0x10, 0x60, g_menu_font,
                                             g_glyph_table, g_glyph_metrics,
                                             0xf9);
        }
    }
    gfx_text_set_no_shadow(0);
}

/* Port of FUN_0041f6c7 — the page-7 root: the central carved panel
 * (misc sprite 0xf) with the Cancel / Start / Defaults labels carved
 * into it, then the country list, the roster, and the option tabs.
 * Retail sets the two sub-pass dirty flags (DAT_0057c948 / DAT_0055ce94)
 * to 1 here, which the subs check-and-clear; on the full-render path the
 * flags are always freshly set so the subs always draw (the flags only
 * gate the not-yet-wired per-frame refresh). */
void engine_title_render_setup_screen(void)
{
    gfx_sprite_render_from_blob(g_misc_asset, 0xf, 0xa0, 0);

    gfx_text_set_no_shadow(1);
    menu_text_entry(0xb, 0xc, 0xa5, 0xc6, 0x4c, g_menu_font, 0x3f);  /* Cancel   */
    menu_text_entry(0xb, 0xd, 0xf3, 0xc6, 0x4c, g_menu_font, 0x3f);  /* Start    */
    menu_text_entry(0xb, 0xe, 0x141, 0xc6, 0x4c, g_menu_font, 0x3f); /* Defaults */
    /* no-shadow stays armed; setup_country_list re-arms then clears it. */

    engine_title_render_setup_country_list();
    engine_title_render_setup_roster();
    engine_title_render_setup_tabs();

    /* Retail FUN_0041f6c7 sets DAT_0057c948 / DAT_0055ce94 to 1, then the
     * subs (FUN_0041f98b / FUN_0041f86d) draw and clear them — so after a
     * full render both land at 0.  Our subs draw unconditionally; mirror the
     * end-state so the per-frame refresh has nothing pending until an input
     * action dirties a sub. */
    g_p7_country_dirty = 0;
    g_p7_tabs_dirty    = 0;
}

/* Port of the page-7 arm of FUN_004ba26e (the per-frame UI-refresh
 * dispatcher): re-render the country list (FUN_0041f98b) and option tabs
 * (FUN_0041f86d(999) → active = 999 so every tab keeps its bevel) only when
 * their dirty flag is set, then clear it.  The third call in retail's arm,
 * FUN_0041ff75(0), is the multiplayer chat-line ticker; it early-returns
 * when DAT_00553030 == 0 (single-player), so it is elided here.
 *
 * In retail the gate lives inside FUN_0041f98b / FUN_0041f86d themselves;
 * our setup_country_list / setup_tabs render unconditionally (the full-scene
 * path needs that), so the gate is hoisted to this caller instead. */
void engine_title_render_refresh_setup(void)
{
    if (g_p7_country_dirty > 0) {
        g_p7_country_dirty = 0;
        engine_title_render_setup_country_list();
    }
    if (g_p7_tabs_dirty > 0) {
        g_p7_tabs_dirty = 0;
        engine_title_render_setup_tabs();
    }
}

/* Render the page-7 setup screen as the page-9 backdrop, with `active_tab`
 * suppressed (its bevel + value label hidden, since the dropdown panel
 * covers it).  Retail FUN_0041fdd6 calls FUN_0041f6c7(DAT_0055306c), which
 * forwards the active tab to the tab-strip sub-renderer (FUN_0041f86d); our
 * setup screen reads it from g_p7_active_tab, so override-and-restore. */
static void render_setup_backdrop(int active_tab)
{
    int saved = g_p7_active_tab;
    g_p7_active_tab = active_tab;
    engine_title_render_setup_screen();
    g_p7_active_tab = saved;
}

/* Port of FUN_0041fdd6 — the page-9 per-option detail dropdown.  Reached by
 * clicking an option tab on the page-7 setup screen: it redraws the
 * bookmarked page underneath (the edited tab suppressed), then a chrome
 * bevel panel anchored at that tab holding `value_count` text rows (entry
 * 0x67 sub base+i), the highlighted row (DAT_00553114) in red 0xf9, the
 * rest in 0x3f.  Tab 2 (the player-count option) grows upward instead of
 * down — its anchor shifts up by (count-1)·16 so the panel's bottom stays
 * pinned at the tab. */
void engine_title_render_option_detail(void)
{
    int tab   = g_p9_tab;
    int count = g_p9_value_count;

    /* Backdrop: redraw the page we came from, with the edited tab hidden.
     * Only bookmark 7 (the page-7 setup screen) is ported; the page-8 MP
     * setup (FUN_0041f77a) and the 0xb/0xc flows are deferred. */
    if (g_p9_bookmark == 7) {
        render_setup_backdrop(tab);
    }

    int x = g_p9_panel_geom[tab][0];
    int y = g_p9_panel_geom[tab][1];
    if (tab == 2) {
        y += (count - 1) * -0x10;
    }
    int rows = count + 2;

    gfx_text_set_no_shadow(1);
    gfx_panel_draw_bevel(g_chrome_asset, x, y, 6, rows);
    for (int i = 0; i < count; i++) {
        uint8_t color = (i + 1 == g_selected_button) ? 0xf9 : 0x3f;
        gfx_wrap_localized_line_centered(&g_loc, 0x67,
                                         g_p7_tab_value_base[tab] + i, x + 1,
                                         i * 0x10 + y + 0x10, 0x60, g_menu_font,
                                         g_glyph_table, g_glyph_metrics, color);
    }
    gfx_text_set_no_shadow(0);

    engine_dirty_rect_mark_box_tiled(g_dirty_state, x, y, 6, rows, 1);
}

/* Port of FUN_0041e7e1. */
void engine_title_render_scene(int screen_state, int page)
{
    if (screen_state != 0x1f) {
        return;
    }

    /* Background first (deferred — FUN_0041e61d long pole). */
    g_background_fn();

    /* Page dispatch.  Page 1 = main menu, page 2 = "Your options", page 5
     * = "Play Now!" campaign chooser; the remaining 3..0xd route to the
     * (still-unported) page renderers via the page hook. */
    if (page == 1) {
        engine_title_render_main_menu();
    } else if (page == 2) {
        engine_title_render_options();
    } else if (page == 3) {
        engine_title_render_filelist(5);
    } else if (page == 4) {
        engine_title_render_choose_title();
    } else if (page == 5) {
        engine_title_render_play_now();
    } else if (page == 7) {
        engine_title_render_setup_screen();
    } else if (page == 9) {
        engine_title_render_option_detail();
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
 *   page 3 (file list)      → FUN_004149ec → engine_title_render_filelist_buttons
 *   page 4 (choose title)   → FUN_0041f321 → engine_title_render_refresh_choose_title
 *   page 5 ("Play Now!")    → FUN_0041f5e8 → engine_title_render_play_now_buttons
 *   page 7 (game setup)     → FUN_0041f98b + FUN_0041f86d(999) + FUN_0041ff75(0)
 *                             → engine_title_render_refresh_setup
 *
 * The remaining setup-flow pages (6, 8..0xd) have their own per-frame
 * refreshers in retail — still unported, so the live menu round-trips
 * pages 1..5 and 7 today; the others don't get the cheap per-frame repaint
 * until their refreshers land. */
void engine_title_render_refresh_buttons(int page)
{
    if (page == 1) {
        engine_title_render_buttons();
    } else if (page == 2) {
        engine_title_render_options_buttons();
    } else if (page == 3) {
        engine_title_render_filelist_buttons();
    } else if (page == 4) {
        engine_title_render_refresh_choose_title();
    } else if (page == 5) {
        engine_title_render_play_now_buttons();
    } else if (page == 7) {
        engine_title_render_refresh_setup();
    }
}
