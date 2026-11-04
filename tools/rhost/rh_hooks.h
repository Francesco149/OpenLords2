/* rh_hooks.h — the hook registry shared by rh_hooks.c (core rows) and rh_hooks_<subsystem>.c files.
 *
 * A subsystem file defines `hook_t RH_HOOKS_<SUB>[] = { { "name", 0xVAu, (void *)wrapper, 0, 0, 0, 0, "" }, ... };`
 * and `const int RH_HOOKS_<SUB>_N = sizeof … / sizeof …;`, and is added to RH_HOOK_TABLES below and to
 * build.sh. A wrapper replaces retail's call sites of VA — or, for a function no call site reaches (a pointer
 * table), its entry (rh_patch_entry; the retail side then calls hook_t.entry) — (same calling convention and arguments) and, per
 * the hook's mode, runs retail and the port under rh_shadow_ex (shadow) or only the port (replace). rhost.py
 * derives VERIFY_HOOKS from every table (all rows are verified). See rh_hooks.c `w_rng` for the pattern. */
#pragma once
#include "rhost.h"

enum { H_OFF, H_SHADOW, H_REPLACE };
typedef struct {
    const char *name; uint32_t va; void *wrapper;
    int mode; long calls, diverge, skipped; char first[400];
    uint32_t entry;             /* where the retail side calls: va, or the trampoline when the entry itself is patched */
    int flags;                  /* RH_SH_MEDIA (rhost.h): the shadow also compares the surfaces' pixels (a render row);
                                   RH_SH_SAMPLE: a hot leaf — only a sample of its calls is shadowed (rh_hook_sample) */
    long sampled;               /* RH_SH_SAMPLE: calls not shadowed (retail ran; not in `calls`) */
} hook_t;

void rh_hook_count(hook_t *h, int r, const char *why);   /* tally one rh_shadow_ex result */
int  rh_hook_sample(hook_t *h, uint32_t ret);              /* shadow this call? (1 unless an RH_SH_SAMPLE row passes it) */

/* X-macro: one entry per subsystem table (T(RH_HOOKS_<SUB>)) */
#define RH_HOOK_TABLES(T) T(RH_HOOKS_AI) T(RH_HOOKS_COMBAT) T(RH_HOOKS_STATE) T(RH_HOOKS_EVENTS) T(RH_HOOKS_BATTLE) \
                             T(RH_HOOKS_ECONOMY) \
                             T(RH_HOOKS_BF_MOVE) T(RH_HOOKS_BF_STRIKE) T(RH_HOOKS_BF_UNIT) T(RH_HOOKS_BF_ACTION) \
                             T(RH_HOOKS_BF_GROUP) T(RH_HOOKS_BF_STANCE) T(RH_HOOKS_BF_SETUP) T(RH_HOOKS_BF_INPUT) \
                             T(RH_HOOKS_RL_IO) T(RH_HOOKS_RL_GLUE) T(RH_HOOKS_RL_RENDER) T(RH_HOOKS_RL_MISC) \
                             T(RH_HOOKS_TILE) \
                             T(RH_HOOKS_GFX) \
                             T(RH_HOOKS_MAPVIEW)
