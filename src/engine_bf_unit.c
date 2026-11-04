/* engine_bf_unit.c — the battlefield port, partition 'unit' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does. */
#include "engine_bf.h"

/* the movement-state table DAT_004d9140 (unit +0x12, 0..10): each handler sets the step parameters, then runs
 * the unit's order through DAT_004d9170 */
static int (*const k_bf_state[11])(void) = {
    bf_4825d1, bf_4826b0, bf_482789, bf_482862, bf_48293b, bf_482a14, bf_482aed, bf_482bc6, bf_482ca8, bf_482d8a,
    bf_482e6c,
};

/* Port of FUN_004822ed (644 B) — the battlefield's per-unit sim driver (combat cluster, once per sub-step).
 * Recounts the class 7..9 units (DAT_00553ff0), the units on a kind-5 cell (cell +7) in formation slot 4
 * (DAT_00553e64) and the units with +0x13 set on a kind-15 cell (DAT_005530e8); a unit on a kind-10 or -17
 * cell goes through FUN_0049459a first. Then per unit: tick counter +0x1a8 + 1, latch +0x35 cleared, the
 * cooldowns +0x175 / +0x182 run down, the movement-state handler (+0x12 through DAT_004d9140; >= 11: the unit
 * is removed) and the troop snapshot +0x1a2 = +0x1a0. The loop index is retail's global DAT_0053e8f8 (the
 * handlers read it as the current unit; re-read after every call). */
int bf_4822ed(void)
{
    g_bf_5530e8     = 0;
    s_slot_00553ff0 = 0;
    s_slot_00553e64 = 0;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) == 0)
            continue;
        int kind = BCELL8(BS32(u, 0x55449c), 0x5440e7);
        if (BU8(u, 0x554492) >= 7 && BU8(u, 0x554492) < 10)
            s_slot_00553ff0++;
        if (kind == 5 && BS8(u, 0x5545fa) == 4)
            s_slot_00553e64++;
        if (kind == 10)
            bf_49459a(g_bf_53e8f8);
        if (kind == 17)
            bf_49459a(g_bf_53e8f8);
        u = g_bf_53e8f8;
        if (kind == 15 && BU8(u, 0x554493) != 0)
            g_bf_5530e8++;
        BS32(u, 0x554628)++;
        BU8(u, 0x5544b5) = 0;
        if (BS8(u, 0x5545f5) != 0)
            BU8(u, 0x5545f5)--;
        if (BU8(u, 0x554602) != 0)
            BU8(u, 0x554602)--;
        if (BU8(u, 0x554492) < 11)
            k_bf_state[BU8(u, 0x554492)]();
        else
            bf_46ebe4(u);
        u = g_bf_53e8f8;
        BS16(u, 0x554622) = BS16(u, 0x554620);
    }
    return 0;
}

/* Port of FUN_0046ebe4 (138 B) — remove battlefield unit `u`: its cell's occupant byte (+5) cleared, the
 * blocked-ring around a unit flagged +0x194 released (FUN_00491f17 at its position), the record zeroed. */
int bf_46ebe4(int u)
{
    BCELL8(BS32(u, 0x55449c), 0x5440e5) = 0;
    if (BU8(u, 0x554614) != 0)
        bf_491f17(BS16(u, 0x5544a0), BS16(u, 0x5544a2));
    return bf_46eb3f((int)(uintptr_t)&BU8(u, 0x554480));
}

/* Port of FUN_0046eb3f (57 B) — zero one 0x1b0-byte unit record at `rec` (a pointer). Returns the last index. */
int bf_46eb3f(int rec)
{
    unsigned i;
    for (i = 0; i < 0x1b0; i++)
        ((uint8_t *)(uintptr_t)rec)[i] = 0;
    return (int)(i - 1);
}
