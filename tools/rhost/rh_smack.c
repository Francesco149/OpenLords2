/* rh_smack.c — deterministic Smacker (--movies): the game's smackw32.dll, mapped by rhost itself.
 *
 * The real loader would place smackw32 (preferred base 0x400000 = retail's) wherever it fits and run
 * its DllMain against the real system: its CRT heap, file handles, clock and timer callbacks would all
 * differ run to run, and the SMACK* it hands retail (DAT_004d4d58) would carry that into retail state.
 * So rhost maps it like retail: at a fixed base (relocations applied), every import resolved through
 * rhost's virtual layers — the import retail itself has (its IAT slot, i.e. rhost's override when there
 * is one: virtual clock, virtual multimedia timer, the heap arena, fake handles + the file overlay,
 * Sleep), silent waveOut fakes, the real system for the rest (GDI/USER blit paths retail never uses) —
 * then calls its DllMain. Retail's smackw32 ordinal slots are bound to the mapped exports.
 * A thread created by smackw32 is logged loudly (it would break determinism). */
#include "rhost.h"
#include <mmsystem.h>

#define SMACK_BASE 0x18000000u
static uint8_t *g_base;
static uint32_t g_size;

/* ---- silent waveOut on the virtual clock (smackw32 plays a movie's soundtrack through it with CALLBACK_NULL
 * and paces the video by polling WHDR_DONE): a written buffer completes when the virtual clock has played
 * its bytes (nAvgBytesPerSec), in order; completions are applied at frame boundaries (rh_smack_frame) and on
 * every waveOut call. ---- */
#define WQ 64
static struct { LPWAVEHDR h; uint64_t due_us; } g_wq[WQ]; static int g_nwq;
static uint32_t g_wo_avg = 11025; static uint64_t g_wo_tail_us;
/* --audible: a copy of every buffer also goes to a real device (side channel; pacing stays virtual) */
static HWAVEOUT g_real; static WAVEHDR g_rh[32]; static uint8_t *g_rbuf[32]; static int g_rhead;
static void real_open(LPCWAVEFORMATEX f)
{
    if (!O.audible || !f) return;
    if (g_real) { waveOutReset(g_real); waveOutClose(g_real); g_real = NULL; }
    if (waveOutOpen(&g_real, WAVE_MAPPER, f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { g_real = NULL; return; }
    for (int i = 0; i < 32; i++) { if (!g_rbuf[i]) g_rbuf[i] = malloc(65536); memset(&g_rh[i], 0, sizeof g_rh[i]); }
}
static void real_write(const WAVEHDR *w)
{
    if (!g_real || w->dwBufferLength > 65536) return;
    WAVEHDR *h = &g_rh[g_rhead];
    if (h->dwFlags & WHDR_PREPARED) { if (!(h->dwFlags & WHDR_DONE)) return; waveOutUnprepareHeader(g_real, h, sizeof *h); }
    memcpy(g_rbuf[g_rhead], w->lpData, w->dwBufferLength);
    memset(h, 0, sizeof *h); h->lpData = (LPSTR)g_rbuf[g_rhead]; h->dwBufferLength = w->dwBufferLength;
    waveOutPrepareHeader(g_real, h, sizeof *h); waveOutWrite(g_real, h, sizeof *h);
    g_rhead = (g_rhead + 1) % 32;
}
static uint64_t now_us(void) { return (uint64_t)rh_vnow() * 1000u; }
static void wo_complete(int all)
{
    uint64_t now = now_us();
    int k = 0;
    while (k < g_nwq && (all || g_wq[k].due_us <= now)) { g_wq[k].h->dwFlags |= WHDR_DONE; g_wq[k].h->dwFlags &= ~WHDR_INQUEUE; k++; }
    if (k) { memmove(g_wq, g_wq + k, (g_nwq - k) * sizeof *g_wq); g_nwq -= k; }
    if (all) g_wo_tail_us = now;
}
void rh_smack_frame(void) { if (g_nwq) wo_complete(0); }
static MMRESULT WINAPI wo_Open(LPHWAVEOUT h, UINT id, LPCWAVEFORMATEX f, DWORD_PTR cb, DWORD_PTR inst, DWORD fl)
{
    (void)id; (void)inst;
    if (fl & 0x00070000u) rh_log("movie: waveOutOpen with callback type %08lx (%p): not emulated\n", (unsigned long)fl, (void *)cb);
    if (fl & WAVE_FORMAT_QUERY) return MMSYSERR_NOERROR;
    g_wo_avg = f && f->nAvgBytesPerSec ? f->nAvgBytesPerSec : 11025;
    g_wo_tail_us = now_us(); g_nwq = 0;
    real_open(f);
    if (h) *h = (HWAVEOUT)(uintptr_t)0x0a110001u;
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI wo_Close(HWAVEOUT h) { (void)h; wo_complete(1); return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wo_Prepare(HWAVEOUT h, LPWAVEHDR w, UINT n) { (void)h; (void)n; w->dwFlags |= WHDR_PREPARED; w->dwFlags &= ~WHDR_DONE; return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wo_Unprepare(HWAVEOUT h, LPWAVEHDR w, UINT n)
{ (void)h; (void)n; wo_complete(0); if (w->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING; w->dwFlags &= ~WHDR_PREPARED; return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wo_Write(HWAVEOUT h, LPWAVEHDR w, UINT n)
{
    (void)h; (void)n;
    wo_complete(0);
    if (g_nwq == WQ) wo_complete(1);
    uint64_t start = g_wo_tail_us > now_us() ? g_wo_tail_us : now_us();
    g_wo_tail_us = start + (uint64_t)w->dwBufferLength * 1000000u / g_wo_avg;
    w->dwFlags = (w->dwFlags | WHDR_INQUEUE) & ~WHDR_DONE;
    g_wq[g_nwq].h = w; g_wq[g_nwq++].due_us = g_wo_tail_us;
    real_write(w);
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI wo_Reset(HWAVEOUT h) { (void)h; wo_complete(1); if (g_real) waveOutReset(g_real); return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wo_SetVolume(HWAVEOUT h, DWORD v) { (void)h; (void)v; return MMSYSERR_NOERROR; }
static MMRESULT WINAPI wo_GetID(HWAVEOUT h, LPUINT id) { (void)h; if (id) *id = 0; return MMSYSERR_NOERROR; }

/* ---- smackw32's own memory: a fixed region OUTSIDE retail's hashed arena (its CRT heap holds thread ids,
 * TLS and system-DLL pointers that are not retail state). Reserve-on-demand bump spans, 64 KB granular. ---- */
#define SHEAP_LO 0x19000000u
#define SHEAP_HI 0x1b000000u
static uint32_t g_stop = SHEAP_LO;
static LPVOID WINAPI sm_VirtualAlloc(LPVOID a, SIZE_T n, DWORD type, DWORD prot)
{
    if (a) {
        uint32_t v = (uint32_t)(uintptr_t)a;
        if (v >= SHEAP_LO && v < SHEAP_HI) return (type & MEM_COMMIT) ? VirtualAlloc(a, n, MEM_COMMIT, prot) : a;
        return VirtualAlloc(a, n, type, prot);
    }
    uint32_t sz = ((uint32_t)n + 0xffffu) & ~0xffffu;
    if (g_stop + sz > SHEAP_HI) rh_fatal("movie: smackw32 heap region exhausted");
    uint32_t p = g_stop; g_stop += sz;
    if (!VirtualAlloc((void *)(uintptr_t)p, sz, MEM_RESERVE, PAGE_READWRITE)) rh_fatal("movie: cannot reserve %08x", p);
    if ((type & MEM_COMMIT) && !VirtualAlloc((void *)(uintptr_t)p, n, MEM_COMMIT, prot)) return NULL;
    return (void *)(uintptr_t)p;
}
static BOOL WINAPI sm_VirtualFree(LPVOID a, SIZE_T n, DWORD type)
{
    uint32_t v = (uint32_t)(uintptr_t)a;
    if (v >= SHEAP_LO && v < SHEAP_HI) return type & MEM_RELEASE ? VirtualFree(a, 0, MEM_DECOMMIT) : VirtualFree(a, n, type);
    return VirtualFree(a, n, type);
}
static DWORD WINAPI sm_GetCurrentThreadId(void) { return 0x0000a11cu; }
static DWORD WINAPI sm_GetCurrentProcessId(void) { return 0x0000a110u; }
static HANDLE WINAPI sm_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL own, LPCSTR name) { (void)name; return rh_handle_fake(CreateMutexA(sa, own, NULL)); }
static BOOL WINAPI sm_ReleaseMutex(HANDLE h) { return ReleaseMutex(rh_handle_real(h)); }

typedef HANDLE (WINAPI *ct_t)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
static ct_t p_ct;
static HANDLE WINAPI sm_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T st, LPTHREAD_START_ROUTINE fn, LPVOID a, DWORD fl, LPDWORD tid)
{
    rh_log("movie: WARNING smackw32 CreateThread(%p) at frame %u — not deterministic\n", (void *)fn, g_frame);
    return p_ct(sa, st, fn, a, fl, tid);
}

static void *resolve(const char *dll, const char *name, int ord)
{
    if (name) {
        static const struct { const char *n; void *f; } FAKE[] = {
            { "waveOutOpen", (void *)wo_Open }, { "waveOutClose", (void *)wo_Close }, { "waveOutPrepareHeader", (void *)wo_Prepare },
            { "waveOutUnprepareHeader", (void *)wo_Unprepare }, { "waveOutWrite", (void *)wo_Write }, { "waveOutReset", (void *)wo_Reset },
            { "waveOutSetVolume", (void *)wo_SetVolume }, { "waveOutGetID", (void *)wo_GetID },
            { "CreateThread", (void *)sm_CreateThread },
            { "VirtualAlloc", (void *)sm_VirtualAlloc }, { "VirtualFree", (void *)sm_VirtualFree },
            { "GetCurrentThreadId", (void *)sm_GetCurrentThreadId }, { "GetCurrentProcessId", (void *)sm_GetCurrentProcessId },
            { "CreateMutexA", (void *)sm_CreateMutexA }, { "ReleaseMutex", (void *)sm_ReleaseMutex },
        };
        for (size_t i = 0; i < sizeof FAKE / sizeof *FAKE; i++) if (!strcmp(FAKE[i].n, name)) return FAKE[i].f;
        void *r = iat_lookup(name);          /* retail's own slot: rhost's override when it has one */
        if (r) return r;
    }
    HMODULE h = LoadLibraryA(dll);
    return h ? (void *)GetProcAddress(h, name ? name : (LPCSTR)(uintptr_t)ord) : NULL;
}

static void __cdecl unresolved(void) { rh_fatal("smackw32 called an unresolved import"); }

/* map <gamedir>\Smackw32.dll at SMACK_BASE; returns its module base or NULL */
static uint8_t *map_smack(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(n);
    if (fread(file, 1, n, f) != (size_t)n) { fclose(f); free(file); return NULL; }
    fclose(f);
    IMAGE_NT_HEADERS32 *nt = (void *)(file + ((IMAGE_DOS_HEADER *)file)->e_lfanew);
    g_size = nt->OptionalHeader.SizeOfImage;
    uint8_t *b = VirtualAlloc((void *)(uintptr_t)SMACK_BASE, g_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (b != (uint8_t *)(uintptr_t)SMACK_BASE) rh_fatal("movie: cannot map smackw32 at %08x", SMACK_BASE);
    memcpy(b, file, nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t raw = sec[i].SizeOfRawData, al = nt->OptionalHeader.SectionAlignment;
        uint32_t vlim = (sec[i].Misc.VirtualSize + al - 1) & ~(al - 1);
        if (sec[i].Misc.VirtualSize && raw > vlim) raw = vlim;
        /* smackw32 is a Watcom PE: VirtualSize 0 everywhere, and .bss has a raw size but no raw data
         * (PointerToRawData 0) — the loader zero-fills it, so must we */
        if (raw && sec[i].PointerToRawData) memcpy(b + sec[i].VirtualAddress, file + sec[i].PointerToRawData, raw);
    }
    /* relocations: the file prefers 0x400000 */
    uint32_t delta = SMACK_BASE - nt->OptionalHeader.ImageBase;
    IMAGE_DATA_DIRECTORY rd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for (uint32_t at = 0; delta && at + 8 <= rd.Size; ) {
        IMAGE_BASE_RELOCATION *br = (void *)(b + rd.VirtualAddress + at);
        if (br->SizeOfBlock < 8) break;
        uint16_t *e = (uint16_t *)(br + 1);
        for (uint32_t i = 0; i < (br->SizeOfBlock - 8) / 2; i++) {
            uint32_t t = e[i] >> 12;
            if (t == IMAGE_REL_BASED_HIGHLOW) *(uint32_t *)(b + br->VirtualAddress + (e[i] & 0xfff)) += delta;
            else if (t != IMAGE_REL_BASED_ABSOLUTE) rh_fatal("movie: smackw32 relocation type %u", t);
        }
        at += br->SizeOfBlock;
    }
    /* imports */
    int nres = 0, nunres = 0;
    IMAGE_DATA_DIRECTORY *idd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(b + idd->VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(b + d->Name);
        IMAGE_THUNK_DATA32 *ilt = (void *)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32 *iat = (void *)(b + d->FirstThunk);
        for (; ilt->u1.AddressOfData; ilt++, iat++) {
            int ord = (ilt->u1.Ordinal & IMAGE_ORDINAL_FLAG32) ? (int)(ilt->u1.Ordinal & 0xffff) : 0;
            const char *name = ord ? NULL : (const char *)((IMAGE_IMPORT_BY_NAME *)(b + ilt->u1.AddressOfData))->Name;
            void *p = resolve(dll, name, ord);
            if (p) nres++; else { nunres++; p = (void *)unresolved; }
            iat->u1.Function = (uint32_t)(uintptr_t)p;
        }
    }
    uint32_t entry = SMACK_BASE + nt->OptionalHeader.AddressOfEntryPoint;
    free(file);
    rh_log("movie: smackw32 mapped at %08x+%x (%d imports, %d unresolved); DllMain\n", SMACK_BASE, g_size, nres, nunres);
    typedef BOOL (WINAPI *dllmain_t)(HINSTANCE, DWORD, LPVOID);
    if (!((dllmain_t)(uintptr_t)entry)((HINSTANCE)b, DLL_PROCESS_ATTACH, NULL)) rh_fatal("movie: smackw32 DllMain failed");
    return b;
}

static void *export_ord(uint8_t *b, int ord)
{
    IMAGE_NT_HEADERS32 *nt = (void *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_EXPORT_DIRECTORY *ex = (void *)(b + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    uint32_t i = (uint32_t)ord - ex->Base;
    if (i >= ex->NumberOfFunctions) return NULL;
    return b + ((uint32_t *)(b + ex->AddressOfFunctions))[i];
}

/* after every rhost layer is installed (smackw32's imports bind to them): map it, bind retail's slots */
void rh_smack_install(void)
{
    if (!O.movies) return;
    p_ct = (ct_t)iat_lookup("CreateThread");
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\Smackw32.dll", O.gamedir);
    g_base = map_smack(p);
    if (!g_base) rh_fatal("movie: cannot read %s", p);
    IMAGE_NT_HEADERS32 *nt = (void *)(0x400000 + ((IMAGE_DOS_HEADER *)0x400000)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *d = (void *)(0x400000 + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    int n = 0;
    for (; d->Name; d++) {
        if (_stricmp((const char *)(0x400000 + d->Name), "smackw32.DLL")) continue;
        IMAGE_THUNK_DATA32 *ilt = (void *)(0x400000 + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32 *iat = (void *)(0x400000 + d->FirstThunk);
        for (; ilt->u1.AddressOfData; ilt++, iat++) {
            int ord = (int)(ilt->u1.Ordinal & 0xffff);
            void *f = export_ord(g_base, ord);
            if (!f) rh_fatal("movie: smackw32 has no ordinal %d", ord);
            iat->u1.Function = (uint32_t)(uintptr_t)f; n++;
        }
    }
    rh_log("movie: %d retail smackw32 imports bound (deterministic Smacker)\n", n);
}
int rh_smack_in(uint32_t v) { return g_base && ((v >= SMACK_BASE && v < SMACK_BASE + g_size) || (v >= SHEAP_LO && v < SHEAP_HI)); }
