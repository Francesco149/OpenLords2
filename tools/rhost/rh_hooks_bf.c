/* rh_hooks_bf.c — the battlefield port's rhost binding (src/engine_bf*.c; docs/findings/battlefield-sim-port.md).
 * No rows here: each partition has its table (rh_hooks_bf_<part>.c).
 *
 * BINDING.  Every g_bf_<va> (src/engine_bf_globals.h, generated) is an ABSOLUTE symbol at its retail VA (the
 * alias block, tools/rhost/rh_bf_alias.h); engine_bf_globals.c is not linked. The save block and the runtime
 * globals other ports own are aliased by their own tables (rh_hooks_ai.c, rh_hooks_combat.c, …).
 *
 * RETAIL CALLS.  A partition's not-yet-ported function is a stub (src/engine_bf_<part>_pending.c) that calls
 * engine_retail_call(va, n, …): bound here to retail's own code at va (cdecl), bracketed by
 * rh_port_retail_enter/leave (the port's file-static mirrors stored before, reloaded after). */
#include "rh_port.h"
#include "engine_bf.h"
#include "rh_bf_alias.h"

__asm__(RH_BF_ALIASES);

typedef uint32_t (__cdecl *r0_t)(void);
typedef uint32_t (__cdecl *r1_t)(uint32_t);
typedef uint32_t (__cdecl *r2_t)(uint32_t, uint32_t);
typedef uint32_t (__cdecl *r3_t)(uint32_t, uint32_t, uint32_t);
typedef uint32_t (__cdecl *r4_t)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (__cdecl *r5_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (__cdecl *r6_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (__cdecl *r7_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (__cdecl *r8_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (__cdecl *r12_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t);

static uint32_t retail_call(uint32_t va, int n, const uint32_t *a)
{
    uint32_t r = 0;
    rh_port_retail_enter();
    switch (n) {
    case 0: r = ((r0_t)va)(); break;
    case 1: r = ((r1_t)va)(a[0]); break;
    case 2: r = ((r2_t)va)(a[0], a[1]); break;
    case 3: r = ((r3_t)va)(a[0], a[1], a[2]); break;
    case 4: r = ((r4_t)va)(a[0], a[1], a[2], a[3]); break;
    case 5: r = ((r5_t)va)(a[0], a[1], a[2], a[3], a[4]); break;
    case 6: r = ((r6_t)va)(a[0], a[1], a[2], a[3], a[4], a[5]); break;
    case 7: r = ((r7_t)va)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]); break;
    case 8: r = ((r8_t)va)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]); break;
    default: r = ((r12_t)va)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]); break;   /* 9..12 */
    }
    rh_port_retail_leave();
    return r;
}

/* retail's own Sleep import (rhost's virtual clock) */
static void retail_sleep(uint32_t ms) { ((VOID (WINAPI *)(DWORD))G32(0x5cf4acu))(ms); }

/* called once from rh_port_bind_once (rh_hooks_ai.c) */
void rh_bf_bind_seams(void)
{
    engine_retail_call_bind(retail_call);
    bf_bind_sleep_fn(retail_sleep);
}
