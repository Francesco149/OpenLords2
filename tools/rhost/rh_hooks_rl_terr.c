/* rh_hooks_rl_terr.c — a retail-layout port, partition 'terr' (src/rl_terr.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself).
 *
 * No row (ported, verified with --shadow-full: docs/findings/rl-terr.md):
 *   rl_465486 / rl_465622 / rl_465761  the army-state handlers 2..4 (DAT_004d6a50). The executor 0x465d28 they call
 *              reads its uninitialized waypoint locals (quirk #229) from just below the handler's esp, and the paged
 *              shadow's first write to a protected page (the handler's +0x154 store) faults: the exception dispatch
 *              writes its frames there, so retail's side reads them instead of the pure run's residue (bot3-s104-t32
 *              f19934: retail-in-shadow moved army 2, the pure run and the port did not) and the kept retail state
 *              leaves the corpus. Under --shadow-full (no faults) they verify. With master's RH_SH_FULL they could take
 *              a row, but the hybrid pass then FAILS together with rl_46521f (ctl): 46521f models the executor's four
 *              locals as stack residue (rl_46521f_res, rh_res_get / put), these handlers run engine_army_move_run on its
 *              globals — the two must share one residue model first (2026-11-05: 0x465622 14 divergences, hybrid FAIL 5). */
#include "rh_port.h"
#include "rl.h"
#include "rl_terr_globals.h"
#include "rh_rl_terr_alias.h"

__asm__(RH_RL_TERR_ALIASES);

#define RH_TABLE RH_HOOKS_RL_TERR
extern hook_t RH_HOOKS_RL_TERR[];
/* row indices (must follow the table's order) */
enum {
    X_rl_46ea28, X_rl_46ea61, X_rl_46ead0, X_rl_46eb09, X_rl_46f1dc, X_rl_46f2ae, X_rl_46f3cd, X_rl_472ad0, X_rl_472ae0, X_rl_472b40,
    X_rl_472bc5, X_rl_472e46, X_rl_46c5b8, X_rl_46c6ba, X_rl_46df51, X_rl_46dfd5, X_rl_46d9da, X_rl_46d7f4, X_rl_46da4b, X_rl_46ac22,
    X_rl_467fd1, X_rl_468121, X_rl_468c41, X_rl_468e61, X_rl_46c080, X_rl_46c147, X_rl_46902a, X_rl_468d4f, X_rl_467a36, X_rl_467ca6,
    X_rl_4684c6, X_rl_46de94, X_rl_46de05, X_rl_46c2de, X_rl_46dbf5, X_rl_46dd34, X_rl_467770, X_rl_46797d, X_rl_46a037, X_rl_4676e0,
    X_rl_46cfbd, X_rl_46733c, X_rl_46d2a3, X_rl_46d5fe, X_rl_4673b8, X_rl_467532, X_rl_467e2c, X_rl_467f2e, X_rl_46841c, X_rl_46942c,
    X_rl_46958f, X_rl_46965a, X_rl_46921d, X_rl_4697cd, X_rl_46988d, X_rl_469a9c, X_rl_469d21, X_rl_46e1b0, X_rl_46f700, X_rl_4701ac,
    X_rl_4707be, X_rl_4708d0, X_rl_46f68e, X_rl_470678, X_rl_46ff43, X_rl_472392, X_rl_47265d, X_rl_472875,
};

HOOK_R(rl_46ea28, 1, rl_46ea28(I(0)))
HOOK_R(rl_46ea61, 1, rl_46ea61(I(0)))
HOOK_R(rl_46ead0, 1, rl_46ead0(I(0)))
HOOK_R(rl_46eb09, 1, rl_46eb09(I(0)))
HOOK_R(rl_46f1dc, 0, rl_46f1dc())
HOOK_R(rl_46f2ae, 0, rl_46f2ae())
HOOK_R(rl_46f3cd, 0, rl_46f3cd())
HOOK_R(rl_472ad0, 0, rl_472ad0())
HOOK_R(rl_472ae0, 0, rl_472ae0())
HOOK_R(rl_472b40, 1, rl_472b40(I(0)))
HOOK_V(rl_472bc5, 8, rl_472bc5(I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7)))
HOOK_V(rl_472e46, 0, rl_472e46())
HOOK_R(rl_46c5b8, 1, rl_46c5b8(I(0)))
HOOK_R(rl_46c6ba, 0, rl_46c6ba())
HOOK_V(rl_46df51, 0, rl_46df51())
HOOK_V(rl_46dfd5, 1, rl_46dfd5(I(0)))
HOOK_R(rl_46d9da, 1, rl_46d9da(I(0)))
HOOK_R(rl_46d7f4, 3, rl_46d7f4(I(0), I(1), I(2)))
HOOK_V(rl_46da4b, 1, rl_46da4b(I(0)))
HOOK_R(rl_46ac22, 5, rl_46ac22(I(0), I(1), I(2), I(3), I(4)))
HOOK_R(rl_467fd1, 1, rl_467fd1(I(0)))
HOOK_R(rl_468121, 1, rl_468121(I(0)))
HOOK_R(rl_468c41, 1, rl_468c41(I(0)))
HOOK_V(rl_468e61, 1, rl_468e61(I(0)))
HOOK_V(rl_46c080, 0, rl_46c080())
HOOK_R(rl_46c147, 1, rl_46c147(I(0)))
HOOK_V(rl_46902a, 1, rl_46902a(I(0)))
HOOK_R(rl_468d4f, 0, rl_468d4f())
HOOK_V(rl_467a36, 0, rl_467a36())
HOOK_V(rl_467ca6, 0, rl_467ca6())
HOOK_V(rl_4684c6, 2, rl_4684c6(I(0), I(1)))
HOOK_R(rl_46de94, 2, rl_46de94(I(0), I(1)))
HOOK_V(rl_46de05, 2, rl_46de05(I(0), I(1)))
HOOK_R(rl_46c2de, 2, rl_46c2de(I(0), I(1)))
HOOK_V(rl_46dbf5, 0, rl_46dbf5())
HOOK_V(rl_46dd34, 0, rl_46dd34())
HOOK_R(rl_467770, 1, rl_467770(I(0)))
HOOK_V(rl_46797d, 1, rl_46797d(I(0)))
HOOK_R(rl_46a037, 1, rl_46a037(I(0)))
HOOK_V(rl_4676e0, 0, rl_4676e0())
HOOK_R(rl_46cfbd, 3, rl_46cfbd(I(0), I(1), I(2)))
HOOK_R(rl_46733c, 2, rl_46733c(I(0), I(1)))
HOOK_R(rl_46d2a3, 3, rl_46d2a3(I(0), I(1), I(2)))
HOOK_V(rl_46d5fe, 0, rl_46d5fe())
HOOK_R(rl_4673b8, 1, rl_4673b8(I(0)))
HOOK_R(rl_467532, 2, rl_467532(I(0), I(1)))
HOOK_R(rl_467e2c, 2, rl_467e2c(I(0), I(1)))
HOOK_R(rl_467f2e, 1, rl_467f2e(I(0)))
HOOK_V(rl_46841c, 1, rl_46841c(I(0)))
HOOK_V(rl_46942c, 1, rl_46942c(I(0)))
HOOK_V(rl_46958f, 1, rl_46958f(I(0)))
HOOK_V(rl_46965a, 1, rl_46965a(I(0)))
HOOK_V(rl_46921d, 1, rl_46921d(I(0)))
HOOK_V(rl_4697cd, 2, rl_4697cd(I(0), I(1)))
HOOK_V(rl_46988d, 3, rl_46988d(I(0), I(1), I(2)))
HOOK_V(rl_469a9c, 2, rl_469a9c(I(0), I(1)))
HOOK_V(rl_469d21, 5, rl_469d21(I(0), I(1), I(2), I(3), I(4)))
HOOK_R(rl_46e1b0, 4, rl_46e1b0(I(0), I(1), I(2), I(3)))
HOOK_V(rl_46f700, 4, rl_46f700(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4701ac, 3, rl_4701ac(I(0), I(1), I(2)))
HOOK_V(rl_4707be, 2, rl_4707be(I(0), I(1)))
HOOK_V(rl_4708d0, 1, rl_4708d0(I(0)))
HOOK_V(rl_46f68e, 4, rl_46f68e(I(0), I(1), I(2), I(3)))
HOOK_R(rl_470678, 1, rl_470678(I(0)))
HOOK_V(rl_46ff43, 0, rl_46ff43())
HOOK_R(rl_472392, 4, rl_472392(I(0), I(1), I(2), I(3)))
HOOK_R(rl_47265d, 4, rl_47265d(I(0), I(1), I(2), I(3)))
HOOK_V(rl_472875, 2, rl_472875(I(0), I(1)))

hook_t RH_HOOKS_RL_TERR[] = {
    { "rl_46ea28", 0x46ea28u, (void *)w_rl_46ea28, 0, 0, 0, 0, "" },
    { "rl_46ea61", 0x46ea61u, (void *)w_rl_46ea61, 0, 0, 0, 0, "" },
    { "rl_46ead0", 0x46ead0u, (void *)w_rl_46ead0, 0, 0, 0, 0, "" },
    { "rl_46eb09", 0x46eb09u, (void *)w_rl_46eb09, 0, 0, 0, 0, "" },
    { "rl_46f1dc", 0x46f1dcu, (void *)w_rl_46f1dc, 0, 0, 0, 0, "" },
    { "rl_46f2ae", 0x46f2aeu, (void *)w_rl_46f2ae, 0, 0, 0, 0, "" },
    { "rl_46f3cd", 0x46f3cdu, (void *)w_rl_46f3cd, 0, 0, 0, 0, "" },
    { "rl_472ad0", 0x472ad0u, (void *)w_rl_472ad0, 0, 0, 0, 0, "" },
    { "rl_472ae0", 0x472ae0u, (void *)w_rl_472ae0, 0, 0, 0, 0, "" },
    { "rl_472b40", 0x472b40u, (void *)w_rl_472b40, 0, 0, 0, 0, "" },
    { "rl_472bc5", 0x472bc5u, (void *)w_rl_472bc5, 0, 0, 0, 0, "" },
    { "rl_472e46", 0x472e46u, (void *)w_rl_472e46, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_46c5b8", 0x46c5b8u, (void *)w_rl_46c5b8, 0, 0, 0, 0, "" },
    { "rl_46c6ba", 0x46c6bau, (void *)w_rl_46c6ba, 0, 0, 0, 0, "" },
    { "rl_46df51", 0x46df51u, (void *)w_rl_46df51, 0, 0, 0, 0, "" },
    { "rl_46dfd5", 0x46dfd5u, (void *)w_rl_46dfd5, 0, 0, 0, 0, "" },
    { "rl_46d9da", 0x46d9dau, (void *)w_rl_46d9da, 0, 0, 0, 0, "" },
    { "rl_46d7f4", 0x46d7f4u, (void *)w_rl_46d7f4, 0, 0, 0, 0, "" },
    { "rl_46da4b", 0x46da4bu, (void *)w_rl_46da4b, 0, 0, 0, 0, "" },
    { "rl_46ac22", 0x46ac22u, (void *)w_rl_46ac22, 0, 0, 0, 0, "" },
    { "rl_467fd1", 0x467fd1u, (void *)w_rl_467fd1, 0, 0, 0, 0, "" },
    { "rl_468121", 0x468121u, (void *)w_rl_468121, 0, 0, 0, 0, "" },
    { "rl_468c41", 0x468c41u, (void *)w_rl_468c41, 0, 0, 0, 0, "" },
    { "rl_468e61", 0x468e61u, (void *)w_rl_468e61, 0, 0, 0, 0, "" },
    { "rl_46c080", 0x46c080u, (void *)w_rl_46c080, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_46c147", 0x46c147u, (void *)w_rl_46c147, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_46902a", 0x46902au, (void *)w_rl_46902a, 0, 0, 0, 0, "" },
    { "rl_468d4f", 0x468d4fu, (void *)w_rl_468d4f, 0, 0, 0, 0, "" },
    { "rl_467a36", 0x467a36u, (void *)w_rl_467a36, 0, 0, 0, 0, "" },
    { "rl_467ca6", 0x467ca6u, (void *)w_rl_467ca6, 0, 0, 0, 0, "" },
    { "rl_4684c6", 0x4684c6u, (void *)w_rl_4684c6, 0, 0, 0, 0, "" },
    { "rl_46de94", 0x46de94u, (void *)w_rl_46de94, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_46de05", 0x46de05u, (void *)w_rl_46de05, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_46c2de", 0x46c2deu, (void *)w_rl_46c2de, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_46dbf5", 0x46dbf5u, (void *)w_rl_46dbf5, 0, 0, 0, 0, "" },
    { "rl_46dd34", 0x46dd34u, (void *)w_rl_46dd34, 0, 0, 0, 0, "" },
    { "rl_467770", 0x467770u, (void *)w_rl_467770, 0, 0, 0, 0, "" },
    { "rl_46797d", 0x46797du, (void *)w_rl_46797d, 0, 0, 0, 0, "" },
    { "rl_46a037", 0x46a037u, (void *)w_rl_46a037, 0, 0, 0, 0, "" },
    { "rl_4676e0", 0x4676e0u, (void *)w_rl_4676e0, 0, 0, 0, 0, "" },
    { "rl_46cfbd", 0x46cfbdu, (void *)w_rl_46cfbd, 0, 0, 0, 0, "" },
    { "rl_46733c", 0x46733cu, (void *)w_rl_46733c, 0, 0, 0, 0, "" },
    { "rl_46d2a3", 0x46d2a3u, (void *)w_rl_46d2a3, 0, 0, 0, 0, "" },
    { "rl_46d5fe", 0x46d5feu, (void *)w_rl_46d5fe, 0, 0, 0, 0, "" },
    { "rl_4673b8", 0x4673b8u, (void *)w_rl_4673b8, 0, 0, 0, 0, "" },
    { "rl_467532", 0x467532u, (void *)w_rl_467532, 0, 0, 0, 0, "" },
    { "rl_467e2c", 0x467e2cu, (void *)w_rl_467e2c, 0, 0, 0, 0, "" },
    { "rl_467f2e", 0x467f2eu, (void *)w_rl_467f2e, 0, 0, 0, 0, "" },
    { "rl_46841c", 0x46841cu, (void *)w_rl_46841c, 0, 0, 0, 0, "" },
    { "rl_46942c", 0x46942cu, (void *)w_rl_46942c, 0, 0, 0, 0, "" },
    { "rl_46958f", 0x46958fu, (void *)w_rl_46958f, 0, 0, 0, 0, "" },
    { "rl_46965a", 0x46965au, (void *)w_rl_46965a, 0, 0, 0, 0, "" },
    { "rl_46921d", 0x46921du, (void *)w_rl_46921d, 0, 0, 0, 0, "" },
    { "rl_4697cd", 0x4697cdu, (void *)w_rl_4697cd, 0, 0, 0, 0, "" },
    { "rl_46988d", 0x46988du, (void *)w_rl_46988d, 0, 0, 0, 0, "" },
    { "rl_469a9c", 0x469a9cu, (void *)w_rl_469a9c, 0, 0, 0, 0, "" },
    { "rl_469d21", 0x469d21u, (void *)w_rl_469d21, 0, 0, 0, 0, "" },
    { "rl_46e1b0", 0x46e1b0u, (void *)w_rl_46e1b0, 0, 0, 0, 0, "" },
    { "rl_46f700", 0x46f700u, (void *)w_rl_46f700, 0, 0, 0, 0, "" },
    { "rl_4701ac", 0x4701acu, (void *)w_rl_4701ac, 0, 0, 0, 0, "" },
    { "rl_4707be", 0x4707beu, (void *)w_rl_4707be, 0, 0, 0, 0, "" },
    { "rl_4708d0", 0x4708d0u, (void *)w_rl_4708d0, 0, 0, 0, 0, "" },
    { "rl_46f68e", 0x46f68eu, (void *)w_rl_46f68e, 0, 0, 0, 0, "" },
    { "rl_470678", 0x470678u, (void *)w_rl_470678, 0, 0, 0, 0, "" },
    { "rl_46ff43", 0x46ff43u, (void *)w_rl_46ff43, 0, 0, 0, 0, "" },
    { "rl_472392", 0x472392u, (void *)w_rl_472392, 0, 0, 0, 0, "" },
    { "rl_47265d", 0x47265du, (void *)w_rl_47265d, 0, 0, 0, 0, "" },
    { "rl_472875", 0x472875u, (void *)w_rl_472875, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_TERR_N = sizeof RH_HOOKS_RL_TERR / sizeof *RH_HOOKS_RL_TERR;
