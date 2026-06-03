/*
 * OpenLords2 — real-time strategic-map army-state machine.  See
 * engine_army_state.h.
 *
 * Verbatim port of the per-tick single-player army-state driver FUN_004650b0
 * and the four state→display handlers it dispatches through the function table
 * PTR_FUN_004d6a50:
 *
 *   state  retail handler   role
 *   ----   --------------   ----------------------------------------------
 *    0     FUN_00465214     no-op stub (never dispatched: state 0 disbands)
 *    1     FUN_0046521f     march/occupy — sprite by head-count bucket;
 *                           region-owner reconcile via army_enter_region
 *    2     FUN_00465486     settle — low head removes the unit; else region
 *                           reconcile via army_settle_region
 *    3     FUN_00465622     idle/in-transit — set region colour directly
 *    4     FUN_00465761     (byte-identical to state 3)
 *    5     (null)           never dispatched in practice
 *
 * Each live record (owner +0x00 != 0) whose state byte is 0 or >= 6 is zeroed
 * (FUN_0046ead0 = memset 0x1a4).  States 1..5 index the handler table.
 *
 * The driver itself runs no RNG, audio or rendering: it mutates the occupant
 * display sprite (+0x07) and effective-region colour (+0x10), decrements the
 * per-occupant timer (+0x160), and — when the occupant sub-state (+0x14c) is 2
 * — hands off to the deep movement-step executor seam (FUN_00465d28, bound
 * separately).  All other callees are already ported:
 *   FUN_004abb36 → engine_combat_army_enter_region
 *   FUN_004abd0f → engine_combat_army_settle_region
 *   FUN_004ad6c0 → engine_realm_collect_territory_income
 *   FUN_0046f0a9 → engine_unit_remove
 *   FUN_0046ead0 → memset(rec, 0, 0x1a4)
 *
 * The two facing→frame tables are baked verbatim from Lords2.exe:
 *   k_facing_march ← DAT_004d6a78 (8 × i32) — states 1/2 (3-frame walk cycle)
 *   k_facing_idle  ← DAT_004d6ab8 (8 × i32) — states 3/4 (6-frame walk cycle)
 */

#include <stdint.h>
#include <string.h>

#include "engine_army_state.h"
#include "save.h"                  /* g_per_territory_420 (retail 0x52f0b0)     */
#include "engine_combat.h"         /* army_enter_region / army_settle_region    */
#include "engine_realm.h"          /* collect_territory_income (FUN_004ad6c0)    */
#include "engine_realm_upkeep.h"   /* engine_unit_remove (FUN_0046f0a9)          */

/* --- per-territory occupant record (g_per_territory_420, stride 0x1a4) ----- */
#define ASTRIDE      0x1a4
#define AREC(i)      (&g_per_territory_420[(i) * ASTRIDE])

#define A_OWNER      0x00     /* owning player id; 0 = dead record (u8)         */
#define A_READY      0x06     /* march-ready flag: 0 → advance sub-state (u8)   */
#define A_SPRITE     0x07     /* display sprite frame, recomputed each tick (u8)*/
#define A_STATE      0x08     /* state byte → handler-table index (u8)          */
#define A_ANIM       0x09     /* walk-cycle phase selector (u8)                 */
#define A_CELL       0x0c     /* byte offset of the occupant's cell (i32)       */
#define A_REGION     0x10     /* effective-region colour key (u8)               */
#define A_FACING     0x1b     /* facing 0..7 → facing-frame table index (u8)    */
#define A_SUBSTATE   0x14c    /* sub-state: 2 → run movement executor (u8)      */
#define A_TICKS      0x154    /* redraw-priority byte, latched per state (u8)   */
#define A_TIMER      0x160    /* per-occupant countdown timer (i32)             */
#define A_HEAD       0x168    /* total head-count (i32)                         */

/* Facing-frame deltas (DAT_004d6a78 / DAT_004d6ab8, 8 × i32 each), indexed by
 * the occupant facing byte (+0x1b, a 0..7 compass direction). */
static const int32_t k_facing_march[8] = { 0, 1, 2, 1, 0, 1, 2, 1 };
static const int32_t k_facing_idle [8] = { 0, 1, 2, 3, 4, 5, 0, 1 };

/* Driver / handler shared state (retail globals). */
int     g_army_state_index;        /* DAT_005533e8 */
int     g_army_state_live_count;    /* DAT_00553f08 */
uint8_t g_army_state_abort;         /* DAT_0050a49e */

/* Per-cell descriptor grid (retail DAT_00522f90); the region key is byte +7 of
 * each 8-byte cell, so g_cells[A_CELL + 7] is the cell's region key. */
static const uint8_t      *g_cells;
static engine_army_move_fn  g_move_fn;

void engine_army_state_bind_cells(const uint8_t *cells)   { g_cells = cells; }
void engine_army_state_bind_move_fn(engine_army_move_fn f) { g_move_fn = f;  }

/* Region key of the cell the current occupant stands on (cell +7). */
static int occupant_region_key(const uint8_t *r)
{
    return g_cells[*(const int32_t *)(r + A_CELL) + 7];
}

/* The walk-cycle phase index: (anim + 1) mod 8. */
static int anim_phase(const uint8_t *r)
{
    return (r[A_ANIM] + 1) & 7;
}

/* --- state 1: marching / occupying (FUN_0046521f) ------------------------- */
static void handler_march(engine_event_queue_state_t *evq, int mp_active)
{
    uint8_t *r = AREC(g_army_state_index);

    r[A_TICKS] = 0xf;

    /* Sprite base by army size: <=300 small, <=600 medium, else large. */
    int head = *(int32_t *)(r + A_HEAD);
    int base = (head <= 0x12c) ? 0x48 : (head <= 0x258) ? 0x60 : 0x78;
    r[A_SPRITE] = (uint8_t)(anim_phase(r) * 3 + base);
    r[A_SPRITE] = (uint8_t)(r[A_SPRITE] + k_facing_march[r[A_FACING] & 7]);

    int region = occupant_region_key(r);
    if (r[A_REGION] != (uint8_t)region) {
        engine_combat_army_enter_region(g_army_state_index, (unsigned)region, evq);
        engine_realm_collect_territory_income();
    }

    if (*(int32_t *)(r + A_TIMER) > 0)
        (*(int32_t *)(r + A_TIMER))--;

    if ((int8_t)r[A_SUBSTATE] == 2) {
        if (g_move_fn) g_move_fn(evq, mp_active);   /* FUN_00465d28 seam */
    } else if (r[A_READY] == 0) {
        r[A_SUBSTATE] = 2;
    }
}

/* --- state 2: settling (FUN_00465486) ------------------------------------ */
static void handler_settle(engine_event_queue_state_t *evq, int mp_active)
{
    uint8_t *r = AREC(g_army_state_index);

    if (*(int32_t *)(r + A_HEAD) < 0x1e) {         /* head < 30 → remove unit */
        engine_unit_remove(g_army_state_index);    /* FUN_0046f0a9 */
        return;
    }

    r[A_TICKS] = 0xa;
    r[A_SPRITE] = (uint8_t)(anim_phase(r) * 3 + 0x90);
    r[A_SPRITE] = (uint8_t)(r[A_SPRITE] + k_facing_march[r[A_FACING] & 7]);

    int region = occupant_region_key(r);
    if (r[A_REGION] != (uint8_t)region)
        engine_realm_collect_territory_income();   /* colour change → re-aggregate */
    /* settle_region latches +0x10 itself and may warn the owner / spawn rebels. */
    engine_combat_army_settle_region(g_army_state_index, (unsigned)region, evq, mp_active);

    if (*(int32_t *)(r + A_TIMER) > 0)
        (*(int32_t *)(r + A_TIMER))--;

    if ((int8_t)r[A_SUBSTATE] == 2 && g_move_fn)
        g_move_fn(evq, mp_active);                  /* FUN_00465d28 seam */
}

/* --- states 3 & 4: idle / in-transit (FUN_00465622 ≡ FUN_00465761) -------- */
static void handler_idle(engine_event_queue_state_t *evq, int mp_active)
{
    uint8_t *r = AREC(g_army_state_index);

    r[A_TICKS] = 0xa;
    r[A_SPRITE] = (uint8_t)(anim_phase(r) * 6);
    r[A_SPRITE] = (uint8_t)(r[A_SPRITE] + k_facing_idle[r[A_FACING] & 7]);

    r[A_REGION] = (uint8_t)occupant_region_key(r);  /* set region colour directly */

    if (*(int32_t *)(r + A_TIMER) > 0)
        (*(int32_t *)(r + A_TIMER))--;

    if ((int8_t)r[A_SUBSTATE] == 2 && g_move_fn)
        g_move_fn(evq, mp_active);                  /* FUN_00465d28 seam */
}

/* Handler dispatch table (PTR_FUN_004d6a50).  Index 0 is the no-op stub (never
 * reached: state 0 disbands); index 5 is null in retail (never dispatched). */
typedef void (*army_state_handler)(engine_event_queue_state_t *, int);
static void handler_stub(engine_event_queue_state_t *evq, int mp) { (void)evq; (void)mp; }
static const army_state_handler k_handlers[6] = {
    handler_stub,    /* 0 */
    handler_march,   /* 1 */
    handler_settle,  /* 2 */
    handler_idle,    /* 3 */
    handler_idle,    /* 4 */
    NULL,            /* 5 */
};

/* --- driver: FUN_004650b0 ------------------------------------------------- */
void engine_army_state_run(engine_event_queue_state_t *evq, int mp_active)
{
    g_army_state_live_count = 0;
    g_army_state_abort = 0;

    for (g_army_state_index = 1; g_army_state_index < 0x97; g_army_state_index++) {
        uint8_t *r = AREC(g_army_state_index);
        if (r[A_OWNER] == 0)
            continue;                              /* dead record — no abort check */

        g_army_state_live_count++;

        int state = r[A_STATE];
        if (state <= 0 || state >= 6) {
            memset(r, 0, ASTRIDE);                 /* FUN_0046ead0 — disband */
        } else {
            army_state_handler h = k_handlers[state];
            if (h) h(evq, mp_active);              /* state 5 (null) never occurs */
        }

        if (g_army_state_abort != 0)               /* set by the movement seam */
            break;
    }
}
