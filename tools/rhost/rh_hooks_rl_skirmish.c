/* rh_hooks_rl_skirmish.c — a retail-layout port, partition 'skirmish' (src/rl_skirmish.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "rl.h"

#define RH_TABLE RH_HOOKS_RL_SKIRMISH
extern hook_t RH_HOOKS_RL_SKIRMISH[];
/* row indices (must follow the table's order) */
enum { X_rl_none_ };

hook_t RH_HOOKS_RL_SKIRMISH[] = {
    { 0 },   /* (placeholder: replace with the first row) */
};
const int RH_HOOKS_RL_SKIRMISH_N = 0;
