/* engine_bf_unit.c — the battlefield port, partition 'unit' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * Findings: docs/findings/bf-unit.md.
 *
 * Unit fields this partition reads / writes (record 0x554480 + 0x1b0 * u; u = the current unit DAT_0053e8f8):
 *   +0x0b / +0x0c  frame tick / period (0x4872ae)   +0x0d  drawn direction   +0x0e  animation phase
 *   +0x10 / +0x11  sprite frame / overlay frame          +0x12  class (0..10; written only at spawn)
 *   +0x13  condition flag   +0x18  facing   +0x19  move direction   +0x20  position col (signed word)
 *   +0x31  order (signed: 0..0x11 the order table, else the unit is removed)   +0x168 engine frame (class 8)
 *   +0x172 armor   +0x173 death timer (signed)   +0x17a formation slot   +0x180 shot windup (signed word)
 *   +0x185 moving flag (== 1)   +0x18a / +0x18b / +0x198 the state's step parameters   +0x194 blocked-ring flag
 *   +0x197 troop band   +0x19a damage accumulator   +0x19c the band's step   +0x1a0 troops (signed word)
 *
 * STACK RESIDUE.  0x488793, 0x488436, 0x48895e and 0x48804a read a local they never set on
 * some paths (a class outside 7..10 / a view rotation outside 0,2,4,6). Each such local is a seam below
 * (bf_unit_res_*: the slot's value in, its final value out); the hook adapters move it with rh_res_get /
 * rh_res_put. A port-to-port call leaves the seam as it is: the sequencer bodies call 0x488793 /
 * 0x488436 / 0x48895e only with class 7..10, where the local is always set first. */
#include "engine_bf.h"

/* the stack-residue seams (see above): the uninitialized local's slot, below the function's entry esp e0 */
uint32_t bf_unit_res_488793;      /* 0x488793 [ebp-4] = e0-8 */
uint32_t bf_unit_res_488436[2];   /* 0x488436 [ebp-4] = e0-8, [ebp-8] = e0-0xc */
uint32_t bf_unit_res_48895e;      /* 0x48895e [ebp-4] = e0-8 */
uint32_t bf_unit_res_48804a;      /* 0x48804a [ebp-0xc] = e0-0x10 */

/* the retail tables the sequencers read (in the g_bf_4d98c8 / 4d9ac8 / 4d9c08 blocks; any VA through AT) */
#define T32_4D98C8(va) AT(int32_t,  g_bf_4d98c8, 0x4d98c8, (va))   /* dword_4D9A00 / 4D9A28 / 4D9A50 */
#define T16_4D98C8(va) AT(uint16_t, g_bf_4d98c8, 0x4d98c8, (va))   /* word_4D98F8 */
#define T8_4D9AC8(va)  AT(uint8_t,  g_bf_4d9ac8, 0x4d9ac8, (va))   /* byte_4D9AC8 / 4D9B18 */
#define T8_4D9C08(va)  AT(uint8_t,  g_bf_4d9c08, 0x4d9c08, (va))   /* byte_4D9C08 / 4D9C30 */

/* the movement-state table DAT_004d9140 (unit +0x12, 0..10): each handler sets the step parameters, then runs
 * the unit's order through DAT_004d9170 */
static int (*const k_bf_state[11])(void) = {
    bf_4825d1, bf_4826b0, bf_482789, bf_482862, bf_48293b, bf_482a14, bf_482aed, bf_482bc6, bf_482ca8, bf_482d8a,
    bf_482e6c,
};

/* the order table DAT_004d9170 (unit +0x31, 0..0x11; partition 'action') */
static int (*const k_bf_order[18])(void) = {
    bf_482f86, bf_482f91, bf_4830e9, bf_48314e, bf_4831d8, bf_4832ea, bf_483a88, bf_483ce1, bf_483e55, bf_483fe1,
    bf_4842c6, bf_48437e, bf_4843bc, bf_4849fc, bf_484a0c, bf_484b89, bf_484bee, bf_484bf9,
};

/* Port of FUN_004822ed (644 B) — the battlefield's per-unit sim driver (combat cluster, once per sub-step).
 * Recounts the class 7..9 units (DAT_00553ff0), the units on a kind-5 cell (cell +7) in formation slot 4
 * (DAT_00553e64) and the units with +0x13 set on a kind-15 cell (DAT_005530e8); a unit on a kind-10 or -17
 * cell goes through 0x49459a first. Then per unit: tick counter +0x1a8 + 1, latch +0x35 cleared, the
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
 * blocked-ring around a unit flagged +0x194 released (0x491f17 at its position), the record zeroed. */
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

/* ---------------------------------------------------------------- the movement-state handlers (DAT_004d9140) */

/* the tail every state handler shares: the unit's order (+0x31, a SIGNED byte: movsx, jl 0 / jle 0x11) through
 * DAT_004d9170; out of range: the unit is removed (0x46ebe4). The order handler's EAX is the handler's
 * return, which 0x4822ed ignores. */
static void unit_run_order(void)
{
    int o = BS8(g_bf_53e8f8, 0x5544b1);
    if (o < 0 || o > 0x11)
        bf_46ebe4(g_bf_53e8f8);
    else
        k_bf_order[o]();
}

/* states 0..6: the troop band first (0x481abb), then the step parameters +0x18a / +0x18b / +0x198 and the
 * armor +0x172 */
static void unit_state_banded(int b60a, int b60b, int w618, int b5f2)
{
    bf_481abb(g_bf_53e8f8);
    int u = g_bf_53e8f8;
    BU8(u, 0x55460a)  = (uint8_t)b60a;
    BU8(u, 0x55460b)  = (uint8_t)b60b;
    BS16(u, 0x554618) = (int16_t)w618;
    BU8(u, 0x5545f2)  = (uint8_t)b5f2;
    unit_run_order();
}

/* states 7..10 (the engines): no troop band — the band step +0x19c is cleared instead */
static void unit_state_fixed(int b60a, int b60b, int w618, int b5f2)
{
    int u = g_bf_53e8f8;
    BS16(u, 0x55461c) = 0;
    BU8(u, 0x55460a)  = (uint8_t)b60a;
    BU8(u, 0x55460b)  = (uint8_t)b60b;
    BS16(u, 0x554618) = (int16_t)w618;
    BU8(u, 0x5545f2)  = (uint8_t)b5f2;
    unit_run_order();
}

/* Port of FUN_004825d1 (223 B) — movement state 0: counts the class-0 unit ticks (DAT_005694fc), then the
 * banded step 1 / 6 / 0 / 0 and the unit's order. */
int bf_4825d1(void)
{
    g_bf_5694fc++;
    unit_state_banded(1, 6, 0, 0);
    return 0;
}

/* Port of FUN_004826b0 (217 B) — movement state 1: banded step 1 / 8 / 0 / 12, then the unit's order. */
int bf_4826b0(void)
{
    unit_state_banded(1, 8, 0, 12);
    return 0;
}

/* Port of FUN_00482789 (217 B) — movement state 2: banded step 2 / 12 / 300 / 12, then the unit's order. */
int bf_482789(void)
{
    unit_state_banded(2, 12, 300, 12);
    return 0;
}

/* Port of FUN_00482862 (217 B) — movement state 3: banded step 3 / 12 / 100 / 35, then the unit's order. */
int bf_482862(void)
{
    unit_state_banded(3, 12, 100, 35);
    return 0;
}

/* Port of FUN_0048293b (217 B) — movement state 4: banded step 2 / 30 / 0 / 35, then the unit's order. */
int bf_48293b(void)
{
    unit_state_banded(2, 30, 0, 35);
    return 0;
}

/* Port of FUN_00482a14 (217 B) — movement state 5: banded step 1 / 6 / 0 / 0 (state 0's without the tick
 * count), then the unit's order. */
int bf_482a14(void)
{
    unit_state_banded(1, 6, 0, 0);
    return 0;
}

/* Port of FUN_00482aed (217 B) — movement state 6: banded step 4 / 16 / 200 / 25, then the unit's order.
 * (No corpus scenario reaches it: unverified.) */
int bf_482aed(void)
{
    unit_state_banded(4, 16, 200, 25);
    return 0;
}

/* Port of FUN_00482bc6 (226 B) — movement state 7: fixed step 0 / 20 / 0 / 33, then the unit's order. */
int bf_482bc6(void)
{
    unit_state_fixed(0, 20, 0, 33);
    return 0;
}

/* Port of FUN_00482ca8 (226 B) — movement state 8: fixed step 0 / 15 / 0 / 35, then the unit's order.
 * (No corpus scenario reaches it: unverified.) */
int bf_482ca8(void)
{
    unit_state_fixed(0, 15, 0, 35);
    return 0;
}

/* Port of FUN_00482d8a (226 B) — movement state 9: fixed step 0 / 30 / 0 / 50, then the unit's order.
 * (No corpus scenario reaches it: unverified.) */
int bf_482d8a(void)
{
    unit_state_fixed(0, 30, 0, 50);
    return 0;
}

/* Port of FUN_00482e6c (282 B) — movement state 10: fixed step 0 / 8 / 0, armor +0x172 = 25 with the
 * condition flag +0x13 set, else 40; then the unit's order. */
int bf_482e6c(void)
{
    int u = g_bf_53e8f8;
    BS16(u, 0x55461c) = 0;
    BU8(u, 0x55460a)  = 0;
    BU8(u, 0x55460b)  = 8;
    BS16(u, 0x554618) = 0;
    BU8(u, 0x5545f2)  = BU8(u, 0x554493) != 0 ? 25 : 40;
    unit_run_order();
    return 0;
}

/* Port of FUN_00481abb (223 B) — unit `u`'s troop band: troops +0x1a0 (signed) against the thresholds
 * DAT_0053e86c / DAT_00522f54 / DAT_00522f5c (signed) → band 0..3 into +0x197, and the band step +0x19c from
 * the per-class table word_4D98F8[4 * class + band]. Returns the step (EAX: the word, zero-extended). */
int bf_481abb(int u)
{
    int t = BS16(u, 0x554620);
    int band;
    if (t >= (int32_t)s_slot_0053e86c)
        band = 0;
    else if (t >= (int32_t)s_slot_00522f54)
        band = 1;
    else if (t >= (int32_t)s_slot_00522f5c)
        band = 2;
    else
        band = 3;
    BU8(u, 0x554617) = (uint8_t)band;
    uint16_t step = T16_4D98C8(0x4d98f8 + 8 * BU8(u, 0x554492) + 2 * band);
    BU16(u, 0x55461c) = step;
    return step;
}

/* ---------------------------------------------------------------- the animation sequencers */
/* 0x4861e9 .. 0x486239: the order handlers' entry points (push / pop of ebx esi edi around the body).
 * Retail returns the body's EAX (an incidental byte); no caller reads it. */

/* Port of FUN_004861e9 (16 B) — thunk: the walk sequencer 0x486249. */
int bf_4861e9(void)
{
    bf_486249();
    return 0;
}

/* Port of FUN_004861f9 (16 B) — thunk: the attack sequencer 0x486d83. */
int bf_4861f9(void)
{
    bf_486d83();
    return 0;
}

/* Port of FUN_00486209 (16 B) — thunk: the stand / turn sequencer 0x4872ae. */
int bf_486209(void)
{
    bf_4872ae();
    return 0;
}

/* Port of FUN_00486219 (16 B) — thunk: the 0x487908 sequencer. (No corpus scenario reaches it.) */
int bf_486219(void)
{
    bf_487908();
    return 0;
}

/* Port of FUN_00486229 (16 B) — thunk: the dying sequencer 0x487ce4. */
int bf_486229(void)
{
    bf_487ce4();
    return 0;
}

/* Port of FUN_00486239 (16 B) — thunk: the missile-shot sequencer 0x48804a. */
int bf_486239(void)
{
    bf_48804a();
    return 0;
}

/* Port of FUN_00486249 (1411 B) — walk sequencer for the current unit. Class >= 7: 0x488793. Else per class
 * a frame stride / idle offset / walk offset: frame +0x10 = stride * (move direction +0x19 % 8, signed) +
 * (moving +0x185 == 1: walk offset + the phase table dword_4D9A00 / 4D9A28 / 4D9A50 [phase +0x0e & 0xfc], the
 * phase stepping 0..0x27; else the idle offset). Class 6 also turns its facing +0x18 toward the nearest
 * direction the byte_4D9C30[8 * facing + move direction] mask allows (searching both ways), frame = mask +
 * phase table, overlay +0x11 = 6 * (facing % 8). Then the drawn direction +0x0d = the move direction. */
int bf_486249(void)
{
    int u   = g_bf_53e8f8;
    int cls = BU8(u, 0x554492);
    if (cls >= 7) {
        bf_488793();
        return 0;
    }
    int stride, idle, walk;
    switch (cls) {
    case 3: case 2: stride = 12; idle = 11; walk = 6; break;
    case 5: case 1: stride = 13; idle = 9; walk = 6; break;
    case 4:         stride = 8; idle = 7; walk = 6; break;
    case 0:         stride = 10; idle = 9; walk = 6; break;
    default:        stride = 12; idle = 0; walk = 0; break;
    }
    int32_t f = stride * (BS8(u, 0x554499) % 8);
    if (BU8(u, 0x554605) == 1) {
        f += walk;
        if (++BU8(u, 0x55448e) >= 0x28)
            BU8(u, 0x55448e) = 0;
        int off = BU8(u, 0x55448e) & 0xfc;
        uint32_t tab;
        if (cls == 3 || cls == 2)
            tab = 0x4d9a00;
        else if (cls == 5 || cls == 1)
            tab = 0x4d9a28;
        else if (cls == 4)
            tab = 0x4d9a50;
        else
            tab = 0x4d9a28;
        f += T32_4D98C8(tab + off);
    } else {
        f += idle;
    }
    BU8(u, 0x554490) = (uint8_t)f;
    if (cls == 6) {
        int down = BS8(u, 0x554498), up = down;
        for (int i = 0; i < 8; i++) {
            if (T8_4D9C08(0x4d9c30 + 8 * up + BS8(u, 0x554499)) != 0) {
                BU8(u, 0x554498) = (uint8_t)up;
                break;
            }
            if (++up >= 8)
                up = 0;
            if (--down < 0)
                down = 7;
            if (T8_4D9C08(0x4d9c30 + 8 * up + BS8(u, 0x554499)) != 0) {
                BU8(u, 0x554498) = (uint8_t)up;
                break;
            }
            if (T8_4D9C08(0x4d9c30 + 8 * down + BS8(u, 0x554499)) != 0) {
                BU8(u, 0x554498) = (uint8_t)down;
                break;
            }
        }
        f = T8_4D9C08(0x4d9c30 + 8 * BS8(u, 0x554498) + BS8(u, 0x554499));
        f += T32_4D98C8(0x4d9a28 + (BU8(u, 0x55448e) & 0xfc));
        BU8(u, 0x554490) = (uint8_t)f;
        BU8(u, 0x554491) = (uint8_t)(6 * (BS8(u, 0x554498) % 8));
    }
    BU8(u, 0x55448d) = BU8(u, 0x554499);
    return 0;
}

/* Port of FUN_00486d83 (684 B) — attack sequencer for the current unit. Class >= 7: 0x488436. Else frame
 * +0x10 = stride(class) * (facing +0x18 % 8, signed) + phase / 4, the phase +0x0e stepping 0..0x17 (read
 * before the step); class 6: frame = facing % 8, overlay +0x11 = 6 * (facing % 8) + phase / 4 (after the
 * step). The drawn direction +0x0d = facing. */
int bf_486d83(void)
{
    int u   = g_bf_53e8f8;
    int cls = BU8(u, 0x554492);
    if (cls >= 7) {
        bf_488436();
        return 0;
    }
    int stride;
    switch (cls) {
    case 3: case 2: stride = 12; break;
    case 5: case 1: stride = 13; break;
    case 4:         stride = 8; break;
    case 0:         stride = 10; break;
    default:        stride = 1; break;
    }
    int32_t f = stride * (BS8(u, 0x554498) % 8);
    f += BU8(u, 0x55448e) >> 2;
    if (++BU8(u, 0x55448e) >= 0x18)
        BU8(u, 0x55448e) = 0;
    BU8(u, 0x554490) = (uint8_t)f;
    if (cls == 6) {
        BU8(u, 0x554490) = (uint8_t)(BS8(u, 0x554498) % 8);
        BU8(u, 0x554491) = (uint8_t)(6 * (BS8(u, 0x554498) % 8) + (BU8(u, 0x55448e) >> 2));
    }
    BU8(u, 0x55448d) = BU8(u, 0x554498);
    return 0;
}

/* Port of FUN_004872ae (841 B) — stand / turn sequencer for the current unit. Class >= 7: 0x488793. Else
 * the frame tick +0x0b counts to the period +0x0c; on the wrap the drawn direction +0x0d turns one step
 * (position word +0x20 odd: +1, wrapping at 8; even: -1 — retail's wrap test there is on a zero-extended byte
 * and never fires, so 0 turns to 0xff). Frame +0x10 = stride(class) * (direction & 7) + a per-unit offset
 * (unit index & 7; 6 → 1, 7 → 2). Class 6: frame = facing % 8, overlay = 6 * (facing % 8). */
int bf_4872ae(void)
{
    int u   = g_bf_53e8f8;
    int cls = BU8(u, 0x554492);
    if (cls >= 7) {
        bf_488793();
        return 0;
    }
    int stride;
    switch (cls) {
    case 3: case 2: stride = 12; break;
    case 5: case 1: stride = 13; break;
    case 4:         stride = 8; break;
    case 0:         stride = 10; break;
    default:        stride = 1; break;
    }
    int ofs = g_bf_53e8f8 & 7;
    if (ofs == 6)
        ofs = 1;
    else if (ofs == 7)
        ofs = 2;
    if (++BU8(u, 0x55448b) > BU8(u, 0x55448c)) {
        BU8(u, 0x55448b) = 0;
        if (BS16(u, 0x5544a0) & 1) {
            if (++BU8(u, 0x55448d) >= 8)
                BU8(u, 0x55448d) = 0;
        } else {
            BU8(u, 0x55448d)--;
        }
    }
    BU8(u, 0x554490) = (uint8_t)((BU8(u, 0x55448d) & 7) * stride + ofs);
    if (cls == 6) {
        BU8(u, 0x554490) = (uint8_t)(BS8(u, 0x554498) % 8);
        BU8(u, 0x554491) = (uint8_t)(6 * (BS8(u, 0x554498) % 8));
    }
    return 0;
}

/* Port of FUN_00487908 (494 B) — the order-9 sequencer (via 0x486219). Class >= 7: 0x488793; class 6:
 * nothing. Else frame +0x10 = base(class) + 3 * ((facing & 6) >> 1) + phase / 32, the phase +0x0e stepping
 * 0..0x5f; the drawn direction +0x0d = facing. (No corpus scenario reaches it: unverified.) */
int bf_487908(void)
{
    int u   = g_bf_53e8f8;
    int cls = BU8(u, 0x554492);
    if (cls >= 7) {
        bf_488793();
        return 0;
    }
    int base;
    switch (cls) {
    case 3: case 2: base = 102; break;
    case 5: case 1: base = 110; break;
    case 4:         base = 70; break;
    case 0:         base = 86; break;
    default:        return 0;
    }
    int32_t f = 3 * ((uint32_t)(BS8(u, 0x554498) & 6) >> 1);
    if (++BU8(u, 0x55448e) >= 0x60)
        BU8(u, 0x55448e) = 0;
    f += BU8(u, 0x55448e) >> 5;
    BU8(u, 0x554490) = (uint8_t)(base + f);
    BU8(u, 0x55448d) = BU8(u, 0x554498);
    return 0;
}

/* Port of FUN_00487ce4 (461 B) — dying sequencer for the current unit. Class >= 7: 0x48895e; class 6: the
 * stand sequencer (0x486209). Else frame +0x10 = base(class) + death timer +0x173 (signed) / 4 — only while
 * that is <= 5 (the last frame holds); the drawn direction +0x0d = facing. */
int bf_487ce4(void)
{
    int u   = g_bf_53e8f8;
    int cls = BU8(u, 0x554492);
    if (cls >= 7) {
        bf_48895e();
        return 0;
    }
    int base;
    switch (cls) {
    case 3: case 2: base = 96; break;
    case 5: case 1: base = 104; break;
    case 4:         base = 64; break;
    case 0:         base = 80; break;
    default:
        if (BU8(g_bf_53e8f8, 0x554492) >= 7)
            bf_488793();
        else
            bf_486209();
        return 0;
    }
    int t = BS8(u, 0x5545f3) >> 2;
    if (t <= 5)
        BU8(u, 0x554490) = (uint8_t)(base + t);
    BU8(u, 0x55448d) = BU8(u, 0x554498);
    return 0;
}

/* Port of FUN_0048804a (502 B) — missile-shot sequencer for the current unit. Class >= 7: 0x488793. Else
 * frame +0x10 = 13 * ((facing + rotation fix) % 8, signed) + 10, the fix per view rotation DAT_00522f7c
 * (0: 0, 2: +6, 4: +4, 6: +2; any other value leaves the local unset: stack residue), plus for class 1 / 5 the
 * draw table byte_4D9B18 / byte_4D9AC8 [shot windup +0x180 (signed), clamped to 79 from above]. The drawn
 * direction +0x0d = facing. */
int bf_48804a(void)
{
    int u   = g_bf_53e8f8;
    int cls = BU8(u, 0x554492);
    if (cls >= 7) {
        bf_488793();
        return 0;
    }
    int32_t f = (int32_t)bf_unit_res_48804a;
    if (g_bf_522f7c == 0)
        f = 13 * (BS8(u, 0x554498) % 8);
    else if (g_bf_522f7c == 2)
        f = 13 * ((BS8(u, 0x554498) + 6) % 8);
    else if (g_bf_522f7c == 4)
        f = 13 * ((BS8(u, 0x554498) + 4) % 8);
    else if (g_bf_522f7c == 6)
        f = 13 * ((BS8(u, 0x554498) + 2) % 8);
    int s = BS16(u, 0x554600);
    if (s >= 80)
        s = 79;
    if (cls == 1)
        f += T8_4D9AC8(0x4d9b18 + s);
    else if (cls == 5)
        f += T8_4D9AC8(0x4d9ac8 + s);
    BU8(u, 0x554490) = (uint8_t)(f + 10);
    BU8(u, 0x55448d) = BU8(u, 0x554498);
    bf_unit_res_48804a = (uint32_t)f;
    return 0;
}

/* Port of FUN_00488436 (861 B) — attack sequencer for class 7..10 (the engines). Class 8 first re-aims its
 * engine frame +0x168 from the facing (1: 0 / 1, 3: 1 / 2, 5: 3 / 2, 7: 3 / 0 by the current frame; even
 * facings: facing / 2 (signed shift)), +0x168 = 2 * that. Then frame +0x10: class 10 = 35 + phase / 8 (phase
 * +0x0e stepping 0..0x2f), 9 = 0, 8 = that aim + 1, 7 = facing + 5; class 7's overlay +0x11 = 5 * facing,
 * - 20 from facing 4. Any other class leaves the frame local unset: stack residue (bf_unit_res_488436). */
int bf_488436(void)
{
    int u       = g_bf_53e8f8;
    uint32_t fr = bf_unit_res_488436[0];   /* [ebp-4] */
    uint32_t am = bf_unit_res_488436[1];   /* [ebp-8] */
    if (BU8(u, 0x554492) == 8) {
        switch (BS8(u, 0x554498)) {
        case 1: am = BU8(u, 0x5545e8) == 2 ? 1 : 0; break;
        case 3: am = BU8(u, 0x5545e8) == 2 ? 1 : 2; break;
        case 5: am = BU8(u, 0x5545e8) == 6 ? 3 : 2; break;
        case 7: am = BU8(u, 0x5545e8) == 6 ? 3 : 0; break;
        default: am = (uint32_t)(BS8(u, 0x554498) >> 1); break;
        }
        BU8(u, 0x5545e8) = (uint8_t)(am + am);
    }
    switch (BU8(u, 0x554492)) {
    case 10:
        if (++BU8(u, 0x55448e) >= 0x30)
            BU8(u, 0x55448e) = 0;
        fr = (uint32_t)(BU8(u, 0x55448e) >> 3) + 35;
        break;
    case 9: fr = 0; break;
    case 8: fr = am + 1; break;
    case 7: fr = (uint32_t)(BS8(u, 0x554498) + 5); break;
    default: break;
    }
    BU8(u, 0x554490) = (uint8_t)fr;
    if (BU8(u, 0x554492) == 7) {
        fr = (uint32_t)(5 * BS8(u, 0x554498));
        if (BS8(u, 0x554498) >= 4)
            fr -= 20;
        BU8(u, 0x554491) = (uint8_t)fr;
    }
    bf_unit_res_488436[0] = fr;
    bf_unit_res_488436[1] = am;
    return 0;
}

/* Port of FUN_00488793 (459 B) — walk / stand sequencer for class 7..10: frame +0x10: class 10 = 35 + phase / 8
 * (phase +0x0e stepping 0..0x2f), 9 = 0, 8 = engine frame +0x168 / 2 + 1, 7 = facing + 5; class 7's overlay
 * +0x11 = 5 * facing, - 20 from facing 4. Any other class (the order handlers 0x4842c6 / 0x48437e /
 * 0x4843bc call it for every unit) stores the unset local: stack residue (bf_unit_res_488793). */
int bf_488793(void)
{
    int u      = g_bf_53e8f8;
    uint32_t v = bf_unit_res_488793;   /* [ebp-4] */
    switch (BU8(u, 0x554492)) {
    case 10:
        if (++BU8(u, 0x55448e) >= 0x30)
            BU8(u, 0x55448e) = 0;
        v = (uint32_t)(BU8(u, 0x55448e) >> 3) + 35;
        break;
    case 9: v = 0; break;
    case 8: v = (uint32_t)(BU8(u, 0x5545e8) >> 1) + 1; break;
    case 7: v = (uint32_t)(BS8(u, 0x554498) + 5); break;
    default: break;
    }
    BU8(u, 0x554490) = (uint8_t)v;
    if (BU8(u, 0x554492) == 7) {
        v = (uint32_t)(5 * BS8(u, 0x554498));
        if (BS8(u, 0x554498) >= 4)
            v -= 20;
        BU8(u, 0x554491) = (uint8_t)v;
    }
    bf_unit_res_488793 = v;
    return 0;
}

/* Port of FUN_0048895e (763 B) — dying / idle sequencer for class 7..10: frame +0x10: class 10 = 42 + engine
 * frame +0x168 / 2, 9 = 0, 8 = +0x168 / 2 + 1, 7 = facing + 5. Class 7: overlay +0x11 = 5 * facing (- 20 from
 * facing 4) + byte_4D9C08[phase / 4], the phase +0x0e stepping 0..0xa0 while the windup +0x180 >= 100, else
 * held at 0. Class 9: the phase steps DOWN (0 → 0xff, clamped to 0x8f), overlay = phase / 16. Any other class
 * (the order handlers call it for every unit) stores the unset local: stack residue (bf_unit_res_48895e). */
int bf_48895e(void)
{
    int u      = g_bf_53e8f8;
    uint32_t v = bf_unit_res_48895e;   /* [ebp-4] */
    switch (BU8(u, 0x554492)) {
    case 10: v = (uint32_t)(BU8(u, 0x5545e8) >> 1) + 42; break;
    case 9:  v = 0; break;
    case 8:  v = (uint32_t)(BU8(u, 0x5545e8) >> 1) + 1; break;
    case 7:  v = (uint32_t)(BS8(u, 0x554498) + 5); break;
    default: break;
    }
    BU8(u, 0x554490) = (uint8_t)v;
    if (BU8(u, 0x554492) == 7) {
        v = (uint32_t)(5 * BS8(u, 0x554498));
        if (BS8(u, 0x554498) >= 4)
            v -= 20;
        if (BS16(u, 0x554600) >= 100) {
            if (++BU8(u, 0x55448e) > 0xa0)
                BU8(u, 0x55448e) = 0;
        } else {
            BU8(u, 0x55448e) = 0;
        }
        v += T8_4D9C08(0x4d9c08 + (BU8(u, 0x55448e) >> 2));
        BU8(u, 0x554491) = (uint8_t)v;
    }
    if (BU8(u, 0x554492) == 9) {
        BU8(u, 0x55448e)--;   /* retail's "< 0" test is on a zero-extended byte: never true */
        if (BU8(u, 0x55448e) >= 0x90)
            BU8(u, 0x55448e) = 0x8f;
        v = (uint32_t)(BU8(u, 0x55448e) >> 4);
        BU8(u, 0x554491) = (uint8_t)v;
    }
    bf_unit_res_48895e = v;
    return 0;
}

/* Port of FUN_0049459a (878 B) — attrition of unit `u` on a kind-10 / kind-17 cell (0x4822ed): unless its
 * order +0x31 is 2 (dead) already, the damage accumulator +0x19a (signed word) grows by a step per DAT_00522f6c
 * (signed: <= 1, 2, 3, more; +0x194 set: 1 / 3 / 5 / 7 against 160, + 2 with the condition flag +0x13; else
 * 3 / 6 / 9 / 12 against 100, + 1 with +0x13); at the limit it drops by it and one troop +0x1a0 is lost. At
 * <= 0 troops: the cue 0x4262cf(11) for formation slot +0x17a 0 / (12) for slot 4, and order 2 (dead). */
int bf_49459a(int u)
{
    if (BS8(u, 0x5544b1) == 2)
        return 0;
    int lim;
    int lvl = (int32_t)s_slot_00522f6c;
    if (BU8(u, 0x554614) != 0) {
        lim = 160;
        if (lvl <= 1)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 1);
        else if (lvl <= 2)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 3);
        else if (lvl <= 3)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 5);
        else
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 7);
        if (BU8(u, 0x554493) != 0)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 2);
    } else {
        lim = 100;
        if (lvl <= 1)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 3);
        else if (lvl <= 2)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 6);
        else if (lvl <= 3)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 9);
        else
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 12);
        if (BU8(u, 0x554493) != 0)
            BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) + 1);
    }
    if (BS16(u, 0x55461a) >= lim) {
        BS16(u, 0x55461a) = (int16_t)(BS16(u, 0x55461a) - lim);
        BS16(u, 0x554620) = (int16_t)(BS16(u, 0x554620) - 1);
    }
    if (BS16(u, 0x554620) <= 0) {
        if (BS8(u, 0x5545fa) == 0)
            bf_4262cf(11);
        else if (BS8(u, 0x5545fa) == 4)
            bf_4262cf(12);
        BU8(u, 0x5544b1) = 2;
    }
    return 0;
}
