/* rh_crt_so.h — HOOK_SO: a CRT row whose function writes through a pointer into its caller's STACK (state the shadow does
 * not track) — the bytes are saved at entry, put back before the port side, and the port's compared with retail's
 * (rh_hooks_rl_crt.c header). Include after the table's RH_TABLE define and its X_<name> enum. Static per table file. */
#pragma once
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- stack outputs (HOOK_SO) */
#define SO_STACK_LO 0x58000000u   /* rhost's fixed stack (README) */
#define SO_STACK_HI 0x59000000u
#define SO_MAX 4096
/* a spec entry: { arg index, kind, n }: kind 0 = n bytes, 1 = the length in argument n, 2 = strlen(argument n) + 1,
 * 3 = n * 16 bytes (an output of unknown length: sprintf's buffer, _splitpath's parts — cut at the stack's top),
 * 4 = n * 16 bytes at the pointer the argument points at (a FILE's _ptr: __output / _write_char's target) */
typedef struct { uint8_t arg, kind, n; } so_spec_t;
#define SO_END { 0xff, 0, 0 }
#define SO_N 5
typedef struct { int n; uint32_t p[SO_N], len[SO_N]; uint8_t *pre[SO_N], *post[SO_N]; } so_ctx_t;
static so_ctx_t *s_so;
static void so_enter(so_ctx_t *c, uint32_t e0, const so_spec_t *spec)
{
    c->n = 0;
    for (; spec->arg != 0xff; spec++) {
        uint32_t ptr = G32(e0 + 4u + 4u * spec->arg), len = spec->n;
        if (spec->kind == 4) ptr = (ptr >= SO_STACK_LO && ptr + 4 <= SO_STACK_HI) || ptr >= 0x400000u ? G32(ptr) : 0;
        if (spec->kind >= 3) {
            len = 16u * spec->n;
            if (ptr >= SO_STACK_LO && ptr < SO_STACK_HI && ptr + len > SO_STACK_HI) len = SO_STACK_HI - ptr;
        }
        if (spec->kind == 1) len = G32(e0 + 4u + 4u * spec->n);
        else if (spec->kind == 2) {
            uint32_t s = G32(e0 + 4u + 4u * spec->n);
            len = s ? (uint32_t)strlen((const char *)(uintptr_t)s) + 1 : 0;
        }
        if (len == 0 || len > SO_MAX || ptr < SO_STACK_LO || ptr + len > SO_STACK_HI || c->n == SO_N)
            continue;   /* a .data / arena target is state: the shadow compares it */
        c->p[c->n] = ptr; c->len[c->n] = len;
        c->pre[c->n] = malloc(len); c->post[c->n] = malloc(len);
        memcpy(c->pre[c->n], (const void *)(uintptr_t)ptr, len);
        c->n++;
    }
}
static void so_leave(so_ctx_t *c) { for (int k = 0; k < c->n; k++) { free(c->pre[k]); free(c->post[k]); } }
static void so_port_pre(int row)
{
    so_ctx_t *c = s_so;
    if (RH_TABLE[row].mode != H_SHADOW || !c) return;
    for (int k = 0; k < c->n; k++) {
        memcpy(c->post[k], (const void *)(uintptr_t)c->p[k], c->len[k]);
        memcpy((void *)(uintptr_t)c->p[k], c->pre[k], c->len[k]);
    }
}
static uint32_t so_port_post(int row, uint32_t r)
{
    so_ctx_t *c = s_so;
    if (RH_TABLE[row].mode != H_SHADOW || !c) return r;
    for (int k = 0; k < c->n; k++)
        if (memcmp(c->post[k], (const void *)(uintptr_t)c->p[k], c->len[k])) {
            uint32_t at = 0;
            while (at < c->len[k] && c->post[k][at] == ((const uint8_t *)(uintptr_t)c->p[k])[at]) at++;
            rh_log("DIVERGE-STACK %s f%u: arg@%08x (%u B) +%u r%02x/p%02x\n", RH_TABLE[row].name, g_frame, c->p[k],
                   c->len[k], at, c->post[k][at], ((const uint8_t *)(uintptr_t)c->p[k])[at]);
            return r ^ 0x80000001u;   /* reported through the compared return value */
        }
    return r;
}
#define HOOK_SO(NAME, N, SPEC, CALL) \
    static const so_spec_t k_so_##NAME[] = SPEC; \
    static uint32_t p_##NAME(const uint32_t *a) \
    { (void)a; so_port_pre(X_##NAME); uint32_t r_ = (uint32_t)(CALL); return so_port_post(X_##NAME, r_); } \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { \
        so_ctx_t c, *keep = s_so; \
        so_enter(&c, e0, k_so_##NAME); \
        s_so = &c; \
        uint32_t r = rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, 1, e0); \
        s_so = keep; \
        so_leave(&c); \
        return r; \
    } \
    RH_ENTRY(w_##NAME, b_##NAME)
#define SPEC(...) { __VA_ARGS__, SO_END }
