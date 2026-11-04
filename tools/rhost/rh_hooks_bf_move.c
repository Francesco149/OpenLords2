/* rh_hooks_bf_move.c — the battlefield port, partition 'move' (src/engine_bf_move.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). Findings: docs/findings/bf-move.md. */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_MOVE
extern hook_t RH_HOOKS_BF_MOVE[];
/* row indices (must follow the table's order) */
enum {
    X_bf_494df9, X_bf_494d08, X_bf_4b3dcd, X_bf_491a34, X_bf_4928a3, X_bf_496155, X_bf_4972f9, X_bf_472227,
    X_bf_48f079, X_bf_491f17, X_bf_49207e, X_bf_490a44, X_bf_490616, X_bf_4904ec, X_bf_491b1f, X_bf_49005f,
};

HOOK_V(bf_494df9, 1, bf_494df9(I(0)))
HOOK_V(bf_494d08, 1, bf_494d08(I(0)))
HOOK_V(bf_4b3dcd, 3, bf_4b3dcd(I(0), I(1), I(2)))
HOOK_R(bf_491a34, 1, bf_491a34(I(0)))
HOOK_V(bf_4928a3, 1, bf_4928a3(I(0)))
HOOK_R(bf_496155, 3, bf_496155(I(0), I(1), I(2)))
HOOK_R(bf_4972f9, 3, bf_4972f9(I(0), I(1), I(2)))
HOOK_R(bf_472227, 1, bf_472227(I(0)))
HOOK_V(bf_48f079, 1, bf_48f079(I(0)))
HOOK_V(bf_491f17, 2, bf_491f17(I(0), I(1)))
HOOK_V(bf_49207e, 2, bf_49207e(I(0), I(1)))
HOOK_R(bf_490a44, 1, bf_490a44(I(0)))
HOOK_R(bf_490616, 1, bf_490616(I(0)))
HOOK_R(bf_4904ec, 0, bf_4904ec())
HOOK_V(bf_491b1f, 1, bf_491b1f(I(0)))
HOOK_R(bf_49005f, 0, bf_49005f())

hook_t RH_HOOKS_BF_MOVE[] = {
    { "bf_494df9", 0x494df9u, (void *)w_bf_494df9, 0, 0, 0, 0, "" },
    { "bf_494d08", 0x494d08u, (void *)w_bf_494d08, 0, 0, 0, 0, "" },
    { "bf_4b3dcd", 0x4b3dcdu, (void *)w_bf_4b3dcd, 0, 0, 0, 0, "" },
    { "bf_491a34", 0x491a34u, (void *)w_bf_491a34, 0, 0, 0, 0, "" },
    { "bf_4928a3", 0x4928a3u, (void *)w_bf_4928a3, 0, 0, 0, 0, "" },
    { "bf_496155", 0x496155u, (void *)w_bf_496155, 0, 0, 0, 0, "" },
    { "bf_4972f9", 0x4972f9u, (void *)w_bf_4972f9, 0, 0, 0, 0, "" },
    { "bf_472227", 0x472227u, (void *)w_bf_472227, 0, 0, 0, 0, "" },
    { "bf_48f079", 0x48f079u, (void *)w_bf_48f079, 0, 0, 0, 0, "" },
    { "bf_491f17", 0x491f17u, (void *)w_bf_491f17, 0, 0, 0, 0, "" },
    { "bf_49207e", 0x49207eu, (void *)w_bf_49207e, 0, 0, 0, 0, "" },
    { "bf_490a44", 0x490a44u, (void *)w_bf_490a44, 0, 0, 0, 0, "" },
    { "bf_490616", 0x490616u, (void *)w_bf_490616, 0, 0, 0, 0, "" },
    { "bf_4904ec", 0x4904ecu, (void *)w_bf_4904ec, 0, 0, 0, 0, "" },
    { "bf_491b1f", 0x491b1fu, (void *)w_bf_491b1f, 0, 0, 0, 0, "" },
    { "bf_49005f", 0x49005fu, (void *)w_bf_49005f, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_MOVE_N = sizeof RH_HOOKS_BF_MOVE / sizeof *RH_HOOKS_BF_MOVE;
