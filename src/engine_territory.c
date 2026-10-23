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
#include "engine_category_buckets.h"  /* clear / append (FUN_004290d0/429153) */
#include "engine_roster.h"            /* player_roster_clear/add + count alias */
#include "engine_map_render.h"        /* set_rotation_offset (DAT_00522f7c=0) */
#include "engine_camera.h"            /* engine_camera_clamp_scroll (FUN_00429b1d) */
#include "save_schema_globals.h"  /* s_slot_0053f23c — feature-density setting;
                                     s_slot_0056d5dc — player/army count */
#include "engine_math.h"          /* engine_math_percent_of (FUN_00404d6b) */

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

/* Optional sink for the per-frame cursor-cache mode flag (retail
 * DAT_0057cac4 = engine.c's g_input_cursor_cache_enabled).  The
 * recenter-on-tile navigator below latches it to 1 on a hit, the way
 * retail does; bound by the orchestrator that owns the input mirror,
 * NULL elsewhere (the latch is then skipped). */
static int *g_cursor_cache_flag;

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

/* DAT_0057c8d0 / DAT_0056846c — the "current cell" col/row scratch registers
 * the capital-region setup subtree (0x468d4f) threads between its passes.
 * The capital / secondary finders (0x467fd1 / 0x468121) leave them pointing
 * at their 4th matching cell; the orchestrator both stashes them into the
 * per-army record and feeds them (as the distance origin) to the coastal-port
 * placer (0x46902a).  Transient — not save fields.  Retail reads them only
 * from within this subtree, so they stay module-private; the accessors below
 * exist for the unit tests. */
static int g_cursor_col;    /* DAT_0057c8d0 */
static int g_cursor_row;    /* DAT_0056846c */

int engine_territory_cursor_col(void) { return g_cursor_col; }
int engine_territory_cursor_row(void) { return g_cursor_row; }

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

void engine_territory_bind_cursor_cache_flag(int *flag)
{
    g_cursor_cache_flag = flag;
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

/* Port of FUN_0046c6ba.  Clear bit 6 (0x40) of byte +2 of every overworld
 * cell — the sibling of engine_territory_clear_cell_render_flags' bit 0x20
 * (SAME byte, different flag), called by the army-select move/merge
 * handoff (FUN_0046f68e, via engine_army_select_open_handoff) right before
 * a fresh path-field flood, clearing whatever the PREVIOUS flood's overlay
 * left set.  No reader of bit 0x40 found in the corpus searched so far
 * (unlike bit 0x20's draw-helper consumers); no trailing repaint request
 * either (retail doesn't call FUN_0045240a here) — faithfully omitted. */
void engine_territory_clear_cell_path_overlay(void)
{
    if (g_cells == NULL) return;

    for (int i = 0; i < CELL_GRID_CELLS; i++)
        g_cells[i * CELL_STRIDE + 2] &= 0xbf;
}

/* Mark the whole screen dirty through the bound dirty state (retail
 * FUN_0045240a — engine_dirty_rect_mark_full_screen).  Exposed so callers in
 * other modules (e.g. the settlement-spawn view refresh) can request a full
 * repaint without owning the dirty state.  No-op when no state is bound. */
void engine_territory_mark_full_screen_dirty(void)
{
    if (g_dirty_state != NULL)
        engine_dirty_rect_mark_full_screen(g_dirty_state);
}

/* Port of FUN_0046e067.  The set-side counterpart of
 * clear_cell_render_flags: OR bit 0x20 into byte +2 of every cell of
 * the (2*radius+1)-square box centred at (col, row), clamped to the
 * 0..63 grid (the box shrinks at the edges, never rejects).  The clamp
 * arithmetic is identical to engine_map_query's box_scan; the only
 * difference is this writes rather than reads, and touches byte +2
 * (the alternate-draw flag) rather than the +1/+5 predicate bytes.
 *
 * Retail drives the two loops with the engine_path nearest-result
 * globals (DAT_00569570 col / DAT_00569578 row) as write-only loop
 * counters — never read afterwards — so we use plain locals and leave
 * those globals alone, the same choice the box scans and the cell-field
 * clears make.  No repaint here; the sole caller (FUN_0046dfd5) stamps
 * a whole key's worth of cells, then marks the screen once. */
void engine_territory_mark_draw_box(int col, int row, int radius)
{
    if (g_cells == NULL) return;

    int side = radius * 2 + 1;
    int x = col - radius;
    int y = row - radius;

    /* Clamp the box to [0, 0x40) per axis, shrinking the extent. */
    int xext, yext;
    if (x < 0)                  { xext = side + x; x = 0; }
    else if (x + side > 0x40)   { xext = 0x40 - x; }
    else                        { xext = side; }
    if (y < 0)                  { yext = side + y; y = 0; }
    else if (y + side > 0x40)   { yext = 0x40 - y; }
    else                        { yext = side; }

    int idx = (y * 0x40 + x) * CELL_STRIDE;
    for (int r = y; r < y + yext; r++) {
        for (int c = x; c < x + xext; c++) {
            g_cells[idx + 2] |= 0x20;       /* DAT_00522f92 */
            idx += CELL_STRIDE;
        }
        idx += (0x40 - xext) * CELL_STRIDE; /* skip to next row's box start */
    }
}

/* Port of FUN_0046dfd5.  Highlight one group-key on the strategic map.
 * Sweep the whole 64×64 grid in row-major order; for every cell whose
 * group-key byte (+7, DAT_00522f97) equals `key`, stamp a radius-1
 * alternate-draw box (the cell plus its 3×3 neighbourhood) via
 * engine_territory_mark_draw_box.  After the sweep, request a
 * full-screen repaint (retail FUN_0045240a) through the bound dirty
 * state.
 *
 * Retail also walks a write-only scratch progress counter
 * (DAT_0053e974, incremented once per cell, never read) — dropped here
 * for a plain index, the same way clear_cell_render_flags drops its
 * counter.  Comparison matches retail's: the +7 byte is zero-extended
 * to unsigned and compared against the full `key` word, so a key above
 * 255 never matches (the byte can only hold 0..255). */
void engine_territory_highlight_key_cells(unsigned key)
{
    if (g_cells != NULL) {
        for (int row = 0; row < 0x40; row++) {
            for (int col = 0; col < 0x40; col++) {
                int off = (row * 0x40 + col) * CELL_STRIDE;
                if ((unsigned)g_cells[off + 7] == key)   /* DAT_00522f97 */
                    engine_territory_mark_draw_box(col, row, 1);
            }
        }
    }

    if (g_dirty_state != NULL)
        engine_dirty_rect_mark_full_screen(g_dirty_state);
}

/* Visible-cell counts the recenter clamp bounds against (retail
 * DAT_0053e8ac col / DAT_0056d67c row — engine.c's g_cam_visible_cols /
 * g_cam_visible_rows).  These are per-frame render state with no producer
 * in the port yet (engine.c keeps them 0, "no writer today"), and the
 * sole caller of the recenter is game-init, before any frame has set
 * them.  At 0 the clamp's UPPER bound never fires for a valid recenter
 * target — scroll col ∈ [-4, 60] < 0x41, scroll row ∈ [-12, 116] < 0x81 —
 * so only the lower clamp-to-0 is active, which is count-independent.  We
 * pass 0/0 to match retail's value on this path; the strategic-render
 * orchestrator (roadmap item 6) is where live counts will arrive. */
#define RECENTER_VISIBLE_COLS 0
#define RECENTER_VISIBLE_ROWS 0

/* Port of FUN_0043278b.  Scan the bound terrain-index tile grid
 * (g_terrain_tiles = DAT_0055cea0, 129 rows × 65 cols of i32) in
 * row-major order for the first cell holding `tile_value`; on a hit,
 * recenter the strategic-map scroll on it and flag a redraw, returning 2
 * (retail's hit code).  Returns 0 if the value is absent.
 *
 * On a hit, when the view-mode global is normal (s_slot_0057cb18 == 0,
 * DAT_0057cb18) the scroll target is set to (col-4, evenrow-12): the
 * column left-shifted by 4 and the row floored to even (& ~1) then up by
 * 12, centring the found cell in the viewport.  The scroll pair is then
 * clamped in place by engine_camera_clamp_scroll (FUN_00429b1d) —
 * UNCONDITIONALLY, outside the view-mode gate, exactly as retail calls it
 * after the assignment.  Finally the below-status redraw flag
 * (DAT_0053e9ac, the dirty-state field) and the cursor-cache flag
 * (DAT_0057cac4, optional bind) are latched to 1.
 *
 * The scroll pair are the canonical save globals (entries 16/17,
 * s_slot_005651b4 / s_slot_005651b8); this is their first producer in the
 * port — see engine.c's camera-mirror note, which anticipated exactly
 * this "castle-table-driven re-centering" writer.  Retail materialises
 * the matched value through a scratch global (DAT_0053f0dc), write-then-
 * read within the caller; we use the argument directly. */
int engine_territory_recenter_on_tile(int tile_value)
{
    if (g_terrain_tiles == NULL) return 0;

    for (int row = 0; row <= 0x80; row++) {            /* 0..128 inclusive */
        for (int col = 0; col < 0x41; col++) {         /* 0..64           */
            if (g_terrain_tiles[row * TERRAIN_GRID_COLS + col] != tile_value)
                continue;

            if (s_slot_0057cb18 == 0) {                /* DAT_0057cb18: normal view */
                s_slot_005651b4 = (uint32_t)(col - 4);            /* DAT_005651b4 */
                s_slot_005651b8 = (uint32_t)((row & ~1) - 0xc);   /* DAT_005651b8 */
            }

            /* Clamp the (possibly-updated) scroll pair in place — retail
             * calls FUN_00429b1d here regardless of the view-mode gate. */
            int sc = (int)s_slot_005651b4;
            int sr = (int)s_slot_005651b8;
            engine_camera_clamp_scroll(&sc, &sr,
                                       RECENTER_VISIBLE_COLS, RECENTER_VISIBLE_ROWS);
            s_slot_005651b4 = (uint32_t)sc;
            s_slot_005651b8 = (uint32_t)sr;

            if (g_dirty_state != NULL)
                g_dirty_state->below_status_redraw = 1;   /* DAT_0053e9ac */
            if (g_cursor_cache_flag != NULL)
                *g_cursor_cache_flag = 1;                  /* DAT_0057cac4 */
            return 2;
        }
    }
    return 0;
}

/* Port of FUN_00432746.  Recenter the strategic map on army `key`'s
 * capital: read the capital cell byte-offset from the army record
 * (g_army_records row + ARMY_CAP_OFF = +0x70, the value
 * engine_territory_build_army_adjacency stores there) and, when non-zero,
 * recenter on it via engine_territory_recenter_on_tile and latch the
 * selected-army global (DAT_0057c93c = save entry 71, s_slot_0057c93c).
 *
 * Retail stages the offset through the scratch global DAT_0053f0dc
 * (write-then-read); we use a local. */
void engine_territory_recenter_on_capital(int key)
{
    if (g_army_records == NULL) return;

    /* row stride 0x300 (ARMY_ROW_STRIDE), capital offset +0x70
     * (ARMY_CAP_OFF) — both #defined later in the file, per its
     * per-section convention; the literals match. */
    int32_t cap = *(int32_t *)(g_army_records + key * 0x300 + 0x70);
    if (cap != 0) {
        engine_territory_recenter_on_tile(cap);
        s_slot_0057c93c = (uint32_t)key;        /* DAT_0057c93c — selected army */
    }
}

int engine_territory_current_slot(void)
{
    return g_current_slot;
}

void engine_territory_set_current_slot(int slot)
{
    g_current_slot = slot;
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

/* Port of FUN_0046d7f4 (486 B) — set an overworld cell's terrain type and
 * re-derive its render/flag bytes.  This is the engine's cell-type mutator:
 * many strategic-map edits route through it.
 *
 *   cell_off : the cell's byte-offset into the grid
 *   type     : new terrain type (written to +0, DAT_00522f90)
 *   variant  : a render-variant tier; ×4 into the +3 descriptor byte
 *              (retail call sites all pass 0)
 *
 * Per type it picks a +3 descriptor base and the +2 flag bits to layer in;
 * when the cell becomes empty (type 0) it also clears the cell's AI score
 * via engine_territory_clear_cell_score (FUN_0046d9da).  The +3 byte keeps
 * its low 2 bits (corrected for an old 0x17/0x18 special) and rebuilds the
 * rest as base + variant*4.  The +2 byte gets bit0 set, bits 2..4 replaced
 * by this type's contribution (bit3 for ordinary types, nothing for the
 * 0x17/0x18 specials), bit7 cleared, then bit7 re-set only for the type
 * band 15..22 (0xe < type < 0x17).
 *
 * NOTE (decompiler vs bytes): Ghidra renders the +3 low-bit correction
 * constant as 0 (default) / 0x82 (old type 0x17|0x18); r2 @0x46d8e6 shows
 * the real default is 0x50.  It is masked with &3 and 0x50 ≡ 0 (mod 4), so
 * the two agree — the real bytes are used here. */
void engine_territory_set_cell_type(int cell_off, int type, char variant)
{
    if (g_cells == NULL) return;

    uint8_t old_type = g_cells[cell_off + 0];           /* DAT_00522f90 */
    g_cells[cell_off + 0] = (uint8_t)type;

    uint8_t base;            /* local_18 — +3 descriptor base for this type */
    uint8_t lowbits = 8;     /* local_8  — +2 bits 2..4 contribution (bit3)  */
    if      (type == 0)    base = 0x50;
    else if (type == 0x17) { base = 0x82; lowbits = 0; }
    else if (type == 0x18) { base = 0x86; lowbits = 0; }
    else if (type == 1)    base = 0x54;
    else if (type == 0x19) base = 0x6c;
    else if (type == 0x1a) base = 0x70;
    else if (type == 0x1b) base = 0x74;
    else if (type == 0x1c) base = 0x78;
    else if (type < 0x13)  base = 0x58;
    else                   base = 0x68;

    if (type == 0)
        engine_territory_clear_cell_score(cell_off);    /* FUN_0046d9da */

    uint8_t corr = 0x50;
    if (old_type == 0x17 || old_type == 0x18) corr = 0x82;
    uint8_t lo = (uint8_t)((g_cells[cell_off + 3] - corr) & 3);  /* DAT_00522f93 */
    g_cells[cell_off + 3] = (uint8_t)(lo + base + variant * 4);

    uint8_t f = g_cells[cell_off + 2];                  /* DAT_00522f92 */
    f |= 1;
    f &= 0xe3;
    f |= lowbits;
    f &= 0x7f;
    if (type > 0xe && type < 0x17) f |= 0x80;
    g_cells[cell_off + 2] = f;
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

/* Per-army terrain-type census counters — five bytes in the army record
 * (g_per_army_768 row + these offsets) that FUN_00469b8d tallies the
 * army's active tiles into, bucketed by each tile's cell +0 TYPE byte.
 *
 *   +0x1ff  TYPE == 1                 (retail DAT_0053fbaf)
 *   +0x200  TYPE 0x0f..0x16           (retail DAT_0053fbb0)
 *   +0x201  TYPE 0x02..0x0e           (retail DAT_0053fbb1)
 *   +0x203  TYPE 0 / 0x17 / 0x18      (retail DAT_0053fbb3)
 *   +0x204  TYPE >= 0x19              (retail DAT_0053fbb4)
 *
 * Offset +0x202 (DAT_0053fbb2) sits inside this run but is intentionally
 * NOT a bucket and is never touched here — a separate field. */
#define ARMY_CENSUS_TYPE1    0x1ff
#define ARMY_CENSUS_HIGH     0x200
#define ARMY_CENSUS_MID      0x201
#define ARMY_CENSUS_SPECIAL  0x203
#define ARMY_CENSUS_OVER     0x204

/* Port of FUN_00469b8d (404 B) — the new-game per-army **terrain census**.
 * The first per-army callee of the army/country setup pass 0x49bd99 (still
 * unported), run once per realm key (1..realm-count) right after the realm's
 * record is seeded.
 *
 * It tallies how many of the army's active feature cells fall into each
 * terrain-TYPE band, into the five record bytes above.  The army's active
 * cells are listed in the per-key cell roster (g_cell_roster / DAT_0053ea00,
 * 20 i32 per row — the same table engine_territory_rebuild_cell_roster
 * fills): each non-zero column holds a cell's byte-offset, so the cell's
 * +0 TYPE byte is `g_cells[offset]` (offset is already cell_index*8 = the
 * type-byte position).  A zero column is an empty slot and is skipped.
 *
 * The type→bucket mapping is retail's exact if/else ladder (see r2
 * @0x469b8d): TYPE 0, 0x17 and 0x18 share the +0x203 bucket; 1 → +0x1ff;
 * 2..0xe → +0x201; 0xf..0x16 → +0x200; >=0x19 → +0x204.
 *
 * QUIRK: retail zeroes the five counters with a daisy-chain of copies
 * (`fbb0=0; fbb1=fbb0; fbaf=fbb1; fbb4=fbaf; fbb3=fbb4;`) rather than five
 * literal stores — a no-difference idiom that always lands 0 in all five.
 * We zero them directly.
 *
 * No bounds check on `key`: retail trusts its 1..realm-count call site
 * (realm-count <= 16, so the row stays inside g_per_army_768).  No-op if
 * the army-record block is unbound; the counting loop additionally needs
 * the roster and cell grid bound (else the army's counters are just
 * cleared, matching retail when its roster row is empty). */
void engine_territory_census_army_terrain(int key)
{
    if (g_army_records == NULL) return;

    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
    rowbase[ARMY_CENSUS_TYPE1]   = 0;
    rowbase[ARMY_CENSUS_HIGH]    = 0;
    rowbase[ARMY_CENSUS_MID]     = 0;
    rowbase[ARMY_CENSUS_SPECIAL] = 0;
    rowbase[ARMY_CENSUS_OVER]    = 0;

    if (g_cell_roster == NULL || g_cells == NULL) return;

    const int32_t *roster_row = g_cell_roster + key * ROSTER_ROW_I32;
    for (int col = 0; col < ROSTER_COLS; col++) {
        int32_t off = roster_row[col];
        if (off == 0) continue;                 /* empty roster slot */

        unsigned type = g_cells[(unsigned)off]; /* cell +0 TYPE byte */
        if (type == 0 || type == 0x17 || type == 0x18) {
            rowbase[ARMY_CENSUS_SPECIAL]++;
        } else if (type < 0x19) {
            if (type < 2)         rowbase[ARMY_CENSUS_TYPE1]++;  /* type == 1 */
            else if (type < 0xf)  rowbase[ARMY_CENSUS_MID]++;    /* 0x02..0x0e */
            else if (type < 0x17) rowbase[ARMY_CENSUS_HIGH]++;   /* 0x0f..0x16 */
        } else {
            rowbase[ARMY_CENSUS_OVER]++;                         /* >= 0x19 */
        }
    }
}

/* Port of FUN_00469b51 (60 B) — the per-realm terrain-census dispatcher.  Runs
 * engine_territory_census_army_terrain for every realm 1..s_slot_0056d5dc (the
 * live realm count).  One of the per-turn passes the turn-advance engine
 * (0x448440) sequences; also the wrapper the new-game setup uses to census the
 * whole map.  No state of its own — see the per-realm census for bind safety. */
void engine_territory_census_all_realms(void)
{
    int count = (int)s_slot_0056d5dc;
    for (int key = 1; key <= count; key++)
        engine_territory_census_army_terrain(key);
}

/* ──────────────────────────────────────────────────────────────────
 * Per-realm AI development / demolition primitives — the cell-roster
 * mutators reached from the per-army AI dispatch (FUN_0049dd01, the first
 * call of the turn-advance engine 0x448440 → 0x49a990).  Each walks the
 * realm's ≤20-cell roster and stamps cell type bytes through
 * engine_territory_set_cell_type, mirroring the way an AI player builds out
 * or tears down its feature cells across a turn.  All read the same bound
 * cell roster + grid the census uses; no-op when either is unbound.
 *
 * Cell-TYPE bands (the census buckets above name them): 0 = empty,
 * 1 = cleared/level-1, 0x02..0x0e = "mid" developed, 0x0f..0x16 = "high"
 * developed, 0x13..0x16 the upper high tier, 0x17/0x18 special, >= 0x19 the
 * "over"/fully-built tier.
 * ────────────────────────────────────────────────────────────────── */

/* Roster cursor (DAT_0053fb0a) the cycle-demolish helpers advance round the
 * ≤20 columns; ARMY_ACTIVE_COUNT (+0x205) is the wrap length (also defined in
 * the secondary-settlement section below — identical, benign). */
#define ARMY_ROSTER_CURSOR  0x15a
#define ARMY_ACTIVE_COUNT   0x205
/* +0x206 (DAT_0053fbb6) — a "mid" reserve companion decremented in step with
 * the census MID bucket when a mid cell is demolished; +0x234 / +0x244
 * (DAT_0053fbe4 / DAT_0053fbf4) — paired stock accumulators the demolish
 * refunds a 1/MID fraction between.  Semantics provisional. */
#define ARMY_MID_RESERVE    0x206
#define ARMY_STOCK_234      0x234
#define ARMY_STOCK_244      0x244

/* Port of FUN_0044c6c4 (182 B).  Develop up to `count` of the realm's cells to
 * the fully-built tier 0x19: walk the roster, stamp each empty (type 0) cell to
 * 0x19 (consuming one of `count`), skip cells already part-built (type 1..0x18,
 * which neither develop nor consume), and let cells already at >= 0x19 consume a
 * slot too — so `count` bounds the realm's total >= 0x19 cells after the pass.
 * No-op when the roster or cell grid is unbound. */
void engine_territory_develop_empty_cells(int key, int count)
{
    if (g_cell_roster == NULL || g_cells == NULL) return;

    const int32_t *roster_row = g_cell_roster + key * ROSTER_ROW_I32;
    for (int col = 0; col < ROSTER_COLS; col++) {
        int32_t off = roster_row[col];
        if (off == 0) continue;
        if (g_cells[(unsigned)off] < 0x19) {
            if (g_cells[(unsigned)off] != 0) continue;   /* part-built — leave */
            engine_territory_set_cell_type(off, 0x19, 0);
        }
        if (--count < 1) return;
    }
}

/* Port of FUN_004697cd (192 B).  Demolish a band of the realm's cells back to
 * the cleared tier 1.  `mode` selects the band: 2 → demolish "mid" cells
 * (types 0x02..0x0e), 0x13 → demolish upper-high cells (types 0x13..0x16).
 * No-op when the roster or cell grid is unbound. */
void engine_territory_demolish_band(int key, int mode)
{
    if (g_cell_roster == NULL || g_cells == NULL) return;

    const int32_t *roster_row = g_cell_roster + key * ROSTER_ROW_I32;
    for (int col = 0; col < ROSTER_COLS; col++) {
        int32_t off = roster_row[col];
        if (off == 0) continue;
        unsigned b = g_cells[(unsigned)off];
        if (mode == 2) {
            if (b > 1 && b < 0xf) engine_territory_set_cell_type(off, 1, 0);
        } else if (mode == 0x13) {
            if (b > 0x12 && b < 0x17) engine_territory_set_cell_type(off, 1, 0);
        }
    }
}

/* Port of FUN_0046988d (274 B).  As engine_territory_demolish_band, but first
 * spend a `budget` of conversions: each cleared (type 1) cell is upgraded to
 * `mode` while budget remains; once the budget is exhausted, surplus cells in
 * the `mode` band are demolished back to type 1 (cells that would have been
 * converted while budget remained instead just draw it down).  No-op when the
 * roster or cell grid is unbound. */
void engine_territory_convert_or_demolish_band(int key, int mode, int budget)
{
    if (g_cell_roster == NULL || g_cells == NULL) return;

    const int32_t *roster_row = g_cell_roster + key * ROSTER_ROW_I32;
    for (int col = 0; col < ROSTER_COLS; col++) {
        int32_t off = roster_row[col];
        if (off == 0) continue;
        unsigned b = g_cells[(unsigned)off];
        if (b == 1 && budget > 0) {
            engine_territory_set_cell_type(off, mode, 0);
            budget--;
        } else if (mode == 2) {
            if (b > 1 && b < 0xf) {
                if (budget < 1) engine_territory_set_cell_type(off, 1, 0);
                else            budget--;
            }
        } else if (mode == 0x13) {
            if (b > 0x12 && b < 0x17) {
                if (budget < 1) engine_territory_set_cell_type(off, 1, 0);
                else            budget--;
            }
        }
    }
}

/* Port of FUN_0046958f (203 B).  Advance the roster cursor (+0x15a, wrapping at
 * the active-cell count +0x205) until it lands on a cleared (type 1) cell, up to
 * 20 steps, then stamp that cell to the high tier 0x13.  Leaves the cursor on
 * the chosen cell.  No-op (cursor still advances) when no type-1 cell is found
 * within 20 steps, or when the roster / cell grid is unbound. */
void engine_territory_cycle_set_type1(int key)
{
    if (g_army_records == NULL) return;
    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
    if (g_cell_roster == NULL || g_cells == NULL) return;

    const int32_t *roster_row = g_cell_roster + key * ROSTER_ROW_I32;
    for (int step = 0; step <= 0x13; step++) {
        uint8_t cur = (uint8_t)(rowbase[ARMY_ROSTER_CURSOR] + 1);
        if (rowbase[ARMY_ACTIVE_COUNT] <= cur) cur = 0;
        rowbase[ARMY_ROSTER_CURSOR] = cur;
        int32_t off = roster_row[cur];
        if (off != 0 && g_cells[(unsigned)off] == 1) {
            engine_territory_set_cell_type(off, 0x13, 0);
            return;
        }
    }
}

/* Port of FUN_0046965a (371 B).  Advance the roster cursor until it lands on a
 * "mid" developed cell (type 0x02..0x0e), up to 20 steps; on a hit, demolish it
 * to the high tier 0x13, move a 1/MID fraction of stock +0x244 into +0x234
 * (refunding the invested input), draw down the +0x206 mid reserve when it still
 * covers the MID census count, and decrement the MID bucket (+0x201).  No-op
 * when no mid cell is found within 20 steps or the roster / grid is unbound.
 * Callers guarantee MID > 0 (the only site, FUN_0046921d, gates on it). */
void engine_territory_cycle_demolish_mid(int key)
{
    if (g_army_records == NULL) return;
    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
    if (g_cell_roster == NULL || g_cells == NULL) return;

    const int32_t *roster_row = g_cell_roster + key * ROSTER_ROW_I32;
    for (int step = 0; step <= 0x13; step++) {
        uint8_t cur = (uint8_t)(rowbase[ARMY_ROSTER_CURSOR] + 1);
        if (rowbase[ARMY_ACTIVE_COUNT] <= cur) cur = 0;
        rowbase[ARMY_ROSTER_CURSOR] = cur;
        int32_t off = roster_row[cur];
        if (off != 0 && g_cells[(unsigned)off] > 1 && g_cells[(unsigned)off] < 0xf) {
            engine_territory_set_cell_type(off, 0x13, 0);

            int frac = engine_math_percent_div(1, rowbase[ARMY_CENSUS_MID]);
            int moved = engine_math_percent_of(
                *(int32_t *)(rowbase + ARMY_STOCK_244), frac);
            *(int32_t *)(rowbase + ARMY_STOCK_244) -= moved;
            *(int32_t *)(rowbase + ARMY_STOCK_234) += moved;

            if (rowbase[ARMY_CENSUS_MID] <= rowbase[ARMY_MID_RESERVE])
                rowbase[ARMY_MID_RESERVE]--;
            rowbase[ARMY_CENSUS_MID]--;
            return;
        }
    }
}

/* Port of FUN_0046921d (138 B).  Re-census the realm, then reduce over-built
 * footprints when it has no high-tier cell (+0x200 == 0): if it also has no
 * cleared/level-1 cell, demolish one mid cell (engine_territory_cycle_demolish_mid)
 * when any remain; otherwise cycle one level-1 cell up to 0x13
 * (engine_territory_cycle_set_type1).  Re-censuses afterward so the buckets
 * reflect the change.  No-op contributions when the records / roster are
 * unbound (each callee guards). */
void engine_territory_recensus_reduce(int key)
{
    if (g_army_records == NULL) return;
    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;

    engine_territory_census_army_terrain(key);
    if (rowbase[ARMY_CENSUS_HIGH] == 0) {
        if (rowbase[ARMY_CENSUS_TYPE1] == 0) {
            if (rowbase[ARMY_CENSUS_MID] != 0)
                engine_territory_cycle_demolish_mid(key);
        } else {
            engine_territory_cycle_set_type1(key);
        }
        engine_territory_census_army_terrain(key);
    }
}

/* Realm-record fields the secondary-settlement stamp reads to pick its
 * descriptor tier (both inside g_per_army_768 row `key`). */
#define ARMY_SETTLE_FLAG  0x1c3   /* DAT_0053fb73 — secondary settlement present  */
#define ARMY_SETTLE_SIZE  0x1c4   /* DAT_0053fb74 — its size/strength byte        */

/* Port of FUN_0046826c (221 B) — stamp the realm's **secondary settlement**
 * 2×2 footprint, a per-realm callee of the army/country setup pass 0x49bd99
 * (called once per realm whose banner-level field +0x1c0 is non-zero, with
 * `level` = that field − 1).  The parallel of the capital placer
 * FUN_00467fd1, which stamps the *capital* 2×2 block; this one stamps at the
 * realm's already-chosen **secondary** cell (record +0x78, ARMY_SEC_OFF).
 *
 * The descriptor base is one of three sprite tiers chosen by the realm's
 * settlement fields, plus a per-level `level*4` stride within the tier:
 *
 *   no secondary settlement (+0x1c3 == 0) → base 'P' (0x50)
 *   small settlement (+0x1c4 < 0x32)      → base '(' (0x28)
 *   large settlement (+0x1c4 >= 0x32)     → base '<' (0x3c)
 *
 * and the +0 TYPE byte is 0x15 + min(level, 4) (0x15..0x19).  The actual
 * cell writes (the 2×2 descriptor gradient, the +2 flag bits 0x10, the +0
 * type) are the shared engine_territory_stamp_block, so this is a thin
 * tier-select wrapper.  No-op if the army-record block is unbound (and
 * stamp_block itself no-ops if the cell grid is unbound). */
void engine_territory_stamp_secondary_keep(int key, int level)
{
    if (g_army_records == NULL) return;

    const uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;

    int base;
    if (rowbase[ARMY_SETTLE_FLAG] == 0)        base = level * 4 + 0x50;  /* 'P' */
    else if (rowbase[ARMY_SETTLE_SIZE] < 0x32) base = level * 4 + 0x28;  /* '(' */
    else                                       base = level * 4 + 0x3c;  /* '<' */

    uint8_t type;
    switch (level) {
        case 0:  type = 0x15; break;
        case 1:  type = 0x16; break;
        case 2:  type = 0x17; break;
        case 3:  type = 0x18; break;
        default: type = 0x19; break;           /* level >= 4 */
    }

    /* +0x78 = ARMY_SEC_OFF (DAT_0053fa28, the secondary cell byte-offset);
     * that macro is defined further down with the capital-region block. */
    int off = *(const int32_t *)(rowbase + 0x78);
    engine_territory_stamp_block(base, 2, off, 0x10, type);
}

/* ──────────────────────────────────────────────────────────────────
 * Per-realm population → job-slot allocator (FUN_0044f6e7)
 * ──────────────────────────────────────────────────────────────────
 *
 * Field offsets within the realm's g_per_army_768 row.  The eight job
 * counters and their caps form a regular run of 0xc-byte triples starting
 * at +0xc4: counter i is at +0xc4 + 0xc*i, and its cap at +0xcc + 0xc*i
 * (= counter + 8).  The ninth 0xc-slot at +0x124 (i == 8) is not a job
 * counter but the unallocated **leftover** and has no cap.  The eight
 * per-bucket target ratios are a separate contiguous i32 run at +0x130
 * (ratio i at +0x130 + 4*i, paired with counter i). */
#define WF_TOTAL       0x24    /* DAT_0053f9d4 — realm population total (i32)   */
#define WF_SPLIT_PCT   0x08    /* DAT_0053f9b8 — pool-1 share, signed-byte %    */
#define WF_FOLD_THRESH 0xb8    /* DAT_0053fa68 — small-leftover fold gate (u8)  */
#define WF_COUNTER(i)  (0xc4 + 0xc * (i))
#define WF_CAP(i)      (0xcc + 0xc * (i))   /* == WF_COUNTER(i) + 8 */
#define WF_RATIO(i)    (0x130 + 4 * (i))
#define WF_LEFTOVER    0x124   /* DAT_0053fad4 — counter slot 8: the remainder  */

/* "Building present" flags, one per phase-2 job bucket (i32 caps zeroed when
 * the flag is clear).  The first three are the stride-0x18 settlement flags
 * the setup pass at 0x49bd99 lights (+0x297/+0x2af/+0x2c7/+0x2df pairs); the
 * mapping to buckets is the retail (non-sequential) order. */
#define WF_GATE_C6     0x297   /* DAT_0053fc47 → cap6 (+0x114) */
#define WF_GATE_C4     0x2af   /* DAT_0053fc5f → cap4 (+0xfc)  */
#define WF_GATE_C7     0x2c7   /* DAT_0053fc77 → cap7 (+0x120) */
#define WF_GATE_C5     0x2df   /* DAT_0053fc8f → cap5 (+0x108) */
#define WF_GATE_C3A    0x1c3   /* DAT_0053fb73 (== ARMY_SETTLE_FLAG) → cap3 */
#define WF_GATE_C3B    0x1b0   /* DAT_0053fb60                        → cap3 */

/* Read a job-bucket cap field, mapping retail's 999999 "unlimited" sentinel
 * to 0 (the same value an absent bucket carries, so a sentinel cap fills
 * nothing — retail's behaviour). */
static int realm_workforce_cap(const uint8_t *row, int off)
{
    int v = *(const int32_t *)(row + off);
    return (v == 999999) ? 0 : v;
}

/* Port of FUN_0044f6e7 (2147 B) — the new-game per-realm **population/job-slot
 * allocator**, a per-realm callee of the army/country setup pass 0x49bd99 (run
 * twice per realm there, with the intervening 0x44df5f / 0x4485a5 passes
 * re-tuning the caps between the two distributions).
 *
 * The realm's total population (record +0x24) is split by the signed-byte
 * percentage at +0x08 into two pools — pool1 = total*pct/100, pool2 = the
 * remainder — which are then distributed across eight job-slot counters, each
 * capped by a per-bucket maximum and steered toward a per-bucket target ratio:
 *
 *   pool2 (remainder)  → counters 0,1,2  (ungated baseline buckets)
 *   pool1 (the share)  → counters 3..7   (settlement/building buckets, each
 *                                         capped to 0 unless its flag is set)
 *
 * Each pool is distributed in two steps: first a greedy pre-fill that pushes
 * each bucket toward `percent_of(pool, ratio_i)` (bounded by its cap), then a
 * weighted round-robin that deals the still-unspent pool one head at a time
 * across the buckets that still have room.  Phase-1's round-robin weights the
 * first three buckets 2 : 3 : 1; phase-2 deals buckets 6,5,4,7 once each per
 * pass with bucket 3 as the catch-all.  Between the two phases a small pool-2
 * remainder (< the +0xb8 threshold byte) is folded back into pool1.  Whatever
 * is left of both pools after the round-robins lands in the +0x124 leftover
 * slot.  Caps carrying retail's 999999 "unlimited" sentinel count as 0 (the
 * greedy/round-robin `counter < cap` test then never fires for them — the same
 * no-fill the engine relies on).
 *
 * The only callee is engine_math_percent_of (FUN_00404d6b).  Touches nothing
 * but the realm's own record row, so no cell grid is needed.  No bounds check
 * on `key`: retail trusts its 1..realm-count call site.  No-op if the
 * army-record block is unbound. */
void engine_territory_allocate_realm_workforce(int key)
{
    if (g_army_records == NULL) return;

    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;

    int total = *(const int32_t *)(rowbase + WF_TOTAL);
    int pct   = (int8_t)rowbase[WF_SPLIT_PCT];
    int pool1 = engine_math_percent_of(total, pct);  /* retail local_20 */
    int pool2 = total - pool1;                        /* retail local_14 */

    /* Per-bucket caps (999999 → 0), then the phase-2 building gates. */
    int cap0 = realm_workforce_cap(rowbase, WF_CAP(0));
    int cap1 = realm_workforce_cap(rowbase, WF_CAP(1));
    int cap2 = realm_workforce_cap(rowbase, WF_CAP(2));
    int cap6 = realm_workforce_cap(rowbase, WF_CAP(6));
    if (rowbase[WF_GATE_C6] == 0) cap6 = 0;
    int cap5 = realm_workforce_cap(rowbase, WF_CAP(5));
    if (rowbase[WF_GATE_C5] == 0) cap5 = 0;
    int cap4 = realm_workforce_cap(rowbase, WF_CAP(4));
    if (rowbase[WF_GATE_C4] == 0) cap4 = 0;
    int cap7 = realm_workforce_cap(rowbase, WF_CAP(7));
    if (rowbase[WF_GATE_C7] == 0) cap7 = 0;
    int cap3 = realm_workforce_cap(rowbase, WF_CAP(3));
    if (rowbase[WF_GATE_C3A] == 0) cap3 = 0;
    if (rowbase[WF_GATE_C3B] == 0) cap3 = 0;

    /* All eight counters (and the leftover slot) start at 0 — retail clears
     * the nine 0xc-slots up front; we accumulate in locals and store finals. */
    int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0, c6 = 0, c7 = 0;
    int progressed;

    /* ── Phase 1: distribute pool2 across buckets 0,1,2 ───────────────── */
    int g1 = engine_math_percent_of(pool2, *(const int32_t *)(rowbase + WF_RATIO(1)));
    int g0 = engine_math_percent_of(pool2, *(const int32_t *)(rowbase + WF_RATIO(0)));
    int g2 = engine_math_percent_of(pool2, *(const int32_t *)(rowbase + WF_RATIO(2)));
    while (g1 > 0 && c1 < cap1) { c1++; g1--; pool2--; }
    while (g0 > 0 && c0 < cap0) { c0++; g0--; pool2--; }
    while (g2 > 0 && c2 < cap2) { c2++; g2--; pool2--; }

    progressed = (pool2 >= 1);
    for (;;) {
        for (;;) {
            if (!progressed) goto phase1_done;
            progressed = 0;
            if (c0 < cap0) { c0++; progressed = 1; if (--pool2 < 1) goto phase1_done; }
            if (c0 < cap0) { c0++; progressed = 1; if (--pool2 < 1) goto phase1_done; }
            if (c1 < cap1) { c1++; progressed = 1; if (--pool2 < 1) goto phase1_done; }
            if (c1 < cap1) { c1++; progressed = 1; if (--pool2 < 1) goto phase1_done; }
            if (c1 < cap1) { c1++; progressed = 1; if (--pool2 < 1) goto phase1_done; }
            if (c2 < cap2) break;   /* retail inner-loop guard: repeat while c2 full */
        }
        c2++; progressed = 1; pool2--;
        if (pool2 <= 0) goto phase1_done;
    }
phase1_done:

    /* Fold a small pool-2 remainder back into pool1 before phase 2. */
    if (pool2 < (int)rowbase[WF_FOLD_THRESH]) { pool1 += pool2; pool2 = 0; }

    /* ── Phase 2: distribute pool1 across buckets 3..7 ────────────────── */
    int g6 = engine_math_percent_of(pool1, *(const int32_t *)(rowbase + WF_RATIO(6)));
    int g5 = engine_math_percent_of(pool1, *(const int32_t *)(rowbase + WF_RATIO(5)));
    int g4 = engine_math_percent_of(pool1, *(const int32_t *)(rowbase + WF_RATIO(4)));
    int g7 = engine_math_percent_of(pool1, *(const int32_t *)(rowbase + WF_RATIO(7)));
    int g3 = engine_math_percent_of(pool1, *(const int32_t *)(rowbase + WF_RATIO(3)));
    while (g6 > 0 && c6 < cap6) { c6++; g6--; pool1--; }
    while (g5 > 0 && c5 < cap5) { c5++; g5--; pool1--; }
    while (g4 > 0 && c4 < cap4) { c4++; g4--; pool1--; }
    while (g7 > 0 && c7 < cap7) { c7++; g7--; pool1--; }
    while (g3 > 0 && c3 < cap3) { c3++; g3--; pool1--; }

    progressed = (pool1 >= 1);
    for (;;) {
        for (;;) {
            if (!progressed) goto phase2_done;
            progressed = 0;
            if (c6 < cap6) { c6++; progressed = 1; if (--pool1 < 1) goto phase2_done; }
            if (c5 < cap5) { c5++; progressed = 1; if (--pool1 < 1) goto phase2_done; }
            if (c4 < cap4) { c4++; progressed = 1; if (--pool1 < 1) goto phase2_done; }
            if (c7 < cap7) { c7++; progressed = 1; if (--pool1 < 1) goto phase2_done; }
            if (c3 < cap3) break;   /* retail inner-loop guard: repeat while c3 full */
        }
        c3++; progressed = 1; pool1--;
        if (pool1 <= 0) goto phase2_done;
    }
phase2_done:

    /* Store the eight job counters and the leftover (= both pools' residue). */
    *(int32_t *)(rowbase + WF_COUNTER(0)) = c0;
    *(int32_t *)(rowbase + WF_COUNTER(1)) = c1;
    *(int32_t *)(rowbase + WF_COUNTER(2)) = c2;
    *(int32_t *)(rowbase + WF_COUNTER(3)) = c3;
    *(int32_t *)(rowbase + WF_COUNTER(4)) = c4;
    *(int32_t *)(rowbase + WF_COUNTER(5)) = c5;
    *(int32_t *)(rowbase + WF_COUNTER(6)) = c6;
    *(int32_t *)(rowbase + WF_COUNTER(7)) = c7;
    *(int32_t *)(rowbase + WF_LEFTOVER)   = pool2 + pool1;
}

/* Port of FUN_0044f699 (78 B) — the per-realm workforce-allocation dispatcher.
 * Runs engine_territory_allocate_realm_workforce for every realm
 * 1..s_slot_0056d5dc (the live realm count).  One of the per-turn passes the
 * turn-advance engine (0x448440) sequences — it appears there twice, once each
 * side of the territory-income/economy re-plan.  The retail loop also reads a
 * per-realm byte (g_per_army_768 row + 0x53f9b5 base) into a scratch local that
 * it never consumes; Ghidra and the host port both elide that dead load.  No
 * state of its own beyond the dispatch — see the per-realm allocator for bind
 * safety. */
void engine_territory_allocate_all_workforce(void)
{
    int count = (int)s_slot_0056d5dc;
    for (int key = 1; key <= count; key++)
        engine_territory_allocate_realm_workforce(key);
}

/* Realm-record flag the band-retype consults for its lo==2 special case
 * (g_per_army_768 row + 0x1a7, retail DAT_0053fb57). */
#define ARMY_RETYPE_FLAG 0x1a7

/* Port of FUN_00469d21 (237 B) — retype an army's active feature cells whose
 * TYPE falls in a band.  Walk all 4096 overworld cells in byte order; for each
 * cell owned by `key` (byte +7), flagged active (byte +1 bit 0x20), with its
 * TYPE byte (+0) in [lo, hi] inclusive, set its terrain type via
 * engine_territory_set_cell_type (FUN_0046d7f4).
 *
 * The first matching cell always becomes (type, variant).  For every *later*
 * match, when the band's low bound is 2 and the realm's +0x1a7 flag is set, the
 * cell is forced to type 2 / variant 0 instead — the band's first cell keeps
 * the requested type and the remainder collapse to base type 2.  (The
 * 0x49bd99 → 0x44d913 caller passes lo == 0x13, so that special case never
 * fires on the new-game path; the 0x44c8ae caller passes lo == 2 and exercises
 * it.)
 *
 * No-op (no cells touched) if the cell grid or army-record block is unbound. */
void engine_territory_retype_army_band(int key, int type, char variant,
                                       int lo, int hi)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    int seen = 0;
    for (int off = 0; off < CELL_GRID_CELLS * CELL_STRIDE; off += CELL_STRIDE) {
        if (g_cells[off + 7] != key)        continue;   /* +7 owner key   */
        if ((g_cells[off + 1] & 0x20) == 0) continue;   /* +1 active flag */
        int t = g_cells[off + 0];                       /* +0 TYPE byte   */
        if (t < lo || t > hi)               continue;

        if (lo == 2 && seen &&
            g_army_records[key * ARMY_ROW_STRIDE + ARMY_RETYPE_FLAG] != 0)
            engine_territory_set_cell_type(off, 2, 0);
        else
            engine_territory_set_cell_type(off, type, variant);
        seen = 1;
    }
}

/* Realm-record fields the density grader reads/writes (g_per_army_768 row):
 *   +0x250  load numerator (i32, retail DAT_0053fc00)
 *   +0x25c  graded value out (i32, retail DAT_0053fc0c)
 * The divisor is the census HIGH bucket +0x200 (ARMY_CENSUS_HIGH), the count
 * of the realm's TYPE 0x0f..0x16 cells the census tallied. */
#define ARMY_DENSITY_LOAD  0x250
#define ARMY_DENSITY_VALUE 0x25c

/* Port of FUN_0044d913 (390 B) — grade the realm's feature cells by population
 * density, a per-realm callee of the army/country setup pass 0x49bd99 (run in
 * its first loop, right after the terrain census seeds +0x200).
 *
 * The density is load(+0x250) per high-terrain cell (the census HIGH count at
 * +0x200): 0 when there is no load, the "saturated" 1000 when there is load but
 * no high-terrain cell to divide by, else the integer quotient.  That density
 * picks a feature-TYPE tier — no load → 0x13, density <0xb → 0x14, <0x15 →
 * 0x15, else 0x16 — which is applied to every one of the realm's feature cells
 * in band [0x13, 0x16] via engine_territory_retype_army_band (FUN_00469d21).
 *
 * It also records a derived value at +0x25c on a parallel density ladder
 * (<0xb → 10, <0x15 → 0x14, <0x1f → 0x1e, else 0x28), overridden to 0x28 when
 * the realm has no high-terrain cell at all.  No new globals; touches only the
 * realm's record (and, through the retype, its owned cells).  No-op if the cell
 * grid or army-record block is unbound. */
void engine_territory_grade_feature_cells(int key)
{
    if (g_army_records == NULL) return;

    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
    int load = *(const int32_t *)(rowbase + ARMY_DENSITY_LOAD);
    int high = rowbase[ARMY_CENSUS_HIGH];           /* +0x200 census bucket */

    int density;
    if (load < 1)        density = 0;
    else if (high == 0)  density = 1000;
    else                 density = load / high;

    int tier;
    if (load < 1)            tier = 0x13;
    else if (density < 0xb)  tier = 0x14;
    else if (density < 0x15) tier = 0x15;
    else                     tier = 0x16;

    engine_territory_retype_army_band(key, tier, 0, 0x13, 0x16);

    int value;
    if (density < 0xb)       value = 10;
    else if (density < 0x15) value = 0x14;
    else if (density < 0x1f) value = 0x1e;
    else                     value = 0x28;
    if (high == 0)           value = 0x28;

    *(int32_t *)(rowbase + ARMY_DENSITY_VALUE) = value;
}

/* Per-army active-cell tally byte: row offset +0x205 (retail DAT_0053fbb5
 * = g_per_army_768 + 0x205). */
#define ARMY_ACTIVE_COUNT 0x205

/* Port of FUN_00467a36 (624 B) — new-game per-army **feature seeding** pass,
 * the second still-unported direct callee of the overworld-grid setup
 * orchestrator at 0x4676e0 (unported).  For every active feature-cell (key +7 in
 * 1..17, flags byte +1 bit 0x20 set), it assigns a feature terrain type
 * based on how many of that army's active cells have been seen so far and
 * the global feature-density setting DAT_0053f23c (s_slot_0053f23c, save
 * entry 13).  The first few cells of each army get the "rich" type 0x14
 * (descriptor base 'h' = 0x68), the next tier type 1 ('T' = 0x54), the rest
 * type 0 ('P' = 0x50) — the per-density cutoffs:
 *
 *   density 3 : idx<4 → 0x14 ; else → 0
 *   density 2 : idx<4 → 0x14 ; idx<6 → 1 ; else → 0
 *   density 1 : idx<6 → 0x14 ; idx<8 → 1 ; else → 0
 *   else (0)  : idx<8 → 0x14 ; else → 1
 *
 * For each chosen cell it pre-writes the +3 descriptor (keeping the old
 * low 2 bits, via the same -0x50≡+0xb0 (mod 4) correction) and the +0 type,
 * then calls engine_territory_set_cell_type (FUN_0046d7f4) to finalize the
 * flag byte and re-derive the descriptor.  The pre-write of +0 is
 * load-bearing: it makes the setter see the *new* type as its "old type",
 * so the setter's 0x17/0x18 correction never fires here (the new type is
 * always 0x14/1/0).  It also keeps a per-army tally at row +0x205, bumped
 * once per active cell.
 *
 * QUIRK (#132, preserved verbatim): the key accept guard is `< 0x12`
 * (admits 17); the per-army block has only 17 rows (0..16), so a key of 17
 * would index a row past g_per_army_768 at +0x205.  Unreachable — the +7
 * producers gate the field to 1..16.  Tests stay within 1..16.
 *
 * No-op if the cell grid or the army-record block is unbound. */
void engine_territory_seed_army_features(void)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    for (int key = 1; key < 0x11; key++)                       /* rows 1..16 */
        g_army_records[key * ARMY_ROW_STRIDE + ARMY_ACTIVE_COUNT] = 0;

    int seen[32] = {0};      /* per-key running active-cell index (retail stack) */

    int cursor_off = 0;
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++, cursor_off += CELL_STRIDE) {
            uint8_t flags = g_cells[cursor_off + 1];           /* DAT_00522f91 */
            unsigned key  = g_cells[cursor_off + 7];           /* DAT_00522f97 */
            uint8_t desc3 = g_cells[cursor_off + 3];           /* DAT_00522f93 */

            if (key == 0 || key >= 0x12)  continue;            /* 1..17 (quirk #132) */
            if ((flags & 0x20) == 0)      continue;

            g_army_records[key * ARMY_ROW_STRIDE + ARMY_ACTIVE_COUNT]++;
            int idx = seen[key]++;

            uint8_t new_type;
            char    base;
            uint32_t density = s_slot_0053f23c;                /* DAT_0053f23c */
            if (density == 3) {
                if (idx < 4)      { new_type = 0x14; base = 'h'; }
                else              { new_type = 0;    base = 'P'; }
            } else if (density == 2) {
                if (idx < 4)      { new_type = 0x14; base = 'h'; }
                else if (idx < 6) { new_type = 1;    base = 'T'; }
                else              { new_type = 0;    base = 'P'; }
            } else if (density == 1) {
                if (idx < 6)      { new_type = 0x14; base = 'h'; }
                else if (idx < 8) { new_type = 1;    base = 'T'; }
                else              { new_type = 0;    base = 'P'; }
            } else {
                if (idx < 8)      { new_type = 0x14; base = 'h'; }
                else              { new_type = 1;    base = 'T'; }
            }

            uint8_t lo = (uint8_t)((desc3 + 0xb0) & 3);
            g_cells[cursor_off + 3] = (uint8_t)(lo + base);
            g_cells[cursor_off + 0] = new_type;
            engine_territory_set_cell_type(cursor_off, new_type, 0);  /* FUN_0046d7f4 */
        }
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Per-army capital-region setup (the 0x468d4f subtree)
 *
 * The new-game overworld setup (0x4676e0) runs this once per army to stand
 * up each army's home region on the overworld grid: a 2×2 capital, the
 * bordering cells of its claim, a handful of special feature cells, a
 * coastal port, and a secondary cell cluster.  All passes operate on the
 * 64×64×8 cell grid (g_cells) and write results into the per-army record
 * block (g_army_records, 0x300-byte rows).
 * ────────────────────────────────────────────────────────────────── */

/* Retail's N×N block fill-order tables (DAT_004d80e0 / e8 / f8), transcribed
 * from Lords2.exe (r2 @0x4d80e0).  Each lists, for an N×N descriptor-stamp
 * block filled in row-major order (col fastest), the gradient offset added to
 * the base descriptor char at each successive cell — a diagonal/spiral ramp.
 * Every reachable call site passes size 2, so only the 2×2 table is live; the
 * 3×3 / 4×4 tables are kept for fidelity but are currently dead. */
static const uint8_t BLOCK_FILL_2[4]  = { 0, 2, 1, 3 };
static const uint8_t BLOCK_FILL_3[9]  = { 0, 2, 5, 1, 4, 7, 3, 6, 8 };
static const uint8_t BLOCK_FILL_4[16] = { 0, 2, 5, 9, 1, 4, 8, 0xc,
                                          3, 7, 0xb, 0xe, 6, 0xa, 0xd, 0xf };

/* Port of FUN_0046ac22 (329 B) — stamp a `size`×`size` block of cells onto
 * the grid starting at byte-offset `off`.  For each cell, in row-major order:
 *   +2 |= 1; +2 &= 0xe3 (clear bits 2..4); +3 ← fill[k] + base (descriptor
 *   gradient, see tables above; for size 1 just `base`, no table); +2 |= flag;
 *   +0 ← type.
 * The block is a contiguous size×size sub-square of the 64-wide grid, so the
 * cursor advances +8 per column and skips (64 - size) cells at each row end.
 *
 * Shared (not init-only): also called by the AI/event castle placers
 * (0x4685.. and the 0x46902a region); lives here with the other cell-grid
 * mutators.  No bounds check on `off`/`size` — retail trusts its call sites
 * (every one passes size 2 onto an in-bounds capital cell).  No-op if the
 * cell grid is unbound. */
void engine_territory_stamp_block(int base, int size, int off,
                                  uint8_t flag_bits, uint8_t type)
{
    if (g_cells == NULL) return;

    int k = 0;
    for (int r = 0; r < size; r++) {
        for (int c = 0; c < size; c++) {
            g_cells[off + 2] |= 1;
            g_cells[off + 2] &= 0xe3;
            if (size == 1)      g_cells[off + 3] = (uint8_t)base;
            else if (size == 2) g_cells[off + 3] = (uint8_t)(BLOCK_FILL_2[k] + base);
            else if (size == 3) g_cells[off + 3] = (uint8_t)(BLOCK_FILL_3[k] + base);
            else if (size == 4) g_cells[off + 3] = (uint8_t)(BLOCK_FILL_4[k] + base);
            g_cells[off + 2] |= flag_bits;
            g_cells[off + 0] = type;
            off += CELL_STRIDE;
            k++;
        }
        off += (0x40 - size) * CELL_STRIDE;
    }
}

/* Port of FUN_00467fd1 (336 B) — locate an army's capital cell and stamp it.
 * Scans the grid in row-major order for cells owned by `key` (+7 == key) that
 * are castle-eligible (+1 bit 0x40 set) and still empty (+0 == 0), counting
 * matches:
 *   1st match: record its offset, set +2 bit 0x80, and stamp a 2×2 capital
 *              footprint (engine_territory_stamp_block('/', 2, off, 0xc, 0)).
 *   3rd match: set +2 bit 0x80.
 *   4th match: leave g_cursor_col/row (DAT_0057c8d0 / DAT_0056846c) at this
 *              cell's (col, row) and RETURN the 1st match's offset.
 * If fewer than 4 eligible cells exist, returns 0 (and the cursor is left
 * unchanged from a prior pass — faithful to retail, which only writes it on
 * the 4th hit).  No-op (returns 0) if the cell grid is unbound. */
int engine_territory_place_capital(unsigned key)
{
    if (g_cells == NULL) return 0;

    int found = 0;      /* local_18 — running match count */
    int first_off = 0;  /* local_c  — offset of the 1st match (the capital)  */
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++) {
            int off = (row * 0x40 + col) * CELL_STRIDE;
            if (g_cells[off + 7] != (uint8_t)key)   continue;   /* DAT_00522f97 */
            if ((g_cells[off + 1] & 0x40) == 0)     continue;   /* DAT_00522f91 */
            if (g_cells[off + 0] != 0)              continue;   /* DAT_00522f90 */

            if (found == 0) {
                first_off = off;
                g_cells[off + 2] |= 0x80;                       /* DAT_00522f92 */
                engine_territory_stamp_block('/', 2, off, 0xc, 0);
            }
            if (found == 2)
                g_cells[off + 2] |= 0x80;
            if (found == 3) {
                g_cursor_row = row;     /* DAT_0056846c */
                g_cursor_col = col;     /* DAT_0057c8d0 */
                return first_off;
            }
            found++;
        }
    }
    return 0;
}

/* Per-army record field offsets the capital-region passes write (retail
 * DAT_0053f9b0 + the army's 0x300 row).  The cell-list is a run of i32
 * cell byte-offsets; the orchestrator's own fields are added with Unit 6. */
#define ARMY_CELL_LIST  0x80   /* DAT_0053fa30 — i32 list of collected cells */

/* Port of FUN_00468c41 (270 B) — collect army `key`'s claimed cells into its
 * record's cell-list.  Row-major scan for cells flagged claimed (+1 bit 0x10)
 * and owned by `key` (+7 == key); each match's byte-offset is appended (as an
 * i32) to the army row's +0x80 list, the cell's +6 byte is set to its +3
 * descriptor, and its +0 type byte is cleared.
 *
 * Retail caps nothing: the running index just keeps incrementing, trusting
 * the count of bit-0x10 cells to stay within the list region (≤ (0x300-0x80)/4
 * = 136 before it would spill into the next army row).  Faithful here; the
 * producers of the 0x10 flag keep capital regions small.  No terminator is
 * written — the consumer relies on the pre-zeroed list (a 0 offset is the
 * implicit end, the same cell-(0,0) blind spot the roster has).
 *
 * No-op if the cell grid or the army-record block is unbound. */
void engine_territory_collect_keyed_cells(unsigned key)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    int32_t *list = (int32_t *)(g_army_records + key * ARMY_ROW_STRIDE + ARMY_CELL_LIST);
    int n = 0;
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++) {
            int off = (row * 0x40 + col) * CELL_STRIDE;
            if ((g_cells[off + 1] & 0x10) == 0)   continue;   /* DAT_00522f91 */
            if (g_cells[off + 7] != (uint8_t)key) continue;   /* DAT_00522f97 */

            list[n++] = off;
            g_cells[off + 6] = g_cells[off + 3];              /* +6 ← +3 descriptor */
            g_cells[off + 0] = 0;                             /* +0 ← 0  (type)     */
        }
    }
}

/* Per-army "special feature cell" slots the 0x468e61 pass fills: a present-flag
 * byte + an i32 cell byte-offset, one pair per feature kind (retail
 * DAT_0053fc5d/60, fc8d/90, fc45/48 = g_per_army_768 + these offsets). */
#define ARMY_FEAT_A_FLAG 0x2ad   /* DAT_0053fc5d — descriptor 0x1e present */
#define ARMY_FEAT_A_OFF  0x2b0   /* DAT_0053fc60 */
#define ARMY_FEAT_B_FLAG 0x2dd   /* DAT_0053fc8d — descriptor 0 present    */
#define ARMY_FEAT_B_OFF  0x2e0   /* DAT_0053fc90 */
#define ARMY_FEAT_C_FLAG 0x295   /* DAT_0053fc45 — descriptor 0x14 present */
#define ARMY_FEAT_C_OFF  0x298   /* DAT_0053fc48 */

/* Port of FUN_00468e61 (457 B) — mark army `key`'s special feature cells.
 * Row-major scan for cells owned by `key` (+7 == key) and inside the capital
 * region (+2 bits 2..4 == 0xc, the flag the capital footprint sets); the
 * cell's +3 descriptor selects which per-army feature slot to record into,
 * sets its +0 type and +2 bit 0x80:
 *   desc 0x1e → slot A (+0x2ad flag, +0x2b0 off), type +0 ← 1
 *   desc 0    → slot B (+0x2dd flag, +0x2e0 off), type +0 ← 4  — ONLY if slot
 *               A's flag is still clear (a 0x1e cell seen earlier this scan
 *               suppresses the type-0 slot)
 *   desc 0x14 → slot C (+0x295 flag, +0x298 off), type +0 ← 0x0a
 * Each slot is overwritten by the LAST matching cell in scan order (retail
 * does not break).  The three retail loop globals (DAT_005533cc cursor +
 * DAT_00569570/78 col/row) are pure scratch — only the byte cursor is read,
 * so a plain linear offset loop suffices.
 *
 * No-op if the cell grid or the army-record block is unbound. */
void engine_territory_mark_capital_features(unsigned key)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
    int off = 0;
    for (int i = 0; i < CELL_GRID_CELLS; i++, off += CELL_STRIDE) {
        if (g_cells[off + 7] != (uint8_t)key)   continue;   /* DAT_00522f97 */
        if ((g_cells[off + 2] & 0x1c) != 0xc)   continue;   /* DAT_00522f92 */

        uint8_t desc = g_cells[off + 3];                    /* DAT_00522f93 */
        if (desc == 0x1e) {
            rowbase[ARMY_FEAT_A_FLAG] = 1;
            *(int32_t *)(rowbase + ARMY_FEAT_A_OFF) = off;
            g_cells[off + 0] = 1;
            g_cells[off + 2] |= 0x80;
        }
        if (desc == 0 && rowbase[ARMY_FEAT_A_FLAG] == 0) {
            rowbase[ARMY_FEAT_B_FLAG] = 1;
            *(int32_t *)(rowbase + ARMY_FEAT_B_OFF) = off;
            g_cells[off + 0] = 4;
            g_cells[off + 2] |= 0x80;
        }
        if (desc == 0x14) {
            rowbase[ARMY_FEAT_C_FLAG] = 1;
            *(int32_t *)(rowbase + ARMY_FEAT_C_OFF) = off;
            g_cells[off + 0] = 0x0a;
            g_cells[off + 2] |= 0x80;
        }
    }
}

/* Port of FUN_004684c6 (474 B) — paint army `key`'s four settlement cells to
 * the village-size graphic that matches its population tier.  The population
 * pass (engine_realm_advance_population, FUN_00449ef3) passes `active`, the
 * number of settlement cells the realm's population currently sustains (0..4);
 * each of the four cell offsets in the army row's +0x80 cell-list (the first
 * four collected cells) is then either grown into a village or reverted to
 * bare terrain:
 *
 *   slot < active  → village ON.  Shrink the +0 type one village stage toward
 *                    the largest sprite (0x13→0x12→0x11→0x10, anything else
 *                    →0x10), set the +3 descriptor (0x3c for the two larger
 *                    stages, 0x3b for the smallest), set +2 bits: clear 2..4,
 *                    set 0xc (capital-region) + 0x80 (drawn), and set +1 bit
 *                    0x10 (settled).
 *   slot >= active → village OFF.  Restore +3 from the saved descriptor +6,
 *                    clear +2 bits 2..4, clear +1 bit 0x10, propagate the
 *                    saved +1 bit 1 into +2 bit 3 (0x8), clear +2 bit 0x80,
 *                    and clear the +0 type.
 *
 * No-op if the cell grid or the army-record block is unbound. */
void engine_territory_update_village_tiles(int key, int active)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    const int32_t *list =
        (const int32_t *)(g_army_records + key * ARMY_ROW_STRIDE + ARMY_CELL_LIST);

    for (int slot = 0; slot < 4; slot++) {
        int off = list[slot];
        uint8_t saved_flags = g_cells[off + 1];         /* bVar1 — +1 (DAT_00522f91) */

        if (slot < active) {
            uint8_t type = g_cells[off + 0];            /* +0 (DAT_00522f90) */
            if (type == 0x13)      { g_cells[off + 0] = 0x12; g_cells[off + 3] = 0x3c; }
            else if (type == 0x12) { g_cells[off + 0] = 0x11; g_cells[off + 3] = 0x3c; }
            else if (type == 0x11) { g_cells[off + 0] = 0x10; g_cells[off + 3] = 0x3b; }
            else                   { g_cells[off + 0] = 0x10; g_cells[off + 3] = 0x3b; }
            g_cells[off + 2] &= 0xe3;                   /* clear bits 2..4 */
            g_cells[off + 2] |= 0x0c;                   /* capital-region   */
            g_cells[off + 2] |= 0x80;                   /* drawn            */
            g_cells[off + 1] |= 0x10;                   /* settled          */
        } else {
            g_cells[off + 3] = g_cells[off + 6];        /* restore saved descriptor */
            g_cells[off + 2] &= 0xe3;                   /* clear bits 2..4 */
            g_cells[off + 1] &= 0xef;                   /* clear settled    */
            if (saved_flags & 2)
                g_cells[off + 2] |= 0x08;
            g_cells[off + 2] &= 0x7f;                   /* clear drawn      */
            g_cells[off + 0] = 0;                       /* clear type       */
        }
    }
}

/* Port of FUN_0046c147 (407 B) — classify one orthogonal neighbour of the
 * cell at byte-offset `off` (whose grid position is (col, row)) for the
 * coastal-port placer.  `dir` is the retail direction code 0/2/4/6 = N/E/S/W.
 * Reads the neighbour's +1 flag byte and its +2 bits 2..4, then:
 *   result = (neighbour +1) & 0xfd                       // drop bit 1
 *   if result == 8    and (nbr +2 & 0x1c) != 4 → result = 0x10
 *   if result == 0x10 and (nbr +2 & 0x1c) != 8 → result = 0
 * An off-grid neighbour (north of row 0, east of col 0x3f, south of row 0x3f,
 * west of col 0) reads as all-zero → result 0.  The caller keeps a neighbour
 * iff (result & 0x20) survives, i.e. it carries the bit-0x20 "coast" flag.
 *
 * Retail reads the neighbour's +2 byte even at the grid edge (where +1 is
 * forced to 0); since that read is dead whenever +1 == 0, the port reads both
 * bytes only when in-bounds — identical result, no out-of-bounds access.
 * No-op (returns 0) if the cell grid is unbound. */
uint8_t engine_territory_classify_border_neighbour(int dir, int col, int row, int off)
{
    if (g_cells == NULL) return 0;

    uint8_t flags = 0;   /* bVar1 — neighbour's +1 byte (0 past the edge)   */
    uint8_t bits  = 0;   /* local_c — neighbour's +2 & 0x1c (dead if flags 0)*/

    if (dir == 0) {                 /* North */
        if (row != 0)    { int n = off - CELL_ROW_BYTES; flags = g_cells[n + 1]; bits = g_cells[n + 2] & 0x1c; }
    } else if (dir == 2) {          /* East  */
        if (col != 0x3f) { int n = off + CELL_STRIDE;     flags = g_cells[n + 1]; bits = g_cells[n + 2] & 0x1c; }
    } else if (dir == 4) {          /* South */
        if (row != 0x3f) { int n = off + CELL_ROW_BYTES;  flags = g_cells[n + 1]; bits = g_cells[n + 2] & 0x1c; }
    } else if (dir == 6) {          /* West  */
        if (col != 0)    { int n = off - CELL_STRIDE;     flags = g_cells[n + 1]; bits = g_cells[n + 2] & 0x1c; }
    }

    uint8_t r = 0;
    if (flags != 0) {
        r = flags & 0xfd;
        if (r == 8    && bits != 4) r = 0x10;
        if (r == 0x10 && bits != 8) r = 0;
    }
    return r;
}

/* max(|x1-x2|, |y1-y2|) — retail's Chebyshev distance FUN_00404f4c
 * (engine_path_chebyshev_distance), inlined here without its per-axis dx/dy
 * global side effects (DAT_00591560 / DAT_0058fea0), which no consumer in this
 * subtree reads — the same write-only-scratch drop the rest of the module makes. */
static int cheb_dist(int x1, int y1, int x2, int y2)
{
    int dx = x1 > x2 ? x1 - x2 : x2 - x1;
    int dy = y1 > y2 ? y1 - y2 : y2 - y1;
    return dx > dy ? dx : dy;
}

/* Per-army coastal-port slot the 0x46902a pass fills. */
#define ARMY_PORT_FLAG 0x2c5   /* DAT_0053fc75 */
#define ARMY_PORT_OFF  0x2c8   /* DAT_0053fc78 */

/* Port of FUN_0046902a (499 B) — place army `key`'s coastal port.  Scan all
 * empty cells owned by `key` (+7 == key, +1 == 0); a cell qualifies if any of
 * its four orthogonal neighbours (a) is itself owned by `key` AND (b) passes
 * engine_territory_classify_border_neighbour with the 0x20 ("coast") bit set.
 * Among qualifying cells, pick the one with the smallest Chebyshev distance to
 * the cursor origin (g_cursor_col/row, left by the capital finder) and < 64.
 *
 * The winning cell becomes the port: record it in the army row (+0x2c5 flag,
 * +0x2c8 offset), set its +1 bit 0x80, +0 type ← 7, +2 = (… &0xe3 |0xc |0x80),
 * +3 descriptor ← 0x0a.  If nothing qualifies (best offset stays 0 — also the
 * cell-(0,0) blind spot) or the chosen cell is no longer empty, nothing is
 * placed.  No-op if the cell grid or army-record block is unbound. */
void engine_territory_place_capital_port(unsigned key)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    int best_dist = 0x40;   /* local_18 — must beat 64 */
    int best_off  = 0;      /* local_14 */
    int off = 0;
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++, off += CELL_STRIDE) {
            if (g_cells[off + 7] != (uint8_t)key) continue;   /* DAT_00522f97 */
            if (g_cells[off + 1] != 0)            continue;   /* DAT_00522f91 empty */

            int32_t nb[4];   /* N, E, S, W neighbour keys */
            engine_territory_load_neighbour_keys(col, row, off, nb);   /* FUN_0046c080 */
            for (int i = 0; i < 4; i++) {
                if ((unsigned)nb[i] != key) continue;
                if ((engine_territory_classify_border_neighbour(i * 2, col, row, off) & 0x20) == 0)
                    continue;
                int d = cheb_dist(g_cursor_col, g_cursor_row, col, row);   /* FUN_00404f4c */
                if (d < best_dist) { best_off = off; best_dist = d; }
            }
        }
    }

    if (best_off != 0 && g_cells[best_off + 1] == 0) {
        uint8_t *rowbase = g_army_records + key * ARMY_ROW_STRIDE;
        rowbase[ARMY_PORT_FLAG] = 1;
        *(int32_t *)(rowbase + ARMY_PORT_OFF) = best_off;
        g_cells[best_off + 1] |= 0x80;          /* DAT_00522f91 */
        g_cells[best_off + 0] = 7;              /* DAT_00522f90 */
        g_cells[best_off + 2] &= 0xe3;          /* DAT_00522f92 clear bits 2..4 */
        g_cells[best_off + 2] |= 0xc;
        g_cells[best_off + 2] |= 0x80;
        g_cells[best_off + 3] = 0x0a;           /* DAT_00522f93 */
    }
}

/* Port of FUN_00468121 (331 B) — claim army `key`'s secondary cell cluster.
 * Row-major scan for cells owned by `key` (+7 == key) flagged 0x80 in +1 and
 * still empty (+0 == 0); each of the first four matches gets its +6 byte set
 * to its +3 descriptor and its +0 type set to 0x14.  The 1st match also gets
 * +2 bit 0x80 and seats the cursor col/row (DAT_0057c8d0 / DAT_0056846c) at
 * its own cell — note this is the FIRST match, unlike the capital finder
 * (0x467fd1) which seats the cursor on its 4th.  Returns the 1st match's
 * byte-offset once a 4th match is reached, else 0 (with any 1..3 matches
 * still mutated and the cursor still seated on the 1st).  No-op (returns 0)
 * if the cell grid is unbound. */
int engine_territory_place_secondary(unsigned key)
{
    if (g_cells == NULL) return 0;

    int found = 0;      /* local_1c */
    int first_off = 0;  /* local_c  */
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++) {
            int off = (row * 0x40 + col) * CELL_STRIDE;
            if (g_cells[off + 7] != (uint8_t)key) continue;   /* DAT_00522f97 */
            if ((g_cells[off + 1] & 0x80) == 0)   continue;   /* DAT_00522f91 */
            if (g_cells[off + 0] != 0)            continue;   /* DAT_00522f90 */

            if (found == 0) {
                first_off = off;
                g_cells[off + 2] |= 0x80;       /* DAT_00522f92 */
                g_cursor_col = col;             /* DAT_0057c8d0 */
                g_cursor_row = row;             /* DAT_0056846c */
            }
            g_cells[off + 6] = g_cells[off + 3];    /* +6 ← +3 descriptor */
            g_cells[off + 0] = 0x14;                /* +0 ← 0x14 (type)   */
            if (found == 3) return first_off;
            found++;
        }
    }
    return 0;
}

/* Per-army result fields the 0x468d4f orchestrator writes (retail DAT_0053fa1c
 * … fa28 = g_per_army_768 + these offsets). */
#define ARMY_CAP_COL  0x6c   /* DAT_0053fa1c — capital cursor col (byte)     */
#define ARMY_CAP_ROW  0x6d   /* DAT_0053fa1d — capital cursor row (byte)     */
#define ARMY_CAP_OFF  0x70   /* DAT_0053fa20 — capital cell byte-offset (i32)*/
#define ARMY_SEC_COL  0x74   /* DAT_0053fa24 — secondary cursor col (byte)   */
#define ARMY_SEC_ROW  0x75   /* DAT_0053fa25 — secondary cursor row (byte)   */
#define ARMY_SEC_OFF  0x78   /* DAT_0053fa28 — secondary cell byte-offset(i32)*/

/* Port of FUN_00468d4f (274 B) — the per-army capital-region setup
 * orchestrator, and the last unported callee of the overworld-grid setup
 * sequence FUN_004676e0.  For each army 1..s_slot_0056d5dc (retail
 * DAT_0056d5dc, save entry 77 — the player/army count):
 *
 *   cap = place_capital(key);  row[+0x70] = cap;       // FUN_00467fd1
 *   if (cap != 0) {
 *       row[+0x6c] = cursor_col;  row[+0x6d] = cursor_row;   // capital cursor
 *       collect_keyed_cells(key);                            // FUN_00468c41
 *       mark_capital_features(key);                          // FUN_00468e61
 *       place_capital_port(key);                             // FUN_0046902a
 *       row[+0x78] = place_secondary(key);                   // FUN_00468121
 *       row[+0x74] = cursor_col;  row[+0x75] = cursor_row;   // secondary cursor
 *   }
 *
 * Retail re-reads the capital cursor back out of +0x6c/+0x6d into the cursor
 * globals right after collect_keyed_cells; since collect leaves the cursor
 * untouched (it never writes DAT_0057c8d0/0056846c), that restore is an
 * identity, so the port keeps the stash write and drops the no-op read-back.
 * The capital cursor (the capital finder's 4th cell) thus survives as the
 * port placer's distance origin, exactly as in retail.
 *
 * No-op if the cell grid or the army-record block is unbound. */
void engine_territory_setup_capitals(void)
{
    if (g_cells == NULL || g_army_records == NULL) return;

    int count = (int)s_slot_0056d5dc;
    for (int key = 1; key <= count; key++) {
        uint8_t *rowbase = g_army_records + (unsigned)key * ARMY_ROW_STRIDE;

        int cap_off = engine_territory_place_capital((unsigned)key);   /* FUN_00467fd1 */
        *(int32_t *)(rowbase + ARMY_CAP_OFF) = cap_off;
        if (cap_off == 0) continue;

        rowbase[ARMY_CAP_COL] = (uint8_t)g_cursor_col;   /* DAT_0057c8d0 */
        rowbase[ARMY_CAP_ROW] = (uint8_t)g_cursor_row;   /* DAT_0056846c */

        engine_territory_collect_keyed_cells((unsigned)key);     /* FUN_00468c41 */
        engine_territory_mark_capital_features((unsigned)key);   /* FUN_00468e61 */
        engine_territory_place_capital_port((unsigned)key);      /* FUN_0046902a */

        int sec_off = engine_territory_place_secondary((unsigned)key);   /* FUN_00468121 */
        *(int32_t *)(rowbase + ARMY_SEC_OFF) = sec_off;
        rowbase[ARMY_SEC_COL] = (uint8_t)g_cursor_col;
        rowbase[ARMY_SEC_ROW] = (uint8_t)g_cursor_row;
    }
}

/* DAT_004eabec — scratch buffer the l2_maps.dat record is loaded into. */
static const uint8_t *g_strat_map_record;
/* DAT_00567970/569518 — category-bucket table seeded during map load. */
static engine_category_buckets_state_t *g_strat_buckets;

void engine_territory_bind_strat_map_record(const uint8_t *record)
{
    g_strat_map_record = record;
}

void engine_territory_bind_category_buckets(engine_category_buckets_state_t *buckets)
{
    g_strat_buckets = buckets;
}

/* Stride between the six per-cell byte planes inside an l2_maps.dat record:
 * each plane is 64×64 = 0x1000 cell-linear bytes (row*0x40 + col). */
#define STRAT_MAP_PLANE_STRIDE 0x1000

/* Port of FUN_00467770 (525 B) — strategic-map record → cell-grid unpacker.
 *
 * Retail body:
 *
 *   DAT_00522f7c = 0;                                    // render rotation
 *   io_read_file_at("l2_maps.dat", DAT_004eabec,
 *                   0x80C1, map_index * 0x80C1);          // <- live seam
 *   DAT_0053e974 = 0;                                    // running offset
 *   DAT_0056d5dc = 0;  DAT_0053e8a4 = 0;                 // player/roster counts
 *   FUN_004290d0();  FUN_0049bc1d();                     // clear buckets+roster
 *   for (row=0; row<0x40; row++)
 *     for (col=0; col<0x40; col++, DAT_0053e974++) { ...unpack one cell... }
 *   return 1;
 *
 * The `map_index` argument selects which 0x80C1-byte record is read from
 * l2_maps.dat (the orchestrator FUN_004676e0 passes `s_slot_0053f034`).
 * That file read is the deferred live Win32 seam — like every asset load
 * (see io.h), the engine modules never touch the filesystem: the live
 * build reads the selected record into the bound buffer; the host binds a
 * synthetic record.  The unpack proper is index-independent (it reads the
 * record from offset 0), so this entry point takes no argument.
 *
 * `DAT_0053e974` is a file-scope scratch running offset reused as a loop
 * counter across several loaders (always reset to 0 on entry, never read
 * across calls); modelled here as a local, not homed as a shared global. */
void engine_territory_load_strat_map(void)
{
    engine_map_render_set_rotation_offset(0);   /* DAT_00522f7c = 0 */

    s_slot_0056d5dc = 0;          /* DAT_0056d5dc — player/army count (recomputed) */
    g_player_roster_count = 0;    /* DAT_0053e8a4 — roster participant count       */

    if (g_strat_buckets != NULL)
        engine_category_buckets_clear(g_strat_buckets);   /* FUN_004290d0 */
    player_roster_clear();                                /* FUN_0049bc1d */

    if (g_cells == NULL || g_strat_map_record == NULL) return;

    const uint8_t *rec = g_strat_map_record;
    int lin = 0;   /* DAT_0053e974 — cell-linear offset, row*0x40 + col */
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++, lin++) {
            uint8_t p0 = rec[lin];                              /* +0x0000 */
            uint8_t p1 = rec[lin + 1 * STRAT_MAP_PLANE_STRIDE]; /* +0x1000 */
            uint8_t p2 = rec[lin + 2 * STRAT_MAP_PLANE_STRIDE]; /* +0x2000 */
            uint8_t p3 = rec[lin + 3 * STRAT_MAP_PLANE_STRIDE]; /* +0x3000 */
            uint8_t p4 = rec[lin + 4 * STRAT_MAP_PLANE_STRIDE]; /* +0x4000 */
            uint8_t p5 = rec[lin + 5 * STRAT_MAP_PLANE_STRIDE]; /* +0x5000 */

            /* Player/army count = the highest plane-5 owner id present that
             * is still in the 0..0x10 player band (cells with p5 > 0x10 are
             * non-player markers and never raise the count). */
            if (p5 <= 0x10 && (int)s_slot_0056d5dc < (int)p5)
                s_slot_0056d5dc = p5;

            uint8_t *cell = g_cells + (size_t)lin * CELL_STRIDE;
            cell[1] = p0;   /* DAT_00522f91 */
            cell[2] = p1;   /* DAT_00522f92 */
            cell[3] = p2;   /* DAT_00522f93 */
            cell[4] = p3;   /* DAT_00522f94 */
            cell[7] = p5;   /* DAT_00522f97 */

            /* Plane 4 non-zero → the cell carries a categorised marker:
             * plane-0 bit 0x40 routes it to a category bucket, bit 0x80 to
             * the player roster.  Retail order: 0x40 wins over 0x80. */
            if (p4 != 0) {
                if (p0 & 0x40) {
                    if (g_strat_buckets != NULL)
                        engine_category_buckets_append(g_strat_buckets,
                                                       p5, p4);   /* FUN_00429153 */
                } else if (p0 & 0x80) {
                    player_roster_add(p5, p4);                    /* FUN_0049bbe8 */
                }
            }
        }
    }
}

/* Port of FUN_004676e0 (144 B) — the overworld-grid setup sequence (a direct
 * FUN_00497ced new-game callee).  Retail body, verbatim order (r2 @0x4676e0):
 *
 *   FUN_0046c5b8(1); (2); (4); (0); (5); (3);   // clear cell bytes 0..5
 *   FUN_00467770(DAT_0053f034);                  // load strategic-map record
 *   FUN_0046df51();                              // clear render flags
 *   FUN_0046dbf5();                              // classify feature-border tiles
 *   FUN_0046dd34();                              // randomise special tiles
 *   0x46a037(DAT_0053f034);                  // per-country data bind  ← deferred
 *   FUN_00467ca6();                              // build per-army adjacency
 *   FUN_00468d4f();                              // set up capital regions
 *   FUN_00467a36();                              // seed per-army features
 *   FUN_0046da4b(1);                             // rebuild per-key cell roster
 *   FUN_004291b3();                              // refresh category-bucket digest
 *
 * The six clear_cell_field passes wipe cell bytes 0..5 (note +6/+7 survive),
 * then load_strat_map writes +1..+4 and +7 — so the cleared-then-loaded order
 * is what leaves +0/+5/+6 zero around the loaded planes.  The two RNG grid
 * passes (0x46dbf5/0x46dd34) share retail's global single-LFSR `DAT_00591564`;
 * the ported pair thread it through `rng` instead, so this orchestrator owns
 * the seeded state and passes it to both (NULL → both no-op).
 *
 * 0x46a037 (the per-country data bind that loads the in-game country map grid
 * 0x52aff0 + the 128×128 preview 0x569590 from map01.pl8) is **deferred**: it
 * is pure file IO with no ported consumer yet — the in-game strategic-map arm
 * that reads 0x52aff0 is unported, and the preview is filled by the separate
 * page-7 reload path.  The file read is the deferred Win32 seam (io.h); the
 * load lands when the strategic-map arm forces it.  See game-start.md. */
void engine_territory_setup_overworld(engine_rng_single_in_t *rng)
{
    engine_territory_clear_cell_field(1);
    engine_territory_clear_cell_field(2);
    engine_territory_clear_cell_field(4);
    engine_territory_clear_cell_field(0);
    engine_territory_clear_cell_field(5);
    engine_territory_clear_cell_field(3);

    engine_territory_load_strat_map();              /* FUN_00467770(s_slot_0053f034) */
    engine_territory_clear_cell_render_flags();     /* FUN_0046df51 */
    engine_territory_classify_feature_tiles(rng);   /* FUN_0046dbf5 */
    engine_territory_randomise_special_tiles(rng);  /* FUN_0046dd34 */

    /* 0x46a037(s_slot_0053f034) — deferred (see the header comment). */

    engine_territory_build_army_adjacency();        /* FUN_00467ca6 */
    engine_territory_setup_capitals();              /* FUN_00468d4f */
    engine_territory_seed_army_features();          /* FUN_00467a36 */
    engine_territory_rebuild_cell_roster(1);        /* FUN_0046da4b(1) */

    if (g_strat_buckets != NULL)
        engine_category_buckets_refresh_digest(g_strat_buckets);  /* FUN_004291b3 */
}
