/* engine_bf_extern.h — the battlefield port, partition 'extern' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names bf_<va>; a function lives in engine_bf_extern.c once ported, else in engine_bf_extern_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once

int bf_43f304(void);                             /* 78 B, coverage 10 */
int bf_477b2f(void);                             /* 346 B, coverage 10 */
int bf_4987b7(void);                             /* 1556 B, coverage 10 */
int bf_499d97(void);                             /* 26 B, coverage 10 */
