/* rh_hooks_bf_input.c — the battlefield port, partition 'input' (src/engine_bf_input.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_INPUT
extern hook_t RH_HOOKS_BF_INPUT[];
/* row indices (must follow the table's order) */
enum { X_bf_none_ };

hook_t RH_HOOKS_BF_INPUT[] = {
    { 0 },   /* (placeholder: replace with the first row) */
};
const int RH_HOOKS_BF_INPUT_N = 0;
