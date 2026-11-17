/* rh_hooks_rl_crt.c — retail's MSVC debug CRT on retail's layout (src/rl_crt.c, docs/findings/crt-port.md) vs retail,
 * on identical state (rh_hooks.h contract; plumbing rh_port.h). One row per ported function, rl_crt_<va>.
 *
 * The CRT's state is retail's own (.data / .bss by VA, rhost's heap arena through retail's IAT slots), so a shadow
 * compares the debug heap's block list, the sbh pages, __pioinfo, errno, the tz / time statics like any state.
 * Host layers the shadow rolls back for these rows: the heap arena's metadata (rh_heap), the fake handles and, for a
 * file opened before the call, its OS position / a close (rh_handles: _read / _lseek / _write / _close rows).
 *
 * STACK OUTPUTS.  A CRT function writing through a pointer into its caller's stack (__sbh_find_block's out
 * parameters, parse_cmdline's counts, time(&t), _read / getcwd / strcpy / memset into a local buffer) writes memory
 * the shadow does not track: HOOK_SO saves those bytes at entry, puts them back before the port side (after noting
 * retail's results) and compares the port's with retail's — a mismatch is a divergence (the port's return value is
 * flipped, the bytes logged). Only in shadow mode.
 *
 * HOST rows (RH_SH_HOST, proven by the hybrid pass only): start / exit / doexit (they never return), and the
 * C++ EH filter install (0x4cc930 / 0x4cc950 / 0x4cca20) with its callers _cinit / _initterm — rhost's
 * SetUnhandledExceptionFilter keeps the filter host-side (not rolled back), so a second run returns the first's. */
#include "rh_port.h"
#include "rl_crt.h"
#include "rh_rl_crt_data_globals_alias.h"

__asm__(RH_RL_CRT_DATA_GLOBALS_ALIASES);   /* src/rl_crt_data_globals.h at retail's VAs (the CRT reads them by VA; the names are for the data image) */

#define RH_TABLE RH_HOOKS_RL_CRT
extern hook_t RH_HOOKS_RL_CRT[];
enum {
    X_rl_crt_4bf620, X_rl_crt_4c2080, X_rl_crt_4c2170, X_rl_crt_4c2200,
    X_rl_crt_4c45b0, X_rl_crt_4c4480, X_rl_crt_4c49d0, X_rl_crt_4c4a80, X_rl_crt_4c4fe0, X_rl_crt_4c4b10,
    X_rl_crt_4c3760, X_rl_crt_4c4370, X_rl_crt_4c4070, X_rl_crt_4bfb30, X_rl_crt_4bfa90, X_rl_crt_4bfa00,
    X_rl_crt_4bfa30, X_rl_crt_4bfea0, X_rl_crt_4c0c60, X_rl_crt_4c1070, X_rl_crt_4c1110, X_rl_crt_4c1170,
    X_rl_crt_4c09f0,
    X_rl_crt_4c5930, X_rl_crt_4c9e50, X_rl_crt_4cccc0, X_rl_crt_4cce10, X_rl_crt_4ccf10, X_rl_crt_4cd030,
    X_rl_crt_4cc3b0, X_rl_crt_4ce640, X_rl_crt_4cea50, X_rl_crt_4cea00, X_rl_crt_4ceff0, X_rl_crt_4cc0b0,
    X_rl_crt_4cf490, X_rl_crt_4cf8f0, X_rl_crt_4cf550, X_rl_crt_4c1b40, X_rl_crt_4c1b10, X_rl_crt_4c57f0,
    X_rl_crt_4c1cf0, X_rl_crt_4c2260, X_rl_crt_4c22c0,
    X_rl_crt_4c6130, X_rl_crt_4c5dc0, X_rl_crt_4cbfc0, X_rl_crt_4c5a60, X_rl_crt_4c5a30, X_rl_crt_4c6530,
    X_rl_crt_4c2740, X_rl_crt_4c6340, X_rl_crt_4c24e0, X_rl_crt_4c24a0, X_rl_crt_4c2340,
    X_rl_crt_4c8cc0, X_rl_crt_4c8990, X_rl_crt_4c8e90, X_rl_crt_4c92f0, X_rl_crt_4c9260,
    X_rl_crt_4c9bf0, X_rl_crt_4c9570, X_rl_crt_4c94a0, X_rl_crt_4c9360,
    X_rl_crt_4cc5e0, X_rl_crt_4cd2f0, X_rl_crt_4cd270, X_rl_crt_4cd3d0, X_rl_crt_4cd3b0, X_rl_crt_4cc740,
    X_rl_crt_4cd4f0, X_rl_crt_4cd5b0, X_rl_crt_4cd5e0, X_rl_crt_4cc950, X_rl_crt_4cc930, X_rl_crt_4cca20,
    X_rl_crt_4bf870, X_rl_crt_4bf6a0, X_rl_crt_4bf770, X_rl_crt_4bf6f0, X_rl_crt_4c3370,
    X_rl_crt_4bfa60, X_rl_crt_4c3200, X_rl_crt_4c28d0, X_rl_crt_4c8870, X_rl_crt_4c2fb0, X_rl_crt_4bf60e, X_rl_crt_4c29d0,
    X_rl_crt_4c77b0, X_rl_crt_4c7680, X_rl_crt_4c7700, X_rl_crt_4c7750, X_rl_crt_4c68f0, X_rl_crt_4c2a80,
};

#include "rh_crt_so.h"

/* find_block's out parameters, through the adapter's own locals copied out (the port takes uint32_t *) */
static uint32_t find_block(const uint32_t *a)
{
    uint32_t hdr = 0, page = 0;
    uint32_t r = rl_crt_4c49d0(a[0], &hdr, &page);
    if (r != 0) { G32(a[1]) = hdr; G32(a[2]) = page; }
    return r;
}

/* sprintf is variadic: the port takes the address of the first variadic argument (e0 + 12, retail's stack — the same
 * in both passes); the buffer is a stack output (HOOK_SO kind 3: 512 bytes) */
static uint32_t s_sprintf_e0;
static const so_spec_t k_so_rl_crt_4c2a80[] = SPEC({ 0, 3, 32 });
static uint32_t p_rl_crt_4c2a80(const uint32_t *a)
{
    so_port_pre(X_rl_crt_4c2a80);
    uint32_t r = (uint32_t)rl_crt_4c2a80(a[0], a[1], s_sprintf_e0 + 12u);
    return so_port_post(X_rl_crt_4c2a80, r);
}
static void w_rl_crt_4c2a80(void);
static __attribute__((used, noinline)) uint32_t __cdecl b_rl_crt_4c2a80(uint32_t e0)
{
    so_ctx_t c, *keep = s_so;
    uint32_t keep_e0 = s_sprintf_e0;
    so_enter(&c, e0, k_so_rl_crt_4c2a80);
    s_so = &c;
    s_sprintf_e0 = e0;
    uint32_t r = rh_port_run(&RH_TABLE[X_rl_crt_4c2a80], (void *)w_rl_crt_4c2a80, p_rl_crt_4c2a80, 1, e0);
    s_sprintf_e0 = keep_e0;
    s_so = keep;
    so_leave(&c);
    return r;
}
RH_ENTRY(w_rl_crt_4c2a80, b_rl_crt_4c2a80)

/* ---------------------------------------------------------------- rows */
HOOK_R(rl_crt_4bf620, 1, rl_crt_4bf620(a[0]))
HOOK_SO(rl_crt_4c2080, 2, SPEC({ 0, 2, 1 }), rl_crt_4c2080(a[0], a[1]))
HOOK_R(rl_crt_4c2170, 2, rl_crt_4c2170(a[0], a[1]))
HOOK_SO(rl_crt_4c2200, 3, SPEC({ 0, 1, 2 }), rl_crt_4c2200(a[0], I(1), a[2]))
HOOK_R(rl_crt_4c45b0, 0, rl_crt_4c45b0())
HOOK_R(rl_crt_4c4480, 0, rl_crt_4c4480())
HOOK_SO(rl_crt_4c49d0, 3, SPEC({ 1, 0, 4 }, { 2, 0, 4 }), find_block(a))
HOOK_V(rl_crt_4c4a80, 3, rl_crt_4c4a80(a[0], a[1], a[2]))
HOOK_R(rl_crt_4c4fe0, 3, rl_crt_4c4fe0(a[0], a[1], a[2]))
HOOK_R(rl_crt_4c4b10, 1, rl_crt_4c4b10(a[0]))
HOOK_R(rl_crt_4c3760, 1, rl_crt_4c3760(a[0]))
HOOK_V(rl_crt_4c4370, 1, rl_crt_4c4370(a[0]))
HOOK_R(rl_crt_4c4070, 7, rl_crt_4c4070(I(0), a[1], I(2), I(3), I(4), a[5], I(6)))
HOOK_R(rl_crt_4bfb30, 4, rl_crt_4bfb30(a[0], I(1), a[2], I(3)))
HOOK_R(rl_crt_4bfa90, 5, rl_crt_4bfa90(a[0], I(1), I(2), a[3], I(4)))
HOOK_R(rl_crt_4bfa00, 1, rl_crt_4bfa00(a[0]))
HOOK_R(rl_crt_4bfa30, 4, rl_crt_4bfa30(a[0], I(1), a[2], I(3)))
HOOK_R(rl_crt_4bfea0, 5, rl_crt_4bfea0(a[0], a[1], I(2), a[3], I(4)))
HOOK_R(rl_crt_4c0c60, 3, rl_crt_4c0c60(a[0], I(1), I(2)))
HOOK_R(rl_crt_4c1070, 1, rl_crt_4c1070(I(0)))
HOOK_R(rl_crt_4c1110, 3, rl_crt_4c1110(a[0], a[1], I(2)))
HOOK_R(rl_crt_4c1170, 1, rl_crt_4c1170(a[0]))
HOOK_R(rl_crt_4c09f0, 2, rl_crt_4c09f0(a[0], I(1)))
HOOK_V(rl_crt_4c5930, 1, rl_crt_4c5930(a[0]))
HOOK_R(rl_crt_4c9e50, 0, rl_crt_4c9e50())
HOOK_R(rl_crt_4cccc0, 0, rl_crt_4cccc0())
HOOK_R(rl_crt_4cce10, 2, rl_crt_4cce10(a[0], a[1]))
HOOK_R(rl_crt_4ccf10, 1, rl_crt_4ccf10(a[0]))
HOOK_R(rl_crt_4cd030, 1, rl_crt_4cd030(a[0]))
HOOK_R(rl_crt_4cc3b0, 3, rl_crt_4cc3b0(a[0], I(1), I(2)))
HOOK_R(rl_crt_4ce640, 1, rl_crt_4ce640(a[0]))
HOOK_R(rl_crt_4cea50, 4, rl_crt_4cea50(a[0], I(1), I(2), I(3)))
HOOK_R(rl_crt_4cea00, 3, rl_crt_4cea00(a[0], I(1), I(2)))
HOOK_SO(rl_crt_4ceff0, 3, SPEC({ 1, 1, 2 }), rl_crt_4ceff0(a[0], a[1], a[2]))
HOOK_R(rl_crt_4cc0b0, 3, rl_crt_4cc0b0(a[0], a[1], a[2]))
HOOK_R(rl_crt_4cf490, 1, rl_crt_4cf490(a[0]))
HOOK_R(rl_crt_4cf8f0, 1, rl_crt_4cf8f0(a[0]))
HOOK_R(rl_crt_4cf550, 1, rl_crt_4cf550(a[0]))
HOOK_SO(rl_crt_4c1b40, 3, SPEC({ 1, 1, 2 }), rl_crt_4c1b40(I(0), a[1], I(2)))
HOOK_SO(rl_crt_4c1b10, 2, SPEC({ 0, 1, 1 }), rl_crt_4c1b10(a[0], I(1)))
HOOK_R(rl_crt_4c57f0, 1, rl_crt_4c57f0(I(0)))
HOOK_R(rl_crt_4c1cf0, 0, rl_crt_4c1cf0())
HOOK_R(rl_crt_4c2260, 2, rl_crt_4c2260(a[0], a[1]))
HOOK_R(rl_crt_4c22c0, 1, rl_crt_4c22c0(a[0]))
/* cvtdate takes 11 arguments (the adapter sees 10): the 11th from the caller's stack at entry */
static uint32_t s_cvt_e0;
static uint32_t p_rl_crt_4c6130(const uint32_t *a)
{ return (uint32_t)rl_crt_4c6130(I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9), (int)G32(s_cvt_e0 + 4u + 40u)); }
static void w_rl_crt_4c6130(void);
static __attribute__((used, noinline)) uint32_t __cdecl b_rl_crt_4c6130(uint32_t e0)
{
    uint32_t keep = s_cvt_e0;
    s_cvt_e0 = e0;
    uint32_t r = rh_port_run(&RH_TABLE[X_rl_crt_4c6130], (void *)w_rl_crt_4c6130, p_rl_crt_4c6130, 1, e0);
    s_cvt_e0 = keep;
    return r;
}
RH_ENTRY(w_rl_crt_4c6130, b_rl_crt_4c6130)
HOOK_R(rl_crt_4c5dc0, 1, rl_crt_4c5dc0(a[0]))
HOOK_R(rl_crt_4cbfc0, 1, rl_crt_4cbfc0(a[0]))
HOOK_V(rl_crt_4c5a60, 0, rl_crt_4c5a60())
HOOK_V(rl_crt_4c5a30, 0, rl_crt_4c5a30())
HOOK_R(rl_crt_4c6530, 7, rl_crt_4c6530(I(0), I(1), I(2), I(3), I(4), I(5), I(6)))
HOOK_SO(rl_crt_4c2740, 1, SPEC({ 0, 0, 4 }), rl_crt_4c2740(a[0]))
HOOK_R(rl_crt_4c6340, 1, rl_crt_4c6340(a[0]))
HOOK_R(rl_crt_4c24e0, 1, rl_crt_4c24e0(a[0]))
HOOK_R(rl_crt_4c24a0, 2, rl_crt_4c24a0(a[0], I(1)))
HOOK_R(rl_crt_4c2340, 1, rl_crt_4c2340(a[0]))
HOOK_R(rl_crt_4c8cc0, 1, rl_crt_4c8cc0(I(0)))
HOOK_R(rl_crt_4c8990, 1, rl_crt_4c8990(I(0)))
HOOK_R(rl_crt_4c8e90, 0, rl_crt_4c8e90())
HOOK_R(rl_crt_4c92f0, 3, rl_crt_4c92f0(a[0], I(1), I(2)))
HOOK_R(rl_crt_4c9260, 1, rl_crt_4c9260(a[0]))
HOOK_R(rl_crt_4c9bf0, 0, rl_crt_4c9bf0())
HOOK_SO(rl_crt_4c9570, 5, SPEC({ 3, 0, 4 }, { 4, 0, 4 }), rl_crt_4c9570(a[0], a[1], a[2], a[3], a[4]))
HOOK_R(rl_crt_4c94a0, 0, rl_crt_4c94a0())
HOOK_R(rl_crt_4c9360, 0, rl_crt_4c9360())
HOOK_V(rl_crt_4cc5e0, 0, rl_crt_4cc5e0())
HOOK_R(rl_crt_4cd2f0, 1, rl_crt_4cd2f0(a[0]))
HOOK_R(rl_crt_4cd270, 1, rl_crt_4cd270(a[0]))
HOOK_R(rl_crt_4cd3d0, 1, rl_crt_4cd3d0(I(0)))
HOOK_R(rl_crt_4cd3b0, 0, rl_crt_4cd3b0())
HOOK_R(rl_crt_4cc740, 0, rl_crt_4cc740())
HOOK_R(rl_crt_4cd4f0, 1, rl_crt_4cd4f0(a[0]))
HOOK_R(rl_crt_4cd5b0, 1, rl_crt_4cd5b0(a[0]))
HOOK_R(rl_crt_4cd5e0, 0, rl_crt_4cd5e0())
HOOK_R(rl_crt_4cc950, 0, rl_crt_4cc950())
HOOK_R(rl_crt_4cc930, 0, rl_crt_4cc930())
HOOK_V(rl_crt_4cca20, 0, rl_crt_4cca20())
HOOK_V(rl_crt_4bf870, 2, rl_crt_4bf870(a[0], a[1]))
HOOK_V(rl_crt_4bf6a0, 0, rl_crt_4bf6a0())
HOOK_V(rl_crt_4bf770, 3, rl_crt_4bf770(I(0), I(1), I(2)))
HOOK_V(rl_crt_4bf6f0, 1, rl_crt_4bf6f0(I(0)))
HOOK_V(rl_crt_4c3370, 0, rl_crt_4c3370())
HOOK_R(rl_crt_4bfa60, 2, rl_crt_4bfa60(a[0], I(1)))
HOOK_R(rl_crt_4c3200, 1, rl_crt_4c3200(a[0]))
HOOK_SO(rl_crt_4c28d0, 3, SPEC({ 0, 1, 2 }), rl_crt_4c28d0(a[0], a[1], a[2]))
HOOK_SO(rl_crt_4c8870, 3, SPEC({ 0, 1, 2 }), rl_crt_4c8870(a[0], a[1], a[2]))
/* the DirectPlayEnumerateA import thunk 0x4bf60e is __stdcall (2 arguments: jmp [IAT] into DPLAYX's): RH_ENTRY with
 * `ret $8` (the entry patcher decodes ff 25 since 2026-11-14) */
#define RH_ENTRY_STD8(W, B) \
    static __attribute__((naked)) void W(void) \
    { __asm__("movl %ebp, _rh_entry_ebp\n\tmovl %esp, %eax\n\tleal -0x40000(%eax), %edx\n\tmovl _rh_c_floor, %ecx\n\t" \
              "testl %ecx, %ecx\n\tjz 1f\n\tsubl $0x400, %ecx\n\tcmpl %ecx, %edx\n\tjb 1f\n\tmovl %ecx, %edx\n" \
              "1:\tmovl %edx, %esp\n\tpushl %eax\n\tpushl %eax\n\t" \
              "call _" #B "\n\taddl $4, %esp\n\tpopl %esp\n\tret $0x8"); }
#define HOOK_R_STD8(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); } \
    RH_ENTRY_STD8(w_##NAME, b_##NAME)
HOOK_R_STD8(rl_crt_4bf60e, 2, rl_crt_4bf60e(I(0), I(1)))
HOOK_V(rl_crt_4c29d0, 1, rl_crt_4c29d0(I(0)))
HOOK_SO(rl_crt_4c2fb0, 5, SPEC({ 1, 0, 3 }, { 2, 3, 16 }, { 3, 3, 16 }, { 4, 3, 16 }), rl_crt_4c2fb0(a[0], a[1], a[2], a[3], a[4]))
HOOK_SO(rl_crt_4c77b0, 1, SPEC({ 0, 0, 4 }), rl_crt_4c77b0(a[0]))
HOOK_SO(rl_crt_4c7680, 3, SPEC({ 1, 0, 8 }, { 1, 4, 1 }, { 2, 0, 4 }), rl_crt_4c7680(I(0), a[1], a[2]))
HOOK_SO(rl_crt_4c7700, 4, SPEC({ 2, 0, 8 }, { 2, 4, 32 }, { 3, 0, 4 }), rl_crt_4c7700(I(0), I(1), a[2], a[3]))
HOOK_SO(rl_crt_4c7750, 4, SPEC({ 2, 0, 8 }, { 2, 4, 32 }, { 3, 0, 4 }), rl_crt_4c7750(a[0], I(1), a[2], a[3]))
HOOK_SO(rl_crt_4c68f0, 3, SPEC({ 0, 0, 8 }, { 0, 4, 32 }), rl_crt_4c68f0(a[0], a[1], a[2]))

hook_t RH_HOOKS_RL_CRT[] = {
    { "rl_crt_4bf620", 0x4bf620u, (void *)w_rl_crt_4bf620, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c2080", 0x4c2080u, (void *)w_rl_crt_4c2080, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c2170", 0x4c2170u, (void *)w_rl_crt_4c2170, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c2200", 0x4c2200u, (void *)w_rl_crt_4c2200, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c45b0", 0x4c45b0u, (void *)w_rl_crt_4c45b0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c4480", 0x4c4480u, (void *)w_rl_crt_4c4480, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c49d0", 0x4c49d0u, (void *)w_rl_crt_4c49d0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c4a80", 0x4c4a80u, (void *)w_rl_crt_4c4a80, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c4fe0", 0x4c4fe0u, (void *)w_rl_crt_4c4fe0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c4b10", 0x4c4b10u, (void *)w_rl_crt_4c4b10, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c3760", 0x4c3760u, (void *)w_rl_crt_4c3760, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c4370", 0x4c4370u, (void *)w_rl_crt_4c4370, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c4070", 0x4c4070u, (void *)w_rl_crt_4c4070, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4bfb30", 0x4bfb30u, (void *)w_rl_crt_4bfb30, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4bfa90", 0x4bfa90u, (void *)w_rl_crt_4bfa90, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4bfa00", 0x4bfa00u, (void *)w_rl_crt_4bfa00, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4bfa30", 0x4bfa30u, (void *)w_rl_crt_4bfa30, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4bfea0", 0x4bfea0u, (void *)w_rl_crt_4bfea0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c0c60", 0x4c0c60u, (void *)w_rl_crt_4c0c60, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c1070", 0x4c1070u, (void *)w_rl_crt_4c1070, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c1110", 0x4c1110u, (void *)w_rl_crt_4c1110, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c1170", 0x4c1170u, (void *)w_rl_crt_4c1170, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c09f0", 0x4c09f0u, (void *)w_rl_crt_4c09f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c5930", 0x4c5930u, (void *)w_rl_crt_4c5930, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c9e50", 0x4c9e50u, (void *)w_rl_crt_4c9e50, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cccc0", 0x4cccc0u, (void *)w_rl_crt_4cccc0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cce10", 0x4cce10u, (void *)w_rl_crt_4cce10, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4ccf10", 0x4ccf10u, (void *)w_rl_crt_4ccf10, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cd030", 0x4cd030u, (void *)w_rl_crt_4cd030, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cc3b0", 0x4cc3b0u, (void *)w_rl_crt_4cc3b0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4ce640", 0x4ce640u, (void *)w_rl_crt_4ce640, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cea50", 0x4cea50u, (void *)w_rl_crt_4cea50, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cea00", 0x4cea00u, (void *)w_rl_crt_4cea00, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4ceff0", 0x4ceff0u, (void *)w_rl_crt_4ceff0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cc0b0", 0x4cc0b0u, (void *)w_rl_crt_4cc0b0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cf490", 0x4cf490u, (void *)w_rl_crt_4cf490, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4cf8f0", 0x4cf8f0u, (void *)w_rl_crt_4cf8f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cf550", 0x4cf550u, (void *)w_rl_crt_4cf550, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c1b40", 0x4c1b40u, (void *)w_rl_crt_4c1b40, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c1b10", 0x4c1b10u, (void *)w_rl_crt_4c1b10, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c57f0", 0x4c57f0u, (void *)w_rl_crt_4c57f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c1cf0", 0x4c1cf0u, (void *)w_rl_crt_4c1cf0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c2260", 0x4c2260u, (void *)w_rl_crt_4c2260, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c22c0", 0x4c22c0u, (void *)w_rl_crt_4c22c0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c6130", 0x4c6130u, (void *)w_rl_crt_4c6130, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c5dc0", 0x4c5dc0u, (void *)w_rl_crt_4c5dc0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cbfc0", 0x4cbfc0u, (void *)w_rl_crt_4cbfc0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c5a60", 0x4c5a60u, (void *)w_rl_crt_4c5a60, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c5a30", 0x4c5a30u, (void *)w_rl_crt_4c5a30, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c6530", 0x4c6530u, (void *)w_rl_crt_4c6530, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c2740", 0x4c2740u, (void *)w_rl_crt_4c2740, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c6340", 0x4c6340u, (void *)w_rl_crt_4c6340, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c24e0", 0x4c24e0u, (void *)w_rl_crt_4c24e0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c24a0", 0x4c24a0u, (void *)w_rl_crt_4c24a0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c2340", 0x4c2340u, (void *)w_rl_crt_4c2340, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c8cc0", 0x4c8cc0u, (void *)w_rl_crt_4c8cc0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c8990", 0x4c8990u, (void *)w_rl_crt_4c8990, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c8e90", 0x4c8e90u, (void *)w_rl_crt_4c8e90, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c92f0", 0x4c92f0u, (void *)w_rl_crt_4c92f0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c9260", 0x4c9260u, (void *)w_rl_crt_4c9260, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c9bf0", 0x4c9bf0u, (void *)w_rl_crt_4c9bf0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c9570", 0x4c9570u, (void *)w_rl_crt_4c9570, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c94a0", 0x4c94a0u, (void *)w_rl_crt_4c94a0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c9360", 0x4c9360u, (void *)w_rl_crt_4c9360, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cc5e0", 0x4cc5e0u, (void *)w_rl_crt_4cc5e0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd2f0", 0x4cd2f0u, (void *)w_rl_crt_4cd2f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd270", 0x4cd270u, (void *)w_rl_crt_4cd270, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd3d0", 0x4cd3d0u, (void *)w_rl_crt_4cd3d0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd3b0", 0x4cd3b0u, (void *)w_rl_crt_4cd3b0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cc740", 0x4cc740u, (void *)w_rl_crt_4cc740, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd4f0", 0x4cd4f0u, (void *)w_rl_crt_4cd4f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd5b0", 0x4cd5b0u, (void *)w_rl_crt_4cd5b0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cd5e0", 0x4cd5e0u, (void *)w_rl_crt_4cd5e0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4cc950", 0x4cc950u, (void *)w_rl_crt_4cc950, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4cc930", 0x4cc930u, (void *)w_rl_crt_4cc930, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4cca20", 0x4cca20u, (void *)w_rl_crt_4cca20, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4bf870", 0x4bf870u, (void *)w_rl_crt_4bf870, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4bf6a0", 0x4bf6a0u, (void *)w_rl_crt_4bf6a0, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4bf770", 0x4bf770u, (void *)w_rl_crt_4bf770, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4bf6f0", 0x4bf6f0u, (void *)w_rl_crt_4bf6f0, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4c3370", 0x4c3370u, (void *)w_rl_crt_4c3370, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_crt_4bfa60", 0x4bfa60u, (void *)w_rl_crt_4bfa60, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c3200", 0x4c3200u, (void *)w_rl_crt_4c3200, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c28d0", 0x4c28d0u, (void *)w_rl_crt_4c28d0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c8870", 0x4c8870u, (void *)w_rl_crt_4c8870, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c2fb0", 0x4c2fb0u, (void *)w_rl_crt_4c2fb0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4bf60e", 0x4bf60eu, (void *)w_rl_crt_4bf60e, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c29d0", 0x4c29d0u, (void *)w_rl_crt_4c29d0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c77b0", 0x4c77b0u, (void *)w_rl_crt_4c77b0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c7680", 0x4c7680u, (void *)w_rl_crt_4c7680, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c7700", 0x4c7700u, (void *)w_rl_crt_4c7700, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c7750", 0x4c7750u, (void *)w_rl_crt_4c7750, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_crt_4c68f0", 0x4c68f0u, (void *)w_rl_crt_4c68f0, 0, 0, 0, 0, "", 0, 0 },
    { "rl_crt_4c2a80", 0x4c2a80u, (void *)w_rl_crt_4c2a80, 0, 0, 0, 0, "", 0, 0 },
};
const int RH_HOOKS_RL_CRT_N = sizeof RH_HOOKS_RL_CRT / sizeof *RH_HOOKS_RL_CRT;
