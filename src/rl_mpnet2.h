/* rl_mpnet2.h — a retail-layout port, partition 'mpnet2' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names rl_<va>; a function lives in rl_mpnet2.c once ported, else in rl_mpnet2_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once
#include <stdint.h>

int rl_40ec2c(void);                             /* 152 B, coverage 1 */
int rl_420630(int a0);                           /* 403 B, coverage 2 */
int rl_42d40b(void);                             /* 159 B, coverage 2 */
int rl_43338e(void);                             /* 211 B, coverage 1 */
int rl_43695d(void);                             /* 96 B, coverage 1 */
int rl_476488(void);                             /* 648 B, coverage 1 */
int rl_479e0c(int owner);                           /* 132 B, coverage 7 */
int rl_49792e(void);                             /* 262 B, coverage 6 */
int rl_4b64a3(uint32_t self);                             /* 53 B, coverage 1 */
int rl_4b6a15(uint32_t self);                             /* 161 B, coverage 1 */
int rl_4b6ab6(uint32_t self);                             /* 47 B, coverage 2 */
int rl_4b6ef7(uint32_t self, int ph);                             /* 320 B, coverage 1 */
int rl_4b743b(void);                             /* 147 B, coverage 2 */
int rl_4b756b(void);                             /* 26 B, coverage 4 */
int rl_4b79e3(int text, int name);                   /* 153 B, coverage 1 */
int rl_4b7a7c(int player);                           /* 30 B, coverage 1 */
int rl_4b8243(int guid);                           /* 124 B, coverage 2 */
int rl_4b82bf(int guid, int name, int major, int minor, int lbox);                             /* 118 B, coverage 2 */
int rl_4b8335(int hdlg, unsigned msg, unsigned wp, int lp);                             /* 260 B, coverage 2 */
int rl_4b8439(int hdlg, unsigned msg, unsigned wp, int lp);                             /* 382 B, coverage 1 */
int rl_4b85b7(int hdlg, unsigned msg, unsigned wp, int lp);                             /* 1488 B, coverage 1 */
int rl_4b8b87(int sd, int timeout, int flags, int lbox);                             /* 179 B, coverage 1 */
int rl_4b92a0(uint32_t self, int flags);                             /* 57 B, coverage 1 */
