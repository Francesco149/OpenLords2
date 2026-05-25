/*
 * Unit tests for engine_path — army pathfinding cluster.
 *
 * Each helper plus the greedy search plus the expanding-square search
 * plus the army-slot stash all get isolated tests that exercise the
 * documented contract and preserved retail quirks.
 *
 * Test data: tests synthesize their own cost_map / stage_scratch
 * inputs rather than depending on a real Castles.dat — keeps tests
 * fully hermetic and reproducible.
 */

#include "t.h"
#include "engine_path.h"
#include <string.h>

/* Helper: clear a state struct to all zero.  All cost_map cells = 0,
 * meaning "impassable" by the search predicates — tests then sprinkle
 * cost > 0 onto the cells they want walkable. */
static void state_zero(engine_path_state_t *s) {
    memset(s, 0, sizeof *s);
}

/* Helper: set every cell on the outer ring to cost 999 (impassable)
 * to mimic the BFS contract.  Interior cells stay at 0; tests fill
 * them in. */
static void state_set_walls(engine_path_state_t *s, int16_t wall_cost) {
    for (int c = 0; c < ENGINE_PATH_GRID_W; c++) {
        s->cost_map[c] = wall_cost;
        s->cost_map[(ENGINE_PATH_GRID_H - 1) * ENGINE_PATH_GRID_W + c] = wall_cost;
    }
    for (int r = 0; r < ENGINE_PATH_GRID_H; r++) {
        s->cost_map[r * ENGINE_PATH_GRID_W] = wall_cost;
        s->cost_map[r * ENGINE_PATH_GRID_W + (ENGINE_PATH_GRID_W - 1)] = wall_cost;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Chebyshev distance helper
 * ────────────────────────────────────────────────────────────────── */

int test_engine_path_chebyshev_zero(void) {
    engine_path_state_t s; state_zero(&s);
    s.last_dx = 99; s.last_dy = 99;  /* pre-stomp to verify reset */
    int r = engine_path_chebyshev_distance(&s, 5, 5, 5, 5);
    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(s.last_dx, 0);
    T_ASSERT_EQ_I(s.last_dy, 0);
    return 0;
}

int test_engine_path_chebyshev_x_dominant(void) {
    engine_path_state_t s; state_zero(&s);
    int r = engine_path_chebyshev_distance(&s, 10, 3, 2, 1);
    T_ASSERT_EQ_I(r, 8);              /* max(8, 2) */
    T_ASSERT_EQ_I(s.last_dx, 8);
    T_ASSERT_EQ_I(s.last_dy, 2);
    return 0;
}

int test_engine_path_chebyshev_y_dominant(void) {
    engine_path_state_t s; state_zero(&s);
    int r = engine_path_chebyshev_distance(&s, 1, 10, 4, 0);
    T_ASSERT_EQ_I(r, 10);             /* max(3, 10) */
    T_ASSERT_EQ_I(s.last_dx, 3);
    T_ASSERT_EQ_I(s.last_dy, 10);
    return 0;
}

int test_engine_path_chebyshev_negative_args_use_signed_subtraction(void) {
    /* Retail compares signed: if (param_3 < param_1) dx = param_1 -
     * param_3 — works for negative inputs since subtraction is in
     * 32-bit signed int. */
    engine_path_state_t s; state_zero(&s);
    int r = engine_path_chebyshev_distance(&s, -5, 0, 3, 0);
    T_ASSERT_EQ_I(s.last_dx, 8);      /* 3 - (-5) = 8 */
    T_ASSERT_EQ_I(r, 8);
    return 0;
}

int test_engine_path_chebyshev_side_effect_preserved(void) {
    /* Subsequent callers can read last_dx/dy without recomputing —
     * preserved as a global-side-effect equivalent. */
    engine_path_state_t s; state_zero(&s);
    engine_path_chebyshev_distance(&s, 0, 0, 7, 3);
    T_ASSERT_EQ_I(s.last_dx, 7);
    T_ASSERT_EQ_I(s.last_dy, 3);
    /* Another call overwrites. */
    engine_path_chebyshev_distance(&s, 5, 5, 5, 5);
    T_ASSERT_EQ_I(s.last_dx, 0);
    T_ASSERT_EQ_I(s.last_dy, 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * 8-way direction helper
 * ────────────────────────────────────────────────────────────────── */

int test_engine_path_direction_equal_is_8(void) {
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 5, 5), 8);
    return 0;
}

int test_engine_path_direction_cardinals(void) {
    /* up    (to has same x, smaller y) */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 5, 3), 0);
    /* down  (same x, larger y) */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 5, 9), 4);
    /* right (larger x, same y) */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 9, 5), 2);
    /* left  (smaller x, same y) */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 1, 5), 6);
    return 0;
}

int test_engine_path_direction_intercardinals(void) {
    /* up-right   = 1 */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 7, 3), 1);
    /* down-right = 3 */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 7, 7), 3);
    /* down-left  = 5 */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 3, 7), 5);
    /* up-left    = 7 */
    T_ASSERT_EQ_I(engine_path_direction_8way(5, 5, 3, 3), 7);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Greedy search — short-circuits and bad-start guards
 * ────────────────────────────────────────────────────────────────── */

int test_engine_path_greedy_chebyshev_zero_returns_max_steps(void) {
    /* Start == dest → chebyshev = 0 → return 150 (MAX_STEPS sentinel)
     * BEFORE even looking at the cost map. */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    s.cost_map[10 * 80 + 10] = 5;     /* would be a fine cost, but unused */

    int r = engine_path_search_greedy(&s, /*dest*/ 10, 10, /*start*/ 10, 10);
    T_ASSERT_EQ_I(r, ENGINE_PATH_MAX_STEPS);
    /* Slot 0 still stamped with the start cell. */
    T_ASSERT_EQ_I(s.path_out[0], 10);
    T_ASSERT_EQ_I(s.path_out[1], 10);
    return 0;
}

int test_engine_path_greedy_adjacent_returns_max_steps(void) {
    /* Chebyshev = 1 (adjacent cells) also short-circuits to 150 —
     * "no walk needed, caller will move directly". */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    int r = engine_path_search_greedy(&s, 11, 10, 10, 10);
    T_ASSERT_EQ_I(r, ENGINE_PATH_MAX_STEPS);
    return 0;
}

int test_engine_path_greedy_bad_start_cost_zero_returns_zero(void) {
    /* Cost at start == 0 means "not part of the BFS" — search bails
     * with 0 (and still stamps slot 0). */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    /* Dest is far away so chebyshev >= 2; cost at start = 0. */
    int r = engine_path_search_greedy(&s, 30, 30, 10, 10);
    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(s.path_out[0], 10);
    T_ASSERT_EQ_I(s.path_out[1], 10);
    return 0;
}

int test_engine_path_greedy_bad_start_cost_wall_returns_zero(void) {
    /* Cost at start > 997 (e.g. 999, the wall sentinel) → return 0. */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    s.cost_map[10 * 80 + 10] = 999;
    int r = engine_path_search_greedy(&s, 30, 30, 10, 10);
    T_ASSERT_EQ_I(r, 0);
    return 0;
}

int test_engine_path_greedy_start_idx_recorded(void) {
    /* start_idx should be stamped to row*80+col regardless of return path. */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    s.cost_map[15 * 80 + 20] = 999;   /* will trigger bad-start return 0 */
    engine_path_search_greedy(&s, 30, 30, 20, 15);
    T_ASSERT_EQ_I(s.start_idx, 15 * 80 + 20);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Greedy search — actual descent
 * ────────────────────────────────────────────────────────────────── */

int test_engine_path_greedy_horizontal_descent(void) {
    /* Set up: 80x80 grid, walls on the border, cost gradient along
     * row 40 from col 30 (cost 1) eastward, increasing by 1 each cell
     * up to col 35 (cost 6).  All stage bytes 0 (uniform terrain).
     * Search from (35, 40) toward (30, 40) — should walk westward
     * for 5 steps. */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    for (int c = 30; c <= 35; c++) {
        s.cost_map[40 * 80 + c] = (int16_t)(c - 29);   /* col 30 → 1, col 35 → 6 */
    }

    int r = engine_path_search_greedy(&s, /*dest*/ 30, 40, /*start*/ 35, 40);
    /* Step counts: start at col 35 (cost 6); each step decreases cost
     * by 1.  Loop exits when cost <= 1.  Steps taken: 35→34→33→32→31→30.
     * After 5 steps we're at cost 1 — return 5. */
    T_ASSERT_EQ_I(r, 5);

    /* Slot 0 = start position (35, 40). */
    T_ASSERT_EQ_I(s.path_out[0], 35);
    T_ASSERT_EQ_I(s.path_out[1], 40);

    /* Slot 4 = (31, 40) — position just before the final step. */
    T_ASSERT_EQ_I(s.path_out[4 * 2 + 0], 31);
    T_ASSERT_EQ_I(s.path_out[4 * 2 + 1], 40);
    return 0;
}

int test_engine_path_greedy_stuck_returns_max_steps(void) {
    /* Pathological setup: start cell has positive cost but no neighbor
     * is walkable (all 8 are cost 0 = "not part of the BFS").  Greedy
     * search should bail with MAX_STEPS. */
    engine_path_state_t s; state_zero(&s);
    /* No walls — every cell defaults to cost 0.  Make ONE cell have
     * cost 5 (the start). */
    s.cost_map[40 * 80 + 40] = 5;

    int r = engine_path_search_greedy(&s, 50, 50, 40, 40);
    T_ASSERT_EQ_I(r, ENGINE_PATH_MAX_STEPS);
    return 0;
}

int test_engine_path_greedy_stage_band_blocks_off_band_neighbor(void) {
    /* Start has stage 0; only neighbor with positive cost is at stage
     * 5 (off-band by > 1) — should be rejected, and search bails. */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    s.cost_map[40 * 80 + 40] = 5;
    s.cost_map[40 * 80 + 39] = 2;     /* would be a good step */
    s.stage_scratch[40 * 80 + 39] = 5;/* but stage band rejects it */

    int r = engine_path_search_greedy(&s, 30, 40, 40, 40);
    T_ASSERT_EQ_I(r, ENGINE_PATH_MAX_STEPS);
    return 0;
}

int test_engine_path_greedy_stage_band_accepts_plus_one(void) {
    /* Start stage 5, neighbor stage 6 (±1) — accepted. */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    s.cost_map[40 * 80 + 40] = 3;
    s.cost_map[40 * 80 + 39] = 1;
    s.stage_scratch[40 * 80 + 40] = 5;
    s.stage_scratch[40 * 80 + 39] = 6;

    int r = engine_path_search_greedy(&s, 30, 40, 40, 40);
    T_ASSERT_EQ_I(r, 1);              /* one step lands on cost 1 cell */
    return 0;
}

int test_engine_path_greedy_tiebreak_prefers_direction_to_dest(void) {
    /* Two neighbors both have cost 1 — tiebreak picks the one whose
     * 8-way index matches the direction toward dest.
     *
     * Start (10, 10), dest (12, 10) → prefer_dir = 2 (right).  Make
     * the LEFT (dir 6) and RIGHT (dir 2) neighbors both cost 1.  The
     * scan visits dir 6 first by index but should rewrite the choice
     * to dir 2 because of the tiebreaker. */
    engine_path_state_t s; state_zero(&s);
    state_set_walls(&s, 999);
    s.cost_map[10 * 80 + 10] = 5;
    s.cost_map[10 * 80 +  9] = 1;     /* left  (dir 6) */
    s.cost_map[10 * 80 + 11] = 1;     /* right (dir 2) */

    int r = engine_path_search_greedy(&s, 12, 10, 10, 10);
    T_ASSERT_EQ_I(r, 1);              /* one step then at cost 1 → exit */
    /* path_out[0] is the start cell; path_out[1] is recorded BEFORE the
     * step is taken, but on iter 2 we exit before recording — so slot
     * 1 isn't touched in this case.  Verify slot 0 alone. */
    T_ASSERT_EQ_I(s.path_out[0], 10);
    T_ASSERT_EQ_I(s.path_out[1], 10);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Expanding-square nearest-matching search
 * ────────────────────────────────────────────────────────────────── */

int test_engine_path_nearest_finds_immediate_self_cell(void) {
    /* Radius 0 covers just the start cell.  If start's stage matches
     * itself (trivially true) and cost is non-zero, it's the match. */
    engine_path_state_t s; state_zero(&s);
    s.cost_map[40 * 80 + 40] = 5;     /* non-zero cost at start */

    int r = engine_path_find_nearest_matching(&s,
                                              /*skip*/ 0, 0,
                                              /*start*/ 40, 40);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(s.nearest_col, 40);
    T_ASSERT_EQ_I(s.nearest_row, 40);
    return 0;
}

int test_engine_path_nearest_skip_point_returns_zero(void) {
    /* Cell at start matches and has non-zero cost, but skip_x/y point
     * to that same cell → return 0 (and side-effect globals still
     * hold the cell's coords). */
    engine_path_state_t s; state_zero(&s);
    s.cost_map[40 * 80 + 40] = 5;

    int r = engine_path_find_nearest_matching(&s,
                                              /*skip*/ 40, 40,
                                              /*start*/ 40, 40);
    T_ASSERT_EQ_I(r, 0);
    /* Side-effect globals still updated. */
    T_ASSERT_EQ_I(s.nearest_col, 40);
    T_ASSERT_EQ_I(s.nearest_row, 40);
    return 0;
}

int test_engine_path_nearest_expands_to_radius_1(void) {
    /* Start cell has cost 0 (skip), one cell at (41, 40) has cost 5.
     * Radius 0 sees no match; radius 1 finds it. */
    engine_path_state_t s; state_zero(&s);
    s.cost_map[40 * 80 + 41] = 5;     /* right of start */
    /* Start cell stage = 0; (41, 40) stage = 0 — both match. */

    int r = engine_path_find_nearest_matching(&s, 0, 0, 40, 40);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(s.nearest_col, 41);
    T_ASSERT_EQ_I(s.nearest_row, 40);
    return 0;
}

int test_engine_path_nearest_stage_mismatch_rejected(void) {
    /* Candidate cell has non-zero cost but different stage → skipped.
     * No other candidates → return 0. */
    engine_path_state_t s; state_zero(&s);
    s.cost_map[40 * 80 + 41] = 5;
    s.stage_scratch[40 * 80 + 41] = 9;  /* start stage is 0 */

    int r = engine_path_find_nearest_matching(&s, 99, 99, 40, 40);
    T_ASSERT_EQ_I(r, 0);
    return 0;
}

int test_engine_path_nearest_clamps_box_at_edges(void) {
    /* Start near top-left corner: (1, 1).  Radius 1 box would normally
     * be (0..2, 0..2); the clamp prevents reading off-grid.  Make a
     * candidate at (2, 0) — note NOT at (0, 0) because of quirk #13
     * (idx 0 invisible to the picker). */
    engine_path_state_t s; state_zero(&s);
    s.cost_map[0 * 80 + 2] = 7;       /* (col=2, row=0) */

    int r = engine_path_find_nearest_matching(&s, 99, 99, 1, 1);
    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(s.nearest_col, 2);
    T_ASSERT_EQ_I(s.nearest_row, 0);
    return 0;
}

int test_engine_path_nearest_quirk13_topleft_invisible(void) {
    /* Engine quirk #13: a matching cell at linear index 0 (col=0,
     * row=0) is invisible to the picker because retail initializes
     * best_idx to 0 and uses `best_idx != 0` as the "found a match"
     * sentinel.  Preserved verbatim.
     *
     * Setup: only the (0,0) cell has cost; from (1,1) we should NOT
     * find it (returns 0). */
    engine_path_state_t s; state_zero(&s);
    s.cost_map[0] = 7;                /* idx 0 = (0,0) — invisible */

    int r = engine_path_find_nearest_matching(&s, 99, 99, 1, 1);
    T_ASSERT_EQ_I(r, 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Army-slot stash
 * ────────────────────────────────────────────────────────────────── */

int test_engine_path_stash_copies_path_and_flags(void) {
    engine_path_state_t s; state_zero(&s);
    /* Synthesize a short path: 4 steps at (1,1) → (2,1) → (2,2) → (3,2). */
    s.path_out[0] = 1; s.path_out[1] = 1;
    s.path_out[2] = 2; s.path_out[3] = 1;
    s.path_out[4] = 2; s.path_out[5] = 2;
    s.path_out[6] = 3; s.path_out[7] = 2;

    uint8_t slot[ENGINE_PATH_ARMY_SLOT_STRIDE];
    memset(slot, 0xee, sizeof slot);  /* poison */

    engine_path_stash_into_army(&s, slot, 4);

    /* Length stamped as int16 at +0x16. */
    int16_t got_len = *(int16_t *)(slot + ENGINE_PATH_ARMY_OFF_LEN);
    T_ASSERT_EQ_I(got_len, 4);

    /* Active flag = 1 at +0x144. */
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_ACTIVE], 1);

    /* Path bytes copied at +0x18..+0x18+7. */
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_PATH + 0], 1);
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_PATH + 1], 1);
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_PATH + 6], 3);
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_PATH + 7], 2);

    /* Byte after the copied region should be untouched poison.
     * Step count = 4 → 8 path bytes → first untouched byte is at
     * +0x18 + 8 = +0x20. */
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_PATH + 8], 0xee);
    return 0;
}

int test_engine_path_stash_zero_step_count_only_sets_active(void) {
    /* step_count == 0 → no path bytes copied, but len and active are
     * still written. */
    engine_path_state_t s; state_zero(&s);

    uint8_t slot[ENGINE_PATH_ARMY_SLOT_STRIDE];
    memset(slot, 0xee, sizeof slot);

    engine_path_stash_into_army(&s, slot, 0);

    T_ASSERT_EQ_I(*(int16_t *)(slot + ENGINE_PATH_ARMY_OFF_LEN), 0);
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_ACTIVE], 1);
    /* No path bytes touched. */
    T_ASSERT_EQ_I(slot[ENGINE_PATH_ARMY_OFF_PATH], 0xee);
    return 0;
}
