/* rh_media.c — the fixed media pool: DirectDraw surface pixels and DirectSound buffer bytes.
 * Retail keeps pointers into both (Lock() results cached in globals: the back buffer's
 * lpSurface, Smacker's target), so they come from a reserved fixed-address region with a
 * first-fit span allocator: every address is a pure function of the allocation sequence. */
#include "rhost.h"

#define POOL_LO 0x60000000u
#define POOL_SZ 0x10000000u            /* 256 MB */
#define GRAN    0x1000u

typedef struct { uint32_t lo, sz; } span_t;
static span_t g_free[4096]; static int g_nfree;
static span_t g_live[4096]; static int g_nlive;
static uint32_t g_top = POOL_LO, g_committed = POOL_LO, g_used;
static CRITICAL_SECTION g_lk;

int rh_media_reserve(void)
{
    InitializeCriticalSection(&g_lk);
    return VirtualAlloc((void *)(uintptr_t)POOL_LO, POOL_SZ, MEM_RESERVE, PAGE_READWRITE) == (void *)(uintptr_t)POOL_LO ? 0 : -1;
}
int rh_media_in_pool(uint32_t v) { return v >= POOL_LO && v < POOL_LO + POOL_SZ; }
uint32_t rh_media_used(void) { return g_used; }

void *rh_media_alloc(uint32_t n)
{
    uint32_t sz = (n + GRAN - 1) & ~(GRAN - 1), p = 0;
    if (!sz) sz = GRAN;
    EnterCriticalSection(&g_lk);
    for (int i = 0; i < g_nfree; i++)
        if (g_free[i].sz >= sz) {
            p = g_free[i].lo; g_free[i].lo += sz; g_free[i].sz -= sz;
            if (!g_free[i].sz) { memmove(&g_free[i], &g_free[i + 1], (g_nfree - i - 1) * sizeof(span_t)); g_nfree--; }
            break;
        }
    if (!p) {
        if (g_top + sz > POOL_LO + POOL_SZ) rh_fatal("media pool exhausted (%u bytes)", n);
        p = g_top; g_top += sz;
        if (g_top > g_committed) {
            uint32_t to = (g_top + 0xfffffu) & ~0xfffffu;
            if (!VirtualAlloc((void *)(uintptr_t)g_committed, to - g_committed, MEM_COMMIT, PAGE_READWRITE))
                rh_fatal("media pool commit failed");
            g_committed = to;
        }
    }
    if (g_nlive == 4096) rh_fatal("media pool: too many live blocks");
    g_live[g_nlive].lo = p; g_live[g_nlive++].sz = sz;
    g_used += sz;
    LeaveCriticalSection(&g_lk);
    return memset((void *)(uintptr_t)p, 0, sz);
}

void rh_media_free(void *ptr)
{
    uint32_t p = (uint32_t)(uintptr_t)ptr;
    if (!ptr) return;
    EnterCriticalSection(&g_lk);
    for (int i = 0; i < g_nlive; i++)
        if (g_live[i].lo == p) {
            uint32_t sz = g_live[i].sz;
            g_live[i] = g_live[--g_nlive];
            g_used -= sz;
            int k = 0;
            while (k < g_nfree && g_free[k].lo < p) k++;
            if (g_nfree == 4096) break;
            memmove(&g_free[k + 1], &g_free[k], (g_nfree - k) * sizeof(span_t));
            g_free[k].lo = p; g_free[k].sz = sz; g_nfree++;
            if (k + 1 < g_nfree && g_free[k].lo + g_free[k].sz == g_free[k + 1].lo) {
                g_free[k].sz += g_free[k + 1].sz;
                memmove(&g_free[k + 1], &g_free[k + 2], (g_nfree - k - 2) * sizeof(span_t)); g_nfree--;
            }
            if (k > 0 && g_free[k - 1].lo + g_free[k - 1].sz == g_free[k].lo) {
                g_free[k - 1].sz += g_free[k].sz;
                memmove(&g_free[k], &g_free[k + 1], (g_nfree - k - 1) * sizeof(span_t)); g_nfree--;
            }
            break;
        }
    LeaveCriticalSection(&g_lk);
}

/* allocator meta (the shadow: a port run that calls back into retail code which allocates media must start from
 * the allocator state retail's run started from; rh_hooks_ai.c rh_port_run, like rh_heap_meta_*) */
typedef struct { int nf, nl; uint32_t top, used; span_t sp[]; } media_meta_t;
void *rh_media_meta_save(void)
{
    EnterCriticalSection(&g_lk);
    media_meta_t *m = malloc(sizeof *m + (size_t)(g_nfree + g_nlive) * sizeof(span_t));
    m->nf = g_nfree; m->nl = g_nlive; m->top = g_top; m->used = g_used;
    memcpy(m->sp, g_free, g_nfree * sizeof(span_t)); memcpy(m->sp + g_nfree, g_live, g_nlive * sizeof(span_t));
    LeaveCriticalSection(&g_lk);
    return m;
}
void rh_media_meta_restore(const void *p)   /* g_committed stays: pages stay committed */
{
    const media_meta_t *m = p;
    EnterCriticalSection(&g_lk);
    g_nfree = m->nf; g_nlive = m->nl; g_top = m->top; g_used = m->used;
    memcpy(g_free, m->sp, g_nfree * sizeof(span_t)); memcpy(g_live, m->sp + g_nfree, g_nlive * sizeof(span_t));
    LeaveCriticalSection(&g_lk);
}
