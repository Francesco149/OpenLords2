/* rl_glue_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * rl_glue.c, add its row to tools/rhost/rh_hooks_rl_glue.c. */
#include "rl.h"

int rl_409429(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x409429u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40ae12(int a0, int a1, int a2) { return (int)engine_retail_call(0x40ae12u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_40c8d1(void) { return (int)engine_retail_call(0x40c8d1u, 0); }
int rl_40c928(void) { return (int)engine_retail_call(0x40c928u, 0); }
int rl_40dd92(int a0, int a1) { return (int)engine_retail_call(0x40dd92u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_40deca(int a0, int a1) { return (int)engine_retail_call(0x40decau, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_40dfcd(void) { return (int)engine_retail_call(0x40dfcdu, 0); }
int rl_40e00a(int a0, int a1) { return (int)engine_retail_call(0x40e00au, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_40e099(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x40e099u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_40e761(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) { return (int)engine_retail_call(0x40e761u, 8, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4, (uint32_t)a5, (uint32_t)a6, (uint32_t)a7); }
int rl_40f10f(void) { return (int)engine_retail_call(0x40f10fu, 0); }
int rl_41423f(void) { return (int)engine_retail_call(0x41423fu, 0); }
int rl_42a8d1(void) { return (int)engine_retail_call(0x42a8d1u, 0); }
int rl_42a9ab(void) { return (int)engine_retail_call(0x42a9abu, 0); }
int rl_42d179(void) { return (int)engine_retail_call(0x42d179u, 0); }
int rl_42e26f(void) { return (int)engine_retail_call(0x42e26fu, 0); }
int rl_42f148(int a0, int a1, int a2) { return (int)engine_retail_call(0x42f148u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_4323fe(void) { return (int)engine_retail_call(0x4323feu, 0); }
int rl_432443(void) { return (int)engine_retail_call(0x432443u, 0); }
int rl_43253a(void) { return (int)engine_retail_call(0x43253au, 0); }
int rl_432893(int a0) { return (int)engine_retail_call(0x432893u, 1, (uint32_t)a0); }
int rl_43292d(void) { return (int)engine_retail_call(0x43292du, 0); }
int rl_4329a4(void) { return (int)engine_retail_call(0x4329a4u, 0); }
int rl_4330c6(void) { return (int)engine_retail_call(0x4330c6u, 0); }
int rl_433a70(int a0) { return (int)engine_retail_call(0x433a70u, 1, (uint32_t)a0); }
int rl_433e93(void) { return (int)engine_retail_call(0x433e93u, 0); }
int rl_433f49(void) { return (int)engine_retail_call(0x433f49u, 0); }
int rl_4344f2(void) { return (int)engine_retail_call(0x4344f2u, 0); }
int rl_43470d(void) { return (int)engine_retail_call(0x43470du, 0); }
int rl_43496c(void) { return (int)engine_retail_call(0x43496cu, 0); }
int rl_434988(void) { return (int)engine_retail_call(0x434988u, 0); }
int rl_434cbe(void) { return (int)engine_retail_call(0x434cbeu, 0); }
int rl_434cee(void) { return (int)engine_retail_call(0x434ceeu, 0); }
int rl_4360f2(void) { return (int)engine_retail_call(0x4360f2u, 0); }
int rl_43618b(void) { return (int)engine_retail_call(0x43618bu, 0); }
int rl_43631e(void) { return (int)engine_retail_call(0x43631eu, 0); }
int rl_436872(void) { return (int)engine_retail_call(0x436872u, 0); }
int rl_4369bd(void) { return (int)engine_retail_call(0x4369bdu, 0); }
int rl_436b22(void) { return (int)engine_retail_call(0x436b22u, 0); }
int rl_436b59(void) { return (int)engine_retail_call(0x436b59u, 0); }
int rl_437002(void) { return (int)engine_retail_call(0x437002u, 0); }
int rl_43733a(void) { return (int)engine_retail_call(0x43733au, 0); }
int rl_43744f(void) { return (int)engine_retail_call(0x43744fu, 0); }
int rl_4374c4(void) { return (int)engine_retail_call(0x4374c4u, 0); }
int rl_4376bb(void) { return (int)engine_retail_call(0x4376bbu, 0); }
int rl_438990(void) { return (int)engine_retail_call(0x438990u, 0); }
int rl_438a91(void) { return (int)engine_retail_call(0x438a91u, 0); }
int rl_438acc(void) { return (int)engine_retail_call(0x438accu, 0); }
int rl_439079(void) { return (int)engine_retail_call(0x439079u, 0); }
int rl_439122(void) { return (int)engine_retail_call(0x439122u, 0); }
int rl_43933b(int a0, int a1) { return (int)engine_retail_call(0x43933bu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_4393eb(void) { return (int)engine_retail_call(0x4393ebu, 0); }
int rl_43b4cb(int a0, int a1) { return (int)engine_retail_call(0x43b4cbu, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_43b593(void) { return (int)engine_retail_call(0x43b593u, 0); }
int rl_43b681(void) { return (int)engine_retail_call(0x43b681u, 0); }
int rl_43b7c4(int a0, int a1, int a2) { return (int)engine_retail_call(0x43b7c4u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int rl_43b848(void) { return (int)engine_retail_call(0x43b848u, 0); }
int rl_43b98a(int a0) { return (int)engine_retail_call(0x43b98au, 1, (uint32_t)a0); }
int rl_43eda0(int a0, int a1) { return (int)engine_retail_call(0x43eda0u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_440666(void) { return (int)engine_retail_call(0x440666u, 0); }
int rl_448d7e(int a0) { return (int)engine_retail_call(0x448d7eu, 1, (uint32_t)a0); }
int rl_451f8a(void) { return (int)engine_retail_call(0x451f8au, 0); }
int rl_46c4a3(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x46c4a3u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int rl_472986(void) { return (int)engine_retail_call(0x472986u, 0); }
int rl_472a31(void) { return (int)engine_retail_call(0x472a31u, 0); }
int rl_475b41(int a0, int a1) { return (int)engine_retail_call(0x475b41u, 2, (uint32_t)a0, (uint32_t)a1); }
int rl_475d73(int a0) { return (int)engine_retail_call(0x475d73u, 1, (uint32_t)a0); }
int rl_47703a(void) { return (int)engine_retail_call(0x47703au, 0); }
int rl_4782c5(void) { return (int)engine_retail_call(0x4782c5u, 0); }
int rl_47f474(void) { return (int)engine_retail_call(0x47f474u, 0); }
int rl_481b9a(void) { return (int)engine_retail_call(0x481b9au, 0); }
int rl_482111(void) { return (int)engine_retail_call(0x482111u, 0); }
int rl_498458(void) { return (int)engine_retail_call(0x498458u, 0); }
int rl_49b8b0(void) { return (int)engine_retail_call(0x49b8b0u, 0); }
int rl_4a82b9(void) { return (int)engine_retail_call(0x4a82b9u, 0); }
int rl_4b1310(void) { return (int)engine_retail_call(0x4b1310u, 0); }
