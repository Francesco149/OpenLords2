/* engine_bf_move.c — the battlefield port, partition 'move' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * Findings: docs/findings/bf-move.md. */
#include <string.h>

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

/* ------------------------------------------------------------------------- the large units (+0x194: 3×3) */

/* FUN_00490fa2 / FUN_00491147 (identical but for the table and its row length): probe the n cells of the
 * leading edge of a 3×3 unit stepping in direction d — the current unit's cell + the n offsets at row d of
 * the table at `tab` (DAT_004d9c90: n = 3, the straight directions; DAT_004d9cf0: n = 5, the diagonals).
 * Each cell latches its occupant in DAT_0052eff4. 2 = blocked (an elevation step > 1, flags 0x90, or any
 * occupant after the scan), 6 = a cell flagged 0x20 / 0x40 met by a unit in movement state 9 (else 2),
 * 1 = free. */
static int bf_move_probe_edge(uint32_t tab, int n, int d)
{
    uint8_t occupied = 0;
    for (int i = 0; i < n; i++) {
        int off = BS32(CUR, 0x55449c) + AT(int32_t, g_bf_4d9c90, 0x4d9c90, tab + 4 * (n * d + i));
        OTHER = BCELL8(off, 0x5440e5);
        uint8_t fl = BCELL8(off, 0x5440e1);
        uint8_t elev = BCELL8(off, 0x5440e4);
        if (elev != g_bf_4eeb24) {
            if (elev < g_bf_4eeb24 - 1)
                return 2;
            if (elev > g_bf_4eeb24 + 1)
                return 2;
        }
        if (fl & 0x90)
            return 2;
        if (fl & 0x20)
            return BU8(CUR, 0x554492) == 9 ? 6 : 2;
        if (fl & 0x40)
            return BU8(CUR, 0x554492) == 9 ? 6 : 2;
        if (OTHER != 0)
            occupied++;
    }
    return occupied ? 2 : 1;
}

/* Port of FUN_00490fa2 (421 B) — the leading 3 cells of a 3×3 unit stepping straight (d = 0 / 2 / 4 / 6). */
int bf_490fa2(int d)
{
    return bf_move_probe_edge(0x4d9c90u, 3, d);
}

/* Port of FUN_00491147 (421 B) — the leading 5 cells of a 3×3 unit stepping diagonally (d = 1 / 3 / 5 / 7). */
int bf_491147(int d)
{
    return bf_move_probe_edge(0x4d9cf0u, 5, d);
}

/* Port of FUN_00490c59 (841 B) — FUN_00490616 for a 3×3 unit (+0x194): a step that would put its edge off the
 * field (x / y within 1 of the border) sets DAT_00568944 = 1 and returns 2; else its leading edge is probed
 * (FUN_00490fa2 straight, FUN_00491147 diagonal). d > 7 (unsigned): 0. */
int bf_490c59(int d)
{
    static const int8_t dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    if ((unsigned)d > 7)
        return 0;
    int x = BS16(CUR, 0x5544a0), y = BS16(CUR, 0x5544a2);
    if ((dx[d] > 0 && x >= 78) || (dx[d] < 0 && x <= 1) || (dy[d] > 0 && y >= 78) || (dy[d] < 0 && y <= 1)) {
        g_bf_568944 = 1;
        return 2;
    }
    return (d & 1) ? bf_491147(d) : bf_490fa2(d);
}

/* Port of FUN_004912ec (422 B) — a 3×3 unit's way round an obstacle: from its step direction DAT_00554474 try
 * the directions 1, 2, 3 steps clockwise (a) and anticlockwise (b) through FUN_00490c59, on the side the unit
 * committed to (+0x195: 1 = clockwise, 2 = anticlockwise, 0 = not yet: both, a first; the side that works is
 * committed). A free one becomes DAT_00554474 (1); none: the side is dropped (+0x195 = 0), 0. */
int bf_4912ec(void)
{
    int b = g_bf_554474, a = b;
    for (int i = 0; i < 3; i++) {
        if (++a > 7)
            a = 0;
        if (--b < 0)
            b = 7;
        if (BU8(CUR, 0x554615) == 0) {
            if (bf_490c59(a) == 1) {
                g_bf_554474 = a;
                BU8(CUR, 0x554615) = 1;
                return 1;
            }
            if (bf_490c59(b) == 1) {
                g_bf_554474 = b;
                BU8(CUR, 0x554615) = 2;
                return 1;
            }
        } else if (BU8(CUR, 0x554615) == 1) {
            if (bf_490c59(a) == 1) {
                g_bf_554474 = a;
                return 1;
            }
        } else if (BU8(CUR, 0x554615) == 2) {
            if (bf_490c59(b) == 1) {
                g_bf_554474 = b;
                return 1;
            }
        }
    }
    BU8(CUR, 0x554615) = 0;
    return 0;
}

/* Port of FUN_00491492 (843 B) — a 3×3 unit in movement state 8 (+0x12) docking: from its facing +0x168, try
 * the four straight sides (2 apart) for one whose cell 2 out (DAT_004d9d90) is at elevation 2 while the cell
 * 1 out (DAT_004d9db0) is not at 1. Found: the unit becomes terrain — the docking cell's flags cleared, the
 * unit removed (FUN_0046ebe4), the footprint's neighbour flags set (FUN_004921e5), each of its 3×3 cells
 * gets +2 = (+2 & 0xe3) | 4, +1 |= 1, a tile byte +3 from DAT_004d9dd0 (9 per side), the centre +2 |= 0x80;
 * the docking cell's tile +3 = 1 (sides 0 / 4) or 2 (2 / 6) and the elevation +4 of the cell 1 out = 2; the path / stage
 * scratch refreshed, the footprint pushed (FUN_0048ee46), DAT_0053e990 += 3, DAT_00553fb0 += 4, cue 17
 * (FUN_004262cf). Returns 1; else 0. */
int bf_491492(int u)
{
    if (BU8(u, 0x554492) != 8)
        return 0;
    int x = BS16(u, 0x5544a0), y = BS16(u, 0x5544a2);
    int d = BU8(u, 0x5545e8);
    int dock = 0, i;
    for (i = 0; i < 4; i++) {
        dock = BS32(u, 0x55449c) + AT(int32_t, g_bf_4d9c90, 0x4d9c90, 0x4d9d90 + 4 * d);
        int near = BS32(u, 0x55449c) + AT(int32_t, g_bf_4d9c90, 0x4d9c90, 0x4d9db0 + 4 * d);
        uint8_t e_dock = BCELL8(dock, 0x5440e4), e_near = BCELL8(near, 0x5440e4);
        if (e_dock == 2 && e_near != 1)
            break;
        d += 2;
        if (d >= 8)
            d -= 8;
    }
    if (i >= 4)
        return 0;
    BCELL8(dock, 0x5440e1) = 0;
    bf_46ebe4(u);
    /* TODO(bf-link): engine_tile_set_footprint_neighbor_flags (engine_tile.c, not linked in rhost) */
    engine_retail_call(0x4921e5u, 3, (uint32_t)x, (uint32_t)y, (uint32_t)d);
    int t = 0;
    for (int j = y - 1; y + 1 >= j; j++)
        for (int k = x - 1; x + 1 >= k; k++) {
            int c = 8 * (k + 80 * j);
            BCELL8(c, 0x5440e2) &= 0xe3;
            BCELL8(c, 0x5440e2) |= 4;
            BCELL8(c, 0x5440e1) |= 1;
            BCELL8(c, 0x5440e3) = AT(uint8_t, g_bf_4d9c90, 0x4d9c90, 0x4d9dd0 + 9 * d + t);
            t++;
            if (j == y && k == x)
                BCELL8(c, 0x5440e2) |= 0x80;
        }
    if (d == 0 || d == 4)
        BCELL8(dock, 0x5440e3) = 1;
    else if (d == 2 || d == 6)
        BCELL8(dock, 0x5440e3) = 2;
    /* the elevation +4 of the cell between the unit and the docking cell (the one 1 out) = 2 */
    if (d == 0)
        BCELL8(dock, 0x544364) = 2;   /* dock + 1 row */
    else if (d == 4)
        BCELL8(dock, 0x543e64) = 2;   /* dock - 1 row */
    else if (d == 2)
        BCELL8(dock, 0x5440dc) = 2;   /* dock - 1 col */
    else if (d == 6)
        BCELL8(dock, 0x5440ec) = 2;   /* dock + 1 col */
    /* TODO(bf-link): engine_tile_refresh_path_scratch / _stage_scratch / _push_footprint_record (engine_tile.c) */
    engine_retail_call(0x471c1fu, 0);
    engine_retail_call(0x471d30u, 0);
    engine_retail_call(0x48ee46u, 1, (uint32_t)dock);
    s_slot_0053e990 += 3;
    s_slot_00553fb0 += 4;
    bf_4262cf(17);
    return 1;
}

/* --------------------------------------------------------------------------------- the path field */
/* The battlefield's path field (DAT_00504030, 80 × 80 i16): built from the unit's cell outward over the base
 * costs DAT_00507250 (g_path_scratch); 998 marks a cell blocked by a friendly unit, >= 999 terrain. Indexed by
 * cell (row * 80 + col) — a border cell's neighbours fall OUTSIDE the field, exactly as in retail (rhost
 * aliases every global at its VA, so these accesses reach retail's neighbours; the standalone must lay the
 * blocks out contiguously). */
#define FIELD(i)  AT(int16_t, g_bf_50402e, 0x50402e, 0x504030 + 2 * (int32_t)(i))
#define STAGE(i)  AT(uint8_t, g_stage_scratch, 0x4f7d80, 0x4f7d80 + (int32_t)(i))   /* elevation band per cell */
#define DELAY(i)  AT(uint8_t, g_bf_4f276f, 0x4f276f, 0x4f2770 + (int32_t)(i))       /* extra wait per cell */
#define WAITS(i)  AT(uint8_t, g_bf_4f6470, 0x4f6470, 0x4f6470 + (int32_t)(i))       /* waits served (BFS) */
#define QUEUE(i)  AT(int32_t, g_bf_4fa820, 0x4fa820, 0x4fa820 + 4 * (int32_t)(i))   /* BFS ring, 6400 */
#define DIROFF(d) AT(int32_t, g_bf_4d6c98, 0x4d6c98, 0x4d6c98 + 4 * (d))             /* cell delta of dir d */
#define DIRDX(d)  AT(int32_t, g_bf_4d6c98, 0x4d6c98, 0x4d6cb8 + 8 * (d))
#define DIRDY(d)  AT(int32_t, g_bf_4d6c98, 0x4d6c98, 0x4d6cbc + 8 * (d))

/* Port of FUN_00471f45 (738 B) — mark the cells the current unit's path must avoid with 998 in the field:
 * every live unit of its own side (+0x2c) — of another group (+0x178), or of its own group and not moving
 * (+0x35 == 0); then each 3×3 unit's (+0x194) ring: the four diagonals, and the four sides — in order 13
 * (+0x31) only E / W (facing +0x168 0 or 4) or N / S (else). Clipped to the field. */
int bf_471f45(void)
{
    for (int i = 1; i < 81; i++) {
        if (BU8(i, 0x5544ac) == 0)
            continue;
        int x = BS16(i, 0x5544a0), y = BS16(i, 0x5544a2);
        int c = x + 80 * y;
        if (BU8(i, 0x5544ac) == BU8(CUR, 0x5544ac)) {
            if (BS16(i, 0x5545f8) != BS16(CUR, 0x5545f8))
                FIELD(c) = 998;
            else if (BU8(i, 0x5544b5) == 0)
                FIELD(c) = 998;
        }
        if (BU8(i, 0x554614) == 0)
            continue;
        if (x < 79 && y > 0)
            FIELD(c - 79) = 998;
        if (x < 79 && y < 79)
            FIELD(c + 81) = 998;
        if (x > 0 && y < 79)
            FIELD(c + 79) = 998;
        if (x > 0 && y > 0)
            FIELD(c - 81) = 998;
        if (BS8(i, 0x5544b1) != 13) {
            if (y > 0)
                FIELD(c - 80) = 998;
            if (x < 79)
                FIELD(c + 1) = 998;
            if (y < 79)
                FIELD(c + 80) = 998;
            if (x > 0)
                FIELD(c - 1) = 998;
        } else if (BU8(i, 0x5545e8) == 0 || BU8(i, 0x5545e8) == 4) {
            if (x < 79)
                FIELD(c + 1) = 998;
            if (x > 0)
                FIELD(c - 1) = 998;
        } else {
            if (y > 0)
                FIELD(c - 80) = 998;
            if (y < 79)
                FIELD(c + 80) = 998;
        }
    }
    return 0;
}

/* the 8-way direction from (x, y) to (tx, ty) (FUN_0046f434's encoding); 8 = there */
static int bf_move_dir(int x, int y, int tx, int ty)
{
    if (x > tx)
        return y > ty ? 7 : y == ty ? 6 : 5;
    if (x == tx)
        return y > ty ? 0 : y < ty ? 4 : 8;
    return y > ty ? 1 : y == ty ? 2 : 3;
}

/* FUN_004710f2 / FUN_00471718's shared body: two cheap probes from the unit's cell (x, y) toward the target
 * (tx, ty) — each steps into the first unvisited (field 0) neighbour, scanning from the direction to the
 * target clockwise (probe A: cursor DAT_0050723c, x / y DAT_004f9818 / DAT_004f9814, live DAT_00507240) or
 * anticlockwise (probe B: DAT_00507238, DAT_0050724c / DAT_00507244, DAT_004f9810), writing the step count
 * into the field. `terrain`: a step must also stay within one elevation band (DAT_004f7d80) and avoid a
 * delay cell (DAT_004f2770). Up to 80 rounds, until the target's field is set or both probes are stuck; a
 * probe standing on the target ends it. Returns field[target] != 0. Retail's loop state is all globals. */
static int bf_move_probe_pair(int x, int y, int tx, int ty, int terrain)
{
    FIELD(g_bf_4f7d70) = 1;
    QUEUE(0) = g_bf_4f7d70;
    g_bf_503018 = 0;
    g_bf_4f981c = 1;
    g_bf_507240 = 1;
    g_bf_4f9810 = 1;
    g_bf_50723c = g_bf_4f7d70;
    g_bf_507238 = g_bf_4f7d70;
    g_bf_4f9818 = x;
    g_bf_4f9814 = y;
    g_bf_50724c = x;
    g_bf_507244 = y;
    int round = 0;
    while (FIELD(g_bf_4f0378) == 0) {
        if (round++ >= 80)
            break;
        if (g_bf_507240 == 0 && g_bf_4f9810 == 0)
            break;
        if (g_bf_507240 != 0) {
            g_bf_507240 = 0;
            int v = FIELD(g_bf_50723c) + 1;
            int st = STAGE(g_bf_50723c);
            int d = bf_move_dir(g_bf_4f9818, g_bf_4f9814, tx, ty);
            if (d == 8)
                break;
            for (int i = 0; i < 8; i++) {
                int c = g_bf_50723c + DIROFF(d);
                if (FIELD(c) == 0 &&
                    (!terrain || ((STAGE(c) == st || (STAGE(c) >= st - 1 && STAGE(c) <= st + 1)) && DELAY(c) == 0))) {
                    g_bf_507240 = 1;
                    FIELD(c) = (int16_t)v;
                    g_bf_50723c = c;
                    g_bf_4f9818 += DIRDX(d);
                    g_bf_4f9814 += DIRDY(d);
                    break;
                }
                if (++d >= 8)
                    d = 0;
            }
        }
        if (g_bf_4f9810 != 0) {
            g_bf_4f9810 = 0;
            int v = FIELD(g_bf_507238) + 1;
            int st = STAGE(g_bf_507238);
            int d = bf_move_dir(g_bf_50724c, g_bf_507244, tx, ty);
            if (d == 8)
                break;
            for (int i = 0; i < 8; i++) {
                int c = g_bf_507238 + DIROFF(d);
                if (FIELD(c) == 0 &&
                    (!terrain || ((STAGE(c) == st || (STAGE(c) >= st - 1 && STAGE(c) <= st + 1)) && DELAY(c) == 0))) {
                    g_bf_4f9810 = 1;
                    FIELD(c) = (int16_t)v;
                    g_bf_507238 = c;
                    g_bf_50724c += DIRDX(d);
                    g_bf_507244 += DIRDY(d);
                    break;
                }
                if (--d < 0)
                    d = 7;
            }
        }
    }
    return FIELD(g_bf_4f0378) != 0;
}

/* Port of FUN_004710f2 (1574 B) — the cheap path try: the field reset to the base costs (FUN_004b3dcd from
 * DAT_00507250), the friendly units marked (FUN_00471f45), DAT_004f7d70 / DAT_004f0378 = the start / target
 * cells; a target marked 998 is cleared, one at >= 999 fails (0). Else DAT_0050a450 + 1 and the two terrain
 * probes (bf_move_probe_pair): 1 if they reached the target. */
int bf_4710f2(int x, int y, int tx, int ty)
{
    bf_4b3dcd((int)(uintptr_t)g_path_scratch, (int)(uintptr_t)&FIELD(0), 12800);
    bf_471f45();
    g_bf_4f7d70 = x + 80 * y;
    g_bf_4f0378 = tx + 80 * ty;
    if (FIELD(g_bf_4f0378) == 998)
        FIELD(g_bf_4f0378) = 0;
    else if (FIELD(g_bf_4f0378) >= 999)
        return 0;
    g_bf_50a450++;
    return bf_move_probe_pair(x, y, tx, ty, 1);
}

/* Port of FUN_00471718 (1287 B) — FUN_004710f2 for a unit in order 9 (+0x31): the field cleared to 0
 * (FUN_004b3e51 = memset) instead of the base costs, so only the friendly units' 998 marks count; a target
 * marked 998 fails (0); the probes ignore elevation and delays. */
int bf_471718(int x, int y, int tx, int ty)
{
    memset(&FIELD(0), 0, 12800);   /* FUN_004b3e51 */
    bf_471f45();
    g_bf_4f7d70 = x + 80 * y;
    g_bf_4f0378 = tx + 80 * ty;
    if (FIELD(g_bf_4f0378) == 998)
        return 0;
    return bf_move_probe_pair(x, y, tx, ty, 0);
}

/* Port of FUN_0047095e (1940 B) — build the path field from (x, y) to (tx, ty) for the current unit. Nothing
 * for a target within 1 (Chebyshev) or when the cheap probes (FUN_004710f2) reach it; nor with `probe_only`
 * == 1. Else the BFS: DAT_004f037c + 1, the field reset to the base costs, the wait counters DAT_004f6470
 * cleared — only the first 4096 of the 6400 cells (FUN_004b3e51(…, 0x1000)): a cell past 4096 keeps the
 * count of the searches before — the friendly units marked (FUN_00471f45); the target (DAT_004f0378, still
 * the probes') marked 998 is cleared, one at >= 999 stops. Then breadth-first from the start cell (field 1)
 * over the ring DAT_004fa820 (head DAT_00503018, tail DAT_004f981c, wrap past 6399) until the target's field
 * is set or the ring is empty: a popped cell with a delay DAT_004f2770 waits — it is re-queued until its wait
 * counter passes the delay — else each unvisited neighbour within one elevation band (N, NE, E, SE, S, SW, W,
 * NW) gets field + 1 + its own delay and is queued. No caller reads the return. */
int bf_47095e(int probe_only, int x, int y, int tx, int ty)
{
    static const int k_nb[8] = { -80, -79, 1, 81, 80, 79, -1, -81 };
    if (engine_path_dist_chebyshev(x, y, tx, ty) <= 1)
        return 0;
    if (bf_4710f2(x, y, tx, ty) != 0)
        return 1;
    if (probe_only == 1)
        return 0;
    g_bf_4f037c++;
    bf_4b3dcd((int)(uintptr_t)g_path_scratch, (int)(uintptr_t)&FIELD(0), 12800);
    memset(&WAITS(0), 0, 4096);   /* FUN_004b3e51 */
    bf_471f45();
    if (FIELD(g_bf_4f0378) == 998)
        FIELD(g_bf_4f0378) = 0;
    else if (FIELD(g_bf_4f0378) >= 999)
        return 0;
    g_bf_4f7d70 = x + 80 * y;
    g_bf_4f0378 = tx + 80 * ty;
    FIELD(g_bf_4f7d70) = 1;
    QUEUE(0) = g_bf_4f7d70;
    g_bf_503018 = 0;
    g_bf_4f981c = 1;
    while (FIELD(g_bf_4f0378) == 0 && g_bf_4f981c != g_bf_503018) {
        int c = QUEUE(g_bf_503018);
        if (DELAY(c) != 0) {
            uint8_t w = WAITS(c);
            WAITS(c) = (uint8_t)(w + 1);
            if (DELAY(c) > w) {
                QUEUE(g_bf_4f981c) = c;
                if (++g_bf_4f981c > 6399)
                    g_bf_4f981c = 0;
                if (++g_bf_503018 > 6399)
                    g_bf_503018 = 0;
                continue;
            }
        }
        int v = FIELD(c) + 1;
        int st = STAGE(c);
        for (int k = 0; k < 8; k++) {
            int n = c + k_nb[k];
            if (FIELD(n) == 0 && (STAGE(n) == st || (STAGE(n) >= st - 1 && STAGE(n) <= st + 1))) {
                FIELD(n) = (int16_t)(v + DELAY(n));
                QUEUE(g_bf_4f981c) = n;
                if (++g_bf_4f981c > 6399)
                    g_bf_4f981c = 0;
            }
        }
        if (++g_bf_503018 > 6399)
            g_bf_503018 = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------- engine_path on retail's globals */
/* engine_path.c's greedy walk / nearest-cell search / stash (FUN_00472392 / FUN_0047265d / FUN_00472875) work
 * on a state struct; these wrappers load it from retail's globals and store back what they change: the walk
 * DAT_004f9680 (g_bf_4f9680), its start cell DAT_004f7d70, the distance deltas DAT_00591560 / DAT_0058fea0
 * (the chebyshev call inside the walk), the cell cursor DAT_00569570 / DAT_00569578. */
extern uint8_t g_bf_4f9680[300];   /* engine_bf_move_globals.c; rhost: aliased at its VA */
static engine_path_state_t s_bf_path;

static void bf_move_path_load(void)
{
    memcpy(s_bf_path.stage_scratch, g_stage_scratch, sizeof s_bf_path.stage_scratch);
    memcpy(s_bf_path.cost_map, &FIELD(0), sizeof s_bf_path.cost_map);
    memcpy(s_bf_path.path_out, g_bf_4f9680, sizeof s_bf_path.path_out);
    s_bf_path.last_dx = g_path_dist_dx;
    s_bf_path.last_dy = g_path_dist_dy;
    s_bf_path.nearest_col = CUR_COL;
    s_bf_path.nearest_row = CUR_ROW;
    s_bf_path.start_idx = g_bf_4f7d70;
}

static void bf_move_path_store(void)
{
    for (unsigned i = 0; i < sizeof s_bf_path.path_out; i++)   /* store-if-changed (shadow page faults) */
        if (g_bf_4f9680[i] != s_bf_path.path_out[i])
            g_bf_4f9680[i] = s_bf_path.path_out[i];
    g_path_dist_dx = s_bf_path.last_dx;
    g_path_dist_dy = s_bf_path.last_dy;
    CUR_COL = s_bf_path.nearest_col;
    CUR_ROW = s_bf_path.nearest_row;
    g_bf_4f7d70 = s_bf_path.start_idx;
}

/* FUN_00472392 (engine_path_search_greedy): walk the field down from (tx, ty) to (x, y) */
static int bf_move_path_greedy(int x, int y, int tx, int ty)
{
    bf_move_path_load();
    int r = engine_path_search_greedy(&s_bf_path, x, y, tx, ty);
    bf_move_path_store();
    return r;
}

/* FUN_0047265d (engine_path_find_nearest_matching): the reachable cell nearest (tx, ty) on its elevation band.
 * Retail's cursor DAT_00569570 / 78 are the box scan's loop variables: after a miss over all 30 radii they
 * hold the end of the radius-29 box; the port stamps them only on in-band cells — put retail's back. A miss
 * that met the skip cell (x, y) leaves the cursor on it in both. */
static int bf_move_path_nearest(int x, int y, int tx, int ty)
{
    bf_move_path_load();
    int r = engine_path_find_nearest_matching(&s_bf_path, x, y, tx, ty);
    if (r == 0 && !(s_bf_path.nearest_col == x && s_bf_path.nearest_row == y)) {
        int x0 = tx - 29, y0 = ty - 29, h = 59, w = 59;
        if (x0 < 0) {
            w = x0 + 59;
            x0 = 0;
        } else if (x0 + w > 80)
            w = 80 - x0;
        if (y0 < 0) {
            h = y0 + 59;
            y0 = 0;
        } else if (y0 + h > 80)
            h = 80 - y0;
        s_bf_path.nearest_col = x0 + w;
        s_bf_path.nearest_row = y0 + h;
    }
    bf_move_path_store();
    return r;
}

/* FUN_00472875 (engine_path_stash_into_army): the walk's first n steps into unit u's path (+0x36 / +0x38) */
static void bf_move_path_stash(int u, int n)
{
    memcpy(s_bf_path.path_out, g_bf_4f9680, sizeof s_bf_path.path_out);
    engine_path_stash_into_army(&s_bf_path, &BU8(u, 0x5544a0), n);
}

/* ------------------------------------------------------------------------------------ the move order */

/* Port of FUN_0048f1dd (3714 B) — the move order's step for the current unit DAT_0053e8f8 (the order handlers
 * FUN_0048314e, FUN_004831d8, …; `wait_only` == 1: only the step pacing). Returns 0 (stopped / re-planned /
 * engaged) or 1 (stepping or still pacing); a dead unit: retail's EAX, 432 × the unit (non-zero).
 *   pacing   +0x34 bit 0 clear: a step is under way — the tick +0x33 runs to the pace +0x189, then the frame
 *            +0x32 advances by 2 to 16; past it the step is done (+0x34 |= 1)
 *   heading  +0x169 = the Chebyshev distance to the target (+0x24 / +0x26); DAT_00554474 = an adjacent enemy
 *            (FUN_004972f9), else the stored path's next step (FUN_00491a34, +0x164 set) or the straight line;
 *            none (8): the path dropped, or +0x34 |= 2 — stop
 *   probe    DAT_004eeb24 = the cell's elevation; FUN_00490616 on the next cell: 5 → order 6 (+0x2f keeps the
 *            old order, +0x19 the direction), 6 → order 14; 999 (an enemy, DAT_0052eff4) → both engage
 *            (order 4, FUN_0048f079 releases their groups' holds, +0x17e the foe, +0x185 1 / 2, FUN_00494d08
 *            / FUN_00494df9); a friend of the same group → FUN_0049005f (swap: the other paces, 2; wait: 0)
 *   blocked  (2 / 3) order 9 on a kind-2 cell: +0x190 = the cell, +0x18f = 1; else a side step
 *            (FUN_004904ec); none: re-plan unless +0x176 >= 4 — after the cooldown +0x165 (64 frames): the
 *            path field (FUN_00471718 for order 9, else FUN_0047095e), +0x166 + 1, a too-long detour
 *            (FUN_00472227) ends the move at the unit's cell; else the greedy walk (FUN_00472392; none: the
 *            nearest reachable cell, FUN_0047265d, DAT_00507248 + 1, +0x176 + 1) stored as the path
 *   3×3      (+0x194) not free: dock (FUN_00491492) or go round (FUN_004912ec); within 2 of the target: arrive
 *   step     free (1): +0x176 / +0x165 = 0, +0x34 &= ~1, +0x18 = the direction, +0x32 = 1, +0x35 = 1, the
 *            cell step (FUN_00491b1f) → 1; else order 1 with +0x30 = 100 (+0x2f the old order) → 0 */
int bf_48f1dd(int wait_only)
{
    if (BU8(CUR, 0x5544ac) == 0)
        return CUR * 432;
    g_bf_568944 = 0;
    if (BS8(CUR, 0x5544b4) & 1) {
        BU8(CUR, 0x5544b2) = 0;
        BU8(CUR, 0x5544b3) = 0;
    } else {
        BU8(CUR, 0x5544b5) = 1;
        int pace = BU8(CUR, 0x554609);
        BU8(CUR, 0x5544b3)++;
        if (BS8(CUR, 0x5544b3) <= pace)
            return 1;
        BU8(CUR, 0x5544b3) = 0;
        BU8(CUR, 0x5544b2) = (uint8_t)(BS8(CUR, 0x5544b2) + 2);   /* both multiplayer branches alike */
        if (BS8(CUR, 0x5544b2) <= 16)
            return 1;
        BU8(CUR, 0x5544b4) = (uint8_t)(BS8(CUR, 0x5544b4) | 1);
        BU8(CUR, 0x5544b2) = 0;
        if (wait_only == 1)
            return 1;
    }
    BU8(CUR, 0x5545e9) = (uint8_t)engine_path_dist_chebyshev(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2),
                                                             BS16(CUR, 0x5544a4), BS16(CUR, 0x5544a6));
    g_bf_554474 = bf_4972f9(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2), BS32(CUR, 0x55449c));
    BU8(CUR, 0x55460d) = 0;
    if (g_bf_554474 >= 8) {
        if (BU8(CUR, 0x5545e4) != 0)
            g_bf_554474 = bf_491a34(CUR);
        else
            g_bf_554474 = engine_path_direction_8way(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2),
                                                     BS16(CUR, 0x5544a4), BS16(CUR, 0x5544a6)) & 0xff;
    }
    if (g_bf_554474 >= 8) {
        if (BU8(CUR, 0x5545e4) != 0)
            BU8(CUR, 0x5545e4) = 0;
        else
            BU8(CUR, 0x5544b4) = (uint8_t)(BS8(CUR, 0x5544b4) | 2);
        return 0;
    }
    g_bf_4eeb24 = BCELL8(BS32(CUR, 0x55449c), 0x5440e4);
    int r = bf_490616(g_bf_554474);
    if (r == 5) {
        BU8(CUR, 0x5544af) = BU8(CUR, 0x5544b1);
        BU8(CUR, 0x5544b1) = 6;
        BU8(CUR, 0x554499) = (uint8_t)g_bf_554474;
        return 1;
    }
    if (r == 6) {
        BU8(CUR, 0x5544b1) = 14;
        return 0;
    }
    if (r == 1)
        BU8(CUR, 0x554615) = 0;
    if (r == 0 && BU8(CUR, 0x554614) == 0) {
        /* a friendly unit on the next cell */
        if (BS8(OTHER, 0x5544b1) == 2)
            r = 3;
        else if (BS16(CUR, 0x5545f8) != BS16(OTHER, 0x5545f8))
            r = 3;
        else {
            BU8(CUR, 0x5545e4) = 0;
            int s = bf_49005f();
            if (s == 2) {
                BU8(OTHER, 0x5544b1) = 1;
                BU8(OTHER, 0x5544af) = 3;
                BU8(OTHER, 0x5544b4) = (uint8_t)(BS8(OTHER, 0x5544b4) & 0xfd);
                BU8(OTHER, 0x5544b0) = (uint8_t)((OTHER & 1) + 1);
                BU8(OTHER, 0x5545f6) = 0;
                BU8(CUR, 0x5545f6) = 0;
                return 0;
            }
            if (s == 0) {
                BU8(CUR, 0x5544af) = BU8(CUR, 0x5544b1);
                BU8(CUR, 0x5544b1) = 1;
                BU8(CUR, 0x5544b0) = (uint8_t)((OTHER & 1) + 1);
                return 0;
            }
            r = 3;
        }
    } else if (r == 999) {
        /* an enemy on the next cell: engage */
        if (BU8(CUR, 0x554492) == 10)
            return 0;
        if (BS8(CUR, 0x5544b1) == 2)
            return 0;
        if (BS8(OTHER, 0x5544b1) == 2)
            r = 3;
        else {
            bf_48f079(BS16(CUR, 0x5545f8));
            BU8(CUR, 0x5544b1) = 4;
            BU8(CUR, 0x5545e4) = 0;
            BU8(CUR, 0x554499) = (uint8_t)g_bf_554474;
            BU8(CUR, 0x5545fe) = (uint8_t)OTHER;
            BU8(CUR, 0x554605) = 1;
            bf_494d08(CUR);
            if (BS8(OTHER, 0x5544b1) != 4) {
                bf_48f079(BS16(OTHER, 0x5545f8));
                BU8(OTHER, 0x5544b1) = 4;
                BU8(OTHER, 0x5545e4) = 0;
                BU8(OTHER, 0x554499) = (uint8_t)((g_bf_554474 + 4) % 8);
                BU8(OTHER, 0x5545fe) = (uint8_t)CUR;
                BU8(OTHER, 0x554605) = 2;
                bf_494df9(OTHER);
            }
            return 0;
        }
    }
    if (r != 1 && BU8(CUR, 0x554614) != 0) {
        /* a 3×3 unit not free to step */
        if (bf_491492(CUR) != 0)
            return 0;
        r = bf_4912ec();
        if (BU8(CUR, 0x5545e9) < 3) {
            BS16(CUR, 0x5544a4) = BS16(CUR, 0x5544a0);
            BS16(CUR, 0x5544a6) = BS16(CUR, 0x5544a2);
            return 0;
        }
    } else if (r == 2 || r == 3) {
        BU8(CUR, 0x5545e4) = 0;
        if (BS8(CUR, 0x5544b1) == 9 && g_bf_4eea94 == 2) {
            BS32(CUR, 0x554610) = g_bf_4eead4;
            BU8(CUR, 0x55460f) = 1;
            return 0;
        }
        g_bf_554474 = bf_4904ec();
        r = 0;
        if (g_bf_554474 != 8)
            r = 1;
        else if (BU8(CUR, 0x5545f6) < 4) {
            if (BU8(CUR, 0x5545e5) != 0) {
                BU8(CUR, 0x5545e5)--;
                return 0;
            }
            if (BS8(CUR, 0x5544b1) == 9)
                bf_471718(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2), BS16(CUR, 0x5544a4), BS16(CUR, 0x5544a6));
            else
                bf_47095e(0, BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2), BS16(CUR, 0x5544a4),
                          BS16(CUR, 0x5544a6));
            BS16(CUR, 0x5545e6)++;
            BU8(CUR, 0x5545e5) = 64;
            if (bf_472227(CUR) != 0) {
                BS16(CUR, 0x5544a4) = BS16(CUR, 0x5544a0);
                BS16(CUR, 0x5544a6) = BS16(CUR, 0x5544a2);
                return 0;
            }
            int n = bf_move_path_greedy(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2), BS16(CUR, 0x5544a4),
                                        BS16(CUR, 0x5544a6));
            if (n == 0) {
                g_bf_507248++;
                BU8(CUR, 0x5545f6)++;
                if (bf_move_path_nearest(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2), BS16(CUR, 0x5544a4),
                                         BS16(CUR, 0x5544a6)) != 0)
                    n = bf_move_path_greedy(BS16(CUR, 0x5544a0), BS16(CUR, 0x5544a2), CUR_COL, CUR_ROW);
            }
            if (n != 0 && n < 150) {
                bf_move_path_stash(CUR, n);
                return 0;
            }
        }
    }
    if (r != 1) {
        BU8(CUR, 0x5544af) = BU8(CUR, 0x5544b1);
        BU8(CUR, 0x5544b1) = 1;
        BU8(CUR, 0x5544b0) = 100;
        return 0;
    }
    BU8(CUR, 0x5545f6) = 0;
    BU8(CUR, 0x5545e5) = 0;
    BU8(CUR, 0x5544b4) = (uint8_t)(BS8(CUR, 0x5544b4) & 0xfe);
    BU8(CUR, 0x554498) = (uint8_t)g_bf_554474;
    BU8(CUR, 0x5544b2) = 1;
    BU8(CUR, 0x5544b5) = 1;
    bf_491b1f(CUR);
    return 1;
}
