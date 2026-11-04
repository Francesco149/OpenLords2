/* rh_hooks_rl_io.c — a retail-layout port, partition 'io' (src/rl_io.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). Findings: docs/findings/rl-io.md.
 *
 * BINDING.  The partition's globals (src/rl_io_globals.h, generated; rl_io_globals.c is not linked) are absolute
 * symbols at their retail VAs (rh_rl_io_alias.h). The platform shim (src/rl_io_plat.c) is bound at load to retail's
 * own IAT slots (rhost's overrides: files, handles, heap arena, DirectSound, the virtual clock, smackw32) and
 * retail's own CRT (the fd table, errno, the debug heap, time's buffers): a port function inside rhost reaches the
 * platform exactly the way retail's code does. */
#include "rh_port.h"
#include "rl.h"
#include "rl_io_globals.h"
#include "rl_io_plat.h"
#include "rh_rl_io_alias.h"

__asm__(RH_RL_IO_ALIASES);

static void __attribute__((constructor)) rl_io_bind(void) { rl_plat_bind_retail(); }

#define RH_TABLE RH_HOOKS_RL_IO
extern hook_t RH_HOOKS_RL_IO[];
/* row indices (must follow the table's order) */
enum {
    X_rl_41b013, X_rl_426529, X_rl_45280b, X_rl_4b0aaa, X_rl_4ae2ab, X_rl_46ea9a, X_rl_46f21f, X_rl_46f267,
    X_rl_4983b7, X_rl_4990f9, X_rl_499261, X_rl_499619, X_rl_433cc1, X_rl_451f70, X_rl_4b11ce, X_rl_497500,
    X_rl_4975d3, X_rl_49c995, X_rl_4978ad, X_rl_4ae2bd, X_rl_42d91b,
    X_rl_4c05a0, X_rl_4c0580, X_rl_49915e, X_rl_499366, X_rl_49964c, X_rl_4996c8,
    X_rl_4af116, X_rl_4af312, X_rl_4af767, X_rl_4af841, X_rl_4aee3e, X_rl_4aecf0, X_rl_4ae1b8, X_rl_4b2575,
    X_rl_4b049b, X_rl_4afe3a, X_rl_4afab9,
    X_rl_4b92e0, X_rl_4b95ba, X_rl_4b9652, X_rl_4b97f9, X_rl_4b984f, X_rl_427c9b, X_rl_427990, X_rl_4262eb, X_rl_426360, X_rl_426216, X_rl_425e4b,
    X_rl_4b36c0, X_rl_4b3714, X_rl_4b3940, X_rl_4b3b92, X_rl_4b37bc, X_rl_4b3acd, X_rl_426534, X_rl_426b92, X_rl_4263ad, X_rl_426dd5, X_rl_499d7d, X_rl_499aca, X_rl_42e320, X_rl_42e298, X_rl_42f58c, X_rl_42e362, X_rl_42dbc7, X_rl_42da18, X_rl_46a101, X_rl_497a34, X_rl_4b8c3a,
};

/* TWO THINGS THE SHADOW DOES NOT ROLL BACK, which the WAV rows touch (HOOK_IO; a shared fix belongs in rh_state /
 * rh_handles' meta):
 *  - an mmio file's read position (a host layer). A row reading a file opened BEFORE the call (the music stream, a
 *    cue's file inside 0x4b984f) would have the port read on where retail's run stopped (the next bytes; EOF: error 3)
 *    and leave the file read twice (the stream ends early: the run leaves the corpus). HOOK_IO notes the file's logical
 *    position at entry (mmioSeek SEEK_CUR through retail's own import: rh_handles' fake → real) and seeks back to it
 *    before the port side;
 *  - the CALLER'S STACK behind a pointer argument (the stack is not state): 0x4b9652 counts a caller's MMCKINFO
 *    down (the port would see retail's count: read nothing), 0x4b984f / 0x4b92e0 / 0x4b95ba write the caller's
 *    locals. HOOK_IO saves each listed argument's bytes at entry, puts them back before the port side (after noting
 *    retail's results) and COMPARES the port's results with retail's: a mismatch is a divergence (the port's return
 *    value is flipped, the bytes logged), so these outputs are verified like state.
 * Only in shadow mode (the port side runs right after retail's, on the same entry); replace mode is untouched. */
#define RL_STACK_LO 0x58000000u   /* rhost's fixed stack (README: 0x58000000..0x59000000) */
#define RL_STACK_HI 0x59000000u
static LONG mmio_seek(uint32_t h, LONG off, int org) { return ((LONG (WINAPI *)(uint32_t, LONG, int))G32(0x5cf5d0u))(h, off, org); }
typedef struct io_ctx {
    uint32_t h; LONG pos;                          /* the mmio file and its position at entry */
    int n; uint32_t p[4], len[4]; uint8_t pre[4][24], post[4][24];
} io_ctx_t;
static io_ctx_t *s_io;
static void io_enter(io_ctx_t *c, uint32_t e0, uint32_t h, const uint8_t *spec)
{
    c->h = h;
    c->pos = h ? mmio_seek(h, 0, SEEK_CUR) : -1;
    c->n = 0;
    for (; spec[0] != 0xff; spec += 2) {
        uint32_t ptr = G32(e0 + 4u + 4u * spec[0]);
        if (ptr < RL_STACK_LO || ptr + spec[1] > RL_STACK_HI || c->n == 4)
            continue;   /* a .data / arena target is state: the shadow compares it */
        c->p[c->n] = ptr; c->len[c->n] = spec[1];
        memcpy(c->pre[c->n], (const void *)(uintptr_t)ptr, spec[1]);
        c->n++;
    }
}
static void io_port_pre(int row)   /* the port side of a shadowed call: back to where retail's side started */
{
    io_ctx_t *c = s_io;
    if (RH_TABLE[row].mode != H_SHADOW || !c) return;
    if (c->h && c->pos != -1) mmio_seek(c->h, c->pos, SEEK_SET);
    for (int k = 0; k < c->n; k++) {
        memcpy(c->post[k], (const void *)(uintptr_t)c->p[k], c->len[k]);
        memcpy((void *)(uintptr_t)c->p[k], c->pre[k], c->len[k]);
    }
}
static uint32_t io_port_post(int row, uint32_t r)
{
    io_ctx_t *c = s_io;
    if (RH_TABLE[row].mode != H_SHADOW || !c) return r;
    for (int k = 0; k < c->n; k++)
        if (memcmp(c->post[k], (const void *)(uintptr_t)c->p[k], c->len[k])) {
            const uint32_t *x = (const uint32_t *)c->post[k], *y = (const uint32_t *)(uintptr_t)c->p[k];
            rh_log("DIVERGE-STACK %s f%u: arg@%08x (%u B) r%08x %08x/p%08x %08x\n", RH_TABLE[row].name, g_frame, c->p[k],
                   c->len[k], x[0], c->len[k] > 4 ? x[1] : 0, y[0], c->len[k] > 4 ? y[1] : 0);
            return r ^ 0x80000001u;   /* reported through the compared return value */
        }
    return r;
}
/* HANDLE: the mmio file the row reads (0: none); SA: { arg index, bytes, ..., 0xff } — the pointer args that may
 * point into the caller's stack */
#define HOOK_IO(NAME, N, HANDLE, SA, CALL) \
    static const uint8_t k_sa_##NAME[] = SA; \
    static uint32_t p_##NAME(const uint32_t *a) \
    { (void)a; io_port_pre(X_##NAME); uint32_t r_ = (uint32_t)(CALL); return io_port_post(X_##NAME, r_); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        io_ctx_t c, *keep = s_io; \
        io_enter(&c, e0, (HANDLE), k_sa_##NAME); \
        s_io = &c; \
        uint32_t r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
        s_io = keep; \
        return r; \
    } \
    RH_ENTRY(w_##NAME, b_##NAME)
#define SA(...) { __VA_ARGS__, 0xff }
#define SA_NONE { 0xff }
/* the music streamer FUN_00426dd5 is winmm's __stdcall TIMECALLBACK (5 arguments, no call site: its entry is patched):
 * RH_ENTRY with `ret $0x14` (rh_call_at runs retail's stdcall at e0 and restores esp itself) */
#define RH_ENTRY_STD20(W, B) \
    static __attribute__((naked)) void W(void) \
    { __asm__("movl %esp, %eax\n\tleal -0x40000(%eax), %edx\n\tmovl _rh_c_floor, %ecx\n\t" \
              "testl %ecx, %ecx\n\tjz 1f\n\tsubl $0x400, %ecx\n\tcmpl %ecx, %edx\n\tjb 1f\n\tmovl %ecx, %edx\n" \
              "1:\tmovl %edx, %esp\n\tpushl %eax\n\tpushl %eax\n\t" \
              "call _" #B "\n\taddl $4, %esp\n\tpopl %esp\n\tret $0x14"); }
#define HOOK_IO_CB(NAME, HANDLE, CALL) \
    static const uint8_t k_sa_##NAME[] = SA_NONE; \
    static uint32_t p_##NAME(const uint32_t *a) \
    { (void)a; io_port_pre(X_##NAME); uint32_t r_ = (uint32_t)(CALL); return io_port_post(X_##NAME, r_); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        io_ctx_t c, *keep = s_io; \
        io_enter(&c, e0, (HANDLE), k_sa_##NAME); \
        s_io = &c; \
        uint32_t r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
        s_io = keep; \
        return r; \
    } \
    RH_ENTRY_STD20(w_##NAME, b_##NAME)
/* A THIRD HOST LAYER (--movies): smackw32 itself (rh_smack.c maps it at 0x18000000 with its CRT heap at
 * 0x19000000..0x1b000000, outside retail's hashed state). A movie row's retail side opens / decodes / closes the movie
 * in it; unrolled, the port side would get another heap block for the SMACK (the handle differs), find the frame
 * already decoded (SmackWait: wait) or free the movie a second time (a crash in smackw32). HOOK_SMK snapshots
 * smackw32's committed writable pages and the position of every live faked file handle (its .smk) at entry and
 * puts both back before the port side. (Not covered: rh_smack's waveOut queue and its span bump pointer — statics;
 * a call that grows smackw32's heap by a new span would still differ.) */
#define SMK_LO 0x18000000u
#define SMK_HI 0x1b000000u
typedef struct { uint32_t lo, n; } smk_span_t;
typedef struct smk_ctx { int nspan; smk_span_t span[64]; uint8_t *buf; size_t len; int nfh; HANDLE fh[32]; LONG fpos[32]; } smk_ctx_t;
static int smk_writable(DWORD p) { return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY; }
static void smk_enter(smk_ctx_t *c)
{
    c->nspan = 0; c->len = 0; c->buf = NULL; c->nfh = 0;
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
        if (VirtualQuery((void *)(uintptr_t)c->span[i].lo, &mi, sizeof mi) && mi.State == MEM_COMMIT)
            memcpy((void *)(uintptr_t)c->span[i].lo, c->buf + o, c->span[i].n);
        o += c->span[i].n;
    }
    for (int i = 0; i < c->nfh; i++) SetFilePointer(c->fh[i], c->fpos[i], NULL, FILE_BEGIN);
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

/* the stream record's HMMIO: DAT_00522bb4 + 84 dwUser (dwUser = the callback's 3rd argument) */
#define STREAM_HMMIO(e0) G32(0x522bb4u + 84u * G32((e0) + 12u))

/* the init / reset stores */
HOOK_V(rl_41b013, 0, rl_41b013())
HOOK_V(rl_426529, 0, rl_426529())
HOOK_V(rl_45280b, 0, rl_45280b())
HOOK_V(rl_4b0aaa, 1, rl_4b0aaa(I(0)))
HOOK_V(rl_4ae2ab, 1, rl_4ae2ab(I(0)))
HOOK_R(rl_46ea9a, 1, rl_46ea9a(I(0)))
HOOK_R(rl_46f21f, 0, rl_46f21f())
HOOK_R(rl_46f267, 0, rl_46f267())
HOOK_V(rl_4983b7, 0, rl_4983b7())
HOOK_V(rl_4990f9, 0, rl_4990f9())
HOOK_V(rl_499261, 0, rl_499261())
HOOK_V(rl_499619, 0, rl_499619())
HOOK_R(rl_433cc1, 0, rl_433cc1())
HOOK_R(rl_451f70, 0, rl_451f70())
HOOK_R(rl_4b11ce, 0, rl_4b11ce())
HOOK_R(rl_497500, 0, rl_497500())
HOOK_R(rl_4975d3, 0, rl_4975d3())
HOOK_R(rl_49c995, 0, rl_49c995())
HOOK_R(rl_4978ad, 0, rl_4978ad())
HOOK_V(rl_4ae2bd, 0, rl_4ae2bd())
HOOK_SMK(rl_42d91b, 5, rl_42d91b(I(0), I(1), I(2), I(3), I(4)))

HOOK_V(rl_4c05a0, 2, rl_4c05a0(I(0), I(1)))
HOOK_V(rl_4c0580, 1, rl_4c0580(I(0)))
HOOK_V(rl_49915e, 0, rl_49915e())
HOOK_V(rl_499366, 0, rl_499366())
HOOK_V(rl_49964c, 0, rl_49964c())
HOOK_R(rl_4996c8, 0, rl_4996c8())

HOOK_R(rl_4af116, 1, rl_4af116(I(0)))
HOOK_R(rl_4af312, 1, rl_4af312(I(0)))
HOOK_R(rl_4af767, 1, rl_4af767(I(0)))
HOOK_R(rl_4af841, 1, rl_4af841(I(0)))
HOOK_R(rl_4aee3e, 0, rl_4aee3e())
HOOK_R(rl_4aecf0, 2, rl_4aecf0(I(0), I(1)))
HOOK_V(rl_4ae1b8, 0, rl_4ae1b8())   /* its EAX passes 0x4ae2ab's through: no caller reads it */
HOOK_R(rl_4b2575, 0, rl_4b2575())

HOOK_R(rl_4b049b, 0, rl_4b049b())
HOOK_R(rl_4afe3a, 1, rl_4afe3a(I(0)))
HOOK_R(rl_4afab9, 3, rl_4afab9(I(0), I(1), I(2)))

HOOK_IO(rl_4b92e0, 4, 0, SA(1, 4, 2, 4, 3, 20), rl_4b92e0(I(0), I(1), I(2), I(3)))   /* *phmmio, *pwfx, the RIFF MMCKINFO */
HOOK_IO(rl_4b95ba, 3, 0, SA(0, 4, 1, 20, 2, 20), rl_4b95ba(I(0), I(1), I(2)))   /* *phmmio, the data / RIFF MMCKINFOs (it seeks absolutely) */
HOOK_IO(rl_4b9652, 5, G32(e0 + 4u), SA(3, 20, 4, 4), rl_4b9652(I(0), I(1), I(2), I(3), I(4)))   /* reads arg 0's file; the 'data' MMCKINFO, *got */
HOOK_IO(rl_4b97f9, 2, 0, SA(0, 4, 1, 4), rl_4b97f9(I(0), I(1)))
HOOK_IO(rl_4b984f, 4, 0, SA(1, 4, 2, 4, 3, 4), rl_4b984f(I(0), I(1), I(2), I(3)))   /* *psize, *pwfx, *pdata */
HOOK_R(rl_427c9b, 0, rl_427c9b())
HOOK_R(rl_427990, 3, rl_427990(I(0), I(1), I(2)))
HOOK_V(rl_4262eb, 1, rl_4262eb(I(0)))
HOOK_V(rl_426360, 1, rl_426360(I(0)))
HOOK_R(rl_426216, 1, rl_426216(I(0)))
HOOK_R(rl_425e4b, 2, rl_425e4b(I(0), I(1)))

HOOK_V(rl_4b36c0, 1, rl_4b36c0(I(0)))
HOOK_V(rl_4b3714, 1, rl_4b3714(I(0)))
HOOK_V(rl_4b3940, 1, rl_4b3940(I(0)))
HOOK_V(rl_4b3b92, 1, rl_4b3b92(I(0)))
HOOK_V(rl_4b37bc, 0, rl_4b37bc())
HOOK_R(rl_4b3acd, 1, rl_4b3acd(I(0)))
HOOK_R(rl_426534, 2, rl_426534(I(0), I(1)))
HOOK_R(rl_426b92, 1, rl_426b92(I(0)))
HOOK_R(rl_4263ad, 3, rl_4263ad(I(0), I(1), I(2)))
HOOK_IO_CB(rl_426dd5, STREAM_HMMIO(e0), rl_426dd5(I(0), I(1), I(2), I(3), I(4)))   /* stdcall; reads the stream's file */
HOOK_R(rl_499d7d, 0, rl_499d7d())
HOOK_R(rl_499aca, 0, rl_499aca())
HOOK_SMK(rl_42e320, 0, rl_42e320())
HOOK_R(rl_42e298, 0, rl_42e298())
HOOK_R(rl_42f58c, 2, rl_42f58c(I(0), I(1)))
HOOK_R(rl_42e362, 0, rl_42e362())
HOOK_SMK(rl_42dbc7, 1, rl_42dbc7(I(0)))
HOOK_SMK(rl_42da18, 4, rl_42da18(I(0), I(1), I(2), I(3)))
HOOK_V(rl_46a101, 0, rl_46a101())
HOOK_R(rl_497a34, 1, rl_497a34(I(0)))
HOOK_R(rl_4b8c3a, 0, rl_4b8c3a())

hook_t RH_HOOKS_RL_IO[] = {
    { "rl_41b013", 0x41b013u, (void *)w_rl_41b013, 0, 0, 0, 0, "" },
    { "rl_426529", 0x426529u, (void *)w_rl_426529, 0, 0, 0, 0, "" },
    { "rl_45280b", 0x45280bu, (void *)w_rl_45280b, 0, 0, 0, 0, "" },
    { "rl_4b0aaa", 0x4b0aaau, (void *)w_rl_4b0aaa, 0, 0, 0, 0, "" },
    { "rl_4ae2ab", 0x4ae2abu, (void *)w_rl_4ae2ab, 0, 0, 0, 0, "" },
    { "rl_46ea9a", 0x46ea9au, (void *)w_rl_46ea9a, 0, 0, 0, 0, "" },
    { "rl_46f21f", 0x46f21fu, (void *)w_rl_46f21f, 0, 0, 0, 0, "" },
    { "rl_46f267", 0x46f267u, (void *)w_rl_46f267, 0, 0, 0, 0, "" },
    { "rl_4983b7", 0x4983b7u, (void *)w_rl_4983b7, 0, 0, 0, 0, "" },
    { "rl_4990f9", 0x4990f9u, (void *)w_rl_4990f9, 0, 0, 0, 0, "" },
    { "rl_499261", 0x499261u, (void *)w_rl_499261, 0, 0, 0, 0, "" },
    { "rl_499619", 0x499619u, (void *)w_rl_499619, 0, 0, 0, 0, "" },
    { "rl_433cc1", 0x433cc1u, (void *)w_rl_433cc1, 0, 0, 0, 0, "" },
    { "rl_451f70", 0x451f70u, (void *)w_rl_451f70, 0, 0, 0, 0, "" },
    { "rl_4b11ce", 0x4b11ceu, (void *)w_rl_4b11ce, 0, 0, 0, 0, "" },
    { "rl_497500", 0x497500u, (void *)w_rl_497500, 0, 0, 0, 0, "" },
    { "rl_4975d3", 0x4975d3u, (void *)w_rl_4975d3, 0, 0, 0, 0, "" },
    { "rl_49c995", 0x49c995u, (void *)w_rl_49c995, 0, 0, 0, 0, "" },
    { "rl_4978ad", 0x4978adu, (void *)w_rl_4978ad, 0, 0, 0, 0, "" },
    { "rl_4ae2bd", 0x4ae2bdu, (void *)w_rl_4ae2bd, 0, 0, 0, 0, "" },
    { "rl_42d91b", 0x42d91bu, (void *)w_rl_42d91b, 0, 0, 0, 0, "" },
    { "rl_4c05a0", 0x4c05a0u, (void *)w_rl_4c05a0, 0, 0, 0, 0, "" },
    { "rl_4c0580", 0x4c0580u, (void *)w_rl_4c0580, 0, 0, 0, 0, "" },
    { "rl_49915e", 0x49915eu, (void *)w_rl_49915e, 0, 0, 0, 0, "" },
    { "rl_499366", 0x499366u, (void *)w_rl_499366, 0, 0, 0, 0, "" },
    { "rl_49964c", 0x49964cu, (void *)w_rl_49964c, 0, 0, 0, 0, "" },
    { "rl_4996c8", 0x4996c8u, (void *)w_rl_4996c8, 0, 0, 0, 0, "" },
    { "rl_4af116", 0x4af116u, (void *)w_rl_4af116, 0, 0, 0, 0, "" },
    { "rl_4af312", 0x4af312u, (void *)w_rl_4af312, 0, 0, 0, 0, "" },
    { "rl_4af767", 0x4af767u, (void *)w_rl_4af767, 0, 0, 0, 0, "" },
    { "rl_4af841", 0x4af841u, (void *)w_rl_4af841, 0, 0, 0, 0, "" },
    { "rl_4aee3e", 0x4aee3eu, (void *)w_rl_4aee3e, 0, 0, 0, 0, "" },
    { "rl_4aecf0", 0x4aecf0u, (void *)w_rl_4aecf0, 0, 0, 0, 0, "" },
    { "rl_4ae1b8", 0x4ae1b8u, (void *)w_rl_4ae1b8, 0, 0, 0, 0, "" },
    { "rl_4b2575", 0x4b2575u, (void *)w_rl_4b2575, 0, 0, 0, 0, "" },
    { "rl_4b049b", 0x4b049bu, (void *)w_rl_4b049b, 0, 0, 0, 0, "" },
    { "rl_4afe3a", 0x4afe3au, (void *)w_rl_4afe3a, 0, 0, 0, 0, "" },
    { "rl_4afab9", 0x4afab9u, (void *)w_rl_4afab9, 0, 0, 0, 0, "" },
    { "rl_4b92e0", 0x4b92e0u, (void *)w_rl_4b92e0, 0, 0, 0, 0, "" },
    { "rl_4b95ba", 0x4b95bau, (void *)w_rl_4b95ba, 0, 0, 0, 0, "" },
    { "rl_4b9652", 0x4b9652u, (void *)w_rl_4b9652, 0, 0, 0, 0, "" },
    { "rl_4b97f9", 0x4b97f9u, (void *)w_rl_4b97f9, 0, 0, 0, 0, "" },
    { "rl_4b984f", 0x4b984fu, (void *)w_rl_4b984f, 0, 0, 0, 0, "" },
    { "rl_427c9b", 0x427c9bu, (void *)w_rl_427c9b, 0, 0, 0, 0, "" },
    { "rl_427990", 0x427990u, (void *)w_rl_427990, 0, 0, 0, 0, "" },
    { "rl_4262eb", 0x4262ebu, (void *)w_rl_4262eb, 0, 0, 0, 0, "" },
    { "rl_426360", 0x426360u, (void *)w_rl_426360, 0, 0, 0, 0, "" },
    { "rl_426216", 0x426216u, (void *)w_rl_426216, 0, 0, 0, 0, "" },
    { "rl_425e4b", 0x425e4bu, (void *)w_rl_425e4b, 0, 0, 0, 0, "" },
    { "rl_4b36c0", 0x4b36c0u, (void *)w_rl_4b36c0, 0, 0, 0, 0, "" },
    { "rl_4b3714", 0x4b3714u, (void *)w_rl_4b3714, 0, 0, 0, 0, "" },
    { "rl_4b3940", 0x4b3940u, (void *)w_rl_4b3940, 0, 0, 0, 0, "" },
    { "rl_4b3b92", 0x4b3b92u, (void *)w_rl_4b3b92, 0, 0, 0, 0, "" },
    { "rl_4b37bc", 0x4b37bcu, (void *)w_rl_4b37bc, 0, 0, 0, 0, "" },
    { "rl_4b3acd", 0x4b3acdu, (void *)w_rl_4b3acd, 0, 0, 0, 0, "" },
    { "rl_426534", 0x426534u, (void *)w_rl_426534, 0, 0, 0, 0, "" },
    { "rl_426b92", 0x426b92u, (void *)w_rl_426b92, 0, 0, 0, 0, "" },
    { "rl_4263ad", 0x4263adu, (void *)w_rl_4263ad, 0, 0, 0, 0, "" },
    { "rl_426dd5", 0x426dd5u, (void *)w_rl_426dd5, 0, 0, 0, 0, "" },
    { "rl_499d7d", 0x499d7du, (void *)w_rl_499d7d, 0, 0, 0, 0, "" },
    { "rl_499aca", 0x499acau, (void *)w_rl_499aca, 0, 0, 0, 0, "" },
    { "rl_42e320", 0x42e320u, (void *)w_rl_42e320, 0, 0, 0, 0, "" },
    { "rl_42e298", 0x42e298u, (void *)w_rl_42e298, 0, 0, 0, 0, "" },
    { "rl_42f58c", 0x42f58cu, (void *)w_rl_42f58c, 0, 0, 0, 0, "" },
    { "rl_42e362", 0x42e362u, (void *)w_rl_42e362, 0, 0, 0, 0, "" },
    { "rl_42dbc7", 0x42dbc7u, (void *)w_rl_42dbc7, 0, 0, 0, 0, "" },
    { "rl_42da18", 0x42da18u, (void *)w_rl_42da18, 0, 0, 0, 0, "" },
    { "rl_46a101", 0x46a101u, (void *)w_rl_46a101, 0, 0, 0, 0, "" },
    { "rl_497a34", 0x497a34u, (void *)w_rl_497a34, 0, 0, 0, 0, "" },
    { "rl_4b8c3a", 0x4b8c3au, (void *)w_rl_4b8c3a, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_IO_N = sizeof RH_HOOKS_RL_IO / sizeof *RH_HOOKS_RL_IO;
