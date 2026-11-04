/* rh_hooks_rl_cov3.c — a retail-layout port, partition 'cov3' (src/rl_cov3.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Router-table actions (0x434308, 0x4367ff, 0x43b741: no E8 call site, the
 * routers call them through a pointer and drop EAX) are entry-patched, HOOK_V. 0x42df30 closes the movie and
 * may start the next one (0x42e060 → 0x42d91b): HOOK_SMK_V (smackw32's state rolled back, rh_hook_smk.h) +
 * RH_SH_MEDIA. docs/findings/rl-cov3.md. */
#include "rh_port.h"
#include "rl.h"
#include "rl_cov3_globals.h"
#include "rh_rl_cov3_alias.h"

__asm__(RH_RL_COV3_ALIASES);

#define RH_TABLE RH_HOOKS_RL_COV3
extern hook_t RH_HOOKS_RL_COV3[];
#include "rh_hook_smk.h"

/* row indices (must follow the table's order) */
enum {
    X_rl_42df30, X_rl_434308, X_rl_4367ff, X_rl_43b741, X_rl_46d130, X_rl_4917dd, X_rl_49ff71, X_rl_4a1b29,
    X_rl_4a3551, X_rl_4ae968, X_rl_4aea70,
};

HOOK_SMK_V(rl_42df30, 0, rl_42df30())   /* closes the movie; 0x42e060 may open the next */
HOOK_V(rl_434308, 0, rl_434308())
HOOK_V(rl_4367ff, 0, rl_4367ff())
HOOK_V(rl_43b741, 0, rl_43b741())
HOOK_R(rl_46d130, 3, rl_46d130(I(0), I(1), I(2)))
HOOK_R(rl_4917dd, 1, rl_4917dd(I(0)))
HOOK_R(rl_49ff71, 2, rl_49ff71(I(0), I(1)))
HOOK_R(rl_4a1b29, 4, rl_4a1b29(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4a3551, 3, rl_4a3551(I(0), I(1), I(2)))
HOOK_R(rl_4ae968, 1, rl_4ae968(I(0)))
HOOK_R(rl_4aea70, 1, rl_4aea70(I(0)))

hook_t RH_HOOKS_RL_COV3[] = {
    { "rl_42df30", 0x42df30u, (void *)w_rl_42df30, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_434308", 0x434308u, (void *)w_rl_434308, 0, 0, 0, 0, "" },
    { "rl_4367ff", 0x4367ffu, (void *)w_rl_4367ff, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },   /* 0x476768: the screen */
    { "rl_43b741", 0x43b741u, (void *)w_rl_43b741, 0, 0, 0, 0, "" },
    { "rl_46d130", 0x46d130u, (void *)w_rl_46d130, 0, 0, 0, 0, "" },
    { "rl_4917dd", 0x4917ddu, (void *)w_rl_4917dd, 0, 0, 0, 0, "" },
    { "rl_49ff71", 0x49ff71u, (void *)w_rl_49ff71, 0, 0, 0, 0, "" },
    { "rl_4a1b29", 0x4a1b29u, (void *)w_rl_4a1b29, 0, 0, 0, 0, "" },
    { "rl_4a3551", 0x4a3551u, (void *)w_rl_4a3551, 0, 0, 0, 0, "" },
    { "rl_4ae968", 0x4ae968u, (void *)w_rl_4ae968, 0, 0, 0, 0, "" },
    { "rl_4aea70", 0x4aea70u, (void *)w_rl_4aea70, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_COV3_N = sizeof RH_HOOKS_RL_COV3 / sizeof *RH_HOOKS_RL_COV3;
