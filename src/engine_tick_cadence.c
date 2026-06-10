/*
 * Sim-tick cadence counters — see engine_tick_cadence.h for the
 * retail-global map and call-site context.
 */
#include "engine_tick_cadence.h"

#include "call_trace.h"

/* Wrap LIMITs in retail order — counter resets to 0 when it EXCEEDS
 * the limit, so each runs 0..LIMIT inclusive (period LIMIT+1). */
static const int32_t k_cadence_limit[ENGINE_TICK_CADENCE_COUNT] = {
    3, 7, 0xf, 0x17, 0x1f, 0x3f, 0x7f, 0xff, 0x17f, 0x2ff,
};

void engine_tick_cadence_step(engine_tick_cadence_t *s)
{
    CALL_TRACE_ENTER(0x0040490d);
    for (int i = 0; i < ENGINE_TICK_CADENCE_COUNT; i++) {
        s->c[i] += 1;
        if (k_cadence_limit[i] < s->c[i]) {
            s->c[i] = 0;
        }
    }
}

void engine_tick_cadence_reset(engine_tick_cadence_t *s)
{
    CALL_TRACE_ENTER(0x0040489e);
    for (int i = 0; i < ENGINE_TICK_CADENCE_COUNT; i++) {
        s->c[i] = 0;
    }
}
