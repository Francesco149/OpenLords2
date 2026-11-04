/* rl_ui4_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_ui4.c, add its row to tools/rhost/rh_hooks_rl_ui4.c. */
#include "rl.h"

int rl_401000(int a0, int a1, int a2) { return (int)engine_retail_call(0x401000u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_401085(int a0, int a1, int a2) { return (int)engine_retail_call(0x401085u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_4010da(int a0, int a1, int a2) { return (int)engine_retail_call(0x4010dau, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_401136(int a0, int a1, int a2) { return (int)engine_retail_call(0x401136u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_4011b0(int a0) { return (int)engine_retail_call(0x4011b0u, 1, (uint32_t)a0); }
int rl_4011ea(int a0) { return (int)engine_retail_call(0x4011eau, 1, (uint32_t)a0); }
int rl_401246(int a0) { return (int)engine_retail_call(0x401246u, 1, (uint32_t)a0); }
int rl_4012a2(int a0) { return (int)engine_retail_call(0x4012a2u, 1, (uint32_t)a0); }
int rl_401303(int a0, int a1) { return (int)engine_retail_call(0x401303u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_401333(int a0, int a1) { return (int)engine_retail_call(0x401333u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4013bc(int a0) { return (int)engine_retail_call(0x4013bcu, 1, (uint32_t)a0); }
int rl_4014f0(int a0, int a1) { return (int)engine_retail_call(0x4014f0u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4015b9(int a0, int a1) { return (int)engine_retail_call(0x4015b9u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_401666(int a0) { return (int)engine_retail_call(0x401666u, 1, (uint32_t)a0); }
int rl_4017bf(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x4017bfu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40187a(int a0) { return (int)engine_retail_call(0x40187au, 1, (uint32_t)a0); }
int rl_4018d7(int a0, int a1) { return (int)engine_retail_call(0x4018d7u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_401984(void) { return (int)engine_retail_call(0x401984u, 0); }
int rl_401a20(int a0) { return (int)engine_retail_call(0x401a20u, 1, (uint32_t)a0); }
int rl_401c70(void) { return (int)engine_retail_call(0x401c70u, 0); }
int rl_401c93(void) { return (int)engine_retail_call(0x401c93u, 0); }
int rl_401ca3(void) { return (int)engine_retail_call(0x401ca3u, 0); }
int rl_401cbc(void) { return (int)engine_retail_call(0x401cbcu, 0); }
int rl_401cda(void) { return (int)engine_retail_call(0x401cdau, 0); }
int rl_401cfc(void) { return (int)engine_retail_call(0x401cfcu, 0); }
int rl_401d11(void) { return (int)engine_retail_call(0x401d11u, 0); }
int rl_401d26(int a0) { return (int)engine_retail_call(0x401d26u, 1, (uint32_t)a0); }
int rl_401dc8(void) { return (int)engine_retail_call(0x401dc8u, 0); }
int rl_401f49(void) { return (int)engine_retail_call(0x401f49u, 0); }
int rl_401f8f(int a0) { return (int)engine_retail_call(0x401f8fu, 1, (uint32_t)a0); }
int rl_402009(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x402009u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_4020ae(void) { return (int)engine_retail_call(0x4020aeu, 0); }
int rl_40210c(int a0, int a1) { return (int)engine_retail_call(0x40210cu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4036f9(int a0, int a1) { return (int)engine_retail_call(0x4036f9u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4037ee(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x4037eeu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40489e(void) { return (int)engine_retail_call(0x40489eu, 0); }
int rl_40490d(void) { return (int)engine_retail_call(0x40490du, 0); }
int rl_404b2c(void) { return (int)engine_retail_call(0x404b2cu, 0); }
int rl_404d6b(int a0, int a1) { return (int)engine_retail_call(0x404d6bu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_404d96(int a0, int a1) { return (int)engine_retail_call(0x404d96u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_404dc1(int a0, int a1) { return (int)engine_retail_call(0x404dc1u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_404e03(int a0, int a1) { return (int)engine_retail_call(0x404e03u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_404e4b(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x404e4bu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_404ead(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x404eadu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_404f4c(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x404f4cu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_408fcb(int a0, int a1) { return (int)engine_retail_call(0x408fcbu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_40ccfa(void) { return (int)engine_retail_call(0x40ccfau, 0); }
int rl_40da1e(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x40da1eu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40e3ee(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x40e3eeu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40e5e7(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x40e5e7u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40e6f2(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x40e6f2u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40e7e4(void) { return (int)engine_retail_call(0x40e7e4u, 0); }
int rl_40e890(void) { return (int)engine_retail_call(0x40e890u, 0); }
int rl_40e8bb(void) { return (int)engine_retail_call(0x40e8bbu, 0); }
int rl_40ecc4(void) { return (int)engine_retail_call(0x40ecc4u, 0); }
int rl_40f1a0(int a0) { return (int)engine_retail_call(0x40f1a0u, 1, (uint32_t)a0); }
int rl_40f5fd(int a0) { return (int)engine_retail_call(0x40f5fdu, 1, (uint32_t)a0); }
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
int rl_427ed0(void) { return (int)engine_retail_call(0x427ed0u, 0); }
int rl_428471(void) { return (int)engine_retail_call(0x428471u, 0); }
int rl_42847c(void) { return (int)engine_retail_call(0x42847cu, 0); }
int rl_4290d0(void) { return (int)engine_retail_call(0x4290d0u, 0); }
int rl_429153(int a0, int a1) { return (int)engine_retail_call(0x429153u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4291b3(void) { return (int)engine_retail_call(0x4291b3u, 0); }
int rl_42925e(int a0) { return (int)engine_retail_call(0x42925eu, 1, (uint32_t)a0); }
int rl_4298c1(int a0) { return (int)engine_retail_call(0x4298c1u, 1, (uint32_t)a0); }
int rl_429b1d(void) { return (int)engine_retail_call(0x429b1du, 0); }
int rl_429ba4(int a0) { return (int)engine_retail_call(0x429ba4u, 1, (uint32_t)a0); }
int rl_42a15f(void) { return (int)engine_retail_call(0x42a15fu, 0); }
int rl_42e5c0(void) { return (int)engine_retail_call(0x42e5c0u, 0); }
int rl_42eb7c(void) { return (int)engine_retail_call(0x42eb7cu, 0); }
int rl_42ec91(void) { return (int)engine_retail_call(0x42ec91u, 0); }
int rl_42f1df(int a0) { return (int)engine_retail_call(0x42f1dfu, 1, (uint32_t)a0); }
int rl_42f306(void) { return (int)engine_retail_call(0x42f306u, 0); }
