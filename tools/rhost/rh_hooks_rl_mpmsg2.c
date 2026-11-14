/* rh_hooks_rl_mpmsg2.c — a retail-layout port, partition 'mpmsg2' (src/rl_mpmsg2.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). docs/findings/rl-mpmsg2.md.
 * The payload builders (DAT_004d57f0[id]), receivers (DAT_004d5980[id]) and delayed-action handlers
 * (DAT_004d5c98[type]) have no E8 call site: entry-patched rows, HOOK_V (0x43eda0 / 0x43eac1 / 0x4472ce drop EAX).
 * 0x446e21 / 0x446f84 / 0x447026 / 0x447136 / 0x4476e4 / 0x447740 are called directly: HOOK_R (EAX modelled).
 * 0x447136 takes 12 arguments: the body keeps the caller's esp, the adapter reads the last two at e0 + 44 / 48
 * (retail never writes its argument slots). 0x4479e9 reads an uninitialized local (e0 - 8): rh_res_get / _put;
 * 0x448339 leaves its three locals as residue (rh_res_set). */
#include "rh_port.h"
#include "rl.h"
#include "rl_mpmsg2_globals.h"
#include "rh_rl_mpmsg2_alias.h"

__asm__(RH_RL_MPMSG2_ALIASES);
/* partition mpmsg's globals this partition uses (rl_mpmsg_globals.h), until that table emits RH_RL_MPMSG_ALIASES (ld
 * accepts the same absolute symbol twice at the same value; PE weak externals cannot carry an absolute default) */
__asm__(".globl _g_rl_512f9c\n\t.set _g_rl_512f9c, 0x512f9c\n\t"
        ".globl _g_rl_515960\n\t.set _g_rl_515960, 0x515960\n\t"
        ".globl _g_rl_542cc0\n\t.set _g_rl_542cc0, 0x542cc0\n\t"
        ".globl _g_rl_553e70\n\t.set _g_rl_553e70, 0x553e70\n\t");

#define RH_TABLE RH_HOOKS_RL_MPMSG2
extern hook_t RH_HOOKS_RL_MPMSG2[];

extern uint32_t rl_4479e9_res;
extern uint32_t rl_448339_res[3];

/* a 12-argument row (0x447136): rh_port_run copies ten argument slots; the body notes e0 for the adapter (saved and
 * restored: the late path 0x4470c4 → 0x43eda0 → a builder may file another action) */
static uint32_t s_e0_12;
#define HOOK_R12(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { uint32_t o = s_e0_12, r; s_e0_12 = e0; r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
      s_e0_12 = o; return r; } \
    RH_ENTRY(w_##NAME, b_##NAME)

/* row indices (must follow the table's order) */
enum {
    X_rl_4431ef, X_rl_443286, X_rl_4433d9, X_rl_44341b, X_rl_44348c, X_rl_4434eb, X_rl_443748, X_rl_4437aa,
    X_rl_44381d, X_rl_4438c3, X_rl_443990, X_rl_443a2d, X_rl_443ad5, X_rl_443b8d, X_rl_443c2f, X_rl_443ca6,
    X_rl_443d0a, X_rl_443dba, X_rl_444060, X_rl_444100, X_rl_444191, X_rl_44419c, X_rl_4441d7, X_rl_444280,
    X_rl_4443fb, X_rl_444415, X_rl_4449a0, X_rl_4449f1, X_rl_444a2f, X_rl_444a9b, X_rl_444d04, X_rl_444d68,
    X_rl_4458e5, X_rl_445960, X_rl_445c99, X_rl_445d23, X_rl_445e6f, X_rl_445ea7, X_rl_445f43, X_rl_445f80,
    X_rl_4462c5, X_rl_4462df, X_rl_446e21, X_rl_446f84, X_rl_447026, X_rl_447136, X_rl_4476e4, X_rl_447740,
    X_rl_4479de, X_rl_4479e9, X_rl_447b8e, X_rl_447c68, X_rl_447c99, X_rl_447d22, X_rl_447db6, X_rl_44800e,
    X_rl_448339, X_rl_448422,
};

/* builders / receivers, by message id */
HOOK_V(rl_4431ef, 0, rl_4431ef())   /* 30 */
HOOK_V(rl_443286, 0, rl_443286())
HOOK_V(rl_4433d9, 0, rl_4433d9())   /* 34 */
HOOK_V(rl_44341b, 0, rl_44341b())
HOOK_V(rl_44348c, 0, rl_44348c())   /* 35 */
HOOK_V(rl_4434eb, 0, rl_4434eb())
HOOK_V(rl_443748, 0, rl_443748())   /* 39 */
HOOK_V(rl_4437aa, 0, rl_4437aa())
HOOK_V(rl_44381d, 0, rl_44381d())   /* 40 */
HOOK_V(rl_4438c3, 0, rl_4438c3())
HOOK_V(rl_443990, 0, rl_443990())   /* 41 */
HOOK_V(rl_443a2d, 0, rl_443a2d())
HOOK_V(rl_443ad5, 0, rl_443ad5())   /* 42 */
HOOK_V(rl_443b8d, 0, rl_443b8d())
HOOK_V(rl_443c2f, 0, rl_443c2f())   /* 43 */
HOOK_V(rl_443ca6, 0, rl_443ca6())
HOOK_V(rl_443d0a, 0, rl_443d0a())   /* 44 */
HOOK_V(rl_443dba, 0, rl_443dba())
HOOK_V(rl_444060, 0, rl_444060())   /* 47 */
HOOK_V(rl_444100, 0, rl_444100())
HOOK_V(rl_444191, 0, rl_444191())   /* 48 */
HOOK_V(rl_44419c, 0, rl_44419c())
HOOK_V(rl_4441d7, 0, rl_4441d7())   /* 49 */
HOOK_V(rl_444280, 0, rl_444280())
HOOK_V(rl_4443fb, 0, rl_4443fb())   /* 51 */
HOOK_V(rl_444415, 0, rl_444415())
HOOK_V(rl_4449a0, 0, rl_4449a0())   /* 59 */
HOOK_V(rl_4449f1, 0, rl_4449f1())
HOOK_V(rl_444a2f, 0, rl_444a2f())   /* 60 */
HOOK_V(rl_444a9b, 0, rl_444a9b())
HOOK_V(rl_444d04, 0, rl_444d04())   /* 64 */
HOOK_V(rl_444d68, 0, rl_444d68())
HOOK_V(rl_4458e5, 0, rl_4458e5())   /* 73 */
HOOK_V(rl_445960, 0, rl_445960())
HOOK_V(rl_445c99, 0, rl_445c99())   /* 74 */
HOOK_V(rl_445d23, 0, rl_445d23())
HOOK_V(rl_445e6f, 0, rl_445e6f())   /* 76 */
HOOK_V(rl_445ea7, 0, rl_445ea7())
HOOK_V(rl_445f43, 0, rl_445f43())   /* 77 */
HOOK_V(rl_445f80, 0, rl_445f80())
HOOK_V(rl_4462c5, 0, rl_4462c5())   /* 84 */
HOOK_V(rl_4462df, 0, rl_4462df())
/* the delayed-action machinery */
HOOK_R(rl_446e21, 1, rl_446e21(I(0)))
HOOK_R(rl_446f84, 0, rl_446f84())
HOOK_R(rl_447026, 1, rl_447026(I(0)))
HOOK_R12(rl_447136, 12, rl_447136(I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9),
                                  (int)G32(s_e0_12 + 44), (int)G32(s_e0_12 + 48)))
HOOK_R(rl_4476e4, 0, rl_4476e4())
HOOK_R(rl_447740, 0, rl_447740())
/* the delayed-action handlers, by type */
HOOK_V(rl_4479de, 0, rl_4479de())   /* 0 */
HOOK_V(rl_4479e9, 0, (rl_4479e9_res = rh_res_get(0x8), rl_4479e9(), rh_res_put(0x8, rl_4479e9_res)))   /* 1 */
HOOK_V(rl_447b8e, 0, rl_447b8e())   /* 7 */
HOOK_V(rl_447c68, 0, rl_447c68())   /* 12 */
HOOK_V(rl_447c99, 0, rl_447c99())   /* 13 */
HOOK_V(rl_447d22, 0, rl_447d22())   /* 16 */
HOOK_V(rl_447db6, 0, rl_447db6())   /* 20 */
HOOK_V(rl_44800e, 0, rl_44800e())   /* 30 */
HOOK_V(rl_448339, 0, (rl_448339(), rh_res_set(0x8, rl_448339_res[0]), rh_res_set(0xc, rl_448339_res[1]),
                      rh_res_set(0x10, rl_448339_res[2])))   /* 43 */
HOOK_V(rl_448422, 0, rl_448422())   /* 49 */

hook_t RH_HOOKS_RL_MPMSG2[] = {
    { "rl_4431ef", 0x4431efu, (void *)w_rl_4431ef, 0, 0, 0, 0, "" },
    { "rl_443286", 0x443286u, (void *)w_rl_443286, 0, 0, 0, 0, "" },
    { "rl_4433d9", 0x4433d9u, (void *)w_rl_4433d9, 0, 0, 0, 0, "" },
    { "rl_44341b", 0x44341bu, (void *)w_rl_44341b, 0, 0, 0, 0, "" },
    { "rl_44348c", 0x44348cu, (void *)w_rl_44348c, 0, 0, 0, 0, "" },
    { "rl_4434eb", 0x4434ebu, (void *)w_rl_4434eb, 0, 0, 0, 0, "" },
    { "rl_443748", 0x443748u, (void *)w_rl_443748, 0, 0, 0, 0, "" },
    { "rl_4437aa", 0x4437aau, (void *)w_rl_4437aa, 0, 0, 0, 0, "" },
    { "rl_44381d", 0x44381du, (void *)w_rl_44381d, 0, 0, 0, 0, "" },
    { "rl_4438c3", 0x4438c3u, (void *)w_rl_4438c3, 0, 0, 0, 0, "" },
    { "rl_443990", 0x443990u, (void *)w_rl_443990, 0, 0, 0, 0, "" },
    { "rl_443a2d", 0x443a2du, (void *)w_rl_443a2d, 0, 0, 0, 0, "" },
    { "rl_443ad5", 0x443ad5u, (void *)w_rl_443ad5, 0, 0, 0, 0, "" },
    { "rl_443b8d", 0x443b8du, (void *)w_rl_443b8d, 0, 0, 0, 0, "" },
    { "rl_443c2f", 0x443c2fu, (void *)w_rl_443c2f, 0, 0, 0, 0, "" },
    { "rl_443ca6", 0x443ca6u, (void *)w_rl_443ca6, 0, 0, 0, 0, "" },
    { "rl_443d0a", 0x443d0au, (void *)w_rl_443d0a, 0, 0, 0, 0, "" },
    { "rl_443dba", 0x443dbau, (void *)w_rl_443dba, 0, 0, 0, 0, "" },
    { "rl_444060", 0x444060u, (void *)w_rl_444060, 0, 0, 0, 0, "" },
    { "rl_444100", 0x444100u, (void *)w_rl_444100, 0, 0, 0, 0, "" },
    { "rl_444191", 0x444191u, (void *)w_rl_444191, 0, 0, 0, 0, "" },
    { "rl_44419c", 0x44419cu, (void *)w_rl_44419c, 0, 0, 0, 0, "" },
    { "rl_4441d7", 0x4441d7u, (void *)w_rl_4441d7, 0, 0, 0, 0, "" },
    { "rl_444280", 0x444280u, (void *)w_rl_444280, 0, 0, 0, 0, "" },
    { "rl_4443fb", 0x4443fbu, (void *)w_rl_4443fb, 0, 0, 0, 0, "" },
    { "rl_444415", 0x444415u, (void *)w_rl_444415, 0, 0, 0, 0, "" },
    { "rl_4449a0", 0x4449a0u, (void *)w_rl_4449a0, 0, 0, 0, 0, "" },
    { "rl_4449f1", 0x4449f1u, (void *)w_rl_4449f1, 0, 0, 0, 0, "" },
    { "rl_444a2f", 0x444a2fu, (void *)w_rl_444a2f, 0, 0, 0, 0, "" },
    { "rl_444a9b", 0x444a9bu, (void *)w_rl_444a9b, 0, 0, 0, 0, "" },
    { "rl_444d04", 0x444d04u, (void *)w_rl_444d04, 0, 0, 0, 0, "" },
    { "rl_444d68", 0x444d68u, (void *)w_rl_444d68, 0, 0, 0, 0, "" },
    { "rl_4458e5", 0x4458e5u, (void *)w_rl_4458e5, 0, 0, 0, 0, "" },
    { "rl_445960", 0x445960u, (void *)w_rl_445960, 0, 0, 0, 0, "" },
    { "rl_445c99", 0x445c99u, (void *)w_rl_445c99, 0, 0, 0, 0, "" },
    { "rl_445d23", 0x445d23u, (void *)w_rl_445d23, 0, 0, 0, 0, "" },
    { "rl_445e6f", 0x445e6fu, (void *)w_rl_445e6f, 0, 0, 0, 0, "" },
    { "rl_445ea7", 0x445ea7u, (void *)w_rl_445ea7, 0, 0, 0, 0, "" },
    { "rl_445f43", 0x445f43u, (void *)w_rl_445f43, 0, 0, 0, 0, "" },
    { "rl_445f80", 0x445f80u, (void *)w_rl_445f80, 0, 0, 0, 0, "" },
    { "rl_4462c5", 0x4462c5u, (void *)w_rl_4462c5, 0, 0, 0, 0, "" },
    { "rl_4462df", 0x4462dfu, (void *)w_rl_4462df, 0, 0, 0, 0, "" },
    { "rl_446e21", 0x446e21u, (void *)w_rl_446e21, 0, 0, 0, 0, "" },
    { "rl_446f84", 0x446f84u, (void *)w_rl_446f84, 0, 0, 0, 0, "" },
    { "rl_447026", 0x447026u, (void *)w_rl_447026, 0, 0, 0, 0, "" },
    { "rl_447136", 0x447136u, (void *)w_rl_447136, 0, 0, 0, 0, "" },
    { "rl_4476e4", 0x4476e4u, (void *)w_rl_4476e4, 0, 0, 0, 0, "" },
    { "rl_447740", 0x447740u, (void *)w_rl_447740, 0, 0, 0, 0, "" },
    { "rl_4479de", 0x4479deu, (void *)w_rl_4479de, 0, 0, 0, 0, "" },
    { "rl_4479e9", 0x4479e9u, (void *)w_rl_4479e9, 0, 0, 0, 0, "" },
    { "rl_447b8e", 0x447b8eu, (void *)w_rl_447b8e, 0, 0, 0, 0, "" },
    { "rl_447c68", 0x447c68u, (void *)w_rl_447c68, 0, 0, 0, 0, "" },
    { "rl_447c99", 0x447c99u, (void *)w_rl_447c99, 0, 0, 0, 0, "" },
    { "rl_447d22", 0x447d22u, (void *)w_rl_447d22, 0, 0, 0, 0, "" },
    { "rl_447db6", 0x447db6u, (void *)w_rl_447db6, 0, 0, 0, 0, "" },
    { "rl_44800e", 0x44800eu, (void *)w_rl_44800e, 0, 0, 0, 0, "" },
    { "rl_448339", 0x448339u, (void *)w_rl_448339, 0, 0, 0, 0, "" },
    { "rl_448422", 0x448422u, (void *)w_rl_448422, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_MPMSG2_N = sizeof RH_HOOKS_RL_MPMSG2 / sizeof *RH_HOOKS_RL_MPMSG2;
