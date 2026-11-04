/* engine_bf_action.c — the battlefield port, partition 'action' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The unit order table DAT_004d9170 (unit +0x31, 0..0x11; the movement-state handlers DAT_004d9140 dispatch it)
 * and its private helpers. The current unit is retail's global DAT_0053e8f8 (g_bf_53e8f8) and the current target
 * DAT_0052eff4 (g_bf_52eff4): retail re-reads both at every access, and callees change them, so the ports read the
 * globals, never a cached copy across a call. docs/findings/bf-action.md. */
#include "engine_bf.h"
#include "engine_path.h"        /* engine_path_dist_chebyshev / _manhattan (0x404f4c / 0x404ead) */
#include "engine_map_query.h"   /* g_cell_cursor_col / _row (DAT_00569570 / DAT_00569578) */

#define CU g_bf_53e8f8   /* DAT_0053e8f8: the current unit (the per-unit driver's loop index) */
#define TU g_bf_52eff4   /* DAT_0052eff4: the current target unit */

/* ------------------------------------------------------------------------------------------------ the orders */

/* Port of FUN_00482f91 (344 B) — order 1: hold. Stand thunk bf_486209; a unit with a missile weapon (+0x16a)
 * and countdown +0x30 > 5 looks for a shot (bf_483337). Unless DAT_005c9268 (the pause), the countdown
 * runs down; at <= 0 it restarts at 5, +0x32 / +0x33 cleared, the order falls back to the default (+0x2f) and the
 * flags +0x34 get bit 0 set, bit 1 cleared. */
int bf_482f91(void)
{
    bf_486209();
    if (BS8(CU, 0x5545ea) != 0 && BS8(CU, 0x5544b0) > 5)
        bf_483337();
    if (g_bf_5c9268 != 0)
        return 0;
    BU8(CU, 0x5544b0)--;
    if (BS8(CU, 0x5544b0) <= 0) {
        BU8(CU, 0x5544b0) = 5;
        BU8(CU, 0x5544b3) = 0;
        BU8(CU, 0x5544b2) = 0;
        BU8(CU, 0x5544b1) = BU8(CU, 0x5544af);
        BU8(CU, 0x5544b4) &= 0xfd;
        BU8(CU, 0x5544b4) |= 1;
    }
    return 0;
}

/* Port of FUN_004830e9 (101 B) — order 2: dying. Animation thunk bf_486229; the death counter +0x173 counts
 * up (a signed byte); past 80 it sticks at 80 and the unit is removed (bf_46ebe4). */
int bf_4830e9(void)
{
    bf_486229();
    BU8(CU, 0x5545f3)++;
    if (BS8(CU, 0x5545f3) > 80) {
        BU8(CU, 0x5545f3) = 80;
        bf_46ebe4(CU);
    }
    return 0;
}

/* Port of FUN_0048314e (138 B) — order 3: move. Walk thunk bf_4861f9, one path step (bf_48f1dd(0)); a
 * step that fails stands the unit (bf_486209). Flag +0x34 bit 1 (arrived) stands it too, clears the bit and
 * sets the order to 5 (idle). */
int bf_48314e(void)
{
    bf_4861f9();
    if (bf_48f1dd(0) == 0)
        bf_486209();
    if (BU8(CU, 0x5544b4) & 2) {
        bf_486209();
        BU8(CU, 0x5544b4) &= 0xfd;
        BU8(CU, 0x5544b1) = 5;
    }
    return 0;
}

/* Port of FUN_004831d8 (274 B) — order 4: melee. Fight thunk bf_4861e9; the target is the unit's melee
 * opponent +0x17e (DAT_0052eff4). A dead / dying target (record empty or order 2), or one not itself in melee
 * (order != 4), ends the fight: order 8. Else the exchange (bf_494908), and while the unit is not disengaged
 * (+0x34 bit 0) it closes in with a path step (bf_48f1dd(1); moved: walk thunk bf_4861f9). */
int bf_4831d8(void)
{
    bf_4861e9();
    TU = BU8(CU, 0x5545fe);
    if (BU8(TU, 0x5544ac) == 0 || BS8(TU, 0x5544b1) == 2 || BS8(TU, 0x5544b1) != 4) {
        BU8(CU, 0x5544b1) = 8;
        return 0;
    }
    bf_494908();
    if ((BU8(CU, 0x5544b4) & 1) == 0 && bf_48f1dd(1) != 0)
        bf_4861f9();
    return 0;
}

/* Port of FUN_004832ea (77 B) — order 5: idle. Stand thunk bf_486209; a unit with a missile weapon (+0x16a)
 * looks for a shot (bf_483337). (Retail also loads the group index +0x178 into a dead local.) */
int bf_4832ea(void)
{
    bf_486209();
    if (BS8(CU, 0x5545ea) != 0)
        bf_483337();
    return 0;
}

/* Port of FUN_00483e55 (396 B) — order 8: pursue. The target +0x174 (DAT_0052eff4); a siege unit (movement state
 * >= 7) switches to order 11 first. No live target: pick the nearest enemy (bf_4954dd) into +0x174; a dying
 * target (order 2) is dropped. No target: stand (bf_486209), order 5. Else the destination +0x24 / +0x26 is
 * the target's position, walk thunk bf_4861f9 and a path step (bf_48f1dd(0); failed: stand). */
int bf_483e55(void)
{
    TU = BU8(CU, 0x5545f4);
    if (BU8(CU, 0x554492) >= 7)
        BU8(CU, 0x5544b1) = 11;
    if (TU == 0 || BU8(TU, 0x5544ac) == 0) {
        TU = bf_4954dd();
        BU8(CU, 0x5545f4) = (uint8_t)TU;
    }
    if (BS8(TU, 0x5544b1) == 2)
        BU8(CU, 0x5545f4) = 0;
    if (BU8(CU, 0x5545f4) == 0) {
        bf_486209();
        BU8(CU, 0x5544b1) = 5;
        return 0;
    }
    BS16(CU, 0x5544a4) = BS16(TU, 0x5544a0);
    BS16(CU, 0x5544a6) = BS16(TU, 0x5544a2);
    bf_4861f9();
    if (bf_48f1dd(0) == 0)
        bf_486209();
    return 0;
}

/* Port of FUN_00482f86 (11 B) — order 0: nothing. */
int bf_482f86(void)
{
    return 0;
}

/* Port of FUN_00484bee (11 B) — order 16: nothing. */
int bf_484bee(void)
{
    return 0;
}

/* the wall breach of orders 6 / 14: DAT_00554034 (digging under a kind-5 cell) past 5000, or DAT_00568da4 (the
 * rest) past 20000 */
#define DIG_WALL ((int32_t)s_slot_00554034)
#define DIG_GATE ((int32_t)s_slot_00568da4)

/* Port of FUN_00483a88 (601 B) — order 6: mine. Exchange phase +0x185 = 1, fight thunk bf_4861e9; a unit on a
 * kind-5 cell (cell +7) adds 1 to DAT_00554034, else to DAT_00568da4. DAT_00554034 > 5000: the wall around the
 * unit is breached (bf_49694f radius 4), DAT_0055307c + 1, the counter restarts. DAT_00568da4 > 20000: the
 * first time (DAT_00569588) the same breach; then DAT_00569588 = 1 and DAT_0053e990 / DAT_00553fb0 + 4 (every
 * tick while above). Then the three cells above (x, x + 1, x - 1 at y - 1) are tried as a dig target (bf_496f72
 * kind 8); none: back to the default order +0x2f. */
int bf_483a88(void)
{
    BU8(CU, 0x554605) = 1;
    bf_4861e9();
    if (BCELL8(BS32(CU, 0x55449c), 0x5440e7) == 5)
        s_slot_00554034++;
    else
        s_slot_00568da4++;
    if (DIG_WALL > 5000) {
        bf_49694f(BS16(CU, 0x5544a0), BS16(CU, 0x5544a2), 4);
        s_slot_0055307c++;
        s_slot_00554034 = 0;
    }
    if (DIG_GATE > 20000) {
        if (s_slot_00569588 == 0)
            bf_49694f(BS16(CU, 0x5544a0), BS16(CU, 0x5544a2), 4);
        s_slot_00569588 = 1;
        s_slot_0053e990 += 4;
        s_slot_00553fb0 += 4;
    }
    if (bf_496f72(BS16(CU, 0x5544a0), BS16(CU, 0x5544a2) - 1, BS32(CU, 0x55449c), 8) != 0)
        return 0;
    if (bf_496f72(BS16(CU, 0x5544a0) + 1, BS16(CU, 0x5544a2) - 1, BS32(CU, 0x55449c), 8) != 0)
        return 0;
    if (bf_496f72(BS16(CU, 0x5544a0) - 1, BS16(CU, 0x5544a2) - 1, BS32(CU, 0x55449c), 8) != 0)
        return 0;
    BU8(CU, 0x5544b1) = BU8(CU, 0x5544af);
    return 0;
}

/* Port of FUN_00483ce1 (372 B) — order 7: engage. Stand thunk bf_486209; an adjacent enemy (bf_494e3f: its
 * direction 0..7, the enemy in DAT_0052eff4) starts a melee: order 4, facing +0x19 = the direction, opponent
 * +0x17e, exchange phase 1 (bf_494d08); an enemy not already in melee answers: order 4, facing the opposite
 * way, opponent this unit, phase 2 (bf_494df9). None adjacent: order 8 (pursue) when bf_4956cc(5, unit, 0)
 * finds a target, else 5 (idle). */
int bf_483ce1(void)
{
    int dir;
    bf_486209();
    dir = bf_494e3f();
    if (dir >= 8) {
        BU8(CU, 0x5544b1) = bf_4956cc(5, CU, 0) != 0 ? 8 : 5;
        return 0;
    }
    BU8(CU, 0x5544b1) = 4;
    BU8(CU, 0x554499) = (uint8_t)dir;
    BU8(CU, 0x5545fe) = (uint8_t)TU;
    BU8(CU, 0x554605) = 1;
    bf_494d08(CU);
    if (BS8(TU, 0x5544b1) != 4) {
        BU8(TU, 0x5544b1) = 4;
        BU8(TU, 0x554499) = (uint8_t)((dir + 4) % 8);
        BU8(TU, 0x5545fe) = (uint8_t)CU;
        BU8(TU, 0x554605) = 2;
        bf_494df9(TU);
    }
    return 0;
}

/* the order-9 / 4843bc / 484bf9 resets */
static void bf_action_clear_fill(void)
{
    BU8(CU, 0x55460f) = 0;
    BU8(CU, 0x55460e) = 0;
    BS32(CU, 0x554610) = 0;
}

/* Port of FUN_00483fe1 (741 B) — order 9: fill the moat. While filling (+0x18f) at the cell +0x190: dig thunk
 * bf_486219; a cell no longer of kind 2 (moat) stops the work; one whose fill level (cell +0) reached
 * DAT_00554090 becomes land (bf_47dd86) and the unit stops; else the work counter +0x18e counts up, and past
 * 80 (100 for a unit with +0x13) the level rises by one. Not filling: the destination must be a moat cell or one
 * is searched around the unit (bf_4926fb; none: order 5, idle); walk thunk bf_4861f9 and a path step
 * (bf_48f1dd(0)); a blocked step while not filling and the facing (+0x169) <= 4 looks for another moat cell
 * (bf_4927fa; none: order 5); then the stand thunk bf_486209. */
int bf_483fe1(void)
{
    if (BU8(CU, 0x55460f) != 0) {
        int c, kind, level, lim;
        bf_486219();
        c = BS32(CU, 0x554610);
        kind = BCELL8(c, 0x5440e7);
        level = BCELL8(c, 0x5440e0);
        lim = BU8(CU, 0x554493) != 0 ? 100 : 80;
        if (kind != 2) {
            bf_action_clear_fill();
        } else if (level >= g_bf_554090) {
            BCELL8(c, 0x5440e0) = 0;
            bf_47dd86(c);
            BU8(CU, 0x55460f) = 0;
            BU8(CU, 0x55460e) = 0;
        } else {
            BU8(CU, 0x55460e)++;
            if (BU8(CU, 0x55460e) > lim) {
                BU8(CU, 0x55460e) = 0;
                BCELL8(c, 0x5440e0)++;
            }
        }
    }
    if (BU8(CU, 0x55460f) != 0)
        return 0;
    if (bf_4926fb(CU) == 0) {
        BU8(CU, 0x5544b1) = 5;
        bf_action_clear_fill();
        return 0;
    }
    bf_4861f9();
    if (bf_48f1dd(0) != 0 || BU8(CU, 0x55460f) != 0)
        return 0;
    if (BU8(CU, 0x5545e9) <= 4 && bf_4927fa(CU) == 0) {
        BU8(CU, 0x5544b1) = 5;
        bf_action_clear_fill();
    }
    bf_486209();
    return 0;
}

/* ------------------------------------------------------------------- the siege orders' stack residue
 * The class 7..10 sequencers 0x488793 / 0x488436 / 0x48895e store an UNSET local into the sprite frame for a unit
 * of another class (partition 'unit' models the slot as the seams bf_unit_res_*: value in, final value out), and
 * the siege orders call them for every unit. The value is what retail's stack holds at the sequencer's
 * [ebp-4] / [ebp-8] — two fixed slots below the order handler's entry esp e0 (slot 1 = e0 - 0x1c - 4 × the
 * handler's locals, slot 2 four bytes lower) — i.e. what the handler's previous call left there:
 *   a sequencer        its own final local (the seam out)
 *   a 1-argument call  its saved ebp = the handler's ebp, e0 - 4 (slot 2: the callee's first local — unknown)
 *   a 3 / 4 / 5 / 6-argument call  its arguments 0 / 1 / 2 / 3 (slot 2: its return address / arguments 0 / 1 / 2)
 *                      as the callee left them (0x49694f clips its x argument in place: max(x - r, 0))
 *   a 0-argument non-sequencer      unknown (only 0x492c8b, always followed by a 1-argument call)
 * bf_action_res[] carries the two slots through a handler; the hook adapter loads them (rh_res_get) and the
 * handler's ebp (bf_action_res_ebp) at entry, and puts slot 1's final value back. Called from port code (the
 * port's per-unit driver) the slots are not retail's: only a non-siege unit with a siege order would show it. */
extern uint32_t bf_unit_res_488793, bf_unit_res_488436[2], bf_unit_res_48895e;   /* engine_bf_unit.c */
uint32_t bf_action_res[2];
uint32_t bf_action_res_ebp;

static void bf_action_seq_488793(void)
{
    bf_unit_res_488793 = bf_action_res[0];
    bf_488793();
    bf_action_res[0] = bf_unit_res_488793;
}
static void bf_action_seq_488436(void)
{
    bf_unit_res_488436[0] = bf_action_res[0];
    bf_unit_res_488436[1] = bf_action_res[1];
    bf_488436();
    bf_action_res[0] = bf_unit_res_488436[0];
    bf_action_res[1] = bf_unit_res_488436[1];
}
static void bf_action_seq_48895e(void)
{
    bf_unit_res_48895e = bf_action_res[0];
    bf_48895e();
    bf_action_res[0] = bf_unit_res_48895e;
}
/* after a k-argument call from the handler */
#define RES_CALL1()           (bf_action_res[0] = bf_action_res_ebp)
#define RES_ARGS(s1, s2)      (bf_action_res[0] = (uint32_t)(s1), bf_action_res[1] = (uint32_t)(s2))

/* Port of FUN_004842c6 (184 B) — order 10: siege move. Siege walk thunk 0x488436, a path step
 * (0x48f1dd(0); failed: the siege stand thunk 0x488793); arrived (+0x34 bit 1): stand, the bit cleared,
 * order 11. The aim point +0x28 / +0x2a is cleared. (Sequencer residue: above.) */
int bf_4842c6(void)
{
    int moved;
    bf_action_seq_488436();
    moved = bf_48f1dd(0);
    RES_CALL1();
    if (moved == 0)
        bf_action_seq_488793();
    if (BU8(CU, 0x5544b4) & 2) {
        bf_action_seq_488793();
        BU8(CU, 0x5544b4) &= 0xfd;
        BU8(CU, 0x5544b1) = 11;
    }
    BS16(CU, 0x5544a8) = 0;
    BS16(CU, 0x5544aa) = 0;
    return 0;
}

/* Port of FUN_0048437e (62 B) — order 11: siege idle. Siege stand thunk 0x488793; the aim point +0x28 / +0x2a
 * cleared. */
int bf_48437e(void)
{
    bf_action_seq_488793();
    BS16(CU, 0x5544a8) = 0;
    BS16(CU, 0x5544aa) = 0;
    return 0;
}

/* Port of FUN_004843bc (1600 B) — order 12: siege fire. No aim point (+0x28 / +0x2a both 0): the group's
 * (+0x178) target +0x16 / +0x18. Moving (+0x32): siege walk thunk bf_488436 and a path step
 * (bf_48f1dd(1); failed: stand bf_488793). Out of range (+0x170 / 8 < the Chebyshev distance): walk toward
 * the aim point (destination +0x24 / +0x26, bf_48f1dd(0)). In range with a pending regroup (+0x183 set, no
 * +0x13): +0x183 and the group's +0x10 cleared, the group sent to the side's rally point (DAT_0055ce30 /
 * DAT_0055ce34 [DAT_0056d6b0], bf_479e90), group +0x1a = 27. Else: face the aim point (bf_46f525), the
 * reload counter +0x180 counts up; at 100 the shot: cooldown +0x182 and group +0xf = 120, a projectile
 * (bf_46e767 → combat record DAT_00568990: owner, kind +0x16a, the flight parameters +0x16e / +0x16c / +0x19e /
 * +0x170, arc ((unit & 0x10) + 4)), eight flight steps (bf_492c8b), the launch (bf_46efbe), record +0x12 =
 * +0x34, cue bf_426120(14); at 180 the counter restarts. Then the siege fire thunk bf_48895e. */
int bf_4843bc(void)
{
    int g = BS16(CU, 0x5545f8), i, d, moved, ty, ux, uy;
    if (BS16(CU, 0x5544a8) == 0 && BS16(CU, 0x5544aa) == 0) {
        BS16(CU, 0x5544a8) = BG16(g, 0x566536);
        BS16(CU, 0x5544aa) = BG16(g, 0x566538);
    }
    if (BS8(CU, 0x5544b2) != 0) {
        bf_action_seq_488436();
        moved = bf_48f1dd(1);
        RES_CALL1();
        if (moved == 0)
            bf_action_seq_488793();
        return 0;
    }
    ux = BS16(CU, 0x5544a0);
    uy = BS16(CU, 0x5544a2);
    d = engine_path_dist_chebyshev(ux, uy, BS16(CU, 0x5544a8), BS16(CU, 0x5544aa));
    RES_ARGS(uy, ux);
    if (BS16(CU, 0x5545f0) / 8 < d) {
        BS16(CU, 0x5544a4) = BS16(CU, 0x5544a8);
        BS16(CU, 0x5544a6) = BS16(CU, 0x5544aa);
        bf_action_seq_488436();
        moved = bf_48f1dd(0);
        RES_CALL1();
        if (moved == 0)
            bf_action_seq_488793();
        return 0;
    }
    if (BU8(CU, 0x554603) != 0 && BU8(CU, 0x554493) == 0) {
        int side = (int)s_slot_0056d6b0;
        BU8(CU, 0x554603) = 0;
        BG8(g, 0x566530) = 0;
        BG16(g, 0x566542) = (int16_t)AT(int32_t, s_slot_0055cd90, 0x55cd90, 0x55ce30 + 8 * side);
        BG16(g, 0x566544) = (int16_t)AT(int32_t, s_slot_0055cd90, 0x55cd90, 0x55ce34 + 8 * (int)s_slot_0056d6b0);
        ty = BG16(g, 0x566544);
        bf_479e90(g, BG16(g, 0x566542), ty, 0, 0, 0);
        RES_ARGS(0, ty);
        BG16(g, 0x56653a) = 27;
        return 0;
    }
    ux = BS16(CU, 0x5544a0);
    uy = BS16(CU, 0x5544a2);
    BU8(CU, 0x554498) = (uint8_t)bf_46f525(ux, uy, BS16(CU, 0x5544a8), BS16(CU, 0x5544aa));
    RES_ARGS(uy, ux);
    BS16(CU, 0x554600)++;
    if (BS16(CU, 0x554600) == 100) {
        BU8(CU, 0x554602) = 120;
        BG8(g, 0x56652f) = 120;
        ux = BS16(CU, 0x5544a0);
        uy = BS16(CU, 0x5544a2);
        bf_46e767(BU8(CU, 0x5544ac), ux, uy, BS16(CU, 0x5544a8), BS16(CU, 0x5544aa));
        RES_ARGS(uy, ux);
        BC16(g_bf_568990, 0x57a106) = (int16_t)CU;
        BC8(g_bf_568990, 0x57a109) = BU8(CU, 0x5545ea);
        BC16(g_bf_568990, 0x57a134) = BS16(CU, 0x5545ee);
        BC8(g_bf_568990, 0x57a132) = BU8(CU, 0x5545ec);
        BC8(g_bf_568990, 0x57a131) = BU8(CU, 0x5545ec);
        BC16(g_bf_568990, 0x57a140) = BS16(CU, 0x55461e);
        BC16(g_bf_568990, 0x57a138) = BS16(CU, 0x5545f0);
        BC8(g_bf_568990, 0x57a12f) = 0;
        BC8(g_bf_568990, 0x57a130) = (uint8_t)((CU & 0x10) + 4);
        BC8(g_bf_568990, 0x57a13a) = 0;
        g_bf_56d5b4 = g_bf_568990;
        for (i = 0; i < 8; i++)
            bf_492c8b();
        bf_46efbe(g_bf_56d5b4);
        RES_CALL1();
        BC16(g_bf_56d5b4, 0x57a112) = BC16(g_bf_56d5b4, 0x57a134);
        bf_426120(14);
        RES_CALL1();
    }
    if (BS16(CU, 0x554600) >= 180)
        BS16(CU, 0x554600) = 0;
    bf_action_seq_48895e();
    return 0;
}

/* Port of FUN_004849fc (16 B) — order 13: the siege fire thunk 0x48895e alone. */
int bf_4849fc(void)
{
    bf_action_seq_48895e();
    return 0;
}

/* Port of FUN_00484a0c (381 B) — order 14: siege ram / sap. Siege fire thunk bf_48895e; 20 a tick onto
 * DAT_00554034 (a kind-5 cell) or DAT_00568da4 (else); the breaches of order 6 (bf_49694f radius 4) — the
 * wall one also sets order 11; on a kind-5 cell two wall breaches (DAT_0055307c >= 2), elsewhere the gate breach
 * (DAT_00569588) end the order (11). */
int bf_484a0c(void)
{
    int kind, ux;
    bf_action_seq_48895e();
    kind = BCELL8(BS32(CU, 0x55449c), 0x5440e7);
    if (kind == 5)
        s_slot_00554034 += 20;
    else
        s_slot_00568da4 += 20;
    if (DIG_WALL > 5000) {
        ux = BS16(CU, 0x5544a0);
        bf_49694f(ux, BS16(CU, 0x5544a2), 4);
        RES_ARGS(ux - 4 < 0 ? 0 : ux - 4, 0x484a99u);   /* its x argument, which it clips in place; the return address */
        s_slot_0055307c++;
        s_slot_00554034 = 0;
        BU8(CU, 0x5544b1) = 11;
    }
    if (DIG_GATE > 20000 && s_slot_00569588 == 0) {
        ux = BS16(CU, 0x5544a0);
        bf_49694f(ux, BS16(CU, 0x5544a2), 4);
        RES_ARGS(ux - 4 < 0 ? 0 : ux - 4, 0x484b11u);
        s_slot_00569588 = 1;
        s_slot_0053e990 += 4;
        s_slot_00553fb0 += 4;
    }
    if (kind == 5 && (int32_t)s_slot_0055307c >= 2)
        BU8(CU, 0x5544b1) = 11;
    if (kind != 5 && s_slot_00569588 != 0)
        BU8(CU, 0x5544b1) = 11;
    return 0;
}

/* Port of FUN_00484b89 (101 B) — order 15: siege engine destroyed. Siege fire thunk bf_48895e; the counter
 * +0x173 counts up; past 120 it sticks and the unit is removed (bf_46ebe4). */
int bf_484b89(void)
{
    bf_action_seq_48895e();
    BU8(CU, 0x5545f3)++;
    if (BS8(CU, 0x5545f3) > 120) {
        BU8(CU, 0x5545f3) = 120;
        bf_46ebe4(CU);
        RES_CALL1();
    }
    return 0;
}

/* Port of FUN_00484bf9 (2340 B) — order 17: shoot. The target +0x174 (DAT_0052eff4), the group g = +0x178 (an
 * area target: group +0x30 != 0). A group with a march timer (+0x14 > 0, not +1, this unit not reloading +0x183)
 * gets the timer 50. No live target and no area: walk to the own position (bf_48f1dd(1)), arrived (+0x34 bit
 * 1): idle (order 5). Not ready (+0x34 bit 0 clear): walk thunk + path step. Out of range (+0x170 / 8 < the
 * Chebyshev distance + 2): walk toward the target (or stay, +0x1a4 == 1), +0x183 = 1. In range: face it
 * (bf_46f525), the reload counter +0x180 counts up past +0x16b: the volley — +0x183 / +0x180 cleared, cooldown
 * +0x182 and group +0xf = 120, the group's position = the unit's, +0x1a4 = 1, a projectile (bf_46e767, the
 * group's area +0x30, damage +0x19e scaled by the formation +0x197: 1 → 4/5, 2 → 3/4, 3+ → 1/2; height = the
 * cell's +4, arc ((unit + DAT_0057c998) & 0x10) + 4), eight flight steps, the launch (bf_46efbe), record +0x12 =
 * +0x34 + +0x2e, cue bf_426120 9 (kind 2) / 7 (kind 1); then the shoot thunk bf_486239. */
int bf_484bf9(void)
{
    int g, area, i;
    TU = BU8(CU, 0x5545f4);
    g = BS16(CU, 0x5545f8);
    area = BG32(g, 0x566550) != 0;
    if (BG16(g, 0x566534) > 0 && BG8(g, 0x566521) == 0 && BU8(CU, 0x554603) == 0)
        BG16(g, 0x566534) = 50;
    if (BU8(TU, 0x5544ac) == 0 && !area) {
        BS16(CU, 0x5544a4) = BS16(CU, 0x5544a0);
        BS16(CU, 0x5544a6) = BS16(CU, 0x5544a2);
        bf_4861f9();
        if (bf_48f1dd(1) == 0)
            bf_486209();
        if (BU8(CU, 0x5544b4) & 2) {
            bf_486209();
            BU8(CU, 0x5544b4) &= 0xfd;
            BU8(CU, 0x5544b1) = 5;
        }
        return 0;
    }
    if ((BU8(CU, 0x5544b4) & 1) == 0) {
        bf_4861f9();
        if (bf_48f1dd(1) == 0)
            bf_486209();
        return 0;
    }
    if (BS16(CU, 0x5545f0) / 8 <
        engine_path_dist_chebyshev(BS16(CU, 0x5544a0), BS16(CU, 0x5544a2), BS16(TU, 0x5544a0), BS16(TU, 0x5544a2)) + 2) {
        if (BS16(CU, 0x554624) == 1) {
            BS16(CU, 0x5544a4) = BS16(CU, 0x5544a0);
            BS16(CU, 0x5544a6) = BS16(CU, 0x5544a2);
        } else {
            BS16(CU, 0x5544a4) = BS16(TU, 0x5544a0);
            BS16(CU, 0x5544a6) = BS16(TU, 0x5544a2);
        }
        bf_4861f9();
        if (bf_48f1dd(1) == 0)
            bf_486209();
        BU8(CU, 0x554603) = 1;
        return 0;
    }
    BU8(CU, 0x554498) = (uint8_t)bf_46f525(BS16(CU, 0x5544a0), BS16(CU, 0x5544a2), BS16(TU, 0x5544a0), BS16(TU, 0x5544a2));
    BS16(CU, 0x554600)++;
    if (BS8(CU, 0x5545eb) < BS16(CU, 0x554600)) {
        int f;
        BU8(CU, 0x554603) = 0;
        BS16(CU, 0x554600) = 0;
        BU8(CU, 0x554602) = 120;
        BG8(g, 0x56652f) = 120;
        BG16(g, 0x566542) = BS16(CU, 0x5544a0);
        BG16(g, 0x566544) = BS16(CU, 0x5544a2);
        BS16(CU, 0x554624) = 1;
        bf_46e767(BU8(CU, 0x5544ac), BS16(CU, 0x5544a0), BS16(CU, 0x5544a2), BS16(TU, 0x5544a0), BS16(TU, 0x5544a2));
        BC32(g_bf_568990, 0x57a144) = BG32(g, 0x566550);
        BC16(g_bf_568990, 0x57a106) = (int16_t)CU;
        BC8(g_bf_568990, 0x57a109) = BU8(CU, 0x5545ea);
        BC16(g_bf_568990, 0x57a134) = BS16(CU, 0x5545ee);
        BC8(g_bf_568990, 0x57a132) = BU8(CU, 0x5545ec);
        BC8(g_bf_568990, 0x57a131) = BU8(CU, 0x5545ec);
        f = BU8(CU, 0x554617);
        if (f == 0)
            BC16(g_bf_568990, 0x57a140) = BS16(CU, 0x55461e);
        else if (f == 1)
            BC16(g_bf_568990, 0x57a140) = (int16_t)(4 * BS16(CU, 0x55461e) / 5);
        else if (f == 2)
            BC16(g_bf_568990, 0x57a140) = (int16_t)(3 * BS16(CU, 0x55461e) / 4);
        else
            BC16(g_bf_568990, 0x57a140) = (int16_t)(BS16(CU, 0x55461e) / 2);
        BC16(g_bf_568990, 0x57a138) = BS16(CU, 0x5545f0);
        BC8(g_bf_568990, 0x57a12f) = BCELL8(BS32(CU, 0x55449c), 0x5440e4);
        BC8(g_bf_568990, 0x57a130) = (uint8_t)(((CU + g_bf_57c998[0]) & 0x10) + 4);
        BC8(g_bf_568990, 0x57a13a) = 0;
        g_bf_56d5b4 = g_bf_568990;
        for (i = 0; i < 8; i++)
            bf_492c8b();
        bf_46efbe(g_bf_56d5b4);
        BC16(g_bf_56d5b4, 0x57a112) = (int16_t)(BC8(g_bf_56d5b4, 0x57a12e) + BC16(g_bf_56d5b4, 0x57a134));
        if (BC8(g_bf_568990, 0x57a109) == 2)
            bf_426120(9);
        else if (BC8(g_bf_568990, 0x57a109) == 1)
            bf_426120(7);
    }
    bf_486239();
    return 0;
}

/* ------------------------------------------------------------------------------------------------ helpers */

/* Port of FUN_00494908 (1024 B) — one melee exchange between the current unit (DAT_0053e8f8) and its target
 * (DAT_0052eff4); DAT_0056d5d0 = the target's own opponent +0x17e. A unit in movement state 10 first throws at
 * the target (bf_47a814). Mutual opponents: a target in exchange phase 1 (+0x185) puts this unit in phase 2.
 * The blow timer +0x17b <= 0 adds the target's strike rate +0x19c to the damage accumulator +0x19a (and rerolls
 * the timer, bf_494df9); every 100 kills one man (+0x1a0) with a cue (bf_4262cf 4 / 5 / 6 by the target's
 * movement state). No men left: a cue (11 for formation slot 0, 12 for slot 4), order 2 (dying). Else in phase 1:
 * the first time, this unit's strike +0x198 goes onto the target's accumulator (latch +0x18c); the target's timer
 * and this unit's phase counter +0x184 run down; at <= 0 the phase passes (bf_494d08: to the target when they
 * are mutual opponents, this unit then in phase 2). */
int bf_494908(void)
{
    int s;
    g_bf_56d5d0 = BU8(TU, 0x5545fe);
    if (BU8(CU, 0x554492) == 10)
        bf_47a814(CU, BS16(TU, 0x5544a0), BS16(TU, 0x5544a2));
    if (g_bf_56d5d0 == CU && BU8(TU, 0x554605) == 1)
        BU8(CU, 0x554605) = 2;
    if (BS8(CU, 0x5545fb) <= 0) {
        BS16(CU, 0x55461a) = (int16_t)(BS16(CU, 0x55461a) + BS16(TU, 0x55461c));
        bf_494df9(CU);
    }
    if (BS16(CU, 0x55461a) >= 100) {
        BS16(CU, 0x55461a) = (int16_t)(BS16(CU, 0x55461a) - 100);
        BS16(CU, 0x554620)--;
        s = BU8(TU, 0x554492);
        if (s == 2)
            bf_4262cf(4);
        else if (s == 3 || s == 6)
            bf_4262cf(5);
        else
            bf_4262cf(6);
    }
    if (BS16(CU, 0x554620) <= 0) {
        if (BS8(CU, 0x5545fa) == 0)
            bf_4262cf(11);
        else if (BS8(CU, 0x5545fa) == 4)
            bf_4262cf(12);
        BU8(CU, 0x5544b1) = 2;
        return 0;
    }
    if (BU8(CU, 0x554605) != 1)
        return 0;
    if (BU8(CU, 0x55460c) == 0) {
        BS16(TU, 0x55461a) = (int16_t)(BS16(CU, 0x554618) + BS16(TU, 0x55461a));
        BU8(CU, 0x55460c) = 1;
    }
    BU8(TU, 0x5545fb) = (uint8_t)(BS8(TU, 0x5545fb) - 1);
    BU8(CU, 0x554604)--;
    if (BS8(CU, 0x554604) <= 0) {
        if (g_bf_56d5d0 == CU) {
            BU8(TU, 0x554605) = 1;
            bf_494d08(TU);
            BU8(CU, 0x554605) = 2;
        } else {
            bf_494d08(CU);
        }
    }
    return 0;
}

/* Port of FUN_0047a814 (655 B) — unit `u` throws at (x, y): a projectile from its position (bf_46e767 → the
 * new combat record DAT_00568990: owner +6 = u, kind +9 = 7, the flight parameters), four flight steps
 * (bf_492c8b), the impact (bf_46efbe(DAT_0056d5b4)); the unit goes to order 2, faces the target along the
 * major axis of the Manhattan delta (+0x168 and +0x18: 0 up / 4 down when |dy| > |dx|, else 6 left / 2 right) and
 * a cue plays (bf_4262cf(3), whose value it returns). */
int bf_47a814(int u, int x, int y)
{
    int i;
    bf_46e767(BU8(u, 0x5544ac), BS16(u, 0x5544a0), BS16(u, 0x5544a2), x, y);
    BC16(g_bf_568990, 0x57a106) = (int16_t)u;
    BC8(g_bf_568990, 0x57a109) = 7;
    BC16(g_bf_568990, 0x57a134) = 0;
    BC8(g_bf_568990, 0x57a132) = 16;
    BC8(g_bf_568990, 0x57a131) = 16;
    BC16(g_bf_568990, 0x57a140) = 0;
    BC16(g_bf_568990, 0x57a138) = 16;
    BC8(g_bf_568990, 0x57a12f) = 4;
    BC8(g_bf_568990, 0x57a130) = 0;
    BC8(g_bf_568990, 0x57a13a) = 0;
    g_bf_56d5b4 = g_bf_568990;
    for (i = 0; i < 4; i++)
        bf_492c8b();
    bf_46efbe(g_bf_56d5b4);
    BU8(u, 0x5544b1) = 2;
    engine_path_dist_manhattan(BS16(u, 0x5544a0), BS16(u, 0x5544a2), x, y);
    if (g_path_dist_dy > g_path_dist_dx)
        BU8(u, 0x5545e8) = BS16(u, 0x5544a2) >= y ? 0 : 4;
    else
        BU8(u, 0x5545e8) = BS16(u, 0x5544a0) >= x ? 6 : 2;
    BU8(u, 0x554498) = BU8(u, 0x5545e8);
    return bf_4262cf(3);
}

/* Port of FUN_004954dd (495 B) — the pursuit target for the current unit: the live enemy (other side +0x2c, not
 * dying, movement state < 7) with the least Chebyshev distance (halved when that enemy has a melee target
 * +0x16a) plus its crowding +0x175; the winner's crowding +2. Returns its index (DAT_0056d5d0, the loop global),
 * or 0. */
int bf_4954dd(void)
{
    int best = 160000, bi = 0, d;
    for (g_bf_56d5d0 = 1; g_bf_56d5d0 < 81; g_bf_56d5d0++) {
        int i = g_bf_56d5d0;
        if (BU8(i, 0x5544ac) == 0 || BU8(CU, 0x5544ac) == BU8(i, 0x5544ac) || BS8(i, 0x5544b1) == 2 ||
            BU8(i, 0x554492) >= 7)
            continue;
        d = engine_path_dist_chebyshev(BS16(CU, 0x5544a0), BS16(CU, 0x5544a2), BS16(i, 0x5544a0), BS16(i, 0x5544a2));
        if (BS8(i, 0x5545ea) != 0)
            d /= 2;
        d += BS8(i, 0x5545f5);
        if (best > d) {
            best = d;
            bi = g_bf_56d5d0;
        }
    }
    if (bi == 0)
        return 0;
    g_bf_56d5d0 = bi;
    BU8(bi, 0x5545f5) = (uint8_t)(BS8(bi, 0x5545f5) + 2);
    return g_bf_56d5d0;
}

/* the 80×80 byte grid at 0x4f2770..0x4f4070 (g_bf_4f276f + 1). The generated block stops at 0x4f3ffe (IDA's
 * name for the strategic grid's row -1 there), so the grid's last 0x72 bytes lie past it: the base is laundered
 * so the compiler does not reason from the declared bound (the standalone must widen the block to 0x4f4070, plus
 * the wedge's 0x142-byte overrun) */
static inline uint8_t *g4f2770(void)
{
    uint8_t *p = &g_bf_4f276f[1];
    __asm__("" : "+r"(p));
    return p;
}
#define G4F2770(i) (g4f2770()[(int32_t)(i)])

/* Port of FUN_00471da6 (415 B) — rebuild the battlefield's 80×80 blocker grid at 0x4f2770: cleared, then a cell
 * with flags (cell +1) & 0x60 marks itself 100 and a wedge below it (+1 row 12, +2 rows 8, +3 / +4 rows three
 * cells of 4 — retail writes those past the grid's end for the bottom rows); with DAT_0057c910 set, such a cell or
 * one of height (cell +4) 1 marks 4 each orthogonal neighbour (DAT_004d8588 odd entries: the cell-grid offsets;
 * DAT_004d6c98: the same neighbour in this grid) whose kind (cell +7) is 5. Returns retail's last EAX (the
 * last cell's offset / index or table entry). */
int bf_471da6(void)
{
    int i, j, m, n = 0, r, off = 0;
    for (i = 0; i < 80; i++)
        for (j = 0; j < 80; j++)
            G4F2770(n++) = 0;
    n = 0;
    r = 0;
    for (i = 0; i < 80; i++) {
        for (j = 0; j < 80; j++, off += 8, n++) {
            if (BCELL8(off, 0x5440e1) & 0x60) {
                G4F2770(n) = 100;
                G4F2770(n + 0x50) = 12;
                G4F2770(n + 0xa0) = 8;
                G4F2770(n + 0xf0) = 4;
                G4F2770(n + 0xf1) = 4;
                G4F2770(n + 0xef) = 4;
                G4F2770(n + 0x140) = 4;
                G4F2770(n + 0x141) = 4;
                G4F2770(n + 0x13f) = 4;
                r = n;
            } else {
                r = off;
                if (BCELL8(off, 0x5440e4) != 1)
                    continue;
            }
            if (s_slot_0057c910 == 0)
                continue;
            for (m = 1; m < 8; m += 2) {
                r = g_bf_4d8588[m];
                if (BCELL8(off + r, 0x5440e7) == 5) {
                    r = AT(int32_t, g_bf_4d6c98, 0x4d6c98, 0x4d6c98 + 4 * m);
                    G4F2770(r + n) = 4;
                }
            }
        }
    }
    return r;
}

/* Port of FUN_0046e97a (174 B) — allocate a battlefield group: the first empty slot 1..80 (+0 == 0; the loop
 * index is the global DAT_005530ec, left at the slot) is zeroed (bf_46ebae) and gets side +0 = a, +1 = b.
 * Returns 1, or 0 when all 80 are taken. */
int bf_46e97a(int a, int b)
{
    for (g_bf_5530ec = 1; g_bf_5530ec < 81; g_bf_5530ec++) {
        if (BG8(g_bf_5530ec, 0x566520) == 0) {
            bf_46ebae((int)(uintptr_t)&BG8(g_bf_5530ec, 0x566520));
            BG8(g_bf_5530ec, 0x566520) = (uint8_t)a;
            BG8(g_bf_5530ec, 0x566521) = (uint8_t)b;
            return 1;
        }
    }
    return 0;
}

/* Port of FUN_0046ebae (54 B) — zero one 0x34-byte group record at `rec` (a pointer). Returns the last index. */
int bf_46ebae(int rec)
{
    unsigned i;
    for (i = 0; i < 0x34; i++)
        ((uint8_t *)(uintptr_t)rec)[i] = 0;
    return (int)(i - 1);
}

/* Port of FUN_0048a672 (359 B) — may group `g` stand on cell `off` (a cell-grid byte offset)? kind = the cell
 * kind (+7) wanted (4: exactly), lvl = the least height (+4) + 1. An empty cell of kind < 4 is refused to a group
 * without +3 in a quick battle (DAT_0053e8fc); an occupied cell (+5 = the unit) with flags (+1) & 0x90 is fine,
 * else an occupant of another side is fine, of another group of the same side not; then the kind, the height
 * and a flagged cell refuse. */
int bf_48a672(int off, int kind, int lvl, int g)
{
    int fl = BCELL8(off, 0x5440e1) & 0x90;
    int k = BCELL8(off, 0x5440e7);
    int h = BCELL8(off, 0x5440e4);
    int occ = BCELL8(off, 0x5440e5);
    int og = BS16(occ, 0x5545f8);
    if (occ == 0 && BG8(g, 0x566523) == 0 && s_slot_0053e8fc != 0 && k < 4)
        return 0;
    if (fl != 0 && occ != 0)
        return 1;
    if (occ != 0) {
        if (BG8(og, 0x566520) != BG8(g, 0x566520))
            return 1;
        if (og != g)
            return 0;
    }
    if (k != kind && kind == 4)
        return 0;
    if (lvl - 1 > h)
        return 0;
    if (fl != 0)
        return 0;
    return 1;
}

/* Port of FUN_0049694f (592 B) — breach the walls in the (2r + 1)² box around (x, y) (clipped to the field): a
 * cue (bathit2.wav), then every cell with flag (cell +1) 0x20 loses it, becomes kind 5 (rubble) and marks the cell
 * below (+3 += 16, +2 |= 1); one with flag 0x40 is cleared to kind 5 with +3 += 40, +2 |= 1. The cursor globals
 * DAT_00569570 / DAT_00569578 are the loop indices (left past the box). Then the path scratch (0x471c1f)
 * and the blocker grid (bf_471da6, whose value it returns) are rebuilt. (Retail tests DAT_0057c910 there and
 * adds 16 either way.) */
int bf_49694f(int x, int y, int r)
{
    int x0 = x - r, y0 = y - r, w = 2 * r + 1, h = 2 * r + 1, off;
    /* TODO(bf-extern): 0x427990, the WAV cue */
    engine_retail_call(0x427990u, 3, (uint32_t)(uintptr_t)&AT(uint8_t, g_bf_4d9ee8, 0x4d9ee8, 0x4d9f38), 0u, 0u);
    if (x0 < 0) {
        w += x0;
        x0 = 0;
    } else if (x0 + w > 80) {
        w = 80 - x0;
    }
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    } else if (y0 + h > 80) {
        h = 80 - y0;
    }
    off = 8 * (x0 + 80 * y0);
    for (g_cell_cursor_row = y0; y0 + h > g_cell_cursor_row; g_cell_cursor_row++, off += 8 * (80 - w)) {
        for (g_cell_cursor_col = x0; x0 + w > g_cell_cursor_col; g_cell_cursor_col++, off += 8) {
            if (BCELL8(off, 0x5440e1) & 0x20) {
                BCELL8(off, 0x5440e1) &= 0xdf;
                BCELL8(off, 0x5440e7) = 5;
                BCELL8(off, 0x544363) = (uint8_t)(BCELL8(off, 0x544363) + 16);
                BCELL8(off, 0x544362) |= 1;
            }
            if (BCELL8(off, 0x5440e1) & 0x40) {
                BCELL8(off, 0x5440e1) = 0;
                BCELL8(off, 0x5440e7) = 5;
                BCELL8(off, 0x5440e3) = (uint8_t)(BCELL8(off, 0x5440e3) + 40);
                BCELL8(off, 0x5440e2) |= 1;
            }
        }
    }
    /* TODO(bf-link): engine_tile_refresh_path_scratch (engine_tile.c, not linked in rhost) */
    engine_retail_call(0x471c1fu, 0);
    return bf_471da6();
}

/* one neighbour of the current unit for bf_494e3f: DAT_0052eff4 = the occupant (+5) of the cell at grid VA
 * `cva` + the unit's offset (0 for a siege unit); an enemy that is not dying? */
static int bf_action_adjacent_enemy(uint32_t cva)
{
    TU = BCELL8(BS32(CU, 0x55449c), cva);
    if (BU8(TU, 0x554492) >= 7)
        TU = 0;
    return TU != 0 && BU8(CU, 0x5544ac) != BU8(TU, 0x5544ac) && BS8(TU, 0x5544b1) != 2;
}

/* Port of FUN_00494e3f (1694 B) — the first adjacent enemy of the current unit (not a siege unit, not dying), in
 * the order up (0), up-left (7), up-right (1), left (6), right (2), [bottom row: none] down-left (5),
 * down-right (3), down (4), the edges skipped; the enemy is left in DAT_0052eff4 (else the last occupant
 * looked at, or 0). Returns the direction, or 8. */
int bf_494e3f(void)
{
    if (BS16(CU, 0x5544a2) > 0) {
        if (bf_action_adjacent_enemy(0x543e65))
            return 0;
        if (BS16(CU, 0x5544a0) > 0 && bf_action_adjacent_enemy(0x543e5d))
            return 7;
        if (BS16(CU, 0x5544a0) < 79 && bf_action_adjacent_enemy(0x543e6d))
            return 1;
    }
    if (BS16(CU, 0x5544a0) > 0 && bf_action_adjacent_enemy(0x5440dd))
        return 6;
    if (BS16(CU, 0x5544a0) < 79 && bf_action_adjacent_enemy(0x5440ed))
        return 2;
    if (BS16(CU, 0x5544a2) >= 79)
        return 8;
    if (BS16(CU, 0x5544a0) > 0 && bf_action_adjacent_enemy(0x54435d))
        return 5;
    if (BS16(CU, 0x5544a0) < 79 && bf_action_adjacent_enemy(0x54436d))
        return 3;
    return bf_action_adjacent_enemy(0x544365) ? 4 : 8;
}

/* Port of FUN_0047dd86 (602 B) — moat cell `off` is filled: kind (+7) 1, flags (+1) 0, +3 &= 0xf; each
 * orthogonal neighbour of kind 3 / 4 / 5 adds 1 to DAT_00553fb0; DAT_0057a0d8 + 1. Then every moat cell (kind 2)
 * of the field — the cursor globals DAT_00569570 / DAT_00569578 and the offset DAT_005533cc as the loop indices —
 * gets its 8-neighbour mask (bf_47da54(2, 1, 1)) and, matched in the 49-entry tile table DAT_004d7610
 * (0x46c2de), the tile +3 = DAT_0057c8a4 + DAT_00568200. The path scratch is rebuilt (0x471c1f, whose
 * value it returns). */
int bf_47dd86(int off)
{
    static const uint32_t k_nb[4] = { 0x543e67u, 0x5440efu, 0x544367u, 0x5440dfu };   /* up, right, down, left */
    int i, k;
    BCELL8(off, 0x5440e7) = 1;
    BCELL8(off, 0x5440e1) = 0;
    BCELL8(off, 0x5440e3) &= 0x0f;
    for (i = 0; i < 4; i++) {
        k = BCELL8(off, k_nb[i]);
        if (k == 3 || k == 5 || k == 4)
            s_slot_00553fb0++;
    }
    s_slot_0057a0d8++;
    g_cell_cursor_row = 0;
    g_bf_5533cc = 0;
    for (; g_cell_cursor_row < 80; g_cell_cursor_row++) {
        for (g_cell_cursor_col = 0; g_cell_cursor_col < 80; g_cell_cursor_col++, g_bf_5533cc += 8) {
            if (BCELL8(g_bf_5533cc, 0x5440e7) != 2)
                continue;
            bf_47da54(2, 1, 1);
            /* TODO(bf-link): engine_territory_pick_feature is not retail-layout (its own table copy and counters);
             * retail's 0x46c2de on the table DAT_004d7610 */
            if (engine_retail_call(0x46c2deu, 2, (uint32_t)(uintptr_t)&g_bf_4d7610, 49u) != 0)
                BCELL8(g_bf_5533cc, 0x5440e3) = (uint8_t)(g_bf_57c8a4 + g_bf_568200);
        }
    }
    /* TODO(bf-link): engine_tile_refresh_path_scratch (engine_tile.c, not linked in rhost) */
    return (int)engine_retail_call(0x471c1fu, 0);
}

/* Port of FUN_004926fb (255 B) — a moat cell for unit `u` to fill: its destination +0x24 / +0x26 if that is one
 * (kind 2), else the nearest one in the rings 1..19 around the unit (bf_496768) becomes the destination.
 * Returns 1, or 0 when none. */
int bf_4926fb(int u)
{
    int i;
    if (BCELL8(8 * (80 * BS16(u, 0x5544a6) + BS16(u, 0x5544a4)), 0x5440e7) == 2)
        return 1;
    for (i = 1; i < 20; i++) {
        if (bf_496768(BS16(u, 0x5544a0), BS16(u, 0x5544a2), i) != 0) {
            BS16(u, 0x5544a4) = (int16_t)g_cell_cursor_col;
            BS16(u, 0x5544a6) = (int16_t)g_cell_cursor_row;
            return 1;
        }
    }
    return 0;
}

/* Port of FUN_004927fa (169 B) — bf_4926fb without the destination test: the nearest moat cell in the rings
 * 1..19 around unit `u` becomes its destination. Returns 1, or 0. */
int bf_4927fa(int u)
{
    int i;
    for (i = 1; i < 20; i++) {
        if (bf_496768(BS16(u, 0x5544a0), BS16(u, 0x5544a2), i) != 0) {
            BS16(u, 0x5544a4) = (int16_t)g_cell_cursor_col;
            BS16(u, 0x5544a6) = (int16_t)g_cell_cursor_row;
            return 1;
        }
    }
    return 0;
}

/* Port of FUN_00496768 (487 B) — the nearest moat cell (kind 2) in the (2r + 1)² box around (x, y), clipped to
 * the field, by bf_405000 (the lesser axis delta; a tie goes to the later cell). The cursor globals
 * DAT_00569570 / DAT_00569578 are the loop indices; found: they are set to the cell, returns 1; else 0 (left past
 * the box). Quirk: the "skip the centre" test compares (x, y) with the box's clipped ORIGIN, not the cursor — it
 * skips the whole box when that origin is (x, y) (r = 0, or a unit at (0, 0)), and never skips the centre. */
int bf_496768(int x, int y, int r)
{
    int x0 = x - r, y0 = y - r, w = 2 * r + 1, h = 2 * r + 1, off, best = 1000, bx = 0, by = 0, d;
    if (x0 < 0) {
        w += x0;
        x0 = 0;
    } else if (x0 + w > 80) {
        w = 80 - x0;
    }
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    } else if (y0 + h > 80) {
        h = 80 - y0;
    }
    off = 8 * (x0 + 80 * y0);
    for (g_cell_cursor_row = y0; y0 + h > g_cell_cursor_row; g_cell_cursor_row++, off += 8 * (80 - w)) {
        for (g_cell_cursor_col = x0; x0 + w > g_cell_cursor_col; g_cell_cursor_col++, off += 8) {
            if (x == x0 && y == y0)
                continue;
            if (BCELL8(off, 0x5440e7) != 2)
                continue;
            d = bf_405000(x, y, g_cell_cursor_col, g_cell_cursor_row);
            if (d <= best) {
                best = d;
                bx = g_cell_cursor_col;
                by = g_cell_cursor_row;
            }
        }
    }
    if (best == 1000)
        return 0;
    g_cell_cursor_col = bx;
    g_cell_cursor_row = by;
    return 1;
}

/* Port of FUN_00405000 (180 B) — the lesser of |x2 - x1| and |y2 - y1|; the deltas are left in DAT_00591560 /
 * DAT_0058fea0 (the distance helpers' globals, engine_path's g_path_dist_dx / _dy). */
int bf_405000(int x1, int y1, int x2, int y2)
{
    g_path_dist_dx = x2 < x1 ? x1 - x2 : x2 > x1 ? x2 - x1 : 0;
    g_path_dist_dy = y2 < y1 ? y1 - y2 : y2 > y1 ? y2 - y1 : 0;
    return g_path_dist_dy <= g_path_dist_dx ? g_path_dist_dy : g_path_dist_dx;
}

/* Port of FUN_00479e90 (2269 B) — order group `g` to (x, y): the group's move / attack command (the stance
 * handlers, the siege fire, the interactive orders). Nothing for group 0 or one without +2. +0xd set: +0xe = 64.
 * No a5: the area target +0x30 cleared. Every live unit of the group: +0x1a4 = 0; the missile units' least range
 * (+0x170 / 8, from 1000) — `missile` / `foot` say which kinds it has.
 *  a4 with both kinds: the missile units leave — their +9 (when it is the side) cleared and +0xa &= ~the side's
 *   bit (DAT_0057bfee + 352 × side); a new group (bf_46e97a: DAT_005530ec) collects every unit whose +9 is the
 *   side (+0x24 / +0x26 the first / last index, +8 = 3), march timer +0x14 = 20, +0x13 = 1, target (x, y); the
 *   player's side (DAT_0057c8cc) selects it (DAT_0053e984). The loop index is retail's DAT_0053e8f8 (the per-unit
 *   driver's: left at 81). Then the aggregator bf_488dfe.
 *  else a4 with missile units, or a5 with missile units and no +3: the stance is recomputed (bf_48143b), area
 *   +0x30 = 0; below stance 5 the target is pulled to the range ring (bf_497437 from the group centre
 *   +0x1e / +0x20) and checked for room (bf_48a7d9; both leave the point in the cursor globals) — a4: +0x2c = a4;
 *   a5: area = the cell offset of (x, y), unit 0's position = (x, y), +0x2c = 0.
 *   Then every throwing unit (movement state 10) of the group standing higher than the target (cell kinds 6 over
 *   < 6, 4 or 5 over < 4) throws (bf_47a814); target +0x22 / +0x24 = (x, y), timer 20, +0x13 = 1; a6: formation
 *   +9 = a6 - 1 and the target = the centre. The aggregator bf_488dfe; a target on kind 2 (moat) for a group
 *   without +1 clears +0x13.
 * Returns retail's EAX (IDA: void; a caller in partition 'input' returns it): g for g = 0 (every call site loads g into
 * EAX to push it), 13 × g for a group without +2 or a moat target, else 0x488dfe's (retail: 0x410 = 13 × 80, its
 * last loop index; the group port returns 0 — no call site reads it, the row is HOOK_V). */
int bf_479e90(int g, int x, int y, int a4, int a5, int a6)
{
    int side, best = 1000, foot = 0, missile = 0, i, v, kind, ck, r;
    if (g == 0)
        return g;
    if (BG8(g, 0x566522) == 0)
        return 13 * g;
    if (BG8(g, 0x56652d) != 0)
        BG8(g, 0x56652e) = 64;
    side = BG8(g, 0x566520);
    if (a5 == 0)
        BG32(g, 0x566550) = 0;
    for (i = BG16(g, 0x566524); BG16(g, 0x566526) >= i; i++) {
        if (BU8(i, 0x5544ac) == 0 || BS16(i, 0x5545f8) != g)
            continue;
        BS16(i, 0x554624) = 0;
        if (BS8(i, 0x5545ea) != 0) {
            missile = 1;
            v = BS16(i, 0x5545f0) / 8;
            if (best > v)
                best = v;
        } else {
            foot = 1;
        }
    }
    if (a4 != 0 && missile == 1 && foot == 1) {
        uint8_t keep = (uint8_t)~AT(uint8_t, g_player_strategic_352, 0x57bf00, 0x57bfee + 352 * side);
        for (CU = 1; CU < 81; CU++) {
            if (BU8(CU, 0x5544ac) != 0 && BS16(CU, 0x5545f8) == g && BS8(CU, 0x5545ea) != 0) {
                if (BU8(CU, 0x554489) == side)
                    BU8(CU, 0x554489) = 0;
                BU8(CU, 0x55448a) &= keep;
            }
        }
        if (bf_46e97a(side, BG8(g, 0x566521)) != 0) {
            for (CU = 1; CU < 81; CU++) {
                if (BU8(CU, 0x5544ac) != 0 && BU8(CU, 0x554489) == side) {
                    if (BG16(g_bf_5530ec, 0x566524) == 0)
                        BG16(g_bf_5530ec, 0x566524) = (int16_t)CU;
                    BG16(g_bf_5530ec, 0x566526) = (int16_t)CU;
                    BG8(g_bf_5530ec, 0x566528) = 3;
                    BS16(CU, 0x5545f8) = (int16_t)g_bf_5530ec;
                }
            }
            BG16(g_bf_5530ec, 0x566534) = 20;
            BG8(g_bf_5530ec, 0x566533) = 1;
            BG16(g_bf_5530ec, 0x566542) = (int16_t)x;
            BG16(g_bf_5530ec, 0x566544) = (int16_t)y;
            if ((int)s_slot_0057c8cc == side)
                s_slot_0053e984 = (uint32_t)g_bf_5530ec;
        }
        return bf_488dfe();
    }
    if ((a4 != 0 && missile == 1) || (a5 != 0 && missile == 1 && BG8(g, 0x566523) == 0)) {
        int attack = a4 != 0;
        bf_48143b(g);
        BG32(g, 0x566550) = 0;
        if (BG8(g, 0x566528) < 5) {
            if (attack) {
                BG8(g, 0x56654c) = (uint8_t)a4;
            } else {
                BG32(g, 0x566550) = 640 * y + 8 * x;
                BS16(0, 0x5544a0) = (int16_t)x;
                BS16(0, 0x5544a2) = (int16_t)y;
                BG8(g, 0x56654c) = 0;
            }
            bf_497437(BG16(g, 0x56653e), BG16(g, 0x566540), x, y, best);
            bf_48a7d9(BG16(g, 0x56653e), BG16(g, 0x566540), g_cell_cursor_col, g_cell_cursor_row, g);
            x = g_cell_cursor_col;
            y = g_cell_cursor_row;
        }
    }
    kind = BCELL8(8 * (80 * y + x), 0x5440e7);
    for (i = BG16(g, 0x566524); BG16(g, 0x566526) >= i; i++) {
        if (BU8(i, 0x5544ac) == 0 || BS16(i, 0x5545f8) != g || BU8(i, 0x554492) != 10)
            continue;
        ck = BCELL8(BS32(i, 0x55449c), 0x5440e7);
        if ((ck == 6 && kind < 6) || (ck == 4 && kind < 4) || (ck == 5 && kind < 4))
            bf_47a814(i, x, y);
    }
    BG16(g, 0x566542) = (int16_t)x;
    BG16(g, 0x566544) = (int16_t)y;
    BG16(g, 0x566534) = 20;
    BG8(g, 0x566533) = 1;
    if (a6 != 0) {
        BG8(g, 0x566529) = (uint8_t)(a6 - 1);
        BG16(g, 0x566542) = BG16(g, 0x56653e);
        BG16(g, 0x566544) = BG16(g, 0x566540);
    }
    r = bf_488dfe();
    if (kind != 2)
        return r;
    if (BG8(g, 0x566521) == 0)
        BG8(g, 0x566533) = 0;
    return 13 * g;
}

/* Port of FUN_0048143b (611 B) — group `g`'s stance +8 from its units (+0x24..+0x26, records not checked for
 * life): any in movement state 7 → 5, else 8 → 6, else 9 → 7, else 10 → 8, else any missile unit (+0x16a) → 1,
 * else 3. Returns 13 × g (retail's last EAX). */
int bf_48143b(int g)
{
    static const uint8_t k_state[4] = { 7, 8, 9, 10 }, k_stance[4] = { 5, 6, 7, 8 };
    int i, k;
    for (k = 0; k < 4; k++)
        for (i = BG16(g, 0x566524); BG16(g, 0x566526) >= i; i++)
            if (BU8(i, 0x554492) == k_state[k]) {
                BG8(g, 0x566528) = k_stance[k];
                return 13 * g;
            }
    for (i = BG16(g, 0x566524); BG16(g, 0x566526) >= i; i++)
        if (BS8(i, 0x5545ea) != 0) {
            BG8(g, 0x566528) = 1;
            return 13 * g;
        }
    BG8(g, 0x566528) = 3;
    return 13 * g;
}

/* Port of FUN_0048a7d9 (494 B) — is there room for group `g` near (tx, ty)? The rings 0..19 around it (clipped
 * to the field, the border cells skipped) are searched for a cell bf_48a672 accepts with the kind (+7) and
 * height (+4) of the group's centre cell (cx, cy). Found: the cursor globals = (tx, ty) — the TARGET, not the
 * cell found — returns 1; else 0 (the cursor untouched). (Retail also reads the centre's occupant +5, unused.) */
int bf_48a7d9(int cx, int cy, int tx, int ty, int g)
{
    int c = 8 * (80 * cy + cx), kind = BCELL8(c, 0x5440e7), h = BCELL8(c, 0x5440e4), r, x0, y0, w, hh, off, j, k;
    for (r = 0; r < 20; r++) {
        x0 = tx - r;
        y0 = ty - r;
        w = hh = 2 * r + 1;
        if (x0 < 0) {
            w += x0;
            x0 = 0;
        } else if (x0 + w > 80) {
            w = 80 - x0;
        }
        if (y0 < 0) {
            hh += y0;
            y0 = 0;
        } else if (y0 + hh > 80) {
            hh = 80 - y0;
        }
        off = 8 * (x0 + 80 * y0);
        for (j = y0; y0 + hh > j; j++, off += 8 * (80 - w)) {
            for (k = x0; x0 + w > k; k++, off += 8) {
                if (k > 0 && k < 79 && j > 0 && j < 79 && bf_48a672(off, kind, h, g) != 0) {
                    g_cell_cursor_col = tx;
                    g_cell_cursor_row = ty;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Port of FUN_00497437 (191 B) — pull the target (tx, ty) toward (x, y) to within `range` - 3 on each axis (the
 * Manhattan helper's deltas): the cursor globals DAT_00569570 / DAT_00569578 = the point, (x, y) on an axis
 * already within. Returns retail's EAX (the last axis value written, else range - 3). */
int bf_497437(int x, int y, int tx, int ty, int range)
{
    int lim = range - 3;
    g_cell_cursor_col = x;
    g_cell_cursor_row = y;
    engine_path_dist_manhattan(x, y, tx, ty);
    if (g_path_dist_dx > lim)
        g_cell_cursor_col = tx >= x ? tx - lim : x - (g_path_dist_dx - lim);
    if (g_path_dist_dy > lim) {
        g_cell_cursor_row = ty >= y ? ty - lim : y - (g_path_dist_dy - lim);
        return g_cell_cursor_row;
    }
    return lim;
}
