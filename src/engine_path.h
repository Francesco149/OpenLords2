/*
 * OpenLords2 — army pathfinding cluster.
 *
 * Five closed-leaf functions that together form the army-movement
 * routing surface.  Walks the engine's pre-computed BFS distance map
 * to reconstruct a step-by-step path between two cells, with two
 * helpers for distance / direction math and a stash for the result.
 *
 *   FUN_00404f4c @ 0x404f4c (180 B) → engine_path_chebyshev_distance
 *   FUN_0046f434 @ 0x46f434 (216 B) → engine_path_direction_8way
 *   FUN_00472392 @ 0x472392 (715 B) → engine_path_search_greedy
 *   FUN_0047265d @ 0x47265d (536 B) → engine_path_find_nearest_matching
 *   FUN_00472875 @ 0x472875 (151 B) → engine_path_stash_into_army
 *
 * The cluster is closed under callees — FUN_00472392 calls only the
 * two pure helpers in this same cluster, and the rest are leaves.
 *
 * Pure C; no Win32 deps; every input/output goes through the state
 * struct so the diff harness + host tests don't need real globals.
 * Matches the diff-harness shape established by engine_tile.
 *
 * --- The 8-way direction encoding -----------------------------------
 *
 *   0 = up        (col+0, row-1)
 *   1 = up-right  (col+1, row-1)
 *   2 = right     (col+1, row+0)
 *   3 = down-right(col+1, row+1)
 *   4 = down      (col+0, row+1)
 *   5 = down-left (col-1, row+1)
 *   6 = left      (col-1, row+0)
 *   7 = up-left   (col-1, row-1)
 *   8 = equal     (no direction; reported when from == to)
 *
 * Linear-index neighbor offsets (80-wide grid) for the same encoding
 * live at retail VA 0x4d6c98 (`k_path_dir_offset[8]`).
 */

#ifndef OPENLORDS2_ENGINE_PATH_H
#define OPENLORDS2_ENGINE_PATH_H

#include <stdint.h>

/* Grid geometry — same 80x80 cells as engine_tile.  Path output is
 * up to 150 (col, row) byte pairs. */
#define ENGINE_PATH_GRID_W        80
#define ENGINE_PATH_GRID_H        80
#define ENGINE_PATH_CELL_COUNT    (ENGINE_PATH_GRID_W * ENGINE_PATH_GRID_H)  /* 6400 */
#define ENGINE_PATH_MAX_STEPS     0x96   /* 150 — retail hardcodes both the
                                           * step cap and the "failed search"
                                           * sentinel return value. */

/* Per-army slot size for the stash function — 0x1b0 = 432 B. */
#define ENGINE_PATH_ARMY_SLOT_STRIDE 0x1b0

/* Within each army's 432-byte slot, the path block lives at these
 * offsets from the slot base.  See FUN_00472875 for retail addresses
 * (slot base = DAT_005544a0). */
#define ENGINE_PATH_ARMY_OFF_LEN     0x16   /* short — step count (DAT_005544b6) */
#define ENGINE_PATH_ARMY_OFF_PATH    0x18   /* byte pairs (col, row) (DAT_005544b8/b9) */
#define ENGINE_PATH_ARMY_OFF_ACTIVE  0x144  /* byte = 1 — "path armed" flag (DAT_005545e4) */

/* All state read or written by the cluster.  Mirrors the retail
 * globals where addresses matter for layout-equivalence with the
 * (future) diff harness.
 *
 *   path_out[]      ← &DAT_004f9680 (interleaved byte col/row pairs)
 *   start_idx       ←  DAT_004f7d70 (linear idx of the search start)
 *   stage_scratch[] ← &DAT_004f7d80 (6400 × u8; terrain stage per cell)
 *   cost_map[]      ← &DAT_00504030 (6400 × i16; BFS distance map)
 *   last_dx         ←  DAT_00591560 (chebyshev-helper side effect)
 *   last_dy         ←  DAT_0058fea0 (chebyshev-helper side effect)
 *   nearest_col     ←  DAT_00569570 (find-nearest output: x)
 *   nearest_row     ←  DAT_00569578 (find-nearest output: y)
 *   armies[]        ← &DAT_005544a0 (per-army 432-B slot base)
 */
typedef struct {
    /* ─── helper side-effect outputs ─────────────────────────────── */

    /* Last |dx| and |dy| from engine_path_chebyshev_distance.  Retail
     * stores these in globals so callers downstream of a chebyshev
     * call can re-read them without recomputing.  See the call sites
     * around 67429 / 71241 / 72060 in docs/decompiled/all.c. */
    int last_dx;
    int last_dy;

    /* Last (col, row) result from engine_path_find_nearest_matching.
     * Retail callers read these globals to consume the search result;
     * the function's return value is just success/fail. */
    int nearest_col;
    int nearest_row;

    /* ─── pathfinder I/O ─────────────────────────────────────────── */

    /* Linear idx (row*80+col) of the start cell.  Stamped by the
     * greedy search before its main loop.  Some callers consume this
     * separately after the search returns. */
    int start_idx;

    /* Path output buffer — interleaved (col, row) byte pairs, room
     * for ENGINE_PATH_MAX_STEPS steps.  Slot i occupies offsets
     * [2*i] = col, [2*i+1] = row.
     *
     * Retail VA: &DAT_004f9680 (col) and &DAT_004f9681 (row), which
     * are two adjacent globals at the same base + 1.  Greedy-search
     * writes the starting cell here BEFORE entering the loop, then
     * the WALKER position (pre-step) into slot local_1c each
     * iteration.  See the FUN_00472392 init at line 28-30 of the
     * Ghidra dump. */
    uint8_t path_out[ENGINE_PATH_MAX_STEPS * 2];

    /* ─── input data (read-only during search) ───────────────────── */

    /* Stage scratch — terrain stage byte per cell.  Greedy search
     * gates neighbour acceptance on stage being within ±1 of the
     * start cell's stage.  Owned by engine_tile (see engine_tile_
     * refresh_stage_scratch). */
    uint8_t stage_scratch[ENGINE_PATH_CELL_COUNT];

    /* BFS cost map — i16 per cell.  Retail VA &DAT_00504030.  Holds
     * step counts from the destination outward, with 999 meaning
     * "impassable / wall" and 1 meaning "at destination".  The map
     * is filled by an upstream BFS we have not yet ported; the
     * cluster here just walks down the gradient. */
    int16_t cost_map[ENGINE_PATH_CELL_COUNT];

    /* ─── per-army storage for the stash function ────────────────── */

    /* Caller supplies the per-army slot base pointer at stash time;
     * no need to keep an embedded slab here.  See the function's
     * dedicated signature below. */
} engine_path_state_t;

/* ──────────────────────────────────────────────────────────────────
 * Helpers — pure leaves
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00404f4c.  Returns max(|param_1 - param_3|,
 * |param_2 - param_4|) — Chebyshev (king-move) distance.  Also
 * stashes the unsigned per-axis deltas into state->last_dx /
 * state->last_dy as a side effect (retail globals DAT_00591560 and
 * DAT_0058fea0). */
int engine_path_chebyshev_distance(engine_path_state_t *state,
                                   int x1, int y1, int x2, int y2);

/* Port of FUN_0046f434.  Returns the 8-way direction code (see
 * header docstring for the encoding) pointing FROM (from_x, from_y)
 * TOWARD (to_x, to_y).  Returns 8 if the two points are equal.
 *
 * Note: retail's codegen passes the high bits of one input through
 * to the high bits of the return value (it doesn't widen properly).
 * We mask to 8 bits before returning to keep the value range to
 * 0..8; no caller in retail ever checks the high bits anyway, so
 * this is observationally equivalent. */
int engine_path_direction_8way(int from_x, int from_y,
                               int to_x, int to_y);

/* ──────────────────────────────────────────────────────────────────
 * Greedy path search
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00472392.  Greedy descent on state->cost_map starting
 * at (start_col, start_row), trying to reach (dest_col, dest_row).
 * Writes the visited cells into state->path_out as (col, row) byte
 * pairs and updates state->start_idx.
 *
 * Returns:
 *
 *   step count [1, 149]
 *     normal success — the cost map gradient led from start to a
 *     cell with cost ≤ 1 within that many steps.  path_out[0..N-1]
 *     holds the visited cells; the final cell is where the descent
 *     terminated (cost reached the goal).
 *
 *   ENGINE_PATH_MAX_STEPS (150)
 *     failed search.  Either (a) start and dest already adjacent
 *     (chebyshev distance < 2) and the cost map says "go that way"
 *     so no walk needed, or (b) the descent got stuck (no neighbor
 *     satisfied the stage-band + non-zero cost predicate) or
 *     hit the 149-step iteration cap.  Retail callers treat both
 *     as "no usable path" and either retry with a different starting
 *     cell or abort the army's move.
 *
 *   0
 *     start cell's cost_map entry is out of range — either 0
 *     ("already at the destination — no step needed") or > 997
 *     ("start is inside or adjacent to a wall, no path possible
 *     from here").  No state changes apart from the initial start
 *     cell write to path_out[0].
 *
 * The retail-side path_out buffer is also written before the main
 * loop with the start cell — even when the function returns 0 the
 * buffer's slot 0 holds (start_col, start_row).  Preserved verbatim
 * since callers may inspect it. */
int engine_path_search_greedy(engine_path_state_t *state,
                              int dest_col, int dest_row,
                              int start_col, int start_row);

/* ──────────────────────────────────────────────────────────────────
 * Expanding-square nearest-matching search
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_0047265d.  Searches outward from (start_col,
 * start_row) in expanding squares (radius 0..29) for a non-zero-
 * cost cell whose stage_scratch byte matches the start cell's.
 * Writes the result into state->nearest_col / state->nearest_row
 * and returns 1 on a hit, 0 on no match within radius 29.
 *
 * Special return: if a candidate cell exactly equals the explicit
 * "skip" point (skip_x, skip_y), the search returns 0 immediately
 * (and DOES NOT update nearest_*).  Retail callers pass the army's
 * own pre-move position there so the picker never returns a cell
 * the army is already on.
 *
 * The expanding square at radius R covers the 2R+1-wide box around
 * the start, clipped to the 80x80 grid.  Within each radius the
 * lowest-cost matching cell wins; ties go to the first encountered
 * in row-major order.  When R=0 (the first iteration) the box is
 * 1x1 — just the start cell itself, which matches its own stage
 * trivially, so callers usually see R=0 succeed unless the start's
 * cost is 0. */
int engine_path_find_nearest_matching(engine_path_state_t *state,
                                      int skip_x, int skip_y,
                                      int start_col, int start_row);

/* ──────────────────────────────────────────────────────────────────
 * Stash into per-army slot
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00472875.  Copies state->path_out (the byte col/row
 * pairs) and `step_count` (as a 16-bit length) into the per-army
 * slot at the given base pointer, then stamps the slot's "active"
 * byte to 1.  Caller computes the slot base from its own
 * armies[] backing storage — typically:
 *
 *   uint8_t *slot = &armies[army_idx * ENGINE_PATH_ARMY_SLOT_STRIDE];
 *
 * Only the first step_count pairs are copied; the rest of the slot's
 * path area is untouched.  Retail iterates with `local_8 < param_2`
 * so step_count > 150 would overrun — but no retail caller does
 * that; the callers always feed back what engine_path_search_greedy
 * returned (clamped at 150). */
void engine_path_stash_into_army(engine_path_state_t *state,
                                 uint8_t *army_slot_base,
                                 int step_count);

#endif /* OPENLORDS2_ENGINE_PATH_H */
