/* engine_bf_strike.h — the battlefield port, partition 'strike' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names bf_<va>; a function lives in engine_bf_strike.c once ported, else in engine_bf_strike_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int bf_483337(void);                             /* 1873 B, coverage 7 */
int bf_426120(int a0);                           /* 246 B, coverage 45 */
int bf_4262cf(int a0);                           /* 28 B, coverage 10 */
int bf_46e767(int a0, int a1, int a2, int a3, int a4); /* 531 B, coverage 7 */
int bf_493cb9(int a0, int a1, int a2, int a3, int a4); /* 956 B, coverage 7 */
int bf_46efbe(int a0);                           /* 235 B, coverage 7 */
int bf_46f525(int a0, int a1, int a2, int a3);   /* 361 B, coverage 7 */
int bf_492c8b(void);                             /* 3798 B, coverage 7 */
int bf_46eb78(int a0);                           /* 54 B, coverage 10 */
int bf_47dfe0(int a0);                           /* 508 B, coverage 0 */
int bf_47da54(int a0, int a1, int a2);           /* 584 B, coverage 0 */
int bf_48551d(int a0, int a1);                   /* 344 B, coverage 0 */
int bf_485675(int a0, int a1, int a2);           /* 492 B, coverage 1 */
int bf_485b47(int a0, int a1);                   /* 106 B, coverage 0 */
int bf_496f72(int a0, int a1, int a2, int a3);   /* 178 B, coverage 1 */
int bf_485861(int a0, int a1);                   /* 388 B, coverage 0 */
int bf_493b61(int a0);                           /* 344 B, coverage 7 */
int bf_494075(void);                             /* 176 B, coverage 7 */
int bf_494125(void);                             /* 160 B, coverage 7 */
int bf_4941c5(void);                             /* 160 B, coverage 7 */
int bf_494265(void);                             /* 821 B, coverage 7 */
int bf_4956cc(int a0, int a1, int a2);           /* 650 B, coverage 7 */
