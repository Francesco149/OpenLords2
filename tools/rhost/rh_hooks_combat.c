/* rh_hooks_combat.c — the battle resolver and army movement (src/engine_combat.c, src/engine_army_move.c and
 * their unit / elimination / diplomacy callees) vs retail, on identical state (rh_hooks.h contract; shared
 * plumbing — binding, sync, EVQ, the generic wrapper body — in rh_port.h / rh_hooks_ai.c).
 *
 * BINDING.  The save block is aliased in rh_hooks_ai.c. The battle / movement RUNTIME globals the port keeps in
 * src/engine_combat_globals.c (not linked here) are aliased below at their retail VAs the same way, plus
 * engine_turn.c's g_sim_turn_progress (DAT_00553fc8). engine.c is cut from the closure: its screen byte
 * (DAT_004eac50) and demand-redraw mode (DAT_0057d340) are bridged as accessors on retail's memory.
 *
 * SEAMS.  The port's bind-able seams get their port implementation where one exists (the executor's arrive /
 * combat / region / special handlers, the pathfinder clock) and RETAIL's function otherwise (audio cues, the
 * footstep / alert / present DirectSound calls, the territory-captured click, the game-over screen): the
 * hybrid falls back to retail for what is not ported, bracketed by rh_port_retail_enter/leave. */
#include "rh_port.h"
#include "save_schema_globals.h"
#include "engine.h"
#include "engine_turn.h"
#include "engine_combat.h"
#include "engine_army_move.h"
#include "engine_army_state.h"
#include "engine_realm_upkeep.h"
#include "engine_ranking.h"
#include "engine_diplomacy.h"
#include "engine_path.h"
#include "engine_territory.h"
#include "engine_army_ai.h"

/* ---------------------------------------------------------------- binding: runtime globals = retail memory */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(
/* engine_combat_globals.c */
A(g_combat_both_garrison,0x4ee89c) A(g_combat_ctrl_408,0x554408) A(g_combat_ctrl_5b0,0x56d5b0)
A(g_combat_render_active,0x56d5c8) A(g_combat_active_terr,0x553f8c) A(g_combat_winner_owner,0x5440cc)
A(g_combat_audio_latch,0x5533f0) A(g_combat_flag_530d0,0x5530d0) A(g_combat_flag_553210,0x553210)
A(g_combat_flag_567d0c,0x567d0c) A(g_combat_flag_53f284,0x53f284) A(g_combat_render_dirty,0x52afcc)
A(g_combat_anim_clock,0x5689a0) A(g_combat_battle_clock,0x57a0d0) A(g_combat_anim_t0,0x53e994)
A(g_combat_anim_deadline,0x568d9c) A(g_combat_anim_scratch,0x515d60)
A(g_siege_sel_attacker,0x55cd58) A(g_siege_sel_region,0x52f0a8) A(g_siege_besieger,0x568ed0) A(g_siege_region,0x554000)
A(g_army_move_cell_flags,0x50a4a4) A(g_army_move_cell_region,0x50a49c) A(g_army_move_cell_terrain,0x50a49d)
A(g_army_move_cell_off,0x50a4a0)
/* engine_turn.c */
A(g_sim_turn_progress,0x553fc8)
);
#undef A

/* not retail globals: FUN_00465d28's uninitialized waypoint locals, moved through the stack residue (rh_port.h) */
uint32_t g_army_move_wp_col, g_army_move_wp_row;

/* ---------------------------------------------------------------- bridges across the link cut (engine.c) */
int  engine_current_screen(void)          { return G8(L2_SCREEN); }               /* DAT_004eac50 (byte) */
void engine_set_current_screen(int s)     { G8(L2_SCREEN) = (uint8_t)s; }
void engine_set_demand_redraw(int mode)   { G32(0x57d340u) = (uint32_t)mode; }     /* DAT_0057d340 */

/* ---------------------------------------------------------------- seams → retail (unported callees) */
#define RETAIL(call) do { rh_port_retail_enter(); call; rh_port_retail_leave(); } while (0)
typedef int (__cdecl *r_i_t)(int);
typedef int (__cdecl *r_wav_t)(const char *, int, int);
typedef int (__cdecl *r_v_t)(void);
typedef int (__cdecl *r_ii_t)(int, int);
static void seam_present(int mode) { RETAIL(((r_i_t)0x426360u)(mode)); }        /* FUN_00426360: Stop buffer */
static void seam_footstep(int id)  { RETAIL(((r_i_t)0x426120u)(id)); }          /* FUN_00426120 */
static void seam_alert(int id)     { RETAIL(((r_i_t)0x426216u)(id)); }          /* FUN_00426216 */
static void seam_wav(const char *n, int loop, int z) { RETAIL(((r_wav_t)0x427990u)(n, loop, z)); }  /* FUN_00427990 */
static void seam_batl(void)        { seam_wav("ff_batl.wav", 0, 0); }
static void seam_click(void)       { RETAIL(((r_v_t)0x499acau)()); }            /* FUN_00499aca */
static void seam_game_over(void)   { RETAIL(((r_ii_t)0x43eda0u)(0x24, 0)); }    /* FUN_0043eda0(0x24, 0) */
static uint32_t seam_clock(void)   { return ((DWORD (WINAPI *)(void))iat_lookup("timeGetTime"))(); }

/* called once from rh_port_bind_once (rh_hooks_ai.c) */
void rh_combat_bind_seams(void)
{
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
}

/* ---------------------------------------------------------------- adapters with a result inside EVQ */
static int dispatch_q(int a, int d)       { int r; EVQ(r = engine_combat_dispatch(a, d, q, MP_ACTIVE)); return r; }
static int rebellion_q(int region)        { int r; EVQ(r = engine_combat_spawn_rebellion(region, q, MP_ACTIVE)); return r; }
static int classify_step_q(int dir)       { int r; EVQ(r = engine_army_move_classify_step(dir, q, MP_ACTIVE)); return r; }
static int eval_dir_q(int dir)            { int r; EVQ(r = engine_army_move_eval_dir(dir, q, MP_ACTIVE)); return r; }
static int classify_cell_q(int off)       { int r; EVQ(r = engine_army_move_classify_cell(off, q, MP_ACTIVE)); return r; }
static int eval_occupied_q(void)          { int r; EVQ(r = engine_army_move_eval_occupied(q, MP_ACTIVE)); return r; }
static void deploy_cell(int army, int region) { int c, r; engine_combat_find_deploy_cell(army, region, &c, &r); }
/* FUN_00465d28 keeps its waypoint in [ebp-0xc] / [ebp-0x10] = e0-0x10 / e0-0x14, never initialized (quirk #229) */
static void move_run_res(void)
{
    g_army_move_wp_col = rh_res_get(0x10); g_army_move_wp_row = rh_res_get(0x14);
    EVQ(engine_army_move_run(q, MP_ACTIVE));
    rh_res_put(0x10, g_army_move_wp_col); rh_res_put(0x14, g_army_move_wp_row);
}

#define RH_TABLE RH_HOOKS_COMBAT
extern hook_t RH_HOOKS_COMBAT[];
/* row indices (must follow RH_HOOKS_COMBAT's order) */
enum {
    X_combat_resolve, X_combat_clear_result, X_combat_load_muster, X_combat_army_strength, X_combat_size_battle,
    X_combat_dispatch, X_combat_aftermath, X_combat_contest_region, X_combat_transfer_ownership,
    X_combat_demolish_realm, X_combat_transfer_army, X_combat_recompute_condition, X_combat_halve_garrison,
    X_combat_merge_stack, X_combat_capital_defender, X_combat_muster_defender, X_combat_prime_muster,
    X_combat_raise_region_unit, X_combat_garrison_realm, X_combat_deploy_cell, X_combat_place_garrison,
    X_combat_settlement_slot, X_combat_break_siege_link, X_combat_reinforce_keep, X_combat_begin_siege,
    X_combat_region_event_hi, X_combat_enter_region, X_combat_notify_occupation, X_combat_settle_region,
    X_combat_spawn_rebellion, X_combat_resign_player, X_combat_mp_resign_sweep, X_combat_rotate_merc_offers,
    X_unit_clear_contract, X_unit_disband, X_unit_remove, X_unit_absorb, X_unit_map_unlink, X_unit_map_link,
    X_unit_recompute_wage, X_realm_refresh_wages, X_rank_check_elimination, X_diplo_battle_adjust,
    X_move_run, X_move_classify_step, X_move_eval_dir, X_move_classify_cell, X_move_eval_occupied,
    X_move_commit_step, X_move_terrain_passable, X_move_decay_cell, X_move_blocked_branch, X_move_special_tile,
    X_move_region_event_lo, X_move_combat_engage, X_move_arrival, X_path_direction_8way, X_terr_mark_draw_box,
    X_ai_run_turn_twin,
};

/* engine_combat.c — the resolver and its callers (retail reads the combatants from the combat scalars) */
HOOK_V(combat_resolve,            0, engine_combat_resolve())
HOOK_V(combat_clear_result,       1, engine_combat_clear_battle_result(I(0)))
HOOK_V(combat_load_muster,        2, engine_combat_load_muster(I(0), I(1)))
HOOK_R(combat_army_strength,      1, engine_combat_army_strength(I(0)))
HOOK_R(combat_size_battle,        0, engine_combat_size_battle((int)s_slot_00568210, (int)s_slot_00568218, (int)s_slot_0057c8cc))
HOOK_R(combat_dispatch,           2, dispatch_q(I(0), I(1)))
HOOK_V(combat_aftermath,          1, EVQ(engine_combat_aftermath(I(0), q, MP_ACTIVE)))
HOOK_V(combat_contest_region,     2, EVQ(engine_combat_contest_region(I(0), a[1], q, MP_ACTIVE)))
HOOK_V(combat_transfer_ownership, 2, EVQ(engine_combat_transfer_ownership(a[0], a[1], q, MP_ACTIVE)))
HOOK_V(combat_demolish_realm,     1, EVQ(engine_combat_demolish_realm(I(0), q, MP_ACTIVE)))
HOOK_V(combat_transfer_army,      2, EVQ(engine_combat_transfer_army(a[0], I(1), q, MP_ACTIVE)))
HOOK_V(combat_recompute_condition, 1, engine_combat_recompute_condition(I(0)))
HOOK_V(combat_halve_garrison,     1, engine_combat_halve_garrison(I(0)))
HOOK_V(combat_merge_stack,        2, EVQ(engine_combat_merge_stack(I(0), I(1), q)))
HOOK_R(combat_capital_defender,   1, engine_combat_find_capital_defender(I(0)))
HOOK_R(combat_muster_defender,    4, engine_combat_muster_defender(I(0), I(1), I(2), I(3)))
HOOK_V(combat_prime_muster,       2, engine_combat_prime_muster(I(0), (int32_t)a[1]))
HOOK_R(combat_raise_region_unit,  1, engine_combat_raise_region_unit(I(0)))
HOOK_R(combat_garrison_realm,     2, engine_combat_find_garrison_realm(I(0), I(1)))
HOOK_V(combat_deploy_cell,        2, deploy_cell(I(0), I(1)))
HOOK_V(combat_place_garrison,     2, EVQ(engine_combat_place_garrison(a[0], I(1), q)))
HOOK_R(combat_settlement_slot,    1, engine_combat_settlement_find_slot(I(0)))
HOOK_V(combat_break_siege_link,   1, engine_combat_break_siege_link(I(0)))
HOOK_V(combat_reinforce_keep,     2, EVQ(engine_combat_reinforce_keep(a[0], I(1), q)))
HOOK_V(combat_begin_siege,        2, EVQ(engine_combat_begin_siege(a[0], I(1), q)))
HOOK_V(combat_region_event_hi,    2, EVQ(engine_army_move_region_event_hi(I(0), I(1), q, MP_ACTIVE)))   /* (self, cell_off) */
HOOK_V(combat_enter_region,       2, EVQ(engine_combat_army_enter_region(I(0), a[1], q)))
HOOK_V(combat_notify_occupation,  2, EVQ(engine_combat_notify_occupation(I(0), I(1), q)))
HOOK_V(combat_settle_region,      2, EVQ(engine_combat_army_settle_region(I(0), a[1], q, MP_ACTIVE)))
HOOK_R(combat_spawn_rebellion,    1, rebellion_q(I(0)))
HOOK_V(combat_resign_player,      1, EVQ(engine_combat_resign_player(I(0), q, MP_ACTIVE)))
HOOK_V(combat_mp_resign_sweep,    0, EVQ(engine_combat_mp_resign_sweep(q, MP_ACTIVE)))
HOOK_V(combat_rotate_merc_offers, 0, engine_combat_rotate_merc_offers())
/* engine_realm_upkeep.c / engine_ranking.c / engine_diplomacy.c — the battle's unit, elimination and relation callees */
HOOK_V(unit_clear_contract,       1, engine_unit_clear_contract(I(0)))
HOOK_V(unit_disband,              1, engine_unit_disband(I(0)))
HOOK_V(unit_remove,               1, engine_unit_remove(I(0)))
HOOK_V(unit_absorb,               1, engine_unit_absorb(I(0)))
HOOK_V(unit_map_unlink,           1, engine_unit_map_unlink(I(0)))
HOOK_V(unit_map_link,             1, engine_unit_map_link(I(0)))
HOOK_R(unit_recompute_wage,       1, engine_unit_recompute_wage(I(0)))
HOOK_V(realm_refresh_wages,       0, engine_realm_refresh_wage_bills())
HOOK_V(rank_check_elimination,    1, EVQ(engine_ranking_check_elimination(a[0], q, MP_ACTIVE)))
HOOK_V(diplo_battle_adjust,       3, EVQ(engine_diplomacy_battle_adjust(I(0), I(1), I(2), q)))
/* engine_army_move.c — the movement-step executor and its classifier / evaluator / event handlers */
HOOK_V(move_run,                  0, move_run_res())
HOOK_R(move_classify_step,        1, classify_step_q(I(0)))
HOOK_R(move_eval_dir,             1, eval_dir_q(I(0)))
HOOK_R(move_classify_cell,        1, classify_cell_q(I(0)))
HOOK_R(move_eval_occupied,        0, eval_occupied_q())
HOOK_V(move_commit_step,          0, engine_army_move_commit_step())
HOOK_R(move_terrain_passable,     1, engine_army_move_terrain_passable(I(0)))
HOOK_V(move_decay_cell,           1, engine_army_move_decay_region_cell(I(0)))
HOOK_V(move_blocked_branch,       0, EVQ(engine_army_move_blocked_branch(q)))
HOOK_V(move_special_tile,         2, EVQ(engine_army_move_special_tile(I(0), I(1), q, MP_ACTIVE)))
HOOK_V(move_region_event_lo,      2, EVQ(engine_army_move_region_event_lo(I(0), I(1), q, MP_ACTIVE)))
HOOK_V(move_combat_engage,        2, EVQ(engine_army_move_combat_engage(I(0), a[1], q, MP_ACTIVE)))
HOOK_V(move_arrival,              1, engine_army_move_arrival(I(0)))
HOOK_R(path_direction_8way,       4, engine_path_direction_8way(I(0), I(1), I(2), I(3)))
HOOK_V(terr_mark_draw_box,        3, engine_territory_mark_draw_box(I(0), I(1), I(2)))
/* the aftermath re-runs the AI through FUN_0049df48, a byte-for-byte twin of FUN_0049a990 (ai_run_turn) */
HOOK_V(ai_run_turn_twin,          0, engine_army_ai_run_turn())

hook_t RH_HOOKS_COMBAT[] = {
    { "combat_resolve", 0x4aad07u, (void *)w_combat_resolve, 0, 0, 0, 0, "" },
    { "combat_clear_result", 0x4aa89fu, (void *)w_combat_clear_result, 0, 0, 0, 0, "" },
    { "combat_load_muster", 0x422c12u, (void *)w_combat_load_muster, 0, 0, 0, 0, "" },
    { "combat_army_strength", 0x4ab2aau, (void *)w_combat_army_strength, 0, 0, 0, 0, "" },
    { "combat_size_battle", 0x4a6a30u, (void *)w_combat_size_battle, 0, 0, 0, 0, "" },
    { "combat_dispatch", 0x4a7158u, (void *)w_combat_dispatch, 0, 0, 0, 0, "" },
    { "combat_aftermath", 0x4ab383u, (void *)w_combat_aftermath, 0, 0, 0, 0, "" },
    { "combat_contest_region", 0x4a6c68u, (void *)w_combat_contest_region, 0, 0, 0, 0, "" },
    { "combat_transfer_ownership", 0x4a72feu, (void *)w_combat_transfer_ownership, 0, 0, 0, 0, "" },
    { "combat_demolish_realm", 0x4ac3c6u, (void *)w_combat_demolish_realm, 0, 0, 0, 0, "" },
    { "combat_transfer_army", 0x437535u, (void *)w_combat_transfer_army, 0, 0, 0, 0, "" },
    { "combat_recompute_condition", 0x4a80dbu, (void *)w_combat_recompute_condition, 0, 0, 0, 0, "" },
    { "combat_halve_garrison", 0x4ad8ccu, (void *)w_combat_halve_garrison, 0, 0, 0, 0, "" },
    { "combat_merge_stack", 0x4aa181u, (void *)w_combat_merge_stack, 0, 0, 0, 0, "" },
    { "combat_capital_defender", 0x46d42cu, (void *)w_combat_capital_defender, 0, 0, 0, 0, "" },
    { "combat_muster_defender", 0x4a50aeu, (void *)w_combat_muster_defender, 0, 0, 0, 0, "" },
    { "combat_prime_muster", 0x4aaa80u, (void *)w_combat_prime_muster, 0, 0, 0, 0, "" },
    { "combat_raise_region_unit", 0x4a5003u, (void *)w_combat_raise_region_unit, 0, 0, 0, 0, "" },
    { "combat_garrison_realm", 0x4a07f2u, (void *)w_combat_garrison_realm, 0, 0, 0, 0, "" },
    { "combat_deploy_cell", 0x4a689du, (void *)w_combat_deploy_cell, 0, 0, 0, 0, "" },
    { "combat_place_garrison", 0x4a79a3u, (void *)w_combat_place_garrison, 0, 0, 0, 0, "" },
    { "combat_settlement_slot", 0x4a551bu, (void *)w_combat_settlement_slot, 0, 0, 0, 0, "" },
    { "combat_break_siege_link", 0x43b917u, (void *)w_combat_break_siege_link, 0, 0, 0, 0, "" },
    { "combat_reinforce_keep", 0x4a78eau, (void *)w_combat_reinforce_keep, 0, 0, 0, 0, "" },
    { "combat_begin_siege", 0x4a7ca2u, (void *)w_combat_begin_siege, 0, 0, 0, 0, "" },
    { "combat_region_event_hi", 0x4686a0u, (void *)w_combat_region_event_hi, 0, 0, 0, 0, "" },
    { "combat_enter_region", 0x4abb36u, (void *)w_combat_enter_region, 0, 0, 0, 0, "" },
    { "combat_notify_occupation", 0x4abf77u, (void *)w_combat_notify_occupation, 0, 0, 0, 0, "" },
    { "combat_settle_region", 0x4abd0fu, (void *)w_combat_settle_region, 0, 0, 0, 0, "" },
    { "combat_spawn_rebellion", 0x4ac185u, (void *)w_combat_spawn_rebellion, 0, 0, 0, 0, "" },
    { "combat_resign_player", 0x49cf20u, (void *)w_combat_resign_player, 0, 0, 0, 0, "" },
    { "combat_mp_resign_sweep", 0x49b2a3u, (void *)w_combat_mp_resign_sweep, 0, 0, 0, 0, "" },
    { "combat_rotate_merc_offers", 0x4aca2bu, (void *)w_combat_rotate_merc_offers, 0, 0, 0, 0, "" },
    { "unit_clear_contract", 0x4ac6feu, (void *)w_unit_clear_contract, 0, 0, 0, 0, "" },
    { "unit_disband", 0x4aa039u, (void *)w_unit_disband, 0, 0, 0, 0, "" },
    { "unit_remove", 0x46f0a9u, (void *)w_unit_remove, 0, 0, 0, 0, "" },
    { "unit_absorb", 0x4aba5au, (void *)w_unit_absorb, 0, 0, 0, 0, "" },
    { "unit_map_unlink", 0x46ec6eu, (void *)w_unit_map_unlink, 0, 0, 0, 0, "" },
    { "unit_map_link", 0x46eddfu, (void *)w_unit_map_link, 0, 0, 0, 0, "" },
    { "unit_recompute_wage", 0x4ad52bu, (void *)w_unit_recompute_wage, 0, 0, 0, 0, "" },
    { "realm_refresh_wages", 0x4ad426u, (void *)w_realm_refresh_wages, 0, 0, 0, 0, "" },
    { "rank_check_elimination", 0x49b42bu, (void *)w_rank_check_elimination, 0, 0, 0, 0, "" },
    { "diplo_battle_adjust", 0x4a1ee1u, (void *)w_diplo_battle_adjust, 0, 0, 0, 0, "" },
    { "move_run", 0x465d28u, (void *)w_move_run, 0, 0, 0, 0, "" },
    { "move_classify_step", 0x46634du, (void *)w_move_classify_step, 0, 0, 0, 0, "" },
    { "move_eval_dir", 0x466893u, (void *)w_move_eval_dir, 0, 0, 0, 0, "" },
    { "move_classify_cell", 0x466c3cu, (void *)w_move_classify_cell, 0, 0, 0, 0, "" },
    { "move_eval_occupied", 0x4658c1u, (void *)w_move_eval_occupied, 0, 0, 0, 0, "" },
    { "move_commit_step", 0x466d84u, (void *)w_move_commit_step, 0, 0, 0, 0, "" },
    { "move_terrain_passable", 0x469e0eu, (void *)w_move_terrain_passable, 0, 0, 0, 0, "" },
    { "move_decay_cell", 0x469e5bu, (void *)w_move_decay_cell, 0, 0, 0, 0, "" },
    { "move_blocked_branch", 0x46673cu, (void *)w_move_blocked_branch, 0, 0, 0, 0, "" },
    { "move_special_tile", 0x468ae2u, (void *)w_move_special_tile, 0, 0, 0, 0, "" },
    { "move_region_event_lo", 0x46873fu, (void *)w_move_region_event_lo, 0, 0, 0, 0, "" },
    { "move_combat_engage", 0x4296b5u, (void *)w_move_combat_engage, 0, 0, 0, 0, "" },
    { "move_arrival", 0x467196u, (void *)w_move_arrival, 0, 0, 0, 0, "" },
    { "path_direction_8way", 0x46f434u, (void *)w_path_direction_8way, 0, 0, 0, 0, "" },
    { "terr_mark_draw_box", 0x46e067u, (void *)w_terr_mark_draw_box, 0, 0, 0, 0, "" },
    { "ai_run_turn_twin", 0x49df48u, (void *)w_ai_run_turn_twin, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_COMBAT_N = sizeof RH_HOOKS_COMBAT / sizeof *RH_HOOKS_COMBAT;
