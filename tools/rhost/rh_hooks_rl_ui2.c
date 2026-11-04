/* rh_hooks_rl_ui2.c — a retail-layout port, partition 'ui2' (src/rl_ui2.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads, or one the port models exactly: compared) + the table row. Rows only for PORTED functions (a row on
 * a pending stub compares retail with itself). Router-table actions (no E8 call site) are entry-patched, HOOK_V.
 * Rows that draw carry RH_SH_MEDIA (the shadow compares every surface pixel). Findings: docs/findings/rl-ui2.md.
 *
 * BINDING.  The partition's globals (src/rl_ui2_globals.h, generated + a hand block; rl_ui2_globals.c is not linked)
 * are absolute symbols at their retail VAs (rh_rl_ui2_alias.h). */
#include "rh_port.h"
#include "rl.h"
#include "rl_ui2_globals.h"
#include "rh_rl_ui2_alias.h"

__asm__(RH_RL_UI2_ALIASES);

#define RH_TABLE RH_HOOKS_RL_UI2
extern hook_t RH_HOOKS_RL_UI2[];
/* row indices (must follow the table's order) */
enum {
    X_rl_43958a, X_rl_4399b0, X_rl_439b52, X_rl_439cc2, X_rl_439f6a, X_rl_439edb, X_rl_44ff4a, X_rl_450000,
    X_rl_450639, X_rl_438bec, X_rl_438b02, X_rl_439541,
    X_rl_41424a, X_rl_46d421, X_rl_434fd5, X_rl_43505a, X_rl_434fa1, X_rl_4350a1, X_rl_43ab76, X_rl_41543f, X_rl_4154ea, X_rl_434d33, X_rl_434d60, X_rl_434787, X_rl_4347c7, X_rl_43484a, X_rl_434888, X_rl_4348c6, X_rl_434904, X_rl_401c5b, X_rl_433dbd, X_rl_433deb, X_rl_4343f8, X_rl_43441c, X_rl_43600c, X_rl_43a997, X_rl_43a950, X_rl_408f1a, X_rl_408ea4, X_rl_413526, X_rl_414e06, X_rl_434174, X_rl_41ef42,
    X_rl_43d5b7, X_rl_43d649, X_rl_43d6a7, X_rl_43d6c3, X_rl_43d78b, X_rl_43d7a7, X_rl_43d882, X_rl_43d8b9, X_rl_43d929, X_rl_43d9cd, X_rl_43da9e, X_rl_43daf3, X_rl_43dbac, X_rl_43dc1d, X_rl_43dd83, X_rl_43ddf4, X_rl_498dcb, X_rl_498fb3, X_rl_43f24b, X_rl_43f352, X_rl_43f484, X_rl_4b7250, X_rl_4b7585, X_rl_4adcf6, X_rl_4af70c, X_rl_4b3994, X_rl_41cb3a, X_rl_4b14ff, X_rl_43c77a, X_rl_43c885, X_rl_43c910, X_rl_479276, X_rl_47935b,
};

HOOK_R(rl_43958a, 0, rl_43958a())
HOOK_R(rl_4399b0, 0, rl_4399b0())
HOOK_R(rl_439b52, 4, rl_439b52(I(0), I(1), I(2), I(3)))
HOOK_R(rl_439cc2, 3, rl_439cc2(I(0), I(1), I(2)))
HOOK_R(rl_439f6a, 3, rl_439f6a(I(0), I(1), I(2)))
HOOK_R(rl_439edb, 2, rl_439edb(I(0), I(1)))
HOOK_R(rl_44ff4a, 1, rl_44ff4a(I(0)))
HOOK_R(rl_450000, 1, rl_450000(I(0)))
HOOK_R(rl_450639, 4, rl_450639(I(0), I(1), I(2), I(3)))
HOOK_R(rl_438bec, 3, rl_438bec(I(0), I(1), I(2)))
HOOK_V(rl_438b02, 0, rl_438b02())
HOOK_R(rl_439541, 0, rl_439541())
HOOK_V(rl_41424a, 0, rl_41424a())
HOOK_V(rl_46d421, 0, rl_46d421())
HOOK_V(rl_434fd5, 0, rl_434fd5())
HOOK_V(rl_43505a, 0, rl_43505a())
HOOK_V(rl_434fa1, 0, rl_434fa1())
HOOK_V(rl_4350a1, 0, rl_4350a1())
HOOK_V(rl_43ab76, 0, rl_43ab76())
HOOK_V(rl_41543f, 0, rl_41543f())
HOOK_V(rl_4154ea, 0, rl_4154ea())
HOOK_V(rl_434d33, 0, rl_434d33())
HOOK_V(rl_434d60, 0, rl_434d60())
HOOK_V(rl_434787, 0, rl_434787())
HOOK_V(rl_4347c7, 0, rl_4347c7())
HOOK_V(rl_43484a, 0, rl_43484a())
HOOK_V(rl_434888, 0, rl_434888())
HOOK_V(rl_4348c6, 0, rl_4348c6())
HOOK_V(rl_434904, 0, rl_434904())
HOOK_V(rl_401c5b, 0, rl_401c5b())
HOOK_V(rl_433dbd, 0, rl_433dbd())
HOOK_V(rl_433deb, 0, rl_433deb())
HOOK_V(rl_4343f8, 0, rl_4343f8())
HOOK_V(rl_43441c, 0, rl_43441c())
HOOK_V(rl_43600c, 0, rl_43600c())
HOOK_V(rl_43a997, 2, rl_43a997(I(0), I(1)))
HOOK_V(rl_43a950, 0, rl_43a950())
HOOK_V(rl_408f1a, 1, rl_408f1a(I(0)))
HOOK_V(rl_408ea4, 1, rl_408ea4(I(0)))
HOOK_V(rl_413526, 0, rl_413526())
HOOK_V(rl_414e06, 1, rl_414e06(I(0)))
HOOK_R(rl_434174, 1, rl_434174(I(0)))
HOOK_V(rl_41ef42, 0, rl_41ef42())
HOOK_V(rl_43d5b7, 0, rl_43d5b7())
HOOK_V(rl_43d649, 0, rl_43d649())
HOOK_V(rl_43d6a7, 0, rl_43d6a7())
HOOK_V(rl_43d6c3, 0, rl_43d6c3())
HOOK_V(rl_43d78b, 0, rl_43d78b())
HOOK_V(rl_43d7a7, 0, rl_43d7a7())
HOOK_V(rl_43d882, 0, rl_43d882())
HOOK_V(rl_43d8b9, 0, rl_43d8b9())
HOOK_V(rl_43d929, 0, rl_43d929())
HOOK_V(rl_43d9cd, 0, rl_43d9cd())
HOOK_V(rl_43da9e, 0, rl_43da9e())
HOOK_V(rl_43daf3, 0, rl_43daf3())
HOOK_V(rl_43dbac, 0, rl_43dbac())
HOOK_V(rl_43dc1d, 0, rl_43dc1d())
HOOK_V(rl_43dd83, 0, rl_43dd83())
HOOK_V(rl_43ddf4, 0, rl_43ddf4())
HOOK_R(rl_498dcb, 0, rl_498dcb())
HOOK_R(rl_498fb3, 2, rl_498fb3(I(0), I(1)))
HOOK_V(rl_43f24b, 0, rl_43f24b())
HOOK_R(rl_43f352, 0, rl_43f352())
HOOK_V(rl_43f484, 0, rl_43f484())
HOOK_R(rl_4b7250, 0, rl_4b7250())
HOOK_R(rl_4b7585, 4, rl_4b7585(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4adcf6, 0, rl_4adcf6())
HOOK_R(rl_4af70c, 1, rl_4af70c(I(0)))
HOOK_V(rl_4b3994, 1, rl_4b3994(I(0)))
HOOK_V(rl_41cb3a, 0, rl_41cb3a())
HOOK_V(rl_4b14ff, 0, rl_4b14ff())
HOOK_V(rl_43c77a, 1, rl_43c77a(I(0)))
HOOK_V(rl_43c885, 1, rl_43c885(I(0)))
HOOK_V(rl_43c910, 1, rl_43c910(I(0)))
HOOK_V(rl_479276, 1, rl_479276(I(0)))
HOOK_V(rl_47935b, 1, rl_47935b(I(0)))

hook_t RH_HOOKS_RL_UI2[] = {
    { "rl_43958a", 0x43958au, (void *)w_rl_43958a, 0, 0, 0, 0, "" },
    { "rl_4399b0", 0x4399b0u, (void *)w_rl_4399b0, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_439b52", 0x439b52u, (void *)w_rl_439b52, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_439cc2", 0x439cc2u, (void *)w_rl_439cc2, 0, 0, 0, 0, "" },
    { "rl_439f6a", 0x439f6au, (void *)w_rl_439f6a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_439edb", 0x439edbu, (void *)w_rl_439edb, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_44ff4a", 0x44ff4au, (void *)w_rl_44ff4a, 0, 0, 0, 0, "" },
    { "rl_450000", 0x450000u, (void *)w_rl_450000, 0, 0, 0, 0, "" },
    { "rl_450639", 0x450639u, (void *)w_rl_450639, 0, 0, 0, 0, "" },
    { "rl_438bec", 0x438becu, (void *)w_rl_438bec, 0, 0, 0, 0, "" },
    { "rl_438b02", 0x438b02u, (void *)w_rl_438b02, 0, 0, 0, 0, "" },
    { "rl_439541", 0x439541u, (void *)w_rl_439541, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41424a", 0x41424au, (void *)w_rl_41424a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_46d421", 0x46d421u, (void *)w_rl_46d421, 0, 0, 0, 0, "" },
    { "rl_434fd5", 0x434fd5u, (void *)w_rl_434fd5, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43505a", 0x43505au, (void *)w_rl_43505a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_434fa1", 0x434fa1u, (void *)w_rl_434fa1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4350a1", 0x4350a1u, (void *)w_rl_4350a1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43ab76", 0x43ab76u, (void *)w_rl_43ab76, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41543f", 0x41543fu, (void *)w_rl_41543f, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4154ea", 0x4154eau, (void *)w_rl_4154ea, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_434d33", 0x434d33u, (void *)w_rl_434d33, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_434d60", 0x434d60u, (void *)w_rl_434d60, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_434787", 0x434787u, (void *)w_rl_434787, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4347c7", 0x4347c7u, (void *)w_rl_4347c7, 0, 0, 0, 0, "" },
    { "rl_43484a", 0x43484au, (void *)w_rl_43484a, 0, 0, 0, 0, "" },
    { "rl_434888", 0x434888u, (void *)w_rl_434888, 0, 0, 0, 0, "" },
    { "rl_4348c6", 0x4348c6u, (void *)w_rl_4348c6, 0, 0, 0, 0, "" },
    { "rl_434904", 0x434904u, (void *)w_rl_434904, 0, 0, 0, 0, "" },
    { "rl_401c5b", 0x401c5bu, (void *)w_rl_401c5b, 0, 0, 0, 0, "" },
    { "rl_433dbd", 0x433dbdu, (void *)w_rl_433dbd, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_433deb", 0x433debu, (void *)w_rl_433deb, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4343f8", 0x4343f8u, (void *)w_rl_4343f8, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43441c", 0x43441cu, (void *)w_rl_43441c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43600c", 0x43600cu, (void *)w_rl_43600c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a997", 0x43a997u, (void *)w_rl_43a997, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a950", 0x43a950u, (void *)w_rl_43a950, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_408f1a", 0x408f1au, (void *)w_rl_408f1a, 0, 0, 0, 0, "" },
    { "rl_408ea4", 0x408ea4u, (void *)w_rl_408ea4, 0, 0, 0, 0, "" },
    { "rl_413526", 0x413526u, (void *)w_rl_413526, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_414e06", 0x414e06u, (void *)w_rl_414e06, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_434174", 0x434174u, (void *)w_rl_434174, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41ef42", 0x41ef42u, (void *)w_rl_41ef42, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d5b7", 0x43d5b7u, (void *)w_rl_43d5b7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d649", 0x43d649u, (void *)w_rl_43d649, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d6a7", 0x43d6a7u, (void *)w_rl_43d6a7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d6c3", 0x43d6c3u, (void *)w_rl_43d6c3, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d78b", 0x43d78bu, (void *)w_rl_43d78b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d7a7", 0x43d7a7u, (void *)w_rl_43d7a7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d882", 0x43d882u, (void *)w_rl_43d882, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d8b9", 0x43d8b9u, (void *)w_rl_43d8b9, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d929", 0x43d929u, (void *)w_rl_43d929, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43d9cd", 0x43d9cdu, (void *)w_rl_43d9cd, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43da9e", 0x43da9eu, (void *)w_rl_43da9e, 0, 0, 0, 0, "" },
    { "rl_43daf3", 0x43daf3u, (void *)w_rl_43daf3, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43dbac", 0x43dbacu, (void *)w_rl_43dbac, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43dc1d", 0x43dc1du, (void *)w_rl_43dc1d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43dd83", 0x43dd83u, (void *)w_rl_43dd83, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43ddf4", 0x43ddf4u, (void *)w_rl_43ddf4, 0, 0, 0, 0, "" },
    { "rl_498dcb", 0x498dcbu, (void *)w_rl_498dcb, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_498fb3", 0x498fb3u, (void *)w_rl_498fb3, 0, 0, 0, 0, "" },
    { "rl_43f24b", 0x43f24bu, (void *)w_rl_43f24b, 0, 0, 0, 0, "" },
    { "rl_43f352", 0x43f352u, (void *)w_rl_43f352, 0, 0, 0, 0, "" },
    { "rl_43f484", 0x43f484u, (void *)w_rl_43f484, 0, 0, 0, 0, "" },
    { "rl_4b7250", 0x4b7250u, (void *)w_rl_4b7250, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4b7585", 0x4b7585u, (void *)w_rl_4b7585, 0, 0, 0, 0, "" },
    { "rl_4adcf6", 0x4adcf6u, (void *)w_rl_4adcf6, 0, 0, 0, 0, "" },
    { "rl_4af70c", 0x4af70cu, (void *)w_rl_4af70c, 0, 0, 0, 0, "" },
    { "rl_4b3994", 0x4b3994u, (void *)w_rl_4b3994, 0, 0, 0, 0, "" },
    { "rl_41cb3a", 0x41cb3au, (void *)w_rl_41cb3a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4b14ff", 0x4b14ffu, (void *)w_rl_4b14ff, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43c77a", 0x43c77au, (void *)w_rl_43c77a, 0, 0, 0, 0, "" },
    { "rl_43c885", 0x43c885u, (void *)w_rl_43c885, 0, 0, 0, 0, "" },
    { "rl_43c910", 0x43c910u, (void *)w_rl_43c910, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_479276", 0x479276u, (void *)w_rl_479276, 0, 0, 0, 0, "" },
    { "rl_47935b", 0x47935bu, (void *)w_rl_47935b, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_UI2_N = sizeof RH_HOOKS_RL_UI2 / sizeof *RH_HOOKS_RL_UI2;
