/* rl_va.c — see rl_va.h. */
#include "rl_va.h"
#include "engine_retail_call.h"

#include <stdarg.h>

uint32_t rl_va_misses;
void (*rl_va_on_miss)(uint32_t va);
static uint32_t s_missed[64];
static int s_nmissed;

const rl_va_entry_t *rl_va_lookup(uint32_t va)
{
    int lo = 0, hi = rl_va_map_n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rl_va_map[mid].va == va)
            return &rl_va_map[mid];
        if (rl_va_map[mid].va < va)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

uint32_t rl_va_call(uint32_t va, int n, ...)
{
    uint32_t a[10] = {0};
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n && i < 10; i++)
        a[i] = va_arg(ap, uint32_t);
    va_end(ap);
    const rl_va_entry_t *e = rl_va_lookup(va);
    if (e)
        return e->fn(a);
    ++rl_va_misses;
    int seen = 0;
    for (int i = 0; i < s_nmissed; i++)
        if (s_missed[i] == va) { seen = 1; break; }
    if (!seen && s_nmissed < 64) {
        s_missed[s_nmissed++] = va;
        if (rl_va_on_miss)
            rl_va_on_miss(va);
    }
    switch (n) {
    case 0: return engine_retail_call(va, 0);
    case 1: return engine_retail_call(va, 1, a[0]);
    default: return engine_retail_call(va, 2, a[0], a[1]);   /* the dispatches pass at most 2 */
    }
}
