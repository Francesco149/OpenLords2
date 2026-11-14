/* rh_hooks_rl_main.c — the host level on retail's layout (src/rl_main.c): WinMain 0x40e9ab and create_main_window
 * 0x4b2258. Both RH_SH_HOST (never shadowed: the window class / the window, and WinMain is the whole game's loop) —
 * the hybrid pass proves them; under --standalone they are the entry (rl_crt_start → rl_40e9ab). */
#include "rh_port.h"
#include "rl.h"
#include "rl_main.h"

#define RH_TABLE RH_HOOKS_RL_MAIN
extern hook_t RH_HOOKS_RL_MAIN[];
enum { X_rl_40e9ab, X_rl_4b2258 };

/* WinMain is the whole game: its entry jumps on with retail's stack untouched — to the port (replace: the port's frame
 * is retail's, src/rl_main.c) or back to retail (shadow passes: a host row is never shadowed, the call is `sampled`).
 * The generic entry moved ESP 0x40000 down and wrapped the port in its frames, so the game ran at another stack depth
 * and stack-pointer residue retail stores (0x488793) differed from pure retail (ai-sally's hybrid FAIL). */
#include <stddef.h>
_Static_assert(offsetof(hook_t, mode) == 12 && offsetof(hook_t, calls) == 16 && offsetof(hook_t, entry) == 428 &&
               offsetof(hook_t, sampled) == 436 && H_REPLACE == 2, "w_rl_40e9ab reads hook_t by offset");
__attribute__((used)) static uint32_t s_prep_esp, s_prep_stack;
static __attribute__((used)) void __cdecl winmain_prep(void) { rh_port_bind_once(); rh_port_sync_in(); }   /* ctx_port's */
static __attribute__((naked, used)) void w_rl_40e9ab(void)
{
    __asm__("cmpl $2, _RH_HOOKS_RL_MAIN+12\n\t jne 1f\n\t incl _RH_HOOKS_RL_MAIN+16\n\t"
            "movl %esp, _s_prep_esp\n\t movl _s_prep_stack, %esp\n\t call _winmain_prep\n\t movl _s_prep_esp, %esp\n\t"   /* off retail's stack */
            "jmp _rl_40e9ab@16\n"
            "1:\t incl _RH_HOOKS_RL_MAIN+436\n\t jmp *_RH_HOOKS_RL_MAIN+428");
}
HOOK_R(rl_4b2258, 1, rl_4b2258((HINSTANCE)(uintptr_t)a[0]))

static uint8_t s_prep_mem[0x10000] __attribute__((aligned(16)));
__attribute__((constructor)) static void prep_stack_init(void) { s_prep_stack = (uint32_t)(uintptr_t)(s_prep_mem + sizeof s_prep_mem - 64); }

hook_t RH_HOOKS_RL_MAIN[] = {
    { "rl_40e9ab", 0x40e9abu, (void *)w_rl_40e9ab, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b2258", 0x4b2258u, (void *)w_rl_4b2258, 0, 0, 0, 0, "", 0, RH_SH_HOST },
};
const int RH_HOOKS_RL_MAIN_N = sizeof RH_HOOKS_RL_MAIN / sizeof *RH_HOOKS_RL_MAIN;
