/* engine_bf_action.h — the battlefield port, partition 'action' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names bf_<va>; a function lives in engine_bf_action.c once ported, else in engine_bf_action_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int bf_482f86(void);                             /* 11 B, coverage 0 */
int bf_482f91(void);                             /* 344 B, coverage 10 */
int bf_4830e9(void);                             /* 101 B, coverage 10 */
int bf_48314e(void);                             /* 138 B, coverage 10 */
int bf_4831d8(void);                             /* 274 B, coverage 10 */
int bf_4832ea(void);                             /* 77 B, coverage 10 */
int bf_483a88(void);                             /* 601 B, coverage 1 */
int bf_483ce1(void);                             /* 372 B, coverage 0 */
int bf_483e55(void);                             /* 396 B, coverage 10 */
int bf_483fe1(void);                             /* 741 B, coverage 0 */
int bf_4842c6(void);                             /* 184 B, coverage 1 */
int bf_48437e(void);                             /* 62 B, coverage 1 */
int bf_4843bc(void);                             /* 1600 B, coverage 0 */
int bf_4849fc(void);                             /* 16 B, coverage 0 */
int bf_484a0c(void);                             /* 381 B, coverage 0 */
int bf_484b89(void);                             /* 101 B, coverage 0 */
int bf_484bee(void);                             /* 11 B, coverage 0 */
int bf_484bf9(void);                             /* 2340 B, coverage 1 */
int bf_494908(void);                             /* 1024 B, coverage 10 */
int bf_47a814(int a0, int a1, int a2);           /* 655 B, coverage 1 */
int bf_4954dd(void);                             /* 495 B, coverage 10 */
int bf_49694f(int a0, int a1, int a2);           /* 592 B, coverage 0 */
int bf_471da6(void);                             /* 415 B, coverage 10 */
int bf_494e3f(void);                             /* 1694 B, coverage 0 */
int bf_47dd86(int a0);                           /* 602 B, coverage 0 */
int bf_4926fb(int a0);                           /* 255 B, coverage 0 */
int bf_4927fa(int a0);                           /* 169 B, coverage 0 */
int bf_496768(int a0, int a1, int a2);           /* 487 B, coverage 0 */
int bf_405000(int a0, int a1, int a2, int a3);   /* 180 B, coverage 0 */
int bf_479e90(int a0, int a1, int a2, int a3, int a4, int a5); /* 2269 B, coverage 10 */
int bf_46e97a(int a0, int a1);                   /* 174 B, coverage 10 */
int bf_46ebae(int a0);                           /* 54 B, coverage 10 */
int bf_48143b(int a0);                           /* 611 B, coverage 0 */
int bf_48a7d9(int a0, int a1, int a2, int a3, int a4); /* 494 B, coverage 0 */
int bf_48a672(int a0, int a1, int a2, int a3);   /* 359 B, coverage 10 */
int bf_497437(int a0, int a1, int a2, int a3, int a4); /* 191 B, coverage 0 */
