/* rh_snap.c — in-process savestates for retail Lords2 (the live sessions' L2 level: tools/drive/).
 *
 * At a frame boundary everything retail's next frames depend on is in this process's deterministic regions —
 * retail's .data/.bss, the heap arena, the media pool (surface pixels, sound buffers), the fixed object page,
 * smackw32's image + heap (--movies), the used part of the fixed game stack — plus rhost's host-side layers
 * (allocator metadata, the virtual queue + window, DirectDraw / DirectSound serials and timers, the clock, the
 * fake-handle table with file positions, the run dir's game files) and the game coroutine's context
 * (rhost.c rh_game_ctx). A savestate is a copy of that; restoring it and running on gives the state a straight
 * replay reaches (the gate: `rhost.py snap-check`).
 *
 * No write tracking (openrecet's rh_snap.c write-protects the state and copies pages on their first write: the
 * fault's exception frame lands on the game stack, which retail's residue readers then see — Lords2 has several,
 * the RH_SH_FULL rows). The state is small (~6 MB), so a take compares every tracked page with the parent
 * savestate's copy (shared when equal, an all-zero page costs nothing) and a restore compares every page with the
 * target's and copies the ones that differ: both O(state) reads (~0.5 ms), no faults, exactly transparent.
 *
 * Commit state is part of a savestate: arena / smack-heap pages committed now but not then are decommitted, media
 * and stack pages not in the savestate are zeroed (as they were before their first use). */
#include "rhost.h"

#define PGSZ 4096u
#define POOL_LO 0x60000000u
#define POOL_HI 0x70000000u
#define FIXED_LO 0x5f080000u
#define FIXED_HI 0x5f090000u
#define STACK_LO 0x58010000u   /* MAIN_STACK_LO + STACK_GUARD */
#define STACK_HI 0x59000000u
#define SMK_LO 0x18000000u
#define SMK_HI 0x1b000000u
enum { K_FIXED = 0, K_DECOMMIT = 1, K_ZERO = 2 };

/* ---- slab pool: 4 KB page copies, refcounted ---- */
#define CHUNK_SLABS 4096u                /* 16 MB chunks */
#define MAX_CHUNKS 256u
static uint8_t *g_chunk[MAX_CHUNKS]; static uint16_t *g_ref[MAX_CHUNKS]; static uint32_t g_nchunk;
static uint32_t *g_free; static uint32_t g_nfree, g_capfree, g_live_slabs, g_slab_cap;
static const uint8_t g_zero[PGSZ];
static uint8_t *slab_ptr(uint32_t h) { uint32_t i = h - 1; return g_chunk[i / CHUNK_SLABS] + (size_t)(i % CHUNK_SLABS) * PGSZ; }
static const uint8_t *hptr(uint32_t h) { return h ? slab_ptr(h) : g_zero; }
static uint32_t slab_new(void)           /* → handle (index + 1), 0 = pool exhausted */
{
    if (!g_slab_cap) { char e[16] = ""; g_slab_cap = (GetEnvironmentVariableA("RH_SNAP_MB", e, sizeof e) ? (uint32_t)atoi(e) : 1024u) * 256u; }
    if (!g_nfree) {
        if (g_nchunk >= MAX_CHUNKS || g_nchunk * CHUNK_SLABS >= g_slab_cap) return 0;
        uint8_t *c = VirtualAlloc(NULL, CHUNK_SLABS * PGSZ, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!c) return 0;
        g_chunk[g_nchunk] = c; g_ref[g_nchunk] = calloc(CHUNK_SLABS, sizeof(uint16_t));
        if (g_capfree < (g_nchunk + 1) * CHUNK_SLABS) { g_capfree = (g_nchunk + 1) * CHUNK_SLABS; g_free = realloc(g_free, g_capfree * 4); }
        for (uint32_t k = CHUNK_SLABS; k-- > 0; ) g_free[g_nfree++] = g_nchunk * CHUNK_SLABS + k;
        g_nchunk++;
    }
    uint32_t i = g_free[--g_nfree];
    g_ref[i / CHUNK_SLABS][i % CHUNK_SLABS] = 1; g_live_slabs++;
    return i + 1;
}
static void slab_ref(uint32_t h) { if (h) g_ref[(h - 1) / CHUNK_SLABS][(h - 1) % CHUNK_SLABS]++; }
static void slab_unref(uint32_t h)
{
    if (!h) return;
    uint32_t i = h - 1;
    if (--g_ref[i / CHUNK_SLABS][i % CHUNK_SLABS] == 0) { g_free[g_nfree++] = i; g_live_slabs--; }
}

/* ---- host state: plain blocks registered by the modules ---- */
#define MAX_BLK 32
static struct { const char *name; void *p; size_t n; } g_blk[MAX_BLK]; static int g_nblk; static size_t g_blk_total;
void rh_snap_register(const char *name, void *p, size_t n)
{
    if (g_nblk >= MAX_BLK) rh_fatal("rh_snap: too many host state blocks");
    g_blk[g_nblk].name = name; g_blk[g_nblk].p = p; g_blk[g_nblk].n = n; g_nblk++; g_blk_total += n;
}

/* ---- the tracked pages now: sorted page numbers + kind ---- */
typedef struct { uint32_t *pg; uint8_t *kind; uint32_t n, cap; } plist_t;
static void pl_add(plist_t *l, uint32_t lo, uint32_t hi, int kind)
{
    for (uint32_t a = lo & ~(PGSZ - 1); a < hi; a += PGSZ) {
        if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 4096; l->pg = realloc(l->pg, l->cap * 4); l->kind = realloc(l->kind, l->cap); }
        if (l->n && l->pg[l->n - 1] >= a >> 12) continue;      /* overlap / order guard */
        l->pg[l->n] = a >> 12; l->kind[l->n] = (uint8_t)kind; l->n++;
    }
}
static void pl_walk(plist_t *l, uint32_t lo, uint32_t hi, int kind, int writable_only)
{
    for (uint32_t a = lo; a < hi; ) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) break;
        uint32_t b = (uint32_t)(uintptr_t)mi.BaseAddress, e = b + (uint32_t)mi.RegionSize;
        if (e <= a) break;
        if (e > hi) e = hi;
        DWORD p = mi.Protect & 0xff;
        int w = p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
        if (mi.State == MEM_COMMIT && (w || !writable_only)) pl_add(l, a, e, kind);
        a = e;
    }
}
static uint32_t g_lw;                 /* lowest game-stack page ever seen in use */
static int page_nonzero(uint32_t a) { const uint64_t *q = (const uint64_t *)(uintptr_t)a; for (uint32_t i = 0; i < PGSZ / 8; i++) if (q[i]) return 1; return 0; }
static uint32_t stack_low(void)
{
    uint32_t gesp = rh_game_esp() & ~(PGSZ - 1);
    if (!g_lw) {                      /* first: the whole stack */
        for (uint32_t a = STACK_LO; a < STACK_HI; a += PGSZ) if (page_nonzero(a)) { g_lw = a; break; }
        if (!g_lw) g_lw = gesp;
    }
    if (gesp < g_lw) g_lw = gesp;
    /* the frames since may have gone deeper: 1 MB below the low water (a run of zero pages is possible in a big zeroed local) */
    for (uint32_t a = g_lw >= STACK_LO + 0x100000u ? g_lw - 0x100000u : STACK_LO; a < g_lw; a += PGSZ)
        if (page_nonzero(a)) { g_lw = a; break; }
    return g_lw;
}
static void tracked(plist_t *l)
{
    static uint32_t r[4096][2];
    l->n = 0;
    pl_add(l, RT_DATA_LO, RT_DATA_HI, K_FIXED);
    if (O.movies) pl_walk(l, SMK_LO, SMK_HI, K_DECOMMIT, 1);
    int n = rh_heap_ranges(r, 4096);
    for (int i = 0; i < n; i++) pl_add(l, r[i][0], r[i][1], K_DECOMMIT);
    pl_add(l, stack_low(), STACK_HI, K_ZERO);
    pl_add(l, FIXED_LO, FIXED_HI, K_FIXED);
    pl_walk(l, POOL_LO, POOL_HI, K_ZERO, 0);
}

/* ---- savestates ---- */
typedef struct snap {
    int id; uint32_t n, *pg, *h; uint8_t *kind;
    uint8_t *host; uint32_t clock[3];
    void *heap, *media, *ds, *dd, *win, *handles, *files;
    uint32_t smk_top, frame;
} snap_t;
#define MAX_SNAP 65536
static snap_t **g_snap; static int g_next_id = 1, g_live_snaps; static snap_t *g_cur;
static uint64_t g_takes, g_restores, g_copied, g_decommits, g_commits;
static double g_take_ms, g_restore_ms;
static plist_t g_pl;
static double now_ms(void) { static LARGE_INTEGER f; LARGE_INTEGER t; if (!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return (double)t.QuadPart * 1000.0 / (double)f.QuadPart; }

static void snap_free_one(snap_t *s)
{
    for (uint32_t i = 0; i < s->n; i++) slab_unref(s->h[i]);
    free(s->pg); free(s->h); free(s->kind); free(s->host);
    rh_heap_meta_free(s->heap); free(s->media); free(s->ds); free(s->dd); free(s->win); free(s->handles); rh_files_snap_free(s->files);
    g_snap[s->id] = NULL; g_live_snaps--;
    if (g_cur == s) g_cur = NULL;
    free(s);
}
void rh_snap_free(int id) { if (id > 0 && id < MAX_SNAP && g_snap && g_snap[id]) snap_free_one(g_snap[id]); }
void rh_snap_free_all(void) { if (g_snap) for (int i = 1; i < MAX_SNAP; i++) if (g_snap[i]) snap_free_one(g_snap[i]); }

int rh_snap_take(void)
{
    double t0 = now_ms();
    if (!g_snap) g_snap = calloc(MAX_SNAP, sizeof *g_snap);
    int id = 0;
    for (int k = 0; k < MAX_SNAP - 1; k++) { int c = g_next_id; g_next_id = g_next_id + 1 < MAX_SNAP ? g_next_id + 1 : 1; if (!g_snap[c]) { id = c; break; } }
    if (!id) return 0;
    tracked(&g_pl);
    snap_t *s = calloc(1, sizeof *s), *par = g_cur;
    s->id = id; s->n = g_pl.n; s->frame = g_frame;
    s->pg = malloc((s->n ? s->n : 1) * 4); s->h = malloc((s->n ? s->n : 1) * 4); s->kind = malloc(s->n ? s->n : 1);
    memcpy(s->pg, g_pl.pg, s->n * 4); memcpy(s->kind, g_pl.kind, s->n);
    uint32_t j = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        uint32_t pg = s->pg[i];
        const uint8_t *cur = (const uint8_t *)(uintptr_t)(pg << 12);
        while (par && j < par->n && par->pg[j] < pg) j++;
        if (par && j < par->n && par->pg[j] == pg && !memcmp(cur, hptr(par->h[j]), PGSZ)) { s->h[i] = par->h[j]; slab_ref(s->h[i]); continue; }
        if (!page_nonzero(pg << 12)) { s->h[i] = 0; continue; }
        uint32_t h = slab_new();
        if (!h) { for (uint32_t k = 0; k < i; k++) slab_unref(s->h[k]); free(s->pg); free(s->h); free(s->kind); free(s); return 0; }
        memcpy(slab_ptr(h), cur, PGSZ);
        s->h[i] = h;
    }
    s->host = malloc(g_blk_total ? g_blk_total : 1);
    size_t o = 0;
    for (int i = 0; i < g_nblk; i++) { memcpy(s->host + o, g_blk[i].p, g_blk[i].n); o += g_blk[i].n; }
    rh_clock_save(s->clock);
    s->heap = rh_heap_meta_save(); s->media = rh_media_meta_save(); s->ds = rh_ds_meta_save(); s->dd = rh_dd_meta_save();
    s->win = rh_win_meta_save(); s->handles = rh_handles_snap(); s->files = rh_files_snap();
    s->smk_top = O.movies ? rh_smack_heap_top() : 0;
    g_snap[id] = s; g_live_snaps++; g_cur = s; g_takes++;
    g_take_ms += now_ms() - t0;
    return id;
}

int rh_snap_restore(int id)
{
    if (!g_snap || id <= 0 || id >= MAX_SNAP || !g_snap[id]) return -1;
    double t0 = now_ms();
    snap_t *s = g_snap[id];
    tracked(&g_pl);
    uint32_t i = 0, j = 0;                                   /* i: now, j: the savestate */
    while (i < g_pl.n || j < s->n) {
        uint32_t pn = i < g_pl.n ? g_pl.pg[i] : 0xffffffffu, ps = j < s->n ? s->pg[j] : 0xffffffffu;
        void *a;
        if (pn < ps) {                                       /* tracked now, not then */
            a = (void *)(uintptr_t)(pn << 12);
            if (g_pl.kind[i] == K_DECOMMIT) { VirtualFree(a, PGSZ, MEM_DECOMMIT); g_decommits++; }
            else if (g_pl.kind[i] == K_ZERO && page_nonzero(pn << 12)) { memset(a, 0, PGSZ); g_copied++; }
            i++;
            continue;
        }
        a = (void *)(uintptr_t)(ps << 12);
        if (ps < pn) {                                       /* then, not now: commit it */
            if (!VirtualAlloc(a, PGSZ, MEM_COMMIT, PAGE_READWRITE)) rh_fatal("rh_snap: commit %08x failed (err %lu)", ps << 12, GetLastError());
            g_commits++;
            memcpy(a, hptr(s->h[j]), PGSZ); g_copied++;
            j++;
            continue;
        }
        if (memcmp(a, hptr(s->h[j]), PGSZ)) { memcpy(a, hptr(s->h[j]), PGSZ); g_copied++; }
        i++; j++;
    }
    size_t o = 0;
    for (int k = 0; k < g_nblk; k++) { memcpy(g_blk[k].p, s->host + o, g_blk[k].n); o += g_blk[k].n; }
    rh_clock_restore(s->clock);
    rh_heap_meta_restore_snap(s->heap); rh_media_meta_restore(s->media); rh_ds_meta_restore(s->ds); rh_dd_meta_restore(s->dd);
    rh_win_meta_restore(s->win); rh_handles_unsnap(s->handles); rh_files_unsnap(s->files);
    if (O.movies) rh_smack_heap_top_set(s->smk_top);
    g_cur = s; g_restores++;
    g_restore_ms += now_ms() - t0;
    return 0;
}

/* dwords that differ between savestate ID and memory now, addresses in [lo, hi) (mark → act → diff) */
long rh_snap_diff(int id, uint32_t lo, uint32_t hi, void (*cb)(uint32_t va, uint32_t was, uint32_t now, void *u), void *u)
{
    if (!g_snap || id <= 0 || id >= MAX_SNAP || !g_snap[id]) return -1;
    const snap_t *s = g_snap[id];
    tracked(&g_pl);
    long n = 0; uint32_t j = 0;
    for (uint32_t i = 0; i < g_pl.n; i++) {
        uint32_t pg = g_pl.pg[i], base = pg << 12;
        if (base + PGSZ <= lo || base >= hi) continue;
        if (g_pl.kind[i] == K_ZERO && base >= STACK_LO && base < STACK_HI) continue;   /* the stack: noise */
        while (j < s->n && s->pg[j] < pg) j++;
        const uint32_t *was = (const uint32_t *)(j < s->n && s->pg[j] == pg ? hptr(s->h[j]) : g_zero), *now = (const uint32_t *)(uintptr_t)base;
        if (!memcmp(was, now, PGSZ)) continue;
        for (uint32_t k = 0; k < PGSZ / 4; k++)
            if (was[k] != now[k] && base + k * 4 >= lo && base + k * 4 < hi) { n++; if (cb) cb(base + k * 4, was[k], now[k], u); }
    }
    return n;
}
int rh_snap_frame(int id) { return g_snap && id > 0 && id < MAX_SNAP && g_snap[id] ? (int)g_snap[id]->frame : -1; }
int rh_snap_live(void) { return g_live_snaps; }

void rh_snap_stats(char *b, size_t n)
{
    snprintf(b, n, "snap: pages=%u takes=%llu (%.3f ms avg) restores=%llu (%.3f ms avg) live=%d slabs=%u (%u MB) copied=%llu commits=%llu decommits=%llu stack_lw=%08x",
             g_pl.n, (unsigned long long)g_takes, g_takes ? g_take_ms / (double)g_takes : 0.0, (unsigned long long)g_restores,
             g_restores ? g_restore_ms / (double)g_restores : 0.0, g_live_snaps, g_live_slabs, g_live_slabs >> 8,
             (unsigned long long)g_copied, (unsigned long long)g_commits, (unsigned long long)g_decommits, g_lw);
}
