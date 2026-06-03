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
#include "save.h"                  /* g_per_territory_420, g_player_strategic_352, g_per_army_768 */
#include "engine_army_state.h"     /* g_army_state_index, g_army_state_abort       */
#include "engine_combat.h"         /* engine_combat_dispatch / _merge_stack, terr  */
#include "engine_diplomacy.h"      /* should_engage / battle_adjust                */
#include "engine_realm_upkeep.h"   /* engine_unit_remove / map link / unlink       */
#include "engine_event_queue.h"    /* engine_event_queue_post (FUN_00472bc5)       */
#include "engine_math.h"           /* engine_math_percent_div / _percent_of        */
#include "engine_territory.h"      /* set_cell_type / mark_draw_box / full_dirty   */
#include "engine_path.h"           /* engine_path_direction_8way (FUN_0046f434)    */
#include "engine.h"                /* engine_current_screen (DAT_004eac50)         */
#include "engine_turn.h"           /* g_sim_turn_progress (DAT_00553fc8)           */
#include "save_schema_globals.h"   /* s_slot_0057c8cc (local player)               */

/* --- per-territory occupant record (g_per_territory_420, stride 0x1a4) ----- */
#define MSTRIDE      0x1a4
#define MREC(i)      (&g_per_territory_420[(i) * MSTRIDE])

#define M_OWNER      0x00     /* owning player id; 0 = dead record (u8)         */
#define M_F01        0x01     /* "has a live order" flag (u8)                   */
#define M_DIR        0x09     /* committed move direction 0..7 (u8; aliases the */
                              /* army-state anim byte — classifier latches it)  */
#define M_STATE      0x08     /* state byte (1 march, 2 settle, 3/4 idle) (u8)  */
#define M_COL        0x0a     /* grid column 0..63 (i8)                         */
#define M_ROW        0x0b     /* grid row 0..63 (i8)                            */
#define M_CELL       0x0c     /* byte offset of the occupant's cell (i32)       */
#define M_F1A        0x1a     /* merge-eligibility guard (u8; 4 = ineligible)   */
#define M_WP_COUNT   0x1c     /* waypoint counter (u8)                          */
#define M_ROUGH      0x14d    /* sticky rough-terrain flag, set per step (u8)    */
#define M_F14E       0x14e    /* clears the cell's 0x80 region-event flag (u8)  */
#define M_F152       0x152    /* expected merge-partner index (u8)              */

/* Executor fields (FUN_00465d28), all in the same record. */
#define M_READY      0x06     /* set → suppress the arrival seam (AI/auto army)(u8)*/
#define M_DEST_COL   0x14     /* final destination column (i8)                   */
#define M_DEST_ROW   0x15     /* final destination row (i8)                      */
#define M_WP_LIST    0x1d     /* waypoint list: (col,row) byte pairs from here   */
#define M_SUBSTATE   0x14c    /* sub-state; executor clears it (0) when finished(u8)*/
#define M_ARRIVED    0x150    /* arrived-this-tick flag, set on path/budget end (u8)*/
#define M_STEP_BUDGET 0x154   /* per-tick progress budget (handler sets 0xf/0xa)(i8)*/
#define M_TIMER      0x160    /* per-occupant countdown timer (i32; reset to 4)  */

/* Classifier hysteresis fields (FUN_0046634d), all in the same record. */
#define M_FACING     0x1b     /* facing/shuffle counter, bumped while stalling (u8)*/
#define M_STALL_ACC  0x149    /* stall accumulator (+2 SP / +4 MP), latch ≥0x10(i8)*/
#define M_RETRY      0x14a    /* per-tick retry counter vs the 3/0 budget (i8)   */
#define M_FLAGS14B   0x14b    /* state flags: bit0 = step-eval armed, bit3 spare(u8)*/
#define M_PROGRESS   0x153    /* path-progress accumulator (+3 fast / +1 rough)(u8)*/

/* --- per-cell descriptor grid (DAT_00522f90, 8 bytes/cell, row stride 0x200) */
#define C_TERRAIN    0        /* terrain / owner byte                           */
#define C_FLAGS      1        /* movement flags                                 */
#define C_OCCUPANT   5        /* occupant-record index (0 = empty)              */
#define C_REGION     7        /* effective-region key                           */
#define CELL_ROW     0x200    /* one grid row in bytes (64 cells × 8)           */

/* --- per-player strategic record (g_player_strategic_352, stride 0x160) ----- */
#define PSTRIDE      0x160
#define P_ACTIVE     0x05     /* (&DAT_0057bf05)[i*0x160] — has-data filter byte */

/* --- per-region record (g_per_army_768, retail DAT_0053f9b0, stride 0x300) -- */
#define RSTRIDE          0x300
#define R_OWNER          0x05   /* region owning player (u8; DAT_0053f9b5)       */
#define R_CENSUS_HIGH    0x200  /* high-terrain (0x0f..0x16) cell census (u8)    */
#define R_CENSUS_MID     0x201  /* mid-terrain (0x02..0x0e) cell census (u8)     */
#define R_MID_RESERVE    0x206  /* mid-tier reserve, drawn down with the census(u8)*/
#define R_STOCK_234      0x234  /* mid-tier stock accumulator (i32)              */
#define R_STOCK_244      0x244  /* mid-tier stock pool (i32)                     */
#define R_DENSITY_LOAD   0x250  /* high-tier load numerator (i32)                */
#define R_LOAD_SCRATCH   0x27c  /* high-tier load drain sink (i32; DAT_0053fc2c) */

static const uint8_t *g_cells;            /* DAT_00522f90 */
static engine_army_move_sfx_fn   g_sfx_fn;    /* footstep seam (retail 0x426120) */
static engine_army_move_sfx_fn   g_alert_fn;  /* blocked-step seam (retail 0x426216)*/

void engine_army_move_bind_sfx_fn(engine_army_move_sfx_fn fn) { g_sfx_fn = fn; }
void engine_army_move_bind_alert_sfx_fn(engine_army_move_sfx_fn fn) { g_alert_fn = fn; }
static void army_move_sfx(int id)   { if (g_sfx_fn)   g_sfx_fn(id);   }
static void army_move_alert(int id) { if (g_alert_fn) g_alert_fn(id); }

/* Movement-executor dispatcher seams (see header).  All default NULL → no-op. */
static engine_army_move_present_fn g_present_fn;   /* 0x426360 surface present */
static engine_army_move_arrive_fn  g_arrive_fn;    /* 0x467196 arrival         */
static engine_army_move_region_fn  g_combat_a_fn;  /* 0x4296b5 combat-engage   */
static engine_army_move_region_fn  g_combat_b_fn;  /* 0x4a6c68 combat-resolve  */
static engine_army_move_cell_fn    g_region_lo_fn; /* 0x46873f region-event lo */
static engine_army_move_cell_fn    g_region_hi_fn; /* 0x4686a0 region-event hi */
static engine_army_move_cell_fn    g_special_fn;   /* 0x468ae2 special tile    */

void engine_army_move_bind_present_fn(engine_army_move_present_fn fn) { g_present_fn = fn; }
void engine_army_move_bind_arrive_fn(engine_army_move_arrive_fn fn)   { g_arrive_fn = fn; }
void engine_army_move_bind_combat_fns(engine_army_move_region_fn engage,
                                      engine_army_move_region_fn resolve)
{ g_combat_a_fn = engage; g_combat_b_fn = resolve; }
void engine_army_move_bind_region_fns(engine_army_move_cell_fn lo,
                                      engine_army_move_cell_fn hi)
{ g_region_lo_fn = lo; g_region_hi_fn = hi; }
void engine_army_move_bind_special_fn(engine_army_move_cell_fn fn) { g_special_fn = fn; }

static void army_move_present(int mode)            { if (g_present_fn) g_present_fn(mode); }
static void army_move_arrive(int self)             { if (g_arrive_fn)  g_arrive_fn(self); }
static void army_move_combat_a(int self, unsigned r) { if (g_combat_a_fn) g_combat_a_fn(self, r); }
static void army_move_combat_b(int self, unsigned r) { if (g_combat_b_fn) g_combat_b_fn(self, r); }
static void army_move_region_lo(int self, int off) { if (g_region_lo_fn) g_region_lo_fn(self, off); }
static void army_move_region_hi(int self, int off) { if (g_region_hi_fn) g_region_hi_fn(self, off); }
static void army_move_special(int self, int off)   { if (g_special_fn)  g_special_fn(self, off); }

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

/* --- FUN_00466d84 @ 0x466d84: commit the move into the chosen neighbour
 * (995 B, __stdcall) -------------------------------------------------------- */
void engine_army_move_commit_step(void)
{
    int self = g_army_state_index;
    uint8_t *r = MREC(self);

    engine_unit_map_unlink(self);              /* FUN_0046ec6e — leave old cell */

    /* Per-state footstep cue (indexed DirectSound buffer; seam). */
    switch (r[M_STATE]) {
    case 3: case 4: army_move_sfx(0xb); break;
    case 2:         army_move_sfx(0x5); break;
    case 1:         army_move_sfx(0xc); break;
    default: break;
    }

    /* Apply the latched direction's column/row/cell-offset delta.  Deltas match
     * engine_army_move_eval_dir: ±1 column = ±8 bytes, ±1 row = ±0x200 bytes.
     * A direction > 7 commits nothing (no relink). */
    int32_t *cell = (int32_t *)(r + M_CELL);
    switch (r[M_DIR]) {
    case 0: r[M_ROW]--;             *cell -= 0x200;             break;  /* N  */
    case 1: r[M_ROW]--; r[M_COL]++; *cell -= 0x200; *cell += 8; break;  /* NE */
    case 2:             r[M_COL]++;                 *cell += 8; break;  /* E  */
    case 3: r[M_ROW]++; r[M_COL]++; *cell += 0x200; *cell += 8; break;  /* SE */
    case 4: r[M_ROW]++;             *cell += 0x200;             break;  /* S  */
    case 5: r[M_ROW]++; r[M_COL]--; *cell += 0x200; *cell -= 8; break;  /* SW */
    case 6:             r[M_COL]--;                 *cell -= 8; break;  /* W  */
    case 7: r[M_ROW]--; r[M_COL]--; *cell -= 0x200; *cell -= 8; break;  /* NW */
    default: return;
    }

    engine_unit_map_link(self);                /* FUN_0046eddf — enter new cell */

    /* Sticky rough-terrain flag: set iff the new cell carries movement bit0. */
    r[M_ROUGH] = (g_cells[*cell + 1] & 1) ? 1 : 0;
}

/* --- FUN_00469e0e @ 0x469e0e: terrain-passability predicate (72 B, __cdecl) -
 * 1 iff the cell at byte offset `cell_off` carries a real land terrain type
 * (2 ≤ terrain < 0x17); 0 for sea/edge (< 2) and special tiles (≥ 0x17).  Reads
 * the cell grid's terrain byte (+0).  Shared by the blocked-branch and the
 * movement executor (FUN_00465d28). */
int engine_army_move_terrain_passable(int cell_off)
{
    uint8_t t = g_cells[cell_off + C_TERRAIN];
    return (t >= 2 && t < 0x17) ? 1 : 0;
}

/* --- FUN_00469e5b @ 0x469e5b: scorched-earth resource decay (476 B, __cdecl) -
 * The destroyed-cell side of the blocked branch: when a stack bulldozes a cell
 * of a foreign region, drain a census-proportional share of that region's
 * stockpile and then flatten the cell (terrain → 0).  Two tiers, partitioned on
 * the cell's terrain byte:
 *   mid  (terrain < 0x0f, while the mid reserve +0x206 is non-empty): if the mid
 *        census +0x201 still fits inside the reserve, move a 1/reserve fraction
 *        of stock +0x244 into +0x234 (capped at 100%) and draw the reserve down;
 *        always decrement the mid census.
 *   high (terrain ≥ 0x0f, with a non-zero high census +0x200 and load +0x250):
 *        move a 1/census fraction of the load +0x250 into the scratch sink +0x27c
 *        and decrement the high census.
 * Then set the cell type to 0 (FUN_0046d7f4). */
static void decay_region_cell(int cell_off)
{
    unsigned region   = g_cells[cell_off + C_REGION];   /* DAT_00522f97 = grid+7 */
    uint8_t  terrain  = g_cells[cell_off + C_TERRAIN];  /* DAT_00522f90 = grid+0 */
    uint8_t *rg       = &g_per_army_768[region * RSTRIDE];
    int      frac     = 0;

    if (terrain < 0x0f && rg[R_MID_RESERVE] != 0) {
        if (rg[R_CENSUS_MID] <= rg[R_MID_RESERVE])
            frac = engine_math_percent_div(1, rg[R_MID_RESERVE]);
        if (frac > 100)
            frac = 100;
        int amt = engine_math_percent_of(*(int32_t *)(rg + R_STOCK_244), frac);
        *(int32_t *)(rg + R_STOCK_244) -= amt;
        *(int32_t *)(rg + R_STOCK_234) += amt;
        if (rg[R_CENSUS_MID] <= rg[R_MID_RESERVE])
            rg[R_MID_RESERVE]--;
        rg[R_CENSUS_MID]--;
    }
    else if (terrain > 0x0e && rg[R_CENSUS_HIGH] != 0 &&
             *(int32_t *)(rg + R_DENSITY_LOAD) != 0) {
        int amt = engine_math_percent_div(1, rg[R_CENSUS_HIGH]);
        amt = engine_math_percent_of(*(int32_t *)(rg + R_DENSITY_LOAD), amt);
        *(int32_t *)(rg + R_DENSITY_LOAD)  -= amt;
        *(int32_t *)(rg + R_LOAD_SCRATCH)  += amt;
        rg[R_CENSUS_HIGH]--;
    }

    engine_territory_set_cell_type(cell_off, 0, 0);   /* FUN_0046d7f4(off,0,'\0') */
}

/* --- FUN_0046673c @ 0x46673c: blocked-step branch (343 B) ------------------
 * Reached from the classifier when a step is impassable.  Bumps the stack's
 * path-progress accumulator (+0x153) and, if the obstacle belongs to a foreign
 * region and the cell is real land, levies a relation penalty (only when the
 * mover is an active player), scorch-decays the cell, and plays the blocked
 * cue.  Idle stacks (state 3/4) never reach this.  `evq` carries the relation
 * notification. */
static void blocked_branch(engine_event_queue_state_t *evq)
{
    uint8_t *r = MREC(g_army_state_index);

    if (r[M_STATE] == 3 || r[M_STATE] == 4)
        return;

    int8_t   mover_owner = (int8_t)r[M_OWNER];
    unsigned region      = g_army_move_cell_region;   /* DAT_0050a49c cell +7 key */
    int      region_owner = g_per_army_768[region * RSTRIDE + R_OWNER];

    r[M_PROGRESS] += 3;

    if ((int)mover_owner != region_owner &&
        engine_army_move_terrain_passable(g_army_move_cell_off) != 0) {
        if (g_player_strategic_352[(uint8_t)mover_owner * PSTRIDE + P_ACTIVE] != 0)
            engine_diplomacy_battle_adjust(region_owner, mover_owner, 10, evq);
        decay_region_cell(g_army_move_cell_off);
        army_move_alert(3);                            /* FUN_00426216(3) */
    }
}

/* --- FUN_0046634d @ 0x46634d: per-step result classifier (1002 B, __cdecl) --
 * The spine between the path step and the outcome dispatch.  Until the stack's
 * step-eval is armed (record flag +0x14b bit0), it runs a stall/retry
 * hysteresis: each tick bumps the retry counter, and once it clears the
 * direction-dependent budget (3 normal, 0 once rough) it shuffles the facing,
 * advances the stall accumulator (+2 single-player, +4 multiplayer), and — at
 * 0x10 — arms the step-eval (returning 2).  While disarmed it returns 1.
 *
 * Once armed, it evaluates the actual step (engine_army_move_eval_dir) and post-
 * processes the outcome: an aborted walk reports 10; a grid edge (0) passes
 * through; codes > 4 (combat/region-event/special/arrival) return straight up;
 * an impassable step (8) runs the blocked branch and is then treated as open
 * (1); a rough step (3) sets the sticky rough flag.  Any committable outcome
 * (1/3) bumps the path-progress accumulator (+3 normal, +1 rough), disarms the
 * step-eval, latches the committed direction at record +0x09, re-primes the
 * stall accumulator to 1, and commits the move (engine_army_move_commit_step). */
int engine_army_move_classify_step(int dir, engine_event_queue_state_t *evq,
                                   int mp_active)
{
    uint8_t *r = MREC(g_army_state_index);
    int result;

    r[M_FLAGS14B] &= 0xf7;          /* clear the spare bit3 each entry */

    if ((r[M_FLAGS14B] & 1) == 0) {
        /* Step-eval disarmed: stall/retry hysteresis. */
        int8_t budget = (r[M_ROUGH] == 0) ? 3 : 0;
        r[M_RETRY]++;
        if (budget < (int8_t)r[M_RETRY]) {
            r[M_RETRY] = 0;
            r[M_FACING]++;
            r[M_STALL_ACC] += (mp_active == 0) ? 2 : 4;
            if ((int8_t)r[M_STALL_ACC] < 0x10) {
                result = 1;
            } else {
                r[M_FLAGS14B] |= 1;     /* arm the step-eval */
                r[M_STALL_ACC] = 0;
                r[M_FACING]    = 0;
                result = 2;
            }
        } else {
            result = 1;
        }
        return result;
    }

    /* Step-eval armed: evaluate and post-process the real step. */
    r[M_STALL_ACC] = 0;
    r[M_RETRY]     = 0;
    result = engine_army_move_eval_dir(dir, evq, mp_active);

    if (g_army_state_abort != 0)
        return 10;

    if (result == 0)
        return 0;

    r[M_ROUGH] = 0;
    if (result == 3) {
        r[M_ROUGH] = 1;
    } else if (result == 8) {
        blocked_branch(evq);
        result = 1;
    } else if (result > 4) {
        return result;
    }

    r[M_PROGRESS] += (r[M_ROUGH] == 0) ? 3 : 1;
    r[M_FLAGS14B] &= 0xfe;          /* disarm the step-eval */
    r[M_DIR] = (uint8_t)dir;        /* latch committed direction for commit_step */
    r[M_STALL_ACC] = 1;
    engine_army_move_commit_step();
    return result;
}

/* --- FUN_00465d28 @ 0x465d28: movement-step executor (1441 B, __cdecl) ------
 * See engine_army_move.h for the full contract. */
void engine_army_move_run(engine_event_queue_state_t *evq, int mp_active)
{
    int      self = g_army_state_index;          /* DAT_005533e8 */
    uint8_t *r    = MREC(self);

    /* Retail leaves these uninitialised on the first (step-eval disarmed) pass;
     * the disarmed classifier ignores the direction computed from them, so the
     * only observable use is the dir==8 grid-arrival guard.  We zero-init for a
     * defined first pass — a real stack is never at the (0,0) corner where that
     * differs (engine-quirk: retail-UB first-pass waypoint). */
    unsigned wp_col = 0, wp_row = 0;

    *(int32_t *)(r + M_TIMER) = 4;               /* +0x160 reset per entry */

    for (;;) {
        if (r[M_FLAGS14B] & 1) {                 /* step-eval armed */
            /* Local player's marching stack: stamp a 13×13 alternate-draw box
             * around it and mark the screen dirty (pure render). */
            if (r[M_STATE] == 1 && (int8_t)r[M_OWNER] == (int8_t)s_slot_0057c8cc) {
                engine_territory_mark_draw_box((int8_t)r[M_COL], (int8_t)r[M_ROW], 6);
                engine_territory_mark_full_screen_dirty();
            }

            /* Per-tick step budget spent, or a turn commit in progress → snap
             * progress to the budget and finalise this army for the tick. */
            if ((int8_t)r[M_STEP_BUDGET] <= (int8_t)r[M_PROGRESS] ||
                g_sim_turn_progress == 1 || g_sim_turn_progress == 2) {
                r[M_PROGRESS] = r[M_STEP_BUDGET];
                if (r[M_F01] != 0)
                    army_move_present(0xc);
                int d = engine_path_direction_8way((int8_t)r[M_COL], (int8_t)r[M_ROW],
                                                   (int8_t)r[M_DEST_COL],
                                                   (int8_t)r[M_DEST_ROW]);
                if ((d & 0xff) == 8)
                    r[M_ARRIVED] = 1;
                r[M_SUBSTATE] = 0;
                if (r[M_READY] != 0)
                    return;
                army_move_arrive(self);
                return;
            }

            /* Pull the next waypoint off the per-record list. */
            if (r[M_WP_COUNT] == 0) {
                r[M_ARRIVED] = 1;
                if (r[M_F01] != 0)
                    army_move_present(0xc);
                r[M_SUBSTATE] = 0;
                if (r[M_READY] != 0)
                    return;
                army_move_arrive(self);          /* path end — but keep walking */
            }
            if (r[M_WP_COUNT] != 0)
                r[M_WP_COUNT]--;
            wp_col = r[M_WP_LIST + r[M_WP_COUNT] * 2 + 0];   /* +0x1d */
            wp_row = r[M_WP_LIST + r[M_WP_COUNT] * 2 + 1];   /* +0x1e */
        }

        /* Direction toward the current waypoint; 8 == already standing on it. */
        int dir = engine_path_direction_8way((int8_t)r[M_COL], (int8_t)r[M_ROW],
                                             (int)wp_col, (int)wp_row);
        if ((dir & 0xff) == 8) {
            r[M_SUBSTATE] = 0;
            r[M_ARRIVED]  = 1;
            return;
        }

        int code = engine_army_move_classify_step(dir & 0xff, evq, mp_active);
        if (code == 10)
            return;                              /* arrival / walk aborted */
        if (code == 2)
            continue;                            /* step-eval just armed — re-loop */

        if (code == 3)
            r[M_ROUGH] = 1;
        if (code > 4) {
            r[M_SUBSTATE] = 0;
            if (code == 5) {                     /* combat */
                army_move_combat_a(self, g_army_move_cell_region);
                army_move_combat_b(self, g_army_move_cell_region);
                if (engine_current_screen() == 0x12)
                    g_army_state_abort = 1;      /* battle screen up → halt walk */
            } else if (code == 6) {              /* region-event by terrain band */
                if (g_army_move_cell_terrain < 0x10)
                    army_move_region_lo(self, g_army_move_cell_off);
                if (g_army_move_cell_terrain > 0x14 && g_army_move_cell_terrain < 0x1a)
                    army_move_region_hi(self, g_army_move_cell_off);
            } else if (code == 7) {              /* special tile */
                army_move_special(self, g_army_move_cell_off);
                r[M_SUBSTATE] = 0;
            }
        }
        return;
    }
}
