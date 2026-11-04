/* engine_bf_setup.c — the battlefield port, partition 'setup' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * Findings: docs/findings/bf-setup.md. */
#include "engine_bf.h"
#include "rl.h"
#include "engine.h"             /* engine_rng_step_single (0x404b2c), g_sim_mp_active, the screen / redraw bridges */
#include "engine_combat.h"      /* g_combat_render_active / _anim_t0 / _msg_clock: DAT_0056d5c8 / 0053e994 / 004eeae8 */
#include "engine_map_query.h"   /* g_cell_cursor_col / _row: DAT_00569570 / DAT_00569578 (rhost: a synced mirror) */
#include "engine_math.h"        /* engine_math_lookup_threshold (0x404e4b) */
#include "engine_territory.h"   /* the bound dirty state (DAT_0053e9ac …), 0x45240a */
#include "strutil.h"            /* strutil_bounded_copy (0x401136; the port takes (dst, src, n)) */

/* the .data tables in the g_bf_4d9578 block (unit classes 0x4d96d0 / 0x4d97b0, battle-size tiers 0x4d9658 …) */
#define T4D(va) AT(int32_t, g_bf_4d9578, 0x4d9578, (va))
/* territory record field `va` (slot 0's VA) of territory t: retail 0x52f0b0, stride 420 (save.h) */
#define TERR(T, t, va) REC(T, g_per_territory_420, 0x52f0b0u, 420, (t), (va))
/* player record field `va` of player p: retail 0x57bf00, stride 352 */
#define PLR8(p, va) REC(uint8_t, g_player_strategic_352, 0x57bf00u, 352, (p), (va))
/* the deployment anchors: s_slot_00553150 (2 × 12 (x, y) dword pairs) and s_slot_00554180+0x80 (the 9 siege pairs) */
#define ANCHOR(va) AT(int32_t, s_slot_00553150, 0x553150, (va))
#define SIEGE_ANCHOR(va) AT(int32_t, s_slot_00554180, 0x554180, (va))

/* ------------------------------------------------------------------ the deployment / spawn chain */

/* Port of FUN_00480599 (201 B) — load unit class `c`'s spawn parameters: the class row 0x4d96d0 (5 dwords: per
 * block DAT_004ee8f8, block width DAT_004eeb10, rows cap DAT_004eeac0, the troop type DAT_004eeab0, +0x189 seed
 * DAT_004eeaec), then the troop-type row 0x4d97b0 (5 dwords → DAT_004eeb20 / 4eeab4 / 4eeb1c / 4eeb68 / 4eeb0c). */
int bf_480599(int c)
{
    g_bf_4ee8f8 = T4D(0x4d96d0 + 20 * c);
    g_bf_4eeb10 = T4D(0x4d96d4 + 20 * c);
    g_bf_4eeac0 = T4D(0x4d96d8 + 20 * c);
    g_bf_4eeab0 = T4D(0x4d96dc + 20 * c);
    g_bf_4eeaec = T4D(0x4d96e0 + 20 * c);
    g_bf_4eeb20 = T4D(0x4d97b0 + 20 * g_bf_4eeab0);
    g_bf_4eeab4 = T4D(0x4d97b4 + 20 * g_bf_4eeab0);
    g_bf_4eeb1c = T4D(0x4d97b8 + 20 * g_bf_4eeab0);
    g_bf_4eeb68 = T4D(0x4d97bc + 20 * g_bf_4eeab0);
    g_bf_4eeb0c = T4D(0x4d97c0 + 20 * g_bf_4eeab0);
    return g_bf_4eeb0c;
}

/* Port of FUN_0046e70e (89 B) — is there a free deployment cell near (x, y)? 0x496155 over the 20 search
 * rings; the first hit leaves the cell in DAT_00569570 / DAT_00569578. */
int bf_46e70e(int x, int y)
{
    for (int i = 0; i < 20; i++)
        if (bf_496155(x, y, i) != 0)
            return 1;
    return 0;
}

/* Port of FUN_0048169e (91 B) — a field-battle block's anchor: the (x, y) pair `slot` of side `def != 0`
 * (s_slot_00553150, 12 pairs per side) → DAT_0053f01c / DAT_0053f020. */
int bf_48169e(int slot, int def)
{
    int s = def != 0;
    g_bf_53f01c = ANCHOR(0x553150 + 96 * s + 8 * slot);
    g_bf_53f020 = ANCHOR(0x553154 + 96 * s + 8 * slot);
    return g_bf_53f020;
}

/* Port of FUN_004816f9 (383 B) — a siege block's anchor by class `c` (the slot argument is unused): classes
 * 1 / 5 → pair 0 (pair 1 once DAT_004eeafc is clear), 2..4 → 4, 0 / 6 → 8, the rest cycle the 9 siege pairs
 * (s_slot_00554180+0x80, cursor DAT_004d98c0; an empty pair restarts at 0) → DAT_0053f01c / DAT_0053f020. */
int bf_4816f9(int slot, int c)
{
    int v;
    (void)slot;
    switch (c) {
    case 0: v = 8; break;
    case 1: v = 0; break;
    case 2: case 3: case 4: v = 4; break;
    case 5: v = 0; break;
    case 6: v = 8; break;
    default: v = 9; break;
    }
    if (v == 0 && g_bf_4eeafc == 0)
        v = 1;
    if (v != 9) {
        g_bf_53f01c = ANCHOR(0x553150 + 8 * v);
        g_bf_53f020 = ANCHOR(0x553154 + 8 * v);
        return g_bf_53f020;
    }
    g_bf_53f01c = SIEGE_ANCHOR(0x554200 + 8 * g_bf_4d98c0);
    g_bf_53f020 = SIEGE_ANCHOR(0x554204 + 8 * g_bf_4d98c0);
    if (g_bf_53f01c == 0 || g_bf_53f020 == 0) {
        g_bf_4d98c0 = 0;
        g_bf_53f01c = SIEGE_ANCHOR(0x554200 + 8 * g_bf_4d98c0);
        g_bf_53f020 = SIEGE_ANCHOR(0x554204 + 8 * g_bf_4d98c0);
    }
    if (++g_bf_4d98c0 > 8)
        g_bf_4d98c0 = 0;
    return g_bf_53f020;
}

/* Port of FUN_00481878 (142 B) — a block member's column offset: member i of a `rows`-row block, `w` apart:
 * w * (i / rows) (rows clamped to 1..5; signed division). */
int bf_481878(int w, int rows, int i)
{
    if (rows <= 1)
        return w * i;
    if (rows <= 2)
        return w * (i / 2);
    if (rows <= 3)
        return w * (i / 3);
    if (rows <= 4)
        return w * (i / 4);
    return w * (i / 5);
}

/* Port of FUN_00481906 (198 B) — a block member's row offset: ±w * (i % rows) (+ for `fwd` != 0; 0 for a
 * one-row block; signed remainder). */
int bf_481906(int w, int rows, int i, int fwd)
{
    int s = fwd != 0 ? 1 : -1;
    if (rows <= 1)
        return 0;
    if (rows <= 2)
        return (int)((uint32_t)(i % 2) * (uint32_t)s * (uint32_t)w);
    if (rows <= 3)
        return (int)((uint32_t)(i % 3) * (uint32_t)s * (uint32_t)w);
    if (rows <= 4)
        return (int)((uint32_t)(i % 4) * (uint32_t)s * (uint32_t)w);
    return (int)((uint32_t)(i % 5) * (uint32_t)s * (uint32_t)w);
}

/* Port of FUN_0046e4c8 (582 B) — place one battlefield unit: the first free slot DAT_0053f014 (1..80; none → 0),
 * a free cell near (x0 + dx, y0 + dy) (0x46e70e → DAT_00569570 / DAT_00569578; none → 0), then the record:
 * position +0x20 / +0x22 (and the target +0x24 / +0x26), cell offset +0x1c (the cell's occupant byte +5 = the
 * slot), side +0x2c (+0x13 = the side's player flag 0x57bf05), class +0x12, group +0x178, facing +0x18 (4 = up
 * in the top half, else 0), +0x34 = 1, the animation phase +0xc = ((16 * x0 + 9 * slot) & 0x3f) + 0xb4. */
int bf_46e4c8(int cls, int x0, int dx, int y0, int dy, int side, int grp)
{
    for (g_bf_53f014 = 1;; g_bf_53f014++) {
        if (g_bf_53f014 >= 81)
            return 0;
        if (BU8(g_bf_53f014, 0x5544ac) == 0)
            break;
    }
    if (bf_46e70e(x0 + dx, y0 + dy) == 0)
        return 0;
    int u = g_bf_53f014;
    BS16(u, 0x5544a0) = (int16_t)g_cell_cursor_col;
    BS16(u, 0x5544a2) = (int16_t)g_cell_cursor_row;
    BS32(u, 0x55449c) = 8 * (g_cell_cursor_col + 80 * g_cell_cursor_row);
    BCELL8(BS32(u, 0x55449c), 0x5440e5) = (uint8_t)g_bf_53f014;
    BU8(u, 0x5544ac) = (uint8_t)side;
    if (PLR8(side, 0x57bf05) != 0)
        BU8(u, 0x554493) = 1;
    BU8(u, 0x554492) = (uint8_t)cls;
    BS16(u, 0x5545f8) = (int16_t)grp;
    if (BS16(u, 0x5544a2) > 40)
        BS8(u, 0x554498) = 0;
    else
        BS8(u, 0x554498) = 4;
    BS16(u, 0x5544a4) = (int16_t)g_cell_cursor_col;
    BS16(u, 0x5544a6) = (int16_t)g_cell_cursor_row;
    BU8(u, 0x5544b4) = 1;
    BU8(u, 0x55448c) = (uint8_t)(((9 * g_bf_53f014 + 16 * x0) & 0x3f) + 0xb4);
    return 1;
}

/* Port of FUN_00479473 (229 B) — side `side`'s 10-byte unit bitmap (DAT_0053e930 + 10 * side) of group `grp`:
 * cleared, then bit (u - 1) set for every unit u of that side in that group. */
int bf_479473(int grp, int side)
{
    for (int i = 0; i < 10; i++)
        AT(uint8_t, g_bf_53e930, 0x53e930, 0x53e930 + 10 * side + i) = 0;
    for (int j = 1; j < 81; j++)
        if (BU8(j, 0x5544ac) == side && BS16(j, 0x5545f8) == grp)
            AT(uint8_t, g_bf_53e930, 0x53e930, 0x53e930 + 10 * side + (j - 1) / 8) |= (uint8_t)(1 << ((j - 1) % 8));
    return 0;
}

/* Port of FUN_0046f0f0 (78 B) — clear the 80 battlefield unit records (0x46eb3f each; loop index DAT_0053e8f8). */
int bf_46f0f0(void)
{
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++)
        bf_46eb3f((int)(uintptr_t)&BU8(g_bf_53e8f8, 0x554480));
    return 0;
}

/* Port of FUN_0046f13e (79 B) — clear the 100 combat-unit records (0x46eb78 each; loop index DAT_0056d5b4). */
int bf_46f13e(void)
{
    for (g_bf_56d5b4 = 1; g_bf_56d5b4 < 101; g_bf_56d5b4++)
        bf_46eb78((int)(uintptr_t)&BC8(g_bf_56d5b4, 0x57a100));
    return 0;
}

/* Port of FUN_0046f18d (79 B) — clear the 80 battlefield group records (0x46ebae each; loop index DAT_00568458). */
int bf_46f18d(void)
{
    for (g_bf_568458 = 1; g_bf_568458 < 81; g_bf_568458++)
        bf_46ebae((int)(uintptr_t)&BG8(g_bf_568458, 0x566520));
    return 0;
}

/* Port of FUN_0047f42d (71 B) — the battle-size tier s_slot_00522f6c's row of 0x4d9800 (3 dwords) →
 * s_slot_0053e86c / s_slot_00522f54 / s_slot_00522f5c. */
int bf_47f42d(void)
{
    s_slot_0053e86c = (uint32_t)T4D(0x4d9800 + 12 * (int32_t)s_slot_00522f6c);
    s_slot_00522f54 = (uint32_t)T4D(0x4d9804 + 12 * (int32_t)s_slot_00522f6c);
    s_slot_00522f5c = (uint32_t)T4D(0x4d9808 + 12 * (int32_t)s_slot_00522f6c);
    return (int)s_slot_00522f5c;
}

/* Port of FUN_00480f2e (93 B) — every live unit's +0x180 word cleared. */
int bf_480f2e(void)
{
    for (int i = 1; i < 81; i++)
        if (BU8(i, 0x5544ac) != 0)
            BS16(i, 0x554600) = 0;
    return 0;
}

/* Port of FUN_00480662 (2252 B) — spawn one block of `count` troops of class `cls` for side `side` as a new
 * group (0x46e97a allocates it: DAT_005530ec). Troops per unit DAT_004eea9c, so ceil(count / per) units in
 * a block DAT_004eeb10 wide with DAT_004ee8e0 rows (rows cap DAT_004eeac0); the anchor (0x4816f9 for a siege
 * deployment `siege`, else 0x48169e by `pos`) → DAT_0053f01c / 4f020. Group +8 = the formation by class
 * (a siege, s_slot_0053e8fc: the defender's (pos 0) first formation-1 block becomes 9, the next two 10); +3 = pos, +2 = the
 * live count, +4 / +6 = first / last unit. Each unit (0x46e4c8 at the block offsets 0x481878 / 0x481906):
 * +0x31 = 5 (foot) / 11 (class >= 7), the siege-engine flag +0x194 (classes 7..9: 0x49207e blocks its ring),
 * +0x17a = pos, +0x2e (pos 4 → 0, 0 → 1), the facing +0x18 nudged by slot & 7 (2: +1, 7: −1; wrapped 0..7) → +0xd,
 * the class parameters (0x480599's DAT_004eeb10 …), +0x14 = `special` == 1, troops +0x1a0 = per (the last
 * unit the remainder). The player's own groups (+0 == DAT_0057c8cc) are listed in the selection strip
 * s_slot_00553400 (10 × 24 B: the 0x479473 bitmap, +0xa = 1). Then 0x4891ad (group aggregate) and the
 * group's +0x26 / +0x28 = +0x1e / +0x20. `a2` is unused. */
int bf_480662(int siege, int cls, int a2, int pos, int count, int side, int special)
{
    (void)a2;
    int nunits = count / g_bf_4eea9c;
    int rem = count % g_bf_4eea9c;
    if (rem != 0)
        nunits++;
    if (nunits >= g_bf_4eeac0)
        g_bf_4eead8 = g_bf_4eeac0 * g_bf_4eeb10;
    else
        g_bf_4eead8 = nunits * g_bf_4eeb10;
    g_bf_4ee8e0 = (nunits - 1) / g_bf_4eeac0 + 1;
    if (siege != 0)
        bf_4816f9(g_bf_4eead0, cls);
    else
        bf_48169e(g_bf_4eead0, pos);
    bf_46e97a(side, PLR8(side, 0x57bf05));
    switch (cls) {
    case 0: BG8(g_bf_5530ec, 0x566528) = 2; break;
    case 1: BG8(g_bf_5530ec, 0x566528) = 1; break;
    case 2: BG8(g_bf_5530ec, 0x566528) = 3; break;
    case 3: BG8(g_bf_5530ec, 0x566528) = 3; break;
    case 4: BG8(g_bf_5530ec, 0x566528) = 2; break;
    case 5: BG8(g_bf_5530ec, 0x566528) = 1; break;
    case 6: BG8(g_bf_5530ec, 0x566528) = 4; break;
    case 7: BG8(g_bf_5530ec, 0x566528) = 5; break;
    case 8: BG8(g_bf_5530ec, 0x566528) = 6; break;
    case 9: BG8(g_bf_5530ec, 0x566528) = 7; break;
    case 10: BG8(g_bf_5530ec, 0x566528) = 8; break;
    default: break;
    }
    if (BG8(g_bf_5530ec, 0x566528) == 1 && s_slot_0053e8fc != 0 && pos == 0) {
        if (g_bf_4eeafc == 0) {
            BG8(g_bf_5530ec, 0x566528) = 9;
            g_bf_4eeafc = 1;
        } else if (g_bf_4ee8ec <= 1) {
            BG8(g_bf_5530ec, 0x566528) = 10;
            g_bf_4ee8ec++;
        }
    }
    BG8(g_bf_5530ec, 0x566523) = (uint8_t)pos;
    BG8(g_bf_5530ec, 0x566522) = 0;
    for (int i = 0; nunits > i; i++) {
        g_bf_4eeaa8 = bf_481878(g_bf_4eeb10, g_bf_4ee8e0, i);
        g_bf_4eeae0 = bf_481906(g_bf_4eeb10, g_bf_4ee8e0, i, pos);
        if (bf_46e4c8(cls, g_bf_53f01c, g_bf_4eeaa8, g_bf_53f020, g_bf_4eeae0, side, g_bf_5530ec) == 0)
            break;
        int u = g_bf_53f014;
        BU8(u, 0x5544b1) = BU8(u, 0x554492) >= 7 ? 11 : 5;
        BG8(g_bf_5530ec, 0x566522)++;
        BU8(u, 0x554614) = 0;
        if (BU8(u, 0x554492) >= 7 && BU8(u, 0x554492) <= 9) {
            BU8(u, 0x554614) = 1;
            bf_49207e(BS16(u, 0x5544a0), BS16(u, 0x5544a2));
        }
        u = g_bf_53f014;
        BU8(u, 0x5545fa) = (uint8_t)pos;
        if (pos == 4)
            BU8(u, 0x5544ae) = 0;
        else if (pos == 0)
            BU8(u, 0x5544ae) = 1;
        int ph = g_bf_53f014 & 7;
        if (ph == 2)
            BS8(u, 0x554498) = (int8_t)(BS8(u, 0x554498) + 1);
        else if (ph == 7)
            BS8(u, 0x554498) = (int8_t)(BS8(u, 0x554498) - 1);
        if (BS8(u, 0x554498) >= 8)
            BS8(u, 0x554498) = 0;
        if (BS8(u, 0x554498) < 0)
            BS8(u, 0x554498) = 7;
        BU8(u, 0x55448d) = BU8(u, 0x554498);
        BU8(u, 0x554607) = (uint8_t)g_bf_4eeb10;
        BU8(u, 0x554608) = (uint8_t)g_bf_4ee8e0;
        BU8(u, 0x5545ea) = (uint8_t)g_bf_4eeab0;
        BS16(u, 0x5545f0) = (int16_t)g_bf_4eeb20;
        BU8(u, 0x5545eb) = (uint8_t)g_bf_4eeab4;
        BU8(u, 0x5545ec) = (uint8_t)g_bf_4eeb1c;
        BS16(u, 0x55461e) = (int16_t)g_bf_4eeb68;
        BS16(u, 0x5545ee) = (int16_t)g_bf_4eeb0c;
        BU8(u, 0x55460c) = 0;
        BU8(u, 0x554609) = (uint8_t)g_bf_4eeaec;
        BU8(u, 0x554494) = special == 1;
        if (BG16(g_bf_5530ec, 0x566524) == 0)
            BG16(g_bf_5530ec, 0x566524) = (int16_t)g_bf_53f014;
        BG16(g_bf_5530ec, 0x566526) = (int16_t)g_bf_53f014;
        BS16(u, 0x554620) = (int16_t)g_bf_4eea9c;
        if (nunits - 1 == i && rem != 0)
            BS16(u, 0x554620) = (int16_t)rem;
    }
    if (BG8(g_bf_5530ec, 0x566520) == s_slot_0057c8cc && g_bf_4eeb04 < 10) {
        bf_479473(g_bf_5530ec, (int)s_slot_0057c8cc);
        strutil_bounded_copy(&AT(uint8_t, s_slot_00553400, 0x553400, 0x553400 + 24 * g_bf_4eeb04),
                             &AT(uint8_t, g_bf_53e930, 0x53e930, 0x53e930 + 10 * (int32_t)s_slot_0057c8cc), 10);
        AT(uint8_t, s_slot_00553400, 0x553400, 0x55340a + 24 * g_bf_4eeb04) = 1;
        g_bf_4eeb04++;
    }
    bf_4891ad(g_bf_5530ec);
    BG16(g_bf_5530ec, 0x566546) = BG16(g_bf_5530ec, 0x56653e);
    BG16(g_bf_5530ec, 0x566548) = BG16(g_bf_5530ec, 0x566540);
    return BG16(g_bf_5530ec, 0x566540);
}

/* the troops per unit DAT_004eea9c of a battle: the size tier's 0x4d9658 entry, halved (64 → 32 …, < 16 → 4)
 * when the territory's army (+0x168) is at most 8 units of it — 0x47fea7 / 0x4801f8's shared head */
static void bf_troops_per_unit(int terr)
{
    int total = TERR(int32_t, terr, 0x52f218);
    g_bf_4eea9c = T4D(0x4d9658 + 4 * (int32_t)s_slot_00522f6c);
    if (total / g_bf_4eea9c <= 8 && g_bf_4eea9c >= 8) {
        if (g_bf_4eea9c < 16)
            g_bf_4eea9c = 4;
        else if (g_bf_4eea9c < 32)
            g_bf_4eea9c = 8;
        else if (g_bf_4eea9c < 64)
            g_bf_4eea9c = 16;
        else if (g_bf_4eea9c < 128)
            g_bf_4eea9c = 32;
        else
            g_bf_4eea9c = 64;
    }
}

/* spawn `n` troops of class `cls` in blocks of at most DAT_004ee8f8 units (DAT_004eead0 counts the blocks) */
static void bf_spawn_blocks(int siege, int cls, int weak, int pos, int n, int terr, int special)
{
    while (n != 0) {
        int side = TERR(int8_t, terr, 0x52f0b0);
        if (g_bf_4ee8f8 * g_bf_4eea9c > n)
            bf_480662(siege, cls, weak, pos, n, side, special);
        else
            bf_480662(siege, cls, weak, pos, g_bf_4ee8f8 * g_bf_4eea9c, side, special);
        if (g_bf_4ee8f8 * g_bf_4eea9c > n)
            n = 0;
        else
            n -= g_bf_4ee8f8 * g_bf_4eea9c;
        g_bf_4eead0++;
    }
}

/* Port of FUN_0047fea7 (849 B) — deploy territory `terr`'s army for a field battle: the troop classes in the
 * order 0x4d9870 (11; the army counts +0x16c, classes >= 7 count units, not troops), then the army's mercenary
 * block (+0x196 troops of class +0x195, `special` = 1). `weak` / `pos`: 0x480662's a2 / a3. */
int bf_47fea7(int terr, int weak, int pos)
{
    g_bf_4ee8f0 = 0;
    bf_troops_per_unit(terr);
    g_bf_4eead0 = 0;
    for (int i = 0; i <= 10; i++) {
        int cls = T4D(0x4d9870 + 4 * i);
        int n = TERR(int16_t, terr, 0x52f21c + 2 * cls);
        if (cls >= 7)
            n *= g_bf_4eea9c;
        bf_480599(cls);
        bf_spawn_blocks(0, cls, weak, pos, n, terr, 0);
    }
    int n = TERR(uint8_t, terr, 0x52f246);
    if (n != 0) {
        int cls = TERR(uint8_t, terr, 0x52f245);
        bf_480599(cls);
        bf_spawn_blocks(0, cls, weak, pos, n, terr, 1);
    }
    return 0;
}

/* Port of FUN_004801f8 (929 B) — 0x47fea7 for a siege (the defender of a castle): the 8 classes of 0x4d98a0,
 * class 10 counts units, classes 1 / 5 (archers / crossbows) spawn 3-unit blocks (DAT_004ee8f8 = 3), anchors
 * from 0x4816f9 (siege = 1); DAT_004eeafc / DAT_004ee8ec (the cavalry-formation counters) restart at 0. */
int bf_4801f8(int terr, int weak, int pos)
{
    g_bf_4ee8f0 = 0;
    bf_troops_per_unit(terr);
    g_bf_4eeafc = 0;
    g_bf_4ee8ec = 0;
    g_bf_4eead0 = 0;
    for (int i = 0; i <= 7; i++) {
        int cls = T4D(0x4d98a0 + 4 * i);
        bf_480599(cls);
        int n = TERR(int16_t, terr, 0x52f21c + 2 * cls);
        if (cls == 10)
            n *= g_bf_4eea9c;
        if (cls == 5 || cls == 1)
            g_bf_4ee8f8 = 3;
        bf_spawn_blocks(1, cls, weak, pos, n, terr, 0);
    }
    int n = TERR(uint8_t, terr, 0x52f246);
    if (n != 0) {
        int cls = TERR(uint8_t, terr, 0x52f245);
        bf_480599(cls);
        if (cls == 5 || cls == 1)
            g_bf_4ee8f8 = 3;
        bf_spawn_blocks(1, cls, weak, pos, n, terr, 1);
    }
    return 0;
}

#define S32(x) (*(int32_t *)&(x))   /* a uint32_t save slot as retail's signed dword */

/* an army's strength: sum over classes 0..6 of count × the class weight 0x4d4b98, + the mercenary block (+0x196
 * troops of class +0x195) when +0x197 is set */
static int32_t bf_army_strength(int terr)
{
    int32_t s = 0;
    for (int v = 0; v < 7; v++)
        s += TERR(int16_t, terr, 0x52f21c + 2 * v) * g_bf_4d4b98[v];
    if (TERR(uint8_t, terr, 0x52f247) != 0)
        s += g_bf_4d4b98[TERR(uint8_t, terr, 0x52f245)] * TERR(uint8_t, terr, 0x52f246);
    return s;
}

/* Port of FUN_0047efee (1087 B) — deploy both armies: the battle-size tier s_slot_00522f6c from the two armies'
 * sizes (+0x168, and their siege-unit counts, classes 7..10, through the thresholds 0x4d9618 / 0x4d95d8) and its
 * parameters (0x47f42d, 0x4d9680 / 0x4d96a8); the strengths s_slot_0054409c (attacker DAT_00568210) /
 * s_slot_00553fc0 (defender DAT_00568218) and the "outnumbered by a third" flags s_slot_00543fc0 / s_slot_0056d624;
 * the per-battle counters reset; the unit / combat-unit / group tables cleared; the attacker deployed at pos 4,
 * the defender at 0 (a siege: 0x4801f8); +0x180 cleared, 0x481b9a, 0x488dfe. Returns s_slot_00553c58. */
int bf_47efee(void)
{
    int32_t att = S32(s_slot_00568210), def = S32(s_slot_00568218);
    int32_t size = TERR(int32_t, def, 0x52f218) + TERR(int32_t, att, 0x52f218);
    int32_t engines = 0;
    for (int v = 7; v < 11; v++)
        engines += TERR(int16_t, att, 0x52f21c + 2 * v);
    for (int v = 7; v < 11; v++)
        engines += TERR(int16_t, def, 0x52f21c + 2 * v);
    int32_t w = engine_math_lookup_threshold(size / 76, &T4D(0x4d9618), 8, 1024);
    size += w * engines;
    S32(s_slot_00522f6c) = engine_math_lookup_threshold(size, &T4D(0x4d95d8), 8, 8);
    bf_47f42d();
    S32(s_slot_0056651c) = T4D(0x4d9680 + 4 * S32(s_slot_00522f6c));
    S32(s_slot_00566514) = T4D(0x4d96a8 + 4 * S32(s_slot_00522f6c));
    S32(s_slot_0054409c) = bf_army_strength(S32(s_slot_00568210));
    S32(s_slot_00553fc0) = bf_army_strength(S32(s_slot_00568218));
    s_slot_0056d5a4 = 0;
    s_slot_0055447c = s_slot_0056d5a4;
    g_bf_4eeae4 = 0;
    g_bf_5653fc = 0;
    s_slot_0053e924 = 0;
    s_slot_00568470 = 0;
    g_bf_4eeab8 = 12;
    g_bf_4eeaf8 = 0;
    s_slot_005653f4 = 0;
    g_combat_render_active = 0;
    g_bf_4ee8e4 = 0;
    s_slot_00543fc0 = S32(s_slot_00553fc0) / 3 + S32(s_slot_00553fc0) < S32(s_slot_0054409c);
    s_slot_0056d624 = S32(s_slot_0054409c) / 3 + S32(s_slot_0054409c) < S32(s_slot_00553fc0);
    bf_46f0f0();
    bf_46f13e();
    bf_46f18d();
    g_bf_4eeafc = 0;
    g_bf_4ee8ec = 0;
    g_bf_4eeb04 = 0;
    bf_47fea7(S32(s_slot_00568210), S32(s_slot_0056d624), 4);
    if (s_slot_0053e8fc != 0)
        bf_4801f8(S32(s_slot_00568218), S32(s_slot_00543fc0), 0);
    else
        bf_47fea7(S32(s_slot_00568218), S32(s_slot_00543fc0), 0);
    bf_480f2e();
    rl_481b9a();
    bf_488dfe();
    s_slot_0056897c = s_slot_0053f028;
    s_slot_00542cc8 = s_slot_00553c58;
    g_bf_4ee8e8 = 0;
    return S32(s_slot_00553c58);
}

/* Port of FUN_00480f8b (1200 B) — every live unit's sprite handles +0 / +4 by class and side (+0x2e: 1 = the
 * attacker's set DAT_0055cd30 …, 0 = the defender's DAT_0053f0d0 …; class 0 DAT_00553f2c / DAT_0053ef50, class 6
 * also +4 = DAT_0055ce58, classes 7..10 the shared DAT_00553250, others leave 0), then every combat unit's +0 =
 * DAT_00566518. Loop indices DAT_0053e8f8 / DAT_0056d5b4. */
int bf_480f8b(void)
{
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) == 0)
            continue;
        int cls = BU8(u, 0x554492);
        BS32(u, 0x554480) = 0;
        BS32(u, 0x554484) = 0;
        if (BU8(u, 0x5544ae) != 0) {
            switch (cls) {
            case 0: BS32(u, 0x554480) = g_bf_553f2c; break;
            case 1: BS32(u, 0x554480) = g_bf_55cd30; break;
            case 2: BS32(u, 0x554480) = g_bf_56d890; break;
            case 3: BS32(u, 0x554480) = g_bf_57c8bc; break;
            case 4: BS32(u, 0x554480) = g_bf_57cb28; break;
            case 5: BS32(u, 0x554480) = g_bf_53f0bc; break;
            case 6: BS32(u, 0x554480) = g_bf_553258; BS32(u, 0x554484) = g_bf_55ce58; break;
            case 7: case 8: case 9: case 10: BS32(u, 0x554480) = g_bf_553250; break;
            default: break;
            }
        } else {
            switch (cls) {
            case 0: BS32(u, 0x554480) = g_bf_53ef50; break;
            case 1: BS32(u, 0x554480) = g_bf_53f0d0; break;
            case 2: BS32(u, 0x554480) = g_bf_56d640; break;
            case 3: BS32(u, 0x554480) = g_bf_56d684; break;
            case 4: BS32(u, 0x554480) = g_bf_57c8d4; break;
            case 5: BS32(u, 0x554480) = g_bf_52afb0; break;
            case 6: BS32(u, 0x554480) = g_bf_53f650; BS32(u, 0x554484) = g_bf_55ce58; break;
            case 7: case 8: case 9: case 10: BS32(u, 0x554480) = g_bf_553250; break;
            default: break;
            }
        }
    }
    for (g_bf_56d5b4 = 1; g_bf_56d5b4 < 101; g_bf_56d5b4++)
        BC32(g_bf_56d5b4, 0x57a100) = g_bf_566518;
    return g_bf_566518;
}

/* Port of FUN_0047ed9b (595 B) — the battlefield's per-frame cursor probe: the mouse (DAT_004e65fc, DAT_004e65f4)
 * inside the grid view (0x4bc222) → the cell under it DAT_00553f88 / DAT_00553f94 (+ the scroll s_slot_0055ce70 /
 * s_slot_005678a0; row clamped to 79), its offset DAT_00553f68, its kind DAT_00553eec (15: DAT_0053e874 = 1); the
 * player's own units counted into s_slot_00565404 — RETAIL BUG: the class test (< 7) reads unit DAT_0053e8f8 (the
 * stale loop index of the last sweep, normally 81 — past the table), not the counted unit. The unit under the
 * cursor: the player's own → DAT_0057cac8 (DAT_00553078 set) or DAT_00553f90; an enemy → DAT_005533b8 (with
 * DAT_00553078 and an own unit); an empty kind-2 cell with s_slot_00553ef0 → DAT_005533b8 = the cell offset. */
int bf_47ed9b(void)
{
    g_bf_5651d0 = 0;
    g_bf_5533b8 = 0;
    g_bf_53e874[0] = 0;
    g_bf_57cac8 = 0;
    g_bf_553f90 = 0;
    /* TODO(bf-link): engine_battlefield_render_screen_in_grid / _pixel_to_col / _pixel_to_row (not linked in rhost) */
    if (engine_retail_call(0x4bc222u, 2, (uint32_t)g_bf_4e65fc, (uint32_t)g_bf_4e65f4) == 0)
        return 0;
    g_bf_5651d0 = 1;
    g_bf_553f88 = (int32_t)engine_retail_call(0x4bc2a5u, 1, (uint32_t)g_bf_4e65fc) + S32(s_slot_0055ce70);
    g_bf_553f94 = (int32_t)engine_retail_call(0x4bc2c5u, 1, (uint32_t)g_bf_4e65f4) + S32(s_slot_005678a0);
    if (g_bf_553f94 >= 80)
        g_bf_553f94 = 79;
    g_bf_553f68 = 8 * (g_bf_553f88 + 80 * g_bf_553f94);
    s_slot_00565404 = 0;
    for (int i = 1; i < 81; i++)
        if (BU8(i, 0x5544ac) != 0 && BU8(i, 0x554489) == s_slot_0057c8cc && BU8(g_bf_53e8f8, 0x554492) < 7)
            s_slot_00565404++;
    g_bf_553eec = BCELL8(g_bf_553f68, 0x5440e7);
    int occ = BCELL8(g_bf_553f68, 0x5440e5);
    int base = BCELL8(g_bf_553f68, 0x5440e4);
    if (g_bf_553eec == 15)
        g_bf_53e874[0] = 1;
    if (occ == 0) {
        if (base == 2 && s_slot_00553ef0 != 0)
            g_bf_5533b8 = g_bf_553f68;
        return 0;
    }
    if (BU8(occ, 0x5544ac) == s_slot_0057c8cc) {
        if (g_bf_553078 != 0)
            g_bf_57cac8 = occ;
        else
            g_bf_553f90 = occ;
    } else if (g_bf_553078 != 0 && s_slot_00565404 != 0) {
        g_bf_5533b8 = occ;
    }
    return occ;
}

/* ------------------------------------------------------------------ the terrain build helpers */

/* Port of FUN_0046c40c (57 B) — byte +0xb of `n` consecutive 12-byte records at `rec` cleared. Returns n. */
int bf_46c40c(int rec, int n)
{
    for (int i = 0; i < n; i++)
        ((uint8_t *)(uintptr_t)rec)[12 * i + 11] = 0;
    return n;
}

/* Port of FUN_0046c7fc (182 B) — byte `k` (0..7, an unsigned byte argument) of every one of the 6400 battlefield
 * cells cleared; the loop indices are retail's globals (DAT_00569578 rows, DAT_00569570 quads of 4 cells,
 * DAT_0053e8b8 the byte offset: they end at 80 / 20 / 0xc800). */
int bf_46c7fc(int k)
{
    uint8_t b = (uint8_t)k;
    g_bf_53e8b8 = 0;
    for (g_cell_cursor_row = 0; g_cell_cursor_row < 80; g_cell_cursor_row++)
        for (g_cell_cursor_col = 0; g_cell_cursor_col < 20; g_cell_cursor_col++, g_bf_53e8b8 += 32) {
            BCELL8(b + g_bf_53e8b8, 0x5440e0) = 0;
            BCELL8(b + g_bf_53e8b8, 0x5440e8) = 0;
            BCELL8(b + g_bf_53e8b8, 0x5440f0) = 0;
            BCELL8(b + g_bf_53e8b8, 0x5440f8) = 0;
        }
    return b;
}

/* 0x404b2c (engine_rng_step_single) on retail's globals: the single LFSR DAT_00591564 → DAT_005c9a84 */
extern int32_t g_bf_591564;   /* engine_bf_setup_globals.c; rhost: aliased at 0x591564 */
static void bf_rng_single(void)
{
    engine_rng_single_in_t in = { (uint32_t)g_bf_591564 };
    engine_rng_single_out_t o;
    engine_rng_step_single(&in, &o);
    g_bf_591564 = (int32_t)o.new_state;
    g_bf_5c9a84 = (int32_t)o.rng_7bit;
}

/* 0x46c2de (engine_territory_pick_feature, retail layout): the first of `n` 12-byte auto-tile patterns at
 * `tab` matching the 8-neighbour mask DAT_00543fe0..fc → DAT_0057c8a4 / DAT_0053e9c0 / DAT_00568200 (the
 * pattern's rotating variant +0xb); returns index + 1 (0: none). */
static int bf_pick_feature(const void *tab, int n)
{
    /* TODO(bf-link): engine_territory_pick_feature (engine_territory.c) runs the strategic map's own 48-entry
     * table with a file-static counter; the battlefield needs the (table, n) form on retail's 0x543fe0 mask */
    return (int)engine_retail_call(0x46c2deu, 2, (uint32_t)(uintptr_t)tab, (uint32_t)n);
}

/* Port of FUN_0047d79c (122 B) — a w × h block of cells from the cell cursor offset DAT_005533cc gets kind
 * (+0) `v`. Returns h. */
int bf_47d79c(int v, int w, int h)
{
    int o = g_bf_5533cc;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            BCELL8(o, 0x5440e0) = (uint8_t)v;
            o += 8;
        }
        o += 8 * (80 - w);
    }
    return h;
}

/* Port of FUN_0047d6fe (158 B) — stamp a w × h multi-cell feature from DAT_005533cc: each cell's tile +3 = `t`
 * (counting up across the block), +1 |= `flags`, kind +0 = 0. Returns h. */
int bf_47d6fe(int t, int flags, int w, int h)
{
    int o = g_bf_5533cc;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            BCELL8(o, 0x5440e3) = (uint8_t)t++;
            BCELL8(o, 0x5440e1) |= (uint8_t)flags;
            BCELL8(o, 0x5440e0) = 0;
            o += 8;
        }
        o += 8 * (80 - w);
    }
    return h;
}

/* Port of FUN_0047d816 (574 B) — the 8-neighbour mask of the cell at the cursor (DAT_005533cc; col / row
 * DAT_00569570 / DAT_00569578) for kind `k`: DAT_00543fe0 .. DAT_00543ffc (N, NE, E, SE, S, SW, W, NW) = 1 where
 * the neighbour's kind is k; a neighbour off the 80 × 80 grid counts as `edge`. Returns nothing a caller reads. */
int bf_47d816(int k, int edge)
{
    int o = g_bf_5533cc, col = g_cell_cursor_col, row = g_cell_cursor_row;
    g_bf_543fe0 = 0; g_bf_543fe4 = 0; g_bf_543fe8 = 0; g_bf_543fec = 0;
    g_bf_543ff0 = 0; g_bf_543ff4 = 0; g_bf_543ff8 = 0; g_bf_543ffc = 0;
    if (row != 0) {
        if (BCELL8(o, 0x543e60) == k)
            g_bf_543fe0 = 1;
    } else
        g_bf_543fe0 = edge;
    if (row != 0 && col != 79) {
        if (BCELL8(o, 0x543e68) == k)
            g_bf_543fe4 = 1;
    } else
        g_bf_543fe4 = edge;
    if (col == 79)
        g_bf_543fe8 = edge;
    else if (BCELL8(o, 0x5440e8) == k)
        g_bf_543fe8 = 1;
    if (col == 79 || row == 79)
        g_bf_543fec = edge;
    else if (BCELL8(o, 0x544368) == k)
        g_bf_543fec = 1;
    if (row == 79)
        g_bf_543ff0 = edge;
    else if (BCELL8(o, 0x544360) == k)
        g_bf_543ff0 = 1;
    if (col != 0 && row != 79) {
        if (BCELL8(o, 0x544358) == k)
            g_bf_543ff4 = 1;
    } else
        g_bf_543ff4 = edge;
    if (col != 0) {
        if (BCELL8(o, 0x5440d8) == k)
            g_bf_543ff8 = 1;
    } else
        g_bf_543ff8 = edge;
    if (col != 0 && row != 0) {
        if (BCELL8(o, 0x543e58) == k)
            g_bf_543ffc = 1;
    } else
        g_bf_543ffc = edge;
    return 0;
}

/* a pass over the 80 × 80 cells with retail's cursor globals: row DAT_00569578, col DAT_00569570, the cell's
 * byte offset DAT_005533cc (each ends at 80 / 80 / 0xc800) */
#define BF_FOR_CELLS \
    for (g_cell_cursor_row = 0, g_bf_5533cc = 0; g_cell_cursor_row < 80; g_cell_cursor_row++) \
        for (g_cell_cursor_col = 0; g_cell_cursor_col < 80; g_cell_cursor_col++, g_bf_5533cc += 8)

/* the clamped anchor slot of a deployment marker: the kind byte (+0) of the cell two to the right (byte_5440F0[off]:
 * the map stores the slot as code 64 + n there) − 64, 0..11 */
static int bf_anchor_slot(int o)
{
    int v = BCELL8(o, 0x5440f0) - 64;
    if (v < 0)
        v = 0;
    if (v >= 12)
        v = 11;
    return v;
}

/* the marker scan's gate slots: a kind-40 marker's = which of the 3 cells right of the one below is a 41; a
 * kind-41 marker's (row, col) = (the cell below-right is 40, its right neighbour 65 → 0 / 66 → 2 / else 1) */
static int bf_gate_slot40(int o)
{
    if (BCELL8(o, 0x544368) == 41)
        return 1;
    if (BCELL8(o, 0x544370) == 41)
        return 2;
    if (BCELL8(o, 0x544378) == 41)
        return 3;
    return 0;
}
static int bf_gate_slot41(int o)
{
    int r = BCELL8(o, 0x544368) == 40, c = 1;
    if (BCELL8(o, 0x5440e8) == 65)
        c = 0;
    else if (BCELL8(o, 0x5440e8) == 66)
        c = 2;
    return 3 * r + c;
}

/* Port of FUN_0047aaa3 (3599 B) — build a field battlefield: the cell bytes 0..7 cleared; the map number
 * s_slot_00553528 (a custom battle: s_slot_0056d590 (+10 for the second set) from file 2, else the next of the 47
 * shuffled maps s_slot_0057cae0 (cursor s_slot_005653f8) from file 0 — 14-byte names at 0x4d9108); the map's
 * 80 × 80 code bytes (offset from the file's 16-byte index entry +12..14) decoded into the cell kinds (+0; codes
 * 0x20..0x3f / 0x50..0x5f: kind 3 / 6 with a tile +3 and +1 |= 0x10); the deployment markers (kind 20 / 30 over a
 * 20 / 30, 40, 41 below) → the anchor tables s_slot_00553150 / +0x60, s_slot_00544050 / +0x20, s_slot_00522d30 /
 * +0x30 (the marker block re-kinded 1, 0x47d79c); the auto-tile pass (per cell one single-LFSR step, +2 &=
 * 0xe3; kinds 1, 4, 11, 12, 13: the neighbour mask 0x47d816 → 0x46c2de over the pattern tables 0x4d7550
 * (11 — it runs into 0x4d75c8's first entry) / 0x4d75c8 (6) / 0x4d7610 (49) / 0x4d7860 (17), counters reset by
 * 0x46c40c); the 4-cell features (kinds 7 / 8 / 9: 0x47d6fe, tiles 0x8c / 0x94 / 0x98 + their edge
 * tiles); the tile scratch refresh, 0x471da6, byte 0 cleared again. The argument is unused. */
int bf_47aaa3(int a0)
{
    (void)a0;
    bf_46c7fc(5); bf_46c7fc(6); bf_46c7fc(1); bf_46c7fc(0);
    bf_46c7fc(3); bf_46c7fc(4); bf_46c7fc(7); bf_46c7fc(2);
    int file;
    if (s_slot_0057a0f0 != 0) {
        if (s_slot_0053e91c != 0)
            s_slot_00553528 = s_slot_0056d590 + 10;
        else
            s_slot_00553528 = s_slot_0056d590;
        file = 2;
    } else {
        s_slot_00553528 = AT(uint8_t, s_slot_0057cae0, 0x57cae0, 0x57cae0 + S32(s_slot_005653f8));
        s_slot_005653f8++;
        if (S32(s_slot_005653f8) >= 47)
            s_slot_005653f8 = 0;
        file = 0;
    }
    /* TODO(bf-link): io_path_chdir_base / io_read_file_at (io_path.c / io.c, not linked in rhost) */
    engine_retail_call(0x4af502u, 0);
    uint32_t name = (uint32_t)(uintptr_t)&g_bf_4d9108[14 * file];
    engine_retail_call(0x4af8d9u, 4, name, (uint32_t)g_bf_4eabec, 0x3e8u, 0u);
    const uint8_t *idx = (const uint8_t *)(uintptr_t)g_bf_4eabec + 16 * S32(s_slot_00553528);
    uint32_t off = (uint32_t)idx[12] + ((uint32_t)idx[14] << 16) + ((uint32_t)idx[13] << 8);
    engine_retail_call(0x4af8d9u, 4, name, (uint32_t)g_bf_4eabec, 0x1900u, off);

    int n = 0;
    BF_FOR_CELLS {
        int o = g_bf_5533cc;
        uint8_t c = ((const uint8_t *)(uintptr_t)g_bf_4eabec)[n++];
        switch (c) {
        case 0x00: BCELL8(o, 0x5440e0) = 1; break;
        case 0x02: BCELL8(o, 0x5440e0) = 4; break;
        case 0x10: BCELL8(o, 0x5440e0) = 7; break;
        case 0x12: BCELL8(o, 0x5440e0) = 8; break;
        case 0x14: BCELL8(o, 0x5440e0) = 9; break;
        case 0x09: BCELL8(o, 0x5440e0) = 11; break;
        case 0x04: BCELL8(o, 0x5440e0) = 20; break;
        case 0x0f: BCELL8(o, 0x5440e0) = 30; break;
        case 0x07: BCELL8(o, 0x5440e0) = 40; break;
        case 0x08: BCELL8(o, 0x5440e0) = 41; break;
        case 0x0a: BCELL8(o, 0x5440e0) = 12; break;
        case 0x15: BCELL8(o, 0x5440e0) = 13; break;
        default:
            if (c >= 0x20 && c <= 0x3f) {
                BCELL8(o, 0x5440e0) = 3;
                BCELL8(o, 0x5440e3) = c;
                BCELL8(o, 0x5440e1) |= 0x10;
            } else if (c >= 0x50 && c <= 0x5f) {
                BCELL8(o, 0x5440e0) = 6;
                BCELL8(o, 0x5440e3) = (uint8_t)(c + 0x2c);
                BCELL8(o, 0x5440e1) |= 0x10;
            } else
                BCELL8(o, 0x5440e0) = c;
            break;
        }
    }
    for (int i = 0; i < 12; i++) {
        ANCHOR(0x553150 + 8 * i) = 0;
        ANCHOR(0x553154 + 8 * i) = 0;
        ANCHOR(0x5531b0 + 8 * i) = 0;
        ANCHOR(0x5531b4 + 8 * i) = 0;
    }
    BF_FOR_CELLS {
        int o = g_bf_5533cc, k = BCELL8(o, 0x5440e0);
        if (k != 20 && k != 30)
            continue;
        int s = k == 20 ? 0 : 1;   /* 20: the attacker's markers (pos 4), 30: the defender's (pos 0) */
        switch (BCELL8(o, 0x544360)) {
        case 20: case 30: {
            if (BCELL8(o, 0x544360) != k)
                break;
            int v = bf_anchor_slot(o);
            ANCHOR(0x553150 + 0x60 * s + 8 * v) = g_cell_cursor_col + 2;
            ANCHOR(0x553154 + 0x60 * s + 8 * v) = g_cell_cursor_row;
            bf_47d79c(1, 6, 2);
            break;
        }
        case 40: {
            int v = bf_gate_slot40(o);
            AT(int32_t, s_slot_00544050, 0x544050, 0x544050 + 0x20 * s + 8 * v) = g_cell_cursor_col + 2;
            AT(int32_t, s_slot_00544050, 0x544050, 0x544054 + 0x20 * s + 8 * v) = g_cell_cursor_row;
            bf_47d79c(1, 4, 2);
            break;
        }
        case 41: {
            int v = bf_gate_slot41(o);
            AT(int32_t, s_slot_00522d30, 0x522d30, 0x522d30 + 0x30 * s + 8 * v) = g_cell_cursor_col + 2;
            AT(int32_t, s_slot_00522d30, 0x522d30, 0x522d34 + 0x30 * s + 8 * v) = g_cell_cursor_row;
            bf_47d79c(1, 4, 2);
            break;
        }
        default:
            break;
        }
    }
    bf_46c40c((int)(uintptr_t)&g_bf_4d7550, 11);
    bf_46c40c((int)(uintptr_t)&g_bf_4d75c8, 6);
    bf_46c40c((int)(uintptr_t)&g_bf_4d7610, 49);
    bf_46c40c((int)(uintptr_t)&g_bf_4d7860, 17);
    BF_FOR_CELLS {
        bf_rng_single();
        int o = g_bf_5533cc;
        BCELL8(o, 0x5440e2) &= 0xe3;
        switch (BCELL8(o, 0x5440e0)) {
        case 1:
            bf_47d816(4, 0);
            if (bf_pick_feature(&g_bf_4d75c8, 6) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4);
            else
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_5c9a84 & 0xf);
            break;
        case 4:
            bf_47d816(4, 1);
            if (bf_pick_feature(&g_bf_4d7550, 11) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4);
            BCELL8(o, 0x5440e1) |= 0x10;
            BCELL8(o, 0x5440e1) |= 0x80;
            break;
        case 11:
            bf_47d816(11, 1);
            if (bf_pick_feature(&g_bf_4d7610, 49) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4);
            BCELL8(o, 0x5440e1) |= 0x10;
            break;
        case 12:
            bf_47d816(12, 1);
            if (bf_pick_feature(&g_bf_4d7860, 17) != 0) {
                if (g_bf_57c8a4 < 16)
                    BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4 + 16);
                else
                    BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_57c8a4 - 23);
                BCELL8(o, 0x5440e7) = 15;
            }
            break;
        case 13:
            bf_47d816(13, 1);
            if (bf_pick_feature(&g_bf_4d7860, 17) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4 - 26);
            BCELL8(o, 0x5440e1) |= 0x10;
            break;
        default:
            break;
        }
    }
    BF_FOR_CELLS {
        int o = g_bf_5533cc;
        switch (BCELL8(o, 0x5440e0)) {
        case 7:
            bf_47d6fe(0x8c, 0, 4, 2);
            BCELL8(o, 0x54435b) = 0xa8;
            BCELL8(o, 0x544361) |= 0x10;
            BCELL8(o, 0x544379) |= 0x10;
            BCELL8(o, 0x544383) = 0xa8;
            break;
        case 8:
            bf_47d6fe(0x94, 0, 4, 1);
            BCELL8(o, 0x5440db) = 0xa0;
            BCELL8(o, 0x5440e1) |= 0x10;
            BCELL8(o, 0x5440f9) |= 0x10;
            BCELL8(o, 0x544103) = 0xa0;
            break;
        case 9:
            bf_47d6fe(0x98, 0, 4, 2);
            BCELL8(o, 0x5440db) = 0xb4;
            BCELL8(o, 0x5440e1) |= 0x10;
            BCELL8(o, 0x5440f9) |= 0x10;
            BCELL8(o, 0x544103) = 0xb4;
            break;
        default:
            break;
        }
    }
    bf_tile_refresh_path();
    bf_tile_refresh_stage();
    bf_471da6();
    return bf_46c7fc(0);
}

/* ------------------------------------------------------------------ the siege map (0x47c4ba) */

/* Port of FUN_0048eefd (72 B) — the 20 (x, y) pairs at DAT_00553c80 (g_footprint_storage_160) cleared. */
int bf_48eefd(void)
{
    for (int i = 0; i < 20; i++) {
        AT(int32_t, g_footprint_storage_160, 0x553c80, 0x553c80 + 8 * i) = 0;
        AT(int32_t, g_footprint_storage_160, 0x553c80, 0x553c84 + 8 * i) = 0;
    }
    return 19;
}

/* Port of FUN_0047dc9c (50 B) — siege-map code 0xed at the cursor cell: tile +3 = 0xed, +2 &= 0xe3. */
int bf_47dc9c(void)
{
    BCELL8(g_bf_5533cc, 0x5440e3) = 0xed;
    BCELL8(g_bf_5533cc, 0x5440e2) &= 0xe3;
    return g_bf_5533cc;
}

/* Port of FUN_0047dcce (184 B) — siege-map code 0xee (water) at the cursor cell: s_slot_00553fb0 = 0, the tile
 * auto-tiled against the kind-11 neighbours (0x47d816 / 0x46c2de over 0x4d7610, 49 patterns; tile 0 if none),
 * +2 = (+2 & 0xe3) | 4, +1 |= 0x10, +7 = 2. Unverified (no corpus scenario reaches it). */
int bf_47dcce(void)
{
    s_slot_00553fb0 = 0;
    BCELL8(g_bf_5533cc, 0x5440e3) = 0;
    bf_47d816(11, 1);
    if (bf_pick_feature(&g_bf_4d7610, 49) != 0)
        BCELL8(g_bf_5533cc, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4);
    BCELL8(g_bf_5533cc, 0x5440e2) &= 0xe3;
    BCELL8(g_bf_5533cc, 0x5440e2) |= 4;
    BCELL8(g_bf_5533cc, 0x5440e1) |= 0x10;
    BCELL8(g_bf_5533cc, 0x5440e7) = 2;
    return g_bf_5533cc;
}

/* Port of FUN_0047e1dc (84 B) — siege-map code 0xef at the cursor cell: tile +3 = the single-LFSR output & 0xf,
 * +2 = (+2 & 0xe3) | 4. */
int bf_47e1dc(void)
{
    BCELL8(g_bf_5533cc, 0x5440e3) = (uint8_t)(g_bf_5c9a84 & 0xf);
    BCELL8(g_bf_5533cc, 0x5440e2) &= 0xe3;
    BCELL8(g_bf_5533cc, 0x5440e2) |= 4;
    return g_bf_5533cc;
}

/* Port of FUN_00497024 (378 B) — does any of the 8 neighbours of the cell (col, row) at offset `off` have kind
 * +7 == k (N, E, S, W, NE, SE, SW, NW; off-grid neighbours never)? */
int bf_497024(int col, int row, int off, int k)
{
    if (row > 0 && BCELL8(off, 0x543e67) == k)
        return 1;
    if (col < 79 && BCELL8(off, 0x5440ef) == k)
        return 1;
    if (row < 79 && BCELL8(off, 0x544367) == k)
        return 1;
    if (col > 0 && BCELL8(off, 0x5440df) == k)
        return 1;
    if (row > 0 && col < 79 && BCELL8(off, 0x543e6f) == k)
        return 1;
    if (col < 79 && row < 79 && BCELL8(off, 0x54436f) == k)
        return 1;
    if (row < 79 && col > 0 && BCELL8(off, 0x54435f) == k)
        return 1;
    return col > 0 && row > 0 && BCELL8(off, 0x543e5f) == k;
}

/* Port of FUN_00497247 (178 B) — does any of the 4 orthogonal neighbours of (col, row) / `off` have +1 bit 3? */
int bf_497247(int col, int row, int off)
{
    if (row > 0 && (BCELL8(off, 0x543e61) & 8) != 0)
        return 1;
    if (col < 79 && (BCELL8(off, 0x5440e9) & 8) != 0)
        return 1;
    if (row < 79 && (BCELL8(off, 0x544361) & 8) != 0)
        return 1;
    return col > 0 && (BCELL8(off, 0x5440d9) & 8) != 0;
}

/* Port of FUN_0049719e (169 B) — are all 4 orthogonal neighbours of (col, row) / `off` (on the grid) of +7 kind 2?
 * Unverified (no corpus scenario reaches it). */
int bf_49719e(int col, int row, int off)
{
    if (row > 0 && BCELL8(off, 0x543e67) != 2)
        return 0;
    if (col < 79 && BCELL8(off, 0x5440ef) != 2)
        return 0;
    if (row < 79 && BCELL8(off, 0x544367) != 2)
        return 0;
    return col <= 0 || BCELL8(off, 0x5440df) == 2;
}

/* the siege zone flood fills: repeat a full cell pass (retail's cursor globals) until a pass changes nothing */
#define BF_UNTIL_STABLE(changed) for (int changed = 1; changed != 0;) \
    for (changed = 0, g_cell_cursor_row = 0, g_bf_5533cc = 0; g_cell_cursor_row < 80; g_cell_cursor_row++) \
        for (g_cell_cursor_col = 0; g_cell_cursor_col < 80; g_cell_cursor_col++, g_bf_5533cc += 8)

/* Port of FUN_0047e263 (292 B) — flood zone 6 (+7): a cell of zone 14, or zone 0 with height +4 >= 4, next to
 * zone 6 (0x496f72) joins it (a zone-14 cell gets height 4). */
int bf_47e263(void)
{
    BF_UNTIL_STABLE(ch) {
        int z = BCELL8(g_bf_5533cc, 0x5440e7), h = BCELL8(g_bf_5533cc, 0x5440e4);
        if ((z == 14 || (z == 0 && h >= 4)) && bf_496f72(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 6) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 6;
            ch = 1;
            if (z == 14)
                BCELL8(g_bf_5533cc, 0x5440e4) = 4;
        }
    }
    return 0;
}

/* Port of FUN_0047e387 (422 B) — flood zone 5: every zone-14 cell, and a zone-0 cell of height <= 1 next to a +1
 * bit-3 cell (0x497247) or zone 7 / 5 (0x496f72). */
int bf_47e387(void)
{
    BF_UNTIL_STABLE(ch) {
        int z = BCELL8(g_bf_5533cc, 0x5440e7), h = BCELL8(g_bf_5533cc, 0x5440e4);
        if (z != 14 && !(z == 0 && h <= 1))
            continue;
        if (z == 14 || bf_497247(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc) != 0 ||
            bf_496f72(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 7) != 0 ||
            bf_496f72(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 5) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 5;
            ch = 1;
        }
    }
    return 0;
}

/* Port of FUN_0047e52d (315 B) — flood zone 4: a zone-0 cell of height != 0 next to zone 5 or 4. */
int bf_47e52d(void)
{
    BF_UNTIL_STABLE(ch) {
        if (BCELL8(g_bf_5533cc, 0x5440e7) != 0 || BCELL8(g_bf_5533cc, 0x5440e4) == 0)
            continue;
        if (bf_496f72(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 5) != 0 ||
            bf_496f72(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 4) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 4;
            ch = 1;
        }
    }
    return 0;
}

/* Port of FUN_0047e668 (330 B) — flood zone 3: a zone-14 cell, or zone 0 of height 0, with an 8-neighbour of
 * zone 4 or 3 (0x497024). */
int bf_47e668(void)
{
    BF_UNTIL_STABLE(ch) {
        int z = BCELL8(g_bf_5533cc, 0x5440e7);
        if (z != 14 && !(z == 0 && BCELL8(g_bf_5533cc, 0x5440e4) == 0))
            continue;
        if (bf_497024(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 4) != 0 ||
            bf_497024(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 3) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 3;
            ch = 1;
        }
    }
    return 0;
}

/* Port of FUN_0047e7b2 (372 B) — flood zone 1 (outside the walls) from the corners (cell 0 and the bottom-left
 * cell get zone 1): a cell of zone 0 or 3 (not 1 / 2, <= 3) and height 0 with an 8-neighbour in zone 1
 * (0x497024), or — zone 0 only — whose 4 neighbours are all zone 2 (0x49719e). */
int bf_47e7b2(void)
{
    BCELL8(0, 0x5440e7) = 1;
    BCELL8(0, 0x550667) = 1;
    BF_UNTIL_STABLE(ch) {
        int z = BCELL8(g_bf_5533cc, 0x5440e7), h = BCELL8(g_bf_5533cc, 0x5440e4);
        if (z == 1 || z == 2 || z > 3 || h > 0)
            continue;
        if (bf_497024(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 1) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 1;
            ch = 1;
        }
        if (z == 3)
            continue;
        if (bf_49719e(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 1;
            ch = 1;
        }
    }
    return 0;
}

/* Port of FUN_0047e926 (284 B) — the leftovers: a zone-0 cell next to zone 2 joins zone 3, else next to zone 6
 * joins 6. */
int bf_47e926(void)
{
    BF_UNTIL_STABLE(ch) {
        if (BCELL8(g_bf_5533cc, 0x5440e7) != 0)
            continue;
        if (bf_496f72(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 2) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 3;
            ch = 1;
        } else if (bf_496f72(g_cell_cursor_col, g_cell_cursor_row, g_bf_5533cc, 6) != 0) {
            BCELL8(g_bf_5533cc, 0x5440e7) = 6;
            ch = 1;
        }
    }
    return 0;
}

/* Port of FUN_0047e230 (51 B) — the siege map's zones (+7): the six flood fills, then byte 0 of every cell cleared. */
int bf_47e230(void)
{
    bf_47e263();
    bf_47e387();
    bf_47e52d();
    bf_47e668();
    bf_47e7b2();
    bf_47e926();
    return bf_46c7fc(0);
}

/* the castle layer file buffer DAT_004eabec (an 80-wide map of marker codes) */
#define LAYER8(o) (((uint8_t *)(uintptr_t)g_bf_4eabec)[o])

/* Port of FUN_0047d474 (68 B) — a castle-layer marker's type: byte o + 1 (returned), then o + 1 and o + 3 cleared. */
int bf_47d474(int o)
{
    int t = LAYER8(o + 1);
    LAYER8(o + 1) = 0;
    LAYER8(o + 3) = 0;
    return t;
}

/* Port of FUN_0047d4b8 (140 B) — a 2-bit marker index from the row below (o + 80, o + 81 == 7: bits 1, 0); the
 * 4 bytes cleared. */
int bf_47d4b8(int o)
{
    int v = 0;
    if (LAYER8(o + 80) == 7)
        v = 2;
    if (LAYER8(o + 81) == 7)
        v++;
    LAYER8(o + 80) = 0;
    LAYER8(o + 81) = 0;
    LAYER8(o + 82) = 0;
    LAYER8(o + 83) = 0;
    return v;
}

/* Port of FUN_0047d5ec (196 B) — a 4-bit marker index from the row below (o + 80 .. o + 83 == 7: bits 3..0); the
 * 4 bytes cleared. */
int bf_47d5ec(int o)
{
    int v = 0;
    if (LAYER8(o + 80) == 7)
        v = 8;
    if (LAYER8(o + 81) == 7)
        v += 4;
    if (LAYER8(o + 82) == 7)
        v += 2;
    if (LAYER8(o + 83) == 7)
        v++;
    LAYER8(o + 80) = 0;
    LAYER8(o + 81) = 0;
    LAYER8(o + 82) = 0;
    LAYER8(o + 83) = 0;
    return v;
}

/* Port of FUN_0047d6b0 (78 B) — a deployment marker's slot: byte o + 2 − 64 (unclamped); o + 80 / o + 81 cleared. */
int bf_47d6b0(int o)
{
    int v = LAYER8(o + 2) - 64;
    LAYER8(o + 80) = 0;
    LAYER8(o + 81) = 0;
    return v;
}

/* clamp a deployment slot to 0..11 */
static int bf_clamp_slot(int v)
{
    if (v < 0)
        v = 0;
    if (v >= 12)
        v = 11;
    return v;
}

/* Port of FUN_0047cec1 (1459 B) — the castle `castle`'s marker layer (stnfield.pl8 / stnfiel2.pl8 for a custom
 * battle: a 32-byte index entry +28..30 → 80 × 80 bytes into DAT_004eabec): the siege anchor tables cleared
 * (s_slot_00554180: 5 × 16 pairs; s_slot_0055cd90: 5 × 4 pairs), then per marker cell (code 4 = the attacker's,
 * 15 = the defender's; its type 0x47d474): the field anchors s_slot_00553150 / +0x60 (types 4 / 15, slot
 * 0x47d6b0) and the siege anchor rows (types 64 / 65 / 68 / 71 → s_slot_00554180 +0 / +0x80 / +0x100 / +0x200 by
 * 0x47d5ec, or s_slot_0055cd90 +0 / +0x20 / +0xa0 / +0x60 by 0x47d4b8; type 67 (defender): the four
 * pairs at +0x40 = (col, row − 2)), each (col + 2, row (+ 2 for the siege rows)). */
int bf_47cec1(int castle)
{
    /* TODO(bf-link): io_path_chdir_base / io_read_file_at (io_path.c / io.c, not linked in rhost) */
    engine_retail_call(0x4af502u, 0);
    engine_retail_call(0x4af8d9u, 4, (uint32_t)(uintptr_t)&g_bf_4d9ee8[s_slot_0057a0f0 != 0 ? 0x00 : 0x10],
                       (uint32_t)g_bf_4eabec, 0x3e8u, 0u);
    const uint8_t *idx = (const uint8_t *)(uintptr_t)g_bf_4eabec + 32 * castle;
    uint32_t off = (uint32_t)idx[28] + ((uint32_t)idx[30] << 16) + ((uint32_t)idx[29] << 8);
    engine_retail_call(0x4af8d9u, 4, (uint32_t)(uintptr_t)&g_bf_4d9ee8[s_slot_0057a0f0 != 0 ? 0x20 : 0x30],
                       (uint32_t)g_bf_4eabec, 0x1900u, off);
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 5; j++) {
            SIEGE_ANCHOR(0x554184 + 128 * j + 8 * i) = 0;
            SIEGE_ANCHOR(0x554180 + 128 * j + 8 * i) = 0;
        }
    for (int k = 0; k < 4; k++)
        for (int m = 0; m < 5; m++) {
            AT(int32_t, s_slot_0055cd90, 0x55cd90, 0x55cd94 + 32 * m + 8 * k) = 0;
            AT(int32_t, s_slot_0055cd90, 0x55cd90, 0x55cd90 + 32 * m + 8 * k) = 0;
        }
#define DEF_ANCHOR(va) AT(int32_t, s_slot_0055cd90, 0x55cd90, (va))
    int n = 0;
    int last = 0;
    BF_FOR_CELLS {
        int o = n++;
        uint8_t c = LAYER8(o);
        int col = g_cell_cursor_col, row = g_cell_cursor_row;
        if (c == 4) {
            int t = bf_47d474(o);
            last = t;
            int v;
            switch (t) {
            case 4:
                v = bf_clamp_slot(bf_47d6b0(o));
                ANCHOR(0x553150 + 8 * v) = col + 2;
                ANCHOR(0x553154 + 8 * v) = row;
                break;
            case 64: v = bf_47d5ec(o); SIEGE_ANCHOR(0x554180 + 8 * v) = col + 2; SIEGE_ANCHOR(0x554184 + 8 * v) = row + 2; break;
            case 65: v = bf_47d5ec(o); SIEGE_ANCHOR(0x554200 + 8 * v) = col + 2; SIEGE_ANCHOR(0x554204 + 8 * v) = row + 2; break;
            case 68: v = bf_47d5ec(o); SIEGE_ANCHOR(0x554280 + 8 * v) = col + 2; SIEGE_ANCHOR(0x554284 + 8 * v) = row + 2; break;
            case 71: v = bf_47d5ec(o); SIEGE_ANCHOR(0x554380 + 8 * v) = col + 2; SIEGE_ANCHOR(0x554384 + 8 * v) = row + 2; break;
            default: break;
            }
        } else if (c == 15) {
            int t = bf_47d474(o);
            last = t;
            int v;
            switch (t) {
            case 15:
                v = bf_clamp_slot(bf_47d6b0(o));
                ANCHOR(0x5531b0 + 8 * v) = col + 2;
                ANCHOR(0x5531b4 + 8 * v) = row;
                break;
            case 64: v = bf_47d4b8(o); DEF_ANCHOR(0x55cd90 + 8 * v) = col + 2; DEF_ANCHOR(0x55cd94 + 8 * v) = row + 2; break;
            case 65: v = bf_47d4b8(o); DEF_ANCHOR(0x55cdb0 + 8 * v) = col + 2; DEF_ANCHOR(0x55cdb4 + 8 * v) = row + 2; break;
            case 67:
                for (int p = 0; p < 4; p++) {
                    DEF_ANCHOR(0x55cdd0 + 8 * p) = col;
                    DEF_ANCHOR(0x55cdd4 + 8 * p) = row - 2;
                }
                break;
            case 68: v = bf_47d4b8(o); DEF_ANCHOR(0x55ce30 + 8 * v) = col + 2; DEF_ANCHOR(0x55ce34 + 8 * v) = row + 2; break;
            case 71: v = bf_47d4b8(o); DEF_ANCHOR(0x55cdf0 + 8 * v) = col + 2; DEF_ANCHOR(0x55cdf4 + 8 * v) = row + 2; break;
            default: break;
            }
        }
    }
#undef DEF_ANCHOR
    (void)last;
    return 0;
}

/* Port of FUN_004787a4 (290 B) — a siege of the current army's castle (DAT_0056822c; army +0x1c3 == 2) resumes
 * its saved siege state (+0x1e4 / +0x1e6 words, +0x1e8 / +0x1ec dwords, +0x1f0 / +0x1f1 bytes → s_slot_0057a0d8,
 * s_slot_0056d648, s_slot_0053e990, s_slot_00553fb0, s_slot_0055307c, s_slot_00569588) and reloads the castle's
 * scenario (castles_read_scenario); any other battle zeroes those army fields. */
int bf_4787a4(void)
{
#define ARMY(T, va) REC(T, g_per_army_768, 0x53f9b0u, 768, S32(s_slot_0056822c), (va))
    if (ARMY(uint8_t, 0x53fb73) == 2) {
        S32(s_slot_0057a0d8) = ARMY(int16_t, 0x53fb94);
        S32(s_slot_0056d648) = ARMY(int16_t, 0x53fb96);
        S32(s_slot_0053e990) = ARMY(int32_t, 0x53fb98);
        S32(s_slot_00553fb0) = ARMY(int32_t, 0x53fb9c);
        s_slot_0055307c = ARMY(uint8_t, 0x53fba0);
        s_slot_00569588 = ARMY(uint8_t, 0x53fba1);
        /* TODO(bf-link): castles_read_scenario (save.c, not linked in rhost; the port takes a path) */
        return (int)engine_retail_call(0x4aea70u, 1, s_slot_0056822c);
    }
    ARMY(int16_t, 0x53fb94) = 0;
    ARMY(int16_t, 0x53fb96) = 0;
    ARMY(int32_t, 0x53fb98) = 0;
    ARMY(int32_t, 0x53fb9c) = 0;
    ARMY(uint8_t, 0x53fba0) = 0;
    ARMY(uint8_t, 0x53fba1) = 0;
    return S32(s_slot_0056822c) << 8;
#undef ARMY
}

/* Port of FUN_0047c4ba (2567 B) — build a siege battlefield for castle `castle` (a custom battle: s_slot_0056d590,
 * also into s_slot_0055401c): the cell bytes cleared; the castle map (batfield.pl8 name slot 1 / 3: a 32-byte
 * index entry +12..14) decoded — pass 1: code 0xee → kind 11 (water), else 1; pass 2: codes 0xed..0xef (one
 * single-LFSR step each: 0x47dc9c / 0x47dcce / 0x47e1dc), any other code is the tile +3 whose height /
 * walk flag come from 0x4d7b80 (a stone castle, s_slot_0057c910) or 0x4d7d80, then the height classes (+4) 5..12
 * become the wall / gate / tower cells (+7 zones 6, 7, 8, 11, 14; the first gate s_slot_00553274, the gatehouse
 * s_slot_00553ee4 / s_slot_0053e9d4 …); the siege counters reset (s_slot_00553fb0 = 500 …; the castle flags
 * s_slot_0055307c / g_bf_542cd4 / g_bf_57cb48 from 0x4d4a98 / 0x4d4ad8 / 0x4d4a18 in a custom battle); the tile
 * scratch; the marker layer (0x47cec1) and the zones (0x47e230); DAT_004eeb64 from the defender's anchor
 * pair 71 of the gate DAT_0056d6b0; DAT_004eeb6c = an RNG roll below 128·(siege towers) − 20·(rams) − 10·(catapults)
 * of the attacker; 0x471da6, 0x4787a4, the path scratch again. */
int bf_47c4ba(int castle)
{
    bf_46c7fc(5); bf_46c7fc(6); bf_46c7fc(1); bf_46c7fc(0);
    bf_46c7fc(3); bf_46c7fc(4); bf_46c7fc(7); bf_46c7fc(2);
    int file;
    if (s_slot_0057a0f0 != 0) {
        s_slot_0055401c = s_slot_0056d590;
        s_slot_00553528 = s_slot_0056d590;
        file = 3;
    } else {
        S32(s_slot_00553528) = castle;
        file = 1;
    }
    /* TODO(bf-link): io_path_chdir_base / io_read_file_at (io_path.c / io.c, not linked in rhost) */
    engine_retail_call(0x4af502u, 0);
    uint32_t name = (uint32_t)(uintptr_t)&g_bf_4d9108[14 * file];
    engine_retail_call(0x4af8d9u, 4, name, (uint32_t)g_bf_4eabec, 0x3e8u, 0u);
    const uint8_t *idx = (const uint8_t *)(uintptr_t)g_bf_4eabec + 32 * S32(s_slot_00553528);
    uint32_t off = ((uint32_t)idx[14] << 16) + ((uint32_t)idx[13] << 8) + (uint32_t)idx[12];
    engine_retail_call(0x4af8d9u, 4, name, (uint32_t)g_bf_4eabec, 0x1900u, off);
    s_slot_0057a0d8 = 0;
    s_slot_0056d648 = 0;
    bf_48eefd();
    s_slot_00553fb0 = 500;
    s_slot_0052af9c = 0;
    s_slot_00554034 = 0;
    s_slot_00568da4 = 0;
    if (s_slot_0057a0f0 != 0)
        s_slot_0055307c = (uint32_t)AT(int32_t, g_bf_4d4a98, 0x4d4a98, 0x4d4a98 + 4 * S32(s_slot_0056d590));
    else
        s_slot_0055307c = s_slot_0055401c == 0 || s_slot_0055401c == 3;
    if (s_slot_0057a0f0 != 0)
        g_bf_542cd4 = AT(int32_t, g_bf_4d4a98, 0x4d4a98, 0x4d4ad8 + 4 * S32(s_slot_0056d590));
    else
        g_bf_542cd4 = s_slot_0055401c == 1;
    if (s_slot_0057a0f0 != 0)
        g_bf_57cb48 = g_bf_4d4a18[S32(s_slot_0056d590)];
    else
        g_bf_57cb48 = 0;
    s_slot_00569588 = 0;
    s_slot_00553270 = 0;
    s_slot_0053e990 = 0;
    s_slot_0053f680 = 0;
    s_slot_00553f3c = 0;
    s_slot_005440a8 = 0;
    s_slot_00553f9c = 0;
    s_slot_00553ee4 = 0;
    s_slot_0053e9d4 = 0;
    s_slot_0055cd74 = 0;
    s_slot_00553274 = 0;
    int n = 0;
    BF_FOR_CELLS
        BCELL8(g_bf_5533cc, 0x5440e0) = ((const uint8_t *)(uintptr_t)g_bf_4eabec)[n++] == 0xee ? 11 : 1;
    n = 0;
    BF_FOR_CELLS {
        uint8_t c = ((const uint8_t *)(uintptr_t)g_bf_4eabec)[n++];
        if (c >= 0xed && c <= 0xef) {
            bf_rng_single();
            if (c == 0xed)
                bf_47dc9c();
            else if (c == 0xee)
                bf_47dcce();
            else
                bf_47e1dc();
            continue;
        }
        int o = g_bf_5533cc;
        BCELL8(o, 0x5440e3) = c;
        BCELL8(o, 0x5440e2) &= 0xe3;
        if (s_slot_0057c910 != 0) {
            BCELL8(o, 0x5440e4) = AT(uint8_t, g_bf_4d7b80, 0x4d7b80, 0x4d7b80 + 2 * c);
            if (AT(uint8_t, g_bf_4d7b80, 0x4d7b80, 0x4d7b81 + 2 * c) == 0)
                BCELL8(o, 0x5440e1) |= 0x10;
        } else {
            BCELL8(o, 0x5440e4) = AT(uint8_t, g_bf_4d7d80, 0x4d7d80, 0x4d7d80 + 2 * c);
            if (AT(uint8_t, g_bf_4d7d80, 0x4d7d80, 0x4d7d81 + 2 * c) == 0)
                BCELL8(o, 0x5440e1) |= 0x10;
        }
        switch (BCELL8(o, 0x5440e4)) {
        case 5:
            BCELL8(o, 0x5440e1) = 0;
            BCELL8(o, 0x5440e1) |= 4;
            BCELL8(o, 0x5440e4) = 3;
            break;
        case 6:
            if (s_slot_00553274 == 0)
                S32(s_slot_00553274) = TERR(int8_t, S32(s_slot_00568210), 0x52f0b1) != 0 ? o - 0x280 : o;
            BCELL8(o, 0x5440e7) = 6;
            BCELL8(o, 0x5440e1) = 0;
            BCELL8(o, 0x5440e1) |= 8;
            BCELL8(o, 0x5440e4) = s_slot_0057c910 != 0 ? 4 : 1;
            BCELL8(o, 0x5440e2) |= 0x80;
            break;
        case 7:
            BCELL8(o, 0x5440e7) = 7;
            BCELL8(o, 0x5440e4) = 2;
            break;
        case 8:
            if (s_slot_00553ee4 == 0)
                S32(s_slot_00553ee4) = o - 0x278;
            if (++s_slot_0055cd74 == 5)
                S32(s_slot_0053e9d4) = o - 0x4f0;
            BCELL8(o, 0x5440e7) = 8;
            BCELL8(o, 0x5440e1) = 0;
            BCELL8(o, 0x5440e1) |= 0x20;
            BCELL8(o, 0x5440e1) |= 4;
            BCELL8(o, 0x5440e4) = 1;
            break;
        case 9:
            if (s_slot_0053e9d4 == 0)
                S32(s_slot_0053e9d4) = o - 0x4f0;
            BCELL8(o, 0x5440e7) = 11;
            BCELL8(o, 0x5440e1) = 0;
            BCELL8(o, 0x5440e1) |= 0x40;
            BCELL8(o, 0x5440e4) = 0;
            break;
        case 10:
            BCELL8(o, 0x5440e7) = 14;
            BCELL8(o, 0x5440e1) |= 4;
            BCELL8(o, 0x5440e4) = 0;
            break;
        case 11:
            BCELL8(o, 0x5440e1) = 0;
            BCELL8(o, 0x5440e1) |= 4;
            BCELL8(o, 0x5440e4) = 1;
            break;
        case 12:
            BCELL8(o, 0x5440e1) = 0;
            BCELL8(o, 0x5440e1) |= 4;
            BCELL8(o, 0x5440e4) = 2;
            break;
        default:
            break;
        }
    }
    bf_tile_refresh_path();
    bf_tile_refresh_stage();
    if (g_bf_57cb48 != 0)
        s_slot_00553fb0 = 500;
    g_bf_57cb48 = 0;
    if (s_slot_0057a0f0 != 0)
        bf_47cec1(S32(s_slot_0056d590));
    else
        bf_47cec1(castle);
    bf_47e230();
    g_bf_4eeb64 = 0;
    int gate = S32(s_slot_0056d6b0);
    if (AT(int32_t, s_slot_0055cd90, 0x55cd90, 0x55cdf4 + 8 * gate) <= 60) {
        if (AT(int32_t, s_slot_0055cd90, 0x55cd90, 0x55cdf0 + 8 * gate) >= 40)
            g_bf_4eeb64 = 2;
        else
            g_bf_4eeb64 = 1;
    }
    int att = S32(s_slot_00568210);
    int roll = TERR(int16_t, att, 0x52f22e) != 0 ? 128 : 0;
    roll -= 20 * TERR(int16_t, att, 0x52f22c);
    roll -= 10 * TERR(int16_t, att, 0x52f22a);
    bf_rng_step();
    g_bf_4eeb6c = g_bf_58fd7c < roll;
    bf_471da6();
    bf_4787a4();
    return (int)bf_tile_refresh_path();
}

/* ------------------------------------------------------------------ the custom-battle field (map set 3) */

/* Port of FUN_0042d806 (81 B) — read custom map `m` (80 × 80 codes, 6400 B at 5748 + 6400·m) of the file named at
 * 0x53f5fc (0x53f1e0 + 0x41c) into `buf`. Unverified (no corpus scenario reaches it). */
int bf_42d806(int m, int buf)
{
    /* TODO(bf-link): io_path_chdir_base / io_read_file_at (io_path.c / io.c, not linked in rhost) */
    engine_retail_call(0x4af502u, 0);
    /* retail: `offset byte_53F1E0 + 0x41c` — the custom map file's name buffer 0x53f5fc has no port symbol yet
     * (the standalone needs one; rhost: g_bf_53f1e0 sits at its VA, so the address is retail's) */
    uint32_t name = (uint32_t)(uintptr_t)g_bf_53f1e0 + 0x41cu;
    return (int)engine_retail_call(0x4af8d9u, 4, name, (uint32_t)buf, 0x1900u, (uint32_t)(6400 * m + 5748));
}

/* the 12 field deployment anchors around a custom map's marker at the cursor: (col, row) + the 12 offsets of
 * 0x4d9578, clamped to 1..78, into the anchor pairs at `va` */
static void bf_custom_anchors(int va)
{
    for (int j = 0; j < 12; j++) {
        int x = g_cell_cursor_col + T4D(0x4d9578 + 8 * j);
        int y = g_cell_cursor_row + T4D(0x4d957c + 8 * j);
        if (x < 1)
            x = 1;
        if (x >= 79)
            x = 78;
        if (y < 1)
            y = 1;
        if (y >= 79)
            y = 78;
        ANCHOR(va + 8 * j) = x;
        ANCHOR(va + 4 + 8 * j) = y;
    }
}

/* Port of FUN_0047b8b2 (3080 B) — 0x47aaa3 for a custom battle on a user-made map (s_slot_0053e91c == 3; map
 * s_slot_0056d590 via 0x42d806): its own code table (18 = kind 8 and a 16 one / two rows below becomes 20,
 * 32 → 3, 80 → 6, unknown → 1), one attacker / defender marker each (kind 20 / 30: the 12 anchors around it
 * (0x4d9578), the four gate pairs and the six siege pairs all at the marker — RETAIL: the attacker's marker
 * fills the defender-side tables s_slot_00544050+0x20 and the defender's +0, as listed), kinds 3 / 6 get a
 * random tile (+32 / +124 + (single-LFSR & 7)). Otherwise 0x47aaa3's passes. Unverified (no corpus scenario
 * plays a custom map). */
int bf_47b8b2(int a0)
{
    (void)a0;
    bf_46c7fc(5); bf_46c7fc(6); bf_46c7fc(1); bf_46c7fc(0);
    bf_46c7fc(3); bf_46c7fc(4); bf_46c7fc(7); bf_46c7fc(2);
    s_slot_00553528 = s_slot_0056d590;
    engine_retail_call(0x4af502u, 0);   /* TODO(bf-link): io_path_chdir_base (io_path.c, not linked in rhost) */
    bf_42d806(S32(s_slot_00553528), g_bf_4eabec);
    int n = 0;
    BF_FOR_CELLS {
        int o = g_bf_5533cc, i = n++;
        uint8_t *buf = (uint8_t *)(uintptr_t)g_bf_4eabec;
        switch (buf[i]) {
        case 0: BCELL8(o, 0x5440e0) = 1; break;
        case 2: BCELL8(o, 0x5440e0) = 4; break;
        case 16: BCELL8(o, 0x5440e0) = 7; break;
        case 20: BCELL8(o, 0x5440e0) = 9; break;
        case 18:
            BCELL8(o, 0x5440e0) = 8;
            if (buf[i + 80] == 16)
                buf[i + 80] = 20;
            if (buf[i + 160] == 16)
                buf[i + 160] = 20;
            break;
        case 9: BCELL8(o, 0x5440e0) = 11; break;
        case 4: BCELL8(o, 0x5440e0) = 20; break;
        case 15: BCELL8(o, 0x5440e0) = 30; break;
        case 32: BCELL8(o, 0x5440e0) = 3; break;
        case 80: BCELL8(o, 0x5440e0) = 6; break;
        case 10: BCELL8(o, 0x5440e0) = 12; break;
        case 21: BCELL8(o, 0x5440e0) = 13; break;
        default: BCELL8(o, 0x5440e0) = 1; break;
        }
    }
    for (int i = 0; i < 12; i++) {
        ANCHOR(0x5531b4 + 8 * i) = 0;
        ANCHOR(0x5531b0 + 8 * i) = 0;
        ANCHOR(0x553154 + 8 * i) = 0;
        ANCHOR(0x553150 + 8 * i) = 0;
    }
    BF_FOR_CELLS {
        int o = g_bf_5533cc, k = BCELL8(o, 0x5440e0);
        if (k != 20 && k != 30)
            continue;
        int s = k == 20 ? 0 : 1;
        bf_custom_anchors(0x553150 + 0x60 * s);
        for (int g = 0; g < 4; g++) {
            AT(int32_t, s_slot_00544050, 0x544050, 0x544070 - 0x20 * s + 8 * g) = g_cell_cursor_col;
            AT(int32_t, s_slot_00544050, 0x544050, 0x544074 - 0x20 * s + 8 * g) = g_cell_cursor_row;
        }
        for (int g = 0; g < 3; g++) {
            AT(int32_t, s_slot_00522d30, 0x522d30, 0x522d30 + 0x30 * s + 8 * g) = g_cell_cursor_col;
            AT(int32_t, s_slot_00522d30, 0x522d30, 0x522d34 + 0x30 * s + 8 * g) = g_cell_cursor_row;
            AT(int32_t, s_slot_00522d30, 0x522d30, 0x522d48 + 0x30 * s + 8 * g) = g_cell_cursor_col;
            AT(int32_t, s_slot_00522d30, 0x522d30, 0x522d4c + 0x30 * s + 8 * g) = g_cell_cursor_row;
        }
        BCELL8(o, 0x5440e0) = 1;
    }
    bf_46c40c((int)(uintptr_t)&g_bf_4d7550, 11);
    bf_46c40c((int)(uintptr_t)&g_bf_4d75c8, 6);
    bf_46c40c((int)(uintptr_t)&g_bf_4d7610, 49);
    bf_46c40c((int)(uintptr_t)&g_bf_4d7860, 17);
    BF_FOR_CELLS {
        bf_rng_single();
        int o = g_bf_5533cc;
        BCELL8(o, 0x5440e2) &= 0xe3;
        switch (BCELL8(o, 0x5440e0)) {
        case 1:
            bf_47d816(4, 0);
            if (bf_pick_feature(&g_bf_4d75c8, 6) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4);
            else
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_5c9a84 & 0xf);
            break;
        case 4:
            bf_47d816(4, 1);
            if (bf_pick_feature(&g_bf_4d7550, 11) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4);
            BCELL8(o, 0x5440e1) |= 0x10;
            BCELL8(o, 0x5440e1) |= 0x80;
            break;
        case 11:
            bf_47d816(11, 1);
            if (bf_pick_feature(&g_bf_4d7610, 49) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4);
            BCELL8(o, 0x5440e1) |= 0x10;
            break;
        case 3:
            BCELL8(o, 0x5440e3) = (uint8_t)((g_bf_5c9a84 & 7) + 32);
            BCELL8(o, 0x5440e1) |= 0x10;
            break;
        case 6:
            BCELL8(o, 0x5440e3) = (uint8_t)((g_bf_5c9a84 & 7) + 124);
            BCELL8(o, 0x5440e1) |= 0x10;
            break;
        case 12:
            bf_47d816(12, 1);
            if (bf_pick_feature(&g_bf_4d7860, 17) != 0) {
                if (g_bf_57c8a4 < 16)
                    BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4 + 16);
                else
                    BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_57c8a4 - 23);
                BCELL8(o, 0x5440e7) = 15;
            }
            break;
        case 13:
            bf_47d816(13, 1);
            if (bf_pick_feature(&g_bf_4d7860, 17) != 0)
                BCELL8(o, 0x5440e3) = (uint8_t)(g_bf_568200 + g_bf_57c8a4 - 26);
            BCELL8(o, 0x5440e1) |= 0x10;
            break;
        default:
            break;
        }
    }
    BF_FOR_CELLS {
        int o = g_bf_5533cc;
        switch (BCELL8(o, 0x5440e0)) {
        case 7:
            bf_47d6fe(0x8c, 0, 4, 2);
            BCELL8(o, 0x54435b) = 0xa8;
            BCELL8(o, 0x544361) |= 0x10;
            BCELL8(o, 0x544379) |= 0x10;
            BCELL8(o, 0x544383) = 0xa8;
            break;
        case 8:
            bf_47d6fe(0x94, 0, 4, 1);
            BCELL8(o, 0x5440db) = 0xa0;
            BCELL8(o, 0x5440e1) |= 0x10;
            BCELL8(o, 0x5440f9) |= 0x10;
            BCELL8(o, 0x544103) = 0xa0;
            break;
        case 9:
            bf_47d6fe(0x98, 0, 4, 2);
            BCELL8(o, 0x5440db) = 0xb4;
            BCELL8(o, 0x5440e1) |= 0x10;
            BCELL8(o, 0x5440f9) |= 0x10;
            BCELL8(o, 0x544103) = 0xb4;
            break;
        default:
            break;
        }
    }
    bf_tile_refresh_path();
    bf_tile_refresh_stage();
    bf_471da6();
    return bf_46c7fc(0);
}

/* ------------------------------------------------------------------ entering the battlefield */

/* DAT_0053e9ac (the below-status-bar redraw flag) is engine_territory's bound dirty state's field (rhost: a synced
 * mirror — writing the alias would be undone by rh_port_sync_out) */
static void bf_mark_below_status_redraw(void)
{
    engine_dirty_rect_state_t *d = engine_territory_dirty_state();
    if (d != NULL)
        d->below_status_redraw = 1;
}

/* the debug log line 0x4afab9(msg, 0, 0) */
static void bf_log(const void *msg)
{
    engine_retail_call(0x4afab9u, 3, (uint32_t)(uintptr_t)msg, 0u, 0u);   /* TODO(bf-extern): 0x4afab9 (the log) */
}

/* Port of FUN_004778a0 (655 B) — enter the battlefield (screen 0x29, sub-state s_slot_0057c8fc = 2): the battle
 * music (0x499d97 / 0x477b2f), the battle clock g_combat_anim_t0 = s_slot_005440c8 + 10, the tick cadence
 * reset; the gate / side picks s_slot_0056d6b0 (0..3) / s_slot_0053f654 (0..1) — multiplayer: cycled, else the
 * single-LFSR output's low bits; the per-battle state cleared (the selection strip s_slot_00553400, the 10 words
 * DAT_00553100, DAT_0053f238 = -1 …); the scroll (32, 33); the sprites (0x4987b7); the terrain: a siege
 * (s_slot_0053e8fc) 0x47c4ba(castle s_slot_0055401c), a custom battle of map set 3 0x47b8b2, else
 * 0x47aaa3; the deployment 0x47efee, the sprite handles 0x480f8b, one unit-driver pass 0x4822ed,
 * 0x47fc01; the full-screen redraw; multiplayer: 0x43f4b7 + 0x43fd2a twice; 0x43f304;
 * DAT_00544030 = 1. */
int bf_4778a0(void)
{
    bf_499d97();
    bf_477b2f();
    g_combat_anim_t0 = s_slot_005440c8 + 10;
    g_bf_5440a0 = g_bf_55403c;
    engine_retail_call(0x40489eu, 0);   /* TODO(bf-link): engine_tick_cadence_reset (engine_tick_cadence.c, not linked) */
    if (g_sim_mp_active != 0) {
        if (++S32(s_slot_0056d6b0) > 3)
            s_slot_0056d6b0 = 0;
        if (++S32(s_slot_0053f654) > 1)
            s_slot_0053f654 = 0;
    } else {
        bf_rng_single();
        S32(s_slot_0056d6b0) = g_bf_5c9a84 & 3;
        S32(s_slot_0053f654) = g_bf_5c9a84 & 1;
    }
    AT(int32_t, g_bf_553f14, 0x553f14, 0x553f14) = 0;
    AT(int32_t, g_bf_553f14, 0x553f14, 0x553f18) = 0;
    g_bf_553f84 = 0;
    s_slot_00553070 = 0;
    s_slot_00553f28 = 0;
    s_slot_0055322c = 0;
    s_slot_0053e9d0 = 0;
    g_combat_msg_clock = 0;
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 24; j++)
            AT(uint8_t, s_slot_00553400, 0x553400, 0x553400 + 24 * i + j) = 0;
    for (int k = 0; k < 10; k++)
        g_bf_553100[k] = 0;
    g_bf_53f238 = -1;
    g_bf_56d664 = 0;
    g_bf_507248 = 0;
    g_bf_4f037c = g_bf_507248;
    g_bf_50a450 = g_bf_4f037c;
    engine_set_current_screen(0x29);
    s_slot_0057c8fc = 2;
    s_slot_00568964 = 1;
    s_slot_0055ce70 = 32;
    s_slot_005678a0 = 33;
    bf_4987b7();
    bf_log(&AT(uint8_t, g_bf_4d9c90, 0x4d9c90, 0x4d9e88));   /* "OK :setup_battle executed." */
    if (s_slot_0053e8fc != 0)
        bf_47c4ba(S32(s_slot_0055401c));
    else if (s_slot_0057a0f0 != 0 && s_slot_0053e91c == 3)
        bf_47b8b2(S32(s_slot_0056d590));
    else
        bf_47aaa3(0);
    bf_47efee();
    bf_480f8b();
    bf_4822ed();
    bf_47fc01();
    bf_mark_below_status_redraw();
    engine_territory_mark_full_screen_dirty();
    engine_set_demand_redraw(2);
    if (g_sim_mp_active != 0) {
        /* TODO(bf-extern): 0x43f4b7 / 0x43fd2a (multiplayer sync; the ledger's "tested" is a comment mention) */
        engine_retail_call(0x43f4b7u, 0);
        engine_retail_call(0x43fd2au, 0);
        engine_retail_call(0x43f4b7u, 0);
        engine_retail_call(0x43fd2au, 0);
    }
    int r = bf_43f304();
    g_bf_544030 = 1;
    return r;
}

/* Port of FUN_00477c89 (371 B) — re-enter a battle restored from a save (0x4976a1): screen 0x29, sub-state 2,
 * DAT_0053f238 = -1; the castle s_slot_0055401c (a custom battle: s_slot_0056d590; else from the current army
 * DAT_0056822c: +0x1c3 == 1 with +0x1c1 → +0x1c1 − 1, == 2 → +0x1f9, else +0x1c0 − 1) and the stone-castle flag
 * s_slot_0057c910 (castle >= 2); the sprites, the tile scratch, 0x471da6, the sprite handles, +0x180 cleared,
 * the size-tier parameters (0x47f42d), 0x47fc01, the redraw, the music. Unverified (no corpus scenario
 * restores a battle). */
int bf_477c89(void)
{
    engine_set_current_screen(0x29);
    s_slot_0057c8fc = 2;
    g_bf_53f238 = -1;
    int a = S32(s_slot_0056822c);
#define ARMY8(va) REC(uint8_t, g_per_army_768, 0x53f9b0u, 768, a, (va))
    if (s_slot_0057a0f0 != 0)
        s_slot_0055401c = s_slot_0056d590;
    else if (ARMY8(0x53fb73) == 1 && ARMY8(0x53fb71) != 0)
        S32(s_slot_0055401c) = ARMY8(0x53fb71) - 1;
    else if (ARMY8(0x53fb73) == 2)
        S32(s_slot_0055401c) = ARMY8(0x53fba9);
    else
        S32(s_slot_0055401c) = ARMY8(0x53fb70) - 1;
#undef ARMY8
    if (s_slot_0057a0f0 == 0)
        s_slot_0057c910 = S32(s_slot_0055401c) >= 2;
    g_combat_render_active = 0;
    bf_4987b7();
    bf_tile_refresh_path();
    bf_tile_refresh_stage();
    bf_471da6();
    bf_log(&AT(uint8_t, g_bf_4d9c90, 0x4d9c90, 0x4d9eb0));   /* "OK :setup_battle reloaded." */
    bf_480f8b();
    bf_480f2e();
    bf_47f42d();
    bf_47fc01();
    bf_mark_below_status_redraw();
    engine_set_demand_redraw(2);
    bf_477b2f();
    return bf_499d97();
}
