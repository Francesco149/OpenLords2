/* rh_hooks_rl_crtenv.c — rows for src/rl_crt_env.c (the locale / environment / wide-char / file paths (_isctype, __crtLCMapStringA, CPtoLCID / setSBCS, atol, wcstombs / wctomb, __crtsetenv / __wtomb_environ, _fcloseall / fclose, _commit, _chsize / _setmode, _validdrive)) vs retail, on identical
 * state (rh_hooks.h contract; plumbing rh_port.h; stack outputs rh_crt_so.h). Unreached by the corpus: the evidence is
 * `rhost.py probe tests/probes/crt-env.probe` (synthetic carriers) + the hybrid pass. Notes: docs/findings/crt-gap-env.md.
 * Stack outputs (HOOK_SO): GetStringTypeA's char-type words, LCMapStringA's / wcstombs' / wctomb's destination. */
#include "rh_port.h"
#include "rl_crt.h"
#include "rl_crt_env.h"

#define RH_TABLE RH_HOOKS_RL_CRTENV
extern hook_t RH_HOOKS_RL_CRTENV[];
enum {
    X_rl_crt_4c1c80, X_rl_crt_4c5710, X_rl_crt_4c1df0, X_rl_crt_4c8d50, X_rl_crt_4c8df0, X_rl_crt_4ccb80,
    X_rl_crt_4ce7f0, X_rl_crt_4ce550, X_rl_crt_4cb350, X_rl_crt_4cbb80, X_rl_crt_4cb860, X_rl_crt_4cde00,
    X_rl_crt_4cd9a0, X_rl_crt_4ccb30, X_rl_crt_4cbf50, X_rl_crt_4cc770, X_rl_crt_4cbbf0, X_rl_crt_4ce280,
    X_rl_crt_4ce1e0, X_rl_crt_4cde70, X_rl_crt_4ccbf0, X_rl_crt_4ce750, X_rl_crt_4ce380, X_rl_crt_4cd1b0,
    X_rl_crt_4ce470, X_rl_crt_4cf9c0, X_rl_crt_4cf640,
};
#include "rh_crt_so.h"

/* ---------------------------------------------------------------- rows */

HOOK_R(rl_crt_4c1c80, 1, rl_crt_4c1c80(a[0]))
HOOK_R(rl_crt_4c5710, 2, rl_crt_4c5710(I(0), I(1)))
HOOK_R(rl_crt_4c1df0, 1, rl_crt_4c1df0(a[0]))
HOOK_R(rl_crt_4c8d50, 1, rl_crt_4c8d50(I(0)))
HOOK_V(rl_crt_4c8df0, 0, rl_crt_4c8df0())
HOOK_R(rl_crt_4ccb80, 3, rl_crt_4ccb80(a[0], a[1], a[2]))
HOOK_R(rl_crt_4ce7f0, 2, rl_crt_4ce7f0(a[0], I(1)))
HOOK_R(rl_crt_4ce550, 2, rl_crt_4ce550(a[0], a[1]))
HOOK_SO(rl_crt_4cb350, 6, SPEC({ 3, 3, 1 }), rl_crt_4cb350(a[0], a[1], I(2), a[3], a[4], a[5]))
HOOK_R(rl_crt_4cbb80, 2, rl_crt_4cbb80(a[0], I(1)))
HOOK_SO(rl_crt_4cb860, 7, SPEC({ 4, 1, 5 }), rl_crt_4cb860(a[0], a[1], a[2], I(3), a[4], I(5), a[6]))
HOOK_R(rl_crt_4cde00, 2, rl_crt_4cde00(a[0], I(1)))
HOOK_R(rl_crt_4cd9a0, 7, rl_crt_4cd9a0(a[0], a[1], a[2], I(3), a[4], I(5), a[6]))
HOOK_R(rl_crt_4ccb30, 1, rl_crt_4ccb30(a[0]))
HOOK_R(rl_crt_4cbf50, 2, rl_crt_4cbf50(a[0], a[1]))
HOOK_SO(rl_crt_4cc770, 2, SPEC({ 0, 0, 4 }), rl_crt_4cc770(a[0], a[1]))
HOOK_SO(rl_crt_4cbbf0, 3, SPEC({ 0, 1, 2 }), rl_crt_4cbbf0(a[0], a[1], a[2]))
HOOK_R(rl_crt_4ce280, 1, rl_crt_4ce280(a[0]))
HOOK_R(rl_crt_4ce1e0, 2, rl_crt_4ce1e0(a[0], a[1]))
HOOK_R(rl_crt_4cde70, 2, rl_crt_4cde70(a[0], I(1)))
HOOK_R(rl_crt_4ccbf0, 0, rl_crt_4ccbf0())
HOOK_V(rl_crt_4ce750, 1, rl_crt_4ce750(a[0]))
HOOK_R(rl_crt_4ce380, 1, rl_crt_4ce380(a[0]))
HOOK_R(rl_crt_4cd1b0, 0, rl_crt_4cd1b0())
HOOK_R(rl_crt_4ce470, 1, rl_crt_4ce470(a[0]))
HOOK_R(rl_crt_4cf9c0, 2, rl_crt_4cf9c0(a[0], I(1)))
HOOK_R(rl_crt_4cf640, 2, rl_crt_4cf640(a[0], I(1)))

hook_t RH_HOOKS_RL_CRTENV[] = {
    { "rl_crt_4c1c80", 0x4c1c80u, (void *)w_rl_crt_4c1c80, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c5710", 0x4c5710u, (void *)w_rl_crt_4c5710, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c1df0", 0x4c1df0u, (void *)w_rl_crt_4c1df0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c8d50", 0x4c8d50u, (void *)w_rl_crt_4c8d50, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c8df0", 0x4c8df0u, (void *)w_rl_crt_4c8df0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ccb80", 0x4ccb80u, (void *)w_rl_crt_4ccb80, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ce7f0", 0x4ce7f0u, (void *)w_rl_crt_4ce7f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ce550", 0x4ce550u, (void *)w_rl_crt_4ce550, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cb350", 0x4cb350u, (void *)w_rl_crt_4cb350, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cbb80", 0x4cbb80u, (void *)w_rl_crt_4cbb80, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cb860", 0x4cb860u, (void *)w_rl_crt_4cb860, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cde00", 0x4cde00u, (void *)w_rl_crt_4cde00, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd9a0", 0x4cd9a0u, (void *)w_rl_crt_4cd9a0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ccb30", 0x4ccb30u, (void *)w_rl_crt_4ccb30, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cbf50", 0x4cbf50u, (void *)w_rl_crt_4cbf50, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cc770", 0x4cc770u, (void *)w_rl_crt_4cc770, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cbbf0", 0x4cbbf0u, (void *)w_rl_crt_4cbbf0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ce280", 0x4ce280u, (void *)w_rl_crt_4ce280, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ce1e0", 0x4ce1e0u, (void *)w_rl_crt_4ce1e0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cde70", 0x4cde70u, (void *)w_rl_crt_4cde70, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ccbf0", 0x4ccbf0u, (void *)w_rl_crt_4ccbf0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ce750", 0x4ce750u, (void *)w_rl_crt_4ce750, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ce380", 0x4ce380u, (void *)w_rl_crt_4ce380, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd1b0", 0x4cd1b0u, (void *)w_rl_crt_4cd1b0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ce470", 0x4ce470u, (void *)w_rl_crt_4ce470, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cf9c0", 0x4cf9c0u, (void *)w_rl_crt_4cf9c0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cf640", 0x4cf640u, (void *)w_rl_crt_4cf640, 0, 0, 0, 0, "", 0, 0 },
};
const int RH_HOOKS_RL_CRTENV_N = sizeof RH_HOOKS_RL_CRTENV / sizeof *RH_HOOKS_RL_CRTENV;
