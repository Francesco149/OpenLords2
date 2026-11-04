/* engine_bf_strike.c — the battlefield port, partition 'strike' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The strike partition = the missile / impact records (BC*, retail 0x57a100, 101 × 0x4c — the "combat unit" block
 * of the save is the battlefield's PROJECTILE table): spawn (0x46e767), the Bresenham flight (0x492c8b and
 * its steppers), the impact effects (fire / explosion records 0x485675 / 0x485861) and the shooter's fire
 * order (0x483337, the target pick 0x4956cc). Findings: docs/findings/bf-strike.md.
 *
 * Retail's loop indices and "current record" cursors are GLOBALS (DAT_00568990 the spawned slot, DAT_0056d5b4 the
 * record in flight, DAT_0053e8f8 the shooter, DAT_0052eff4 the target / the unit hit): every access below reads
 * the global, as retail does after each call. Mirrored globals (the rhost sync list: DAT_00591560 / DAT_0058fea0,
 * DAT_00569570 / DAT_00569578) go through their owning port symbols (engine_path.h g_path_dist_*,
 * engine_map_query.h g_cell_cursor_*), never through their g_bf_ aliases. */
#include "engine_bf.h"
#include "engine_map_query.h"
#include "engine_path.h"

/* the projectile record `p` (BC*) as the pointer retail passes to 0x46eb78 */
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

/* Port of FUN_004262cf (28 B) — the same cue through a second entry point (0x426120(id)). */
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
 * (+0x14 / +0x16, +0x1c = byte offset), the 8-way heading (+0x2e, engine_path_direction_8way), the sprite blob
 * DAT_00566518 (+0x00), then the line set-up 0x493cb9. Returns 1, or 0 when no record was free. */
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

/* ------------------------------------------------------------------------------------------------ impact effects */

/* Port of FUN_00496f72 (178 B) — 1 when a 4-neighbour (N, E, S, W; the field's edges excluded) of cell (x, y) —
 * byte offset `off` — has kind (+7) `kind`. */
int bf_496f72(int x, int y, int off, int kind)
{
    if (y > 0 && BCELL8(off, 0x543e67) == kind)
        return 1;
    if (x < 79 && BCELL8(off, 0x5440ef) == kind)
        return 1;
    if (y < 79 && BCELL8(off, 0x544367) == kind)
        return 1;
    return x > 0 && BCELL8(off, 0x5440df) == kind;
}

/* Port of FUN_00485b47 (106 B) — scorch cell (x, y): flags +2 bits 2..4 = 1 (blocked), the overlay +3 keeps its low
 * nibble. */
int bf_485b47(int x, int y)
{
    int off = 8 * (x + 80 * y);
    BCELL8(off, 0x5440e2) &= 0xe3;
    BCELL8(off, 0x5440e2) |= 4;
    BCELL8(off, 0x5440e3) &= 0x0f;
    return 0;
}

/* Port of FUN_00485675 (492 B) — set cell (x, y) burning: a kind-5 effect record (0x46e767 at the cell, not
 * checked: a full table writes record 101) drawn 16 / 24 sub-cells up-left, inert (no steps, no damage), life
 * 20000, timer +0x3c = 640 - `burn`; the cell's kind goes to +0x3e and becomes 10 (fire). A kind-7 cell (wood)
 * also loses its height / overlay and is blocked. */
int bf_485675(int x, int y, int burn)
{
    bf_46e767(1, x, y, x, y);
    BC8(g_bf_568990, 0x57a109) = 5;
    BCU16(g_bf_568990, 0x57a10a) = (uint16_t)(BC16(g_bf_568990, 0x57a10a) - 16);
    BCU16(g_bf_568990, 0x57a10c) = (uint16_t)(BC16(g_bf_568990, 0x57a10c) - 24);
    BC8(g_bf_568990, 0x57a132) = 0;
    BC8(g_bf_568990, 0x57a131) = 0;
    BC16(g_bf_568990, 0x57a140) = 0;
    BC16(g_bf_568990, 0x57a138) = 20000;
    BCU16(g_bf_568990, 0x57a13c) = (uint16_t)(640 - burn);
    BC8(g_bf_568990, 0x57a12f) = 6;
    BC8(g_bf_568990, 0x57a13a) = 0;
    int off = 8 * (x + 80 * y);
    BC8(g_bf_568990, 0x57a13e) = BCELL8(off, 0x5440e7);
    BCELL8(off, 0x5440e7) = 10;
    if (BC8(g_bf_568990, 0x57a13e) == 7) {
        BCELL8(off, 0x5440e1) = 0;
        BCELL8(off, 0x5440e4) = 0;
        BCELL8(off, 0x5440e2) &= 0xe3;
        BCELL8(off, 0x5440e2) |= 4;
        BCELL8(off, 0x5440e3) = 0;
    }
    return 0;
}

/* Port of FUN_00485861 (388 B) — the impact flash at cell (x, y): like 0x485675's record (kind 5, inert, life
 * 20000) but only when a record was free, timer 640 - 10 * ((x + y) & 31), +0x3e = 15, and the cell's kind becomes
 * 16. Returns the cell offset, 0 when the table was full. */
int bf_485861(int x, int y)
{
    if (bf_46e767(1, x, y, x, y) == 0)
        return 0;
    BC8(g_bf_568990, 0x57a109) = 5;
    BCU16(g_bf_568990, 0x57a10a) = (uint16_t)(BC16(g_bf_568990, 0x57a10a) - 16);
    BCU16(g_bf_568990, 0x57a10c) = (uint16_t)(BC16(g_bf_568990, 0x57a10c) - 24);
    BC8(g_bf_568990, 0x57a132) = 0;
    BC8(g_bf_568990, 0x57a131) = 0;
    BC16(g_bf_568990, 0x57a140) = 0;
    BC16(g_bf_568990, 0x57a138) = 20000;
    BCU16(g_bf_568990, 0x57a13c) = (uint16_t)(640 - 10 * ((x + y) & 0x1f));
    BC8(g_bf_568990, 0x57a12f) = 6;
    BC8(g_bf_568990, 0x57a13a) = 0;
    int off = 8 * (x + 80 * y);
    BC8(g_bf_568990, 0x57a13e) = 15;
    BCELL8(off, 0x5440e7) = 16;
    return off;
}

/* Port of FUN_0048551d (344 B) — a fire missile lands on wood at cell (x, y): the "dest_ind.wav" cue, the cell set
 * burning (0x485675, timer 640 - 120), then five growing squares around it (the whole square each time, so
 * inner cells are re-scanned): a cell with a burning (kind-10) 4-neighbour catches fire when it is wood (kind 7;
 * each later one 7 ticks sooner, up to 120) or is scorched when it is kind 5. Cells off the field are scanned too
 * (retail reads past the grid there). */
int bf_48551d(int x, int y)
{
    /* TODO(bf-link): the WAV cue loader 0x427990 (ported elsewhere, not linked in rhost) */
    engine_retail_call(0x427990u, 3, (uint32_t)(uintptr_t)&AT(uint8_t, g_bf_4d9ee8, 0x4d9ee8, 0x4d9f28), 0u, 0u);
    bf_485675(x, y, 120);
    int sooner = 0;
    for (int r = 1; r < 6; r++) {
        int x0 = x - r, x1 = r + x, y0 = y - r, y1 = r + y;
        for (int j = y0; y1 >= j; j++) {
            for (int k = x0; x1 >= k; k++) {
                int off = 8 * (k + 80 * j);
                if (bf_496f72(k, j, off, 10) == 0)
                    continue;
                if (BCELL8(off, 0x5440e7) == 7) {
                    bf_485675(k, j, 120 - sooner);
                    sooner += 7;
                    if (sooner > 120)
                        sooner = 120;
                } else if (BCELL8(off, 0x5440e7) == 5) {
                    bf_485b47(k, j);
                }
            }
        }
    }
    return 0;
}

/* Port of FUN_0047da54 (584 B) — the 8-neighbour mask of the cell at the sweep cursor (DAT_005533cc offset,
 * DAT_00569570 col / DAT_00569578 row) into DAT_00543fe0[8] (N, NE, E, SE, S, SW, W, NW): 1 where the neighbour's
 * kind (+7) is `kind`, `edge` where it is off the field; `clear` zeroes the mask first (otherwise the slots keep
 * their value: the masks of several kinds accumulate). */
int bf_47da54(int kind, int edge, int clear)
{
    int c = g_bf_5533cc;
    if (clear != 0) {
        g_bf_543fe0 = 0; g_bf_543fe4 = 0; g_bf_543fe8 = 0; g_bf_543fec = 0;
        g_bf_543ff0 = 0; g_bf_543ff4 = 0; g_bf_543ff8 = 0; g_bf_543ffc = 0;
    }
    if (g_cell_cursor_row == 0)
        g_bf_543fe0 = edge;
    else if (BCELL8(c, 0x543e67) == kind)
        g_bf_543fe0 = 1;
    if (g_cell_cursor_row == 0 || g_cell_cursor_col == 79)
        g_bf_543fe4 = edge;
    else if (BCELL8(c, 0x543e6f) == kind)
        g_bf_543fe4 = 1;
    if (g_cell_cursor_col == 79)
        g_bf_543fe8 = edge;
    else if (BCELL8(c, 0x5440ef) == kind)
        g_bf_543fe8 = 1;
    if (g_cell_cursor_col == 79 || g_cell_cursor_row == 79)
        g_bf_543fec = edge;
    else if (BCELL8(c, 0x54436f) == kind)
        g_bf_543fec = 1;
    if (g_cell_cursor_row == 79)
        g_bf_543ff0 = edge;
    else if (BCELL8(c, 0x544367) == kind)
        g_bf_543ff0 = 1;
    if (g_cell_cursor_col == 0 || g_cell_cursor_row == 79)
        g_bf_543ff4 = edge;
    else if (BCELL8(c, 0x54435f) == kind)
        g_bf_543ff4 = 1;
    if (g_cell_cursor_col == 0)
        g_bf_543ff8 = edge;
    else if (BCELL8(c, 0x5440df) == kind)
        g_bf_543ff8 = 1;
    if (g_cell_cursor_col == 0 || g_cell_cursor_row == 0)
        g_bf_543ffc = edge;
    else if (BCELL8(c, 0x543e5f) == kind)
        g_bf_543ffc = 1;
    return 0;
}

/* Port of FUN_0047dfe0 (508 B) — a wall cell (offset `off`) breaks: it becomes rubble (kind 9, height 2, no
 * overlay owner +4, blocked); each 4-neighbour of kind 5 gets a footprint record (DAT_0053e990 / DAT_0056d648
 * count them); then every rubble cell of the field re-picks its tile from the rubble / wall / ground / kind-5
 * neighbour mask (the pattern table DAT_004d7930, 49 entries) and the path / stage scratch is refreshed. The sweep
 * cursor is retail's DAT_00569570 / 78 / DAT_005533cc. */
int bf_47dfe0(int off)
{
    BCELL8(off, 0x5440e7) = 9;
    BCELL8(off, 0x5440e1) = 2;
    BCELL8(off, 0x5440e4) = 0;
    BCELL8(off, 0x5440e2) &= 0xe3;
    BCELL8(off, 0x5440e2) |= 4;
    /* TODO(bf-link): engine_tile_push_footprint_record (0x48ee46) — engine_tile.c is not linked in rhost */
    if (BCELL8(off, 0x543e67) == 5) {
        s_slot_0053e990++;
        engine_retail_call(0x48ee46u, 1, (uint32_t)(off - 640));
        s_slot_0056d648++;
    }
    if (BCELL8(off, 0x5440ef) == 5) {
        s_slot_0053e990++;
        engine_retail_call(0x48ee46u, 1, (uint32_t)(off + 8));
        s_slot_0056d648++;
    }
    if (BCELL8(off, 0x544367) == 5) {
        s_slot_0053e990++;
        engine_retail_call(0x48ee46u, 1, (uint32_t)(off + 640));
        s_slot_0056d648++;
    }
    if (BCELL8(off, 0x5440df) == 5) {
        s_slot_0053e990++;
        engine_retail_call(0x48ee46u, 1, (uint32_t)(off - 8));
        s_slot_0056d648++;
    }
    g_cell_cursor_row = 0;
    g_bf_5533cc = 0;
    for (; g_cell_cursor_row < 80; g_cell_cursor_row++) {
        for (g_cell_cursor_col = 0; g_cell_cursor_col < 80; g_cell_cursor_col++, g_bf_5533cc += 8) {
            if (BCELL8(g_bf_5533cc, 0x5440e7) != 9)
                continue;
            bf_47da54(9, 1, 1);
            bf_47da54(3, 1, 0);
            bf_47da54(1, 1, 0);
            bf_47da54(5, 1, 0);
            /* TODO(bf-link): engine_territory_pick_feature (0x46c2de) is not retail-layout (its own table, the
             * counters file-static): retail's, on the battlefield table DAT_004d7930 */
            if (engine_retail_call(0x46c2deu, 2, (uint32_t)(uintptr_t)g_bf_4d7930, 49u) != 0)
                BCELL8(g_bf_5533cc, 0x5440e3) = (uint8_t)(g_bf_57c8a4 + g_bf_568200);
        }
    }
    /* TODO(bf-link): engine_tile_refresh_path_scratch (0x471c1f) / engine_tile_refresh_stage_scratch (0x471d30) */
    engine_retail_call(0x471c1fu, 0);
    return (int)engine_retail_call(0x471d30u, 0);
}

/* ------------------------------------------------------------------------------------------------ the strike */

/* the record in flight (DAT_0056d5b4), re-read at every access like retail */
#define PJ g_bf_56d5b4

/* Port of FUN_00492c8b (3798 B) — one flight tick of projectile DAT_0056d5b4 (shot by unit +0x06, of its group
 * +0x5545f8; missile kind +0x09: 1 arrow, 2 bolt, 3 stone, 4 spent stone, 5 effect).
 *  - on a kind-15 cell (the gate) with a gate target +0x44: a flash (0x485861), DAT_0053e9d0 = 1, the record
 *    cleared, the group's +0x30 = 0 and unit 0's side byte cleared — unless the shooter has +0x13 set and
 *    the projectile is not yet AT the target cell.
 *  - a climb past the start height (+0x2f + 1 < the cell's height +4; or a cell height < 0: then 99) marks it
 *    falling (+0x3a); a falling arrow / bolt / stone ages +0x30 and dies at > 32 or on reaching that height — the
 *    shooter's +0x183 = 1, the group's +0x2d = 200.
 *  - +0x36 counts ticks; past the life +0x38 the record dies (shooter +0x183 = 1, group +0x2d = 1).
 *  - else +0x31 (signed) sub-steps: Bresenham along the major axis (0x493b61, 0x494125 / 004941c5) or past
 *    the line's end along the heading (0x494265); off the field = gone; the cell (+0x14 / +0x16 / +0x1c) is
 *    recomputed. While the cooldown +0x3c runs nothing hits. A wood cell (kind 7) sets it burning (0x48551d).
 *    A stone on a wall (kind 4, overlay +3 > 2, height 1..3) damages it (the cell's +0 counter; at 16 the wall
 *    breaks, 0x47dfe0; cue 15) or, taller, just bounces (cue 16); either way the stone is spent (kind 4, one
 *    sub-step, life + 1000, cooldown 120, drawn 16 up-left). An arrow / bolt / stone on a live enemy (cell +5 =
 *    DAT_0052eff4) hits it: damage +0x40 cut by the climb (targets with +0x13 set /4 · /2, else /5 · /3), minus
 *    the target's armour +0x172; siege targets (+0x12 >= 7): /2 · /3 for bolts, arrows floor at 6 / 4, capped at
 *    2·(5·DAT_00522f6c + 10 / + 5); at least 2. The wound accumulator +0x19a converts 100 (siege 160) per troop
 *    (1..4 at once) out of +0x1a0; at 0 the unit is dead (+0x31 = 2, cue 13). Cues: bolt 10, arrow 8. */
int bf_492c8b(void)
{
    int height = BCELL8(BC32(PJ, 0x57a11c), 0x5440e4);            /* [ebp-0x28] */
    int8_t cell_h = BCELLS8(BC32(PJ, 0x57a11c), 0x5440e1);        /* [ebp-0x14] (its sign bit) */
    int kind = BCELL8(BC32(PJ, 0x57a11c), 0x5440e7);              /* [ebp-0x18] */
    int shooter = BC16(PJ, 0x57a106);                             /* [ebp-8] */
    int grp = BS16(shooter, 0x5545f8);                            /* [ebp-0x24] */

    if (kind == 15 && BC32(PJ, 0x57a144) != 0 &&
        (BU8(shooter, 0x554493) == 0 || BC32(PJ, 0x57a11c) == BC32(PJ, 0x57a144))) {
        BC16(PJ, 0x57a13c) = 8;
        bf_485861(BC16(PJ, 0x57a114), BC16(PJ, 0x57a116));
        s_slot_0053e9d0 = 1;
        bf_46eb78(BF_PROJ_REC(PJ));
        BG32(grp, 0x566550) = 0;
        BU8(0, 0x5544ac) = 0;
        return 0;
    }
    if (BC8(PJ, 0x57a12f) + 1 < height)
        BC8(PJ, 0x57a13a) = 1;
    if (cell_h < 0) {
        BC8(PJ, 0x57a13a) = 1;
        height = 99;
    }
    if (BC8(PJ, 0x57a13a) != 0 && BC8(PJ, 0x57a109) < 3) {
        BC8(PJ, 0x57a130)++;
        if (BC8(PJ, 0x57a130) > 0x20 || BC8(PJ, 0x57a12f) == height) {
            BU8(shooter, 0x554603) = 1;
            BG8(grp, 0x56654d) = 0xc8;
            bf_46eb78(BF_PROJ_REC(PJ));
            return 0;
        }
    }
    BCU16(PJ, 0x57a136)++;
    if (BC16(PJ, 0x57a136) > BC16(PJ, 0x57a138)) {
        bf_46eb78(BF_PROJ_REC(PJ));
        BU8(shooter, 0x554603) = 1;
        BG8(grp, 0x56654d) = 1;
        return 0;
    }
    for (int i = 0; BCS8(PJ, 0x57a131) > i; i++) {
        if (BC32(PJ, 0x57a124) + BC32(PJ, 0x57a120) > 0) {
            bf_493b61(BC8(PJ, 0x57a12c));
            if (BC8(PJ, 0x57a12c) == 2) {
                bf_494125();
                if (BC32(PJ, 0x57a128) >= 0) {
                    BC32(PJ, 0x57a120)--;
                    bf_4941c5();
                }
            } else {
                bf_4941c5();
                if (BC32(PJ, 0x57a128) >= 0) {
                    BC32(PJ, 0x57a124)--;
                    bf_494125();
                }
            }
        } else {
            bf_494265();
        }
        if (bf_494075() != 0) {
            bf_46eb78(BF_PROJ_REC(PJ));
            return 0;
        }
        BCU16(PJ, 0x57a114) = (uint16_t)((BC16(PJ, 0x57a10a) + 8) / 32);
        BCU16(PJ, 0x57a116) = (uint16_t)((BC16(PJ, 0x57a10c) + 8) / 32);
        BC32(PJ, 0x57a11c) = 8 * (BC16(PJ, 0x57a114) + 80 * BC16(PJ, 0x57a116));
        kind = BCELL8(BC32(PJ, 0x57a11c), 0x5440e7);
        if (BC16(PJ, 0x57a13c) != 0)
            continue;
        if (kind == 7) {
            BC16(PJ, 0x57a13c) = 8;
            bf_48551d(BC16(PJ, 0x57a114), BC16(PJ, 0x57a116));
        }
        if (BC8(PJ, 0x57a109) == 3) {
            height = BCELL8(BC32(PJ, 0x57a11c), 0x5440e4);
            int overlay = BCELL8(BC32(PJ, 0x57a11c), 0x5440e3);     /* [ebp-0x20] */
            kind = BCELL8(BC32(PJ, 0x57a11c), 0x5440e7);
            if (height < 1 || kind != 4 || overlay <= 2)
                continue;
            if (height > 3) {
                BU8(shooter, 0x554603) = 1;
                bf_426120(16);
            } else {
                BU8(shooter, 0x554603) = 0;
                BCELL8(BC32(PJ, 0x57a11c), 0x5440e0)++;
                if (BCELL8(BC32(PJ, 0x57a11c), 0x5440e0) >= 0x10)
                    bf_47dfe0(BC32(PJ, 0x57a11c));
                bf_4262cf(15);
            }
            BC16(PJ, 0x57a13c) = 120;
            BC8(PJ, 0x57a109) = 4;
            BC8(PJ, 0x57a131) = 1;
            BCU16(PJ, 0x57a138) = (uint16_t)(BC16(PJ, 0x57a138) + 1000);
            BC32(PJ, 0x57a124) = 0;
            BC32(PJ, 0x57a120) = BC32(PJ, 0x57a124);
            BCU16(PJ, 0x57a10a) = (uint16_t)(BC16(PJ, 0x57a10a) - 16);
            BCU16(PJ, 0x57a10c) = (uint16_t)(BC16(PJ, 0x57a10c) - 16);
            return 0;
        }
        if (BC8(PJ, 0x57a109) > 2)
            continue;
        g_bf_52eff4 = BCELL8(BC32(PJ, 0x57a11c), 0x5440e5);
        if (g_bf_52eff4 == 0)
            continue;
        if (BS8(g_bf_52eff4, 0x5544b1) == 2)
            continue;
        if (BC8(PJ, 0x57a108) == BU8(g_bf_52eff4, 0x5544ac))
            continue;

        BG8(grp, 0x56654d) = 0;
        BU8(g_bf_52eff4, 0x554495) = 1;
        BU16(g_bf_52eff4, 0x554496) = BCU16(PJ, 0x57a106);
        height = BCELL8(BC32(PJ, 0x57a11c), 0x5440e4);
        int dmg = BC16(PJ, 0x57a140);                                  /* [ebp-4] */
        if (BU8(g_bf_52eff4, 0x554493) != 0) {
            if (BC8(PJ, 0x57a12f) + 3 < height)
                dmg /= 4;
            else if (BC8(PJ, 0x57a12f) + 1 < height)
                dmg /= 2;
        } else {
            if (BC8(PJ, 0x57a12f) + 3 < height)
                dmg /= 5;
            else if (BC8(PJ, 0x57a12f) + 1 < height)
                dmg /= 3;
        }
        dmg -= BU8(g_bf_52eff4, 0x5545f2);
        int per_troop;                                                 /* [ebp-0x10] */
        if (BU8(g_bf_52eff4, 0x554492) < 7) {
            per_troop = 100;
        } else {
            per_troop = 160;
            if (BC8(PJ, 0x57a109) == 2) {
                if (BU8(g_bf_52eff4, 0x554493) != 0)
                    dmg /= 2;
                else
                    dmg /= 3;
            } else if (BC8(PJ, 0x57a109) == 1 && dmg <= 0) {
                dmg = BU8(g_bf_52eff4, 0x554493) != 0 ? 6 : 4;
            }
            int cap = BU8(g_bf_52eff4, 0x554493) != 0 ? 2 * (5 * (int32_t)s_slot_00522f6c + 10)
                                                       : 2 * (5 * (int32_t)s_slot_00522f6c + 5);   /* [ebp-0x1c] */
            if (dmg > cap)
                dmg = cap;
        }
        if (dmg <= 1)
            dmg = 2;
        if (BC8(PJ, 0x57a109) == 2)
            bf_426120(10);
        else if (BC8(PJ, 0x57a109) == 1)
            bf_426120(8);
        BU16(g_bf_52eff4, 0x55461a) = (uint16_t)(BS16(g_bf_52eff4, 0x55461a) + dmg);
        if (BS16(g_bf_52eff4, 0x55461a) >= per_troop) {
            int lost;
            if (BS16(g_bf_52eff4, 0x55461a) >= 4 * per_troop)
                lost = 4;
            else if (BS16(g_bf_52eff4, 0x55461a) >= 3 * per_troop)
                lost = 3;
            else if (BS16(g_bf_52eff4, 0x55461a) >= 2 * per_troop)
                lost = 2;
            else
                lost = 1;
            BU16(g_bf_52eff4, 0x554620) = (uint16_t)(BS16(g_bf_52eff4, 0x554620) - lost);
            BS16(g_bf_52eff4, 0x55461a) = 0;
            if (BC8(PJ, 0x57a109) == 2)
                bf_426120(10);
            else if (BC8(PJ, 0x57a109) == 1)
                bf_426120(8);
        }
        if (BS16(g_bf_52eff4, 0x554620) <= 0) {
            BS16(g_bf_52eff4, 0x554620) = 0;
            BU8(g_bf_52eff4, 0x5544b1) = 2;
            bf_4262cf(13);
        }
        BC16(PJ, 0x57a13c) = 2;
        return 0;
    }
    return 0;
}

/* Port of FUN_00483337 (1873 B) — the fire order of unit DAT_0053e8f8 (not siege class: +0x12 < 7). The group's
 * remembered target (+0x2c) becomes the unit's target +0x174 (a dead one is forgotten). The reload counter +0x180
 * counts up: at its draw point (+0x16b − 10) the unit picks the nearest enemy within (its range +0x170) / 8 cells
 * (0x4956cc; none: target 0, the counter set back to 4, the group's memory cleared) and turns to it
 * (+0x18, 0x46f525); past +0x16b it shoots — counter 0, cooldown +0x182 = 120, the group's +0x0f = 120 — a
 * projectile (0x46e767; DAT_00568990, used even when the table was full) of the unit's missile kind +0x16a,
 * flight speed +0x16e, +0x16c sub-steps a tick, damage +0x19e (cut to 4/5, 3/4 or 1/2 by the fatigue +0x197),
 * life = the range, start height = the shooter's cell, fall age (unit & 0x10) + 4; a group neither charging nor
 * holding (+0x01 / +0x03 == 0) fires at the gate (+0x44 = 1) once more than 3 units stand on a kind-15 cell
 * (DAT_005530e8). The shot flies 8 ticks at once (0x492c8b, the record in flight DAT_0056d5b4), joins its
 * cell's chain (0x46efbe), takes its sprite frame +0x12 = speed + heading, and plays its cue (bolt 9, arrow 7 —
 * by the kind of the LAST record spawned: an impact flash on the way changes DAT_00568990). A unit with a target
 * then faces it (bf_486239). */
int bf_483337(void)
{
    int grp = BS16(g_bf_53e8f8, 0x5545f8);                        /* [ebp-0xc] */
    if (BU8(g_bf_53e8f8, 0x554492) >= 7)
        return 0;
    int remembered = BG8(BS16(g_bf_53e8f8, 0x5545f8), 0x56654c);  /* [ebp-0x10] */
    if (remembered != 0) {
        if (BU8(remembered, 0x5544ac) != 0) {
            BU8(g_bf_53e8f8, 0x5545f4) = (uint8_t)remembered;
        } else {
            BG8(BS16(g_bf_53e8f8, 0x5545f8), 0x56654c) = 0;
            remembered = 0;
        }
    }
    BU16(g_bf_53e8f8, 0x554600)++;
    if (BS8(g_bf_53e8f8, 0x5545eb) - 10 == BS16(g_bf_53e8f8, 0x554600)) {
        if (bf_4956cc(BS16(g_bf_53e8f8, 0x5545f0) / 8, g_bf_53e8f8, 0) != 0) {
            BU8(g_bf_53e8f8, 0x5545f4) = (uint8_t)g_bf_52eff4;
            BU8(g_bf_53e8f8, 0x554498) = (uint8_t)bf_46f525(BS16(g_bf_53e8f8, 0x5544a0), BS16(g_bf_53e8f8, 0x5544a2),
                                                            BS16(g_bf_52eff4, 0x5544a0), BS16(g_bf_52eff4, 0x5544a2));
        } else {
            BU8(g_bf_53e8f8, 0x5545f4) = 0;
            BS16(g_bf_53e8f8, 0x554600) = 4;
            BG8(BS16(g_bf_53e8f8, 0x5545f8), 0x56654c) = 0;
            remembered = 0;
        }
    }
    (void)remembered;
    if (BS8(g_bf_53e8f8, 0x5545eb) < BS16(g_bf_53e8f8, 0x554600)) {
        BS16(g_bf_53e8f8, 0x554600) = 0;
        g_bf_52eff4 = BU8(g_bf_53e8f8, 0x5545f4);
        if (BU8(g_bf_52eff4, 0x5544ac) != 0) {
            BU8(g_bf_53e8f8, 0x554602) = 120;
            BG8(grp, 0x56652f) = 120;
            bf_46e767(BU8(g_bf_53e8f8, 0x5544ac), BS16(g_bf_53e8f8, 0x5544a0), BS16(g_bf_53e8f8, 0x5544a2),
                      BS16(g_bf_52eff4, 0x5544a0), BS16(g_bf_52eff4, 0x5544a2));
            if (BG8(grp, 0x566521) == 0 && BG8(grp, 0x566523) == 0 && g_bf_5530e8 > 3)
                BC32(g_bf_568990, 0x57a144) = 1;
            BCU16(g_bf_568990, 0x57a106) = (uint16_t)g_bf_53e8f8;
            BC8(g_bf_568990, 0x57a109) = BU8(g_bf_53e8f8, 0x5545ea);
            BCU16(g_bf_568990, 0x57a134) = BU16(g_bf_53e8f8, 0x5545ee);
            BC8(g_bf_568990, 0x57a132) = BU8(g_bf_53e8f8, 0x5545ec);
            BC8(g_bf_568990, 0x57a131) = BU8(g_bf_53e8f8, 0x5545ec);
            if (BU8(g_bf_53e8f8, 0x554617) == 0)
                BCU16(g_bf_568990, 0x57a140) = BU16(g_bf_53e8f8, 0x55461e);
            else if (BU8(g_bf_53e8f8, 0x554617) == 1)
                BCU16(g_bf_568990, 0x57a140) = (uint16_t)(4 * BS16(g_bf_53e8f8, 0x55461e) / 5);
            else if (BU8(g_bf_53e8f8, 0x554617) == 2)
                BCU16(g_bf_568990, 0x57a140) = (uint16_t)(3 * BS16(g_bf_53e8f8, 0x55461e) / 4);
            else
                BCU16(g_bf_568990, 0x57a140) = (uint16_t)(BS16(g_bf_53e8f8, 0x55461e) / 2);
            BCU16(g_bf_568990, 0x57a138) = BU16(g_bf_53e8f8, 0x5545f0);
            BC8(g_bf_568990, 0x57a12f) = BCELL8(BS32(g_bf_53e8f8, 0x55449c), 0x5440e4);
            BC8(g_bf_568990, 0x57a130) = (uint8_t)((g_bf_53e8f8 & 0x10) + 4);
            BC8(g_bf_568990, 0x57a13a) = 0;
            g_bf_56d5b4 = g_bf_568990;
            for (int i = 0; i < 8; i++)
                bf_492c8b();
            bf_46efbe(g_bf_56d5b4);
            BCU16(g_bf_56d5b4, 0x57a112) = (uint16_t)(BC8(g_bf_56d5b4, 0x57a12e) + BC16(g_bf_56d5b4, 0x57a134));
            if (BC8(g_bf_568990, 0x57a109) == 2)
                bf_426120(9);
            else if (BC8(g_bf_568990, 0x57a109) == 1)
                bf_426120(7);
        }
    }
    if (BU8(g_bf_53e8f8, 0x5545f4) != 0)
        bf_486239();
    return 0;
}
