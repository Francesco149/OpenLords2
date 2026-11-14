/* rl_mpnet.h — a retail-layout port, partition 'mpnet' (tools/bf/parts.py; docs/findings/battlefield-sim-port.md).
 * Canonical names rl_<va>; a function lives in rl_mpnet.c once ported, else in rl_mpnet_pending.c
 * (a retail stub). Signatures start at retail's arity with an int return; the owner may refine them. */
#pragma once
#include <stdint.h>
/* the SIGS session's methods (0x4b61b0..0x4b6e8e) are __thiscall in retail: the session is `self` here */

int rl_41f3e9(void);                             /* 184 B, coverage 3 */
int rl_41f4a1(void);                             /* 241 B, coverage 3 */
int rl_41f77a(int a0);                           /* 243 B, coverage 6 */
int rl_43306a(void);                             /* 92 B, coverage 3 */
int rl_433551(void);                             /* 159 B, coverage 3 */
int rl_46f2f1(void);                             /* 220 B, coverage 4 */
int rl_475e07(void);                             /* 1665 B, coverage 4 */
int rl_475e07_r(int kind_in);                    /* … with its stack residue (quirk #240) */
extern int rl_475e07_res_set, rl_475e07_res_kind, rl_475e07_res_who;   /* what it leaves at e0 - 12 / e0 - 8 */
extern int rl_475e07_res_in;                    /* the port's model of retail's residue in its [ebp-8] (quirk #240) */
int rl_47f937(void);                             /* 714 B, coverage 1 */
int rl_49b973(int fresh);                           /* 374 B, coverage 4 */
int rl_49ca60(int seat, int who);                   /* 74 B, coverage 3 */
int rl_4b3d5d(int src, int dst, int n);           /* 112 B, coverage 2 */
int rl_4b61b0(uint32_t self);                             /* 314 B, coverage 6 */
int rl_4b62ea(uint32_t self, int hwnd, int dir, int exe, int game);                             /* 230 B, coverage 6 */
int rl_4b63d0(uint32_t self);                             /* 211 B, coverage 6 */
int rl_4b64d8(uint32_t self);                             /* 636 B, coverage 6 */
int rl_4b6754(uint32_t self);                             /* 705 B, coverage 6 */
int rl_4b6ae5(uint32_t self);                             /* 140 B, coverage 6 */
int rl_4b6b71(uint32_t self, int buf, unsigned size, int pfrom);                             /* 232 B, coverage 6 */
int rl_4b6c59(uint32_t self, int buf, int len);                             /* 41 B, coverage 6 */
int rl_4b6c82(uint32_t self, int player, int buf, int len);                             /* 45 B, coverage 3 */
int rl_4b6ccb(uint32_t self);                             /* 35 B, coverage 6 */
int rl_4b6cee(uint32_t self, int cb);                             /* 43 B, coverage 6 */
int rl_4b6d19(uint32_t self, int cb);                             /* 43 B, coverage 6 */
int rl_4b6d44(uint32_t self);                             /* 129 B, coverage 6 */
int rl_4b6dc5(uint32_t self);                             /* 33 B, coverage 6 */
int rl_4b6de6(uint32_t self);                             /* 53 B, coverage 6 */
int rl_4b6e1b(uint32_t self);                             /* 28 B, coverage 6 */
int rl_4b6e8e(uint32_t self, int name, int dst, unsigned count);                             /* 105 B, coverage 6 */
int rl_4b7052(void);                             /* 288 B, coverage 6 */
int rl_4b7514(void);                             /* 87 B, coverage 3 */
int rl_4b7a9a(void);                             /* 1246 B, coverage 6 */
int rl_4b7f78(int hdlg, unsigned msg, unsigned wp, int lp);                             /* 715 B, coverage 6 */
int rl_4b90fb(int to, int slot, int len, int reliable);           /* 414 B, coverage 6 */
int rl_4bbf89(void);                             /* 73 B, coverage 3 */
int rl_4bbfd2(void);                             /* 73 B, coverage 3 */
