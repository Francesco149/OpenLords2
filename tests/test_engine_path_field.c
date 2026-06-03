/*
 * Tests for engine_path_field.c — the strategic-map cost-field pathfinder
 * (Dijkstra flood + gradient-descent trace, FUN_0046f700 group).
 *
 * The move-cost input is a 16-bit-per-cell grid (bound base = retail 0x4f3ffe)
 * where, for logical cell c, the 8 directional step costs and the diagonal-
 * forbid flag live at fixed int16 offsets (c+1 N, c+0x42 E, c+0x81 S, c+0x40 W,
 * c+0x41 diag-flag, c+2 NE, c+0x82 SE, c+0x80 SW, c+0 NW).  These cross-couple
 * between neighbouring cells, so the tests build *sparse corridors* (only the
 * costs needed for one route are nonzero) whose flood + trace are hand-checked.
 * Byte-exactness vs retail is the job of the diff harness; here we pin the
 * relaxation arithmetic, the cost-table offsets, the trace descent, and the
 * record copy.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_path_field.h"
#include "save.h"                   /* g_per_territory_420 */
#include "save_schema_globals.h"    /* s_slot_0057c8cc     */

#define MREC(i)   (&g_per_territory_420[(i) * 0x1a4])
#define CELL(col, row)  ((row) * ENGINE_PATH_FIELD_DIM + (col))

/* int16 cost grid; padded past CELLS by the max SE offset (0x82). */
static int16_t tf_terrain[ENGINE_PATH_FIELD_CELLS + 0x83];

/* Cost-table int16 offsets (mirror engine_path_field.c). */
enum { O_NW = 0x00, O_N = 0x01, O_NE = 0x02, O_W = 0x40, O_DIAG = 0x41,
       O_E = 0x42, O_SW = 0x80, O_S = 0x81, O_SE = 0x82 };

static void tf_reset(void)
{
    memset(tf_terrain, 0, sizeof tf_terrain);
    engine_path_field_bind_terrain(tf_terrain);
    s_slot_0057c8cc = 1;            /* local player id 1; slot 0 → transient */
}

/* --- fill: a 3-cell east corridor floods to costs 1,2,3 ------------------- */
int test_engine_path_field_fill_corridor(void)
{
    tf_reset();
    int s0 = CELL(10, 10), s1 = CELL(11, 10), s2 = CELL(12, 10);
    tf_terrain[s0 + O_E] = 1;        /* step S0→E costs 1 */
    tf_terrain[s1 + O_E] = 1;        /* step S1→E costs 1 */

    engine_path_field_fill(0, 10, 10, 0);

    T_ASSERT_EQ_I(engine_path_field_cost(0, s0), 1);   /* source */
    T_ASSERT_EQ_I(engine_path_field_cost(0, s1), 2);   /* 1 + 1  */
    T_ASSERT_EQ_I(engine_path_field_cost(0, s2), 3);   /* 2 + 1  */
    T_ASSERT_EQ_I(engine_path_field_cost(0, CELL(10, 11)), 0);  /* untouched */
    return 0;
}

/* --- fill: blocked_mode clamps any step cost > 1 to 100 ------------------- */
int test_engine_path_field_blocked_clamp(void)
{
    tf_reset();
    int s0 = CELL(10, 10), s1 = CELL(11, 10);
    tf_terrain[s0 + O_E] = 5;

    engine_path_field_fill(0, 10, 10, 1);              /* blocked_mode = 1 */
    T_ASSERT_EQ_I(engine_path_field_cost(0, s1), 101); /* 1 + clamp(5)→100 */

    tf_reset();
    tf_terrain[s0 + O_E] = 5;
    engine_path_field_fill(0, 10, 10, 0);              /* blocked_mode = 0 */
    T_ASSERT_EQ_I(engine_path_field_cost(0, s1), 6);   /* 1 + 5 */
    return 0;
}

/* --- trace: descend the east corridor back to the source ------------------ */
int test_engine_path_field_trace_corridor(void)
{
    tf_reset();
    int s0 = CELL(10, 10), s1 = CELL(11, 10);
    tf_terrain[s0 + O_E] = 1;
    tf_terrain[s1 + O_E] = 1;
    engine_path_field_fill(0, 10, 10, 0);

    int ok = engine_path_field_trace(0, 12, 10);       /* start at the far end */
    T_ASSERT_EQ_I(ok, 1);
    T_ASSERT_EQ_I(engine_path_field_wp_count(0), 2);

    int c, r;
    engine_path_field_wp(0, 0, &c, &r); T_ASSERT_EQ_I(c, 12); T_ASSERT_EQ_I(r, 10);
    engine_path_field_wp(0, 1, &c, &r); T_ASSERT_EQ_I(c, 11); T_ASSERT_EQ_I(r, 10);
    return 0;
}

/* --- trace: a start already at the source (cost 1) yields an empty path --- */
int test_engine_path_field_trace_at_source(void)
{
    tf_reset();
    engine_path_field_fill(0, 10, 10, 0);              /* only the source = 1 */
    int ok = engine_path_field_trace(0, 10, 10);
    T_ASSERT_EQ_I(ok, 1);
    T_ASSERT_EQ_I(engine_path_field_wp_count(0), 0);
    return 0;
}

/* --- trace: an unflooded start (field == 0) hits the cost<2 early return --
 * Retail reads field[start]; 0 < 2, so it returns 1 with an empty path without
 * scanning.  (The genuine return-0 stall — a flooded cell with no descending
 * neighbour — is unreachable from a clean flood, since every flooded non-source
 * cell was relaxed FROM a strictly cheaper predecessor.) */
int test_engine_path_field_trace_unflooded(void)
{
    tf_reset();
    engine_path_field_fill(0, 10, 10, 0);
    int ok = engine_path_field_trace(0, 30, 30);       /* far from any flood */
    T_ASSERT_EQ_I(ok, 1);
    T_ASSERT_EQ_I(engine_path_field_wp_count(0), 0);
    return 0;
}

/* --- clear + copy: scratch slot → army-record waypoint list ---------------- */
int test_engine_path_field_copy_record(void)
{
    tf_reset();
    int s0 = CELL(10, 10), s1 = CELL(11, 10);
    tf_terrain[s0 + O_E] = 1;
    tf_terrain[s1 + O_E] = 1;
    engine_path_field_fill(0, 10, 10, 0);
    engine_path_field_trace(0, 12, 10);

    memset(MREC(5), 0xaa, 0x1a4);
    engine_path_field_copy_record(0, 5);

    uint8_t *r = MREC(5);
    T_ASSERT_EQ_U(r[0x1c], 2);                          /* count */
    T_ASSERT_EQ_U(r[0x1d + 0], 12); T_ASSERT_EQ_U(r[0x1d + 1], 10);
    T_ASSERT_EQ_U(r[0x1d + 2], 11); T_ASSERT_EQ_U(r[0x1d + 3], 10);
    T_ASSERT_EQ_U(r[0x1d + 4], 0);  T_ASSERT_EQ_U(r[0x1d + 5], 0);  /* zero tail */
    return 0;
}

/* --- field selection: local-player slot and others use separate fields ---- */
int test_engine_path_field_field_selection(void)
{
    tf_reset();
    /* Two independent east corridors: one at (10,10), one at (20,20). */
    tf_terrain[CELL(10, 10) + O_E] = 1;
    tf_terrain[CELL(11, 10) + O_E] = 1;
    tf_terrain[CELL(20, 20) + O_E] = 1;
    tf_terrain[CELL(21, 20) + O_E] = 1;

    engine_path_field_fill(0, 10, 10, 0);              /* slot 0 != local → transient */
    engine_path_field_fill(1, 20, 20, 0);              /* slot 1 == local → persistent */

    /* Each field carries only its own flood. */
    T_ASSERT_EQ_I(engine_path_field_cost(0, CELL(11, 10)), 2);  /* transient */
    T_ASSERT_EQ_I(engine_path_field_cost(0, CELL(21, 20)), 0);
    T_ASSERT_EQ_I(engine_path_field_cost(1, CELL(21, 20)), 2);  /* persistent */
    T_ASSERT_EQ_I(engine_path_field_cost(1, CELL(11, 10)), 0);
    return 0;
}

/* --- diagonal step + diag-forbid flag ------------------------------------- */
int test_engine_path_field_diagonal(void)
{
    tf_reset();
    int s0 = CELL(10, 10), se = CELL(11, 11);
    tf_terrain[s0 + O_SE] = 1;                          /* SE step costs 1 */

    engine_path_field_fill(0, 10, 10, 0);
    T_ASSERT_EQ_I(engine_path_field_cost(0, se), 2);   /* diagonal allowed */

    tf_reset();
    tf_terrain[s0 + O_SE]   = 1;
    tf_terrain[s0 + O_DIAG] = 1;                        /* forbid diagonals */
    engine_path_field_fill(0, 10, 10, 0);
    T_ASSERT_EQ_I(engine_path_field_cost(0, se), 0);   /* SE not relaxed */
    return 0;
}
