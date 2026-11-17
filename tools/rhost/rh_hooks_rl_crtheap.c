/* rh_hooks_rl_crtheap.c — rows for src/rl_crt_dbgheap.c (the debug heap's check / realloc / leak-dump paths and the sbh internals they reach (_CrtCheckMemory, _realloc_dbg, _CrtDumpMemoryLeaks, __sbh_decommit_pages / resize / heap_check, _callnewh)) vs retail, on identical
 * state (rh_hooks.h contract; plumbing rh_port.h; stack outputs rh_crt_so.h). Unreached by the corpus: the evidence is
 * `rhost.py probe tests/probes/crt-dbgheap.probe` (synthetic carriers) + the hybrid pass. */
#include "rh_port.h"
#include "rl_crt.h"
#include "rl_crt_dbgheap.h"

#define RH_TABLE RH_HOOKS_RL_CRTHEAP
extern hook_t RH_HOOKS_RL_CRTHEAP[];
enum {
    X_rl_crt_4bff40, X_rl_crt_4bff80, X_rl_crt_4c0cf0, X_rl_crt_4c1370, X_rl_crt_4c15f0, X_rl_crt_4c18b0,
    X_rl_crt_4c19b0, X_rl_crt_4c3220, X_rl_crt_4c3650, X_rl_crt_4c3690, X_rl_crt_4c36c0, X_rl_crt_4c4090,
    X_rl_crt_4c4160, X_rl_crt_4c43e0, X_rl_crt_4c47c0, X_rl_crt_4c4850, X_rl_crt_4c52e0, X_rl_crt_4c54a0,
};
#include "rh_crt_so.h"

/* ---------------------------------------------------------------- rows */
/* _CrtMemCheckpoint writes its _CrtMemState wherever the caller keeps it — _CrtDumpMemoryLeaks's own local, so no caller
 * passes a stack state through this row's entry; HOOK_V (retail's EAX is _lTotalAlloc's leftover, not a result).
 * memcpy into a caller's stack buffer: HOOK_SO (the length in argument 2). */
HOOK_R(rl_crt_4bff40, 5, rl_crt_4bff40(a[0], a[1], I(2), a[3], I(4)))
HOOK_R(rl_crt_4bff80, 6, rl_crt_4bff80(a[0], a[1], I(2), a[3], I(4), I(5)))
HOOK_R(rl_crt_4c0cf0, 0, rl_crt_4c0cf0())
HOOK_V(rl_crt_4c1370, 1, rl_crt_4c1370(a[0]))
HOOK_V(rl_crt_4c15f0, 1, rl_crt_4c15f0(a[0]))
HOOK_V(rl_crt_4c18b0, 1, rl_crt_4c18b0(a[0]))
HOOK_R(rl_crt_4c19b0, 0, rl_crt_4c19b0())
HOOK_SO(rl_crt_4c3220, 3, SPEC({ 0, 1, 2 }), rl_crt_4c3220(a[0], a[1], a[2]))
HOOK_R(rl_crt_4c3650, 1, rl_crt_4c3650(a[0]))
HOOK_R(rl_crt_4c3690, 1, rl_crt_4c3690(a[0]))
HOOK_R(rl_crt_4c36c0, 2, rl_crt_4c36c0(a[0], I(1)))
HOOK_R(rl_crt_4c4090, 2, rl_crt_4c4090(a[0], a[1]))
HOOK_R(rl_crt_4c4160, 2, rl_crt_4c4160(a[0], a[1]))
HOOK_R(rl_crt_4c43e0, 0, rl_crt_4c43e0())
HOOK_V(rl_crt_4c47c0, 1, rl_crt_4c47c0(a[0]))
HOOK_V(rl_crt_4c4850, 1, rl_crt_4c4850(I(0)))
HOOK_R(rl_crt_4c52e0, 4, rl_crt_4c52e0(a[0], a[1], a[2], a[3]))
HOOK_R(rl_crt_4c54a0, 0, rl_crt_4c54a0())

hook_t RH_HOOKS_RL_CRTHEAP[] = {
    { "rl_crt_4bff40", 0x4bff40u, (void *)w_rl_crt_4bff40, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4bff80", 0x4bff80u, (void *)w_rl_crt_4bff80, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c0cf0", 0x4c0cf0u, (void *)w_rl_crt_4c0cf0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c1370", 0x4c1370u, (void *)w_rl_crt_4c1370, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c15f0", 0x4c15f0u, (void *)w_rl_crt_4c15f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c18b0", 0x4c18b0u, (void *)w_rl_crt_4c18b0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c19b0", 0x4c19b0u, (void *)w_rl_crt_4c19b0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c3220", 0x4c3220u, (void *)w_rl_crt_4c3220, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c3650", 0x4c3650u, (void *)w_rl_crt_4c3650, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c3690", 0x4c3690u, (void *)w_rl_crt_4c3690, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c36c0", 0x4c36c0u, (void *)w_rl_crt_4c36c0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c4090", 0x4c4090u, (void *)w_rl_crt_4c4090, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c4160", 0x4c4160u, (void *)w_rl_crt_4c4160, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c43e0", 0x4c43e0u, (void *)w_rl_crt_4c43e0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c47c0", 0x4c47c0u, (void *)w_rl_crt_4c47c0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c4850", 0x4c4850u, (void *)w_rl_crt_4c4850, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c52e0", 0x4c52e0u, (void *)w_rl_crt_4c52e0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c54a0", 0x4c54a0u, (void *)w_rl_crt_4c54a0, 0, 0, 0, 0, "", 0, 0 },
};
const int RH_HOOKS_RL_CRTHEAP_N = sizeof RH_HOOKS_RL_CRTHEAP / sizeof *RH_HOOKS_RL_CRTHEAP;
