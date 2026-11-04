/* rl_ui4_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_ui4.c, add its row to tools/rhost/rh_hooks_rl_ui4.c. */
#include "rl.h"

int rl_410a5d(int a0, int a1, int a2) { return (int)engine_retail_call(0x410a5du, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_412b33(void) { return (int)engine_retail_call(0x412b33u, 0); }
int rl_412e6b(void) { return (int)engine_retail_call(0x412e6bu, 0); }
int rl_413155(void) { return (int)engine_retail_call(0x413155u, 0); }
int rl_413590(void) { return (int)engine_retail_call(0x413590u, 0); }
int rl_413b30(void) { return (int)engine_retail_call(0x413b30u, 0); }
int rl_4140f3(void) { return (int)engine_retail_call(0x4140f3u, 0); }
int rl_414220(void) { return (int)engine_retail_call(0x414220u, 0); }
int rl_41593b(int a0) { return (int)engine_retail_call(0x41593bu, 1, (uint32_t)a0); }
int rl_415bdc(void) { return (int)engine_retail_call(0x415bdcu, 0); }
int rl_415e42(int a0) { return (int)engine_retail_call(0x415e42u, 1, (uint32_t)a0); }
int rl_415fb7(int a0) { return (int)engine_retail_call(0x415fb7u, 1, (uint32_t)a0); }
int rl_41789b(void) { return (int)engine_retail_call(0x41789bu, 0); }
int rl_417960(void) { return (int)engine_retail_call(0x417960u, 0); }
int rl_4180f6(void) { return (int)engine_retail_call(0x4180f6u, 0); }
int rl_4184c6(void) { return (int)engine_retail_call(0x4184c6u, 0); }
int rl_418e2d(void) { return (int)engine_retail_call(0x418e2du, 0); }
int rl_419c78(void) { return (int)engine_retail_call(0x419c78u, 0); }
int rl_41ad5d(void) { return (int)engine_retail_call(0x41ad5du, 0); }
int rl_41aea2(void) { return (int)engine_retail_call(0x41aea2u, 0); }
int rl_41e61d(void) { return (int)engine_retail_call(0x41e61du, 0); }
int rl_41fdd6(void) { return (int)engine_retail_call(0x41fdd6u, 0); }
int rl_425b10(void) { return (int)engine_retail_call(0x425b10u, 0); }
int rl_425ded(void) { return (int)engine_retail_call(0x425dedu, 0); }
int rl_426d11(int a0) { return (int)engine_retail_call(0x426d11u, 1, (uint32_t)a0); }
int rl_427d19(void) { return (int)engine_retail_call(0x427d19u, 0); }
int rl_42e5c0(void) { return (int)engine_retail_call(0x42e5c0u, 0); }
int rl_42eb7c(void) { return (int)engine_retail_call(0x42eb7cu, 0); }
int rl_42ec91(void) { return (int)engine_retail_call(0x42ec91u, 0); }
int rl_42f1df(int a0) { return (int)engine_retail_call(0x42f1dfu, 1, (uint32_t)a0); }
int rl_42f306(void) { return (int)engine_retail_call(0x42f306u, 0); }
