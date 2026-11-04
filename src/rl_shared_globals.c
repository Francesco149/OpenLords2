/* rl_shared_globals.c — the g_rl_<va> globals more than one rl partition names (io and glue), defined ONCE for the
 * standalone at the largest extent either view uses (each partition's header keeps its own typed view; rhost aliases
 * them at their VAs: rh_rl_io_alias.h). NOT linked into rhost (build.sh skips *_globals.c). */
#include <stdint.h>

uint8_t g_rl_4ea130[16] __attribute__((aligned(4)));
uint8_t g_rl_4ea1b0[768] __attribute__((aligned(4)));
uint8_t g_rl_516a4c[4] __attribute__((aligned(4)));
uint8_t g_rl_52afbc[4] __attribute__((aligned(4)));
uint8_t g_rl_53f288[48] __attribute__((aligned(4)));
uint8_t g_rl_5440b0[4] __attribute__((aligned(4)));
uint8_t g_rl_553248[4] __attribute__((aligned(4)));
uint8_t g_rl_5681e0[8] __attribute__((aligned(4)));
uint8_t g_rl_57cb2c[4] __attribute__((aligned(4)));
uint8_t g_rl_57cb40[8] __attribute__((aligned(4)));
