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
#include "rl_link.h"
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

/* the battlefield's way out (siege result, return to map, writeback, result panel / movie, message): bound by
 * rl_link_bind_once (src/rl_link_logic.c) to retail-layout ports */

#define RH_TABLE RH_HOOKS_BATTLE
extern hook_t RH_HOOKS_BATTLE[];
/* row indices (must follow RH_HOOKS_BATTLE's order) */
enum {
    X_battle_auto_resolve, X_battle_end, X_battle_end_tick, X_battle_pick_result_row, X_battle_siege_assault,
    X_battle_stage_siege_side,
};

HOOK_V(battle_auto_resolve,      0, rl_43b622())
HOOK_V(battle_end,               0, EVQ(engine_combat_battlefield_end(q, MP_ACTIVE)))
HOOK_V(battle_end_tick,          0, rl_477dfc())
HOOK_V(battle_pick_result_row,   0, engine_combat_pick_result_row())
HOOK_V(battle_siege_assault,     1, rl_4a8aab(I(0)))
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
