/* engine_bf_group.c — the battlefield port, partition 'group' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does. */
#include "engine_bf.h"

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
