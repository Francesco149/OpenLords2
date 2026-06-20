/*
 * test_engine_ingame_input.c — coverage for the in-game input dispatcher
 * (engine_ingame_input_dispatch, port of FUN_0042ff10) and the cursor
 * region classifier (engine_ingame_input_classify_region, FUN_00432640).
 *
 * The dispatcher's sub-handlers are bound to recording hooks: each logs an
 * id when called and returns a per-id programmed value, so the tests pin
 * retail's short-circuit CALL ORDER and the screen/redraw/latch state
 * transitions.  Action hooks capture the screen at call time (to prove the
 * dispatcher sets screen=4 before opening terrain-info, etc.).
 */

#include <string.h>

#include "t.h"
#include "engine_ingame_input.h"

/* ── recording hook harness ──────────────────────────────────────────── */

enum {
    H_EDGE = 1, H_DRAG, H_HUDA, H_KBD, H_HUDB, H_GRID, H_SCROLL, H_PANEL,
    H_TAB, H_GENERIC, H_TISUB, H_DISMISSCHIP, H_UIROUTER,
    A_DISMISSMAP, A_MODE2, A_ARMY, A_TIOPEN, A_TICLOSE, A_0FCOMMIT,
    H_MAX
};

static int g_log[64];
static int g_logn;
static int g_prog[H_MAX];          /* programmed consume-hook returns */
static int g_screen_at[H_MAX];     /* st->screen captured when a hook ran */

static void rec(int id, engine_ingame_state_t *st)
{
    if (g_logn < (int)(sizeof g_log / sizeof g_log[0])) {
        g_log[g_logn++] = id;
    }
    g_screen_at[id] = st->screen;
}

#define DEF_CONSUME(name, id) \
    static int name(const engine_ingame_input_in_t *in, engine_ingame_state_t *st) \
    { (void)in; rec(id, st); return g_prog[id]; }
#define DEF_ACTION(name, id) \
    static void name(const engine_ingame_input_in_t *in, engine_ingame_state_t *st) \
    { (void)in; rec(id, st); }

DEF_CONSUME(hk_edge,    H_EDGE)
DEF_CONSUME(hk_drag,    H_DRAG)
DEF_CONSUME(hk_huda,    H_HUDA)
DEF_CONSUME(hk_kbd,     H_KBD)
DEF_CONSUME(hk_hudb,    H_HUDB)
DEF_CONSUME(hk_grid,    H_GRID)
DEF_CONSUME(hk_scroll,  H_SCROLL)
DEF_CONSUME(hk_panel,   H_PANEL)
DEF_CONSUME(hk_tab,     H_TAB)
DEF_CONSUME(hk_generic, H_GENERIC)
DEF_CONSUME(hk_tisub,   H_TISUB)
DEF_CONSUME(hk_dismissc,H_DISMISSCHIP)
DEF_CONSUME(hk_uirouter,H_UIROUTER)
DEF_ACTION(hk_dismissmap, A_DISMISSMAP)
DEF_ACTION(hk_mode2,      A_MODE2)
DEF_ACTION(hk_army,       A_ARMY)
DEF_ACTION(hk_tiopen,     A_TIOPEN)
DEF_ACTION(hk_ticlose,    A_TICLOSE)
DEF_ACTION(hk_0fcommit,   A_0FCOMMIT)

static const engine_ingame_hooks_t REC_HOOKS = {
    .dismiss_chip       = hk_dismissc,
    .ui_router          = hk_uirouter,
    .edge_scroll        = hk_edge,
    .minimap_drag       = hk_drag,
    .hud_btn_a          = hk_huda,
    .kbd_table          = hk_kbd,
    .hud_btn_b          = hk_hudb,
    .action_grid        = hk_grid,
    .scrollbar          = hk_scroll,
    .panel_toggles      = hk_panel,
    .tab_strip          = hk_tab,
    .generic_click      = hk_generic,
    .ti_subhandlers     = hk_tisub,
    .dismiss_on_map     = hk_dismissmap,
    .special_mode2_jump = hk_mode2,
    .army_select        = hk_army,
    .terrain_info_open  = hk_tiopen,
    .terrain_info_close = hk_ticlose,
    .screen0f_commit    = hk_0fcommit,
};

static void rec_reset(void)
{
    g_logn = 0;
    memset(g_log, 0, sizeof g_log);
    memset(g_prog, 0, sizeof g_prog);
    memset(g_screen_at, -1, sizeof g_screen_at);
}

static int log_has(int id)
{
    for (int i = 0; i < g_logn; i++) {
        if (g_log[i] == id) return 1;
    }
    return 0;
}

static engine_ingame_input_in_t base_in(void)
{
    engine_ingame_input_in_t in;
    memset(&in, 0, sizeof in);  /* idle turn, no clicks, not map-ready */
    return in;
}

static engine_ingame_state_t base_st(int screen)
{
    engine_ingame_state_t st;
    memset(&st, 0, sizeof st);
    st.screen = screen;
    return st;
}

/* ── region classifier (FUN_00432640) ────────────────────────────────── */

static engine_ingame_region_in_t region_base(int screen, int cx, int cy, int gm)
{
    engine_ingame_region_in_t r;
    r.screen = screen; r.cursor_x = cx; r.cursor_y = cy; r.game_mode = gm;
    r.panel_x = 0x1de; r.panel_w = 0x40;     /* panel rect [0x1de, 0x21e) */
    r.panel_y = 0x40;  r.panel_h = 0x100;    /* [0x40, 0x140) (mode!=0) */
    return r;
}

int test_ingame_region_map_area(void)
{
    /* cursor over the map (below the top bar, left of the panel) → 0 */
    engine_ingame_region_in_t r = region_base(0, 0x100, 0x80, 0);
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 0);
    return 0;
}

int test_ingame_region_top_bar(void)
{
    engine_ingame_region_in_t r = region_base(0, 0x100, 0x10, 0);
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 1);
    return 0;
}

int test_ingame_region_right_panel_mode_nonzero(void)
{
    /* mode != 0: panel band is [panel_y, panel_y+panel_h). */
    engine_ingame_region_in_t r = region_base(0, 0x200, 0x50, 1);
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 2);
    /* one px above panel_y → not in band (mode != 0) */
    r.cursor_y = 0x3f;
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 0);
    return 0;
}

int test_ingame_region_right_panel_mode0_extends_up(void)
{
    /* mode 0: the band starts 0x18 px higher (panel_y - 0x18 = 0x28). */
    engine_ingame_region_in_t r = region_base(0, 0x200, 0x30, 0);  /* 0x28<=0x30<0x140 */
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 2);
    r.cursor_y = 0x27;   /* just above the extended top */
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 0);
    return 0;
}

int test_ingame_region_high_screen_returns_zero(void)
{
    engine_ingame_region_in_t r = region_base(8, 0x100, 0x10, 0);
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 0);
    return 0;
}

int test_ingame_region_panel_x_excludes_left(void)
{
    /* cursor left of the panel x-range → map area even at panel-Y */
    engine_ingame_region_in_t r = region_base(0, 0x1dd, 0x80, 1);
    T_ASSERT_EQ_I(engine_ingame_input_classify_region(&r), 0);
    return 0;
}

/* ── prologue / tail latches ─────────────────────────────────────────── */

int test_ingame_prologue_latches(void)
{
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(0);
    st.prev_cac4 = 7;
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.latch_afa0, 7);     /* latched prev_cac4 */
    T_ASSERT_EQ_I(st.prev_cac4, 0);      /* reset */
    T_ASSERT_EQ_I(st.latch_565488, 0);   /* 7 != 1 → no falling-edge set */
    T_ASSERT_EQ_I(st.latch_5533a4, 8);
    T_ASSERT_EQ_I(st.edge_4eabb4, 0);    /* tail clear */
    return 0;
}

int test_ingame_tail_falling_edge_latch(void)
{
    /* prev_cac4 == 1 at entry, reset to 0 in prologue, no producer → the
     * tail falling-edge detector sets latch_565488. */
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(0);
    st.prev_cac4 = 1;
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.latch_afa0, 1);
    T_ASSERT_EQ_I(st.latch_565488, 1);
    return 0;
}

/* ── dismiss-chip / ui-router subsumption ────────────────────────────── */

int test_ingame_dismiss_chip_consumes_skips_body(void)
{
    rec_reset();
    g_prog[H_DISMISSCHIP] = 1;            /* dismiss chip consumes */
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(H_DISMISSCHIP), 1);
    T_ASSERT_EQ_I(log_has(H_UIROUTER), 0);   /* short-circuit: router not called */
    T_ASSERT_EQ_I(log_has(H_EDGE), 0);       /* body skipped */
    T_ASSERT_EQ_I(st.edge_4eabb4, 0);        /* tail latch still ran */
    return 0;
}

int test_ingame_ui_router_consumes_skips_body(void)
{
    rec_reset();
    g_prog[H_UIROUTER] = 1;
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(H_DISMISSCHIP), 1);
    T_ASSERT_EQ_I(log_has(H_UIROUTER), 1);
    T_ASSERT_EQ_I(log_has(H_EDGE), 0);       /* body skipped */
    return 0;
}

/* ── afa8 latch path ─────────────────────────────────────────────────── */

int test_ingame_afa8_rmb_swallow(void)
{
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.rmb_released = 1;
    engine_ingame_state_t st = base_st(0);
    st.afa8 = 1;
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.afa8, 0);            /* cleared */
    T_ASSERT_EQ_I(st.demand_redraw, 2);
    T_ASSERT_EQ_I(log_has(H_EDGE), 0);   /* strategic body skipped */
    return 0;
}

/* ── strategic call order + short-circuit ────────────────────────────── */

int test_ingame_strategic_call_order(void)
{
    /* All consume hooks return 0, no clicks, not map-ready → the full HUD
     * chain runs in retail order; tab_strip not called (no press edge). */
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(0);
    st.hold_56d64c = 5;
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    int expect[] = { H_DISMISSCHIP, H_UIROUTER, H_EDGE, H_DRAG, H_HUDA,
                     H_KBD, H_HUDB, H_GRID, H_SCROLL, H_PANEL };
    T_ASSERT_EQ_I(g_logn, (int)(sizeof expect / sizeof expect[0]));
    for (int i = 0; i < g_logn; i++) {
        T_ASSERT_EQ_I(g_log[i], expect[i]);
    }
    T_ASSERT_EQ_I(log_has(H_TAB), 0);
    T_ASSERT_EQ_I(st.hold_56d64c, 0);    /* !map_ready → hold cleared */
    return 0;
}

int test_ingame_strategic_edge_scroll_short_circuits(void)
{
    rec_reset();
    g_prog[H_EDGE] = 1;                   /* edge-scroll consumes */
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(H_EDGE), 1);
    T_ASSERT_EQ_I(log_has(H_DRAG), 0);   /* short-circuit */
    T_ASSERT_EQ_I(log_has(H_KBD), 0);
    return 0;
}

int test_ingame_strategic_turn_busy_gates_inner(void)
{
    /* turn not idle → the 3 outer handlers run, but the inner chain
     * (kbd/hud_b/grid/scroll/panel) + map fall-through do NOT. */
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.turn_progress = 1;                 /* busy */
    in.map_ready = 1;
    in.rmb_released = 1;                   /* would open terrain-info if idle */
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(H_HUDA), 1);
    T_ASSERT_EQ_I(log_has(H_KBD), 0);
    T_ASSERT_EQ_I(log_has(A_TIOPEN), 0);
    T_ASSERT_EQ_I(st.screen, 0);
    return 0;
}

/* ── strategic map fall-through ──────────────────────────────────────── */

int test_ingame_map_rmb_opens_terrain_info(void)
{
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.map_ready = 1;
    in.rmb_released = 1;
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.screen, 4);              /* switched to terrain-info */
    T_ASSERT_EQ_I(log_has(A_TIOPEN), 1);
    T_ASSERT_EQ_I(g_screen_at[A_TIOPEN], 4);  /* screen set BEFORE the opener */
    T_ASSERT_EQ_I(log_has(H_TAB), 0);         /* consumed → tail skipped */
    return 0;
}

int test_ingame_map_lmb_selects_army(void)
{
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.map_ready = 1;
    in.lmb_released = 1;
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(A_ARMY), 1);
    T_ASSERT_EQ_I(st.screen, 0);             /* army-select doesn't switch here */
    T_ASSERT_EQ_I(log_has(A_TIOPEN), 0);
    return 0;
}

int test_ingame_map_lmb_precedence_over_rmb(void)
{
    /* both edges set: LMB (army) is tested before RMB (terrain-info). */
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.map_ready = 1;
    in.lmb_released = 1;
    in.rmb_released = 1;
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(A_ARMY), 1);
    T_ASSERT_EQ_I(log_has(A_TIOPEN), 0);
    return 0;
}

int test_ingame_map_dismiss_precedes_clicks(void)
{
    /* open scroll + RMB → dismiss, NOT terrain-info (dismiss is first). */
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.map_ready = 1;
    in.rmb_released = 1;
    in.extended_pause = 1;
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(A_DISMISSMAP), 1);
    T_ASSERT_EQ_I(log_has(A_TIOPEN), 0);
    T_ASSERT_EQ_I(st.screen, 0);
    return 0;
}

int test_ingame_map_viewmode2_jump_bare_returns(void)
{
    /* view-mode-2 LMB-press over map → screen 0 + jump, and the BARE
     * return skips the tail latch (edge_4eabb4 stays at its entry value). */
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.map_ready = 1;
    in.lmb_pressed = 1;
    in.view_mode = 2;
    engine_ingame_state_t st = base_st(0);
    st.edge_4eabb4 = 9;                   /* sentinel: tail would clear to 0 */
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(A_MODE2), 1);
    T_ASSERT_EQ_I(st.returned_early, 1);
    T_ASSERT_EQ_I(st.edge_4eabb4, 9);     /* tail latch NOT reached */
    return 0;
}

/* ── terrain-info (screen 4) close ───────────────────────────────────── */

int test_ingame_screen4_rmb_closes(void)
{
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.rmb_released = 1;
    engine_ingame_state_t st = base_st(4);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT_EQ_I(log_has(A_TICLOSE), 1);
    T_ASSERT_EQ_I(log_has(H_EDGE), 1);    /* screen-4 path tries edge-scroll first */
    return 0;
}

int test_ingame_screen4_turn_busy_closes(void)
{
    rec_reset();
    engine_ingame_input_in_t in = base_in();
    in.turn_progress = 1;                 /* busy */
    engine_ingame_state_t st = base_st(4);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT_EQ_I(log_has(A_TICLOSE), 1);
    T_ASSERT_EQ_I(log_has(H_EDGE), 0);    /* busy closes before edge-scroll */
    return 0;
}

int test_ingame_screen4_generic_click_closes(void)
{
    rec_reset();
    g_prog[H_GENERIC] = 1;
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(4);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT_EQ_I(log_has(A_TICLOSE), 1);
    return 0;
}

int test_ingame_screen4_subhandler_consumes_no_close(void)
{
    /* a sub-interaction consumes without closing the popup. */
    rec_reset();
    g_prog[H_TISUB] = 1;
    engine_ingame_input_in_t in = base_in();
    engine_ingame_state_t st = base_st(4);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.screen, 4);          /* stays open */
    T_ASSERT_EQ_I(log_has(A_TICLOSE), 0);
    return 0;
}

int test_ingame_screen4_idle_no_click_stays_open(void)
{
    rec_reset();
    engine_ingame_input_in_t in = base_in();  /* idle, no click */
    engine_ingame_state_t st = base_st(4);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(st.screen, 4);
    T_ASSERT_EQ_I(log_has(A_TICLOSE), 0);
    return 0;
}

/* ── shared tail (tab strip) ─────────────────────────────────────────── */

int test_ingame_tail_tab_strip_resets_screen(void)
{
    /* press edge, nothing else consumes, tab strip consumes → game_mode 0
     * resets screen to 0 and arms the empty-redraw gate. */
    rec_reset();
    g_prog[H_TAB] = 1;
    engine_ingame_input_in_t in = base_in();
    in.lmb_pressed = 1;                   /* press edge; not map-ready */
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(H_TAB), 1);
    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT_EQ_I(st.empty_redraw_gate, 2);
    return 0;
}

int test_ingame_tail_tab_strip_not_run_without_press(void)
{
    rec_reset();
    g_prog[H_TAB] = 1;
    engine_ingame_input_in_t in = base_in();  /* no press edge */
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(log_has(H_TAB), 0);
    T_ASSERT_EQ_I(st.empty_redraw_gate, 0);
    return 0;
}

int test_ingame_null_hooks_safe(void)
{
    /* NULL hooks → pure skeleton, every handler inert, no crash. */
    engine_ingame_input_in_t in = base_in();
    in.map_ready = 1;
    in.rmb_released = 1;
    engine_ingame_state_t st = base_st(0);
    engine_ingame_input_dispatch(&in, &st, NULL);
    T_ASSERT_EQ_I(st.screen, 4);          /* RMB-open still flips the screen */
    return 0;
}
