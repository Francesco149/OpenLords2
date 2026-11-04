/* rh_hooks_bf_input.c — the battlefield port, partition 'input' (src/engine_bf_input.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "engine_bf.h"
#include "engine_bf_input_globals.h"
#include "rh_bf_input_alias.h"

__asm__(RH_BF_INPUT_ALIASES);

#define RH_TABLE RH_HOOKS_BF_INPUT
extern hook_t RH_HOOKS_BF_INPUT[];
/* row indices (must follow the table's order) */
enum {
    X_bf_4238b8, X_bf_4239d5, X_bf_43b9a1, X_bf_43ba29, X_bf_43bb68, X_bf_43bbd7, X_bf_43bd02, X_bf_43bd67, X_bf_43bdcd, X_bf_43bf07, X_bf_43c247, X_bf_43c2a9, X_bf_43c4c6, X_bf_43c55c, X_bf_43c57d, X_bf_43c634, X_bf_4784ca, X_bf_478987, X_bf_478f0b, X_bf_479a71, X_bf_479b58, X_bf_479cf7, X_bf_47a76d, X_bf_47f76d, X_bf_499cb1,
};

HOOK_V(bf_4238b8, 0, bf_4238b8())
HOOK_V(bf_4239d5, 0, bf_4239d5())
HOOK_V(bf_43b9a1, 0, bf_43b9a1())
HOOK_V(bf_43ba29, 0, bf_43ba29())
HOOK_V(bf_43bb68, 0, bf_43bb68())
HOOK_V(bf_43bbd7, 2, bf_43bbd7(I(0), I(1)))
HOOK_V(bf_43bd02, 0, bf_43bd02())
HOOK_V(bf_43bd67, 0, bf_43bd67())
HOOK_V(bf_43bdcd, 0, bf_43bdcd())
HOOK_R(bf_43bf07, 0, bf_43bf07())
HOOK_V(bf_43c247, 8, bf_43c247(I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7)))
HOOK_R(bf_43c2a9, 0, bf_43c2a9())
HOOK_V(bf_43c4c6, 1, bf_43c4c6(I(0)))
HOOK_V(bf_43c55c, 1, bf_43c55c(I(0)))
HOOK_R(bf_43c57d, 0, bf_43c57d())
HOOK_V(bf_43c634, 2, bf_43c634(I(0), I(1)))
HOOK_V(bf_4784ca, 0, bf_4784ca())
HOOK_V(bf_478987, 1, bf_478987(I(0)))
HOOK_V(bf_478f0b, 1, bf_478f0b(I(0)))
HOOK_V(bf_479a71, 1, bf_479a71(I(0)))
HOOK_V(bf_479b58, 7, bf_479b58(I(0), I(1), I(2), I(3), I(4), I(5), I(6)))
HOOK_R(bf_479cf7, 0, bf_479cf7())
HOOK_V(bf_47a76d, 1, bf_47a76d(I(0)))
HOOK_V(bf_47f76d, 0, bf_47f76d())
HOOK_V(bf_499cb1, 1, bf_499cb1(I(0)))

hook_t RH_HOOKS_BF_INPUT[] = {
    { "bf_4238b8", 0x4238b8u, (void *)w_bf_4238b8, 0, 0, 0, 0, "" },
    { "bf_4239d5", 0x4239d5u, (void *)w_bf_4239d5, 0, 0, 0, 0, "" },
    { "bf_43b9a1", 0x43b9a1u, (void *)w_bf_43b9a1, 0, 0, 0, 0, "" },
    { "bf_43ba29", 0x43ba29u, (void *)w_bf_43ba29, 0, 0, 0, 0, "" },
    { "bf_43bb68", 0x43bb68u, (void *)w_bf_43bb68, 0, 0, 0, 0, "" },
    { "bf_43bbd7", 0x43bbd7u, (void *)w_bf_43bbd7, 0, 0, 0, 0, "" },
    { "bf_43bd02", 0x43bd02u, (void *)w_bf_43bd02, 0, 0, 0, 0, "" },
    { "bf_43bd67", 0x43bd67u, (void *)w_bf_43bd67, 0, 0, 0, 0, "" },
    { "bf_43bdcd", 0x43bdcdu, (void *)w_bf_43bdcd, 0, 0, 0, 0, "" },
    { "bf_43bf07", 0x43bf07u, (void *)w_bf_43bf07, 0, 0, 0, 0, "" },
    { "bf_43c247", 0x43c247u, (void *)w_bf_43c247, 0, 0, 0, 0, "" },
    { "bf_43c2a9", 0x43c2a9u, (void *)w_bf_43c2a9, 0, 0, 0, 0, "" },
    { "bf_43c4c6", 0x43c4c6u, (void *)w_bf_43c4c6, 0, 0, 0, 0, "" },
    { "bf_43c55c", 0x43c55cu, (void *)w_bf_43c55c, 0, 0, 0, 0, "" },
    { "bf_43c57d", 0x43c57du, (void *)w_bf_43c57d, 0, 0, 0, 0, "" },
    { "bf_43c634", 0x43c634u, (void *)w_bf_43c634, 0, 0, 0, 0, "" },
    { "bf_4784ca", 0x4784cau, (void *)w_bf_4784ca, 0, 0, 0, 0, "" },
    { "bf_478987", 0x478987u, (void *)w_bf_478987, 0, 0, 0, 0, "" },
    { "bf_478f0b", 0x478f0bu, (void *)w_bf_478f0b, 0, 0, 0, 0, "" },
    { "bf_479a71", 0x479a71u, (void *)w_bf_479a71, 0, 0, 0, 0, "" },
    { "bf_479b58", 0x479b58u, (void *)w_bf_479b58, 0, 0, 0, 0, "" },
    { "bf_479cf7", 0x479cf7u, (void *)w_bf_479cf7, 0, 0, 0, 0, "" },
    { "bf_47a76d", 0x47a76du, (void *)w_bf_47a76d, 0, 0, 0, 0, "" },
    { "bf_47f76d", 0x47f76du, (void *)w_bf_47f76d, 0, 0, 0, 0, "" },
    { "bf_499cb1", 0x499cb1u, (void *)w_bf_499cb1, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_INPUT_N = sizeof RH_HOOKS_BF_INPUT / sizeof *RH_HOOKS_BF_INPUT;
