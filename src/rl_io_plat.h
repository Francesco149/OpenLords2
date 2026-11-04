/* rl_io_plat.h — the io partition's platform shim (docs/findings/rl-io.md).
 *
 * Retail reaches the platform two ways, and a port running INSIDE rhost must reach it the same way or it bypasses
 * rhost's virtual layers (files, handles, heap arena, DirectSound, clock):
 *   imports   Win32 / winmm / smackw32 through its import table. rhost overrides the IAT slots (rh_files, rh_handles,
 *             rh_heap, rh_dsound, rh_smack, the virtual clock), so a bound shim calls THROUGH RETAIL'S SLOT; the
 *             standalone calls the real API (smackw32 loaded lazily; absent = SmackOpen fails, as retail tolerates).
 *   CRT       the MSVC debug CRT linked into Lords2.exe. A CRT function with state (the fd table __pioinfo, errno /
 *             _doserrno, time's static tm and asctime's buffer, the debug heap's block list) lives in retail's .data:
 *             bound, the shim calls RETAIL'S OWN CRT FUNCTION (cdecl, no mirror bracket — the CRT touches no port
 *             mirror); the standalone calls the host CRT (malloc: a block with the debug CRT's header, so the ported
 *             _free_dbg 0x4c05a0 frees it the same way). Pure CRT (strcmp, strcat, memset, memcpy) is not shimmed:
 *             the port uses the host's (rl_io.c: rl_strcmp keeps retail's -1 / 0 / +1).
 * rhost binds the shim once (tools/rhost/rh_hooks_rl_io.c: rl_plat_bind_retail); unbound = the standalone. COM calls
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
    X(SmackDoFrame, 0x5cf5fcu)

/* X(name, retail VA) — retail's CRT functions with state */
#define RL_PLAT_CRT(X)                                                                                           \
    X(open, 0x4cea00u) X(close, 0x4ce640u) X(read, 0x4ceff0u) X(write, 0x4cc0b0u) X(lseek, 0x4cc3b0u)            \
    X(chdir, 0x4cf550u) X(time, 0x4c2740u) X(localtime, 0x4c24e0u) X(asctime, 0x4c2340u) X(malloc, 0x4bfa00u)    \
    X(exit, 0x4bf6f0u) X(CrtCheckMemory, 0x4c0cf0u) X(CrtIsValidHeapPointer, 0x4c1170u)                          \
    X(CrtDbgReport, 0x4c3950u) X(CheckBytes, 0x4c0c60u) X(free_base, 0x4c4370u)

void rl_plat_bind_retail(void);   /* rhost: every shim call goes through retail's IAT slot / retail's CRT */
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
