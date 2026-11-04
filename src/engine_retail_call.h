/* engine_retail_call.h — port code calling a RETAIL function that is not ported yet.
 *
 * The battlefield port (engine_bf_*.c) is written top-down and bottom-up at once: a port function calls its
 * callees by their canonical port names (bf_<va>), and a callee nobody has ported yet is a stub in an
 * engine_bf_<part>_pending.c that calls engine_retail_call(va, n, args...). The hybrid host (tools/rhost)
 * binds the trampoline: retail's own code at `va` runs (cdecl, n int arguments), bracketed by
 * rh_port_retail_enter/leave like every other seam. The standalone binds nothing — reaching a stub there is a
 * missing port: reported with the VA, then fatal. What is left = the *_pending.c files.
 * (Bare VAs here, never FUN_ tokens: those count as ported in the ledger.) */
#pragma once
#include <stdint.h>

typedef uint32_t (*engine_retail_call_fn)(uint32_t va, int n, const uint32_t *args);
void     engine_retail_call_bind(engine_retail_call_fn fn);
uint32_t engine_retail_call(uint32_t va, int n, ...);   /* n <= 8 int arguments */
