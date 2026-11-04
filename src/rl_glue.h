/* rl_glue.h — a retail-layout port, partition 'glue' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names rl_<va>; a function lives in rl_glue.c once ported, else in rl_glue_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int rl_409429(int a0, int a1, int a2, int a3);   /* 68 B, coverage 5 */
int rl_40ae12(int a0, int a1, int a2);           /* 175 B, coverage 47 */
int rl_40c8d1(void);                             /* 87 B, coverage 5 */
int rl_40c928(void);                             /* 68 B, coverage 5 */
int rl_40dd92(int a0, int a1);                   /* 312 B, coverage 5 */
int rl_40deca(int a0, int a1);                   /* 152 B, coverage 52 */
int rl_40dfcd(void);                             /* 61 B, coverage 5 */
int rl_40e00a(int a0, int a1);                   /* 143 B, coverage 48 */
int rl_40e099(int a0, int a1, int a2, int a3);   /* 149 B, coverage 5 */
int rl_40e761(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7); /* 131 B, coverage 1 */
int rl_40f10f(void);                             /* 145 B, coverage 54 */
int rl_41423f(void);                             /* 11 B, coverage 54 */
int rl_42a8d1(void);                             /* 218 B, coverage 14 */
int rl_42a9ab(void);                             /* 218 B, coverage 8 */
int rl_42d179(void);                             /* 157 B, coverage 54 */
int rl_42e26f(void);                             /* 41 B, coverage 54 */
int rl_42f148(int a0, int a1, int a2);           /* 151 B, coverage 54 */
int rl_4323fe(void);                             /* 69 B, coverage 52 */
int rl_432443(void);                             /* 247 B, coverage 15 */
int rl_43253a(void);                             /* 262 B, coverage 52 */
int rl_432893(int a0);                           /* 154 B, coverage 1 */
int rl_43292d(void);                             /* 58 B, coverage 52 */
int rl_4329a4(void);                             /* 61 B, coverage 15 */
int rl_4330c6(void);                             /* 143 B, coverage 9 */
int rl_433a70(int a0);                           /* 306 B, coverage 9 */
int rl_433e93(void);                             /* 182 B, coverage 1 */
int rl_433f49(void);                             /* 194 B, coverage 1 */
int rl_4344f2(void);                             /* 100 B, coverage 5 */
int rl_43470d(void);                             /* 122 B, coverage 4 */
int rl_43496c(void);                             /* 28 B, coverage 1 */
int rl_434988(void);                             /* 28 B, coverage 1 */
int rl_434cbe(void);                             /* 48 B, coverage 1 */
int rl_434cee(void);                             /* 48 B, coverage 1 */
int rl_4360f2(void);                             /* 41 B, coverage 5 */
int rl_43618b(void);                             /* 79 B, coverage 4 */
int rl_43631e(void);                             /* 84 B, coverage 1 */
int rl_436872(void);                             /* 139 B, coverage 9 */
int rl_4369bd(void);                             /* 203 B, coverage 40 */
int rl_436b22(void);                             /* 55 B, coverage 21 */
int rl_436b59(void);                             /* 425 B, coverage 21 */
int rl_437002(void);                             /* 261 B, coverage 19 */
int rl_43733a(void);                             /* 277 B, coverage 5 */
int rl_43744f(void);                             /* 117 B, coverage 5 */
int rl_4374c4(void);                             /* 113 B, coverage 16 */
int rl_4376bb(void);                             /* 504 B, coverage 3 */
int rl_438990(void);                             /* 257 B, coverage 19 */
int rl_438a91(void);                             /* 59 B, coverage 19 */
int rl_438acc(void);                             /* 54 B, coverage 17 */
int rl_439079(void);                             /* 169 B, coverage 52 */
int rl_439122(void);                             /* 537 B, coverage 52 */
int rl_43933b(int a0, int a1);                   /* 176 B, coverage 45 */
int rl_4393eb(void);                             /* 342 B, coverage 1 */
int rl_43b4cb(int a0, int a1);                   /* 200 B, coverage 40 */
int rl_43b593(void);                             /* 143 B, coverage 39 */
int rl_43b681(void);                             /* 192 B, coverage 3 */
int rl_43b7c4(int a0, int a1, int a2);           /* 132 B, coverage 3 */
int rl_43b848(void);                             /* 207 B, coverage 3 */
int rl_43b98a(int a0);                           /* 23 B, coverage 1 */
int rl_43eda0(int a0, int a1);                   /* 637 B, coverage 51 */
int rl_440666(void);                             /* 383 B, coverage 15 */
int rl_448d7e(int a0);                           /* 148 B, coverage 54 */
int rl_451f8a(void);                             /* 66 B, coverage 52 */
int rl_46c4a3(int a0, int a1, int a2, int a3);   /* 277 B, coverage 15 */
int rl_472986(void);                             /* 63 B, coverage 45 */
int rl_472a31(void);                             /* 152 B, coverage 54 */
int rl_475b41(int a0, int a1);                   /* 562 B, coverage 10 */
int rl_475d73(int a0);                           /* 148 B, coverage 47 */
int rl_47703a(void);                             /* 126 B, coverage 54 */
int rl_4782c5(void);                             /* 340 B, coverage 54 */
int rl_47f474(void);                             /* 761 B, coverage 14 */
int rl_481b9a(void);                             /* 1399 B, coverage 54 */
int rl_482111(void);                             /* 476 B, coverage 54 */
int rl_498458(void);                             /* 132 B, coverage 54 */
int rl_49b8b0(void);                             /* 195 B, coverage 52 */
int rl_4a82b9(void);                             /* 365 B, coverage 52 */
int rl_4b1310(void);                             /* 221 B, coverage 18 */
