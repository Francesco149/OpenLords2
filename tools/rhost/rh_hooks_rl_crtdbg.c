/* rh_hooks_rl_crtdbg.c — rows for src/rl_crt_dbg.c (the report / message / exit / signal path: _CrtDbgReport and its window, _amsg_exit / _NMSG_WRITE, _exit / abort / raise, _flsbuf / _getbuf, the C++ unhandled-exception filter) vs retail, on identical
 * state (rh_hooks.h contract; plumbing rh_port.h; stack outputs rh_crt_so.h). Unreached by the corpus: the evidence is
 * `rhost.py probe tests/probes/crt-dbg.probe` (synthetic carriers) + the hybrid pass.
 *
 * HOST rows (RH_SH_HOST: they never return — _exit / _amsg_exit / abort / terminate end the process through doexit's
 * ExitProcess): proven by the hybrid pass only, no probe line (_fptrap 0x4cd4d0 = _amsg_exit(2): no carrier at all, retail
 * formats no floating point). __alloca_probe 0x4caae0 has no row (eax = size, moves
 * esp: not a C function). __CxxUnhandledExceptionFilter is __stdcall (RH_ENTRY with `ret $4`). */
#include "rh_port.h"
#include "rl_crt.h"
#include "rl_crt_dbg.h"

#define RH_TABLE RH_HOOKS_RL_CRTDBG
extern hook_t RH_HOOKS_RL_CRTDBG[];
enum {
    X_rl_crt_4c3950, X_rl_crt_4c3d40, X_rl_crt_4c37d0, X_rl_crt_4cb010, X_rl_crt_4c2090, X_rl_crt_4ca5d0,
    X_rl_crt_4ca630, X_rl_crt_4ca900, X_rl_crt_4ca9f0, X_rl_crt_4c6650, X_rl_crt_4cc4d0, X_rl_crt_4cc590,
    X_rl_crt_4ca330, X_rl_crt_4ca2d0, X_rl_crt_4c35c0, X_rl_crt_4bf710, X_rl_crt_4cad80, X_rl_crt_4cafa0,
    X_rl_crt_4ccb00, X_rl_crt_4c8700, X_rl_crt_4ccac0, X_rl_crt_4cc980, X_rl_crt_4cd4d0,
};
#include "rh_crt_so.h"

/* variadic rows (_CrtDbgReport, _snprintf): the port takes the address of the first variadic argument (retail's stack,
 * e0 + 4 + 4 * fixed — the same in both passes); a stack output (HOOK_SO spec) as HOOK_SO */
#define HOOK_VA(NAME, NFIX, SPECV, CALL) \
    static uint32_t s_e0_##NAME; \
    static const so_spec_t k_so_##NAME[] = SPECV; \
    static uint32_t p_##NAME(const uint32_t *a) \
    { \
        uint32_t argp = s_e0_##NAME + 4u + 4u * (NFIX); (void)argp; \
        so_port_pre(X_##NAME); uint32_t r_ = (uint32_t)(CALL); return so_port_post(X_##NAME, r_); \
    } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        so_ctx_t c, *keep = s_so; \
        uint32_t keep_e0 = s_e0_##NAME; \
        so_enter(&c, e0, k_so_##NAME); \
        s_so = &c; \
        s_e0_##NAME = e0; \
        uint32_t r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
        s_e0_##NAME = keep_e0; \
        s_so = keep; \
        so_leave(&c); \
        return r; \
    } \
    RH_ENTRY(w_##NAME, b_##NAME)

/* __stdcall, 1 argument: RH_ENTRY with `ret $4` (rh_call_at runs retail's stdcall at e0 and restores esp itself) */
#define RH_ENTRY_STD4(W, B) \
    static __attribute__((naked)) void W(void) \
    { __asm__("movl %ebp, _rh_entry_ebp\n\tmovl %esp, %eax\n\tleal -0x40000(%eax), %edx\n\tmovl _rh_c_floor, %ecx\n\t" \
              "testl %ecx, %ecx\n\tjz 1f\n\tsubl $0x400, %ecx\n\tcmpl %ecx, %edx\n\tjb 1f\n\tmovl %ecx, %edx\n" \
              "1:\tmovl %edx, %esp\n\tpushl %eax\n\tpushl %eax\n\t" \
              "call _" #B "\n\taddl $4, %esp\n\tpopl %esp\n\tret $0x4"); }
#define HOOK_R_STD4(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); } \
    RH_ENTRY_STD4(w_##NAME, b_##NAME)

/* ---------------------------------------------------------------- rows */
/* the module name (argument 3) is cut in place by _CrtMessageWindow: a stack output */
HOOK_VA(rl_crt_4c3950, 5, SPEC({ 3, 2, 3 }), rl_crt_4c3950(I(0), a[1], I(2), a[3], a[4], argp))
HOOK_SO(rl_crt_4c3d40, 5, SPEC({ 3, 2, 3 }), rl_crt_4c3d40(I(0), a[1], a[2], a[3], a[4]))
HOOK_V(rl_crt_4c37d0, 0, rl_crt_4c37d0())
HOOK_R(rl_crt_4cb010, 3, rl_crt_4cb010(a[0], a[1], a[2]))
HOOK_SO(rl_crt_4c2090, 2, SPEC({ 0, 3, 4 }), rl_crt_4c2090(a[0], a[1]))
HOOK_SO(rl_crt_4ca5d0, 3, SPEC({ 1, 3, 3 }), rl_crt_4ca5d0(I(0), a[1], I(2)))
HOOK_SO(rl_crt_4ca630, 4, SPEC({ 1, 3, 3 }), rl_crt_4ca630(a[0], a[1], a[2], I(3)))
HOOK_VA(rl_crt_4ca900, 3, SPEC({ 0, 1, 1 }), rl_crt_4ca900(a[0], a[1], a[2], argp))
HOOK_SO(rl_crt_4ca9f0, 4, SPEC({ 0, 1, 1 }), rl_crt_4ca9f0(a[0], a[1], a[2], a[3]))
HOOK_R(rl_crt_4c6650, 2, rl_crt_4c6650(I(0), a[1]))
HOOK_R(rl_crt_4cc4d0, 1, rl_crt_4cc4d0(a[0]))
HOOK_R(rl_crt_4cc590, 1, rl_crt_4cc590(I(0)))
HOOK_R(rl_crt_4ca330, 1, rl_crt_4ca330(I(0)))
HOOK_V(rl_crt_4ca2d0, 0, rl_crt_4ca2d0())
HOOK_V(rl_crt_4c35c0, 1, rl_crt_4c35c0(I(0)))
HOOK_V(rl_crt_4bf710, 1, rl_crt_4bf710(I(0)))
HOOK_R(rl_crt_4cad80, 1, rl_crt_4cad80(I(0)))
HOOK_R(rl_crt_4cafa0, 1, rl_crt_4cafa0(I(0)))
HOOK_V(rl_crt_4ccb00, 0, rl_crt_4ccb00())
HOOK_V(rl_crt_4cd4d0, 0, rl_crt_4cd4d0())
HOOK_V(rl_crt_4c8700, 0, rl_crt_4c8700())
HOOK_R(rl_crt_4ccac0, 1, rl_crt_4ccac0(a[0]))
HOOK_R_STD4(rl_crt_4cc980, 1, rl_crt_4cc980(a[0]))

hook_t RH_HOOKS_RL_CRTDBG[] = {
    { "rl_crt_4c3950", 0x4c3950u, (void *)w_rl_crt_4c3950, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c3d40", 0x4c3d40u, (void *)w_rl_crt_4c3d40, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c37d0", 0x4c37d0u, (void *)w_rl_crt_4c37d0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cb010", 0x4cb010u, (void *)w_rl_crt_4cb010, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c2090", 0x4c2090u, (void *)w_rl_crt_4c2090, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ca5d0", 0x4ca5d0u, (void *)w_rl_crt_4ca5d0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ca630", 0x4ca630u, (void *)w_rl_crt_4ca630, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ca900", 0x4ca900u, (void *)w_rl_crt_4ca900, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ca9f0", 0x4ca9f0u, (void *)w_rl_crt_4ca9f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c6650", 0x4c6650u, (void *)w_rl_crt_4c6650, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cc4d0", 0x4cc4d0u, (void *)w_rl_crt_4cc4d0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cc590", 0x4cc590u, (void *)w_rl_crt_4cc590, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ca330", 0x4ca330u, (void *)w_rl_crt_4ca330, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ca2d0", 0x4ca2d0u, (void *)w_rl_crt_4ca2d0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c35c0", 0x4c35c0u, (void *)w_rl_crt_4c35c0, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4bf710", 0x4bf710u, (void *)w_rl_crt_4bf710, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4cad80", 0x4cad80u, (void *)w_rl_crt_4cad80, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cafa0", 0x4cafa0u, (void *)w_rl_crt_4cafa0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4ccb00", 0x4ccb00u, (void *)w_rl_crt_4ccb00, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4c8700", 0x4c8700u, (void *)w_rl_crt_4c8700, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4ccac0", 0x4ccac0u, (void *)w_rl_crt_4ccac0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cc980", 0x4cc980u, (void *)w_rl_crt_4cc980, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd4d0", 0x4cd4d0u, (void *)w_rl_crt_4cd4d0, 0, 0, 0, 0, "", 0, RH_SH_HOST },
};
const int RH_HOOKS_RL_CRTDBG_N = sizeof RH_HOOKS_RL_CRTDBG / sizeof *RH_HOOKS_RL_CRTDBG;
