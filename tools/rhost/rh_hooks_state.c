/* rh_hooks_state.c — the strategic-map army-state machine (src/engine_army_state.c: the per-tick driver FUN_004650b0,
 * the turn driver's movement arming / siege-build / raid / march planners) vs retail, on identical state (rh_hooks.h
 * contract; shared plumbing — binding, sync, EVQ, the generic wrapper body — in rh_port.h / rh_hooks_ai.c).
 *
 * BINDING.  engine_army_state's file-scope mirrors (record cursor DAT_005533e8, abort flag DAT_0050a49e, live count
 * DAT_00553f08, raider cursor DAT_004e59dc) are synced in rh_port_sync_in/out; the driver's movement seam is the port
 * executor (rl_link_bind_once, src/rl_link_logic.c).
 *
 * HANDLERS.  FUN_004650b0 reaches its four state handlers (FUN_0046521f / 00465486 / 00465622 / 00465761) only
 * through the pointer table PTR_FUN_004d6a50: no E8 call site, so they are verified as part of the driver's row.
 *
 * STACK RESIDUE.  The driver's handlers call the executor FUN_00465d28 from frames of two depths (quirk #232), so the
 * executor's uninitialized waypoint (quirk #229) is read from e0-0x3c/-0x40 (states 1/2) or e0-0x38/-0x3c (states
 * 3/4) below the driver's e0.  The port models the window e0-0x30..e0-0x40 (g_army_state_res); the adapter loads it
 * and puts it back with rh_res_set (not compared: the other callees' frames — enter / settle region, income — also
 * write there, and the port does not model them; the state dwords are compared as always). */
#include "rh_port.h"
#include "rl_link.h"
#include "save_schema_globals.h"
#include "engine_army_state.h"
#include "engine_army_move.h"

#define RH_TABLE RH_HOOKS_STATE
extern hook_t RH_HOOKS_STATE[];
/* row indices (must follow RH_HOOKS_STATE's order) */
enum {
    X_state_run, X_state_arm_movement, X_state_siege_build_step, X_state_scan_siege_built,
    X_state_raid_assign_target, X_state_plan_raids, X_state_plan_capital_marches, X_state_plan_settle_marches,
    X_state_find_settle_spot,
};

/* entered from retail code only (the port's frame driver calls engine_army_state_run directly, with the window
 * engine_army_state_res_ai_step models): the window is the real residue */
static void state_run_res(void)
{
    for (int k = 0; k < 5; k++) g_army_state_res[k] = rh_res_get(0x30 + 4 * k);
    EVQ(engine_army_state_run(q, MP_ACTIVE));
    for (int k = 0; k < 5; k++) rh_res_set(0x30 + 4 * k, g_army_state_res[k]);
}
/* FUN_00428007 leaves the matched cell in the shared cursor DAT_00569570/78 (the box scan's loop counters, #226) */
static int find_settle_spot(int realm) { int c, r; return engine_army_state_find_settle_spot(realm, &c, &r); }

HOOK_V(state_run,                  0, state_run_res())
HOOK_R(state_arm_movement,         1, engine_army_state_arm_movement((unsigned)a[0]))
HOOK_R(state_siege_build_step,     1, engine_army_state_siege_build_step(I(0)))
HOOK_R(state_scan_siege_built,     0, engine_army_state_scan_siege_built())
HOOK_R(state_raid_assign_target,   1, engine_army_state_raid_assign_target(I(0)))
HOOK_V(state_plan_raids,           0, engine_army_state_plan_raids())
HOOK_V(state_plan_capital_marches, 0, engine_army_state_plan_capital_marches())
HOOK_V(state_plan_settle_marches,  0, engine_army_state_plan_settle_marches())
HOOK_R(state_find_settle_spot,     1, rl_428007(I(0)))

hook_t RH_HOOKS_STATE[] = {
    { "state_run", 0x4650b0u, (void *)w_state_run, 0, 0, 0, 0, "" },
    { "state_arm_movement", 0x4a4f5bu, (void *)w_state_arm_movement, 0, 0, 0, 0, "" },
    { "state_siege_build_step", 0x4a8507u, (void *)w_state_siege_build_step, 0, 0, 0, 0, "" },
    { "state_scan_siege_built", 0x4a84bau, (void *)w_state_scan_siege_built, 0, 0, 0, 0, "" },
    { "state_raid_assign_target", 0x4ac5bau, (void *)w_state_raid_assign_target, 0, 0, 0, 0, "" },
    { "state_plan_raids", 0x4ac499u, (void *)w_state_plan_raids, 0, 0, 0, 0, "" },
    { "state_plan_capital_marches", 0x429418u, (void *)w_state_plan_capital_marches, 0, 0, 0, 0, "" },
    { "state_plan_settle_marches", 0x4280e9u, (void *)w_state_plan_settle_marches, 0, 0, 0, 0, "" },
    { "state_find_settle_spot", 0x428007u, (void *)w_state_find_settle_spot, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_STATE_N = sizeof RH_HOOKS_STATE / sizeof *RH_HOOKS_STATE;
