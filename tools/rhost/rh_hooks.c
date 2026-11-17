/* rh_hooks.c — port functions vs retail on identical state (the registry; core rows here,
 * subsystem rows in rh_hooks_<subsystem>.c — see rh_hooks.h).
 *
 *   --hook shadow:NAME[,…]   every retail call of NAME runs retail, then the PORT on the identical
 *                            pre-state (rh_shadow_ex), compares every state dword; retail's
 *                            result is kept. Divergences are logged with the first differing VAs.
 *   --hook replace:NAME      the port runs INSTEAD of retail (hybrid mode).
 *   --hook @FILE             the spec read from a file (rhost.py always passes one).
 * Add a port function = a retail/port wrapper pair + one table row (rhost.py derives VERIFY_HOOKS).
 * Rule: a port function never calls its own retail VA. */
#include "rh_hooks.h"
#include "engine.h"

void rh_hook_count(hook_t *h, int r, const char *why)
{
    h->calls++;
    if (r < 0) { if (h->skipped++ < 5) rh_log("SKIP %s f%u %s\n", h->name, g_frame, why); return; }
    if (r > 0) {
        if (!h->diverge++) snprintf(h->first, sizeof h->first, "f%u %s", g_frame, why);
        if (h->diverge <= 5) rh_log("DIVERGE %s f%u %s\n", h->name, g_frame, why);
    }
}

/* RH_SH_SAMPLE rows — hot render leaves (~1 us a call; a shadowed call costs ~400 us, its page faults dominate): per
 * (row, caller's return address, screen id DAT_004eac50) the first SAMPLE_FIRST calls are shadowed, then every
 * power-of-two-th one; the rest run retail and count in `sampled` (hook-result sampled=N), not in `calls`. Every
 * call site on every screen gets compared; the hybrid pass runs every call. --shadow-all: no sampling. */
#define SAMPLE_FIRST 64
#define SAMPLE_N 65536
static struct { hook_t *h; uint32_t ret; uint32_t n; uint8_t scr; } g_smp[SAMPLE_N];
int rh_hook_sample(hook_t *h, uint32_t ret)
{
    if (h->flags & RH_SH_HOST) { h->sampled++; return 0; }   /* host level: retail runs in shadow passes */
    if (!(h->flags & RH_SH_SAMPLE) || O.shadow_all) return 1;
    uint8_t scr = G8(L2_SCREEN);
    uint32_t k = ((uint32_t)(uintptr_t)h * 2654435761u ^ ret * 40503u ^ scr * 97u) & (SAMPLE_N - 1);
    for (uint32_t i = 0; i < SAMPLE_N; i++, k = (k + 1) & (SAMPLE_N - 1)) {
        if (!g_smp[k].h) { g_smp[k].h = h; g_smp[k].ret = ret; g_smp[k].scr = scr; }
        else if (g_smp[k].h != h || g_smp[k].ret != ret || g_smp[k].scr != scr) continue;
        uint32_t n = ++g_smp[k].n;
        if (n <= SAMPLE_FIRST || !(n & (n - 1))) return 1;
        h->sampled++;
        return 0;
    }
    return 1;   /* table full: shadow */
}

/* ---------------------------------------------------------------- rng_step: FUN_00404a46 (src/engine_rng.c) */
static hook_t *H_RNG;
static void rng_retail(void *c) { (void)c; ((void (__cdecl *)(void))(uintptr_t)H_RNG->entry)(); }
static void rng_port(void *c)
{
    (void)c;
    engine_rng_in_t in = { G32(0x58fe28u), G32(0x58fe08u) };
    engine_rng_out_t o;
    engine_rng_step(&in, &o);
    G32(0x58fe28u) = o.new_state_a; G32(0x58fe08u) = o.new_state_b;
    G32(0x5aea58u) = o.rng_15bit_a; G32(0x58fd7cu) = o.rng_7bit_a; G32(0x58fd70u) = o.rng_3bit_a;
    G32(0x5aea5cu) = o.rng_15bit_b; G32(0x58fd60u) = o.rng_7bit_b; G32(0x58fd6cu) = o.rng_3bit_b;
}
static void __cdecl w_rng(void)
{
    if (H_RNG->mode == H_REPLACE) { H_RNG->calls++; rng_port(NULL); return; }
    if (rh_in_shadow()) { rng_retail(NULL); return; }
    char why[400];
    rh_hook_count(H_RNG, rh_shadow_ex("rng_step", rng_retail, rng_port, NULL, why, sizeof why), why);
}

static hook_t CORE_HOOKS[] = {
    { "rng_step", 0x404a46u, (void *)w_rng, 0, 0, 0, 0, "" },
};

/* every table: core + one per subsystem file (rh_hooks.h RH_HOOK_TABLES) */
#define T(name) extern hook_t name[]; extern const int name##_N;
RH_HOOK_TABLES(T)
#undef T
static struct { hook_t *t; int n; } TABLES[] = {
    { CORE_HOOKS, (int)(sizeof CORE_HOOKS / sizeof *CORE_HOOKS) },
#define T(name) { name, 0 },
    RH_HOOK_TABLES(T)
#undef T
};
#define NTAB ((int)(sizeof TABLES / sizeof *TABLES))
static hook_t *find_hook(const char *name)
{
    for (int k = 0; k < NTAB; k++) for (int i = 0; i < TABLES[k].n; i++) if (!strcmp(TABLES[k].t[i].name, name)) return &TABLES[k].t[i];
    return NULL;
}

/* --standalone: the retail code range holds INT3 (retail_map data_only) and, at every row's entry, a 5-byte `jmp rel32`
 * to its wrapper — written here, never copied from the file. Every row is REPLACE (no retail side to shadow). */
#define SA_MAX 4096
static rh_thunk_t g_sa[SA_MAX]; static int g_nsa;
static void sa_thunk(uint32_t va, void *to, const char *name)
{
    for (int i = 0; i < g_nsa; i++)
        if (g_sa[i].va == va) {
            /* two rows for one function (an old port and its retail-layout successor, both verified): the first
             * table's wins (rh_hooks.h order), the other is logged */
            if (g_sa[i].target != to) rh_log("standalone: %s and %s share the entry %08x: %s kept\n", g_sa[i].name, name, va, g_sa[i].name);
            return;
        }
    if (va < RT_TEXT_LO || va + 5 > RT_TEXT_LO + RT_TEXT_SZ) rh_fatal("standalone: %s at %08x is outside the code range", name, va);
    if (g_nsa == SA_MAX) rh_fatal("standalone: more than %d thunks", SA_MAX);
    uint8_t *e = (uint8_t *)(uintptr_t)va;
    e[0] = 0xE9; *(int32_t *)(e + 1) = (int32_t)((uint32_t)(uintptr_t)to - (va + 5));
    g_sa[g_nsa].va = va; g_sa[g_nsa].target = to; g_sa[g_nsa++].name = name;
}
static int sa_cmp(const void *a, const void *b) { uint32_t x = ((const rh_thunk_t *)a)->va, y = ((const rh_thunk_t *)b)->va; return x < y ? -1 : x > y; }
int rh_hooks_thunks(const rh_thunk_t **out) { *out = g_sa; return g_nsa; }
static void sa_install(void)
{
    for (int k = 0; k < NTAB; k++) for (int i = 0; i < TABLES[k].n; i++) {
        hook_t *h = &TABLES[k].t[i];
        h->mode = H_REPLACE;
        sa_thunk(h->va, h->wrapper, h->name);
    }
    qsort(g_sa, (size_t)g_nsa, sizeof *g_sa, sa_cmp);
    for (int i = 1; i < g_nsa; i++)
        if (g_sa[i].va - g_sa[i - 1].va < 5)
            rh_fatal("standalone: thunks %s %08x / %s %08x overlap", g_sa[i - 1].name, g_sa[i - 1].va, g_sa[i].name, g_sa[i].va);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)RT_TEXT_LO, RT_TEXT_SZ);
    rh_log("standalone: %d entry thunks into port code (every row REPLACE)\n", g_nsa);
}

void rh_hooks_install(void)
{
    H_RNG = &CORE_HOOKS[0];
    {   int k = 1;
#define T(name) TABLES[k++].n = name##_N;
        RH_HOOK_TABLES(T)
#undef T
        (void)k;
    }
    for (int k = 0; k < NTAB; k++) for (int i = 0; i < TABLES[k].n; i++) TABLES[k].t[i].entry = TABLES[k].t[i].va;
    if (O.standalone) {
        if (O.hooks && *O.hooks && O.hooks[0] != '@') rh_fatal("--standalone: every row is REPLACE (no --hook)");
        sa_install();
        return;
    }
    if (!O.hooks || !*O.hooks) return;
    /* every row replaced at once is > 11 KB of spec (506 rows, 2026-11-04): an 8 KB buffer cut the tail off */
    static char spec[1 << 17];
    if (O.hooks[0] == '@') {
        FILE *f = fopen(O.hooks + 1, "r");
        if (!f) rh_fatal("cannot read hook spec %s", O.hooks + 1);
        size_t n = fread(spec, 1, sizeof spec - 1, f); spec[n] = 0;
        if (fgetc(f) != EOF) rh_fatal("hook spec %s: longer than %u bytes", O.hooks + 1, (unsigned)sizeof spec - 1);
        fclose(f);
    } else if ((size_t)snprintf(spec, sizeof spec, "%s", O.hooks) >= sizeof spec) rh_fatal("--hook: longer than %u bytes", (unsigned)sizeof spec - 1);
    for (char *t = strtok(spec, ", \r\n"); t; t = strtok(NULL, ", \r\n")) {
        char *c = strchr(t, ':');
        if (!c) rh_fatal("hook spec '%s': want shadow:NAME / replace:NAME", t);
        *c = 0;
        int mode = !strcmp(t, "shadow") ? H_SHADOW : !strcmp(t, "replace") ? H_REPLACE : !strcmp(t, "off") ? H_OFF : -1;
        if (mode < 0) rh_fatal("hook mode '%s'", t);
        hook_t *h = find_hook(c + 1);
        if (!h) rh_fatal("unknown hook '%s'", c + 1);
        h->mode = mode;
        if (mode == H_OFF) continue;
        int n = O.hook_entry ? 0 : redirect_calls(h->va, h->wrapper);
        /* also reached through a pointer, or replaced (a port seam that calls the VA by address, engine_retail_call, must
         * land in the port as in the standalone, whose thunks cover every entry): patch the entry too (retail side:
         * trampoline) */
        if (n && (rh_is_addr_taken(h->va) || mode == H_REPLACE)) {
            h->entry = rh_patch_entry(h->va, h->wrapper);
            rh_log("hook %s:%s %08x <- %d call site(s) + entry patch (address taken; trampoline %08x)\n", t, h->name, h->va, n, h->entry);
            continue;
        }
        if (n) { rh_log("hook %s:%s %08x <- %d call site(s)\n", t, h->name, h->va, n); continue; }
        h->entry = rh_patch_entry(h->va, h->wrapper);   /* no call site (a pointer table) or --hook-entry */
        rh_log("hook %s:%s %08x <- entry patch (trampoline %08x)\n", t, h->name, h->va, h->entry);
    }
}

/* --probe FILE: SYNTHETIC CARRIERS for rows no scenario reaches (dead or error-path code: the CRT's report / locale /
 * environment paths). At the end of frame F (frame_wrap, on retail's stack) each probe line `F NAME ARG...` calls the
 * row's wrapper with those arguments, exactly as a retail call site would: in shadow mode retail runs, then the port on
 * the identical pre-state, state + return value compared (hook-result). Arguments: an integer (0x.. / decimal /
 * negative), "text" (C escapes \n \t \\ \" \xHH; NUL-terminated on retail's stack above the call), w"text" (the same,
 * UTF-16), buf:N (N bytes 0xcc on the stack: an output — a HOOK_SO row compares it), va:VA (a retail address as is),
 * $N / $N+K / $N-K (the value probe call N returned — retail's EAX, N counting call lines from 1 — plus K: a heap block,
 * an fd, a FILE* an earlier line set up, passed along).
 * Stack data sits above the wrapper's e0, so retail's own frames never overwrite it. Lines: '#' comments; `scenario`
 * lines are rhost.py's (ignored here). The run is no longer a pure replay: rhost.py probe compares hook results only. */
#define PROBE_MAX 512
static struct { uint32_t f; char name[64]; char args[400]; uint32_t ret; int ran; } g_pr[PROBE_MAX];
static int g_npr, g_pr_done;
static void probe_load(void)
{
    FILE *f = fopen(O.probe, "r");
    if (!f) rh_fatal("--probe: cannot read %s", O.probe);
    char l[512];
    while (fgets(l, sizeof l, f)) {
        char *p = l; while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == '\n' || *p == '\r' || !strncmp(p, "scenario", 8)) continue;
        if (g_npr == PROBE_MAX) rh_fatal("--probe: more than %d lines", PROBE_MAX);
        char *e; g_pr[g_npr].f = strtoul(p, &e, 0); p = e; while (*p == ' ') p++;
        int k = 0; while (*p && *p != ' ' && *p != '\n' && *p != '\r' && k < 63) g_pr[g_npr].name[k++] = *p++;
        g_pr[g_npr].name[k] = 0; while (*p == ' ') p++;
        snprintf(g_pr[g_npr].args, sizeof g_pr[g_npr].args, "%s", p);
        char *nl = strpbrk(g_pr[g_npr].args, "\r\n"); if (nl) *nl = 0;
        if (!find_hook(g_pr[g_npr].name)) rh_fatal("--probe: unknown row '%s'", g_pr[g_npr].name);
        g_npr++;
    }
    fclose(f);
    rh_log("probe: %d synthetic call(s) from %s\n", g_npr, O.probe);
}
/* call `fn` with n dwords (cdecl or stdcall: esp is restored from esi either way) */
static uint32_t probe_call(void *fn, const uint32_t *a, int n)
{
    uint32_t r;
    __asm__ volatile(
        "pushl %%esi\n\tmovl %%esp, %%esi\n\t"
        "1:\ttestl %%ecx, %%ecx\n\tjz 2f\n\tdecl %%ecx\n\tpushl (%%edx,%%ecx,4)\n\tjmp 1b\n"
        "2:\tcall *%%eax\n\tmovl %%esi, %%esp\n\tpopl %%esi"
        : "=a"(r), "+c"(n), "+d"(a) : "a"(fn) : "memory", "ebx", "edi");
    return r;
}
static __attribute__((noinline)) void probe_one(int i)
{
    char buf[8192];   /* the probe's stack data, above every frame the call makes */
    uint32_t a[16]; int n = 0; size_t used = 0;
    const char *p = g_pr[i].args;
    while (*p && n < 16) {
        while (*p == ' ') p++;
        if (!*p) break;
        int wide = 0;
        if (p[0] == 'w' && p[1] == '"') { wide = 1; p++; }
        if (*p == '"') {
            p++;
            uint32_t at = (uint32_t)(uintptr_t)(buf + used);
            while (*p && *p != '"') {
                unsigned c = (uint8_t)*p++;
                if (c == '\\') {
                    c = (uint8_t)*p++;
                    if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == '0') c = 0;
                    else if (c == 'x') { char h[3] = { p[0], p[1], 0 }; c = (unsigned)strtoul(h, 0, 16); p += 2; }
                }
                if (used + 4 > sizeof buf) rh_fatal("--probe: line %d: stack data over %u bytes", i, (unsigned)sizeof buf);
                buf[used++] = (char)c; if (wide) buf[used++] = 0;
            }
            if (*p == '"') p++;
            buf[used++] = 0; if (wide) buf[used++] = 0;
            used = (used + 3) & ~(size_t)3;
            a[n++] = at;
        } else if (!strncmp(p, "buf:", 4)) {
            size_t k = strtoul(p + 4, (char **)&p, 0);
            if (used + k + 4 > sizeof buf) rh_fatal("--probe: line %d: stack data over %u bytes", i, (unsigned)sizeof buf);
            memset(buf + used, 0xcc, k);
            a[n++] = (uint32_t)(uintptr_t)(buf + used);
            used = (used + k + 3) & ~(size_t)3;
        } else if (!strncmp(p, "va:", 3)) {
            a[n++] = (uint32_t)strtoul(p + 3, (char **)&p, 0);
        } else if (*p == '$') {
            char *e; long k = strtol(p + 1, &e, 10);
            if (e == p + 1 || k < 1 || k > g_npr) rh_fatal("--probe: line %d: bad reference '%s'", i, p);
            if (!g_pr[k - 1].ran) rh_fatal("--probe: line %d: $%ld has not run yet", i, k);
            p = e;
            uint32_t v = g_pr[k - 1].ret;
            if (*p == '+' || *p == '-') v += (uint32_t)strtol(p, (char **)&p, 0);
            a[n++] = v;
        } else {
            char *e; a[n++] = (uint32_t)strtoll(p, &e, 0);
            if (e == p) rh_fatal("--probe: line %d: bad argument '%s'", i, p);
            p = e;
        }
    }
    hook_t *h = find_hook(g_pr[i].name);
    if (h->mode == H_OFF) rh_fatal("--probe: row %s is not hooked (--hook shadow:%s)", h->name, h->name);
    uint32_t r = probe_call(h->wrapper, a, n);
    g_pr[i].ret = r; g_pr[i].ran = 1;
    rh_log("probe %s f%u args=%d ret=%08x\n", h->name, g_frame, n, r);
}
void rh_probe_frame(void)
{
    if (!O.probe) return;
    if (!g_pr_done) { g_pr_done = 1; probe_load(); }
    for (int i = 0; i < g_npr; i++) if (g_pr[i].f == g_frame) probe_one(i);
}

void rh_hooks_report(void)
{
    rh_shadow_report();
    for (int k = 0; k < NTAB; k++) for (int i = 0; i < TABLES[k].n; i++) {
        hook_t *h = &TABLES[k].t[i];
        if (h->mode == H_OFF) continue;
        char smp[32] = ""; if (h->sampled) snprintf(smp, sizeof smp, " sampled=%ld", h->sampled);
        rh_log("hook-result %s mode=%s calls=%ld diverge=%ld skipped=%ld%s%s%s\n", h->name, h->mode == H_SHADOW ? "shadow" : "replace",
               h->calls, h->diverge, h->skipped, smp, h->diverge ? " first: " : "", h->diverge ? h->first : "");
    }
}
