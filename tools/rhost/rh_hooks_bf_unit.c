/* rh_hooks_bf_unit.c — the battlefield port, partition 'unit' (src/engine_bf_unit.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_UNIT
extern hook_t RH_HOOKS_BF_UNIT[];
/* row indices (must follow the table's order) */
enum { X_bf_4822ed, X_bf_46ebe4, X_bf_46eb3f };

HOOK_V(bf_4822ed, 0, bf_4822ed())
HOOK_R(bf_46ebe4, 1, bf_46ebe4(I(0)))
HOOK_R(bf_46eb3f, 1, bf_46eb3f(I(0)))

hook_t RH_HOOKS_BF_UNIT[] = {
    { "bf_4822ed", 0x4822edu, (void *)w_bf_4822ed, 0, 0, 0, 0, "" },
    { "bf_46ebe4", 0x46ebe4u, (void *)w_bf_46ebe4, 0, 0, 0, 0, "" },
    { "bf_46eb3f", 0x46eb3fu, (void *)w_bf_46eb3f, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_UNIT_N = sizeof RH_HOOKS_BF_UNIT / sizeof *RH_HOOKS_BF_UNIT;
