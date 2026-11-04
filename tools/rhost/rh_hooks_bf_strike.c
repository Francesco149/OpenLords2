/* rh_hooks_bf_strike.c — the battlefield port, partition 'strike' (src/engine_bf_strike.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_STRIKE
extern hook_t RH_HOOKS_BF_STRIKE[];
/* row indices (must follow the table's order) */
enum {
    X_bf_426120, X_bf_4262cf, X_bf_46eb78, X_bf_46efbe, X_bf_46e767, X_bf_493cb9, X_bf_493b61, X_bf_494075,
    X_bf_494125, X_bf_4941c5, X_bf_494265, X_bf_46f525, X_bf_4956cc,
};

HOOK_V(bf_426120, 1, bf_426120(I(0)))              /* callers ignore EAX (FUN_004262cf passes it on: ignored too) */
HOOK_V(bf_4262cf, 1, bf_4262cf(I(0)))
HOOK_R(bf_46eb78, 1, bf_46eb78(I(0)))
HOOK_V(bf_46efbe, 1, bf_46efbe(I(0)))              /* retail's AL = the record index: no caller reads it */
HOOK_R(bf_46e767, 5, bf_46e767(I(0), I(1), I(2), I(3), I(4)))
HOOK_V(bf_493cb9, 5, bf_493cb9(I(0), I(1), I(2), I(3), I(4)))
HOOK_V(bf_493b61, 1, bf_493b61(I(0)))
HOOK_R(bf_494075, 0, bf_494075())
HOOK_V(bf_494125, 0, bf_494125())
HOOK_V(bf_4941c5, 0, bf_4941c5())
HOOK_V(bf_494265, 0, bf_494265())
/* the facing is AL; retail's EAX upper bytes are y0's (every path loads arg 2 into EAX before `mov al, N`) */
HOOK_R(bf_46f525, 4, ((uint32_t)bf_46f525(I(0), I(1), I(2), I(3)) & 0xffu) | (a[1] & 0xffffff00u))
HOOK_R(bf_4956cc, 3, bf_4956cc(I(0), I(1), I(2)))

hook_t RH_HOOKS_BF_STRIKE[] = {
    { "bf_426120", 0x426120u, (void *)w_bf_426120, 0, 0, 0, 0, "" },
    { "bf_4262cf", 0x4262cfu, (void *)w_bf_4262cf, 0, 0, 0, 0, "" },
    { "bf_46eb78", 0x46eb78u, (void *)w_bf_46eb78, 0, 0, 0, 0, "" },
    { "bf_46efbe", 0x46efbeu, (void *)w_bf_46efbe, 0, 0, 0, 0, "" },
    { "bf_46e767", 0x46e767u, (void *)w_bf_46e767, 0, 0, 0, 0, "" },
    { "bf_493cb9", 0x493cb9u, (void *)w_bf_493cb9, 0, 0, 0, 0, "" },
    { "bf_493b61", 0x493b61u, (void *)w_bf_493b61, 0, 0, 0, 0, "" },
    { "bf_494075", 0x494075u, (void *)w_bf_494075, 0, 0, 0, 0, "" },
    { "bf_494125", 0x494125u, (void *)w_bf_494125, 0, 0, 0, 0, "" },
    { "bf_4941c5", 0x4941c5u, (void *)w_bf_4941c5, 0, 0, 0, 0, "" },
    { "bf_494265", 0x494265u, (void *)w_bf_494265, 0, 0, 0, 0, "" },
    { "bf_46f525", 0x46f525u, (void *)w_bf_46f525, 0, 0, 0, 0, "" },
    { "bf_4956cc", 0x4956ccu, (void *)w_bf_4956cc, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_STRIKE_N = sizeof RH_HOOKS_BF_STRIKE / sizeof *RH_HOOKS_BF_STRIKE;
