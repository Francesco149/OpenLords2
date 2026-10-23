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
int rh_shadow_ex(const char *name, void (*retail)(void *), void (*port)(void *), void *ctx, char *why, size_t whyn)
{
    static uint32_t r[MAXR][2], r2[MAXR][2];
    int n = state_ranges(r);
    size_t tot = 0;
    for (int i = 0; i < n; i++) tot += r[i][1] - r[i][0];
    if (tot > g_cap) { free(g_pre); free(g_ret); g_cap = tot + (16u << 20); g_pre = malloc(g_cap); g_ret = malloc(g_cap); }
    #define COPY_OUT(buf) { size_t o = 0; for (int i = 0; i < n; i++) { size_t l = r[i][1] - r[i][0]; memcpy((buf) + o, (void *)(uintptr_t)r[i][0], l); o += l; } }
    #define COPY_IN(buf)  { size_t o = 0; for (int i = 0; i < n; i++) { size_t l = r[i][1] - r[i][0]; memcpy((void *)(uintptr_t)r[i][0], (buf) + o, l); o += l; } }
    uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
    uint32_t clk0[3], clk1[3]; rh_clock_save(clk0);
    void *meta = rh_heap_meta_save();
    COPY_OUT(g_pre);
    g_shadow_depth++;
    retail(ctx);
    g_shadow_depth--;
    int n2 = state_ranges(r2);
    if (n2 != n || memcmp(r, r2, n * 8)) {
        snprintf(why, whyn, "%s: arena commit changed during call (ranges %d->%d); not compared", name, n, n2);
        rh_heap_meta_free(meta);
        return -1;
    }
    COPY_OUT(g_ret);
    void *meta_ret = rh_heap_meta_save();
    rh_clock_save(clk1);
    COPY_IN(g_pre);
    rh_heap_meta_restore(meta);
    rh_clock_restore(clk0);
    __asm__ volatile("fldcw %0" : : "m"(cw));
    g_shadow_depth++;
    port(ctx);
    g_shadow_depth--;
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
    if (diff) { COPY_IN(g_ret); if (w < (int)whyn - 24) snprintf(why + w, whyn - w, " (%ld differ)", diff); }
    rh_heap_meta_restore(meta_ret);
    rh_clock_restore(clk1);
    __asm__ volatile("fldcw %0" : : "m"(cw));
    rh_heap_meta_free(meta); rh_heap_meta_free(meta_ret);
    return diff ? 1 : 0;
    #undef COPY_OUT
    #undef COPY_IN
}
