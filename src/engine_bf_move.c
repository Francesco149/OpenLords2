/* engine_bf_move.c — the battlefield port, partition 'move' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * Findings: docs/findings/bf-move.md. */
#include "engine_bf.h"
#include "engine_path.h"
#include "engine_map_query.h"

/* the current unit (retail's loop index DAT_0053e8f8; re-read after every call) and the "other unit" the last
 * neighbour probe met (DAT_0052eff4, FUN_00490a44) */
#define CUR   (g_bf_53e8f8)
#define OTHER (g_bf_52eff4)
/* Retail globals another linked port module already MIRRORS (rh_hooks_ai.c rh_port_sync_in / _out reloads and
 * stores them around every port call): written through g_bf_<va> they would be overwritten by the stale mirror
 * on the way out — so go through the mirror. DAT_00569570 / DAT_00569578: engine_map_query's cell cursor;
 * DAT_00591560 / DAT_0058fea0: engine_path's distance deltas (engine_path_dist_chebyshev writes them). */
#define CUR_COL (g_cell_cursor_col)
#define CUR_ROW (g_cell_cursor_row)

/* ------------------------------------------------------------------------------------------------ leaves */

/* Port of FUN_00494df9 (70 B) — a unit's morale-ish counter +0x17b (signed byte) += its step +0x18b. */
int bf_494df9(int u)
{
    BU8(u, 0x5545fb) = (uint8_t)(BU8(u, 0x55460b) + BS8(u, 0x5545fb));
    return 0;
}

/* Port of FUN_00494d08 (241 B) — a unit's engagement timer +0x184 from its engagement kind +0x18a:
 * 0 → 0, 1 → 40, 2 / 3 → 80, else 120. */
int bf_494d08(int u)
{
    uint8_t k = BU8(u, 0x55460a);
    BU8(u, 0x554604) = k == 0 ? 0 : k == 1 ? 40 : (k == 2 || k == 3) ? 80 : 120;
    return 0;
}

/* Port of FUN_004b3dcd (132 B) — copy `n` bytes from `src` to `dst` in whole 64-byte blocks (a do-while: at
 * least one block, rounded up). Returns the last dword copied. */
int bf_4b3dcd(int src, int dst, int n)
{
    const uint32_t *s = (const uint32_t *)(uintptr_t)src;
    uint32_t *d = (uint32_t *)(uintptr_t)dst;
    uint32_t last;
    do {
        for (int k = 0; k < 16; k++)
            d[k] = s[k];
        last = s[15];
        s += 16;
        d += 16;
        n -= 64;
    } while (n > 0);
    return (int)last;
}

/* Port of FUN_00491a34 (235 B) — pop the next step of unit u's stored path (+0x36 length, +0x38 (col, row)
 * byte pairs, last step first): the 8-way direction from the unit's cell to it, the length - 1. Empty: 8. */
int bf_491a34(int u)
{
    int n = BS16(u, 0x5544b6);
    if (n <= 0)
        return 8;
    int d = engine_path_direction_8way(BS16(u, 0x5544a0), BS16(u, 0x5544a2), BU8(u, 0x5544b6 + 2 * n),
                                       BU8(u, 0x5544b7 + 2 * n)) & 0xff;
    BS16(u, 0x5544b6) = (int16_t)(BS16(u, 0x5544b6) - 1);
    return d;
}

/* Port of FUN_004928a3 (174 B) — a no-op: retail's first instruction jumps to the epilogue. The dead body
 * would re-target unit u at a free cell within radius 1..19 (FUN_00496155) or remove it (FUN_0046ebe4). */
int bf_4928a3(int u)
{
    (void)u;
    return 0;
}

/* Port of FUN_00496155 (374 B) — is there a free cell (occupant +5 == 0, flags +1 & 0x90 == 0) in the
 * (2r+1)² box around (x, y), clipped to the field? Scans row-major with retail's globals DAT_00569570 (col) /
 * DAT_00569578 (row) as the loop variables: on a hit they hold its cell (1), else their end values (0). */
int bf_496155(int x, int y, int r)
{
    int x0 = x - r, y0 = y - r;
    int h = 2 * r + 1, w = h;
    if (x0 < 0) {
        w = x0 + h;
        x0 = 0;
    } else if (x0 + w > 80)
        w = 80 - x0;
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    } else if (y0 + h > 80)
        h = 80 - y0;
    int off = 8 * (x0 + 80 * y0);
    int skip = 8 * (80 - w);
    for (CUR_ROW = y0; y0 + h > CUR_ROW; CUR_ROW++, off += skip)
        for (CUR_COL = x0; x0 + w > CUR_COL; CUR_COL++, off += 8) {
            uint8_t fl = BCELL8(off, 0x5440e1) & 0x90;
            if (BCELL8(off, 0x5440e5) == 0 && fl == 0)
                return 1;
        }
    return 0;
}

/* Port of FUN_004972f9 (318 B) — the direction (0..7, DAT_004d8588 order) of the first enemy unit adjacent to
 * the current unit's cell `off` on the same elevation (+4); 8 if none, if the unit's +0x18d is set, or if
 * (x, y) is on the field's border. */
int bf_4972f9(int x, int y, int off)
{
    if (BU8(CUR, 0x55460d) != 0 || x <= 0 || y <= 0 || x >= 79 || y >= 79)
        return 8;
    int elev = BCELL8(off, 0x5440e4);
    for (int i = 0; i < 8; i++) {
        int occ = BCELL8(g_bf_4d8588[i] + off, 0x5440e5);
        int e = BCELL8(g_bf_4d8588[i] + off, 0x5440e4);
        if (occ != 0 && BU8(CUR, 0x5544ac) != BU8(occ, 0x5544ac) && e == elev)
            return i;
    }
    return 8;
}

/* Port of FUN_00472227 (363 B) — should unit u give up its move? Only for a unit with +0x13 set (and, in a
 * quick-resolved battle DAT_0053e8fc, a formation slot +0x17a): the cost field DAT_00504030 at its target
 * (+0x24, +0x26) is a real distance (1..997) above 20 (quick: 150) and more than 5 × the straight-line
 * (Chebyshev) distance. */
int bf_472227(int u)
{
    if (BU8(u, 0x554493) == 0)
        return 0;
    if (s_slot_0053e8fc != 0 && BS8(u, 0x5545fa) == 0)
        return 0;
    int lim = s_slot_0053e8fc != 0 ? 150 : 20;
    int idx = 80 * BS16(u, 0x5544a6) + BS16(u, 0x5544a4);
    int cost = AT(int16_t, g_bf_50402e, 0x50402e, 0x504030 + 2 * idx);
    int dist = engine_path_dist_chebyshev(BS16(u, 0x5544a0), BS16(u, 0x5544a2), BS16(u, 0x5544a4),
                                          BS16(u, 0x5544a6));
    if (cost <= 0 || cost >= 998)
        return 0;
    if (lim >= cost)
        return 0;
    return 5 * dist < cost;
}

/* Port of FUN_0048f079 (356 B) — release group g's hold: unless the group is flagged (+0xe), in mode 1 (+8)
 * or +1 is set, its +0x10 is cleared and every live unit of the group (+0x178 == g) whose order +0x31 is not
 * 4 / 2 gets order 8 (one in order 1 gets its saved order +0x2f = 8 instead). */
int bf_48f079(int g)
{
    if (BG8(g, 0x56652e) != 0 || BG8(g, 0x566528) == 1 || BG8(g, 0x566521) != 0)
        return 0;
    BG8(g, 0x566530) = 0;
    for (int i = 1; i < 81; i++) {
        if (BU8(i, 0x5544ac) == 0 || BS16(i, 0x5545f8) != g)
            continue;
        int o = BS8(i, 0x5544b1);
        if (o == 4 || o == 2)
            continue;
        if (o == 1)
            BU8(i, 0x5544af) = 8;
        else
            BU8(i, 0x5544b1) = 8;
    }
    return 0;
}

/* the 3×3 ring of cells around (x, y), clipped to the field: flags byte +1 of each (centre first, then N, NE,
 * E, SE, S, SW, W, NW — retail's order) */
#define RING(x, y, OP)                                                         \
    do {                                                                       \
        int o_ = 8 * ((x) + 80 * (y));                                         \
        OP(o_, 0x5440e1);                                                      \
        if ((y) > 0) OP(o_, 0x543e61);                                         \
        if ((x) < 79 && (y) > 0) OP(o_, 0x543e69);                             \
        if ((x) < 79) {                                                        \
            OP(o_, 0x5440e9);                                                  \
            if ((y) < 79) OP(o_, 0x544369);                                    \
        }                                                                      \
        if ((y) < 79) OP(o_, 0x544361);                                        \
        if ((x) > 0 && (y) < 79) OP(o_, 0x544359);                             \
        if ((x) > 0) {                                                         \
            OP(o_, 0x5440d9);                                                  \
            if ((y) > 0) OP(o_, 0x543e59);                                     \
        }                                                                      \
    } while (0)
#define CLR80(o, va) (BCELL8(o, va) &= 0x7f)
#define SET80(o, va) (BCELL8(o, va) |= 0x80)

/* Port of FUN_00491f17 (359 B) — clear the blocked bit 0x80 on the 3×3 ring of cells around (x, y) (a large
 * unit +0x194 leaving its cell). */
int bf_491f17(int x, int y)
{
    RING(x, y, CLR80);
    return 0;
}

/* Port of FUN_0049207e (359 B) — set the blocked bit 0x80 on the 3×3 ring of cells around (x, y). */
int bf_49207e(int x, int y)
{
    RING(x, y, SET80);
    return 0;
}

/* Port of FUN_00490a44 (533 B) — probe cell `off` for the current unit's step. Latches the probe in retail's
 * globals: DAT_004eead4 = off, DAT_0052eff4 = its occupant (+5), DAT_004eea94 = its kind (+7); a kind-7 cell
 * met by a slot-4 unit (+0x17a) fires FUN_0048551d at it. Returns 2 (blocked: an elevation +4 step of more
 * than one, unless the cell is at 5 — or flags 0x90 — or a kind-8 flag for a slot-4 unit, which also sets
 * DAT_00553f3c), 0 (a friendly occupant, flag 0x40 or 8), 999 (an enemy occupant), 5 (flag 0x20, a unit in
 * a slot), else 1 (free). */
int bf_490a44(int off)
{
    g_bf_4eead4 = off;
    OTHER = BCELL8(off, 0x5440e5);
    g_bf_4eea94 = BCELL8(off, 0x5440e7);
    uint8_t fl = BCELL8(off, 0x5440e1);
    uint8_t elev = BCELL8(off, 0x5440e4);
    if (g_bf_4eea94 == 7 && BS8(CUR, 0x5545fa) == 4) {
        int c = off / 8;
        bf_48551d(c % 80, c / 80);
    }
    if (elev != g_bf_4eeb24 && elev != 5) {
        if (elev < g_bf_4eeb24 - 1)
            return 2;
        if (elev > g_bf_4eeb24 + 1)
            return 2;
    }
    if (OTHER != 0)
        return BU8(CUR, 0x5544ac) == BU8(OTHER, 0x5544ac) ? 0 : 999;
    if (fl & 0x90)
        return 2;
    if (fl & 0x40)
        return 0;
    if (fl & 0x20)
        return BS8(CUR, 0x5545fa) != 0 ? 5 : 1;
    if (fl & 8) {
        if (BS8(CUR, 0x5545fa) == 4) {
            s_slot_00553f3c = 1;
            return 2;
        }
        return 0;
    }
    return 1;
}

/* Port of FUN_00490616 (1070 B) — probe the neighbour of the current unit in direction d (0..7; a large
 * unit +0x194: FUN_00490c59). Off the field: DAT_00568944 = 1, 2. Else FUN_00490a44 on the neighbour cell;
 * d > 7 (unsigned): 0. */
int bf_490616(int d)
{
    static const int8_t dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    if (BU8(CUR, 0x554614) != 0)
        return bf_490c59(d);
    if ((unsigned)d > 7)
        return 0;
    int x = BS16(CUR, 0x5544a0), y = BS16(CUR, 0x5544a2);
    /* retail tests x then y per case (both outcomes the same: 2 with DAT_00568944 = 1) */
    if ((dx[d] > 0 && x >= 79) || (dx[d] < 0 && x <= 0) || (dy[d] > 0 && y >= 79) || (dy[d] < 0 && y <= 0)) {
        g_bf_568944 = 1;
        return 2;
    }
    return bf_490a44(BS32(CUR, 0x55449c) + 8 * (dx[d] + 80 * dy[d]));
}

/* Port of FUN_004904ec (298 B) — a side step for the current unit next to its target (Chebyshev distance
 * +0x169 <= 1): from the direction to the target, fan out both ways (+1 / -1, five times) for a neighbour
 * FUN_00490616 calls free (1). Returns it, else 8 (also for a distance > 1). */
int bf_4904ec(void)
{
    if (BU8(CUR, 0x5545e9) > 1)
        return 8;
    int b = engine_path_direction_8way(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2), BS16(CUR, 0x5544a4),
                                       BS16(CUR, 0x5544a6)) & 0xff;
    int a = b;
    for (int i = 0; i < 5; i++) {
        if (bf_490616(a) == 1)
            return a;
        if (bf_490616(b) == 1)
            return b;
        if (++a > 7)
            a = 0;
        if (--b < 0)
            b = 7;
    }
    return 8;
}

/* Port of FUN_00491b1f (1016 B) — step unit u one cell in its direction +0x18 (signed; 0..7, else nothing):
 * leave its cell (occupant +5 cleared if it is u; a large unit +0x194 releases its ring, FUN_00491f17),
 * move x / y (+0x20 / +0x22) and the cell offset +0x1c, then claim the new cell (a large unit re-blocks its
 * ring, FUN_0049207e). An occupied cell is taken only when the LAST PROBED unit DAT_0052eff4 and the
 * occupant are both in movement state 8 (+0x12). */
int bf_491b1f(int u)
{
    if (BCELL8(BS32(u, 0x55449c), 0x5440e5) == (unsigned)u)
        BCELL8(BS32(u, 0x55449c), 0x5440e5) = 0;
    if (BU8(u, 0x554614) != 0)
        bf_491f17(BS16(u, 0x5544a0), BS16(u, 0x5544a2));
    int d = BS8(u, 0x554498);
    switch (d) {
    case 0: BS16(u, 0x5544a2)--; BS32(u, 0x55449c) -= 640; break;
    case 1: BS16(u, 0x5544a2)--; BS16(u, 0x5544a0)++; BS32(u, 0x55449c) -= 640; BS32(u, 0x55449c) += 8; break;
    case 2: BS16(u, 0x5544a0)++; BS32(u, 0x55449c) += 8; break;
    case 3: BS16(u, 0x5544a2)++; BS16(u, 0x5544a0)++; BS32(u, 0x55449c) += 640; BS32(u, 0x55449c) += 8; break;
    case 4: BS16(u, 0x5544a2)++; BS32(u, 0x55449c) += 640; break;
    case 5: BS16(u, 0x5544a2)++; BS16(u, 0x5544a0)--; BS32(u, 0x55449c) += 640; BS32(u, 0x55449c) -= 8; break;
    case 6: BS16(u, 0x5544a0)--; BS32(u, 0x55449c) -= 8; break;
    case 7: BS16(u, 0x5544a2)--; BS16(u, 0x5544a0)--; BS32(u, 0x55449c) -= 640; BS32(u, 0x55449c) -= 8; break;
    default: return 0;
    }
    int occ = BCELL8(BS32(u, 0x55449c), 0x5440e5);
    if (occ == 0) {
        BCELL8(BS32(u, 0x55449c), 0x5440e5) = (uint8_t)u;
        if (BU8(u, 0x554614) != 0)
            bf_49207e(BS16(u, 0x5544a0), BS16(u, 0x5544a2));
    } else if (BU8(OTHER, 0x554492) == 8 && BU8(occ, 0x554492) == 8)
        BCELL8(BS32(u, 0x55449c), 0x5440e5) = (uint8_t)u;
    return 0;
}

/* Port of FUN_0049005f (1165 B) — the current unit meets a unit of its own group (DAT_0052eff4) on its next
 * cell. 1 = wait (the other is not on a +0x13 order and the battle is not quick-resolved, or the current unit
 * is in slot 4 / has no +0x16a), 0 = blocked (different movement states +0x12, either >= 7, the other in
 * order 2 / 3 / 8 — or both going to the same target: FUN_004928a3, a no-op), 1 for the other in order
 * 4 / 9 / 6; else the two swap places (x, y, cell offset, +0x19a, +0x1a0, the occupant bytes): 2. */
int bf_49005f(void)
{
    if (BU8(OTHER, 0x554493) == 0) {
        if (s_slot_0053e8fc == 0)
            return 1;
        if (BS8(CUR, 0x5545fa) == 4)
            return 1;
        if (BS8(CUR, 0x5545ea) == 0)
            return 1;
    }
    if (BU8(CUR, 0x554492) != BU8(OTHER, 0x554492))
        return 0;
    if (BU8(OTHER, 0x554492) >= 7)
        return 0;
    if (BU8(CUR, 0x554492) >= 7)
        return 0;
    switch (BS8(OTHER, 0x5544b1)) {
    case 2: case 3: case 8: return 0;
    case 4: case 9: case 6: return 1;
    default: break;
    }
    if (BS16(CUR, 0x5544a4) == BS16(OTHER, 0x5544a4) && BS16(CUR, 0x5544a6) == BS16(OTHER, 0x5544a6)) {
        bf_4928a3(CUR);
        return 0;
    }
    int me = CUR, ot = OTHER;
    int16_t x = BS16(me, 0x5544a0), y = BS16(me, 0x5544a2), s1 = BS16(me, 0x55461a), s2 = BS16(me, 0x554620);
    int32_t off = BS32(me, 0x55449c);
    BS16(me, 0x5544a0) = BS16(ot, 0x5544a0);
    BS16(me, 0x5544a2) = BS16(ot, 0x5544a2);
    BS32(me, 0x55449c) = BS32(ot, 0x55449c);
    BS16(me, 0x55461a) = BS16(ot, 0x55461a);
    BS16(me, 0x554620) = BS16(ot, 0x554620);
    BS16(ot, 0x5544a0) = x;
    BS16(ot, 0x5544a2) = y;
    BS32(ot, 0x55449c) = off;
    BS16(ot, 0x55461a) = s1;
    BS16(ot, 0x554620) = s2;
    BCELL8(BS32(me, 0x55449c), 0x5440e5) = (uint8_t)me;
    BCELL8(BS32(ot, 0x55449c), 0x5440e5) = (uint8_t)ot;
    return 2;
}
