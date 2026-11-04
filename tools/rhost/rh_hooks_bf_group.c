/* rh_hooks_bf_group.c — the battlefield port, partition 'group' (src/engine_bf_group.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_GROUP
extern hook_t RH_HOOKS_BF_GROUP[];
/* row indices (must follow the table's order) */
enum { X_bf_478952, X_bf_4859e5, X_bf_46ef3c, X_bf_485bb1, X_bf_488dfe };

HOOK_V(bf_478952, 0, bf_478952())
HOOK_V(bf_4859e5, 0, bf_4859e5())
HOOK_V(bf_46ef3c, 0, bf_46ef3c())
HOOK_V(bf_485bb1, 0, bf_485bb1())
HOOK_V(bf_488dfe, 0, bf_488dfe())

hook_t RH_HOOKS_BF_GROUP[] = {
    { "bf_478952", 0x478952u, (void *)w_bf_478952, 0, 0, 0, 0, "" },
    { "bf_4859e5", 0x4859e5u, (void *)w_bf_4859e5, 0, 0, 0, 0, "" },
    { "bf_46ef3c", 0x46ef3cu, (void *)w_bf_46ef3c, 0, 0, 0, 0, "" },
    { "bf_485bb1", 0x485bb1u, (void *)w_bf_485bb1, 0, 0, 0, 0, "" },
    { "bf_488dfe", 0x488dfeu, (void *)w_bf_488dfe, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_GROUP_N = sizeof RH_HOOKS_BF_GROUP / sizeof *RH_HOOKS_BF_GROUP;
