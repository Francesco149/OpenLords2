/* rl_main.c — the host level on retail's layout: the CRT entry's game-facing half, WinMain, the main window.
 *
 * Retail: start 0x4c3370 (MSVC CRT) → WinMain 0x40e9ab → create_main_window 0x4b2258, then the message loop whose
 * idle branch is the frame guard 0x40e8bb (rl_ui4.c) → 0x40e890 → the frame driver 0x4b99c0. Every Win32 call goes
 * THROUGH RETAIL'S IAT SLOT (the slot VAs below): under rhost the slots hold rhost's virtual layers, in the standalone
 * the data-image loader fills them with the real APIs — one code path. Globals are retail's, by VA (the data image is
 * mapped at retail's addresses in both). docs/findings/standalone-path.md. */
#include "rl.h"
#include "rl_main.h"
#include "rl_io_plat.h"
#include "rl_crt.h"

#include <windows.h>

#define V32(va) (*(volatile int32_t *)(uintptr_t)(va))
#define V8(va)  (*(volatile uint8_t *)(uintptr_t)(va))
/* a Win32 import through retail's IAT slot */
#define IAT(T, slot) ((T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)(slot))

/* the frame driver the frame guard's 0x40e890 calls: rhost points it at its frame boundary (frame_wrap, which calls
 * 0x4b99c0 by address); unset = the port's rl_4b99c0 */
int (*rl_frame_driver)(void);
int rl_frame_port_guard;

/* ---------------------------------------------------------------- create_main_window 0x4b2258 */
/* Port of FUN_004b2258 (797 B) — the instance init: the two LFSR seeds 0x58fe28 / 0x58fe08 and 0x591564, the input
 * bytes, the 768-byte palette scratch 0x4e6600, the 640 x 480 view rects (centred: (W - w) / 2 with the cdq / sar
 * rounding), the pitch 0x4eb274 / height 0x4eac4c and their product 0x4eabf8, the screen metrics, the panel sheet
 * (220000 B), the status-log header line, the start time 0x4eaba0; then the window class "WinLords2Class" (style 8 =
 * CS_DBLCLKS, wndproc 0x4b29be by address, icon 0x65, black brush), eight cursors (0x69 0x66 0x67 0x68 0x6e 0x6f 0x69
 * 0x71), the WS_POPUP | WS_VISIBLE | WS_SYSMENU window over the whole screen (WS_EX_APPWINDOW), UpdateWindow,
 * SetFocus, the DirectDraw slots cleared; EAX = the palette init 0x4b2922. 0 when the class or the window fails. */
static int center(int whole, int part) { int d = whole - part; return (d - (d >> 31)) >> 1; }   /* cdq; sub; sar 1 */
int rl_4b2258(HINSTANCE inst)
{
    V32(0x58fe28) = 0x54657687;
    V32(0x58fe08) = 0x72641663;
    V32(0x591564) = 0x34518632;
    V8(0x5bb4a6) = 0;
    V8(0x5bb4a7) = V8(0x5bb4a6);
    V8(0x5bb4a4) = V8(0x5bb4a7);
    V8(0x5bb4a5) = V8(0x5bb4a4);
    for (int i = 0; i < 0x300; i++) V8(0x4e6600 + i) = 0;
    V8(0x4e65a4) = 0;
    V32(0x58fe9c) = 0;
    V32(0x5cd40c) = 0;
    V32(0x5aea40) = 0;
    V8(0x4ea0f8) = 1;
    V32(0x4eb250) = 640;
    V32(0x4eb254) = 480;
    V32(0x4ea120) = V32(0x4eb250);
    V32(0x4ea124) = V32(0x4eb254);
    V32(0x4e65b0) = center(V32(0x4eb250), V32(0x4ea120));
    V32(0x4e65b4) = center(V32(0x4eb254), V32(0x4ea124));
    V32(0x4e65b8) = V32(0x4ea120) + V32(0x4e65b0);
    V32(0x4e65bc) = V32(0x4ea124) + V32(0x4e65b4);
    V32(0x4eb274) = 640;
    V32(0x4eac4c) = 480;
    V32(0x4eabf8) = V32(0x4eb274) * V32(0x4eac4c);
    V32(0x4e65a0) = IAT(int (WINAPI *)(int), 0x5cf580u)(SM_CXSCREEN);
    V32(0x4ea838) = IAT(int (WINAPI *)(int), 0x5cf580u)(SM_CYSCREEN);
    rl_4b25b0(220000);
    rl_4afab9(0x4df588, 0, 0);   /* "Lords2 - Debug and status report on " */
    V32(0x4eaba0) = (int32_t)IAT(DWORD (WINAPI *)(void), 0x5cf5c4u)();
    WNDCLASSA wc;
    wc.style = 8;
    wc.lpfnWndProc = (WNDPROC)(uintptr_t)0x4b29beu;   /* the window procedure by address (rl_4b29be under a thunk) */
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = IAT(HICON (WINAPI *)(HINSTANCE, LPCSTR), 0x5cf564u)(inst, (LPCSTR)(uintptr_t)0x65);
    wc.hCursor = NULL;
    wc.hbrBackground = IAT(HGDIOBJ (WINAPI *)(int), 0x5cf380u)(BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = (LPCSTR)(uintptr_t)0x4df5b0u;   /* "WinLords2Class" */
    int atom = IAT(ATOM (WINAPI *)(const WNDCLASSA *), 0x5cf55cu)(&wc);
    if (atom == 0) return 0;
    typedef HCURSOR (WINAPI *lc_t)(HINSTANCE, LPCSTR);
#define CUR(id) IAT(lc_t, 0x5cf56cu)(inst, (LPCSTR)(uintptr_t)(id))
    V32(0x4ea834) = (int32_t)(uintptr_t)CUR(0x69);
    V32(0x4e659c) = (int32_t)(uintptr_t)CUR(0x66);
    V32(0x4ea194) = (int32_t)(uintptr_t)CUR(0x67);
    V32(0x4ea518) = (int32_t)(uintptr_t)CUR(0x68);
    V32(0x4eaba8) = (int32_t)(uintptr_t)CUR(0x6e);
    V32(0x4eac58) = (int32_t)(uintptr_t)CUR(0x6f);
    V32(0x4eb258) = (int32_t)(uintptr_t)CUR(0x69);
    V32(0x4eabe4) = (int32_t)(uintptr_t)CUR(0x71);
#undef CUR
    int h = IAT(int (WINAPI *)(int), 0x5cf580u)(SM_CYSCREEN);
    int w = IAT(int (WINAPI *)(int), 0x5cf580u)(SM_CXSCREEN);
    HWND wnd = IAT(HWND (WINAPI *)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID),
                   0x5cf570u)(0x40000u, (LPCSTR)(uintptr_t)0x4df5c8u /* "WinLords2Class" */,
                              (LPCSTR)(uintptr_t)0x4df5c0u /* "Lords2" */, 0x90080000u, 0, 0, w, h, NULL, NULL, inst, NULL);
    V32(0x4e5a00) = (int32_t)(uintptr_t)wnd;
    if (V32(0x4e5a00) == 0) return 0;
    IAT(BOOL (WINAPI *)(HWND), 0x5cf574u)((HWND)(uintptr_t)(uint32_t)V32(0x4e5a00));
    IAT(HWND (WINAPI *)(HWND), 0x5cf568u)((HWND)(uintptr_t)(uint32_t)V32(0x4e5a00));
    V32(0x4eabd0) = 0;
    V32(0x4eafac) = 0;
    V32(0x4e6974) = 0;
    V32(0x4ea12c) = 0;
    return rl_4b2922();
}

/* ---------------------------------------------------------------- WinMain 0x40e9ab */
/* Port of _WinMain@16 (641 B) — the boot: the startup flag 0x553c5c, engine state 0, the current drive 0x5cd414 and
 * directory 0x58fe30 (100 B), l2.eng (120000 B into 0x591580), the localization 0x4ae1b8, the hardware defaults
 * 0x40ecc4, the window 0x4b2258 (fail: "ERR: exited, could not create window"-class log, EAX 0); audio 0x425b10, the
 * setup sound, the stream 0x426d11(0), the "partially set" log line, the CD-audio probe 0x4aef7e, 0x58fd84 = 0x5c9a74,
 * 0x55305c pulsed 1 → 0, engine state 1, 0x4b3571(0); DirectDraw 0x4975d3 and 0x497500 (each failure logged, then the
 * exit path); "fully set", 0x53f684 = 0 (the dead 0x53f684 == 1 branch kept), 0x553018 = 1, the title 0x42d179 /
 * 0x40f10f / 0x476a5d, 0x553030 = 0, the save pump 0x4978ad; then the loop: PeekMessage (no remove) → GetMessage /
 * Translate / Dispatch (WM_QUIT → exit), else the input poll 0x4b8c3a and, active (0x4e65a8) and not paused (0x4e65d0),
 * the frame guard 0x40e8bb, else Sleep(200); the quit request byte 0x5aeb68 posts WM_CLOSE. Exit: WinHelp(HELP_QUIT),
 * 0x49767d, 0x4b2636; EAX = the last message's wParam. __stdcall (ret 0x10). */
/* Written as retail's frame, instruction for instruction (push ebp / sub esp, 1Ch / push ebx esi edi; MSG at ebp−1Ch;
 * every call with retail's pushes): everything WinMain calls — the frame guard, the WndProc through DispatchMessage,
 * the whole game — then runs at retail's stack depth. A compiled frame shifted it, and retail stores stack residue
 * holding stack pointers (0x488793: a dead unit's frame byte) — the hybrid / standalone differed from pure retail
 * there (ai-sally: units 35 / 36, 0x88 vs 0x34). */
__attribute__((naked)) int __stdcall rl_40e9ab(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    __asm__(
        "pushl %ebp\n\t movl %esp, %ebp\n\t subl $0x1c, %esp\n\t pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t"
        "movl $1, 0x553c5c\n\t movl $0, 0x52af94\n\t"
        "call _rl_plat_getdrive\n\t movl %eax, 0x5cd414\n\t"
        "pushl $0x64\n\t pushl $0x58fe30\n\t call _rl_plat_getcwd\n\t addl $8, %esp\n\t"
        "pushl $0\n\t pushl $0x1d4c0\n\t pushl $0x591580\n\t pushl $0x4d2784\n\t call _rl_4af8d9\n\t addl $0x10, %esp\n\t"
        "call _rl_4ae1b8\n\t call _rl_40ecc4\n\t"
        "movl 8(%ebp), %eax\n\t pushl %eax\n\t call _rl_4b2258\n\t addl $4, %esp\n\t testl %eax, %eax\n\t jnz 1f\n\t"
        "pushl $0\n\t pushl $0\n\t pushl $0x4d278c\n\t call _rl_4afab9\n\t addl $0xc, %esp\n\t xorl %eax, %eax\n\t jmp 9f\n"
        "1:\n\t call _rl_425b10\n\t"
        "pushl $0\n\t pushl $0\n\t pushl $0x4d27ac\n\t call _rl_4263ad\n\t addl $0xc, %esp\n\t"
        "pushl $0\n\t call _rl_426d11\n\t addl $4, %esp\n\t"
        "movl $0, 0x553c5c\n\t"
        "pushl $0\n\t pushl $0\n\t pushl $0x4d27b8\n\t call _rl_4afab9\n\t addl $0xc, %esp\n\t"
        "call _rl_4aef7e\n\t movl 0x5c9a74, %eax\n\t movl %eax, 0x58fd84\n\t"
        "movl $1, 0x55305c\n\t movl $0, 0x55305c\n\t movl $1, 0x52af94\n\t"
        "pushl $0\n\t call _rl_4b3571\n\t addl $4, %esp\n\t"
        "call _rl_4975d3\n\t testl %eax, %eax\n\t jnz 2f\n\t"
        "pushl $0\n\t pushl $0\n\t pushl $0x4d27d4\n\t call _rl_4afab9\n\t addl $0xc, %esp\n\t jmp 8f\n"
        "2:\n\t call _rl_497500\n\t testl %eax, %eax\n\t jnz 3f\n\t"
        "pushl $0\n\t pushl $0\n\t pushl $0x4d27f4\n\t call _rl_4afab9\n\t addl $0xc, %esp\n\t jmp 8f\n"
        "3:\n\t pushl $0\n\t pushl $0\n\t pushl $0x4d2814\n\t call _rl_4afab9\n\t addl $0xc, %esp\n\t"
        "movl $0, 0x53f684\n\t movl $1, 0x553018\n\t"
        "cmpl $1, 0x53f684\n\t jnz 4f\n\t movl $1, 0x5c9a74\n\t movl 0x5c9a74, %eax\n\t movl %eax, 0x58fd84\n"   /* dead in retail too */
        "4:\n\t call _rl_42d179\n\t call _rl_40f10f\n\t call _rl_476a5d\n\t movl $0, 0x553030\n\t call _rl_4978ad\n"
        "5:\n\t pushl $0\n\t pushl $0\n\t pushl $0\n\t pushl $0\n\t leal -0x1c(%ebp), %eax\n\t pushl %eax\n\t"
        "call *0x5cf4f0\n\t testl %eax, %eax\n\t jz 6f\n\t"                                    /* PeekMessageA */
        "pushl $0\n\t pushl $0\n\t pushl $0\n\t leal -0x1c(%ebp), %eax\n\t pushl %eax\n\t call *0x5cf58c\n\t"   /* GetMessageA */
        "testl %eax, %eax\n\t jz 8f\n\t"
        "leal -0x1c(%ebp), %eax\n\t pushl %eax\n\t call *0x5cf590\n\t"                         /* TranslateMessage */
        "leal -0x1c(%ebp), %eax\n\t pushl %eax\n\t call *0x5cf510\n\t jmp 7f\n"               /* DispatchMessageA */
        "6:\n\t call _rl_4b8c3a\n\t cmpl $0, 0x4e65d0\n\t jnz 61f\n\t cmpl $0, 0x4e65a8\n\t jz 61f\n\t"
        "call _rl_40e8bb\n\t jmp 7f\n"
        "61:\n\t pushl $0xc8\n\t call *0x5cf4ac\n"                                              /* Sleep(200) */
        "7:\n\t xorl %eax, %eax\n\t movb 0x5aeb68, %al\n\t cmpl $1, %eax\n\t jnz 5b\n\t"
        "pushl $0\n\t pushl $0\n\t pushl $0x10\n\t movl 0x4e5a00, %eax\n\t pushl %eax\n\t call *0x5cf514\n\t"   /* PostMessageA WM_CLOSE */
        "movb $0, 0x5aeb68\n\t jmp 5b\n"
        "8:\n\t pushl $1\n\t pushl $2\n\t pushl $0x4d282c\n\t movl 0x4e5a00, %eax\n\t pushl %eax\n\t call *0x5cf518\n\t"   /* WinHelpA(HELP_QUIT) */
        "call _rl_49767d\n\t call _rl_4b2636\n\t movl -0x14(%ebp), %eax\n"                     /* the last MSG's wParam */
        "9:\n\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t leave\n\t ret $0x10");
}

/* ---------------------------------------------------------------- the CRT entry */
/* The standalone's entry: retail's start 0x4c3370, ported with the rest of its CRT (rl_crt.c: heap, _ioinit, the
 * mbctable, argv / envp, _cinit, then WinMain, then exit → ExitProcess through retail's slot). Never returns. */
int rl_crt_start(void)
{
    rl_crt_4c3370();
    return 0;
}
