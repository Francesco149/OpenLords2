/*
 * OpenLords2 — title-menu input + page navigation.  See header for the
 * call topology and the retail provenance of each piece.
 *
 * Ports: FUN_0040e5e7 (hover), FUN_0040e3ee (click dispatch),
 * FUN_00432b05 (page-1 action), FUN_00432cc8 (page-2 action),
 * FUN_00433461 (page-5 campaign-select action), FUN_004335f0 / FUN_004337b4
 * / FUN_00433905 / FUN_00433998 (the page-7 setup-screen button / scrollbar
 * / country-row / option-tab actions), FUN_00433ce4 / FUN_00433a23 (the
 * page-9 dropdown hover hit-test + value pick), and the pages-1/2/5/7/9 arm
 * of FUN_004ba9c8 (per-page input dispatcher).
 */

#include <stddef.h>

#include "engine_menu_input.h"
#include "engine_title_render.h"
#include "engine_button.h"         /* engine_button_rec_t (page-3 strip) */
#include "engine.h"                /* g_sim_mp_active (DAT_00553030) */
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

/* DAT_005cd41c — the page-3 "Load" request latch.  FUN_004342f3 writes
 * 100; the deferred load-confirm pump (0x4ad9f0, deep load →
 * game-start path) consumes it.  Modeled here so the Load button's
 * faithful write is observable to tests; nothing in the drop-in acts on
 * it yet. */
static int g_filelist_load_request; /* DAT_005cd41c */

int engine_menu_input_filelist_load_request(void) { return g_filelist_load_request; }

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

/* ─── page-4 ("Choose your title and your shield") click actions ──── */

/* Port of FUN_00432ee6 — a shield click (`param` 1..5), single-player
 * branch (DAT_00553030 == 0).  Claims that shield for the current player
 * (the flashing slot jumps to it) and asks for a redraw.
 *
 * Deferred — the roster recompute: retail also runs FUN_0049caaa
 * (engine_npc_schedule_init) to re-seat the AI players from the new
 * shield assignment, and the strategic colour-table write inside
 * FUN_00432fab; both are strategic-roster state the page-4 render does
 * not read (same deferral as the page-9 pick + page-7 defaults).  The
 * multiplayer branches (DAT_00553030 != 0) gate the claim on a flash
 * timer and play sound cues 5/7/3 — not ported (no MP). */
static void menu_action_p4_shield(int param)
{
    engine_title_render_choose_title_assign_shield(param);   /* FUN_00432fab */
    g_redraw_fn(2);                 /* DAT_0057d340 = 2 */
    /* TODO(roster): FUN_0049caaa AI re-seat + FUN_00432fab colour table. */
}

/* Port of FUN_00433155 — the page-4 Back / Continue buttons, single-player
 * branch.  `param` is 1 (Back) or 2 (Continue).
 *
 * Retail prologue (both buttons): DAT_0053f648 = -1 (re-arm the click
 * debounce) and FUN_0049bae9(999, DAT_0057c8cc, &DAT_0053f1e0) — commit the
 * typed name into the current player's seat (player_seat_assign, ported).
 * The name commit writes strategic-roster state the page-4 / page-7 render
 * reads from the synthetic seed, so it is deferred with the roster like the
 * page-1 profile scan.
 *
 * Continue is gated in retail on (DAT_00553030 == 0 || DAT_0057c940 != 0):
 * single-player always passes; the multiplayer path needs the Continue-
 * enable timer (0x4bbe51).  We handle the single-player ladder:
 *   - DAT_0057d320 == 1 (Play Now! quick-start) → finalize roster + load the
 *     opening stage (0x499e5d / 0x4329ec) and drop into the strategic
 *     map (DAT_0052afa4 = 1) — the game-start subsystem, deferred.
 *   - DAT_0055302c == 3 (Skirmish!) → the skirmish save/load page 0xc, its
 *     subsystem (0x4263ad / 0x42b919 …) deferred.
 *   - else (Custom game) → the page-7 game-setup screen.  This is the live
 *     path.
 * Continue's tail 0x4330c6 (shield-colour refresh, strategic) and the
 * 0x43eda0(3,0) sound cue are deferred. */
static void menu_action_p4_button(int param)
{
    g_last_selected_ack = -1;       /* DAT_0053f648 = -1 */
    /* TODO(roster): FUN_0049bae9(999, DAT_0057c8cc, &DAT_0053f1e0) name commit. */

    if (param == 1) {
        /* Back → main menu (page 1).  Retail also clears DAT_0057a0f0 /
         * DAT_0057c8fc, reloads the misc-sel sheet (FUN_00499034, ported as
         * gfx_load_misc_sheet) and runs the new-game reset 0x499a1c(0)
         * [+ MP teardown 0x4b743b / 0x43e8f4 when DAT_00553030 != 0];
         * those match the page-7 Cancel deferral. */
        engine_title_render_set_page(1);
        g_redraw_fn(2);             /* DAT_0057d340 = 2 */
        /* TODO(setup-exit): FUN_00499034 misc reload + 0x499a1c(0) reset. */
        return;
    }

    if (param == 2 && g_sim_mp_active == 0) {
        if (g_loadlist_dir_flag == 1) {
            /* Play Now! quick-start — game-start subsystem (deferred). */
            g_redraw_fn(2);
            /* TODO(game-start): 0x499e5d + 0x4329ec; DAT_0052afa4 = 1. */
            return;
        }
        if (s_slot_0055302c == 3) {
            /* Skirmish! — the page-0xc save/load list (deferred). */
            g_redraw_fn(2);
            /* TODO(skirmish): page-0xc bring-up. */
            return;
        }
        /* Custom game — the page-7 game-setup screen. */
        engine_title_render_set_page(7);
        g_redraw_fn(2);             /* DAT_0057d340 = 2 */
        /* TODO(roster/audio): 0x4330c6 shield-colour refresh +
         * 0x43eda0(3, 0) sound cue. */
        return;
    }
    /* Continue in a multiplayer setup (DAT_00553030 != 0) is gated on the
     * Continue-enable timer and routes to pages 6/8/0xb — deferred with MP. */
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
        /* Defaults — re-apply the default option values (FUN_004ae539) and
         * clamp the player count to the live roster size (FUN_004ae5e2),
         * then mark the tab strip dirty so the refresh repaints the new
         * value caps.  Retail's roster/preview recompute tail (FUN_00433cc1
         * → DAT_0053f268 + 0x49caaa) is deferred with the roster subsystem;
         * the multiplayer-only sound cue (0x43eda0) is likewise deferred. */
        engine_title_render_setup_apply_defaults();        /* FUN_004ae539 */
        engine_title_render_setup_clamp_player_count((int)s_slot_0053e8a4); /* FUN_004ae5e2 */
        engine_title_render_setup_mark_tabs_dirty();       /* DAT_0055ce94 = 1 */
        g_redraw_fn(2);
        /* TODO(roster): FUN_00433cc1 recompute (DAT_0053f268 + 0x49caaa). */
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

/* Port of FUN_00433a23 — pick a value from the open page-9 dropdown.
 * `hover` is the 1-based highlighted row (FUN_00433ce4's result), so the
 * stored value is `hover - 1`.  Writes the value back to the edited option,
 * then closes the dropdown by returning to the bookmarked page. */
static void menu_action_p9_pick(int hover)
{
    engine_title_render_option_detail_pick(hover - 1);   /* FUN_00433ba2 */

    /* TODO(mp): retail plays sound cue 9 (0x43eda0(9, 0)) on the pick when
     * a multiplayer session is active (DAT_00553030 != 0); single-player is
     * silent, and the MP/audio path isn't ported. */

    /* Close: DAT_005530f0 = DAT_00553e5c (the bookmarked underlay page). */
    engine_title_render_set_page(engine_title_render_option_detail_bookmark());
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

/* ─── page-3 ("Loading a game." file list) click actions ─────────── */

/* Port of FUN_004342f3 — the "Load" gauntlet button.  Sets the load
 * request latch (DAT_005cd41c = 100); the actual list-entry load +
 * game-start runs off the deferred confirm pump (0x4ad9f0), so here
 * the faithful write is all that lands. */
static void menu_action_p3_load(void)
{
    g_filelist_load_request = 100;        /* DAT_005cd41c = 100 */
}

/* Port of FUN_00434308 — the "Cancel" gauntlet button.  On the title
 * menu (DAT_004eac50 == 0x1f) this returns to the page that opened the
 * file list (DAT_005530f0 = DAT_00553e5c).  Retail's else-branch handles
 * the in-game save/load dialog (DAT_004eac50 = DAT_0057c90c) — a
 * different screen state the drop-in doesn't drive here. */
static void menu_action_p3_cancel(void)
{
    engine_title_render_set_page(g_filelist_return_page);  /* DAT_005530f0 */
    g_redraw_fn(2);                                        /* DAT_0057d340 = 2 */
}

/* Port of FUN_00434346 — the scroll up/down arrows.  `param0` is the
 * step (±3), `param1` the layout mode (1 for the page-3 grid). */
static void menu_action_p3_scroll(int param0, int param1)
{
    engine_title_render_filelist_scroll(param0, param1);
    g_redraw_fn(2);                                        /* DAT_0057d340 = 2 */
}

/* Action dispatcher for the page-3 button strip — maps the record's
 * retail action code (the address baked into DAT_004ddd78) to the ported
 * handler, mirroring retail's dispatch through the record's function
 * pointer. */
static void filelist_button_dispatch(uint32_t action, int param0, int param1)
{
    switch (action) {
    case 0x004342f3: menu_action_p3_load();                break;  /* Load   */
    case 0x00434308: menu_action_p3_cancel();              break;  /* Cancel */
    case 0x00434346: menu_action_p3_scroll(param0, param1); break; /* scroll */
    default: break;
    }
}

/* ─── button-record hit-test (FUN_0040da1e) ──────────────────────── */

/* Auto-repeat acceleration gate — retail DAT_004d2748, dumped from the
 * binary (px @ 0x4d2748).  While a type-4 button is held, its repeat
 * counter (the record's `pad_e` byte) increments once per input frame;
 * the action fires only when this table's entry for the counter is
 * non-zero.  Counters < 8 never fire (the initial hold delay); from 8
 * the gate thins from "every 6th frame" toward "every 2nd", and once
 * the counter saturates at 0x2f it fires every frame (max scroll speed).
 * Indices 0..7 are never consulted (the < 8 early-out); kept for a
 * verbatim 0x30-entry table. */
static const uint8_t k_button_repeat_gate[0x30] = {
    /* 0x00 */ 0,0,0,0,0,0,0,0,
    /* 0x08 */ 1,0,0,0,0,0,1,0,
    /* 0x10 */ 0,0,0,1,0,0,0,1,
    /* 0x18 */ 0,0,1,0,0,1,0,0,
    /* 0x20 */ 1,0,1,0,1,0,0,1,
    /* 0x28 */ 1,1,1,1,1,1,1,0,
};

/* Half-open square box test: the record's `kind` field doubles as the
 * sprite side length (0x18 = 24, 0x20 = 32), so the hitbox is
 * [x+ox, x+kind+ox) × [y+oy, y+kind+oy) — retail FUN_0040da1e's four
 * inlined comparisons. */
static int button_rec_contains(const engine_button_rec_t *r, int ox, int oy,
                               int cx, int cy)
{
    return r->x + ox <= cx && cx < r->x + r->kind + ox &&
           r->y + oy <= cy && cy < r->y + r->kind + oy;
}

int engine_menu_button_dispatch(int ox, int oy,
                                engine_button_rec_t *recs, int count,
                                const engine_menu_click_in_t *in,
                                engine_menu_button_action_fn dispatch)
{
    /* Pass 1 — per-button press-frame animation countdown (runs for
     * every record every frame, cursor-independent).  A pressed button
     * counts its `pressed` timer down; on reaching 0 it returns to the
     * released frame (state = 1) and a type-5 button fires its action
     * then.  Type-3 records hold state cleared. */
    for (int i = 0; i < count; i++) {
        engine_button_rec_t *r = &recs[i];
        if (r->pressed == 0) {
            r->pad_e = 0;                       /* reset repeat counter */
        } else {
            r->pressed--;
            if (r->pressed == 0) {
                r->state = 1;
                r->pad_e = 0;
                if (r->type == 5 && dispatch)
                    dispatch(r->action, r->param0, r->param1);
            }
        }
        if (r->type == 3)
            r->state = 0;
    }

    /* Pass 2 — hit-test the cursor against each record and dispatch by
     * edge type; the first record under the cursor that fires wins. */
    for (int i = 0; i < count; i++) {
        engine_button_rec_t *r = &recs[i];
        if (!button_rec_contains(r, ox, oy, in->cursor_x, in->cursor_y))
            continue;

        if (r->type == 4) {
            /* Press / double-click → fire immediately with the click
             * flash (retail also plays 0x426216(1); the menu-click
             * cue is deferred project-wide). */
            if (in->lmb_newly_pressed || in->lmb_dblclk) {
                r->pressed = 3;
                r->pad_e   = 0;
                r->state   = 1;
                if (dispatch)
                    dispatch(r->action, r->param0, r->param1);
                return i + 1;
            }
            /* Held → auto-repeat, gated by the accel table. */
            if (!in->lmb_held)
                return 0;
            r->pressed = 3;
            if (in->repeat_tick) {
                r->pad_e++;
                if (r->pad_e < 0x30) {
                    if (r->pad_e < 8)
                        return 0;
                    if (k_button_repeat_gate[r->pad_e] == 0)
                        return 0;
                } else {
                    r->pad_e = 0x2f;
                }
                if (dispatch)
                    dispatch(r->action, r->param0, r->param1);
                return i + 1;
            }
            return 0;
        }
        if (r->type == 2) {
            /* Toggle on the first contact of a press-hold (pad_e 0). */
            r->pressed = 1;
            if (r->pad_e == 0) {
                r->state ^= 1;
                if (dispatch)
                    dispatch(r->action, r->param0, r->param1);
            }
            return i + 1;
        }
        if (r->type == 5) {
            /* Arm the long press-flash; the action fires when the
             * countdown ends in pass 1 (not here). */
            if (!in->lmb_newly_pressed && !in->lmb_dblclk)
                return 0;
            r->pressed = 0x14;
            r->state   = 1;
            return i + 1;
        }
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

/* Page 4 ("Choose your title and your shield", retail DAT_004dcc20) — 7
 * press-triggered hotspots: five shield slots then Back / Continue.  Boxes
 * are the verbatim short[] from the binary; shield params 1..5 (→ the
 * clicked design), Back param 1, Continue param 2.  The shield boxes sit at
 * x = (i-1)·0x58 + 0x70 (64 wide) over y∈[0x8c,0xd2); the two buttons match
 * the rendered 0xc0×0x18 bevel rects at (0x70,0xd7) / (0x150,0xd7).
 *
 * When the Back/Continue row is hidden (the short MP layout,
 * engine_title_render_choose_title_buttons_shown() == 0) the click pass
 * tests only the first 5 entries; the hover pass always tests all 7
 * (retail FUN_0040e5e7(…,7)). */
static const engine_menu_hotspot_t k_choose_title_buttons[7] = {
    { 0x070, 0x8c, 0x0b0, 0xd2, menu_action_p4_shield, 1, 1 },  /* shield 1 */
    { 0x0c8, 0x8c, 0x108, 0xd2, menu_action_p4_shield, 1, 2 },  /* shield 2 */
    { 0x120, 0x8c, 0x160, 0xd2, menu_action_p4_shield, 1, 3 },  /* shield 3 */
    { 0x178, 0x8c, 0x1b8, 0xd2, menu_action_p4_shield, 1, 4 },  /* shield 4 */
    { 0x1d0, 0x8c, 0x210, 0xd2, menu_action_p4_shield, 1, 5 },  /* shield 5 */
    { 0x070, 0xd7, 0x130, 0xf0, menu_action_p4_button, 1, 1 },  /* Back     */
    { 0x150, 0xd7, 0x210, 0xf0, menu_action_p4_button, 1, 2 },  /* Continue */
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

    /* FUN_004ba9c8 line 9: DAT_005aeb78 = 0 — reset the text editor's
     * enable gate each frame (the input-suppress timer DAT_0057a0cc is a
     * deferred MP/scroll nicety; in the drop-in input is never suppressed).
     * Each editable page arm sets it back to 1 via its name-tail, so the
     * WM_CHAR route only edits while a live field is on screen. */
    engine_title_render_title_editor_disable();

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
    } else if (page == 3) {
        /* Page-3 arm of FUN_004ba9c8 ("Loading a game." file list).  In
         * retail order: enable + stream the shared editor into the
         * filename field, then run the button-record hit-test over the
         * gauntlet Load/Cancel + scroll-arrow strip.  No hover pass
         * (retail omits FUN_0040e5e7 here).
         *
         * Deferred: 0x4ad9f0 (the load-confirm pump retail calls
         * between the two) — it only acts once the DAT_0057d3c4 confirm
         * timer is armed, on the deep load → game-start path. */
        engine_title_render_filelist_name_tail();
        int n = 0;
        engine_button_rec_t *recs =
            engine_title_render_filelist_button_table(&n);
        if (engine_menu_button_dispatch(0x60, 10, recs, n, in,
                                        filelist_button_dispatch))
            return 1;
    } else if (page == 4) {
        /* Page-4 arm of FUN_004ba9c8 ("Choose your title and your shield").
         * The click pass tests 5 or 7 hotspots depending on whether the
         * Back/Continue row is shown; the hover pass always tests all 7
         * (retail FUN_0040e5e7(…,7)).
         *
         * Deferred from retail's arm — none has a page-4 render consumer in
         * the drop-in:
         *   - FUN_00404a46 (the per-frame double-LFSR advance, ported as
         *     engine_rng_step) perturbs only global RNG state the title
         *     screen never reads;
         *   - 0x4bbe51 enables Continue (DAT_0057c940 = 1) once the 6 s
         *     join window elapses, multiplayer-only (DAT_00553030 != 0). */
        int n = engine_title_render_choose_title_buttons_shown() ? 7 : 5;
        if (engine_menu_hotspot_click(0, 0, k_choose_title_buttons, n, in))
            return 1;
        engine_title_render_set_selected_button(
            engine_menu_hotspot_hover(0, 0, k_choose_title_buttons, 7,
                                      in->cursor_x, in->cursor_y));
        /* Name-editor tail (DAT_005aeb78 = 1; FUN_00401f49;
         * FUN_0040210c(&DAT_0053f1e0, 0x1f)): enable the editor, recompute
         * its length, and stream its text into the name box so live
         * keystrokes (routed by the host WndProc) show up.  The 0x404a46
         * RNG prologue and the 0x4bbe51 MP join-timer are still deferred
         * (no page-4 render consumer in the drop-in). */
        engine_title_render_choose_title_name_tail();
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
    } else if (page == 9) {
        /* Page-9 arm of FUN_004ba9c8: the open option dropdown.
         *
         *   1. Hover hit-test (FUN_00433ce4) → highlight (DAT_00553114).
         *   2. A press-edge click on a highlighted value row picks it
         *      (FUN_00433a23: write back + close).
         *   3. Else dispatch the page-7 option-tab grid — clicking another
         *      tab re-targets the dropdown (menu_action_p7_tab re-publishes
         *      the detail state without re-bookmarking, since page == 9).
         *   4. Else any press edge closes the dropdown, returning to the
         *      bookmarked underlay page. */
        int hover = engine_title_render_option_detail_hover(in->cursor_x,
                                                            in->cursor_y);
        engine_title_render_set_selected_button(hover);   /* DAT_00553114 */

        if (in->lmb_newly_pressed && hover != 0) {
            menu_action_p9_pick(hover);
            return 1;
        }
        if (engine_menu_hotspot_click(0, 0, k_page7_tabs, 12, in))
            return 1;
        if (in->lmb_newly_pressed) {
            engine_title_render_set_page(
                engine_title_render_option_detail_bookmark());
            g_redraw_fn(2);             /* DAT_0057d340 = 2 */
            return 1;
        }
    }
    return 0;
}
