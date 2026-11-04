/* rh_hooks_rl_ui3.c — a retail-layout port, partition 'ui3' (src/rl_ui3.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return the port
 * models exactly on every path: compared). Router-table actions (no E8 call site) are entry-patched, HOOK_V. Rows
 * that draw (or call a drawer) carry RH_SH_MEDIA (the shadow compares every surface pixel). Findings:
 * docs/findings/rl-ui3.md.
 *
 * BINDING.  The partition's globals (src/rl_ui3_globals.h, generated + a hand block; rl_ui3_globals.c is not linked)
 * are absolute symbols at their retail VAs (rh_rl_ui3_alias.h). */
#include "rh_port.h"
#include "rl.h"
#include "rl_ui3_globals.h"
#include "rh_rl_ui3_alias.h"

__asm__(RH_RL_UI3_ALIASES);

#define RH_TABLE RH_HOOKS_RL_UI3
extern hook_t RH_HOOKS_RL_UI3[];
/* row indices (must follow the table's order) */
enum {
    X_rl_434556, X_rl_4345d0, X_rl_434693, X_rl_43480c, X_rl_434942, X_rl_4349a4, X_rl_434a29, X_rl_434a9a,
    X_rl_434ad5, X_rl_434b10, X_rl_434e9e, X_rl_434ec9, X_rl_40df62, X_rl_4477e7,
    X_rl_4361da, X_rl_436229, X_rl_4362ca, X_rl_43bbe7,
    X_rl_4378b3, X_rl_437afb, X_rl_437d65, X_rl_437e9e, X_rl_437fd7,
};

HOOK_V(rl_434556, 0, rl_434556())
HOOK_V(rl_4345d0, 0, rl_4345d0())
HOOK_V(rl_434693, 0, rl_434693())
HOOK_V(rl_43480c, 0, rl_43480c())
HOOK_V(rl_434942, 0, rl_434942())
HOOK_V(rl_4349a4, 0, rl_4349a4())
HOOK_V(rl_434a29, 0, rl_434a29())
HOOK_V(rl_434a9a, 0, rl_434a9a())
HOOK_V(rl_434ad5, 0, rl_434ad5())
HOOK_V(rl_434b10, 0, rl_434b10())
HOOK_V(rl_434e9e, 0, rl_434e9e())
HOOK_V(rl_434ec9, 0, rl_434ec9())
HOOK_V(rl_40df62, 0, rl_40df62())
HOOK_V(rl_4477e7, 1, rl_4477e7(I(0)))
HOOK_V(rl_4361da, 0, rl_4361da())
HOOK_V(rl_436229, 0, rl_436229())
HOOK_V(rl_4362ca, 0, rl_4362ca())
HOOK_V(rl_43bbe7, 0, rl_43bbe7())
HOOK_V(rl_4378b3, 0, rl_4378b3())
HOOK_V(rl_437afb, 0, rl_437afb())
HOOK_R(rl_437d65, 0, rl_437d65())
HOOK_R(rl_437e9e, 0, rl_437e9e())
HOOK_R(rl_437fd7, 4, rl_437fd7(I(0), I(1), I(2), I(3)))

hook_t RH_HOOKS_RL_UI3[] = {
    { "rl_434556", 0x434556u, (void *)w_rl_434556, 0, 0, 0, 0, "" },
    { "rl_4345d0", 0x4345d0u, (void *)w_rl_4345d0, 0, 0, 0, 0, "" },
    { "rl_434693", 0x434693u, (void *)w_rl_434693, 0, 0, 0, 0, "" },
    { "rl_43480c", 0x43480cu, (void *)w_rl_43480c, 0, 0, 0, 0, "" },
    { "rl_434942", 0x434942u, (void *)w_rl_434942, 0, 0, 0, 0, "" },
    { "rl_4349a4", 0x4349a4u, (void *)w_rl_4349a4, 0, 0, 0, 0, "" },
    { "rl_434a29", 0x434a29u, (void *)w_rl_434a29, 0, 0, 0, 0, "" },
    { "rl_434a9a", 0x434a9au, (void *)w_rl_434a9a, 0, 0, 0, 0, "" },
    { "rl_434ad5", 0x434ad5u, (void *)w_rl_434ad5, 0, 0, 0, 0, "" },
    { "rl_434b10", 0x434b10u, (void *)w_rl_434b10, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_434e9e", 0x434e9eu, (void *)w_rl_434e9e, 0, 0, 0, 0, "" },
    { "rl_434ec9", 0x434ec9u, (void *)w_rl_434ec9, 0, 0, 0, 0, "" },
    { "rl_40df62", 0x40df62u, (void *)w_rl_40df62, 0, 0, 0, 0, "" },
    { "rl_4477e7", 0x4477e7u, (void *)w_rl_4477e7, 0, 0, 0, 0, "" },
    { "rl_4361da", 0x4361dau, (void *)w_rl_4361da, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_436229", 0x436229u, (void *)w_rl_436229, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4362ca", 0x4362cau, (void *)w_rl_4362ca, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43bbe7", 0x43bbe7u, (void *)w_rl_43bbe7, 0, 0, 0, 0, "" },
    { "rl_4378b3", 0x4378b3u, (void *)w_rl_4378b3, 0, 0, 0, 0, "" },
    { "rl_437afb", 0x437afbu, (void *)w_rl_437afb, 0, 0, 0, 0, "" },
    { "rl_437d65", 0x437d65u, (void *)w_rl_437d65, 0, 0, 0, 0, "" },
    { "rl_437e9e", 0x437e9eu, (void *)w_rl_437e9e, 0, 0, 0, 0, "" },
    { "rl_437fd7", 0x437fd7u, (void *)w_rl_437fd7, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_UI3_N = sizeof RH_HOOKS_RL_UI3 / sizeof *RH_HOOKS_RL_UI3;
