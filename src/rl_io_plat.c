/* rl_io_plat.c — the io partition's platform shim (rl_io_plat.h; docs/findings/rl-io.md). */
#include "rl_io_plat.h"
#include "rl_io.h"

#include "rl_crt.h"
#include "engine_retail_call.h"

#include <stdlib.h>
#include <string.h>

static int s_bound;   /* imports through retail's IAT slots */
#define SLOT(n, va) static const uint32_t k_slot_##n = va;
RL_PLAT_IMPORTS(SLOT)
#undef SLOT
#define PV(p) ((uint32_t)(uintptr_t)(p))   /* a retail pointer value */

void rl_plat_bind_retail(void) { s_bound = 1; }
void rl_plat_bind_imports(void) { s_bound = 1; }
int rl_plat_bound(void) { return s_bound; }


/* bound: the function retail's IAT slot holds NOW (rhost's override); else the host's */
#define IMP(n, T, host) (s_bound ? (T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)k_slot_##n : (T)(host))

/* ---------------------------------------------------------------- imports */
HGLOBAL rl_plat_GlobalAlloc(UINT fl, SIZE_T n)
{ return IMP(GlobalAlloc, HGLOBAL (WINAPI *)(UINT, SIZE_T), GlobalAlloc)(fl, n); }
HGLOBAL rl_plat_GlobalFree(HGLOBAL h)
{ return IMP(GlobalFree, HGLOBAL (WINAPI *)(HGLOBAL), GlobalFree)(h); }
void rl_plat_GlobalMemoryStatus(MEMORYSTATUS *m)
{ IMP(GlobalMemoryStatus, void (WINAPI *)(MEMORYSTATUS *), GlobalMemoryStatus)(m); }
HANDLE rl_plat_FindFirstFileA(const char *pat, WIN32_FIND_DATAA *d)
{ return IMP(FindFirstFileA, HANDLE (WINAPI *)(LPCSTR, LPWIN32_FIND_DATAA), FindFirstFileA)(pat, d); }
BOOL rl_plat_FindNextFileA(HANDLE h, WIN32_FIND_DATAA *d)
{ return IMP(FindNextFileA, BOOL (WINAPI *)(HANDLE, LPWIN32_FIND_DATAA), FindNextFileA)(h, d); }
BOOL rl_plat_FindClose(HANDLE h)
{ return IMP(FindClose, BOOL (WINAPI *)(HANDLE), FindClose)(h); }
DWORD rl_plat_GetTickCount(void)
{ return IMP(GetTickCount, DWORD (WINAPI *)(void), GetTickCount)(); }
static LONG WINAPI host_InterlockedExchange(LONG *t, LONG v) { return InterlockedExchange(t, v); }
LONG rl_plat_InterlockedExchange(LONG *target, LONG v)
{ return IMP(InterlockedExchange, LONG (WINAPI *)(LONG *, LONG), host_InterlockedExchange)(target, v); }
void rl_plat_OutputDebugStringA(const char *s)
{ IMP(OutputDebugStringA, void (WINAPI *)(LPCSTR), OutputDebugStringA)(s); }
void rl_plat_DebugBreak(void)
{ IMP(DebugBreak, void (WINAPI *)(void), DebugBreak)(); }
LONG rl_plat_mmioRead(HMMIO h, char *buf, LONG n)
{ return IMP(mmioRead, LONG (WINAPI *)(HMMIO, HPSTR, LONG), mmioRead)(h, buf, n); }
MMRESULT rl_plat_mmioGetInfo(HMMIO h, MMIOINFO *info, UINT fl)
{ return IMP(mmioGetInfo, MMRESULT (WINAPI *)(HMMIO, LPMMIOINFO, UINT), mmioGetInfo)(h, info, fl); }
MMRESULT rl_plat_mmioAdvance(HMMIO h, MMIOINFO *info, UINT fl)
{ return IMP(mmioAdvance, MMRESULT (WINAPI *)(HMMIO, LPMMIOINFO, UINT), mmioAdvance)(h, info, fl); }
MMRESULT rl_plat_mmioSetInfo(HMMIO h, const MMIOINFO *info, UINT fl)
{ return IMP(mmioSetInfo, MMRESULT (WINAPI *)(HMMIO, LPCMMIOINFO, UINT), mmioSetInfo)(h, info, fl); }
HMMIO rl_plat_mmioOpenA(char *name, MMIOINFO *info, DWORD fl)
{ return IMP(mmioOpenA, HMMIO (WINAPI *)(LPSTR, LPMMIOINFO, DWORD), mmioOpenA)(name, info, fl); }
MMRESULT rl_plat_mmioDescend(HMMIO h, MMCKINFO *ck, const MMCKINFO *parent, UINT fl)
{ return IMP(mmioDescend, MMRESULT (WINAPI *)(HMMIO, LPMMCKINFO, const MMCKINFO *, UINT), mmioDescend)(h, ck, parent, fl); }
MMRESULT rl_plat_mmioAscend(HMMIO h, MMCKINFO *ck, UINT fl)
{ return IMP(mmioAscend, MMRESULT (WINAPI *)(HMMIO, LPMMCKINFO, UINT), mmioAscend)(h, ck, fl); }
MMRESULT rl_plat_mmioClose(HMMIO h, UINT fl)
{ return IMP(mmioClose, MMRESULT (WINAPI *)(HMMIO, UINT), mmioClose)(h, fl); }
LONG rl_plat_mmioSeek(HMMIO h, LONG off, int origin)
{ return IMP(mmioSeek, LONG (WINAPI *)(HMMIO, LONG, int), mmioSeek)(h, off, origin); }
MMRESULT rl_plat_timeBeginPeriod(UINT p)
{ return IMP(timeBeginPeriod, MMRESULT (WINAPI *)(UINT), timeBeginPeriod)(p); }
MMRESULT rl_plat_timeEndPeriod(UINT p)
{ return IMP(timeEndPeriod, MMRESULT (WINAPI *)(UINT), timeEndPeriod)(p); }
MMRESULT rl_plat_timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK cb, DWORD_PTR user, UINT fl)
{ return IMP(timeSetEvent, MMRESULT (WINAPI *)(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT), timeSetEvent)(delay, res, cb, user, fl); }
DWORD rl_plat_timeGetTime(void)
{ return IMP(timeGetTime, DWORD (WINAPI *)(void), timeGetTime)(); }
BOOL rl_plat_PostMessageA(HWND w, UINT m, WPARAM wp, LPARAM lp)
{ return IMP(PostMessageA, BOOL (WINAPI *)(HWND, UINT, WPARAM, LPARAM), PostMessageA)(w, m, wp, lp); }
HWND rl_plat_GetForegroundWindow(void)
{ return IMP(GetForegroundWindow, HWND (WINAPI *)(void), GetForegroundWindow)(); }
INT_PTR rl_plat_DialogBoxParamA(HINSTANCE inst, const char *tmpl, HWND parent, DLGPROC proc, LPARAM init)
{ return IMP(DialogBoxParamA, INT_PTR (WINAPI *)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM), DialogBoxParamA)(inst, tmpl, parent, proc, init); }
BOOL rl_plat_WinHelpA(HWND w, const char *file, UINT cmd, ULONG_PTR data)
{ return IMP(WinHelpA, BOOL (WINAPI *)(HWND, LPCSTR, UINT, ULONG_PTR), WinHelpA)(w, file, cmd, data); }
HCURSOR rl_plat_SetCursor(HCURSOR c)
{ return IMP(SetCursor, HCURSOR (WINAPI *)(HCURSOR), SetCursor)(c); }

/* ui4: dsound / ddraw are loaded on first use in the standalone (rhost.exe links neither) */
static FARPROC dll_proc(const char *dll, const char *name)
{
    HMODULE m = LoadLibraryA(dll);
    return m ? GetProcAddress(m, name) : NULL;
}
HRESULT rl_plat_DirectSoundCreate(const GUID *g, void **ds, void *outer)
{
    typedef HRESULT (WINAPI *f_t)(const GUID *, void **, void *);
    f_t f = s_bound ? (f_t)(uintptr_t)*(volatile uint32_t *)(uintptr_t)k_slot_DirectSoundCreate
                    : (f_t)(uintptr_t)dll_proc("dsound.dll", "DirectSoundCreate");
    return f ? f(g, ds, outer) : (HRESULT)0x80004005;
}
HRESULT rl_plat_DirectDrawCreate(GUID *g, void **dd, void *outer)
{
    typedef HRESULT (WINAPI *f_t)(GUID *, void **, void *);
    f_t f = s_bound ? (f_t)(uintptr_t)*(volatile uint32_t *)(uintptr_t)k_slot_DirectDrawCreate
                    : (f_t)(uintptr_t)dll_proc("ddraw.dll", "DirectDrawCreate");
    return f ? f(g, dd, outer) : (HRESULT)0x80004005;
}
MMRESULT rl_plat_timeKillEvent(UINT id)
{ return IMP(timeKillEvent, MMRESULT (WINAPI *)(UINT), timeKillEvent)(id); }
BOOL rl_plat_UpdateWindow(HWND w)
{ return IMP(UpdateWindow, BOOL (WINAPI *)(HWND), UpdateWindow)(w); }
HDC rl_plat_GetDC(HWND w)
{ return IMP(GetDC, HDC (WINAPI *)(HWND), GetDC)(w); }
int rl_plat_ReleaseDC(HWND w, HDC dc)
{ return IMP(ReleaseDC, int (WINAPI *)(HWND, HDC), ReleaseDC)(w, dc); }
int rl_plat_GetDeviceCaps(HDC dc, int index)
{ return IMP(GetDeviceCaps, int (WINAPI *)(HDC, int), GetDeviceCaps)(dc, index); }
UINT rl_plat_GetPaletteEntries(HPALETTE pal, UINT start, UINT n, PALETTEENTRY *e)
{ return IMP(GetPaletteEntries, UINT (WINAPI *)(HPALETTE, UINT, UINT, PALETTEENTRY *), GetPaletteEntries)(pal, start, n, e); }
UINT rl_plat_SetSystemPaletteUse(HDC dc, UINT use)
{ return IMP(SetSystemPaletteUse, UINT (WINAPI *)(HDC, UINT), SetSystemPaletteUse)(dc, use); }
int rl_plat_GetSystemMetrics(int index)
{ return IMP(GetSystemMetrics, int (WINAPI *)(int), GetSystemMetrics)(index); }
LONG rl_plat_GetWindowLongA(HWND w, int index)
{ return IMP(GetWindowLongA, LONG (WINAPI *)(HWND, int), GetWindowLongA)(w, index); }
LONG rl_plat_SetWindowLongA(HWND w, int index, LONG v)
{ return IMP(SetWindowLongA, LONG (WINAPI *)(HWND, int, LONG), SetWindowLongA)(w, index, v); }
BOOL rl_plat_SetRect(RECT *r, int l, int t, int ri, int b)
{ return IMP(SetRect, BOOL (WINAPI *)(RECT *, int, int, int, int), SetRect)(r, l, t, ri, b); }
HMENU rl_plat_GetMenu(HWND w)
{ return IMP(GetMenu, HMENU (WINAPI *)(HWND), GetMenu)(w); }
BOOL rl_plat_AdjustWindowRectEx(RECT *r, DWORD style, BOOL menu, DWORD ex)
{ return IMP(AdjustWindowRectEx, BOOL (WINAPI *)(RECT *, DWORD, BOOL, DWORD), AdjustWindowRectEx)(r, style, menu, ex); }
BOOL rl_plat_SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT fl)
{ return IMP(SetWindowPos, BOOL (WINAPI *)(HWND, HWND, int, int, int, int, UINT), SetWindowPos)(w, after, x, y, cx, cy, fl); }
BOOL rl_plat_SystemParametersInfoA(UINT a, UINT ui, void *pv, UINT win)
{ return IMP(SystemParametersInfoA, BOOL (WINAPI *)(UINT, UINT, void *, UINT), SystemParametersInfoA)(a, ui, pv, win); }
BOOL rl_plat_GetWindowRect(HWND w, RECT *r)
{ return IMP(GetWindowRect, BOOL (WINAPI *)(HWND, RECT *), GetWindowRect)(w, r); }
BOOL rl_plat_PeekMessageA(MSG *m, HWND w, UINT lo, UINT hi, UINT rm)
{ return IMP(PeekMessageA, BOOL (WINAPI *)(MSG *, HWND, UINT, UINT, UINT), PeekMessageA)(m, w, lo, hi, rm); }
BOOL rl_plat_GetMessageA(MSG *m, HWND w, UINT lo, UINT hi)
{ return IMP(GetMessageA, BOOL (WINAPI *)(MSG *, HWND, UINT, UINT), GetMessageA)(m, w, lo, hi); }
BOOL rl_plat_TranslateMessage(const MSG *m)
{ return IMP(TranslateMessage, BOOL (WINAPI *)(const MSG *), TranslateMessage)(m); }
LRESULT rl_plat_DispatchMessageA(const MSG *m)
{ return IMP(DispatchMessageA, LRESULT (WINAPI *)(const MSG *), DispatchMessageA)(m); }

/* smackw32: the standalone loads it on first use (none = every call fails / does nothing) */
static FARPROC smack(int ord)
{
    static HMODULE m;
    static int tried;
    if (!tried) { tried = 1; m = LoadLibraryA("smackw32.dll"); }
    return m ? GetProcAddress(m, (LPCSTR)(uintptr_t)ord) : NULL;
}
#define SMK(n, ord, T) (s_bound ? (T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)k_slot_##n : (T)(uintptr_t)smack(ord))
uint32_t rl_plat_SmackOpen(const char *name, uint32_t flags, uint32_t extrabuf)
{
    uint32_t (WINAPI *f)(const char *, uint32_t, uint32_t) = SMK(SmackOpen, 14, uint32_t (WINAPI *)(const char *, uint32_t, uint32_t));
    return f ? f(name, flags, extrabuf) : 0;
}
void rl_plat_SmackClose(uint32_t smk)
{
    void (WINAPI *f)(uint32_t) = SMK(SmackClose, 18, void (WINAPI *)(uint32_t));
    if (f) f(smk);
}
uint32_t rl_plat_SmackDoFrame(uint32_t smk)
{
    uint32_t (WINAPI *f)(uint32_t) = SMK(SmackDoFrame, 19, uint32_t (WINAPI *)(uint32_t));
    return f ? f(smk) : 0;
}
void rl_plat_SmackNextFrame(uint32_t smk)
{
    void (WINAPI *f)(uint32_t) = SMK(SmackNextFrame, 21, void (WINAPI *)(uint32_t));
    if (f) f(smk);
}
void rl_plat_SmackToBuffer(uint32_t smk, uint32_t l, uint32_t t, uint32_t pitch, uint32_t h, uint32_t buf, uint32_t flags)
{
    void (WINAPI *f)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) =
        SMK(SmackToBuffer, 23, void (WINAPI *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t));
    if (f) f(smk, l, t, pitch, h, buf, flags);
}
uint32_t rl_plat_SmackToBufferRect(uint32_t smk, uint32_t surface)
{
    uint32_t (WINAPI *f)(uint32_t, uint32_t) = SMK(SmackToBufferRect, 28, uint32_t (WINAPI *)(uint32_t, uint32_t));
    return f ? f(smk, surface) : 0;
}
uint32_t rl_plat_SmackWait(uint32_t smk)
{
    uint32_t (WINAPI *f)(uint32_t) = SMK(SmackWait, 32, uint32_t (WINAPI *)(uint32_t));
    return f ? f(smk) : 0;
}
uint32_t rl_plat_SmackSoundUseDirectSound(uint32_t lpds)
{
    uint32_t (WINAPI *f)(uint32_t) = SMK(SmackSoundUseDirectSound, 37, uint32_t (WINAPI *)(uint32_t));
    return f ? f(lpds) : 0;
}

/* the streamer's timer callback */
static void CALLBACK stream_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2)
{
    rl_426dd5((int)id, (int)msg, (int)user, (int)d1, (int)d2);
}
LPTIMECALLBACK rl_plat_stream_timer_cb(void)
{
    return s_bound ? (LPTIMECALLBACK)(uintptr_t)0x426dd5u : stream_cb;
}

/* ---------------------------------------------------------------- CRT */
/* retail's own CRT, ported on its layout (rl_crt.c): the same code under rhost and in the standalone — its state in
 * retail's .data (the fd table, errno, the debug heap's block list, tzset / time statics), its OS calls through
 * retail's IAT slots (rhost's file overlay, fake handles, heap arena, virtual clock). */
int rl_plat_open(const char *path, int oflag, int pmode) { return rl_crt_4cea00(PV(path), oflag, pmode); }
int rl_plat_close(int fd) { return rl_crt_4ce640((uint32_t)fd); }
int rl_plat_read(int fd, void *buf, unsigned n) { return rl_crt_4ceff0((uint32_t)fd, PV(buf), n); }
int rl_plat_write(int fd, const void *buf, unsigned n) { return rl_crt_4cc0b0((uint32_t)fd, PV(buf), n); }
int rl_plat_lseek(int fd, int off, int origin) { return rl_crt_4cc3b0((uint32_t)fd, off, origin); }
int rl_plat_filelength(int fd) { return rl_crt_4cf490((uint32_t)fd); }
int rl_plat_chdir(const char *path) { return rl_crt_4cf550(PV(path)); }
int32_t rl_plat_time(int32_t *t) { return rl_crt_4c2740(PV(t)); }
void *rl_plat_localtime(const int32_t *t) { return (void *)(uintptr_t)rl_crt_4c24e0(PV(t)); }
char *rl_plat_asctime(const void *tm) { return (char *)(uintptr_t)rl_crt_4c2340(PV(tm)); }
void rl_plat_exit(int code) { rl_crt_4bf6f0(code); }
void *rl_plat_malloc(uint32_t n) { return (void *)(uintptr_t)rl_crt_4bfa00(n); }
/* the debug heap's helpers (_free_dbg 0x4c05a0's callees): _CrtCheckMemory / _CrtDbgReport are unported (never
 * reached: _CRTDBG_CHECK_ALWAYS_DF is off and every report is an error path) — retail under rhost, a gap standalone */
int rl_plat_CrtCheckMemory(void) { return (int)engine_retail_call(0x4c0cf0u, 0); }
int rl_plat_CrtIsValidHeapPointer(const void *p) { return rl_crt_4c1170(PV(p)); }
int rl_plat_CrtDbgReport(int type, const char *file, int line, const char *mod, const char *fmt, uint32_t a5, uint32_t a6, uint32_t a7)
{
    return (int)engine_retail_call(0x4c3950u, 8, (uint32_t)type, PV(file), (uint32_t)line, PV(mod), PV(fmt), a5, a6, a7);
}
int rl_plat_CheckBytes(const void *p, int c, int n) { return rl_crt_4c0c60(PV(p), c, n); }
void rl_plat_free_base(void *block) { rl_crt_4c4370(PV(block)); }
/* _pfnAllocHook as retail calls it (rl_crt_alloc_hook: retail's default as its port, another by address) */
int rl_plat_alloc_hook(uint32_t hook, int type, void *data, int size, int use, int req, const char *file, int line)
{
    return rl_crt_alloc_hook(hook, type, PV(data), (uint32_t)size, use, req, PV(file), line);
}

/* turn */
int rl_plat_remove(const char *path) { return rl_crt_4c22c0(PV(path)); }
int rl_plat_rename(const char *from, const char *to) { return rl_crt_4c2260(PV(from), PV(to)); }
int rl_plat_getdrive(void) { return rl_crt_4c1cf0(); }
char *rl_plat_getcwd(char *buf, int n) { return (char *)(uintptr_t)rl_crt_4c1b10(PV(buf), n); }
/* host */
BOOL rl_plat_GetCursorPos(POINT *p)
{ return IMP(GetCursorPos, BOOL (WINAPI *)(POINT *), GetCursorPos)(p); }
LRESULT rl_plat_DefWindowProcA(HWND w, UINT m, WPARAM wp, LPARAM lp)
{ return IMP(DefWindowProcA, LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM), DefWindowProcA)(w, m, wp, lp); }
BOOL rl_plat_IsIconic(HWND w) { return IMP(IsIconic, BOOL (WINAPI *)(HWND), IsIconic)(w); }
HDC rl_plat_BeginPaint(HWND w, PAINTSTRUCT *ps) { return IMP(BeginPaint, HDC (WINAPI *)(HWND, PAINTSTRUCT *), BeginPaint)(w, ps); }
BOOL rl_plat_EndPaint(HWND w, const PAINTSTRUCT *ps)
{ return IMP(EndPaint, BOOL (WINAPI *)(HWND, const PAINTSTRUCT *), EndPaint)(w, ps); }
void rl_plat_PostQuitMessage(int code) { IMP(PostQuitMessage, void (WINAPI *)(int), PostQuitMessage)(code); }
BOOL rl_plat_GetClientRect(HWND w, RECT *r) { return IMP(GetClientRect, BOOL (WINAPI *)(HWND, RECT *), GetClientRect)(w, r); }
BOOL rl_plat_ClientToScreen(HWND w, POINT *p) { return IMP(ClientToScreen, BOOL (WINAPI *)(HWND, POINT *), ClientToScreen)(w, p); }
