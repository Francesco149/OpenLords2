/* rh_hooks_rl_crtenv.c — rows for src/rl_crt_env.c (the locale / environment / wide-char / file paths (_isctype, __crtLCMapStringA, CPtoLCID / setSBCS, atol, wcstombs / wctomb, __crtsetenv / __wtomb_environ, _fcloseall / fclose, _commit, _chsize / _setmode, _validdrive)) vs retail, on identical
 * state (rh_hooks.h contract; plumbing rh_port.h; stack outputs rh_crt_so.h). Unreached by the corpus: the evidence is
 * `rhost.py probe tests/probes/crt-env.probe` (synthetic carriers) + the hybrid pass. */
#include "rh_port.h"
#include "rl_crt.h"
#include "rl_crt_env.h"

#define RH_TABLE RH_HOOKS_RL_CRTENV
extern hook_t RH_HOOKS_RL_CRTENV[];
enum {
    X_rl_crtenv_none_,
};
#include "rh_crt_so.h"

/* ---------------------------------------------------------------- rows */

hook_t RH_HOOKS_RL_CRTENV[] = {
};
const int RH_HOOKS_RL_CRTENV_N = sizeof RH_HOOKS_RL_CRTENV / sizeof *RH_HOOKS_RL_CRTENV;
