/* rh_hooks_rl_misc.c — a retail-layout port, partition 'misc' (src/rl_misc.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "rl.h"
#include "rh_rl_misc_alias.h"

__asm__(RH_RL_MISC_ALIASES);

#define RH_TABLE RH_HOOKS_RL_MISC
extern hook_t RH_HOOKS_RL_MISC[];
#include "rh_hook_smk.h"
/* row indices (must follow the table's order) */
enum {
    X_rl_45242b, X_rl_497879, X_rl_49a3e6, X_rl_4b0cb4, X_rl_4b0e03, X_rl_4b12fb, X_rl_4b1897, X_rl_4b18f8, X_rl_4b1cf3, X_rl_4b1dd5, X_rl_4b209a, X_rl_4b25fc, X_rl_4b26f8, X_rl_4b28cd, X_rl_4b3571, X_rl_4bbe51, X_rl_4bbea7, X_rl_4bbf15,
};

HOOK_V(rl_45242b, 0, rl_45242b())
HOOK_V(rl_497879, 0, rl_497879())
HOOK_V(rl_49a3e6, 0, rl_49a3e6())
HOOK_V(rl_4b0cb4, 3, rl_4b0cb4(I(0), I(1), I(2)))
HOOK_V(rl_4b0e03, 0, rl_4b0e03())
HOOK_R(rl_4b12fb, 0, rl_4b12fb())
HOOK_V(rl_4b1897, 0, rl_4b1897())
HOOK_V(rl_4b18f8, 2, rl_4b18f8(I(0), I(1)))
HOOK_V(rl_4b1cf3, 1, rl_4b1cf3(I(0)))
HOOK_V(rl_4b1dd5, 0, rl_4b1dd5())
HOOK_R(rl_4b209a, 1, rl_4b209a(I(0)))
HOOK_V(rl_4b25fc, 0, rl_4b25fc())
HOOK_V(rl_4b26f8, 0, rl_4b26f8())
HOOK_V(rl_4b28cd, 1, rl_4b28cd(I(0)))
HOOK_SMK_V(rl_4b3571, 1, rl_4b3571(I(0)))   /* plays intro.smk: smackw32's state rolled back (rh_hook_smk.h) */
HOOK_V(rl_4bbe51, 0, rl_4bbe51())
HOOK_R(rl_4bbea7, 0, rl_4bbea7())
HOOK_R(rl_4bbf15, 0, rl_4bbf15())

hook_t RH_HOOKS_RL_MISC[] = {
    { "rl_45242b", 0x45242bu, (void *)w_rl_45242b, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },   /* an empty retail function called every frame: sampled (2026-11-17, ~10% of a verify's shadowed calls with its siblings) */
    { "rl_497879", 0x497879u, (void *)w_rl_497879, 0, 0, 0, 0, "" },
    { "rl_49a3e6", 0x49a3e6u, (void *)w_rl_49a3e6, 0, 0, 0, 0, "" },
    { "rl_4b0cb4", 0x4b0cb4u, (void *)w_rl_4b0cb4, 0, 0, 0, 0, "" },
    { "rl_4b0e03", 0x4b0e03u, (void *)w_rl_4b0e03, 0, 0, 0, 0, "" },
    { "rl_4b12fb", 0x4b12fbu, (void *)w_rl_4b12fb, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },   /* an empty retail function called every frame: sampled (2026-11-17, ~10% of a verify's shadowed calls with its siblings) */
    { "rl_4b1897", 0x4b1897u, (void *)w_rl_4b1897, 0, 0, 0, 0, "" },
    { "rl_4b18f8", 0x4b18f8u, (void *)w_rl_4b18f8, 0, 0, 0, 0, "" },
    { "rl_4b1cf3", 0x4b1cf3u, (void *)w_rl_4b1cf3, 0, 0, 0, 0, "" },
    { "rl_4b1dd5", 0x4b1dd5u, (void *)w_rl_4b1dd5, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },   /* an empty retail function called every frame: sampled (2026-11-17, ~10% of a verify's shadowed calls with its siblings) */
    { "rl_4b209a", 0x4b209au, (void *)w_rl_4b209a, 0, 0, 0, 0, "" },
    { "rl_4b25fc", 0x4b25fcu, (void *)w_rl_4b25fc, 0, 0, 0, 0, "" },
    { "rl_4b26f8", 0x4b26f8u, (void *)w_rl_4b26f8, 0, 0, 0, 0, "" },
    { "rl_4b28cd", 0x4b28cdu, (void *)w_rl_4b28cd, 0, 0, 0, 0, "" },
    { "rl_4b3571", 0x4b3571u, (void *)w_rl_4b3571, 0, 0, 0, 0, "" },
    { "rl_4bbe51", 0x4bbe51u, (void *)w_rl_4bbe51, 0, 0, 0, 0, "" },
    { "rl_4bbea7", 0x4bbea7u, (void *)w_rl_4bbea7, 0, 0, 0, 0, "" },
    { "rl_4bbf15", 0x4bbf15u, (void *)w_rl_4bbf15, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_MISC_N = sizeof RH_HOOKS_RL_MISC / sizeof *RH_HOOKS_RL_MISC;
