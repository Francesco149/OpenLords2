/* rl_link.c — see rl_link.h.  Moved from tools/rhost/rh_hooks_ai.c (rh_port_sync_in / _out, the event queue, the
 * live flag) so retail-layout code can call an old port without rhost's row machinery; rhost forwards to it. */
#include "rl_link.h"
#include <string.h>
#include "rl_link_globals.h"
/* the partitions' globals this file uses (their headers conflict when included together: DUP symbols typed per
 * partition) — the types as rl_ctl / rl_terr / rl_io / rl_turn / rl_glue declare them */
extern int32_t g_rl_50d09c, g_rl_50a4c4, g_rl_50a4c8, g_rl_50a4bc, g_rl_50a4c0;   /* rl_ctl: the dirty rectangle */
extern int32_t g_rl_504020, g_rl_504024, g_rl_4f4070, g_rl_500c20, g_rl_4f2768, g_rl_507230, g_rl_507234; /* rl_terr */
extern int32_t g_rl_5533e8, g_rl_52afe0, g_rl_56d6a0;
extern int32_t g_rl_553f60, g_rl_567594, g_rl_57cb30, g_rl_57c8e0, g_rl_552ffc, g_rl_57d34c, g_rl_5533bc, g_rl_553538,
               g_rl_553218;                                                        /* rl_io: the economy tuning */
extern uint8_t g_rl_553510[0x18];                                                  /* the event queue's staging */
extern uint8_t g_rl_568500[0x430];                                                 /* rl_terr: the ring past g_misc_128 */
extern int32_t g_rl_543fd0, g_rl_553ec4, g_rl_565484, g_rl_57c954;                  /* the event queue's indices */
#include "engine_bf_globals.h"
#include "save.h"   /* g_misc_128: the event ring's first 128 B (its rest: rl_terr's g_rl_568500) */
#include "engine_army_economy.h"
#include "engine_army_ai.h"
#include "engine_army_state.h"
#include "engine_territory.h"
#include "engine_map_query.h"
#include "engine_path.h"
#include "engine_path_field.h"
#include "engine_hud_panel.h"
#include "engine_workforce_hud.h"
#include "rl.h"   /* rl_4120e0 */

engine_dirty_rect_state_t rl_link_dirty;

/* the mirrored globals, and their values at sync_in: sync_out stores a mirror only when the PORT changed it */
#define U32(sym) (*(uint32_t *)(void *)&(sym))
enum { M_COST, M_REV, M_POOLA, M_POOLB, M_DX, M_DY, M_CCOL, M_CROW, M_SLOT, M_RANK,
       M_DMARK, M_DXMIN, M_DXMAX, M_DYMIN, M_DYMAX, M_BELOW,
       M_PFSC, M_PFSR, M_PFSCELL, M_PFHEAD, M_PFTAIL, M_PFCC, M_PFCR, M_ASIDX, M_ASABORT, M_ASLIVE, M_RAIDER, M_N };
static uint32_t s_in[M_N];
static uint32_t *mirror_global(int k)
{
    switch (k) {
    case M_COST: return &U32(g_rl_55cd6c);   case M_REV: return &U32(g_rl_5656f0);
    case M_POOLA: return &U32(g_rl_57c904);  case M_POOLB: return &U32(g_rl_56d628);
    case M_DX: return &U32(g_bf_591560);     case M_DY: return &U32(g_bf_58fea0);
    case M_CCOL: return &U32(g_bf_569570);   case M_CROW: return &U32(g_bf_569578);
    case M_SLOT: return &U32(g_rl_52afe0);   case M_RANK: return &U32(g_rl_56d6a0);
    case M_DMARK: return &U32(g_rl_50d09c);  case M_DXMIN: return &U32(g_rl_50a4c4);
    case M_DXMAX: return &U32(g_rl_50a4c8);  case M_DYMIN: return &U32(g_rl_50a4bc);
    case M_DYMAX: return &U32(g_rl_50a4c0);  case M_BELOW: return &U32(g_bf_53e9ac);
    case M_PFSC: return &U32(g_rl_504020);   case M_PFSR: return &U32(g_rl_504024);
    case M_PFSCELL: return &U32(g_rl_4f4070); case M_PFHEAD: return &U32(g_rl_500c20);
    case M_PFTAIL: return &U32(g_rl_4f2768); case M_PFCC: return &U32(g_rl_507230);
    case M_PFCR: return &U32(g_rl_507234);   case M_ASIDX: return &U32(g_rl_5533e8);
    case M_ASABORT: return (uint32_t *)(void *)&g_rl_50a49e;   /* a byte: read / stored through PUT8 below */
    case M_ASLIVE: return &U32(g_rl_553f08); default: return &U32(g_rl_4e59dc);
    }
}
static uint32_t gv(int k) { return k == M_ASABORT ? (uint32_t)g_rl_50a49e : *mirror_global(k); }
static void put(int k, uint32_t v)   /* store-if-changed, and only a mirror the port changed */
{
    if (k == M_ASABORT) {
        if ((v & 0xffu) != (s_in[k] & 0xffu) && g_rl_50a49e != (uint8_t)v) g_rl_50a49e = (uint8_t)v;
        return;
    }
    uint32_t *g = mirror_global(k);
    if (v != s_in[k] && *g != v) *g = v;
}

void rl_link_sync_in(void)
{
    engine_army_economy_set_prices((int)g_rl_553f60, (int)g_rl_567594, (int)g_rl_57cb30);
    engine_army_economy_set_production_tuning((int)g_rl_57c8e0, (int)g_rl_552ffc, (int)g_rl_57d34c, (int)g_rl_5533bc);
    engine_army_economy_set_yield_divisors((int)g_rl_553538, (int)g_rl_553218);
    for (int k = 0; k < M_N; k++) s_in[k] = gv(k);
    engine_army_economy_set_eval_scratch((int)s_in[M_COST], (int)s_in[M_REV]);
    engine_army_economy_set_supply_pools((int)s_in[M_POOLA], (int)s_in[M_POOLB]);
    g_path_dist_dx = (int32_t)s_in[M_DX]; g_path_dist_dy = (int32_t)s_in[M_DY];
    g_cell_cursor_col = (int32_t)s_in[M_CCOL]; g_cell_cursor_row = (int32_t)s_in[M_CROW];
    engine_territory_set_current_slot((int)s_in[M_SLOT]);
    engine_army_ai_set_ranking_dirty((int)s_in[M_RANK]);
    rl_link_dirty.dirty_mark = (int)s_in[M_DMARK]; rl_link_dirty.dirty_xmin = (int)s_in[M_DXMIN];
    rl_link_dirty.dirty_xmax = (int)s_in[M_DXMAX]; rl_link_dirty.dirty_ymin = (int)s_in[M_DYMIN];
    rl_link_dirty.dirty_ymax = (int)s_in[M_DYMAX]; rl_link_dirty.below_status_redraw = (int)s_in[M_BELOW];
    g_pf_src_col = (int32_t)s_in[M_PFSC]; g_pf_src_row = (int32_t)s_in[M_PFSR]; g_pf_src_cell = (int32_t)s_in[M_PFSCELL];
    g_pf_head = (int32_t)s_in[M_PFHEAD]; g_pf_tail = (int32_t)s_in[M_PFTAIL];
    g_pf_cur_col = (int32_t)s_in[M_PFCC]; g_pf_cur_row = (int32_t)s_in[M_PFCR];
    g_army_state_index = (int)s_in[M_ASIDX]; g_army_state_abort = (uint8_t)s_in[M_ASABORT];
    g_army_state_live_count = (int)s_in[M_ASLIVE]; g_raider_target_cursor = (int)s_in[M_RAIDER];
}

void rl_link_sync_out(void)
{
    put(M_COST, (uint32_t)engine_army_economy_eval_cost());
    put(M_REV, (uint32_t)engine_army_economy_eval_revenue());
    put(M_POOLA, (uint32_t)engine_army_economy_supply_pool_a());
    put(M_POOLB, (uint32_t)engine_army_economy_supply_pool_b());
    put(M_DX, (uint32_t)g_path_dist_dx); put(M_DY, (uint32_t)g_path_dist_dy);
    put(M_CCOL, (uint32_t)g_cell_cursor_col); put(M_CROW, (uint32_t)g_cell_cursor_row);
    put(M_SLOT, (uint32_t)engine_territory_current_slot());
    put(M_RANK, (uint32_t)engine_army_ai_ranking_dirty());
    put(M_DMARK, (uint32_t)rl_link_dirty.dirty_mark); put(M_DXMIN, (uint32_t)rl_link_dirty.dirty_xmin);
    put(M_DXMAX, (uint32_t)rl_link_dirty.dirty_xmax); put(M_DYMIN, (uint32_t)rl_link_dirty.dirty_ymin);
    put(M_DYMAX, (uint32_t)rl_link_dirty.dirty_ymax); put(M_BELOW, (uint32_t)rl_link_dirty.below_status_redraw);
    put(M_PFSC, (uint32_t)g_pf_src_col); put(M_PFSR, (uint32_t)g_pf_src_row); put(M_PFSCELL, (uint32_t)g_pf_src_cell);
    put(M_PFHEAD, (uint32_t)g_pf_head); put(M_PFTAIL, (uint32_t)g_pf_tail);
    put(M_PFCC, (uint32_t)g_pf_cur_col); put(M_PFCR, (uint32_t)g_pf_cur_row);
    put(M_ASIDX, (uint32_t)g_army_state_index); put(M_ASABORT, g_army_state_abort);
    put(M_ASLIVE, (uint32_t)g_army_state_live_count); put(M_RAIDER, (uint32_t)g_raider_target_cursor);
}

/* ---------------------------------------------------------------- the event queue (FUN_00472bc5 / FUN_00472ae0) */
_Static_assert(sizeof ((engine_event_queue_state_t *)0)->ring == sizeof g_misc_128 + sizeof g_rl_568500, "the event ring");
static engine_event_queue_state_t s_evq;
static int s_evq_open;   /* an EVQ caller is running: a call back into retail stores / reloads the queue too */
engine_event_queue_state_t *rl_link_evq_load(void)
{
    s_evq_open = 1;
    memcpy(&s_evq.scratch, g_rl_553510, sizeof s_evq.scratch);   /* DAT_00553510: 24 B staging */
    memcpy(s_evq.ring, g_misc_128, sizeof g_misc_128);           /* DAT_00568480: 50 × 24 B, two port symbols */
    memcpy((uint8_t *)s_evq.ring + sizeof g_misc_128, g_rl_568500, sizeof s_evq.ring - sizeof g_misc_128);
    s_evq.write_idx = (int)g_rl_543fd0; s_evq.read_idx = (int)g_rl_553ec4;
    s_evq.notify_flag = (int)g_rl_565484; s_evq.consumer_cooldown = (int)g_rl_57c954;
    return &s_evq;
}
void rl_link_evq_store(void)
{
    s_evq_open = 0;
    if (memcmp(g_rl_553510, &s_evq.scratch, sizeof s_evq.scratch)) memcpy(g_rl_553510, &s_evq.scratch, sizeof s_evq.scratch);
    if (memcmp(g_misc_128, s_evq.ring, sizeof g_misc_128)) memcpy(g_misc_128, s_evq.ring, sizeof g_misc_128);
    const uint8_t *rest = (const uint8_t *)s_evq.ring + sizeof g_misc_128;
    if (memcmp(g_rl_568500, rest, sizeof g_rl_568500)) memcpy(g_rl_568500, rest, sizeof g_rl_568500);
    if (g_rl_543fd0 != s_evq.write_idx) g_rl_543fd0 = s_evq.write_idx;
    if (g_rl_553ec4 != s_evq.read_idx) g_rl_553ec4 = s_evq.read_idx;
    if (g_rl_565484 != s_evq.notify_flag) g_rl_565484 = s_evq.notify_flag;
    if (g_rl_57c954 != s_evq.consumer_cooldown) g_rl_57c954 = s_evq.consumer_cooldown;
}

/* ---------------------------------------------------------------- liveness */
/* 1 while old-port code runs with the mirrors authoritative (no retail-layout / retail code since the last sync_in) */
static int s_live;
int rl_link_live(void) { return s_live; }
int rl_link_port_enter(void)
{
    int live = s_live;
    /* entered from port code with no retail in between: the caller's mirrors are live — store them first, or the
     * reload drops what the caller computed (mp-leave-p1 f3352: resign 0x49cf20 → plan_realm's eval scratch lost) */
    if (live) rl_link_sync_out();
    rl_link_sync_in();
    s_live = 1;
    return live;
}
void rl_link_port_leave(int saved)
{
    rl_link_sync_out();
    s_live = saved;
}
void rl_link_retail_enter(void)
{
    rl_link_sync_out();
    if (s_evq_open) { rl_link_evq_store(); s_evq_open = 1; }
    s_live = 0;
}
void rl_link_retail_leave(void)
{
    if (s_evq_open) rl_link_evq_load();
    rl_link_sync_in();
    s_live = 1;
}

/* ---------------------------------------------------------------- the old ports' environments */
extern uint8_t g_rl_591580[120000];   /* rl_render: the localization blob (l2_eng.dat) */
extern uint8_t *g_rl_5c9270;          /* rl_render: the walker (a pointer into the blob) */
extern int32_t g_bf_5530c8;           /* the mode's misc sheet pointer DAT_005530c8 */
extern uint8_t g_rl_5c9a90[9504];     /* fntl2_9 */
extern uint8_t g_rl_5aeba0[3400];     /* font_10 */
extern uint8_t g_bf_5af8f0[14000];    /* fntl2_14 */
extern uint8_t g_rl_4d71d0[0x140];    /* the measure char map; the render map DAT_004d71f0 = +0x20 */

static localization_state_t s_loc;
localization_state_t *rl_link_loc_load(void)
{
    s_loc.blob = g_rl_591580;
    s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
    return &s_loc;
}
void rl_link_loc_store(void)
{
    uint8_t *w = g_rl_591580 + s_loc.walker_offset;
    if (g_rl_5c9270 != w) g_rl_5c9270 = w;
}

static void panel_wf_rows(int county) { engine_workforce_compute_rows(county); }   /* FUN_0045161e (an old port) */
static void panel_wf_redraw(void)   /* 0x4120e0: the workforce backdrop + rows + seal (retail layout) */
{
    rl_link_loc_store();
    rl_link_retail_enter();
    rl_4120e0();
    rl_link_retail_leave();
    rl_link_loc_load();
}
void rl_link_panel_env(void)
{
    rl_link_loc_load();
    engine_hud_panel_env_t env = {
        .chrome = (const uint8_t *)(uintptr_t)(uint32_t)g_bf_5530c8,   /* DAT_005530c8 misc sheet */
        .font_name = g_rl_5c9a90, .font_res = g_rl_5aeba0, .font_menu = g_bf_5af8f0,
        .glyph = g_rl_4d71d0 + 0x20, .metrics = g_rl_4d71d0,
        .loc = &s_loc, .dirty = engine_territory_dirty_state(),
        .workforce_rows = panel_wf_rows, .workforce_redraw = panel_wf_redraw,
    };
    engine_hud_panel_bind_env(&env);
}

/* ---------------------------------------------------------------- retail-layout entries of old ports */
/* an old port entered from retail-layout code: the mirrors loaded (a live caller's stored first), the environment
 * bound, the walker stored back.  Retail's EAX: nobody reads it (HOOK_V rows) — 0. */
#define OLD_PORT(env, call) do { int live_ = rl_link_port_enter(); env; call; rl_link_loc_store(); rl_link_port_leave(live_); } while (0)
int rl_40f7d3(int a1) { OLD_PORT(rl_link_panel_env(), engine_hud_panel_draw(a1)); return 0; }
int rl_41a734(int a1) { OLD_PORT(rl_link_panel_env(), engine_hud_panel_endturn(a1)); return 0; }
