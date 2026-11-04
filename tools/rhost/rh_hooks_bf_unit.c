/* rh_hooks_bf_unit.c — the battlefield port, partition 'unit' (src/engine_bf_unit.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself).
 *
 * EAX.  Only FUN_00481abb's return is a value (the band step); the sequencers / thunks / state handlers return
 * an incidental register that flows (through the order handlers' tail calls) to FUN_004822ed, which ignores it.
 *
 * STACK RESIDUE.  The sequencers that read an unset local (src/engine_bf_unit.c, bf_unit_res_*) take it from the
 * caller's residue at entry and put its final value back (rh_port.h: replace writes it, shadow compares it).
 * FUN_00486239 is entered 0x14 bytes above FUN_0048804a's e0 (its push ebp / ebx / esi / edi + the call), so
 * FUN_0048804a's e0-0x10 is the thunk's e0-0x24. */
#include "rh_port.h"
#include "engine_bf.h"

extern uint32_t bf_unit_res_488793, bf_unit_res_488436[2], bf_unit_res_48895e, bf_unit_res_48804a;
extern uint32_t bf_unit_e0_4822ed, bf_unit_e0_state;

#define RH_TABLE RH_HOOKS_BF_UNIT
extern hook_t RH_HOOKS_BF_UNIT[];
/* row indices (must follow the table's order) */
enum {
    X_bf_4822ed, X_bf_46ebe4, X_bf_46eb3f,
    X_bf_4825d1, X_bf_4826b0, X_bf_482789, X_bf_482862, X_bf_48293b, X_bf_482a14, X_bf_482aed, X_bf_482bc6,
    X_bf_482ca8, X_bf_482d8a, X_bf_482e6c, X_bf_481abb,
    X_bf_4861e9, X_bf_4861f9, X_bf_486209, X_bf_486219, X_bf_486229, X_bf_486239,
    X_bf_486249, X_bf_486d83, X_bf_4872ae, X_bf_487908, X_bf_487ce4, X_bf_48804a,
    X_bf_488436, X_bf_488793, X_bf_48895e, X_bf_49459a,
};

/* E0 rows: the per-unit driver and the state handlers note their retail entry esp (b_ → s_e0) for the siege
 * orders' residue model (src/engine_bf_unit.c bf_unit_e0_*: the order handler's ebp) */
static uint32_t s_e0;
#define HOOK_V_E0(NAME, SEAM) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; SEAM = s_e0; NAME(); return 0; } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { uint32_t o = s_e0, r; s_e0 = e0; r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 0, e0); \
      s_e0 = o; return r; } \
    RH_ENTRY(w_##NAME, b_##NAME)

HOOK_V_E0(bf_4822ed, bf_unit_e0_4822ed)
HOOK_R(bf_46ebe4, 1, bf_46ebe4(I(0)))
HOOK_R(bf_46eb3f, 1, bf_46eb3f(I(0)))

HOOK_V_E0(bf_4825d1, bf_unit_e0_state)
HOOK_V_E0(bf_4826b0, bf_unit_e0_state)
HOOK_V_E0(bf_482789, bf_unit_e0_state)
HOOK_V_E0(bf_482862, bf_unit_e0_state)
HOOK_V_E0(bf_48293b, bf_unit_e0_state)
HOOK_V_E0(bf_482a14, bf_unit_e0_state)
HOOK_V_E0(bf_482aed, bf_unit_e0_state)
HOOK_V_E0(bf_482bc6, bf_unit_e0_state)
HOOK_V_E0(bf_482ca8, bf_unit_e0_state)
HOOK_V_E0(bf_482d8a, bf_unit_e0_state)
HOOK_V_E0(bf_482e6c, bf_unit_e0_state)
HOOK_R(bf_481abb, 1, bf_481abb(I(0)))

HOOK_V(bf_4861e9, 0, bf_4861e9())
HOOK_V(bf_4861f9, 0, bf_4861f9())
HOOK_V(bf_486209, 0, bf_486209())
HOOK_V(bf_486219, 0, bf_486219())
HOOK_V(bf_486229, 0, bf_486229())
HOOK_V(bf_486239, 0, (bf_unit_res_48804a = rh_res_get(0x24), bf_486239(), rh_res_put(0x24, bf_unit_res_48804a)))

HOOK_V(bf_486249, 0, bf_486249())
HOOK_V(bf_486d83, 0, bf_486d83())
HOOK_V(bf_4872ae, 0, bf_4872ae())
HOOK_V(bf_487908, 0, bf_487908())
HOOK_V(bf_487ce4, 0, bf_487ce4())
HOOK_V(bf_48804a, 0, (bf_unit_res_48804a = rh_res_get(0x10), bf_48804a(), rh_res_put(0x10, bf_unit_res_48804a)))
HOOK_V(bf_488436, 0, (bf_unit_res_488436[0] = rh_res_get(0x8), bf_unit_res_488436[1] = rh_res_get(0xc), bf_488436(),
                      rh_res_put(0x8, bf_unit_res_488436[0]), rh_res_put(0xc, bf_unit_res_488436[1])))
HOOK_V(bf_488793, 0, (bf_unit_res_488793 = rh_res_get(0x8), bf_488793(), rh_res_put(0x8, bf_unit_res_488793)))
HOOK_V(bf_48895e, 0, (bf_unit_res_48895e = rh_res_get(0x8), bf_48895e(), rh_res_put(0x8, bf_unit_res_48895e)))
HOOK_V(bf_49459a, 1, bf_49459a(I(0)))

hook_t RH_HOOKS_BF_UNIT[] = {
    { "bf_4822ed", 0x4822edu, (void *)w_bf_4822ed, 0, 0, 0, 0, "" },
    { "bf_46ebe4", 0x46ebe4u, (void *)w_bf_46ebe4, 0, 0, 0, 0, "" },
    { "bf_46eb3f", 0x46eb3fu, (void *)w_bf_46eb3f, 0, 0, 0, 0, "" },
    { "bf_4825d1", 0x4825d1u, (void *)w_bf_4825d1, 0, 0, 0, 0, "" },
    { "bf_4826b0", 0x4826b0u, (void *)w_bf_4826b0, 0, 0, 0, 0, "" },
    { "bf_482789", 0x482789u, (void *)w_bf_482789, 0, 0, 0, 0, "" },
    { "bf_482862", 0x482862u, (void *)w_bf_482862, 0, 0, 0, 0, "" },
    { "bf_48293b", 0x48293bu, (void *)w_bf_48293b, 0, 0, 0, 0, "" },
    { "bf_482a14", 0x482a14u, (void *)w_bf_482a14, 0, 0, 0, 0, "" },
    { "bf_482aed", 0x482aedu, (void *)w_bf_482aed, 0, 0, 0, 0, "" },
    { "bf_482bc6", 0x482bc6u, (void *)w_bf_482bc6, 0, 0, 0, 0, "" },
    { "bf_482ca8", 0x482ca8u, (void *)w_bf_482ca8, 0, 0, 0, 0, "" },
    { "bf_482d8a", 0x482d8au, (void *)w_bf_482d8a, 0, 0, 0, 0, "" },
    { "bf_482e6c", 0x482e6cu, (void *)w_bf_482e6c, 0, 0, 0, 0, "" },
    { "bf_481abb", 0x481abbu, (void *)w_bf_481abb, 0, 0, 0, 0, "" },
    { "bf_4861e9", 0x4861e9u, (void *)w_bf_4861e9, 0, 0, 0, 0, "" },
    { "bf_4861f9", 0x4861f9u, (void *)w_bf_4861f9, 0, 0, 0, 0, "" },
    { "bf_486209", 0x486209u, (void *)w_bf_486209, 0, 0, 0, 0, "" },
    { "bf_486219", 0x486219u, (void *)w_bf_486219, 0, 0, 0, 0, "" },
    { "bf_486229", 0x486229u, (void *)w_bf_486229, 0, 0, 0, 0, "" },
    { "bf_486239", 0x486239u, (void *)w_bf_486239, 0, 0, 0, 0, "" },
    { "bf_486249", 0x486249u, (void *)w_bf_486249, 0, 0, 0, 0, "" },
    { "bf_486d83", 0x486d83u, (void *)w_bf_486d83, 0, 0, 0, 0, "" },
    { "bf_4872ae", 0x4872aeu, (void *)w_bf_4872ae, 0, 0, 0, 0, "" },
    { "bf_487908", 0x487908u, (void *)w_bf_487908, 0, 0, 0, 0, "" },
    { "bf_487ce4", 0x487ce4u, (void *)w_bf_487ce4, 0, 0, 0, 0, "" },
    { "bf_48804a", 0x48804au, (void *)w_bf_48804a, 0, 0, 0, 0, "" },
    { "bf_488436", 0x488436u, (void *)w_bf_488436, 0, 0, 0, 0, "" },
    { "bf_488793", 0x488793u, (void *)w_bf_488793, 0, 0, 0, 0, "" },
    { "bf_48895e", 0x48895eu, (void *)w_bf_48895e, 0, 0, 0, 0, "" },
    { "bf_49459a", 0x49459au, (void *)w_bf_49459a, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_UNIT_N = sizeof RH_HOOKS_BF_UNIT / sizeof *RH_HOOKS_BF_UNIT;
