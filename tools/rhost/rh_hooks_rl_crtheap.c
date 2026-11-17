/* rh_hooks_rl_crtheap.c — rows for src/rl_crt_dbgheap.c (the debug heap's check / realloc / leak-dump paths and the sbh internals they reach (_CrtCheckMemory, _realloc_dbg, _CrtDumpMemoryLeaks, __sbh_decommit_pages / resize / heap_check, _callnewh)) vs retail, on identical
 * state (rh_hooks.h contract; plumbing rh_port.h; stack outputs rh_crt_so.h). Unreached by the corpus: the evidence is
 * `rhost.py probe tests/probes/crt-dbgheap.probe` (synthetic carriers) + the hybrid pass. */
#include "rh_port.h"
#include "rl_crt.h"
#include "rl_crt_dbgheap.h"

#define RH_TABLE RH_HOOKS_RL_CRTHEAP
extern hook_t RH_HOOKS_RL_CRTHEAP[];
enum {
    X_rl_crtheap_none_,
};
#include "rh_crt_so.h"

/* ---------------------------------------------------------------- rows */

hook_t RH_HOOKS_RL_CRTHEAP[] = {
};
const int RH_HOOKS_RL_CRTHEAP_N = sizeof RH_HOOKS_RL_CRTHEAP / sizeof *RH_HOOKS_RL_CRTHEAP;
