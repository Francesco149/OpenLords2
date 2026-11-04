/* rh_hooks_bf_move.c — the battlefield port, partition 'move' (src/engine_bf_move.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). Findings: docs/findings/bf-move.md. */
#include "rh_port.h"
#include "engine_bf.h"

/* the partition's own globals (src/engine_bf_move_globals.c, not linked): absolute symbols at their retail VAs */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(A(g_bf_4f9680,0x4f9680));

#define RH_TABLE RH_HOOKS_BF_MOVE
extern hook_t RH_HOOKS_BF_MOVE[];
/* row indices (must follow the table's order) */
enum {
    X_bf_494df9, X_bf_494d08, X_bf_4b3dcd, X_bf_491a34, X_bf_4928a3, X_bf_496155, X_bf_4972f9, X_bf_472227,
    X_bf_48f079, X_bf_491f17, X_bf_49207e, X_bf_490a44, X_bf_490616, X_bf_4904ec, X_bf_491b1f, X_bf_49005f,
    X_bf_490fa2, X_bf_491147, X_bf_490c59, X_bf_4912ec, X_bf_491492,
    X_bf_471f45, X_bf_4710f2, X_bf_471718, X_bf_47095e, X_bf_48f1dd,
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
HOOK_R(bf_490fa2, 1, bf_490fa2(I(0)))
HOOK_R(bf_491147, 1, bf_491147(I(0)))
HOOK_R(bf_490c59, 1, bf_490c59(I(0)))
HOOK_R(bf_4912ec, 0, bf_4912ec())
HOOK_R(bf_491492, 1, bf_491492(I(0)))
HOOK_V(bf_471f45, 0, bf_471f45())
HOOK_R(bf_4710f2, 4, bf_4710f2(I(0), I(1), I(2), I(3)))
HOOK_R(bf_471718, 4, bf_471718(I(0), I(1), I(2), I(3)))
HOOK_V(bf_47095e, 5, bf_47095e(I(0), I(1), I(2), I(3), I(4)))
HOOK_R(bf_48f1dd, 1, bf_48f1dd(I(0)))

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
    { "bf_490fa2", 0x490fa2u, (void *)w_bf_490fa2, 0, 0, 0, 0, "" },
    { "bf_491147", 0x491147u, (void *)w_bf_491147, 0, 0, 0, 0, "" },
    { "bf_490c59", 0x490c59u, (void *)w_bf_490c59, 0, 0, 0, 0, "" },
    { "bf_4912ec", 0x4912ecu, (void *)w_bf_4912ec, 0, 0, 0, 0, "" },
    { "bf_491492", 0x491492u, (void *)w_bf_491492, 0, 0, 0, 0, "" },
    { "bf_471f45", 0x471f45u, (void *)w_bf_471f45, 0, 0, 0, 0, "" },
    { "bf_4710f2", 0x4710f2u, (void *)w_bf_4710f2, 0, 0, 0, 0, "" },
    { "bf_471718", 0x471718u, (void *)w_bf_471718, 0, 0, 0, 0, "" },
    { "bf_47095e", 0x47095eu, (void *)w_bf_47095e, 0, 0, 0, 0, "" },
    { "bf_48f1dd", 0x48f1ddu, (void *)w_bf_48f1dd, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_MOVE_N = sizeof RH_HOOKS_BF_MOVE / sizeof *RH_HOOKS_BF_MOVE;
