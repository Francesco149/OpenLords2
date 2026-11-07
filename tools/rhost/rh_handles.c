/* rh_handles.c — deterministic kernel / mmio handle values for retail Lords2.
 *
 * Handle numbers depend on everything else the process opened first, so the values retail
 * stores (CRT __pioinfo osfhnd, the music stream's HMMIO, event handles) would differ run to
 * run. Every handle-RETURNING retail import gets a fake, sequence-numbered handle; every
 * handle-CONSUMING retail import translates it back. Handles retail never received from us
 * (pseudo handles, NULL, INVALID_HANDLE_VALUE) pass through unchanged.
 * Installed AFTER rh_files so it wraps the overlay's overrides. (openrecet / OpenSummoners origin.) */
#include "rhost.h"
#include <mmsystem.h>

#define FAKE_BASE 0x7E000000u
#define FAKE_MAX  8192
static HANDLE g_real[FAKE_MAX];
static uint32_t g_next;
/* a file handle's identity (CreateFileA's arguments) — the shadow re-opens a file a shadowed call closed (below) */
typedef struct { char path[MAX_PATH]; DWORD acc, share, fl; } fopen_t;
static fopen_t *g_fo[FAKE_MAX];
/* files opened inside a shadowed call, tagged with the meta generation (rh_handles_meta_save) current then: the
 * port side's opens (after the snapshot of retail's post-state) are discarded with that side and closed at the
 * restore, or a leaked handle holds the file (no FILE_SHARE_DELETE: a later remove / rename of it fails) */
typedef struct { HANDLE real; uint32_t gen; } sopen_t;
static sopen_t g_sopen[256]; static int g_nsopen; static uint32_t g_gen;
static CRITICAL_SECTION g_lk;
static long g_live;

HANDLE rh_handle_fake(HANDLE real)
{
    if (real == NULL || real == INVALID_HANDLE_VALUE) return real;
    EnterCriticalSection(&g_lk);
    uint32_t k;
    if (g_next < FAKE_MAX)
        k = g_next++;
    else {   /* the sequence is spent: reuse the lowest closed slot (deterministic — the call order is). Retail opens and
              * closes files every frame on some screens (the Battle Master ratings 0x2e re-reads score.pl8 + score.dat
              * per frame): a long stay there issues thousands of handles with one live. */
        for (k = 0; k < FAKE_MAX && g_real[k]; k++) {}
        if (k == FAKE_MAX) { LeaveCriticalSection(&g_lk); rh_fatal("rh_handles: more than %d live handles", FAKE_MAX); }
    }
    g_real[k] = real; g_live++;
    if (g_fo[k]) { free(g_fo[k]); g_fo[k] = NULL; }   /* (h_CreateFileA sets it for a file) */
    LeaveCriticalSection(&g_lk);
    return (HANDLE)(uintptr_t)(FAKE_BASE + 4u * k);
}
HANDLE rh_handle_real(HANDLE h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v < FAKE_BASE || v >= FAKE_BASE + 4u * FAKE_MAX || (v & 3)) return h;
    HANDLE r = g_real[(v - FAKE_BASE) >> 2];
    return r ? r : INVALID_HANDLE_VALUE;
}
void rh_handle_forget(HANDLE h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v < FAKE_BASE || v >= FAKE_BASE + 4u * FAKE_MAX || (v & 3)) return;
    EnterCriticalSection(&g_lk); g_real[(v - FAKE_BASE) >> 2] = NULL; g_live--; LeaveCriticalSection(&g_lk);
}
#define fake_of rh_handle_fake
#define real_of rh_handle_real

typedef HANDLE (WINAPI *cfa_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *ct_t)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
typedef HMMIO (WINAPI *mo_t)(LPSTR, LPMMIOINFO, DWORD);
typedef HANDLE (WINAPI *ff_t)(LPCSTR, LPWIN32_FIND_DATAA);
static cfa_t p_CreateFileA; static ct_t p_CreateThread; static mo_t p_mmioOpenA;

static HANDLE WINAPI h_CreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t)
{
    HANDLE h = fake_of(p_CreateFileA(p, a, s, sa, d, f, real_of(t)));
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v >= FAKE_BASE && v < FAKE_BASE + 4u * FAKE_MAX && !(v & 3)) {
        uint32_t k = (v - FAKE_BASE) >> 2;
        if (!g_fo[k]) g_fo[k] = malloc(sizeof *g_fo[k]);
        snprintf(g_fo[k]->path, MAX_PATH, "%s", p); g_fo[k]->acc = a; g_fo[k]->share = s; g_fo[k]->fl = f;
        if (rh_in_shadow() && g_nsopen < 256) { g_sopen[g_nsopen].real = g_real[k]; g_sopen[g_nsopen].gen = g_gen; g_nsopen++; }
    }
    return h;
}
static HANDLE WINAPI h_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T st, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD fl, LPDWORD tid)
{
    rh_log("WARNING: retail CreateThread(%p) at frame %u — a second retail thread is not deterministic\n", (void *)fn, g_frame);
    return fake_of(p_CreateThread(sa, st, fn, arg, fl, tid));
}
static HANDLE WINAPI h_CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL man, BOOL init, LPCSTR name) { return fake_of(CreateEventA(sa, man, init, NULL)); (void)name; }
static BOOL WINAPI h_SetEvent(HANDLE h) { return SetEvent(real_of(h)); }
static BOOL WINAPI h_ResetEvent(HANDLE h) { return ResetEvent(real_of(h)); }
static DWORD WINAPI h_WaitForSingleObject(HANDLE h, DWORD ms) { return WaitForSingleObject(real_of(h), ms); }
static BOOL WINAPI h_GetExitCodeThread(HANDLE h, LPDWORD c) { return GetExitCodeThread(real_of(h), c); }
static BOOL WINAPI h_TerminateThread(HANDLE h, DWORD c) { return TerminateThread(real_of(h), c); }
/* a file closed inside a shadowed call: the shadow puts the pre-state back before the port side runs, so the file
 * must be open again there (the port's own close / read / seek would otherwise fail on a dead handle) */
typedef struct { HANDLE real; fopen_t fo; } closed_t;
static closed_t g_closed[64]; static int g_nclosed;
static BOOL WINAPI h_CloseHandle(HANDLE h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (rh_in_shadow() && v >= FAKE_BASE && v < FAKE_BASE + 4u * FAKE_MAX && !(v & 3) && g_fo[(v - FAKE_BASE) >> 2] &&
        g_real[(v - FAKE_BASE) >> 2] && g_nclosed < 64) {
        g_closed[g_nclosed].real = g_real[(v - FAKE_BASE) >> 2];
        g_closed[g_nclosed].fo = *g_fo[(v - FAKE_BASE) >> 2];
        g_nclosed++;
    }
    BOOL r = CloseHandle(real_of(h)); rh_handle_forget(h); return r;
}
static BOOL WINAPI h_ReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD got, LPOVERLAPPED o)
{ rh_shadow_prewrite(b, n); rh_shadow_prewrite(got, 4); return ReadFile(real_of(h), b, n, got, o); }
static BOOL WINAPI h_WriteFile(HANDLE h, LPCVOID b, DWORD n, LPDWORD put, LPOVERLAPPED o)
{
    if (h == (HANDLE)(uintptr_t)1 || h == (HANDLE)(uintptr_t)2 || h == INVALID_HANDLE_VALUE) { if (put) *put = n; return TRUE; }
    return WriteFile(real_of(h), b, n, put, o);
}
static DWORD WINAPI h_SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD m) { return SetFilePointer(real_of(h), lo, hi, m); }
static DWORD WINAPI h_GetFileType(HANDLE h)
{
    if (h == INVALID_HANDLE_VALUE || !h) return FILE_TYPE_UNKNOWN;
    return GetFileType(real_of(h));
}
static BOOL WINAPI h_FlushFileBuffers(HANDLE h) { return FlushFileBuffers(real_of(h)); }
static BOOL WINAPI h_SetEndOfFile(HANDLE h) { return SetEndOfFile(real_of(h)); }
static BOOL WINAPI h_SetStdHandle(DWORD n, HANDLE h) { (void)n; (void)h; return TRUE; }
/* mmio: the HMMIO retail keeps in its stream records is a handle too */
static HMMIO WINAPI h_mmioOpenA(LPSTR p, LPMMIOINFO info, DWORD fl) { return (HMMIO)fake_of((HANDLE)p_mmioOpenA(p, info, fl)); }
static MMRESULT WINAPI h_mmioClose(HMMIO h, UINT fl) { MMRESULT r = mmioClose((HMMIO)real_of((HANDLE)h), fl); rh_handle_forget((HANDLE)h); return r; }
static LONG WINAPI h_mmioRead(HMMIO h, HPSTR b, LONG n) { rh_shadow_prewrite(b, n > 0 ? (size_t)n : 0); return mmioRead((HMMIO)real_of((HANDLE)h), b, n); }
static LONG WINAPI h_mmioSeek(HMMIO h, LONG o, int org) { return mmioSeek((HMMIO)real_of((HANDLE)h), o, org); }
static MMRESULT WINAPI h_mmioDescend(HMMIO h, LPMMCKINFO c, const MMCKINFO *p, UINT fl) { return mmioDescend((HMMIO)real_of((HANDLE)h), c, p, fl); }
static MMRESULT WINAPI h_mmioAscend(HMMIO h, LPMMCKINFO c, UINT fl) { return mmioAscend((HMMIO)real_of((HANDLE)h), c, fl); }
static MMRESULT WINAPI h_mmioGetInfo(HMMIO h, LPMMIOINFO i, UINT fl)
{ MMRESULT r = mmioGetInfo((HMMIO)real_of((HANDLE)h), i, fl); if (!r) i->hmmio = h; return r; }
static MMRESULT WINAPI h_mmioSetInfo(HMMIO h, LPCMMIOINFO i, UINT fl)
{ MMIOINFO c = *i; c.hmmio = (HMMIO)real_of((HANDLE)i->hmmio); return mmioSetInfo((HMMIO)real_of((HANDLE)h), &c, fl); }
static MMRESULT WINAPI h_mmioAdvance(HMMIO h, LPMMIOINFO i, UINT fl)
{
    HMMIO keep = i->hmmio; i->hmmio = (HMMIO)real_of((HANDLE)keep);
    MMRESULT r = mmioAdvance((HMMIO)real_of((HANDLE)h), i, fl);
    i->hmmio = keep;
    return r;
}

void rh_handles_install(void)
{
    InitializeCriticalSection(&g_lk);
    p_CreateFileA = (cfa_t)iat_lookup("CreateFileA");
    p_CreateThread = (ct_t)iat_lookup("CreateThread");
    p_mmioOpenA = (mo_t)iat_lookup("mmioOpenA");
    iat_override("CreateFileA", h_CreateFileA);
    iat_override("CreateThread", h_CreateThread);
    iat_override("CreateEventA", h_CreateEventA);
    iat_override("SetEvent", h_SetEvent);
    iat_override("ResetEvent", h_ResetEvent);
    iat_override("WaitForSingleObject", h_WaitForSingleObject);
    iat_override("GetExitCodeThread", h_GetExitCodeThread);
    iat_override("TerminateThread", h_TerminateThread);
    iat_override("CloseHandle", h_CloseHandle);
    iat_override("ReadFile", h_ReadFile);
    iat_override("WriteFile", h_WriteFile);
    iat_override("SetFilePointer", h_SetFilePointer);
    iat_override("GetFileType", h_GetFileType);
    iat_override("FlushFileBuffers", h_FlushFileBuffers);
    iat_override("SetEndOfFile", h_SetEndOfFile);
    iat_override("SetStdHandle", h_SetStdHandle);
    iat_override("mmioOpenA", h_mmioOpenA);
    iat_override("mmioClose", h_mmioClose);
    iat_override("mmioRead", h_mmioRead);
    iat_override("mmioSeek", h_mmioSeek);
    iat_override("mmioDescend", h_mmioDescend);
    iat_override("mmioAscend", h_mmioAscend);
    iat_override("mmioGetInfo", h_mmioGetInfo);
    iat_override("mmioSetInfo", h_mmioSetInfo);
    iat_override("mmioAdvance", h_mmioAdvance);
}

long rh_handles_live(void) { return g_live; }
uint32_t rh_handles_issued(void) { return g_next; }

/* handle-sequence meta (see rh_media_meta_save): the fake numbers issued so far and their real handles. A real
 * handle opened by a discarded run is dropped (leaked to the OS), never closed twice. Also each live FILE's position
 * (after real[]: an int64 per slot, -1 = not a file): a shadowed call that reads / seeks / writes a file opened before
 * it leaves the OS position where retail's side stopped — the port side must start where retail's did (_read,
 * _lseek, _write rows). A file the call closed is re-opened (its CreateFileA arguments, OPEN_EXISTING) at restore. */
typedef struct { uint32_t next; long live; HANDLE real[]; } handles_meta_t;
#define META_POS(m) ((int64_t *)((m)->real + (m)->next))
void *rh_handles_meta_save(void)
{
    EnterCriticalSection(&g_lk);
    handles_meta_t *m = malloc(sizeof *m + g_next * (sizeof(HANDLE) + sizeof(int64_t)));
    m->next = g_next; m->live = g_live; memcpy(m->real, g_real, g_next * sizeof(HANDLE));
    g_gen++;
    int64_t *pos = META_POS(m);
    for (uint32_t k = 0; k < g_next; k++) {
        LARGE_INTEGER z = { .QuadPart = 0 }, at;
        pos[k] = g_real[k] && g_fo[k] && SetFilePointerEx(g_real[k], z, &at, FILE_CURRENT) ? at.QuadPart : -1;
    }
    LeaveCriticalSection(&g_lk);
    return m;
}
void rh_handles_meta_restore(const void *p)
{
    const handles_meta_t *m = p;
    const int64_t *pos = META_POS(m);
    EnterCriticalSection(&g_lk);
    for (uint32_t k = m->next; k < g_next; k++) g_real[k] = NULL;
    memcpy(g_real, m->real, m->next * sizeof(HANDLE)); g_next = m->next; g_live = m->live;
    for (int c = 0; c < g_nclosed; c++)   /* closed since: open it again for this state */
        for (uint32_t k = 0; k < m->next; k++)
            if (g_real[k] == g_closed[c].real) {
                const fopen_t *f = &g_closed[c].fo;
                HANDLE r = p_CreateFileA(f->path, f->acc, f->share, NULL, OPEN_EXISTING, f->fl, NULL);
                g_real[k] = r == INVALID_HANDLE_VALUE ? NULL : r;
                if (!g_real[k]) rh_log("rh_handles: shadow: cannot re-open %s (closed in a shadowed call)\n", f->path);
                ((handles_meta_t *)m)->real[k] = g_real[k];   /* (a later restore of this snapshot: the new handle) */
                if (!g_fo[k]) g_fo[k] = malloc(sizeof *g_fo[k]);
                *g_fo[k] = *f;
                break;
            }
    g_nclosed = 0;
    for (int i = 0; i < g_nsopen; i++) {   /* opened since the latest snapshot and not in this one: discarded */
        int keep = 0;
        for (uint32_t k = 0; k < m->next && !keep; k++) keep = g_real[k] == g_sopen[i].real;
        if (g_sopen[i].gen == g_gen && !keep) CloseHandle(g_sopen[i].real);
    }
    g_nsopen = 0;
    for (uint32_t k = 0; k < m->next; k++)
        if (g_real[k] && pos[k] >= 0) {
            LARGE_INTEGER to = { .QuadPart = pos[k] };
            SetFilePointerEx(g_real[k], to, NULL, FILE_BEGIN);
        }
    LeaveCriticalSection(&g_lk);
}
