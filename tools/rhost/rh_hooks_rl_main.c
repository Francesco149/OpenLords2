/* rh_hooks_rl_main.c — the host level on retail's layout (src/rl_main.c): WinMain 0x40e9ab and create_main_window
 * 0x4b2258. Both RH_SH_HOST (never shadowed: the window class / the window, and WinMain is the whole game's loop) —
 * the hybrid pass proves them; under --standalone they are the entry (rl_crt_start → rl_40e9ab). */
#include "rh_port.h"
#include "rl.h"
#include "rl_main.h"

#define RH_TABLE RH_HOOKS_RL_MAIN
extern hook_t RH_HOOKS_RL_MAIN[];
enum { X_rl_40e9ab, X_rl_4b2258 };

/* WinMain is __stdcall (ret 0x10): RH_ENTRY with the stdcall return (rh_hooks_rl_host.c's RH_ENTRY_STD16) */
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

HOOK_R_STD16(rl_40e9ab, 4, rl_40e9ab((HINSTANCE)(uintptr_t)a[0], (HINSTANCE)(uintptr_t)a[1], (LPSTR)(uintptr_t)a[2], I(3)))
HOOK_R(rl_4b2258, 1, rl_4b2258((HINSTANCE)(uintptr_t)a[0]))

hook_t RH_HOOKS_RL_MAIN[] = {
    { "rl_40e9ab", 0x40e9abu, (void *)w_rl_40e9ab, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b2258", 0x4b2258u, (void *)w_rl_4b2258, 0, 0, 0, 0, "", 0, RH_SH_HOST },
};
const int RH_HOOKS_RL_MAIN_N = sizeof RH_HOOKS_RL_MAIN / sizeof *RH_HOOKS_RL_MAIN;
