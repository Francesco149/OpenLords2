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
