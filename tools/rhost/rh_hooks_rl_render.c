/* rh_hooks_rl_render.c — a retail-layout port, partition 'render' (src/rl_render.c) vs retail, on identical state
 * AND identical pixels (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per
 * ported function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail with
 * itself). Findings: docs/findings/rl-render.md.
 *
 * BINDING.  The partition's globals (src/rl_render_globals.h, generated + a hand block; rl_render_globals.c is not
 * linked) are absolute symbols at their retail VAs (rh_rl_render_alias.h). The gfx layer the ports draw through
 * runs on retail's globals already (rh_hooks_gfx.c aliases them); its three port-side bindings (the default font
 * and the tall font retail compares by address, the sprite renderers' error log) are set here too, so the rows
 * work without the gfx table's rows installed.
 *
 * MEDIA.  Every row carries RH_SH_MEDIA: its shadow rolls back and compares the pixels of every live surface
 * (docs/findings/render-under-rhost.md §3). */
#include "rh_port.h"
#include "rl.h"
#include "rl_render_globals.h"
#include "gfx_dispatch.h"
#include "gfx_wrap.h"
#include "gfx_sprite_render.h"
#include "rh_rl_render_alias.h"

__asm__(RH_RL_RENDER_ALIASES);

/* the sprite renderers' / panel passes' error log → retail FUN_004afab9(fmt, 0, n), as rh_hooks_gfx.c binds it */
static void seam_log(uint32_t fmt, int n)
{ rh_port_retail_enter(); ((void (__cdecl *)(uint32_t, int, int))(uintptr_t)0x4afab9u)(fmt, 0, n); rh_port_retail_leave(); }
static void __attribute__((constructor)) rl_render_bind(void)
{
    gfx_dispatch_set_default_font((const uint8_t *)(uintptr_t)0x5af8f0u);   /* &DAT_005af8f0 */
    gfx_wrap_set_tall_font_marker((const uint8_t *)(uintptr_t)0x5b2fa0u);   /* &DAT_005b2fa0 */
    gfx_sprite_render_set_log(seam_log);
}

#define RH_TABLE RH_HOOKS_RL_RENDER
extern hook_t RH_HOOKS_RL_RENDER[];

/* smackw32 as a host layer (--movies): a row whose port plays a movie (0x42e060 → 0x42d91b) gets the treatment of
 * rh_hooks_rl_io.c's HOOK_SMK (a copy: that one is file-static there) — smackw32's committed writable pages and the
 * live disk handles' positions are snapshotted at entry and put back before the port side, so the port's SmackOpen
 * gets the same heap block as retail's (DAT_004d4d58, the SMACK*). Void rows (HOOK_SMK_V). */
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
/* row indices (must follow the table's order) */
enum {
    X_rl_410f4b, X_rl_41a639, X_rl_41a844, X_rl_42476b, X_rl_4248a3, X_rl_423530, X_rl_423739, X_rl_44098f,
    X_rl_420316, X_rl_40c725, X_rl_4120e0, X_rl_412795, X_rl_41298a, X_rl_410c71,
    X_rl_414819, X_rl_414f68, X_rl_41515c, X_rl_4152ea, X_rl_4192b1, X_rl_419354, X_rl_41e1dd, X_rl_421f14,
    X_rl_4224e7, X_rl_422d22, X_rl_422ff1, X_rl_423241, X_rl_4233f7,
    X_rl_41608b, X_rl_417bd3, X_rl_417aef, X_rl_417cef, X_rl_40cd58, X_rl_41ff75, X_rl_412421, X_rl_4788c6, X_rl_438e3b, X_rl_42e060, X_rl_41a166, X_rl_419789, X_rl_4198aa, X_rl_41438c,
    X_rl_41b19d, X_rl_41c996, X_rl_41d299, X_rl_41da2f,
    X_rl_423ba4, X_rl_424992,
};

/* the frame driver's HUD draws */
HOOK_V(rl_410f4b, 0, rl_410f4b())
HOOK_V(rl_41a639, 0, rl_41a639())
HOOK_V(rl_41a844, 0, rl_41a844())
HOOK_V(rl_42476b, 2, rl_42476b(I(0), I(1)))
HOOK_V(rl_4248a3, 2, rl_4248a3(I(0), I(1)))
HOOK_V(rl_423530, 0, rl_423530())
HOOK_V(rl_423739, 0, rl_423739())
HOOK_V(rl_44098f, 0, rl_44098f())
HOOK_V(rl_420316, 0, rl_420316())
HOOK_V(rl_40c725, 0, rl_40c725())
HOOK_V(rl_4120e0, 0, rl_4120e0())
HOOK_V(rl_412795, 0, rl_412795())
HOOK_V(rl_41298a, 0, rl_41298a())
HOOK_V(rl_410c71, 3, rl_410c71(I(0), I(1), I(2)))
/* the screen dispatcher's arms */
HOOK_V(rl_414819, 1, rl_414819(I(0)))
HOOK_V(rl_414f68, 0, rl_414f68())
HOOK_V(rl_41515c, 0, rl_41515c())
HOOK_V(rl_4152ea, 0, rl_4152ea())
HOOK_V(rl_4192b1, 0, rl_4192b1())
HOOK_V(rl_419354, 0, rl_419354())
HOOK_V(rl_41e1dd, 0, rl_41e1dd())
HOOK_V(rl_421f14, 0, rl_421f14())
HOOK_V(rl_4224e7, 6, rl_4224e7(I(0), I(1), I(2), I(3), I(4), I(5)))
HOOK_V(rl_422d22, 0, rl_422d22())
HOOK_V(rl_422ff1, 0, rl_422ff1())
HOOK_V(rl_423241, 0, rl_423241())
HOOK_R(rl_4233f7, 1, rl_4233f7(I(0)))   /* 0x434d77 returns it with its own AL stored over */
HOOK_V(rl_41608b, 0, rl_41608b())
HOOK_V(rl_417bd3, 1, rl_417bd3(I(0)))
HOOK_V(rl_417aef, 1, rl_417aef(I(0)))
HOOK_V(rl_417cef, 1, rl_417cef(I(0)))
HOOK_V(rl_40cd58, 0, rl_40cd58())
HOOK_V(rl_41ff75, 1, rl_41ff75(I(0)))
HOOK_V(rl_412421, 0, rl_412421())
HOOK_R(rl_4788c6, 0, rl_4788c6())
HOOK_R(rl_438e3b, 0, rl_438e3b())
HOOK_SMK_V(rl_42e060, 1, rl_42e060(I(0)))   /* plays imptitle / credits.smk at the title */
HOOK_V(rl_41a166, 1, rl_41a166(I(0)))
HOOK_V(rl_419789, 1, rl_419789(I(0)))
HOOK_V(rl_4198aa, 0, rl_4198aa())
HOOK_V(rl_41438c, 0, rl_41438c())
HOOK_V(rl_41b19d, 0, rl_41b19d())
HOOK_V(rl_41c996, 0, rl_41c996())
HOOK_V(rl_41d299, 0, rl_41d299())
HOOK_V(rl_41da2f, 0, rl_41da2f())
HOOK_V(rl_423ba4, 2, rl_423ba4(I(0), I(1)))
HOOK_V(rl_424992, 0, rl_424992())

hook_t RH_HOOKS_RL_RENDER[] = {
    { "rl_410f4b", 0x410f4bu, (void *)w_rl_410f4b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41a639", 0x41a639u, (void *)w_rl_41a639, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41a844", 0x41a844u, (void *)w_rl_41a844, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42476b", 0x42476bu, (void *)w_rl_42476b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4248a3", 0x4248a3u, (void *)w_rl_4248a3, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_423530", 0x423530u, (void *)w_rl_423530, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_423739", 0x423739u, (void *)w_rl_423739, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_44098f", 0x44098fu, (void *)w_rl_44098f, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_420316", 0x420316u, (void *)w_rl_420316, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40c725", 0x40c725u, (void *)w_rl_40c725, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4120e0", 0x4120e0u, (void *)w_rl_4120e0, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_412795", 0x412795u, (void *)w_rl_412795, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41298a", 0x41298au, (void *)w_rl_41298a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_410c71", 0x410c71u, (void *)w_rl_410c71, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_414819", 0x414819u, (void *)w_rl_414819, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_414f68", 0x414f68u, (void *)w_rl_414f68, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41515c", 0x41515cu, (void *)w_rl_41515c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4152ea", 0x4152eau, (void *)w_rl_4152ea, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4192b1", 0x4192b1u, (void *)w_rl_4192b1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_419354", 0x419354u, (void *)w_rl_419354, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41e1dd", 0x41e1ddu, (void *)w_rl_41e1dd, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_421f14", 0x421f14u, (void *)w_rl_421f14, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4224e7", 0x4224e7u, (void *)w_rl_4224e7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_422d22", 0x422d22u, (void *)w_rl_422d22, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_422ff1", 0x422ff1u, (void *)w_rl_422ff1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_423241", 0x423241u, (void *)w_rl_423241, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4233f7", 0x4233f7u, (void *)w_rl_4233f7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41608b", 0x41608bu, (void *)w_rl_41608b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_417bd3", 0x417bd3u, (void *)w_rl_417bd3, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_417aef", 0x417aefu, (void *)w_rl_417aef, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_417cef", 0x417cefu, (void *)w_rl_417cef, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40cd58", 0x40cd58u, (void *)w_rl_40cd58, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41ff75", 0x41ff75u, (void *)w_rl_41ff75, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_412421", 0x412421u, (void *)w_rl_412421, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4788c6", 0x4788c6u, (void *)w_rl_4788c6, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_438e3b", 0x438e3bu, (void *)w_rl_438e3b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42e060", 0x42e060u, (void *)w_rl_42e060, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41a166", 0x41a166u, (void *)w_rl_41a166, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_419789", 0x419789u, (void *)w_rl_419789, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4198aa", 0x4198aau, (void *)w_rl_4198aa, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41438c", 0x41438cu, (void *)w_rl_41438c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41b19d", 0x41b19du, (void *)w_rl_41b19d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41c996", 0x41c996u, (void *)w_rl_41c996, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41d299", 0x41d299u, (void *)w_rl_41d299, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41da2f", 0x41da2fu, (void *)w_rl_41da2f, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_423ba4", 0x423ba4u, (void *)w_rl_423ba4, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_424992", 0x424992u, (void *)w_rl_424992, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_RL_RENDER_N = sizeof RH_HOOKS_RL_RENDER / sizeof *RH_HOOKS_RL_RENDER;
