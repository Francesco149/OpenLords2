/* rh_hooks_rl_mpnet.c — a retail-layout port, partition 'mpnet' (src/rl_mpnet.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). docs/findings/rl-mpnet.md.
 *
 * THISCALL rows (the SIGS session's methods 0x4b61b0..0x4b6e8e: ECX = the session, the callee pops its arguments):
 * the entry stub saves ECX (s_tc_in) before RH_ENTRY's body uses it and returns with `ret $N`; the body makes it the
 * row's current `this` (s_tc_this, saved / restored around the run: methods nest) — the adapter passes it as the
 * port's first argument, and the retail side runs through a per-row thunk (hook_t.entry, swapped on the first call as
 * rh_hooks_gfx.c does for its EAX row) that loads ECX from it and jumps to the original entry.
 *
 * THE MONITOR COROUTINE.  rh_net.c runs retail's monitor thread (StartAddress 0x4b7052) on its own 256 KB stack
 * (0x5e000000..0x5e040000); it calls 0x4b6d44 / 0x4b6ae5. RH_ENTRY's 256 KB drop would leave that stack, and a shadow
 * there would take a rollback point inside the coroutine (rh_net: fatal). So a call on that stack bypasses the
 * machinery: replace → the port runs in place (no mirror sync: the coroutine runs inside the main thread's Sleep /
 * wait, possibly inside a port call); otherwise → retail's original entry. Those calls are proven by the hybrid pass
 * only (StartAddress itself — an entry-patched row: rh_net calls the pointer CreateThread got — is never on the main
 * stack).
 *
 * The provider dialog's DLGPROC 0x4b7f78 (__stdcall, 4 arguments; rhost's fake DialogBoxParamA calls the pointer: entry
 * patch) is RH_SH_HOST: rh_win.c's fake list box (g_lb / g_lbn) is host state no shadow rolls back — retail's
 * LB_ADDSTRING would leave row 0 and the port's add row 1 (wParam 0 vs 1). The hybrid pass proves it. */
#include "rh_port.h"
#include "rl.h"
#include "rl_mpnet_globals.h"
#include "rh_rl_mpnet_alias.h"

#include <string.h>

__asm__(RH_RL_MPNET_ALIASES);

#define RH_TABLE RH_HOOKS_RL_MPNET
extern hook_t RH_HOOKS_RL_MPNET[];

/* row indices (must follow the table's order) */
enum {
    X_rl_4b61b0, X_rl_4b62ea, X_rl_4b63d0, X_rl_4b64d8, X_rl_4b6754, X_rl_4b6ae5, X_rl_4b6b71, X_rl_4b6c59, X_rl_4b6c82,
    X_rl_4b6ccb, X_rl_4b6cee, X_rl_4b6d19, X_rl_4b6d44, X_rl_4b6dc5, X_rl_4b6de6, X_rl_4b6e1b, X_rl_4b6e8e, X_rl_4b7052,
    X_rl_4b7514, X_rl_4b7a9a, X_rl_4b7f78, X_rl_4b90fb, X_rl_4bbf89, X_rl_4bbfd2, X_rl_43306a, X_rl_46f2f1, X_rl_47f937,
    X_rl_49b973, X_rl_49ca60, X_rl_4b3d5d, X_rl_433551, X_rl_41f3e9, X_rl_41f4a1, X_rl_41f77a, X_rl_475e07,
};

/* ---------------------------------------------------------------- thiscall + the monitor coroutine */
static uint32_t __attribute__((used)) s_tc_in, s_tc_this;   /* ECX at the stub; the running row's `this` */
static int __attribute__((used)) s_co_ran;                  /* the coroutine path ran the port */
#define SELF s_tc_this

static uint32_t co_run(hook_t *h, rh_port_fn p, uint32_t e0)
{
    if (h->mode != H_REPLACE) { s_co_ran = 0; return 0; }
    uint32_t a[10];
    memcpy(a, (const void *)(uintptr_t)(e0 + 4), sizeof a);
    h->calls++;
    rh_port_bind_once();
    uint32_t o = s_tc_this;
    s_tc_this = s_tc_in;
    uint32_t r = p(a);
    s_tc_this = o;
    s_co_ran = 1;
    return r;
}

/* the entry stub: ECX saved; on the coroutine's stack → c_NAME (the port in place, or retail's entry); else RH_ENTRY's
 * body (rh_port.h) → b_NAME; returns popping POP bytes */
#define TC_STUB(NAME, POP) \
    static __attribute__((naked)) void w_##NAME(void) \
    { __asm__("movl %ecx, _s_tc_in\n\t" \
              "cmpl $0x5e000000, %esp\n\tjb 1f\n\tcmpl $0x5e040000, %esp\n\tjae 1f\n\t" \
              "movl %esp, %eax\n\tpushl %eax\n\tcall _c_" #NAME "\n\taddl $4, %esp\n\t" \
              "cmpl $0, _s_co_ran\n\tje 2f\n\tret $" #POP "\n" \
              "2:\tmovl _s_tc_in, %ecx\n\tjmp *_s_orig_" #NAME "\n" \
              "1:\tmovl %ebp, _rh_entry_ebp\n\tmovl %esp, %eax\n\tleal -0x40000(%eax), %edx\n\tmovl _rh_c_floor, %ecx\n\t" \
              "testl %ecx, %ecx\n\tjz 3f\n\tsubl $0x400, %ecx\n\tcmpl %ecx, %edx\n\tjb 3f\n\tmovl %ecx, %edx\n" \
              "3:\tmovl %edx, %esp\n\tpushl %eax\n\tpushl %eax\n\t" \
              "call _b_" #NAME "\n\taddl $4, %esp\n\tpopl %esp\n\tret $" #POP); }
#define HOOK_TC(NAME, POP, HAS_RET, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static uint32_t __attribute__((used)) s_orig_##NAME; \
    static __attribute__((naked, used)) void t_##NAME(void) { __asm__("movl _s_tc_this, %ecx\n\tjmp *_s_orig_" #NAME); } \
    static void fix_##NAME(void) \
    { \
        hook_t *h = &RH_TABLE[X_##NAME]; \
        if (h->entry != (uint32_t)(uintptr_t)t_##NAME) { s_orig_##NAME = h->entry; h->entry = (uint32_t)(uintptr_t)t_##NAME; } \
    } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        fix_##NAME(); \
        uint32_t o = s_tc_this; s_tc_this = s_tc_in; \
        uint32_t r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, HAS_RET, e0); \
        s_tc_this = o; \
        return r; \
    } \
    static __attribute__((used, noinline)) uint32_t __cdecl c_##NAME(uint32_t e0) \
    { fix_##NAME(); return co_run(&RH_TABLE[X_##NAME], p_##NAME, e0); } \
    TC_STUB(NAME, POP)

/* the DLGPROC: RH_ENTRY with `ret $0x10` (rh_call_at runs retail's stdcall at e0 and restores esp itself) */
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

/* the session class (__thiscall) */
HOOK_TC(rl_4b61b0, 0, 1, rl_4b61b0(SELF))
HOOK_TC(rl_4b62ea, 16, 1, rl_4b62ea(SELF, I(0), I(1), I(2), I(3)))
HOOK_TC(rl_4b63d0, 0, 1, rl_4b63d0(SELF))
HOOK_TC(rl_4b64d8, 0, 1, rl_4b64d8(SELF))
HOOK_TC(rl_4b6754, 0, 1, rl_4b6754(SELF))
HOOK_TC(rl_4b6ae5, 0, 1, rl_4b6ae5(SELF))
HOOK_TC(rl_4b6b71, 12, 1, rl_4b6b71(SELF, I(0), (unsigned)a[1], I(2)))
HOOK_TC(rl_4b6c59, 8, 1, rl_4b6c59(SELF, I(0), I(1)))
HOOK_TC(rl_4b6c82, 12, 1, rl_4b6c82(SELF, I(0), I(1), I(2)))
HOOK_TC(rl_4b6ccb, 0, 1, rl_4b6ccb(SELF))
HOOK_TC(rl_4b6cee, 4, 1, rl_4b6cee(SELF, I(0)))
HOOK_TC(rl_4b6d19, 4, 1, rl_4b6d19(SELF, I(0)))
HOOK_TC(rl_4b6d44, 0, 1, rl_4b6d44(SELF))
HOOK_TC(rl_4b6dc5, 0, 1, rl_4b6dc5(SELF))
HOOK_TC(rl_4b6de6, 0, 1, rl_4b6de6(SELF))
HOOK_TC(rl_4b6e1b, 0, 1, rl_4b6e1b(SELF))
HOOK_TC(rl_4b6e8e, 12, 1, rl_4b6e8e(SELF, I(0), I(1), (unsigned)a[2]))
/* the monitor thread (only ever on the coroutine: TC_STUB's in-place path; `retn`, no pop) */
HOOK_TC(rl_4b7052, 0, 1, rl_4b7052())
/* the transport's users (cdecl) */
HOOK_V(rl_4b7514, 0, rl_4b7514())   /* SP: EAX is the caller's */
HOOK_R(rl_4b7a9a, 0, rl_4b7a9a())
HOOK_R_STD16(rl_4b7f78, 4, rl_4b7f78(I(0), (unsigned)a[1], (unsigned)a[2], I(3)))
HOOK_V(rl_4b90fb, 4, rl_4b90fb(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4bbf89, 0, rl_4bbf89())
HOOK_R(rl_4bbfd2, 0, rl_4bbfd2())
/* MP state helpers */
HOOK_V(rl_43306a, 0, rl_43306a())   /* EAX: (44 × player & ~0xff) | shield — no reader */
HOOK_V(rl_46f2f1, 0, rl_46f2f1())
HOOK_R(rl_47f937, 0, rl_47f937())
HOOK_R(rl_49b973, 1, rl_49b973(I(0)))
HOOK_R(rl_49ca60, 2, rl_49ca60(I(0), I(1)))
HOOK_V(rl_4b3d5d, 3, rl_4b3d5d(I(0), I(1), I(2)))
HOOK_V(rl_433551, 0, rl_433551())
/* MP title pages + the MP message popup (render rows: the surfaces' pixels compared too) */
HOOK_V(rl_41f3e9, 0, rl_41f3e9())
HOOK_V(rl_41f4a1, 0, rl_41f4a1())
HOOK_V(rl_41f77a, 1, rl_41f77a(I(0)))
/* 0x475e07 reads its [ebp-8] before any store (the previous call's kind, e0 - 12) and leaves kind / sender at
 * e0 - 12 / e0 - 8 — modelled (rh_res_get / rh_res_put); EAX: no reader */
static void p475e07(void)
{
    rl_475e07_r((int)rh_res_get(12));
    if (rl_475e07_res_set) {
        rh_res_put(12, (uint32_t)rl_475e07_res_kind);
        rh_res_put(8, (uint32_t)rl_475e07_res_who);
    }
}
HOOK_V(rl_475e07, 0, p475e07())

hook_t RH_HOOKS_RL_MPNET[] = {
    { "rl_4b61b0", 0x4b61b0u, (void *)w_rl_4b61b0, 0, 0, 0, 0, "" },
    { "rl_4b62ea", 0x4b62eau, (void *)w_rl_4b62ea, 0, 0, 0, 0, "" },
    { "rl_4b63d0", 0x4b63d0u, (void *)w_rl_4b63d0, 0, 0, 0, 0, "" },
    { "rl_4b64d8", 0x4b64d8u, (void *)w_rl_4b64d8, 0, 0, 0, 0, "" },
    { "rl_4b6754", 0x4b6754u, (void *)w_rl_4b6754, 0, 0, 0, 0, "" },
    { "rl_4b6ae5", 0x4b6ae5u, (void *)w_rl_4b6ae5, 0, 0, 0, 0, "" },
    { "rl_4b6b71", 0x4b6b71u, (void *)w_rl_4b6b71, 0, 0, 0, 0, "" },
    { "rl_4b6c59", 0x4b6c59u, (void *)w_rl_4b6c59, 0, 0, 0, 0, "" },
    { "rl_4b6c82", 0x4b6c82u, (void *)w_rl_4b6c82, 0, 0, 0, 0, "" },
    { "rl_4b6ccb", 0x4b6ccbu, (void *)w_rl_4b6ccb, 0, 0, 0, 0, "" },
    { "rl_4b6cee", 0x4b6ceeu, (void *)w_rl_4b6cee, 0, 0, 0, 0, "" },
    { "rl_4b6d19", 0x4b6d19u, (void *)w_rl_4b6d19, 0, 0, 0, 0, "" },
    { "rl_4b6d44", 0x4b6d44u, (void *)w_rl_4b6d44, 0, 0, 0, 0, "" },
    { "rl_4b6dc5", 0x4b6dc5u, (void *)w_rl_4b6dc5, 0, 0, 0, 0, "" },
    { "rl_4b6de6", 0x4b6de6u, (void *)w_rl_4b6de6, 0, 0, 0, 0, "" },
    { "rl_4b6e1b", 0x4b6e1bu, (void *)w_rl_4b6e1b, 0, 0, 0, 0, "" },
    { "rl_4b6e8e", 0x4b6e8eu, (void *)w_rl_4b6e8e, 0, 0, 0, 0, "" },
    { "rl_4b7052", 0x4b7052u, (void *)w_rl_4b7052, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b7514", 0x4b7514u, (void *)w_rl_4b7514, 0, 0, 0, 0, "" },
    { "rl_4b7a9a", 0x4b7a9au, (void *)w_rl_4b7a9a, 0, 0, 0, 0, "" },
    { "rl_4b7f78", 0x4b7f78u, (void *)w_rl_4b7f78, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b90fb", 0x4b90fbu, (void *)w_rl_4b90fb, 0, 0, 0, 0, "" },
    { "rl_4bbf89", 0x4bbf89u, (void *)w_rl_4bbf89, 0, 0, 0, 0, "" },
    { "rl_4bbfd2", 0x4bbfd2u, (void *)w_rl_4bbfd2, 0, 0, 0, 0, "" },
    { "rl_43306a", 0x43306au, (void *)w_rl_43306a, 0, 0, 0, 0, "" },
    { "rl_46f2f1", 0x46f2f1u, (void *)w_rl_46f2f1, 0, 0, 0, 0, "" },
    { "rl_47f937", 0x47f937u, (void *)w_rl_47f937, 0, 0, 0, 0, "" },
    { "rl_49b973", 0x49b973u, (void *)w_rl_49b973, 0, 0, 0, 0, "" },
    { "rl_49ca60", 0x49ca60u, (void *)w_rl_49ca60, 0, 0, 0, 0, "" },
    { "rl_4b3d5d", 0x4b3d5du, (void *)w_rl_4b3d5d, 0, 0, 0, 0, "" },
    { "rl_433551", 0x433551u, (void *)w_rl_433551, 0, 0, 0, 0, "" },
    { "rl_41f3e9", 0x41f3e9u, (void *)w_rl_41f3e9, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41f4a1", 0x41f4a1u, (void *)w_rl_41f4a1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41f77a", 0x41f77au, (void *)w_rl_41f77a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_475e07", 0x475e07u, (void *)w_rl_475e07, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_RL_MPNET_N = sizeof RH_HOOKS_RL_MPNET / sizeof *RH_HOOKS_RL_MPNET;
