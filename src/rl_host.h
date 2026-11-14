/* rl_host.h — a retail-layout port, partition 'host' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names rl_<va>; a function lives in rl_host.c once ported, else in rl_host_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int rl_4a57ac(int a0);                           /* 328 B, coverage 53 */
int rl_4a58f4(int a0);                           /* 169 B, coverage 21 */
int rl_4a5b1f(int a0);                           /* 1003 B, coverage 38 */
int rl_4a599d(int a0);                           /* 386 B, order 6 */
int rl_4a5f0a(int a0);                           /* 431 B, coverage 5 */
int rl_4a60b9(int a0);                           /* 439 B, coverage 51 */
int rl_4a6270(int a0);                           /* 602 B, coverage 53 */
int rl_4a64ca(int a0, int a1);                   /* 217 B, coverage 38 */
int rl_4a65a3(int a0, int a1);                   /* 402 B, coverage 53 */
int rl_4a6735(int a0, int a1);                   /* 360 B, coverage 38 */
int rl_4a7e0a(int a0, int a1);                   /* 171 B, coverage 5 */
int rl_4a7eb5(int a0);                           /* 550 B, coverage 5 */
int rl_4a8426(int a0);                           /* 148 B, coverage 5 */
int rl_4a8d9c(void);                             /* 111 B, coverage 44 */
int rl_4a8e0b(void);                             /* 943 B, coverage 44 */
int rl_4a91ba(void);                             /* 152 B, coverage 44 */
int rl_4a9252(void);                             /* 288 B, coverage 44 */
int rl_4a9372(void);                             /* 669 B, coverage 18 */
int rl_4a960f(void);                             /* 334 B, coverage 4 */
int rl_4a975d(void);                             /* 222 B, coverage 13 */
int rl_4a983b(void);                             /* 195 B, coverage 39 */
int rl_4a98fe(int a0, int a1, int a2, int a3, int a4); /* 412 B, coverage 44 */
int rl_4a9eb1(int a0);                           /* 103 B, coverage 53 */
int rl_4a9f18(int a0);                           /* 90 B, coverage 53 */
int rl_4a9f72(int a0);                           /* 199 B, coverage 53 */
int rl_4aabd8(int a0, int a1);                   /* 303 B, coverage 2 */
int rl_4ac7f3(int a0, int a1);                   /* 273 B, coverage 14 */
int rl_4ac904(void);                             /* 295 B, coverage 54 */
int rl_4acb54(int a0);                           /* 128 B, coverage 51 */
int rl_4ace5e(void);                             /* 650 B, coverage 54 */
int rl_4ad0e8(int a0);                           /* 132 B, coverage 6 */
int rl_4ad16c(int a0);                           /* 196 B, coverage 6 */
int rl_4ad230(int a0);                           /* 230 B, coverage 13 */
int rl_4ad316(int a0);                           /* 132 B, coverage 4 */
int rl_4ad39a(void);                             /* 140 B, coverage 54 */
int rl_4ad9f0(void);                             /* 774 B, coverage 4 */
int rl_4ade93(int a0);                           /* 339 B, coverage 54 */
int rl_4adfe6(int a0);                           /* 377 B, coverage 3 */
int rl_4ae310(void);                             /* 553 B, coverage 59 */
int rl_4ae74d(void);                             /* 144 B, coverage 54 */
int rl_4ae8a9(void);                             /* 191 B, coverage 54 */
int rl_4aef7e(void);                             /* 408 B, coverage 59 */
int rl_4af502(void);                             /* 72 B, coverage 59 */
int rl_4af54a(int a0);                           /* 112 B, coverage 59 */
int rl_4af62c(int a0);                           /* 73 B, coverage 3 */
int rl_4af675(int a0);                           /* 84 B, coverage 3 */
int rl_4af8d9(int a0, int a1, int a2, int a3);   /* 381 B, coverage 59 */
int rl_4b0ab5(int a0);                           /* 212 B, coverage 59 */
int rl_4b1867(void);                             /* 48 B, coverage 59 */
int rl_4b18e3(void);                             /* 21 B, coverage 46 */
int rl_4b191e(void);                             /* 981 B, coverage 59 */
int rl_4b1de0(void);                             /* 11 B, coverage 59 */
int rl_4b1deb(int a0, int a1, int a2, int a3);   /* 92 B, coverage 53 */
int rl_4b20ed(void);                             /* 68 B, coverage 59 */
int rl_4b25b0(int a0);                           /* 76 B, coverage 59 */
int rl_4b27bf(void);                             /* 143 B, coverage 59 */
int rl_4b284e(void);                             /* 127 B, coverage 59 */
int rl_4b2922(void);                             /* 156 B, coverage 59 */
int rl_43ff63(void);                             /* 96 B */
int rl_446f06(void);                             /* 126 B */
int rl_43ff08(void);                             /* 91 B */
int rl_43ffc3(void);                             /* 117 B */
int rl_43f7e3(void);                             /* 114 B — F8: reload old_turn.sv[ab] */
int rl_4b29be(int hwnd, int msg, int wparam, int lparam);   /* 2962 B, coverage 59 — __stdcall (the window procedure; was a 0-argument guess) */
int rl_4b35c1(int a0, int a1);                   /* 255 B, coverage 54 */
int rl_4b3768(int a0);                           /* 84 B, coverage 1 */
int rl_4b3e51(int a0, int a1);                   /* 111 B, coverage 59 */
int rl_4b495a(int a0);                           /* 93 B, coverage 57 */
int rl_4b99c0(void);                             /* 2222 B, coverage 59 */
int rl_4ba26e(void);                             /* 1882 B, coverage 59 */
int rl_4ba9c8(void);                             /* 3832 B, coverage 59 */
int rl_4bb8c0(void);                             /* 184 B, coverage 59 */
int rl_4bb978(void);                             /* 619 B, coverage 59 */
int rl_4bbbe3(void);                             /* 157 B, coverage 45 */
int rl_4bbc80(void);                             /* 465 B, coverage 59 */
int rl_4bc020(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10); /* 231 B, coverage 17 */
