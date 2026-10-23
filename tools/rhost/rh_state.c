/* rh_state.c — state hashing, dumps, shadow execution.
 *
 * "State" = retail .data/.bss (RT_DATA_LO..HI from the PE: 0x4d2000..) + the committed heap arena
 * (retail's heap, and rhost's fake DirectDraw / DirectSound objects). Surface pixels and sound
 * bytes (the media pool) are not state: the presented frame has its own hash (rh_shot.c).
 *   full hash     : every byte.
 *   semantic hash : every dword except those at VOLATILE ADDRESSES (<rundir>\volatile.txt).
 *                   With rhost's fake HWND / DirectDraw / DirectSound / handles there should be
 *                   none: `rhost.py det` reports any slot that still differs between runs.
 * Dump (.rhs)   : "RHS2" frame x87cw nranges {lo,hi}* bytes* extpage-bitmap(128K) nvol vol*
 *                 — read by tools/rhost/rhs.py. (openrecet / OpenSummoners origin.) */
#include "rhost.h"

#define MAXR 4096
#define MAXVOL 65536
static uint32_t g_vol[MAXVOL]; static int g_nvol, g_vol_sorted;
static uint8_t g_volpg[1u << 17];
static uint8_t g_ext[1u << 17];

void rh_state_volatile_add(uint32_t va)
{
    if (g_nvol >= MAXVOL) rh_fatal("volatile.txt: more than %d addresses", MAXVOL);
    g_vol[g_nvol++] = va & ~3u; g_vol_sorted = 0;
    g_volpg[va >> 15] |= (uint8_t)(1u << ((va >> 12) & 7));
}
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static int is_vol(uint32_t a)
{
    if (!(g_volpg[a >> 15] >> ((a >> 12) & 7) & 1)) return 0;
    if (!g_vol_sorted) { qsort(g_vol, g_nvol, 4, cmp_u32); g_vol_sorted = 1; }
    int lo = 0, hi = g_nvol - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (g_vol[m] == a) return 1; if (g_vol[m] < a) lo = m + 1; else hi = m - 1; }
    return 0;
}
/* pages of any mapping rhost does not lay out deterministically (dump classification only) */
static void build_ext_map(void)
{
    memset(g_ext, 0, sizeof g_ext);
    uint32_t a = 0x10000;
    for (;;) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) break;
        uint32_t lo = (uint32_t)(uintptr_t)mi.BaseAddress, sz = (uint32_t)mi.RegionSize, hi = lo + sz;
        if (mi.State != MEM_FREE && !rh_is_det_addr(lo))
            for (uint32_t p = lo >> 12; p < (hi ? hi >> 12 : 0x100000u); p++) g_ext[p >> 3] |= (uint8_t)(1u << (p & 7));
        if (!hi || hi <= a) break;
        a = hi;
    }
}
static int state_ranges(uint32_t (*r)[2])
{
    r[0][0] = RT_DATA_LO; r[0][1] = RT_DATA_HI & ~3u;
    return 1 + rh_heap_ranges(r + 1, MAXR - 1);
}

void rh_state_hash(rh_hash_t *h)
{
    static uint32_t r[MAXR][2];
    int n = state_ranges(r);
    uint64_t f = 1469598103934665603ull, s = f;
    uint32_t masked = 0, bytes = 0;
    for (int i = 0; i < n; i++) {
        for (const uint32_t *p = (const uint32_t *)(uintptr_t)r[i][0]; p < (const uint32_t *)(uintptr_t)r[i][1]; p++) {
            uint32_t v = *p;
            f ^= v; f *= 1099511628211ull;
            if (g_nvol && is_vol((uint32_t)(uintptr_t)p)) { v = 0x9e3779b9u; masked++; }
            s ^= v; s *= 1099511628211ull;
        }
        bytes += r[i][1] - r[i][0];
    }
    h->full = f; h->semantic = s; h->masked = masked; h->ranges = (uint32_t)n; h->bytes = bytes;
}

int rh_state_dump(const char *path)
{
    static uint32_t r[MAXR][2];
    int n = state_ranges(r);
    build_ext_map();
    FILE *f = fopen(path, "wb");
    if (!f) { rh_log("dump: cannot open %s\n", path); return -1; }
    uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
    uint32_t hdr[4] = { 0x32534852u /* "RHS2" */, g_frame, cw, (uint32_t)n };
    fwrite(hdr, 4, 4, f);
    fwrite(r, 8, n, f);
    for (int i = 0; i < n; i++) fwrite((void *)(uintptr_t)r[i][0], 1, r[i][1] - r[i][0], f);
    fwrite(g_ext, 1, sizeof g_ext, f);
    uint32_t nv = (uint32_t)g_nvol; fwrite(&nv, 4, 1, f); fwrite(g_vol, 4, g_nvol, f);
    fclose(f);
    rh_log("dump frame=%u ranges=%d -> %s\n", g_frame, n, path);
    return 0;
}

/* ---- shadow execution: retail(ctx) and port(ctx) on the identical pre-state ----
 * snapshot (.data + arena + allocator metadata + virtual clock) → retail → save → restore →
 * port → compare every state dword. Retail's post-state is kept either way. External effects
 * inside the function run twice — use for sim/logic functions. */
static uint8_t *g_pre, *g_ret; static size_t g_cap;
static int g_shadow_depth;
int rh_in_shadow(void) { return g_shadow_depth != 0; }
/* per-phase cost of the shadow (reported with the hook results): ranges, snapshot, retail, swap, port, compare */
enum { SP_RANGES, SP_SNAP, SP_RETAIL, SP_SWAP, SP_PORT, SP_CMP, SP_DONE, SP_N };
static const char *const g_spn[SP_N] = { "ranges", "snap", "retail", "swap", "port", "cmp", "done" };
static int64_t g_spt[SP_N], g_spt0; static long g_spcalls, g_spfaults, g_sppages;
static void sp_mark(int k) { LARGE_INTEGER t; QueryPerformanceCounter(&t); if (k >= 0) g_spt[k] += t.QuadPart - g_spt0; g_spt0 = t.QuadPart; }
void rh_shadow_report(void)
{
    if (!g_spcalls) return;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    char b[512]; int w = snprintf(b, sizeof b, "shadow-prof %s calls=%ld faults=%ld pages/call=%.1f us/call:", O.shadow_full ? "copy" : "paged",
                                  g_spcalls, g_spfaults, (double)g_sppages / g_spcalls);
    for (int k = 0; k < SP_N; k++) w += snprintf(b + w, sizeof b - w, " %s=%.1f", g_spn[k], 1e6 * g_spt[k] / f.QuadPart / g_spcalls);
    rh_log("%s\n", b);
}
static void poison(void) { if (O.shadow_poison) *(volatile uint32_t *)(uintptr_t)O.shadow_poison ^= 1; }
/* host-side state retail's code advances that is not retail memory: the allocator metadata and rhost's own
 * layers (media pool, DirectSound serials / registry / timers, the fake-handle sequence, the virtual message
 * queue + window state). File I/O is not rolled back: a shadowed call that writes a file writes it twice. Saved and rolled
 * with the state: the port run starts from the host state retail's run started from (a port that calls back
 * into retail — an audio cue loads a WAV and creates a buffer, FUN_00427990 — would otherwise see it
 * advanced), and retail's post-state is put back after the comparison. */
typedef struct { void *heap, *media, *ds, *hnd, *win; } meta_t;
static meta_t *meta_save(void)
{
    meta_t *m = malloc(sizeof *m);
    m->heap = rh_heap_meta_save(); m->media = rh_media_meta_save(); m->ds = rh_ds_meta_save(); m->hnd = rh_handles_meta_save();
    m->win = rh_win_meta_save();
    return m;
}
static void meta_restore(const meta_t *m)
{
    rh_heap_meta_restore(m->heap);
    if (m->media) rh_media_meta_restore(m->media);
    if (m->ds) rh_ds_meta_restore(m->ds);
    if (m->hnd) rh_handles_meta_restore(m->hnd);
    rh_win_meta_restore(m->win);
}
static void meta_free(meta_t *m) { rh_heap_meta_free(m->heap); free(m->media); free(m->ds); free(m->hnd); free(m->win); free(m); }

static int shadow_copy(const char *name, void (*retail)(void *), void (*port)(void *), void *ctx, char *why, size_t whyn)
{
    static uint32_t r[MAXR][2], r2[MAXR][2];
    sp_mark(-1); g_spcalls++;
    int n = state_ranges(r);
    sp_mark(SP_RANGES);
    size_t tot = 0;
    for (int i = 0; i < n; i++) tot += r[i][1] - r[i][0];
    if (tot > g_cap) { free(g_pre); free(g_ret); g_cap = tot + (16u << 20); g_pre = malloc(g_cap); g_ret = malloc(g_cap); }
    #define COPY_OUT(buf) { size_t o = 0; for (int i = 0; i < n; i++) { size_t l = r[i][1] - r[i][0]; memcpy((buf) + o, (void *)(uintptr_t)r[i][0], l); o += l; } }
    #define COPY_IN(buf)  { size_t o = 0; for (int i = 0; i < n; i++) { size_t l = r[i][1] - r[i][0]; memcpy((void *)(uintptr_t)r[i][0], (buf) + o, l); o += l; } }
    uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
    uint32_t clk0[3], clk1[3]; rh_clock_save(clk0);
    meta_t *meta = meta_save();
    COPY_OUT(g_pre);
    sp_mark(SP_SNAP);
    g_shadow_depth++;
    retail(ctx);
    g_shadow_depth--;
    sp_mark(SP_RETAIL);
    int n2 = state_ranges(r2);
    if (n2 != n || memcmp(r, r2, n * 8)) {
        snprintf(why, whyn, "%s: arena commit changed during call (ranges %d->%d); not compared", name, n, n2);
        meta_free(meta);
        return -1;
    }
    COPY_OUT(g_ret);
    meta_t *meta_ret = meta_save();
    rh_clock_save(clk1);
    COPY_IN(g_pre);
    meta_restore(meta);
    rh_clock_restore(clk0);
    __asm__ volatile("fldcw %0" : : "m"(cw));
    sp_mark(SP_SWAP);
    g_shadow_depth++;
    port(ctx);
    g_shadow_depth--;
    poison();
    sp_mark(SP_PORT);
    long diff = 0; int k = 0;
    int w = snprintf(why, whyn, "%s:", name);
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        size_t l = r[i][1] - r[i][0];
        const uint32_t *a = (const uint32_t *)(g_ret + o), *b = (const uint32_t *)(uintptr_t)r[i][0];
        if (memcmp(a, b, l))
            for (size_t q = 0; q < l / 4; q++)
                if (a[q] != b[q]) {
                    if (k++ < 6 && w < (int)whyn - 48)
                        w += snprintf(why + w, whyn - w, " %08x r%08x/p%08x", r[i][0] + (uint32_t)q * 4, a[q], b[q]);
                    diff++;
                }
        o += l;
    }
    sp_mark(SP_CMP);
    if (diff) { COPY_IN(g_ret); if (w < (int)whyn - 24) snprintf(why + w, whyn - w, " (%ld differ)", diff); }
    meta_restore(meta_ret);
    rh_clock_restore(clk1);
    __asm__ volatile("fldcw %0" : : "m"(cw));
    meta_free(meta); meta_free(meta_ret);
    sp_mark(SP_DONE);
    return diff ? 1 : 0;
    #undef COPY_OUT
    #undef COPY_IN
}

/* ---- page-tracked shadow (the default): the state is write-protected for the call and a vectored handler
 * copies each page on its FIRST write, so a call costs what it writes instead of ~5 MB copied four times
 * (~2.6 ms a call: shadowing hot leaves tripped the watchdog). retail → its dirty pages saved and restored,
 * the state re-protected → port → only the pages either side wrote are compared. A kernel-mode write into a
 * protected page fails instead of faulting, so the buffer-writing passthroughs call rh_shadow_prewrite first.
 * Same result as shadow_copy (--shadow-full); --shadow-poison VA checks both report a planted difference. */
#define NPG (1u << 20)
#define PBIT(m, p) ((m)[(p) >> 3] >> ((p) & 7) & 1)
#define PSET(m, p) ((m)[(p) >> 3] |= (uint8_t)(1u << ((p) & 7)))
#define PCLR(m, p) ((m)[(p) >> 3] &= (uint8_t)~(1u << ((p) & 7)))
static uint8_t g_trk[NPG / 8], g_open[NPG / 8], g_xp[NPG / 8];   /* state pages this call; unprotected now; RWX ones */
static uint32_t *g_spg; static uint8_t *g_spre, *g_sret, *g_sdone; static int g_nsp, g_spcap;   /* written pages */
static int32_t *g_spix;                           /* page → index in g_spg, -1 */
static volatile int g_armed;
static int g_paged_off;                           /* a state range was not uniformly RW / RWX: copy instead */

static void page_open(uint32_t pg)
{
    if (g_spix[pg] < 0) {   /* first write by either side: keep the pre-state */
        g_spix[pg] = g_nsp; g_spg[g_nsp] = pg; g_sdone[g_nsp] = 0;
        memcpy(g_spre + (size_t)g_nsp * 4096, (void *)(uintptr_t)(pg << 12), 4096);
        g_nsp++;
    }
    DWORD old; g_spfaults++;
    if (!VirtualProtect((void *)(uintptr_t)(pg << 12), 4096, PBIT(g_xp, pg) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old))
        rh_fatal("shadow: cannot unprotect page %08x (%lu)", pg << 12, GetLastError());
    PSET(g_open, pg);
}
static LONG CALLBACK shadow_veh(EXCEPTION_POINTERS *e)
{
    const EXCEPTION_RECORD *x = e->ExceptionRecord;
    if (!g_armed || x->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || x->NumberParameters < 2 || x->ExceptionInformation[0] != 1)
        return EXCEPTION_CONTINUE_SEARCH;
    uint32_t pg = (uint32_t)x->ExceptionInformation[1] >> 12;
    if (!PBIT(g_trk, pg) || PBIT(g_open, pg)) return EXCEPTION_CONTINUE_SEARCH;
    page_open(pg);
    return EXCEPTION_CONTINUE_EXECUTION;
}
void rh_shadow_prewrite(const void *p, size_t n)
{
    if (!g_armed || !n) return;
    uint32_t a = (uint32_t)(uintptr_t)p;
    for (uint32_t pg = a >> 12; pg <= (a + (uint32_t)n - 1) >> 12; pg++)
        if (PBIT(g_trk, pg) && !PBIT(g_open, pg)) page_open(pg);
}
/* release [lo, hi): runs of pages back to RW / RWX (what they were armed from), untracked */
static void release(uint32_t lo, uint32_t hi)
{
    for (uint32_t a = lo, e; a < hi; a = e) {
        int x = PBIT(g_xp, a >> 12);
        for (e = a + 4096; e < hi && PBIT(g_xp, e >> 12) == x; e += 4096) ;
        DWORD old;
        if (!VirtualProtect((void *)(uintptr_t)a, e - a, x ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old))
            for (uint32_t q = a; q < e; q += 4096) {   /* retail decommitted some (the call is then not compared) */
                MEMORY_BASIC_INFORMATION mi;
                if (VirtualQuery((void *)(uintptr_t)q, &mi, sizeof mi) && mi.State != MEM_COMMIT) continue;
                if (!VirtualProtect((void *)(uintptr_t)q, 4096, x ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old))
                    rh_fatal("shadow: cannot unprotect %08x (%lu)", q, GetLastError());
            }
        for (uint32_t pg = a >> 12; pg < e >> 12; pg++) PCLR(g_trk, pg);
    }
}
static int writable(DWORD p) { return p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY; }
/* write-protect every state page (ro=1), or give it back (ro=0). Walks VirtualQuery regions: wine reports
 * the retail image's untouched pages as (EXECUTE_)WRITECOPY. 0: a page that is not writable or would not
 * protect — everything protected so far is released, the caller copies instead */
static int protect_ranges(uint32_t (*r)[2], int n, int ro)
{
    for (int i = 0; i < n; i++) {
        uint32_t lo = r[i][0] & ~0xfffu, hi = (r[i][1] + 0xfffu) & ~0xfffu;
        if (!ro) { release(lo, hi); continue; }
        for (uint32_t a = lo, e; a < hi; a = e) {
            MEMORY_BASIC_INFORMATION mi; DWORD old;
            int ok = VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi) && mi.State == MEM_COMMIT && writable(mi.Protect);
            int x = ok && (mi.Protect == PAGE_EXECUTE_READWRITE || mi.Protect == PAGE_EXECUTE_WRITECOPY);
            e = ok ? (uint32_t)(uintptr_t)mi.BaseAddress + (uint32_t)mi.RegionSize : a + 4096;
            if (e > hi) e = hi;
            if (ok) ok = VirtualProtect((void *)(uintptr_t)a, e - a, x ? PAGE_EXECUTE_READ : PAGE_READONLY, &old);
            if (!ok) {
                for (int j = 0; j < i; j++) release(r[j][0] & ~0xfffu, (r[j][1] + 0xfffu) & ~0xfffu);
                release(lo, a);
                rh_log("shadow: state page %08x is not writable (protect %08lx); page tracking off (full copies)\n",
                       a, (unsigned long)mi.Protect);
                return 0;
            }
            for (uint32_t pg = a >> 12; pg < e >> 12; pg++) {
                PSET(g_trk, pg); PCLR(g_open, pg);
                if (x) PSET(g_xp, pg); else PCLR(g_xp, pg);
            }
        }
    }
    return 1;
}
static void disarm(uint32_t (*r)[2], int n)
{
    g_armed = 0;
    protect_ranges(r, n, 0);
    for (int i = 0; i < g_nsp; i++) { g_spix[g_spg[i]] = -1; PCLR(g_open, g_spg[i]); }
    g_nsp = 0;
}
static int cmp_spg(const void *a, const void *b) { uint32_t x = g_spg[*(const int *)a], y = g_spg[*(const int *)b]; return x < y ? -1 : x > y; }
static int shadow_paged(const char *name, void (*retail)(void *), void (*port)(void *), void *ctx, char *why, size_t whyn)
{
    static uint32_t r[MAXR][2], r2[MAXR][2];
    static int *ord;
    if (!g_spix) {
        g_spix = malloc(NPG * sizeof *g_spix); memset(g_spix, 0xff, NPG * sizeof *g_spix);
        AddVectoredExceptionHandler(1, shadow_veh);
    }
    sp_mark(-1); g_spcalls++;
    int n = state_ranges(r);
    sp_mark(SP_RANGES);
    int pages = 0;
    for (int i = 0; i < n; i++) pages += (int)((((r[i][1] + 0xfffu) & ~0xfffu) - (r[i][0] & ~0xfffu)) >> 12);
    if (pages > g_spcap) {   /* every state page may be written: the handler never allocates */
        g_spcap = pages + 1024;
        g_spg = realloc(g_spg, g_spcap * sizeof *g_spg); ord = realloc(ord, g_spcap * sizeof *ord); g_sdone = realloc(g_sdone, g_spcap);
        g_spre = realloc(g_spre, (size_t)g_spcap * 4096); g_sret = realloc(g_sret, (size_t)g_spcap * 4096);
        if (!g_spg || !ord || !g_sdone || !g_spre || !g_sret) rh_fatal("shadow: out of memory for %d pages", g_spcap);
    }
    uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
    uint32_t clk0[3], clk1[3]; rh_clock_save(clk0);
    meta_t *meta = meta_save();
    if (!protect_ranges(r, n, 1)) {
        g_paged_off = 1; meta_free(meta);
        return shadow_copy(name, retail, port, ctx, why, whyn);
    }
    g_armed = 1;
    sp_mark(SP_SNAP);
    g_shadow_depth++;
    retail(ctx);
    g_shadow_depth--;
    sp_mark(SP_RETAIL);
    int n2 = state_ranges(r2);
    if (n2 != n || memcmp(r, r2, n * 8)) {
        disarm(r, n);
        snprintf(why, whyn, "%s: arena commit changed during call (ranges %d->%d); not compared", name, n, n2);
        meta_free(meta);
        return -1;
    }
    int n1 = g_nsp;   /* the pages retail wrote: keep its post-state, put the pre-state back */
    for (int i = 0; i < n1; i++) {
        void *pp = (void *)(uintptr_t)(g_spg[i] << 12);
        memcpy(g_sret + (size_t)i * 4096, pp, 4096);
        memcpy(pp, g_spre + (size_t)i * 4096, 4096);
        g_sdone[i] = 1;
    }
    for (int i = 0; i < n1; i++) {   /* only the pages retail opened are writable now */
        uint32_t pg = g_spg[i]; DWORD old;
        if (!VirtualProtect((void *)(uintptr_t)(pg << 12), 4096, PBIT(g_xp, pg) ? PAGE_EXECUTE_READ : PAGE_READONLY, &old))
            rh_fatal("shadow: %s: cannot re-protect page %08x (%lu)", name, pg << 12, GetLastError());
        PCLR(g_open, pg);
    }
    meta_t *meta_ret = meta_save();
    rh_clock_save(clk1);
    meta_restore(meta);
    rh_clock_restore(clk0);
    __asm__ volatile("fldcw %0" : : "m"(cw));
    sp_mark(SP_SWAP);
    g_shadow_depth++;
    port(ctx);
    g_shadow_depth--;
    poison();
    sp_mark(SP_PORT);
    g_armed = 0;
    protect_ranges(r, n, 0);
    g_sppages += g_nsp;
    for (int i = 0; i < g_nsp; i++) ord[i] = i;
    qsort(ord, g_nsp, sizeof *ord, cmp_spg);   /* report differences in address order, like shadow_copy */
    uint32_t data_end = RT_DATA_HI & ~3u;
    long diff = 0; int k = 0;
    int w = snprintf(why, whyn, "%s:", name);
    for (int j = 0; j < g_nsp; j++) {
        int i = ord[j];
        uint32_t base = g_spg[i] << 12, len = 4096;
        if (base < data_end && base + 4096 > data_end) len = data_end - base;   /* .data's last page: state ends there */
        const uint32_t *a = (const uint32_t *)((g_sdone[i] ? g_sret : g_spre) + (size_t)i * 4096), *b = (const uint32_t *)(uintptr_t)base;
        if (memcmp(a, b, len))
            for (uint32_t q = 0; q < len / 4; q++)
                if (a[q] != b[q]) {
                    if (k++ < 6 && w < (int)whyn - 48)
                        w += snprintf(why + w, whyn - w, " %08x r%08x/p%08x", base + q * 4, a[q], b[q]);
                    diff++;
                }
    }
    sp_mark(SP_CMP);
    if (diff) {   /* keep retail's post-state */
        for (int i = 0; i < g_nsp; i++)
            memcpy((void *)(uintptr_t)(g_spg[i] << 12), (g_sdone[i] ? g_sret : g_spre) + (size_t)i * 4096, 4096);
        if (w < (int)whyn - 24) snprintf(why + w, whyn - w, " (%ld differ)", diff);
    }
    for (int i = 0; i < g_nsp; i++) { g_spix[g_spg[i]] = -1; PCLR(g_open, g_spg[i]); }
    g_nsp = 0;
    meta_restore(meta_ret);
    rh_clock_restore(clk1);
    __asm__ volatile("fldcw %0" : : "m"(cw));
    meta_free(meta); meta_free(meta_ret);
    sp_mark(SP_DONE);
    return diff ? 1 : 0;
}

int rh_shadow_ex(const char *name, void (*retail)(void *), void (*port)(void *), void *ctx, char *why, size_t whyn)
{
    return O.shadow_full || g_paged_off ? shadow_copy(name, retail, port, ctx, why, whyn)
                                        : shadow_paged(name, retail, port, ctx, why, whyn);
}
