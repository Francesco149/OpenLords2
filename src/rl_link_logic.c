/* rl_link_logic.c — the logic family of the link layer (docs/findings/rl-link.md): the runtime binds the old logic
 * ports need (moved from rhost's rh_port_bind_once and the combat / battle / economy seam binders,
 * tools/rhost/rh_hooks_ai.c / _combat.c / _battle.c / _economy.c) and a retail-layout entry rl_<va> for the old ports
 * retail-layout code reaches (rows: rh_hooks_ai / _combat / _battle / _events / _economy / _state.c).
 *
 * BIND.  rl_link_bind_once: the table / storage binds (the army records, the cell roster and descriptors, the
 * territory records, the AI score table inside the county records, the tile grid, the path field's storage), the
 * dirty rectangle (rl_link_dirty), and the callbacks.  The callbacks the old ports were handed are retail-layout ports
 * now (rhost handed retail VAs behind rh_port_retail_enter / _leave): rl_ code runs with the mirrors live, so only an
 * open event queue changes hands (rl_link_evq_callout_enter / _leave) — the callee may post into retail's ring.
 *
 * ENTRIES.  An entry runs between rl_link_port_enter / _leave with the queue loaded around the call when the old port
 * takes one (EVQ).  0x465d28 (engine_army_move_run) takes its four uninitialized locals (quirks #229 / #232) from the
 * caller's model: rl_465d28_res(res). */
#include "rl_link.h"
#include <stdint.h>
#include "rl.h"
#include "rl_io_plat.h"        /* rl_plat_timeGetTime */
#include "rl_link_globals.h"
#include "engine.h"            /* g_sim_mp_active (DAT_00553030), engine_set_demand_redraw */
#include "save.h"
#include "save_schema_globals.h"
#include "engine_event_queue.h"
#include "engine_army_ai.h"
#include "engine_army_economy.h"
#include "engine_army_move.h"
#include "engine_army_state.h"
#include "engine_combat.h"
#include "engine_map_query.h"
#include "engine_path_field.h"
#include "engine_ranking.h"
#include "engine_realm.h"
#include "engine_realm_events.h"
#include "engine_realm_upkeep.h"
#include "engine_sendsupplies.h"
#include "engine_territory.h"
#include "engine_trade.h"
#include "engine_armory.h"     /* g_arm_pct / g_arm_conscripts / g_arm_merc / g_arm_march_active */
#include "engine_diplomacy_action.h"
#include "text_input.h"
#include "gfx_globals.h"       /* g_gfx_first_pos_target / _y: DAT_005bb4a8 / DAT_0058fd54 (the editor's cursor / anchor) */
#include <string.h>

/* the partitions' globals (their headers conflict when included together: DUP symbols typed per partition) */
extern int32_t g_rl_55cea0[8385];     /* DAT_0055cea0: the 129 × 65 strategic tile grid */
extern int32_t g_rl_57cac4;           /* DAT_0057cac4: the cursor cache flag */
extern uint8_t g_rl_4f3ffe[0x72];     /* the strategic move-cost grid (read past the symbol: retail's layout) */
extern int16_t g_rl_500c30[0x1000];   /* the path field: the local player's cost field */
extern uint8_t g_rl_4f0380[9119], g_rl_4f9820[4096], g_rl_503020[3950], g_rl_53f070[12], g_rl_553550[0x708];
extern uint8_t g_rl_542cf8[4448];     /* the mercgrid payload (arm_grid.pl8) */
extern int32_t g_bf_58fd60, g_bf_58fd7c;   /* the season's seed; the mood input */
extern int32_t g_bf_4e65fc, g_bf_4e65f4;   /* the cursor x / y */
extern uint8_t g_bf_4eafb4;           /* the press */
int bf_426120(int a0);                /* engine_bf_strike.c: the footstep cue 0x426120 */

#define MP_ACTIVE (g_sim_mp_active)

/* ---------------------------------------------------------------- the callbacks: rl_ ports, the queue handed over */
#define CALLOUT(...) do { int q_ = rl_link_evq_callout_enter(); __VA_ARGS__; rl_link_evq_callout_leave(q_); } while (0)
/* engine_combat / engine_army_move */
static void seam_present(int mode) { CALLOUT(rl_426360(mode)); }                 /* 0x426360: stop buffer */
static void seam_footstep(int id)  { CALLOUT(bf_426120(id)); }                   /* 0x426120 */
static void seam_alert(int id)     { CALLOUT(rl_426216(id)); }                   /* 0x426216 */
static void seam_wav(const char *n, int loop, int z) { CALLOUT(rl_427990((int)(uintptr_t)n, loop, z)); }   /* 0x427990 */
static void seam_batl(void)        { seam_wav("ff_batl.wav", 0, 0); }
static void seam_click(void)       { CALLOUT(rl_499aca()); }                     /* 0x499aca */
static void seam_game_over(void)   { CALLOUT(rl_43eda0(0x24, 0)); }              /* 0x43eda0(0x24, 0) */
static uint32_t seam_clock(void)   { return (uint32_t)rl_plat_timeGetTime(); }
/* the battlefield's way out (engine_combat_bind_battlefield_fns) */
static void seam_siege_result(void)  { CALLOUT(rl_42c64f()); }
static void seam_return_to_map(void) { CALLOUT(rl_4788c6()); }
static void seam_writeback(void)     { CALLOUT(rl_47f474()); }
static void seam_result_panel(void)  { CALLOUT(rl_423241()); }
static void seam_stop_stream(int s)  { CALLOUT(rl_426d11(s)); }
static void seam_message(int id, int a) { CALLOUT(rl_43eda0(id, a)); }
static void seam_result_movie(int siege, int index, int x, int y, int z, int screen)   /* 0x42d91b: retail's own string */
{ CALLOUT(rl_42d91b((int)(uintptr_t)(g_rl_4d9278 + (siege ? 0x180 : 0) + 16 * index), x, y, z, screen)); }
/* the economy screens: trade, the county-action strip */
static void seam_trade_render(void) { CALLOUT(rl_416308()); }
static void seam_voice(int n)       { CALLOUT(rl_4b3714(n)); }
/* the strip's armory entry: the conscripts at the slider % (0x435ebc(county, DAT_0056d65c)), the muster pool
 * (0x4aa90a(county, DAT_00543fd8)), march / merc off (DAT_005679d0 / DAT_0055446c) */
static void seam_armory(int county)
{ CALLOUT(rl_435ebc(county, g_arm_pct); rl_4aa90a(county, g_arm_conscripts); g_arm_march_active = 0; g_arm_merc = 0); }

void rl_link_bind_once(void)
{
    static int done;
    if (done) return;
    done = 1;
    /* engine_game_init_bind_runtime_tables, minus what the port derives itself (strat cost) */
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_bind_cell_roster((const int32_t *)g_misc_1360);
    engine_army_economy_bind_cells(g_overworld_map);
    engine_army_economy_bind_resource_pool(g_player_strategic_352 + 0x120);
    engine_territory_bind_records(g_per_territory_420);
    engine_territory_bind_cells(g_overworld_map);
    engine_territory_bind_cell_roster((int32_t *)g_misc_1360);
    /* the AI score table DAT_0053fa40 lives INSIDE the county records (+0x90, 0x180 i16 a row) */
    engine_territory_bind_cell_score_table((int16_t *)(g_per_army_768 + 0x90));
    engine_territory_bind_terrain_tiles(g_rl_55cea0);
    engine_territory_bind_army_records(g_per_army_768);
    engine_map_query_bind_cell_descriptors(g_overworld_map);
    engine_army_state_bind_cells(g_overworld_map);
    engine_territory_bind_cursor_cache_flag((int *)&g_rl_57cac4);
    engine_territory_set_dirty_state(&rl_link_dirty);
    engine_path_field_bind_terrain((const int16_t *)(const void *)g_rl_4f3ffe);
    engine_path_field_bind_storage(g_rl_500c30, (int16_t *)(void *)g_rl_4f0380, (int32_t *)(void *)g_rl_4f9820,
                                   (int32_t *)(void *)g_rl_503020, g_rl_53f070, g_rl_553550);
    engine_army_move_bind_cells(g_overworld_map);
    /* combat / army movement */
    engine_army_state_bind_move_fn(engine_army_move_run);
    engine_army_move_bind_present_fn(seam_present);
    engine_army_move_bind_arrive_fn(engine_army_move_arrival);
    engine_army_move_bind_combat_fns(engine_army_move_combat_engage, engine_combat_contest_region);
    engine_army_move_bind_region_fns(engine_army_move_region_event_lo, engine_army_move_region_event_hi);
    engine_army_move_bind_special_fn(engine_army_move_special_tile);
    engine_army_move_bind_redraw_fn(engine_set_demand_redraw);
    engine_army_move_bind_sfx_fn(seam_footstep);
    engine_army_move_bind_alert_sfx_fn(seam_alert);
    engine_combat_bind_wav_fn(seam_wav);
    engine_combat_bind_battle_sfx(seam_batl);
    engine_combat_bind_click_fn(seam_click);
    engine_combat_bind_clock_fn(seam_clock);
    engine_ranking_bind_game_over_fn(seam_game_over);
    /* the battlefield's way out */
    engine_combat_bind_battlefield_fns(seam_siege_result, seam_return_to_map, seam_writeback, seam_result_panel,
                                       seam_stop_stream, seam_result_movie, seam_message);
    /* the economy screens */
    engine_trade_bind_wav_fn(seam_wav);
    engine_trade_bind_open_render_fn(seam_trade_render);
    engine_county_action_bind_wav_fn(seam_wav);
    engine_county_action_bind_voice_fn(seam_voice);
    engine_county_action_bind_armory_fn(seam_armory);
}

/* ---------------------------------------------------------------- entries.  Retail's EAX: nobody reads the void ones — 0. */
#define LG(...) do { int live_ = rl_link_port_enter(); rl_link_bind_once(); __VA_ARGS__; rl_link_port_leave(live_); } while (0)
#define EVQ(...) LG(engine_event_queue_state_t *q = rl_link_evq_load(); __VA_ARGS__; rl_link_evq_store())

/* engine_realm / _upkeep / _events: the season passes */
int rl_448819(void) { LG(engine_realm_roll_events(g_bf_58fd60)); return 0; }
int rl_449889(void) { EVQ(engine_realm_update_mood(g_bf_58fd60, g_bf_58fd7c, q)); return 0; }
int rl_44aa41(void) { EVQ(engine_realm_escalate_unrest(q, MP_ACTIVE)); return 0; }
int rl_44ae3c(void) { EVQ(engine_realm_coalition_pass(q, MP_ACTIVE)); return 0; }
int rl_4508de(void) { EVQ(engine_realm_complete_settlements(q)); return 0; }
int rl_4acbd4(void) { EVQ(engine_realm_pay_upkeep(q)); return 0; }
int rl_46eddf(int rec) { LG(engine_unit_map_link(rec)); return 0; }
/* engine_army_ai */
int rl_49a581(void) { EVQ(engine_army_ai_run_per_tick(q, MP_ACTIVE, g_bf_58fd60)); return 0; }
/* engine_combat */
int rl_4ac3c6(int realm) { EVQ(engine_combat_demolish_realm(realm, q, MP_ACTIVE)); return 0; }
int rl_437535(int occ, int region) { EVQ(engine_combat_transfer_army((uint32_t)occ, region, q, MP_ACTIVE)); return 0; }
int rl_49b2a3(void) { EVQ(engine_combat_mp_resign_sweep(q, MP_ACTIVE)); return 0; }
int rl_4abb36(int army, int region) { EVQ(engine_combat_army_enter_region(army, (uint32_t)region, q)); return 0; }
int rl_4a8aab(int rec) { EVQ(engine_combat_siege_assault(rec, q, MP_ACTIVE)); return 0; }
int rl_43b622(void) { EVQ(engine_combat_auto_resolve_button(q, MP_ACTIVE)); return 0; }
int rl_477dfc(void) { EVQ(engine_combat_battlefield_end_tick(q, MP_ACTIVE)); return 0; }
int rl_4ab383(int a0) { EVQ(engine_combat_aftermath(a0, q, MP_ACTIVE)); return 0; }
int rl_43be65(void) { EVQ(engine_combat_battlefield_end(q, MP_ACTIVE)); return 0; }
/* engine_army_move: the executor 0x465d28; res[] = its locals dir / outcome code / waypoint col / row ([ebp-4..-0x10],
 * never initialized: quirk #229) in and out — the caller's residue model (rl_46521f_res) */
void rl_465d28_res(uint32_t *res)
{
    EVQ(g_army_move_dir = res[0]; g_army_move_code = res[1]; g_army_move_wp_col = res[2]; g_army_move_wp_row = res[3];
        engine_army_move_run(q, MP_ACTIVE);
        res[0] = g_army_move_dir; res[1] = g_army_move_code; res[2] = g_army_move_wp_col; res[3] = g_army_move_wp_row);
}
/* engine_army_state: FUN_00428007 leaves the matched cell in the shared cursor DAT_00569570/78 (#226) */
int rl_428007(int realm) { int r, c, w; LG(r = engine_army_state_find_settle_spot(realm, &c, &w)); return r; }
/* the screens' hit tests over retail's input (cursor DAT_004e65fc / DAT_004e65f4, press DAT_004eafb4) */
int rl_43b412(int ox, int oy)
{ int r; LG(r = engine_sendsupplies_target_select(ox, oy, g_bf_4e65fc, g_bf_4e65f4, g_bf_4eafb4)); return r; }
int rl_4357a6(void)   /* FUN_004ba9c8 → the merchant scene's rug: the mercgrid payload DAT_00542cf8 */
{ int r; LG(r = engine_merchant_zone_click(g_rl_542cf8, g_bf_4e65fc, g_bf_4e65f4, g_bf_4eafb4)); return r; }

/* the hot-spot actions of the trade grid / gift screen / county-action strip / Send Supplies (router tables
 * DAT_004dd838 / 004dd9d0 / 004dda30 / 004dc688.. / 004dd538 / 004dc7e8: reached by retail VA, rl_va.h); the arrows /
 * the all-toggle / the seal read the router's param0 DAT_0059154c */
extern uint8_t g_bf_59154c[0xc];
extern uint8_t g_rl_5cd550[2000];     /* the text editor's buffer */
extern int32_t g_rl_58fe94, g_rl_57d3b0;   /* its length, pixel overflow */
#define LK_PARAM0 (*(const int32_t *)(const void *)g_bf_59154c)
int rl_435339(void) { LG(engine_trade_arrow_up()); return 0; }
int rl_43543d(void) { LG(engine_trade_arrow_down()); return 0; }
int rl_4355db(void) { LG(engine_trade_buy_all()); return 0; }
int rl_435541(void) { LG(engine_trade_sell_all()); return 0; }
int rl_435286(void) { LG(engine_trade_seal_confirm()); return 0; }
int rl_4352f2(void) { LG(engine_trade_seal_cancel()); return 0; }
int rl_436372(void) { LG(engine_diplomacy_action_arrow(LK_PARAM0)); return 0; }
/* the gift / message seal: the port's editor is one struct over retail's scattered globals (FUN_004020ae's set) */
int rl_436408(void)
{
    static text_input_state_t ed;
    memcpy(ed.buf, g_rl_5cd550, sizeof ed.buf);
    ed.cursor = g_gfx_first_pos_target; ed.length = g_rl_58fe94;
    ed.pixel_overflow = g_rl_57d3b0; ed.anchor_offset = g_gfx_first_pos_y;
    EVQ(engine_diplomacy_action_seal(LK_PARAM0, &ed, q));
    if (memcmp(g_rl_5cd550, ed.buf, sizeof ed.buf)) memcpy(g_rl_5cd550, ed.buf, sizeof ed.buf);
    g_gfx_first_pos_target = ed.cursor; g_rl_58fe94 = ed.length;
    g_rl_57d3b0 = ed.pixel_overflow; g_gfx_first_pos_y = ed.anchor_offset;
    return 0;
}
int rl_43ae30(void) { EVQ(engine_county_action_open(LK_PARAM0, q)); return 0; }
int rl_43b27a(void) { LG(engine_sendsupplies_arrow_up(LK_PARAM0)); return 0; }
int rl_43b1ca(void) { LG(engine_sendsupplies_arrow_down(LK_PARAM0)); return 0; }
int rl_43b32a(void) { LG(engine_sendsupplies_toggle_all(LK_PARAM0)); return 0; }
int rl_43530e(void) { LG(engine_trade_open()); return 0; }
