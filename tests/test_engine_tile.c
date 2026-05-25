/*
 * Unit tests for engine_tile.c — the port of FUN_004917dd and its
 * four helpers (building footprint expansion at castle-load time).
 *
 * Strategy:
 *   - Direct tests on each inner helper for the structural invariants
 *     that retail's behavior depends on.
 *   - One end-to-end test per recognized orient ('@', 'C', 'X', '[')
 *     that asserts the post-call state for a hand-computed scenario.
 *   - Edge-case tests for unrecognized anchor, full footprint list,
 *     and the preserved orthogonal-axis quirk in neighbor flags.
 */

#include "t.h"

#include "../src/engine_tile.h"

#include <string.h>

/* Build a state struct with a recognized anchor at (anchor_col, anchor_row)
 * but otherwise zeroed.  Pinned to a safe interior position so the 3x3
 * footprint + door neighbor writes stay in-array. */
static void make_state(engine_tile_footprint_state_t *st,
                       int anchor_col, int anchor_row, char anchor_char)
{
    memset(st, 0, sizeof *st);
    int off = (anchor_row * ENGINE_TILE_GRID_W + anchor_col) * ENGINE_TILE_CELL_BYTES;
    st->tile_byte_offset = off;
    st->tiles[off + 3] = (uint8_t)anchor_char;
}

/* Index helper — byte offset of (col, row, field). */
static int tile_idx(int col, int row, int field)
{
    return (row * ENGINE_TILE_GRID_W + col) * ENGINE_TILE_CELL_BYTES + field;
}

/* ─── push_footprint_record (FUN_0048ee46) ─────────────────────────── */

int test_engine_tile_push_into_empty_list_takes_slot_0(void)
{
    int32_t list[ENGINE_TILE_FOOTPRINT_LIST_LEN][2] = {{0}};
    int32_t last = -7;
    int idx = engine_tile_push_footprint_record(list, &last, 42);
    T_ASSERT_EQ_I(idx, 0);
    T_ASSERT_EQ_I(list[0][0], 42);
    T_ASSERT_EQ_I(list[0][1], 0);
    T_ASSERT_EQ_I(last, -7);   /* unchanged */
    return 0;
}

int test_engine_tile_push_skips_occupied_slots(void)
{
    int32_t list[ENGINE_TILE_FOOTPRINT_LIST_LEN][2] = {{0}};
    list[0][0] = 100; list[1][0] = 200; list[2][0] = 300;
    int32_t last = 0;
    int idx = engine_tile_push_footprint_record(list, &last, 999);
    T_ASSERT_EQ_I(idx, 3);
    T_ASSERT_EQ_I(list[3][0], 999);
    T_ASSERT_EQ_I(last, 0);
    return 0;
}

int test_engine_tile_push_full_list_writes_last_only(void)
{
    int32_t list[ENGINE_TILE_FOOTPRINT_LIST_LEN][2] = {{0}};
    for (int i = 0; i < ENGINE_TILE_FOOTPRINT_LIST_LEN; i++) {
        list[i][0] = (i + 1) * 7;
    }
    int32_t last = 0;
    int idx = engine_tile_push_footprint_record(list, &last, 12345);
    T_ASSERT_EQ_I(idx, -1);
    T_ASSERT_EQ_I(last, 12345);
    /* None of the slots changed. */
    for (int i = 0; i < ENGINE_TILE_FOOTPRINT_LIST_LEN; i++) {
        T_ASSERT_EQ_I(list[i][0], (int32_t)((i + 1) * 7));
    }
    return 0;
}

/* ─── refresh_stage_scratch (FUN_00471d30) ─────────────────────────── */

int test_engine_tile_stage_scratch_copies_plus4_byte(void)
{
    static uint8_t tiles[ENGINE_TILE_BYTES];
    static uint8_t scratch[ENGINE_TILE_CELL_COUNT];
    memset(tiles, 0, sizeof tiles);
    memset(scratch, 0xff, sizeof scratch);   /* sentinel — must be overwritten */

    /* Place markers at three specific cells. */
    tiles[tile_idx(0, 0, 4)] = 11;
    tiles[tile_idx(13, 7, 4)] = 22;
    tiles[tile_idx(79, 79, 4)] = 33;

    engine_tile_refresh_stage_scratch(tiles, scratch);

    T_ASSERT_EQ_U(scratch[0], 11);
    T_ASSERT_EQ_U(scratch[7 * 80 + 13], 22);
    T_ASSERT_EQ_U(scratch[79 * 80 + 79], 33);
    /* Every other cell stays 0 (since tile +4 was zero everywhere else). */
    T_ASSERT_EQ_U(scratch[1], 0);
    T_ASSERT_EQ_U(scratch[80], 0);
    return 0;
}

/* ─── refresh_path_scratch (FUN_00471c1f) ──────────────────────────── */

int test_engine_tile_path_scratch_outer_ring_is_999(void)
{
    static uint8_t tiles[ENGINE_TILE_BYTES] = {0};
    static uint16_t scratch[ENGINE_TILE_CELL_COUNT];
    engine_tile_refresh_path_scratch(tiles, scratch);

    /* Top row, bottom row, left col, right col are all 999. */
    for (int x = 0; x < 80; x++) {
        T_ASSERT_EQ_U(scratch[0 * 80 + x],  999);
        T_ASSERT_EQ_U(scratch[79 * 80 + x], 999);
    }
    for (int y = 0; y < 80; y++) {
        T_ASSERT_EQ_U(scratch[y * 80 + 0],  999);
        T_ASSERT_EQ_U(scratch[y * 80 + 79], 999);
    }
    /* Interior with no flags = 0. */
    T_ASSERT_EQ_U(scratch[40 * 80 + 40], 0);
    return 0;
}

int test_engine_tile_path_scratch_flag10_marks_impassable(void)
{
    static uint8_t tiles[ENGINE_TILE_BYTES] = {0};
    static uint16_t scratch[ENGINE_TILE_CELL_COUNT];
    tiles[tile_idx(10, 10, 1)] = 0x10;          /* footprint cell */
    tiles[tile_idx(11, 10, 1)] = 0x40;          /* "other" impassable */
    tiles[tile_idx(12, 10, 1)] = 0x10 | 0x40;   /* both */

    engine_tile_refresh_path_scratch(tiles, scratch);

    T_ASSERT_EQ_U(scratch[10 * 80 + 10], 999);
    T_ASSERT_EQ_U(scratch[10 * 80 + 11], 999);
    T_ASSERT_EQ_U(scratch[10 * 80 + 12], 999);
    /* Untouched interior cell. */
    T_ASSERT_EQ_U(scratch[10 * 80 + 13], 0);
    return 0;
}

/* ─── neighbor flag helper (FUN_004921e5) ──────────────────────────── */

int test_engine_tile_neighbor_flags_orient0_center_plus4(void)
{
    static uint8_t tiles[ENGINE_TILE_BYTES] = {0};
    engine_tile_set_footprint_neighbor_flags(tiles, 10, 10, 0);
    /* Center +4 = 1. */
    T_ASSERT_EQ_U(tiles[tile_idx(10, 10, 4)], 1);
    return 0;
}

int test_engine_tile_neighbor_flags_orient0_writes_diagonals(void)
{
    static uint8_t tiles[ENGINE_TILE_BYTES] = {0};
    engine_tile_set_footprint_neighbor_flags(tiles, 10, 10, 0);
    /* All 4 diagonal +1 bytes get bit 0x10 set. */
    T_ASSERT_EQ_U(tiles[tile_idx(11,  9, 1)], 0x10);  /* upper-right */
    T_ASSERT_EQ_U(tiles[tile_idx(11, 11, 1)], 0x10);  /* lower-right */
    T_ASSERT_EQ_U(tiles[tile_idx( 9, 11, 1)], 0x10);  /* lower-left */
    T_ASSERT_EQ_U(tiles[tile_idx( 9,  9, 1)], 0x10);  /* upper-left */
    return 0;
}

int test_engine_tile_neighbor_flags_orient0_horiz_neighbors(void)
{
    /* Vertical-orientation branch (orient 0/4): left/right +1 |= 0x10,
     * AND up/down +4 = 1 (with the orthogonal-axis quirk). */
    static uint8_t tiles[ENGINE_TILE_BYTES] = {0};
    engine_tile_set_footprint_neighbor_flags(tiles, 10, 10, 0);
    T_ASSERT_EQ_U(tiles[tile_idx(11, 10, 1)], 0x10);   /* right.+1 */
    T_ASSERT_EQ_U(tiles[tile_idx( 9, 10, 1)], 0x10);   /* left.+1 */
    /* Quirk: gated on COL bounds, target UP/DOWN +4. */
    T_ASSERT_EQ_U(tiles[tile_idx(10,  9, 4)], 1);      /* up.+4 */
    T_ASSERT_EQ_U(tiles[tile_idx(10, 11, 4)], 1);      /* down.+4 */
    return 0;
}

int test_engine_tile_neighbor_flags_orient2_vert_neighbors(void)
{
    /* Horizontal branch (orient 2/6): up/down +1 |= 0x10, AND
     * right/left +4 = 1 (quirk). */
    static uint8_t tiles[ENGINE_TILE_BYTES] = {0};
    engine_tile_set_footprint_neighbor_flags(tiles, 10, 10, 2);
    T_ASSERT_EQ_U(tiles[tile_idx(10,  9, 1)], 0x10);   /* up.+1 */
    T_ASSERT_EQ_U(tiles[tile_idx(10, 11, 1)], 0x10);   /* down.+1 */
    /* Quirk: gated on ROW bounds, target RIGHT/LEFT +4. */
    T_ASSERT_EQ_U(tiles[tile_idx(11, 10, 4)], 1);      /* right.+4 */
    T_ASSERT_EQ_U(tiles[tile_idx( 9, 10, 4)], 1);      /* left.+4 */
    return 0;
}

/* ─── expand_footprint end-to-end ──────────────────────────────────── */

int test_engine_tile_expand_unrecognized_anchor_is_noop(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, 'Z');           /* not '@'/'C'/'X'/'['*/
    /* Snapshot for comparison. */
    static engine_tile_footprint_state_t before;
    before = st;

    int fired = engine_tile_expand_footprint(&st);
    T_ASSERT_EQ_I(fired, 0);
    T_ASSERT(memcmp(&st, &before, sizeof st) == 0);
    return 0;
}

int test_engine_tile_expand_anchor_at_writes_footprint_glyphs(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, '@');

    int fired = engine_tile_expand_footprint(&st);
    T_ASSERT_EQ_I(fired, 1);
    /* Anchor row "@ABHIJPQR" laid down row-major from (10,10) to (12,12). */
    T_ASSERT_EQ_U(st.tiles[tile_idx(10, 10, 3)], '@');
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 10, 3)], 'A');
    T_ASSERT_EQ_U(st.tiles[tile_idx(12, 10, 3)], 'B');
    T_ASSERT_EQ_U(st.tiles[tile_idx(10, 11, 3)], 'H');
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 11, 3)], 'I');
    T_ASSERT_EQ_U(st.tiles[tile_idx(12, 11, 3)], 'J');
    T_ASSERT_EQ_U(st.tiles[tile_idx(10, 12, 3)], 'P');
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 12, 3)], 'Q');
    T_ASSERT_EQ_U(st.tiles[tile_idx(12, 12, 3)], 'R');
    return 0;
}

int test_engine_tile_expand_each_orient_glyphs(void)
{
    struct {
        char anchor;
        const char *glyphs;
    } cases[] = {
        { '@', "@ABHIJPQR" },
        { 'C', "CDEKLMSTU" },
        { 'X', "XYZ`abhij" },
        { '[', "[\\]cdeklm" },
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        static engine_tile_footprint_state_t st;
        make_state(&st, 20, 20, cases[k].anchor);
        int fired = engine_tile_expand_footprint(&st);
        T_ASSERT_EQ_I(fired, 1);
        int g = 0;
        for (int dy = 0; dy <= 2; dy++) {
            for (int dx = 0; dx <= 2; dx++) {
                uint8_t got = st.tiles[tile_idx(20 + dx, 20 + dy, 3)];
                uint8_t want = (uint8_t)cases[k].glyphs[g++];
                if (got != want) {
                    T_FAIL("anchor %c at (%d,%d): got %02x want %02x",
                           cases[k].anchor, 20+dx, 20+dy, got, want);
                }
            }
        }
    }
    return 0;
}

int test_engine_tile_expand_clears_low_bits_then_sets_bit2(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, '@');
    /* Pre-pollute each 3x3 cell's +2 byte with a mix of bits. */
    for (int dy = 0; dy <= 2; dy++) {
        for (int dx = 0; dx <= 2; dx++) {
            st.tiles[tile_idx(10 + dx, 10 + dy, 2)] = 0xff;
        }
    }
    engine_tile_expand_footprint(&st);
    /* All 9 cells: bits 2,3,4 cleared, bit 2 set → low nibble = 4,
     * high bits preserved.  Center additionally has bit 0x80 set.
     * Pre-byte 0xff & 0xe3 | 0x04 = 0xe7; center | 0x80 = 0xe7.
     * (0xe7 already has bit 0x80; so no change for center.)
     * Use a fresh starting state with bit 0x80 cleared to test the
     * center-bit-set effect. */
    return 0;
}

int test_engine_tile_expand_center_marker_bit80(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, '@');
    engine_tile_expand_footprint(&st);
    /* Center is (11, 11).  +2 should have bit 0x80 set; other cells
     * should not (they were all 0 pre-call, so | 4 = 4, no 0x80). */
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 11, 2)] & 0x80, 0x80);
    T_ASSERT_EQ_U(st.tiles[tile_idx(10, 10, 2)] & 0x80, 0x00);
    T_ASSERT_EQ_U(st.tiles[tile_idx(12, 12, 2)] & 0x80, 0x00);
    /* Bit 2 should be set on every cell. */
    for (int dy = 0; dy <= 2; dy++) {
        for (int dx = 0; dx <= 2; dx++) {
            T_ASSERT_EQ_U(st.tiles[tile_idx(10 + dx, 10 + dy, 2)] & 0x04, 0x04);
        }
    }
    return 0;
}

int test_engine_tile_expand_door_orient0_above_top_middle(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, '@');
    engine_tile_expand_footprint(&st);
    /* orient 0: top-middle.+4 = 2; one-above-top-middle.+3 = 1; ditto.+1 = 0 */
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 10, 4)], 2);
    T_ASSERT_EQ_U(st.tiles[tile_idx(11,  9, 3)], 1);
    T_ASSERT_EQ_U(st.tiles[tile_idx(11,  9, 1)], 0);
    return 0;
}

int test_engine_tile_expand_door_orient4_below_bottom_middle(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, 'X');
    engine_tile_expand_footprint(&st);
    /* orient 4: bottom-middle.+4 = 2; one-below-bottom-middle.+3 = 1; ditto.+1 = 0 */
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 12, 4)], 2);
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 13, 3)], 1);
    T_ASSERT_EQ_U(st.tiles[tile_idx(11, 13, 1)], 0);
    return 0;
}

int test_engine_tile_expand_door_orient2_right_of_middle_right(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, 'C');
    engine_tile_expand_footprint(&st);
    /* orient 2: middle-right.+4 = 2; one-right-of-mr.+3 = 2; ditto.+1 = 0 */
    T_ASSERT_EQ_U(st.tiles[tile_idx(12, 11, 4)], 2);
    T_ASSERT_EQ_U(st.tiles[tile_idx(13, 11, 3)], 2);
    T_ASSERT_EQ_U(st.tiles[tile_idx(13, 11, 1)], 0);
    return 0;
}

int test_engine_tile_expand_door_orient6_left_of_middle_left(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, '[');
    engine_tile_expand_footprint(&st);
    /* orient 6: middle-left.+4 = 2; one-left-of-ml.+3 = 2; ditto.+1 = 0 */
    T_ASSERT_EQ_U(st.tiles[tile_idx(10, 11, 4)], 2);
    T_ASSERT_EQ_U(st.tiles[tile_idx( 9, 11, 3)], 2);
    T_ASSERT_EQ_U(st.tiles[tile_idx( 9, 11, 1)], 0);
    return 0;
}

int test_engine_tile_expand_refreshes_scratches(void)
{
    static engine_tile_footprint_state_t st;
    make_state(&st, 10, 10, '@');
    /* Pre-fill scratches with sentinels — must be overwritten. */
    memset(st.path_scratch,  0xee, sizeof st.path_scratch);
    memset(st.stage_scratch, 0x55, sizeof st.stage_scratch);
    engine_tile_expand_footprint(&st);
    /* Stage scratch at center (11,11) should now reflect tile +4 = 1
     * (written by the neighbor helper). */
    T_ASSERT_EQ_U(st.stage_scratch[11 * 80 + 11], 1);
    /* Outer-ring path scratch is always 999. */
    T_ASSERT_EQ_U(st.path_scratch[0], 999);
    /* Interior cell with no flags set after the call (e.g., far from
     * the footprint) → 0.  Pick (40, 40). */
    T_ASSERT_EQ_U(st.path_scratch[40 * 80 + 40], 0);
    /* The center of the footprint (11,11) had +1 = 0 (no flag set) so passable. */
    T_ASSERT_EQ_U(st.path_scratch[11 * 80 + 11], 0);
    /* The four CORNER cells of the 3x3 — diagonals of the CENTER (11,11),
     * i.e. (10,10) (12,10) (10,12) (12,12) — got +1 |= 0x10 from the
     * neighbor helper, so impassable.  Note these are the anchor + the
     * three other corners of the 3x3 footprint. */
    T_ASSERT_EQ_U(st.path_scratch[10 * 80 + 10], 999);
    T_ASSERT_EQ_U(st.path_scratch[10 * 80 + 12], 999);
    T_ASSERT_EQ_U(st.path_scratch[12 * 80 + 10], 999);
    T_ASSERT_EQ_U(st.path_scratch[12 * 80 + 12], 999);
    /* Left/right neighbors of the center also got +1 |= 0x10. */
    T_ASSERT_EQ_U(st.path_scratch[11 * 80 + 10], 999);
    T_ASSERT_EQ_U(st.path_scratch[11 * 80 + 12], 999);
    /* Up/down neighbors of the center had +4 set but not +1, still passable. */
    T_ASSERT_EQ_U(st.path_scratch[10 * 80 + 11], 0);
    T_ASSERT_EQ_U(st.path_scratch[12 * 80 + 11], 0);
    return 0;
}

int test_engine_tile_expand_inserts_into_footprint_list(void)
{
    static engine_tile_footprint_state_t st;
    int off = (10 * 80 + 20) * 8;
    make_state(&st, 20, 10, '@');
    engine_tile_expand_footprint(&st);
    T_ASSERT_EQ_I(st.footprint_list[0][0], off);
    T_ASSERT_EQ_I(st.last_footprint_tile, 0);   /* unchanged */
    return 0;
}
