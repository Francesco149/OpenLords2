/* rh_hooks_battle.c — the battle path's exits and the siege assault (src/engine_combat.c: the AUTO_RESOLVE button
 * FUN_0043b622, the battlefield end FUN_0043be65 / per-frame end check FUN_00477dfc and its result-row picker
 * FUN_00478419, the siege assault FUN_004a8aab and its equipment staging FUN_004aa6ca) vs retail, on identical state
 * (rh_hooks.h contract; shared plumbing in rh_port.h / rh_hooks_ai.c).
 *
 * BINDING.  The new battlefield runtime globals (engine_combat_globals.c) and engine.c's engine state DAT_0052af94
 * are aliased at their retail VAs. The battlefield and result screens stay retail: the port's seams call retail's own
 * functions, bracketed by rh_port_retail_enter/leave. The result movie seam passes retail's own name string (the
 * 16-byte-stride tables at 0x4d9278 / 0x4d93f8), so whatever FUN_0042d91b keeps is retail's pointer.
 *
 * The corpus has no interactive battlefield and no siege yet: these rows are reached by human traces (docs/
 * human-traces.md §A) or a bot that besieges. */
#include "rh_port.h"
#include "save_schema_globals.h"
#include "engine.h"
#include "engine_combat.h"

/* ---------------------------------------------------------------- binding: runtime globals = retail memory */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(
A(g_combat_owner_5656f8,0x5656f8) A(g_combat_ctrl_52afac,0x52afac) A(g_combat_flag_53f248,0x53f248)
A(g_combat_result_row,0x553fb8) A(g_combat_msg_clock,0x4eeae8)
/* engine.c */
A(g_engine_state,0x52af94)
);
#undef A

/* ---------------------------------------------------------------- seams → retail (unported callees) */
#define RETAIL(call) do { rh_port_retail_enter(); call; rh_port_retail_leave(); } while (0)
typedef int (__cdecl *r_v_t)(void);
typedef int (__cdecl *r_i_t)(int);
typedef int (__cdecl *r_ii_t)(int, int);
typedef int (__cdecl *r_smk_t)(uint32_t, int, int, int, int);
static void seam_siege_result(void)  { RETAIL(((r_v_t)0x42c64fu)()); }    /* FUN_0042c64f */
static void seam_return_to_map(void) { RETAIL(((r_v_t)0x4788c6u)()); }    /* FUN_004788c6 */
static void seam_writeback(void)     { RETAIL(((r_v_t)0x47f474u)()); }    /* FUN_0047f474 */
static void seam_result_panel(void)  { RETAIL(((r_v_t)0x423241u)()); }    /* FUN_00423241 */
static void seam_stop_stream(int s)  { RETAIL(((r_i_t)0x426d11u)(s)); }   /* FUN_00426d11 */
static void seam_message(int id, int a) { RETAIL(((r_ii_t)0x43eda0u)(id, a)); }   /* FUN_0043eda0 */
static void seam_result_movie(int siege, int index, int x, int y, int z, int screen)   /* FUN_0042d91b */
{
    uint32_t name = (siege ? 0x4d93f8u : 0x4d9278u) + (uint32_t)index * 16;   /* retail's own string */
    RETAIL(((r_smk_t)0x42d91bu)(name, x, y, z, screen));
}

/* called once from rh_port_bind_once (rh_hooks_ai.c) */
void rh_battle_bind_seams(void)
{
    engine_combat_bind_battlefield_fns(seam_siege_result, seam_return_to_map, seam_writeback, seam_result_panel,
                                       seam_stop_stream, seam_result_movie, seam_message);
}

#define RH_TABLE RH_HOOKS_BATTLE
extern hook_t RH_HOOKS_BATTLE[];
/* row indices (must follow RH_HOOKS_BATTLE's order) */
enum {
    X_battle_auto_resolve, X_battle_end, X_battle_end_tick, X_battle_pick_result_row, X_battle_siege_assault,
    X_battle_stage_siege_side,
};

HOOK_V(battle_auto_resolve,      0, EVQ(engine_combat_auto_resolve_button(q, MP_ACTIVE)))
HOOK_V(battle_end,               0, EVQ(engine_combat_battlefield_end(q, MP_ACTIVE)))
HOOK_V(battle_end_tick,          0, EVQ(engine_combat_battlefield_end_tick(q, MP_ACTIVE)))
HOOK_V(battle_pick_result_row,   0, engine_combat_pick_result_row())
HOOK_V(battle_siege_assault,     1, EVQ(engine_combat_siege_assault(I(0), q, MP_ACTIVE)))
HOOK_V(battle_stage_siege_side,  2, engine_combat_stage_siege_side(I(0), I(1)))

hook_t RH_HOOKS_BATTLE[] = {
    { "battle_auto_resolve", 0x43b622u, (void *)w_battle_auto_resolve, 0, 0, 0, 0, "" },
    { "battle_end", 0x43be65u, (void *)w_battle_end, 0, 0, 0, 0, "" },
    { "battle_end_tick", 0x477dfcu, (void *)w_battle_end_tick, 0, 0, 0, 0, "" },
    { "battle_pick_result_row", 0x478419u, (void *)w_battle_pick_result_row, 0, 0, 0, 0, "" },
    { "battle_siege_assault", 0x4a8aabu, (void *)w_battle_siege_assault, 0, 0, 0, 0, "" },
    { "battle_stage_siege_side", 0x4aa6cau, (void *)w_battle_stage_siege_side, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BATTLE_N = sizeof RH_HOOKS_BATTLE / sizeof *RH_HOOKS_BATTLE;
