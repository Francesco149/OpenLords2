/*
 * OpenLords2 — strategic-map army movement: destination-cell evaluator.  See
 * engine_army_move.h.
 *
 * Verbatim port of the three functions that answer "what is in the cell my
 * stack is about to enter?" for the real-time army-movement executor:
 *
 *   FUN_00466893 → engine_army_move_eval_dir   pick the neighbour cell in a
 *                                              compass direction, grid-bound it,
 *                                              then classify it
 *   FUN_00466c3c → classify_cell               latch the cell's scratch (flags /
 *                                              region / terrain / offset /
 *                                              occupant) and map the movement
 *                                              flags to an outcome code; if the
 *                                              cell already holds a stack, defer
 *                                              to eval_occupied
 *   FUN_004658c1 → eval_occupied               resolve a collision with the
 *                                              occupying stack: merge (same
 *                                              owner), fight (hostile owner via
 *                                              engine_combat_dispatch), or block
 *
 * Outcome codes (returned up to the executor): 1 open, 3 rough-base, 5 combat,
 * 6 region-event, 7 special, 8 impassable, 10 arrival (this stack was destroyed
 * in the collision), 0 grid edge.
 *
 * The current occupant index is engine_army_state's g_army_state_index (retail
 * DAT_005533e8); the occupied-cell stack index is g_combat_active_terr (retail
 * DAT_00553f8c), which classify_cell latches from the destination cell's +5
 * byte before eval_occupied reads it.  All combat/diplomacy/event callees are
 * already ported.
 */

#include <stdint.h>

#include "engine_army_move.h"
#include "save.h"                  /* g_per_territory_420, g_player_strategic_352 */
#include "engine_army_state.h"     /* g_army_state_index, g_army_state_abort       */
#include "engine_combat.h"         /* engine_combat_dispatch / _merge_stack, terr  */
#include "engine_diplomacy.h"      /* should_engage / battle_adjust                */
#include "engine_realm_upkeep.h"   /* engine_unit_remove (FUN_0046f0a9)            */
#include "engine_event_queue.h"    /* engine_event_queue_post (FUN_00472bc5)       */
#include "save_schema_globals.h"   /* s_slot_0057c8cc (local player)               */

/* --- per-territory occupant record (g_per_territory_420, stride 0x1a4) ----- */
#define MSTRIDE      0x1a4
#define MREC(i)      (&g_per_territory_420[(i) * MSTRIDE])

#define M_OWNER      0x00     /* owning player id; 0 = dead record (u8)         */
#define M_F01        0x01     /* "has a live order" flag (u8)                   */
#define M_STATE      0x08     /* state byte (1 march, 2 settle, 3/4 idle) (u8)  */
#define M_COL        0x0a     /* grid column 0..63 (i8)                         */
#define M_ROW        0x0b     /* grid row 0..63 (i8)                            */
#define M_CELL       0x0c     /* byte offset of the occupant's cell (i32)       */
#define M_F1A        0x1a     /* merge-eligibility guard (u8; 4 = ineligible)   */
#define M_WP_COUNT   0x1c     /* waypoint counter (u8)                          */
#define M_F14E       0x14e    /* clears the cell's 0x80 region-event flag (u8)  */
#define M_F152       0x152    /* expected merge-partner index (u8)              */

/* --- per-cell descriptor grid (DAT_00522f90, 8 bytes/cell, row stride 0x200) */
#define C_TERRAIN    0        /* terrain / owner byte                           */
#define C_FLAGS      1        /* movement flags                                 */
#define C_OCCUPANT   5        /* occupant-record index (0 = empty)              */
#define C_REGION     7        /* effective-region key                           */
#define CELL_ROW     0x200    /* one grid row in bytes (64 cells × 8)           */

/* --- per-player strategic record (g_player_strategic_352, stride 0x160) ----- */
#define PSTRIDE      0x160
#define P_ACTIVE     0x05     /* (&DAT_0057bf05)[i*0x160] — has-data filter byte */

static const uint8_t *g_cells;            /* DAT_00522f90 */

/* Outcome scratch latched per classified cell (retail runtime scalars). */
uint8_t g_army_move_cell_flags;           /* DAT_0050a4a4 */
uint8_t g_army_move_cell_region;          /* DAT_0050a49c */
uint8_t g_army_move_cell_terrain;         /* DAT_0050a49d */
int     g_army_move_cell_off;             /* DAT_0050a4a0 */

void engine_army_move_bind_cells(const uint8_t *cells) { g_cells = cells; }

/* Build the per-player has-data filter the event-queue post() consults (retail
 * (&DAT_0057bf05)[i*0x160], one byte per slot 0..6; last slot inert). */
static void build_has_data(uint8_t hd[7])
{
    for (int i = 0; i < 6; i++)
        hd[i] = g_player_strategic_352[i * PSTRIDE + P_ACTIVE];
    hd[6] = 0;
}

/* --- FUN_004658c1 @ 0x4658c1: resolve a destination cell already holding a
 * stack (1112 B, __stdcall) ------------------------------------------------- */
static int eval_occupied(engine_event_queue_state_t *evq, int mp_active)
{
    int self  = g_army_state_index;       /* DAT_005533e8 */
    int other = g_combat_active_terr;      /* DAT_00553f8c (cell +5, latched)   */
    uint8_t *rs = MREC(self);
    uint8_t *ro = MREC(other);

    /* Base outcome from the cell's bit0 (rough terrain): 3 if set, else 1. */
    int result = (g_army_move_cell_flags & 1) ? 3 : 1;

    /* A stack that is itself idle (state 3/4) never interacts; nor does a
     * target that is idle (state 3). */
    if (rs[M_STATE] == 3 || rs[M_STATE] == 4)
        return result;
    if (ro[M_STATE] == 3)
        return result;

    if (ro[M_STATE] == 1 || ro[M_STATE] == 2) {
        if (rs[M_OWNER] == ro[M_OWNER]) {
            /* Same owner → merge the two stacks. */
            if (ro[M_F01] != 0) {
                /* Target carries a live order: only merge if it is the partner
                 * this stack expected (+0x152). */
                if (rs[M_F152] != (uint8_t)other)
                    return result;
                engine_combat_merge_stack(self, other, evq);
                return result;
            }
            if (rs[M_F1A] == 4 || ro[M_F1A] == 4)
                return result;
            engine_combat_merge_stack(self, other, evq);
            return result;
        }
        /* Different owner → fight if the relation allows it. */
        if (engine_diplomacy_should_engage((int8_t)rs[M_OWNER],
                                           (int8_t)ro[M_OWNER]) == 0)
            return result;
        if (engine_combat_dispatch(self, other, evq, mp_active) != 0) {
            /* Interactive battle handed off — halt the army walk and bump this
             * stack's waypoint counter so it retries the step next tick. */
            g_army_state_abort = 1;        /* DAT_0050a49e */
            rs[M_WP_COUNT]++;
            return result;
        }
        /* Auto-resolved: if this stack was destroyed (owner cleared), report
         * arrival/done (10); otherwise it survived in place. */
        if (rs[M_OWNER] != 0)
            return result;
        return 10;
    }

    /* Target is a garrison/region stack (state 4) of a hostile owner →
     * overrun it: relation hit, both owners notified (0xa2 / 0xa1), remove it. */
    if (ro[M_STATE] == 4 && rs[M_OWNER] != ro[M_OWNER] &&
        engine_diplomacy_should_engage((int8_t)rs[M_OWNER],
                                       (int8_t)ro[M_OWNER]) != 0) {
        uint8_t hd[7]; build_has_data(hd);
        int local = (int)s_slot_0057c8cc;
        engine_diplomacy_battle_adjust((int8_t)rs[M_OWNER], (int8_t)ro[M_OWNER],
                                       5, evq);
        engine_event_queue_post(evq, 0, (int8_t)rs[M_OWNER], 0xa2, 0, 0, 0, 0, 0,
                                hd, local);
        engine_event_queue_post(evq, 0, (int8_t)ro[M_OWNER], 0xa1, 0, 0, 0, 0, 0,
                                hd, local);
        engine_unit_remove(other);
        result = 10;
    }
    return result;
}

/* --- FUN_00466c3c @ 0x466c3c: classify a destination cell by byte offset
 * (328 B, __cdecl) ---------------------------------------------------------- */
static int classify_cell(int cell_off, engine_event_queue_state_t *evq,
                         int mp_active)
{
    const uint8_t *cell = &g_cells[cell_off];

    g_army_move_cell_flags   = cell[C_FLAGS];
    g_army_move_cell_region  = cell[C_REGION];
    g_army_move_cell_terrain = cell[C_TERRAIN];
    g_army_move_cell_off     = cell_off;
    g_combat_active_terr     = cell[C_OCCUPANT];   /* DAT_00553f8c */

    /* The 0x80 (region-event) flag is suppressed on terrain 0x14 and whenever
     * this stack carries its +0x14e suppressor. */
    if (g_army_move_cell_flags & 0x80) {
        if (g_army_move_cell_terrain == 0x14)
            g_army_move_cell_flags &= 0x7f;
        if (MREC(g_army_state_index)[M_F14E] != 0)
            g_army_move_cell_flags &= 0x7f;
    }

    if (g_combat_active_terr == 0) {
        /* Empty cell — outcome from the (post-suppression) movement flags, in
         * priority order bit0 > bit6 > bit7 > bit4 > bit5. */
        uint8_t f = g_army_move_cell_flags;
        if (f & 0x01) return 3;
        if (f & 0x40) return 5;
        if (f & 0x80) return 6;
        if (f & 0x10) return 7;
        if (f & 0x20) return 8;
        return 1;
    }
    return eval_occupied(evq, mp_active);
}

/* --- FUN_00466893: evaluate the neighbour cell in a compass direction ------ */
int engine_army_move_eval_dir(int dir, engine_event_queue_state_t *evq,
                              int mp_active)
{
    /* Neighbour deltas (bytes) and the grid-edge guard for each direction.
     * Guard bits: a direction stepping toward column 64 needs col < 63, toward
     * column −1 needs col >= 1; likewise for rows (row stride 0x200). */
    enum { G_COL_MAX = 1, G_COL_MIN = 2, G_ROW_MAX = 4, G_ROW_MIN = 8 };
    static const struct { int delta; uint8_t guard; } k_dir[8] = {
        { -CELL_ROW,            G_ROW_MIN             },  /* 0  N  */
        { -CELL_ROW + 8,        G_COL_MAX | G_ROW_MIN },  /* 1  NE */
        {            8,         G_COL_MAX             },  /* 2  E  */
        {  CELL_ROW + 8,        G_COL_MAX | G_ROW_MAX },  /* 3  SE */
        {  CELL_ROW,            G_ROW_MAX             },  /* 4  S  */
        {  CELL_ROW - 8,        G_COL_MIN | G_ROW_MAX },  /* 5  SW */
        {           -8,         G_COL_MIN             },  /* 6  W  */
        { -CELL_ROW - 8,        G_COL_MIN | G_ROW_MIN },  /* 7  NW */
    };

    if (dir < 0 || dir > 7)
        return 0;

    uint8_t *r = MREC(g_army_state_index);
    int col = (int8_t)r[M_COL];
    int row = (int8_t)r[M_ROW];
    uint8_t guard = k_dir[dir].guard;

    if ((guard & G_COL_MAX) && !(col < 0x3f)) return 0;
    if ((guard & G_COL_MIN) && !(col >= 1))   return 0;
    if ((guard & G_ROW_MAX) && !(row < 0x3f)) return 0;
    if ((guard & G_ROW_MIN) && !(row >= 1))   return 0;

    int cell_off = *(int32_t *)(r + M_CELL) + k_dir[dir].delta;
    return classify_cell(cell_off, evq, mp_active);
}
