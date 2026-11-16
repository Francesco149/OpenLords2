/* rl_ui4.h — a retail-layout port, partition 'ui4' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names rl_<va>; a function lives in rl_ui4.c once ported, else in rl_ui4_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int rl_401000(int a0, int a1, int a2);           /* 133 B, coverage 59 */
int rl_401085(int a0, int a1, int a2);           /* 85 B, coverage 59 */
int rl_4010da(int a0, int a1, int a2);           /* 92 B, coverage 59 */
int rl_401136(int a0, int a1, int a2);           /* 64 B, coverage 59 */
int rl_4011b0(int a0);                           /* 58 B, coverage 2 */
int rl_4011ea(int a0);                           /* 92 B, coverage 59 */
int rl_401246(int a0);                           /* 92 B, coverage 6 */
int rl_4012a2(int a0);                           /* 97 B, coverage 59 */
int rl_401303(int a0, int a1);                   /* 48 B, coverage 3 */
int rl_401333(int a0, int a1);                   /* 49 B, coverage 1 */
int rl_4013bc(int a0);                           /* 89 B, coverage 4 */
int rl_4014f0(int a0, int a1);                   /* 201 B, coverage 59 */
int rl_4015b9(int a0, int a1);                   /* 173 B, coverage 57 */
int rl_401666(int a0);                           /* 141 B, coverage 4 */
int rl_4017bf(int a0, int a1, int a2, int a3);   /* 187 B, coverage 59 */
int rl_40187a(int a0);                           /* 93 B, coverage 59 */
int rl_4018d7(int a0, int a1);                   /* 173 B, coverage 59 */
int rl_401984(void);                             /* 156 B, coverage 4 */
int rl_401a20(int a0);                           /* 543 B, coverage 4 */
int rl_401c70(void);                             /* 35 B, coverage 3 */
int rl_401c93(void);                             /* 16 B, coverage 1 */
int rl_401ca3(void);                             /* 25 B, coverage 1 */
int rl_401cbc(void);                             /* 30 B, coverage 1 */
int rl_401cda(void);                             /* 34 B, coverage 1 */
int rl_401cfc(void);                             /* 21 B, coverage 2 */
int rl_401d11(void);                             /* 21 B, coverage 4 */
int rl_401d26(int a0);                           /* 162 B, coverage 4 */
int rl_401dc8(void);                             /* 64 B, coverage 3 */
int rl_401f49(void);                             /* 70 B, coverage 57 */
int rl_401f8f(int a0);                           /* 122 B, coverage 4 */
int rl_402009(int a0, int a1, int a2, int a3);   /* 165 B, coverage 57 */
int rl_4020ae(void);                             /* 94 B, coverage 54 */
int rl_40210c(int a0, int a1);                   /* 90 B, coverage 57 */
int rl_4036f9(int a0, int a1);                   /* 245 B, coverage 57 */
int rl_4037ee(int a0, int a1, int a2, int a3);   /* 347 B, coverage 59 */
int rl_40489e(void);                             /* 111 B, coverage 17 */
int rl_40490d(void);                             /* 313 B, coverage 57 */
int rl_404b2c(void);                             /* 109 B, coverage 57 */
int rl_404d6b(int a0, int a1);                   /* 43 B, coverage 59 */
int rl_404d96(int a0, int a1);                   /* 43 B, coverage 54 */
int rl_404dc1(int a0, int a1);                   /* 66 B, coverage 59 */
int rl_404e03(int a0, int a1);                   /* 72 B, coverage 54 */
int rl_404e4b(int a0, int a1, int a2, int a3);   /* 98 B, coverage 57 */
int rl_404ead(int a0, int a1, int a2, int a3);   /* 159 B, coverage 56 */
int rl_404f4c(int a0, int a1, int a2, int a3);   /* 180 B, coverage 57 */
int rl_408fcb(int a0, int a1);                   /* 96 B, coverage 59 */
int rl_40ccfa(void);                             /* 94 B, coverage 23 */
int rl_40da1e(int a0, int a1, int a2, int a3);   /* 884 B, coverage 53 */
int rl_40e3ee(int a0, int a1, int a2, int a3);   /* 505 B, coverage 59 */
int rl_40e5e7(int a0, int a1, int a2, int a3);   /* 153 B, coverage 59 */
int rl_40e6f2(int a0, int a1, int a2, int a3);   /* 111 B, coverage 23 */
int rl_40e7e4(void);                             /* 87 B, coverage 51 */
int rl_40e890(void);                             /* 43 B, coverage 59 */
int rl_40e8bb(void);                             /* 240 B, coverage 59 */
int rl_40ecc4(void);                             /* 1099 B, coverage 59 */
int rl_40f1a0(int a0);                           /* 1117 B, coverage 59 */
int rl_40f5fd(int a0);                           /* 470 B, coverage 54 */
int rl_410a5d(int a0, int a1, int a2);           /* 76 B, coverage 1 */
int rl_412b33(void);                             /* 824 B, coverage 2 */
int rl_412e6b(void);                             /* 746 B, coverage 1 */
int rl_413155(void);                             /* 977 B, coverage 1 */
int rl_413590(void);                             /* 1440 B, coverage 1 */
int rl_413b30(void);                             /* 1475 B, coverage 2 */
int rl_4140f3(void);                             /* 301 B, coverage 1 */
int rl_414220(void);                             /* 31 B, coverage 1 */
int rl_41593b(int a0);                           /* 673 B, coverage 1 */
int rl_415bdc(void);                             /* 614 B, coverage 1 */
int rl_415e42(int a0);                           /* 373 B, coverage 1 */
int rl_415fb7(int a0);                           /* 212 B, coverage 38 */
int rl_41789b(void);                             /* 197 B, coverage 39 */
int rl_417960(void);                             /* 399 B, coverage 37 */
int rl_4180f6(void);                             /* 245 B, coverage 1 */
int rl_4184c6(void);                             /* 397 B, coverage 2 */
int rl_418e2d(void);                             /* 408 B, coverage 2 */
int rl_419c78(void);                             /* 1262 B, coverage 59 */
int rl_41ad5d(void);                             /* 325 B, coverage 1 */
int rl_41aea2(void);                             /* 369 B, coverage 1 */
int rl_41e61d(void);                             /* 452 B, coverage 59 */
int rl_41fdd6(void);                             /* 415 B, coverage 1 */
int rl_425b10(void);                             /* 549 B, coverage 59 */
int rl_425ded(void);                             /* 94 B, coverage 59 */
int rl_426d11(int a0);                           /* 196 B, coverage 59 */
int rl_427d19(void);                             /* 89 B, coverage 59 */
int rl_427ed0(void);                             /* 311 B, coverage 54 */
int rl_428471(void);                             /* 11 B, coverage 54 */
int rl_42847c(void);                             /* 82 B, coverage 54 */
int rl_4290d0(void);                             /* 131 B, coverage 59 */
int rl_429153(int a0, int a1);                   /* 96 B, coverage 59 */
int rl_4291b3(void);                             /* 171 B, coverage 54 */
int rl_42925e(int a0);                           /* 81 B, coverage 54 */
int rl_4298c1(int a0);                           /* 604 B, coverage 59 */
int rl_429b1d(void);                             /* 135 B, coverage 59 */
int rl_429ba4(int a0);                           /* 878 B, coverage 59 */
int rl_42a15f(void);                             /* 206 B, coverage 59 */
int rl_42e5c0(void);                             /* 1254 B, coverage 59 */
int rl_42eb7c(void);                             /* 277 B, coverage 59 */
int rl_42ec91(void);                             /* 1207 B, coverage 59 */
int rl_42f1df(int a0);                           /* 295 B, coverage 59 */
int rl_42f306(void);                             /* 323 B, coverage 59 */
/* the standalone's last seams (2026-11-16) */
int rl_40902b(int name, int pal);
int rl_41e5d0(void);
int rl_420428(void);
int rl_4247f5(void);
int rl_42563c(void);
int rl_425a6a(void);
int rl_427d72(int hr);
int rl_42f60e(int hr);
int rl_428078(int k);
int rl_42e3ba(int hwnd);
int rl_42e45e(int x, int y, int w, int h);
int rl_42e50d(void);
int rl_42f449(void);
int rl_43b101(void);
int rl_43baf9(void);
int rl_43e668(int a1);
int rl_49bb9d(int p);
int rl_4b2234(void);
int rl_4b26b8(void);
int rl_4b2730(void);
int rl_4b74ce(void);
