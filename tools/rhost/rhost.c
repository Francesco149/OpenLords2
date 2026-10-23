/* rhost — run retail Lords of the Realm II inside this process, deterministically.
 * Ported from openrecet / OpenSummoners tools/rhost (2026-10 methodology reset) to Lords2.exe
 * (MSVC 4.2, DirectDraw 8 bpp, DirectSound + a multimedia-timer music streamer, window-message
 * input, Smacker movies, single-threaded in single player).
 *
 *   rhost.exe --exe Lords2.exe --game <gamedir> --run <rundir> [options]
 *
 * Determinism (always on):
 *   clock   timeGetTime = GetTickCount = virtual: vbase + step_ms per frame, frozen inside a frame;
 *           after creep_after reads without a frame it creeps 1 ms/read (busy-wait breaker, a pure
 *           function of the call sequence); Sleep(n) advances it n ms. Local/system time from it.
 *   frame   one call of FUN_004b99c0 (the sim + render driver) — every call site is redirected
 *           to frame_wrap: retail's frame k runs with g_frame == k.
 *   video   software DirectDraw (rh_ddraw.c): never fullscreen, no display device needed.
 *   sound   silent virtual DirectSound + timeSetEvent fired on the main thread (rh_dsound.c).
 *   window  fake HWND, virtual message queue, input only from the trace (rh_win.c).
 *   heap    fixed-address arena (rh_heap.c); handles sequence-numbered (rh_handles.c).
 *   files   game dir read-only, writes copy-on-write into the run dir (rh_files.c).
 *   movies  SmackOpen fails (retail logs and skips the movie) unless --movies.
 *   x87     control word 0x027F at entry (what Windows gives a thread; mingw's CRT sets 0x037F).
 * Output: <rundir>\rhost.log; last line "RESULT key=value ..." (parsed by rhost.py). */
#include "rhost.h"

rh_opts_t O;
volatile uint32_t g_frame;
FILE *g_log;
static FILE *g_logf;
static char g_gamedir_abs[MAX_PATH];
static int g_finished;
uint32_t RT_TEXT_LO, RT_TEXT_SZ, RT_DATA_LO, RT_DATA_HI, RT_IMG_HI;

void rh_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap); fflush(stdout);
    if (g_logf) { va_start(ap, fmt); vfprintf(g_logf, fmt, ap); va_end(ap); fflush(g_logf); }
}
void rh_fatal(const char *fmt, ...)
{
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    rh_log("FATAL %s\nRESULT status=fatal frame=%u\n", b, g_frame);
    if (g_logf) fflush(g_logf);
    ExitProcess(3);
}

/* ---- IAT patching (retail image only) ---- */
static IMAGE_IMPORT_DESCRIPTOR *imports(void)
{
    IMAGE_NT_HEADERS32 *nt = (void *)(0x400000 + ((IMAGE_DOS_HEADER *)0x400000)->e_lfanew);
    return (void *)(0x400000 + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
}
static uint32_t *iat_slot(const char *name)
{
    for (IMAGE_IMPORT_DESCRIPTOR *d = imports(); d->Name; d++) {
        IMAGE_THUNK_DATA32 *ilt = (void *)(0x400000 + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32 *iat = (void *)(0x400000 + d->FirstThunk);
        for (; ilt->u1.AddressOfData; ilt++, iat++)
            if (!(ilt->u1.Ordinal & IMAGE_ORDINAL_FLAG32) &&
                !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(0x400000 + ilt->u1.AddressOfData))->Name, name))
                return (uint32_t *)&iat->u1.Function;
    }
    return NULL;
}
void iat_override(const char *name, void *fn)
{
    uint32_t *s = iat_slot(name);
    if (!s) rh_fatal("iat_override: retail does not import %s", name);
    *s = (uint32_t)(uintptr_t)fn;
}
int iat_try(const char *name, void *fn) { uint32_t *s = iat_slot(name); if (s) *s = (uint32_t)(uintptr_t)fn; return s != NULL; }
void *iat_lookup(const char *name) { uint32_t *s = iat_slot(name); return s ? (void *)(uintptr_t)*s : NULL; }
int redirect_calls(uint32_t va, void *hook)
{
    int n = 0;
    for (uint8_t *p = (uint8_t *)(uintptr_t)RT_TEXT_LO, *e = p + RT_TEXT_SZ - 5; p < e; p++) {
        if (*p != 0xE8) continue;
        uint32_t tgt = (uint32_t)(uintptr_t)p + 5 + *(int32_t *)(p + 1);
        if (tgt != va) continue;
        *(int32_t *)(p + 1) = (int32_t)((uint32_t)(uintptr_t)hook - ((uint32_t)(uintptr_t)p + 5));
        n++;
    }
    return n;
}

/* ---- fixed-address host objects (vtables, command line, environment) ---- */
#define FIXED_LO 0x5f080000u
#define FIXED_SZ 0x10000u
static uint8_t *g_fixed_top;
void *rh_fixed_alloc(size_t n)
{
    if (!g_fixed_top) {
        g_fixed_top = VirtualAlloc((void *)(uintptr_t)FIXED_LO, FIXED_SZ, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (g_fixed_top != (uint8_t *)(uintptr_t)FIXED_LO) rh_fatal("cannot map the fixed object page at %08x", FIXED_LO);
    }
    uint8_t *p = g_fixed_top;
    g_fixed_top += (n + 15) & ~(size_t)15;
    if (g_fixed_top > (uint8_t *)(uintptr_t)(FIXED_LO + FIXED_SZ)) rh_fatal("fixed object page exhausted");
    return memset(p, 0, n);
}

/* ---- fixed-address main stack (retail stores stack addresses; TEB bounds for SEH) ---- */
#define MAIN_STACK_LO 0x58000000u
#define MAIN_STACK_HI 0x59000000u
#define STACK_GUARD   0x10000u
static void stacks_init(void)
{
    if (VirtualAlloc((void *)(uintptr_t)MAIN_STACK_LO, MAIN_STACK_HI - MAIN_STACK_LO, MEM_RESERVE, PAGE_READWRITE) != (void *)(uintptr_t)MAIN_STACK_LO)
        rh_fatal("cannot reserve the fixed stack at %08x (err %lu)", MAIN_STACK_LO, GetLastError());
    if (!VirtualAlloc((void *)(uintptr_t)(MAIN_STACK_LO + STACK_GUARD), MAIN_STACK_HI - MAIN_STACK_LO - STACK_GUARD, MEM_COMMIT, PAGE_READWRITE))
        rh_fatal("cannot commit the fixed stack (err %lu)", GetLastError());
}
static uint32_t __attribute__((noinline)) call_on_stack(void *fn, uint32_t lo, uint32_t top)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    void *ob = tib->StackBase, *ol = tib->StackLimit; struct _EXCEPTION_REGISTRATION_RECORD *oe = tib->ExceptionList;
    tib->StackBase = (void *)(uintptr_t)MAIN_STACK_HI; tib->StackLimit = (void *)(uintptr_t)(lo + STACK_GUARD);
    tib->ExceptionList = (void *)(uintptr_t)0xffffffffu;
    static const uint16_t cw = 0x027f;
    uint32_t r;
    __asm__ volatile(
        "fninit\n\t"
        "fldcw %3\n\t"
        "movl %%esp, %%esi\n\t"
        "movl %2, %%esp\n\t"
        "call *%1\n\t"
        "movl %%esi, %%esp\n\t"
        : "=a"(r) : "r"(fn), "r"(top - 64), "m"(cw) : "esi", "ecx", "edx", "memory", "cc");
    tib->StackBase = ob; tib->StackLimit = ol; tib->ExceptionList = oe;
    return r;
}

/* ---- virtual time ---- */
static uint32_t g_vclock, g_calls_since_frame, g_creep_total;
uint32_t rh_vnow(void) { return g_vclock; }
void rh_clock_save(uint32_t s[3]) { s[0] = g_vclock; s[1] = g_calls_since_frame; s[2] = g_creep_total; }
void rh_clock_restore(const uint32_t s[3]) { g_vclock = s[0]; g_calls_since_frame = s[1]; g_creep_total = s[2]; }
static DWORD vread(void)
{
    if (++g_calls_since_frame > O.creep_after) { g_vclock++; g_creep_total++; }
    return g_vclock;
}
static DWORD WINAPI my_timeGetTime(void) { return vread(); }
static DWORD WINAPI my_GetTickCount(void) { return vread(); }
static void WINAPI my_GetLocalTime(SYSTEMTIME *t)
{
    uint32_t s = g_vclock / 1000u;
    memset(t, 0, sizeof *t); t->wYear = 1997; t->wMonth = 3; t->wDay = 14; t->wDayOfWeek = 5;
    t->wHour = (WORD)(12 + (s / 3600) % 12); t->wMinute = (WORD)((s / 60) % 60); t->wSecond = (WORD)(s % 60);
    t->wMilliseconds = (WORD)(g_vclock % 1000u);
}
static void WINAPI my_GetSystemTime(SYSTEMTIME *t) { my_GetLocalTime(t); }
static DWORD WINAPI my_GetTimeZoneInformation(TIME_ZONE_INFORMATION *z) { memset(z, 0, sizeof *z); return TIME_ZONE_ID_UNKNOWN; }
static void WINAPI my_Sleep(DWORD ms) { g_vclock += ms; if (O.realtime && ms) Sleep(ms > 50 ? 50 : ms); }

/* ---- process identity the CRT copies into retail memory ---- */
static char *g_cmdline, *g_envA;
static WCHAR *g_envW;
static LPSTR WINAPI my_GetCommandLineA(void) { return g_cmdline; }
static LPCH WINAPI my_GetEnvironmentStrings(void) { return g_envA; }
static LPWCH WINAPI my_GetEnvironmentStringsW(void) { return g_envW; }
static BOOL WINAPI my_FreeEnvironmentStringsA(LPCH p) { (void)p; return TRUE; }
static BOOL WINAPI my_FreeEnvironmentStringsW(LPWCH p) { (void)p; return TRUE; }
static VOID WINAPI my_GetStartupInfoA(LPSTARTUPINFOA si) { memset(si, 0, sizeof *si); si->cb = sizeof *si; si->wShowWindow = SW_SHOWNORMAL; }
static HANDLE WINAPI my_GetStdHandle(DWORD n) { (void)n; return INVALID_HANDLE_VALUE; }
static DWORD WINAPI my_GetVersion(void) { return 0x0a280105u; }          /* Windows XP (5.1.2600): an NT CRT path, as on Win11 */
static UINT WINAPI my_GetACP(void) { return 1252; }
static UINT WINAPI my_GetOEMCP(void) { return 437; }
static HMODULE WINAPI my_GetModuleHandleA(LPCSTR n) { return n ? GetModuleHandleA(n) : (HMODULE)0x400000; }
static DWORD WINAPI my_GetModuleFileNameA(HMODULE h, LPSTR buf, DWORD n)
{
    if (!h || h == (HMODULE)0x400000) { snprintf(buf, n, "%s\\Lords2.exe", g_gamedir_abs); return (DWORD)strlen(buf); }
    return GetModuleFileNameA(h, buf, n);
}
static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI my_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f) { SetUnhandledExceptionFilter(f); return NULL; }
static void WINAPI my_GlobalMemoryStatus(LPMEMORYSTATUS m)
{
    memset(m, 0, sizeof *m); m->dwLength = sizeof *m; m->dwMemoryLoad = 25;
    m->dwTotalPhys = 64u << 20; m->dwAvailPhys = 48u << 20; m->dwTotalPageFile = 128u << 20; m->dwAvailPageFile = 96u << 20;
    m->dwTotalVirtual = 2047u << 20; m->dwAvailVirtual = 1900u << 20;
}
static BOOL WINAPI my_SetConsoleCtrlHandler(PHANDLER_ROUTINE h, BOOL a) { (void)h; (void)a; return TRUE; }
static void WINAPI my_OutputDebugStringA(LPCSTR s) { rh_log("retail OutputDebugString: %s\n", s ? s : ""); }
static void WINAPI my_DebugBreak(void) { rh_fatal("retail DebugBreak at frame %u", g_frame); }
static HMODULE WINAPI my_LoadLibraryA(LPCSTR n) { rh_log("retail LoadLibraryA(%s) at frame %u\n", n ? n : "", g_frame); return LoadLibraryA(n); }

/* ---- imports rhost supplies instead of a DLL ---- */
static HRESULT WINAPI dp_Create(void *a, void *b, void *c) { (void)a; (void)b; (void)c; rh_log("DPLAYX DirectPlayCreate: unavailable (f%u)\n", g_frame); return 0x88770082; /* DPERR_UNAVAILABLE */ }
static HRESULT WINAPI dp_Enumerate(void *a, void *b) { (void)a; (void)b; rh_log("DPLAYX DirectPlayEnumerateA: none (f%u)\n", g_frame); return 0; }
/* Smacker, movies off: SmackOpen fails — retail logs "ERR" and skips the movie (FUN_0042da18) */
static void *WINAPI sm_Open(const char *n, uint32_t f, int32_t b) { (void)f; (void)b; rh_log("movie: SmackOpen(%s) skipped (f%u)\n", n ? n : "", g_frame); return NULL; }
static uint32_t WINAPI sm_1(void *a) { (void)a; return 0; }
static uint32_t WINAPI sm_2(void *a, uint32_t b) { (void)a; (void)b; return 0; }
static uint32_t WINAPI sm_ToBuffer(void *a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, void *f, uint32_t g)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return 0; }
void *rh_import_policy(const char *dll, const char *name, int ord, int *skip)
{
    if (!_stricmp(dll, "DDRAW.dll")) { *skip = 1; return name && !strcmp(name, "DirectDrawCreate") ? (void *)rh_DirectDrawCreate : NULL; }
    if (!_stricmp(dll, "DSOUND.dll")) { *skip = 1; return name && !strcmp(name, "DirectSoundCreate") ? (void *)rh_DirectSoundCreate : NULL; }
    if (!_stricmp(dll, "DPLAYX.dll")) { *skip = 1; return ord == 1 ? (void *)dp_Create : ord == 2 ? (void *)dp_Enumerate : NULL; }
    if (!_stricmp(dll, "smackw32.DLL") && !O.movies) {
        *skip = 1;
        switch (ord) {
        case 14: return (void *)sm_Open;          /* SmackOpen@12 */
        case 17: case 27: case 28: return (void *)sm_2;   /* SoundOnOff@8, Goto@8, ToBufferRect@8 */
        case 18: case 19: case 21: case 32: case 37: return (void *)sm_1;   /* Close, DoFrame, NextFrame, Wait, SetDirectSoundHWND (@4) */
        case 23: return (void *)sm_ToBuffer;      /* SmackToBuffer@28 */
        default: return NULL;
        }
    }
    return NULL;
}

/* ---- coverage: one-shot INT3 at listed function entries ---- */
static uint32_t *g_cov_va; static uint8_t *g_cov_orig; static uint32_t *g_cov_hit; static int g_cov_n;
static int cov_find(uint32_t va)
{ int lo = 0, hi = g_cov_n - 1; while (lo <= hi) { int m = (lo + hi) / 2; if (g_cov_va[m] == va) return m; if (g_cov_va[m] < va) lo = m + 1; else hi = m - 1; } return -1; }
static LONG CALLBACK cov_veh(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t a = (uint32_t)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    int i = cov_find(a);
    if (i < 0) return EXCEPTION_CONTINUE_SEARCH;
    *(volatile uint8_t *)(uintptr_t)a = g_cov_orig[i];
    if (!g_cov_hit[i]) g_cov_hit[i] = g_frame + 1;
    e->ContextRecord->Eip = a;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static int cmpu(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static void cov_install(void)
{
    FILE *f = fopen(O.coverage, "r"); if (!f) rh_fatal("cannot read %s", O.coverage);
    unsigned v; int cap = 4096; g_cov_va = malloc(cap * 4);
    while (fscanf(f, "%x", &v) == 1) {
        if (v < RT_TEXT_LO || v >= RT_TEXT_LO + RT_TEXT_SZ) continue;
        if (g_cov_n == cap) { cap *= 2; g_cov_va = realloc(g_cov_va, cap * 4); }
        g_cov_va[g_cov_n++] = v;
    }
    fclose(f);
    qsort(g_cov_va, g_cov_n, 4, cmpu);
    g_cov_orig = malloc(g_cov_n); g_cov_hit = calloc(g_cov_n, 4);
    AddVectoredExceptionHandler(1, cov_veh);
    for (int i = 0; i < g_cov_n; i++) { g_cov_orig[i] = *(uint8_t *)(uintptr_t)g_cov_va[i]; *(uint8_t *)(uintptr_t)g_cov_va[i] = 0xCC; }
    rh_log("coverage: %d entries armed\n", g_cov_n);
}
static void cov_dump(void)
{
    if (!g_cov_n) return;
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\coverage.txt", O.rundir);
    FILE *f = fopen(p, "w"); if (!f) return;
    int hits = 0;
    for (int i = 0; i < g_cov_n; i++) if (g_cov_hit[i]) { fprintf(f, "%08x %u\n", g_cov_va[i], g_cov_hit[i] - 1); hits++; }
    fclose(f);
    rh_log("coverage: %d/%d hit\n", hits, g_cov_n);
}

/* ---- --trap VA[,VA]: registers + stack args at an instruction (INT3, re-armed by single-step) ---- */
static uint32_t g_tva[8]; static int g_ntrap; static uint8_t g_torig[8]; static long g_thits[8]; static int g_trearm = -1;
static LONG CALLBACK trap_veh(EXCEPTION_POINTERS *e)
{
    CONTEXT *c = e->ContextRecord;
    uint32_t a = (uint32_t)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
        for (int i = 0; i < g_ntrap; i++) if (g_tva[i] == a) {
            if (g_thits[i]++ < (long)O.trap_max) {
                const uint32_t *sp = (const uint32_t *)(uintptr_t)c->Esp;
                rh_log("trap %08x f%u ret=%08x eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx"
                       " args %08x %08x %08x %08x %08x %08x\n", a, g_frame, sp[0], c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi,
                       c->Ebp, c->Esp, sp[1], sp[2], sp[3], sp[4], sp[5], sp[6]);
            }
            *(volatile uint8_t *)(uintptr_t)a = g_torig[i];
            c->Eip = a; c->EFlags |= 0x100; g_trearm = i;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && g_trearm >= 0 && !(c->Dr6 & 0xf)) {
        *(volatile uint8_t *)(uintptr_t)g_tva[g_trearm] = 0xCC; g_trearm = -1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- --watch-write VA[@FRAME]: hardware write watchpoints (DR0..DR3) on the main thread ---- */
static uint32_t g_wva[4], g_wfr[4], g_wlast[4]; static int g_nw, g_warmed; static long g_whits[4];
#define WATCH_ARM_CODE 0xE0551001u
static void stack_chain(char *b, size_t n, uint32_t esp)
{
    int k = 0; size_t o = 0; b[0] = 0;
    for (int i = 0; i < 512 && k < 6; i++) {
        uint32_t v = ((uint32_t *)(uintptr_t)esp)[i];
        if (v >= RT_TEXT_LO && v < RT_TEXT_LO + RT_TEXT_SZ) { o += snprintf(b + o, n - o, "%s%x", k ? "<" : "", v); k++; }
    }
}
static LONG CALLBACK watch_veh(EXCEPTION_POINTERS *e)
{
    CONTEXT *c = e->ContextRecord;
    if (e->ExceptionRecord->ExceptionCode == WATCH_ARM_CODE) {
        uint32_t dr7 = 0;
        for (int i = 0; i < g_nw; i++) if (g_warmed & (1 << i)) {
            (&c->Dr0)[i] = g_wva[i];
            dr7 |= (1u << (2 * i)) | (1u << (16 + 4 * i)) | (3u << (18 + 4 * i));
        }
        c->Dr7 = dr7;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t dr6 = c->Dr6; int hit = 0;
    for (int i = 0; i < g_nw; i++) if (dr6 & (1u << i)) {
        hit = 1;
        uint32_t nv = G32(g_wva[i]);
        int changed = nv != g_wlast[i]; g_wlast[i] = nv;
        if (g_whits[i]++ < 40 || (changed && g_whits[i] < 1000000)) {
            char st[128]; stack_chain(st, sizeof st, c->Esp);
            rh_log("watch %08x f%u eip=%08lx val=%08x [%s]\n", g_wva[i], g_frame, c->Eip, nv, st);
        }
    }
    if (!hit) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void watch_arm_due(void)
{
    int arm = 0;
    for (int i = 0; i < g_nw; i++) if (!(g_warmed & (1 << i)) && g_wfr[i] <= g_frame) { g_warmed |= 1 << i; arm = 1; }
    if (arm) { RaiseException(WATCH_ARM_CODE, 0, 0, NULL); rh_log("watch: armed at frame %u\n", g_frame); }
}
static LONG CALLBACK crash_veh(EXCEPTION_POINTERS *e)
{
    static int n;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || n++ >= 8) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
    char b[160]; int o = 0; uint32_t *fp = (uint32_t *)(uintptr_t)c->Ebp;
    uintptr_t lo = c->Esp, hi = c->Esp + 0x100000;
    for (int i = 0; i < 8 && (uintptr_t)fp >= lo && (uintptr_t)fp + 8 <= hi && !((uintptr_t)fp & 3); i++, fp = (uint32_t *)(uintptr_t)fp[0])
        o += snprintf(b + o, sizeof b - o, " %08x", fp[1]);
    rh_log("AV f%u at %08x (%s %08lx) eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx esp=%08lx ebp=%08lx callers%s\n",
           g_frame, (unsigned)(uintptr_t)e->ExceptionRecord->ExceptionAddress,
           e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read", (unsigned long)e->ExceptionRecord->ExceptionInformation[1],
           c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Esp, c->Ebp, b);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- the frame ---- */
static void finish(const char *why);
void rh_finish(const char *why) { finish(why); }
static LARGE_INTEGER g_t0, g_qf;
static uint64_t g_fb_chain = 0xcbf29ce484222325ull; static uint32_t g_fb_n;
static int is_listed(const uint32_t *a, int n, uint32_t f) { for (int i = 0; i < n; i++) if (a[i] == f) return 1; return 0; }
static void watch_str(char *wb, size_t n)
{
    size_t wo = 0; wb[0] = 0;
    for (int i = 0; i < O.n_watch; i++) wo += snprintf(wb + wo, n - wo, " %x=%d", O.watch[i], (int)G32(O.watch[i]));
}
/* end of frame k (= g_frame): what it presented, the state it left */
static void end_of_frame(void)
{
    char t[32]; snprintf(t, sizeof t, "f%06u", g_frame);
    if (is_listed(O.dump_at, O.n_dump, g_frame)) { char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\state_%s.rhs", O.rundir, t); rh_state_dump(p); }
    if (is_listed(O.shot_at, O.n_shot, g_frame) || (O.shot_every && g_frame % O.shot_every == 0)) rh_shot(t);
    if (O.fb_hash) { uint64_t fh; if (rh_fb_hash(&fh)) { g_fb_chain = (g_fb_chain ^ fh) * 0x100000001b3ull; g_fb_n++; } }
    rh_cap_frame();
    if (O.log_every && g_frame % O.log_every == 0) {
        uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
        char wb[256]; watch_str(wb, sizeof wb);
        rh_log("frame %u tick=%u vclock=%u screen=%02x state=%u rng=%08x/%08x cw=%04x creep=%u%s\n", g_frame, G32(L2_SIM_TICK), g_vclock,
               G8(L2_SCREEN), G32(L2_ENGINE_STATE), G32(L2_RNG_A), G32(L2_RNG_B), cw, g_creep_total, wb);
    }
    if (O.hash_every && g_frame % O.hash_every == 0) {
        rh_hash_t h; rh_state_hash(&h);
        rh_log("hash f%u full=%016llx sem=%016llx masked=%u fb=%016llx\n", g_frame, (unsigned long long)h.full,
               (unsigned long long)h.semantic, h.masked, (unsigned long long)g_fb_chain);
    }
}
typedef void (__cdecl *frame_fn_t)(void);
static void __cdecl frame_wrap(void)
{
    if (g_finished) return;
    if (g_nw) watch_arm_due();
    ((frame_fn_t)(uintptr_t)L2_FRAME_FN)();
    end_of_frame();
    if (g_frame + 1 >= O.frames) { g_frame++; finish("frames"); }
    g_frame++;
    g_vclock += O.step_ms; g_calls_since_frame = 0;
    rh_mm_frame();
    rh_ds_mix();
    rh_win_frame();
    rh_bot_frame();
    if (O.realtime) {
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        double due = (double)g_frame * O.step_ms, now = (double)(q.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_qf.QuadPart;
        if (due > now + 1) Sleep((DWORD)(due - now));
    }
}

static void finish(const char *why)
{
    if (g_finished) return;
    g_finished = 1;
    rh_trace_finish();
    rh_cap_finish();
    cov_dump();
    rh_hooks_report();
    for (int i = 0; i < g_ntrap; i++) rh_log("trap-count %08x hits=%ld\n", g_tva[i], g_thits[i]);
    rh_dd_report();
    rh_ds_report();
    if (is_listed(O.dump_at, O.n_dump, 0xffffffffu)) { char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\state_end.rhs", O.rundir); rh_state_dump(p); }
    if (is_listed(O.shot_at, O.n_shot, 0xffffffffu)) rh_shot("end");
    char hs[256]; rh_heap_stats(hs, sizeof hs);
    rh_log("%s handles=%u/%ld\n", hs, rh_handles_issued(), rh_handles_live());
    rh_hash_t h; rh_state_hash(&h);
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    double ms = (double)(q.QuadPart - g_t0.QuadPart) * 1000.0 / (double)g_qf.QuadPart;
    char fb[64] = "";
    if (O.fb_hash) snprintf(fb, sizeof fb, " fb=%016llx fb_frames=%u", (unsigned long long)g_fb_chain, g_fb_n);
    rh_log("RESULT status=%s reason=%s frame=%u tick=%u screen=%02x state=%u rng=%08x/%08x state_full=%016llx state_sem=%016llx masked=%u state_bytes=%u wall_ms=%.0f fps=%.0f creep=%u %s%s\n",
           !strcmp(why, "game-exit") ? "exit" : "ok", why, g_frame, G32(L2_SIM_TICK), G8(L2_SCREEN), G32(L2_ENGINE_STATE),
           G32(L2_RNG_A), G32(L2_RNG_B), (unsigned long long)h.full, (unsigned long long)h.semantic, h.masked, h.bytes, ms,
           ms > 0 ? g_frame * 1000.0 / ms : 0.0, g_creep_total, rh_trace_status(), fb);
    if (rh_bot_poked()) rh_log("RESULT-NOTE poked=%u (memory writes over the control channel: not a pure replay)\n", rh_bot_poked());
    if (g_logf) fclose(g_logf);
    ExitProcess(0);
}
static VOID WINAPI my_ExitProcess(UINT code) { if (!g_finished) { rh_log("retail ExitProcess(%u) at frame %u\n", code, g_frame); finish("game-exit"); } ExitProcess(code); }
static BOOL WINAPI my_TerminateProcess(HANDLE p, UINT code)
{
    if (p == GetCurrentProcess() || p == (HANDLE)(intptr_t)-1) my_ExitProcess(code);
    return TerminateProcess(p, code);
}

/* ---- CLI ---- */
static void usage(void)
{
    fprintf(stderr, "usage: rhost --exe Lords2.exe --game DIR --run DIR [--frames N] [--trace F] [--record F]\n"
                    "  [--dump-at F,..|end] [--shot-at F,..|end] [--shot-every N] [--fb-hash] [--hash-every N] [--log-every N]\n"
                    "  [--tail N] [--step-ms N] [--vbase MS] [--creep-after N] [--show] [--scale N] [--realtime] [--movies]\n"
                    "  [--hook SPEC] [--coverage F] [--watch VA,..] [--trap VA,..] [--watch-write VA[@F],..] [--bot HOST:PORT] [--out-trace F]\n"
                    "  [--log-files] [--log-heap] [--log-input] [--log-dd] [--log-ds] [--log-win]\n");
    ExitProcess(2);
}
static void parse_list(const char *s, uint32_t *out, int *n, int max)
{
    char b[1024]; snprintf(b, sizeof b, "%s", s);
    for (char *t = strtok(b, ","); t && *n < max; t = strtok(NULL, ","))
        out[(*n)++] = !strcmp(t, "end") ? 0xffffffffu : strtoul(t, 0, 0);
}
static void parse(int argc, char **argv)
{
    O.tail = 60; O.log_every = 600; O.step_ms = 17; O.creep_after = 2000; O.vbase = 1000000;
    O.stall_wall_ms = 60000; O.trap_max = 50; O.scale = 2;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define ARG() (i + 1 < argc ? argv[++i] : (usage(), (char *)0))
        if (!strcmp(a, "--exe")) O.exe = ARG();
        else if (!strcmp(a, "--game")) O.gamedir = ARG();
        else if (!strcmp(a, "--run")) O.rundir = ARG();
        else if (!strcmp(a, "--frames")) O.frames = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--trace")) O.trace = ARG();
        else if (!strcmp(a, "--record")) { O.record = ARG(); O.realtime = 1; O.show = 1; O.audible = 1; }
        else if (!strcmp(a, "--audible")) O.audible = 1;
        else if (!strcmp(a, "--silent")) O.audible = -1;
        else if (!strcmp(a, "--out-trace")) O.out_trace = ARG();
        else if (!strcmp(a, "--dump-at")) parse_list(ARG(), O.dump_at, &O.n_dump, 128);
        else if (!strcmp(a, "--shot-at")) parse_list(ARG(), O.shot_at, &O.n_shot, 64);
        else if (!strcmp(a, "--shot-every")) O.shot_every = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--fb-hash")) O.fb_hash = 1;
        else if (!strcmp(a, "--cap")) { const char *v = ARG(); O.cap_from = strtoul(v, 0, 0); const char *c = strchr(v, ':'); O.cap_count = c ? strtoul(c + 1, 0, 0) : 1; }
        else if (!strcmp(a, "--hash-every")) O.hash_every = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--log-every")) O.log_every = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--tail")) O.tail = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--step-ms")) O.step_ms = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--vbase")) O.vbase = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--creep-after")) O.creep_after = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--stall-wall")) O.stall_wall_ms = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--show")) O.show = 1;
        else if (!strcmp(a, "--scale")) O.scale = (int)strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--realtime")) O.realtime = 1;
        else if (!strcmp(a, "--movies")) O.movies = 1;
        else if (!strcmp(a, "--hook")) O.hooks = ARG();
        else if (!strcmp(a, "--coverage")) O.coverage = ARG();
        else if (!strcmp(a, "--bot")) O.bot = ARG();
        else if (!strcmp(a, "--no-arena")) O.no_arena = 1;
        else if (!strcmp(a, "--no-overlay")) O.no_overlay = 1;
        else if (!strcmp(a, "--log-files")) O.log_files = 1;
        else if (!strcmp(a, "--log-heap")) O.log_heap = 1;
        else if (!strcmp(a, "--log-input")) O.log_input = 1;
        else if (!strcmp(a, "--log-dd")) O.log_dd = 1;
        else if (!strcmp(a, "--log-ds")) O.log_ds = 1;
        else if (!strcmp(a, "--log-win")) O.log_win = 1;
        else if (!strcmp(a, "--trap-max")) O.trap_max = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--heap-trace")) { const char *v = ARG(); O.htrace_f0 = strtoul(v, 0, 0); const char *d = strchr(v, '-'); O.htrace_f1 = d ? strtoul(d + 1, 0, 0) : O.htrace_f0; }
        else if (!strcmp(a, "--trap")) {
            char b[256]; snprintf(b, sizeof b, "%s", ARG());
            for (char *t = strtok(b, ","); t && g_ntrap < 8; t = strtok(NULL, ",")) g_tva[g_ntrap++] = strtoul(t, 0, 16);
        }
        else if (!strcmp(a, "--watch-write")) {
            char b[256]; snprintf(b, sizeof b, "%s", ARG());
            for (char *t = strtok(b, ","); t && g_nw < 4; t = strtok(NULL, ",")) {
                char *at = strchr(t, '@'); g_wfr[g_nw] = at ? strtoul(at + 1, 0, 0) : 0; if (at) *at = 0;
                g_wva[g_nw++] = strtoul(t, 0, 16) & ~3u;
            }
        }
        else if (!strcmp(a, "--watch")) {
            char b[256]; snprintf(b, sizeof b, "%s", ARG());
            for (char *t = strtok(b, ","); t && O.n_watch < 16; t = strtok(NULL, ",")) O.watch[O.n_watch++] = strtoul(t, 0, 16);
        }
        else { fprintf(stderr, "unknown option %s\n", a); usage(); }
        #undef ARG
    }
    if (!O.exe || !O.gamedir || !O.rundir) usage();
    if (O.scale < 1) O.scale = 1;
    if (O.audible < 0) O.audible = 0;
    static char rec_abs[MAX_PATH], out_abs[MAX_PATH];
    if (O.record && !strchr(O.record, ':') && O.record[0] != '\\') { snprintf(rec_abs, sizeof rec_abs, "%s\\%s", O.rundir, O.record); O.record = rec_abs; }
    if (O.record && !O.out_trace) O.out_trace = O.record;
    if (O.out_trace && !strchr(O.out_trace, ':') && O.out_trace[0] != '\\') { snprintf(out_abs, sizeof out_abs, "%s\\%s", O.rundir, O.out_trace); O.out_trace = out_abs; }
}

static void load_volatile(void)
{
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\volatile.txt", O.rundir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[256]; unsigned v; int n = 0;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "%x", &v) == 1) { rh_state_volatile_add(v); n++; }
    fclose(f);
    rh_log("volatile: %d masked addresses\n", n);
}
static void layout_from_pe(void)
{
    IMAGE_NT_HEADERS32 *nt = (void *)(0x400000 + ((IMAGE_DOS_HEADER *)0x400000)->e_lfanew);
    RT_IMG_HI = 0x400000u + nt->OptionalHeader.SizeOfImage;
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t lo = 0x400000u + s[i].VirtualAddress;
        if (!memcmp(s[i].Name, ".text", 6)) {   /* VirtualSize, or the raw size if bigger: the GOG patch's code lies past it */
            RT_TEXT_LO = lo; RT_TEXT_SZ = s[i].Misc.VirtualSize;
            if (s[i].SizeOfRawData > RT_TEXT_SZ && s[i].SizeOfRawData <= ((RT_TEXT_SZ + 0xfffu) & ~0xfffu)) RT_TEXT_SZ = s[i].SizeOfRawData;
        }
        if (!memcmp(s[i].Name, ".data", 6)) { RT_DATA_LO = lo; RT_DATA_HI = (lo + s[i].Misc.VirtualSize + 3u) & ~3u; }
    }
    if (nt->OptionalHeader.SizeOfImage != L2_IMAGE_SIZE || 0x400000u + nt->OptionalHeader.AddressOfEntryPoint != L2_ENTRY)
        rh_fatal("not Lords2.exe GOG 2.0.0.3 (SizeOfImage %08x entry %08x)", nt->OptionalHeader.SizeOfImage,
                 0x400000u + nt->OptionalHeader.AddressOfEntryPoint);
}
static DWORD WINAPI watchdog(LPVOID p)
{
    (void)p;
    uint32_t last = 0xffffffffu; DWORD since = GetTickCount();
    for (;;) {
        Sleep(500);
        if (g_finished) return 0;
        if (g_frame != last) { last = g_frame; since = GetTickCount(); continue; }
        if (GetTickCount() - since > O.stall_wall_ms) {
            rh_log("STALL: no frame for %u ms wall at frame %u\nRESULT status=stalled reason=wall frame=%u tick=%u screen=%02x\n",
                   O.stall_wall_ms, g_frame, g_frame, G32(L2_SIM_TICK), G8(L2_SCREEN));
            if (g_logf) fflush(g_logf);
            ExitProcess(4);
        }
    }
}

RETAIL_HOLE_DEFINE

int main(int argc, char **argv)
{
    parse(argc, argv);
    QueryPerformanceFrequency(&g_qf); QueryPerformanceCounter(&g_t0);
    CreateDirectoryA(O.rundir, NULL);
    char lp[MAX_PATH]; snprintf(lp, sizeof lp, "%s\\rhost.log", O.rundir);
    g_logf = fopen(lp, "w");
    g_log = stdout;
    g_vclock = O.vbase;
    if (!O.no_arena && rh_heap_init()) rh_fatal("heap arena reservation failed");
    if (rh_media_reserve()) rh_fatal("cannot reserve the media pool");
    stacks_init();
    GetFullPathNameA(O.gamedir, sizeof g_gamedir_abs, g_gamedir_abs, NULL);
    for (size_t l = strlen(g_gamedir_abs); l && g_gamedir_abs[l - 1] == '\\'; ) g_gamedir_abs[--l] = 0;
    char exe_abs[MAX_PATH]; GetFullPathNameA(O.exe, sizeof exe_abs, exe_abs, NULL);
    if (!SetCurrentDirectoryA(g_gamedir_abs)) rh_fatal("cannot cd to %s", g_gamedir_abs);
    retail_image_t img;
    if (retail_map(exe_abs, &img)) rh_fatal("map %s failed", exe_abs);
    layout_from_pe();
    rh_log("rhost: mapped %s (imports: %d real, %d rhost, %d unresolved) entry=%08x text=%08x+%x data=%08x..%08x\n",
           exe_abs, img.n_imports_resolved, img.n_imports_host, img.n_imports_stubbed, img.entry, RT_TEXT_LO, RT_TEXT_SZ, RT_DATA_LO, RT_DATA_HI);
    iat_override("GetModuleHandleA", my_GetModuleHandleA);
    iat_override("GetModuleFileNameA", my_GetModuleFileNameA);
    iat_override("SetUnhandledExceptionFilter", my_SetUnhandledExceptionFilter);
    iat_override("ExitProcess", my_ExitProcess);
    iat_override("TerminateProcess", my_TerminateProcess);
    g_cmdline = rh_fixed_alloc(MAX_PATH + 8);
    snprintf(g_cmdline, MAX_PATH + 8, "\"%s\\Lords2.exe\"", g_gamedir_abs);
    static const char envA[] = "SystemRoot=C:\\Windows\0\0";
    static const WCHAR envW[] = L"SystemRoot=C:\\Windows\0\0";
    g_envA = rh_fixed_alloc(sizeof envA); memcpy(g_envA, envA, sizeof envA);
    g_envW = rh_fixed_alloc(sizeof envW); memcpy(g_envW, envW, sizeof envW);
    iat_override("GetCommandLineA", my_GetCommandLineA);
    iat_try("GetEnvironmentStrings", my_GetEnvironmentStrings);
    iat_try("GetEnvironmentStringsW", my_GetEnvironmentStringsW);
    iat_try("FreeEnvironmentStringsA", my_FreeEnvironmentStringsA);
    iat_try("FreeEnvironmentStringsW", my_FreeEnvironmentStringsW);
    iat_override("GetStartupInfoA", my_GetStartupInfoA);
    iat_override("GetStdHandle", my_GetStdHandle);
    iat_override("GetVersion", my_GetVersion);
    iat_override("GetACP", my_GetACP);
    iat_override("GetOEMCP", my_GetOEMCP);
    iat_override("GlobalMemoryStatus", my_GlobalMemoryStatus);
    iat_override("SetConsoleCtrlHandler", my_SetConsoleCtrlHandler);
    iat_override("OutputDebugStringA", my_OutputDebugStringA);
    iat_override("DebugBreak", my_DebugBreak);
    iat_override("LoadLibraryA", my_LoadLibraryA);
    iat_override("timeGetTime", my_timeGetTime);
    iat_override("GetTickCount", my_GetTickCount);
    iat_override("GetLocalTime", my_GetLocalTime);
    iat_override("GetSystemTime", my_GetSystemTime);
    iat_override("GetTimeZoneInformation", my_GetTimeZoneInformation);
    iat_override("Sleep", my_Sleep);
    if (!O.no_arena) rh_heap_install();
    if (!O.no_overlay) rh_files_install();
    rh_handles_install();                    /* after files: wraps their overrides */
    rh_mm_install();
    rh_win_install();
    int nf = redirect_calls(L2_FRAME_FN, (void *)frame_wrap);
    if (nf < 1) rh_fatal("frame driver %08x: no call sites", L2_FRAME_FN);
    rh_log("hooks: frame driver %08x <- %d call site(s)\n", L2_FRAME_FN, nf);
    load_volatile();
    rh_hooks_install();
    if (O.coverage) cov_install();
    AddVectoredExceptionHandler(0, crash_veh);
    if (g_nw) AddVectoredExceptionHandler(1, watch_veh);
    if (g_ntrap) {
        AddVectoredExceptionHandler(1, trap_veh);
        for (int i = 0; i < g_ntrap; i++) { g_torig[i] = G8(g_tva[i]); G8(g_tva[i]) = 0xCC; }
    }
    if (!O.frames) O.frames = (O.record || O.bot) ? 0xfffffff0u : O.trace ? rh_trace_last_frame() + O.tail : 3000u;
    rh_log("rhost: frames<=%u step=%ums vbase=%u creep_after=%u realtime=%d show=%d movies=%d arena=%d overlay=%d\n",
           O.frames, O.step_ms, O.vbase, O.creep_after, O.realtime, O.show, O.movies, !O.no_arena, !O.no_overlay);
    rh_bot_install();                        /* bot mode: serves the controller until its first `run` */
    if (!O.record && !O.show && !O.bot && O.stall_wall_ms) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    call_on_stack((void *)(uintptr_t)img.entry, MAIN_STACK_LO, MAIN_STACK_HI - 192);   /* CRT entry: never returns */
    finish("entry-returned");
    return 0;
}
