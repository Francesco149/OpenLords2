/*
 * Tests for engine_army_move.c — the strategic-map army-movement
 * destination-cell evaluator (FUN_00466893 directional pick / FUN_00466c3c
 * cell-flag classify / FUN_004658c1 occupied-cell resolve).
 *
 * Operates on the per-territory occupant block (g_per_territory_420, stride
 * 0x1a4) and the shared 8-byte cell grid (64 columns, row stride 0x200 bytes;
 * cell fields +0 terrain, +1 flags, +5 occupant index, +7 region key).  The
 * current occupant is engine_army_state's g_army_state_index; the cell's
 * occupant is g_combat_active_terr, latched by classify_cell.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_army_move.h"
#include "engine_army_state.h"     /* g_army_state_index, g_army_state_abort */
#include "engine_combat.h"         /* g_combat_active_terr                   */
#include "engine_diplomacy.h"
#include "engine_event_queue.h"
#include "engine_territory.h"      /* engine_territory_bind_cells (decay sink)*/
#include "engine.h"                /* engine_set_current_screen (battle 0x12)  */
#include "engine_turn.h"           /* g_sim_turn_progress (forced arrival)     */
#include "save.h"
#include "save_schema_globals.h"   /* s_slot_0057c8cc (local player) */

#define MREC(i)   (&g_per_territory_420[(i) * 0x1a4])
#define SREC(s)   (&g_player_strategic_352[(s) * 0x160])
#define I32(p,o)  (*(int32_t *)((p) + (o)))

#define M_OWNER   0x00
#define M_F01     0x01
#define M_STATE   0x08
#define M_DIR     0x09
#define M_COL     0x0a
#define M_ROW     0x0b
#define M_CELL    0x0c
#define M_F1A     0x1a
#define M_WP      0x1c
#define M_ROUGH   0x14d
#define M_F14E    0x14e
#define M_F152    0x152

#define D_LORD    0x81

#define CELL_ROW  0x200

static uint8_t g_test_cells[0x8000];   /* 64 rows × 0x200 bytes */

static void mv_reset(void)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_test_cells, 0, sizeof g_test_cells);
    engine_army_move_bind_cells(g_test_cells);
    s_slot_0057c8cc = 1;
    g_army_state_index = 1;
    g_army_state_abort = 0;
    g_combat_active_terr = 0;
}

/* Place occupant `occ` at grid (col,row); returns its cell byte offset. */
static int place(int occ, int col, int row)
{
    uint8_t *r = MREC(occ);
    int off = row * CELL_ROW + col * 8;
    r[M_COL] = (uint8_t)col;
    r[M_ROW] = (uint8_t)row;
    I32(r, M_CELL) = off;
    return off;
}

/* --- FUN_00466893: directional neighbour offsets -------------------------- */
int test_engine_army_move_dir_offsets(void)
{
    static const int delta[8] = {
        -CELL_ROW,            /* 0 N  */
        -CELL_ROW + 8,        /* 1 NE */
                   8,         /* 2 E  */
         CELL_ROW + 8,        /* 3 SE */
         CELL_ROW,            /* 4 S  */
         CELL_ROW - 8,        /* 5 SW */
                  -8,         /* 6 W  */
        -CELL_ROW - 8,        /* 7 NW */
    };
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    for (int dir = 0; dir < 8; dir++) {
        mv_reset();
        int base = place(1, 10, 10);
        /* Empty open neighbour → classify returns 1 and latches the offset. */
        T_ASSERT_EQ_I(engine_army_move_eval_dir(dir, &evq, 0), 1);
        T_ASSERT_EQ_I(g_army_move_cell_off, base + delta[dir]);
    }
    return 0;
}

/* --- FUN_00466893: grid-edge guards return 0 (no classify) ---------------- */
int test_engine_army_move_dir_edges(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    /* Top-left corner (col 0, row 0): only E(2)/S(4)/SE(3) are in-grid. */
    mv_reset(); place(1, 0, 0);
    T_ASSERT_EQ_I(engine_army_move_eval_dir(0, &evq, 0), 0);   /* N  */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(1, &evq, 0), 0);   /* NE */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(6, &evq, 0), 0);   /* W  */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(7, &evq, 0), 0);   /* NW */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(5, &evq, 0), 0);   /* SW (col 0) */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 1);   /* E  ok */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(4, &evq, 0), 1);   /* S  ok */

    /* Bottom-right corner (col 63, row 63): only N(0)/W(6)/NW(7) in-grid. */
    mv_reset(); place(1, 0x3f, 0x3f);
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 0);   /* E  */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(4, &evq, 0), 0);   /* S  */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(3, &evq, 0), 0);   /* SE */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(0, &evq, 0), 1);   /* N  ok */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(6, &evq, 0), 1);   /* W  ok */
    return 0;
}

/* --- FUN_00466c3c: empty-cell flag classification ------------------------- */
static int classify_e(uint8_t flags, uint8_t terrain)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    int base = place(1, 10, 10);
    int e = base + 8;                          /* the cell to the east */
    g_test_cells[e + 1] = flags;
    g_test_cells[e + 0] = terrain;
    g_test_cells[e + 5] = 0;                   /* empty */
    return engine_army_move_eval_dir(2, &evq, 0);
}

int test_engine_army_move_classify_flags(void)
{
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x00, 0), 1);   /* open       */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x01, 0), 3);   /* rough      */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x40, 0), 5);   /* combat     */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x80, 0), 6);   /* region-evt */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x10, 0), 7);   /* special    */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x20, 0), 8);   /* impassable */
    /* priority: bit0 outranks bit6; bit6 outranks bit5. */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x41, 0), 3);
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x60, 0), 5);
    return 0;
}

/* classify latches the cell's flags / region / terrain scratch. */
int test_engine_army_move_classify_scratch(void)
{
    mv_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    int base = place(1, 10, 10);
    int e = base + 8;
    g_test_cells[e + 1] = 0x40;     /* flags  */
    g_test_cells[e + 7] = 0x09;     /* region */
    g_test_cells[e + 0] = 0x05;     /* terrain*/
    engine_army_move_eval_dir(2, &evq, 0);
    T_ASSERT_EQ_U(g_army_move_cell_flags, 0x40);
    T_ASSERT_EQ_U(g_army_move_cell_region, 0x09);
    T_ASSERT_EQ_U(g_army_move_cell_terrain, 0x05);
    T_ASSERT_EQ_I(g_army_move_cell_off, e);
    return 0;
}

/* The 0x80 region-event flag is suppressed on terrain 0x14 and when the stack
 * carries its +0x14e suppressor — falling through to the next-priority bit. */
int test_engine_army_move_classify_event_suppress(void)
{
    /* terrain 0x14 clears 0x80 → no other bits → open (1). */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x80, 0x14), 1);
    /* terrain 0x14 clears 0x80 but bit4 remains → special (7). */
    mv_reset(); T_ASSERT_EQ_I(classify_e(0x90, 0x14), 7);
    /* the +0x14e suppressor on the moving stack clears 0x80 too. */
    mv_reset();
    MREC(1)[M_F14E] = 1;
    T_ASSERT_EQ_I(classify_e(0x80, 0x00), 1);
    return 0;
}

/* --- FUN_004658c1: occupied-cell resolve --------------------------------- */

/* Put occupant index `other` on the cell east of the moving stack. */
static int place_occupied(int other, uint8_t flags)
{
    int base = place(1, 10, 10);
    int e = base + 8;
    g_test_cells[e + 1] = flags;
    g_test_cells[e + 5] = (uint8_t)other;
    return e;
}

/* An idle moving stack (state 3/4) never interacts — returns the base code. */
int test_engine_army_move_self_idle_no_interact(void)
{
    mv_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    place_occupied(2, 0x01);          /* rough flag → base code 3 */
    MREC(1)[M_STATE] = 3;             /* self idle */
    MREC(1)[M_OWNER] = 2;
    MREC(2)[M_STATE] = 1; MREC(2)[M_OWNER] = 3;
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 3);
    T_ASSERT_EQ_U(MREC(2)[M_OWNER], 3);   /* untouched */
    return 0;
}

/* A target that is itself idle (state 3) is passed over. */
int test_engine_army_move_other_idle_no_interact(void)
{
    mv_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    place_occupied(2, 0x00);
    MREC(1)[M_STATE] = 1; MREC(1)[M_OWNER] = 2;
    MREC(2)[M_STATE] = 3; MREC(2)[M_OWNER] = 3;
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 1);
    return 0;
}

/* Hostile collision with one's own lord does NOT engage (no abort, base code). */
int test_engine_army_move_lord_no_engage(void)
{
    mv_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    place_occupied(2, 0x00);
    MREC(1)[M_STATE] = 1; MREC(1)[M_OWNER] = 2;
    MREC(2)[M_STATE] = 1; MREC(2)[M_OWNER] = 3;
    SREC(2)[D_LORD] = 3;              /* player 2's lord is 3 → no fight */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 1);
    T_ASSERT_EQ_U(g_army_state_abort, 0);
    return 0;
}

/* Same-owner stacks merge, but only with the expected partner: a target that
 * carries a live order (+1) whose index isn't the mover's +0x152 is skipped. */
int test_engine_army_move_merge_partner_guard(void)
{
    mv_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    place_occupied(2, 0x00);
    MREC(1)[M_STATE] = 1; MREC(1)[M_OWNER] = 2; MREC(1)[M_F152] = 9; /* != 2 */
    MREC(2)[M_STATE] = 1; MREC(2)[M_OWNER] = 2; MREC(2)[M_F01] = 1;
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 1);
    T_ASSERT_EQ_U(MREC(2)[M_OWNER], 2);   /* not merged away */
    return 0;
}

/* The merge-eligibility guard (+0x1a == 4) also skips the merge. */
int test_engine_army_move_merge_eligibility_guard(void)
{
    mv_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    place_occupied(2, 0x00);
    MREC(1)[M_STATE] = 1; MREC(1)[M_OWNER] = 2; MREC(1)[M_F1A] = 4;
    MREC(2)[M_STATE] = 1; MREC(2)[M_OWNER] = 2; MREC(2)[M_F01] = 0;
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 1);
    T_ASSERT_EQ_U(MREC(2)[M_OWNER], 2);
    return 0;
}

/* Overrunning a hostile garrison (target state 4) removes it and reports
 * arrival (code 10). */
int test_engine_army_move_overrun_garrison(void)
{
    mv_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    place_occupied(2, 0x00);
    MREC(1)[M_STATE] = 1; MREC(1)[M_OWNER] = 2;
    MREC(2)[M_STATE] = 4; MREC(2)[M_OWNER] = 3;   /* enemy garrison */
    T_ASSERT_EQ_I(engine_army_move_eval_dir(2, &evq, 0), 10);
    T_ASSERT_EQ_U(MREC(2)[M_OWNER], 0);           /* garrison removed */
    return 0;
}

/* --- FUN_00466d84: commit the move into the chosen neighbour -------------- */
static int g_last_sfx;
static void cap_sfx(int id) { g_last_sfx = id; }

/* The commit-step uses the GLOBAL overworld grid for the map link/unlink
 * (engine_unit_map_*), so bind the evaluator to it too. */
static void cm_reset(void)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    engine_army_move_bind_cells(g_overworld_map);
    engine_army_move_bind_sfx_fn(cap_sfx);
    g_army_state_index = 1;
    g_last_sfx = -1;
}

int test_engine_army_move_commit_directions(void)
{
    /* expected (col, row, cell) from base (10, 10, 0x1450) per direction. */
    static const struct { int col, row, cell; } exp[8] = {
        { 10,  9, 0x1250 },  /* 0 N  */
        { 11,  9, 0x1258 },  /* 1 NE */
        { 11, 10, 0x1458 },  /* 2 E  */
        { 11, 11, 0x1658 },  /* 3 SE */
        { 10, 11, 0x1650 },  /* 4 S  */
        {  9, 11, 0x1648 },  /* 5 SW */
        {  9, 10, 0x1448 },  /* 6 W  */
        {  9,  9, 0x1248 },  /* 7 NW */
    };
    for (int dir = 0; dir < 8; dir++) {
        cm_reset();
        uint8_t *r = MREC(1);
        r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
        r[M_STATE] = 1; r[M_DIR] = (uint8_t)dir;
        engine_army_move_commit_step();
        T_ASSERT_EQ_I((int8_t)r[M_COL], exp[dir].col);
        T_ASSERT_EQ_I((int8_t)r[M_ROW], exp[dir].row);
        T_ASSERT_EQ_I(I32(r, M_CELL), exp[dir].cell);
    }
    return 0;
}

/* A direction byte > 7 commits nothing. */
int test_engine_army_move_commit_out_of_range(void)
{
    cm_reset();
    uint8_t *r = MREC(1);
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 1; r[M_DIR] = 8;
    engine_army_move_commit_step();
    T_ASSERT_EQ_I((int8_t)r[M_COL], 10);
    T_ASSERT_EQ_I((int8_t)r[M_ROW], 10);
    T_ASSERT_EQ_I(I32(r, M_CELL), 0x1450);
    return 0;
}

/* The sticky rough flag (+0x14d) tracks the new cell's movement bit0. */
int test_engine_army_move_commit_rough_flag(void)
{
    cm_reset();
    uint8_t *r = MREC(1);
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 1; r[M_DIR] = 2;                  /* E → 0x1458 */
    g_overworld_map[0x1458 + 1] = 0x01;            /* rough */
    engine_army_move_commit_step();
    T_ASSERT_EQ_U(r[M_ROUGH], 1);

    cm_reset();
    r = MREC(1);
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 1; r[M_DIR] = 2;
    g_overworld_map[0x1458 + 1] = 0x40;            /* not bit0 */
    engine_army_move_commit_step();
    T_ASSERT_EQ_U(r[M_ROUGH], 0);
    return 0;
}

/* The per-state footstep cue: idle(3/4)=0xb, settle(2)=5, march(1)=0xc. */
int test_engine_army_move_commit_sfx(void)
{
    cm_reset();
    uint8_t *r = MREC(1);
    r[M_DIR] = 2; r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;

    r[M_STATE] = 1; engine_army_move_commit_step(); T_ASSERT_EQ_I(g_last_sfx, 0xc);
    cm_reset(); r = MREC(1); r[M_DIR] = 2; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 2; engine_army_move_commit_step(); T_ASSERT_EQ_I(g_last_sfx, 0x5);
    cm_reset(); r = MREC(1); r[M_DIR] = 2; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 3; engine_army_move_commit_step(); T_ASSERT_EQ_I(g_last_sfx, 0xb);
    cm_reset(); r = MREC(1); r[M_DIR] = 2; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 4; engine_army_move_commit_step(); T_ASSERT_EQ_I(g_last_sfx, 0xb);
    return 0;
}

/* --- FUN_00469e0e: terrain-passability predicate ------------------------- */
int test_engine_army_move_terrain_passable(void)
{
    mv_reset();
    int off = 0x100;
    g_test_cells[off] = 0x00; T_ASSERT_EQ_I(engine_army_move_terrain_passable(off), 0);
    g_test_cells[off] = 0x01; T_ASSERT_EQ_I(engine_army_move_terrain_passable(off), 0);
    g_test_cells[off] = 0x02; T_ASSERT_EQ_I(engine_army_move_terrain_passable(off), 1);
    g_test_cells[off] = 0x16; T_ASSERT_EQ_I(engine_army_move_terrain_passable(off), 1);
    g_test_cells[off] = 0x17; T_ASSERT_EQ_I(engine_army_move_terrain_passable(off), 0);
    g_test_cells[off] = 0xff; T_ASSERT_EQ_I(engine_army_move_terrain_passable(off), 0);
    return 0;
}

/* --- FUN_0046634d: stall/retry hysteresis (step-eval disarmed) ----------- */
#define M_FACING    0x1b
#define M_STALL     0x149
#define M_RETRY     0x14a
#define M_FLAGS14B  0x14b
#define M_PROGRESS  0x153

int test_engine_army_move_classify_hysteresis(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    /* No wrap: a single retry increment under-budget returns 1, no stall bump. */
    mv_reset();
    uint8_t *r = MREC(1);
    r[M_FLAGS14B] = 0; r[M_ROUGH] = 0; r[M_RETRY] = 0; r[M_STALL] = 0;
    T_ASSERT_EQ_I(engine_army_move_classify_step(0, &evq, 0), 1);
    T_ASSERT_EQ_U(r[M_RETRY], 1);
    T_ASSERT_EQ_I((int8_t)r[M_STALL], 0);

    /* Wrap (budget 3, retry hits 4): reset retry, bump facing, stall += 2 (SP). */
    mv_reset(); r = MREC(1);
    r[M_FLAGS14B] = 0; r[M_ROUGH] = 0; r[M_RETRY] = 3; r[M_STALL] = 0; r[M_FACING] = 0;
    T_ASSERT_EQ_I(engine_army_move_classify_step(0, &evq, 0), 1);
    T_ASSERT_EQ_U(r[M_RETRY], 0);
    T_ASSERT_EQ_U(r[M_FACING], 1);
    T_ASSERT_EQ_I((int8_t)r[M_STALL], 2);

    /* Multiplayer wrap advances the stall accumulator by 4 instead of 2. */
    mv_reset(); r = MREC(1);
    r[M_FLAGS14B] = 0; r[M_ROUGH] = 0; r[M_RETRY] = 3; r[M_STALL] = 0;
    engine_army_move_classify_step(0, &evq, 1);
    T_ASSERT_EQ_I((int8_t)r[M_STALL], 4);

    /* Rough flag drops the budget to 0, so the first increment wraps. */
    mv_reset(); r = MREC(1);
    r[M_FLAGS14B] = 0; r[M_ROUGH] = 1; r[M_RETRY] = 0; r[M_STALL] = 0;
    engine_army_move_classify_step(0, &evq, 0);
    T_ASSERT_EQ_U(r[M_RETRY], 0);
    T_ASSERT_EQ_I((int8_t)r[M_STALL], 2);

    /* Reaching the 0x10 stall ceiling arms the step-eval and returns 2. */
    mv_reset(); r = MREC(1);
    r[M_FLAGS14B] = 0; r[M_ROUGH] = 0; r[M_RETRY] = 3; r[M_STALL] = 0x0e; r[M_FACING] = 5;
    T_ASSERT_EQ_I(engine_army_move_classify_step(0, &evq, 0), 2);
    T_ASSERT_EQ_U(r[M_FLAGS14B] & 1, 1);
    T_ASSERT_EQ_I((int8_t)r[M_STALL], 0);
    T_ASSERT_EQ_U(r[M_FACING], 0);
    return 0;
}

/* --- FUN_0046634d: armed step → open commit ------------------------------ */
int test_engine_army_move_classify_commit_open(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    cm_reset();                       /* binds the global grid for commit link */
    g_army_state_abort = 0;
    uint8_t *r = MREC(1);
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450; r[M_STATE] = 1;
    r[M_FLAGS14B] = 1; r[M_ROUGH] = 0; r[M_PROGRESS] = 0;   /* armed, east open */

    T_ASSERT_EQ_I(engine_army_move_classify_step(2, &evq, 0), 1);
    T_ASSERT_EQ_U(r[M_FLAGS14B] & 1, 0);    /* step-eval disarmed again      */
    T_ASSERT_EQ_U(r[M_DIR], 2);             /* committed direction latched   */
    T_ASSERT_EQ_U(r[M_PROGRESS], 3);        /* +3 for an open step           */
    T_ASSERT_EQ_I((int8_t)r[M_STALL], 1);   /* stall accumulator re-primed   */
    T_ASSERT_EQ_I((int8_t)r[M_COL], 11);    /* stack stepped east            */
    return 0;
}

/* --- FUN_0046634d: armed step → rough commit ----------------------------- */
int test_engine_army_move_classify_commit_rough(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    cm_reset();
    g_army_state_abort = 0;
    uint8_t *r = MREC(1);
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450; r[M_STATE] = 1;
    r[M_FLAGS14B] = 1; r[M_PROGRESS] = 0;
    g_overworld_map[0x1458 + 1] = 0x01;     /* east cell carries rough bit0  */

    T_ASSERT_EQ_I(engine_army_move_classify_step(2, &evq, 0), 3);
    T_ASSERT_EQ_U(r[M_ROUGH], 1);           /* sticky rough flag set         */
    T_ASSERT_EQ_U(r[M_PROGRESS], 1);        /* +1 for a rough step           */
    T_ASSERT_EQ_I((int8_t)r[M_COL], 11);    /* still committed               */
    return 0;
}

/* --- FUN_0046634d: combat / region-event / special pass straight up ------ */
int test_engine_army_move_classify_passthrough(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    struct { uint8_t flags; int code; } cases[] = {
        { 0x40, 5 },   /* combat       */
        { 0x80, 6 },   /* region-event */
        { 0x10, 7 },   /* special      */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        mv_reset();
        uint8_t *r = MREC(1);
        r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450; r[M_STATE] = 1;
        r[M_FLAGS14B] = 1;
        g_test_cells[0x1458 + 1] = cases[i].flags;
        T_ASSERT_EQ_I(engine_army_move_classify_step(2, &evq, 0), cases[i].code);
        T_ASSERT_EQ_U(r[M_FLAGS14B] & 1, 1);     /* still armed (not committed) */
        T_ASSERT_EQ_I((int8_t)r[M_COL], 10);     /* stack did not move          */
    }
    return 0;
}

/* --- FUN_0046634d: a grid-edge step yields 0 and commits nothing --------- */
int test_engine_army_move_classify_grid_edge(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    mv_reset();
    uint8_t *r = MREC(1);
    r[M_COL] = 0; r[M_ROW] = 0; I32(r, M_CELL) = 0; r[M_STATE] = 1;
    r[M_FLAGS14B] = 1;
    T_ASSERT_EQ_I(engine_army_move_classify_step(0, &evq, 0), 0);   /* N off top */
    T_ASSERT_EQ_I((int8_t)r[M_COL], 0);
    return 0;
}

/* --- FUN_0046673c: impassable foreign-region step → scorch + relation hit  */
static int g_last_alert;
static void cap_alert(int id) { g_last_alert = id; }

int test_engine_army_move_classify_blocked(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    cm_reset();
    g_army_state_abort = 0;
    engine_territory_bind_cells(g_overworld_map);   /* decay's set_cell_type sink */
    engine_territory_bind_cell_roster(NULL);        /* clear_cell_score → no-op   */
    engine_territory_bind_cell_score_table(NULL);
    engine_army_move_bind_alert_sfx_fn(cap_alert);
    g_last_alert = -1;

    uint8_t *r = MREC(1);
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 1; r[M_OWNER] = 2; r[M_FLAGS14B] = 1; r[M_PROGRESS] = 0;
    g_player_strategic_352[2 * 0x160 + 0x05] = 1;   /* mover is an active player  */

    int e = 0x1458;                                  /* the cell to the east       */
    g_overworld_map[e + 1] = 0x20;                   /* impassable (bit5)          */
    g_overworld_map[e + 0] = 5;                      /* real land terrain          */
    g_overworld_map[e + 7] = 9;                      /* region key 9               */
    g_overworld_map[e + 5] = 0;                      /* empty                      */

    uint8_t *rg = &g_per_army_768[9 * 0x300];
    rg[0x05]  = 3;        /* foreign region owner                                  */
    rg[0x201] = 2;        /* mid census  (≤ reserve → drain path)                  */
    rg[0x206] = 4;        /* mid reserve                                           */
    I32(rg, 0x244) = 1000;/* mid stock pool                                        */
    I32(rg, 0x234) = 0;   /* mid stock accumulator                                 */

    int res = engine_army_move_classify_step(2, &evq, 0);

    T_ASSERT_EQ_I(res, 1);                  /* impassable (8) handled → open (1)  */
    T_ASSERT_EQ_I(g_last_alert, 3);         /* blocked-step cue played            */
    T_ASSERT_EQ_U(r[M_PROGRESS], 6);        /* +3 (blocked) +3 (classifier)       */
    /* decay: frac = 100/4 = 25; amt = 1000*25/100 = 250. */
    T_ASSERT_EQ_I(I32(rg, 0x244), 750);
    T_ASSERT_EQ_I(I32(rg, 0x234), 250);
    T_ASSERT_EQ_U(rg[0x206], 3);            /* reserve drawn down                 */
    T_ASSERT_EQ_U(rg[0x201], 1);            /* mid census decremented             */
    T_ASSERT_EQ_U(g_overworld_map[e + 0], 0); /* cell flattened (terrain → 0)     */
    T_ASSERT_EQ_I((int8_t)r[M_COL], 11);    /* and the stack pushes through       */
    return 0;
}

/* --- FUN_0046673c: same-region / sea-terrain block does NOT scorch ------- */
int test_engine_army_move_classify_blocked_friendly(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    cm_reset();
    g_army_state_abort = 0;
    engine_territory_bind_cells(g_overworld_map);
    engine_territory_bind_cell_roster(NULL);
    engine_territory_bind_cell_score_table(NULL);
    engine_army_move_bind_alert_sfx_fn(cap_alert);
    g_last_alert = -1;

    uint8_t *r = MREC(1);
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_STATE] = 1; r[M_OWNER] = 3; r[M_FLAGS14B] = 1; r[M_PROGRESS] = 0;

    int e = 0x1458;
    g_overworld_map[e + 1] = 0x20;          /* impassable                         */
    g_overworld_map[e + 0] = 5;             /* land terrain                       */
    g_overworld_map[e + 7] = 9;
    uint8_t *rg = &g_per_army_768[9 * 0x300];
    rg[0x05]  = 3;                          /* SAME owner as the mover            */
    rg[0x201] = 2; rg[0x206] = 4; I32(rg, 0x244) = 1000;

    T_ASSERT_EQ_I(engine_army_move_classify_step(2, &evq, 0), 1);
    T_ASSERT_EQ_I(g_last_alert, -1);        /* no cue                             */
    T_ASSERT_EQ_I(I32(rg, 0x244), 1000);    /* stock untouched                    */
    T_ASSERT_EQ_U(g_overworld_map[e + 0], 5); /* cell not flattened               */
    T_ASSERT_EQ_U(r[M_PROGRESS], 6);        /* +3 (blocked) +3 (classifier) still */
    return 0;
}

/* ==========================================================================
 * FUN_00465d28 — movement-step executor (engine_army_move_run)
 * ========================================================================== */

#define M_READY       0x06
#define M_DEST_COL    0x14
#define M_DEST_ROW    0x15
#define M_WP_LIST     0x1d
#define M_SUBSTATE    0x14c
#define M_ARRIVED     0x150
#define M_STEP_BUDGET 0x154
#define M_TIMER_OFF   0x160

/* Dispatcher-seam spies. */
static int ex_present, ex_arrive, ex_arrive_self;
static int ex_combat_a, ex_combat_b, ex_combat_self, ex_combat_region;
static int ex_region_lo, ex_region_hi, ex_region_self, ex_region_off;
static int ex_special, ex_special_off;

static void sp_present(int m) { (void)m; ex_present++; }
static void sp_arrive(int s)  { ex_arrive++; ex_arrive_self = s; }
static void sp_combat_a(int s, unsigned r, engine_event_queue_state_t *e, int mp)
{ (void)e; (void)mp; ex_combat_a++; ex_combat_self = s; ex_combat_region = (int)r; }
static void sp_combat_b(int s, unsigned r, engine_event_queue_state_t *e, int mp)
{ (void)s; (void)r; (void)e; (void)mp; ex_combat_b++; }
static void sp_region_lo(int s, int o, engine_event_queue_state_t *e, int mp)
{ (void)e; (void)mp; ex_region_lo++; ex_region_self = s; ex_region_off = o; }
static void sp_region_hi(int s, int o, engine_event_queue_state_t *e, int mp)
{ (void)s; (void)e; (void)mp; ex_region_hi++; ex_region_off = o; }
static void sp_special(int s, int o, engine_event_queue_state_t *e, int mp)
{ (void)s; (void)e; (void)mp; ex_special++; ex_special_off = o; }

static void ex_reset(void)
{
    cm_reset();                                 /* binds g_overworld_map */
    engine_army_move_bind_present_fn(sp_present);
    engine_army_move_bind_arrive_fn(sp_arrive);
    engine_army_move_bind_combat_fns(sp_combat_a, sp_combat_b);
    engine_army_move_bind_region_fns(sp_region_lo, sp_region_hi);
    engine_army_move_bind_special_fn(sp_special);
    ex_present = ex_arrive = ex_arrive_self = 0;
    ex_combat_a = ex_combat_b = ex_combat_self = ex_combat_region = 0;
    ex_region_lo = ex_region_hi = ex_region_self = ex_region_off = 0;
    ex_special = ex_special_off = 0;
    s_slot_0057c8cc = 1;
    g_army_state_index = 1;
    g_army_state_abort = 0;
    g_sim_turn_progress = 0;
    engine_set_current_screen(0);
}

/* Stack at (10,10) facing east toward a single waypoint at (11,10). */
static uint8_t *ex_place_east(void)
{
    uint8_t *r = MREC(1);
    r[M_OWNER] = 2;                 /* != local → skip the render box */
    r[M_STATE] = 1;
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_FLAGS14B] = 1;              /* step-eval armed */
    r[M_STEP_BUDGET] = 0xf; r[M_PROGRESS] = 0;   /* budget unspent */
    r[M_WP] = 1; r[M_WP_LIST + 0] = 11; r[M_WP_LIST + 1] = 10;  /* wp[0] = (11,10) */
    return r;
}

/* --- per-tick budget spent → snap + arrival seam ------------------------- */
int test_engine_army_move_exec_arrival_budget(void)
{
    ex_reset();
    uint8_t *r = MREC(1);
    r[M_OWNER] = 2; r[M_STATE] = 1; r[M_FLAGS14B] = 1;
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_DEST_COL] = 10; r[M_DEST_ROW] = 10;     /* already on the destination */
    r[M_STEP_BUDGET] = 3; r[M_PROGRESS] = 5;    /* budget spent (3 <= 5)      */
    r[M_F01] = 1;                               /* carries a live order       */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    T_ASSERT_EQ_U(r[M_PROGRESS], 3);            /* clamped to budget          */
    T_ASSERT_EQ_U(r[M_SUBSTATE], 0);            /* movement finished          */
    T_ASSERT_EQ_U(r[M_ARRIVED], 1);             /* dir-to-dest == 8           */
    T_ASSERT_EQ_I(ex_present, 1);               /* F01 set → map recenter     */
    T_ASSERT_EQ_I(ex_arrive, 1);
    T_ASSERT_EQ_I(ex_arrive_self, 1);
    T_ASSERT_EQ_I(I32(r, M_TIMER_OFF), 4);      /* +0x160 reset on entry      */
    return 0;
}

/* --- record +0x06 suppresses the arrival seam ---------------------------- */
int test_engine_army_move_exec_arrival_ready_suppressed(void)
{
    ex_reset();
    uint8_t *r = MREC(1);
    r[M_OWNER] = 2; r[M_STATE] = 1; r[M_FLAGS14B] = 1;
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_DEST_COL] = 12; r[M_DEST_ROW] = 10;
    r[M_STEP_BUDGET] = 3; r[M_PROGRESS] = 5;
    r[M_READY] = 1;                             /* +0x06 set → suppress arrive */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    T_ASSERT_EQ_U(r[M_SUBSTATE], 0);            /* still finished              */
    T_ASSERT_EQ_I(ex_arrive, 0);                /* but no arrival callback     */
    return 0;
}

/* --- a turn commit in progress forces the arrival path ------------------- */
int test_engine_army_move_exec_turn_progress_forces_arrival(void)
{
    ex_reset();
    uint8_t *r = MREC(1);
    r[M_OWNER] = 2; r[M_STATE] = 1; r[M_FLAGS14B] = 1;
    r[M_COL] = 10; r[M_ROW] = 10; I32(r, M_CELL) = 0x1450;
    r[M_DEST_COL] = 12; r[M_DEST_ROW] = 10;
    r[M_STEP_BUDGET] = 0xf; r[M_PROGRESS] = 0;  /* budget NOT spent            */
    g_sim_turn_progress = 1;                    /* … but a turn is committing  */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    T_ASSERT_EQ_I(ex_arrive, 1);                /* forced arrival              */
    T_ASSERT_EQ_U(r[M_PROGRESS], 0xf);          /* clamped to budget           */
    g_sim_turn_progress = 0;
    return 0;
}

/* --- an open step commits and steps the stack one cell east -------------- */
int test_engine_army_move_exec_open_step_walks(void)
{
    ex_reset();
    ex_place_east();                            /* east cell is open (flags 0) */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    uint8_t *r = MREC(1);
    T_ASSERT_EQ_I((int8_t)r[M_COL], 11);        /* stepped east                */
    T_ASSERT_EQ_U(r[M_FLAGS14B] & 1, 0);        /* committed → step-eval disarmed */
    T_ASSERT_EQ_I(ex_arrive, 0);                /* mid-path, no arrival        */
    return 0;
}

/* --- destination already on the waypoint → immediate stop ---------------- */
int test_engine_army_move_exec_already_on_waypoint(void)
{
    ex_reset();
    uint8_t *r = ex_place_east();
    r[M_WP_LIST + 0] = 10; r[M_WP_LIST + 1] = 10;   /* wp == current cell      */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    T_ASSERT_EQ_U(r[M_SUBSTATE], 0);
    T_ASSERT_EQ_U(r[M_ARRIVED], 1);
    T_ASSERT_EQ_I((int8_t)r[M_COL], 10);        /* did not move                */
    return 0;
}

/* --- combat outcome (5) runs both combat seams with the cell region ------ */
int test_engine_army_move_exec_combat_dispatch(void)
{
    ex_reset();
    ex_place_east();
    g_overworld_map[0x1458 + 1] = 0x40;         /* east cell: combat flag      */
    g_overworld_map[0x1458 + 7] = 9;            /* region key 9                */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    uint8_t *r = MREC(1);
    T_ASSERT_EQ_I(ex_combat_a, 1);
    T_ASSERT_EQ_I(ex_combat_b, 1);
    T_ASSERT_EQ_I(ex_combat_self, 1);
    T_ASSERT_EQ_I(ex_combat_region, 9);
    T_ASSERT_EQ_U(r[M_SUBSTATE], 0);
    T_ASSERT_EQ_U(g_army_state_abort, 0);       /* not on the battle screen    */
    return 0;
}

/* --- combat while the battle screen (0x12) is up aborts the army walk ---- */
int test_engine_army_move_exec_combat_abort_on_battle_screen(void)
{
    ex_reset();
    ex_place_east();
    g_overworld_map[0x1458 + 1] = 0x40;
    engine_set_current_screen(0x12);

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    T_ASSERT_EQ_U(g_army_state_abort, 1);
    engine_set_current_screen(0);
    return 0;
}

/* --- region-event (6): terrain band selects the lo vs hi seam ------------ */
int test_engine_army_move_exec_region_event_bands(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    /* low terrain (< 0x10) → region_lo only */
    ex_reset();
    ex_place_east();
    g_overworld_map[0x1458 + 1] = 0x80;         /* region-event flag           */
    g_overworld_map[0x1458 + 0] = 5;            /* low terrain                 */
    engine_army_move_run(&evq, 0);
    T_ASSERT_EQ_I(ex_region_lo, 1);
    T_ASSERT_EQ_I(ex_region_hi, 0);
    T_ASSERT_EQ_I(ex_region_self, 1);
    T_ASSERT_EQ_I(ex_region_off, 0x1458);

    /* high terrain (0x15..0x19) → region_hi only */
    ex_reset();
    ex_place_east();
    g_overworld_map[0x1458 + 1] = 0x80;
    g_overworld_map[0x1458 + 0] = 0x16;         /* high terrain                */
    engine_army_move_run(&evq, 0);
    T_ASSERT_EQ_I(ex_region_lo, 0);
    T_ASSERT_EQ_I(ex_region_hi, 1);
    T_ASSERT_EQ_I(ex_region_off, 0x1458);
    return 0;
}

/* --- special tile (7) runs the special seam ------------------------------ */
int test_engine_army_move_exec_special_dispatch(void)
{
    ex_reset();
    ex_place_east();
    g_overworld_map[0x1458 + 1] = 0x10;         /* special flag                */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_run(&evq, 0);

    uint8_t *r = MREC(1);
    T_ASSERT_EQ_I(ex_special, 1);
    T_ASSERT_EQ_I(ex_special_off, 0x1458);
    T_ASSERT_EQ_U(r[M_SUBSTATE], 0);
    return 0;
}

/* ==========================================================================
 * FUN_00468ae2 — special-tile plunder (engine_army_move_special_tile)
 * ========================================================================== */

/* Shared setup: army idx 1 (owner 3), a special tile at off 0x1458 in foreign
 * region 9 (owner 2) with 1000 in its +0x24 stockpile. */
static uint8_t *st_setup(int off)
{
    cm_reset();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    engine_army_move_bind_alert_sfx_fn(cap_alert);
    g_last_alert = -1;

    uint8_t *r = MREC(1);
    r[M_OWNER] = 3; r[M_STATE] = 1; r[M_PROGRESS] = 0;

    g_overworld_map[off + 0] = 0x10;            /* untouched special tile */
    g_overworld_map[off + 7] = 9;               /* region key            */
    uint8_t *rg = &g_per_army_768[9 * 0x300];
    rg[0x05] = 2;                               /* region owner != mover  */
    I32(rg, 0x24) = 1000;
    return r;
}

/* --- the full plunder: flip tile, stamp countdown, drain 1/4, cue ---------- */
int test_engine_army_move_special_plunder(void)
{
    int off = 0x1458;
    uint8_t *r = st_setup(off);

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_special_tile(1, off, &evq, 0);

    T_ASSERT_EQ_U(g_overworld_map[off + 0], 0x13);   /* plundered          */
    T_ASSERT_EQ_U(g_overworld_map[off + 3], 0x3c);   /* regrow countdown   */
    T_ASSERT_EQ_I(I32(&g_per_army_768[9 * 0x300], 0x24), 750);  /* 1000-250 */
    T_ASSERT_EQ_U(r[M_PROGRESS], 7);                 /* +7 progress        */
    T_ASSERT_EQ_I(g_last_alert, 3);                  /* blocked-cue id 3   */
    return 0;
}

/* --- a same-owner region is not plundered -------------------------------- */
int test_engine_army_move_special_same_owner_noop(void)
{
    int off = 0x1458;
    uint8_t *r = st_setup(off);
    g_per_army_768[9 * 0x300 + 0x05] = 3;            /* owner == mover      */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_special_tile(1, off, &evq, 0);

    T_ASSERT_EQ_U(g_overworld_map[off + 0], 0x10);   /* untouched           */
    T_ASSERT_EQ_I(I32(&g_per_army_768[9 * 0x300], 0x24), 1000);
    T_ASSERT_EQ_U(r[M_PROGRESS], 0);
    T_ASSERT_EQ_I(g_last_alert, -1);
    return 0;
}

/* --- an idle stack (state 3/4) never plunders ---------------------------- */
int test_engine_army_move_special_idle_noop(void)
{
    int off = 0x1458;
    uint8_t *r = st_setup(off);
    r[M_STATE] = 3;

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_special_tile(1, off, &evq, 0);

    T_ASSERT_EQ_U(g_overworld_map[off + 0], 0x10);
    T_ASSERT_EQ_I(g_last_alert, -1);
    return 0;
}

/* --- an already-plundered tile (terrain != 0x10) is a no-op -------------- */
int test_engine_army_move_special_already_plundered_noop(void)
{
    int off = 0x1458;
    uint8_t *r = st_setup(off);
    g_overworld_map[off + 0] = 0x13;                /* already flipped     */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_special_tile(1, off, &evq, 0);

    T_ASSERT_EQ_I(I32(&g_per_army_768[9 * 0x300], 0x24), 1000);  /* untouched */
    T_ASSERT_EQ_U(r[M_PROGRESS], 0);
    return 0;
}

/* ==========================================================================
 * FUN_0046873f — region-event low-terrain develop (engine_army_move_region_event_lo)
 * ========================================================================== */

/* Shared setup: army idx 1 (owner 3), a cell at off 0x1458 in foreign region 9
 * (owner 2).  Caller sets the cell terrain.  Returns the army record. */
static uint8_t *rl_setup(int off, uint8_t terrain)
{
    cm_reset();
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    engine_army_move_bind_alert_sfx_fn(cap_alert);
    g_last_alert = -1;

    uint8_t *r = MREC(1);
    r[M_OWNER] = 3; r[M_STATE] = 1; r[M_PROGRESS] = 0;

    g_overworld_map[off + 0] = terrain;
    g_overworld_map[off + 7] = 9;               /* region key            */
    g_per_army_768[9 * 0x300 + 0x05] = 2;       /* region owner != mover */
    return r;
}

/* --- each terrain band develops to its sentinel + resets its node --------- */
int test_engine_army_move_region_lo_bands(void)
{
    int off = 0x1458;
    /* {from terrain, to sentinel, regrow, node_b3, node_b0, node_w0, node_wm1, node_w0b} */
    struct { uint8_t from, to, regrow; unsigned b3, b0, w0, wm1, w0b; } cases[] = {
        { 1,  0x03, 0x2e, 0x2ae, 0x2ac, 0x2c0, 0x0f8, 0x0fc },
        { 5,  0x06, 0x09, 0x2de, 0x2dc, 0x2f0, 0x104, 0x108 },
        { 8,  0x09, 0x13, 0x2c6, 0x2c4, 0x2d8, 0x11c, 0x120 },
        { 11, 0x0c, 0x1d, 0x296, 0x294, 0x2a8, 0x110, 0x114 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t *r = rl_setup(off, cases[i].from);
        uint8_t *rg = &g_per_army_768[9 * 0x300];
        rg[cases[i].b3] = 9; rg[cases[i].b0] = 9;     /* dirty → must be reset */

        engine_event_queue_state_t evq; engine_event_queue_init(&evq);
        engine_army_move_region_event_lo(1, off, &evq, 0);

        T_ASSERT_EQ_U(g_overworld_map[off + 0], cases[i].to);
        T_ASSERT_EQ_U(g_overworld_map[off + 3], cases[i].regrow);
        T_ASSERT_EQ_U(rg[cases[i].b3], 3);
        T_ASSERT_EQ_U(rg[cases[i].b0], 0);
        T_ASSERT_EQ_I(I32(rg, cases[i].w0),  0);
        T_ASSERT_EQ_I(I32(rg, cases[i].wm1), -1);
        T_ASSERT_EQ_I(I32(rg, cases[i].w0b), 0);
        T_ASSERT_EQ_U(r[M_PROGRESS], 7);
        T_ASSERT_EQ_I(g_last_alert, 3);
    }
    return 0;
}

/* --- an already-developed sentinel (terrain 3/6/9/0xc) is a no-op --------- */
int test_engine_army_move_region_lo_sentinel_noop(void)
{
    int off = 0x1458;
    uint8_t sentinels[] = { 3, 6, 9, 0xc };
    for (unsigned i = 0; i < sizeof sentinels; i++) {
        uint8_t *r = rl_setup(off, sentinels[i]);
        engine_event_queue_state_t evq; engine_event_queue_init(&evq);
        engine_army_move_region_event_lo(1, off, &evq, 0);
        T_ASSERT_EQ_U(g_overworld_map[off + 0], sentinels[i]);  /* untouched */
        T_ASSERT_EQ_U(r[M_PROGRESS], 0);
        T_ASSERT_EQ_I(g_last_alert, -1);
    }
    return 0;
}

/* --- terrain >= 0xd (none of the four bands) is a no-op ------------------- */
int test_engine_army_move_region_lo_high_noop(void)
{
    int off = 0x1458;
    uint8_t *r = rl_setup(off, 0x0d);
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_region_event_lo(1, off, &evq, 0);
    T_ASSERT_EQ_U(g_overworld_map[off + 0], 0x0d);
    T_ASSERT_EQ_U(r[M_PROGRESS], 0);
    T_ASSERT_EQ_I(g_last_alert, -1);
    return 0;
}

/* --- a same-owner region does not develop -------------------------------- */
int test_engine_army_move_region_lo_same_owner_noop(void)
{
    int off = 0x1458;
    uint8_t *r = rl_setup(off, 1);
    g_per_army_768[9 * 0x300 + 0x05] = 3;           /* owner == mover */
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_region_event_lo(1, off, &evq, 0);
    T_ASSERT_EQ_U(g_overworld_map[off + 0], 1);     /* untouched */
    T_ASSERT_EQ_U(r[M_PROGRESS], 0);
    T_ASSERT_EQ_I(g_last_alert, -1);
    return 0;
}

/* --- an idle stack (state 3/4) never develops ---------------------------- */
int test_engine_army_move_region_lo_idle_noop(void)
{
    int off = 0x1458;
    uint8_t *r = rl_setup(off, 1);
    r[M_STATE] = 4;
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_move_region_event_lo(1, off, &evq, 0);
    T_ASSERT_EQ_U(g_overworld_map[off + 0], 1);
    T_ASSERT_EQ_U(r[M_PROGRESS], 0);
    T_ASSERT_EQ_I(g_last_alert, -1);
    return 0;
}
