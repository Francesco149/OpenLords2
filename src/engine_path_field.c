/* engine_path_field.c — strategic-map cost-field pathfinder.
 *
 * Pure-C port of the FUN_0046f700 group (see engine_path_field.h).  A Dijkstra /
 * Bellman-Ford cost-field flood with a wrapping ring-buffer work queue, followed
 * by an 8-way gradient-descent trace back down the field.  Deterministic — no
 * RNG.  All cell deltas / cost-table offsets r2-verified against the retail
 * decompile.
 *
 * Field selection mirrors retail exactly: `slot == local-player-id` uses the
 * persistent field (retail 0x500c30) + queue (0x4f9820); any other slot uses the
 * transient field (0x4f0380) + queue (0x503020).  The arrival handler always
 * passes slot 0, so it floods into the transient pair.
 */

#include <string.h>

#include "engine_path_field.h"
#include "save.h"                  /* g_per_territory_420 (army records) */
#include "save_schema_globals.h"   /* s_slot_0057c8cc (local player id)  */

/* --- army record waypoint list (g_per_territory_420, stride 0x1a4) --------- */
#define MSTRIDE      0x1a4
#define MREC(i)      (&g_per_territory_420[(i) * MSTRIDE])
#define M_WP_COUNT   0x1c     /* waypoint counter (u8; DAT_0052f0cc)           */
#define M_WP_LIST    0x1d     /* waypoint (col,row) byte pairs (DAT_0052f0cd)  */

/* --- move-cost input (bound; retail base 0x4f3ffe, int16/cell) -------------
 * Each direction reads the 16-bit step cost at a fixed cell offset from the NW
 * base; the diagonal-forbid flag shares the table.  Offsets in int16 units:   */
#define COST_NW   0x00
#define COST_N    0x01
#define COST_NE   0x02
#define COST_W    0x40
#define COST_DIAG 0x41   /* diagonal-forbid flag (== 1 → no diagonal steps)    */
#define COST_E    0x42
#define COST_SW   0x80
#define COST_S    0x81
#define COST_SE   0x82

/* Cell deltas (row stride 0x40): the 8 compass directions. */
#define D_N   (-0x40)
#define D_NE  (-0x3f)
#define D_E   (  0x01)
#define D_SE  (  0x41)
#define D_S   (  0x40)
#define D_SW  (  0x3f)
#define D_W   (-0x01)
#define D_NW  (-0x41)

#define QUEUE_MASK 0x3ff       /* ring wraps when index exceeds 0x3ff          */

static const int16_t *g_terrain;   /* DAT_004f3ffe (move-cost grid)            */

/* Two cost fields + two BFS ring queues (recompute scratch).  field_persistent
 * is the local player's (retail 0x500c30); field_transient covers everyone else
 * (retail 0x4f0380).  Module storage by default; engine_path_field_bind_storage
 * can point them at another copy (the in-process verifier binds retail's). */
static int16_t s_field_persistent[ENGINE_PATH_FIELD_CELLS];
static int16_t s_field_transient[ENGINE_PATH_FIELD_CELLS];
static int32_t s_queue_persistent[QUEUE_MASK + 1];
static int32_t s_queue_transient[QUEUE_MASK + 1];
static int16_t *g_field_persistent = s_field_persistent;
static int16_t *g_field_transient  = s_field_transient;
static int32_t *g_queue_persistent = s_queue_persistent;
static int32_t *g_queue_transient  = s_queue_transient;

/* Per-slot waypoint scratch (retail DAT_0053f070 count, DAT_00553550 list). */
static uint8_t s_wp_count[ENGINE_PATH_FIELD_SLOTS];
static uint8_t s_wp_list[ENGINE_PATH_FIELD_SLOTS][ENGINE_PATH_FIELD_MAX_WP * 2];
static uint8_t *g_wp_count = s_wp_count;
static uint8_t (*g_wp_list)[ENGINE_PATH_FIELD_MAX_WP * 2] = s_wp_list;

/* Retail keeps the flood / trace bookkeeping in globals too (see the .h). */
int32_t g_pf_src_col;    /* DAT_00504020 */
int32_t g_pf_src_row;    /* DAT_00504024 */
int32_t g_pf_src_cell;   /* DAT_004f4070 */
int32_t g_pf_head;       /* DAT_00500c20 */
int32_t g_pf_tail;       /* DAT_004f2768 */
int32_t g_pf_cur_col;    /* DAT_00507230 */
int32_t g_pf_cur_row;    /* DAT_00507234 */

void engine_path_field_bind_terrain(const int16_t *terrain_4f3ffe)
{
    g_terrain = terrain_4f3ffe;
}

void engine_path_field_bind_storage(int16_t *field_persistent, int16_t *field_transient,
                                    int32_t *queue_persistent, int32_t *queue_transient,
                                    uint8_t *wp_count, uint8_t *wp_list)
{
    g_field_persistent = field_persistent;
    g_field_transient  = field_transient;
    g_queue_persistent = queue_persistent;
    g_queue_transient  = queue_transient;
    g_wp_count = wp_count;
    g_wp_list  = (uint8_t (*)[ENGINE_PATH_FIELD_MAX_WP * 2])wp_list;
}

/* Pick the (field, queue) pair for a slot.  Mirrors `slot == DAT_0057c8cc`. */
static int16_t *field_for(int slot)
{
    return (slot == (int)s_slot_0057c8cc) ? g_field_persistent : g_field_transient;
}
static int32_t *queue_for(int slot)
{
    return (slot == (int)s_slot_0057c8cc) ? g_queue_persistent : g_queue_transient;
}

/* Relax one neighbour: stamp the new cost and enqueue (ring wraps). */
static void pf_relax(int16_t *field, int32_t *queue, int *tail,
                     int ncell, int newcost)
{
    field[ncell] = (int16_t)newcost;
    queue[*tail] = ncell;
    *tail += 1;
    if (*tail > QUEUE_MASK)
        *tail = 0;
}

/* Try to relax `dir_delta`-neighbour of `cell` with step cost at `cost_off`. */
static void pf_step(int16_t *field, int32_t *queue, int *tail, int cell,
                    int curcost, int dir_delta, int cost_off, int blocked_mode)
{
    int step = g_terrain[cell + cost_off];
    if (blocked_mode != 0 && step > 1)
        step = 100;
    if (step == 0)
        return;
    int ncell = cell + dir_delta;
    int nv = field[ncell];
    if (nv == 0 || curcost + step < nv)
        pf_relax(field, queue, tail, ncell, curcost + step);
}

/* --- FUN_0046f700 @ 0x46f700: cost-field flood ----------------------------- */
void engine_path_field_fill(int slot, int col, int row, int blocked_mode)
{
    int16_t *field = field_for(slot);
    int32_t *queue = queue_for(slot);

    memset(field, 0, ENGINE_PATH_FIELD_CELLS * sizeof *field);  /* retail 0x4b3e51 */

    g_pf_src_col  = col;
    g_pf_src_row  = row;
    g_pf_src_cell = row * ENGINE_PATH_FIELD_DIM + col;
    field[g_pf_src_cell] = 1;
    queue[0] = g_pf_src_cell;

    g_pf_head = 0;
    g_pf_tail = 1;
    int *tail = &g_pf_tail;
    while (g_pf_tail != g_pf_head) {
        int cell    = queue[g_pf_head];
        int curcost = field[cell];
        int diag    = g_terrain[cell + COST_DIAG];

        /* Cardinals always; diagonals only when the cell does not forbid them. */
        pf_step(field, queue, tail, cell, curcost, D_N, COST_N, blocked_mode);
        pf_step(field, queue, tail, cell, curcost, D_E, COST_E, blocked_mode);
        pf_step(field, queue, tail, cell, curcost, D_S, COST_S, blocked_mode);
        pf_step(field, queue, tail, cell, curcost, D_W, COST_W, blocked_mode);
        if (diag != 1) {
            pf_step(field, queue, tail, cell, curcost, D_NE, COST_NE, blocked_mode);
            pf_step(field, queue, tail, cell, curcost, D_SE, COST_SE, blocked_mode);
            pf_step(field, queue, tail, cell, curcost, D_SW, COST_SW, blocked_mode);
            pf_step(field, queue, tail, cell, curcost, D_NW, COST_NW, blocked_mode);
        }

        g_pf_head += 1;
        if (g_pf_head > QUEUE_MASK)
            g_pf_head = 0;
    }
}

/* Trace neighbour table, in retail's scan order (index 0..7). */
static const struct { int fdelta, col_d, row_d; } TRACE_DIR[8] = {
    { D_N,   0, -1 },   /* 0 N  */
    { D_NE,  1, -1 },   /* 1 NE */
    { D_E,   1,  0 },   /* 2 E  */
    { D_SE,  1,  1 },   /* 3 SE */
    { D_S,   0,  1 },   /* 4 S  */
    { D_SW, -1,  1 },   /* 5 SW */
    { D_W,  -1,  0 },   /* 6 W  */
    { D_NW, -1, -1 },   /* 7 NW */
};

/* --- FUN_004701ac @ 0x4701ac: gradient-descent trace ----------------------- */
int engine_path_field_trace(int slot, int col, int row)
{
    const int16_t *field = field_for(slot);
    uint8_t *wp = g_wp_list[slot];

    engine_path_field_clear_slot(slot);              /* FUN_004708d0 */

    int n       = 0;
    g_pf_src_col = g_pf_cur_col = col;   /* the cursor lives in retail's globals */
    g_pf_src_row = g_pf_cur_row = row;
    g_pf_src_cell = row * ENGINE_PATH_FIELD_DIM + col;
    int cell    = g_pf_src_cell;
    int best    = field[cell];

    /* Retail pre-seeds waypoint 0 with the start cell before the loop. */
    wp[0] = (uint8_t)g_pf_cur_col;
    wp[1] = (uint8_t)g_pf_cur_row;

    for (;;) {
        if (best < 2) {                              /* reached the source */
            g_wp_count[slot] = (uint8_t)n;
            return 1;
        }
        wp[n * 2 + 0] = (uint8_t)g_pf_cur_col;
        wp[n * 2 + 1] = (uint8_t)g_pf_cur_row;

        best = field[cell];
        int chosen = -1;
        int stride = (g_terrain[cell + COST_DIAG] == 1) ? 2 : 1;

        for (int c = 0; c < 8; c += stride) {
            int v = field[cell + TRACE_DIR[c].fdelta];
            if (v != 0 && v < best) { best = v; chosen = c; }
        }
        if (chosen == -1 && stride != 1) {           /* diag-forbidden retry, all 8 */
            for (int c = 0; c < 8; c += 1) {
                int v = field[cell + TRACE_DIR[c].fdelta];
                if (v != 0 && v < best) { best = v; chosen = c; }
            }
        }
        if (chosen == -1)
            break;                                   /* stalled */

        g_pf_cur_col += TRACE_DIR[chosen].col_d;
        g_pf_cur_row += TRACE_DIR[chosen].row_d;
        cell   += TRACE_DIR[chosen].fdelta;
        n      += 1;
    }
    return 0;
}

/* --- FUN_004708d0 @ 0x4708d0: clear a path scratch slot -------------------- */
void engine_path_field_clear_slot(int slot)
{
    g_wp_count[slot] = 0;
    memset(g_wp_list[slot], 0, ENGINE_PATH_FIELD_MAX_WP * 2);
}

/* --- FUN_004707be @ 0x4707be: scratch slot → army record waypoint list ----- */
void engine_path_field_copy_record(int slot, int army_idx)
{
    uint8_t *r  = MREC(army_idx);
    uint8_t *wp = g_wp_list[slot];
    int count   = g_wp_count[slot];

    r[M_WP_COUNT] = (uint8_t)count;
    for (int i = 0; i < ENGINE_PATH_FIELD_MAX_WP; i++) {
        if (i < count) {
            r[M_WP_LIST + i * 2 + 0] = wp[i * 2 + 0];
            r[M_WP_LIST + i * 2 + 1] = wp[i * 2 + 1];
        } else {
            r[M_WP_LIST + i * 2 + 0] = 0;
            r[M_WP_LIST + i * 2 + 1] = 0;
        }
    }
}

/* --- test / retail-diff accessors ----------------------------------------- */
int16_t engine_path_field_cost(int slot, int cell)
{
    return field_for(slot)[cell];
}
int engine_path_field_wp_count(int slot)
{
    return g_wp_count[slot];
}
void engine_path_field_wp(int slot, int i, int *col, int *row)
{
    if (col) *col = g_wp_list[slot][i * 2 + 0];
    if (row) *row = g_wp_list[slot][i * 2 + 1];
}
