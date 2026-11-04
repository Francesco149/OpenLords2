/* engine_retail_call.c — see engine_retail_call.h. */
#include "engine_retail_call.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static engine_retail_call_fn s_fn;

void engine_retail_call_bind(engine_retail_call_fn fn) { s_fn = fn; }

uint32_t engine_retail_call(uint32_t va, int n, ...)
{
    uint32_t a[8] = {0};
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n && i < 8; i++)
        a[i] = va_arg(ap, uint32_t);
    va_end(ap);
    if (!s_fn) {
        fprintf(stderr, "openlords2: unported retail function 0x%06x reached (a *_pending.c stub)\n", (unsigned)va);
        fflush(stderr);
        abort();
    }
    return s_fn(va, n, a);
}
