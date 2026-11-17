/* engine_bf_input.c — the battlefield port, partition 'input' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does. */
#include "engine_bf.h"
#include "engine_bf_input_globals.h"
#include "rl.h"
#include "engine_battlefield_render.h"
#include "engine.h"
#include "engine_combat.h"
#include "engine_territory.h"
#include "strutil.h"

/* the selection mask of player p's units: realm record byte +0xee (DAT_0057bfee) */
#define SEL_MASK(p) AT(uint8_t, g_player_strategic_352, 0x57bf00u, 0x57bfeeu + 352 * (p))
/* the battlefield pixel → cell quantizers (engine_battlefield_render.c: retail-layout since the screens render batch) */
static int px_col_right(int px) { return engine_battlefield_render_pixel_to_col_right_biased(px); }   /* 0x4bc2e5 */
static int px_col_left(int px)  { return engine_battlefield_render_pixel_to_col_left_biased(px); }    /* 0x4bc346 */
static int px_row_bottom(int py) { return engine_battlefield_render_pixel_to_row_bottom_biased(py); } /* 0x4bc3ac */
static int px_row_top(int py)    { return engine_battlefield_render_pixel_to_row_top_biased(py); }    /* 0x4bc40d */

/* Port of FUN_00479a71 (231 B) — deselect everything player `p` holds: every unit selected by p (+9 == p) loses
 * it, every unit's selection bit for p (+0xa, mask realm +0xee) is cleared; the panel redraw gate DAT_0053e9ac = 1
 * and the four class counts of the selection (DAT_00553ef0 / 005533c0 / 00569504 / 0053e8bc) are zeroed. The loop
 * index is retail's global DAT_0053e8f8. Returns the last 432 * index (retail's EAX). */
int bf_479a71(int p)
{
    uint8_t keep = (uint8_t)~SEL_MASK(p);
    int r = 352 * p;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        if (BU8(g_bf_53e8f8, 0x554489) == p)
            BU8(g_bf_53e8f8, 0x554489) = 0;
        r = 432 * g_bf_53e8f8;
        BU8(g_bf_53e8f8, 0x55448a) &= keep;
    }
    engine_territory_dirty_state()->below_status_redraw = 1;   /* DAT_0053e9ac */
    s_slot_00553ef0 = 0;
    s_slot_005533c0 = 0;
    s_slot_00569504 = 0;
    s_slot_0053e8bc = 0;
    return r;
}

/* Port of FUN_0043c55c (33 B) — deselect all (0x479a71) and gate a panel redraw. */
int bf_43c55c(int p)
{
    int r = bf_479a71(p);
    engine_territory_dirty_state()->below_status_redraw = 1;   /* DAT_0053e9ac */
    return r;
}

/* Port of FUN_0043c4c6 (150 B) — drop unit `u` from its owner's selection: its selection bit (+0xa) and its
 * selector (+9) cleared, then the owner's selection regrouped (0x478987). */
int bf_43c4c6(int u)
{
    int owner = BU8(u, 0x5544ac);
    BU8(u, 0x55448a) &= (uint8_t)~SEL_MASK(owner);
    BU8(u, 0x554489) = 0;
    int r = bf_478987(owner);
    engine_territory_dirty_state()->below_status_redraw = 1;   /* DAT_0053e9ac */
    return r;
}

/* Port of FUN_0047a76d (167 B) — "charge": every foot unit (class < 7) of player `p` gets order 8 (advance) and its
 * group's routed/charge flag +0x2a set. Returns retail's EAX (the last 432 * index computed). */
int bf_47a76d(int p)
{
    int r = 0;
    for (int i = 1; i < 81; i++) {
        r = 432 * i;
        if (BU8(i, 0x5544ac) == p) {
            if (BU8(i, 0x554492) < 7) {
                BG8(BS16(i, 0x5545f8), 0x56654a) = 1;
                BU8(i, 0x5544b1) = 8;
            }
        }
    }
    return r;
}

/* Port of FUN_00479cf7 (277 B) — classify the drag from the press point (DAT_0057a0f4, DAT_0057a0e8) to the cursor
 * (DAT_004e65fc, DAT_004e65f4): both corners in 16-px cells are kept (DAT_005653f0 / 005653ec / 005656f4 /
 * 005656e8); a drag of more than 24 px on either axis is a box (1); else 2 while an enemy / a target is hovered
 * (DAT_0057cac8, DAT_00553f90), else 0 (a click). */
int bf_479cf7(void)
{
    int dx = g_bf_4e65fc <= g_bf_57a0f4 ? g_bf_57a0f4 - g_bf_4e65fc : g_bf_4e65fc - g_bf_57a0f4;
    int dy = g_bf_4e65f4 <= g_bf_57a0e8 ? g_bf_57a0e8 - g_bf_4e65f4 : g_bf_4e65f4 - g_bf_57a0e8;
    g_bf_5653f0 = g_bf_57a0f4 / 16;
    g_bf_5653ec = g_bf_4e65fc / 16;
    g_bf_5656f4 = g_bf_57a0e8 / 16;
    g_bf_5656e8 = g_bf_4e65f4 / 16;
    if (dx > 24)
        return 1;
    if (dy > 24)
        return 1;
    if (g_bf_57cac8 != 0)
        return 2;
    if (g_bf_553f90 != 0)
        return 2;
    return 0;
}

/* Port of FUN_00479b58 (415 B) — box-select for player `p`: the screen rectangle (x0, y0)–(x1, y1) (either corner
 * order) quantized to cells (the right / left / bottom / top-biased quantizers, + the camera cell (col0, row0)), and
 * every unit occupying a cell of it (cell +5) gets p's selection bit (+0xa); p's own units are selected (+9 = p);
 * the last one found is the panel's unit (DAT_00553e68). Returns the bottom row (retail's EAX). */
int bf_479b58(int p, int col0, int row0, int x0, int y0, int x1, int y1)
{
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int c0 = px_col_right(x0) + col0;
    int c1 = px_col_left(x1) + col0;
    int r0 = px_row_bottom(y0) + row0;
    int r1 = px_row_top(y1) + row0;
    int off = 8 * (c0 + 80 * r0);
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            uint8_t u = BCELL8(off, 0x5440e5);
            if (u != 0) {
                BU8(u, 0x55448a) |= SEL_MASK(p);
                if (BU8(u, 0x5544ac) == p)
                    BU8(u, 0x554489) = (uint8_t)p;
                g_bf_553e68 = u;
            }
            off += 8;
        }
        off += 8 * (80 - (c1 - c0 + 1));
    }
    return r1;
}

/* Port of FUN_00478f0b (392 B) — keep player `p`'s selection to one group: the group of p's first selected unit
 * (+0x14c); every other unit p holds is deselected (+9 = 0, p's bit in +0xa cleared). */
int bf_478f0b(int p)
{
    int r = 352 * p;
    uint8_t keep = (uint8_t)~SEL_MASK(p);
    int g = 0;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        r = 432 * g_bf_53e8f8;
        if (BU8(g_bf_53e8f8, 0x5544ac) != 0) {
            r = 432 * g_bf_53e8f8;
            if (BU8(g_bf_53e8f8, 0x554489) == p) {
                r = BS16(g_bf_53e8f8, 0x5545f8);
                g = r;
                break;
            }
        }
    }
    if (g > 0) {
        for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
            r = 432 * g_bf_53e8f8;
            if (BU8(g_bf_53e8f8, 0x5544ac) != 0) {
                r = 432 * g_bf_53e8f8;
                if (BU8(g_bf_53e8f8, 0x554489) == p) {
                    r = BS16(g_bf_53e8f8, 0x5545f8);
                    if (r != g) {
                        BU8(g_bf_53e8f8, 0x554489) = 0;
                        r = 432 * g_bf_53e8f8;
                        BU8(g_bf_53e8f8, 0x55448a) &= keep;
                    }
                }
            }
        }
    }
    return r;
}

/* Port of FUN_00478987 (1412 B) — regroup player `p`'s selection. The first selected unit's group is the panel's
 * group (DAT_0053e984, for the local player DAT_0057c8cc); the selection is counted by class (7: DAT_00553ef0,
 * 8: DAT_005533c0, 9: DAT_00569504, 10: DAT_0053e8bc + its cell kind DAT_00568968, else DAT_00565404) and the
 * counts zeroed when more than one kind is present. The first group's retarget clock (+0x14) restarts at 20. If the
 * selection is not exactly that whole group (another unit of the group unselected, or a selected unit of another
 * group), a new group is made (0x46e97a from the first unit's owner / +0x13) for the selection: retarget 20, the
 * side flag +3 copied, member range +4/+6, stance 1 (ranged first unit +0x13e) else 3, each member's group set;
 * then the group aggregate (0x488dfe). */
int bf_478987(int p)
{
    s_slot_00553ef0 = 0;
    s_slot_005533c0 = 0;
    s_slot_00569504 = 0;
    s_slot_0053e8bc = 0;
    s_slot_00568968 = 0;
    s_slot_00565404 = 0;
    int g = 0, first = 0;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) != 0 && BU8(u, 0x554489) == p) {
            if ((int)s_slot_0057c8cc == p)
                s_slot_0053e984 = (uint32_t)(int32_t)BS16(u, 0x5545f8);
            g = BS16(u, 0x5545f8);
            first = u;
            break;
        }
    }
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) != 0 && BU8(u, 0x554489) == p) {
            switch (BU8(u, 0x554492)) {
            case 7:  s_slot_00553ef0++; break;
            case 8:  s_slot_005533c0++; break;
            case 9:  s_slot_00569504++; break;
            case 10:
                s_slot_00568968 = BCELL8(BS32(u, 0x55449c), 0x5440e7);
                s_slot_0053e8bc++;
                break;
            default: s_slot_00565404++; break;
            }
        }
    }
    int kinds = s_slot_00553ef0 != 0;
    if (s_slot_005533c0 != 0) kinds++;
    if (s_slot_00569504 != 0) kinds++;
    if (s_slot_0053e8bc != 0) kinds++;
    if (s_slot_00565404 != 0) kinds++;
    if (kinds > 1) {
        s_slot_00553ef0 = 0;
        s_slot_005533c0 = 0;
        s_slot_00569504 = 0;
        s_slot_0053e8bc = 0;
        s_slot_00568968 = 0;
        s_slot_00565404 = 0;
    }
    int split = 0;
    for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
        int u = g_bf_53e8f8;
        if (BU8(u, 0x5544ac) != 0) {
            if (BU8(u, 0x554489) == 0 && BS16(u, 0x5545f8) == g)
                split = 1;
            if (BU8(u, 0x554489) == p && BS16(u, 0x5545f8) != g)
                split = 1;
        }
    }
    int r = 13 * g;
    BG16(g, 0x566534) = 20;
    if (split != 0) {
        r = bf_46e97a(BU8(first, 0x5544ac), BU8(first, 0x554493));
        if (r != 0) {
            int ng = g_bf_5530ec;
            BG16(ng, 0x566534) = 20;
            BG8(ng, 0x566523) = BG8(g, 0x566523);
            for (g_bf_53e8f8 = 1; g_bf_53e8f8 < 81; g_bf_53e8f8++) {
                int u = g_bf_53e8f8;
                if (BU8(u, 0x5544ac) != 0 && BU8(u, 0x554489) == p) {
                    ng = g_bf_5530ec;
                    if (BG16(ng, 0x566524) == 0)
                        BG16(ng, 0x566524) = (int16_t)u;
                    BG16(ng, 0x566526) = (int16_t)u;
                    BG8(ng, 0x566528) = BU8(u, 0x5545ea) != 0 ? 1 : 3;
                    BS16(u, 0x5545f8) = (int16_t)ng;
                    if ((int)s_slot_0057c8cc == p)
                        s_slot_0053e984 = (uint32_t)ng;
                }
            }
            return bf_488dfe();
        }
    }
    return r;
}

/* Port of FUN_0043c247 (98 B) — apply a box selection for player `p`: deselect all (0x479a71), box-select the
 * rectangle (0x479b58, camera cell (col0, row0)), and with `regroup` (single player) regroup the selection into
 * one group (0x478987 / 0x478f0b) and refresh the panel (0x481b9a). */
int bf_43c247(int p, int regroup, int col0, int row0, int x0, int y0, int x1, int y1)
{
    bf_479a71(p);
    int r = bf_479b58(p, col0, row0, x0, y0, x1, y1);
    if (regroup == 1) {
        bf_478987(p);
        bf_478f0b(p);
        return rl_481b9a();
    }
    return r;
}

/* ---------------------------------------------------------------- seams: retail functions this partition calls */
#define MP_ON (g_sim_mp_active != 0)   /* DAT_00553030 */
static int mp_message(int id)  { return rl_43eda0(id, 0); }
static int wav(const void *name) { return (int)rl_427990((uint32_t)(uintptr_t)name, 1u, 0u); }
/* the confirm dialog 0x40e6f2(msg, x, y, callback VA): retail keeps the callback's ADDRESS — retail's VA here
 * (rl_40e6f2, rl ui4) */
static int confirm(int msg, uint32_t cb_va) { return rl_40e6f2(msg, 160, 160, (int)cb_va); }
static int cursor_in(int x, int y, int w, int h)   /* 0x4b1deb (rl_host) */
{ return rl_4b1deb(x, y, w, h); }

/* Port of FUN_00499cb1 (204 B) — a unit voice cue for player `p`'s nation (DAT_0055408c): kind `k` (0 selected,
 * 1 move, 2 attack, 3 …) cycles its take 0..3 (DAT_0053ef60[11 p + nation]; kind 3 always take 0) and plays
 * peas_u1.wav's row (the 16-B name table at 0x4db0d0: nation × 256 + kind × 64 + take × 16); DAT_005533f0 = 1. */
int bf_499cb1(int k)
{
    int32_t *t = &g_bf_53ef60[11 * k + g_bf_55408c];
    if (++*t > 3)
        *t = 0;
    int take = *t;
    if (k == 3)
        take = 0;
    g_combat_audio_latch = 1;
    return wav(g_bf_4db0d0 + 256 * g_bf_55408c + 64 * k + 16 * take);
}

/* Port of FUN_0043bf07 (832 B) — the battlefield's left button (screen 0x29 / 0x2a): a press over the field starts a
 * drag (screen 0x2a: the press cell DAT_0055ce60/64, the press pixel DAT_0057a0f4/e8, the cell's unit → the panel
 * unit DAT_00553e68); while held a box drag reselects live (0x43c247, no regroup); the release (screen back to
 * 0x29) classifies the drag (0x479cf7): a click with a target (2) selects 8 px around the cursor, a box (1)
 * selects the box — regrouped in single player — with a "selected" voice cue when anything was selected.
 * Gated on the battle running (DAT_00568964 == 1) and no pending order (DAT_00553c6c). */
int bf_43bf07(void)
{
    if (s_slot_00568964 != 1)
        return 0;
    if (g_bf_553c6c != 0)
        return 0;
    if (g_bf_4eafb4 != 0) {
        if (g_bf_5651d0 == 0)
            return 0;
        if (engine_current_screen() == 42)
            return 0;
        g_bf_55ce60 = g_bf_553f88;
        g_bf_55ce64 = g_bf_553f94;
        g_bf_57a0f4 = g_bf_4e65fc;
        g_bf_57a0e8 = g_bf_4e65f4;
        engine_set_current_screen(42);
        if (BCELL8(g_bf_553f68, 0x5440e5) != 0)
            g_bf_553e68 = BCELL8(g_bf_553f68, 0x5440e5);
        return 1;
    }
    if (g_bf_4e65cc != 0 && engine_current_screen() == 42) {
        if (bf_479cf7() == 0)
            return 0;
        if (g_bf_4ea4b0 == 0)
            return 1;
        if (g_bf_53f238 != 0 && MP_ON)
            return 1;
        g_bf_53e98c = g_bf_553f88;
        g_bf_53e99c = g_bf_553f94;
        bf_43c247((int)s_slot_0057c8cc, 0, (int)s_slot_0055ce70, (int)s_slot_005678a0, g_bf_57a0f4, g_bf_57a0e8,
                  g_bf_4e65fc, g_bf_4e65f4);
        return 1;
    }
    if ((g_bf_4e65d8 == 0 && g_bf_4eabc5 == 0) || engine_current_screen() != 42)
        return 0;
    engine_set_current_screen(41);
    int kind = bf_479cf7();
    if (kind != 1) {
        if (kind == 2) {
            g_bf_57a0f4 -= 8;
            g_bf_57a0e8 -= 8;
            g_bf_4e65fc += 8;
            g_bf_4e65f4 += 8;
            bf_43c247((int)s_slot_0057c8cc, MP_ON ? 0 : 1, (int)s_slot_0055ce70, (int)s_slot_005678a0, g_bf_57a0f4,
                      g_bf_57a0e8, g_bf_4e65fc, g_bf_4e65f4);
            bf_499cb1(0);
            return 1;
        }
        return 0;
    }
    if (g_bf_553078 != 0) {
        bf_43c247((int)s_slot_0057c8cc, MP_ON ? 0 : 1, (int)s_slot_0055ce70, (int)s_slot_005678a0, g_bf_57a0f4,
                  g_bf_57a0e8, g_bf_4e65fc, g_bf_4e65f4);
        if (g_bf_553078 != 0)
            bf_499cb1(0);
    }
    return 1;
}

/* Port of FUN_0043c2a9 (541 B) — the battlefield's right button outside an order: a right press (DAT_004e6900)
 * deselects all unless over the minimap (480.. × 24..183); a left press over the unit roster (the panel's slots:
 * the table at 0x4d31f0, 7 dwords a slot, page offset DAT_00553220 = 0 / 12 / 30 by the selection size
 * DAT_00553078) picks the roster slot's unit (DAT_0056d5d0) and drops it from the selection (0x43c4c6; MP:
 * message 66). */
int bf_43c2a9(void)
{
    int slot = 0;
    if (g_bf_4e6900 != 0) {
        if (g_bf_4e65fc > 480 && g_bf_4e65f4 >= 24 && g_bf_4e65f4 < 184)
            return 0;
        bf_43c55c((int)s_slot_0057c8cc);
        return 1;
    }
    if (g_bf_4eafb4 == 0)
        return 0;
    if (g_bf_553078 > 12)
        g_bf_553220 = g_bf_553078 > 18 ? 30 : 12;
    else
        g_bf_553220 = 0;
    for (int i = 1; i < 81; i++) {
        if (BU8(i, 0x5544ac) == 0 || BU8(i, 0x554489) != (int)s_slot_0057c8cc)
            continue;
        if (slot >= 50)
            return 0;
        const int32_t *e = (const int32_t *)(g_bf_4d31f0 + 4 * (7 * slot + 7 * g_bf_553220));
        if (cursor_in(e[1], e[2], e[3], e[4]) == 0) {
            slot++;
            continue;
        }
        g_bf_56d5d0 = i;
        if (MP_ON)
            mp_message(66);
        else
            bf_43c4c6(g_bf_56d5d0);
        return 1;
    }
    return 0;
}

/* Port of FUN_0043c634 (326 B) — an order click at cell (col, row): the voice cue (attack when an enemy is hovered
 * DAT_005533b8, kind 3 for DAT_00553eec == 2, else move), the order marker (DAT_00553f84 = 30, MP 10; at
 * DAT_00553f14/18), the order serial (DAT_00591550 = ++DAT_00553100[player]; DAT_0059154c = 0), then the order
 * itself: 0x479e90(group DAT_0053e984, col, row, target, DAT_0053e874, 0), or MP message 67 ×3. Not while an
 * order is pending (DAT_00553c6c). Returns retail's EAX (garbage when it does nothing: 0 here). */
int bf_43c634(int col, int row)
{
    if (g_bf_553c6c != 0)
        return 0;
    if (g_bf_5533b8 != 0)
        bf_499cb1(2);
    else if (g_bf_553eec == 2)
        bf_499cb1(3);
    else
        bf_499cb1(1);
    g_bf_553f84 = MP_ON ? 10 : 30;
    AT(int32_t, g_bf_553f14, 0x553f14u, 0x553f14u) = col;
    AT(int32_t, g_bf_553f14, 0x553f14u, 0x553f18u) = row;
    AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu) = 0;
    AT(int32_t, g_bf_59154c, 0x59154cu, 0x591550u) = ++g_bf_553100[s_slot_0057c8cc];
    if (MP_ON) {
        AT(int32_t, g_bf_59154c, 0x59154cu, 0x591554u) = 0;
        mp_message(67);
        bf_sleep(30);
        AT(int32_t, g_bf_59154c, 0x59154cu, 0x591554u) = 1;
        mp_message(67);
        bf_sleep(30);
        AT(int32_t, g_bf_59154c, 0x59154cu, 0x591554u) = 1;
        return mp_message(67);
    }
    return bf_479e90((int)s_slot_0053e984, col, row, g_bf_5533b8, g_bf_53e874[0], 0);
}

/* Port of FUN_0043c57d (183 B) — the battlefield's right release: an order at the cursor cell (0x43c634) when
 * the battle runs, a selection exists (DAT_00553078), nothing is hovered on the panel (DAT_0057cac8 / DAT_00553f90),
 * the game is not paused (DAT_0053f238) and no order is pending. */
int bf_43c57d(void)
{
    if (g_bf_5651d0 == 0) return 0;
    if (g_bf_57cac8 != 0) return 0;
    if (g_bf_553f90 != 0) return 0;
    if (g_bf_4e65d8 == 0) return 0;
    if (g_bf_553078 == 0) return 0;
    if (g_bf_53f238 != 0) return 0;
    if (g_bf_553c6c != 0) return 0;
    bf_43c634(g_bf_553f88, g_bf_553f94);
    return 1;
}

/* ---------------------------------------------------------------- the control panel's buttons (router table) */

/* Port of FUN_0043b9a1 (136 B) — the hourglass: toggle the pause DAT_0053f238 (all bits: ~), full-screen redraw
 * (DAT_0053e9ac = 2); MP: message 21. DAT_00553ed8 = 1; pausing plays s032_01.wav. Only on the player's own
 * battlefield (DAT_005533d0). */
int bf_43b9a1(void)
{
    int r = 0;
    if (s_slot_005533d0 != 0) {
        if (MP_ON) {
            r = mp_message(21);
        } else {
            g_bf_53f238 = ~g_bf_53f238;
            engine_territory_dirty_state()->below_status_redraw = 2;   /* DAT_0053e9ac */
            engine_territory_mark_full_screen_dirty();
        }
        g_bf_553ed8 = 1;
        if (g_bf_53f238 == 1) {
            g_combat_audio_latch = 1;
            return wav(g_bf_4d57d0);
        }
    }
    return r;
}

/* Port of FUN_0043bd02 (101 B) — "Mop up enemy troops", once a battle (DAT_0055322c): every foot unit of the local
 * player charges (0x47a76d); MP: message 64. */
int bf_43bd02(void)
{
    if (s_slot_005533d0 != 0 && s_slot_0055322c == 0) {
        s_slot_0055322c = 1;
        if (MP_ON)
            return mp_message(64);
        return bf_47a76d((int)s_slot_0057c8cc);
    }
    return 0;
}

/* Port of FUN_0043bbd7 (16 B) — the retreat confirm's yes (IDA: 2 arguments from its call site, unused): leave the battlefield (0x43be65: engine_combat.c's
 * port through its rl_link entry). */
int bf_43bbd7(int a0, int a1) { (void)a0; (void)a1; return (int)rl_43be65(); }

/* Port of FUN_0043bb68 (111 B) — the retreat confirm's callback: back to the screen before the dialog
 * (DAT_0057c90c), DAT_00553ed8 = 1; on yes (DAT_0059151c) the retreat flag DAT_005653f4 = 1 and the battle ends
 * (0x43bbd7; MP: message 62). */
int bf_43bb68(void)
{
    engine_set_current_screen(g_bf_57c90c);
    g_bf_553ed8 = 1;
    int r = g_bf_59151c[0];
    if (g_bf_59151c[0] != 0) {
        s_slot_005653f4 = 1;
        if (MP_ON)
            return mp_message(62);
        return bf_43bbd7((int)s_slot_0057c8cc, (int)s_slot_005653f4);
    }
    return r;
}

/* Port of FUN_0043ba29 (208 B) — the retreat button: on the player's own battlefield (DAT_005533d0 == 1; in an MP
 * siege only for the side that is not the besieger DAT_0053ef5c), confirm "Surrender castle?" (11, callback
 * 0x43baf9) when the local player defends a siege, else "Retreat from field?" (12, 0x43bb68). Returns
 * retail's EAX: the defender county's owner byte when nothing opens. */
int bf_43ba29(void)
{
    int r = AT(uint8_t, g_per_territory_420, 0x52f0b0u, 0x52f0b0u + 420 * s_slot_00568218);
    if (s_slot_005533d0 == 1) {
        if (s_slot_0057a0f0 == 0 || !MP_ON || (r = (int)s_slot_0057c8cc, s_slot_0053ef5c != s_slot_0057c8cc)) {
            if (s_slot_005533d0 != 0) {
                if (s_slot_0053e8fc != 0 &&
                    (int)s_slot_0057c8cc == AT(uint8_t, g_per_territory_420, 0x52f0b0u, 0x52f0b0u + 420 * s_slot_00568218))
                    return confirm(11, 0x43baf9u);
                return confirm(12, 0x43bb68u);
            }
        }
    }
    return r;
}

/* Port of FUN_0043bdcd (152 B) — the autocalc confirm's callback: back to the previous screen; on yes leave the
 * battlefield (0x43be65) — MP: write the casualties back (0x47f474), stage the result block
 * (0x47f76d) and send messages 51, 51, 63, 63 with Sleeps between. */
int bf_43bdcd(void)
{
    engine_set_current_screen(g_bf_57c90c);
    int r = g_bf_59151c[0];
    if (g_bf_59151c[0] == 1) {
        if (MP_ON) {
            rl_47f474();
            AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu) = 0;
            bf_47f76d();
            mp_message(51);
            bf_sleep(50);
            mp_message(51);
            bf_sleep(20);
            mp_message(63);
            bf_sleep(50);
            return mp_message(63);
        }
        return (int)rl_43be65();   /* engine_combat_battlefield_end 0x43be65 (rl_link) */
    }
    return r;
}

/* Port of FUN_0043bd67 (102 B) — the autocalc button: confirm "Autocalc battle?" (9, callback 0x43bdcd) on the
 * player's own battlefield (DAT_005533d0 == 1), in an MP siege only for the side that is not the besieger. */
int bf_43bd67(void)
{
    int r = 0;
    if (s_slot_005533d0 == 1) {
        if (s_slot_0057a0f0 == 0)
            return confirm(9, 0x43bdcdu);
        if (!MP_ON)
            return confirm(9, 0x43bdcdu);
        r = (int)s_slot_0057c8cc;
        if (s_slot_0053ef5c != s_slot_0057c8cc)
            return confirm(9, 0x43bdcdu);
    }
    return r;
}

/* ---------------------------------------------------------------- leaving the battlefield */

/* Port of FUN_004784ca (730 B) — a siege battle's castle record update (county DAT_0056822c's castle, the 0x300-B
 * records at 0x53f9b0): when engines were built (DAT_0057a0d8 rams / DAT_0056d648 …) store the counts, the two
 * breach timers (DAT_0053e990 / DAT_00553fb0) and DAT_0055307c / DAT_00569588; add (state +0x1c3 == 1) or set the
 * damage figures by the defence level DAT_0055401c (< 2: 10 per engine on +0x1d4 / +0x1e0, else 15 on +0x1d0 /
 * +0x1dc; always 15 / engine + 5 / ram on +0x1cc / +0x1d8); level → +0x1f9; state 2, +0x1c4 = 0; then the castle
 * file is written (0x4ae968 — save.c's castles_write_scenario takes a path: TODO(bf-link)). */
#define CASTLE(T, va) REC(T, g_per_army_768, 0x53f9b0u, 768, s_slot_0056822c, va)
int bf_4784ca(void)
{
    if (s_slot_0053e8fc != 0 && (s_slot_0057a0d8 != 0 || s_slot_0056d648 != 0)) {
        int32_t rams = (int32_t)s_slot_0057a0d8, eng = (int32_t)s_slot_0056d648;
        CASTLE(int16_t, 0x53fb94) = (int16_t)rams;
        CASTLE(int16_t, 0x53fb96) = (int16_t)eng;
        CASTLE(int32_t, 0x53fb98) = (int32_t)s_slot_0053e990;
        CASTLE(int32_t, 0x53fb9c) = (int32_t)s_slot_00553fb0;
        CASTLE(uint8_t, 0x53fba0) = (uint8_t)s_slot_0055307c;
        CASTLE(uint8_t, 0x53fba1) = (uint8_t)s_slot_00569588;
        int add = CASTLE(uint8_t, 0x53fb73) == 1;
        if ((int32_t)s_slot_0055401c < 2) {
            if (add) { CASTLE(int32_t, 0x53fb84) += 10 * eng; CASTLE(int32_t, 0x53fb90) += 10 * eng; }
            else     { CASTLE(int32_t, 0x53fb84) = 10 * eng;  CASTLE(int32_t, 0x53fb90) = 10 * eng; }
        } else if (CASTLE(uint8_t, 0x53fb73) == 1) {
            CASTLE(int32_t, 0x53fb80) += 15 * eng; CASTLE(int32_t, 0x53fb8c) += 15 * eng;
        } else {
            CASTLE(int32_t, 0x53fb80) = 15 * eng;  CASTLE(int32_t, 0x53fb8c) = 15 * eng;
        }
        if (CASTLE(uint8_t, 0x53fb73) == 1) {
            CASTLE(int32_t, 0x53fb7c) += 15 * (int32_t)s_slot_0056d648 + 5 * (int32_t)s_slot_0057a0d8;
            CASTLE(int32_t, 0x53fb88) += 15 * (int32_t)s_slot_0056d648 + 5 * (int32_t)s_slot_0057a0d8;
        } else {
            CASTLE(int32_t, 0x53fb7c) = 15 * (int32_t)s_slot_0056d648 + 5 * (int32_t)s_slot_0057a0d8;
            CASTLE(int32_t, 0x53fb88) = 15 * (int32_t)s_slot_0056d648 + 5 * (int32_t)s_slot_0057a0d8;
        }
        CASTLE(uint8_t, 0x53fba9) = (uint8_t)s_slot_0055401c;
        CASTLE(uint8_t, 0x53fb73) = 2;
        CASTLE(uint8_t, 0x53fb74) = 0;
        return rl_4ae968((int)s_slot_0056822c);
    }
    return 0;
}
#undef CASTLE

/* Port of FUN_0047f76d (458 B) — stage the MP battle-result block (0x515920, 42 B): both armies' counties' troop
 * figures (county +0x168, +0x196, the seven +0x16c.. pairs; attacker DAT_00568210, defender DAT_00568218), the
 * order serial DAT_0059154c (2 B), the winner DAT_0057c924 and its owner DAT_005440cc (1 B each). */
int bf_47f76d(void)
{
    uint8_t *d = g_bf_515920;
    uint8_t *a = g_per_territory_420 + 420 * s_slot_00568210;
    uint8_t *b = g_per_territory_420 + 420 * s_slot_00568218;
    strutil_bounded_copy(d, a + 360, 2);
    strutil_bounded_copy(d + 2, a + 406, 2);
    for (int i = 0; i < 7; i++)
        strutil_bounded_copy(d + 2 * i + 4, a + 364 + 2 * i, 2);
    strutil_bounded_copy(d + 20, b + 360, 2);
    strutil_bounded_copy(d + 22, b + 406, 2);
    for (int j = 0; j < 7; j++)
        strutil_bounded_copy(d + 2 * j + 24, b + 364 + 2 * j, 2);
    strutil_bounded_copy(d + 38, g_bf_59154c, 2);
    strutil_bounded_copy(d + 40, &s_slot_0057c924, 1);
    strutil_bounded_copy(d + 41, &g_combat_winner_owner, 1);
    return 0;
}

/* ---------------------------------------------------------------- the unit roster (the panel's HUD) */
/* the roster slot table at 0x4d31f0: 7 dwords a slot (sprite base, x, y, w, h, …), pages of 12 / 18 / 30 */
#define ROSTER(slot, k) (((const int32_t *)g_bf_4d31f0)[7 * (slot) + (k)])
static int sprite_opaque(int blob, int frame, int x, int y)   /* gfx_sprite_render_opaque_from_blob 0x40a567 (rl_link) */
{ return (int)rl_40a567((uint32_t)blob, (uint32_t)frame, (uint32_t)x, (uint32_t)y); }
static int label_dec(int v, int pad, const void *suffix, int x, int y, const void *font, int color)   /* gfx_text_label_render_dec 0x402f64 (rl_link) */
{ return (int)rl_402f64((uint32_t)v, (uint32_t)pad, (uint32_t)(uintptr_t)suffix, (uint32_t)x,
                                 (uint32_t)y, (uint32_t)(uintptr_t)font, (uint32_t)color); }

/* Port of FUN_004238b8 (285 B) — draw the roster's unit icons: each of the local player's selected units with
 * troops (+0x1a0), up to 80, its class icon (blob DAT_005530c8, frame = class + the slot's base) at the slot of the
 * page DAT_00553220. */
int bf_4238b8(void)
{
    int n = 0;
    for (int i = 1; i < 81; i++) {
        if (BU8(i, 0x5544ac) != 0 && BU8(i, 0x554489) == (int)s_slot_0057c8cc) {
            if (n >= 80)
                return 0;
            if (BS16(i, 0x554620) != 0) {
                sprite_opaque(g_bf_5530c8, BU8(i, 0x554492) + ROSTER(n + g_bf_553220, 0), ROSTER(n + g_bf_553220, 1),
                              ROSTER(n + g_bf_553220, 2));
                n++;
            }
        }
    }
    return 0;
}

/* Port of FUN_004239d5 (378 B) — the roster's troop counts: below page 30, each selected unit's count (+0x1a0) in
 * the label font (0x5af8f0, colour 63) right of its icon (x + 20, or + 12 for more than 100 — the 3-digit pad). */
int bf_4239d5(void)
{
    int n = 0;
    if (g_bf_553220 < 30) {
        for (int i = 1; i < 81; i++) {
            if (BU8(i, 0x5544ac) != 0 && BU8(i, 0x554489) == (int)s_slot_0057c8cc) {
                if (n >= 80)
                    return 0;
                if (BS16(i, 0x554620) != 0) {
                    n++;
                    int x = ROSTER(g_bf_553220 - 1 + n, 1), y = ROSTER(g_bf_553220 - 1 + n, 2);
                    if (BS16(i, 0x554620) <= 100)
                        label_dec(BS16(i, 0x554620), 32, g_bf_4d4450 + 4, x + 20, y + 2, g_bf_5af8f0, 63);
                    else
                        label_dec(BS16(i, 0x554620), 32, g_bf_4d4450, x + 12, y + 2, g_bf_5af8f0, 63);
                }
            }
        }
    }
    return 0;
}
