/* rl_main.h — the host level on retail's layout (rl_main.c): WinMain 0x40e9ab, create_main_window 0x4b2258, the CRT
 * entry (the standalone's: start 0x4c3370's port, rl_crt.c). */
#pragma once
#include <windows.h>

/* 1 while the port's frame guard rl_40e890 runs a frame: the frame driver's entry esp is then the port's, not the
 * one retail's WinMain chain gives it (rhost's row models retail's for the residue models: quirk #239) */
extern int rl_frame_port_guard;
extern int (*rl_frame_driver)(void);   /* 0x40e890's frame call: rhost's frame boundary; NULL = rl_4b99c0 */
int rl_4b2258(HINSTANCE inst);
int __stdcall rl_40e9ab(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show);
int rl_crt_start(void);
