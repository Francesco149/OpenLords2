/* rh_hooks_bf_action.c — the battlefield port, partition 'action' (src/engine_bf_action.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). docs/findings/bf-action.md. */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_ACTION
extern hook_t RH_HOOKS_BF_ACTION[];
/* row indices (must follow the table's order) */
enum {
    X_bf_482f86, X_bf_482f91, X_bf_4830e9, X_bf_48314e, X_bf_4831d8, X_bf_4832ea, X_bf_483a88, X_bf_483ce1,
    X_bf_483e55, X_bf_483fe1, X_bf_4842c6, X_bf_48437e, X_bf_4843bc, X_bf_4849fc, X_bf_484a0c, X_bf_484b89,
    X_bf_484bee, X_bf_484bf9,
    X_bf_494908, X_bf_47a814, X_bf_4954dd, X_bf_49694f, X_bf_471da6, X_bf_494e3f, X_bf_47dd86, X_bf_4926fb,
    X_bf_4927fa, X_bf_496768, X_bf_405000, X_bf_479e90, X_bf_46e97a, X_bf_46ebae, X_bf_48143b, X_bf_48a7d9,
    X_bf_48a672, X_bf_497437,
};

/* STACK RESIDUE (src/engine_bf_action.c, "the siege orders' stack residue"): the six siege-order handlers feed
 * the sequencers' unset local from two slots below their entry esp e0 — slot 1 at e0 - 0x1c - 4 × the handler's
 * locals (0x1c; 0x20 for 0x484a0c, 0x28 for 0x4843bc), slot 2 four bytes lower — and a 1-argument call leaves the
 * handler's ebp (e0 - 4) in slot 1. A SEQ row's body notes e0 (b_ → s_e0) for the adapter: slot 1's final value
 * is put back (replace: written; shadow: compared). */
extern uint32_t bf_action_res[2], bf_action_res_ebp;
static uint32_t s_e0;
#define HOOK_BODY_E0(NAME) \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { uint32_t o = s_e0, r; s_e0 = e0; r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 0, e0); \
      s_e0 = o; return r; } \
    RH_ENTRY(w_##NAME, b_##NAME)
#define HOOK_SEQ(NAME, OFF) \
    static uint32_t p_##NAME(const uint32_t *a) \
    { (void)a; bf_action_res_ebp = s_e0 - 4; bf_action_res[0] = rh_res_get(OFF); bf_action_res[1] = rh_res_get((OFF) + 4); \
      NAME(); rh_res_put(OFF, bf_action_res[0]); return 0; } \
    HOOK_BODY_E0(NAME)

/* the order handlers (DAT_004d9170, unit +0x31 = 0..17): the movement-state handlers return their EAX,
 * FUN_004822ed drops it */
HOOK_V(bf_482f86, 0, bf_482f86())
HOOK_V(bf_482f91, 0, bf_482f91())
HOOK_V(bf_4830e9, 0, bf_4830e9())
HOOK_V(bf_48314e, 0, bf_48314e())
HOOK_V(bf_4831d8, 0, bf_4831d8())
HOOK_V(bf_4832ea, 0, bf_4832ea())
HOOK_V(bf_483a88, 0, bf_483a88())
HOOK_V(bf_483ce1, 0, bf_483ce1())
HOOK_V(bf_483e55, 0, bf_483e55())
HOOK_V(bf_483fe1, 0, bf_483fe1())
HOOK_SEQ(bf_4842c6, 0x1c)
HOOK_SEQ(bf_48437e, 0x1c)
HOOK_SEQ(bf_4843bc, 0x28)
HOOK_SEQ(bf_4849fc, 0x1c)
HOOK_SEQ(bf_484a0c, 0x20)
HOOK_SEQ(bf_484b89, 0x1c)
HOOK_V(bf_484bee, 0, bf_484bee())
HOOK_V(bf_484bf9, 0, bf_484bf9())
/* helpers (a value-returning helper is HOOK_R even where no caller reads it: the port models retail's EAX) */
HOOK_V(bf_494908, 0, bf_494908())
HOOK_R(bf_47a814, 3, bf_47a814(I(0), I(1), I(2)))
HOOK_R(bf_4954dd, 0, bf_4954dd())
HOOK_R(bf_49694f, 3, bf_49694f(I(0), I(1), I(2)))
HOOK_R(bf_471da6, 0, bf_471da6())
HOOK_R(bf_494e3f, 0, bf_494e3f())
HOOK_R(bf_47dd86, 1, bf_47dd86(I(0)))
HOOK_R(bf_4926fb, 1, bf_4926fb(I(0)))
HOOK_R(bf_4927fa, 1, bf_4927fa(I(0)))
HOOK_R(bf_496768, 3, bf_496768(I(0), I(1), I(2)))
HOOK_R(bf_405000, 4, bf_405000(I(0), I(1), I(2), I(3)))
/* FUN_00479e90: void to IDA; no call site reads its EAX (the input partition's 0x478987 passes it on, a HOOK_V
 * row). Its modelled EAX includes 0x488dfe's, which the group port does not model (retail: 0x410) — HOOK_V. */
HOOK_V(bf_479e90, 6, bf_479e90(I(0), I(1), I(2), I(3), I(4), I(5)))
HOOK_R(bf_46e97a, 2, bf_46e97a(I(0), I(1)))
HOOK_R(bf_46ebae, 1, bf_46ebae(I(0)))
HOOK_R(bf_48143b, 1, bf_48143b(I(0)))
HOOK_R(bf_48a7d9, 5, bf_48a7d9(I(0), I(1), I(2), I(3), I(4)))
HOOK_R(bf_48a672, 4, bf_48a672(I(0), I(1), I(2), I(3)))
HOOK_R(bf_497437, 5, bf_497437(I(0), I(1), I(2), I(3), I(4)))

hook_t RH_HOOKS_BF_ACTION[] = {
    { "bf_482f86", 0x482f86u, (void *)w_bf_482f86, 0, 0, 0, 0, "" },
    { "bf_482f91", 0x482f91u, (void *)w_bf_482f91, 0, 0, 0, 0, "" },
    { "bf_4830e9", 0x4830e9u, (void *)w_bf_4830e9, 0, 0, 0, 0, "" },
    { "bf_48314e", 0x48314eu, (void *)w_bf_48314e, 0, 0, 0, 0, "" },
    { "bf_4831d8", 0x4831d8u, (void *)w_bf_4831d8, 0, 0, 0, 0, "" },
    { "bf_4832ea", 0x4832eau, (void *)w_bf_4832ea, 0, 0, 0, 0, "" },
    { "bf_483a88", 0x483a88u, (void *)w_bf_483a88, 0, 0, 0, 0, "" },
    { "bf_483ce1", 0x483ce1u, (void *)w_bf_483ce1, 0, 0, 0, 0, "" },
    { "bf_483e55", 0x483e55u, (void *)w_bf_483e55, 0, 0, 0, 0, "" },
    { "bf_483fe1", 0x483fe1u, (void *)w_bf_483fe1, 0, 0, 0, 0, "" },
    { "bf_4842c6", 0x4842c6u, (void *)w_bf_4842c6, 0, 0, 0, 0, "" },
    { "bf_48437e", 0x48437eu, (void *)w_bf_48437e, 0, 0, 0, 0, "" },
    { "bf_4843bc", 0x4843bcu, (void *)w_bf_4843bc, 0, 0, 0, 0, "" },
    { "bf_4849fc", 0x4849fcu, (void *)w_bf_4849fc, 0, 0, 0, 0, "" },
    { "bf_484a0c", 0x484a0cu, (void *)w_bf_484a0c, 0, 0, 0, 0, "" },
    { "bf_484b89", 0x484b89u, (void *)w_bf_484b89, 0, 0, 0, 0, "" },
    { "bf_484bee", 0x484beeu, (void *)w_bf_484bee, 0, 0, 0, 0, "" },
    { "bf_484bf9", 0x484bf9u, (void *)w_bf_484bf9, 0, 0, 0, 0, "" },
    { "bf_494908", 0x494908u, (void *)w_bf_494908, 0, 0, 0, 0, "" },
    { "bf_47a814", 0x47a814u, (void *)w_bf_47a814, 0, 0, 0, 0, "" },
    { "bf_4954dd", 0x4954ddu, (void *)w_bf_4954dd, 0, 0, 0, 0, "" },
    { "bf_49694f", 0x49694fu, (void *)w_bf_49694f, 0, 0, 0, 0, "" },
    { "bf_471da6", 0x471da6u, (void *)w_bf_471da6, 0, 0, 0, 0, "" },
    { "bf_494e3f", 0x494e3fu, (void *)w_bf_494e3f, 0, 0, 0, 0, "" },
    { "bf_47dd86", 0x47dd86u, (void *)w_bf_47dd86, 0, 0, 0, 0, "" },
    { "bf_4926fb", 0x4926fbu, (void *)w_bf_4926fb, 0, 0, 0, 0, "" },
    { "bf_4927fa", 0x4927fau, (void *)w_bf_4927fa, 0, 0, 0, 0, "" },
    { "bf_496768", 0x496768u, (void *)w_bf_496768, 0, 0, 0, 0, "" },
    { "bf_405000", 0x405000u, (void *)w_bf_405000, 0, 0, 0, 0, "" },
    { "bf_479e90", 0x479e90u, (void *)w_bf_479e90, 0, 0, 0, 0, "" },
    { "bf_46e97a", 0x46e97au, (void *)w_bf_46e97a, 0, 0, 0, 0, "" },
    { "bf_46ebae", 0x46ebaeu, (void *)w_bf_46ebae, 0, 0, 0, 0, "" },
    { "bf_48143b", 0x48143bu, (void *)w_bf_48143b, 0, 0, 0, 0, "" },
    { "bf_48a7d9", 0x48a7d9u, (void *)w_bf_48a7d9, 0, 0, 0, 0, "" },
    { "bf_48a672", 0x48a672u, (void *)w_bf_48a672, 0, 0, 0, 0, "" },
    { "bf_497437", 0x497437u, (void *)w_bf_497437, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_ACTION_N = sizeof RH_HOOKS_BF_ACTION / sizeof *RH_HOOKS_BF_ACTION;
