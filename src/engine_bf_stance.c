/* engine_bf_stance.c — the battlefield port, partition 'stance' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The siege battle's group AI (docs/findings/bf-stance.md). 0x489401 (partition 'group') walks the groups with
 * its loop index DAT_00568458 as a GLOBAL (the current group: every handler here reads it, re-read after each call)
 * and, in a siege (DAT_0053e8fc), dispatches the group's stance byte (+8) through DAT_004d91d0 (the defending
 * side, group +3 != 0: stances 1..7 here) or DAT_004d91f8 (the attackers: stances 1..4, 8..10 here). Each handler
 * runs on a clock: the group's +0x1c counts up to 100 / 200 sub-steps; then (most handlers: unless the group is
 * busy, +0xd) the clock restarts, one canned step runs, and the step counter +0x1a advances.
 *
 * Group record fields used (0x566520 + 0x34 * g): +0 owner, +4/+6 first / last unit, +8 stance, +0xa target group,
 * +0xc has-target, +0xd busy, +0xf (0x48e097's gate), +0x10, +0x1a step, +0x1c clock, +0x1e/+0x20 centroid,
 * +0x22/+0x24 anchor (the order's destination, then 0x479e90), +0x2c locked enemy unit.
 *
 * The siege state the handlers read (writers in brackets):
 *   DAT_00553fb0 / DAT_0053e990  the breach counters (siege setup 0x47c4ba from the county; += 4 when the gate gives
 *                                way: 0x496b9f, 0x483a88, 0x484a0c; DAT_00553fb0 = 500 outside a siege)
 *   DAT_00542cd4                 the attackers' kit flag (0x47c4ba: DAT_0055401c == 1, MP DAT_004d4ad8)
 *   DAT_00569588                 the gate is down
 *   DAT_00553e64                 units on a kind-5 cell in formation slot 4 (0x4822ed's count): at the wall foot
 *   DAT_004eea98 > DAT_00552ff0  the battle clock (0x47fc01) past its limit (0x4983b7)
 *   0x48bff9(k)                  to the cell DAT_00553274 (k == 1, a reserve DAT_0055307c, realm 13) else DAT_00553ee4 */
#include "engine_bf.h"
#include "engine_combat.h"   /* g_combat_render_active (DAT_0056d5c8), g_combat_owner_5656f8 (DAT_005656f8) */
#include "engine_path.h"     /* engine_path_dist_manhattan: 0x404ead on DAT_00591560 / DAT_0058fea0 */

#define GRP g_bf_568458        /* DAT_00568458: 0x489401's group loop index = the current group */
#define SV(x) ((int32_t)(x))   /* a save-block dword as retail compares it (signed) */
/* the deployment points DAT_0055cd90 (save block, 192 B): dword pairs, indexed by 8 * DAT_0056d6b0 */
#define DEPLOY(va) AT(int32_t, s_slot_0055cd90, 0x55cd90u, (uint32_t)(va) + 8u * (uint32_t)SV(s_slot_0056d6b0))
/* the cell grid byte `va` (0x5440e0 + k) of the cell at column x, row y */
#define CELL_AT(x, y, va) BCELL8(8 * (80 * (y) + (x)), va)

/* the clock every handler opens with: +0x1c up by one; below `n` nothing happens; at `n` a busy group (+0xd, when
 * `gate`) waits with the clock where it is; else the clock restarts and the handler's step runs */
static int clock_due(int n, int gate)
{
    BG16(GRP, 0x56653c)++;
    if (BG16(GRP, 0x56653c) < n)
        return 0;
    if (gate && BG8(GRP, 0x56652d) != 0)
        return 0;
    BG16(GRP, 0x56653c) = 0;
    return 1;
}

/* the order every step ends with: the anchor +0x22/+0x24 = (x, y), 0x479e90(group, x, y, 0, 0, 0) */
static void order_to(int x, int y)
{
    BG16(GRP, 0x566542) = (int16_t)x;
    BG16(GRP, 0x566544) = (int16_t)y;
    bf_479e90(GRP, BG16(GRP, 0x566542), BG16(GRP, 0x566544), 0, 0, 0);
}

/* a square window of radius r around (x, y), clipped to the 80 × 80 grid, as 0x495f3d / 00496566 / 00496d76
 * compute it: top-left (x0, y0), w columns × h rows */
typedef struct { int x0, y0, w, h; } bf_win_t;
static bf_win_t window_of(int x, int y, int r)
{
    bf_win_t q = { x - r, y - r, 2 * r + 1, 2 * r + 1 };
    if (q.x0 < 0) {
        q.w += q.x0;
        q.x0 = 0;
    } else if (q.x0 + q.w > 80) {
        q.w = 80 - q.x0;
    }
    if (q.y0 < 0) {
        q.h += q.y0;
        q.y0 = 0;
    } else if (q.y0 + q.h > 80) {
        q.h = 80 - q.y0;
    }
    return q;
}

/* ================================================================ search helpers */

/* 0x495956 / 0x495c41 differ in one operation: a ranged target's distance halves / doubles */
static int nearest_enemy(int range, int u, int grp, int ranged_double)
{
    int best = 160, pick = 0;
    int lock = BG8(BS16(u, 0x5545f8), 0x56654c);
    if (BG8(BS16(u, 0x5545f8), 0x56654c) != 0) {
        g_bf_56d5d0 = lock;
        if (BU8(g_bf_56d5d0, 0x5544ac) == 0) {
            BG8(BS16(u, 0x5545f8), 0x56654c) = 0;
            lock = 0;
        }
    }
    for (g_bf_56d5d0 = 1; g_bf_56d5d0 < 81; g_bf_56d5d0++) {
        int e = g_bf_56d5d0;
        if (BU8(e, 0x5544ac) == 0 || BU8(e, 0x5544ac) == BU8(u, 0x5544ac) || BS8(e, 0x5544b1) == 2)
            continue;
        if (grp != 0 && BS16(e, 0x5545f8) != grp)
            continue;
        int d = engine_path_dist_manhattan(BS16(u, 0x5544a0), BS16(u, 0x5544a2), BS16(e, 0x5544a0), BS16(e, 0x5544a2));
        if (e == lock)
            d = 0;
        if (g_path_dist_dx > range || g_path_dist_dy > range)   /* DAT_00591560 / DAT_0058fea0: |dx|, |dy| */
            continue;
        if (BU8(e, 0x554492) >= 7) {        /* a class 7+ target (siege engines …): only a ranged hunter, +35 */
            if (BS8(u, 0x5545ea) == 0)
                continue;
            d += 35;
        } else if (BS8(e, 0x5544b1) == 9) {
            d /= 4;
        } else if (BS8(e, 0x5545ea) != 0) {
            d = ranged_double ? d * 2 : d / 2;
        }
        if (best > d) {
            best = d;
            pick = e;
        }
    }
    if (pick == 0)
        return 0;
    g_bf_52eff4 = pick;
    return 1;
}

/* Port of FUN_00495956 (747 B) — the enemy unit nearest to unit `u` (within `range` on both axes; of group `grp`
 * when non-zero; never dead units, order 2): Manhattan distance, 0 for the group's locked enemy (+0x2c, cleared
 * when that unit is gone), / 4 for a target on order 9, / 2 for a ranged one; a class 7+ target only for a ranged
 * hunter, at +35. The pick (best < 160) goes to DAT_0052eff4: returns 1, else 0. The loop index is retail's global
 * DAT_0056d5d0. */
int bf_495956(int range, int u, int grp) { return nearest_enemy(range, u, grp, 0); }

/* Port of FUN_00495c41 (739 B) — 0x495956 with a ranged target's distance DOUBLED (shun the archers). */
int bf_495c41(int range, int u, int grp) { return nearest_enemy(range, u, grp, 1); }

/* Port of FUN_00495f3d (536 B) — count the enemies of `owner` standing in the window of radius r around (x, y)
 * on a cell of kind (+7) <= maxkind (occupant +5 live, not owner's, not dead): the first such cell's offset when
 * there are at least `need`, else 0. The loop indices are retail's globals DAT_00569578 (row) / DAT_00569570. */
int bf_495f3d(int x, int y, int owner, int maxkind, int r, int need)
{
    bf_win_t q = window_of(x, y, r);
    int off = 8 * (q.x0 + 80 * q.y0), skip = 8 * (80 - q.w);
    int first = 0, n = 0;
    for (g_bf_569578 = q.y0; q.h + q.y0 > g_bf_569578; g_bf_569578++, off += skip)
        for (g_bf_569570 = q.x0; q.x0 + q.w > g_bf_569570; g_bf_569570++, off += 8) {
            int kind = BCELL8(off, 0x5440e7), occ = BCELL8(off, 0x5440e5);
            if (kind > maxkind || occ == 0 || BU8(occ, 0x5544ac) == owner || BU8(occ, 0x5544ac) == 0 ||
                BS8(occ, 0x5544b1) == 2)
                continue;
            n++;
            if (first == 0)
                first = off;
        }
    return n < need ? 0 : first;
}

/* Port of FUN_00496566 (514 B) — the kind-4 cell (+7 == 4) of height (+4) <= maxh nearest to (x, y) in the window
 * of radius r (Manhattan when maxh == 4, else 0x405000 — min(|dx|, |dy|)); a tie goes to the later cell. Its
 * column / row go to DAT_0057d328 / DAT_0057d324; returns its offset (0: none). Loop indices DAT_00569578 /
 * DAT_00569570 (globals). */
int bf_496566(int x, int y, int r, int maxh)
{
    bf_win_t q = window_of(x, y, r);
    int off = 8 * (q.x0 + 80 * q.y0), skip = 8 * (80 - q.w);
    int best = 1000, pick = 0;
    for (g_bf_569578 = q.y0; q.h + q.y0 > g_bf_569578; g_bf_569578++, off += skip)
        for (g_bf_569570 = q.x0; q.x0 + q.w > g_bf_569570; g_bf_569570++, off += 8) {
            int kind = BCELL8(off, 0x5440e7), h = BCELL8(off, 0x5440e4);
            if (kind != 4 || h > maxh)
                continue;
            int d = maxh == 4 ? engine_path_dist_manhattan(x, y, g_bf_569570, g_bf_569578)
                              : bf_405000(x, y, g_bf_569570, g_bf_569578);
            if (best >= d) {
                best = d;
                pick = off;
                g_bf_57d328 = g_bf_569570;
                g_bf_57d324 = g_bf_569578;
            }
        }
    return pick;
}

/* Port of FUN_00496d76 (508 B) — a kind-5 cell (+7 == 5) in the window of radius r around (x, y): the one nearest
 * (Manhattan) to the window's CLIPPED TOP-LEFT corner — retail measures from (and "skips the centre" by comparing
 * against) the corner, not (x, y): with r > 0 and the window inside the grid nothing is skipped. Its column / row
 * go to DAT_00569570 / DAT_00569578 (also the loop indices): returns 1, else 0. */
int bf_496d76(int x, int y, int r)
{
    bf_win_t q = window_of(x, y, r);
    int off = 8 * (q.x0 + 80 * q.y0), skip = 8 * (80 - q.w);
    int best = 1000000, pick = 0;
    for (g_bf_569578 = q.y0; q.h + q.y0 > g_bf_569578; g_bf_569578++, off += skip)
        for (g_bf_569570 = q.x0; q.x0 + q.w > g_bf_569570; g_bf_569570++, off += 8) {
            if (x == q.x0 && y == q.y0)
                continue;
            if (BCELL8(off, 0x5440e7) != 5)
                continue;
            int d = engine_path_dist_manhattan(q.x0, q.y0, g_bf_569570, g_bf_569578);
            if (best > d) {
                best = d;
                pick = off;
            }
        }
    if (pick == 0)
        return 0;
    pick /= 8;
    g_bf_569570 = pick % 80;
    g_bf_569578 = pick / 80;
    return 1;
}

/* Port of FUN_0048ed95 (177 B) — the footprint cell (DAT_00553c80: 20 × [cell offset, claiming group]) group `g`
 * holds, else the first unclaimed one, now claimed by g; returns its cell offset (0: none). */
int bf_48ed95(int g)
{
    for (int i = 0; i < 20; i++)
        if (g_footprint_list[i][1] == g)
            return g_footprint_list[i][0];
    for (int i = 0; i < 20; i++)
        if (g_footprint_list[i][0] != 0 && g_footprint_list[i][1] == 0) {
            g_footprint_list[i][1] = g;
            return g_footprint_list[i][0];
        }
    return 0;
}

/* Port of FUN_0048ea95 (348 B) — on a cell of height (+4) >= 2 under the current group's centroid: the enemies
 * near it (0x495f3d, radius 6): on a kind >= 6 cell at least 2 on cells of kind <= 4, else at least 3 on kind
 * <= 6. Returns the first one's cell offset, 0 otherwise. */
int bf_48ea95(void)
{
    int kind = CELL_AT(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), 0x5440e7);
    int h = CELL_AT(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), 0x5440e4);
    if (h < 2)
        return 0;
    if (kind >= 6)
        return bf_495f3d(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), BG8(GRP, 0x566520), 6, 4, 2);
    return bf_495f3d(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), BG8(GRP, 0x566520), 6, 6, 3);
}

/* Port of FUN_0048ebf1 (184 B) — the current group's centroid cell is up on the wall: height (+4) >= 2 and kind
 * (+7) >= 6. Returns 1 / 0. */
int bf_48ebf1(void)
{
    int kind = CELL_AT(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), 0x5440e7);
    int h = CELL_AT(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), 0x5440e4);
    if (h < 2)
        return 0;
    return kind >= 6;
}

/* Port of FUN_00496b9f (471 B) — the gate gives way: once per battle (DAT_0052af9c), in a siege whose attacker has
 * siege engines (MP DAT_0057a0f0: the realm's DAT_004d4b18 flag; else DAT_0055401c > 2), the first cell flagged
 * 0x40 (+1) in a linear scan of the grid and the 4 × 7 block from it become passable kind 3 (+1 = 0, +3 from
 * DAT_004d9e18, +2 |= 1 & 0xe3); DAT_00569588 = 1 (breached), DAT_00553fb0 / DAT_0053e990 += 4, the path / stage
 * scratch rebuilt (0x471c1f, 0x471d30). The scan's loop indices are DAT_00569578 / DAT_00569570: retail breaks
 * only the inner loop, so every later row re-finds the same cell at once (final 80, 0). Returns nothing (AL). */
int bf_496b9f(void)
{
    if (s_slot_0052af9c != 0)
        return 0;
    if (s_slot_0057a0f0 != 0) {
        if (AT(int32_t, g_bf_4d4a98, 0x4d4a98u, 0x4d4b18u + 4u * (uint32_t)SV(s_slot_0056d590)) == 0)
            return 0;
    } else if (SV(s_slot_0055401c) <= 2) {
        return 0;
    }
    uint8_t found = 0;
    int off = 0;   /* retail adds a row stride local that is still 0 here: the scan runs linearly, 8 B a cell */
    for (g_bf_569578 = 0; g_bf_569578 < 80; g_bf_569578++)
        for (g_bf_569570 = 0; g_bf_569570 < 80; g_bf_569570++, off += 8)
            if (BCELL8(off, 0x5440e1) & 0x40) {
                found = 1;
                break;
            }
    if (!found)
        return 0;
    int k = 0;
    for (int i = 0; i < 7; i++, off += 0x260)
        for (int j = 0; j < 4; j++, off += 8, k++) {
            BCELL8(off, 0x5440e1) = 0;
            BCELL8(off, 0x5440e7) = 3;
            BCELL8(off, 0x5440e3) = AT(uint8_t, g_bf_4d9c90, 0x4d9c90u, 0x4d9e18u + 4u * (uint32_t)k);
            BCELL8(off, 0x5440e2) |= 1;
            BCELL8(off, 0x5440e2) &= 0xe3;
        }
    s_slot_00569588 = 1;
    s_slot_0052af9c = 1;
    s_slot_00553fb0 += 4;
    s_slot_0053e990 += 4;
    engine_retail_call(0x471c1fu, 0);   /* TODO(bf-link): engine_tile_refresh_path_scratch (engine_tile, not linked) */
    engine_retail_call(0x471d30u, 0);   /* TODO(bf-link): engine_tile_refresh_stage_scratch */
    return 0;
}

/* ================================================================ attacker helpers */

/* 0x48cba9 / 0x48cce3: a member of the current group with an enemy near (0x495956 / 00495c41, range
 * 40 → DAT_0052eff4) whose surroundings (radius 20) hold a kind-4 cell of height <= 4: the group's anchor = that
 * cell, the group ordered there. The first such member decides. */
static void member_to_wall(int (*pick)(int, int, int))
{
    for (int i = 1; i < 81; i++) {
        if (BU8(i, 0x5544ac) != 0 && BS16(i, 0x5545f8) == GRP && pick(40, i, 0) != 0 &&
            bf_496566(BS16(g_bf_52eff4, 0x5544a0), BS16(g_bf_52eff4, 0x5544a2), 20, 4) != 0) {
            order_to(g_bf_57d328, g_bf_57d324);
            return;
        }
    }
}

/* Port of FUN_0048cba9 (314 B) — send the current group to the wall cell nearest a member's enemy (0x495956). */
int bf_48cba9(void)
{
    member_to_wall(bf_495956);
    return 0;
}

/* Port of FUN_0048cce3 (314 B) — 0x48cba9 with the enemy picked by 0x495c41 (ranged targets shunned). */
int bf_48cce3(void)
{
    member_to_wall(bf_495c41);
    return 0;
}

/* ================================================================ attacker stances (DAT_004d91f8) */

/* Port of FUN_0048e097 (413 B) — attacker stance 1, every 200 sub-steps: past the time limit (after the gate
 * check 0x496b9f) retarget (0x48ef45, range 80) and pursue (0x48c56f); else with the kit and a breach
 * 0x48bff9(1), with 3+ at the wall foot 0x48bff9(0), else on even steps, unless +0xf, march to the next of the 9
 * approach points (0x48bbe2(1, DAT_0056d8a4)). */
int bf_48e097(void)
{
    if (!clock_due(200, 1))
        return 0;
    if (g_bf_4eea98 > g_bf_552ff0)
        bf_496b9f();
    if (g_bf_4eea98 > g_bf_552ff0) {
        bf_48ef45(GRP, 80, 1);
        bf_48c56f(GRP);
    } else if (g_bf_542cd4 != 0 && s_slot_00553fb0 != 0) {
        bf_48bff9(1);
    } else if (SV(s_slot_00553e64) >= 3) {
        bf_48bff9(0);
    } else if (BG16(GRP, 0x56653a) % 2 == 0 && BG8(GRP, 0x56652f) == 0) {
        bf_48bbe2(1, SV(s_slot_0056d8a4));
        s_slot_0056d8a4++;
        if (SV(s_slot_0056d8a4) > 8)
            s_slot_0056d8a4 = 0;
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048e39c (304 B) — attacker stance 2, every 100 sub-steps (no busy wait): without a live target
 * group retarget (0x48ef45, range 10); past the time limit (after 0x496b9f) disperse (0x48c8af), else
 * 0x48bff9(1). */
int bf_48e39c(void)
{
    if (!clock_due(100, 0))
        return 0;
    int tg = BG16(GRP, 0x56652a);
    if (BG8(GRP, 0x56652c) == 0 || BG8(tg, 0x566520) == 0)
        bf_48ef45(GRP, 10, 0);
    if (g_bf_4eea98 > g_bf_552ff0)
        bf_496b9f();
    if (g_bf_4eea98 > g_bf_552ff0)
        bf_48c8af(GRP);
    else
        bf_48bff9(1);
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048e4cc (680 B) — attacker stance 3 (the wall assault), every 100 sub-steps: its footprint cell
 * (0x48ed95 claims one). Past the time limit (after 0x496b9f) disperse (0x48c8af); a live target group on
 * a cell of kind >= 4: go to it (0x48c4bd); kit and breach, or 6+ at the wall foot: 0x48bff9(1); 4..5 there: the
 * approach (0x48bbe2(2, 0)); else with a target on a lower cell a kind-5 cell near the footprint (0x48c3fe), every
 * 15th step the footprint (0x48c363) or the next of 4 approach points (0x48bbe2(0, DAT_00568234)). */
int bf_48e4cc(void)
{
    if (!clock_due(100, 1))
        return 0;
    int tg = BG16(GRP, 0x56652a);
    int near = 0;
    if (BG8(GRP, 0x56652c) != 0 && BG8(tg, 0x566520) != 0)
        near = CELL_AT(BG16(tg, 0x56653e), BG16(tg, 0x566540), 0x5440e7) >= 4 ? 2 : 1;
    int slot = bf_48ed95(GRP);
    if (g_bf_4eea98 > g_bf_552ff0)
        bf_496b9f();
    if (g_bf_4eea98 > g_bf_552ff0) {
        bf_48c8af(GRP);
    } else if (near == 2) {
        bf_48c4bd(tg);
    } else if ((g_bf_542cd4 != 0 && s_slot_00553fb0 != 0) || SV(s_slot_00553e64) >= 6) {
        bf_48bff9(1);
    } else if (SV(s_slot_00553e64) >= 4) {
        bf_48bbe2(2, 0);
    } else if (slot != 0 && near == 1) {
        bf_48c3fe(slot);
    } else if (BG16(GRP, 0x56653a) % 15 == 0) {
        if (slot != 0) {
            bf_48c363(slot);
        } else {
            bf_48bbe2(0, SV(s_slot_00568234));
            s_slot_00568234++;
            if (SV(s_slot_00568234) > 3)
                s_slot_00568234 = 0;
        }
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048e774 (324 B) — attacker stance 4, every 200 sub-steps: past the time limit (after
 * 0x496b9f) disperse (0x48c8af); kit and breach, or anyone at the wall foot: 0x48bff9(1); else every 20th step
 * the approach (0x48bbe2(2, 0)). */
int bf_48e774(void)
{
    if (!clock_due(200, 1))
        return 0;
    if (g_bf_4eea98 > g_bf_552ff0)
        bf_496b9f();
    if (g_bf_4eea98 > g_bf_552ff0)
        bf_48c8af(GRP);
    else if ((g_bf_542cd4 != 0 && s_slot_00553fb0 != 0) || SV(s_slot_00553e64) >= 1)
        bf_48bff9(1);
    else if (BG16(GRP, 0x56653a) % 20 == 0)
        bf_48bbe2(2, 0);
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048e8b8 (477 B) — attacker stance 8, every 200 sub-steps: enemies around a raised centroid
 * (0x48ea95): move onto the first one's cell; else with 2+ at the wall foot, or kit and breach, the kind-4
 * cell near DAT_00553274 (0x48c06f); up on the wall (0x48ebf1): hold; every 3rd step with a target 0x48c06f,
 * else a wall cell near a member's enemy, archers shunned (0x48cce3). */
int bf_48e8b8(void)
{
    if (!clock_due(200, 1))
        return 0;
    int c = bf_48ea95();
    if (c != 0) {
        c /= 8;
        order_to(c % 80, c / 80);
    } else if (SV(s_slot_00553e64) >= 2 || (g_bf_542cd4 != 0 && s_slot_00553fb0 != 0)) {
        bf_48c06f();
    } else if (bf_48ebf1() != 0) {
        /* hold */
    } else if (BG16(GRP, 0x56653a) % 3 == 0 && BG8(GRP, 0x56652c) != 0) {
        bf_48c06f();
    } else {
        bf_48cce3();
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048e234 (148 B) — attacker stance 9: the clock only (every 200 sub-steps, unless busy, the step
 * counter advances). */
int bf_48e234(void)
{
    if (clock_due(200, 1))
        BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048e2c8 (212 B) — attacker stance 10, every 200 sub-steps: the kit and a breach counter above 3,
 * or 3+ at the wall foot: 0x48c06f; else a wall cell near a member's enemy (0x48cba9). */
int bf_48e2c8(void)
{
    if (!clock_due(200, 1))
        return 0;
    if ((g_bf_542cd4 != 0 && SV(s_slot_00553fb0) > 3) || SV(s_slot_00553e64) >= 3)
        bf_48c06f();
    else
        bf_48cba9();
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* ================================================================ defender helpers */

/* Port of FUN_0048ce1d (100 B) — send the current group to the kind-4 cell of height <= 4 nearest its centroid
 * within 40 (0x496566), if any (0x48c363). */
int bf_48ce1d(void)
{
    int c = bf_496566(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), 40, 4);
    if (c != 0)
        bf_48c363(c);
    return 0;
}

/* Port of FUN_0048ce81 (402 B) — defenders man the walls: with DAT_00553fb0 in 16..400 the side's wall point
 * (DAT_0055cdf0 + 8 * DAT_0056d6b0); with <= 15 the first kind-2 cell found by 0x496768 at radius 12..19 around
 * the centroid (its column / row in DAT_00569570 / DAT_00569578); > 400: nothing. */
int bf_48ce81(void)
{
    if (SV(s_slot_00553fb0) > 400)
        return 0;
    if (SV(s_slot_00553fb0) > 15) {
        order_to(DEPLOY(0x55cdf0u), DEPLOY(0x55cdf4u));
        return 0;
    }
    for (int i = 12; i < 20; i++)
        if (bf_496768(BG16(GRP, 0x56653e), BG16(GRP, 0x566540), i) != 0) {
            order_to(g_bf_569570, g_bf_569578);
            return 0;
        }
    return 0;
}

/* Port of FUN_0048d013 (347 B) — defenders fall back: +0x10 = 0; the keep (DAT_0055cdd0 + 2, DAT_0055cdd4 + 12)
 * when DAT_004eeb6c, else the side's home point (DAT_0055cd90 pair) when DAT_00553fb0 <= 15 or >= 400, else 5 rows
 * below its wall point (DAT_0055cdf0 pair). */
int bf_48d013(void)
{
    BG8(GRP, 0x566530) = 0;
    if (g_bf_4eeb6c != 0) {
        BG16(GRP, 0x566542) = (int16_t)(AT(int32_t, s_slot_0055cd90, 0x55cd90u, 0x55cdd0u) + 2);
        BG16(GRP, 0x566544) = (int16_t)(AT(int32_t, s_slot_0055cd90, 0x55cd90u, 0x55cdd4u) + 12);
    } else if (SV(s_slot_00553fb0) <= 15 || SV(s_slot_00553fb0) >= 400) {
        BG16(GRP, 0x566542) = (int16_t)DEPLOY(0x55cd90u);
        BG16(GRP, 0x566544) = (int16_t)DEPLOY(0x55cd94u);
    } else {
        BG16(GRP, 0x566542) = (int16_t)DEPLOY(0x55cdf0u);
        BG16(GRP, 0x566544) = (int16_t)(DEPLOY(0x55cdf4u) + 5);
    }
    bf_479e90(GRP, BG16(GRP, 0x566542), BG16(GRP, 0x566544), 0, 0, 0);
    return 0;
}

/* the defenders' breach reaction shared by stances 2..4: to the fallback point (0x48bff9(0)) while the breach
 * counter DAT_0053e990 is positive, else only when no class 7..9 unit is left (DAT_00553ff0) */
static void defend_breach(void)
{
    if (SV(s_slot_0053e990) > 0)
        bf_48bff9(0);
    else if (s_slot_00553ff0 == 0)
        bf_48bff9(0);
}

/* ================================================================ defender stances (DAT_004d91d0) */

/* Port of FUN_0048d16e (676 B) — defender stance 1, every 200 sub-steps (cycle DAT_004d9c74: 0..2). Breached
 * (DAT_00553fb0 > 2 and DAT_0053e990): outnumbered (DAT_00553f74 <= DAT_0056d6ac) fall back (0x48bff9(0)); on cycle
 * 0 after 3 rounds (DAT_00553f9c) fall back, or with no reserve (DAT_0055307c < 1) on the 6th try (DAT_004d9c7c)
 * man the nearest wall (0x48ce1d); on cycle 0 count a round; else rout check (0x48c974) or the nearest wall.
 * Not breached: step 0 deploy (0x48becd(1)); every 15th step the post (0x48bcfb(1)); past step 10, with the breach
 * counter < 10 and DAT_00553f74 - DAT_0057c980 <= DAT_0056d6ac, man the wall (0x48ce81) / fall back
 * (0x48d013) by the counter DAT_004d9c78 (0..15: > 10 falls back), else rout check or the post. */
int bf_48d16e(void)
{
    if (!clock_due(200, 1))
        return 0;
    g_bf_4d9c74++;
    if (g_bf_4d9c74 > 2 || g_bf_4d9c74 < 0)
        g_bf_4d9c74 = 0;
    if (SV(s_slot_00553fb0) > 2 && s_slot_0053e990 != 0) {
        if (SV(s_slot_00553f74) <= SV(s_slot_0056d6ac)) {
            bf_48bff9(0);
        } else if (SV(s_slot_00553f9c) > 3 && g_bf_4d9c74 == 0) {
            if (SV(s_slot_0055307c) >= 1) {
                bf_48bff9(0);
            } else {
                g_bf_4d9c7c++;
                if (g_bf_4d9c7c < 6)
                    bf_48bff9(0);
                else
                    bf_48ce1d();
            }
        } else if (g_bf_4d9c74 == 0) {
            s_slot_00553f9c++;
        } else if (bf_48c974(0) == 0) {
            bf_48ce1d();
        }
    } else if (BG16(GRP, 0x56653a) == 0) {
        bf_48becd(1);
    } else if (BG16(GRP, 0x56653a) % 15 == 0) {
        bf_48bcfb(1);
    } else if (BG16(GRP, 0x56653a) > 10) {
        if (SV(s_slot_00553fb0) < 10 && SV(s_slot_00553f74) - SV(s_slot_0057c980) <= SV(s_slot_0056d6ac)) {
            if (g_bf_4d9c78 > 10)
                bf_48d013();
            else
                bf_48ce81();
            g_bf_4d9c78++;
            if (g_bf_4d9c78 > 15)
                g_bf_4d9c78 = 0;
            if (g_bf_4d9c78 < 0)
                g_bf_4d9c78 = 0;
        } else if (bf_48c974(0) == 0) {
            bf_48bcfb(1);
        }
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048d412 (746 B) — defender stance 2, every 200 sub-steps (cycle DAT_004d9c80: 0..2). Breached:
 * past 150 (DAT_004eea98) disperse (0x48c8af); more than 6 rounds (DAT_005440a8) on cycle 0: the breach reaction;
 * on cycle 0 count a round; else under 6 rounds fall back (0x48d013). Not breached, by step: < 8 deploy
 * (0x48becd(0)), then alternately fall back (0x48d013: 8..17, 25..30, 40..50, 60..70) and man the wall
 * (0x48ce81: 18..24 — where the kit flag DAT_00542cd4 jumps the step to 100 —, 31..39, 51..59, 71+). */
int bf_48d412(void)
{
    if (!clock_due(200, 1))
        return 0;
    g_bf_4d9c80++;
    if (g_bf_4d9c80 > 2 || g_bf_4d9c80 < 0)
        g_bf_4d9c80 = 0;
    if (SV(s_slot_00553fb0) > 2 && s_slot_0053e990 != 0) {
        if (g_bf_4eea98 > 150)
            bf_48c8af(GRP);
        else if (SV(s_slot_005440a8) > 6 && g_bf_4d9c80 == 0)
            defend_breach();
        else if (g_bf_4d9c80 == 0)
            s_slot_005440a8++;
        else if (SV(s_slot_005440a8) < 6)
            bf_48d013();
    } else if (BG16(GRP, 0x56653a) < 8) {
        bf_48becd(0);
    } else if (BG16(GRP, 0x56653a) < 18) {
        bf_48d013();
    } else if (BG16(GRP, 0x56653a) < 25) {
        if (g_bf_542cd4 != 0)
            BG16(GRP, 0x56653a) = 100;
        bf_48ce81();
    } else if (BG16(GRP, 0x56653a) < 31) {
        bf_48d013();
    } else if (BG16(GRP, 0x56653a) < 40) {
        bf_48ce81();
    } else if (BG16(GRP, 0x56653a) < 51) {
        bf_48d013();
    } else if (BG16(GRP, 0x56653a) < 60) {
        bf_48ce81();
    } else if (BG16(GRP, 0x56653a) < 71) {
        bf_48d013();
    } else {
        bf_48ce81();
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048d6fc (722 B) — defender stance 3, every 200 sub-steps (cycle DAT_004d9c84: 0..5). Breached: on
 * cycles 0 and 3 the breach reaction, on cycle 5 hold the centroid (0x48b386). Not breached, by step: <= 5 wait,
 * < 15 deploy (0x48becd(0)), then alternately fall back (0x48d013: 15..24, 30..40, 50..60, 70..80) and man the
 * wall (0x48ce81: 25..29 — the kit flag jumps the step to 100 —, 41..49, 61..69, 81+). */
int bf_48d6fc(void)
{
    if (!clock_due(200, 1))
        return 0;
    g_bf_4d9c84++;
    if (g_bf_4d9c84 > 5 || g_bf_4d9c84 < 0)
        g_bf_4d9c84 = 0;
    if (SV(s_slot_00553fb0) > 2 && s_slot_0053e990 != 0) {
        if (g_bf_4d9c84 == 0 || g_bf_4d9c84 == 3)
            defend_breach();
        else if (g_bf_4d9c84 == 5)
            bf_48b386();
    } else if (BG16(GRP, 0x56653a) <= 5) {
        /* wait */
    } else if (BG16(GRP, 0x56653a) < 15) {
        bf_48becd(0);
    } else if (BG16(GRP, 0x56653a) < 25) {
        bf_48d013();
    } else if (BG16(GRP, 0x56653a) < 30) {
        if (g_bf_542cd4 != 0)
            BG16(GRP, 0x56653a) = 100;
        bf_48ce81();
    } else if (BG16(GRP, 0x56653a) < 41) {
        bf_48d013();
    } else if (BG16(GRP, 0x56653a) < 50) {
        bf_48ce81();
    } else if (BG16(GRP, 0x56653a) < 61) {
        bf_48d013();
    } else if (BG16(GRP, 0x56653a) < 70) {
        bf_48ce81();
    } else if (BG16(GRP, 0x56653a) < 81) {
        bf_48d013();
    } else {
        bf_48ce81();
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048d9ce (438 B) — defender stance 4 (the lord's group), every 200 sub-steps: outnumbered
 * (DAT_00553f74 <= DAT_0057c980) before any breach (DAT_0053e990 == 0): SURRENDER — DAT_0056d5c8 = 1, the losing
 * owner DAT_005656f8 = the group's owner. Cycle DAT_004d9c88 (0..2). Breached: the breach reaction on cycle 0. Not
 * breached: step < 10 deploy (0x48becd(3)), < 31 the post (0x48bcfb(3)). */
int bf_48d9ce(void)
{
    if (!clock_due(200, 1))
        return 0;
    if (SV(s_slot_00553f74) <= SV(s_slot_0057c980) && s_slot_0053e990 == 0) {
        g_combat_render_active = 1;
        g_combat_owner_5656f8 = BG8(GRP, 0x566520);
    }
    g_bf_4d9c88++;
    if (g_bf_4d9c88 > 2 || g_bf_4d9c88 < 0)
        g_bf_4d9c88 = 0;
    if (SV(s_slot_00553fb0) > 2 && s_slot_0053e990 != 0) {
        if (g_bf_4d9c88 == 0)
            defend_breach();
    } else if (BG16(GRP, 0x56653a) < 10) {
        bf_48becd(3);
    } else if (BG16(GRP, 0x56653a) < 31) {
        bf_48bcfb(3);
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* the first live member of the current group (units +4..+6) not on order 12: 0x48db84 / 0x48ddc7.
 * Returns 1 with it in *u, else 0. */
static int first_member(int *u)
{
    for (int i = BG16(GRP, 0x566524); BG16(GRP, 0x566526) >= i; i++)
        if (BU8(i, 0x5544ac) != 0 && BS16(i, 0x5545f8) == GRP && BS8(i, 0x5544b1) != 12) {
            *u = i;
            return 1;
        }
    return 0;
}

/* Port of FUN_0048db84 (579 B) — defender stance 5, every 200 sub-steps (no busy wait), by step: < 4 wait, <= 12
 * deploy (0x48becd(5)), <= 20 and every 100th the post (0x48bcfb(5)), > 30: the first member not on order 12 goes
 * to the kind-4 cell of height <= 2 nearest it within 20 (0x48c363), else the post — and the step stays. */
int bf_48db84(void)
{
    if (!clock_due(200, 0))
        return 0;
    if (BG16(GRP, 0x56653a) >= 4) {
        if (BG16(GRP, 0x56653a) <= 12) {
            bf_48becd(5);
        } else if (BG16(GRP, 0x56653a) <= 20 || BG16(GRP, 0x56653a) % 100 == 0) {
            bf_48bcfb(5);
        } else if (BG16(GRP, 0x56653a) > 30) {
            int i;
            if (first_member(&i)) {
                int c = bf_496566(BS16(i, 0x5544a0), BS16(i, 0x5544a2), 20, 2);
                if (c != 0)
                    bf_48c363(c);
                else
                    bf_48bcfb(5);
                return 0;
            }
        }
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048ddc7 (500 B) — defender stance 6, every 200 sub-steps (no busy wait): step <= 10 deploy
 * (0x48becd(3)); then with DAT_00553fb0 > 10 (or > 6 past step 60) the first member not on order 12 goes to the
 * kind-4 cell of height <= 2 nearest it in the smallest radius 10..30 that has one (0x48c363), else the post
 * (0x48bcfb(3)) — and the step stays. */
int bf_48ddc7(void)
{
    if (!clock_due(200, 0))
        return 0;
    if (BG16(GRP, 0x56653a) <= 10) {
        bf_48becd(3);
    } else if ((BG16(GRP, 0x56653a) > 60 && SV(s_slot_00553fb0) > 6) || SV(s_slot_00553fb0) > 10) {
        int i;
        if (first_member(&i)) {
            for (int r = 10; r <= 30; r++) {
                int c = bf_496566(BS16(i, 0x5544a0), BS16(i, 0x5544a2), r, 2);
                if (c != 0) {
                    bf_48c363(c);
                    return 0;
                }
            }
            bf_48bcfb(3);
            return 0;
        }
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}

/* Port of FUN_0048dfbb (220 B) — defender stance 7, every 200 sub-steps (no busy wait): to the group's waypoint
 * +0x26/+0x28 (0x48b546) once the gate is down (DAT_00569588) — without the kit flag also while the breach
 * counter is < 8 —, else the post (0x48bcfb(2)). */
int bf_48dfbb(void)
{
    if (!clock_due(200, 0))
        return 0;
    if (g_bf_542cd4 != 0) {
        if (s_slot_00569588 != 0)
            bf_48b546();
        else
            bf_48bcfb(2);
    } else if (SV(s_slot_00553fb0) < 8 || s_slot_00569588 != 0) {
        bf_48b546();
    } else {
        bf_48bcfb(2);
    }
    BG16(GRP, 0x56653a)++;
    return 0;
}
