/* rl_ui2_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_ui2.c, add its row to tools/rhost/rh_hooks_rl_ui2.c. */
#include "rl.h"

int rl_401c5b(void) { return (int)engine_retail_call(0x401c5bu, 0); }
int rl_408ea4(int a0) { return (int)engine_retail_call(0x408ea4u, 1, (uint32_t)a0); }
int rl_408f1a(int a0) { return (int)engine_retail_call(0x408f1au, 1, (uint32_t)a0); }
int rl_413526(void) { return (int)engine_retail_call(0x413526u, 0); }
int rl_41424a(void) { return (int)engine_retail_call(0x41424au, 0); }
int rl_414e06(int a0) { return (int)engine_retail_call(0x414e06u, 1, (uint32_t)a0); }
int rl_41543f(void) { return (int)engine_retail_call(0x41543fu, 0); }
int rl_4154ea(void) { return (int)engine_retail_call(0x4154eau, 0); }
int rl_41cb3a(void) { return (int)engine_retail_call(0x41cb3au, 0); }
int rl_41ef42(void) { return (int)engine_retail_call(0x41ef42u, 0); }
int rl_433dbd(void) { return (int)engine_retail_call(0x433dbdu, 0); }
int rl_433deb(void) { return (int)engine_retail_call(0x433debu, 0); }
int rl_434174(int a0) { return (int)engine_retail_call(0x434174u, 1, (uint32_t)a0); }
int rl_4343f8(void) { return (int)engine_retail_call(0x4343f8u, 0); }
int rl_43441c(void) { return (int)engine_retail_call(0x43441cu, 0); }
int rl_434787(void) { return (int)engine_retail_call(0x434787u, 0); }
int rl_4347c7(void) { return (int)engine_retail_call(0x4347c7u, 0); }
int rl_43484a(void) { return (int)engine_retail_call(0x43484au, 0); }
int rl_434888(void) { return (int)engine_retail_call(0x434888u, 0); }
int rl_4348c6(void) { return (int)engine_retail_call(0x4348c6u, 0); }
int rl_434904(void) { return (int)engine_retail_call(0x434904u, 0); }
int rl_434d33(void) { return (int)engine_retail_call(0x434d33u, 0); }
int rl_434d60(void) { return (int)engine_retail_call(0x434d60u, 0); }
int rl_434fa1(void) { return (int)engine_retail_call(0x434fa1u, 0); }
int rl_434fd5(void) { return (int)engine_retail_call(0x434fd5u, 0); }
int rl_43505a(void) { return (int)engine_retail_call(0x43505au, 0); }
int rl_4350a1(void) { return (int)engine_retail_call(0x4350a1u, 0); }
int rl_43600c(void) { return (int)engine_retail_call(0x43600cu, 0); }
int rl_438b02(void) { return (int)engine_retail_call(0x438b02u, 0); }
int rl_438bec(int a0, int a1, int a2) { return (int)engine_retail_call(0x438becu, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_439541(void) { return (int)engine_retail_call(0x439541u, 0); }
int rl_43958a(void) { return (int)engine_retail_call(0x43958au, 0); }
int rl_4399b0(void) { return (int)engine_retail_call(0x4399b0u, 0); }
int rl_439b52(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x439b52u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_439cc2(int a0, int a1, int a2) { return (int)engine_retail_call(0x439cc2u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_439edb(int a0, int a1) { return (int)engine_retail_call(0x439edbu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_439f6a(int a0, int a1, int a2) { return (int)engine_retail_call(0x439f6au, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_43a950(void) { return (int)engine_retail_call(0x43a950u, 0); }
int rl_43a997(int a0, int a1) { return (int)engine_retail_call(0x43a997u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_43ab76(void) { return (int)engine_retail_call(0x43ab76u, 0); }
int rl_43c77a(int a0) { return (int)engine_retail_call(0x43c77au, 1, (uint32_t)a0); }
int rl_43c885(int a0) { return (int)engine_retail_call(0x43c885u, 1, (uint32_t)a0); }
int rl_43c910(int a0) { return (int)engine_retail_call(0x43c910u, 1, (uint32_t)a0); }
int rl_43d5b7(void) { return (int)engine_retail_call(0x43d5b7u, 0); }
int rl_43d649(void) { return (int)engine_retail_call(0x43d649u, 0); }
int rl_43d6a7(void) { return (int)engine_retail_call(0x43d6a7u, 0); }
int rl_43d6c3(void) { return (int)engine_retail_call(0x43d6c3u, 0); }
int rl_43d78b(void) { return (int)engine_retail_call(0x43d78bu, 0); }
int rl_43d7a7(void) { return (int)engine_retail_call(0x43d7a7u, 0); }
int rl_43d882(void) { return (int)engine_retail_call(0x43d882u, 0); }
int rl_43d8b9(void) { return (int)engine_retail_call(0x43d8b9u, 0); }
int rl_43d929(void) { return (int)engine_retail_call(0x43d929u, 0); }
int rl_43d9cd(void) { return (int)engine_retail_call(0x43d9cdu, 0); }
int rl_43da9e(void) { return (int)engine_retail_call(0x43da9eu, 0); }
int rl_43daf3(void) { return (int)engine_retail_call(0x43daf3u, 0); }
int rl_43dbac(void) { return (int)engine_retail_call(0x43dbacu, 0); }
int rl_43dc1d(void) { return (int)engine_retail_call(0x43dc1du, 0); }
int rl_43dd83(void) { return (int)engine_retail_call(0x43dd83u, 0); }
int rl_43ddf4(void) { return (int)engine_retail_call(0x43ddf4u, 0); }
int rl_43f24b(void) { return (int)engine_retail_call(0x43f24bu, 0); }
int rl_43f352(void) { return (int)engine_retail_call(0x43f352u, 0); }
int rl_43f484(void) { return (int)engine_retail_call(0x43f484u, 0); }
int rl_44ff4a(int a0) { return (int)engine_retail_call(0x44ff4au, 1, (uint32_t)a0); }
int rl_450000(int a0) { return (int)engine_retail_call(0x450000u, 1, (uint32_t)a0); }
int rl_450639(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x450639u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_46d421(void) { return (int)engine_retail_call(0x46d421u, 0); }
int rl_479276(int a0) { return (int)engine_retail_call(0x479276u, 1, (uint32_t)a0); }
int rl_47935b(int a0) { return (int)engine_retail_call(0x47935bu, 1, (uint32_t)a0); }
int rl_498dcb(void) { return (int)engine_retail_call(0x498dcbu, 0); }
int rl_498fb3(int a0, int a1) { return (int)engine_retail_call(0x498fb3u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4adcf6(void) { return (int)engine_retail_call(0x4adcf6u, 0); }
int rl_4af70c(int a0) { return (int)engine_retail_call(0x4af70cu, 1, (uint32_t)a0); }
int rl_4b14ff(void) { return (int)engine_retail_call(0x4b14ffu, 0); }
int rl_4b3994(int a0) { return (int)engine_retail_call(0x4b3994u, 1, (uint32_t)a0); }
int rl_4b7250(void) { return (int)engine_retail_call(0x4b7250u, 0); }
int rl_4b7585(int a0) { return (int)engine_retail_call(0x4b7585u, 1, (uint32_t)a0); }
