/* rh_hooks_rl_host.c — a retail-layout port, partition 'host' (src/rl_host.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). GENERATED rows (the partition owner's build/hs/gentable.py); edit the prelude by hand.
 *
 * No row (ported, not shadowable — docs/findings/rl-host.md): see NOROW below. */
#include "rh_port.h"
#include "rl.h"
#include "rl_host_globals.h"
#include "rh_rl_host_alias.h"

__asm__(RH_RL_HOST_ALIASES);

#define RH_TABLE RH_HOOKS_RL_HOST
extern hook_t RH_HOOKS_RL_HOST[];
#include "rh_hook_smk.h"   /* the frame driver plays the movies (0x42dbc7): smackw32 rolled back around its shadow */

/* an 11-argument row (0x4bc020): rh_port_run copies ten argument slots, so the body keeps the caller's esp and
 * the adapter reads the 11th at e0 + 44 (retail's side only reads its arguments; the function calls nothing) */
static uint32_t s_e0_11;
#define HOOK_R11(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { s_e0_11 = e0; return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); } \
    RH_ENTRY(w_##NAME, b_##NAME)

/* the window procedure 0x4b29be is __stdcall (4 arguments; the window layer calls it through the class, so its entry
 * is patched): RH_ENTRY with `ret $0x10` (rh_call_at runs retail's stdcall at e0 and restores esp itself) */
#define RH_ENTRY_STD16(W, B) \
    static __attribute__((naked)) void W(void) \
    { __asm__("movl %ebp, _rh_entry_ebp\n\tmovl %esp, %eax\n\tleal -0x40000(%eax), %edx\n\tmovl _rh_c_floor, %ecx\n\t" \
              "testl %ecx, %ecx\n\tjz 1f\n\tsubl $0x400, %ecx\n\tcmpl %ecx, %edx\n\tjb 1f\n\tmovl %ecx, %edx\n" \
              "1:\tmovl %edx, %esp\n\tpushl %eax\n\tpushl %eax\n\t" \
              "call _" #B "\n\taddl $4, %esp\n\tpopl %esp\n\tret $0x10"); }
#define HOOK_R_STD16(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); } \
    RH_ENTRY_STD16(w_##NAME, b_##NAME)

/* the menu input 0x4ba9c8: a click can open a movie (movie-skip: lom.smk from the title menu). smackw32's spans come
 * from rh_smack's bump pointer (g_stop), which no shadow rolls back: the port side's SmackOpen got another block
 * (DAT_004d4d58 0x190002c0 vs 0x19000900) and, under rh_hook_smk's page rewind, crashed in smackw32 (f662). So with
 * --movies the row is host-level for the run (RH_SH_HOST: retail in the shadow passes, the hybrid proves it);
 * without movies it is shadowed as usual. */
#define HOOK_R_NOMOVIES(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        if (O.movies) RH_TABLE[X_##NAME].flags |= RH_SH_HOST; \
        return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
    } \
    RH_ENTRY(w_##NAME, b_##NAME)

/* the frame driver 0x4b99c0: HOOK_SMK_V (it plays the movies) + its retail entry esp for the battle chain's residue
 * model (src/rl_host.c rl_host_e0_4b99c0 → bf_unit_e0_4822ed before its port-to-port call of 0x4822ed) */
extern uint32_t rl_host_e0_4b99c0;
/* 0x4b99c0's entry esp in a pure run, for the residue models (quirk #239): retail's chain (start → WinMain → 0x40e8bb
 * → 0x40e890) puts esp at RH_RETAIL_CFA_FW before its call of the frame driver (0x40e896, redirected to rhost's
 * frame_wrap) — retail code only, so a constant — and frame_wrap's own frame (rh_fw_frame, measured every frame: it
 * changes with the compiler / frame_wrap's code) lies between. Under retail's guard the real esp is used and checked.
 * (It replaced RH_RETAIL_E0_4B99C0 = 0x58fffdcc, stale by 8 since frame_wrap grew: field-orders' and ai-sally's
 * hybrid FAIL — a dead unit's sprite frame byte, 0x488793's unset local.) */
#define RH_RETAIL_CFA_FW 0x58fffe18u
extern int rl_frame_port_guard;
extern uint32_t rh_fw_frame;   /* rhost.c */
static uint32_t e0_4b99c0_model(uint32_t e0)
{
    static int warned;
    uint32_t m = RH_RETAIL_CFA_FW - rh_fw_frame - 4;
    if (rl_frame_port_guard) return m;
    if (e0 != m && !warned) {
        warned = 1;
        rh_log("warning: frame driver 0x4b99c0 entered at esp %08x under retail's frame guard, model %08x (RH_RETAIL_CFA_FW)\n", e0, m);
    }
    return e0;
}
#define HOOK_SMK_V_E0(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) \
    { (void)a; if (RH_TABLE[X_##NAME].mode == H_SHADOW && s_smk) smk_rewind(s_smk); CALL; return 0; } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        uint32_t o_ = rl_host_e0_4b99c0, r; \
        rl_host_e0_4b99c0 = e0_4b99c0_model(e0); \
        if (RH_TABLE[X_##NAME].mode != H_SHADOW || rh_in_shadow()) { \
            r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 0, e0); \
            rl_host_e0_4b99c0 = o_; \
            return r; \
        } \
        smk_ctx_t *c = malloc(sizeof *c), *keep = s_smk; \
        smk_enter(c); s_smk = c; \
        r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 0, e0); \
        s_smk = keep; free(c->buf); free(c); \
        rl_host_e0_4b99c0 = o_; \
        return r; \
    } \
    RH_ENTRY(w_##NAME, b_##NAME)
/* row indices (must follow the table's order) */
enum {
    X_rl_4a57ac, X_rl_4a58f4, X_rl_4a5b1f, X_rl_4a5f0a, X_rl_4a60b9, X_rl_4a6270, X_rl_4a64ca, X_rl_4a65a3,
    X_rl_4a6735, X_rl_4a7e0a, X_rl_4a7eb5, X_rl_4a9eb1, X_rl_4a9f18, X_rl_4a9f72, X_rl_4ac7f3, X_rl_4acb54,
    X_rl_4ace5e, X_rl_4ad0e8, X_rl_4ad16c, X_rl_4ad230, X_rl_4ad316, X_rl_4ad39a, X_rl_4aabd8, X_rl_4b495a,
    X_rl_4aef7e, X_rl_4af502, X_rl_4af54a, X_rl_4af62c, X_rl_4af675, X_rl_4af8d9, X_rl_4b0ab5, X_rl_4b1867,
    X_rl_4b18e3, X_rl_4b191e, X_rl_4b1de0, X_rl_4b1deb, X_rl_4b20ed, X_rl_4b25b0, X_rl_4b27bf, X_rl_4b35c1,
    X_rl_4b3768, X_rl_4b3e51, X_rl_4bb8c0, X_rl_4bb978, X_rl_4bbbe3, X_rl_4bbc80, X_rl_4a8426, X_rl_4a8d9c,
    X_rl_4a8e0b, X_rl_4a91ba, X_rl_4a9252, X_rl_4a9372, X_rl_4a960f, X_rl_4a975d, X_rl_4a983b, X_rl_4a98fe,
    X_rl_4ac904, X_rl_4ae74d, X_rl_4ae8a9, X_rl_4ade93, X_rl_4adfe6, X_rl_4ae310, X_rl_4ad9f0, X_rl_4ba26e,
    X_rl_4ba9c8, X_rl_4b99c0, X_rl_4b284e, X_rl_4b2922, X_rl_4bc020, X_rl_4b29be,
    X_rl_43ff63, X_rl_446f06, X_rl_43ff08, X_rl_43ffc3, X_rl_43f7e3,
};

HOOK_R(rl_4a57ac, 1, rl_4a57ac(I(0)))
HOOK_R(rl_4a58f4, 1, rl_4a58f4(I(0)))
HOOK_R(rl_4a5b1f, 1, rl_4a5b1f(I(0)))
HOOK_R(rl_4a5f0a, 1, rl_4a5f0a(I(0)))
HOOK_R(rl_4a60b9, 1, rl_4a60b9(I(0)))
HOOK_R(rl_4a6270, 1, rl_4a6270(I(0)))
HOOK_R(rl_4a64ca, 2, rl_4a64ca(I(0), I(1)))
HOOK_R(rl_4a65a3, 2, rl_4a65a3(I(0), I(1)))
HOOK_R(rl_4a6735, 2, rl_4a6735(I(0), I(1)))
HOOK_R(rl_4a7e0a, 2, rl_4a7e0a(I(0), I(1)))
HOOK_V(rl_4a7eb5, 1, rl_4a7eb5(I(0)))
HOOK_V(rl_4a9eb1, 1, rl_4a9eb1(I(0)))
HOOK_V(rl_4a9f18, 1, rl_4a9f18(I(0)))
HOOK_R(rl_4a9f72, 1, rl_4a9f72(I(0)))
HOOK_R(rl_4ac7f3, 2, rl_4ac7f3(I(0), I(1)))
HOOK_R(rl_4acb54, 1, rl_4acb54(I(0)))
HOOK_V(rl_4ace5e, 0, rl_4ace5e())
HOOK_V(rl_4ad0e8, 1, rl_4ad0e8(I(0)))
HOOK_V(rl_4ad16c, 1, rl_4ad16c(I(0)))
HOOK_R(rl_4ad230, 1, rl_4ad230(I(0)))
HOOK_V(rl_4ad316, 1, rl_4ad316(I(0)))
HOOK_V(rl_4ad39a, 0, rl_4ad39a())
HOOK_R(rl_4aabd8, 2, rl_4aabd8(I(0), I(1)))
HOOK_R(rl_4b495a, 1, rl_4b495a(I(0)))   /* a hot leaf of the RLE blits: sampled */
HOOK_R(rl_4aef7e, 0, rl_4aef7e())
HOOK_R(rl_4af502, 0, rl_4af502())
HOOK_R(rl_4af54a, 1, rl_4af54a(I(0)))
HOOK_R(rl_4af62c, 1, rl_4af62c(I(0)))
HOOK_R(rl_4af675, 1, rl_4af675(I(0)))
HOOK_R(rl_4af8d9, 4, rl_4af8d9(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4b0ab5, 1, rl_4b0ab5(I(0)))
HOOK_V(rl_4b1867, 0, rl_4b1867())   /* EAX: the caller's when there is no back buffer */
HOOK_V(rl_4b18e3, 0, rl_4b18e3())
HOOK_R(rl_4b191e, 0, rl_4b191e())
HOOK_V(rl_4b1de0, 0, rl_4b1de0())
HOOK_R(rl_4b1deb, 4, rl_4b1deb(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4b20ed, 0, rl_4b20ed())
HOOK_R(rl_4b25b0, 1, rl_4b25b0(I(0)))
HOOK_R(rl_4b27bf, 0, rl_4b27bf())
HOOK_V(rl_4b35c1, 2, rl_4b35c1(I(0), I(1)))   /* EAX: the caller's for an event outside the cue ranges */
HOOK_V(rl_4b3768, 1, rl_4b3768(I(0)))   /* EAX: the caller's for k outside 0..7 */
HOOK_R(rl_4b3e51, 2, rl_4b3e51(I(0), I(1)))
HOOK_R(rl_4bb8c0, 0, rl_4bb8c0())
HOOK_R(rl_4bb978, 0, rl_4bb978())
HOOK_R(rl_4bbbe3, 0, rl_4bbbe3())
HOOK_R(rl_4bbc80, 0, rl_4bbc80())
HOOK_R(rl_4a8426, 1, rl_4a8426(I(0)))
HOOK_R(rl_4a8d9c, 0, rl_4a8d9c())
HOOK_V(rl_4a8e0b, 0, rl_4a8e0b())
HOOK_R(rl_4a91ba, 0, rl_4a91ba())
HOOK_V(rl_4a9252, 0, rl_4a9252())   /* EAX: the dialog's, or the caller's with no reachable hover */
HOOK_V(rl_4a9372, 0, rl_4a9372())   /* confirm-dialog callbacks: entry-patched (no call site) */
HOOK_V(rl_4a960f, 0, rl_4a960f())
HOOK_V(rl_4a975d, 0, rl_4a975d())
HOOK_V(rl_4a983b, 0, rl_4a983b())
HOOK_R(rl_4a98fe, 5, rl_4a98fe(I(0), I(1), I(2), I(3), I(4)))
HOOK_R(rl_4ac904, 0, rl_4ac904())
HOOK_V(rl_4ae74d, 0, rl_4ae74d())
HOOK_R(rl_4ae8a9, 0, rl_4ae8a9())
HOOK_R(rl_4ade93, 1, rl_4ade93(I(0)))
HOOK_R(rl_4adfe6, 1, rl_4adfe6(I(0)))
HOOK_V(rl_4ae310, 0, rl_4ae310())
HOOK_V(rl_4ad9f0, 0, rl_4ad9f0())
HOOK_V(rl_4ba26e, 0, rl_4ba26e())
HOOK_R_NOMOVIES(rl_4ba9c8, 0, rl_4ba9c8())   /* a click can open a movie: host-level with --movies (above) */
HOOK_SMK_V_E0(rl_4b99c0, 0, rl_4b99c0())   /* the movies (smackw32 rolled back) + the retail e0 for 0x4822ed's residue model */
HOOK_R(rl_4b284e, 0, rl_4b284e())   /* host-level: creates the DirectDraw object + surfaces (RH_SH_HOST: hybrid only) */
HOOK_R(rl_4b2922, 0, rl_4b2922())   /* host-level: through 0x4b284e (RH_SH_HOST) */
HOOK_R11(rl_4bc020, 11, rl_4bc020(I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9), (int)G32(s_e0_11 + 44)))   /* the 11th argument from the caller's stack */
HOOK_R_STD16(rl_4b29be, 4, rl_4b29be(I(0), I(1), I(2), I(3)))   /* __stdcall: ret 0x10; host-level (RH_SH_HOST: the teardown / re-init arms) */
HOOK_V(rl_43ff63, 0, rl_43ff63())
HOOK_V(rl_446f06, 0, rl_446f06())
HOOK_V(rl_43ff08, 0, rl_43ff08())
HOOK_V(rl_43ffc3, 0, rl_43ffc3())
HOOK_R(rl_43f7e3, 0, rl_43f7e3())   /* 0x442b08 returns its EAX (timeGetTime) */

hook_t RH_HOOKS_RL_HOST[] = {
    { "rl_4a57ac", 0x4a57acu, (void *)w_rl_4a57ac, 0, 0, 0, 0, "" },
    { "rl_4a58f4", 0x4a58f4u, (void *)w_rl_4a58f4, 0, 0, 0, 0, "" },
    { "rl_4a5b1f", 0x4a5b1fu, (void *)w_rl_4a5b1f, 0, 0, 0, 0, "" },
    { "rl_4a5f0a", 0x4a5f0au, (void *)w_rl_4a5f0a, 0, 0, 0, 0, "" },
    { "rl_4a60b9", 0x4a60b9u, (void *)w_rl_4a60b9, 0, 0, 0, 0, "" },
    { "rl_4a6270", 0x4a6270u, (void *)w_rl_4a6270, 0, 0, 0, 0, "" },
    { "rl_4a64ca", 0x4a64cau, (void *)w_rl_4a64ca, 0, 0, 0, 0, "" },
    { "rl_4a65a3", 0x4a65a3u, (void *)w_rl_4a65a3, 0, 0, 0, 0, "" },
    { "rl_4a6735", 0x4a6735u, (void *)w_rl_4a6735, 0, 0, 0, 0, "" },
    { "rl_4a7e0a", 0x4a7e0au, (void *)w_rl_4a7e0a, 0, 0, 0, 0, "" },
    { "rl_4a7eb5", 0x4a7eb5u, (void *)w_rl_4a7eb5, 0, 0, 0, 0, "" },
    { "rl_4a9eb1", 0x4a9eb1u, (void *)w_rl_4a9eb1, 0, 0, 0, 0, "" },
    { "rl_4a9f18", 0x4a9f18u, (void *)w_rl_4a9f18, 0, 0, 0, 0, "" },
    { "rl_4a9f72", 0x4a9f72u, (void *)w_rl_4a9f72, 0, 0, 0, 0, "" },
    { "rl_4ac7f3", 0x4ac7f3u, (void *)w_rl_4ac7f3, 0, 0, 0, 0, "" },
    { "rl_4acb54", 0x4acb54u, (void *)w_rl_4acb54, 0, 0, 0, 0, "" },
    { "rl_4ace5e", 0x4ace5eu, (void *)w_rl_4ace5e, 0, 0, 0, 0, "" },
    { "rl_4ad0e8", 0x4ad0e8u, (void *)w_rl_4ad0e8, 0, 0, 0, 0, "" },
    { "rl_4ad16c", 0x4ad16cu, (void *)w_rl_4ad16c, 0, 0, 0, 0, "" },
    { "rl_4ad230", 0x4ad230u, (void *)w_rl_4ad230, 0, 0, 0, 0, "" },
    { "rl_4ad316", 0x4ad316u, (void *)w_rl_4ad316, 0, 0, 0, 0, "" },
    { "rl_4ad39a", 0x4ad39au, (void *)w_rl_4ad39a, 0, 0, 0, 0, "" },
    { "rl_4aabd8", 0x4aabd8u, (void *)w_rl_4aabd8, 0, 0, 0, 0, "" },
    { "rl_4b495a", 0x4b495au, (void *)w_rl_4b495a, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_4aef7e", 0x4aef7eu, (void *)w_rl_4aef7e, 0, 0, 0, 0, "" },
    { "rl_4af502", 0x4af502u, (void *)w_rl_4af502, 0, 0, 0, 0, "" },
    { "rl_4af54a", 0x4af54au, (void *)w_rl_4af54a, 0, 0, 0, 0, "" },
    { "rl_4af62c", 0x4af62cu, (void *)w_rl_4af62c, 0, 0, 0, 0, "" },
    { "rl_4af675", 0x4af675u, (void *)w_rl_4af675, 0, 0, 0, 0, "" },
    { "rl_4af8d9", 0x4af8d9u, (void *)w_rl_4af8d9, 0, 0, 0, 0, "" },
    { "rl_4b0ab5", 0x4b0ab5u, (void *)w_rl_4b0ab5, 0, 0, 0, 0, "" },
    { "rl_4b1867", 0x4b1867u, (void *)w_rl_4b1867, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4b18e3", 0x4b18e3u, (void *)w_rl_4b18e3, 0, 0, 0, 0, "" },
    { "rl_4b191e", 0x4b191eu, (void *)w_rl_4b191e, 0, 0, 0, 0, "" },
    { "rl_4b1de0", 0x4b1de0u, (void *)w_rl_4b1de0, 0, 0, 0, 0, "" },
    { "rl_4b1deb", 0x4b1debu, (void *)w_rl_4b1deb, 0, 0, 0, 0, "" },
    { "rl_4b20ed", 0x4b20edu, (void *)w_rl_4b20ed, 0, 0, 0, 0, "" },
    { "rl_4b25b0", 0x4b25b0u, (void *)w_rl_4b25b0, 0, 0, 0, 0, "" },
    { "rl_4b27bf", 0x4b27bfu, (void *)w_rl_4b27bf, 0, 0, 0, 0, "" },
    { "rl_4b35c1", 0x4b35c1u, (void *)w_rl_4b35c1, 0, 0, 0, 0, "" },
    { "rl_4b3768", 0x4b3768u, (void *)w_rl_4b3768, 0, 0, 0, 0, "" },
    { "rl_4b3e51", 0x4b3e51u, (void *)w_rl_4b3e51, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4bb8c0", 0x4bb8c0u, (void *)w_rl_4bb8c0, 0, 0, 0, 0, "" },
    { "rl_4bb978", 0x4bb978u, (void *)w_rl_4bb978, 0, 0, 0, 0, "" },
    { "rl_4bbbe3", 0x4bbbe3u, (void *)w_rl_4bbbe3, 0, 0, 0, 0, "" },
    { "rl_4bbc80", 0x4bbc80u, (void *)w_rl_4bbc80, 0, 0, 0, 0, "" },
    { "rl_4a8426", 0x4a8426u, (void *)w_rl_4a8426, 0, 0, 0, 0, "" },
    { "rl_4a8d9c", 0x4a8d9cu, (void *)w_rl_4a8d9c, 0, 0, 0, 0, "" },
    { "rl_4a8e0b", 0x4a8e0bu, (void *)w_rl_4a8e0b, 0, 0, 0, 0, "" },
    { "rl_4a91ba", 0x4a91bau, (void *)w_rl_4a91ba, 0, 0, 0, 0, "" },
    { "rl_4a9252", 0x4a9252u, (void *)w_rl_4a9252, 0, 0, 0, 0, "" },
    { "rl_4a9372", 0x4a9372u, (void *)w_rl_4a9372, 0, 0, 0, 0, "" },
    { "rl_4a960f", 0x4a960fu, (void *)w_rl_4a960f, 0, 0, 0, 0, "" },
    { "rl_4a975d", 0x4a975du, (void *)w_rl_4a975d, 0, 0, 0, 0, "" },
    { "rl_4a983b", 0x4a983bu, (void *)w_rl_4a983b, 0, 0, 0, 0, "" },
    { "rl_4a98fe", 0x4a98feu, (void *)w_rl_4a98fe, 0, 0, 0, 0, "" },
    { "rl_4ac904", 0x4ac904u, (void *)w_rl_4ac904, 0, 0, 0, 0, "" },
    { "rl_4ae74d", 0x4ae74du, (void *)w_rl_4ae74d, 0, 0, 0, 0, "" },
    { "rl_4ae8a9", 0x4ae8a9u, (void *)w_rl_4ae8a9, 0, 0, 0, 0, "" },
    { "rl_4ade93", 0x4ade93u, (void *)w_rl_4ade93, 0, 0, 0, 0, "" },
    { "rl_4adfe6", 0x4adfe6u, (void *)w_rl_4adfe6, 0, 0, 0, 0, "" },
    { "rl_4ae310", 0x4ae310u, (void *)w_rl_4ae310, 0, 0, 0, 0, "" },
    { "rl_4ad9f0", 0x4ad9f0u, (void *)w_rl_4ad9f0, 0, 0, 0, 0, "" },
    { "rl_4ba26e", 0x4ba26eu, (void *)w_rl_4ba26e, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "rl_4ba9c8", 0x4ba9c8u, (void *)w_rl_4ba9c8, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4b99c0", 0x4b99c0u, (void *)w_rl_4b99c0, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "rl_4b284e", 0x4b284eu, (void *)w_rl_4b284e, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b2922", 0x4b2922u, (void *)w_rl_4b2922, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4bc020", 0x4bc020u, (void *)w_rl_4bc020, 0, 0, 0, 0, "" },
    { "rl_4b29be", 0x4b29beu, (void *)w_rl_4b29be, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_43ff63", 0x43ff63u, (void *)w_rl_43ff63, 0, 0, 0, 0, "" },
    { "rl_446f06", 0x446f06u, (void *)w_rl_446f06, 0, 0, 0, 0, "" },
    { "rl_43ff08", 0x43ff08u, (void *)w_rl_43ff08, 0, 0, 0, 0, "" },
    { "rl_43ffc3", 0x43ffc3u, (void *)w_rl_43ffc3, 0, 0, 0, 0, "" },
    { "rl_43f7e3", 0x43f7e3u, (void *)w_rl_43f7e3, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_HOST_N = sizeof RH_HOOKS_RL_HOST / sizeof *RH_HOOKS_RL_HOST;
