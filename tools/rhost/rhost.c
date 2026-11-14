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
 *   movies  SmackOpen fails (retail logs and skips the movie) unless --movies: then smackw32 is
 *           mapped by rhost at a fixed base with its imports bound to these layers (rh_smack.c).
 *   x87     control word 0x027F at entry (what Windows gives a thread; mingw's CRT sets 0x037F).
 * Output: <rundir>\rhost.log; last line "RESULT key=value ..." (parsed by rhost.py). */
#include "rhost.h"
#include "rl_main.h"
#include "rl_io_plat.h"
void rh_port_bind_once(void);   /* rh_hooks_ai.c */

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
#include "rh_callsites.h"   /* build dir: tools/rhost/gen_callsites.py */
static int is_callsite(uint32_t a)
{
    unsigned lo = 0, hi = k_callsites_n;
    while (lo < hi) { unsigned m = (lo + hi) / 2; if (k_callsites[m] < a) lo = m + 1; else hi = m; }
    return lo < k_callsites_n && k_callsites[lo] == a;
}
/* a function whose address retail takes (a pointer table, a stored callback): its call sites alone miss those calls */
int rh_is_addr_taken(uint32_t va)
{
    unsigned lo = 0, hi = k_addrtaken_n;
    while (lo < hi) { unsigned m = (lo + hi) / 2; if (k_addrtaken[m] < va) lo = m + 1; else hi = m; }
    return lo < k_addrtaken_n && k_addrtaken[lo] == va;
}
/* redirect every E8 call of `va` to `hook`: only at real call instructions (the IDA export's list) — an E8 byte inside
 * another instruction (an immediate, a displacement) aimed at va by chance is left alone (it crashed retail once:
 * 0x47f421 inside FUN_0047efee's `mov dword_4EE8E8, 0`). Without the list: the raw scan. */
int redirect_calls(uint32_t va, void *hook)
{
    int n = 0, false_hits = 0;
    for (uint8_t *p = (uint8_t *)(uintptr_t)RT_TEXT_LO, *e = p + RT_TEXT_SZ - 5; p < e; p++) {
        if (*p != 0xE8) continue;
        uint32_t tgt = (uint32_t)(uintptr_t)p + 5 + *(int32_t *)(p + 1);
        if (tgt != va) continue;
        if (k_callsites_n && !is_callsite((uint32_t)(uintptr_t)p)) { false_hits++; continue; }
        *(int32_t *)(p + 1) = (int32_t)((uint32_t)(uintptr_t)hook - ((uint32_t)(uintptr_t)p + 5));
        n++;
    }
    if (false_hits) rh_log("redirect_calls %08x: %d E8 byte(s) inside other instructions left alone\n", va, false_hits);
    return n;
}

/* entry hook: for a function no E8 call site reaches (only a pointer table — PTR_FUN_004d6a50's handlers — or a
 * host seam), its first instructions become `jmp hook`; they move to a trampoline that jumps back, and the hook's
 * retail side calls the trampoline (hook_t.entry). .text is not state: hashes do not change. Prologue
 * instructions only — an unknown opcode is fatal, never a guess; the displaced tail bytes become int3. */
static uint8_t g_tramp[0x8000] __attribute__((aligned(4096)));
static size_t g_ntramp;
static int modrm_len(const uint8_t *p)          /* ModRM (+ SIB + displacement) */
{
    int mod = p[0] >> 6, rm = p[0] & 7, n = 1;
    if (mod != 3 && rm == 4) { n++; if (mod == 0 && (p[1] & 7) == 5) n += 4; }
    if (mod == 0 && rm == 5) n += 4;
    else if (mod == 1) n += 1;
    else if (mod == 2) n += 4;
    return n;
}
static int insn_len(const uint8_t *p, int *rel)  /* *rel: offset of a rel32 to relocate, else -1 */
{
    *rel = -1;
    uint8_t o = p[0];
    if ((o >= 0x50 && o <= 0x5f) || o == 0x90) return 1;                  /* push / pop r32, nop */
    if (o == 0x6a || (o >= 0xb0 && o <= 0xb7)) return 2;                   /* push imm8, mov r8, imm8 */
    if (o == 0x68 || (o >= 0xb8 && o <= 0xbf) || o == 0xa1 || o == 0xa3) return 5;   /* imm32 / moffs32 */
    if (o == 0xe8 || o == 0xe9) { *rel = 1; return 5; }                   /* call / jmp rel32 */
    if (o == 0x0f && (p[1] == 0xb6 || p[1] == 0xb7 || p[1] == 0xbe || p[1] == 0xbf)) return 2 + modrm_len(p + 2);
    switch (o) {
    case 0x01: case 0x03: case 0x09: case 0x0b: case 0x21: case 0x23: case 0x29: case 0x2b: case 0x31: case 0x33:
    case 0x39: case 0x3b: case 0x85: case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8d: case 0xd9: case 0xdd:
        return 1 + modrm_len(p + 1);
    case 0x80: case 0x83: case 0xc6: return 2 + modrm_len(p + 1);         /* imm8 */
    case 0x81: case 0xc7: return 5 + modrm_len(p + 1);                    /* imm32 */
    case 0xf7: if (((p[1] >> 3) & 7) <= 1) return 5 + modrm_len(p + 1);   /* test r/m32, imm32 (the CRT's strlen) */
        break;
    }
    return 0;
}
uint32_t rh_patch_entry(uint32_t va, void *hook)
{
    if (!g_ntramp) { DWORD old; VirtualProtect(g_tramp, sizeof g_tramp, PAGE_EXECUTE_READWRITE, &old); }
    if (g_ntramp + 32 > sizeof g_tramp) rh_fatal("entry hook %08x: trampoline pool exhausted", va);
    uint8_t *f = (uint8_t *)(uintptr_t)va, *t = g_tramp + g_ntramp;
    int n = 0;
    while (n < 5) {
        int rel, l = insn_len(f + n, &rel);
        if (!l) rh_fatal("entry hook %08x: unknown instruction %02x %02x %02x at +%d", va, f[n], f[n + 1], f[n + 2], n);
        memcpy(t + n, f + n, l);
        if (rel >= 0) *(int32_t *)(t + n + rel) += (int32_t)((uint32_t)(uintptr_t)(f + n) - (uint32_t)(uintptr_t)(t + n));
        n += l;
    }
    t[n] = 0xe9; *(int32_t *)(t + n + 1) = (int32_t)(va + n - ((uint32_t)(uintptr_t)(t + n) + 5));
    g_ntramp += (size_t)(n + 5 + 15) & ~(size_t)15;
    f[0] = 0xe9; *(int32_t *)(f + 1) = (int32_t)((uint32_t)(uintptr_t)hook - (va + 5));
    for (int i = 5; i < n; i++) f[i] = 0xcc;
    FlushInstructionCache(GetCurrentProcess(), f, (SIZE_T)n);
    return (uint32_t)(uintptr_t)t;
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

/* a fixed reservation failed: log what holds [lo, hi) — modules by path, private / mapped allocations by base
 * (Windows places some per-process allocations at random; the log says which one got in the way) */
void rh_log_occupants(uint32_t lo, uint32_t hi)
{
    for (uint32_t a = lo; a < hi; ) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) break;
        uint32_t b = (uint32_t)(uintptr_t)mi.BaseAddress, e = b + (uint32_t)mi.RegionSize;
        if (mi.State != MEM_FREE) {
            char path[MAX_PATH] = "";
            if (mi.Type == MEM_IMAGE) GetModuleFileNameA((HMODULE)mi.AllocationBase, path, sizeof path);
            rh_log("occupant %08x..%08x alloc %08x %s %s prot %08lx %s\n", b, e, (uint32_t)(uintptr_t)mi.AllocationBase,
                   mi.State == MEM_COMMIT ? "commit" : "reserve",
                   mi.Type == MEM_IMAGE ? "image" : mi.Type == MEM_MAPPED ? "mapped" : "private", (unsigned long)mi.Protect, path);
        }
        if (e <= a) break;
        a = e;
    }
}

/* a fixed range is taken: log the occupants, then relaunch this same command (up to 3 times) and exit with the
 * child's code. Windows' compatibility engine (apphelp.dll) sometimes loads into a FRESH exe's process before
 * main, at its per-boot base — 0x67030000 this boot, inside the media pool; the next launch finds the
 * compatibility cache warm. The child rewrites rhost.log; the occupants go to stdout. */
static void fixed_range_taken(const char *what, uint32_t lo, uint32_t hi)
{
    char v[16] = "";
    GetEnvironmentVariableA("RHOST_RELAUNCH", v, sizeof v);
    int k = atoi(v);
    if (g_logf) { fclose(g_logf); g_logf = NULL; }
    rh_log_occupants(lo, hi);
    if (k < 3) {
        snprintf(v, sizeof v, "%d", k + 1);
        SetEnvironmentVariableA("RHOST_RELAUNCH", v);
        rh_log("%s: fixed range %08x..%08x taken: relaunching (%d/3)\n", what, lo, hi, k + 1);
        fflush(stdout);
        STARTUPINFOA si = { .cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES, .hStdInput = GetStdHandle(STD_INPUT_HANDLE),
                            .hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE), .hStdError = GetStdHandle(STD_ERROR_HANDLE) };
        PROCESS_INFORMATION pi; DWORD code = 1;
        if (CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &code);
            ExitProcess(code);
        }
    }
    rh_fatal("%s: fixed range %08x..%08x taken (err %lu)", what, lo, hi, GetLastError());
}

/* ---- fixed-address main stack (retail stores stack addresses; TEB bounds for SEH) ---- */
#define MAIN_STACK_LO 0x58000000u
#define MAIN_STACK_HI 0x59000000u
#define STACK_GUARD   0x10000u
static void stacks_init(void)
{
    if (VirtualAlloc((void *)(uintptr_t)MAIN_STACK_LO, MAIN_STACK_HI - MAIN_STACK_LO, MEM_RESERVE, PAGE_READWRITE) != (void *)(uintptr_t)MAIN_STACK_LO)
        fixed_range_taken("stack", MAIN_STACK_LO, MAIN_STACK_HI);
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

/* ---- the game coroutine (--live / --snap-test) ----
 * Retail runs on its fixed stack; the host (the live server, the snap-test loop) on the main thread's own. At the
 * end of frame_wrap — the same frame boundary a plain run has, at a constant stack depth (WinMain → the guard
 * 0x40e8bb → 0x40e890 → the frame driver: one call chain) — retail's side yields; rh_step() resumes it for one frame.
 * Host code never runs on the game stack, so a session leaves retail's stack residue as a plain replay does, and a
 * savestate restore can rewrite the game stack [rh_game_esp(), top) while retail is suspended. */
uint32_t g_gesp, g_hesp, g_gseh = 0xffffffffu, g_cw0 = 0x027f;
static uint32_t g_hseh, g_hbase, g_hlimit; static uint16_t g_gcw = 0x027f;
uint32_t rh_game_esp(void) { return g_gesp; }
__attribute__((naked)) static void swap_to_host(void)
{
    __asm__("pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t pushl %ebp\n\t"
            "movl %esp, _g_gesp\n\t movl _g_hesp, %esp\n\t"
            "popl %ebp\n\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t ret");
}
__attribute__((naked)) static void swap_to_game(void)
{
    __asm__("pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t pushl %ebp\n\t"
            "movl %esp, _g_hesp\n\t movl _g_gesp, %esp\n\t"
            "popl %ebp\n\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t ret");
}
static void yield_to_host(void)                       /* retail's side, end of frame_wrap */
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    __asm__ volatile("fnstcw %0" : "=m"(g_gcw));
    g_gseh = (uint32_t)(uintptr_t)tib->ExceptionList;
    tib->StackBase = (void *)(uintptr_t)g_hbase; tib->StackLimit = (void *)(uintptr_t)g_hlimit; tib->ExceptionList = (void *)(uintptr_t)g_hseh;
    swap_to_host();
    __asm__ volatile("fldcw %0" : : "m"(g_gcw));
}
void rh_step(void)                                    /* the host's side: one frame */
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    if (O.live) rh_live_frame_begin();
    g_hseh = (uint32_t)(uintptr_t)tib->ExceptionList;
    tib->StackBase = (void *)(uintptr_t)MAIN_STACK_HI; tib->StackLimit = (void *)(uintptr_t)(MAIN_STACK_LO + STACK_GUARD);
    tib->ExceptionList = (void *)(uintptr_t)g_gseh;
    uint16_t hcw; __asm__ volatile("fnstcw %0" : "=m"(hcw));
    swap_to_game();
    __asm__ volatile("fldcw %0" : : "m"(hcw));
}
uint32_t g_entry;
void __cdecl rh_game_main(void);
/* retail's entry is called from here directly, at call_on_stack's depth: an intermediate C frame shifted retail's whole
 * stack 16 bytes down, and stack pointers left as residue (0x488793 stores an uninitialized local: a dead unit's frame
 * byte) then differed from a plain replay's — siege-castles@13263, rd verify MISMATCH on one byte */
__attribute__((naked)) static void game_tramp(void) { __asm__("fninit\n\t fldcw _g_cw0\n\t call *_g_entry\n\t call _rh_game_main\n\t hlt"); }
static void coro_init(void *entry)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    g_hbase = (uint32_t)(uintptr_t)tib->StackBase; g_hlimit = (uint32_t)(uintptr_t)tib->StackLimit;
    g_entry = (uint32_t)(uintptr_t)entry;
    uint32_t sp = MAIN_STACK_HI - 192 - 64;           /* call_on_stack's entry depth */
    sp -= 4; G32(sp) = (uint32_t)(uintptr_t)game_tramp;
    for (int i = 0; i < 4; i++) { sp -= 4; G32(sp) = 0; }   /* ebx esi edi ebp */
    g_gesp = sp;
    rh_snap_register("game.esp", &g_gesp, sizeof g_gesp);
    rh_snap_register("game.seh", &g_gseh, sizeof g_gseh);
    rh_snap_register("game.cw", &g_gcw, sizeof g_gcw);
    rh_snap_register("frame", (void *)&g_frame, sizeof g_frame);
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
/* retail's CRT installs a top-level filter; an unhandled fault then reached the default handler — wine's
 * auto-debugger / Windows error reporting suspended every thread (the watchdog too) and the run hung until
 * the driver's timeout. rhost keeps its own last-chance filter (rh_crash_filter) and only records retail's. */
static LPTOP_LEVEL_EXCEPTION_FILTER g_retail_filter;
static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI my_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f)
{ LPTOP_LEVEL_EXCEPTION_FILTER old = g_retail_filter; g_retail_filter = f; return old; }
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
    if (!_stricmp(dll, "smackw32.DLL") && O.movies) { *skip = 1; return NULL; }   /* bound by rh_smack_install */
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

static LONG WINAPI rh_crash_filter(EXCEPTION_POINTERS *e)
{
    rh_log("RESULT status=crash reason=%08lx frame=%u tick=%u screen=%02x at=%08x\n", e->ExceptionRecord->ExceptionCode, g_frame,
           G32(L2_SIM_TICK), G8(L2_SCREEN), (unsigned)(uintptr_t)e->ExceptionRecord->ExceptionAddress);
    ExitProcess(5);
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
uint32_t rh_fw_frame;   /* frame_wrap's own depth: its caller's esp before the call − esp at its frame call (compiler-dependent) */
static void __cdecl frame_wrap(void)
{
    if (g_finished) return;
    if (g_nw) watch_arm_due();
    { uint32_t sp_; __asm__ volatile("movl %%esp, %0" : "=r"(sp_)); rh_fw_frame = (uint32_t)(uintptr_t)__builtin_dwarf_cfa() - sp_; }
    ((frame_fn_t)(uintptr_t)L2_FRAME_FN)();
    end_of_frame();
    if (g_frame + 1 >= O.frames && !O.finish_late) { g_frame++; finish("frames"); }
    g_frame++;
    g_vclock += O.step_ms; g_calls_since_frame = 0;
    rh_mm_frame();
    rh_smack_frame();
    rh_ds_mix();
    rh_win_frame();
    if (O.finish_late && g_frame >= O.frames) finish("frames");   /* after the timers + input of the boundary, like a live session */
    rh_bot_frame();
    if (O.live || O.snap_test) { yield_to_host(); return; }
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
    if (O.snap_test) rh_log("RESULT-SNAP%s\n", rh_snap_test_status());
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
                    "  [--hook SPEC] [--shadow-full] [--shadow-media] [--shadow-all] [--shadow-poison VA] [--shadow-poison-px X,Y] [--coverage F] [--watch VA,..] [--trap VA,..] [--watch-write VA[@F],..] [--bot HOST:PORT] [--out-trace F]\n"
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
        else if (!strcmp(a, "--standalone")) O.standalone = 1;
        else if (!strcmp(a, "--hook")) O.hooks = ARG();
        else if (!strcmp(a, "--shadow-full")) O.shadow_full = 1;
        else if (!strcmp(a, "--hook-entry")) O.hook_entry = 1;
        else if (!strcmp(a, "--shadow-poison")) O.shadow_poison = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--shadow-media")) O.shadow_media = 1;
        else if (!strcmp(a, "--shadow-all")) O.shadow_all = 1;
        else if (!strcmp(a, "--shadow-poison-px")) { const char *v = ARG(); O.poison_px = 1; O.poison_x = (int)strtol(v, 0, 0); const char *c = strchr(v, ','); O.poison_y = c ? (int)strtol(c + 1, 0, 0) : 0; }
        else if (!strcmp(a, "--coverage")) O.coverage = ARG();
        else if (!strcmp(a, "--bot")) O.bot = ARG();
        else if (!strcmp(a, "--live")) { O.live = 1; O.live_port = atoi(ARG()); }
        else if (!strcmp(a, "--live-idle")) O.live_idle = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--live-until")) O.live_until = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--snap-test")) O.snap_test = ARG();
        else if (!strcmp(a, "--finish-late")) O.finish_late = 1;
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

/* ---- --standalone: the trap for retail code, the seal, the audit (OpenSummoners origin) ---- */
static char g_audit[MAX_PATH];
static void sa_audit(const char *fmt, ...)
{
    FILE *f = fopen(g_audit, "a"); if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fclose(f);
}
static LONG CALLBACK sa_veh(EXCEPTION_POINTERS *e)
{
    uint32_t code = e->ExceptionRecord->ExceptionCode, at = (uint32_t)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    if (code == EXCEPTION_BREAKPOINT && at >= RT_TEXT_LO && at < RT_TEXT_LO + RT_TEXT_SZ) {
        uint32_t ret = *(uint32_t *)(uintptr_t)e->ContextRecord->Esp;
        rh_log("GAP retail %08x ret %08x frame %u\n", at, ret, g_frame);
        rh_fatal("standalone: the game reached retail code at %08x (no port for it yet; return address %08x) frame %u", at, ret, g_frame);
    }
    if (code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->ExceptionInformation[0] == 8 && at >= RT_IMG_LO && at < RT_IMG_HI)
        rh_fatal("standalone: execution of mapped game DATA at %08x (DEP) frame %u", at, g_frame);
    return EXCEPTION_CONTINUE_SEARCH;
}
/* sealed before the first port instruction runs: the code range (INT3 + the thunks) execute / read, everything taken
 * from the file read / write and never executable; the audit re-derives every claim from memory and from the file and
 * a failed audit refuses to run */
static void sa_seal(const char *exe_abs, const retail_image_t *img)
{
    DWORD old;
    uint32_t code_hi = (RT_TEXT_LO + RT_TEXT_SZ + 0xfffu) & ~0xfffu;
    if (!VirtualProtect((void *)(uintptr_t)RT_IMG_LO, RT_IMG_HI - RT_IMG_LO, PAGE_READWRITE, &old) ||
        !VirtualProtect((void *)(uintptr_t)RT_TEXT_LO, code_hi - RT_TEXT_LO, PAGE_EXECUTE_READ, &old))
        rh_fatal("standalone: cannot seal the image (%lu)", GetLastError());
    snprintf(g_audit, sizeof g_audit, "%s\\standalone_audit.txt", O.rundir);
    DeleteFileA(g_audit);
    sa_audit("# OpenLords2 standalone audit: every byte taken from the game's files, and the code range\n");
    sa_audit("exe %s image %08x..%08x\n", exe_abs, RT_IMG_LO, RT_IMG_HI);
    int bad = 0;
    for (int i = 0; i < img->nr; i++) {
        const retail_range_t *r = &img->r[i];
        sa_audit("range %-8s %08x..%08x ch %08x %s\n", r->name, r->va, r->va + r->len, r->ch,
                 r->copied ? "copied" : "CODE-not-copied");
        if (r->copied && RETAIL_SEC_CODE(r->ch)) { sa_audit("FAIL copied a code section %s\n", r->name); bad++; }
    }
    /* the code range: INT3 everywhere but the thunks */
    const rh_thunk_t *t; int nt_ = rh_hooks_thunks(&t);
    uint8_t *mark = VirtualAlloc(NULL, code_hi - RT_TEXT_LO, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (int i = 0; i < nt_; i++) {
        const uint8_t *e = (const uint8_t *)(uintptr_t)t[i].va;
        int32_t rel = (int32_t)((uint32_t)(uintptr_t)t[i].target - (t[i].va + 5));
        if (e[0] != 0xE9 || *(const int32_t *)(e + 1) != rel) { sa_audit("FAIL thunk %08x %s altered\n", t[i].va, t[i].name); bad++; }
        for (int k = 0; k < 5; k++) mark[t[i].va - RT_TEXT_LO + k] = 1;
        sa_audit("thunk %08x -> %08x %s\n", t[i].va, (uint32_t)(uintptr_t)t[i].target, t[i].name);
    }
    uint32_t int3 = 0, other = 0, notcopied = 0;
    for (int r = 0; r < img->nr; r++) {
        if (img->r[r].copied) continue;
        const uint8_t *code = (const uint8_t *)(uintptr_t)img->r[r].va;
        notcopied += img->r[r].len;
        for (uint32_t i = 0; i < img->r[r].len; i++) {
            uint32_t va = img->r[r].va + i;
            if (va >= RT_TEXT_LO && va < code_hi && mark[va - RT_TEXT_LO]) continue;
            if (code[i] == 0xCC) int3++;
            else if (other++ < 16) sa_audit("FAIL code byte %08x = %02x\n", va, code[i]);
        }
    }
    VirtualFree(mark, 0, MEM_RELEASE);
    bad += other != 0;
    /* against the file: no 16-byte window of retail's code (all-INT3 windows aside) is present in memory */
    uint32_t windows = 0, same = 0;
    HANDLE f = CreateFileA(exe_abs, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE m = f != INVALID_HANDLE_VALUE ? CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL) : NULL;
    const uint8_t *v = m ? MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : NULL;
    if (v) {
        IMAGE_NT_HEADERS32 *fn = (void *)(v + ((IMAGE_DOS_HEADER *)v)->e_lfanew);
        IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(fn);
        for (int i = 0; i < fn->FileHeader.NumberOfSections; i++) {
            if (!RETAIL_SEC_CODE(sec[i].Characteristics)) continue;
            const uint8_t *ft = v + sec[i].PointerToRawData, *mt = (const uint8_t *)(uintptr_t)(RT_IMG_LO + sec[i].VirtualAddress);
            for (uint32_t o = 0; o + 16 <= sec[i].SizeOfRawData; o += 16) {
                int all_cc = 1;
                for (int k = 0; k < 16; k++) if (mt[o + k] != 0xCC) { all_cc = 0; break; }
                if (all_cc) continue;
                windows++;
                if (!memcmp(mt + o, ft + o, 16)) same++;
            }
        }
        UnmapViewOfFile(v);
    } else { sa_audit("FAIL cannot re-read %s\n", exe_abs); bad++; }
    if (m) CloseHandle(m);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    bad += same != 0;
    sa_audit("code_range %08x..%08x bytes %u (all sections not copied: %u) thunks %d (%d bytes) int3 %u other %u\n",
             RT_TEXT_LO, code_hi, code_hi - RT_TEXT_LO, notcopied, nt_, 5 * nt_, int3, other);
    sa_audit("retail_text_windows_compared %u identical %u\n", windows, same);
    /* the protections: nothing executable in the image but the code range */
    uint32_t a = RT_IMG_LO, xout = 0;
    while (a < RT_IMG_HI) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) break;
        uint32_t lo = (uint32_t)(uintptr_t)mi.BaseAddress, hi = lo + (uint32_t)mi.RegionSize;
        if (hi > RT_IMG_HI) hi = RT_IMG_HI;
        int x = (mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        sa_audit("protect %08x..%08x %04lx%s\n", lo, hi, (unsigned long)mi.Protect, x ? " exec" : "");
        if (x && (lo < RT_TEXT_LO || hi > code_hi)) xout++;
        a = hi;
    }
    bad += xout != 0;
    sa_audit("exec_outside_code_range %u\n", xout);
    sa_audit("libraries smackw32=%s (RAD's Smacker DLL from the game dir, not Lords2.exe: only with --movies)\n",
             O.movies ? "loaded" : "not-loaded");
    sa_audit("verdict %s\n", bad ? "FAIL" : "OK");
    rh_log("standalone: %d thunks, code range %u bytes (%u INT3, %u other), retail code windows identical %u/%u, "
           "exec outside the code range %u — audit %s (%s)\n", nt_, code_hi - RT_TEXT_LO, int3, other, same, windows, xout,
           bad ? "FAIL" : "OK", g_audit);
    if (bad) rh_fatal("standalone: the audit failed (%s)", g_audit);
}
/* the standalone's entry: the port of retail's start (rl_crt.c) → WinMain → exit → ExitProcess (never returns) */
static void sa_entry(void)
{
    rh_port_bind_once();   /* the port's seams (engine_retail_call → a VA: a thunk, or a gap) and table binds */
    int code = rl_crt_start();
    ((void (WINAPI *)(UINT))iat_lookup("ExitProcess"))((UINT)code);
}

void __cdecl rh_game_main(void)                    /* game_tramp: retail's entry returned */
{
    finish("entry-returned");
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
    char occ[16] = "", rl[4] = "";   /* RHOST_TEST_OCCUPY=VA: take 64 KB there in the first launch (tests the relaunch) */
    if (GetEnvironmentVariableA("RHOST_TEST_OCCUPY", occ, sizeof occ) && !GetEnvironmentVariableA("RHOST_RELAUNCH", rl, sizeof rl))
        VirtualAlloc((void *)(uintptr_t)strtoul(occ, 0, 0), 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    if (!O.no_arena && rh_heap_init()) fixed_range_taken("heap arena", 0x20000000u, 0x58000000u);
    if (rh_media_reserve()) fixed_range_taken("media pool", 0x60000000u, 0x70000000u);
    stacks_init();
    GetFullPathNameA(O.gamedir, sizeof g_gamedir_abs, g_gamedir_abs, NULL);
    for (size_t l = strlen(g_gamedir_abs); l && g_gamedir_abs[l - 1] == '\\'; ) g_gamedir_abs[--l] = 0;
    char exe_abs[MAX_PATH]; GetFullPathNameA(O.exe, sizeof exe_abs, exe_abs, NULL);
    if (!SetCurrentDirectoryA(g_gamedir_abs)) rh_fatal("cannot cd to %s", g_gamedir_abs);
    retail_image_t img;
    if (O.standalone && (O.coverage || g_ntrap)) rh_fatal("--standalone maps no retail code: nothing to cover / trap");
    if (retail_map(exe_abs, &img, O.standalone)) rh_fatal("map %s failed", exe_abs);
    layout_from_pe();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(rh_crash_filter);
    AddVectoredExceptionHandler(0, crash_veh);   /* first in, last called: probes' handlers see theirs first */
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
    rh_smack_install();                      /* last: smackw32's imports bind to every layer above */
    if (O.standalone) rl_plat_bind_imports();   /* retail's slots; the CRT is the port of retail's (rl_crt.c) */
    rl_frame_driver = (int (*)(void))frame_wrap;   /* the port's 0x40e890 calls it as retail's redirected call site does (no frame between) */
    if (!O.standalone) {                     /* (standalone: no call sites — the port's 0x40e890 calls rl_frame_driver) */
        int nf = redirect_calls(L2_FRAME_FN, (void *)frame_wrap);
        if (nf < 1) rh_fatal("frame driver %08x: no call sites", L2_FRAME_FN);
        rh_log("hooks: frame driver %08x <- %d call site(s)\n", L2_FRAME_FN, nf);
    }
    load_volatile();
    rh_hooks_install();
    if (O.coverage) cov_install();
    if (g_nw) AddVectoredExceptionHandler(1, watch_veh);
    if (g_ntrap) {
        AddVectoredExceptionHandler(1, trap_veh);
        for (int i = 0; i < g_ntrap; i++) { g_torig[i] = G8(g_tva[i]); G8(g_tva[i]) = 0xCC; }
    }
    if (!O.frames) O.frames = (O.record || O.bot || O.live) ? 0xfffffff0u : O.trace ? rh_trace_last_frame() + O.tail : 3000u;
    rh_log("rhost: frames<=%u step=%ums vbase=%u creep_after=%u realtime=%d show=%d movies=%d arena=%d overlay=%d standalone=%d\n",
           O.frames, O.step_ms, O.vbase, O.creep_after, O.realtime, O.show, O.movies, !O.no_arena, !O.no_overlay, O.standalone);
    if (O.standalone) { AddVectoredExceptionHandler(1, sa_veh); sa_seal(exe_abs, &img); }
    rh_bot_install();                        /* bot mode: serves the controller until its first `run` */
    if (!O.record && !O.show && !O.bot && !O.live && O.stall_wall_ms) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    if (O.live || O.snap_test) {             /* the host drives retail frame by frame */
        coro_init(O.standalone ? (void *)sa_entry : (void *)(uintptr_t)img.entry);
        rh_snap_register("fb.chain", &g_fb_chain, sizeof g_fb_chain); rh_snap_register("fb.n", &g_fb_n, sizeof g_fb_n);
        rh_win_snap_register();
        if (O.live) { rh_live_install(); rh_live_register(); rh_live_main(); }
        for (;;) { rh_snap_selftest(); rh_step(); }
    }
    call_on_stack(O.standalone ? (void *)sa_entry : (void *)(uintptr_t)img.entry, MAIN_STACK_LO, MAIN_STACK_HI - 192);   /* CRT entry: never returns */
    finish("entry-returned");
    return 0;
}
