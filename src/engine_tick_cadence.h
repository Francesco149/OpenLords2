/*
 * Sim-tick cadence counters — pure-C port of the FUN_0040490d /
 * FUN_0040489e pair.
 *
 * Ten free-running modular counters stepped ONCE PER SIM SUB-STEP as
 * the first call of the screen-substate-0 trio (retail sub-step body
 * FUN_004b99c0: `FUN_0040490d(); FUN_0049a010(); FUN_004650b0();`).
 * Downstream sim/UI passes use them as cheap "every Nth tick" gates
 * (e.g. retail 0x482f91 fires on the mod-4 counter == 0).
 *
 *   slot  retail global   period (counter wraps to 0 past LIMIT)
 *    0    DAT_005c9268      4
 *    1    DAT_005c926c      8
 *    2    DAT_005aeb98     16
 *    3    DAT_005af8e8     24
 *    4    DAT_005bb470     32
 *    5    DAT_005bb49c     64
 *    6    DAT_005c9284    128
 *    7    DAT_005c9290    256
 *    8    DAT_005c9a78    384
 *    9    DAT_005cd418    768
 *
 * All ten are .bss runtime state (none is a save-schema slot); a fresh
 * boot starts them at 0 and a load does NOT restore them — so the
 * port's load route needs no seeding, matching retail.
 *
 * engine_tick_cadence_step  — port of FUN_0040490d @ 0x40490d (313 B):
 *   each counter += 1, then `if (LIMIT < c) c = 0` (period LIMIT+1).
 * engine_tick_cadence_reset — port of FUN_0040489e @ 0x40489e (111 B):
 *   zero all ten (retail calls it from the new-game / screen-change
 *   initialisers; cite-by-hex pending their ports).
 *
 * The single live instance lives in engine.c (the sub-step wiring).
 *
 * Reference: docs/decompiled/by-address/{40490d,40489e}.c.
 */
#ifndef OPENLORDS2_ENGINE_TICK_CADENCE_H
#define OPENLORDS2_ENGINE_TICK_CADENCE_H

#include <stdint.h>

#define ENGINE_TICK_CADENCE_COUNT 10

typedef struct {
    int32_t c[ENGINE_TICK_CADENCE_COUNT];
} engine_tick_cadence_t;

void engine_tick_cadence_step(engine_tick_cadence_t *s);
void engine_tick_cadence_reset(engine_tick_cadence_t *s);

#endif /* OPENLORDS2_ENGINE_TICK_CADENCE_H */
