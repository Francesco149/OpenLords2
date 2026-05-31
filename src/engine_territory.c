/*
 * OpenLords2 — per-territory record helpers + overworld cell-grid resets.
 *
 * Operates on the per-territory record array (retail `DAT_0052f0b0`,
 * 151 records × 0x1a4 bytes, slot 0 reserved) — the same memory the
 * strategic-map overlay/primary passes bind — and on the 64×64×8
 * overworld cell-descriptor grid (retail `DAT_00522f90`), which it both
 * claims (the entity placer) and bulk-clears at new-game (the two cell
 * resets below).  It also randomises the separate terrain-index *render*
 * tile grid (retail `DAT_0055cea0`, the array the map-render row helpers
 * iterate) at new-game.  Records are bound via engine_territory_bind_records,
 * the cell grid via engine_territory_bind_cells, the tile grid via
 * engine_territory_bind_terrain_tiles.  Pure C; the only outward coupling
 * is the optional dirty-rect state the cell reset marks.
 *
 *   FUN_004651b9 @ 0x4651b9 (91 B)  → engine_territory_clear_size_state
 *   FUN_0046c5b8 @ 0x46c5b8 (258 B) → engine_territory_clear_cell_field
 *   FUN_0046df51 @ 0x46df51 (132 B) → engine_territory_clear_cell_render_flags
 *   FUN_0046da4b @ 0x46da4b (293 B) → engine_territory_rebuild_cell_roster
 *   FUN_0046dd34 @ 0x46dd34 (209 B) → engine_territory_randomise_special_tiles
 *   FUN_0046c080 @ 0x46c080 (199 B) → engine_territory_load_neighbour_keys
 *   FUN_00467ca6 @ 0x467ca6 (385 B) → engine_territory_build_army_adjacency
 */

#include <stddef.h>
#include <string.h>

#include "engine.h"            /* engine_rng_step_single, engine_rng_single_* */
#include "engine_territory.h"
#include "engine_dirty_rect.h"

#define TERRITORY_RECORD_STRIDE 0x1a4
#define TERRITORY_RECORD_COUNT  0x97   /* iterate 1..0x96 (1..150) */

/* Byte fields cleared per record.  +0x153 is the territory's max-size
 * byte the overlay city drawer caps against (see engine_overlay_render.h
 * record[+0x153]); +0x14c is the adjacent companion field. */
#define TERRITORY_FIELD_14C 0x14c
#define TERRITORY_FIELD_153 0x153

static uint8_t *g_records;   /* DAT_0052f0b0 — record array base */
static uint8_t *g_cells;     /* DAT_00522f90 — 64×64×8 cell descriptor grid */

/* DAT_0053ea00 — save entry 12 (g_misc_1360, 1360 B = 17 rows × 20 i32).
 * The per-key cell roster the rebuild below fills: row k (1..16), column
 * c holds the byte-offset of the c-th active cell whose group-key (cell
 * byte +7) == k.  Row 0 is unused.  Optional + NULL-safe. */
static int32_t *g_cell_roster;

/* DAT_0053fa40 — a parallel per-key i16 table (0x300-byte = 384-short
 * rows) the strategic AI scorer (the ~0x4358xx demand/price functions,
 * unported) consumes.  Only the new-game rebuild path (reset_score_table)
 * zeroes its first 20 columns of rows 1..16; optional + NULL-safe. */
static int16_t *g_cell_score;

/* Optional render-invalidation sink, bound by the new-game orchestrator
 * so engine_territory_clear_cell_render_flags can request a full repaint
 * (retail's FUN_0046df51 → FUN_0045240a).  NULL in headless/unit
 * contexts — the mark is then simply skipped.  Same pattern the render
 * modules (engine_overlay_render / engine_battlefield_render) use. */
static engine_dirty_rect_state_t *g_dirty_state;

/* Overworld cell grid geometry (retail DAT_00522f90). */
#define CELL_GRID_CELLS   0x1000   /* 64×64 = 4096 cells */
#define CELL_STRIDE       8        /* bytes per cell descriptor */

/* DAT_0053f9b0 — save entry 4, the per-army record block (g_per_army_768,
 * 17 rows × 0x300 B, slot 0 reserved).  The army-adjacency builder below
 * writes two header fields of each army's row: a neighbour-count byte
 * (+0x5a) and a 16-byte list of bordering army keys (+0x5c).  The AI score
 * region g_cell_score binds (DAT_0053fa40 = this block + 0x90) is the same
 * memory, a different field of the same rows.  Optional + NULL-safe.  Same
 * name engine_overlay_render binds it under (read-only there). */
static uint8_t *g_army_records;

/* DAT_0055cea0 — the terrain-index render tile grid (the same array the
 * map-render row helpers iterate, engine_map_render_bind_tile_array).
 * 129 rows × 65 cols of i32, row stride 0x104 B (= 65 i32, contiguous),
 * so the i32 index is row*65 + col.  Optional + NULL-safe.  Bound mutably
 * here only for the new-game randomiser below; the render modules bind the
 * same memory read-only. */
static int32_t *g_terrain_tiles;
#define TERRAIN_GRID_ROWS 0x81     /* 129 rows (retail outer bound) */
#define TERRAIN_GRID_COLS 0x41     /* 65  cols (retail inner bound, = i32 stride) */

/* Retail's empty/special tile base 0xfff0000 (ENGINE_MAP_RENDER_CELL_EMPTY):
 * a tile word below this is a *real* cell, holding the byte-offset of its
 * descriptor in the cell grid (cell index × 8); at or above it the low
 * nibble is a special-tile variant. */
#define TERRAIN_TILE_SPECIAL_BASE       0xfff0000u

/* Special-tile sentinel the randomiser re-rolls: the special base + 6.  A
 * matching cell is replaced with base+6 + (rng & 0xf) → 0xfff0006..0xfff0015. */
#define TERRAIN_TILE_RANDOMISE_SENTINEL 0xfff0006u

/* DAT_0052afe0 — the "current entity slot" register the placer leaves
 * pointing at the record it just allocated.  Transient (not a save
 * field); several callers (FUN_00427ed0 and the 0x4a0015 / 0x4a551b /
 * 0x4ac185 setup paths) read it right after engine_territory_place_entity
 * to stamp extra fields into the fresh record. */
static int g_current_slot;

void engine_territory_bind_records(uint8_t *records)
{
    g_records = records;
}

void engine_territory_bind_cells(uint8_t *cells)
{
    g_cells = cells;
}

void engine_territory_set_dirty_state(engine_dirty_rect_state_t *state)
{
    g_dirty_state = state;
}

void engine_territory_bind_cell_roster(int32_t *roster)
{
    g_cell_roster = roster;
}

void engine_territory_bind_cell_score_table(int16_t *scores)
{
    g_cell_score = scores;
}

void engine_territory_bind_terrain_tiles(int32_t *tiles)
{
    g_terrain_tiles = tiles;
}

void engine_territory_bind_army_records(uint8_t *records)
{
    g_army_records = records;
}

/* Port of FUN_0046c5b8.  Zero byte `field` of every overworld cell
 * descriptor (all 4096 cells of the 64×64×8 grid).  Retail unrolls the
 * sweep 8 cells at a time and drives it with three scratch globals
 * (DAT_0053e8b8 byte cursor + the engine_path nearest-result pair
 * DAT_00569570/78 reused as the 8× / 64 loop counters); all three are
 * write-only scratch reset at entry and never read afterwards, so we use
 * a plain cell loop and leave engine_path's result globals alone — same
 * choice the map_query box scans make (see engine_map_query.c).
 *
 * The two callers (0x4676e0 new-game init, 0x46d3da reset) sweep
 * fields 0..5 in turn; `field` is the byte offset within the 8-byte cell.
 * No bounds check on `field` — retail trusts its 0..5 call sites. */
void engine_territory_clear_cell_field(uint8_t field)
{
    if (g_cells == NULL) return;

    for (int i = 0; i < CELL_GRID_CELLS; i++)
        g_cells[i * CELL_STRIDE + field] = 0;
}

/* Port of FUN_0046df51.  Clear bit 5 (0x20) of byte +2 of every
 * overworld cell, then request a full-screen repaint.  Byte +2 is a
 * per-cell render-flag byte; bit 0x20 is set elsewhere (0x46e067 /
 * 0x469370) and consumed by the strategic draw helpers
 * (0x408438 / 0x4063c1 / 0x4071a0 / 0x406673 / 0x406bba, all gated on
 * DAT_0053f264 == 1) — a per-cell "marked for the alternate draw path"
 * flag the engine wipes at new-game.  Retail also resets a scratch
 * progress counter (DAT_0053e974, write-only here) and calls
 * FUN_0045240a (mark full screen); we skip the counter (locals) and
 * route the repaint through the bound dirty state if any. */
void engine_territory_clear_cell_render_flags(void)
{
    if (g_cells != NULL) {
        for (int i = 0; i < CELL_GRID_CELLS; i++)
            g_cells[i * CELL_STRIDE + 2] &= 0xdf;
    }

    if (g_dirty_state != NULL)
        engine_dirty_rect_mark_full_screen(g_dirty_state);
}

int engine_territory_current_slot(void)
{
    return g_current_slot;
}

/* Port of FUN_004651b9.  Zero the +0x14c and +0x153 bytes of every
 * territory record 1..150 (slot 0 skipped).  Part of the new-game /
 * scenario init sequence (state-7 branch of the game-state driver). */
void engine_territory_clear_size_state(void)
{
    if (g_records == NULL) return;

    for (int i = 1; i < TERRITORY_RECORD_COUNT; i++) {
        uint8_t *rec = g_records + i * TERRITORY_RECORD_STRIDE;
        rec[TERRITORY_FIELD_14C] = 0;
        rec[TERRITORY_FIELD_153] = 0;
    }
}

/* Port of FUN_0046e1b0 (792 B) — allocate and place a map entity at cell
 * (col, row).  Returns 1 on success (and leaves engine_territory_current_slot
 * pointing at the new record), 0 if (col, row) is off-grid, the target
 * cell is already occupied, or the 149-slot record table is full.
 *
 * The cell is "free" iff its head-link byte (+5) is zero AND its raw-flag
 * byte (+1) carries only the low 2 bits ((flag & 0xfc) == 0).  On
 * success the placer scans records 1..149 (slot 0 reserved) for the first
 * with a zero type byte (+0), zeroes that whole 0x1a4 record
 * (FUN_0046ead0), and fills:
 *
 *   +0x00 type      +0x09 = 1        +0x0a/0x14/0x16 col  (three copies)
 *   +0x08 kind      +0x0b/0x15/0x17 row (three copies)   +0x0c cell idx (i32)
 *   +0x12 col<<4    +0x13 row<<4     +0x14b |= 1
 *
 * then marks the map cell: +5 ← slot index, +2 |= 1.  (Retail copies the
 * +0x14/0x16/0x15/0x17 bytes from +0x0a/0x0b after writing them; the
 * memset-then-write makes a direct assignment equivalent.)
 *
 * `kind` is retail param_1 (→ +0x08), `type` is param_4 (→ +0x00); the
 * army-start caller passes kind=3, type=6.  Not init-only: shared with
 * the AI/event setup paths, so it lives here rather than game_init.c. */
int engine_territory_place_entity(int kind, int col, int row, int type)
{
    if (g_records == NULL || g_cells == NULL) return 0;
    if (col < 0 || col >= 0x40 || row < 0 || row >= 0x40) return 0;

    int cell = (row * 0x40 + col) * 8;          /* byte offset of (col,row) */
    if (g_cells[cell + 5] != 0)         return 0;   /* head-link: occupied  */
    if ((g_cells[cell + 1] & 0xfc) != 0) return 0;  /* raw-flag: blocked    */

    for (g_current_slot = 1; g_current_slot < 0x96; g_current_slot++) {
        uint8_t *rec = g_records + g_current_slot * TERRITORY_RECORD_STRIDE;
        if (rec[0] != 0) continue;              /* slot in use              */

        memset(rec, 0, TERRITORY_RECORD_STRIDE);            /* FUN_0046ead0 */
        rec[0x00] = (uint8_t)type;
        rec[0x08] = (uint8_t)kind;
        rec[0x0a] = (uint8_t)col;
        rec[0x14] = rec[0x0a];                  /* col copy (retail re-reads +0x0a) */
        rec[0x16] = rec[0x14];
        rec[0x0b] = (uint8_t)row;
        rec[0x15] = rec[0x0b];                  /* row copy (retail re-reads +0x0b) */
        rec[0x17] = rec[0x15];
        *(int32_t *)(rec + 0x0c) = cell;
        rec[0x12] = (uint8_t)(col << 4);
        rec[0x13] = (uint8_t)(row << 4);
        rec[0x09] = 1;
        rec[0x14b] |= 1;

        g_cells[cell + 5] = (uint8_t)g_current_slot;   /* claim the cell    */
        g_cells[cell + 2] |= 1;
        return 1;
    }
    return 0;   /* table full — g_current_slot left at 0x96 (faithful) */
}

/* Roster geometry (retail DAT_0053ea00 / g_misc_1360).  20 i32 per key
 * row; the AI score table (DAT_0053fa40) uses 384 i16 per key row. */
#define ROSTER_COLS        20      /* per-key capacity (0x50 B / 4)        */
#define ROSTER_ROW_I32     20      /* i32 stride between roster rows       */
#define SCORE_ROW_I16      0x180   /* i16 stride between score rows (0x300)*/

/* Port of FUN_0046da4b (293 B) — rebuild the per-key cell roster from the
 * overworld grid.  Called near the tail of the new-game overworld setup
 * (0x4676e0, with reset_score_table=1) and from the mid-game recompute
 * at 0x4976a1 (with 0).
 *
 * Step 1: zero rows 1..16 of the roster (NOT row 0, NOT row 17) — and, on
 * the new-game path, the same rows × first 20 columns of the AI score
 * table.
 *
 * Step 2: walk all 4096 cells in byte order.  A cell is an "active
 * feature" iff its group-key byte (+7, DAT_00522f97) is in 1..17 AND its
 * raw-flag byte (+1, DAT_00522f91) has bit 0x20 set.  Each such cell's
 * byte-offset is appended to the first free (== 0) column of its key's
 * roster row, capped at 20.  A cell that finds no free column (its key
 * already has 20 members) is EVICTED: type byte (+0) → 0, byte +3 → 6,
 * flag byte (+1) → 0, and byte +2 (DAT_00522f92) clears bits 2..4 (&0xe3).
 *
 * QUIRKS (preserved verbatim — see engine-quirks.md):
 *  - The key guard is `< 0x12` (allows 17) but the roster has only 17
 *    rows (0..16); a key of 17 would write 1 i32 past g_misc_1360.  Safe
 *    only because the +7 field is gated to 1..16 at its producers
 *    (engine_strat_cost reads it `< 0x11`).  Tests stay within 1..16.
 *  - The "free column" sentinel is offset 0, which is also cell (0,0)'s
 *    own byte-offset — so cell 0, if it were ever an active feature,
 *    could never register distinctly.  Retail has the same blind spot;
 *    cell (0,0) is map-border terrain and never a feature. */
void engine_territory_rebuild_cell_roster(int reset_score_table)
{
    if (g_cell_roster == NULL || g_cells == NULL) return;

    for (int row = 1; row < 0x11; row++) {              /* rows 1..16 */
        for (int col = 0; col < ROSTER_COLS; col++)
            g_cell_roster[row * ROSTER_ROW_I32 + col] = 0;
        if (reset_score_table == 1 && g_cell_score != NULL) {
            for (int col = 0; col < ROSTER_COLS; col++)
                g_cell_score[row * SCORE_ROW_I16 + col] = 0;
        }
    }

    for (int off = 0; off < CELL_GRID_CELLS * CELL_STRIDE; off += CELL_STRIDE) {
        unsigned key = g_cells[off + 7];                /* DAT_00522f97 */
        if (key == 0 || key >= 0x12)            continue;
        if ((g_cells[off + 1] & 0x20) == 0)     continue;   /* DAT_00522f91 */

        int placed = 0;
        for (int col = 0; col < ROSTER_COLS; col++) {
            if (g_cell_roster[key * ROSTER_ROW_I32 + col] == 0) {
                g_cell_roster[key * ROSTER_ROW_I32 + col] = off;
                placed = 1;
                break;
            }
        }
        if (!placed) {                          /* key's 20 slots full → evict */
            g_cells[off + 0] = 0;               /* DAT_00522f90  type          */
            g_cells[off + 3] = 6;               /* DAT_00522f93                */
            g_cells[off + 1] = 0;               /* DAT_00522f91  flags         */
            g_cells[off + 2] &= 0xe3;           /* DAT_00522f92  clear bits 2-4*/
        }
    }
}

/* Port of FUN_0046d9da (113 B) — clear the AI score for one cell's roster
 * slot.  Called by the cell-mutation helper at 0x46d7f4 (unported) when a cell's
 * feature changes: locate cell_off's column in its key's roster row
 * (key = the cell's +7 byte, DAT_00522f97) and zero the parallel i16 in
 * the AI score table.  The roster stores cell byte-offsets, so the match
 * is an exact equality on cell_off.  If the cell is absent from its row
 * (all 20 columns scanned without a hit), nothing is cleared — retail
 * falls through the loop and returns.  Only the score is touched; the
 * roster entry itself is left intact. */
void engine_territory_clear_cell_score(int cell_off)
{
    if (g_cells == NULL || g_cell_roster == NULL || g_cell_score == NULL)
        return;

    unsigned key = g_cells[cell_off + 7];               /* DAT_00522f97 */
    for (int col = 0; col < ROSTER_COLS; col++) {
        if (g_cell_roster[key * ROSTER_ROW_I32 + col] == cell_off) {
            g_cell_score[key * SCORE_ROW_I16 + col] = 0;
            return;
        }
    }
}

/* Advance the single-LFSR RNG (retail FUN_00404b2c), threading its state
 * word through *rng and returning the 7-bit mirror (retail DAT_005c9a84).
 * Mirrors game_init.c's rng_next_7b for the double LFSR. */
static uint32_t rng_next_single_7bit(engine_rng_single_in_t *rng)
{
    engine_rng_single_out_t out = {0};
    engine_rng_step_single(rng, &out);   /* FUN_00404b2c */
    rng->state = out.new_state;          /* DAT_00591564 writeback */
    return out.rng_7bit;                 /* DAT_005c9a84 */
}

/* Port of FUN_0046dd34 (209 B) — the new-game terrain-tile randomiser.
 * Sweeps the bound terrain-index render grid (DAT_0055cea0, 129×65 i32) in
 * row-major order and, for every cell holding exactly the special sentinel
 * 0xfff0006, re-rolls it to 0xfff0006 + (rng & 0xf) using the single-LFSR
 * RNG (engine_rng_step_single, threaded through *rng).
 *
 * Retail reaches the match through a two-step signed ladder
 * (`value >= 0xfff0000` then `== 0xfff0006`) shared with the terrain
 * classifier at 0x46de94/0x46dbf5; the outer arm is redundant given the
 * exact-equality inner arm, so the operative test below is just the
 * sentinel compare.
 *
 * Two write-only scratch globals are dropped (same choice the cell-field
 * sweep makes for its scratch cursors — see engine_territory_clear_cell_field):
 *  - DAT_0053e974, a per-cell progress counter reset at entry and bumped
 *    once per visited cell (8385 total); never read here.
 *  - DAT_00568200 = (mirror & 7), rewritten on every sentinel cell.  It is
 *    a cross-function scratch register, but no consumer observes the residue
 *    0x46dd34 leaves: its readers (the sibling seeder 0x46dbf5 and the
 *    feature picker 0x46c2de, both unported) run earlier in the 0x4676e0
 *    setup sequence and re-derive it before use, and no ported code reads it.
 *
 * The live 0x4676e0 orchestrator will own the single-LFSR state word
 * (retail's DAT_00591564, seeded 0x34518632 at InitInstance) and thread it
 * through here and the sibling seeders, exactly as game_init threads the
 * double-LFSR state through the new-game seeders. */
void engine_territory_randomise_special_tiles(engine_rng_single_in_t *rng)
{
    if (g_terrain_tiles == NULL || rng == NULL) return;

    for (int row = 0; row < TERRAIN_GRID_ROWS; row++) {
        for (int col = 0; col < TERRAIN_GRID_COLS; col++) {
            int32_t *cell = &g_terrain_tiles[row * TERRAIN_GRID_COLS + col];
            if ((uint32_t)*cell == TERRAIN_TILE_RANDOMISE_SENTINEL) {
                uint32_t mirror = rng_next_single_7bit(rng);
                *cell = (int32_t)((mirror & 0xfu) + TERRAIN_TILE_RANDOMISE_SENTINEL);
            }
        }
    }
}

/* Port of FUN_0046de94 (189 B) — the per-neighbour terrain eligibility
 * predicate underpinning the new-game feature-spread (seeder 0x46dbf5 →
 * classifier 0x46c2de).  Reads the terrain-index tile at (col, row) of the
 * bound 129×65 grid (DAT_0055cea0) and reports whether it is "eligible":
 *
 *   - off-grid (col<0 || col>=65 || row<0 || row>=129)        → 0
 *   - a real cell (tile value < 0xfff0000, the byte-offset of its
 *     descriptor in the cell grid) → 1 iff its flags byte (+1) lacks bit 0x4
 *   - the un-rolled special sentinel 0xfff0006                → 1
 *   - any other special value (>= 0xfff0000)                  → 0
 *
 * The bounds guard is load-bearing: the neighbour offset table below pushes
 * (col, row) off the edge for border cells, and those off-grid reads must
 * count as ineligible rather than index out of bounds.  Retail has no
 * unbound-grid guard (the grids are always populated when it runs); the
 * g_terrain_tiles / g_cells NULL checks are host-safety only. */
int engine_territory_tile_eligible(int col, int row)
{
    if (g_terrain_tiles == NULL) return 0;
    if (col < 0 || row < 0 || col >= TERRAIN_GRID_COLS || row >= TERRAIN_GRID_ROWS)
        return 0;

    uint32_t v = (uint32_t)g_terrain_tiles[row * TERRAIN_GRID_COLS + col];
    if (v < TERRAIN_TILE_SPECIAL_BASE)                       /* real cell */
        return (g_cells != NULL && (g_cells[v + 1] & 0x4) == 0) ? 1 : 0;
    return v == TERRAIN_TILE_RANDOMISE_SENTINEL ? 1 : 0;     /* only the sentinel */
}

/* Hex-grid neighbour offsets (retail DAT_004d6c28 / DAT_004d6be8), in
 * offset coordinates {dx, dy} added to (col, row).  The strategic map is a
 * doubled-row hex grid, so even and odd rows have distinct neighbour sets;
 * retail selects the table by row parity (`row & 1`). */
static const int8_t NEIGHBOUR_OFFSETS[2][8][2] = {
    /* even row (row & 1 == 0) — retail DAT_004d6c28 */
    { {1,-1}, {1,0}, {1,1}, {0,2}, {0,1}, {-1,0}, {0,-1}, {0,-2} },
    /* odd  row (row & 1 == 1) — retail DAT_004d6be8 */
    { {0,-1}, {1,0}, {0,1}, {0,2}, {-1,1}, {-1,0}, {-1,-1}, {0,-2} },
};

/* Port of FUN_0046de05 (143 B) — compute the 8-neighbour eligibility mask
 * for overworld tile (col, row) into mask[8].  For each of the 8 hex
 * neighbours (offset table chosen by row parity) it stores
 * engine_territory_tile_eligible(col + dx, row + dy) as 0/1.
 *
 * Retail writes the mask into the shared scratch global DAT_00543fe0, which
 * the feature classifier 0x46c2de (the next call in seeder 0x46dbf5) reads
 * back.  DAT_00543fe0 is reused across several independent feature passes as
 * a produce-then-consume register, never carrying state between them, so the
 * port threads it explicitly through the caller's mask[8] — the same idiom
 * the module uses for the single-LFSR RNG — rather than keeping a mutable
 * module global. */
void engine_territory_compute_neighbour_mask(int col, int row, int32_t mask[8])
{
    const int parity = row & 1;
    for (int i = 0; i < 8; i++) {
        int dx = NEIGHBOUR_OFFSETS[parity][i][0];
        int dy = NEIGHBOUR_OFFSETS[parity][i][1];
        mask[i] = engine_territory_tile_eligible(col + dx, row + dy);
    }
}

/* Retail's 48-entry hex auto-tile / feature-variant pattern table
 * (DAT_004d7310, dbf5 passes it with count 0x30).  Each row is 12 retail
 * bytes; the first 11 are immutable and held here:
 *   [0..7] pattern over the 8 hex neighbours — 0 = neighbour must be
 *          ineligible, 1 = must be eligible, 2 = wildcard (always matches);
 *   [8]    FT_TYPE — the feature/tile variant code emitted on a match;
 *   [9]    FT_AUX  — a secondary byte (0 throughout this table);
 *   [10]   FT_CAP  — the rotating variant counter's modulus.
 * The 12th retail byte (+0xb) is the per-entry variant counter — runtime
 * state, kept in g_feature_counter below (all 0 at load).
 *
 * Transcribed verbatim from Lords2.exe @ 0x4d7310 (see game-start.md).  The
 * table is complete: every one of the 256 neighbour configurations matches
 * some entry (entry 46, the all-ineligible row, is the isolated-tile
 * catch-all; entry 47 is an unreachable all-zero terminator). */
#define FEATURE_TABLE_ENTRIES 0x30   /* 48 — retail's count argument */
enum { FT_TYPE = 8, FT_AUX = 9, FT_CAP = 0xa };
static const uint8_t FEATURE_TABLE[FEATURE_TABLE_ENTRIES][11] = {
    { 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x4e, 0x00, 0x02 },
    { 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x00, 0x02, 0x4b, 0x00, 0x01 },
    { 0x00, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x4c, 0x00, 0x01 },
    { 0x01, 0x02, 0x00, 0x02, 0x01, 0x02, 0x01, 0x02, 0x4d, 0x00, 0x01 },
    { 0x01, 0x02, 0x01, 0x02, 0x00, 0x02, 0x01, 0x02, 0x4a, 0x00, 0x01 },
    { 0x01, 0x02, 0x00, 0x02, 0x01, 0x02, 0x00, 0x02, 0x36, 0x00, 0x02 },
    { 0x00, 0x02, 0x01, 0x02, 0x00, 0x02, 0x01, 0x02, 0x38, 0x00, 0x02 },
    { 0x01, 0x02, 0x01, 0x02, 0x00, 0x00, 0x00, 0x02, 0x3a, 0x00, 0x04 },
    { 0x00, 0x02, 0x01, 0x02, 0x01, 0x02, 0x00, 0x00, 0x3e, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x02, 0x01, 0x02, 0x01, 0x02, 0x42, 0x00, 0x04 },
    { 0x01, 0x02, 0x00, 0x00, 0x00, 0x02, 0x01, 0x02, 0x46, 0x00, 0x04 },
    { 0x01, 0x02, 0x01, 0x02, 0x00, 0x01, 0x00, 0x02, 0x5c, 0x00, 0x01 },
    { 0x00, 0x02, 0x01, 0x02, 0x01, 0x02, 0x00, 0x01, 0x5d, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x02, 0x01, 0x02, 0x01, 0x02, 0x5e, 0x00, 0x01 },
    { 0x01, 0x02, 0x00, 0x01, 0x00, 0x02, 0x01, 0x02, 0x5f, 0x00, 0x01 },
    { 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x26, 0x00, 0x04 },
    { 0x00, 0x02, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x02, 0x01, 0x02, 0x00, 0x00, 0x2e, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x02, 0x32, 0x00, 0x04 },
    { 0x01, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x50, 0x00, 0x01 },
    { 0x00, 0x02, 0x01, 0x02, 0x00, 0x01, 0x00, 0x00, 0x53, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x02, 0x01, 0x02, 0x00, 0x01, 0x56, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01, 0x02, 0x59, 0x00, 0x01 },
    { 0x01, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x51, 0x00, 0x01 },
    { 0x00, 0x02, 0x01, 0x02, 0x00, 0x00, 0x00, 0x01, 0x54, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x02, 0x01, 0x02, 0x00, 0x00, 0x57, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x01, 0x02, 0x5a, 0x00, 0x01 },
    { 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x52, 0x00, 0x01 },
    { 0x00, 0x02, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0x55, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x02, 0x01, 0x02, 0x00, 0x01, 0x58, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x01, 0x02, 0x5b, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x7a, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x78, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x79, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x76, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x77, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x74, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x75, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x72, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x73, 0x00, 0x01 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x70, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x71, 0x00, 0x01 },
    { 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6c, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x60, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x08 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};
static uint8_t g_feature_counter[FEATURE_TABLE_ENTRIES];   /* retail byte +0xb, init 0 */

void engine_territory_reset_feature_counters(void)
{
    for (int i = 0; i < FEATURE_TABLE_ENTRIES; i++) g_feature_counter[i] = 0;
}

/* Port of FUN_0046c2de.  See the table above and engine_territory.h. */
int engine_territory_pick_feature(const int32_t mask[8],
                                  engine_territory_feature_pick_t *out)
{
    for (int n = 0; n < FEATURE_TABLE_ENTRIES; n++) {
        const uint8_t *e = FEATURE_TABLE[n];
        int i;
        for (i = 0; i < 8; i++) {
            uint8_t pat = e[i];
            /* the retail three-way OR: wildcard, want-eligible, or want-ineligible */
            int ok = (pat == 2) ||
                     (pat != 0 && mask[i] != 0) ||
                     (pat == 0 && mask[i] == 0);
            if (!ok) break;
        }
        if (i < 8) continue;            /* a byte mismatched → next entry */

        /* full match: advance the rotating variant counter (++ then wrap at
         * FT_CAP) and emit the feature.  Counter math is 8-bit, matching the
         * retail `inc byte`; FT_CAP is small so it never overflows. */
        uint8_t cnt = (uint8_t)(g_feature_counter[n] + 1);
        if (e[FT_CAP] <= cnt) cnt = 0;
        g_feature_counter[n] = cnt;
        if (out != NULL) {
            out->type    = e[FT_TYPE];
            out->aux     = e[FT_AUX];
            out->variant = cnt;
        }
        return n + 1;
    }
    return 0;
}

/* The "isolated tile" feature type (table entry 46, the all-ineligible
 * catch-all): when the classifier returns it, the variant is re-rolled from
 * the single-LFSR RNG instead of the entry's rotating counter. */
#define FEATURE_TYPE_ISOLATED 0x16

/* Port of FUN_0046dbf5 (319 B) — the new-game terrain auto-tiling pass, the
 * sibling of engine_territory_randomise_special_tiles (FUN_0046dd34).  Sweep
 * the 129×65 terrain-index grid (DAT_0055cea0) in row-major order; for every
 * "feature-border" cell — a real cell (tile < 0xfff0000) whose flags byte (+1)
 * has bit 0x4 SET, or a special tile that is NOT the sentinel 0xfff0006 —
 * classify it by its 8 hex neighbours and write the chosen transition tile:
 *
 *   mask            = engine_territory_compute_neighbour_mask(col, row)
 *   (type, variant) = engine_territory_pick_feature(mask)
 *   if (type == 0x16) variant = rng_single_7bit & 7;   // isolated → random
 *   real cell : g_cells[tile + 3] = (uint8_t)(type + variant)   // byte +3
 *   special   : tile = 0xfff0000 + type + variant
 *
 * Eligible cells (real flag-clear, or the sentinel) are skipped untouched.
 * The grid is mutated IN PLACE in row-major order, exactly as retail: a
 * rewritten special tile IS visible to later cells' neighbour masks, while
 * real-cell writes land in the cell's +3 byte and never perturb the tile grid
 * — so the result is order-dependent and must not be computed off a snapshot.
 *
 * The single-LFSR state is threaded through *rng (the live 0x4676e0
 * orchestrator owns it and threads it through this, randomise_special_tiles,
 * and the sibling seeders in sequence).  One write-only scratch global is
 * dropped: the per-cell progress counter DAT_0053e974 (never read). */
void engine_territory_classify_feature_tiles(engine_rng_single_in_t *rng)
{
    if (g_terrain_tiles == NULL || rng == NULL) return;

    for (int row = 0; row < TERRAIN_GRID_ROWS; row++) {
        for (int col = 0; col < TERRAIN_GRID_COLS; col++) {
            int32_t *cell = &g_terrain_tiles[row * TERRAIN_GRID_COLS + col];
            uint32_t v = (uint32_t)*cell;

            int is_border;
            if (v < TERRAIN_TILE_SPECIAL_BASE)
                is_border = g_cells != NULL && (g_cells[v + 1] & 0x4) != 0;
            else
                is_border = v != TERRAIN_TILE_RANDOMISE_SENTINEL;
            if (!is_border) continue;

            int32_t mask[8];
            engine_territory_compute_neighbour_mask(col, row, mask);
            engine_territory_feature_pick_t pick = {0};
            engine_territory_pick_feature(mask, &pick);

            int variant = pick.variant;
            if (pick.type == FEATURE_TYPE_ISOLATED)
                variant = (int)(rng_next_single_7bit(rng) & 7u);

            if (v < TERRAIN_TILE_SPECIAL_BASE)
                g_cells[v + 3] = (uint8_t)(pick.type + variant);
            else
                *cell = (int32_t)(TERRAIN_TILE_SPECIAL_BASE +
                                  (uint32_t)(pick.type + variant));
        }
    }
}

/* Per-army record geometry (retail DAT_0053f9b0, 17 × 0x300).  The
 * adjacency builder touches two header fields of each army's row:
 *   +0x5a (DAT_0053fa0a)  neighbour count byte
 *   +0x5c (DAT_0053fa0c)  16-byte list of distinct bordering army keys */
#define ARMY_ROW_STRIDE   0x300
#define ARMY_ADJ_COUNT    0x5a
#define ARMY_ADJ_LIST     0x5c
#define ARMY_ADJ_MAX      0x10   /* list capacity (16) */

/* Byte offset between the cell grid's NESW orthogonal neighbours: one row
 * is 64 cells × 8 B = 0x200, one column is 8 B. */
#define CELL_ROW_BYTES    (0x40 * CELL_STRIDE)   /* 0x200 */

/* Port of FUN_0046c080 (199 B) — load the group-key (cell byte +7) of the
 * four orthogonal neighbours of the cell at byte-offset `cursor_off`
 * (= (row*64 + col)*8) into out[] in N, E, S, W order; an edge neighbour
 * (north of row 0, east of col 0x3f, south of row 0x3f, west of col 0)
 * reads 0.  Retail scatters these to DAT_00553280/88/90/98 (stride-8
 * slots, only the even ones used); the port returns them compacted.
 * Shared: also called by the 0x468d4f castle-border subtree (0x46902a).
 * No-op (zeroed out[]) if the cell grid is unbound. */
void engine_territory_load_neighbour_keys(int col, int row, int cursor_off,
                                          int32_t out[4])
{
    if (g_cells == NULL) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    out[0] = (row == 0)    ? 0 : g_cells[cursor_off - CELL_ROW_BYTES + 7]; /* N: DAT_00522d97 */
    out[1] = (col == 0x3f) ? 0 : g_cells[cursor_off + CELL_STRIDE   + 7];  /* E: DAT_00522f9f */
    out[2] = (row == 0x3f) ? 0 : g_cells[cursor_off + CELL_ROW_BYTES + 7]; /* S: DAT_00523197 */
    out[3] = (col == 0)    ? 0 : g_cells[cursor_off - CELL_STRIDE   + 7];  /* W: DAT_00522f8f */
}

/* Port of FUN_00467ca6 (385 B) — build the per-army territory-adjacency
 * lists.  Walk all 4096 overworld cells in byte order; for each cell whose
 * owning-army key (byte +7) is in 1..17, fetch its four orthogonal
 * neighbours' keys (engine_territory_load_neighbour_keys) and, for every
 * neighbour key that is itself in 1..17 and differs from the cell's own
 * key, append it (once) to that cell-key's army row: scan the +0x5c list of
 * up to 16 entries, skip if the neighbour key is already present, else drop
 * it in the first free (== 0) slot and bump the +0x5a count.  A full list
 * (16 distinct neighbours) silently drops further keys.
 *
 * QUIRK (#132, preserved verbatim): the accept guard is `<= 0x11` (admits
 * key 17), but the block has only 17 rows (0..16) — a key of 17 indexes one
 * 0x300-row past g_per_army_768.  Unreachable in practice: the +7 producers
 * gate the field to 1..16.  Tests stay within 1..16.
 *
 * No-op if the cell grid or the army-record block is unbound. */
void engine_territory_build_army_adjacency(void)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    int cursor_off = 0;
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++, cursor_off += CELL_STRIDE) {
            unsigned key = g_cells[cursor_off + 7];        /* DAT_00522f97 */
            if (key == 0 || key > 0x11) continue;          /* 1..17 (quirk #132) */

            int32_t nb[4];
            engine_territory_load_neighbour_keys(col, row, cursor_off, nb);

            uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
            uint8_t *list    = rowbase + ARMY_ADJ_LIST;
            for (int i = 0; i < 4; i++) {
                unsigned other = (unsigned)nb[i];
                if (other == 0 || other > 0x11 || other == key) continue;

                for (int slot = 0; slot < ARMY_ADJ_MAX; slot++) {
                    if (list[slot] == (uint8_t)other) break;   /* already listed */
                    if (list[slot] == 0) {                     /* first free → add */
                        list[slot] = (uint8_t)other;
                        rowbase[ARMY_ADJ_COUNT]++;
                        break;
                    }
                }
            }
        }
    }
}
