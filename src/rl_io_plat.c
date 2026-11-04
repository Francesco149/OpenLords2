/* rl_io_plat.c — the io partition's platform shim (rl_io_plat.h; docs/findings/rl-io.md). */
#include "rl_io_plat.h"
#include "rl_io.h"

#include <direct.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the host CRT's low-level I/O (msvcrt; <io.h> is shadowed by src/io.h) */
_CRTIMP int __cdecl _open(const char *path, int oflag, ...);
_CRTIMP int __cdecl _close(int fd);
_CRTIMP int __cdecl _read(int fd, void *buf, unsigned n);
_CRTIMP int __cdecl _write(int fd, const void *buf, unsigned n);
_CRTIMP long __cdecl _lseek(int fd, long off, int origin);

static int s_bound;
#define SLOT(n, va) static const uint32_t k_slot_##n = va;
#define CRTVA(n, va) static const uint32_t k_crt_##n = va;
RL_PLAT_IMPORTS(SLOT)
RL_PLAT_CRT(CRTVA)
#undef SLOT
#undef CRTVA

void rl_plat_bind_retail(void) { s_bound = 1; }
int rl_plat_bound(void) { return s_bound; }

/* bound: the function retail's IAT slot holds NOW (rhost's override); else the host's */
#define IMP(n, T, host) (s_bound ? (T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)k_slot_##n : (T)(host))
#define CRT(n, T) ((T)(uintptr_t)k_crt_##n)

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
int rl_plat_open(const char *path, int oflag, int pmode)
{ return s_bound ? CRT(open, int (__cdecl *)(const char *, int, int))(path, oflag, pmode) : _open(path, oflag, pmode); }
int rl_plat_close(int fd)
{ return s_bound ? CRT(close, int (__cdecl *)(int))(fd) : _close(fd); }
int rl_plat_read(int fd, void *buf, unsigned n)
{ return s_bound ? CRT(read, int (__cdecl *)(int, void *, unsigned))(fd, buf, n) : _read(fd, buf, n); }
int rl_plat_write(int fd, const void *buf, unsigned n)
{ return s_bound ? CRT(write, int (__cdecl *)(int, const void *, unsigned))(fd, buf, n) : _write(fd, buf, n); }
int rl_plat_lseek(int fd, int off, int origin)
{ return s_bound ? CRT(lseek, int (__cdecl *)(int, int, int))(fd, off, origin) : (int)_lseek(fd, off, origin); }
int rl_plat_chdir(const char *path)
{ return s_bound ? CRT(chdir, int (__cdecl *)(const char *))(path) : _chdir(path); }
int32_t rl_plat_time(int32_t *t)
{
    if (s_bound) return CRT(time, int32_t (__cdecl *)(int32_t *))(t);
    int32_t v = (int32_t)time(NULL);
    if (t) *t = v;
    return v;
}
void *rl_plat_localtime(const int32_t *t)
{
    if (s_bound) return CRT(localtime, void *(__cdecl *)(const int32_t *))(t);
    time_t tt = (time_t)*t;
    return localtime(&tt);
}
char *rl_plat_asctime(const void *tm)
{ return s_bound ? CRT(asctime, char *(__cdecl *)(const void *))(tm) : asctime((const struct tm *)tm); }
void rl_plat_exit(int code)
{
    if (s_bound) CRT(exit, void (__cdecl *)(int))(code);
    exit(code);
}

/* The standalone's debug heap: retail's free (FUN_004c0580 → the _free_dbg port FUN_004c05a0) walks the debug
 * CRT's block headers (_CrtMemBlockHeader, 0x20 B before the data + 4 no-man's-land bytes after it) and its block
 * list (_pFirstBlock DAT_004e5b28, _pLastBlock DAT_004e5b20, _lCurAlloc DAT_004e5b2c), so the standalone's malloc
 * builds the same block (_heap_alloc_dbg: linked at the head, fills 0xfd / 0xcd) on the host heap. */
#include "rl_io_globals.h"
typedef struct hdr { struct hdr *next, *prev; const char *file; int line; uint32_t size; int use; int req; uint8_t gap[4]; } hdr_t;
void *rl_plat_malloc(uint32_t n)
{
    if (s_bound) return CRT(malloc, void *(__cdecl *)(uint32_t))(n);
    static int req;
    hdr_t *h = malloc(sizeof *h + n + 4);
    if (!h) return NULL;
    h->next = (hdr_t *)(uintptr_t)(uint32_t)g_rl_4e5b28;
    h->prev = NULL;
    if (h->next) h->next->prev = h; else g_rl_4e5b20 = (int32_t)(uintptr_t)h;
    g_rl_4e5b28 = (int32_t)(uintptr_t)h;
    h->file = NULL; h->line = 0; h->size = n; h->use = 1; h->req = ++req;
    g_rl_4e5b2c += (int32_t)n;
    memset(h->gap, 0xfd, 4);
    memset(h + 1, 0xcd, n);
    memset((uint8_t *)(h + 1) + n, 0xfd, 4);
    return h + 1;
}
int rl_plat_CrtCheckMemory(void)
{ return s_bound ? CRT(CrtCheckMemory, int (__cdecl *)(void))() : 1; }
int rl_plat_CrtIsValidHeapPointer(const void *p)
{ return s_bound ? CRT(CrtIsValidHeapPointer, int (__cdecl *)(const void *))(p) : 1; }
int rl_plat_CrtDbgReport(int type, const char *file, int line, const char *mod, const char *fmt, uint32_t a5, uint32_t a6, uint32_t a7)
{
    if (s_bound)
        return CRT(CrtDbgReport, int (__cdecl *)(int, const char *, int, const char *, const char *, ...))(type, file, line, mod, fmt, a5, a6, a7);
    return 0;
}
int rl_plat_CheckBytes(const void *p, int c, int n)
{
    if (s_bound) return CRT(CheckBytes, int (__cdecl *)(const void *, int, int))(p, c, n);
    for (int i = 0; i < n; i++)
        if (((const uint8_t *)p)[i] != (uint8_t)c) return 0;
    return 1;
}
void rl_plat_free_base(void *block)
{
    if (s_bound) CRT(free_base, void (__cdecl *)(void *))(block);
    else free(block);
}
int rl_plat_alloc_hook(uint32_t hook, int type, void *data, int size, int use, int req, const char *file, int line)
{
    if (!s_bound && hook == 0x4c4070u) return 1;   /* the standalone: retail's default _CrtDefaultAllocHook */
    return ((int (__cdecl *)(int, void *, int, int, int, const char *, int))(uintptr_t)hook)(type, data, size, use, req, file, line);
}
