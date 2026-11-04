/* rl_cov3_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_cov3.c, add its row to tools/rhost/rh_hooks_rl_cov3.c. */
#include "rl.h"

int rl_42df30(void) { return (int)engine_retail_call(0x42df30u, 0); }
int rl_434308(void) { return (int)engine_retail_call(0x434308u, 0); }
int rl_4367ff(void) { return (int)engine_retail_call(0x4367ffu, 0); }
int rl_43b741(void) { return (int)engine_retail_call(0x43b741u, 0); }
int rl_46d130(int a0, int a1, int a2) { return (int)engine_retail_call(0x46d130u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_4917dd(int a0) { return (int)engine_retail_call(0x4917ddu, 1, (uint32_t)a0); }
int rl_49ff71(int a0, int a1) { return (int)engine_retail_call(0x49ff71u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4a1b29(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x4a1b29u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_4a3551(int a0, int a1, int a2) { return (int)engine_retail_call(0x4a3551u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_4ae968(int a0) { return (int)engine_retail_call(0x4ae968u, 1, (uint32_t)a0); }
int rl_4aea70(int a0) { return (int)engine_retail_call(0x4aea70u, 1, (uint32_t)a0); }
