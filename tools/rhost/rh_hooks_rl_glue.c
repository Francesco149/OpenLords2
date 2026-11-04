/* rh_hooks_rl_glue.c — a retail-layout port, partition 'glue' (src/rl_glue.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). Router-table actions (no E8 call site: the button routers 0x40e3ee / 0x40da1e / the menu bar
 * call them through a pointer and drop EAX) are entry-patched, HOOK_V.
 *
 * No row (ported, verified through their callers or not at all — docs/findings/rl-glue.md):
 *   rl_47f474  rhost's call-site scan (rhost.c redirect_calls: any E8 rel32 aimed at the VA) finds a FALSE site
 *              inside FUN_0047efee's `mov dword_4EE8E8, 0` (bytes E8 4E 00 00 00 at 0x47f421 → 0x47f474) and
 *              rewrites it: retail crashes at 0x47f41e. Its port runs under rl_4782c5's shadow (and bf_43bdcd's
 *              seam when that is relinked).
 *   rl_42f148  IDirectDraw::CreateSurface is a host side effect the shadow does not roll back (retail's run made
 *              surface 2, the port's 3: ret r00000002/p00000003); replace mode passed corpus --check. */
#include "rh_port.h"
#include "rl.h"
#include "rl_glue_globals.h"
#include "rh_rl_glue_alias.h"

__asm__(RH_RL_GLUE_ALIASES);

/* retail's clock imports (timeGetTime DAT_005cf5c4, GetTickCount DAT_005cf3d8: rhost's virtual clock), bound into
 * src/rl_glue.c at startup (rh_port_bind_once is rh_hooks_ai.c's; a constructor keeps the binding here) */
typedef DWORD (WINAPI *rl_clock_t)(void);
static uint32_t retail_time(void) { return ((rl_clock_t)G32(0x5cf5c4u))(); }
static uint32_t retail_ticks(void) { return ((rl_clock_t)G32(0x5cf3d8u))(); }
void rl_glue_bind_clock(uint32_t (*time_fn)(void), uint32_t (*ticks_fn)(void));
__attribute__((constructor)) static void rl_glue_bind(void) { rl_glue_bind_clock(retail_time, retail_ticks); }

#define RH_TABLE RH_HOOKS_RL_GLUE
extern hook_t RH_HOOKS_RL_GLUE[];
/* row indices (must follow the table's order) */
enum {
    X_rl_41423f, X_rl_42e26f, X_rl_4360f2, X_rl_43496c, X_rl_434988, X_rl_40dfcd, X_rl_451f8a, X_rl_472986,
    X_rl_433a70, X_rl_4330c6, X_rl_40e00a, X_rl_40e099, X_rl_43b4cb, X_rl_4393eb, X_rl_439079, X_rl_46c4a3,
    X_rl_4323fe, X_rl_432443, X_rl_43253a, X_rl_432893, X_rl_43292d, X_rl_4329a4, X_rl_437002, X_rl_438990, X_rl_438a91, X_rl_43933b, X_rl_439122, X_rl_40deca, X_rl_40dd92, X_rl_40c8d1, X_rl_40c928, X_rl_40e761, X_rl_434cbe, X_rl_434cee,
    X_rl_433e93, X_rl_433f49, X_rl_4344f2, X_rl_43470d, X_rl_43618b, X_rl_43631e, X_rl_436872, X_rl_4369bd, X_rl_436b22, X_rl_436b59, X_rl_43733a, X_rl_43744f, X_rl_4374c4, X_rl_4376bb, X_rl_438acc,
    X_rl_43b593, X_rl_43b681, X_rl_43b7c4, X_rl_43b848, X_rl_43b98a, X_rl_4782c5, X_rl_47f474, X_rl_472a31, X_rl_498458, X_rl_448d7e, X_rl_47703a,
    X_rl_440666, X_rl_49b8b0, X_rl_481b9a, X_rl_482111, X_rl_4a82b9, X_rl_43eda0, X_rl_40ae12, X_rl_42a8d1, X_rl_42a9ab, X_rl_42d179, X_rl_4b1310, X_rl_409429, X_rl_40f10f, X_rl_475b41, X_rl_475d73,
};

HOOK_V(rl_41423f, 0, rl_41423f())
HOOK_R(rl_42e26f, 0, rl_42e26f())
HOOK_V(rl_4360f2, 0, rl_4360f2())
HOOK_V(rl_43496c, 0, rl_43496c())
HOOK_V(rl_434988, 0, rl_434988())
HOOK_V(rl_40dfcd, 0, rl_40dfcd())
HOOK_V(rl_451f8a, 0, rl_451f8a())
HOOK_V(rl_472986, 0, rl_472986())
HOOK_R(rl_433a70, 1, rl_433a70(I(0)))
HOOK_V(rl_4330c6, 0, rl_4330c6())
HOOK_R(rl_40e00a, 2, rl_40e00a(I(0), I(1)))
HOOK_R(rl_40e099, 4, rl_40e099(I(0), I(1), I(2), I(3)))
HOOK_R(rl_43b4cb, 2, rl_43b4cb(I(0), I(1)))
HOOK_R(rl_4393eb, 0, rl_4393eb())
HOOK_R(rl_439079, 0, rl_439079())
HOOK_V(rl_46c4a3, 4, rl_46c4a3(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4323fe, 0, rl_4323fe())
HOOK_R(rl_432443, 0, rl_432443())
HOOK_R(rl_43253a, 0, rl_43253a())
HOOK_V(rl_432893, 1, rl_432893(I(0)))
HOOK_R(rl_43292d, 0, rl_43292d())
HOOK_R(rl_4329a4, 0, rl_4329a4())
HOOK_R(rl_437002, 0, rl_437002())
HOOK_R(rl_438990, 0, rl_438990())
HOOK_R(rl_438a91, 0, rl_438a91())
HOOK_V(rl_43933b, 2, rl_43933b(I(0), I(1)))
HOOK_R(rl_439122, 0, rl_439122())
HOOK_R(rl_40deca, 2, rl_40deca(I(0), I(1)))
HOOK_R(rl_40dd92, 2, rl_40dd92(I(0), I(1)))
HOOK_V(rl_40c8d1, 0, rl_40c8d1())
HOOK_V(rl_40c928, 0, rl_40c928())
HOOK_V(rl_40e761, 8, rl_40e761(I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7)))
HOOK_V(rl_434cbe, 0, rl_434cbe())
HOOK_V(rl_434cee, 0, rl_434cee())
HOOK_V(rl_433e93, 0, rl_433e93())
HOOK_V(rl_433f49, 0, rl_433f49())
HOOK_V(rl_4344f2, 0, rl_4344f2())
HOOK_V(rl_43470d, 0, rl_43470d())
HOOK_V(rl_43618b, 0, rl_43618b())
HOOK_V(rl_43631e, 0, rl_43631e())
HOOK_V(rl_436872, 0, rl_436872())
HOOK_R(rl_4369bd, 0, rl_4369bd())
HOOK_V(rl_436b22, 0, rl_436b22())
HOOK_V(rl_436b59, 0, rl_436b59())
HOOK_V(rl_43733a, 0, rl_43733a())
HOOK_V(rl_43744f, 0, rl_43744f())
HOOK_V(rl_4374c4, 0, rl_4374c4())
HOOK_V(rl_4376bb, 0, rl_4376bb())
HOOK_V(rl_438acc, 0, rl_438acc())
HOOK_V(rl_43b593, 0, rl_43b593())
HOOK_V(rl_43b681, 0, rl_43b681())
HOOK_V(rl_43b7c4, 3, rl_43b7c4(I(0), I(1), I(2)))
HOOK_V(rl_43b848, 0, rl_43b848())
HOOK_V(rl_43b98a, 1, rl_43b98a(I(0)))
HOOK_V(rl_4782c5, 0, rl_4782c5())
HOOK_V(rl_47f474, 0, rl_47f474())   /* the casualty write-back (its false E8 site at 0x47f421: rhost.c redirect_calls now skips it) */
HOOK_V(rl_472a31, 0, rl_472a31())
HOOK_V(rl_498458, 0, rl_498458())
HOOK_V(rl_448d7e, 1, rl_448d7e(I(0)))
HOOK_V(rl_47703a, 0, rl_47703a())
HOOK_R(rl_440666, 0, rl_440666())
HOOK_V(rl_49b8b0, 0, rl_49b8b0())
HOOK_V(rl_481b9a, 0, rl_481b9a())
HOOK_V(rl_482111, 0, rl_482111())
HOOK_V(rl_4a82b9, 0, rl_4a82b9())
HOOK_V(rl_43eda0, 2, rl_43eda0(I(0), I(1)))
HOOK_V(rl_40ae12, 3, rl_40ae12(I(0), I(1), I(2)))
HOOK_V(rl_42a8d1, 0, rl_42a8d1())
HOOK_V(rl_42a9ab, 0, rl_42a9ab())
HOOK_V(rl_42d179, 0, rl_42d179())
HOOK_V(rl_4b1310, 0, rl_4b1310())
HOOK_V(rl_409429, 4, rl_409429(I(0), I(1), I(2), I(3)))
HOOK_V(rl_40f10f, 0, rl_40f10f())
HOOK_V(rl_475b41, 2, rl_475b41(I(0), I(1)))
HOOK_V(rl_475d73, 1, rl_475d73(I(0)))

hook_t RH_HOOKS_RL_GLUE[] = {
    { "rl_41423f", 0x41423fu, (void *)w_rl_41423f, 0, 0, 0, 0, "" },
    { "rl_42e26f", 0x42e26fu, (void *)w_rl_42e26f, 0, 0, 0, 0, "" },
    { "rl_4360f2", 0x4360f2u, (void *)w_rl_4360f2, 0, 0, 0, 0, "" },
    { "rl_43496c", 0x43496cu, (void *)w_rl_43496c, 0, 0, 0, 0, "" },
    { "rl_434988", 0x434988u, (void *)w_rl_434988, 0, 0, 0, 0, "" },
    { "rl_40dfcd", 0x40dfcdu, (void *)w_rl_40dfcd, 0, 0, 0, 0, "" },
    { "rl_451f8a", 0x451f8au, (void *)w_rl_451f8a, 0, 0, 0, 0, "" },
    { "rl_472986", 0x472986u, (void *)w_rl_472986, 0, 0, 0, 0, "" },
    { "rl_433a70", 0x433a70u, (void *)w_rl_433a70, 0, 0, 0, 0, "" },
    { "rl_4330c6", 0x4330c6u, (void *)w_rl_4330c6, 0, 0, 0, 0, "" },
    { "rl_40e00a", 0x40e00au, (void *)w_rl_40e00a, 0, 0, 0, 0, "" },
    { "rl_40e099", 0x40e099u, (void *)w_rl_40e099, 0, 0, 0, 0, "" },
    { "rl_43b4cb", 0x43b4cbu, (void *)w_rl_43b4cb, 0, 0, 0, 0, "" },
    { "rl_4393eb", 0x4393ebu, (void *)w_rl_4393eb, 0, 0, 0, 0, "" },
    { "rl_439079", 0x439079u, (void *)w_rl_439079, 0, 0, 0, 0, "" },
    { "rl_46c4a3", 0x46c4a3u, (void *)w_rl_46c4a3, 0, 0, 0, 0, "" },
    { "rl_4323fe", 0x4323feu, (void *)w_rl_4323fe, 0, 0, 0, 0, "" },
    { "rl_432443", 0x432443u, (void *)w_rl_432443, 0, 0, 0, 0, "" },
    { "rl_43253a", 0x43253au, (void *)w_rl_43253a, 0, 0, 0, 0, "" },
    { "rl_432893", 0x432893u, (void *)w_rl_432893, 0, 0, 0, 0, "" },
    { "rl_43292d", 0x43292du, (void *)w_rl_43292d, 0, 0, 0, 0, "" },
    { "rl_4329a4", 0x4329a4u, (void *)w_rl_4329a4, 0, 0, 0, 0, "" },
    { "rl_437002", 0x437002u, (void *)w_rl_437002, 0, 0, 0, 0, "" },
    { "rl_438990", 0x438990u, (void *)w_rl_438990, 0, 0, 0, 0, "" },
    { "rl_438a91", 0x438a91u, (void *)w_rl_438a91, 0, 0, 0, 0, "" },
    { "rl_43933b", 0x43933bu, (void *)w_rl_43933b, 0, 0, 0, 0, "" },
    { "rl_439122", 0x439122u, (void *)w_rl_439122, 0, 0, 0, 0, "" },
    { "rl_40deca", 0x40decau, (void *)w_rl_40deca, 0, 0, 0, 0, "" },
    { "rl_40dd92", 0x40dd92u, (void *)w_rl_40dd92, 0, 0, 0, 0, "" },
    { "rl_40c8d1", 0x40c8d1u, (void *)w_rl_40c8d1, 0, 0, 0, 0, "" },
    { "rl_40c928", 0x40c928u, (void *)w_rl_40c928, 0, 0, 0, 0, "" },
    { "rl_40e761", 0x40e761u, (void *)w_rl_40e761, 0, 0, 0, 0, "" },
    { "rl_434cbe", 0x434cbeu, (void *)w_rl_434cbe, 0, 0, 0, 0, "" },
    { "rl_434cee", 0x434ceeu, (void *)w_rl_434cee, 0, 0, 0, 0, "" },
    { "rl_433e93", 0x433e93u, (void *)w_rl_433e93, 0, 0, 0, 0, "" },
    { "rl_433f49", 0x433f49u, (void *)w_rl_433f49, 0, 0, 0, 0, "" },
    { "rl_4344f2", 0x4344f2u, (void *)w_rl_4344f2, 0, 0, 0, 0, "" },
    { "rl_43470d", 0x43470du, (void *)w_rl_43470d, 0, 0, 0, 0, "" },
    { "rl_43618b", 0x43618bu, (void *)w_rl_43618b, 0, 0, 0, 0, "" },
    { "rl_43631e", 0x43631eu, (void *)w_rl_43631e, 0, 0, 0, 0, "" },
    { "rl_436872", 0x436872u, (void *)w_rl_436872, 0, 0, 0, 0, "" },
    { "rl_4369bd", 0x4369bdu, (void *)w_rl_4369bd, 0, 0, 0, 0, "" },
    { "rl_436b22", 0x436b22u, (void *)w_rl_436b22, 0, 0, 0, 0, "" },
    { "rl_436b59", 0x436b59u, (void *)w_rl_436b59, 0, 0, 0, 0, "" },
    { "rl_43733a", 0x43733au, (void *)w_rl_43733a, 0, 0, 0, 0, "" },
    { "rl_43744f", 0x43744fu, (void *)w_rl_43744f, 0, 0, 0, 0, "" },
    { "rl_4374c4", 0x4374c4u, (void *)w_rl_4374c4, 0, 0, 0, 0, "" },
    { "rl_4376bb", 0x4376bbu, (void *)w_rl_4376bb, 0, 0, 0, 0, "" },
    { "rl_438acc", 0x438accu, (void *)w_rl_438acc, 0, 0, 0, 0, "" },
    { "rl_43b593", 0x43b593u, (void *)w_rl_43b593, 0, 0, 0, 0, "" },
    { "rl_43b681", 0x43b681u, (void *)w_rl_43b681, 0, 0, 0, 0, "" },
    { "rl_43b7c4", 0x43b7c4u, (void *)w_rl_43b7c4, 0, 0, 0, 0, "" },
    { "rl_43b848", 0x43b848u, (void *)w_rl_43b848, 0, 0, 0, 0, "" },
    { "rl_43b98a", 0x43b98au, (void *)w_rl_43b98a, 0, 0, 0, 0, "" },
    { "rl_4782c5", 0x4782c5u, (void *)w_rl_4782c5, 0, 0, 0, 0, "" },
    { "rl_47f474", 0x47f474u, (void *)w_rl_47f474, 0, 0, 0, 0, "" },
    { "rl_472a31", 0x472a31u, (void *)w_rl_472a31, 0, 0, 0, 0, "" },
    { "rl_498458", 0x498458u, (void *)w_rl_498458, 0, 0, 0, 0, "" },
    { "rl_448d7e", 0x448d7eu, (void *)w_rl_448d7e, 0, 0, 0, 0, "" },
    { "rl_47703a", 0x47703au, (void *)w_rl_47703a, 0, 0, 0, 0, "" },
    { "rl_440666", 0x440666u, (void *)w_rl_440666, 0, 0, 0, 0, "" },
    { "rl_49b8b0", 0x49b8b0u, (void *)w_rl_49b8b0, 0, 0, 0, 0, "" },
    { "rl_481b9a", 0x481b9au, (void *)w_rl_481b9a, 0, 0, 0, 0, "" },
    { "rl_482111", 0x482111u, (void *)w_rl_482111, 0, 0, 0, 0, "" },
    { "rl_4a82b9", 0x4a82b9u, (void *)w_rl_4a82b9, 0, 0, 0, 0, "" },
    { "rl_43eda0", 0x43eda0u, (void *)w_rl_43eda0, 0, 0, 0, 0, "" },
    { "rl_40ae12", 0x40ae12u, (void *)w_rl_40ae12, 0, 0, 0, 0, "" },
    { "rl_42a8d1", 0x42a8d1u, (void *)w_rl_42a8d1, 0, 0, 0, 0, "" },
    { "rl_42a9ab", 0x42a9abu, (void *)w_rl_42a9ab, 0, 0, 0, 0, "" },
    { "rl_42d179", 0x42d179u, (void *)w_rl_42d179, 0, 0, 0, 0, "" },
    { "rl_4b1310", 0x4b1310u, (void *)w_rl_4b1310, 0, 0, 0, 0, "" },
    { "rl_409429", 0x409429u, (void *)w_rl_409429, 0, 0, 0, 0, "" },
    { "rl_40f10f", 0x40f10fu, (void *)w_rl_40f10f, 0, 0, 0, 0, "" },
    { "rl_475b41", 0x475b41u, (void *)w_rl_475b41, 0, 0, 0, 0, "" },
    { "rl_475d73", 0x475d73u, (void *)w_rl_475d73, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_GLUE_N = sizeof RH_HOOKS_RL_GLUE / sizeof *RH_HOOKS_RL_GLUE;
