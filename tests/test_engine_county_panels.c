/*
 * Unit tests for engine_county_panels — the county control-panel detail
 * screens (population 0x14 / tax 0x15 / happiness 0x16 / rations 0x19):
 * the 2×2 action-grid hit-test (FUN_00438ceb), the shared dispatcher
 * case (42ff10.c:445-560), the rations stepper (FUN_0043a379) + set
 * action (FUN_0043a5a9), the history graph (FUN_004156a7), and the four
 * panel renderers against real retail assets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "t.h"
#include "engine_county_panels.h"
#include "engine_ingame_input.h"
#include "engine_pl8.h"
#include "gfx_dispatch.h"
#include "gfx_paint.h"
#include "gfx_glyph_table.h"
#include "gfx_text.h"
#include "io.h"
#include "localization.h"
#include "save.h"
#include "save_schema_globals.h"

#define CREC(key) (&g_per_army_768[(key) * 0x300])
#define CI32(p, o) (*(int32_t *)((p) + (o)))

/* ─── FUN_00438ceb — action-grid classify ───────────────────────────── */

static engine_county_grid_in_t grid_at(int x, int y)
{
    engine_county_grid_in_t in;
    memset(&in, 0, sizeof in);
    in.lmb_pressed   = 1;
    in.cursor_x      = x;
    in.cursor_y      = y;
    in.county_owner  = 3;
    in.current_realm = 3;
    return in;
}

int test_county_grid_quadrants_map_to_screens(void)
{
    engine_county_grid_in_t in;
    in = grid_at(0x200, 0xc0); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0x14);
    in = grid_at(0x250, 0xc0); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0x16);
    in = grid_at(0x200, 0xe0); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0x15);
    in = grid_at(0x250, 0xe0); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0x19);
    return 0;
}

int test_county_grid_gutter_and_bounds_not_consumed(void)
{
    /* The dead x-gutter between columns (0x224..0x237 inclusive). */
    engine_county_grid_in_t in = grid_at(0x230, 0xc0);
    T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0);
    /* Rect edges are exclusive on both sides (retail `<`/`>` pairs). */
    in = grid_at(0x1e7, 0xc0); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0);
    in = grid_at(0x276, 0xc0); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0);
    in = grid_at(0x200, 0xb5); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0);
    in = grid_at(0x200, 0xf1); T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0);
    return 0;
}

int test_county_grid_owner_gate(void)
{
    engine_county_grid_in_t in = grid_at(0x200, 0xc0);
    in.county_owner = 2;                     /* not the local player's */
    T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0);
    in.spectate = 1;                         /* DAT_0053f684 overrides */
    T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0x14);
    in.spectate = 0;
    in.county_owner = 3;
    in.lmb_pressed = 0;                      /* needs the press edge */
    T_ASSERT_EQ_I(engine_county_action_grid_classify(&in), 0);
    return 0;
}

/* ─── The shared dispatcher case (screens 0x14/0x15/0x16/0x19) ───────── */

static int g_hook_calls[8];
enum { H_BTN_B, H_BTN_A, H_RATIONS, H_GRID, H_SCROLL, H_TOGGLE, H_MINIMAP,
       H_TAB };

static int rec_hook_btn_b(const engine_ingame_input_in_t *in,
                          engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_BTN_B]++; return 0; }
static int rec_hook_btn_a(const engine_ingame_input_in_t *in,
                          engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_BTN_A]++; return 0; }
static int rec_hook_rations(const engine_ingame_input_in_t *in,
                            engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_RATIONS]++; return 0; }
static int rec_hook_grid(const engine_ingame_input_in_t *in,
                         engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_GRID]++; return 0; }
static int rec_hook_scroll(const engine_ingame_input_in_t *in,
                           engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_SCROLL]++; return 0; }
static int rec_hook_toggle(const engine_ingame_input_in_t *in,
                           engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_TOGGLE]++; return 0; }
static int rec_hook_minimap(const engine_ingame_input_in_t *in,
                            engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_MINIMAP]++; return 0; }
static int rec_hook_tab(const engine_ingame_input_in_t *in,
                        engine_ingame_state_t *st)
{ (void)in; (void)st; g_hook_calls[H_TAB]++; return 1; }

static int grid_switch_hook(const engine_ingame_input_in_t *in,
                            engine_ingame_state_t *st)
{ (void)in; g_hook_calls[H_GRID]++; st->screen = 0x16;
  st->demand_redraw = 2; return 1; }

static engine_ingame_hooks_t panel_hooks(void)
{
    engine_ingame_hooks_t h;
    memset(&h, 0, sizeof h);
    h.hud_btn_b      = rec_hook_btn_b;
    h.hud_btn_a      = rec_hook_btn_a;
    h.rations_arrows = rec_hook_rations;
    h.action_grid    = rec_hook_grid;
    h.scrollbar      = rec_hook_scroll;
    h.panel_toggles  = rec_hook_toggle;
    h.minimap_drag   = rec_hook_minimap;
    h.tab_strip      = rec_hook_tab;
    return h;
}

static void panel_dispatch(int screen, int rmb, int lmb_pressed,
                           const engine_ingame_hooks_t *h,
                           engine_ingame_state_t *st)
{
    engine_ingame_input_in_t in;
    memset(&in, 0, sizeof in);
    in.rmb_released = rmb;
    in.lmb_pressed  = lmb_pressed;
    memset(st, 0, sizeof *st);
    st->screen = screen;
    memset(g_hook_calls, 0, sizeof g_hook_calls);
    engine_ingame_input_dispatch(&in, st, h);
}

int test_county_panel_case_rmb_closes(void)
{
    engine_ingame_hooks_t h = panel_hooks();
    engine_ingame_state_t st;
    int screens[4] = { 0x14, 0x15, 0x16, 0x19 };
    for (int i = 0; i < 4; i++) {
        panel_dispatch(screens[i], 1, 0, &h, &st);
        T_ASSERT_EQ_I(st.screen, 0);
        T_ASSERT_EQ_I(st.demand_redraw, 2);
    }
    return 0;
}

int test_county_panel_case_chain_order_and_no_tail(void)
{
    engine_ingame_hooks_t h = panel_hooks();
    engine_ingame_state_t st;
    /* No click at all: the chain runs, nothing closes, and the shared
     * tab-strip tail must NOT run (retail's case body ends in goto). */
    panel_dispatch(0x14, 0, 1, &h, &st);
    T_ASSERT_EQ_I(st.screen, 0x14);
    T_ASSERT_EQ_I(g_hook_calls[H_BTN_B], 1);
    T_ASSERT_EQ_I(g_hook_calls[H_BTN_A], 1);
    T_ASSERT_EQ_I(g_hook_calls[H_GRID], 1);
    T_ASSERT_EQ_I(g_hook_calls[H_SCROLL], 1);
    T_ASSERT_EQ_I(g_hook_calls[H_TOGGLE], 1);
    T_ASSERT_EQ_I(g_hook_calls[H_MINIMAP], 1);
    T_ASSERT_EQ_I(g_hook_calls[H_RATIONS], 0);   /* 0x19 only */
    T_ASSERT_EQ_I(g_hook_calls[H_TAB], 0);       /* no shared tail */
    return 0;
}

int test_county_panel_case_rations_hook_only_on_0x19(void)
{
    engine_ingame_hooks_t h = panel_hooks();
    engine_ingame_state_t st;
    panel_dispatch(0x19, 0, 0, &h, &st);
    T_ASSERT_EQ_I(g_hook_calls[H_RATIONS], 1);
    T_ASSERT_EQ_I(st.screen, 0x19);
    panel_dispatch(0x15, 0, 0, &h, &st);
    T_ASSERT_EQ_I(g_hook_calls[H_RATIONS], 0);
    return 0;
}

int test_county_panel_case_action_grid_switches_panels(void)
{
    engine_ingame_hooks_t h = panel_hooks();
    h.action_grid = grid_switch_hook;
    engine_ingame_state_t st;
    panel_dispatch(0x14, 0, 1, &h, &st);
    T_ASSERT_EQ_I(st.screen, 0x16);              /* switched, not closed */
    T_ASSERT_EQ_I(st.demand_redraw, 2);
    T_ASSERT_EQ_I(g_hook_calls[H_SCROLL], 0);    /* chain short-circuits */
    return 0;
}

int test_county_panel_case_turn_busy_closes(void)
{
    engine_ingame_hooks_t h = panel_hooks();
    engine_ingame_input_in_t in;
    memset(&in, 0, sizeof in);
    in.turn_progress = 1;                        /* not idle */
    engine_ingame_state_t st;
    memset(&st, 0, sizeof st);
    st.screen = 0x16;
    memset(g_hook_calls, 0, sizeof g_hook_calls);
    engine_ingame_input_dispatch(&in, &st, &h);
    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT_EQ_I(st.demand_redraw, 2);
    T_ASSERT_EQ_I(g_hook_calls[H_BTN_B], 0);     /* chain skipped */
    return 0;
}

static int seal_click_hook(const engine_ingame_input_in_t *in,
                           engine_ingame_state_t *st)
{ (void)in; (void)st; return 1; }               /* FUN_0040e7e4 hit */

int test_county_panel_case_generic_click_closes(void)
{
    engine_ingame_hooks_t h = panel_hooks();
    h.generic_click = seal_click_hook;
    engine_ingame_state_t st;
    panel_dispatch(0x15, 0, 0, &h, &st);
    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT_EQ_I(st.demand_redraw, 2);
    return 0;
}

/* ─── FUN_0043a379 — rations stepper ────────────────────────────────── */

static engine_county_rations_in_t rations_at(int x, int y)
{
    engine_county_rations_in_t in;
    memset(&in, 0, sizeof in);
    in.lmb_pressed      = 1;
    in.lmb_held         = 1;
    in.hold_repeat_gate = 1;         /* mouse_event: press edge counts */
    in.cursor_x         = x;
    in.cursor_y         = y;
    in.county_owner     = 3;
    in.current_realm    = 3;
    in.ration_pct       = 50;
    return in;
}

int test_county_rations_arrows_step(void)
{
    int v = 0, drag = 0, changed = 0;
    engine_county_rations_in_t in = rations_at(0xc8 + 4, 0xdc + 4);
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 1);
    T_ASSERT_EQ_I(changed, 1);
    T_ASSERT_EQ_I(v, 49);
    T_ASSERT_EQ_I(drag, 0);
    in = rations_at(0x145 + 4, 0xdc + 4);
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 1);
    T_ASSERT_EQ_I(v, 51);
    return 0;
}

int test_county_rations_band_absolute_and_clamp(void)
{
    int v = 0, drag = 0, changed = 0;
    /* Band x∈[0xe0, 0x146): value = cursor_x − 0xe0. */
    engine_county_rations_in_t in = rations_at(0xe0 + 30, 0xdc + 4);
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 1);
    T_ASSERT_EQ_I(changed, 1);
    T_ASSERT_EQ_I(v, 30);
    T_ASSERT_EQ_I(drag, 1);
    in = rations_at(0xe0 + 0x64, 0xdc + 4);      /* band's right edge = 100 */
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 1);
    T_ASSERT_EQ_I(v, 100);
    /* x = 0x145 hits BOTH the band and the + arrow rects; retail gives
     * the arrow priority (local_1c wins over the band value). */
    in = rations_at(0x145, 0xdc + 4);
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 1);
    T_ASSERT_EQ_I(v, 51);
    T_ASSERT_EQ_I(drag, 0);
    return 0;
}

int test_county_rations_gates(void)
{
    int v = 0, drag = 0, changed = 0;
    /* LMB release edge blocks the whole handler. */
    engine_county_rations_in_t in = rations_at(0xc8 + 4, 0xdc + 4);
    in.lmb_released = 1;
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 0);
    /* Held without the mouse-event gate (and no repeat edge) blocks. */
    in = rations_at(0xc8 + 4, 0xdc + 4);
    in.hold_repeat_gate = 0;
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 0);
    /* ...but a double-click repeat edge re-enables it. */
    in.click_repeat = 1;
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 1);
    /* Not the county's owner: never consumed. */
    in = rations_at(0xc8 + 4, 0xdc + 4);
    in.county_owner = 1;
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 0);
    /* Outside every rect: not consumed. */
    in = rations_at(0x50, 0x50);
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 0);
    /* Arrow hit whose step lands on the current value → consumed,
     * unchanged (held-repeat frame without a press edge). */
    in = rations_at(0xc8 + 4, 0xdc + 4);
    in.lmb_pressed = 0;
    T_ASSERT_EQ_I(engine_county_rations_input(&in, &v, &drag, &changed), 1);
    T_ASSERT_EQ_I(changed, 0);
    return 0;
}

/* ─── FUN_0043a5a9 — ration set ─────────────────────────────────────── */

int test_county_ration_set_simple_write(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0057c93c = 4;
    CREC(4)[0x15f] = 50;
    /* +0x250 == 0 → no snap loop; the write + economy tail run. */
    T_ASSERT_EQ_I(engine_county_ration_set(4, 60, 0), 1);
    T_ASSERT_EQ_I((int)(int8_t)CREC(4)[0x15f], 60);
    /* A different county returns 0 (caller skips the re-render). */
    T_ASSERT_EQ_I(engine_county_ration_set(5, 10, 0), 0);
    T_ASSERT_EQ_I((int)(int8_t)CREC(5)[0x15f], 10);
    return 0;
}

int test_county_ration_set_snap_walks_to_clamp_when_outcome_never_flips(void)
{
    /* Active county (+0x250) with a nonzero fed outcome (+0x17c) that the
     * (zero-state) provisioning pass never changes: retail's snap loop
     * steps the SAME direction until the 100-try cap, ending clamped.
     * From 50 stepping +1 → parks at 100.  Behavior pin for the loop. */
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0057c93c = 4;
    CREC(4)[0x15f] = 50;
    CI32(CREC(4), 0x250) = 1;
    CI32(CREC(4), 0x17c) = 7;
    engine_county_ration_set(4, 51, 0);
    T_ASSERT_EQ_I((int)(int8_t)CREC(4)[0x15f], 100);
    return 0;
}

int test_county_ration_set_dead_drag_reverts(void)
{
    /* Same dead-outcome setup, but a DRAG (is_drag=1) to a specific
     * value: the loop reaches the target without an outcome flip and
     * reverts to the original percent. */
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0057c93c = 4;
    CREC(4)[0x15f] = 50;
    CI32(CREC(4), 0x250) = 1;
    CI32(CREC(4), 0x17c) = 7;
    engine_county_ration_set(4, 60, 1);
    T_ASSERT_EQ_I((int)(int8_t)CREC(4)[0x15f], 50);
    return 0;
}

/* ─── The arrow-button steppers (FUN_0043aa83/ab25, FUN_0043a23f/a307) ── */

int test_county_tax_step_clamps(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    CREC(4)[0xb9] = 0x31;
    engine_county_tax_step(4, 1);
    T_ASSERT_EQ_I(CREC(4)[0xb9], 0x32);
    engine_county_tax_step(4, 1);               /* at cap — stays */
    T_ASSERT_EQ_I(CREC(4)[0xb9], 0x32);
    CREC(4)[0xb9] = 0;
    engine_county_tax_step(4, -1);              /* at floor — stays */
    T_ASSERT_EQ_I(CREC(4)[0xb9], 0);
    CREC(4)[0xb9] = 5;
    engine_county_tax_step(4, -1);
    T_ASSERT_EQ_I(CREC(4)[0xb9], 4);
    return 0;
}

int test_county_wanted_step_clamps(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    CREC(4)[0x15e] = 4;
    engine_county_wanted_step(4, 1);
    T_ASSERT_EQ_I((int)(int8_t)CREC(4)[0x15e], 5);
    engine_county_wanted_step(4, 1);            /* at cap — stays */
    T_ASSERT_EQ_I((int)(int8_t)CREC(4)[0x15e], 5);
    CREC(4)[0x15e] = 0;
    engine_county_wanted_step(4, -1);           /* at floor — stays */
    T_ASSERT_EQ_I((int)(int8_t)CREC(4)[0x15e], 0);
    return 0;
}

int test_county_button_tables_shape(void)
{
    /* The .rdata extraction pins (r2 pxw @ 0x4dd790 / 0x4dd7c0). */
    engine_button_rec_t *t = engine_county_tax_buttons();
    T_ASSERT_EQ_I(t[0].x, 0xc0);  T_ASSERT_EQ_I(t[0].y, 0xa2);
    T_ASSERT_EQ_I(t[0].sprite, 0x15);
    T_ASSERT_EQ_I(t[0].kind, 0x18);
    T_ASSERT_EQ_I(t[0].type, 4);
    T_ASSERT_EQ_I((int)t[0].action, 0x43aa32);
    T_ASSERT_EQ_I(t[1].x, 0xe0);
    T_ASSERT_EQ_I((int)t[1].action, 0x43aad5);
    engine_button_rec_t *w = engine_county_wanted_buttons();
    T_ASSERT_EQ_I(w[0].x, 0x158); T_ASSERT_EQ_I(w[0].y, 0x82);
    T_ASSERT_EQ_I((int)w[0].action, 0x43a1e9);
    T_ASSERT_EQ_I(w[1].x, 0x170);
    T_ASSERT_EQ_I((int)w[1].action, 0x43a2b2);
    return 0;
}

int test_county_slider_render_null_safe(void)
{
    engine_county_panels_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    gfx_paint_bind_canvas(NULL, 0, 0);
    engine_county_ration_slider_render(&ctx);   /* all sheets NULL */
    return 0;
}

/* ─── FUN_004156a7 — history graph ──────────────────────────────────── */

int test_county_graph_ring_max_and_wrap(void)
{
    static uint8_t canvas[64 * 64];
    gfx_paint_bind_canvas(canvas, 64, 64);       /* bevel line target */

    static uint8_t hist[51200];
    memset(hist, 0, sizeof hist);
    engine_county_panels_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.history    = hist;
    ctx.county     = 2;
    ctx.hist_head  = 398;                        /* wraps 398→399→0 */
    ctx.hist_count = 3;

    /* Cell = slot*0x80 + (county-1)*8; +0 i32 pop, +4 u8 happiness. */
    CI32(hist, 398 * 0x80 + 8) = 10;
    CI32(hist, 399 * 0x80 + 8) = 250;
    CI32(hist,   0 * 0x80 + 8) = 30;
    hist[398 * 0x80 + 8 + 4] = 4;
    hist[399 * 0x80 + 8 + 4] = 90;
    hist[  0 * 0x80 + 8 + 4] = 12;

    T_ASSERT_EQ_I(engine_county_graph_render(&ctx, 0, 0, 0), 250);
    T_ASSERT_EQ_I(engine_county_graph_render(&ctx, 0, 0, 1), 90);
    gfx_paint_bind_canvas(NULL, 0, 0);
    return 0;
}

/* ─── The four renderers against real retail assets ─────────────────── */

enum { CANVAS_W = 640, CANVAS_H = 480 };

static int cp_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void cp_resolve_game_dir(char *dst, size_t cap)
{
    const char *env = getenv("OPENLORDS2_GAME_DIR");
    if (env && *env) { snprintf(dst, cap, "%s", env); return; }
    snprintf(dst, cap, "%s/vendor/original", OPENLORDS2_ROOT);
}

/* Shared asset bundle for the render tests. */
static uint8_t cp_canvas[CANVAS_W * CANVAS_H];
static uint8_t cp_font_title[0x8000];    /* fntl2_22.pl8 (DAT_005b2fa0) */
static uint8_t cp_font_menu[0x36b0];     /* fntl2_14.pl8 (DAT_005af8f0) */
static uint8_t cp_chrome[0x128e0];       /* panels.pl8   (DAT_0057d3d0) */
static uint8_t cp_graphs[200000];        /* graphs.pl8 */
static uint8_t cp_locbuf[120000];        /* l2.eng */

/* Returns 0 on success, 1 (already-failed) never, 2 = skip. */
static int cp_render_setup(engine_county_panels_ctx_t *ctx,
                           localization_state_t *loc)
{
    char gdir[1024];
    cp_resolve_game_dir(gdir, sizeof gdir);

    char sav[1200];
    snprintf(sav, sizeof sav, "%s/lastturn.sav", gdir);
    if (!cp_file_exists(sav))
        return 2;
    if (save_load(sav, "/tmp/openlords2_county_castles.dat") != 0)
        return 2;

    if (!engine_pl8_load_strategic_banks(gdir))
        return 2;

    char p[1400];
    snprintf(p, sizeof p, "%s/fntl2_22.pl8", gdir);
    if (io_read_file_at(p, cp_font_title, sizeof cp_font_title, 0) < 1)
        return 2;
    snprintf(p, sizeof p, "%s/fntl2_14.pl8", gdir);
    if (io_read_file_at(p, cp_font_menu, sizeof cp_font_menu, 0) < 1)
        return 2;
    snprintf(p, sizeof p, "%s/panels.pl8", gdir);
    if (io_read_file_at(p, cp_chrome, sizeof cp_chrome, 0) < 1)
        return 2;
    snprintf(p, sizeof p, "%s/graphs.pl8", gdir);
    if (io_read_file_at(p, cp_graphs, sizeof cp_graphs, 0) < 1)
        return 2;
    snprintf(p, sizeof p, "%s/l2.eng", gdir);
    if (io_read_file_at(p, cp_locbuf, sizeof cp_locbuf, 0) < 1)
        return 2;

    memset(cp_canvas, 0, sizeof cp_canvas);
    gfx_paint_bind_canvas(cp_canvas, CANVAS_W, CANVAS_H);
    gfx_text_set_screen_byte(0);
    gfx_dispatch_set_alt_clip(0, 0, 0);
    gfx_dispatch_set_default_font(cp_font_menu);

    loc->blob = cp_locbuf;
    loc->walker_offset = 0;

    memset(ctx, 0, sizeof *ctx);
    ctx->county_block    = g_per_army_768;
    ctx->realm_block     = g_player_strategic_352;
    ctx->history         = g_realm_history;
    ctx->hist_head       = (int)s_slot_00568da8;
    ctx->hist_count      = (int)s_slot_00553f30;
    ctx->hist_start_year = (int)s_slot_00553228;
    ctx->year            = (int)s_slot_00553edc;
    ctx->county          = (int)s_slot_0057c93c;
    ctx->map_id          = (int)s_slot_0053f034;
    ctx->advanced_mode   = (int)s_slot_0053f260;
    ctx->chrome          = cp_chrome;
    ctx->misc_sheet      = engine_pl8_misc_cty();
    ctx->graphs          = cp_graphs;
    ctx->system_sheet    = NULL;             /* seal draw guards NULL */
    ctx->title_font      = cp_font_title;
    ctx->menu_font       = cp_font_menu;
    ctx->glyph_table     = gfx_glyph_render_map();
    ctx->glyph_metrics   = gfx_glyph_measure_map();
    ctx->loc             = loc;
    return 0;
}

static int cp_painted(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (cp_canvas[x + y * CANVAS_W] != 0)
                n++;
    return n;
}

static void cp_dump(const char *name)
{
    char outdir[1200], outraw[1500];
    snprintf(outdir, sizeof outdir, "%s/runs/parity/county_panels",
             OPENLORDS2_ROOT);
    mkdir(outdir, 0755);
    snprintf(outraw, sizeof outraw, "%s/%s.raw", outdir, name);
    FILE *o = fopen(outraw, "wb");
    if (o) { fwrite(cp_canvas, 1, sizeof cp_canvas, o); fclose(o); }
}

int test_county_panel_population_renders(void)
{
    engine_county_panels_ctx_t ctx;
    localization_state_t loc;
    int rc = cp_render_setup(&ctx, &loc);
    if (rc == 2)
        T_SKIP("retail assets not available");
    engine_county_panel_population(&ctx);
    cp_dump("population");
    int painted = cp_painted(0x10, 0x30, 0x10 + 0x1c * 16, 0x30 + 0x17 * 16);
    fprintf(stderr, "[county] population painted=%d county=%d\n",
            painted, ctx.county);
    if (painted < 1000)
        T_FAIL("population panel painted only %d px", painted);
    gfx_paint_bind_canvas(NULL, 0, 0);
    return 0;
}

int test_county_panel_tax_renders(void)
{
    engine_county_panels_ctx_t ctx;
    localization_state_t loc;
    int rc = cp_render_setup(&ctx, &loc);
    if (rc == 2)
        T_SKIP("retail assets not available");
    engine_county_panel_tax(&ctx);
    cp_dump("tax");
    int painted = cp_painted(0x50, 0x90, 0x50 + 0x14 * 16, 0x90 + 9 * 16);
    fprintf(stderr, "[county] tax painted=%d\n", painted);
    if (painted < 1000)
        T_FAIL("tax panel painted only %d px", painted);
    gfx_paint_bind_canvas(NULL, 0, 0);
    return 0;
}

int test_county_panel_happiness_renders(void)
{
    engine_county_panels_ctx_t ctx;
    localization_state_t loc;
    int rc = cp_render_setup(&ctx, &loc);
    if (rc == 2)
        T_SKIP("retail assets not available");
    engine_county_panel_happiness(&ctx);
    cp_dump("happiness");
    int painted = cp_painted(0x10, 0x30, 0x10 + 0x1c * 16, 0x30 + 0x18 * 16);
    fprintf(stderr, "[county] happiness painted=%d\n", painted);
    if (painted < 1000)
        T_FAIL("happiness panel painted only %d px", painted);
    gfx_paint_bind_canvas(NULL, 0, 0);
    return 0;
}

int test_county_panel_rations_renders(void)
{
    engine_county_panels_ctx_t ctx;
    localization_state_t loc;
    int rc = cp_render_setup(&ctx, &loc);
    if (rc == 2)
        T_SKIP("retail assets not available");
    engine_county_panel_rations(&ctx);
    cp_dump("rations");
    int painted = cp_painted(0x80, 0x60, 0x80 + 0x12 * 16, 0x60 + 0x11 * 16);
    fprintf(stderr, "[county] rations painted=%d advanced=%d\n",
            painted, ctx.advanced_mode);
    if (painted < 1000)
        T_FAIL("rations panel painted only %d px", painted);
    gfx_paint_bind_canvas(NULL, 0, 0);
    return 0;
}

int test_county_panel_render_null_assets_safe(void)
{
    localization_state_t loc = {0};
    engine_county_panels_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.county_block = g_per_army_768;
    ctx.loc = &loc;
    gfx_paint_bind_canvas(NULL, 0, 0);
    engine_county_panel_population(&ctx);
    engine_county_panel_tax(&ctx);
    engine_county_panel_happiness(&ctx);
    engine_county_panel_rations(&ctx);
    engine_county_delta_label(&ctx, 3, 10, 10, NULL, 0x3f, 0xf9);
    return 0;
}
