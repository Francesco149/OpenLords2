/*
 * OpenLords2 — per-tile building footprint expansion.
 *
 * Port of retail FUN_004917dd @ 0x4917dd (599 bytes) and its four
 * helpers:
 *
 *   FUN_004921e5 @ 0x4921e5 (451 bytes) — neighbor flag/stage setter
 *   FUN_00471c1f @ 0x471c1f (273 bytes) — pathfinding scratch refresh
 *   FUN_00471d30 @ 0x471d30 (118 bytes) — stage scratch refresh
 *   FUN_0048ee46 @ 0x48ee46  (87 bytes) — recent-footprint list insert
 *
 * This is the "castle-load fixup" the save dispatch flagged as missing.
 * When the engine loads a Castles.dat scenario block, each cell is
 * initially populated only with its persisted +1 (flags) and +3 (anchor
 * char) bytes — runtime fields are unset.  For every tile whose +3
 * holds a building anchor char ('@', 'C', 'X', '['), the engine calls
 * FUN_004917dd to "expand" the footprint: lay down the 9-glyph 3x3
 * pattern, mark a door cell, refresh two whole-grid scratches, and
 * record the footprint in a small recent-changes list.
 *
 * See docs/findings/save-format.md ("Per-record in-memory layout") for
 * the per-tile struct fields touched.
 *
 * Pure function — every input/output explicit through the state
 * struct.  No globals read or written.  Matches the diff-harness
 * shape established by engine_state_hash.
 */

#ifndef OPENLORDS2_ENGINE_TILE_H
#define OPENLORDS2_ENGINE_TILE_H

#include <stdint.h>

/* Grid is 80 × 80 cells; each cell is 8 bytes; row stride 640 bytes. */
#define ENGINE_TILE_GRID_W       80
#define ENGINE_TILE_GRID_H       80
#define ENGINE_TILE_CELL_BYTES   8
#define ENGINE_TILE_ROW_BYTES    (ENGINE_TILE_GRID_W * ENGINE_TILE_CELL_BYTES)  /* 640 */
#define ENGINE_TILE_CELL_COUNT   (ENGINE_TILE_GRID_W * ENGINE_TILE_GRID_H)      /* 6400 */
#define ENGINE_TILE_BYTES        (ENGINE_TILE_CELL_COUNT * ENGINE_TILE_CELL_BYTES) /* 51200 */

/* The recent-footprint list: 19 (int, int) pairs.  Only the first int
 * of each pair is consumed by the insert in FUN_0048ee46 (checked for
 * ==0 to find a free slot).  The second int's semantics are opaque
 * from this function's POV. */
#define ENGINE_TILE_FOOTPRINT_LIST_LEN 19

/* All state read and written by engine_tile_expand_footprint() and its
 * helpers.  Mirrors of retail globals:
 *
 *   tiles[]             ← &DAT_005440e0 (51200 B, save schema entry 1)
 *   path_scratch[]      ← &DAT_00507250 (6400 × u16, "999/0" cost map)
 *   stage_scratch[]     ← &DAT_004f7d80 (6400 × u8, snapshot of tile +4)
 *   footprint_list[]    ← &DAT_00553c80 (19 × 2 × i32)
 *   last_footprint_tile ←  DAT_00553d18 (last param_1 seen)
 */
typedef struct {
    /* Byte offset (into tiles[]) of the top-LEFT cell of a 3x3 building
     * footprint.  Must be 8-aligned and represent a cell in [0, 6400).
     * Bounds for safety:
     *   col = (offset/8) % 80 ∈ [1, 77]   (need col-1 and col+2 in [0,79])
     *   row = (offset/8) / 80 ∈ [1, 76]   (need row-1 and row+3 in [0,79])
     */
    int tile_byte_offset;

    /* Per-tile array — 80×80 × 8-byte cells.  Function reads
     * tiles[offset+3] (anchor char) and writes tiles[*+1, *+2, *+3, *+4]
     * across the 3x3 footprint plus selected neighbor cells. */
    uint8_t tiles[ENGINE_TILE_BYTES];

    /* Pathfinding cost scratch.  FUN_00471c1f overwrites the entire
     * array based on tile +1 flag bits (0x10, 0x40) and edge cells.
     * Each entry is 999 (impassable / edge) or 0 (passable). */
    uint16_t path_scratch[ENGINE_TILE_CELL_COUNT];

    /* Stage scratch.  FUN_00471d30 overwrites the entire array with
     * a verbatim copy of every tile's +4 byte (the build/improvement
     * stage). */
    uint8_t stage_scratch[ENGINE_TILE_CELL_COUNT];

    /* Recent-footprint list.  FUN_0048ee46 inserts tile_byte_offset
     * into the first slot whose [0] is zero; if all 19 slots are
     * occupied, the offset is recorded only in last_footprint_tile. */
    int32_t footprint_list[ENGINE_TILE_FOOTPRINT_LIST_LEN][2];

    /* Last tile_byte_offset that overflowed the recent-footprint list.
     * Only written when all 19 slots are full at insert time; otherwise
     * carries through unchanged. */
    int32_t last_footprint_tile;
} engine_tile_footprint_state_t;

/* Run the four-stage footprint expansion on state->tile_byte_offset.
 *
 *   1. Decode anchor char at tiles[offset+3] → orientation (0/2/4/6).
 *      If not '@'/'C'/'X'/'[', return 0 without modifying any state.
 *   2. Call the neighbor flag/stage setter (FUN_004921e5).
 *   3. Walk the 3x3 cells starting at (col, row), writing +2 flags
 *      and +3 footprint glyphs; mark the center cell with +2 |= 0x80.
 *   4. Per-orientation: write a "door" trio to one specific neighbor
 *      cell (+4 = 2 and an outer cell's +3 = 1 or 2, +1 = 0).
 *   5. Recompute path_scratch (FUN_00471c1f) and stage_scratch
 *      (FUN_00471d30) end-to-end.
 *   6. Insert offset into footprint_list (FUN_0048ee46) and write
 *      last_footprint_tile.
 *
 * Returns 1 if the anchor was recognized and the expansion fired, 0 if
 * the anchor char was unrecognized (no state changes).
 */
int engine_tile_expand_footprint(engine_tile_footprint_state_t *state);

/* Pointer-arg sibling — for callers that already have the five backing
 * regions live in their own storage (e.g. save.c's castles_read_scenario
 * loop, which operates directly on g_castles_state + the runtime scratch
 * globals).  Same behaviour as engine_tile_expand_footprint; just skips
 * the state-struct wrap-up.
 *
 *   tiles               80×80 × 8-byte cells (51200 B)
 *   path_scratch        6400 × u16 — recomputed end-to-end
 *   stage_scratch       6400 × u8  — recomputed end-to-end
 *   footprint_list      19 × (int, int) — sparse insert
 *   last_footprint_tile fallback when the list is full
 */
int engine_tile_expand_footprint_at(int tile_byte_offset,
                                    uint8_t *tiles,
                                    uint16_t *path_scratch,
                                    uint8_t *stage_scratch,
                                    int32_t footprint_list[][2],
                                    int32_t *last_footprint_tile);

/* Inner helpers exposed for direct unit testing.  Production callers
 * should go through engine_tile_expand_footprint.  Each helper takes
 * pointers to the relevant fields rather than the whole state so tests
 * can isolate one stage at a time. */

/* Helper for stage 2 — port of FUN_004921e5.  Called with (col, row,
 * orient) where col/row are the CENTER of the 3x3 (i.e. anchor + 1)
 * and orient is one of 0/2/4/6.  Reads no tile state; writes +1 flag
 * bits and +4 stage bytes to neighbors of the center cell.
 *
 * Note: retail uses the orthogonal-axis bound to gate y-direction
 * writes (e.g. `param_1 < 0x4f` gates up+4/down+4 writes in the IF
 * branch).  This is preserved as a quirk — see engine-quirks.md. */
void engine_tile_set_footprint_neighbor_flags(uint8_t *tiles,
                                              int center_col,
                                              int center_row,
                                              int orient);

/* Helper for stage 5a — port of FUN_00471c1f.  Recomputes the entire
 * 6400-entry path_scratch from tile +1 flags.  Per cell:
 *   - 999 (impassable) if flag bit 0x10 or 0x40 set,
 *   - 999 if cell is on the outer ring (col 0/79 or row 0/79),
 *   - 0 otherwise. */
void engine_tile_refresh_path_scratch(const uint8_t *tiles,
                                      uint16_t *path_scratch);

/* Helper for stage 5b — port of FUN_00471d30.  Copies tile +4
 * verbatim into stage_scratch. */
void engine_tile_refresh_stage_scratch(const uint8_t *tiles,
                                       uint8_t *stage_scratch);

/* Helper for stage 6 — port of FUN_0048ee46.  Inserts tile_byte_offset
 * into the first slot of footprint_list whose [0] is zero and returns
 * the slot index used.  If all 19 slots are already occupied, sets
 * *last_footprint_tile = tile_byte_offset (otherwise leaves it alone)
 * and returns -1. */
int engine_tile_push_footprint_record(int32_t footprint_list[][2],
                                      int32_t *last_footprint_tile,
                                      int32_t tile_byte_offset);

#endif /* OPENLORDS2_ENGINE_TILE_H */
