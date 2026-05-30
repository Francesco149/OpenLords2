/*
 * OpenLords2 — per-territory record helpers + overworld cell-grid resets.
 *
 * Operates on the per-territory record array (retail `DAT_0052f0b0`,
 * 151 records × 0x1a4 bytes, slot 0 reserved) — the same memory the
 * strategic-map overlay/primary passes bind — and on the 64×64×8
 * overworld cell-descriptor grid (retail `DAT_00522f90`), which it both
 * claims (the entity placer) and bulk-clears at new-game (the two cell
 * resets below).  Records are bound via engine_territory_bind_records,
 * the cell grid via engine_territory_bind_cells.  Pure C; the only
 * outward coupling is the optional dirty-rect state the cell reset marks.
 *
 *   FUN_004651b9 @ 0x4651b9 (91 B)  → engine_territory_clear_size_state
 *   FUN_0046c5b8 @ 0x46c5b8 (258 B) → engine_territory_clear_cell_field
 *   FUN_0046df51 @ 0x46df51 (132 B) → engine_territory_clear_cell_render_flags
 */

#include <stddef.h>
#include <string.h>

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

/* Optional render-invalidation sink, bound by the new-game orchestrator
 * so engine_territory_clear_cell_render_flags can request a full repaint
 * (retail's FUN_0046df51 → FUN_0045240a).  NULL in headless/unit
 * contexts — the mark is then simply skipped.  Same pattern the render
 * modules (engine_overlay_render / engine_battlefield_render) use. */
static engine_dirty_rect_state_t *g_dirty_state;

/* Overworld cell grid geometry (retail DAT_00522f90). */
#define CELL_GRID_CELLS   0x1000   /* 64×64 = 4096 cells */
#define CELL_STRIDE       8        /* bytes per cell descriptor */

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

/* Port of FUN_0046c5b8.  Zero byte `field` of every overworld cell
 * descriptor (all 4096 cells of the 64×64×8 grid).  Retail unrolls the
 * sweep 8 cells at a time and drives it with three scratch globals
 * (DAT_0053e8b8 byte cursor + the engine_path nearest-result pair
 * DAT_00569570/78 reused as the 8× / 64 loop counters); all three are
 * write-only scratch reset at entry and never read afterwards, so we use
 * a plain cell loop and leave engine_path's result globals alone — same
 * choice the map_query box scans make (see engine_map_query.c).
 *
 * The two callers (FUN_004676e0 new-game init, FUN_0046d3da reset) sweep
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
 * per-cell render-flag byte; bit 0x20 is set elsewhere (FUN_0046e067 /
 * FUN_00469370) and consumed by the strategic draw helpers
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
