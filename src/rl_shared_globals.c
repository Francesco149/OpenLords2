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
uint8_t g_rl_4d2168[4] __attribute__((aligned(4)));
uint8_t g_rl_4d216c[4] __attribute__((aligned(4)));
uint8_t g_rl_4dd640[336] __attribute__((aligned(4))) = {80,1,126,1,25,0,24,0,204,138,67,0,0,0,0,4,0,0,0,0,0,0,0,0,8,0,36,0,56,0,32,0,225,41,67,0,0,0,0,4,0,0,0,0,0,0,0,0,8,0,44,0,62,0,32,0,225,41,67,0,0,0,0,4,0,0,0,0,0,0,0,0,48,0,24,0,21,0,24,0,233,161,67,0,0,0,0,4,0,0,0,0,0,0,0,0,72,0,24,0,23,0,24,0,178,162,67,0,0,0,0,4,0,0,0,0,0,0,0,0,8,0,116,1,62,0,32,0,225,41,67,0,0,0,0,4,0,0,0,0,0,0,0,0,48,0,116,1,56,0,32,0,225,41,67,0,0,0,0,4,0,0,0,0,0,0,0,0,144,0,71,0,21,0,24,0,50,170,67,0,0,0,0,4,0,0,0,0,0,0,0,0,168,0,71,0,23,0,24,0,213,170,67,0,0,0,0,4,0,0,0,0,0,0,0,0,8,0,16,0,62,0,32,0,242,168,67,0,0,0,0,5,0,0,0,0,0,0,0,0,8,0,76,0,62,0,32,0,225,41,67,0,0,0,0,5,0,0,0,0,0,0,0,0,8,0,136,0,62,0,32,0,225,41,67,0,0,0,0,5,0,0,0,0,0,0,0,0,8,0,196,0,62,0,32,0,225,41,67,0,0,0,0,5,0,0,0,0,0,0,0,0,8,0,248,0,56,0,32,0,225,41,67,0,0,0,0,4,0,0,0,0,0,0,0,0};
uint8_t g_rl_522ca8[4] __attribute__((aligned(4)));
uint8_t g_rl_522cac[4] __attribute__((aligned(4)));
uint8_t g_rl_522cb0[4] __attribute__((aligned(4)));
uint8_t g_rl_522cb4[4] __attribute__((aligned(4)));
uint8_t g_rl_5440b8[4] __attribute__((aligned(4)));
uint8_t g_rl_553018[4] __attribute__((aligned(4)));
uint8_t g_rl_553074[4] __attribute__((aligned(4)));
uint8_t g_rl_5530f4[4] __attribute__((aligned(4)));
uint8_t g_rl_5530fc[4] __attribute__((aligned(4)));
uint8_t g_rl_553214[4] __attribute__((aligned(4)));
uint8_t g_rl_553d2c[4] __attribute__((aligned(4)));
uint8_t g_rl_553fa8[4] __attribute__((aligned(4)));
uint8_t g_rl_554040[4] __attribute__((aligned(4)));
uint8_t g_rl_554468[4] __attribute__((aligned(4)));
uint8_t g_rl_5681d0[4] __attribute__((aligned(4)));
uint8_t g_rl_568204[4] __attribute__((aligned(4)));
uint8_t g_rl_568474[4] __attribute__((aligned(4)));
uint8_t g_rl_569580[4] __attribute__((aligned(4)));
uint8_t g_rl_56d6a0[4] __attribute__((aligned(4)));
uint8_t g_rl_57a0cc[4] __attribute__((aligned(4)));
uint8_t g_rl_57c908[4] __attribute__((aligned(4)));
uint8_t g_rl_57cb3c[4] __attribute__((aligned(4)));
uint8_t g_rl_58fe1c[4] __attribute__((aligned(4)));
uint8_t g_rl_58fe20[4] __attribute__((aligned(4)));
uint8_t g_rl_58fe24[4] __attribute__((aligned(4)));
uint8_t g_rl_5aeb74[4] __attribute__((aligned(4)));
uint8_t g_rl_5cd410[4] __attribute__((aligned(4)));
uint8_t g_rl_5c9270[4] __attribute__((aligned(4)));   /* render: a pointer; glue: a dword */
uint8_t g_rl_5533e0[8] __attribute__((aligned(4)));
uint8_t g_rl_55cd34[4] __attribute__((aligned(4)));
uint8_t g_rl_5653e8[4] __attribute__((aligned(4)));
uint8_t g_rl_56899c[4] __attribute__((aligned(4)));
uint8_t g_rl_4ea0f4[4] __attribute__((aligned(4)));
uint8_t g_rl_4ea83c[4] __attribute__((aligned(4)));
