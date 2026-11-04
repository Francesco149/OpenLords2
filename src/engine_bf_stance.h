/* engine_bf_stance.h — the battlefield port, partition 'stance' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names bf_<va>; a function lives in engine_bf_stance.c once ported, else in engine_bf_stance_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int bf_48cba9(void);                             /* 314 B, coverage 1 */
int bf_48cce3(void);                             /* 314 B, coverage 1 */
int bf_48ce1d(void);                             /* 100 B, coverage 0 */
int bf_48ce81(void);                             /* 402 B, coverage 0 */
int bf_48d013(void);                             /* 347 B, coverage 0 */
int bf_48d16e(void);                             /* 676 B, coverage 0 */
int bf_48d412(void);                             /* 746 B, coverage 0 */
int bf_48d6fc(void);                             /* 722 B, coverage 0 */
int bf_48d9ce(void);                             /* 438 B, coverage 0 */
int bf_48db84(void);                             /* 579 B, coverage 0 */
int bf_48ddc7(void);                             /* 500 B, coverage 0 */
int bf_48dfbb(void);                             /* 220 B, coverage 0 */
int bf_48e097(void);                             /* 413 B, coverage 1 */
int bf_48e234(void);                             /* 148 B, coverage 1 */
int bf_48e2c8(void);                             /* 212 B, coverage 1 */
int bf_48e39c(void);                             /* 304 B, coverage 1 */
int bf_48e4cc(void);                             /* 680 B, coverage 1 */
int bf_48e774(void);                             /* 324 B, coverage 0 */
int bf_48e8b8(void);                             /* 477 B, coverage 1 */
int bf_48ea95(void);                             /* 348 B, coverage 1 */
int bf_48ebf1(void);                             /* 184 B, coverage 1 */
int bf_48ed95(int a0);                           /* 177 B, coverage 1 */
int bf_495956(int a0, int a1, int a2);           /* 747 B, coverage 1 */
int bf_495c41(int a0, int a1, int a2);           /* 739 B, coverage 1 */
int bf_495f3d(int a0, int a1, int a2, int a3, int a4, int a5); /* 536 B, coverage 1 */
int bf_496566(int a0, int a1, int a2, int a3);   /* 514 B, coverage 1 */
int bf_496b9f(void);                             /* 471 B, coverage 1 */
int bf_496d76(int a0, int a1, int a2);           /* 508 B, coverage 0 */
