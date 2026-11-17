/* rh_hooks_rl_crtdbg.c — rows for src/rl_crt_dbg.c (the report / message / exit / signal path: _CrtDbgReport and its window, _amsg_exit / _NMSG_WRITE, _exit / abort / raise, _flsbuf / _getbuf, the C++ unhandled-exception filter) vs retail, on identical
 * state (rh_hooks.h contract; plumbing rh_port.h; stack outputs rh_crt_so.h). Unreached by the corpus: the evidence is
 * `rhost.py probe tests/probes/crt-dbg.probe` (synthetic carriers) + the hybrid pass. */
#include "rh_port.h"
#include "rl_crt.h"
#include "rl_crt_dbg.h"

#define RH_TABLE RH_HOOKS_RL_CRTDBG
extern hook_t RH_HOOKS_RL_CRTDBG[];
enum {
    X_rl_crtdbg_none_,
};
#include "rh_crt_so.h"

/* ---------------------------------------------------------------- rows */

hook_t RH_HOOKS_RL_CRTDBG[] = {
};
const int RH_HOOKS_RL_CRTDBG_N = sizeof RH_HOOKS_RL_CRTDBG / sizeof *RH_HOOKS_RL_CRTDBG;
