/* rh_hooks_rl_mpmsg.c — a retail-layout port, partition 'mpmsg' (src/rl_mpmsg.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "rl.h"
#include "rl_mpmsg_globals.h"
#include "rh_rl_mpmsg_alias.h"

#define RH_TABLE RH_HOOKS_RL_MPMSG
extern hook_t RH_HOOKS_RL_MPMSG[];
/* row indices (must follow the table's order) */
enum { X_rl_none_ };

hook_t RH_HOOKS_RL_MPMSG[] = {
    { 0 },   /* (placeholder: replace with the first row) */
};
const int RH_HOOKS_RL_MPMSG_N = 0;
