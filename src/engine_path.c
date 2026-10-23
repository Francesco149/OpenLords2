/*
 * Army pathfinding cluster — pure-C port of the FUN_00472392 group.
 * See engine_path.h for the API doc and retail address map.
 */

#include "engine_path.h"

#include <stddef.h>

/* 8-way neighbor offsets in linear-index space (80-wide grid).
 * Retail .data table at VA 0x4d6c98 — verified byte-for-byte via PE
 * extraction.  Indices match the 8-way direction encoding documented
 * in engine_path.h. */
static const int k_dir_offset[8] = {
    -80,   /* 0 — up        */
    -79,   /* 1 — up-right  */
    +1,    /* 2 — right     */
    +81,   /* 3 — down-right*/
    +80,   /* 4 — down      */
    +79,   /* 5 — down-left */
    -1,    /* 6 — left      */
    -81,   /* 7 — up-left   */
};

/* ──────────────────────────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────────────────────────── */

/* Shared dx/dy side-effect computation — both Chebyshev and Manhattan
 * distance helpers write the same DAT_00591560 / DAT_0058fea0 retail
 * globals.  Factored out here so the two functions can mirror retail
 * byte-exactly without duplicating the three-way compare per axis. */
static void store_unsigned_deltas(engine_path_state_t *s,
                                  int x1, int y1, int x2, int y2)
{
    if (x2 < x1)       s->last_dx = x1 - x2;
    else if (x1 < x2)  s->last_dx = x2 - x1;
    else               s->last_dx = 0;

    if (y2 < y1)       s->last_dy = y1 - y2;
    else if (y1 < y2)  s->last_dy = y2 - y1;
    else               s->last_dy = 0;
}

int engine_path_chebyshev_distance(engine_path_state_t *s,
                                   int x1, int y1, int x2, int y2)
{
    store_unsigned_deltas(s, x1, y1, x2, y2);

    /* Chebyshev = max(|dx|, |dy|). */
    int r = s->last_dx;
    if (s->last_dx < s->last_dy) r = s->last_dy;
    return r;
}

int engine_path_manhattan_distance(engine_path_state_t *s,
                                   int x1, int y1, int x2, int y2)
{
    store_unsigned_deltas(s, x1, y1, x2, y2);
    return s->last_dx + s->last_dy;
}

int32_t g_path_dist_dx;   /* DAT_00591560 */
int32_t g_path_dist_dy;   /* DAT_0058fea0 */

static void store_global_deltas(int x1, int y1, int x2, int y2)
{
    g_path_dist_dx = (x2 < x1) ? x1 - x2 : (x1 < x2) ? x2 - x1 : 0;
    g_path_dist_dy = (y2 < y1) ? y1 - y2 : (y1 < y2) ? y2 - y1 : 0;
}

int engine_path_dist_chebyshev(int x1, int y1, int x2, int y2)
{
    store_global_deltas(x1, y1, x2, y2);
    return (g_path_dist_dx < g_path_dist_dy) ? g_path_dist_dy : g_path_dist_dx;
}

int engine_path_dist_manhattan(int x1, int y1, int x2, int y2)
{
    store_global_deltas(x1, y1, x2, y2);
    return g_path_dist_dx + g_path_dist_dy;
}

int engine_path_direction_8way(int from_x, int from_y,
                               int to_x, int to_y)
{
    /* Mirrors FUN_0046f434 verbatim — three-way compare on x, then
     * three-way compare on y per branch.  Returns 0..7 for a real
     * direction, 8 for equal points.  Direction encoding:
     *
     *   to.x < from.x  (to is LEFT)   →  upper-left (7), left (6), lower-left (5)
     *   to.x == from.x (same column)  →  up (0), [middle is "equal" → 8], down (4)
     *   to.x > from.x  (to is RIGHT)  →  upper-right (1), right (2), lower-right (3)
     */
    if (to_x < from_x) {
        if (to_y < from_y) return 7;   /* up-left */
        if (to_y == from_y) return 6;  /* left */
        /* to_y > from_y */            return 5;  /* down-left */
    } else if (to_x == from_x) {
        if (to_y < from_y) return 0;   /* up */
        if (from_y < to_y) return 4;   /* down */
        /* to_y == from_y */           return 8;  /* equal */
    } else { /* from_x < to_x */
        if (to_y < from_y) return 1;   /* up-right */
        if (to_y == from_y) return 2;  /* right */
        /* to_y > from_y */            return 3;  /* down-right */
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Greedy path search
 * ────────────────────────────────────────────────────────────────── */

int engine_path_search_greedy(engine_path_state_t *s,
                              int dest_col, int dest_row,
                              int start_col, int start_row)
{
    /* Linear idx of the start cell.  Retail writes this to DAT_004f7d70
     * BEFORE entering the loop; consumers downstream of a search call
     * read it as the "where did the search start" cookie.  Preserved
     * as a side effect on the state struct. */
    int cur_idx = start_row * ENGINE_PATH_GRID_W + start_col;
    int cur_col = start_col;
    int cur_row = start_row;
    s->start_idx = cur_idx;

    /* Stamp slot 0 of the output with the start cell — retail does
     * this unconditionally, even on the early returns below. */
    s->path_out[0]     = (uint8_t)start_col;
    s->path_out[0 + 1] = (uint8_t)start_row;

    /* Initial cost at start.  If 0 (already at dest), or > 997 (start
     * inside a wall), the greedy descent can't proceed — retail
     * returns 0 here. */
    int16_t start_cost = s->cost_map[cur_idx];

    /* Chebyshev short-circuit: if start and dest are within 1 cell
     * (i.e. adjacent or identical), retail bails with the
     * "MAX_STEPS" sentinel — caller interprets as "no walk needed,
     * issue the move directly".  Done BEFORE the start_cost check
     * (note the order in Ghidra's decomp). */
    int cheby = engine_path_chebyshev_distance(s,
                                               dest_col, dest_row,
                                               start_col, start_row);
    if (cheby < 2) {
        return ENGINE_PATH_MAX_STEPS;
    }

    /* Bad-start guard.  sVar1 < 1 catches cost == 0 (at dest), and
     * 997 < sVar1 catches cost == 999 (wall).  In both cases retail
     * returns 0 and leaves the output buffer with just slot-0
     * stamped. */
    if (start_cost < 1 || 0x3e5 < start_cost) {
        return 0;
    }

    /* Greedy descent loop.  Each iteration:
     *
     *   1. Refresh sVar1 (current cell's cost) and its stage byte.
     *      If sVar1 < 2 (= cost 1, which means "at destination"),
     *      we're done — return the step count so far.
     *   2. Look at all 8 neighbors.  Accept a neighbor iff its
     *      stage byte is within ±1 of the current stage AND its
     *      cost is non-zero.  Pick the lowest cost; tiebreak by
     *      "the direction toward the actual destination" (uVar6).
     *   3. If no candidate found, return MAX_STEPS (stuck).
     *   4. Step into the chosen neighbor; record cur_col/cur_row
     *      into path_out at the step's slot.
     *
     * Max 149 iterations because the loop exits when local_1c >=
     * 0x96 (=150) after a successful step.  Slot 0 is the start
     * cell (already stamped above); slot N is the position
     * AFTER N steps. */
    int step_i = 0;
    for (;;) {
        /* Re-read sVar1: this is the cost at the CURRENT walker
         * position.  Done at the top of each loop iter even though
         * the previous iter could have computed it. */
        int16_t cur_cost = s->cost_map[cur_idx];
        if (cur_cost < 2) {
            /* Reached cost 1 — at the destination.  Return the
             * number of steps taken (slot count, 0-based -> step_i). */
            return step_i;
        }

        /* Record the walker's pre-step position into slot step_i.
         * Note retail writes the CURRENT position (local_20/24) here,
         * which is the position the walker is ABOUT to step out of. */
        s->path_out[step_i * 2 + 0] = (uint8_t)cur_col;
        s->path_out[step_i * 2 + 1] = (uint8_t)cur_row;

        uint8_t cur_stage = s->stage_scratch[cur_idx];

        /* Tiebreaker direction: which 8-way step from here would
         * head straight toward the destination. */
        int prefer_dir = engine_path_direction_8way(cur_col, cur_row,
                                                    dest_col, dest_row);

        /* Scan the 8 neighbors. */
        unsigned chosen_dir = 0xffffffff;
        int16_t best_cost = cur_cost;
        for (int d = 0; d < 8; d++) {
            int n_idx = cur_idx + k_dir_offset[d];
            int16_t n_cost = s->cost_map[n_idx];
            uint8_t n_stage = s->stage_scratch[n_idx];

            /* Stage-band predicate: neighbor's stage must be equal
             * to or within ±1 of current.  Retail collapses this
             * into "(n_stage == cur_stage) || ((cur_stage-1) <=
             * n_stage && n_stage <= (cur_stage+1))" — the first
             * disjunct is logically redundant with the second but
             * preserved in the decomp; we keep that shape. */
            int stage_ok = (n_stage == cur_stage) ||
                           ((int)(cur_stage - 1) <= (int)n_stage &&
                            (int)n_stage <= (int)cur_stage + 1);

            if (!stage_ok || n_cost == 0) continue;

            if (n_cost < best_cost) {
                chosen_dir = (unsigned)d;
                best_cost  = n_cost;
            } else if (n_cost == best_cost &&
                       d == (prefer_dir & 0xff)) {
                /* Tiebreak: same cost as current best, but heading
                 * straight at the destination — prefer this dir. */
                chosen_dir = (unsigned)d;
                best_cost  = n_cost;
            }
        }

        if (chosen_dir == 0xffffffff) {
            /* Stuck — no valid neighbor.  Return the failure sentinel. */
            return ENGINE_PATH_MAX_STEPS;
        }

        /* Step into the chosen neighbor.  Update both (col, row) and
         * the linear idx in lockstep so subsequent reads stay
         * consistent with retail's local_10 cursor. */
        switch (chosen_dir) {
        case 0: /* up        */ cur_row -= 1;             cur_idx -= 80; break;
        case 1: /* up-right  */ cur_row -= 1; cur_col += 1; cur_idx -= 79; break;
        case 2: /* right     */ cur_col += 1;             cur_idx += 1;  break;
        case 3: /* down-right*/ cur_col += 1; cur_row += 1; cur_idx += 81; break;
        case 4: /* down      */ cur_row += 1;             cur_idx += 80; break;
        case 5: /* down-left */ cur_col -= 1; cur_row += 1; cur_idx += 79; break;
        case 6: /* left      */ cur_col -= 1;             cur_idx -= 1;  break;
        case 7: /* up-left   */ cur_col -= 1; cur_row -= 1; cur_idx -= 81; break;
        }

        step_i++;
        if (step_i >= ENGINE_PATH_MAX_STEPS) {
            /* Loop iteration cap — retail's `local_1c < 0x96` falls
             * through to the iVar5 = 0x96 assignment. */
            return ENGINE_PATH_MAX_STEPS;
        }
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Expanding-square nearest-matching search
 * ────────────────────────────────────────────────────────────────── */

int engine_path_find_nearest_matching(engine_path_state_t *s,
                                      int skip_x, int skip_y,
                                      int start_col, int start_row)
{
    /* Radius 0..29 (retail loops while `local_28 <= 0x1d`).  Each
     * iter expands the bounding box outward by one cell and scans
     * its interior. */
    for (int r = 0; r <= 0x1d; r++) {
        /* Compute the box: top-left (x0, y0) of size (w x h).
         * Retail's `local_8 = local_28 * 2 + 1` is the base extent
         * (always odd), shrunk by overflow on either side. */
        int x0 = start_col - r;
        int y0 = start_row - r;
        int base_extent = r * 2 + 1;

        int w, h;
        if (x0 < 0) {
            /* Left overflow: shrink from the left by (-x0). */
            w  = base_extent + x0;   /* (extent - |x0|) */
            x0 = 0;
        } else {
            w = base_extent;
            if (ENGINE_PATH_GRID_W < x0 + w) {
                /* Right overflow: shrink from the right.  Retail's
                 * arithmetic is `local_c = local_8 - (local_1c +
                 * local_8 + -0x50)` — algebraically equivalent to
                 * `local_8 - (x_end - 80)` which is the right-trim. */
                w = w - (x0 + w - ENGINE_PATH_GRID_W);
            }
        }
        if (y0 < 0) {
            h  = base_extent + y0;
            y0 = 0;
        } else {
            h = base_extent;
            if (ENGINE_PATH_GRID_H < y0 + h) {
                h = h - (y0 + h - ENGINE_PATH_GRID_H);
            }
        }

        /* Box scan in row-major order.  Track the best-cost cell
         * whose stage matches the start cell's stage.  Cost 0 means
         * "impassable / nothing here"; we skip it. */
        int16_t best_cost = 900;
        int     best_idx  = 0;
        int     cur_idx   = y0 * ENGINE_PATH_GRID_W + x0;

        uint8_t start_stage =
            s->stage_scratch[start_row * ENGINE_PATH_GRID_W + start_col];

        for (int row_i = y0; row_i < y0 + h; row_i++) {
            for (int col_i = x0; col_i < x0 + w; col_i++) {
                int16_t cell_cost  = s->cost_map[cur_idx];
                uint8_t cell_stage = s->stage_scratch[cur_idx];

                if (cell_stage == start_stage) {
                    /* In-band: consider it as a candidate. */
                    if (cell_cost != 0 && cell_cost < best_cost) {
                        best_idx  = cur_idx;
                        best_cost = cell_cost;
                    }
                    /* Skip-point hit: an in-band cell whose
                     * (col, row) matches (skip_x, skip_y).
                     *
                     * Retail writes DAT_00569570 = col_i and
                     * DAT_00569578 = row_i as a side effect of the
                     * scan WALK (not the match), so by the time we
                     * see the skip-point hit, nearest_col/row both
                     * already hold the current (col, row).  The
                     * caller's contract is: a 0 return means
                     * "no usable cell" — and retail leaves the side-
                     * effect globals at "the last cell we walked".
                     *
                     * We preserve that side effect by stamping the
                     * cell into nearest_col/row on every loop iter
                     * BEFORE checking the skip predicate. */
                    s->nearest_col = col_i;
                    s->nearest_row = row_i;
                    if (skip_x == col_i && skip_y == row_i) {
                        return 0;
                    }
                }
                cur_idx++;
            }
            /* End-of-row stride correction: skip past the cells
             * outside the box on this row to land on the start of
             * the next row's box. */
            cur_idx += (ENGINE_PATH_GRID_W - w);
        }

        if (best_idx != 0) {
            /* Found a best-cost match within this radius.  Report
             * it and return success. */
            s->nearest_col = best_idx % ENGINE_PATH_GRID_W;
            s->nearest_row = best_idx / ENGINE_PATH_GRID_W;
            return 1;
        }
        /* else: no in-band non-zero-cost cell in this box; expand. */
    }
    /* Exhausted radius 0..29 with no match.  Retail loops `while
     * (true)` and returns 0 only from the early skip-point exit OR
     * the radius check at the top.  We mirror by returning 0 here. */
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Stash into per-army slot
 * ────────────────────────────────────────────────────────────────── */

void engine_path_stash_into_army(engine_path_state_t *s,
                                 uint8_t *army_slot,
                                 int step_count)
{
    /* Stamp the step count as a 16-bit short at +0x16 in the slot. */
    *(int16_t *)(army_slot + ENGINE_PATH_ARMY_OFF_LEN) =
        (int16_t)step_count;

    /* Stamp the "active" flag at +0x144 to 1. */
    army_slot[ENGINE_PATH_ARMY_OFF_ACTIVE] = 1;

    /* Copy the (col, row) byte pairs into the slot's path area at
     * +0x18.  Retail iterates `local_8 < param_2` so the last slot
     * written is index step_count-1. */
    for (int i = 0; i < step_count; i++) {
        army_slot[ENGINE_PATH_ARMY_OFF_PATH + i * 2 + 0] =
            s->path_out[i * 2 + 0];
        army_slot[ENGINE_PATH_ARMY_OFF_PATH + i * 2 + 1] =
            s->path_out[i * 2 + 1];
    }
}
