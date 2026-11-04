/* engine_bf_group.c — the battlefield port, partition 'group' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does. */
#include "engine_bf.h"
#include "engine.h"        /* g_sim_mp_active (DAT_00553030) */
#include "engine_math.h"   /* engine_math_percent_div (FUN_00404dc1) */
#include "engine_path.h"   /* engine_path_dist_manhattan (FUN_00404ead; |dx| / |dy| → DAT_00591560 / DAT_0058fea0) */

/* DAT_00567968 (the formation origin row, the target-search result row) sits inside the economy's staging
 * array g_caravan_order (retail 0x5678c0, engine_economy_globals.c) — its owner symbol */
extern int32_t g_caravan_order[8][6];
#define G_567968 AT(int32_t, g_caravan_order, 0x5678c0, 0x567968)
/* the per-class formation tables (.data): DAT_004d98c8[cls] the formation priority, DAT_004d96d4 / DAT_004d96d8
 * (stride 0x14) the spacing and the columns */
#define K_4D98C8(c) AT(int32_t, g_bf_4d98c8, 0x4d98c8, 0x4d98c8 + 4 * (c))
#define K_4D96D4(c) AT(int32_t, g_bf_4d9578, 0x4d9578, 0x4d96d4 + 0x14 * (c))
#define K_4D96D8(c) AT(int32_t, g_bf_4d9578, 0x4d9578, 0x4d96d8 + 0x14 * (c))

/* Port of FUN_00478952 (53 B) — the battlefield's per-sub-step selection-marker countdown (the first call of
 * the frame driver's combat cluster): DAT_00553f84, the marker gate the renderer reads, falls by one to 0. */
int bf_478952(void)
{
    if (g_bf_553f84 > 0)
        g_bf_553f84--;
    return 0;
}

/* Port of FUN_004859e5 (354 B) — the fire pass (last of the combat cluster). Runs only while the "fire burning"
 * flag DAT_0053e9d0 is > 0 (signed): clears it, then every kind-16 cell (cell +7) becomes kind 17 (burning) and
 * re-arms the flag; a second sweep hands every kind-15 cell with a kind-17 neighbour (FUN_00496f72) to the
 * ignite helper FUN_00485861. The sweep cursor is retail's globals DAT_00569570 (col) / DAT_00569578 (row) /
 * DAT_005533cc (cell byte offset): callees read them, so they stay globals (re-read after every call). */
int bf_4859e5(void)
{
    if ((int32_t)s_slot_0053e9d0 <= 0)
        return 0;
    s_slot_0053e9d0 = 0;
    g_bf_569578 = 0;
    g_bf_5533cc = 0;
    for (; g_bf_569578 < 80; g_bf_569578++)
        for (g_bf_569570 = 0; g_bf_569570 < 80; g_bf_569570++, g_bf_5533cc += 8)
            if (BCELL8(g_bf_5533cc, 0x5440e7) == 16) {
                BCELL8(g_bf_5533cc, 0x5440e7) = 17;
                s_slot_0053e9d0 = 1;
            }
    g_bf_569578 = 0;
    g_bf_5533cc = 0;
    for (; g_bf_569578 < 80; g_bf_569578++)
        for (g_bf_569570 = 0; g_bf_569570 < 80; g_bf_569570++, g_bf_5533cc += 8)
            if (BCELL8(g_bf_5533cc, 0x5440e7) == 15 && bf_496f72(g_bf_569570, g_bf_569578, g_bf_5533cc, 17) != 0)
                bf_485861(g_bf_569570, g_bf_569578);
    return 0;
}

/* Port of FUN_0046ef3c (130 B) — the projectile pass's prologue: every live projectile record (0x57a100 +8)
 * clears its cell's projectile byte (cell +6) and its +4 word. Loop index: retail's global DAT_00553068. */
int bf_46ef3c(void)
{
    for (g_bf_553068 = 1; g_bf_553068 < 101; g_bf_553068++) {
        int p = g_bf_553068;
        if (BC8(p, 0x57a108) != 0) {
            BCELL8(BC32(p, 0x57a11c), 0x5440e6) = 0;
            BC16(p, 0x57a104) = 0;
        }
    }
    return 0;
}

/* Port of FUN_00485bb1 (1592 B) — the projectile / effect pass (combat cluster, after the unit driver). After
 * FUN_0046ef3c, per live record (0x57a100, 1..100; loop index retail's global DAT_0056d5b4): +4 cleared, the
 * flight step FUN_00492c8b, then by kind (+9):
 *   4  the animation frame +0x12 = DAT_004d9950[life >> 1], FUN_0046efbe, the +0x31 toggle (1 → 0, else 1);
 *   5  +0x12 = DAT_004d99a8[life >> 4], FUN_0046efbe when > 0; at life 2 the cell's kind (+7) becomes the
 *      record's +0x3e (7: kind 5 and cell +1/+4/+3 cleared; 15: kind 0 and +1/+3 cleared);
 *   7  a blast: each of the cell and its four neighbours whose kind is not 7 / 10 gets FUN_00485675 at
 *      (x, y-1 / y+1 / x+1 / x-1) with a strength 32*(+0x36 - 1) + 40 / 40 / 0 / 25 / 10;
 *   else FUN_0046efbe.
 * Then the life counter +0x3c (signed word) runs down; reaching <= 0 frees the record (FUN_0046eb78). */
int bf_485bb1(void)
{
    bf_46ef3c();
    for (g_bf_56d5b4 = 1; g_bf_56d5b4 < 101; g_bf_56d5b4++) {
        if (BC8(g_bf_56d5b4, 0x57a108) == 0)
            continue;
        BC16(g_bf_56d5b4, 0x57a104) = 0;
        bf_492c8b();
        int p = g_bf_56d5b4;
        int kind = BC8(p, 0x57a109);
        if (kind == 4) {
            BC16(p, 0x57a112) = AT(uint8_t, g_bf_4d98c8, 0x4d98c8, 0x4d9950 + (BC16(p, 0x57a13c) >> 1));
            bf_46efbe(g_bf_56d5b4);
            p = g_bf_56d5b4;
            BC8(p, 0x57a131) = BCS8(p, 0x57a131) == 1 ? 0 : 1;
        } else if (kind == 5) {
            BC16(p, 0x57a112) = AT(uint8_t, g_bf_4d98c8, 0x4d98c8, 0x4d99a8 + (BC16(p, 0x57a13c) >> 4));
            if (BC16(p, 0x57a112) > 0)
                bf_46efbe(g_bf_56d5b4);
            p = g_bf_56d5b4;
            if (BC16(p, 0x57a13c) == 2) {
                int32_t off = BC32(p, 0x57a11c);
                if (BC8(p, 0x57a13e) == 7) {
                    BCELL8(off, 0x5440e7) = 5;
                    BCELL8(off, 0x5440e1) = 0;
                    BCELL8(off, 0x5440e4) = 0;
                    BCELL8(off, 0x5440e3) = 0;
                } else if (BC8(p, 0x57a13e) == 15) {
                    BCELL8(off, 0x5440e7) = 0;
                    BCELL8(off, 0x5440e1) = 0;
                    BCELL8(off, 0x5440e3) = 0;
                } else {
                    BCELL8(off, 0x5440e7) = BC8(p, 0x57a13e);
                }
            }
        } else if (kind == 7) {
            /* the cell, the row above (-0x280), the row below (+0x280), the next / previous cell (+-8) */
            static const struct { int32_t nb; int dx, dy, add; } k[5] = {
                { 0x5440e7, 0, 0, 40 }, { 0x543e67, 0, -1, 40 }, { 0x544367, 0, 1, 0 },
                { 0x5440ef, 1, 0, 25 }, { 0x5440df, -1, 0, 10 },
            };
            for (int n = 0; n < 5; n++) {
                p = g_bf_56d5b4;
                int c = BCELL8(BC32(p, 0x57a11c), k[n].nb);
                if (c != 7 && c != 10)
                    bf_485675(BC16(p, 0x57a114) + k[n].dx, BC16(p, 0x57a116) + k[n].dy,
                              32 * (BC16(p, 0x57a136) - 1) + k[n].add);
            }
        } else {
            bf_46efbe(g_bf_56d5b4);
        }
        p = g_bf_56d5b4;
        if (BC16(p, 0x57a13c) != 0) {
            BC16(p, 0x57a13c)--;
            if (BC16(p, 0x57a13c) <= 0)
                bf_46eb78((int)(uintptr_t)&BC8(p, 0x57a100));
        }
    }
    return 0;
}

/* Port of FUN_00488dfe (943 B) — the group aggregator (combat cluster; also after deployment / a regroup). Only
 * in battle phase DAT_0057c8fc == 2. Per live group (0x566520, 1..80): count +2, owner +0, first member +4
 * cleared; the cooldown +0xc runs down, else (at 0) the hit counter +0x12; +0xf and +0xe run down; +0x2d runs
 * down and, while it ran, zeroes +0xf; the melee flag +0xd cleared. Then each live unit re-buckets into its
 * group (unit +0x178): first (+4, when 0) / last (+6) member, count, owner = the unit's +0x2c; a unit with the
 * "was hit" latch +0x15 clears it, bumps the group's hit counter +0x12, sets the cooldown +0xc = 50 and the
 * group's attacker group +0xa = the attacker's (unit +0x16 → DAT_0056d5d0) group; order 4 sets +0xd, order 9
 * +0x11. DAT_0057c8dc = the live-group count. */
int bf_488dfe(void)
{
    if (s_slot_0057c8fc != 2)
        return 0;
    for (int g = 1; g < 81; g++) {
        if (BG8(g, 0x566520) == 0)
            continue;
        BG8(g, 0x566522) = 0;
        BG8(g, 0x566520) = 0;
        BG16(g, 0x566524) = 0;
        if (BG8(g, 0x56652c) != 0)
            BG8(g, 0x56652c)--;
        else if (BG8(g, 0x566532) != 0)
            BG8(g, 0x566532)--;
        if (BG8(g, 0x56652f) != 0)
            BG8(g, 0x56652f)--;
        if (BG8(g, 0x56652e) != 0)
            BG8(g, 0x56652e)--;
        if (BG8(g, 0x56654d) != 0) {
            BG8(g, 0x56654d)--;
            BG8(g, 0x56652f) = 0;
        }
        BG8(g, 0x56652d) = 0;
    }
    for (int u = 1; u < 81; u++) {
        if (BU8(u, 0x5544ac) == 0)
            continue;
        int g = BS16(u, 0x5545f8);
        if (BG16(g, 0x566524) == 0)
            BG16(g, 0x566524) = (int16_t)u;
        BG16(g, 0x566526) = (int16_t)u;
        BG8(g, 0x566522)++;
        BG8(g, 0x566520) = BU8(u, 0x5544ac);
        if (BU8(u, 0x554495) != 0) {
            BU8(u, 0x554495) = 0;
            BG8(g, 0x566532)++;
            BG8(g, 0x56652c) = 50;
            g_bf_56d5d0 = BS16(u, 0x554496);
            BG16(g, 0x56652a) = BS16(g_bf_56d5d0, 0x5545f8);
        }
        if (BS8(u, 0x5544b1) == 4)
            BG8(g, 0x56652d) = 1;
        if (BS8(u, 0x5544b1) == 9)
            BG8(g, 0x566531) = 1;
    }
    g_bf_57c8dc = 0;
    for (int g = 1; g < 81; g++)
        if (BG8(g, 0x566520) != 0)
            g_bf_57c8dc++;
    return 0;
}

/* ---------------------------------------------------------------- the group AI driver and its formation chain */

/* the stance tables FUN_00489401 dispatches through (group +8): DAT_004d91b8 interactive (DAT_0053e8fc == 0),
 * DAT_004d91f8 auto-resolve, DAT_004d91d0 auto-resolve for a group with +3 set */
static int (*const k_bf_stance_b8[5])(void) = { bf_48a9c7, bf_48a9d2, bf_48acd2, bf_48b02b, bf_48b02b };
static int (*const k_bf_stance_f8[11])(void) = {
    bf_48a9c7, bf_48e097, bf_48e39c, bf_48e4cc, bf_48e774, bf_48a9c7, bf_48a9c7, bf_48a9c7, bf_48e8b8, bf_48e234,
    bf_48e2c8,
};
static int (*const k_bf_stance_d0[9])(void) = {
    bf_48a9c7, bf_48d16e, bf_48d412, bf_48d6fc, bf_48d9ce, bf_48db84, bf_48ddc7, bf_48dfbb, bf_48a9c7,
};

/* Port of FUN_00489401 (595 B) — the group AI driver (combat cluster, after the projectile pass). Every 101st call
 * the strength ratio FUN_0047fc01 (DAT_004d9c70 the counter). Per live group (loop index retail's global
 * DAT_00568458 — the stance handlers read it as the current group): the bbox / centroid FUN_004891ad; a group
 * not flagged +1 runs its stance (+8) handler from one of three tables (above); then the re-target countdown
 * +0x14 (signed word): at <= 0 it re-arms to 500 and, when FUN_00489654 allows, FUN_0048970e re-forms the
 * group. */
int bf_489401(void)
{
    if (++g_bf_4d9c70 > 100) {
        bf_47fc01();
        g_bf_4d9c70 = 0;
    }
    for (g_bf_568458 = 1; g_bf_568458 < 81; g_bf_568458++) {
        if (BG8(g_bf_568458, 0x566520) == 0)
            continue;
        bf_4891ad(g_bf_568458);
        if (BG8(g_bf_568458, 0x566521) == 0) {
            unsigned st = BG8(g_bf_568458, 0x566528);
            if (s_slot_0053e8fc != 0) {
                if (BG8(g_bf_568458, 0x566523) != 0) {
                    if (st <= 8)
                        k_bf_stance_d0[st]();
                } else if (st <= 10) {
                    k_bf_stance_f8[st]();
                }
            } else if (st <= 4) {
                k_bf_stance_b8[st]();
            }
        }
        BG16(g_bf_568458, 0x566534)--;
        if (BG16(g_bf_568458, 0x566534) <= 0) {
            BG16(g_bf_568458, 0x566534) = 500;
            if (bf_489654(g_bf_568458) != 0)
                bf_48970e(g_bf_568458);
        }
    }
    return 0;
}

/* Port of FUN_004891ad (596 B) — group g's bbox over its live members in [+4, +6] (unit +0x178 == g): the
 * centre ((max - min) / 2 + min, truncating) → +0x1e / +0x20; the anchor +0x22 / +0x24 takes the centre while
 * both are 0. The empty bbox starts at min 80 / max 0. Only when the member count +2 is nonzero. */
int bf_4891ad(int g)
{
    if (BG8(g, 0x566522) == 0)
        return 0;
    int x0 = 80, x1 = 0, y0 = 80, y1 = 0;
    for (int u = BG16(g, 0x566524); BG16(g, 0x566526) >= u; u++) {
        if (BU8(u, 0x5544ac) == 0 || BS16(u, 0x5545f8) != g)
            continue;
        if (BS16(u, 0x5544a0) < x0)
            x0 = BS16(u, 0x5544a0);
        if (BS16(u, 0x5544a0) > x1)
            x1 = BS16(u, 0x5544a0);
        if (BS16(u, 0x5544a2) < y0)
            y0 = BS16(u, 0x5544a2);
        if (BS16(u, 0x5544a2) > y1)
            y1 = BS16(u, 0x5544a2);
    }
    BG16(g, 0x56653e) = (int16_t)((x1 - x0) / 2 + x0);
    BG16(g, 0x566540) = (int16_t)((y1 - y0) / 2 + y0);
    if (BG16(g, 0x566542) == 0 && BG16(g, 0x566544) == 0) {
        BG16(g, 0x566542) = BG16(g, 0x56653e);
        BG16(g, 0x566544) = BG16(g, 0x566540);
    }
    return 0;
}

/* Port of FUN_00489654 (186 B) — may group g re-form? Not when empty (+2) or routed (+0x2a); yes with 4+
 * members, the re-form request +0x13, or not flagged +1. */
int bf_489654(int g)
{
    if (BG8(g, 0x566522) == 0)
        return 0;
    if (BG8(g, 0x56654a) != 0)
        return 0;
    if (BG8(g, 0x566522) >= 4 || BG8(g, 0x566533) != 0 || BG8(g, 0x566521) == 0)
        return 1;
    return 0;
}

/* Port of FUN_0048970e (138 B) — re-form group g: the formation fits at the anchor (FUN_00489f9d) → lay it out
 * (FUN_00489896); stance 6 with DAT_00553fb0 > 6 → everyone onto the anchor (FUN_00489981); else member by
 * member onto enemy cells (FUN_00489798). Clears the re-form request +0x13. */
int bf_48970e(int g)
{
    if (bf_489f9d(g) != 0)
        bf_489896(g);
    else if (BG8(g, 0x566528) == 6 && (int32_t)s_slot_00553fb0 > 6)
        bf_489981(g);
    else
        bf_489798(g);
    BG8(g, 0x566533) = 0;
    return 0;
}

/* Port of FUN_00489798 (254 B) — re-target group g member by member: the per-unit "assigned" latches (+0x17c)
 * and the visited-cell list DAT_004ee900[100] cleared; then up to count (+2) times: the next enemy cell
 * (FUN_0048a38e → DAT_0057c970 / DAT_00567968; none: stop), its nearest unassigned member (FUN_00489a62) gets
 * the order (FUN_00489b8d). The count is re-read every pass (unsigned byte). */
int bf_489798(int g)
{
    for (int u = 1; u < 81; u++)
        BU8(u, 0x5545fc) = 0;
    for (int m = 0; m < 100; m++)
        g_bf_4ee900[m] = 0;
    for (int k = 0; (int)BG8(g, 0x566522) > k; k++) {
        if (bf_48a38e(g) == 0)
            break;
        int x = g_bf_57c970, y = G_567968;
        int u = bf_489a62(x, y, g);
        if (u != 0)
            bf_489b8d(x, y, u, g);
    }
    return 0;
}

/* Port of FUN_00489896 (235 B) — lay group g out in formation: latches cleared, the layout FUN_0048a1c9 (origin
 * DAT_0057c970 / DAT_00567968, spacing DAT_00568224, columns DAT_0056d8a8); slot j = (j % cols, j / cols) ×
 * spacing from the origin goes to the nearest unassigned member. */
int bf_489896(int g)
{
    for (int u = 1; u < 81; u++)
        BU8(u, 0x5545fc) = 0;
    bf_48a1c9(g);
    for (int j = 0; (int)BG8(g, 0x566522) > j; j++) {
        int x = g_bf_57c970 + (j % g_bf_56d8a8) * g_bf_568224;
        int y = G_567968 + (j / g_bf_56d8a8) * g_bf_568224;
        int u = bf_489a62(x, y, g);
        if (u != 0)
            bf_489b8d(x, y, u, g);
    }
    return 0;
}

/* Port of FUN_00489981 (225 B) — send every member of group g to the anchor (+0x22 / +0x24): latches cleared,
 * the layout FUN_0048a1c9 computed (its globals are state), then count times the nearest unassigned member. */
int bf_489981(int g)
{
    for (int u = 1; u < 81; u++)
        BU8(u, 0x5545fc) = 0;
    bf_48a1c9(g);
    for (int j = 0; (int)BG8(g, 0x566522) > j; j++) {
        int x = BG16(g, 0x566542), y = BG16(g, 0x566544);
        int u = bf_489a62(x, y, g);
        if (u != 0)
            bf_489b8d(x, y, u, g);
    }
    return 0;
}

/* Port of FUN_00489a62 (299 B) — group g's live, unassigned (+0x17c), not dead (order +0x31 != 2) member in
 * [+4, +6] nearest (Manhattan, FUN_00404ead — it leaves |dx| / |dy| in its globals) to (x, y); ties keep the
 * first. 0 when none. */
int bf_489a62(int x, int y, int g)
{
    int best = 0, bd = 1000000;
    for (int u = BG16(g, 0x566524); BG16(g, 0x566526) >= u; u++) {
        if (BU8(u, 0x5544ac) == 0 || BS16(u, 0x5545f8) != g || BU8(u, 0x5545fc) != 0 || BS8(u, 0x5544b1) == 2)
            continue;
        int d = engine_path_dist_manhattan(BS16(u, 0x5544a0), BS16(u, 0x5544a2), x, y);
        if (d < bd) {
            bd = d;
            best = u;
        }
    }
    return best;
}

/* Port of FUN_00489b8d (1040 B) — the group AI's order issuer: unit u of group g goes to (x, y). The target unit
 * DAT_0052eff4 = the anchor cell's occupant (DAT_005681fc, left by FUN_00489f9d), 0 when it is u's own side.
 * A unit holding order 9 keeps it unless the group asked for a re-form (+0x13). Foot (class < 7): order 9 when
 * the anchor cell is kind 2 (DAT_00553fe4; class 6 then keeps its order), else 3. Engines (7+): a "tired" one
 * (+0x13) keeps its order unless re-forming, else 10. A missile weapon 1..2 (+0x13e, signed) with a target or
 * the group's +0x30 flag: order 17 on that target, staying put; 3+ with the anchor terrain DAT_005440d0 in 1..3:
 * order 12, the group's waypoint +0x16 / +0x18 = (x, y), unit +0x28 / +0x2a cleared, staying put. Then the move
 * latches (+0x18d = 1, +0x18f / +0x18e / +0x190 = 0); a changed destination (+0x24 / +0x26) clears +0x164 /
 * +0x176; the destination stored, the assigned latch +0x17c set, flag bit 1 of +0x34 cleared. */
int bf_489b8d(int x, int y, int u, int g)
{
    int flag30 = BG32(g, 0x566550) != 0;
    g_bf_52eff4 = g_bf_5681fc;
    if (BU8(u, 0x5544ac) == BU8(g_bf_52eff4, 0x5544ac))
        g_bf_52eff4 = 0;
    if (BS8(u, 0x5544b1) == 9 && BG8(g, 0x566533) == 0)
        return 0;
    if (BU8(u, 0x554492) >= 7) {
        if (BU8(u, 0x554493) != 0 && BG8(g, 0x566533) == 0)
            return 0;
        BU8(u, 0x5544b1) = 10;
    } else if (g_bf_553fe4 != 0) {
        if (BU8(u, 0x554492) == 6)
            return 0;
        BU8(u, 0x5544b1) = 9;
    } else {
        BU8(u, 0x5544b1) = 3;
    }
    if (BS8(u, 0x5545ea) > 0 && BS8(u, 0x5545ea) < 3 && (g_bf_52eff4 != 0 || flag30)) {
        BU8(u, 0x5545f4) = (uint8_t)g_bf_52eff4;
        BU8(u, 0x5544b1) = 17;
        x = BS16(u, 0x5544a0);
        y = BS16(u, 0x5544a2);
    }
    if (BS8(u, 0x5545ea) >= 3 && g_bf_5440d0 >= 1 && g_bf_5440d0 <= 3) {
        BU8(u, 0x5544b1) = 12;
        BG16(g, 0x566536) = (int16_t)x;
        BG16(g, 0x566538) = (int16_t)y;
        BS16(u, 0x5544a8) = 0;
        BS16(u, 0x5544aa) = 0;
        x = BS16(u, 0x5544a0);
        y = BS16(u, 0x5544a2);
    }
    BU8(u, 0x55460d) = 1;
    BU8(u, 0x55460f) = 0;
    BU8(u, 0x55460e) = 0;
    BS32(u, 0x554610) = 0;
    if (BS16(u, 0x5544a4) != x || BS16(u, 0x5544a6) != y) {
        BU8(u, 0x5545e4) = 0;
        BU8(u, 0x5545f6) = 0;
    }
    BS16(u, 0x5544a4) = (int16_t)x;
    BS16(u, 0x5544a6) = (int16_t)y;
    BU8(u, 0x5545fc) = 1;
    BU8(u, 0x5544b4) &= 0xfd;
    return 0;
}

/* Port of FUN_00489f9d (556 B) — does group g's formation fit at its anchor (+0x22 / +0x24)? Leaves the anchor
 * cell's facts for FUN_00489b8d: DAT_00553fe4 = kind (+7) == 2, DAT_005681fc = occupant (+5), DAT_005440d0 =
 * terrain (+4). Lays the formation out (FUN_0048a1c9); every slot must be inside 1..78, on the anchor's terrain,
 * free or held by the group, and not blocked (+1 & 0x90). An anchor on a kind-2 cell never fits (checked per
 * slot: an empty group fits). */
int bf_489f9d(int g)
{
    int ax = BG16(g, 0x566542), ay = BG16(g, 0x566544);
    int32_t off = (ay * 80 + ax) * 8;
    int kind = BCELL8(off, 0x5440e7), terr = BCELL8(off, 0x5440e4), occ = BCELL8(off, 0x5440e5);
    g_bf_553fe4 = kind == 2;
    g_bf_5681fc = occ;
    g_bf_5440d0 = terr;
    bf_48a1c9(g);
    for (int i = 0; (int)BG8(g, 0x566522) > i; i++) {
        int x = g_bf_57c970 + (i % g_bf_56d8a8) * g_bf_568224;
        int y = G_567968 + (i / g_bf_56d8a8) * g_bf_568224;
        if (kind == 2 || x <= 0 || x >= 79 || y <= 0 || y >= 79)
            return 0;
        int32_t o = (y * 80 + x) * 8;
        int t = BCELL8(o, 0x5440e4), c = BCELL8(o, 0x5440e5), blk = BCELL8(o, 0x5440e1) & 0x90;
        if (terr != t)
            return 0;
        if (c != 0 && g != BS16(c, 0x5545f8))
            return 0;
        if (blk != 0)
            return 0;
    }
    return 1;
}

/* Port of FUN_0048a1c9 (453 B) — group g's formation layout: spacing DAT_00568224 / columns DAT_0056d8a8 from
 * the members' classes (FUN_004819cc); a group flagged +9 == 1 uses 2 columns; no more columns than members.
 * Width DAT_0057cb34 = cols × spacing (1 for a lone unit), depth DAT_0057c984 = ((n - 1) / cols) × spacing;
 * centred on the anchor: origin DAT_0057c970 / DAT_00567968, far edges DAT_005530d4 / DAT_00542cc4 (the edges
 * before the clamp); the origin is clamped into the 80 × 80 field (a negative origin to 0, an overhang shifts
 * it back to end at 79). */
int bf_48a1c9(int g)
{
    bf_4819cc(g);
    if (BG8(g, 0x566529) == 1)
        g_bf_56d8a8 = 2;
    if ((int)BG8(g, 0x566522) < g_bf_56d8a8)
        g_bf_56d8a8 = BG8(g, 0x566522);
    g_bf_57cb34 = g_bf_56d8a8 * g_bf_568224;
    if (BG8(g, 0x566522) == 1)
        g_bf_57cb34 = 1;
    g_bf_57c984 = (((int)BG8(g, 0x566522) - 1) / g_bf_56d8a8) * g_bf_568224;
    g_bf_57c970 = BG16(g, 0x566542) - (g_bf_57cb34 - 1) / 2;
    g_bf_5530d4 = g_bf_57c970 + g_bf_57cb34 - 1;
    G_567968 = BG16(g, 0x566544) - (g_bf_57c984 - 1) / 2;
    g_bf_542cc4 = G_567968 + g_bf_57c984;
    if (g_bf_57c970 < 0)
        g_bf_57c970 = 0;
    else if (g_bf_5530d4 >= 80)
        g_bf_57c970 -= g_bf_5530d4 - 79;
    if (G_567968 < 0)
        G_567968 = 0;
    else if (g_bf_542cc4 >= 80)
        G_567968 -= g_bf_542cc4 - 79;
    return 0;
}

/* Port of FUN_0048a38e (740 B) — the next enemy cell for group g (FUN_00489798's fallback): from the anchor
 * (+0x22 / +0x24): a kind-2 anchor cell, or kind 4 for a stance-5 group, is the answer itself; else squares of
 * side 2i + 1 (i = 0..19, clipped to the field) centred on the anchor, scanned row by row over cells 1..78, the
 * first FUN_0048a672 accepts (anchor kind / terrain as context) → DAT_0057c970 / DAT_00567968 (written on every
 * accepted cell); a cell already in the visited list DAT_004ee900[100] is skipped, a new one appended (return
 * 1). 0 when nothing (or the list is full). */
int bf_48a38e(int g)
{
    int ax = BG16(g, 0x566542), ay = BG16(g, 0x566544);
    int32_t off0 = (ay * 80 + ax) * 8;
    int kind = BCELL8(off0, 0x5440e7), terr = BCELL8(off0, 0x5440e4);
    if (kind == 2 || (kind == 4 && BG8(g, 0x566528) == 5)) {
        g_bf_57c970 = ax;
        G_567968 = ay;
        return 1;
    }
    for (int i = 0; i < 20; i++) {
        int x0 = ax - i, y0 = ay - i, h = 2 * i + 1, w = h;
        if (x0 < 0) {
            w += x0;
            x0 = 0;
        } else if (w + x0 > 80) {
            w -= w + x0 - 80;
        }
        if (y0 < 0) {
            h += y0;
            y0 = 0;
        } else if (h + y0 > 80) {
            h -= h + y0 - 80;
        }
        int32_t off = (y0 * 80 + x0) * 8, skip = (80 - w) * 8;
        for (int y = y0; h + y0 > y; y++, off += skip)
            for (int x = x0; w + x0 > x; x++, off += 8) {
                if (x <= 0 || x >= 79 || y <= 0 || y >= 79)
                    continue;
                if (bf_48a672(off, kind, terr, g) == 0)
                    continue;
                g_bf_57c970 = x;
                G_567968 = y;
                for (int m = 0; m < 100; m++) {
                    if (g_bf_4ee900[m] == 0) {
                        g_bf_4ee900[m] = off;
                        return 1;
                    }
                    if (g_bf_4ee900[m] == off)
                        break;
                }
            }
    }
    return 0;
}

/* Port of FUN_0047fc01 (522 B) — the battle's strength ratio DAT_004eea98: the live units' troops (+0x1a0)
 * weighted by class (1, 4, 5: ×2; 2, 3: ×3; 6: ×4), split by side flag +0x13; percent(side 0, side 1) - 100,
 * plus an RNG jitter (DAT_0058fd7c & 31) - 10 outside multiplayer. Loop index: retail's global DAT_0053e8f8. */
int bf_47fc01(void)
{
    int s1 = 0, s0 = 0;
    g_bf_4eea98 = 0;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) == 0)
            continue;
        int v = BS16(u, 0x554620);
        switch (BU8(u, 0x554492)) {
        case 1: case 4: case 5: v *= 2; break;
        case 2: case 3: v *= 3; break;
        case 6: v *= 4; break;
        default: break;
        }
        if (BU8(u, 0x554493) != 0)
            s1 += v;
        else
            s0 += v;
    }
    g_bf_4eea98 = engine_math_percent_div(s0, s1) - 100;
    if (g_sim_mp_active == 0) {
        bf_rng_step();
        g_bf_4eea98 += (g_bf_58fd7c & 0x1f) - 10;
    }
    return 0;
}

/* Port of FUN_004819cc (239 B) — group g's formation class: the live member class (+0x12) with the highest
 * DAT_004d98c8 priority (> 0, first wins) → spacing DAT_00568224 / columns DAT_0056d8a8 from the class table
 * (class 0's when none). */
int bf_4819cc(int g)
{
    int best = 0, cls = 0;
    for (int u = 1; u < 81; u++) {
        if (BU8(u, 0x5544ac) == 0 || BS16(u, 0x5545f8) != g)
            continue;
        if (K_4D98C8(BU8(u, 0x554492)) > best) {
            best = K_4D98C8(BU8(u, 0x554492));
            cls = BU8(u, 0x554492);
        }
    }
    g_bf_568224 = K_4D96D4(cls);
    g_bf_56d8a8 = K_4D96D8(cls);
    return 0;
}
