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
#include "engine_math.h"           /* engine_math_percent_div (FUN_00404dc1)     */
#include "save_schema_globals.h"   /* s_slot_0057cad4 — siege-scan cursor        */
#include "engine_path_field.h"     /* fill / trace / copy_record (raid planning) */
#include "engine_map_query.h"      /* box_any_rawflag_bit0 (FUN_0046cfbd)        */
#include "engine_army_move.h"      /* the executor's residue locals (#229 / #232) */

/* --- per-territory occupant record (g_per_territory_420, stride 0x1a4) ----- */
#define ASTRIDE      0x1a4
#define AREC(i)      (&g_per_territory_420[(i) * ASTRIDE])

#define A_OWNER      0x00     /* owning player id; 0 = dead record (u8)         */
#define A_CONTROL    0x01     /* control-type flag: 0 = local/viewer (u8)       */
#define A_POS_COL    0x0a     /* occupant map column (u8)                       */
#define A_POS_ROW    0x0b     /* occupant map row (u8)                          */
#define A_READY      0x06     /* march-ready flag: 0 → advance sub-state (u8)   */
#define A_SPRITE     0x07     /* display sprite frame, recomputed each tick (u8)*/
#define A_STATE      0x08     /* state byte → handler-table index (u8)          */
#define A_ANIM       0x09     /* walk-cycle phase selector (u8)                 */
#define A_CELL       0x0c     /* byte offset of the occupant's cell (i32)       */
#define A_REGION     0x10     /* effective-region colour key (u8)               */
#define A_FACING     0x1b     /* facing 0..7 → facing-frame table index (u8)    */
#define A_DEST_COL   0x16     /* path-trace goal column (u8) — the raider's dest */
#define A_DEST_ROW   0x17     /* path-trace goal row (u8)                        */
#define A_RAID_FLAG  0x150    /* raider path-retry flag, cleared on retarget(u8)*/
#define A_RAID_DEST  0x151    /* raider target realm id (u8)                    */
#define A_PLAN_PEND  0x14e    /* path-planning pending flag, cleared pre-plan(u8)*/
#define A_SUBSTATE   0x14c    /* sub-state: 2 → run movement executor (u8)      */
#define A_TICKS      0x154    /* redraw-priority byte, latched per state (u8)   */
#define A_TIMER      0x160    /* per-occupant countdown timer (i32)             */
#define A_HEAD       0x168    /* total head-count (i32)                         */

/* Siege-construction sub-record (besieging stacks only).  When a stack is
 * besieging a region (+0x199 != 0, the C_F199 siege link), state 1, it builds
 * three siege-equipment slots over successive turns.  Each slot is three i16:
 * { count, pct, value } at +0x182 stride 6 — `count` siege engines of that
 * type ordered, `value` build progress accumulated toward the per-engine target
 * (k_siege_build_target[slot] * count), `pct` = value as a percent of target
 * (clamped to 100 = built).  The aggregate remaining-work byte (+0x19c) is the
 * head-count fraction of all unfinished gaps, rounded up; 0 = fully built. */
#define A_SIEGE_LINK   0x199  /* besieged-region link (C_F199); 0 = not besieging*/
#define A_SIEGE_SLOTS  0x182  /* 3 × { i16 count, i16 pct, i16 value }, stride 6 */
#define A_SIEGE_REMAIN 0x19c  /* aggregate build-remaining byte; 0 = built (u8)  */
#define SIEGE_SLOT_COUNT 0    /* i16 offset within a slot: engines ordered       */
#define SIEGE_SLOT_PCT   2    /* i16 offset within a slot: percent built (0..100)*/
#define SIEGE_SLOT_VALUE 4    /* i16 offset within a slot: accumulated build     */

/* Facing-frame deltas (DAT_004d6a78 / DAT_004d6ab8, 8 × i32 each), indexed by
 * the occupant facing byte (+0x1b, a 0..7 compass direction). */
static const int32_t k_facing_march[8] = { 0, 1, 2, 1, 0, 1, 2, 1 };
static const int32_t k_facing_idle [8] = { 0, 1, 2, 3, 4, 5, 0, 1 };

/* Driver / handler shared state (retail globals). */
int     g_army_state_index;        /* DAT_005533e8 */
int     g_army_state_live_count;    /* DAT_00553f08 */
uint8_t g_army_state_abort;         /* DAT_0050a49e */

/* The executor's stack residue (quirk #229) seen from the driver.  FUN_00465d28
 * never initializes its waypoint locals, and the handlers call it from frames
 * of two depths: the state-1/2 handlers reserve one local (sub esp,4), the
 * state-3/4 handlers none.  Relative to the driver's entry esp the executor's
 * locals [ebp-4] dir / [ebp-8] code / [ebp-0xc] wp col / [ebp-0x10] wp row sit
 * at -0x34/-0x38/-0x3c/-0x40 under a state-1/2 handler and -0x30/-0x34/-0x38/
 * -0x3c under a state-3/4 one: an idle stack after a marching one reads the
 * marcher's outcome code and waypoint column as its waypoint (quirk #232).
 * g_army_state_res[k] = the dword at (driver entry esp) - 0x30 - 4k; the
 * executor's four locals move through it around each call.  A state-1 region
 * change leaves 0x4ad6c0's four locals there (handler_march).  What the other
 * callees' frames (settle region, unit remove) leave is not modelled.
 *
 * At the driver's entry the window is the AI step's imprint (quirk #238): the
 * driver's only caller, the frame driver 0x4b99c0, calls 0x49a010 right before
 * it at the same esp, and 0x49a010's callees' frames rewrite the window on
 * nearly every pass (saved ebx / esi / edi, return addresses, frame pointers:
 * large positive values; the executor only compares a waypoint with a map
 * coordinate, engine_path_direction_8way, so any large value behaves alike). */
uint32_t g_army_state_res[5];
static engine_army_move_fn g_move_fn;

/* the AI step 0x49a010's imprint on the window (driver e0 -0x30 .. -0x40): retail's
 * dominant values under rhost (its frame loop's ebx / esi / edi saved by 0x49a010's
 * callees: 13,787 of 13,965 passes of bot-s16-t20; the waypoint slots -0x38 / -0x3c /
 * -0x40 large in 13,947).  Not modelled: the siege scan's step (0x4a84ba → 0x4a8507)
 * leaves its loop bound 0x96 at -0x38 (18 passes). */
void engine_army_state_res_ai_step(void)
{
    g_army_state_res[0] = 6;          /* -0x30: never read before written */
    g_army_state_res[1] = 0x14a000;   /* -0x34: never read before written */
    g_army_state_res[2] = 0x13ff84c;  /* -0x38: a state-3/4 waypoint column */
    g_army_state_res[3] = 0x1400000;  /* -0x3c: waypoint column (1/2) / row (3/4) */
    g_army_state_res[4] = 0x1400000;  /* -0x40: a state-1/2 waypoint row */
}

static void run_move(int shallow, engine_event_queue_state_t *evq, int mp_active)
{
    uint32_t *w = &g_army_state_res[shallow ? 0 : 1];
    g_army_move_dir = w[0]; g_army_move_code = w[1];
    g_army_move_wp_col = w[2]; g_army_move_wp_row = w[3];
    g_move_fn(evq, mp_active);                     /* FUN_00465d28 seam */
    w[0] = g_army_move_dir; w[1] = g_army_move_code;
    w[2] = g_army_move_wp_col; w[3] = g_army_move_wp_row;
}

/* Per-cell descriptor grid (retail DAT_00522f90); the region key is byte +7 of
 * each 8-byte cell, so g_cells[A_CELL + 7] is the cell's region key. */
static const uint8_t      *g_cells;

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
        /* FUN_004ad6c0's locals (sub esp,0x10 at the executor's depth) overwrite
         * its four slots last: the loop counters j = 151 / k = 17 (var_C reused:
         * the region byte, then k), army 150's owner (movsx) and that army's
         * county's owner (src/rl_ctl.c rl_46521f, docs/findings/rl-ctl.md) */
        const uint8_t *a150 = AREC(150);
        uint32_t *w = &g_army_state_res[1];
        w[0] = 151;
        w[1] = (uint32_t)(int32_t)(int8_t)a150[A_OWNER];
        w[2] = 17;
        w[3] = g_per_army_768[a150[A_REGION] * 0x300 + 5];   /* county +5 (0x53f9b5) */
        r = AREC(g_army_state_index);
    }

    if (*(int32_t *)(r + A_TIMER) > 0)
        (*(int32_t *)(r + A_TIMER))--;

    if ((int8_t)r[A_SUBSTATE] == 2) {
        if (g_move_fn) run_move(0, evq, mp_active);
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
        run_move(0, evq, mp_active);
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
        run_move(1, evq, mp_active);
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

/* --- FUN_004a4f5b: arm movement for all occupants of a given state -------- */
int engine_army_state_arm_movement(unsigned state)
{
    int armed = 0;

    for (g_army_state_index = 1; g_army_state_index < 0x97; g_army_state_index++) {
        uint8_t *r = AREC(g_army_state_index);
        /* state byte zero-extended == arg (retail compares the full uint),
         * and the sub-state currently non-zero (a pending sub-state). */
        if ((unsigned)r[A_STATE] == state && r[A_SUBSTATE] != 0) {
            r[A_SUBSTATE] = 2;                     /* → run movement executor */
            armed = 1;
        }
    }
    return armed;
}

/* --- FUN_004a4e3d: arm movement for owner's local occupants of a state ---- */
int engine_army_state_arm_movement_owned(unsigned state, int owner)
{
    int armed = 0;

    for (g_army_state_index = 1; g_army_state_index < 0x97; g_army_state_index++) {
        uint8_t *r = AREC(g_army_state_index);
        /* state byte == arg, sub-state pending, owner byte (signed) == arg,
         * and the control-type byte (+0x01) clear (a local/viewer occupant). */
        if ((unsigned)r[A_STATE] == state && r[A_SUBSTATE] != 0 &&
            (int8_t)r[A_OWNER] == owner && r[A_CONTROL] == 0) {
            r[A_SUBSTATE] = 2;                     /* → run movement executor */
            if (r[A_STATE] == 1)                   /* settle-on-keep teardown */
                engine_combat_break_siege_link(g_army_state_index);
            armed = 1;
        }
    }
    return armed;
}

/* Per-engine-type build target multiplied by the slot's engine count to give
 * the full build value (retail DAT_004de440, 3 × i32). */
static const int32_t k_siege_build_target[3] = { 200, 200, 400 };

/* --- FUN_004a8507: advance one besieging stack's siege construction ------- */
int engine_army_state_siege_build_step(int idx)
{
    uint8_t *r = AREC(idx);

    /* Only live (owner != 0), state-1, actively-besieging stacks build, and
     * only while they have head-count (the per-tick build budget). */
    if (r[A_OWNER] == '\0')              return 0;
    if (r[A_STATE] != 1)                 return 0;
    if (r[A_SIEGE_LINK] == '\0')         return 0;

    int head = *(int32_t *)(r + A_HEAD);
    if (head < 1)                        return 0;

    /* Each slot is three i16 { count, pct, value } at +0x182 stride 6; mirror
     * retail's byte-offset addressing (`&DAT_0052f232 + i*6 + offset`). */
#define SLOT(i, field) (*(int16_t *)(r + A_SIEGE_SLOTS + (i) * 6 + (field)))

    /* An "active" slot has engines ordered (count > 0) and is not yet built
     * (pct < 100).  Split the build budget evenly across active slots. */
    int active = 0;
    for (int i = 0; i < 3; i++)
        if (SLOT(i, SIEGE_SLOT_COUNT) > 0 && SLOT(i, SIEGE_SLOT_PCT) < 100)
            active++;

    int share = head;
    if (active != 0)
        share = head / active;

    /* Pass 1: pour each active slot's even share into its build value (capped
     * at the slot's target), tracking how much budget is left unspent. */
    int budget = head;
    for (int i = 0; i < 3; i++) {
        if (SLOT(i, SIEGE_SLOT_COUNT) > 0 && SLOT(i, SIEGE_SLOT_PCT) < 100) {
            int16_t value  = SLOT(i, SIEGE_SLOT_VALUE);
            int     target = k_siege_build_target[i] * SLOT(i, SIEGE_SLOT_COUNT);
            int     spent;
            if (share < target - value) {
                SLOT(i, SIEGE_SLOT_VALUE) = (int16_t)(value + share);
                spent = share;
            } else {
                SLOT(i, SIEGE_SLOT_VALUE) = (int16_t)target;
                spent = target - value;
            }
            budget -= spent;
        }
    }

    /* Pass 2: top up the still-unfinished slots with whatever budget remains,
     * in slot order, until either each is full or the budget runs dry. */
    for (int i = 0; i < 3; i++) {
        if (SLOT(i, SIEGE_SLOT_COUNT) > 0 && SLOT(i, SIEGE_SLOT_PCT) < 100) {
            int value  = SLOT(i, SIEGE_SLOT_VALUE);
            int target = k_siege_build_target[i] * SLOT(i, SIEGE_SLOT_COUNT);
            if (value < target) {
                if (budget < target - value) {
                    SLOT(i, SIEGE_SLOT_VALUE) = (int16_t)(value + budget);
                    budget = 0;
                } else {
                    SLOT(i, SIEGE_SLOT_VALUE) = (int16_t)target;
                    budget -= target - value;
                }
            }
        }
    }

    /* Pass 3: recompute each active slot's percent, clamp built slots to 100,
     * and accumulate the remaining build gap across the still-unfinished ones. */
    int remaining = 0;
    for (int i = 0; i < 3; i++) {
        if (SLOT(i, SIEGE_SLOT_COUNT) > 0 && SLOT(i, SIEGE_SLOT_PCT) < 100) {
            int16_t value  = SLOT(i, SIEGE_SLOT_VALUE);
            int     target = k_siege_build_target[i] * SLOT(i, SIEGE_SLOT_COUNT);
            SLOT(i, SIEGE_SLOT_PCT) = (int16_t)engine_math_percent_div(value, target);
            if (SLOT(i, SIEGE_SLOT_PCT) < 100)
                remaining += target - value;
            else
                SLOT(i, SIEGE_SLOT_PCT) = 100;
        }
    }

    /* Remaining-work byte = ceil(remaining / head) (retail inlines the ceiling
     * as div + (mod != 0)); built when it reaches 0. */
    r[A_SIEGE_REMAIN] = (uint8_t)(remaining / head);
    if (remaining % head != 0)
        r[A_SIEGE_REMAIN] = (uint8_t)(r[A_SIEGE_REMAIN] + 1);

    return r[A_SIEGE_REMAIN] == '\0' ? 1 : 0;
#undef SLOT
}

/* --- FUN_004a84ba: scan for the next besieging stack that finished building - */
int engine_army_state_scan_siege_built(void)
{
    /* Resume from the persistent cursor (reset to 1 at phase-2 entry by the
     * retail dispatcher's 0x4a82b9 init) and advance one record at a time;
     * stop and report on the first stack whose siege construction just hit
     * 100% (engine_army_state_siege_build_step returns 1), leaving the cursor
     * parked on it for the assault trigger.  Returns 0 once the cursor walks
     * past the last record (index 150) with nothing built this pass. */
    while ((int)s_slot_0057cad4 <= 0x96) {
        if (engine_army_state_siege_build_step((int)s_slot_0057cad4) != 0)
            return 1;
        s_slot_0057cad4 = s_slot_0057cad4 + 1;
    }
    return 0;
}

/* Round-robin realm cursor for raider targeting (retail DAT_004e59dc — a plain
 * runtime scalar, not part of the save schema, shared across every record the
 * planner visits this turn). */
int g_raider_target_cursor;

/* Capital-cursor offsets within a per-realm record (g_per_army_768, stride
 * 0x300): the realm's capital map cell col/row (retail DAT_0053fa1c/1d). */
#define RAID_RSTRIDE  0x300
#define RAID_CAP_COL  0x6c
#define RAID_CAP_ROW  0x6d

/* --- FUN_004ac5ba: assign a roving stack its target realm + capital dest ---- */
int engine_army_state_raid_assign_target(int idx)
{
    uint8_t *r = AREC(idx);

    /* (Re)pick a target when the global planner is in force-mode 2 or the stack
     * has no target yet: step the shared realm cursor (wrapping at the realm
     * count), and if it lands on the stack's own region advance once more. */
    if (s_slot_0057c934 == 2 || r[A_RAID_DEST] == '\0') {
        g_raider_target_cursor++;
        if ((int)s_slot_0056d5dc < g_raider_target_cursor)
            g_raider_target_cursor = 1;
        if ((uint8_t)r[A_REGION] == g_raider_target_cursor &&
            (g_raider_target_cursor++, (int)s_slot_0056d5dc < g_raider_target_cursor))
            g_raider_target_cursor = 1;
        r[A_RAID_DEST] = (uint8_t)g_raider_target_cursor;
    }

    /* Point the stack's destination at the target realm's capital.  Retail reads
     * the capital through the *shared* cursor (not the per-record target byte),
     * so a stack that kept a prior target picks up whichever realm the cursor
     * was last left on this pass — reproduced verbatim (see engine-quirks). */
    if (r[A_RAID_DEST] != '\0') {
        r[A_RAID_FLAG] = 0;
        r[A_DEST_COL] = g_per_army_768[g_raider_target_cursor * RAID_RSTRIDE + RAID_CAP_COL];
        r[A_DEST_ROW] = g_per_army_768[g_raider_target_cursor * RAID_RSTRIDE + RAID_CAP_ROW];
    }
    return r[A_RAID_DEST] != '\0';
}

/* --- FUN_004ac499: plan raider movement for all settling stacks ------------- */
void engine_army_state_plan_raids(void)
{
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = AREC(i);
        if ((unsigned)r[A_STATE] != 2)
            continue;
        if (engine_army_state_raid_assign_target(i) == 0)
            continue;

        r[A_PLAN_PEND] = 0;
        /* Flood a cost field from the stack, trace it to the target capital, and
         * if reachable commit the path into the stack's waypoints + arm it. */
        engine_path_field_fill(0, (int8_t)r[A_POS_COL], (int8_t)r[A_POS_ROW], 0);
        if (engine_path_field_trace(0, (int8_t)r[A_DEST_COL], (int8_t)r[A_DEST_ROW]) != 0) {
            engine_path_field_copy_record(0, i);
            r[A_SUBSTATE] = 1;
        }
    }
}

/* Move-executor destination + path-trace goal fields used by the phase-3/6
 * march planners: the executor's own final-destination bytes (retail
 * M_DEST_COL/ROW) plus the path-trace goal pair the planner floods toward —
 * the same +0x16/+0x17 the raider planner targets (A_DEST_COL/ROW). */
#define A_MOVE_DEST_COL 0x14   /* executor final destination col (u8; M_DEST_COL)*/
#define A_MOVE_DEST_ROW 0x15   /* executor final destination row (u8; M_DEST_ROW)*/
#define A_PATH_COL      0x16   /* path-trace goal col (u8)                        */
#define A_PATH_ROW      0x17   /* path-trace goal row (u8)                        */
#define A_TARGET_REALM  0x167  /* target realm id; capital is the march goal (u8) */

/* Flood/trace a path-field route from a stack's cell to its goal and, if it
 * reaches in under 0x96 (150) waypoints, commit the route + arm the executor.
 * Retail floods conservatively first (blocked_mode 1 — costly cells heavily
 * penalised) and, only if that route is degenerate (>= 150 waypoints, i.e. it
 * hit the trace cap), re-floods permissively (blocked_mode 0) and retries once.
 * Shared by the phase-3 and phase-6 planners. */
static void plan_route_to_goal(uint8_t *r, int idx)
{
    engine_path_field_fill(0, (int8_t)r[A_POS_COL], (int8_t)r[A_POS_ROW], 1);
    if (engine_path_field_trace(0, (int8_t)r[A_PATH_COL], (int8_t)r[A_PATH_ROW]) == 0)
        return;
    if (engine_path_field_wp_count(0) < 0x96) {
        engine_path_field_copy_record(0, idx);
        r[A_SUBSTATE] = 1;
        return;
    }
    engine_path_field_fill(0, (int8_t)r[A_POS_COL], (int8_t)r[A_POS_ROW], 0);
    if (engine_path_field_trace(0, (int8_t)r[A_PATH_COL], (int8_t)r[A_PATH_ROW]) != 0) {
        engine_path_field_copy_record(0, idx);
        r[A_SUBSTATE] = 1;
    }
}

/* --- FUN_00429418: phase-3 — route idle (state 4) stacks to a target capital - */
void engine_army_state_plan_capital_marches(void)
{
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = AREC(i);
        if ((unsigned)r[A_STATE] != 4)
            continue;

        /* Goal = the target realm's (+0x167) capital cell (g_per_army_768
         * +0x6c/+0x6d).  Mirror it into both the executor destination and the
         * path-trace goal, then republish the target byte + clear the planning
         * flags, exactly as retail. */
        uint8_t realm = r[A_TARGET_REALM];
        r[A_MOVE_DEST_COL] = g_per_army_768[realm * RAID_RSTRIDE + RAID_CAP_COL];
        r[A_PATH_COL]      = r[A_MOVE_DEST_COL];
        r[A_MOVE_DEST_ROW] = g_per_army_768[realm * RAID_RSTRIDE + RAID_CAP_ROW];
        r[A_PATH_ROW]      = r[A_MOVE_DEST_ROW];
        r[A_RAID_DEST]     = realm;
        r[A_RAID_FLAG]     = 0;
        r[A_PLAN_PEND]     = 0;

        plan_route_to_goal(r, i);
    }
}

/* Find-nearest scratch (retail DAT_00569570 col / DAT_00569578 row): the settle
 * cell FUN_0046cfbd surfaces from its box scan, meaningful only immediately
 * after a hit.  Retail keeps these in shared scalar globals; here they back the
 * single ported consumer (the phase-6 planner).  Promote to a shared global if
 * another FUN_00428007 caller (e.g. FUN_004292af) is ported. */
static int s_settle_col, s_settle_row;

/* Port of FUN_00428007 (113 B, __cdecl bool).  Probe radii 1..3 around realm
 * `realm`'s capital cell (g_per_army_768 +0x6c/+0x6d) for an unlinked,
 * bit0-flagged cell (engine_map_query_box_any_rawflag_bit0 = FUN_0046cfbd).  On
 * the first hit it surfaces that cell in s_settle_col/row and returns 1; returns
 * 0 if no radius yields one (leaving the scratch untouched, as retail does). */
static int find_settle_spot_near_realm(int realm)
{
    int col = g_per_army_768[realm * RAID_RSTRIDE + RAID_CAP_COL];
    int row = g_per_army_768[realm * RAID_RSTRIDE + RAID_CAP_ROW];
    for (int radius = 1; radius < 4; radius++)
        if (engine_map_query_box_any_rawflag_bit0(col, row, radius,
                                                  &s_settle_col, &s_settle_row))
            return 1;
    return 0;
}

/* Expose FUN_00428007's result for the other ported caller (FUN_004292af, the AI
 * caravan dispatcher in engine_army_ai): probe for a settle spot near `realm`'s
 * capital and, on a hit, return 1 and the matched cell via the out-params.  This is
 * the "promote to a shared global" the comment above anticipated — done via
 * out-params (the project's scratch-as-out-param convention) rather than
 * resurrecting DAT_00569570/78 as a global. */
int engine_army_state_find_settle_spot(int realm, int *col, int *row)
{
    if (!find_settle_spot_near_realm(realm))
        return 0;
    *col = s_settle_col;
    *row = s_settle_row;
    return 1;
}

#define A_ADJ_CURSOR  0x164  /* round-robin category-bucket cursor 0..15 (u8)    */

/* --- FUN_004280e9: phase-6 — settle idle (state 3) stacks near a neighbour --- */
void engine_army_state_plan_settle_marches(void)
{
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = AREC(i);
        if ((unsigned)r[A_STATE] != 3)
            continue;

        /* A stack flagged "arrived" (+0x150) picks a fresh settle target: scan up
         * to 16 entries of category-bucket row (i-1) from the round-robin cursor
         * (+0x164, wrapping at 16), taking the first in-range (<= realm count)
         * realm id.  If one exists AND a settle cell is found near its capital,
         * route there and clear the arrived flag; if no cell, skip the record. */
        if (r[A_RAID_FLAG] != 0) {
            unsigned realm = 0;
            for (int n = 0; realm == 0 && n < 0x10; n++) {
                realm = s_slot_00567970[(i - 1) * 0x10 + r[A_ADJ_CURSOR]];
                if ((int)s_slot_0056d5dc < (int)realm)
                    realm = 0;
                r[A_ADJ_CURSOR]++;
                if (r[A_ADJ_CURSOR] > 0xf)
                    r[A_ADJ_CURSOR] = 0;
            }
            if (realm != 0) {
                if (find_settle_spot_near_realm((int)realm) == 0)
                    continue;            /* retail goto LAB_004280fe (next record) */
                r[A_MOVE_DEST_COL] = (uint8_t)s_settle_col;
                r[A_PATH_COL]      = r[A_MOVE_DEST_COL];
                r[A_MOVE_DEST_ROW] = (uint8_t)s_settle_row;
                r[A_PATH_ROW]      = r[A_MOVE_DEST_ROW];
                r[A_RAID_DEST]     = (uint8_t)realm;
                r[A_RAID_FLAG]     = 0;
            }
        }
        if (r[A_RAID_FLAG] == 0) {
            r[A_PLAN_PEND] = 0;
            plan_route_to_goal(r, i);
        }
    }
}
