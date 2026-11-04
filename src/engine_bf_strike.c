/* engine_bf_strike.c — the battlefield port, partition 'strike' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The strike partition = the missile / impact records (BC*, retail 0x57a100, 101 × 0x4c — the "combat unit" block
 * of the save is the battlefield's PROJECTILE table): spawn (FUN_0046e767), the Bresenham flight (FUN_00492c8b and
 * its steppers), the impact effects (fire / explosion records FUN_00485675 / FUN_00485861) and the shooter's fire
 * order (FUN_00483337, the target pick FUN_004956cc). Findings: docs/findings/bf-strike.md.
 *
 * Retail's loop indices and "current record" cursors are GLOBALS (DAT_00568990 the spawned slot, DAT_0056d5b4 the
 * record in flight, DAT_0053e8f8 the shooter, DAT_0052eff4 the target / the unit hit): every access below reads
 * the global, as retail does after each call. Mirrored globals (the rhost sync list: DAT_00591560 / DAT_0058fea0,
 * DAT_00569570 / DAT_00569578) go through their owning port symbols (engine_path.h g_path_dist_*,
 * engine_map_query.h g_cell_cursor_*), never through their g_bf_ aliases. */
#include "engine_bf.h"
#include "engine_map_query.h"
#include "engine_path.h"

/* the projectile record `p` (BC*) as the pointer retail passes to FUN_0046eb78 */
#define BF_PROJ_REC(p) ((int)(uintptr_t)&BC32((p), 0x57a100))

/* ------------------------------------------------------------------------------------------------ sound cues */

/* IDirectSoundBuffer methods, by vtable offset (retail calls them through the buffer's vtable) */
typedef int32_t (__stdcall *bf_dsb_status_fn)(uint32_t self, uint32_t *status);                       /* +0x24 */
typedef int32_t (__stdcall *bf_dsb_play_fn)(uint32_t self, uint32_t res1, uint32_t prio, uint32_t flags); /* +0x30 */
#define BF_DSB_METHOD(buf, off) (*(uint32_t *)(uintptr_t)(*(uint32_t *)(uintptr_t)(buf) + (off)))

/* Port of FUN_00426120 (246 B) — play preloaded sound cue `id` (the DirectSound buffer DAT_00522afc[id]): nothing
 * without DirectSound (DAT_004d4760), without the buffer, or with sound off (DAT_0053f214). GetStatus first: a bad
 * HRESULT logs "OK :DS BAD STATUS -" (id + 1) and fails; a buffer whose status is exactly 1 (DSBSTATUS_PLAYING —
 * a looping buffer, status 5, is restarted) is left alone; else Play(0, 0, 0). Both HRESULTs land in DAT_00522b58.
 * Returns 1 when Play succeeded. Also the strategic map's movement cue (engine_army_move reaches retail's). */
int bf_426120(int id)
{
    uint32_t status[2];   /* retail's [ebp-8] (GetStatus out) and [ebp-4] (the Play flags, 0) */
    if (AT(uint32_t, g_bf_4d4760, 0x4d4760, 0x4d4760) == 0)
        return 0;
    if (AT(uint32_t, &g_bf_522afc, 0x522afc, 0x522afc + 4 * id) == 0)
        return 0;
    if (g_bf_53f214 == 0)
        return 0;
    uint32_t buf = AT(uint32_t, &g_bf_522afc, 0x522afc, 0x522afc + 4 * id);
    g_bf_522b58 = ((bf_dsb_status_fn)(uintptr_t)BF_DSB_METHOD(buf, 0x24))(buf, &status[0]);
    if (g_bf_522b58 != 0) {
        /* TODO(bf-extern): the status-line logger 0x4afab9 is not ported */
        engine_retail_call(0x4afab9u, 3, (uint32_t)(uintptr_t)g_bf_4d4850, 0u, (uint32_t)(id + 1));
        return 0;
    }
    if (status[0] == 1)
        return 0;
    status[1] = 0;
    buf = AT(uint32_t, &g_bf_522afc, 0x522afc, 0x522afc + 4 * id);
    g_bf_522b58 = ((bf_dsb_play_fn)(uintptr_t)BF_DSB_METHOD(buf, 0x30))(buf, 0, 0, status[1]);
    return g_bf_522b58 == 0;
}

/* Port of FUN_004262cf (28 B) — the same cue through a second entry point (FUN_00426120(id)). */
int bf_4262cf(int id)
{
    return bf_426120(id);
}

/* ------------------------------------------------------------------------------------------------ records */

/* Port of FUN_0046eb78 (54 B) — zero one 0x4c-byte projectile record at `rec` (a pointer). Returns the last index. */
int bf_46eb78(int rec)
{
    unsigned i;
    for (i = 0; i < 0x4c; i++)
        ((uint8_t *)(uintptr_t)rec)[i] = 0;
    return (int)(i - 1);
}

/* Port of FUN_0046efbe (235 B) — link projectile `p` into its cell's chain: the cell's head byte (+6) when empty,
 * else the end of the chain through +0x04 (at most 10 hops; a self-link met on the way is cut to 0). */
int bf_46efbe(int p)
{
    int hops = 0;
    int k = BCELL8(BC32(p, 0x57a11c), 0x5440e6);
    if (k == 0) {
        BCELL8(BC32(p, 0x57a11c), 0x5440e6) = (uint8_t)p;
        return 0;
    }
    while (BC16(k, 0x57a104) != 0 && hops < 10) {
        if (BC16(k, 0x57a104) == k)
            BC16(k, 0x57a104) = 0;
        else
            k = BC16(k, 0x57a104);
        hops++;
    }
    BCU16(k, 0x57a104) = (uint16_t)p;
    return 0;
}

/* Port of FUN_0046e767 (531 B) — spawn a projectile of side `side` flying from cell (x, y) to (tx, ty): the first
 * free record (side byte +0x08 == 0) in 1..100 — its index stays in DAT_00568990 (101 when the table is full; the
 * callers mostly use it anyway). Position / target in 1/32 cells (+0x0a / +0x0c, +0x0e / +0x10), the cell
 * (+0x14 / +0x16, +0x1c = byte offset), the 8-way heading (+0x2e, FUN_0046f434), the sprite blob DAT_00566518
 * (+0x00), then the line set-up FUN_00493cb9. Returns 1, or 0 when no record was free. */
int bf_46e767(int side, int x, int y, int tx, int ty)
{
    for (g_bf_568990 = 1; g_bf_568990 < 101; g_bf_568990++) {
        if (BC8(g_bf_568990, 0x57a108) != 0)
            continue;
        BC8(g_bf_568990, 0x57a108) = (uint8_t)side;
        BCU16(g_bf_568990, 0x57a10e) = (uint16_t)((uint32_t)tx << 5);
        BCU16(g_bf_568990, 0x57a110) = (uint16_t)((uint32_t)ty << 5);
        BCU16(g_bf_568990, 0x57a10a) = (uint16_t)((uint32_t)x << 5);
        BCU16(g_bf_568990, 0x57a10c) = (uint16_t)((uint32_t)y << 5);
        BCU16(g_bf_568990, 0x57a114) = (uint16_t)x;
        BCU16(g_bf_568990, 0x57a116) = (uint16_t)y;
        BC32(g_bf_568990, 0x57a11c) = 8 * (BC16(g_bf_568990, 0x57a114) + 80 * BC16(g_bf_568990, 0x57a116));
        BC8(g_bf_568990, 0x57a12e) = (uint8_t)engine_path_direction_8way(x, y, tx, ty);
        BC32(g_bf_568990, 0x57a100) = g_bf_566518;
        bf_493cb9(g_bf_568990, BC16(g_bf_568990, 0x57a10a), BC16(g_bf_568990, 0x57a10c),
                  BC16(g_bf_568990, 0x57a10e), BC16(g_bf_568990, 0x57a110));
        return 1;
    }
    return 0;
}

/* Port of FUN_00493cb9 (956 B) — the Bresenham set-up of projectile `p` from (x0, y0) to (x1, y1) (1/32 cells):
 * |dx| +0x20, |dy| +0x24, the error term +0x28 (2·minor − major, 0 when equal), the heading +0x2e snapped to an
 * axis when the line is steeper / flatter than 2:1, and the major axis +0x2c (1 = x, 2 = y). */
int bf_493cb9(int p, int x0, int y0, int x1, int y1)
{
    BC32(p, 0x57a120) = (x0 > x1) ? x0 - x1 : x1 - x0;
    BC32(p, 0x57a124) = (y0 > y1) ? y0 - y1 : y1 - y0;
    if (BC32(p, 0x57a124) > BC32(p, 0x57a120))
        BC32(p, 0x57a128) = 2 * BC32(p, 0x57a120) - BC32(p, 0x57a124);
    else if (BC32(p, 0x57a124) < BC32(p, 0x57a120))
        BC32(p, 0x57a128) = 2 * BC32(p, 0x57a124) - BC32(p, 0x57a120);
    else
        BC32(p, 0x57a128) = 0;
    if (2 * BC32(p, 0x57a120) < BC32(p, 0x57a124)) {   /* steep: diagonals snap to N / S */
        switch (BC8(p, 0x57a12e)) {
        case 1: BC8(p, 0x57a12e) = 0; break;
        case 3: BC8(p, 0x57a12e) = 4; break;
        case 5: BC8(p, 0x57a12e) = 4; break;
        case 7: BC8(p, 0x57a12e) = 0; break;
        default: break;
        }
    }
    if (2 * BC32(p, 0x57a124) < BC32(p, 0x57a120)) {   /* flat: diagonals snap to E / W */
        switch (BC8(p, 0x57a12e)) {
        case 1: BC8(p, 0x57a12e) = 2; break;
        case 3: BC8(p, 0x57a12e) = 2; break;
        case 5: BC8(p, 0x57a12e) = 6; break;
        case 7: BC8(p, 0x57a12e) = 6; break;
        default: break;
        }
    }
    BC8(p, 0x57a12c) = (BC32(p, 0x57a124) > BC32(p, 0x57a120)) ? 2 : 1;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ flight steppers
 * (all on the record in flight, DAT_0056d5b4) */

/* Port of FUN_00493b61 (344 B) — the Bresenham error update before a step along major axis `axis` (1 = x: the
 * error +0x28 grows by 2·|dy| (2·(|dy| − |dx|) once it is >= 0) and |dx| counts down; 2 = y: mirrored). */
int bf_493b61(int axis)
{
    int p = g_bf_56d5b4;
    if (axis == 1) {
        if (BC32(p, 0x57a128) >= 0)
            BC32(p, 0x57a128) += 2 * (BC32(p, 0x57a124) - BC32(p, 0x57a120));
        else
            BC32(p, 0x57a128) += 2 * BC32(p, 0x57a124);
        BC32(p, 0x57a120)--;
    } else {
        if (BC32(p, 0x57a128) >= 0)
            BC32(p, 0x57a128) += 2 * (BC32(p, 0x57a120) - BC32(p, 0x57a124));
        else
            BC32(p, 0x57a128) += 2 * BC32(p, 0x57a120);
        BC32(p, 0x57a124)--;
    }
    return 0;
}

/* Port of FUN_00494075 (176 B) — 1 when the record in flight has left the 80 × 80 field (cell +0x14 / +0x16). */
int bf_494075(void)
{
    if (BC16(g_bf_56d5b4, 0x57a114) < 0)
        return 1;
    if (BC16(g_bf_56d5b4, 0x57a116) < 0)
        return 1;
    if (BC16(g_bf_56d5b4, 0x57a114) >= 80)
        return 1;
    return BC16(g_bf_56d5b4, 0x57a116) >= 80;
}

/* Port of FUN_00494125 (160 B) — one 1/32-cell step of y (+0x0c) toward the target y (+0x10). */
int bf_494125(void)
{
    if (BC16(g_bf_56d5b4, 0x57a10c) < BC16(g_bf_56d5b4, 0x57a110))
        BCU16(g_bf_56d5b4, 0x57a10c)++;
    else if (BC16(g_bf_56d5b4, 0x57a10c) > BC16(g_bf_56d5b4, 0x57a110))
        BCU16(g_bf_56d5b4, 0x57a10c)--;
    return 0;
}

/* Port of FUN_004941c5 (160 B) — one 1/32-cell step of x (+0x0a) toward the target x (+0x0e). */
int bf_4941c5(void)
{
    if (BC16(g_bf_56d5b4, 0x57a10a) < BC16(g_bf_56d5b4, 0x57a10e))
        BCU16(g_bf_56d5b4, 0x57a10a)++;
    else if (BC16(g_bf_56d5b4, 0x57a10a) > BC16(g_bf_56d5b4, 0x57a10e))
        BCU16(g_bf_56d5b4, 0x57a10a)--;
    return 0;
}

/* Port of FUN_00494265 (821 B) — a step past the line's end: one 1/32 cell along the heading +0x2e (0 = N, then
 * clockwise; y grows southward). */
int bf_494265(void)
{
    int p = g_bf_56d5b4;
    switch (BC8(p, 0x57a12e)) {
    case 0: BCU16(p, 0x57a10c)--; break;
    case 1: BCU16(p, 0x57a10a)++; BCU16(p, 0x57a10c)--; break;
    case 2: BCU16(p, 0x57a10a)++; break;
    case 3: BCU16(p, 0x57a10a)++; BCU16(p, 0x57a10c)++; break;
    case 4: BCU16(p, 0x57a10c)++; break;
    case 5: BCU16(p, 0x57a10a)--; BCU16(p, 0x57a10c)++; break;
    case 6: BCU16(p, 0x57a10a)--; break;
    case 7: BCU16(p, 0x57a10a)--; BCU16(p, 0x57a10c)--; break;
    default: break;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ aiming */

/* Port of FUN_0046f525 (361 B) — the 8-way facing from (x0, y0) toward (x1, y1), snapped to an axis when one
 * delta is at least 3× the other (0 = N, clockwise; 0 also for the same cell). Retail returns it in AL only (the
 * rest of EAX is y0's upper bytes — the rhost row models that). */
int bf_46f525(int x0, int y0, int x1, int y1)
{
    int dx = (x0 > x1) ? x0 - x1 : x1 - x0;
    int dy = (y1 < y0) ? y0 - y1 : y1 - y0;
    if (3 * dy <= dx)
        y1 = y0;
    else if (3 * dx <= dy)
        x1 = x0;
    if (x0 > x1) {
        if (y1 < y0) return 7;
        if (y1 == y0) return 6;
        if (y1 > y0) return 5;
    } else if (x0 == x1) {
        if (y1 < y0) return 0;
        if (y1 > y0) return 4;
    } else {
        if (y1 < y0) return 1;
        if (y1 == y0) return 2;
        if (y1 > y0) return 3;
    }
    return 0;
}

/* Port of FUN_004956cc (650 B) — pick the nearest enemy of unit `u` within `range` cells on both axes (Manhattan
 * distance; its remembered target, the group's +0x2c, counts as distance 0 — a dead one is forgotten first),
 * optionally only of group `grp`; siege-class targets (+0x12 >= 7) cost 35 more and only units with a missile kind
 * (+0x16a) consider them; nothing at 160 or beyond. The loop index is retail's DAT_0056d5d0 (81 after); the pick
 * goes to DAT_0052eff4. Returns 1 when one was found. */
int bf_4956cc(int range, int u, int grp)
{
    int best = 160, pick = 0;
    int remembered = BG8(BS16(u, 0x5545f8), 0x56654c);
    if (remembered != 0) {
        g_bf_56d5d0 = remembered;
        if (BU8(g_bf_56d5d0, 0x5544ac) == 0) {
            BG8(BS16(u, 0x5545f8), 0x56654c) = 0;
            remembered = 0;
        }
    }
    for (g_bf_56d5d0 = 1; g_bf_56d5d0 < 81; g_bf_56d5d0++) {
        if (BU8(g_bf_56d5d0, 0x5544ac) == 0)
            continue;
        if (BU8(u, 0x5544ac) == BU8(g_bf_56d5d0, 0x5544ac))
            continue;
        if (BS8(g_bf_56d5d0, 0x5544b1) == 2)
            continue;
        if (grp != 0 && BS16(g_bf_56d5d0, 0x5545f8) != grp)
            continue;
        int d = engine_path_dist_manhattan(BS16(u, 0x5544a0), BS16(u, 0x5544a2),
                                           BS16(g_bf_56d5d0, 0x5544a0), BS16(g_bf_56d5d0, 0x5544a2));
        if (g_bf_56d5d0 == remembered)
            d = 0;
        if (range < g_path_dist_dx)   /* DAT_00591560: |dx| of that distance */
            continue;
        if (g_path_dist_dy > range)   /* DAT_0058fea0: |dy| */
            continue;
        if (BU8(g_bf_56d5d0, 0x554492) >= 7) {
            if (BS8(u, 0x5545ea) == 0)
                continue;
            d += 35;
        }
        if (best > d) {
            best = d;
            pick = g_bf_56d5d0;
        }
    }
    if (pick == 0)
        return 0;
    g_bf_52eff4 = pick;
    return 1;
}
