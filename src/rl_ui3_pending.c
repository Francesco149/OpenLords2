/* rl_ui3_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_ui3.c, add its row to tools/rhost/rh_hooks_rl_ui3.c. */
#include "rl.h"

int rl_40df62(void) { return (int)engine_retail_call(0x40df62u, 0); }
int rl_434556(void) { return (int)engine_retail_call(0x434556u, 0); }
int rl_4345d0(void) { return (int)engine_retail_call(0x4345d0u, 0); }
int rl_434693(void) { return (int)engine_retail_call(0x434693u, 0); }
int rl_43480c(void) { return (int)engine_retail_call(0x43480cu, 0); }
int rl_434942(void) { return (int)engine_retail_call(0x434942u, 0); }
int rl_4349a4(void) { return (int)engine_retail_call(0x4349a4u, 0); }
int rl_434a29(void) { return (int)engine_retail_call(0x434a29u, 0); }
int rl_434a9a(void) { return (int)engine_retail_call(0x434a9au, 0); }
int rl_434ad5(void) { return (int)engine_retail_call(0x434ad5u, 0); }
int rl_434b10(void) { return (int)engine_retail_call(0x434b10u, 0); }
int rl_434e9e(void) { return (int)engine_retail_call(0x434e9eu, 0); }
int rl_434ec9(void) { return (int)engine_retail_call(0x434ec9u, 0); }
int rl_4361da(void) { return (int)engine_retail_call(0x4361dau, 0); }
int rl_436229(void) { return (int)engine_retail_call(0x436229u, 0); }
int rl_4362ca(void) { return (int)engine_retail_call(0x4362cau, 0); }
int rl_4378b3(void) { return (int)engine_retail_call(0x4378b3u, 0); }
int rl_437afb(void) { return (int)engine_retail_call(0x437afbu, 0); }
int rl_437d65(void) { return (int)engine_retail_call(0x437d65u, 0); }
int rl_437e9e(void) { return (int)engine_retail_call(0x437e9eu, 0); }
int rl_437fd7(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x437fd7u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_43bbe7(void) { return (int)engine_retail_call(0x43bbe7u, 0); }
int rl_4477e7(int a0) { return (int)engine_retail_call(0x4477e7u, 1, (uint32_t)a0); }
