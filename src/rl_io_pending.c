/* rl_io_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_io.c, add its row to tools/rhost/rh_hooks_rl_io.c. */
#include "rl.h"

int rl_41b013(void) { return (int)engine_retail_call(0x41b013u, 0); }
int rl_425e4b(int a0, int a1) { return (int)engine_retail_call(0x425e4bu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_426216(int a0) { return (int)engine_retail_call(0x426216u, 1, (uint32_t)a0); }
int rl_4262eb(int a0) { return (int)engine_retail_call(0x4262ebu, 1, (uint32_t)a0); }
int rl_426360(int a0) { return (int)engine_retail_call(0x426360u, 1, (uint32_t)a0); }
int rl_4263ad(int a0, int a1, int a2) { return (int)engine_retail_call(0x4263adu, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_426529(void) { return (int)engine_retail_call(0x426529u, 0); }
int rl_426534(int a0, int a1) { return (int)engine_retail_call(0x426534u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_426b92(int a0) { return (int)engine_retail_call(0x426b92u, 1, (uint32_t)a0); }
int rl_426dd5(void) { return (int)engine_retail_call(0x426dd5u, 0); }
int rl_427990(int a0, int a1, int a2) { return (int)engine_retail_call(0x427990u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_427c9b(void) { return (int)engine_retail_call(0x427c9bu, 0); }
int rl_42d91b(int a0, int a1, int a2, int a3, int a4) { return (int)engine_retail_call(0x42d91bu, 5, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4); }
int rl_42da18(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x42da18u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_42dbc7(int a0) { return (int)engine_retail_call(0x42dbc7u, 1, (uint32_t)a0); }
int rl_42e298(void) { return (int)engine_retail_call(0x42e298u, 0); }
int rl_42e320(void) { return (int)engine_retail_call(0x42e320u, 0); }
int rl_42e362(void) { return (int)engine_retail_call(0x42e362u, 0); }
int rl_42f58c(int a0, int a1) { return (int)engine_retail_call(0x42f58cu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_433cc1(void) { return (int)engine_retail_call(0x433cc1u, 0); }
int rl_451f70(void) { return (int)engine_retail_call(0x451f70u, 0); }
int rl_45280b(void) { return (int)engine_retail_call(0x45280bu, 0); }
int rl_46a101(void) { return (int)engine_retail_call(0x46a101u, 0); }
int rl_46ea9a(int a0) { return (int)engine_retail_call(0x46ea9au, 1, (uint32_t)a0); }
int rl_46f21f(void) { return (int)engine_retail_call(0x46f21fu, 0); }
int rl_46f267(void) { return (int)engine_retail_call(0x46f267u, 0); }
int rl_497500(void) { return (int)engine_retail_call(0x497500u, 0); }
int rl_4975d3(void) { return (int)engine_retail_call(0x4975d3u, 0); }
int rl_4978ad(void) { return (int)engine_retail_call(0x4978adu, 0); }
int rl_497a34(int a0) { return (int)engine_retail_call(0x497a34u, 1, (uint32_t)a0); }
int rl_4983b7(void) { return (int)engine_retail_call(0x4983b7u, 0); }
int rl_4990f9(void) { return (int)engine_retail_call(0x4990f9u, 0); }
int rl_49915e(void) { return (int)engine_retail_call(0x49915eu, 0); }
int rl_499261(void) { return (int)engine_retail_call(0x499261u, 0); }
int rl_499366(void) { return (int)engine_retail_call(0x499366u, 0); }
int rl_499619(void) { return (int)engine_retail_call(0x499619u, 0); }
int rl_49964c(void) { return (int)engine_retail_call(0x49964cu, 0); }
int rl_4996c8(void) { return (int)engine_retail_call(0x4996c8u, 0); }
int rl_499aca(void) { return (int)engine_retail_call(0x499acau, 0); }
int rl_499d7d(void) { return (int)engine_retail_call(0x499d7du, 0); }
int rl_49c995(void) { return (int)engine_retail_call(0x49c995u, 0); }
int rl_4ae1b8(void) { return (int)engine_retail_call(0x4ae1b8u, 0); }
int rl_4ae2ab(int a0) { return (int)engine_retail_call(0x4ae2abu, 1, (uint32_t)a0); }
int rl_4ae2bd(void) { return (int)engine_retail_call(0x4ae2bdu, 0); }
int rl_4aecf0(int a0, int a1) { return (int)engine_retail_call(0x4aecf0u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4aee3e(void) { return (int)engine_retail_call(0x4aee3eu, 0); }
int rl_4af116(int a0) { return (int)engine_retail_call(0x4af116u, 1, (uint32_t)a0); }
int rl_4af312(int a0) { return (int)engine_retail_call(0x4af312u, 1, (uint32_t)a0); }
int rl_4af767(int a0) { return (int)engine_retail_call(0x4af767u, 1, (uint32_t)a0); }
int rl_4af841(int a0) { return (int)engine_retail_call(0x4af841u, 1, (uint32_t)a0); }
int rl_4afab9(int a0, int a1, int a2) { return (int)engine_retail_call(0x4afab9u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_4afe3a(int a0) { return (int)engine_retail_call(0x4afe3au, 1, (uint32_t)a0); }
int rl_4b049b(void) { return (int)engine_retail_call(0x4b049bu, 0); }
int rl_4b0aaa(int a0) { return (int)engine_retail_call(0x4b0aaau, 1, (uint32_t)a0); }
int rl_4b11ce(void) { return (int)engine_retail_call(0x4b11ceu, 0); }
int rl_4b2575(void) { return (int)engine_retail_call(0x4b2575u, 0); }
int rl_4b36c0(int a0) { return (int)engine_retail_call(0x4b36c0u, 1, (uint32_t)a0); }
int rl_4b3714(int a0) { return (int)engine_retail_call(0x4b3714u, 1, (uint32_t)a0); }
int rl_4b37bc(void) { return (int)engine_retail_call(0x4b37bcu, 0); }
int rl_4b3940(int a0) { return (int)engine_retail_call(0x4b3940u, 1, (uint32_t)a0); }
int rl_4b3acd(int a0) { return (int)engine_retail_call(0x4b3acdu, 1, (uint32_t)a0); }
int rl_4b3b92(int a0) { return (int)engine_retail_call(0x4b3b92u, 1, (uint32_t)a0); }
int rl_4b8c3a(void) { return (int)engine_retail_call(0x4b8c3au, 0); }
int rl_4b92e0(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x4b92e0u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_4b95ba(int a0, int a1, int a2) { return (int)engine_retail_call(0x4b95bau, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_4b9652(int a0, int a1, int a2, int a3, int a4) { return (int)engine_retail_call(0x4b9652u, 5, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4); }
int rl_4b97f9(int a0, int a1) { return (int)engine_retail_call(0x4b97f9u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4b984f(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x4b984fu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_4c0580(int a0) { return (int)engine_retail_call(0x4c0580u, 1, (uint32_t)a0); }
int rl_4c05a0(int a0, int a1) { return (int)engine_retail_call(0x4c05a0u, 2, (uint32_t)a0, (uint32_t)a1); }
