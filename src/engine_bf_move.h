/* engine_bf_move.h — the battlefield port, partition 'move' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names bf_<va>; a function lives in engine_bf_move.c once ported, else in engine_bf_move_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int bf_48f1dd(int a0);                           /* 3714 B, coverage 10 */
int bf_47095e(int a0, int a1, int a2, int a3, int a4); /* 1940 B, coverage 10 */
int bf_4710f2(int a0, int a1, int a2, int a3);   /* 1574 B, coverage 10 */
int bf_471f45(void);                             /* 738 B, coverage 10 */
int bf_4b3dcd(int a0, int a1, int a2);           /* 132 B, coverage 10 */
int bf_471718(int a0, int a1, int a2, int a3);   /* 1287 B, coverage 0 */
int bf_472227(int a0);                           /* 363 B, coverage 10 */
int bf_48f079(int a0);                           /* 356 B, coverage 10 */
int bf_49005f(void);                             /* 1165 B, coverage 10 */
int bf_4928a3(int a0);                           /* 174 B, coverage 9 */
int bf_496155(int a0, int a1, int a2);           /* 374 B, coverage 10 */
int bf_4904ec(void);                             /* 298 B, coverage 10 */
int bf_490616(int a0);                           /* 1070 B, coverage 10 */
int bf_490a44(int a0);                           /* 533 B, coverage 10 */
int bf_490c59(int a0);                           /* 841 B, coverage 0 */
int bf_490fa2(int a0);                           /* 421 B, coverage 0 */
int bf_491147(int a0);                           /* 421 B, coverage 0 */
int bf_4912ec(void);                             /* 422 B, coverage 0 */
int bf_491492(int a0);                           /* 843 B, coverage 0 */
int bf_491a34(int a0);                           /* 235 B, coverage 10 */
int bf_491b1f(int a0);                           /* 1016 B, coverage 10 */
int bf_491f17(int a0, int a1);                   /* 359 B, coverage 0 */
int bf_49207e(int a0, int a1);                   /* 359 B, coverage 1 */
int bf_494d08(int a0);                           /* 241 B, coverage 10 */
int bf_494df9(int a0);                           /* 70 B, coverage 10 */
int bf_4972f9(int a0, int a1, int a2);           /* 318 B, coverage 10 */
