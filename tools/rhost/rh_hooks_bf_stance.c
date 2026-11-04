/* rh_hooks_bf_stance.c — the battlefield port, partition 'stance' (src/engine_bf_stance.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). The stance handlers and FUN_00496b9f return garbage EAX no caller reads (0x489401 ignores the
 * dispatch's value; FUN_00496b9f's other callers 0x43bcf8 / 0x448295 tail-call it from void functions): HOOK_V. */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_STANCE
extern hook_t RH_HOOKS_BF_STANCE[];
/* row indices (must follow the table's order) */
enum {
    X_bf_48cba9, X_bf_48cce3, X_bf_48ce1d, X_bf_48ce81, X_bf_48d013, X_bf_48d16e, X_bf_48d412, X_bf_48d6fc,
    X_bf_48d9ce, X_bf_48db84, X_bf_48ddc7, X_bf_48dfbb, X_bf_48e097, X_bf_48e234, X_bf_48e2c8, X_bf_48e39c,
    X_bf_48e4cc, X_bf_48e774, X_bf_48e8b8, X_bf_48ea95, X_bf_48ebf1, X_bf_48ed95, X_bf_495956, X_bf_495c41,
    X_bf_495f3d, X_bf_496566, X_bf_496b9f, X_bf_496d76,
};

HOOK_V(bf_48cba9, 0, bf_48cba9())
HOOK_V(bf_48cce3, 0, bf_48cce3())
HOOK_V(bf_48ce1d, 0, bf_48ce1d())
HOOK_V(bf_48ce81, 0, bf_48ce81())
HOOK_V(bf_48d013, 0, bf_48d013())
HOOK_V(bf_48d16e, 0, bf_48d16e())
HOOK_V(bf_48d412, 0, bf_48d412())
HOOK_V(bf_48d6fc, 0, bf_48d6fc())
HOOK_V(bf_48d9ce, 0, bf_48d9ce())
HOOK_V(bf_48db84, 0, bf_48db84())
HOOK_V(bf_48ddc7, 0, bf_48ddc7())
HOOK_V(bf_48dfbb, 0, bf_48dfbb())
HOOK_V(bf_48e097, 0, bf_48e097())
HOOK_V(bf_48e234, 0, bf_48e234())
HOOK_V(bf_48e2c8, 0, bf_48e2c8())
HOOK_V(bf_48e39c, 0, bf_48e39c())
HOOK_V(bf_48e4cc, 0, bf_48e4cc())
HOOK_V(bf_48e774, 0, bf_48e774())
HOOK_V(bf_48e8b8, 0, bf_48e8b8())
HOOK_R(bf_48ea95, 0, bf_48ea95())
HOOK_R(bf_48ebf1, 0, bf_48ebf1())
HOOK_R(bf_48ed95, 1, bf_48ed95(I(0)))
HOOK_R(bf_495956, 3, bf_495956(I(0), I(1), I(2)))
HOOK_R(bf_495c41, 3, bf_495c41(I(0), I(1), I(2)))
HOOK_R(bf_495f3d, 6, bf_495f3d(I(0), I(1), I(2), I(3), I(4), I(5)))
HOOK_R(bf_496566, 4, bf_496566(I(0), I(1), I(2), I(3)))
HOOK_V(bf_496b9f, 0, bf_496b9f())
HOOK_R(bf_496d76, 3, bf_496d76(I(0), I(1), I(2)))

hook_t RH_HOOKS_BF_STANCE[] = {
    { "bf_48cba9", 0x48cba9u, (void *)w_bf_48cba9, 0, 0, 0, 0, "" },
    { "bf_48cce3", 0x48cce3u, (void *)w_bf_48cce3, 0, 0, 0, 0, "" },
    { "bf_48ce1d", 0x48ce1du, (void *)w_bf_48ce1d, 0, 0, 0, 0, "" },
    { "bf_48ce81", 0x48ce81u, (void *)w_bf_48ce81, 0, 0, 0, 0, "" },
    { "bf_48d013", 0x48d013u, (void *)w_bf_48d013, 0, 0, 0, 0, "" },
    { "bf_48d16e", 0x48d16eu, (void *)w_bf_48d16e, 0, 0, 0, 0, "" },
    { "bf_48d412", 0x48d412u, (void *)w_bf_48d412, 0, 0, 0, 0, "" },
    { "bf_48d6fc", 0x48d6fcu, (void *)w_bf_48d6fc, 0, 0, 0, 0, "" },
    { "bf_48d9ce", 0x48d9ceu, (void *)w_bf_48d9ce, 0, 0, 0, 0, "" },
    { "bf_48db84", 0x48db84u, (void *)w_bf_48db84, 0, 0, 0, 0, "" },
    { "bf_48ddc7", 0x48ddc7u, (void *)w_bf_48ddc7, 0, 0, 0, 0, "" },
    { "bf_48dfbb", 0x48dfbbu, (void *)w_bf_48dfbb, 0, 0, 0, 0, "" },
    { "bf_48e097", 0x48e097u, (void *)w_bf_48e097, 0, 0, 0, 0, "" },
    { "bf_48e234", 0x48e234u, (void *)w_bf_48e234, 0, 0, 0, 0, "" },
    { "bf_48e2c8", 0x48e2c8u, (void *)w_bf_48e2c8, 0, 0, 0, 0, "" },
    { "bf_48e39c", 0x48e39cu, (void *)w_bf_48e39c, 0, 0, 0, 0, "" },
    { "bf_48e4cc", 0x48e4ccu, (void *)w_bf_48e4cc, 0, 0, 0, 0, "" },
    { "bf_48e774", 0x48e774u, (void *)w_bf_48e774, 0, 0, 0, 0, "" },
    { "bf_48e8b8", 0x48e8b8u, (void *)w_bf_48e8b8, 0, 0, 0, 0, "" },
    { "bf_48ea95", 0x48ea95u, (void *)w_bf_48ea95, 0, 0, 0, 0, "" },
    { "bf_48ebf1", 0x48ebf1u, (void *)w_bf_48ebf1, 0, 0, 0, 0, "" },
    { "bf_48ed95", 0x48ed95u, (void *)w_bf_48ed95, 0, 0, 0, 0, "" },
    { "bf_495956", 0x495956u, (void *)w_bf_495956, 0, 0, 0, 0, "" },
    { "bf_495c41", 0x495c41u, (void *)w_bf_495c41, 0, 0, 0, 0, "" },
    { "bf_495f3d", 0x495f3du, (void *)w_bf_495f3d, 0, 0, 0, 0, "" },
    { "bf_496566", 0x496566u, (void *)w_bf_496566, 0, 0, 0, 0, "" },
    { "bf_496b9f", 0x496b9fu, (void *)w_bf_496b9f, 0, 0, 0, 0, "" },
    { "bf_496d76", 0x496d76u, (void *)w_bf_496d76, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_STANCE_N = sizeof RH_HOOKS_BF_STANCE / sizeof *RH_HOOKS_BF_STANCE;
