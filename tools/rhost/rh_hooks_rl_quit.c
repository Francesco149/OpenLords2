/* rh_hooks_rl_quit.c — a retail-layout port, partition 'quit' (src/rl_quit.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "rl.h"

#define RH_TABLE RH_HOOKS_RL_QUIT
extern hook_t RH_HOOKS_RL_QUIT[];
#include "rh_rl_quit_alias.h"

__asm__(RH_RL_QUIT_ALIASES);

/* row indices (must follow the table's order) */
enum { X_rl_42dfe2, X_rl_425d35, X_rl_4ae15f, X_rl_49767d, X_rl_42eaa6, X_rl_4b7180, X_rl_4b2636, X_rl_43f43e };

HOOK_V(rl_42dfe2, 0, rl_42dfe2())
HOOK_R(rl_425d35, 0, rl_425d35())
HOOK_V(rl_4ae15f, 0, rl_4ae15f())
HOOK_V(rl_49767d, 0, rl_49767d())
HOOK_R(rl_42eaa6, 1, rl_42eaa6(I(0)))
HOOK_R(rl_4b7180, 1, rl_4b7180(I(0)))
HOOK_V(rl_4b2636, 0, rl_4b2636())
HOOK_V(rl_43f43e, 0, rl_43f43e())

hook_t RH_HOOKS_RL_QUIT[] = {
    { "rl_42dfe2", 0x42dfe2u, (void *)w_rl_42dfe2, 0, 0, 0, 0, "" },
    { "rl_425d35", 0x425d35u, (void *)w_rl_425d35, 0, 0, 0, 0, "" },
    { "rl_4ae15f", 0x4ae15fu, (void *)w_rl_4ae15f, 0, 0, 0, 0, "" },
    { "rl_49767d", 0x49767du, (void *)w_rl_49767d, 0, 0, 0, 0, "" },
    { "rl_42eaa6", 0x42eaa6u, (void *)w_rl_42eaa6, 0, 0, 0, 0, "" },
    { "rl_4b7180", 0x4b7180u, (void *)w_rl_4b7180, 0, 0, 0, 0, "" },
    { "rl_4b2636", 0x4b2636u, (void *)w_rl_4b2636, 0, 0, 0, 0, "" },
    { "rl_43f43e", 0x43f43eu, (void *)w_rl_43f43e, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_QUIT_N = sizeof RH_HOOKS_RL_QUIT / sizeof RH_HOOKS_RL_QUIT[0];
