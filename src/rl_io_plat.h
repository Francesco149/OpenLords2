/* rl_io_plat.h — the io partition's platform shim (docs/findings/rl-io.md).
 *
 * Retail reaches the platform two ways, and a port running INSIDE rhost must reach it the same way or it bypasses
 * rhost's virtual layers (files, handles, heap arena, DirectSound, clock):
 *   imports   Win32 / winmm / smackw32 through its import table. rhost overrides the IAT slots (rh_files, rh_handles,
 *             rh_heap, rh_dsound, rh_smack, the virtual clock), so a bound shim calls THROUGH RETAIL'S SLOT; the
 *             standalone calls the real API (smackw32 loaded lazily; absent = SmackOpen fails, as retail tolerates).
 *   CRT       the MSVC debug CRT linked into Lords2.exe, PORTED on retail's layout (rl_crt.c): its state (the fd table
 *             __pioinfo, errno, time's statics and asctime's buffer, the debug heap's block list, the sbh pages) is
 *             retail's .data / arena, its OS calls go through retail's IAT slots — one code path under rhost and in
 *             the standalone. Pure CRT the port uses internally (strcmp, memcpy …) is the host's where no retail
 *             state is involved (rl_io.c: rl_strcmp keeps retail's -1 / 0 / +1).
 * rhost binds the shim once (tools/rhost/rh_hooks_rl_io.c: rl_plat_bind_retail; --standalone: rl_plat_bind_imports). COM calls
 * (DirectSound, DirectDraw, DirectPlay) go through the object's own vtable and need no shim. */
#pragma once
#include <stdint.h>
#include <windows.h>
#include <mmsystem.h>

/* X(name, retail IAT slot VA) — the imports the io partition calls */
#define RL_PLAT_IMPORTS(X)                                                                                       \
    X(GlobalAlloc, 0x5cf3c4u) X(GlobalMemoryStatus, 0x5cf3c8u) X(FindFirstFileA, 0x5cf3ccu)                      \
    X(FindNextFileA, 0x5cf3d0u) X(FindClose, 0x5cf3d4u) X(GetTickCount, 0x5cf3d8u)                               \
    X(InterlockedExchange, 0x5cf3dcu) X(GlobalFree, 0x5cf3e0u) X(OutputDebugStringA, 0x5cf3b4u)                  \
    X(DebugBreak, 0x5cf3acu)                                                                                     \
    X(mmioRead, 0x5cf59cu) X(mmioGetInfo, 0x5cf5a0u) X(mmioAdvance, 0x5cf5a4u) X(mmioSetInfo, 0x5cf5a8u)         \
    X(mmioOpenA, 0x5cf5acu) X(mmioDescend, 0x5cf5b0u) X(mmioAscend, 0x5cf5c8u) X(mmioClose, 0x5cf5ccu)           \
    X(mmioSeek, 0x5cf5d0u)                                                                                       \
    X(timeBeginPeriod, 0x5cf5b8u) X(timeSetEvent, 0x5cf5bcu) X(timeEndPeriod, 0x5cf5c0u) X(timeGetTime, 0x5cf5c4u) \
    X(SmackWait, 0x5cf5d8u) X(SmackSoundUseDirectSound, 0x5cf5e4u) X(SmackToBufferRect, 0x5cf5e8u)               \
    X(SmackOpen, 0x5cf5ecu) X(SmackToBuffer, 0x5cf5f0u) X(SmackClose, 0x5cf5f4u) X(SmackNextFrame, 0x5cf5f8u)    \
    X(SmackDoFrame, 0x5cf5fcu)                                                                                   \
    X(PostMessageA, 0x5cf514u) X(GetForegroundWindow, 0x5cf550u) X(SetCursor, 0x5cf578u)                     \
    X(DialogBoxParamA, 0x5cf52cu) X(WinHelpA, 0x5cf518u)                                                       \
    /* ui4 */ X(DirectSoundCreate, 0x5cf370u) X(DirectDrawCreate, 0x5cf35cu) X(timeKillEvent, 0x5cf5b4u)       \
    X(UpdateWindow, 0x5cf574u) X(GetDC, 0x5cf584u) X(ReleaseDC, 0x5cf588u) X(GetDeviceCaps, 0x5cf388u)         \
    X(GetPaletteEntries, 0x5cf378u) X(SetSystemPaletteUse, 0x5cf37cu) X(GetSystemMetrics, 0x5cf580u)           \
    X(GetWindowLongA, 0x5cf51cu) X(SetWindowLongA, 0x5cf4ecu) X(SetRect, 0x5cf594u) X(GetMenu, 0x5cf4fcu)       \
    X(AdjustWindowRectEx, 0x5cf508u) X(SetWindowPos, 0x5cf4f8u) X(SystemParametersInfoA, 0x5cf504u)            \
    X(GetWindowRect, 0x5cf500u) X(PeekMessageA, 0x5cf4f0u) X(GetMessageA, 0x5cf58cu)                           \
    X(TranslateMessage, 0x5cf590u) X(DispatchMessageA, 0x5cf510u) \
    /* host */ X(GetCursorPos, 0x5cf57cu) X(DefWindowProcA, 0x5cf558u) X(IsIconic, 0x5cf54cu) X(BeginPaint, 0x5cf4e8u)   \
    X(EndPaint, 0x5cf4f4u) X(PostQuitMessage, 0x5cf554u) X(GetClientRect, 0x5cf544u) X(ClientToScreen, 0x5cf548u) \
    /* gaps */ X(PatBlt, 0x5cf384u) X(InvalidateRect, 0x5cf560u) X(Smack17, 0x5cf5e0u) X(Smack27, 0x5cf5dcu)

void rl_plat_bind_retail(void);   /* rhost: imports through retail's IAT slots */
void rl_plat_bind_imports(void);  /* rhost --standalone: the same (the slots hold rhost's layers) */
int  rl_plat_bound(void);

/* imports */
HGLOBAL  rl_plat_GlobalAlloc(UINT fl, SIZE_T n);
HGLOBAL  rl_plat_GlobalFree(HGLOBAL h);
void     rl_plat_GlobalMemoryStatus(MEMORYSTATUS *m);
HANDLE   rl_plat_FindFirstFileA(const char *pat, WIN32_FIND_DATAA *d);
BOOL     rl_plat_FindNextFileA(HANDLE h, WIN32_FIND_DATAA *d);
BOOL     rl_plat_FindClose(HANDLE h);
DWORD    rl_plat_GetTickCount(void);
LONG     rl_plat_InterlockedExchange(LONG *target, LONG v);
void     rl_plat_OutputDebugStringA(const char *s);
void     rl_plat_DebugBreak(void);
LONG     rl_plat_mmioRead(HMMIO h, char *buf, LONG n);
MMRESULT rl_plat_mmioGetInfo(HMMIO h, MMIOINFO *info, UINT fl);
MMRESULT rl_plat_mmioAdvance(HMMIO h, MMIOINFO *info, UINT fl);
MMRESULT rl_plat_mmioSetInfo(HMMIO h, const MMIOINFO *info, UINT fl);
HMMIO    rl_plat_mmioOpenA(char *name, MMIOINFO *info, DWORD fl);
MMRESULT rl_plat_mmioDescend(HMMIO h, MMCKINFO *ck, const MMCKINFO *parent, UINT fl);
MMRESULT rl_plat_mmioAscend(HMMIO h, MMCKINFO *ck, UINT fl);
MMRESULT rl_plat_mmioClose(HMMIO h, UINT fl);
LONG     rl_plat_mmioSeek(HMMIO h, LONG off, int origin);
MMRESULT rl_plat_timeBeginPeriod(UINT p);
MMRESULT rl_plat_timeEndPeriod(UINT p);
MMRESULT rl_plat_timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK cb, DWORD_PTR user, UINT fl);
DWORD    rl_plat_timeGetTime(void);
BOOL     rl_plat_PostMessageA(HWND w, UINT m, WPARAM wp, LPARAM lp);
HWND     rl_plat_GetForegroundWindow(void);
HCURSOR  rl_plat_SetCursor(HCURSOR c);
INT_PTR  rl_plat_DialogBoxParamA(HINSTANCE inst, const char *tmpl, HWND parent, DLGPROC proc, LPARAM init);
BOOL     rl_plat_WinHelpA(HWND w, const char *file, UINT cmd, ULONG_PTR data);   /* rhost: a stub (rh_win.c) */
/* ui4: DirectSound / DirectDraw creation (the standalone loads dsound / ddraw on first use), the stream timer kill,
 * the DirectDraw init's window and message calls */
HRESULT  rl_plat_DirectSoundCreate(const GUID *g, void **ds, void *outer);
HRESULT  rl_plat_DirectDrawCreate(GUID *g, void **dd, void *outer);
MMRESULT rl_plat_timeKillEvent(UINT id);
BOOL     rl_plat_UpdateWindow(HWND w);
HDC      rl_plat_GetDC(HWND w);
int      rl_plat_ReleaseDC(HWND w, HDC dc);
int      rl_plat_GetDeviceCaps(HDC dc, int index);
UINT     rl_plat_GetPaletteEntries(HPALETTE pal, UINT start, UINT n, PALETTEENTRY *e);
UINT     rl_plat_SetSystemPaletteUse(HDC dc, UINT use);
int      rl_plat_GetSystemMetrics(int index);
LONG     rl_plat_GetWindowLongA(HWND w, int index);
LONG     rl_plat_SetWindowLongA(HWND w, int index, LONG v);
BOOL     rl_plat_SetRect(RECT *r, int l, int t, int ri, int b);
HMENU    rl_plat_GetMenu(HWND w);
BOOL     rl_plat_AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex);
BOOL     rl_plat_SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT fl);
BOOL     rl_plat_SystemParametersInfoA(UINT a, UINT ui, void *pv, UINT win);
BOOL     rl_plat_GetWindowRect(HWND w, RECT *r);
BOOL     rl_plat_PeekMessageA(MSG *m, HWND w, UINT lo, UINT hi, UINT rm);
BOOL     rl_plat_GetMessageA(MSG *m, HWND w, UINT lo, UINT hi);
BOOL     rl_plat_TranslateMessage(const MSG *m);
LRESULT  rl_plat_DispatchMessageA(const MSG *m);
/* smackw32 (__stdcall, by ordinal): the Smack struct is opaque to the shim (retail reads its fields itself) */
uint32_t rl_plat_SmackOpen(const char *name, uint32_t flags, uint32_t extrabuf);      /* #14 */
void     rl_plat_SmackClose(uint32_t smk);                                            /* #18 */
uint32_t rl_plat_SmackDoFrame(uint32_t smk);                                          /* #19 */
void     rl_plat_SmackNextFrame(uint32_t smk);                                        /* #21 */
void     rl_plat_SmackToBuffer(uint32_t smk, uint32_t l, uint32_t t, uint32_t pitch, uint32_t h, uint32_t buf,
                               uint32_t flags);                                       /* #23 */
uint32_t rl_plat_SmackToBufferRect(uint32_t smk, uint32_t surface);                   /* #28 */
uint32_t rl_plat_SmackWait(uint32_t smk);                                             /* #32 */
uint32_t rl_plat_SmackSoundUseDirectSound(uint32_t lpds);                             /* #37 */
uint32_t rl_plat_Smack17(uint32_t smk, uint32_t on);                          /* #17 (name unconfirmed: (smk, 0 / 1) around a #27) */
void     rl_plat_Smack27(uint32_t smk, uint32_t frame);                             /* #27 (name unconfirmed: (smk, frame)) */

/* the music streamer's timeSetEvent callback (0x426dd5): bound = retail's entry (rhost hooks it); the
 * standalone = a CALLBACK thunk into rl_426dd5 */
LPTIMECALLBACK rl_plat_stream_timer_cb(void);

/* CRT */
int      rl_plat_open(const char *path, int oflag, int pmode);
int      rl_plat_close(int fd);
int      rl_plat_read(int fd, void *buf, unsigned n);
int      rl_plat_write(int fd, const void *buf, unsigned n);
int      rl_plat_lseek(int fd, int off, int origin);
int      rl_plat_chdir(const char *path);
int32_t  rl_plat_time(int32_t *t);
void    *rl_plat_localtime(const int32_t *t);    /* a struct tm (retail's static one when bound) */
char    *rl_plat_asctime(const void *tm);
void    *rl_plat_malloc(uint32_t n);
int      rl_plat_filelength(int fd);
void     rl_plat_exit(int code);
/* the debug heap's helpers (0x4c05a0's callees) */
int      rl_plat_CrtCheckMemory(void);
int      rl_plat_CrtIsValidHeapPointer(const void *p);
int      rl_plat_CrtDbgReport(int type, const char *file, int line, const char *mod, const char *fmt,
                              uint32_t a5, uint32_t a6, uint32_t a7);   /* up to 3 format arguments */
int      rl_plat_CheckBytes(const void *p, int c, int n);
void     rl_plat_free_base(void *block);
int      rl_plat_alloc_hook(uint32_t hook, int type, void *data, int size, int use, int req, const char *file,
                            int line);   /* call _pfnAllocHook DAT_004e4610 (retail's default hook = 1) */
/* turn */
int      rl_plat_remove(const char *path);
int      rl_plat_getdrive(void);                 /* _getdrive (1 = A:) */
int      rl_plat_rename(const char *from, const char *to);
/* host */
BOOL     rl_plat_GetCursorPos(POINT *p);
char    *rl_plat_getcwd(char *buf, int n);
LRESULT  rl_plat_DefWindowProcA(HWND w, UINT m, WPARAM wp, LPARAM lp);
BOOL     rl_plat_IsIconic(HWND w);
HDC      rl_plat_BeginPaint(HWND w, PAINTSTRUCT *ps);
BOOL     rl_plat_EndPaint(HWND w, const PAINTSTRUCT *ps);
void     rl_plat_PostQuitMessage(int code);
BOOL     rl_plat_GetClientRect(HWND w, RECT *r);
BOOL     rl_plat_ClientToScreen(HWND w, POINT *p);
/* gaps (the movie's WM_PAINT 0x42e3ba, the pause 0x4b26b8) */
BOOL     rl_plat_PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop);
BOOL     rl_plat_InvalidateRect(HWND w, const RECT *r, BOOL erase);
