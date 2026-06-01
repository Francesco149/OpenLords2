/*
 * OpenLords2 — per-territory record helpers + overworld cell-grid resets.
 * See engine_territory.c.
 */

#ifndef OPENLORDS2_ENGINE_TERRITORY_H
#define OPENLORDS2_ENGINE_TERRITORY_H

#include <stdint.h>

#include "engine.h"            /* engine_rng_single_in_t */
#include "engine_dirty_rect.h"
#include "engine_category_buckets.h"  /* engine_category_buckets_state_t */

/* Bind the per-territory record array (retail `DAT_0052f0b0`, 151
 * records × 0x1a4 bytes, slot 0 reserved). */
void engine_territory_bind_records(uint8_t *records);

/* Bind the 64×64×8 map-cell descriptor grid (retail `DAT_00522f90`),
 * mutable here because engine_territory_place_entity marks cells as it
 * claims them (cell +5 ← slot, +2 |= 1), and the cell resets below
 * bulk-clear it.  Same memory engine_map_query reads read-only. */
void engine_territory_bind_cells(uint8_t *cells);

/* Bind the render-invalidation state engine_territory_clear_cell_render_flags
 * marks full-screen.  Optional — leave unbound (NULL) and the repaint
 * request is skipped (host/headless). */
void engine_territory_set_dirty_state(engine_dirty_rect_state_t *state);

/* Bind the per-key cell roster (retail `DAT_0053ea00` = save entry 12,
 * g_misc_1360 — 17 rows × 20 i32) that engine_territory_rebuild_cell_roster
 * fills.  Required for the rebuild (NULL → no-op). */
void engine_territory_bind_cell_roster(int32_t *roster);

/* Bind the parallel per-key AI score table (retail `DAT_0053fa40`, 384
 * i16 per row) the rebuild zeroes on the new-game path.  Optional — NULL
 * skips that secondary clear. */
void engine_territory_bind_cell_score_table(int16_t *scores);

/* Bind the per-army record block (retail `DAT_0053f9b0` = save entry 4,
 * g_per_army_768 — 17 rows × 0x300 B, slot 0 reserved) that
 * engine_territory_build_army_adjacency writes the adjacency header of.
 * Required for the adjacency build (NULL → no-op).  The AI score table
 * (engine_territory_bind_cell_score_table) is a sub-view of this same
 * block at +0x90. */
void engine_territory_bind_army_records(uint8_t *records);

/* Bind the terrain-index render tile grid (retail `DAT_0055cea0`, 129×65
 * i32, row stride 0x104 B = 65 i32 contiguous) that the new-game tile
 * randomiser re-rolls.  Optional — NULL → randomiser is a no-op.  The same
 * memory the map-render modules bind read-only. */
void engine_territory_bind_terrain_tiles(int32_t *tiles);

/* Port of FUN_004651b9.  Zero the +0x14c and +0x153 bytes of every
 * territory record 1..150.  Used during new-game/scenario init. */
void engine_territory_clear_size_state(void);

/* Port of FUN_0046e1b0.  Allocate the first free record (slots 1..149)
 * and place a map entity at cell (col, row); returns 1 on success (cell
 * free + a slot available), else 0.  `kind` → record +0x08, `type` →
 * record +0x00.  See the .c for the full field map. */
int engine_territory_place_entity(int kind, int col, int row, int type);

/* The record slot the last successful place_entity allocated (retail
 * DAT_0052afe0).  Callers read it to stamp extra fields into the fresh
 * record; undefined before the first placement. */
int engine_territory_current_slot(void);

/* Port of FUN_0046c5b8.  Zero byte `field` (0..5 at the call sites) of
 * every overworld cell descriptor (all 4096 cells). */
void engine_territory_clear_cell_field(uint8_t field);

/* Port of FUN_0046df51.  Clear bit 0x20 of byte +2 of every overworld
 * cell, then mark the full screen dirty (if a dirty state is bound). */
void engine_territory_clear_cell_render_flags(void);

/* Port of FUN_0046e067.  Set bit 0x20 of byte +2 (the alternate-draw
 * flag clear_cell_render_flags wipes) on every cell of the
 * (2*radius+1)-square box centred at (col, row), CLAMPED to the 0..63
 * grid (the box shrinks at the edges rather than being rejected).  No
 * dirty mark — callers repaint once after a batch of stamps. */
void engine_territory_mark_draw_box(int col, int row, int radius);

/* Port of FUN_0046dfd5.  Highlight group-key `key` on the strategic map:
 * scan the whole 64×64 grid and, for every cell whose group-key byte (+7)
 * equals `key`, stamp a radius-1 alternate-draw box (its 3×3
 * neighbourhood) via engine_territory_mark_draw_box; then mark the full
 * screen dirty (if a dirty state is bound).  Pairs with
 * engine_territory_clear_cell_render_flags, which wipes the same flag. */
void engine_territory_highlight_key_cells(unsigned key);

/* Bind the optional cursor-cache mode flag (retail DAT_0057cac4) the
 * recenter navigator latches to 1 on a hit.  NULL → the latch is
 * skipped.  Same optional-sink pattern as the dirty state. */
void engine_territory_bind_cursor_cache_flag(int *flag);

/* Port of FUN_0043278b.  Scan the bound terrain-index tile grid for the
 * first cell holding `tile_value`; on a hit, recenter the strategic-map
 * scroll on it — set the scroll save globals (entries 16/17) to
 * (col-4, evenrow-12) when the view mode is normal (entry 15 == 0),
 * clamp them via engine_camera_clamp_scroll, and latch the below-status
 * redraw flag (dirty state) plus the cursor-cache flag — then return 2.
 * Returns 0 if the value is absent.  Requires the terrain tiles bound. */
int engine_territory_recenter_on_tile(int tile_value);

/* Port of FUN_00432746.  Read army `key`'s capital cell byte-offset
 * (record +0x70) and, when non-zero, recenter on it via
 * engine_territory_recenter_on_tile and latch the selected-army global
 * (entry 71).  Requires the army records bound (NULL → no-op). */
void engine_territory_recenter_on_capital(int key);

/* Port of FUN_0046da4b.  Rebuild the per-key cell roster (bound via
 * engine_territory_bind_cell_roster) from the overworld grid: index every
 * active feature-cell (+7 key 1..16, +1 bit 0x20 set) into its key row,
 * capped at 20 per key; cells beyond the cap are evicted.  Pass 1 to also
 * zero the AI score table (new-game), 0 to leave it (mid-game recompute). */
void engine_territory_rebuild_cell_roster(int reset_score_table);

/* Port of FUN_0046d9da.  Clear the AI-score entry for a single cell: scan
 * the cell's key row (key = its +7 byte) of the roster (bound via
 * engine_territory_bind_cell_roster) for the column holding cell_off, and
 * zero the matching i16 in the AI score table (engine_territory_bind_cell_
 * score_table).  No-op if the cell is not in its roster row, or if either
 * table is unbound.  cell_off is the cell's byte-offset into the grid. */
void engine_territory_clear_cell_score(int cell_off);

/* Port of FUN_0046d7f4.  Set an overworld cell's terrain type (byte +0) and
 * re-derive its +3 render-descriptor byte (type base + variant*4, keeping
 * the low 2 bits) and +2 flag byte (bit0 set; bits 2..4 = this type's
 * contribution; bit7 set only for the type band 15..22).  When type is 0
 * the cell's AI score is also cleared (engine_territory_clear_cell_score).
 * Requires the cell grid bound (engine_territory_bind_cells); variant is the
 * render tier (retail call sites pass 0). */
void engine_territory_set_cell_type(int cell_off, int type, char variant);

/* Port of FUN_0046dd34.  New-game terrain-tile randomiser: sweep the bound
 * terrain-index grid (engine_territory_bind_terrain_tiles) and re-roll every
 * cell holding the special sentinel 0xfff0006 to 0xfff0006 + (rng & 0xf),
 * advancing the single-LFSR RNG (threaded through *rng) once per match.
 * No-op if the grid or rng is unbound/NULL. */
void engine_territory_randomise_special_tiles(engine_rng_single_in_t *rng);

/* Port of FUN_0046de94.  Per-neighbour terrain eligibility predicate over
 * the bound terrain-tile grid (engine_territory_bind_terrain_tiles) and cell
 * grid (engine_territory_bind_cells): returns 1 if tile (col, row) is a real
 * cell whose flags byte lacks bit 0x4, or the special sentinel 0xfff0006;
 * 0 if off-grid (col∉[0,65) || row∉[0,129)) or any other value. */
int engine_territory_tile_eligible(int col, int row);

/* Port of FUN_0046de05.  Fill mask[8] with the eligibility (0/1) of the 8
 * hex neighbours of tile (col, row) — the neighbour offsets are chosen by
 * row parity (the strategic map is a doubled-row hex grid).  This is the
 * adjacency mask the (still-unported) feature classifier 0x46c2de consumes;
 * retail passes it through the shared scratch global DAT_00543fe0. */
void engine_territory_compute_neighbour_mask(int col, int row, int32_t mask[8]);

/* Feature/tile the classifier emits for a neighbour mask (retail globals
 * DAT_0057c8a4 / DAT_0053e9c0 / DAT_00568200). */
typedef struct {
    int type;     /* DAT_0057c8a4 — matched entry +8: the tile/feature variant code */
    int aux;      /* DAT_0053e9c0 — matched entry +9 (0 throughout the retail table) */
    int variant;  /* DAT_00568200 — the entry's post-rotation variant counter (+0xb) */
} engine_territory_feature_pick_t;

/* Port of FUN_0046c2de.  Match the 8-neighbour eligibility mask (from
 * engine_territory_compute_neighbour_mask) against retail's 48-entry hex
 * auto-tile pattern table, write the first matching entry's feature into
 * *out, and advance that entry's rotating variant counter.  Returns the
 * matched entry index + 1, or 0 if nothing matched (*out left untouched —
 * unreachable with the shipped table, which covers all 256 masks). */
int engine_territory_pick_feature(const int32_t mask[8],
                                  engine_territory_feature_pick_t *out);

/* Reset every pattern-table entry's rotating variant counter to 0.  Retail
 * never resets these (the table is process-lifetime .data); the harness and
 * tests use it to seat a deterministic starting state. */
void engine_territory_reset_feature_counters(void);

/* Port of FUN_0046dbf5.  New-game terrain auto-tiling pass (sibling of
 * engine_territory_randomise_special_tiles): sweep the bound terrain grid and
 * rewrite every feature-border cell (a real cell with flags bit 0x4 set, or a
 * non-sentinel special tile) with the transition tile its 8-neighbour
 * adjacency selects from the pattern table — real cells into the cell's +3
 * byte, special cells into the tile itself; isolated tiles (type 0x16) take a
 * random variant from the single-LFSR RNG (threaded through *rng).  Mutates
 * the grid in place in row-major order.  No-op if the grid or rng is
 * unbound/NULL. */
void engine_territory_classify_feature_tiles(engine_rng_single_in_t *rng);

/* Port of FUN_0046c080.  Load the group-key (cell byte +7) of the four
 * orthogonal neighbours of the cell at byte-offset cursor_off (= (row*64 +
 * col)*8) of the bound cell grid into out[] in N, E, S, W order; an
 * off-grid edge neighbour reads 0.  Shared with the castle-border subtree
 * (0x468d4f → 0x46902a).  Zeroes out[] if the cell grid is unbound. */
void engine_territory_load_neighbour_keys(int col, int row, int cursor_off,
                                          int32_t out[4]);

/* Port of FUN_00467ca6.  Build the per-army territory-adjacency lists in
 * the bound army-record block (engine_territory_bind_army_records): for
 * every cell whose owning-army key (+7) is in 1..17, record each distinct
 * neighbouring army key into that army's row — a 16-entry list at +0x5c
 * with a count at +0x5a.  No-op if the cell grid or army block is unbound. */
void engine_territory_build_army_adjacency(void);

/* Port of FUN_00469b8d.  New-game per-army terrain census: tally the army's
 * active feature cells (listed in the bound cell roster) into five
 * terrain-TYPE buckets in the army record — +0x1ff (TYPE 1), +0x200 (TYPE
 * 0x0f..0x16), +0x201 (TYPE 0x02..0x0e), +0x203 (TYPE 0/0x17/0x18) and
 * +0x204 (TYPE >= 0x19).  The first per-army callee of the army/country
 * setup pass 0x49bd99.  No-op if the army-record block is unbound; the
 * counters are merely cleared if the roster or cell grid is unbound. */
void engine_territory_census_army_terrain(int key);

/* Port of FUN_00469b51.  Per-realm terrain-census dispatcher: run
 * engine_territory_census_army_terrain for every realm 1..s_slot_0056d5dc.  A
 * per-turn pass of the turn-advance engine (0x448440); also the new-game
 * whole-map census wrapper. */
void engine_territory_census_all_realms(void);

/* Port of FUN_0046826c.  Stamp the realm's secondary-settlement 2×2 footprint
 * at its secondary cell (record +0x78), via engine_territory_stamp_block.  The
 * descriptor base is one of three tiers ('P'/'('/'<') picked by the realm's
 * settlement fields (+0x1c3 / +0x1c4) plus a `level*4` stride, and the +0 TYPE
 * is 0x15 + min(level, 4).  A per-realm callee of the army/country setup pass
 * 0x49bd99.  No-op if the army-record block (or cell grid) is unbound. */
void engine_territory_stamp_secondary_keep(int key, int level);

/* Port of FUN_0044f6e7.  New-game per-realm population/job-slot allocator: split
 * the realm's total population (record +0x24) by the +0x08 percentage into two
 * pools and distribute them across eight capped job-slot counters (+0xc4 .. +0x118,
 * 0xc-strided) toward per-bucket target ratios (+0x130 run), leaving the unspent
 * remainder at +0x124.  Pool 2 fills the three ungated buckets (weighted 2:3:1),
 * pool 1 the five settlement/building buckets (each gated by a "present" flag).  A
 * per-realm callee of the army/country setup pass 0x49bd99.  No-op if the
 * army-record block is unbound. */
void engine_territory_allocate_realm_workforce(int key);

/* Port of FUN_0044f699.  Per-realm workforce-allocation dispatcher: run
 * engine_territory_allocate_realm_workforce for every realm 1..s_slot_0056d5dc.
 * Sequenced twice by the turn-advance engine (0x448440), either side of the
 * territory-income/economy re-plan. */
void engine_territory_allocate_all_workforce(void);

/* Port of FUN_00469d21.  Retype an army's active feature cells (owner +7 == key,
 * +1 bit 0x20 set) whose TYPE byte (+0) lies in [lo, hi] via
 * engine_territory_set_cell_type.  The first match takes (type, variant); when
 * lo == 2 and the realm's +0x1a7 flag is set, every later match collapses to
 * type 2 / variant 0 instead.  A helper of the 0x49bd99 → 0x44d913 setup path
 * (and the 0x44c8ae caller).  No-op if the cell grid or army-record block is
 * unbound. */
void engine_territory_retype_army_band(int key, int type, char variant,
                                       int lo, int hi);

/* Port of FUN_0044d913.  Grade the realm's feature cells by population density
 * = load(+0x250) / census-HIGH-count(+0x200): the density picks a feature-TYPE
 * tier (0x13..0x16) applied to the realm's band-[0x13,0x16] cells via
 * engine_territory_retype_army_band, and a parallel ladder writes a derived
 * value to +0x25c (0x28 when the realm has no high-terrain cell).  A per-realm
 * callee of the army/country setup pass 0x49bd99.  No-op if the cell grid or
 * army-record block is unbound. */
void engine_territory_grade_feature_cells(int key);

/* Port of FUN_00467a36.  New-game per-army feature-seeding pass: for every
 * active feature-cell (+7 key 1..17, +1 bit 0x20 set), assign a feature
 * terrain type (0x14 / 1 / 0) by the army's running active-cell index and
 * the global density setting (s_slot_0053f23c), finalising via
 * engine_territory_set_cell_type; also tallies active cells per army at row
 * +0x205.  Needs the cell grid and army-record block bound. */
void engine_territory_seed_army_features(void);

/* ──────────────────────────────────────────────────────────────────
 * Per-army capital-region setup (the 0x468d4f subtree)
 * ────────────────────────────────────────────────────────────────── */

/* The "current cell" col/row scratch registers (retail DAT_0057c8d0 /
 * DAT_0056846c) the capital-region passes leave behind.  Exposed for tests;
 * production code in this module reads the statics directly. */
int engine_territory_cursor_col(void);   /* DAT_0057c8d0 */
int engine_territory_cursor_row(void);   /* DAT_0056846c */

/* Port of FUN_0046ac22.  Stamp a `size`×`size` block of grid cells starting
 * at byte-offset `off`: per cell set +2 |= 1, clear +2 bits 2..4, write the
 * +3 descriptor as base + a per-cell gradient offset (retail's N×N fill-order
 * tables; size 1 writes just base), OR in `flag_bits` to +2, and write `type`
 * to +0.  Shared cell-grid mutator; no-op if the cell grid is unbound. */
void engine_territory_stamp_block(int base, int size, int off,
                                  uint8_t flag_bits, uint8_t type);

/* Port of FUN_00467fd1.  Find army `key`'s capital cell (+7 == key, +1 bit
 * 0x40 set, +0 == 0) by row-major scan; on the 1st such cell stamp a 2×2
 * capital footprint and return its byte-offset (after also touching the 3rd
 * and 4th matches and leaving the cursor col/row at the 4th).  Returns 0 if
 * fewer than 4 eligible cells exist or the grid is unbound. */
int engine_territory_place_capital(unsigned key);

/* Port of FUN_00468c41.  Collect army `key`'s claimed cells (+1 bit 0x10,
 * +7 == key) into its record row's i32 cell-list at +0x80, copying each
 * cell's +3 descriptor to +6 and clearing its +0 type.  No-op if the cell
 * grid or army-record block is unbound. */
void engine_territory_collect_keyed_cells(unsigned key);

/* Port of FUN_00468e61.  Mark army `key`'s special feature cells: for each
 * owned cell (+7 == key) inside the capital region (+2 bits 2..4 == 0xc),
 * record it into one of three per-army feature slots by its +3 descriptor
 * (0x1e → slot A, 0 → slot B but only if A is unset, 0x14 → slot C), stamping
 * the cell's +0 type (1/4/0x0a) and +2 bit 0x80.  No-op if the cell grid or
 * army-record block is unbound. */
void engine_territory_mark_capital_features(unsigned key);

/* Port of FUN_0046c147.  Classify the orthogonal neighbour (dir 0/2/4/6 =
 * N/E/S/W) of the cell at (col, row) / byte-offset `off` for the coastal-port
 * placer: returns (neighbour +1 & 0xfd), remapped 8→0x10 / 0x10→0 by the
 * neighbour's +2 bits, or 0 off-grid or if the cell grid is unbound.  The
 * caller keeps neighbours whose result has bit 0x20 set. */
uint8_t engine_territory_classify_border_neighbour(int dir, int col, int row, int off);

/* Port of FUN_0046902a.  Place army `key`'s coastal port: among empty cells
 * owned by `key` (+1 == 0) that border a same-key coast neighbour (per
 * engine_territory_classify_border_neighbour), pick the one closest
 * (Chebyshev) to the capital cursor and stamp it as a port (+0 ← 7, +3 ← 0x0a,
 * +1 bit 0x80, +2 = …&0xe3|0xc|0x80), recording it in the army row at +0x2c5/
 * +0x2c8.  No-op if the cell grid or army-record block is unbound. */
void engine_territory_place_capital_port(unsigned key);

/* Port of FUN_00468121.  Claim army `key`'s secondary cell cluster: the first
 * four cells owned by `key` (+7 == key) with +1 bit 0x80 and +0 == 0 get
 * +6 ← +3 and +0 ← 0x14; the 1st also gets +2 bit 0x80 and seats the cursor
 * col/row on itself.  Returns the 1st match's byte-offset once a 4th match is
 * found, else 0.  No-op if the cell grid is unbound. */
int engine_territory_place_secondary(unsigned key);

/* Port of FUN_00468d4f — the per-army capital-region setup orchestrator (the
 * last callee of the overworld-grid setup sequence FUN_004676e0).  For each
 * army 1..s_slot_0056d5dc it places the capital, and (if one was found)
 * collects the army's cells, marks its feature cells, places its coastal port,
 * and claims its secondary cluster — stashing the capital/secondary cell
 * offsets and cursor col/row into the army record row (+0x6c..+0x78).  Needs
 * the cell grid and army-record block bound. */
void engine_territory_setup_capitals(void);

/* Bind the scratch buffer the strategic-map record is loaded into (retail
 * `DAT_004eabec`) — the source `engine_territory_load_strat_map` unpacks.
 * The buffer holds one l2_maps.dat record (0x80C1 bytes, six 0x1000-byte
 * cell-linear planes first).  The file read that fills it is the deferred
 * live Win32 seam (see the .c); NULL → the unpack loop is skipped. */
void engine_territory_bind_strat_map_record(const uint8_t *record);

/* Bind the per-category byte-bucket table (retail `DAT_00567970`/`569518`)
 * that the map loader clears and seeds.  Optional — NULL skips the clear
 * and the per-cell append. */
void engine_territory_bind_category_buckets(engine_category_buckets_state_t *buckets);

/* Port of FUN_00467770 (525 B) — the strategic-map record → cell-grid
 * unpacker (the 1st `FUN_004676e0` callee that loads l2_maps.dat).  Resets
 * the render rotation offset and the player/roster counts, clears the
 * category buckets + player roster, then walks all 64×64 cells: it copies
 * planes 0..3 into cell bytes +1..+4 and plane 5 into +7, tracks the
 * player/army count `s_slot_0056d5dc` as the max plane-5 value ≤ 0x10, and
 * — where plane 4 is non-zero — appends to a category bucket (plane-0 bit
 * 0x40) or seats a roster entry (plane-0 bit 0x80).  Needs the cell grid
 * (engine_territory_bind_cells) and the record buffer bound; the buckets
 * bind is optional.  See the .c for the deferred file-read seam. */
void engine_territory_load_strat_map(void);

/* Port of FUN_004676e0 (144 B) — the overworld-grid setup sequence and a
 * direct callee of the new-game initialiser FUN_00497ced.  Runs the fixed
 * new-game pipeline that stands up the overworld cell grid + its terrain
 * index tables: clear cell bytes 0..5 (six clear_cell_field passes in
 * retail's 1,2,4,0,5,3 order), load the strategic-map record, clear the
 * render flags, classify feature-border tiles + randomise the special
 * tiles (both threading the single-LFSR `rng`), then build the per-army
 * adjacency, set up capital regions, seed army features, rebuild the cell
 * roster, and refresh the category-bucket digest.  Each pass is NULL-safe
 * on its own bound tables, so a partially-bound world simply runs the
 * passes whose inputs are present.
 *
 * `rng` is retail's persistent single-LFSR state `DAT_00591564` (seeded
 * 0x34518632 at InitInstance, never reset here); the live owner threads
 * the seeded state through, host tests pass a local.  NULL → the two
 * RNG-driven grid passes no-op.
 *
 * The per-country data bind 0x46a037(`s_slot_0053f034`) is **deferred**
 * at its call site — see the .c. */
void engine_territory_setup_overworld(engine_rng_single_in_t *rng);

/* Port of FUN_004684c6.  Paint army `key`'s four settlement cells (its +0x80
 * cell-list) to the village-size graphic for `active` (0..4) sustained
 * settlements: slots below `active` grow one village stage (type
 * 0x13→0x12→0x11→0x10, descriptor 0x3c/0x3b, +2 |= 0xc|0x80, +1 |= 0x10);
 * slots at/above revert to bare terrain (restore +3 from +6, clear the
 * settled/drawn flags + type).  Driven by the population pass
 * (engine_realm_advance_population).  No-op if the cell grid or army-record
 * block is unbound. */
void engine_territory_update_village_tiles(int key, int active);

#endif /* OPENLORDS2_ENGINE_TERRITORY_H */
