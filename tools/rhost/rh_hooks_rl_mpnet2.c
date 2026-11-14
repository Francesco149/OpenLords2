/* rh_hooks_rl_mpnet2.c — a retail-layout port, partition 'mpnet2' (src/rl_mpnet2.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). docs/findings/rl-mpnet2.md.
 *
 * THISCALL rows (the SIGS session's teardown 0x4b64a3 / 0x4b6a15 / 0x4b6ab6 / 0x4b6ef7 / 0x4b92a0: ECX = the session,
 * the callee pops its arguments): rh_hooks_rl_mpnet.c's HOOK_TC, copied (its statics are per file) — the entry stub
 * saves ECX, the row's `this` is the port's first argument, the retail side runs through a per-row thunk that loads
 * ECX. None of them runs on the monitor coroutine's stack; the stub keeps mpnet's coroutine bypass anyway.
 *
 * __stdcall rows: the DLGPROCs 0x4b8335 / 0x4b8439 / 0x4b85b7 (4 arguments; rh_win.c's fake DialogBoxParamA calls the
 * pointer: entry patch), EnumSessions' callback 0x4b8b87 (4), DirectPlayEnumerateA's 0x4b82bf (5). RH_SH_HOST where the
 * call adds a row to rh_win.c's fake list box (g_lb / g_lbn: host state no shadow rolls back — a shadow's second
 * LB_ADDSTRING lands on the next row): 0x4b82bf, 0x4b8b87 and 0x4b85b7 (EnumSessions calls 0x4b8b87). 0x4b8335 / 0x4b8439
 * only EndDialog / WM_SETTEXT / SetFocus (idempotent host stores): shadowed.
 *
 * 0x4b79e3 (TellMeSpecialChatReceived's callback, cdecl varargs) prints "%s: %s" with &arg_4: SierraNW passes the
 * sender's name by value on the stack, so the adapter passes its address on retail's stack (e0 + 8), not a copy.
 * 0x40ec2c (a message pump step: GetMessage / Dispatch are host queue state): RH_SH_HOST. */
#include "rh_port.h"
#include "rl.h"
#include "rl_mpnet2_globals.h"
#include "rh_rl_mpnet2_alias.h"

#include <string.h>

__asm__(RH_RL_MPNET2_ALIASES);

#define RH_TABLE RH_HOOKS_RL_MPNET2
extern hook_t RH_HOOKS_RL_MPNET2[];

/* row indices (must follow the table's order) */
enum {
    X_rl_4b64a3, X_rl_4b6a15, X_rl_4b6ab6, X_rl_4b6ef7, X_rl_4b92a0, X_rl_4b743b, X_rl_4b756b, X_rl_4b79e3, X_rl_4b7a7c,
    X_rl_4b8243, X_rl_4b82bf, X_rl_4b8335, X_rl_4b8439, X_rl_4b85b7, X_rl_4b8b87, X_rl_49792e, X_rl_479e0c, X_rl_43695d,
    X_rl_43338e, X_rl_42d40b, X_rl_40ec2c, X_rl_420630, X_rl_476488,
};

/* ---------------------------------------------------------------- thiscall (rh_hooks_rl_mpnet.c's machinery) */
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

/* ---------------------------------------------------------------- __stdcall (RH_ENTRY with `ret $POP`) */
#define RH_ENTRY_STD(W, B, POP) \
    static __attribute__((naked)) void W(void) \
    { __asm__("movl %ebp, _rh_entry_ebp\n\tmovl %esp, %eax\n\tleal -0x40000(%eax), %edx\n\tmovl _rh_c_floor, %ecx\n\t" \
              "testl %ecx, %ecx\n\tjz 1f\n\tsubl $0x400, %ecx\n\tcmpl %ecx, %edx\n\tjb 1f\n\tmovl %ecx, %edx\n" \
              "1:\tmovl %edx, %esp\n\tpushl %eax\n\tpushl %eax\n\t" \
              "call _" #B "\n\taddl $4, %esp\n\tpopl %esp\n\tret $" #POP); }
#define HOOK_R_STD(NAME, POP, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); } \
    RH_ENTRY_STD(w_##NAME, b_##NAME, POP)

/* the session's teardown (__thiscall) */
HOOK_TC(rl_4b64a3, 0, 1, rl_4b64a3(SELF))
HOOK_TC(rl_4b6a15, 0, 1, rl_4b6a15(SELF))
HOOK_TC(rl_4b6ab6, 0, 1, rl_4b6ab6(SELF))
HOOK_TC(rl_4b6ef7, 4, 1, rl_4b6ef7(SELF, I(0)))
HOOK_TC(rl_4b92a0, 4, 1, rl_4b92a0(SELF, I(0)))
/* leaving MP, the SierraNW callbacks (cdecl) */
HOOK_V(rl_4b743b, 0, rl_4b743b())   /* EAX: the last call's — no reader */
HOOK_R(rl_4b756b, 0, rl_4b756b())
/* 0x4b79e3: the name is BY VALUE at e0 + 8 (its address, not the adapter's copy) */
static uint32_t s_e0_4b79e3;
static uint32_t p_rl_4b79e3(const uint32_t *a) { rl_4b79e3(I(0), (int)(s_e0_4b79e3 + 8)); return 0; }
static void w_rl_4b79e3(void);
static __attribute__((used, noinline)) uint32_t __cdecl b_rl_4b79e3(uint32_t e0)
{
    uint32_t o = s_e0_4b79e3;
    s_e0_4b79e3 = e0;
    uint32_t r = rh_port_run(&RH_TABLE[X_rl_4b79e3], (void *)w_rl_4b79e3, p_rl_4b79e3, 0, e0);
    s_e0_4b79e3 = o;
    return r;
}
RH_ENTRY(w_rl_4b79e3, b_rl_4b79e3)
HOOK_R(rl_4b7a7c, 1, rl_4b7a7c(I(0)))
/* the DirectPlay connection, dialogs and callbacks */
HOOK_V(rl_4b8243, 1, rl_4b8243(I(0)))   /* EAX: Release's / DirectPlayCreate's / the caller's — no reader */
HOOK_R_STD(rl_4b82bf, 0x14, rl_4b82bf(I(0), I(1), I(2), I(3), I(4)))
HOOK_R_STD(rl_4b8335, 0x10, rl_4b8335(I(0), (unsigned)a[1], (unsigned)a[2], I(3)))
HOOK_R_STD(rl_4b8439, 0x10, rl_4b8439(I(0), (unsigned)a[1], (unsigned)a[2], I(3)))
HOOK_R_STD(rl_4b85b7, 0x10, rl_4b85b7(I(0), (unsigned)a[1], (unsigned)a[2], I(3)))
HOOK_R_STD(rl_4b8b87, 0x10, rl_4b8b87(I(0), I(1), I(2), I(3)))
/* MP save, popups, title / menu helpers */
HOOK_R(rl_49792e, 0, rl_49792e())
HOOK_R(rl_479e0c, 1, rl_479e0c(I(0)))
HOOK_V(rl_43695d, 0, rl_43695d())   /* EAX: 0x43eda0's — no reader */
HOOK_V(rl_43338e, 0, rl_43338e())   /* EAX: 0x43eda0's or the caller's — no reader */
HOOK_R(rl_42d40b, 0, rl_42d40b())
HOOK_R(rl_40ec2c, 0, rl_40ec2c())
HOOK_V(rl_420630, 1, rl_420630(I(0)))   /* render; EAX: 0x420147's — no reader */
HOOK_V(rl_476488, 0, rl_476488())       /* render; EAX: the last draw's — no reader */

hook_t RH_HOOKS_RL_MPNET2[] = {
    { "rl_4b64a3", 0x4b64a3u, (void *)w_rl_4b64a3, 0, 0, 0, 0, "" },
    { "rl_4b6a15", 0x4b6a15u, (void *)w_rl_4b6a15, 0, 0, 0, 0, "" },
    { "rl_4b6ab6", 0x4b6ab6u, (void *)w_rl_4b6ab6, 0, 0, 0, 0, "" },
    { "rl_4b6ef7", 0x4b6ef7u, (void *)w_rl_4b6ef7, 0, 0, 0, 0, "" },
    { "rl_4b92a0", 0x4b92a0u, (void *)w_rl_4b92a0, 0, 0, 0, 0, "" },
    { "rl_4b743b", 0x4b743bu, (void *)w_rl_4b743b, 0, 0, 0, 0, "" },
    { "rl_4b756b", 0x4b756bu, (void *)w_rl_4b756b, 0, 0, 0, 0, "" },
    { "rl_4b79e3", 0x4b79e3u, (void *)w_rl_4b79e3, 0, 0, 0, 0, "" },
    { "rl_4b7a7c", 0x4b7a7cu, (void *)w_rl_4b7a7c, 0, 0, 0, 0, "" },
    { "rl_4b8243", 0x4b8243u, (void *)w_rl_4b8243, 0, 0, 0, 0, "" },
    { "rl_4b82bf", 0x4b82bfu, (void *)w_rl_4b82bf, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b8335", 0x4b8335u, (void *)w_rl_4b8335, 0, 0, 0, 0, "" },
    { "rl_4b8439", 0x4b8439u, (void *)w_rl_4b8439, 0, 0, 0, 0, "" },
    { "rl_4b85b7", 0x4b85b7u, (void *)w_rl_4b85b7, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b8b87", 0x4b8b87u, (void *)w_rl_4b8b87, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_49792e", 0x49792eu, (void *)w_rl_49792e, 0, 0, 0, 0, "" },
    { "rl_479e0c", 0x479e0cu, (void *)w_rl_479e0c, 0, 0, 0, 0, "" },
    { "rl_43695d", 0x43695du, (void *)w_rl_43695d, 0, 0, 0, 0, "" },
    { "rl_43338e", 0x43338eu, (void *)w_rl_43338e, 0, 0, 0, 0, "" },
    { "rl_42d40b", 0x42d40bu, (void *)w_rl_42d40b, 0, 0, 0, 0, "" },
    { "rl_40ec2c", 0x40ec2cu, (void *)w_rl_40ec2c, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_420630", 0x420630u, (void *)w_rl_420630, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_476488", 0x476488u, (void *)w_rl_476488, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_RL_MPNET2_N = sizeof RH_HOOKS_RL_MPNET2 / sizeof *RH_HOOKS_RL_MPNET2;
