/* rl_terr.c — a retail-layout port, partition 'terr' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The strategic map's territory build (0x4676e0 / 0x467770 load, seeds, capitals / ports, the village update), the
 * cell edits 0x46c5b8 .. 0x46e1b0, the map queries, the AI's nearest-army queries, the minimap grids, the record
 * clears, the strategic path field (fill / trace / cost rebuild), the battlefield's greedy path walk, the event
 * queue and its popup step, the army-state handlers 2..4. Findings: docs/findings/rl-terr.md. */
#include <string.h>

#include "rl.h"
#include "rl_terr_globals.h"
#include "engine_map_query.h"       /* g_cell_cursor_col / _row (DAT_00569570 / 78: a mirror) */
#include "engine_territory.h"       /* g_cell_sweep_counter (DAT_0053e974), the current slot (DAT_0052afe0: a mirror),
                                       0x46e067 / 0x469b8d (linked, verified) */
#include "engine_path_field.h"      /* g_pf_* (DAT_00504020 / 24, 0x4f4070, 0x500c20, 0x4f2768, 0x507230 / 34: mirrors) */
#include "engine_path.h"            /* g_path_dist_dx / _dy (mirrors), 0x46f434 (linked, verified) */
#include "engine_army_state.h"      /* g_army_state_index (DAT_005533e8: a mirror) */
#include "engine_army_move.h"       /* 0x465d28 / 0x467196 (linked, verified), the executor's residue globals */
#include "engine_combat.h"          /* g_combat_active_terr (DAT_00553f8c), 0x4abd0f (linked, verified) */
#include "engine_realm.h"           /* 0x4ad6c0 (linked, verified) */
#include "engine_realm_upkeep.h"    /* 0x46f0a9 (linked, verified) */
#include "engine_diplomacy.h"       /* 0x4a16f7 (linked, verified) */
#include "engine_event_queue.h"     /* the port queue struct the linked ports take (terr_evq_*) */
#include "engine_minimap.h"         /* g_minimap_region_grid (DAT_0052aff0) */
#include "engine_mapview_globals.h" /* g_mv_ti_* (the terrain compute's outputs), g_mv_rotation (DAT_00522f7c) */
#include "gfx_globals.h"            /* g_gfx_current_blob (DAT_004eabec), g_gfx_screen_byte (DAT_004eac50) */
#include "engine.h"                 /* g_sim_mp_active (DAT_00553030) */

/* other partitions' globals (declared here as in their headers: those conflict in one unit) */
extern int32_t g_rl_55cea0[8385];   /* DAT_0055cea0 (rl_ui2_globals.h): the 129 × 65 terrain tile grid */
extern int32_t g_rl_56795c;         /* DAT_0056795c (rl_ui2_globals.h): the cell byte offset under the cursor */
extern int32_t g_rl_553f78;         /* DAT_00553f78 (rl_ui2_globals.h): the terrain compute's cell +3 */
extern int32_t g_rl_5651bc;         /* DAT_005651bc (rl_ui2_globals.h): the terrain compute's derived bit */
extern int32_t g_rl_553f54;         /* DAT_00553f54 (rl_render_globals.h) */
extern uint8_t g_rl_569590[16384];  /* DAT_00569590 (rl_render_globals.h): the second minimap grid */
extern uint8_t g_bf_4f9680[300];    /* DAT_004f9680 (engine_bf_move_globals.c; rhost: rh_hooks_bf_move.c): the greedy walk */

/* ---------------------------------------------------------------- records and retail's globals */
/* the overworld cell grid DAT_00522f90: 64 × 64 cells × 8 B; byte_522F9x[off] = CELL8(off, 0x522f9x) (IDA's names
 * around the grid — byte_522D97, byte_522F8F, byte_523197 … — are the same grid at a neighbour cell) */
#define CELL8(off, va)  AT(uint8_t, g_overworld_map, 0x522f90u, (int32_t)(va) + (int32_t)(off))
#define CELLS8(off, va) AT(int8_t,  g_overworld_map, 0x522f90u, (int32_t)(va) + (int32_t)(off))
/* the army records DAT_0052f0b0: 151 × 420 B (slot 0 unused) */
#define TER8(i, va)   AT(uint8_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (int32_t)(i))
#define TERS8(i, va)  AT(int8_t,  g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (int32_t)(i))
#define TER32(i, va)  AT(int32_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (int32_t)(i))
#define TERREC(i)     (&TER8(i, 0x52f0b0u))
/* the county records DAT_0053f9b0: 17 × 768 B (slot 0 unused) */
#define CO8(c, va)    AT(uint8_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (int32_t)(c))
#define COS8(c, va)   AT(int8_t,  g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (int32_t)(c))
#define CO32(c, va)   AT(int32_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (int32_t)(c))
#define CO16(c, va)   AT(int16_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (int32_t)(c))
/* the per-county cell roster DAT_0053ea00: 17 rows × 20 cell offsets (i32) */
#define ROSTER(k, i)  AT(int32_t, g_misc_1360, 0x53ea00u, 0x53ea00 + 80 * (int32_t)(k) + 4 * (int32_t)(i))
/* the realm records DAT_0057bf00: 6 × 352 B */
#define PL8(p, va)    AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (int32_t)(p))
/* the terrain tile grid DAT_0055cea0: 129 rows × 65 i32 */
#define TILE(r, c)    g_rl_55cea0[65 * (r) + (c)]
#define TILE_SPECIAL  0xfff0000                 /* 268369920: a tile word at or above it is a special tile */

#define CUR_COL   g_cell_cursor_col             /* DAT_00569570 (a mirror: engine_map_query) */
#define CUR_ROW   g_cell_cursor_row             /* DAT_00569578 */
#define CELLOFF   g_bf_5533cc                   /* DAT_005533cc: the sweeps' cell byte offset */
#define SWEEP8    g_bf_53e8b8                   /* DAT_0053e8b8: the unrolled clears' byte cursor */
#define LOCAL     ((int32_t)s_slot_0057c8cc)    /* DAT_0057c8cc: the local player */
#define NCOUNTY   ((int32_t)s_slot_0056d5dc)    /* DAT_0056d5dc: the county count */

/* ================================================================ the record clears */

/* Port of FUN_0046ea28 (57 B) — zero one 0x300-B county record. EAX: the last index (0x2ff). */
int rl_46ea28(int a0)
{
    uint8_t *p = (uint8_t *)(uintptr_t)(uint32_t)a0;
    for (unsigned i = 0; i < 0x300u; i++) p[i] = 0;
    return 0x2ff;
}

/* Port of FUN_0046ea61 (57 B) — zero one 0x160-B realm record. EAX: 0x15f. */
int rl_46ea61(int a0)
{
    uint8_t *p = (uint8_t *)(uintptr_t)(uint32_t)a0;
    for (unsigned i = 0; i < 0x160u; i++) p[i] = 0;
    return 0x15f;
}

/* Port of FUN_0046ead0 (57 B) — zero one 0x1a4-B army record. EAX: 0x1a3. */
int rl_46ead0(int a0)
{
    uint8_t *p = (uint8_t *)(uintptr_t)(uint32_t)a0;
    for (unsigned i = 0; i < 0x1a4u; i++) p[i] = 0;
    return 0x1a3;
}

/* Port of FUN_0046eb09 (54 B) — zero one 0x14-B unit-type record. EAX: 0x13. */
int rl_46eb09(int a0)
{
    uint8_t *p = (uint8_t *)(uintptr_t)(uint32_t)a0;
    for (unsigned i = 0; i < 0x14u; i++) p[i] = 0;
    return 0x13;
}

/* Port of FUN_0046f1dc (67 B) — clear the 17 county records (DAT_0053f9b0). */
int rl_46f1dc(void)
{
    int r = 0;
    for (int i = 0; i < 17; i++) r = rl_46ea28((int)(uintptr_t)&CO8(i, 0x53f9b0u));
    return r;
}

/* Port of FUN_0046f2ae (67 B) — clear the 13 unit-type records (DAT_00568dc0, 20 B each). */
int rl_46f2ae(void)
{
    int r = 0;
    for (int i = 0; i < 13; i++)
        r = rl_46eb09((int)(uintptr_t)&AT(uint8_t, g_unit_table_20, 0x568dc0u, 0x568dc0 + 20 * i));
    return r;
}

/* Port of FUN_0046f3cd (78 B) — clear the 151 army records (DAT_0052f0b0). */
int rl_46f3cd(void)
{
    int r = 0;
    for (int i = 0; i < 151; i++) r = rl_46ead0((int)(uintptr_t)TERREC(i));
    return r;
}

/* ================================================================ the event queue */
/* DAT_00568480: 50 × 24-B slots (+0 audience, +4 target, +8 code, +0xc a, +0x11 / +0x12 / +0x13 flags, +0x14 b);
 * the staging record DAT_00553510 (same layout); write / read indices DAT_00543fd0 / DAT_00553ec4, the notify
 * flag DAT_00565484, the popup cooldown DAT_0057c954. g_misc_128 is the ring's first 128 B (the save entry), the
 * rest is g_rl_568500 (contiguous under rhost, two blocks in the standalone). */
static uint8_t *evq_at(int32_t va)   /* the ring's byte at `va`: g_misc_128 (the save entry) or g_rl_568500 past it */
{
    return va < 0x568500 ? &AT(uint8_t, g_misc_128, 0x568480u, va) : &AT(uint8_t, g_rl_568500, 0x568500u, va);
}
#define EVQ_SLOT32(i, off) (*(int32_t *)evq_at(0x568480 + 24 * (int32_t)(i) + (off)))   /* no field straddles 0x568500 */
#define EVQ_SLOT8(i, off)  (*evq_at(0x568480 + 24 * (int32_t)(i) + (off)))
#define EVQ_STAGE32(off)   AT(int32_t, g_rl_553510, 0x553510u, 0x553510 + (off))
#define EVQ_W   g_rl_543fd0
#define EVQ_R   g_rl_553ec4

/* Port of FUN_00472b40 (133 B) — zero a queue slot: the code (+8) of slot `a0`, every other field of the READ
 * slot DAT_00553ec4 (retail's quirk: the init loop sweeps a0 = 0..49 with the read index at 0). EAX: 3 × read. */
int rl_472b40(int a0)
{
    EVQ_SLOT32(a0, 8) = 0;
    EVQ_SLOT32(EVQ_R, 0xc) = 0;
    EVQ_SLOT32(EVQ_R, 0x14) = 0;
    EVQ_SLOT8(EVQ_R, 0x12) = 0;
    EVQ_SLOT8(EVQ_R, 0x13) = 0;
    EVQ_SLOT8(EVQ_R, 0x11) = 0;
    EVQ_SLOT32(EVQ_R, 4) = 0;
    return 3 * EVQ_R;
}

/* Port of FUN_00472ae0 (96 B) — reset the queue: indices, notify flag, cooldown, then 0x472b40 for each slot. */
int rl_472ae0(void)
{
    int r = 0;
    EVQ_W = 0;
    EVQ_R = 0;
    g_rl_565484 = 0;
    g_rl_57c954 = 0;
    for (int i = 0; i < 50; i++) r = rl_472b40(i);
    return r;
}

/* Port of FUN_00472ad0 (16 B) — the init's trampoline. */
int rl_472ad0(void) { return rl_472ae0(); }

/* Port of FUN_00472bc5 (560 B) — post an event: stage it in DAT_00553510 (always), then copy it into the ring at
 * the write index when the audience a1 is everyone (0) or the local player (every branch of retail's filter on
 * the target's realm byte +5 reduces to that; its MP relay loop is dead code: the gate local is 0). */
int rl_472bc5(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7)
{
    EVQ_STAGE32(0) = a1;
    EVQ_STAGE32(4) = a0;
    EVQ_STAGE32(8) = a2;
    EVQ_STAGE32(0xc) = a3;
    g_rl_553510[0x11] = (uint8_t)a4;
    g_rl_553510[0x12] = (uint8_t)a5;
    g_rl_553510[0x13] = (uint8_t)a6;
    EVQ_STAGE32(0x14) = a7;
    int pub;
    if (a0 != 0 && PL8(a0, 0x57bf05u) != 0) {
        pub = LOCAL == a1;
        if (a1 == 0) pub = 1;
    } else
        pub = a1 == 0 || LOCAL == a1;
    if (pub) {
        for (int i = 0; i < 24; i++)   /* 0x401136's byte copy, inlined: slot 5 straddles the ring's two blocks */
            EVQ_SLOT8(EVQ_W, i) = g_rl_553510[i];
        if (++EVQ_W >= 50) EVQ_W = 0;
        g_rl_565484 = 1;
    }
    return 0;
}

/* ================================================================ the cell sweeps */

/* Port of FUN_0046c5b8 (258 B) — zero byte `a0` of every cell. Retail unrolls 8 cells a pass: the byte cursor
 * DAT_0053e8b8 steps 64, the cell cursor DAT_00569570 / 78 counts the passes (8) and rows (64) — live state. */
int rl_46c5b8(int a0)
{
    unsigned f = (uint8_t)a0;
    SWEEP8 = 0;
    for (CUR_ROW = 0; CUR_ROW < 64; CUR_ROW++)
        for (CUR_COL = 0; CUR_COL < 8; CUR_COL++, SWEEP8 += 64)
            for (int k = 0; k < 8; k++) CELL8(f + SWEEP8 + 8 * k, 0x522f90u) = 0;
    return (int)f;
}

/* Port of FUN_0046c6ba (322 B) — clear bit 0x40 of every cell's +2 (the path overlay), the same unrolled sweep. */
int rl_46c6ba(void)
{
    SWEEP8 = 0;
    for (CUR_ROW = 0; CUR_ROW < 64; CUR_ROW++)
        for (CUR_COL = 0; CUR_COL < 8; CUR_COL++, SWEEP8 += 64)
            for (int k = 0; k < 8; k++) CELL8(SWEEP8 + 8 * k, 0x522f92u) &= (uint8_t)~0x40u;
    return SWEEP8 - 64;
}

/* Port of FUN_0046df51 (132 B) — clear bit 0x20 of every cell's +2 (counting the cells in DAT_0053e974), then mark
 * the whole screen dirty (0x45240a; its EAX is returned). */
int rl_46df51(void)
{
    g_cell_sweep_counter = 0;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++, g_cell_sweep_counter++) CELL8(512 * i + 8 * j, 0x522f92u) &= (uint8_t)~0x20u;
    return rl_45240a();
}

/* Port of FUN_0046dfd5 (146 B) — highlight county `a0`: a radius-1 draw box (0x46e067) around each of its cells
 * (DAT_0053e974 counts the cells), then the whole screen dirty (0x45240a). */
int rl_46dfd5(int a0)
{
    g_cell_sweep_counter = 0;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++, g_cell_sweep_counter++)
            if ((int)CELL8(512 * i + 8 * j, 0x522f97u) == a0) engine_territory_mark_draw_box(j, i, 1);
    return rl_45240a();
}

/* ================================================================ the cell edits */

/* Port of FUN_0046d9da (113 B) — clear the AI score (DAT_0053fa40: county +0x90, i16 per roster column) of the
 * roster column holding cell `a0` in its county's row (key = cell +7). EAX: the column, else 19. */
int rl_46d9da(int a0)
{
    int k = CELL8(a0, 0x522f97u);
    for (int i = 0; i < 20; i++)
        if (ROSTER(k, i) == a0) {
            CO16(k, 0x53fa40 + 2 * i) = 0;
            return i;
        }
    return 19;
}

/* Port of FUN_0046d7f4 (486 B) — set cell `a0`'s type (+0) to `a1` and re-derive its sprite (+3: the type's base,
 * the old variant (+3 − the old base) & 3, + 4 × `a2`) and render flags (+2: bit 0 on, bits 2..4 = 8 unless a
 * camp 23 / 24, bit 7 for types 15..22). Type 0 clears the cell's AI score (0x46d9da). EAX: a0. */
int rl_46d7f4(int a0, int a1, int a2)
{
    uint8_t v7 = 8, v4;
    uint8_t old = CELL8(a0, 0x522f90u);
    CELL8(a0, 0x522f90u) = (uint8_t)a1;
    if (a1 == 0) v4 = 0x50;
    else if (a1 == 23) { v4 = 0x82; v7 = 0; }
    else if (a1 == 24) { v4 = 0x86; v7 = 0; }
    else if (a1 == 1) v4 = 0x54;
    else if (a1 == 25) v4 = 0x6c;
    else if (a1 == 26) v4 = 0x70;
    else if (a1 == 27) v4 = 0x74;
    else if (a1 == 28) v4 = 0x78;
    else v4 = a1 >= 19 ? 0x68 : 0x58;
    if (a1 == 0) rl_46d9da(a0);
    uint8_t v5 = 0x50;
    if (old == 23) v5 = 0x82;
    if (old == 24) v5 = 0x82;
    uint8_t var = (uint8_t)((uint8_t)(CELL8(a0, 0x522f93u) - v5) & 3);
    CELL8(a0, 0x522f93u) = (uint8_t)(var + v4 + 4 * a2);
    CELL8(a0, 0x522f92u) |= 1;
    CELL8(a0, 0x522f92u) &= 0xe3;
    CELL8(a0, 0x522f92u) |= v7;
    CELL8(a0, 0x522f92u) &= 0x7f;
    if (a1 >= 15 && a1 <= 22) CELL8(a0, 0x522f92u) |= 0x80;
    return a0;
}

/* Port of FUN_0046da4b (426 B) — rebuild the per-county cell roster DAT_0053ea00: clear rows 1..16 (and their AI
 * scores when a0 == 1), then file every feature cell (+1 bit 0x20, county 1..17) in its county's row; a cell that
 * finds its row full is wiped back to plain land (+0 = 0, +3 = 6, +1 = 0, +2 bits 2..4 off). County 17 indexes the
 * row past the table (retail's layout: the save slots after it). */
int rl_46da4b(int a0)
{
    for (int i = 1; i < 17; i++) {
        for (int j = 0; j < 20; j++) ROSTER(i, j) = 0;
        if (a0 == 1)
            for (int k = 0; k < 20; k++) CO16(i, 0x53fa40 + 2 * k) = 0;
    }
    for (int m = 0; m < 0x8000; m += 8) {
        int k = CELL8(m, 0x522f97u);
        if (k == 0 || k > 0x11 || !(CELL8(m, 0x522f91u) & 0x20)) continue;
        int put = 0;
        for (int n = 0; n < 20; n++)
            if (ROSTER(k, n) == 0) {
                ROSTER(k, n) = m;
                put = 1;
                break;
            }
        if (!put) {
            CELL8(m, 0x522f90u) = 0;
            CELL8(m, 0x522f93u) = 6;
            CELL8(m, 0x522f91u) = 0;
            CELL8(m, 0x522f92u) &= 0xe3;
        }
    }
    return 0;
}

/* Port of FUN_0046ac22 (329 B) — stamp an `a1` × `a1` block from cell offset `a2`: per cell +2 bit 0 on, bits 2..4
 * off, then | `a3`; the sprite +3 = `a0` (+ the size's offset table DAT_004d80e0 / 0x4d80e8 / 0x4d80f8 for 2..4);
 * the type +0 = `a4`. EAX: a1. */
int rl_46ac22(int a0, int a1, int a2, int a3, int a4)
{
    int off = a2, n = 0;
    for (int r = 0; r < a1; r++) {
        for (int i = 0; i < a1; i++) {
            CELL8(off, 0x522f92u) |= 1;
            CELL8(off, 0x522f92u) &= 0xe3;
            switch (a1) {
            case 1: CELL8(off, 0x522f93u) = (uint8_t)a0; break;
            case 2: CELL8(off, 0x522f93u) = (uint8_t)(a0 + AT(uint8_t, g_rl_4d80e0, 0x4d80e0u, 0x4d80e0 + n)); break;
            case 3: CELL8(off, 0x522f93u) = (uint8_t)(a0 + AT(uint8_t, g_rl_4d80e0, 0x4d80e0u, 0x4d80e8 + n)); break;
            case 4: CELL8(off, 0x522f93u) = (uint8_t)(a0 + AT(uint8_t, g_rl_4d80e0, 0x4d80e0u, 0x4d80f8 + n)); break;
            default: break;
            }
            CELL8(off, 0x522f92u) |= (uint8_t)a3;
            CELL8(off, 0x522f90u) = (uint8_t)a4;
            off += 8;
            n++;
        }
        off += 8 * (64 - a1);
    }
    return a1;
}

/* Port of FUN_00467fd1 (336 B) — county `a0`'s capital: its 1st / 3rd / 4th untyped town cell (+1 bit 0x40) get
 * +2 bit 7; the 1st also the 2×2 keep (0x46ac22: sprite 47, flags 12, type 0). At the 4th the cell cursor
 * DAT_0057c8d0 / DAT_0056846c = its (col, row); returns the 1st's offset (0: fewer than 4). */
int rl_467fd1(int a0)
{
    int n = 0, first = 0;
    for (int r = 0; r < 64; r++)
        for (int i = 0; i < 64; i++) {
            int off = 512 * r + 8 * i;
            if ((int)CELL8(off, 0x522f97u) != a0 || !(CELL8(off, 0x522f91u) & 0x40) || CELL8(off, 0x522f90u) != 0)
                continue;
            if (n == 0) {
                first = 8 * (i + (r << 6));
                CELL8(first, 0x522f92u) |= 0x80;
                rl_46ac22(47, 2, first, 12, 0);
            }
            if (n == 2) CELL8(off, 0x522f92u) |= 0x80;
            if (n == 3) {
                g_rl_57c8d0 = i;
                g_rl_56846c = r;
                return first;
            }
            n++;
        }
    return 0;
}

/* Port of FUN_00468121 (331 B) — county `a0`'s secondary keep: its first 4 untyped castle cells (+1 bit 7) become
 * type 20 with the sprite saved (+6 = +3); the 1st gets +2 bit 7 and the cursor DAT_0057c8d0 / DAT_0056846c.
 * Returns the 1st's offset (0: fewer than 4). */
int rl_468121(int a0)
{
    int n = 0, first = 0;
    for (int r = 0; r < 64; r++)
        for (int i = 0; i < 64; i++) {
            int off = 512 * r + 8 * i;
            if ((int)CELL8(off, 0x522f97u) != a0 || CELLS8(off, 0x522f91u) >= 0 || CELL8(off, 0x522f90u) != 0)
                continue;
            if (n == 0) {
                first = 8 * (i + (r << 6));
                CELL8(first, 0x522f92u) |= 0x80;
                g_rl_57c8d0 = i;
                g_rl_56846c = r;
            }
            CELL8(off, 0x522f96u) = CELL8(off, 0x522f93u);
            CELL8(off, 0x522f90u) = 20;
            if (n == 3) return first;
            n++;
        }
    return 0;
}

/* Port of FUN_00468c41 (270 B) — county `a0`'s village cells (+1 bit 0x10): their offsets into the county's list
 * (+0x80, i32 each, in scan order), sprite saved (+6 = +3), type 0. */
int rl_468c41(int a0)
{
    int n = 0;
    for (int r = 0; r < 64; r++)
        for (int i = 0; i < 64; i++) {
            int off = 512 * r + 8 * i;
            int k = CELL8(off, 0x522f97u);
            if ((CELL8(off, 0x522f91u) & 0x10) && a0 == k) {
                CO32(k, 0x53fa30 + 4 * n) = 8 * (i + (r << 6));
                CELL8(off, 0x522f96u) = CELL8(off, 0x522f93u);
                CELL8(off, 0x522f90u) = 0;
                n++;
            }
        }
    return 0;
}

/* Port of FUN_00468e61 (457 B) — county `a0`'s special town cells (+2 bits 2..4 == 3): sprite 30 → the castle
 * (+0x2ad = 1, +0x2b0 = the cell, type 1), sprite 0 while no castle yet → the alternative (+0x2dd / +0x2e0, type 4),
 * sprite 20 → (+0x295 / +0x298, type 10); each gets +2 bit 7. The sweep runs on retail's globals: the cell cursor
 * DAT_00569570 / 78 and the offset DAT_005533cc. */
int rl_468e61(int a0)
{
    int r = 0;
    CUR_ROW = 0;
    CELLOFF = 0;
    while (CUR_ROW < 64) {
        CUR_COL = 0;
        while (CUR_COL < 64) {
            r = a0;
            if ((int)CELL8(CELLOFF, 0x522f97u) == a0) {
                r = CELLOFF;
                if ((CELL8(CELLOFF, 0x522f92u) & 0x1c) == 0xc) {
                    int v2 = CELL8(CELLOFF, 0x522f93u);
                    if (v2 == 30) {
                        CO8(a0, 0x53fc5d) = 1;
                        CO32(a0, 0x53fc60) = CELLOFF;
                        CELL8(CELLOFF, 0x522f90u) = 1;
                        CELL8(CELLOFF, 0x522f92u) |= 0x80;
                    }
                    if (v2 == 0) {
                        r = COS8(a0, 0x53fc5d);
                        if (CO8(a0, 0x53fc5d) == 0) {
                            CO8(a0, 0x53fc8d) = 1;
                            CO32(a0, 0x53fc90) = CELLOFF;
                            CELL8(CELLOFF, 0x522f90u) = 4;
                            r = CELLOFF;
                            CELL8(CELLOFF, 0x522f92u) |= 0x80;
                        }
                    }
                    if (v2 == 20) {
                        CO8(a0, 0x53fc45) = 1;
                        CO32(a0, 0x53fc48) = CELLOFF;
                        CELL8(CELLOFF, 0x522f90u) = 10;
                        r = CELLOFF;
                        CELL8(CELLOFF, 0x522f92u) |= 0x80;
                    }
                }
            }
            CUR_COL++;
            CELLOFF += 8;
        }
        CUR_ROW++;
    }
    return r;
}

/* the four orthogonal neighbours' keys DAT_00553280 / 88 / 90 / 98 (i = 0, 2, 4, 6: N, E, S, W) */
#define NBKEY(i) AT(int32_t, g_rl_553280, 0x553280u, 0x553280 + 4 * (int32_t)(i))

/* Port of FUN_0046c080 (199 B) — load the four orthogonal neighbours' county keys (+7) of the cell at DAT_005533cc
 * (cursor DAT_00569570 / 78) into DAT_00553280[0 / 2 / 4 / 6]; 0 off the grid. */
int rl_46c080(void)
{
    NBKEY(0) = CUR_ROW != 0 ? CELL8(CELLOFF, 0x522d97u) : 0;
    NBKEY(2) = CUR_COL == 63 ? 0 : CELL8(CELLOFF, 0x522f9fu);
    NBKEY(4) = CUR_ROW == 63 ? 0 : CELL8(CELLOFF, 0x523197u);
    NBKEY(6) = CUR_COL != 0 ? CELL8(CELLOFF, 0x522f8fu) : 0;
    return CELLOFF;
}

/* Port of FUN_0046c147 (407 B) — classify the neighbour `a0` (0 N, 2 E, 4 S, 6 W) of the cell at DAT_005533cc: its
 * flags (+1, 0 off the grid) less bit 1; a coast (8) whose +2 bits 2..4 are not 1 counts as 16; 16 needs them
 * == 2, else 0. */
int rl_46c147(int a0)
{
    uint8_t v2 = 0;
    int v3 = 0;
    if (a0 == 0) {
        v3 = CUR_ROW != 0 ? CELL8(CELLOFF, 0x522d91u) : 0;
        v2 = CELL8(CELLOFF, 0x522d92u) & 0x1c;
    } else if (a0 == 2) {
        v3 = CUR_COL == 63 ? 0 : CELL8(CELLOFF, 0x522f99u);
        v2 = CELL8(CELLOFF, 0x522f9au) & 0x1c;
    } else if (a0 == 4) {
        v3 = CUR_ROW == 63 ? 0 : CELL8(CELLOFF, 0x523191u);
        v2 = CELL8(CELLOFF, 0x523192u) & 0x1c;
    } else if (a0 == 6) {
        v3 = CUR_COL != 0 ? CELL8(CELLOFF, 0x522f89u) : 0;
        v2 = CELL8(CELLOFF, 0x522f8au) & 0x1c;
    }
    if (v3 != 0) {
        v3 &= 0xfd;
        if (v3 == 8 && v2 != 4) v3 = 16;
        if (v3 == 16 && v2 != 8) return 0;
    }
    return v3;
}

/* Port of FUN_0046902a (499 B) — county `a0`'s port: of its plain cells (+1 == 0) with a same-county neighbour that
 * classifies as water (0x46c147 & 0x20), the nearest (Chebyshev, 0x404f4c) to the cursor DAT_0057c8d0 / DAT_0056846c
 * (the capital); it becomes type 7, sprite 10, +1 bit 7, +2 bits 2..4 = 3 + bit 7 (+0x2c5 = 1, +0x2c8 = the cell). */
int rl_46902a(int a0)
{
    int best = 64, at = 0;
    CUR_ROW = 0;
    CELLOFF = 0;
    while (CUR_ROW < 64) {
        CUR_COL = 0;
        while (CUR_COL < 64) {
            if ((int)CELL8(CELLOFF, 0x522f97u) == a0 && CELL8(CELLOFF, 0x522f91u) == 0) {
                rl_46c080();
                for (int i = 0; i < 8; i += 2)
                    if (NBKEY(i) == a0 && (rl_46c147(i) & 0x20)) {
                        int d = rl_404f4c(g_rl_57c8d0, g_rl_56846c, CUR_COL, CUR_ROW);
                        if (d < best) {
                            best = d;
                            at = CELLOFF;
                        }
                    }
            }
            CUR_COL++;
            CELLOFF += 8;
        }
        CUR_ROW++;
    }
    if (at != 0 && CELL8(at, 0x522f91u) == 0) {
        CO8(a0, 0x53fc75) = 1;
        CO32(a0, 0x53fc78) = at;
        CELL8(at, 0x522f91u) |= 0x80;
        CELL8(at, 0x522f90u) = 7;
        CELL8(at, 0x522f92u) &= 0xe3;
        CELL8(at, 0x522f92u) |= 0xc;
        CELL8(at, 0x522f92u) |= 0x80;
        CELL8(at, 0x522f93u) = 10;
    }
    return 0;
}

/* Port of FUN_00468d4f (274 B) — every county's capital setup (1..DAT_0056d5dc): the capital (0x467fd1: +0x70, its
 * cursor cell → +0x6c / +0x6d), the villages (0x468c41), the special towns (0x468e61), the port (0x46902a, from the
 * capital's cell), the secondary keep (0x468121: +0x78, cursor → +0x74 / +0x75). EAX: the county past the last. */
int rl_468d4f(void)
{
    int i;
    for (i = 1; NCOUNTY >= i; i++) {
        CO32(i, 0x53fa20) = rl_467fd1(i);
        if (CO32(i, 0x53fa20) == 0) continue;
        CO8(i, 0x53fa1c) = (uint8_t)g_rl_57c8d0;
        CO8(i, 0x53fa1d) = (uint8_t)g_rl_56846c;
        rl_468c41(i);
        g_rl_57c8d0 = CO8(i, 0x53fa1c);
        g_rl_56846c = CO8(i, 0x53fa1d);
        rl_468e61(i);
        rl_46902a(i);
        CO32(i, 0x53fa28) = rl_468121(i);
        CO8(i, 0x53fa24) = (uint8_t)g_rl_57c8d0;
        CO8(i, 0x53fa25) = (uint8_t)g_rl_56846c;
    }
    return i;
}

/* Port of FUN_00467a36 (624 B) — seed the counties' feature cells (+1 bit 0x20, county 1..17): count them (+0x205,
 * cleared first for 1..16 — county 17 counts past the table), and type each by its rank in the county under the
 * feature-density setting DAT_0053f23c (3: 4 farms then plain; 2: 4 farms, 2 type-1, plain; 1: 6 farms, 2 type-1,
 * plain; else 8 farms then type 1): +3 = the base + (the old +3 − 80) & 3, +0 = the type, then 0x46d7f4. */
int rl_467a36(void)
{
    int cnt[32];
    for (int i = 1; i < 17; i++) CO8(i, 0x53fbb5) = 0;
    for (int j = 0; j < 32; j++) cnt[j] = 0;
    for (int k = 0; k < 64; k++)
        for (int m = 0; m < 64; m++) {
            int off = 512 * k + 8 * m;
            uint8_t fl = CELL8(off, 0x522f91u);
            int key = CELL8(off, 0x522f97u);
            uint8_t v2 = CELL8(off, 0x522f93u);
            if (key <= 0 || key > 17 || !(fl & 0x20)) continue;
            CO8(key, 0x53fbb5)++;
            int rank = cnt[key]++;
            uint8_t type, base;
            switch ((int32_t)s_slot_0053f23c) {
            case 3:
                if (rank >= 4) { type = 0; base = 80; } else { type = 20; base = 104; }
                break;
            case 2:
                if (rank < 4) { type = 20; base = 104; }
                else if (rank < 6) { type = 1; base = 84; }
                else { type = 0; base = 80; }
                break;
            case 1:
                if (rank < 6) { type = 20; base = 104; }
                else if (rank < 8) { type = 1; base = 84; }
                else { type = 0; base = 80; }
                break;
            default:
                if (rank >= 8) { type = 1; base = 84; } else { type = 20; base = 104; }
                break;
            }
            v2 = (uint8_t)((uint8_t)(v2 - 80) & 3);
            CELL8(off, 0x522f93u) = (uint8_t)(base + v2);
            CELL8(off, 0x522f90u) = type;
            rl_46d7f4(8 * (m + (k << 6)), type, 0);
        }
    return 0;
}

/* Port of FUN_00467ca6 (390 B) — every county's neighbour list (+0x5c, 16 keys, count +0x5a): for each cell of
 * county 1..17 (the sweep on DAT_00569570 / 78 / DAT_005533cc), each orthogonal neighbour of another county 1..17
 * (0x46c080) not yet listed is appended. */
int rl_467ca6(void)
{
    CUR_ROW = 0;
    CELLOFF = 0;
    while (CUR_ROW < 64) {
        CUR_COL = 0;
        while (CUR_COL < 64) {
            int v1 = CELL8(CELLOFF, 0x522f97u);
            if (v1 != 0 && v1 <= 0x11) {
                rl_46c080();
                for (int i = 0; i < 8; i += 2) {
                    int v2 = NBKEY(i);
                    if (v2 <= 0 || v2 > 17 || v2 == v1) continue;
                    for (int j = 0; j < 16; j++) {
                        if ((int)CO8(v1, 0x53fa0c + j) == v2) break;
                        if (CO8(v1, 0x53fa0c + j) == 0) {
                            CO8(v1, 0x53fa0c + j) = (uint8_t)v2;
                            CO8(v1, 0x53fa0a)++;
                            break;
                        }
                    }
                }
            }
            CUR_COL++;
            CELLOFF += 8;
        }
        CUR_ROW++;
    }
    return 0;
}

/* Port of FUN_004684c6 (474 B) — county `a0`'s four village cells (+0x80): the first `a1` grow a level (type 19 →
 * 18, 18 → 17, else 16; sprite 60 / 60 / 59; +2 bits 2..4 = 3 + bit 7; +1 bit 0x10), the rest revert (sprite from
 * +6, +2 bits 2..4 off (+ bit 3 when +1 bit 1 was set), +2 bit 7 off, +1 bit 0x10 off, type 0). */
int rl_4684c6(int a0, int a1)
{
    int off = 0;
    for (int i = 0; i < 4; i++) {
        off = CO32(a0, 0x53fa30 + 4 * i);
        uint8_t fl = CELL8(off, 0x522f91u);
        if (i < a1) {
            uint8_t t = CELL8(off, 0x522f90u);
            if (t == 19) { CELL8(off, 0x522f90u) = 18; CELL8(off, 0x522f93u) = 60; }
            else if (t == 18) { CELL8(off, 0x522f90u) = 17; CELL8(off, 0x522f93u) = 60; }
            else { CELL8(off, 0x522f90u) = 16; CELL8(off, 0x522f93u) = 59; }
            CELL8(off, 0x522f92u) &= 0xe3;
            CELL8(off, 0x522f92u) |= 0xc;
            CELL8(off, 0x522f92u) |= 0x80;
            CELL8(off, 0x522f91u) |= 0x10;
        } else {
            CELL8(off, 0x522f93u) = CELL8(off, 0x522f96u);
            CELL8(off, 0x522f92u) &= 0xe3;
            CELL8(off, 0x522f91u) &= (uint8_t)~0x10u;
            if (fl & 2) CELL8(off, 0x522f92u) |= 8;
            CELL8(off, 0x522f92u) &= 0x7f;
            CELL8(off, 0x522f90u) = 0;
        }
    }
    return off;
}

/* ================================================================ the terrain tiles */

/* Port of FUN_0046de94 (189 B) — is the tile (col `a0`, row `a1`) of the 129 × 65 grid DAT_0055cea0 open ground: a
 * cell whose +1 lacks bit 2, or the plain special tile 0xfff0006 (0 off the grid). */
int rl_46de94(int a0, int a1)
{
    if (a1 < 0 || a0 < 0 || a1 >= 129 || a0 >= 65) return 0;
    int32_t t = TILE(a1, a0);
    if (t < TILE_SPECIAL) {
        if (!(CELL8(t, 0x522f91u) & 4)) return 1;
    } else if (t == TILE_SPECIAL + 6)
        return 1;
    return 0;
}

/* the 8-neighbour mask DAT_00543fe0[8] (engine_bf_globals names only its first dword: exact under rhost) */
#define NBMASK(i) AT(int32_t, &g_bf_543fe0, 0x543fe0u, 0x543fe0 + 4 * (int32_t)(i))

/* Port of FUN_0046de05 (143 B) — the 8-neighbour mask DAT_00543fe0[8] of tile (row `a0`, col `a1`): 0x46de94 at the
 * offsets of DAT_004d6be8 (odd rows) / DAT_004d6c28 (even rows), (dx, dy) i32 pairs. */
int rl_46de05(int a0, int a1)
{
    int r = 0;
    for (int i = 0; i < 8; i++) {
        int dx, dy;
        if (a0 & 1) {
            dx = AT(int32_t, g_rl_4d6be8, 0x4d6be8u, 0x4d6be8 + 8 * i);
            dy = AT(int32_t, g_rl_4d6be8, 0x4d6be8u, 0x4d6bec + 8 * i);
        } else {
            dx = AT(int32_t, g_rl_4d6be8, 0x4d6be8u, 0x4d6c28 + 8 * i);
            dy = AT(int32_t, g_rl_4d6be8, 0x4d6be8u, 0x4d6c2c + 8 * i);
        }
        r = rl_46de94(a1 + dx, a0 + dy);
        NBMASK(i) = r;
    }
    return r;
}

/* Port of FUN_0046c2de (302 B) — the first of the `a1` 12-B feature rules at `a0` whose 8 neighbour conditions (2:
 * any, else set ⇔ the mask DAT_00543fe0 bit) all hold: DAT_0057c8a4 = its +8 (the sprite base), DAT_0053e9c0 = +9,
 * its round-robin variant +11 advances (wrapping at +10) into DAT_00568200; returns its index + 1 (0: none). The
 * rule tables are retail .data (the variants are state). */
int rl_46c2de(int a0, int a1)
{
    uint8_t *p = (uint8_t *)(uintptr_t)(uint32_t)a0;
    int i;
    for (i = 0;; i++) {
        if (i >= a1) return 0;
        int j;
        for (j = 0; j < 8; j++) {
            int m = NBMASK(j);
            if (!(p[j] == 2 || (p[j] != 0 && m != 0) || (p[j] == 0 && m == 0))) break;
        }
        if (j >= 8) break;
        p += 12;
    }
    g_bf_57c8a4 = p[8];
    g_rl_53e9c0 = p[9];
    if (p[10] <= (unsigned)++p[11]) p[11] = 0;
    g_bf_568200 = p[11];
    return i + 1;
}

/* Port of FUN_0046dbf5 (319 B) — auto-tile the terrain: every tile that is a feature cell (+1 bit 2) or a special
 * tile other than 0xfff0006 gets its neighbour mask (0x46de05) matched against the 48 rules DAT_004d7310 (0x46c2de;
 * no match keeps the last rule's outputs); rule sprite 22 draws its variant from the RNG (0x404b2c, & 7). The cell's
 * +3 (or the special tile) = sprite + variant. DAT_0053e974 counts the tiles. */
int rl_46dbf5(void)
{
    g_cell_sweep_counter = 0;
    for (int r = 0; r < 129; r++)
        for (int i = 0; i < 65; i++, g_cell_sweep_counter++) {
            int32_t t = TILE(r, i);
            if (t < TILE_SPECIAL) {
                if (!(CELL8(t, 0x522f91u) & 4)) continue;
            } else if (t == TILE_SPECIAL + 6)
                continue;
            rl_46de05(r, i);
            rl_46c2de((int)(uintptr_t)g_rl_4d7310, 48);
            if (g_bf_57c8a4 == 22) {
                rl_404b2c();
                g_bf_568200 = g_bf_5c9a84 & 7;
            }
            if (t < TILE_SPECIAL)
                CELL8(t, 0x522f93u) = (uint8_t)(g_bf_57c8a4 + g_bf_568200);
            else
                TILE(r, i) = g_bf_57c8a4 + g_bf_568200 + TILE_SPECIAL;
        }
    return 0;
}

/* Port of FUN_0046dd34 (209 B) — re-roll every plain special tile 0xfff0006: 0x404b2c, DAT_00568200 = rng & 7, the
 * tile = 0xfff0006 + (rng & 15). DAT_0053e974 counts the tiles. */
int rl_46dd34(void)
{
    g_cell_sweep_counter = 0;
    for (int r = 0; r < 129; r++)
        for (int i = 0; i < 65; i++, g_cell_sweep_counter++)
            if (TILE(r, i) == TILE_SPECIAL + 6) {
                rl_404b2c();
                g_bf_568200 = g_bf_5c9a84 & 7;
                TILE(r, i) = (g_bf_5c9a84 & 0xf) + TILE_SPECIAL + 6;
            }
    return 0;
}

/* ================================================================ the map load */

extern uint8_t g_rl_4d6af8[240];   /* DAT_004d6af8 (rl_io_globals.h): the minimap files "map01.pl8" …, 16 B each */
#define STR_L2MAPS   ((int)(uintptr_t)&AT(uint8_t, g_rl_4d6be8, 0x4d6be8u, 0x4d6c68))   /* "l2_maps.dat" */
#define STR_L2MAPS_0 ((int)(uintptr_t)&AT(uint8_t, g_rl_4d6be8, 0x4d6be8u, 0x4d6c74))   /* "l2_maps.dat" (2nd copy) */
#define STR_OKMAP    ((int)(uintptr_t)&AT(uint8_t, g_rl_4d6be8, 0x4d6be8u, 0x4d6c80))   /* "OK :MAP scenario " */
#define BLOB(i)      (((const uint8_t *)g_gfx_current_blob)[i])                          /* DAT_004eabec[] */

/* Port of FUN_00467770 (525 B) — load map `a0` of l2_maps.dat (0x80c1 B a record) into the load buffer DAT_004eabec
 * (0x4af8d9) and unpack its six 64 × 64 planes into the cells: +1 / +2 / +3 / +4 / +7 (the county; the largest ≤ 16
 * is the county count DAT_0056d5dc); a cell with a plane-5 id is a category entry (+1 bit 0x40: 0x429153) or a
 * roster entry (bit 7: 0x49bbe8). Resets the rotation DAT_00522f7c, DAT_0053e8a4, the buckets (0x4290d0) and the
 * roster (0x49bc1d); DAT_0053e974 counts the cells. */
int rl_467770(int a0)
{
    g_mv_rotation = 0;
    rl_4af8d9(STR_L2MAPS, (int)(uintptr_t)g_gfx_current_blob, 0x80c1, 0x80c1 * a0);
    g_cell_sweep_counter = 0;
    s_slot_0056d5dc = 0;
    s_slot_0053e8a4 = 0;
    rl_4290d0();
    rl_49bc1d();
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++, g_cell_sweep_counter++) {
            int o = g_cell_sweep_counter;
            uint8_t v9 = BLOB(o), v6 = BLOB(o + 0x1000), v5 = BLOB(o + 0x2000), v2 = BLOB(o + 0x3000);
            uint8_t v4 = BLOB(o + 0x4000), v3 = BLOB(o + 0x5000);
            if (v3 <= 0x10 && (int32_t)v3 > NCOUNTY) s_slot_0056d5dc = v3;
            int off = 512 * i + 8 * j;
            CELL8(off, 0x522f91u) = v9;
            CELL8(off, 0x522f92u) = v6;
            CELL8(off, 0x522f93u) = v5;
            CELL8(off, 0x522f94u) = v2;
            CELL8(off, 0x522f97u) = v3;
            if (v4 == 0) continue;
            if (v9 & 0x40) rl_429153(v3, v4);
            else if (v9 & 0x80) rl_49bbe8(v3, v4);
        }
    return 1;
}

/* Port of FUN_0046797d (185 B) — load map `a0`'s terrain plane (l2_maps.dat record +0x6000) into the 129 × 65 tile
 * grid DAT_0055cea0 as special tiles (0xfff0000 + the byte); DAT_0053e974 counts the tiles. EAX: the last tile. */
int rl_46797d(int a0)
{
    int r = rl_4af8d9(STR_L2MAPS_0, (int)(uintptr_t)g_gfx_current_blob, 0x80c1, 0x80c1 * a0);
    g_cell_sweep_counter = 0;
    for (int v = 0; v < 129; v++)
        for (int i = 0; i < 65; i++, g_cell_sweep_counter++) {
            r = BLOB(g_cell_sweep_counter + 0x6000) + TILE_SPECIAL;
            TILE(v, i) = r;
        }
    return r;
}

/* Port of FUN_0046a037 (202 B) — load map `a0`'s two minimap grids (16 KB each: the region grid DAT_0052aff0 and
 * DAT_00569590) from map0N.pl8 (N = a0 / 4 + 1; the file's offset table at +12 + 80 (a0 & 3) / + 16 (5 (a0 & 3) + 1))
 * when the file exists (0x4af767), then log "OK :MAP scenario " (0x4afab9: its EAX). */
int rl_46a037(int a0)
{
    int name = (int)(uintptr_t)&g_rl_4d6af8[16 * (a0 / 4)];
    int r = rl_4af767(name);
    if (r == 0) return 0;
    int32_t at = 0;
    rl_4af8d9(name, (int)(uintptr_t)&at, 4, 80 * (a0 & 3) + 12);
    rl_4af8d9(name, (int)(uintptr_t)g_minimap_region_grid, 0x4000, at);
    rl_4af8d9(name, (int)(uintptr_t)&at, 4, 16 * (5 * (a0 & 3) + 1) + 12);
    rl_4af8d9(name, (int)(uintptr_t)g_rl_569590, 0x4000, at);
    return rl_4afab9(STR_OKMAP, name, 0);
}

/* Port of FUN_004676e0 (144 B) — the new-game overworld build: clear cell bytes 1, 2, 4, 0, 5, 3 (0x46c5b8), load
 * the map DAT_0053f034 (0x467770), clear the render flags (0x46df51), auto-tile and re-roll the terrain (0x46dbf5 /
 * 0x46dd34), the minimap grids (0x46a037), the county adjacency (0x467ca6), the capitals (0x468d4f), the feature
 * seeds (0x467a36), the cell roster with the AI scores (0x46da4b(1)), the bucket digest (0x4291b3). */
int rl_4676e0(void)
{
    rl_46c5b8(1);
    rl_46c5b8(2);
    rl_46c5b8(4);
    rl_46c5b8(0);
    rl_46c5b8(5);
    rl_46c5b8(3);
    rl_467770((int)s_slot_0053f034);
    rl_46df51();
    rl_46dbf5();
    rl_46dd34();
    rl_46a037((int)s_slot_0053f034);
    rl_467ca6();
    rl_468d4f();
    rl_467a36();
    rl_46da4b(1);
    return rl_4291b3();
}

/* ================================================================ the map queries */

/* Port of FUN_0046cfbd (371 B) — is there, in the (2 `a2` + 1)-square box around (`a0`, `a1`) clamped to the grid,
 * a cell with no army (+5 == 0) and +1 bit 0? The box scan's counters are the cell cursor DAT_00569570 / 78 (a hit
 * leaves it on the cell). Route (a): engine_map_query's box scan runs on retail's grid and the cursor mirror. */
int rl_46cfbd(int a0, int a1, int a2) { return engine_map_query_box_any_rawflag_bit0(a0, a1, a2, NULL, NULL); }

/* Port of FUN_0046733c (124 B) — the ring distance 1..5 from (`a0`, `a1`) to the nearest free feature cell: per d
 * the +1-bit-0 box of radius d + 1 (0x46cfbd), then the +1 ⊆ {bit 1} box of radius d (0x46d130); 0: none. The
 * cursor DAT_00569570 / 78 is left on the hit. Route (a): engine_map_query_nearest_feature_ring. */
int rl_46733c(int a0, int a1) { return engine_map_query_nearest_feature_ring(a0, a1); }

/* Port of FUN_0046d2a3 (311 B) — is the `a2` × `a2` block at (`a0`, `a1`) inside the grid with every cell free (+5
 * == 0, +1 == 0)? The scan counts in the cursor DAT_00569570 / 78. Route (a): engine_map_query_region_clear. */
int rl_46d2a3(int a0, int a1, int a2) { return engine_map_query_region_clear(a0, a1, a2); }

/* Port of FUN_0046d5fe (502 B) — the terrain info of the cell under the cursor DAT_0056795c: its flags (+1 →
 * DAT_005440b0), county (+7 → DAT_005681e0, 32 → 0) and its owner (county +5 → DAT_005681d0), type (+0 → DAT_0057cb2c);
 * a 2-cell feature (+1 bit 7 and type ≥ 21, or bit 6) is resolved to its anchor cell by +4's low nibble; from the
 * anchor: flags, the army (+5 → DAT_0052afbc), type, sprite (+3 → DAT_00553f78); flags 0 for a keep site (bit 7 +
 * type 20) or an empty village (bit 4 + type 0); DAT_005651bc = a coast (bit 3) whose +2 bits 2..4 == 1. */
int rl_46d5fe(void)
{
    int c = g_rl_56795c;
    g_mv_ti_flags = CELL8(c, 0x522f91u);
    g_mv_ti_head = CELL8(c, 0x522f97u);
    g_mv_ti_owner = CO8(g_mv_ti_head, 0x53f9b5);
    g_mv_ti_type = CELL8(c, 0x522f90u);
    uint8_t span;
    if (!(g_mv_ti_flags & 0x80u))
        span = (g_mv_ti_flags & 0x40) ? 2 : 1;
    else
        span = g_mv_ti_type < 21 ? 1 : 2;
    if (g_mv_ti_head == 32) g_mv_ti_head = 0;
    int a;
    if (span <= 1)
        a = c;
    else {
        int n = CELL8(c, 0x522f94u) & 0xf;
        a = c - 8 * (n % span) - ((n / span) << 9);
    }
    g_mv_ti_flags = CELL8(a, 0x522f91u);
    g_mv_ti_county = CELL8(a, 0x522f95u);
    g_mv_ti_type = CELL8(a, 0x522f90u);
    g_rl_553f78 = CELL8(a, 0x522f93u);
    g_rl_5651bc = 0;
    if ((g_mv_ti_flags & 0x80u) && g_mv_ti_type == 20) g_mv_ti_flags = 0;
    if ((g_mv_ti_flags & 0x10) && g_mv_ti_type == 0) g_mv_ti_flags = 0;
    if (g_mv_ti_flags & 8) {
        int r = CELL8(a, 0x522f92u) & 0x1c;
        if (r == 4) g_rl_5651bc = 1;
        return r;
    }
    return a;
}

/* ================================================================ the AI's army queries */

/* Port of FUN_004673b8 (378 B) — the nearest (Chebyshev, 0x404f4c) live idle army (state +8 == 1, +0x198 == 0) of
 * another realm (+0, signed) to army `a0`; returns the distance (1000: none), the army in DAT_00553f8c (the scan's
 * counter). */
int rl_4673b8(int a0)
{
    int best = 1000, who = 0;
    for (g_combat_active_terr = 1; g_combat_active_terr < 151; g_combat_active_terr++) {
        int k = g_combat_active_terr;
        if (TERS8(k, 0x52f0b0) == 0 || TERS8(a0, 0x52f0b0) == TERS8(k, 0x52f0b0) || TER8(k, 0x52f0b8) != 1 ||
            TER8(k, 0x52f248) != 0)
            continue;
        int d = rl_404f4c(TERS8(a0, 0x52f0ba), TERS8(a0, 0x52f0bb), TERS8(k, 0x52f0ba), TERS8(k, 0x52f0bb));
        if (best > d) {
            best = d;
            who = g_combat_active_terr;
        }
    }
    g_combat_active_terr = who;
    return best;
}

/* Port of FUN_00467532 (426 B) — as 0x4673b8, but for idle armies whose realm army `a0`'s realm should engage
 * (0x4a16f7) standing in county `a1` (+0x10). */
int rl_467532(int a0, int a1)
{
    int best = 1000, who = 0;
    for (g_combat_active_terr = 1; g_combat_active_terr < 151; g_combat_active_terr++) {
        int k = g_combat_active_terr;
        if (TERS8(k, 0x52f0b0) == 0 || TER8(k, 0x52f0b8) != 1 ||
            engine_diplomacy_should_engage(TERS8(a0, 0x52f0b0), TERS8(g_combat_active_terr, 0x52f0b0)) == 0)
            continue;
        k = g_combat_active_terr;
        if (TER8(k, 0x52f248) != 0 || (int)TER8(k, 0x52f0c0) != a1) continue;
        int d = rl_404f4c(TERS8(a0, 0x52f0ba), TERS8(a0, 0x52f0bb), TERS8(k, 0x52f0ba), TERS8(k, 0x52f0bb));
        if (best > d) {
            best = d;
            who = g_combat_active_terr;
        }
    }
    g_combat_active_terr = who;
    return best;
}

/* Port of FUN_00467e2c (132 B) — is county `a1` in county `a0`'s neighbour list (+0x5c, 0-terminated, 16)? */
int rl_467e2c(int a0, int a1)
{
    for (int i = 0; i < 16; i++) {
        if ((int)CO8(a0, 0x53fa0c + i) == a1) return 1;
        if (CO8(a0, 0x53fa0c + i) == 0) return 0;
    }
    return 0;
}

/* Port of FUN_00467f2e (163 B) — does county `a0` border a county owned (+5) by another realm (not 0)? */
int rl_467f2e(int a0)
{
    for (int i = 0; i < 16; i++) {
        int v = CO8(a0, 0x53fa0c + i);
        if (v == 0) return 0;
        if (CO8(v, 0x53f9b5) != 0 && CO8(a0, 0x53f9b5) != CO8(v, 0x53f9b5)) return 1;
    }
    return 0;
}

/* Port of FUN_0046841c (170 B) — evict the armies standing on county `a0`'s 2×2 keep (from +0x78): each arrives
 * (0x467196) and gets +0x14e = 1. EAX: 496. */
int rl_46841c(int a0)
{
    int off = CO32(a0, 0x53fa28);
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            int a = CELL8(off, 0x522f95u);
            if (a != 0) {
                engine_army_move_arrival(a);
                TER8(a, 0x52f1fe) = 1;
            }
            off += 8;
        }
        off += 496;
    }
    return 496;
}

/* ================================================================ the county cell cycles */

/* Port of FUN_0046942c (147 B) — clear county `a0`'s camp cells (roster cells of type 23 / 24) back to type 0. */
int rl_46942c(int a0)
{
    int r = 0;
    for (int i = 0; i < 20; i++) {
        int c = ROSTER(a0, i);
        r = c;
        if (c == 0) continue;
        int t = CELL8(c, 0x522f90u);
        if (t == 23) r = rl_46d7f4(c, 0, 0);
        if (t == 24) r = rl_46d7f4(c, 0, 0);
    }
    return r;
}

/* the county's round-robin roster cursors (+0x15a / +0x15b), wrapping at its feature count (+0x205) */
static int terr_cursor_step(int c, uint32_t va)
{
    uint8_t v = ++CO8(c, va);
    if (CO8(c, 0x53fbb5) <= v) CO8(c, va) = 0;
    return CO8(c, va);
}

/* Port of FUN_0046958f (203 B) — advance county `a0`'s roster cursor (+0x15a) to the next type-1 cell (≤ 20 steps)
 * and make it type 19 (0x46d7f4: its EAX). */
int rl_46958f(int a0)
{
    int r = 0;
    for (int i = 0; i < 20; i++) {
        int c = ROSTER(a0, terr_cursor_step(a0, 0x53fb0a));
        r = c;
        if (c != 0 && CELL8(c, 0x522f90u) == 1) return rl_46d7f4(c, 19, 0);
    }
    return r;
}

/* Port of FUN_0046965a (371 B) — advance county `a0`'s roster cursor (+0x15a) to the next building (type 2..14, ≤ 20
 * steps): it becomes type 19; the county's +0x244 loses 1 / +0x201's share (0x404dc1 / 0x404d6b) to +0x234; +0x206
 * drops with +0x201 when it is not below it; +0x201 − 1. */
int rl_46965a(int a0)
{
    for (int i = 0; i < 20; i++) {
        int c = ROSTER(a0, terr_cursor_step(a0, 0x53fb0a));
        if (c == 0 || CELL8(c, 0x522f90u) < 2 || CELL8(c, 0x522f90u) > 14) continue;
        rl_46d7f4(c, 19, 0);
        int pct = rl_404dc1(1, CO8(a0, 0x53fbb1));
        int v = rl_404d6b(CO32(a0, 0x53fbf4), pct);
        CO32(a0, 0x53fbf4) -= v;
        CO32(a0, 0x53fbe4) += v;
        if (CO8(a0, 0x53fbb6) >= CO8(a0, 0x53fbb1)) CO8(a0, 0x53fbb6)--;
        CO8(a0, 0x53fbb1)--;
        return 0;
    }
    return 0;
}

/* Port of FUN_0046921d (138 B) — re-census county `a0` (0x469b8d); with no free land left (+0x200 == 0) convert one
 * type-1 cell (+0x1ff: 0x46958f) or else demolish a building (+0x201: 0x46965a), and census again. */
int rl_46921d(int a0)
{
    engine_territory_census_army_terrain(a0);
    if (CO8(a0, 0x53fbb0) != 0) return a0 << 8;
    if (CO8(a0, 0x53fbaf) != 0) rl_46958f(a0);
    else if (CO8(a0, 0x53fbb1) != 0) rl_46965a(a0);
    engine_territory_census_army_terrain(a0);
    return 0;
}

/* Port of FUN_004697cd (192 B) — demolish county `a0`'s band `a1` (2: buildings 2..14, 19: 19..22) to type 1. */
int rl_4697cd(int a0, int a1)
{
    int r = 0;
    for (int i = 0; i < 20; i++) {
        int c = ROSTER(a0, i);
        r = c;
        if (c == 0) continue;
        int t = CELL8(c, 0x522f90u);
        if (a1 == 2) {
            if (t >= 2 && t <= 14) r = rl_46d7f4(c, 1, 0);
        } else if (a1 == 19 && t >= 19 && t <= 22)
            r = rl_46d7f4(c, 1, 0);
    }
    return r;
}

/* Port of FUN_0046988d (274 B) — county `a0`: turn up to `a2` type-1 cells into type `a1`; of band `a1` (2: 2..14,
 * 19: 19..22) keep the first `a2` left over and demolish the rest to type 1. */
int rl_46988d(int a0, int a1, int a2)
{
    int r = 0;
    for (int i = 0; i < 20; i++) {
        int c = ROSTER(a0, i);
        r = c;
        if (c == 0) continue;
        int t = CELL8(c, 0x522f90u);
        if (t == 1 && a2 > 0) {
            r = rl_46d7f4(c, a1, 0);
            a2--;
            continue;
        }
        int band = (a1 == 2 && t >= 2 && t <= 14) || (a1 == 19 && t >= 19 && t <= 22);
        if (!band) continue;
        if (a2 <= 0)
            r = rl_46d7f4(c, 1, 0);
        else
            a2--;
    }
    return r;
}

/* Port of FUN_00469a9c (181 B) — advance county `a0`'s second roster cursor (+0x15b) to its next filled column (≤ 20
 * steps) and set that cell to type `a1` (0x46d7f4: its EAX). */
int rl_469a9c(int a0, int a1)
{
    int r = 0;
    for (int i = 0; i < 20; i++) {
        r = ROSTER(a0, terr_cursor_step(a0, 0x53fb0b));
        if (r != 0) return rl_46d7f4(r, a1, 0);
    }
    return r;
}

/* Port of FUN_00469d21 (237 B) — retype county `a0`'s feature cells (+1 bit 0x20) whose type is in `a3`..`a4` to
 * (`a1`, variant `a2`) (0x46d7f4); for the band from 2 with the county's +0x1a7 set, all but the first become 2. */
int rl_469d21(int a0, int a1, int a2, int a3, int a4)
{
    int r = 0, seen = 0;
    for (int i = 0; i < 0x8000; i += 8) {
        r = CELL8(i, 0x522f97u);
        if (r != a0) continue;
        r = CELL8(i, 0x522f91u);
        if (!(r & 0x20)) continue;
        int t = CELL8(i, 0x522f90u);
        r = t;
        if (t < a3 || t > a4) continue;
        if (a3 == 2 && CO8(a0, 0x53fb57) != 0 && seen != 0)
            r = rl_46d7f4(i, 2, 0);
        else
            r = rl_46d7f4(i, a1, a2);
        seen = 1;
    }
    return r;
}

/* ================================================================ the entity placer */

/* Port of FUN_0046e1b0 (792 B) — place an army of kind `a0` and realm `a3` on cell (`a1`, `a2`): inside the grid, no
 * army there (+5) and +1 free of bits 2..7; the first free record 1..149 (+0 == 0, the scan's counter is the current
 * slot DAT_0052afe0) is cleared (0x46ead0) and stamped: realm, kind, (col, row) ×3 (+0xa / +0x14 / +0x16), the cell
 * (+0xc), the pixel position (+0x12 / +0x13 = 16 × col / row, bytes), state 1 (+9), +0x14b bit 0; the cell gets the
 * army (+5) and +2 bit 0. Returns 1 (0: refused / full). */
int rl_46e1b0(int a0, int a1, int a2, int a3)
{
    if (a1 < 0 || a1 >= 64 || a2 < 0 || a2 >= 64) return 0;
    int off = 8 * (a1 + (a2 << 6));
    if (CELL8(off, 0x522f95u) != 0) return 0;
    if (CELL8(512 * a2 + 8 * a1, 0x522f91u) & 0xfc) return 0;
    int s;
    for (s = 1; s < 150; s++) {
        engine_territory_set_current_slot(s);
        if (TERS8(s, 0x52f0b0) != 0) continue;
        rl_46ead0((int)(uintptr_t)TERREC(s));
        TER8(s, 0x52f0b0) = (uint8_t)a3;
        TER8(s, 0x52f0b8) = (uint8_t)a0;
        TER8(s, 0x52f0ba) = (uint8_t)a1;
        TER8(s, 0x52f0c4) = TER8(s, 0x52f0ba);
        TER8(s, 0x52f0c6) = TER8(s, 0x52f0c4);
        TER8(s, 0x52f0bb) = (uint8_t)a2;
        TER8(s, 0x52f0c5) = TER8(s, 0x52f0bb);
        TER8(s, 0x52f0c7) = TER8(s, 0x52f0c5);
        TER32(s, 0x52f0bc) = off;
        TER8(s, 0x52f0c2) = (uint8_t)(16 * a1);
        TER8(s, 0x52f0c3) = (uint8_t)(16 * a2);
        TER8(s, 0x52f0b9) = 1;
        TER8(s, 0x52f1fb) |= 1;
        CELL8(off, 0x522f95u) = (uint8_t)s;
        CELL8(off, 0x522f92u) |= 1;
        return 1;
    }
    engine_territory_set_current_slot(s);
    return 0;
}

/* ================================================================ the strategic path field */
/* the two cost fields (the local player's DAT_00500c30, everyone else's DAT_004f0380: 64 × 64 i16), the move-cost
 * grid DAT_004f4080 (cell 0; its neighbours from DAT_004f3ffe), the per-slot waypoint lists DAT_00553550 (300 B) and
 * counts DAT_0053f070. Route (a) for the flood / trace / copy / clear: engine_path_field runs on retail's storage
 * (bound) and its bookkeeping globals are the mirrors g_pf_*. */
#define FIELD_A(i)   AT(int16_t, g_rl_500c2e, 0x500c2eu, 0x500c30 + 2 * (int32_t)(i))
#define MOVECOST(i)  AT(int16_t, g_rl_4f407e, 0x4f407eu, 0x4f4080 + 2 * (int32_t)(i))

/* Port of FUN_0046f700 (2115 B) — flood slot `a0`'s cost field from (`a1`, `a2`) over the move-cost grid (`a3`:
 * every step dearer than 1 costs 100), a 1024-entry ring queue; the bookkeeping DAT_00504020 / 24 / 0x4f4070 /
 * 0x500c20 / 0x4f2768. Route (a): engine_path_field_fill. EAX: the queue head (== the tail) at exit. */
int rl_46f700(int a0, int a1, int a2, int a3)
{
    engine_path_field_fill(a0, a1, a2, a3);
    return g_pf_head;
}

/* Port of FUN_004701ac (1228 B) — trace slot `a0`'s cost field down from (`a1`, `a2`) to the source into the slot's
 * waypoint list (cleared first: 0x4708d0; the cursor DAT_00507230 / 34), N first, diagonals unless the cell forbids
 * them (cost == 1; a stall retries all 8). 1: reached, 0: stuck. Route (a): engine_path_field_trace. */
int rl_4701ac(int a0, int a1, int a2) { return engine_path_field_trace(a0, a1, a2); }

/* Port of FUN_004707be (274 B) — copy slot `a0`'s waypoints (count DAT_0053f070, list DAT_00553550) into army `a1`'s
 * record (+0x1c count, +0x1d 150 (col, row) pairs, zero past the count). Route (a): engine_path_field_copy_record. */
int rl_4707be(int a0, int a1)
{
    engine_path_field_copy_record(a0, a1);
    return 0;
}

/* Port of FUN_004708d0 (142 B) — clear slot `a0`'s waypoint count and list. Route (a): engine_path_field_clear_slot.
 * EAX: 149. */
int rl_4708d0(int a0)
{
    engine_path_field_clear_slot(a0);
    return 149;
}

/* Port of FUN_0046f68e (114 B) — the path overlay reset for a move from (`a2`, `a3`): clear the cost field of
 * player `a0`'s kind (the local player's DAT_00500c30, else DAT_004f0380; 0x4b3e51), set the flood source DAT_00504020
 * / 24 / 0x4f4070, clear the cells' +2 bit 6 (0x46c6ba) and the LOCAL player's waypoint slot (0x4708d0). */
int rl_46f68e(int a0, int a1, int a2, int a3)
{
    (void)a1;
    if (LOCAL == a0)
        rl_4b3e51((int)(uintptr_t)&FIELD_A(0), 0x2000);
    else
        rl_4b3e51((int)(uintptr_t)g_rl_4f0380, 0x2000);
    g_pf_src_col = a2;
    g_pf_src_row = a3;
    g_pf_src_cell = a2 + (a3 << 6);
    rl_46c6ba();
    return rl_4708d0(LOCAL);
}

/* Port of FUN_00470678 (326 B) — the smallest non-zero cost among cell `a0`'s 8 neighbours in the local player's
 * field DAT_00500c30, starting from the cell's own (i16, signed). */
int rl_470678(int a0)
{
    static const int8_t k_d[8] = { -64, -63, 1, 65, 64, 63, -1, -65 };
    int v = FIELD_A(a0);
    for (int i = 0; i < 8; i++) {
        int n = FIELD_A(a0 + k_d[i]);
        if (n != 0 && v > n) v = n;
    }
    return v;
}

/* Port of FUN_0046ff43 (617 B) — rebuild the strategic move-cost grid DAT_004f4080 from the cells: water (+1 bits 2..3)
 * and county ≥ 17 impassable (0); an army's cell (+1 bit 0) 1; features (bit 5) 3, buildings (type 2..22) 6; towns
 * (bit 6) 100; villages (bit 4) 100 when type 16 else 0; plain land 3; castles (bit 7) by type (20: 3; 3 / 6 / 9 /
 * 12: 0; else 100). EAX: the last index. */
int rl_46ff43(void)
{
    int idx = 0, off = 0;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 64; j++, off += 8, idx++) {
            uint8_t fl = CELL8(off, 0x522f91u);
            uint8_t t = CELL8(off, 0x522f90u);
            int16_t c;
            if (fl & 0xc) c = 0;
            else if (CELL8(off, 0x522f97u) >= 0x11) c = 0;
            else if (fl & 1) c = 1;
            else if (fl & 0x20) c = t > 1 ? (t < 0x17 ? 6 : 3) : 3;
            else if (fl & 0x40) c = 100;
            else if (!(fl & 0x80)) c = (fl & 0x10) ? (t == 16 ? 100 : 0) : 3;
            else if (t == 0x14) c = 3;
            else if (t == 3 || t == 6 || t == 0xc || t == 9) c = 0;
            else c = 100;
            MOVECOST(idx) = c;
        }
    return idx - 1;
}

/* ================================================================ the battlefield's greedy path walk */
/* the battlefield distance field DAT_00504030 (80 × 80 i16), the elevation bands DAT_004f7d80 (g_stage_scratch), the
 * walk DAT_004f9680 ((col, row) byte pairs, 150), its start DAT_004f7d70, the 8 cell deltas DAT_004d6c98 */
#define BFIELD(i)   AT(int16_t, g_bf_50402e, 0x50402eu, 0x504030 + 2 * (int32_t)(i))
#define BSTAGE(i)   AT(uint8_t, g_stage_scratch, 0x4f7d80u, 0x4f7d80 + (int32_t)(i))
#define BWALK(i)    AT(uint8_t, g_bf_4f9680, 0x4f9680u, 0x4f9680 + (int32_t)(i))
#define BDELTA(i)   AT(int32_t, g_bf_4d6c98, 0x4d6c98u, 0x4d6c98 + 4 * (int32_t)(i))

/* Port of FUN_00472392 (715 B) — walk the battlefield distance field down from (`a2`, `a3`) towards (`a0`, `a1`) into
 * DAT_004f9680: each step to the lowest non-zero neighbour within one elevation band (a tie only in the direction of
 * the goal, 0x46f434); returns the step count, 150 when adjacent already (Chebyshev ≤ 1, 0x404f4c), stuck or 150
 * steps long, 0 when the start is unreachable (≤ 0 or ≥ 998). */
int rl_472392(int a0, int a1, int a2, int a3)
{
    int x = a2, y = a3, n = 0;
    g_bf_4f7d70 = a2 + 80 * a3;
    int cell = a2 + 80 * a3;
    BWALK(0) = (uint8_t)a2;
    BWALK(1) = (uint8_t)a3;
    int best = BFIELD(cell);
    if (rl_404f4c(a0, a1, a2, a3) <= 1) return 150;
    if (best <= 0 || best >= 998) return 0;
    while (best > 1) {
        BWALK(2 * n) = (uint8_t)x;
        BWALK(2 * n + 1) = (uint8_t)y;
        best = BFIELD(cell);
        int band = BSTAGE(cell);
        int dir = -1;
        int goal = (uint8_t)engine_path_direction_8way(x, y, a0, a1);
        for (int i = 0; i < 8; i++) {
            int nc = cell + BDELTA(i);
            int v = BFIELD(nc);
            int b = BSTAGE(nc);
            if (!((b == band || (band - 1 <= b && band + 1 >= b)) && BFIELD(nc) != 0)) continue;
            if (v >= best) {
                if (v == best && i == goal) {
                    best = BFIELD(nc);
                    dir = i;
                }
            } else {
                best = BFIELD(nc);
                dir = i;
            }
        }
        if (dir == -1) return 150;
        switch (dir) {
        case 0: y--; cell -= 80; break;
        case 1: y--; x++; cell -= 79; break;
        case 2: x++; cell++; break;
        case 3: x++; y++; cell += 81; break;
        case 4: y++; cell += 80; break;
        case 5: y++; x--; cell += 79; break;
        case 6: x--; cell--; break;
        case 7: x--; y--; cell -= 81; break;
        default: break;
        }
        if (++n >= 150) return 150;
    }
    return n;
}

/* Port of FUN_0047265d (536 B) — the reachable cell (non-zero field) on (`a2`, `a3`)'s elevation band nearest to it:
 * square rings of radius 0..29 (clamped to the 80 × 80 field), the lowest field value in the first ring holding any;
 * found → the cell cursor DAT_00569570 / 78 = it, 1. Meeting (`a0`, `a1`) on the band first → 0 (the cursor on it);
 * none → 0 (the cursor past the last box). The box's counters are the cursor. */
int rl_47265d(int a0, int a1, int a2, int a3)
{
    int band = BSTAGE(80 * a3 + a2);
    for (int i = 0; i < 30; i++) {
        int x0 = a2 - i, y0 = a3 - i, h = 2 * i + 1, w = h;
        if (a2 - i >= 0) {
            if (h + x0 > 80) w = 80 - x0;
        } else {
            w = x0 + h;
            x0 = 0;
        }
        if (y0 >= 0) {
            if (h + y0 > 80) h = 80 - y0;
        } else {
            h += y0;
            y0 = 0;
        }
        int lo = 900, at = 0, c = x0 + 80 * y0;
        for (CUR_ROW = y0; h + y0 > CUR_ROW; CUR_ROW++) {
            for (CUR_COL = x0; w + x0 > CUR_COL; CUR_COL++, c++) {
                if (BSTAGE(c) != band) continue;
                if (BFIELD(c) != 0 && BFIELD(c) < lo) {
                    lo = BFIELD(c);
                    at = c;
                }
                if (a0 == CUR_COL && CUR_ROW == a1) return 0;
            }
            c += 80 - w;
        }
        if (at != 0) {
            CUR_ROW = at / 80;
            CUR_COL = at % 80;
            return 1;
        }
    }
    return 0;
}

/* Port of FUN_00472875 (151 B) — battlefield unit `a0`'s path: the walk's first `a1` steps (DAT_004f9680) into +0x38
 * (pairs), the count +0x36 (i16), +0x164 = 1. */
int rl_472875(int a0, int a1)
{
    BS16(a0, 0x5544b6) = (int16_t)a1;
    BU8(a0, 0x5545e4) = 1;
    for (int i = 0; i < a1; i++) {
        BU8(a0, 0x5544b8 + 2 * i) = BWALK(2 * i);
        BU8(a0, 0x5544b9 + 2 * i) = BWALK(2 * i + 1);
    }
    return a1;
}

/* ================================================================ the event popup step */

/* Port of FUN_00472e46 (600 B) — the event popup's per-tick step. Outside the strategic screens (screen 0 / 39 / 41,
 * or 15 with DAT_00553f54 == 8) an open popup (DAT_0053e910) is dismissed (0x476768). On them: the battle sub-state 2
 * dismisses an open popup; else with the cooldown DAT_0057c954 run out the next event is taken from the read slot
 * (its code → DAT_0053e910; none: the read index catches up one) — its fields into DAT_005440ac / DAT_005678ac /
 * DAT_0055ce54 / DAT_0057c8ac / DAT_005440a4 / DAT_00553ee0, the slot zeroed (0x472b40), the cooldown 2000; while it
 * runs (MP only, not for kind 14) a kind-4 popup closes at 0, others at 1600; single player holds it at 1. An open
 * popup is drawn (0x47309e). */
int rl_472e46(void)
{
    int s = g_gfx_screen_byte;
    if (!(s == 0 || s == 39 || (s == 15 && g_rl_553f54 == 8) || s == 41)) {
        if (g_rl_53e910 != 0) return rl_476768();
        return s;
    }
    if ((int32_t)s_slot_0057c8fc == 2 && g_rl_53e910 != 0) return rl_476768();
    int r = s;
    if (g_rl_57c954 <= 0) {
        g_rl_57c954 = 0;
        g_rl_53e910 = EVQ_SLOT32(EVQ_R, 8);
        if (g_rl_53e910 == 0) {
            r = EVQ_W;
            if (EVQ_R != EVQ_W && ++EVQ_R >= 50) EVQ_R = 0;
            return r;
        }
        g_rl_5440ac = EVQ_SLOT32(EVQ_R, 0xc);
        g_rl_5678ac = EVQ_SLOT32(EVQ_R, 0x14);
        g_rl_55ce54 = EVQ_SLOT8(EVQ_R, 0x12);
        g_rl_57c8ac = EVQ_SLOT8(EVQ_R, 0x13);
        g_rl_5440a4 = EVQ_SLOT8(EVQ_R, 0x11);
        g_rl_553ee0 = EVQ_SLOT32(EVQ_R, 4);
        r = rl_472b40(EVQ_R);
        if (++EVQ_R >= 50) EVQ_R = 0;
        g_rl_57c954 = 2000;
    } else {
        --g_rl_57c954;
        if (g_sim_mp_active == 0 || g_rl_5440a4 == 14) {
            if (g_rl_57c954 <= 0) g_rl_57c954 = 1;
        } else if (g_rl_5440a4 == 4) {
            if (g_rl_57c954 <= 0) r = rl_476768();
        } else if (g_rl_57c954 <= 1600)
            r = rl_476768();
    }
    if (g_rl_53e910 != 0) return rl_47309e();
    return r;
}

/* ================================================================ the army-state handlers */
/* 0x4650b0's table DAT_004d6a50, entries 2..4 (no rows: the paged shadow's fault frames, docs/findings/rl-terr.md).
 * The linked ports they call that post take the port's queue struct: loaded from retail's six queue globals around
 * the call and stored back. The executor 0x465d28 reads its uninitialized waypoint locals (quirk #229) through
 * g_army_move_* (a row loads them from the residue below the handler's e0). */
static engine_event_queue_state_t s_terr_evq;
static engine_event_queue_state_t *terr_evq_load(void)
{
    memcpy(&s_terr_evq.scratch, g_rl_553510, sizeof s_terr_evq.scratch);
    memcpy(s_terr_evq.ring, g_misc_128, 128);
    memcpy((uint8_t *)s_terr_evq.ring + 128, g_rl_568500, sizeof s_terr_evq.ring - 128);
    s_terr_evq.write_idx = EVQ_W;
    s_terr_evq.read_idx = EVQ_R;
    s_terr_evq.notify_flag = g_rl_565484;
    s_terr_evq.consumer_cooldown = g_rl_57c954;
    return &s_terr_evq;
}
static void terr_evq_store(void)
{
    if (memcmp(g_rl_553510, &s_terr_evq.scratch, sizeof s_terr_evq.scratch))
        memcpy(g_rl_553510, &s_terr_evq.scratch, sizeof s_terr_evq.scratch);
    if (memcmp(g_misc_128, s_terr_evq.ring, 128)) memcpy(g_misc_128, s_terr_evq.ring, 128);
    if (memcmp(g_rl_568500, (uint8_t *)s_terr_evq.ring + 128, sizeof s_terr_evq.ring - 128))
        memcpy(g_rl_568500, (uint8_t *)s_terr_evq.ring + 128, sizeof s_terr_evq.ring - 128);
    if (EVQ_W != s_terr_evq.write_idx) EVQ_W = s_terr_evq.write_idx;
    if (EVQ_R != s_terr_evq.read_idx) EVQ_R = s_terr_evq.read_idx;
    if (g_rl_565484 != s_terr_evq.notify_flag) g_rl_565484 = s_terr_evq.notify_flag;
    if (g_rl_57c954 != s_terr_evq.consumer_cooldown) g_rl_57c954 = s_terr_evq.consumer_cooldown;
}
static void terr_move_run(void)
{
    engine_event_queue_state_t *q = terr_evq_load();
    engine_army_move_run(q, g_sim_mp_active);
    terr_evq_store();
}
#define FACING_MARCH(f) AT(int32_t, g_rl_4d6a78, 0x4d6a78u, 0x4d6a78 + 4 * (int32_t)(f))
#define FACING_IDLE(f)  AT(int32_t, g_rl_4d6ab8, 0x4d6ab8u, 0x4d6ab8 + 4 * (int32_t)(f))

/* Port of FUN_00465486 (412 B) — army state 2 (settling), the army DAT_005533e8: fewer than 30 men (+0x168) → removed
 * (0x46f0a9); else +0x154 = 10, the sprite +7 = 3 × ((+9 + 1) & 7) − 112 + the facing table DAT_004d6a78[+0x1b]; a
 * county change (cell +7 vs +0x10) re-collects the income (0x4ad6c0); the settle (0x4abd0f); the timer +0x160
 * counts down; sub-state +0x14c == 2 runs the executor (0x465d28). */
int rl_465486(void)
{
    int a = g_army_state_index;
    if (TER32(a, 0x52f218) < 30) {
        engine_unit_remove(a);
        return 0;
    }
    TER8(a, 0x52f204) = 10;
    TER8(a, 0x52f0b7) = (uint8_t)(3 * ((TER8(a, 0x52f0b9) + 1) & 7) - 112);
    TER8(a, 0x52f0b7) += (uint8_t)FACING_MARCH(TER8(a, 0x52f0cb));
    int key = CELL8(TER32(a, 0x52f0bc), 0x522f97u);
    if ((int)TER8(a, 0x52f0c0) != key) engine_realm_collect_territory_income();
    engine_event_queue_state_t *q = terr_evq_load();
    engine_combat_army_settle_region(g_army_state_index, (unsigned)key, q, g_sim_mp_active);
    terr_evq_store();
    a = g_army_state_index;
    if (TER32(a, 0x52f210) > 0) TER32(a, 0x52f210)--;
    if (TER8(a, 0x52f1fc) == 2) terr_move_run();
    return 0;
}

/* states 3 and 4 are byte-identical in retail: +0x154 = 10, the sprite = 6 × ((+9 + 1) & 7) + DAT_004d6ab8[+0x1b], the
 * county +0x10 = the cell's, the timer counts down, sub-state 2 runs the executor */
static void terr_state_idle(void)
{
    int a = g_army_state_index;
    TER8(a, 0x52f204) = 10;
    TER8(a, 0x52f0b7) = (uint8_t)(6 * ((TER8(a, 0x52f0b9) + 1) & 7));
    TER8(a, 0x52f0b7) += (uint8_t)FACING_IDLE(TER8(a, 0x52f0cb));
    TER8(a, 0x52f0c0) = CELL8(TER32(a, 0x52f0bc), 0x522f97u);
    if (TER32(a, 0x52f210) > 0) TER32(a, 0x52f210)--;
    if (TER8(a, 0x52f1fc) == 2) terr_move_run();
}

/* Port of FUN_00465622 (319 B) — army state 3 (idle). */
int rl_465622(void)
{
    terr_state_idle();
    return 0;
}

/* Port of FUN_00465761 (319 B) — army state 4 (in transit): retail's copy of state 3. */
int rl_465761(void)
{
    terr_state_idle();
    return 0;
}
