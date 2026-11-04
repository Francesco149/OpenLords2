/* rh_hook_smk.h — a movie row's shadow (the third host layer, --movies): smackw32 itself (rh_smack.c maps it at
 * 0x18000000, its CRT heap 0x19000000..0x1b000000, outside retail's hashed state). Unrolled, a port side that opens /
 * decodes / closes a movie after retail's run gets another heap block for the SMACK (DAT_004d4d58 differs), finds the
 * frame decoded or frees the movie twice. HOOK_SMK / HOOK_SMK_V snapshot smackw32's committed writable pages and every
 * live faked disk file's position at entry and put both back before the port side (from rh_hooks_rl_io.c; include it
 * once per table file, after RH_TABLE is defined). The spans retail's run decommitted are committed again, the heap's
 * bump pointer is put back (rh_smack_heap_top_set). Not covered: rh_smack's waveOut queue. */
#pragma once
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#define SMK_LO 0x18000000u
#define SMK_HI 0x1b000000u
typedef struct { uint32_t lo, n; } smk_span_t;
typedef struct smk_ctx { int nspan; smk_span_t span[64]; uint8_t *buf; size_t len; int nfh; HANDLE fh[32]; LONG fpos[32]; uint32_t top; } smk_ctx_t;
static int smk_writable(DWORD p) { return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY; }
static void smk_enter(smk_ctx_t *c)
{
    c->nspan = 0; c->len = 0; c->buf = NULL; c->nfh = 0; c->top = rh_smack_heap_top();
    for (uint32_t a = SMK_LO; a < SMK_HI; ) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) break;
        uint32_t lo = (uint32_t)(uintptr_t)mi.BaseAddress, hi = lo + (uint32_t)mi.RegionSize;
        if (hi > SMK_HI) hi = SMK_HI;
        if (mi.State == MEM_COMMIT && smk_writable(mi.Protect) && c->nspan < 64) {
            c->span[c->nspan].lo = a; c->span[c->nspan].n = hi - a; c->len += hi - a; c->nspan++;
        }
        if (hi <= a) break;
        a = hi;
    }
    if (!c->nspan) return;   /* smackw32 not mapped (no --movies) */
    c->buf = malloc(c->len);
    size_t o = 0;
    for (int i = 0; i < c->nspan; i++) { memcpy(c->buf + o, (void *)(uintptr_t)c->span[i].lo, c->span[i].n); o += c->span[i].n; }
    /* the live faked handles (rh_handles' table: { next, live, real[next] }) that are disk files */
    struct { uint32_t next; long live; HANDLE real[]; } *m = rh_handles_meta_save();
    for (uint32_t k = 0; k < m->next && c->nfh < 32; k++) {
        HANDLE h = m->real[k];
        if (!h || h == INVALID_HANDLE_VALUE || GetFileType(h) != FILE_TYPE_DISK) continue;
        LONG p = (LONG)SetFilePointer(h, 0, NULL, FILE_CURRENT);
        if (p == (LONG)INVALID_SET_FILE_POINTER) continue;
        c->fh[c->nfh] = h; c->fpos[c->nfh] = p; c->nfh++;
    }
    free(m);
}
static void smk_rewind(const smk_ctx_t *c)
{
    size_t o = 0;
    for (int i = 0; i < c->nspan; i++) {
        MEMORY_BASIC_INFORMATION mi;
        /* a span retail's run freed (SmackClose: smackw32's VirtualFree → decommit) is committed again first — else
         * the port side's smackw32 reads its rewound heap through decommitted pages (movie-skip: lom.smk) */
        if (VirtualQuery((void *)(uintptr_t)c->span[i].lo, &mi, sizeof mi) && mi.State != MEM_FREE) {
            if (mi.State != MEM_COMMIT || mi.RegionSize < c->span[i].n)
                VirtualAlloc((void *)(uintptr_t)c->span[i].lo, c->span[i].n, MEM_COMMIT, PAGE_READWRITE);
            memcpy((void *)(uintptr_t)c->span[i].lo, c->buf + o, c->span[i].n);
        }
        o += c->span[i].n;
    }
    for (int i = 0; i < c->nfh; i++) SetFilePointer(c->fh[i], c->fpos[i], NULL, FILE_BEGIN);
    if (c->nspan) rh_smack_heap_top_set(c->top);   /* the port side's new spans land where retail's did */
}
static smk_ctx_t *s_smk;
#define HOOK_SMK(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) \
    { (void)a; if (RH_TABLE[X_##NAME].mode == H_SHADOW && s_smk) smk_rewind(s_smk); return (uint32_t)(CALL); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        if (RH_TABLE[X_##NAME].mode != H_SHADOW || rh_in_shadow()) \
            return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
        smk_ctx_t *c = malloc(sizeof *c), *keep = s_smk; \
        smk_enter(c); s_smk = c; \
        uint32_t r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
        s_smk = keep; free(c->buf); free(c); \
        return r; \
    } \
    RH_ENTRY(w_##NAME, b_##NAME)

#define HOOK_SMK_V(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) \
    { (void)a; if (RH_TABLE[X_##NAME].mode == H_SHADOW && s_smk) smk_rewind(s_smk); CALL; return 0; } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        if (RH_TABLE[X_##NAME].mode != H_SHADOW || rh_in_shadow()) \
            return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 0, e0); \
        smk_ctx_t *c = malloc(sizeof *c), *keep = s_smk; \
        smk_enter(c); s_smk = c; \
        uint32_t r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 0, e0); \
        s_smk = keep; free(c->buf); free(c); \
        return r; \
    } \
    RH_ENTRY(w_##NAME, b_##NAME)
