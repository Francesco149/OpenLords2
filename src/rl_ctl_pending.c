/* rl_ctl_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_ctl.c, add its row to tools/rhost/rh_hooks_rl_ctl.c. */
#include "rl.h"

int rl_42ff10(void) { return (int)engine_retail_call(0x42ff10u, 0); }
int rl_4398f5(int a0, int a1) { return (int)engine_retail_call(0x4398f5u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_439df0(void) { return (int)engine_retail_call(0x439df0u, 0); }
int rl_43a123(void) { return (int)engine_retail_call(0x43a123u, 0); }
int rl_43de70(void) { return (int)engine_retail_call(0x43de70u, 0); }
int rl_4472ce(int a0) { return (int)engine_retail_call(0x4472ceu, 1, (uint32_t)a0); }
int rl_45183a(int a0, int a1) { return (int)engine_retail_call(0x45183au, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_46521f(void) { return (int)engine_retail_call(0x46521fu, 0); }
