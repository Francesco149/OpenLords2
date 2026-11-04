/* engine_bf_setup_globals.c — retail globals the 'setup' partition needs that engine_bf_globals.h lacks
 * (docs/findings/bf-setup.md). The standalone links this; rhost does not (tools/rhost/build.sh leaves every
 * *_globals.c out) — tools/rhost/rh_hooks_bf_setup.c aliases each symbol at its retail VA. */
#include <stdint.h>

/* DAT_00591564: the single-LFSR state FUN_00404b2c advances (retail seeds it 0x34518632 at InitInstance; the
 * strategic map's terrain passes thread their own copy — engine_territory.c) */
int32_t g_bf_591564 = 0x34518632;
