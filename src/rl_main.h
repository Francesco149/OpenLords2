/* rl_main.h — the host level on retail's layout (rl_main.c): WinMain 0x40e9ab, create_main_window 0x4b2258, the CRT
 * entry's game-facing half (the standalone's entry). */
#pragma once
#include <windows.h>

extern int (*rl_frame_driver)(void);   /* 0x40e890's frame call: rhost's frame boundary; NULL = rl_4b99c0 */
int rl_4b2258(HINSTANCE inst);
int __stdcall rl_40e9ab(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show);
int rl_crt_start(void);
