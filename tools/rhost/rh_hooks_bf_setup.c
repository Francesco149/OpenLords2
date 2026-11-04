/* rh_hooks_bf_setup.c — the battlefield port, partition 'setup' (src/engine_bf_setup.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). Findings: docs/findings/bf-setup.md. */
#include "rh_port.h"
#include "engine_bf.h"

/* the partition's own globals (src/engine_bf_setup_globals.c, not linked): absolute symbols at their retail VAs */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(A(g_bf_591564,0x591564));
#undef A

#define RH_TABLE RH_HOOKS_BF_SETUP
extern hook_t RH_HOOKS_BF_SETUP[];
/* row indices (must follow the table's order) */
enum {
    X_bf_480599, X_bf_46e70e, X_bf_48169e, X_bf_4816f9, X_bf_481878, X_bf_481906, X_bf_46e4c8, X_bf_479473,
    X_bf_46f0f0, X_bf_46f13e, X_bf_46f18d, X_bf_47f42d, X_bf_480f2e, X_bf_46c40c, X_bf_46c7fc,
    X_bf_480662, X_bf_47fea7, X_bf_4801f8, X_bf_47efee, X_bf_480f8b, X_bf_47ed9b,
    X_bf_47d79c, X_bf_47d6fe, X_bf_47d816, X_bf_47aaa3,
    X_bf_48eefd, X_bf_47dc9c, X_bf_47dcce, X_bf_47e1dc, X_bf_497024, X_bf_497247, X_bf_49719e,
    X_bf_47e263, X_bf_47e387, X_bf_47e52d, X_bf_47e668, X_bf_47e7b2, X_bf_47e926, X_bf_47e230,
    X_bf_47d474, X_bf_47d4b8, X_bf_47d5ec, X_bf_47d6b0,
    X_bf_47cec1, X_bf_4787a4, X_bf_47c4ba, X_bf_42d806, X_bf_47b8b2, X_bf_4778a0, X_bf_477c89,
};

/* the deployment / spawn chain */
HOOK_V(bf_480599, 1, bf_480599(I(0)))
HOOK_R(bf_46e70e, 2, bf_46e70e(I(0), I(1)))
HOOK_V(bf_48169e, 2, bf_48169e(I(0), I(1)))
HOOK_V(bf_4816f9, 2, bf_4816f9(I(0), I(1)))
HOOK_R(bf_481878, 3, bf_481878(I(0), I(1), I(2)))
HOOK_R(bf_481906, 4, bf_481906(I(0), I(1), I(2), I(3)))
HOOK_R(bf_46e4c8, 7, bf_46e4c8(I(0), I(1), I(2), I(3), I(4), I(5), I(6)))
HOOK_V(bf_479473, 2, bf_479473(I(0), I(1)))
HOOK_V(bf_46f0f0, 0, bf_46f0f0())
HOOK_V(bf_46f13e, 0, bf_46f13e())
HOOK_V(bf_46f18d, 0, bf_46f18d())
HOOK_V(bf_47f42d, 0, bf_47f42d())
HOOK_V(bf_480f2e, 0, bf_480f2e())
/* the terrain build helpers */
HOOK_V(bf_46c40c, 2, bf_46c40c(I(0), I(1)))
HOOK_V(bf_46c7fc, 1, bf_46c7fc(I(0)))
/* the deployment drivers, the sprite binding, the per-frame cursor probe */
HOOK_V(bf_480662, 7, bf_480662(I(0), I(1), I(2), I(3), I(4), I(5), I(6)))
HOOK_V(bf_47fea7, 3, bf_47fea7(I(0), I(1), I(2)))
HOOK_V(bf_4801f8, 3, bf_4801f8(I(0), I(1), I(2)))
HOOK_V(bf_47efee, 0, bf_47efee())
HOOK_V(bf_480f8b, 0, bf_480f8b())
HOOK_V(bf_47ed9b, 0, bf_47ed9b())
/* the terrain build */
HOOK_V(bf_47d79c, 3, bf_47d79c(I(0), I(1), I(2)))
HOOK_V(bf_47d6fe, 4, bf_47d6fe(I(0), I(1), I(2), I(3)))
HOOK_V(bf_47d816, 2, bf_47d816(I(0), I(1)))
HOOK_V(bf_47aaa3, 1, bf_47aaa3(I(0)))
/* the siege map, the custom-battle map, entering the battlefield */
HOOK_V(bf_48eefd, 0, bf_48eefd())
HOOK_V(bf_47dc9c, 0, bf_47dc9c())
HOOK_V(bf_47dcce, 0, bf_47dcce())
HOOK_V(bf_47e1dc, 0, bf_47e1dc())
HOOK_R(bf_497024, 4, bf_497024(I(0), I(1), I(2), I(3)))
HOOK_R(bf_497247, 3, bf_497247(I(0), I(1), I(2)))
HOOK_R(bf_49719e, 3, bf_49719e(I(0), I(1), I(2)))
HOOK_V(bf_47e263, 0, bf_47e263())
HOOK_V(bf_47e387, 0, bf_47e387())
HOOK_V(bf_47e52d, 0, bf_47e52d())
HOOK_V(bf_47e668, 0, bf_47e668())
HOOK_V(bf_47e7b2, 0, bf_47e7b2())
HOOK_V(bf_47e926, 0, bf_47e926())
HOOK_V(bf_47e230, 0, bf_47e230())
HOOK_R(bf_47d474, 1, bf_47d474(I(0)))
HOOK_R(bf_47d4b8, 1, bf_47d4b8(I(0)))
HOOK_R(bf_47d5ec, 1, bf_47d5ec(I(0)))
HOOK_R(bf_47d6b0, 1, bf_47d6b0(I(0)))
HOOK_V(bf_47cec1, 1, bf_47cec1(I(0)))
HOOK_V(bf_4787a4, 0, bf_4787a4())
HOOK_V(bf_47c4ba, 1, bf_47c4ba(I(0)))
HOOK_V(bf_42d806, 2, bf_42d806(I(0), I(1)))
HOOK_V(bf_47b8b2, 1, bf_47b8b2(I(0)))
HOOK_V(bf_4778a0, 0, bf_4778a0())
HOOK_V(bf_477c89, 0, bf_477c89())

hook_t RH_HOOKS_BF_SETUP[] = {
    { "bf_480599", 0x480599u, (void *)w_bf_480599, 0, 0, 0, 0, "" },
    { "bf_46e70e", 0x46e70eu, (void *)w_bf_46e70e, 0, 0, 0, 0, "" },
    { "bf_48169e", 0x48169eu, (void *)w_bf_48169e, 0, 0, 0, 0, "" },
    { "bf_4816f9", 0x4816f9u, (void *)w_bf_4816f9, 0, 0, 0, 0, "" },
    { "bf_481878", 0x481878u, (void *)w_bf_481878, 0, 0, 0, 0, "" },
    { "bf_481906", 0x481906u, (void *)w_bf_481906, 0, 0, 0, 0, "" },
    { "bf_46e4c8", 0x46e4c8u, (void *)w_bf_46e4c8, 0, 0, 0, 0, "" },
    { "bf_479473", 0x479473u, (void *)w_bf_479473, 0, 0, 0, 0, "" },
    { "bf_46f0f0", 0x46f0f0u, (void *)w_bf_46f0f0, 0, 0, 0, 0, "" },
    { "bf_46f13e", 0x46f13eu, (void *)w_bf_46f13e, 0, 0, 0, 0, "" },
    { "bf_46f18d", 0x46f18du, (void *)w_bf_46f18d, 0, 0, 0, 0, "" },
    { "bf_47f42d", 0x47f42du, (void *)w_bf_47f42d, 0, 0, 0, 0, "" },
    { "bf_480f2e", 0x480f2eu, (void *)w_bf_480f2e, 0, 0, 0, 0, "" },
    { "bf_46c40c", 0x46c40cu, (void *)w_bf_46c40c, 0, 0, 0, 0, "" },
    { "bf_46c7fc", 0x46c7fcu, (void *)w_bf_46c7fc, 0, 0, 0, 0, "" },
    { "bf_480662", 0x480662u, (void *)w_bf_480662, 0, 0, 0, 0, "" },
    { "bf_47fea7", 0x47fea7u, (void *)w_bf_47fea7, 0, 0, 0, 0, "" },
    { "bf_4801f8", 0x4801f8u, (void *)w_bf_4801f8, 0, 0, 0, 0, "" },
    { "bf_47efee", 0x47efeeu, (void *)w_bf_47efee, 0, 0, 0, 0, "" },
    { "bf_480f8b", 0x480f8bu, (void *)w_bf_480f8b, 0, 0, 0, 0, "" },
    { "bf_47ed9b", 0x47ed9bu, (void *)w_bf_47ed9b, 0, 0, 0, 0, "" },
    { "bf_47d79c", 0x47d79cu, (void *)w_bf_47d79c, 0, 0, 0, 0, "" },
    { "bf_47d6fe", 0x47d6feu, (void *)w_bf_47d6fe, 0, 0, 0, 0, "" },
    { "bf_47d816", 0x47d816u, (void *)w_bf_47d816, 0, 0, 0, 0, "" },
    { "bf_47aaa3", 0x47aaa3u, (void *)w_bf_47aaa3, 0, 0, 0, 0, "" },
    { "bf_48eefd", 0x48eefdu, (void *)w_bf_48eefd, 0, 0, 0, 0, "" },
    { "bf_47dc9c", 0x47dc9cu, (void *)w_bf_47dc9c, 0, 0, 0, 0, "" },
    { "bf_47dcce", 0x47dcceu, (void *)w_bf_47dcce, 0, 0, 0, 0, "" },
    { "bf_47e1dc", 0x47e1dcu, (void *)w_bf_47e1dc, 0, 0, 0, 0, "" },
    { "bf_497024", 0x497024u, (void *)w_bf_497024, 0, 0, 0, 0, "" },
    { "bf_497247", 0x497247u, (void *)w_bf_497247, 0, 0, 0, 0, "" },
    { "bf_49719e", 0x49719eu, (void *)w_bf_49719e, 0, 0, 0, 0, "" },
    { "bf_47e263", 0x47e263u, (void *)w_bf_47e263, 0, 0, 0, 0, "" },
    { "bf_47e387", 0x47e387u, (void *)w_bf_47e387, 0, 0, 0, 0, "" },
    { "bf_47e52d", 0x47e52du, (void *)w_bf_47e52d, 0, 0, 0, 0, "" },
    { "bf_47e668", 0x47e668u, (void *)w_bf_47e668, 0, 0, 0, 0, "" },
    { "bf_47e7b2", 0x47e7b2u, (void *)w_bf_47e7b2, 0, 0, 0, 0, "" },
    { "bf_47e926", 0x47e926u, (void *)w_bf_47e926, 0, 0, 0, 0, "" },
    { "bf_47e230", 0x47e230u, (void *)w_bf_47e230, 0, 0, 0, 0, "" },
    { "bf_47d474", 0x47d474u, (void *)w_bf_47d474, 0, 0, 0, 0, "" },
    { "bf_47d4b8", 0x47d4b8u, (void *)w_bf_47d4b8, 0, 0, 0, 0, "" },
    { "bf_47d5ec", 0x47d5ecu, (void *)w_bf_47d5ec, 0, 0, 0, 0, "" },
    { "bf_47d6b0", 0x47d6b0u, (void *)w_bf_47d6b0, 0, 0, 0, 0, "" },
    { "bf_47cec1", 0x47cec1u, (void *)w_bf_47cec1, 0, 0, 0, 0, "" },
    { "bf_4787a4", 0x4787a4u, (void *)w_bf_4787a4, 0, 0, 0, 0, "" },
    { "bf_47c4ba", 0x47c4bau, (void *)w_bf_47c4ba, 0, 0, 0, 0, "" },
    { "bf_42d806", 0x42d806u, (void *)w_bf_42d806, 0, 0, 0, 0, "" },
    { "bf_47b8b2", 0x47b8b2u, (void *)w_bf_47b8b2, 0, 0, 0, 0, "" },
    { "bf_4778a0", 0x4778a0u, (void *)w_bf_4778a0, 0, 0, 0, 0, "" },
    { "bf_477c89", 0x477c89u, (void *)w_bf_477c89, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_SETUP_N = sizeof RH_HOOKS_BF_SETUP / sizeof *RH_HOOKS_BF_SETUP;
