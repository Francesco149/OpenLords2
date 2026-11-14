/* rl_mpnet2_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_mpnet2.c, add its row to tools/rhost/rh_hooks_rl_mpnet2.c. */
#include "rl.h"

int rl_40ec2c(void) { return (int)engine_retail_call(0x40ec2cu, 0); }
int rl_420630(int a0) { return (int)engine_retail_call(0x420630u, 1, (uint32_t)a0); }
int rl_42d40b(void) { return (int)engine_retail_call(0x42d40bu, 0); }
int rl_43338e(void) { return (int)engine_retail_call(0x43338eu, 0); }
int rl_43695d(void) { return (int)engine_retail_call(0x43695du, 0); }
int rl_476488(void) { return (int)engine_retail_call(0x476488u, 0); }
int rl_479e0c(int a0) { return (int)engine_retail_call(0x479e0cu, 1, (uint32_t)a0); }
int rl_49792e(void) { return (int)engine_retail_call(0x49792eu, 0); }
int rl_4b64a3(void) { return (int)engine_retail_call(0x4b64a3u, 0); }
int rl_4b6a15(void) { return (int)engine_retail_call(0x4b6a15u, 0); }
int rl_4b6ab6(void) { return (int)engine_retail_call(0x4b6ab6u, 0); }
int rl_4b6ef7(void) { return (int)engine_retail_call(0x4b6ef7u, 0); }
int rl_4b743b(void) { return (int)engine_retail_call(0x4b743bu, 0); }
int rl_4b756b(void) { return (int)engine_retail_call(0x4b756bu, 0); }
int rl_4b79e3(int a0, int a1) { return (int)engine_retail_call(0x4b79e3u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4b7a7c(int a0) { return (int)engine_retail_call(0x4b7a7cu, 1, (uint32_t)a0); }
int rl_4b8243(int a0) { return (int)engine_retail_call(0x4b8243u, 1, (uint32_t)a0); }
int rl_4b82bf(void) { return (int)engine_retail_call(0x4b82bfu, 0); }
int rl_4b8335(void) { return (int)engine_retail_call(0x4b8335u, 0); }
int rl_4b8439(void) { return (int)engine_retail_call(0x4b8439u, 0); }
int rl_4b85b7(void) { return (int)engine_retail_call(0x4b85b7u, 0); }
int rl_4b8b87(void) { return (int)engine_retail_call(0x4b8b87u, 0); }
int rl_4b92a0(void) { return (int)engine_retail_call(0x4b92a0u, 0); }
