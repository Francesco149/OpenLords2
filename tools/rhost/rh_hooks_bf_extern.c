/* rh_hooks_bf_extern.c — the battlefield's 'extern' partition (src/engine_bf_extern.c) vs retail (rh_hooks.h contract). */
#include "rh_port.h"
#include "engine_bf.h"
#include "rh_bf_extern_alias.h"

__asm__(RH_BF_EXTERN_ALIASES);

#define RH_TABLE RH_HOOKS_BF_EXTERN
extern hook_t RH_HOOKS_BF_EXTERN[];
enum { X_bf_43f304, X_bf_499d97, X_bf_477b2f, X_bf_4987b7 };

HOOK_V(bf_43f304, 0, bf_43f304())
HOOK_V(bf_499d97, 0, bf_499d97())
HOOK_V(bf_477b2f, 0, bf_477b2f())
HOOK_R(bf_4987b7, 0, bf_4987b7())

hook_t RH_HOOKS_BF_EXTERN[] = {
    { "bf_43f304", 0x43f304u, (void *)w_bf_43f304, 0, 0, 0, 0, "" },
    { "bf_499d97", 0x499d97u, (void *)w_bf_499d97, 0, 0, 0, 0, "" },
    { "bf_477b2f", 0x477b2fu, (void *)w_bf_477b2f, 0, 0, 0, 0, "" },
    { "bf_4987b7", 0x4987b7u, (void *)w_bf_4987b7, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_EXTERN_N = sizeof RH_HOOKS_BF_EXTERN / sizeof *RH_HOOKS_BF_EXTERN;
