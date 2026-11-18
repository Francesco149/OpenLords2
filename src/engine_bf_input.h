/* engine_bf_input.h — the battlefield port, partition 'input' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names bf_<va>; a function lives in engine_bf_input.c once ported, else in engine_bf_input_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int bf_4238b8(void);                             /* 285 B, coverage 10 */
int bf_4239d5(void);                             /* 378 B, coverage 10 */
int bf_43b9a1(void);                             /* 136 B, coverage 10 */
int bf_43ba29(void);                             /* 208 B, coverage 5 */
int bf_43bb68(void);                             /* 111 B, coverage 5 */
int bf_43bbd7(int a0, int a1);                   /* 16 B, coverage 5 */
int bf_43bd02(void);                             /* 101 B, coverage 10 */
int bf_43bd67(void);                             /* 102 B, coverage 4 */
int bf_43bdcd(void);                             /* 152 B, coverage 4 */
int bf_43bf07(void);                             /* 832 B, coverage 10 */
int bf_43c247(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7); /* 98 B, coverage 10 */
int bf_43c2a9(void);                             /* 541 B, coverage 10 */
int bf_43c4c6(int a0);                           /* 150 B, coverage 0 */
int bf_43c55c(int a0);                           /* 33 B, coverage 10 */
int bf_43c57d(void);                             /* 183 B, coverage 10 */
int bf_43c634(int a0, int a1);                   /* 326 B, coverage 0 */
int bf_4784ca(void);                             /* 730 B, coverage 10 */
int bf_478987(int a0);                           /* 1412 B, coverage 10 */
int bf_478f0b(int a0);                           /* 392 B, coverage 10 */
int bf_47fe0b(int p);                            /* 156 B — a resigning player's battlefield units die */
int bf_479a71(int a0);                           /* 231 B, coverage 10 */
int bf_479b58(int a0, int a1, int a2, int a3, int a4, int a5, int a6); /* 415 B, coverage 10 */
int bf_479cf7(void);                             /* 277 B, coverage 10 */
int bf_47a76d(int a0);                           /* 167 B, coverage 10 */
int bf_47f76d(void);                             /* 458 B, coverage 10 */
int bf_499cb1(int a0);                           /* 204 B, coverage 10 */
