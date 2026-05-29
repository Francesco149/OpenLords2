/*
 * OpenLords2 — title-menu input + page navigation.  See header for the
 * call topology and the retail provenance of each piece.
 *
 * Ports: FUN_0040e5e7 (hover), FUN_0040e3ee (click dispatch),
 * FUN_00432b05 (page-1 action), FUN_00432cc8 (page-2 action),
 * FUN_00433461 (page-5 campaign-select action), FUN_004335f0 / FUN_004337b4
 * / FUN_00433905 / FUN_00433998 (the page-7 setup-screen button / scrollbar
 * / country-row / option-tab actions), and the pages-1/2/5/7 arm of
 * FUN_004ba9c8 (per-page input dispatcher).
 */

#include <stddef.h>

#include "engine_menu_input.h"
#include "engine_title_render.h"
#include "save_schema_globals.h"   /* s_slot_0055302c (DAT_0055302c) */

/* ─── engine-side-effect hooks + modeled scalars ─────────────────── */

static int g_stub_last_redraw_mode;
static void redraw_stub(int mode) { g_stub_last_redraw_mode = mode; }
static engine_menu_redraw_fn g_redraw_fn = redraw_stub;

static int g_stub_quit_count;
static void quit_stub(void) { g_stub_quit_count++; }
static engine_menu_quit_fn g_quit_fn = quit_stub;

/* DAT_005c9a74 — single-player availability gate (see header). */
static int g_sp_available = 1;

/* Modeled title-menu scalars with no cross-module consumer in the
 * drop-in.  Set faithfully by the click actions; carried here so the
 * port matches retail's writes without inventing engine-wide globals.
 *
 *   DAT_0053f648  last-acknowledged selected button (debounce snapshot,
 *                 read by 0x42ff10's 0x1f confirm arm — deferred)
 *   DAT_0055302c  → s_slot_0055302c (shared save-schema scalar)
 *   DAT_0058fe10  multiplayer-init result flag (MP path deferred)
 *   DAT_0057d320  load-list "selected slot is dir" flag (load path deferred)
 *   DAT_00553e5c  page to return to after the file-list page (load/skirmish)
 *   DAT_0057d3c4 / DAT_00591548  file-list scroll/selection (deferred) */
static int g_last_selected_ack;   /* DAT_0053f648 */
static int g_mp_init_flag;         /* DAT_0058fe10 */
static int g_loadlist_dir_flag;    /* DAT_0057d320 */
static int g_filelist_return_page; /* DAT_00553e5c */

void engine_menu_input_bind_redraw_fn(engine_menu_redraw_fn fn)
{
    g_redraw_fn = fn ? fn : redraw_stub;
}

void engine_menu_input_bind_quit_fn(engine_menu_quit_fn fn)
{
    g_quit_fn = fn ? fn : quit_stub;
}

void engine_menu_input_set_sp_available(int value) { g_sp_available = value; }

int  engine_menu_input_last_redraw_mode(void) { return g_stub_last_redraw_mode; }
int  engine_menu_input_quit_count(void)       { return g_stub_quit_count; }
void engine_menu_input_reset_counters(void)
{
    g_stub_last_redraw_mode = 0;
    g_stub_quit_count = 0;
}

/* ─── click actions ──────────────────────────────────────────────── */

/* Port of FUN_00432b05 — the main-menu (page 1) click action.  `param`
 * is the clicked button's DAT_0059154c value:
 *   1 = Single player → "Your options" (page 2)
 *   2 = Multiple players → MP setup (deferred)
 *   3 = Exit game → quit latch
 *   4 = Lords of Magic? → promo movie (deferred) */
static void menu_action_page1(int param)
{
    /* Common prefix (retail lines 10-13). */
    g_loadlist_dir_flag = 0;        /* DAT_0057d320 = 0 */
    g_last_selected_ack = -1;       /* DAT_0053f648 = -1 (re-arm debounce) */
    s_slot_0055302c     = 2;        /* DAT_0055302c = 2 */
    g_mp_init_flag      = 0;        /* DAT_0058fe10 = 0 */

    if (param == 3) {
        /* Exit game — retail DAT_005aeb68 = 1 (deferred-shutdown latch). */
        g_quit_fn();
    } else if (param == 2) {
        /* Multiple players — MP session bring-up (0x4978ad save-pump,
         * 0x4b7250 session probe, page 4 lobby vs page 10 fallback).
         * The MP subsystem is unported; the page transition depends on
         * its result so we defer the whole branch.  Faithful behavior is
         * restored when MP lands. */
        g_redraw_fn(2);
        /* TODO(mp): 0x4978ad / 0x4b7250 → page 4 (lobby) or
         * page 10 (no-session), + s011_02.wav cue. */
    } else if (param == 1) {
        /* Single player — go to "Your options" (page 2).  Retail routes
         * to the deferred profile-select page 10 when SP isn't available
         * (DAT_005c9a74 outside [1,2]). */
        g_redraw_fn(2);
        if (g_sp_available < 1 || 2 < g_sp_available) {
            engine_title_render_set_page(10);
        } else {
            engine_title_render_set_page(2);
        }
        /* TODO(saves): 0x49bae9(0, player, &DAT_0053f1e0) — profile /
         * save-slot scan that backs the page-2 list affordances.  Page 2
         * renders without it (static buttons); deferred. */
    } else if (param == 4) {
        /* Lords of Magic? — plays lom.smk (Smacker promo).  Movie
         * playback chain (0x42d91b) is wired for the boot intro but
         * the menu-triggered cutscene path isn't ported yet. */
        g_redraw_fn(2);
        /* TODO(movie): 0x42d91b("lom.smk", …) promo cutscene. */
    }
}

/* Port of FUN_00432cc8 — the "Your options" (page 2) click action:
 *   1 = Play Now!   → page 5 (quick-start)
 *   2 = Load a game → file-list page 3
 *   3 = Custom game → page 4
 *   4 = Back.       → main menu (page 1)
 *   5 = Skirmish!   → page 4 (skirmish variant, substate 3) */
static void menu_action_page2(int param)
{
    g_last_selected_ack = -1;       /* DAT_0053f648 = -1 */

    if (param == 4) {
        /* Back — return to the main menu. */
        engine_title_render_set_page(1);
        g_redraw_fn(2);
        g_loadlist_dir_flag = 0;    /* DAT_0057d320 = 0 */
    } else if (param == 1) {
        /* Play Now! — quick-start page 5. */
        engine_title_render_set_page(5);
        g_redraw_fn(2);
    } else if (param == 2) {
        /* Load a game — populate the save-list then open file-list page 3.
         * The list-populate (0x4aecf0 over the .sav/.svb/.sva set) + fade
         * (0x402009) are deferred; the page transition + return-page
         * bookmark are faithful. */
        g_filelist_return_page = engine_title_render_page();  /* DAT_00553e5c */
        g_loadlist_dir_flag    = 1;                           /* DAT_0057d320 = 1 */
        engine_title_render_set_page(3);
        g_redraw_fn(2);
        /* TODO(saves): 0x4aecf0(<pattern>,0) save enumeration +
         * 0x402009 fade-in; DAT_0057d3c4 / DAT_00591548 list state. */
    } else if (param == 3) {
        /* Custom game — page 4. */
        engine_title_render_set_page(4);
        s_slot_0055302c = 2;        /* DAT_0055302c = 2 */
        g_redraw_fn(2);
        g_loadlist_dir_flag = 0;
        /* TODO(audio/transition): 0x402009 fade + s011_02.wav cue. */
    } else if (param == 5) {
        /* Skirmish! — page 4, substate 3. */
        engine_title_render_set_page(4);
        s_slot_0055302c = 3;        /* DAT_0055302c = 3 */
        g_redraw_fn(2);
        g_loadlist_dir_flag = 0;
        /* TODO(audio/transition): 0x402009 fade + s011_02.wav cue. */
    }
}

/* Port of FUN_00433461 — the page-5 ("Play Now!" campaign chooser) click
 * action.  Both buttons share it; `param` selects the campaign and it
 * advances to the page-4 setup screen ("Choose your title and your shield"):
 *   0 = Original campaign  (DAT_0053f258 = 0)
 *   1 = The new campaign   (DAT_0053f258 = 2)
 * The chosen campaign is recorded in the shared save-schema scalars
 * DAT_0053f640 / DAT_0053f258 (read later by the unported game-setup path);
 * DAT_0055302c = 1 marks the full-game substate. */
static void menu_action_page5(int param)
{
    engine_title_render_set_page(4);          /* DAT_005530f0 = 4 */
    g_redraw_fn(2);                           /* DAT_0057d340 = 2 */
    g_loadlist_dir_flag = 1;                  /* DAT_0057d320 = 1 */
    s_slot_0055302c     = 1;                  /* DAT_0055302c = 1 */
    s_slot_0053f640     = param;              /* DAT_0053f640 = campaign id */
    s_slot_0053f258     = (param == 1) ? 2 : 0;   /* DAT_0053f258 */
    /* TODO(transition/audio): retail also runs the 0x402009 fade, a
     * camera/scroll reset (0x427d19), the 0x401f49 + _DAT_005533f0=1 setup,
     * and the s011_02.wav cue (0x427990) — deferred like the page-2
     * transitions; the page change + campaign scalars are faithful. */
}

/* ─── page-7 ("game setup") click actions ────────────────────────── */

/* Port of FUN_004337b4 — the country-selector scrollbar (the two arrow
 * hotspots, type-2 auto-repeat).  `param` is -1 (up) or +1 (down); the
 * scroll + selected-country recompute + dirty marking live in
 * engine_title_render (the state owner).  Retail raises DAT_0057d340 = 2
 * only when the scroll actually moves, which the return value gates. */
static void menu_action_p7_scroll(int param)
{
    if (engine_title_render_country_scroll(param)) {
        g_redraw_fn(2);             /* DAT_0057d340 = 2 */
    }
}

/* Port of FUN_00433905 — a country-list row click (`param` 1..5 → visible
 * row 0..4). */
static void menu_action_p7_country(int param)
{
    engine_title_render_country_select(param - 1);
    g_redraw_fn(2);                 /* DAT_0057d340 = 2 */
}

/* Port of FUN_004335f0 — the carved Cancel / Start / Defaults buttons.
 *   1 = Cancel   → back to the main menu (page 1)
 *   2 = Start    → launch the game (deferred — game-start subsystem)
 *   3 = Defaults → reset the option tabs to defaults (deferred) */
static void menu_action_p7_button(int param)
{
    g_last_selected_ack = -1;       /* DAT_0053f648 = -1 */

    if (param == 1) {
        /* Cancel — return to the main menu.  Retail also reloads the
         * misc-sel sheet (0x499034, already ported as gfx_load_misc_sheet)
         * and runs the new-game-state reset 0x499a1c(0); for a multiplayer
         * setup (DAT_00553030 != 0) it tears the session down first
         * (0x4b743b / 0x43e8f4).  Those are deferred — the page change is
         * the faithful, observable part. */
        engine_title_render_set_page(1);
        g_redraw_fn(2);
        /* TODO(setup-exit): 0x499a1c(0) reset; MP teardown 0x4b743b/0x43e8f4. */
    } else if (param == 2) {
        /* Start — finalize the roster (0x499dc3), load the opening stage
         * (0x4329ec) and drop into the strategic map (DAT_0052afa4 = 1),
         * gated on DAT_00553f98 <= DAT_0053e8a4.  The game-start subsystem
         * is unported; deferred. */
        g_redraw_fn(2);
        /* TODO(game-start): 0x499dc3 + 0x4329ec; DAT_0052afa4 = 1. */
    } else if (param == 3) {
        /* Defaults — re-apply the default option values (0x4ae539) and
         * recompute the AI roster / preview (0x4ae5e2 / 0x433cc1), then
         * mark the tab strip dirty.  The recompute is deferred; marking the
         * tabs dirty re-renders them (unchanged values) which is harmless. */
        engine_title_render_setup_mark_tabs_dirty();   /* DAT_0055ce94 = 1 */
        g_redraw_fn(2);
        /* TODO(defaults): 0x4ae539 default-options apply + 0x4ae5e2/0x433cc1. */
    }
}

/* Per-tab option value counts (retail DAT_004d5560), read by the option-tab
 * click to size the page-9 dropdown. */
static const int k_p7_tab_value_count[12] = {
    2, 2, 4, 2, 4, 4, 6, 4, 5, 3, 7, 2,
};

/* Port of FUN_00433998 — an option-tab click (`param` 1..12).  Opens the
 * per-option detail screen (page 9), bookmarking the page to return to and
 * publishing the dropdown state (edited tab + its value count) for the
 * page-9 render. */
static void menu_action_p7_tab(int param)
{
    g_last_selected_ack = -1;       /* DAT_0053f648 = -1 */

    if (engine_title_render_page() != 9) {
        g_filelist_return_page = engine_title_render_page();  /* DAT_00553e5c */
        engine_title_render_set_page(9);                      /* DAT_005530f0 = 9 */
    }

    /* Detail state (retail DAT_0055306c / DAT_00553fb4): the edited tab
     * (param 1..12 → 0..11) and its value count, with tab 2 (the player-
     * count option) clamped to one below the live roster size.  retail also
     * zeroes _DAT_00553530 (the dropdown's scroll origin) here; that scroll
     * is part of the still-deferred page-9 input arm, so it isn't modeled. */
    int tab = param - 1;                            /* DAT_0055306c */
    int count = k_p7_tab_value_count[tab];          /* DAT_004d5560[tab] */
    if (tab == 2 && (int)s_slot_0053e8a4 < 5) {
        count = (int)s_slot_0053e8a4 - 1;
    }
    engine_title_render_set_option_detail(tab, count, g_filelist_return_page);

    g_redraw_fn(2);                 /* DAT_0057d340 = 2 */
}

/* ─── hit-test primitives ────────────────────────────────────────── */

/* Half-open box test shared by hover + click (retail inlines the same
 * four comparisons in FUN_0040e5e7 and FUN_0040e3ee). */
static int hotspot_contains(const engine_menu_hotspot_t *e, int ox, int oy,
                            int cx, int cy)
{
    return e->x0 + ox <= cx && cx < e->x1 + ox &&
           e->y0 + oy <= cy && cy < e->y1 + oy;
}

int engine_menu_hotspot_hover(int ox, int oy,
                              const engine_menu_hotspot_t *table, int count,
                              int cursor_x, int cursor_y)
{
    for (int i = 0; i < count; i++) {
        if (hotspot_contains(&table[i], ox, oy, cursor_x, cursor_y))
            return i + 1;
    }
    return 0;
}

int engine_menu_hotspot_click(int ox, int oy,
                              const engine_menu_hotspot_t *table, int count,
                              const engine_menu_click_in_t *in)
{
    for (int i = 0; i < count; i++) {
        const engine_menu_hotspot_t *e = &table[i];
        if (!hotspot_contains(e, ox, oy, in->cursor_x, in->cursor_y))
            continue;

        int fire = 0;
        if (e->button_type == 1) {
            /* Press edge (retail DAT_004eafb4). */
            fire = in->lmb_newly_pressed != 0;
        } else if (e->button_type == 3) {
            /* Release edge (retail DAT_004e65d8). */
            fire = in->lmb_newly_released != 0;
        } else if (e->button_type == 2) {
            /* Held / repeat — fire on press or dblclk, else on the 320 ms
             * tick while held (retail's DAT_0057d3c8 auto-repeat).  Note:
             * retail's type-2 arm has a pointer/index desync on the
             * held-but-not-firing fall-through; no title-menu page uses
             * type 2, so the clean-loop form here is exact for pages 1/2
             * and adequate for the others when they land. */
            if (in->lmb_held || in->lmb_newly_pressed || in->lmb_dblclk) {
                if (in->lmb_newly_pressed || in->lmb_dblclk)
                    fire = 1;
                else if (in->tick_320ms)
                    fire = 1;
            }
        }

        if (fire) {
            if (e->action)
                e->action(e->param);  /* retail stores param→DAT_0059154c first */
            return i + 1;
        }
        /* Inside but edge not satisfied — keep scanning (matches retail's
         * goto-advance for types 1/3). */
    }
    return 0;
}

/* ─── page hotspot tables (retail DAT_004dcb48 / DAT_004dcba8) ─────── */

/* Page 1 (main menu) — 4 buttons.  Boxes match the rendered rows
 * (x=0xe0, w=0xc0, h=0x18; rows step 0x24).  Button 3 ("Lords of
 * Magic?") is release-triggered (type 3); the rest are press (type 1).
 * Action params: 1=Single, 2=Multiple, 4=LoM, 3=Exit. */
static const engine_menu_hotspot_t k_page1_hotspots[4] = {
    { 0xe0, 0x5b, 0x1a0, 0x73, menu_action_page1, 1, 1 },  /* Single player   */
    { 0xe0, 0x7f, 0x1a0, 0x97, menu_action_page1, 1, 2 },  /* Multiple players*/
    { 0xe0, 0xa3, 0x1a0, 0xbb, menu_action_page1, 3, 4 },  /* Lords of Magic? */
    { 0xe0, 0xc7, 0x1a0, 0xdf, menu_action_page1, 1, 3 },  /* Exit game       */
};

/* Page 2 ("Your options") — 5 buttons, all press-triggered.
 * Params: 1=Play Now!, 2=Load, 5=Skirmish!, 3=Custom, 4=Back. */
static const engine_menu_hotspot_t k_page2_hotspots[5] = {
    { 0xe0, 0x5b, 0x1a0, 0x73,  menu_action_page2, 1, 1 },  /* Play Now!   */
    { 0xe0, 0x7f, 0x1a0, 0x97,  menu_action_page2, 1, 2 },  /* Load a game */
    { 0xe0, 0xa3, 0x1a0, 0xbb,  menu_action_page2, 1, 5 },  /* Skirmish!   */
    { 0xe0, 0xc7, 0x1a0, 0xdf,  menu_action_page2, 1, 3 },  /* Custom game */
    { 0xe0, 0xeb, 0x1a0, 0x103, menu_action_page2, 1, 4 },  /* Back.       */
};

/* Page 5 ("Play Now!" campaign chooser, retail DAT_004dccf8) — 2 buttons,
 * both firing menu_action_page5 on the press edge.  Boxes match the
 * rendered side-by-side buttons (left x∈[0x70,0x110), right x∈[0x170,0x210);
 * y∈[0x76,0x8e)).  Params: 0 = Original campaign, 1 = The new campaign. */
static const engine_menu_hotspot_t k_page5_hotspots[2] = {
    { 0x70,  0x76, 0x110, 0x8e, menu_action_page5, 1, 0 },  /* Original campaign */
    { 0x170, 0x76, 0x210, 0x8e, menu_action_page5, 1, 1 },  /* The new campaign  */
};

/* Page 7 ("game setup") — three retail tables dispatched in order:
 *
 *   DAT_004dcd28 (3) — the carved Cancel / Start / Defaults buttons,
 *                      press-triggered, params 1/2/3.
 *   DAT_004dcd88 (7) — the country selector: two type-2 (auto-repeat)
 *                      scrollbar arrows (params -1/+1) then five
 *                      press-triggered country rows (params 1..5).
 *   DAT_004dce30 (12)— the option-tab grid (4 cols × 3 rows, column-major),
 *                      press-triggered, params 1..0xc.
 *
 * Boxes are the verbatim short[] from the binary.  Page 7 has no
 * hover-highlight pass (FUN_004ba9c8's page-7 arm omits the FUN_0040e5e7
 * call), so there is no k_page7 hover lookup. */
static const engine_menu_hotspot_t k_page7_buttons[3] = {
    { 165, 181, 241, 236, menu_action_p7_button, 1, 1 },  /* Cancel   */
    { 243, 181, 319, 236, menu_action_p7_button, 1, 2 },  /* Start    */
    { 321, 181, 397, 236, menu_action_p7_button, 1, 3 },  /* Defaults */
};

static const engine_menu_hotspot_t k_page7_country[7] = {
    { 603, 140, 624, 160, menu_action_p7_scroll,  2, -1 },  /* scroll up   */
    { 603, 209, 624, 229, menu_action_p7_scroll,  2,  1 },  /* scroll down */
    { 496, 141, 601, 157, menu_action_p7_country, 1,  1 },  /* row 0 */
    { 496, 157, 601, 173, menu_action_p7_country, 1,  2 },  /* row 1 */
    { 496, 173, 601, 189, menu_action_p7_country, 1,  3 },  /* row 2 */
    { 496, 189, 601, 205, menu_action_p7_country, 1,  4 },  /* row 3 */
    { 496, 205, 601, 221, menu_action_p7_country, 1,  5 },  /* row 4 */
};

static const engine_menu_hotspot_t k_page7_tabs[12] = {
    { 170, 280, 266, 328, menu_action_p7_tab, 1, 1 },   /* col 0, row 0 */
    { 170, 353, 266, 401, menu_action_p7_tab, 1, 2 },   /* col 0, row 1 */
    { 170, 426, 266, 474, menu_action_p7_tab, 1, 3 },   /* col 0, row 2 */
    { 290, 280, 386, 328, menu_action_p7_tab, 1, 4 },   /* col 1, row 0 */
    { 290, 353, 386, 401, menu_action_p7_tab, 1, 5 },   /* col 1, row 1 */
    { 290, 426, 386, 474, menu_action_p7_tab, 1, 6 },   /* col 1, row 2 */
    { 410, 280, 506, 328, menu_action_p7_tab, 1, 7 },   /* col 2, row 0 */
    { 410, 353, 506, 401, menu_action_p7_tab, 1, 8 },   /* col 2, row 1 */
    { 410, 426, 506, 474, menu_action_p7_tab, 1, 9 },   /* col 2, row 2 */
    { 530, 280, 626, 328, menu_action_p7_tab, 1, 10 },  /* col 3, row 0 */
    { 530, 353, 626, 401, menu_action_p7_tab, 1, 11 },  /* col 3, row 1 */
    { 530, 426, 626, 474, menu_action_p7_tab, 1, 12 },  /* col 3, row 2 */
};

/* ─── per-page dispatcher (FUN_004ba9c8, 0x1f arm) ───────────────── */

int engine_menu_title_input(const engine_menu_click_in_t *in)
{
    int page = engine_title_render_page();

    if (page == 1) {
        if (engine_menu_hotspot_click(0, 0, k_page1_hotspots, 4, in))
            return 1;
        engine_title_render_set_selected_button(
            engine_menu_hotspot_hover(0, 0, k_page1_hotspots, 4,
                                      in->cursor_x, in->cursor_y));
    } else if (page == 2) {
        if (engine_menu_hotspot_click(0, 0, k_page2_hotspots, 5, in))
            return 1;
        engine_title_render_set_selected_button(
            engine_menu_hotspot_hover(0, 0, k_page2_hotspots, 5,
                                      in->cursor_x, in->cursor_y));
    } else if (page == 5) {
        if (engine_menu_hotspot_click(0, 0, k_page5_hotspots, 2, in))
            return 1;
        engine_title_render_set_selected_button(
            engine_menu_hotspot_hover(0, 0, k_page5_hotspots, 2,
                                      in->cursor_x, in->cursor_y));
    } else if (page == 7) {
        /* Three tables dispatched in order (buttons → country → tabs);
         * the first hotspot whose edge fires consumes the click.  No hover
         * pass for this page (retail omits FUN_0040e5e7). */
        if (engine_menu_hotspot_click(0, 0, k_page7_buttons, 3, in))
            return 1;
        if (engine_menu_hotspot_click(0, 0, k_page7_country, 7, in))
            return 1;
        if (engine_menu_hotspot_click(0, 0, k_page7_tabs, 12, in))
            return 1;
    }
    return 0;
}
