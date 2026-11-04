/* rl_io.h — a retail-layout port, partition 'io' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names rl_<va>; a function lives in rl_io.c once ported, else in rl_io_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int rl_41b013(void);                             /* 31 B, coverage 54 */
int rl_425e4b(int a0, int a1);                   /* 725 B, coverage 52 */
int rl_426216(int a0);                           /* 185 B, coverage 48 */
int rl_4262eb(int a0);                           /* 117 B, coverage 52 */
int rl_426360(int a0);                           /* 77 B, coverage 42 */
int rl_4263ad(int a0, int a1, int a2);           /* 255 B, coverage 54 */
int rl_426529(void);                             /* 11 B, coverage 54 */
int rl_426534(int a0, int a1);                   /* 1630 B, coverage 54 */
int rl_426b92(int a0);                           /* 383 B, coverage 54 */
int rl_426dd5(int a0, int a1, int a2, int a3, int a4); /* 3003 B, coverage 54 — a __stdcall TIMECALLBACK in retail (id, msg, dwUser, dw1, dw2): the generated 0-arg signature was wrong; no code calls it (timeSetEvent does) */
int rl_427990(int a0, int a1, int a2);           /* 779 B, coverage 54 */
int rl_427c9b(void);                             /* 126 B, coverage 54 */
int rl_42d91b(int a0, int a1, int a2, int a3, int a4); /* 83 B, coverage 54 */
int rl_42da18(int a0, int a1, int a2, int a3);   /* 431 B, coverage 54 */
int rl_42dbc7(int a0);                           /* 873 B, coverage 54 */
int rl_42e298(void);                             /* 136 B, coverage 6 */
int rl_42e320(void);                             /* 66 B, coverage 54 */
int rl_42e362(void);                             /* 31 B, coverage 6 */
int rl_42f58c(int a0, int a1);                   /* 130 B, coverage 54 */
int rl_433cc1(void);                             /* 35 B, coverage 54 */
int rl_451f70(void);                             /* 26 B, coverage 54 */
int rl_45280b(void);                             /* 11 B, coverage 54 */
int rl_46a101(void);                             /* 165 B, coverage 54 */
int rl_46ea9a(int a0);                           /* 54 B, coverage 54 */
int rl_46f21f(void);                             /* 72 B, coverage 54 */
int rl_46f267(void);                             /* 71 B, coverage 54 */
int rl_497500(void);                             /* 211 B, coverage 54 */
int rl_4975d3(void);                             /* 170 B, coverage 54 */
int rl_4978ad(void);                             /* 129 B, coverage 54 */
int rl_497a34(int a0);                           /* 697 B, coverage 54 */
int rl_4983b7(void);                             /* 161 B, coverage 54 */
int rl_4990f9(void);                             /* 101 B, coverage 54 */
int rl_49915e(void);                             /* 259 B, coverage 54 */
int rl_499261(void);                             /* 261 B, coverage 54 */
int rl_499366(void);                             /* 691 B, coverage 54 */
int rl_499619(void);                             /* 51 B, coverage 54 */
int rl_49964c(void);                             /* 124 B, coverage 54 */
int rl_4996c8(void);                             /* 247 B, coverage 47 */
int rl_499aca(void);                             /* 487 B, coverage 52 */
int rl_499d7d(void);                             /* 26 B, coverage 52 */
int rl_49c995(void);                             /* 157 B, coverage 54 */
int rl_4ae1b8(void);                             /* 243 B, coverage 54 */
int rl_4ae2ab(int a0);                           /* 18 B, coverage 54 */
int rl_4ae2bd(void);                             /* 83 B, coverage 54 */
int rl_4aecf0(int a0, int a1);                   /* 334 B, coverage 54 */
int rl_4aee3e(void);                             /* 320 B, coverage 1 */
int rl_4af116(int a0);                           /* 508 B, coverage 54 */
int rl_4af312(int a0);                           /* 496 B, coverage 54 */
int rl_4af767(int a0);                           /* 218 B, coverage 54 */
int rl_4af841(int a0);                           /* 152 B, coverage 54 */
int rl_4afab9(int a0, int a1, int a2);           /* 897 B, coverage 54 */
int rl_4afe3a(int a0);                           /* 179 B, coverage 54 */
int rl_4b049b(void);                             /* 103 B, coverage 54 */
int rl_4b0aaa(int a0);                           /* 11 B, coverage 54 */
int rl_4b11ce(void);                             /* 34 B, coverage 54 */
int rl_4b2575(void);                             /* 59 B, coverage 54 */
int rl_4b36c0(int a0);                           /* 84 B, coverage 21 */
int rl_4b3714(int a0);                           /* 84 B, coverage 32 */
int rl_4b37bc(void);                             /* 388 B, coverage 18 */
int rl_4b3940(int a0);                           /* 84 B, coverage 21 */
int rl_4b3acd(int a0);                           /* 197 B, coverage 5 */
int rl_4b3b92(int a0);                           /* 89 B, coverage 17 */
int rl_4b8c3a(void);                             /* 1217 B, coverage 54 */
int rl_4b92e0(int a0, int a1, int a2, int a3);   /* 730 B, coverage 54 */
int rl_4b95ba(int a0, int a1, int a2);           /* 152 B, coverage 54 */
int rl_4b9652(int a0, int a1, int a2, int a3, int a4); /* 423 B, coverage 54 */
int rl_4b97f9(int a0, int a1);                   /* 86 B, coverage 54 */
int rl_4b984f(int a0, int a1, int a2, int a3);   /* 361 B, coverage 54 */
int rl_4c0580(int a0);                           /* 25 B, coverage 54 */
int rl_4c05a0(int a0, int a1);                   /* 1057 B, coverage 54 */
