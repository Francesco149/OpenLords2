/*
 * OpenLords2 — combat-mode (battlefield) renderer.
 *
 * Sibling of `engine_map_render` and `engine_overlay_render` (the
 * strategic-mode primary + overlay passes).  This module owns the
 * cluster hung off the combat-mode frame driver at `FUN_004bc142 @
 * 0x4bc142` — invoked from the main-loop tick when
 * `DAT_0057c8fc == 2` (game-mode = combat).
 *
 * Chips 2 + 3 + 4 of the battlefield port (see
 * docs/findings/battlefield-render.md for the full chip plan):
 *
 *   FUN_004bc142 @ 0x4bc142 (143 B)  → engine_battlefield_render_frame
 *     Outer driver — sets camera globals, fans out to the zoom-
 *     dispatched terrain pass, the always-on middle pass, the zoom-
 *     dispatched overlay pass, the always-on late + final passes, and
 *     then decrements the force-redraw counter when non-zero.
 *
 *   FUN_004bcbd1 @ 0x4bcbd1 (11 B)   → internal no-op stub
 *     The zoom-16 second pass.  Retail body is literally `return;`.
 *     Ported as a true no-op (still bumps the per-pass counter so the
 *     test harness can confirm dispatch fidelity).
 *
 *   FUN_004bc8fc @ 0x4bc8fc (725 B)  → internal pass_terrain_zoom16
 *     Zoom-16 first pass (terrain blit).  Nested row/col loop over the
 *     visible grid.  For each dirty (or force-redraw) cell, stages
 *     sprite metadata from one of two sprite blobs and dispatches the
 *     16×16 raw blit primitive `gfx_sprite_blit_16x16_raw`.  Boundary
 *     cells are tagged with bit 0x02 of their flags byte (so they
 *     get re-painted next frame, handling scroll smear).
 *
 *   FUN_004bcbdc @ 0x4bcbdc (1187 B) → internal pass_terrain_zoom32
 *     Zoom-32 first pass — sibling of the zoom-16 terrain pass.  Uses
 *     the 32×32 raw blit primitive `gfx_sprite_blit_32x32_raw` for
 *     the first sprite per cell.  Adds two new per-cell behaviors
 *     absent from the zoom-16 pass:
 *
 *       1. A descriptor[+1] & 4 EARLY-SKIP gate: dirty cells with the
 *          bit set are silently dropped — NO drawing AND NO boundary
 *          marking.  Clean cells still get boundary marking.
 *       2. A SECOND sprite per cell, gated on descriptor[+0] != 0 and
 *          descriptor[+4] ∈ {1, 2, 3}.  The second sprite always uses
 *          the alt blob, picks slot 0x8b..0x9a by descriptor[+0]
 *          (clamped at 0x9a for values ≥ 0x10), and goes through the
 *          full clipped-blit dispatcher (clip_x → clip_y →
 *          apply_skip_and_dispatch_sprite).
 *
 *     The first sprite has a `[1, 0x4baf0]` src-offset gate — when the
 *     staged glyph_offset falls outside that band, the cell aborts
 *     immediately (skips boundary marking + skips the second sprite).
 *     Same gate is repeated on the alt-blob first-sprite path; both
 *     are preserved verbatim.
 *
 *   FUN_004bd07f @ 0x4bd07f (436 B) → internal pass_overlay_zoom32
 *     Zoom-32 second pass (overlay).  Outer shape mirrors the zoom-32
 *     terrain pass at 32 px cell pitch, but with three key differences:
 *
 *       1. Layer gate: drawing REQUIRES descriptor[+1] & 4.  Without
 *          that bit set, the cell is silently skipped — the
 *          complementary side of the terrain zoom-32 EARLY-SKIP
 *          (terrain only draws when the bit is CLEAR).  Net effect:
 *          per cell, exactly one of terrain-z32 or overlay-z32 may
 *          draw, picked by descriptor[+1] & 4.  Cells where neither
 *          draws (skip-bit set but cell is clean) still leave the
 *          dirty bit alone — both passes only clear it when they
 *          actually draw.
 *       2. Always uses the PRIMARY blob (DAT_004e5d78); the alt blob
 *          is not consulted (no (flags & 0x1c) selector here).
 *       3. NO boundary marking and NO second sprite — straight-line
 *          per-cell loop with no descriptor mutation beyond the dirty
 *          clear.  Same [1, 0x4baf0] glyph_offset gate as terrain-z32;
 *          out-of-band offsets just skip the blit (the only side-effect
 *          had already happened: dirty bit was cleared above the gate).
 *
 *   FUN_004bd355 @ 0x4bd355 (543 B)  → internal pass_middle (chip 5a.i)
 *     Middle pass — always-on, zoom-independent.  Three behaviors over
 *     the terrain/overlay passes:
 *
 *       1. Three setup helpers called once each at the head of the pass
 *          build, sort, and render a Y-ordered list of visible entities.
 *
 *          - chip 5a.ii.a (ported now) — FUN_004bd938 (visible-list
 *            build), FUN_004bda92 (bubble sort, 500-iter safety cap),
 *            FUN_004bdb95 (per-entity render loop).  The per-entity
 *            draw (FUN_004bdc31) stays as a per-entity stub for chip
 *            5a.ii.a; chip 5a.ii.b ports its body.
 *       2. Iterates an OVER-SCAN extension of the visible grid: starts
 *          one cell west / one cell north of the viewport, ends up to
 *          two cells past the south-east corner, clamped to the total
 *          grid extent (`camera_row_stride` × `total_rows`).
 *       3. Per-cell dispatch:
 *            - descriptor[+2] & 0x80 SET, descriptor[+3] == 0 → call
 *              FUN_004bd574 (player-marker draw) AND re-mark the cell
 *              dirty (set descriptor[+2] |= 1).  Re-dirtying is unique
 *              to this branch — the unit-key path below does NOT
 *              re-dirty.
 *            - descriptor[+2] & 0x80 SET, descriptor[+3] != 0 → call
 *              FUN_004bd759(descriptor[+3]) (unit-key sprite draw).
 *            - descriptor[+6] != 0 → call FUN_004beed4(descriptor[+6])
 *              (effect-chain walk).  Independent of the bit-0x80 gate;
 *              evaluated for every cell.
 *
 *     The three per-cell draw leaves (FUN_004bd574 / FUN_004bd759 /
 *     FUN_004beed4) are real ports as of chip 5a.iii; see the chip 5a.iii
 *     bindings further below.
 *
 * The other two passes are NOT ported yet.  Each is wired here as a
 * static stub that bumps a per-pass call counter so the driver's
 * dispatch shape (zoom-16 vs zoom-32, always-on vs zoom-gated) can be
 * verified before the bodies land.  Replacing a stub with a real port
 * is a one-line change in `engine_battlefield_render.c`:
 *
 *   FUN_004bd233 @ 0x4bd233 (290 B)  late pass         [stub]
 *   FUN_004bf1f9 @ 0x4bf1f9 (433 B)  final pass        [stub]
 *
 * State model: this module owns its own statics for the camera (set
 * by the driver from its parameters, retail DAT_004e657c / DAT_004e6580),
 * the zoom level (retail DAT_004e656c), and the force-redraw counter
 * (retail DAT_0053e9ac), plus the terrain-pass bindings (descriptor
 * table, grid dimensions, viewport offset, camera row stride, sprite
 * blobs).  The cursor (gfx_glyph cursor_x / cursor_y) and sprite
 * metadata sinks (gfx_glyph / gfx_dispatch) are staged through the
 * shared dispatcher just before each blit — identical to retail's
 * direct writes to DAT_005c9258 / DAT_005aeb70 / etc.
 */

#ifndef OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H
#define OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H

#include <stdint.h>

#include "engine_dirty_rect.h"

/* ─────────────────────────────────────────────────────────────────
 * Bindings — install retail globals into this module's statics
 * ───────────────────────────────────────────────────────────────── */

/* Bind the zoom level (retail `DAT_004e656c`).
 *
 *   0x10 → 16-px hex grid (zoom-1 / "small")
 *   0x20 → 32-px hex grid (zoom-2 / "large")
 *
 * Any other value causes the zoom-dispatched terrain + overlay passes
 * to be skipped (middle / late / final still fire) — matches retail's
 * if/else-if chain with no fall-through default. */
void engine_battlefield_render_set_zoom(int zoom);
int  engine_battlefield_render_zoom(void);

/* Bind the force-redraw counter (retail `DAT_0053e9ac`).  The frame
 * driver decrements this at end-of-frame when non-zero; when non-zero
 * the terrain pass overrides per-cell dirty bits, redrawing every
 * cell. */
void engine_battlefield_render_set_force_redraw(int counter);
int  engine_battlefield_render_force_redraw(void);

/* Bind the per-cell descriptor table for the battlefield grid.
 *
 *   base    retail `DAT_004e6574`  — pointer to the first entry.
 *   stride  retail `DAT_004e5d50`  — bytes per entry.
 *
 * Each entry holds (at least) two bytes consumed by the terrain pass:
 *
 *   +2  flag byte
 *         bit 0x01  — dirty (cell needs redraw); cleared after draw
 *         bit 0x02  — boundary (set after draw on grid-edge cells)
 *         bits 0x04..0x10  — sprite-blob selector
 *                              0 → primary blob (DAT_004e5d78)
 *                              4 → alt blob     (DAT_004e6560)
 *   +3  sprite index byte (into descriptor blob, slot * 16 + 8)
 *
 * Non-const because the terrain pass mutates the flag byte (clears
 * dirty, sets boundary). */
void engine_battlefield_render_bind_descriptor_table(uint8_t *base, int stride);

/* Bind the visible grid dimensions and row-wrap adjustment.
 *
 *   cols       retail `DAT_004e5d5c`  — visible columns per row.
 *   rows       retail `DAT_004e5d4c`  — visible rows.
 *   row_wrap   retail `DAT_004e5d70`  — descriptor offset added at
 *                                       end of each row, to skip the
 *                                       off-screen cells and reach
 *                                       the next visible row's first
 *                                       cell. */
void engine_battlefield_render_bind_grid(int cols, int rows, int row_wrap);

/* Bind the screen viewport offset — pixel coords of the top-left
 * corner of the visible grid.
 *
 *   x  retail `DAT_004e6558`  — initial cursor_x per row.
 *   y  retail `DAT_004e6554`  — initial cursor_y for the first row. */
void engine_battlefield_render_bind_viewport_offset(int x, int y);

/* Bind the camera row-stride — how many cells per row in the underlying
 * descriptor table (NOT necessarily the visible cols).  Used to
 * compute the per-frame descriptor cursor:
 *
 *   cursor_byte_off = (cam_x + cam_y * row_stride) * descriptor_stride
 *
 *   row_stride  retail `DAT_004e6578`. */
void engine_battlefield_render_bind_camera_row_stride(int row_stride);

/* Bind the total row count of the underlying grid (retail
 * `DAT_004e6570`).  Used by the middle pass to clamp the over-scan
 * extension so it doesn't walk past the last valid row.  The
 * over-scan analog for columns clamps against `camera_row_stride`,
 * which is already bound separately.
 *
 * Not consumed by terrain / overlay passes, which iterate exactly the
 * visible grid (`cols * rows`).  The middle pass extends iteration by
 * 1 cell west/north and up to 2 cells east/south, clamped to the total
 * extent. */
void engine_battlefield_render_bind_total_rows(int total_rows);

/* Bind the two sprite blobs the terrain pass dispatches to.
 *
 *   primary  retail `DAT_004e5d78`  — used when (flags & 0x1c) == 0.
 *   alt      retail `DAT_004e6560`  — used when (flags & 0x1c) == 4.
 *                                     ALSO used unconditionally by the
 *                                     zoom-32 pass for its second-
 *                                     sprite-per-cell path.
 *
 * Either pointer may be NULL if the corresponding selector is never
 * exercised; cells that hit a NULL blob have their dispatch suppressed
 * entirely (the dirty bit is still cleared and boundary marking still
 * runs — matches retail's strict if/else-if chain with no default). */
void engine_battlefield_render_bind_blobs(const uint8_t *primary,
                                          const uint8_t *alt);

/* Bind the cell sub-band width (retail `DAT_004e5d3c`).  Hex tiles
 * aren't square — the cell rect is divided into three vertical bands,
 * each `band_width` pixels wide, used by the variant screen→hex
 * converters (`pixel_to_col_left_biased` / `pixel_to_col_right_biased`)
 * to pick the neighboring hex column when the pixel falls in the
 * first / last band of a cell.  For a 32-px cell with a typical hex
 * shape, `band_width` is ~8 (one quarter of the cell), so:
 *
 *   residue ∈ [0, band_width)            → first band  (left neighbor)
 *   residue ∈ [band_width, band_width*3) → middle band (this cell)
 *   residue ∈ [band_width*3, zoom)       → third band  (right neighbor)
 *
 * Not consumed by the basic `pixel_to_col` / `pixel_to_row` pair. */
void engine_battlefield_render_bind_cell_band_width(int band_width);

/* Bind the per-entity table consumed by the middle pass's setup
 * helpers (chip 5a.ii.a).  Retail layout:
 *
 *   base    address of entity[0]'s record (entity ids 1..max_id are
 *           the iteration range; id 0 is reserved/sentinel and never
 *           visited).  Each record is `stride` bytes.
 *   stride  per-entry byte count (retail
 *           `ENGINE_BATTLEFIELD_ENTITY_STRIDE_RETAIL` = 0x1b0).
 *   max_id  last valid entity id, inclusive (retail
 *           `ENGINE_BATTLEFIELD_ENTITY_MAX_ID_RETAIL` = 0x50 = 80).
 *
 * Field offsets within each record (consumed by chip 5a.ii.a):
 *
 *   +0x20  short  x coord (DAT_005544a0)        — hex column
 *   +0x22  short  y coord (DAT_005544a2)        — hex row
 *   +0x2c  byte   alive flag (DAT_005544ac)     — zero ⇒ skipped
 *
 * Further fields (consumed by chip 5a.ii.b / FUN_004bdc31 port):
 *
 *   +0x00  void*  primary sprite blob ptr (DAT_00554480, 32-bit in
 *                 retail; the port reads it as a host-width pointer so
 *                 64-bit host tests can store real pointers).
 *   +0x04  int    secondary sprite blob ptr (DAT_00554484; reserved
 *                 for chip 5a.iv — FUN_004be4df type-6 secondary draw).
 *   +0x10  byte   primary sprite frame (DAT_00554490)
 *   +0x11  byte   secondary sprite frame (DAT_00554491)
 *   +0x12  byte   entity type (DAT_00554492)
 *   +0x18  byte   facing direction (DAT_00554498)
 *   +0x31  byte   subtype/variant (DAT_005544b1)
 *   +0x32  byte   animation frame within facing (DAT_005544b2)
 *
 * A NULL base pointer disables the build helper — visible-entity
 * count comes out as 0 and the render loop is a no-op.  Tests that
 * only exercise the per-cell dispatch (chip 5a.i) can skip this
 * binding. */
void engine_battlefield_render_bind_entity_table(uint8_t *base,
                                                 int stride, int max_id);

/* Retail constants for the entity table.  Exposed so tests can size
 * fixtures matching the retail layout. */
#define ENGINE_BATTLEFIELD_ENTITY_STRIDE_RETAIL  0x1b0
#define ENGINE_BATTLEFIELD_ENTITY_MAX_ID_RETAIL  0x50

#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB    0x00
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_BLOB  0x04
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_FRAME   0x10
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME 0x11
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE            0x12
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING          0x18
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_X               0x20
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y               0x22
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_ALIVE           0x2c
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_SUBTYPE         0x31
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME      0x32

/* Bind the per-entity animation-offset lookup tables (chip 5a.ii.b).
 *
 *   z16  retail `DAT_004e3bf0`  — used at zoom 0x10.
 *   z32  retail `DAT_004e4030`  — used at zoom 0x20.
 *
 * Both tables share the same layout:
 *
 *   entry(facing, frame) = base + facing * 0x88 + frame * 8
 *
 * Each entry is a little-endian (int x_off, int y_off) pair (8 bytes).
 * Facing and frame are read as SIGNED 8-bit values from the entity
 * record (`+0x18` facing, `+0x32` anim_frame) — negative values index
 * BACKWARDS into the table (filed as the corresponding quirk on the
 * 5a.ii.b dispatch).
 *
 * Per-facing capacity is 17 frames (0x88 / 8); per-table size is
 * facings × 0x88.  Retail's tables hold 8 facings each, so 0x440 bytes
 * per table (the two tables sit adjacent in retail).
 *
 * Either pointer may be NULL — entities are then drawn at their
 * un-adjusted base anchor (no per-frame animation offset).  Tests
 * that don't care about animation can pass NULL for both. */
void engine_battlefield_render_bind_anim_offset_tables(const uint8_t *z16,
                                                       const uint8_t *z32);

/* Per-facing / per-frame strides for the anim-offset tables. */
#define ENGINE_BATTLEFIELD_ANIM_FACING_STRIDE 0x88
#define ENGINE_BATTLEFIELD_ANIM_FRAME_STRIDE  8

/* Bind the dirty-rect state used by the per-entity draw (chip 5a.ii.b)
 * for the per-entity 5×5 / 9×9 box mark (retail `FUN_004527a6` call).
 * NULL ⇒ skip dirty-rect mark (rendering still happens). */
void engine_battlefield_render_set_dirty_state(engine_dirty_rect_state_t *state);

/* Bind the clip rect used by the zoom-32 pass's second-sprite-per-cell
 * path.  Retail passes these inline to FUN_0040464c / FUN_0040477b:
 *
 *   FUN_0040464c(DAT_004e6564, DAT_004e5d54)   — clip_x bounds
 *   FUN_0040477b(DAT_004e5d48, DAT_004e5d6c)   — clip_y bounds
 *
 *   left    retail `DAT_004e6564`
 *   right   retail `DAT_004e5d54`
 *   top     retail `DAT_004e5d48`
 *   bottom  retail `DAT_004e5d6c`
 *
 * Not consumed by the zoom-16 pass (which never goes through the
 * clipped blitter family). */
void engine_battlefield_render_bind_clip_rect(int left, int top,
                                              int right, int bottom);

/* ─────────────────────────────────────────────────────────────────
 * Middle-pass per-cell draw leaves (chip 5a.iii)
 * ───────────────────────────────────────────────────────────────── */

/* Player-marker bindings (FUN_004bd574).  The marker is the small
 * animated sprite drawn over the active player's hex (the 8-frame cycle
 * at slots [0x21..0x28] within a banner band).  Three inputs:
 *
 *   blob          retail `DAT_00566518`  — sprite blob (PL8 payload) the
 *                                          marker reads slots from.  NULL
 *                                          ⇒ leaf is a no-op.
 *   slot_table    retail `&DAT_0052f0b2` — byte-addressable per-player
 *                                          table.  Byte at
 *                                          `slot_table[player_id*stride]`
 *                                          is the player's banner band
 *                                          base (multiplied by 8 to skip
 *                                          past the 8-frame cycle of the
 *                                          previous band).  Inputs typically
 *                                          come from castle/player ownership.
 *   stride        retail `0x1a4`         — per-player record stride.
 *   player_id     retail `DAT_00568218`  — current active player id
 *                                          (0-based; 0..N-1 indexes the
 *                                          slot_table).
 *   anim_advance  retail `DAT_00591510`  — non-zero ⇒ advance frame
 *                                          counter; zero ⇒ hold frame.
 *                                          Set by a sibling "should
 *                                          animate" tick elsewhere.
 *
 * The 0..7 wrapped animation frame counter (retail `DAT_004e5b18`) is
 * owned by this module — `player_marker_anim_frame()` exposes it for
 * tests; `reset_counts()` zeros it. */
void engine_battlefield_render_bind_player_marker_blob(const uint8_t *blob);
void engine_battlefield_render_bind_player_marker_slot_table(const uint8_t *table,
                                                              int stride);
void engine_battlefield_render_set_player_marker_player_id(int player_id);
void engine_battlefield_render_set_player_marker_anim_advance(int advance);

/* Retail per-player record stride for the slot table. */
#define ENGINE_BATTLEFIELD_PLAYER_SLOT_TABLE_STRIDE_RETAIL 0x1a4

/* Unit-key bindings (FUN_004bd759).  The unit-key sprite is the small
 * letter sprite drawn over a unit's hex to identify it.  Just the blob
 * pointer — slot selection is driven by the key byte the middle pass
 * pulls from `descriptor[+3]`:
 *
 *   key  retail constant      slot
 *   0x49 ('I')                0x1f
 *   0x4c ('L')                0x20
 *   0x61 ('a')                0x21
 *   0x64 ('d')                0x22
 *   anything else             no draw (leaf returns immediately)
 *
 *   blob  retail `DAT_00553250`  — sprite blob containing slots 0x1f..0x22. */
void engine_battlefield_render_bind_unit_key_blob(const uint8_t *blob);

/* Effect-chain bindings (FUN_004beed4).  The effect chain is a linked
 * list of pending visual effects (arrows, hits, fire, etc) anchored to
 * a particular battlefield cell.  Each effect record has its own blob
 * pointer + per-record sprite slot — so multiple effects with
 * heterogeneous sprite sources can layer on the same cell.
 *
 *   base    retail `&DAT_0057a100` — effect-record array.  Effect ids
 *                                    1..max_id index into this; id 0 is
 *                                    the chain terminator (never read).
 *   stride  retail `0x4c`           — per-record byte count.
 *   max_id  retail (table size /    — last valid effect id, inclusive.
 *           stride - 1)               Caps recursion safety to this
 *                                     value.  Tests pick whatever
 *                                     accommodates the chain shape.
 *
 * Per-record field offsets:
 *
 *   +0x00  void*  sprite blob ptr — retail stores a 32-bit pointer
 *                 here.  The port IGNORES this field (the host's 8-byte
 *                 pointer would overlap +0x04 next_id on 64-bit) and
 *                 instead pulls a single shared blob from
 *                 `bind_effect_chain_shared_blob`.  All effects use that
 *                 shared blob.  Once we have a real game capture
 *                 demonstrating per-record blob differentiation, this
 *                 will be promoted to a side-channel host blob table
 *                 keyed by id; until then, the simpler shape suffices
 *                 for the chip-5a.iii tests.
 *   +0x04  short  next-id (for chain walk; 0 ends the chain; self-loop
 *                 — next-id == this id — also ends, as a safety guard).
 *   +0x08  char   type byte (0 ⇒ empty record, ends chain).
 *   +0x09  char   subtype byte.  Special values:
 *                   7  ⇒ skip render (chain walk continues to next-id).
 *                   5  ⇒ animated; per-frame offset added (advances
 *                        the shared 0..15 wrapped frame counter).
 *                   any other ⇒ render at static (x, y) offset.
 *   +0x0a  short  signed x offset (relative to the cell's pixel-space).
 *   +0x0c  short  signed y offset.
 *   +0x12  short  sprite slot index (into the record's own blob).
 *
 * Chain walk hard-caps at 10 hops per call (retail `9 < local_c` break).
 * Each iteration sets retail `DAT_00568994` to the current id (so the
 * end-of-loop next-pointer read is from the CURRENT record, even if
 * recursive calls in nested logic mutated it — the port mirrors this
 * with a module static). */
void engine_battlefield_render_bind_effect_chain_table(const uint8_t *base,
                                                       int stride,
                                                       int max_id);

/* Effect-chain shared blob — temporary substitute for the retail per-
 * record blob pointer (see notes on +0x00 above).  All effects with
 * subtype != 7 render from this blob; subtype-7 records skip render
 * entirely.  NULL ⇒ every render attempt becomes a no-op (counters
 * still bump). */
void engine_battlefield_render_bind_effect_chain_shared_blob(const uint8_t *blob);

/* Effect-chain animation-offset table (retail `DAT_004e4470`).  Used by
 * subtype-5 (animated) effects to add a per-frame (x_off, y_off) jitter.
 * 16 entries; entry i lives at `table + i * 8` and is a little-endian
 * `(int x_off, int y_off)` pair.
 *
 * NULL ⇒ subtype-5 animation hold at (0, 0).  The shared 0..15 wrapped
 * frame counter (retail `DAT_004e5b1c`) still advances. */
void engine_battlefield_render_bind_effect_chain_anim_table(const uint8_t *table);

/* Per-record stride + field offsets for the effect-chain table.  Exposed
 * so tests can populate fixtures matching the retail layout. */
#define ENGINE_BATTLEFIELD_EFFECT_CHAIN_STRIDE_RETAIL      0x4c
#define ENGINE_BATTLEFIELD_EFFECT_ANIM_TABLE_FRAMES        0x10

/* Retail's blob field at +0x00 is NOT consumed by the port; see
 * `bind_effect_chain_shared_blob` above for the shared-blob substitute. */
#define ENGINE_BATTLEFIELD_EFFECT_OFFSET_NEXT_ID  0x04
#define ENGINE_BATTLEFIELD_EFFECT_OFFSET_TYPE     0x08
#define ENGINE_BATTLEFIELD_EFFECT_OFFSET_SUBTYPE  0x09
#define ENGINE_BATTLEFIELD_EFFECT_OFFSET_X_OFFSET 0x0a
#define ENGINE_BATTLEFIELD_EFFECT_OFFSET_Y_OFFSET 0x0c
#define ENGINE_BATTLEFIELD_EFFECT_OFFSET_SLOT     0x12

/* Scroll/pan pad (retail `DAT_004e5d44`).  Added to BOTH cursor_x and
 * cursor_y by the effect-chain leaf when it positions an effect from
 * its (x, y) offset.  Likely a half-cell centering constant (so an
 * effect anchored at hex (X, Y) draws centred rather than corner-
 * aligned).  Tests typically leave at 0 unless probing this offset. */
void engine_battlefield_render_bind_scroll_pad(int pad);

/* ─────────────────────────────────────────────────────────────────
 * Per-entity type-special leaves (chip 5a.iv)
 *
 * These three leaves are dispatched from `middle_leaf_draw_entity`
 * (chip 5a.ii.b) under specific type/subtype gates:
 *
 *   type == 6              → FUN_004be4df (PRE-call, before main draw)
 *   type == 7              → FUN_004be7be (POST-call, after main draw)
 *   type == 9 AND
 *   subtype == 0x0e         → FUN_004beab9 (POST-call, after main draw)
 *
 * All three share the same shape: stage sprite descriptor from a
 * leaf-specific blob/slot pair, gate on the [1, 0x4baf0] glyph_offset
 * window, apply zoom-conditional anim offset + centering (with leaf-
 * specific X/Y biases), mark a 6×6 tile dirty box (force_full=2), set
 * clip + dispatch RLE blit, restore cursor.
 *
 * The 9-sub-0xe leaf is unique in that it draws TWO sprites per call
 * — slot+0x16 first (with cursor_y -= 16), then slot+0xd (with
 * cursor_y += 0x56), both anchored at the entity's entry cursor.  Both
 * are independently gated by the glyph_offset window; the second only
 * fires if the first passed.
 * ───────────────────────────────────────────────────────────────── */

/* Bind the shared blob for the type-6 PRE-call (FUN_004be4df).  Retail
 * reads this from the entity record at +0x04 (the secondary blob
 * pointer DAT_00554484), but on 64-bit hosts the 8-byte pointer would
 * overlap the primary blob at +0x00 — see the corresponding note in
 * the chip 5a.iv bindings docs.  All type-6 entities use this single
 * bound blob; the slot index still comes from the entity record's
 * +0x11 (secondary frame).  NULL ⇒ leaf is a no-op (counter still
 * bumps, but no blit). */
void engine_battlefield_render_bind_entity_type6_blob(const uint8_t *blob);

/* Bind the two facing-selected blobs for the type-7 POST-call
 * (FUN_004be7be).  Retail picks between two distinct sprite blobs
 * based on the entity's facing direction:
 *
 *   facing < 4   → use `low`  (retail DAT_00553f6c)
 *   facing >= 4  → use `high` (retail DAT_00522f78)
 *
 * Slot index comes from the entity record's +0x11 (secondary frame).
 * Either NULL ⇒ leaf is a no-op for the corresponding facing band
 * (counter still bumps). */
void engine_battlefield_render_bind_entity_type7_blobs(const uint8_t *low,
                                                       const uint8_t *high);

/* The type-9-sub-0xe POST-call (FUN_004beab9) reads its blob from the
 * entity record's +0x00 (primary blob).  No new binding required —
 * reuses the entity table + primary blob already accessible via
 * `bind_entity_table`. */

/* ─────────────────────────────────────────────────────────────────
 * Late-pass — per-cell creature draw + selection marker (chip 5c)
 *
 * Two leaves dispatched by the late-pass driver (FUN_004bd233 @ 0x4bd233):
 *
 *   FUN_004be070 (719 B)  per-cell creature draw — fires for each
 *                         descriptor cell with descriptor[+5] != 0 AND
 *                         the per-entity / per-player visibility mask
 *                         AND'd byte is non-zero.
 *
 *   FUN_004be33f (416 B)  selection marker — fires once per frame, after
 *                         the cell-loop, if the marker gate (retail
 *                         DAT_00553f84) is positive.  Draws a fixed-slot
 *                         (0x3f) sprite at a special world-position;
 *                         also invokes a world-map dirty hook.
 *
 * Both leaves share the same sprite blob (retail DAT_005530c8 —
 * "misc_ske.PL8") and dispatch through the same clipped-RLE blit family
 * the per-entity / type-special leaves use.
 *
 * The late-pass driver has its own per-cell pitch (hardcoded 0x20),
 * which differs from the middle pass (`g_zoom`) — see late-pass quirks
 * for the cross-zoom implications.
 * ───────────────────────────────────────────────────────────────── */

/* Byte offset within each battlefield descriptor entry holding the
 * entity id on that cell (0 ⇒ no entity, non-zero ⇒ index into the
 * entity table bound via `bind_entity_table`).  Hardcoded at retail. */
#define ENGINE_BATTLEFIELD_DESC_OFFSET_LATE_CREATURE_ID 5

/* Additional per-entity record offsets consumed by the late-pass
 * creature draw — sit alongside the chip 5a.ii.b offsets defined above. */
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_VISIBILITY_MASK 0x0a
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_SPRITE_SLOT     0x1a0

/* Bind the creature sprite blob (retail DAT_005530c8).  Used by BOTH
 * late-pass leaves: the per-cell creature draw picks slots from it via
 * the per-entity +0x1a0 short (shifted by `bind_creature_slot_shift`,
 * plus base 0x2e); the selection marker uses fixed slot 0x3f.
 *
 * NULL ⇒ both leaves are no-ops (counters still bump but no blits
 * dispatch). */
void engine_battlefield_render_bind_creature_sprite_blob(const uint8_t *blob);

/* Bind the per-player visibility mask byte (retail base &DAT_0057bfee,
 * stride 0x160, player_id DAT_0057c8cc).
 *
 *   base       byte address of player 0's visibility mask byte.  Bytes
 *              for subsequent players sit at base[player_id * stride].
 *   stride     per-player record stride (retail 0x160).
 *   player_id  active local player id.
 *
 * The byte at `base[player_id * stride]` is AND'd with the per-entity
 * visibility mask byte (at entity offset +0x0a) for each cell with a
 * non-zero descriptor[+5].  Cells where the AND is zero are silently
 * skipped (no creature draw, no counter bump).
 *
 * NULL base ⇒ every cell with descriptor[+5] != 0 is treated as
 * visibility-failed (no draw).  Tests that want to bypass the mask can
 * bind a single 0xff byte and player_id=0.
 *
 * The per-player visibility table is separate from the player-id used
 * by the player marker (chip 5a.iii — that's a different retail global,
 * DAT_00568218). */
void engine_battlefield_render_bind_late_player_visibility_mask(
    const uint8_t *base, int stride, int player_id);

/* Bind the sprite slot shift control (retail DAT_00566514 / DAT_0056651c).
 *
 *   amount  shift amount (0..31, masked with 0x1f at use site).
 *   sign    < 0 ⇒ right shift; >= 0 ⇒ left shift.
 *
 * Used by the creature draw leaf to derive the actual slot from the
 * per-entity +0x1a0 short:
 *
 *   slot_short = (sign < 0) ? (raw >> (amount & 0x1f))
 *                           : (raw << (amount & 0x1f))
 *   slot       = slot_short + 0x2e
 *
 * Defaults to (amount=0, sign=0) — left-shift by 0 is identity. */
void engine_battlefield_render_bind_creature_slot_shift(int amount, int sign);

/* Bind the selection-marker special position (retail DAT_00553f14 /
 * DAT_00553f18 / DAT_00553f84).
 *
 *   x     world-map x of the active hero / army.
 *   y     world-map y of the active hero / army.
 *   gate  non-zero ⇒ marker fires after the cell-loop; zero ⇒ skip.
 *
 * The marker draws at screen pixel
 *
 *   px_x = (x - cam_x) * 0x20 + viewport_x + 2
 *   px_y = (y - cam_y) * 0x20 + viewport_y + 9
 *
 * with fixed slot 0x3f from the creature sprite blob.  After the draw
 * (inside the glyph-offset gate), the world-map dirty hook is called
 * with `(x - 1, y - 1, 3, 3)` to mark the corresponding 3×3 box of
 * world-map cells dirty.  The dirty-rect mark on the battlefield's own
 * dirty state (3×3 cells, force_full=2) ALWAYS runs, regardless of
 * the glyph-offset gate.
 *
 * The marker reuses the late pass's hardcoded 0x20 cell pitch — at
 * zoom 16 the marker is drawn at zoom-32 spacing (see late-pass quirks). */
void engine_battlefield_render_bind_selection_marker(int x, int y, int gate);

/* Bind the world-map dirty hook called by the selection marker leaf
 * (retail FUN_0046c4a3).  Retail ORs bit 1 into the per-cell collision-
 * grid byte at &DAT_005440e2 + (y*0x50+x)*8 for each cell of the box.
 *
 * The port leaves the actual world-map subsystem out of the battlefield
 * module — bind a callback (or NULL ⇒ no-op).  Tests can bind a
 * counting stub to verify the hook fires. */
typedef void (*engine_battlefield_world_dirty_hook_t)(int x, int y,
                                                       int w, int h);
void engine_battlefield_render_bind_world_map_dirty_hook(
    engine_battlefield_world_dirty_hook_t hook);

/* ─────────────────────────────────────────────────────────────────
 * Final-pass — per-creature debug label overlay (chip 5d)
 *
 * Single function: `FUN_004bf1f9 @ 0x4bf1f9` (433 B).  Iterates the
 * same visible cells the late pass walks (NO over-scan), gated on a
 * single debug-mode flag (retail DAT_005bb4a5).  For each cell with
 * descriptor[+5] != 0:
 *
 *   1. Sum the first 0x19e bytes of the entity record at
 *      `entity_base + entity_id * 0x1b0`.  Cast the sum to signed
 *      char (sign-extended to int) — high-bit-set sums become
 *      negative values.
 *   2. Three-way bucket on the result:
 *        sum <  0x65        → value = sum,        pen = 0x3f
 *        sum <  0xc9        → value = sum - 100,  pen = 0xf9
 *        else               → value = sum - 200,  pen = 0x20
 *   3. Skip render when value == 0 OR value == 999 (magic filters).
 *   4. Otherwise render the decimal value at
 *        px_x = col * zoom + viewport_x + 1
 *        px_y = row * zoom + viewport_y + 1
 *      via the shared label dispatcher (prefix=' ', suffix from a
 *      bound string, font from a bound table pair).
 *
 * The pixel pitch IS zoom-aware (uses `g_zoom` directly — UNLIKE the
 * late pass which hardcodes 0x20 — see quirk #97 absence here).
 *
 * The whole pass is a no-op when the debug flag is 0.  Used in retail
 * during AI-tuning sessions or designer playtests.
 * ───────────────────────────────────────────────────────────────── */

/* Bind the debug-mode gate (retail DAT_005bb4a5).  Non-zero ⇒ final
 * pass walks the grid; zero ⇒ pass is a no-op.  Default 0. */
void engine_battlefield_render_bind_debug_label_enabled(int enabled);

/* Bind the font + char-glyph table used by the final pass for label
 * rendering.  Retail uses `&DAT_005af8f0` for the font; the port's
 * `gfx_text_label_render_dec` factors the `char_glyph_table` out
 * separately (see notes on the renderer split).  Either NULL ⇒
 * label render is suppressed (the pass still walks cells, just
 * silently skips the renderer call). */
void engine_battlefield_render_bind_debug_label_font(
    const uint8_t *font_base, const uint8_t *char_glyph_table);

/* Bind the suffix string appended to the decimal label (retail
 * `&DAT_004e4520`).  Defaults to an empty string. */
void engine_battlefield_render_bind_debug_label_suffix(const char *suffix);

/* Final-pass test counters.  Bumped by the real port (FUN_004bf1f9).
 *
 *   per_cell_count   — cells visited (rows * cols).  Bumps once
 *                      per cell EVEN when the debug flag is off.
 *                      Wait — actually retail's outer `if (DAT_005bb4a5 != '\0')`
 *                      gates the WHOLE walk; counter only bumps when
 *                      the debug pass actually runs.
 *   label_render_count — labels actually dispatched to the renderer
 *                        (entity_id != 0 AND value not 0 / 999 AND
 *                        font bound). */
int engine_battlefield_render_final_per_cell_count(void);
int engine_battlefield_render_final_label_render_count(void);

/* Late-pass test counters.  Bumped by the real port (FUN_004bd233 +
 * FUN_004be070 + FUN_004be33f).  Distinct from the per-pass call
 * counter `count_late` — that bumps once per frame; these bump per
 * sub-event.
 *
 *   per_cell_count             — cells visited (rows * cols).
 *   creature_draw_count        — cells where descriptor[+5] != 0 AND
 *                                visibility mask AND'd non-zero (i.e.
 *                                creature leaf was actually dispatched).
 *   creature_blit_count        — creature leaves that passed the
 *                                glyph-offset gate AND reached the
 *                                RLE blit dispatcher (post y_clip_mode
 *                                != 5 gate).
 *   selection_marker_count     — selection-marker leaf dispatched (gate
 *                                was non-zero).
 *   selection_marker_blit_count — selection-marker leaf reached the
 *                                blit (gate non-zero + glyph-offset
 *                                in band + y_clip_mode != 5).
 *   world_dirty_hook_count     — world-map dirty hook fired (inside
 *                                the selection-marker glyph-offset gate). */
int engine_battlefield_render_late_per_cell_count(void);
int engine_battlefield_render_late_creature_draw_count(void);
int engine_battlefield_render_late_creature_blit_count(void);
int engine_battlefield_render_late_selection_marker_count(void);
int engine_battlefield_render_late_selection_marker_blit_count(void);
int engine_battlefield_render_late_world_dirty_hook_count(void);

/* ─────────────────────────────────────────────────────────────────
 * Frame render
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_004bc142.  Render one battlefield frame at camera
 * (`camera_x`, `camera_y`).
 *
 *   1. Set camera globals (retail DAT_004e657c / DAT_004e6580).
 *   2. Dispatch the zoom-dependent terrain pass:
 *        zoom == 0x10 → FUN_004bc8fc (stub)
 *        zoom == 0x20 → FUN_004bcbdc (stub)
 *   3. Always: middle pass (FUN_004bd355, stub).
 *   4. Dispatch the zoom-dependent overlay pass:
 *        zoom == 0x10 → FUN_004bcbd1 (real no-op, bumps counter)
 *        zoom == 0x20 → FUN_004bd07f (stub)
 *   5. Always: late pass (FUN_004bd233, port; per-cell creature draw +
 *      selection marker).
 *   6. Always: final pass (FUN_004bf1f9, port; debug label overlay).
 *   7. Decrement force-redraw counter if non-zero. */
void engine_battlefield_render_frame(int camera_x, int camera_y);

/* Camera accessors (retail DAT_004e657c, DAT_004e6580).  Set by
 * `engine_battlefield_render_frame` from its parameters. */
int engine_battlefield_render_camera_x(void);
int engine_battlefield_render_camera_y(void);

/* ─────────────────────────────────────────────────────────────────
 * Coordinate helpers — small pure functions for input handling.
 * Each is a faithful 1:1 port of a tiny retail helper at the head of
 * the battlefield cluster.  They read the same module statics the
 * passes do (viewport, zoom, grid dims, camera_row_stride, cell band
 * width), so the bindings used to drive a render frame also drive
 * these queries.
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_004bc222 @ 0x4bc222 (131 B).  Test whether the pixel
 * coord (px, py) falls inside the visible grid rectangle.  Returns
 * 1 if inside, 0 otherwise.
 *
 *   inside = px ∈ [viewport_x, viewport_x + cols*zoom)
 *         ∧ py ∈ [viewport_y, viewport_y + rows*zoom)
 *
 * Uses `cols` (DAT_004e5d5c), `rows` (DAT_004e5d4c), `zoom`
 * (DAT_004e656c), and the viewport (DAT_004e6558, DAT_004e6554). */
int engine_battlefield_render_screen_in_grid(int px, int py);

/* Port of FUN_004bc2a5 @ 0x4bc2a5 (32 B).  Map a screen pixel X to a
 * visible-grid hex column.  Integer division — no sub-cell band
 * correction:  `col = (px - viewport_x) / zoom`.
 *
 * No clamping to grid extents — caller is expected to either pre-check
 * with `screen_in_grid` or accept out-of-range values. */
int engine_battlefield_render_pixel_to_col(int px);

/* Port of FUN_004bc2c5 @ 0x4bc2c5 (32 B).  Sibling of `pixel_to_col`
 * on the Y axis:  `row = (py - viewport_y) / zoom`. */
int engine_battlefield_render_pixel_to_row(int py);

/* Port of FUN_004bc2e5 @ 0x4bc2e5 (97 B).  Variant of `pixel_to_col`
 * that biases towards the RIGHT neighbor when the pixel falls in the
 * third sub-band of the cell:
 *
 *   col = (px - viewport_x) / zoom
 *   if ((px - viewport_x) % zoom >= band_width * 3) col += 1
 *   if (col < 1) col = 0
 *
 * The < 1 → 0 clamp ensures negative columns floor to 0 (no left-of-
 * grid columns ever emerge from this variant).  Used for input
 * coordinate quantization where right-half pixels should attribute
 * to the right-adjacent hex. */
int engine_battlefield_render_pixel_to_col_right_biased(int px);

/* Port of FUN_004bc346 @ 0x4bc346 (102 B).  Variant of `pixel_to_col`
 * that biases towards the LEFT neighbor when the pixel falls in the
 * first sub-band of the cell:
 *
 *   col = (px - viewport_x) / zoom
 *   if ((px - viewport_x) % zoom < band_width) col -= 1
 *   if (col >= camera_row_stride) col = camera_row_stride - 1
 *
 * Asymmetry vs. `pixel_to_col_right_biased`: this variant DOES clamp
 * the upper bound (to `camera_row_stride - 1`) but does NOT clamp the
 * lower bound.  Negative columns can propagate (left-of-grid hits)
 * even after the -1 bias. */
int engine_battlefield_render_pixel_to_col_left_biased(int px);

/* ─────────────────────────────────────────────────────────────────
 * Test instrumentation — per-pass call counters
 * ───────────────────────────────────────────────────────────────── */

/* Per-pass call counters.  Bumped each time the named pass is
 * dispatched by the frame driver.  Not retail — purely for unit
 * tests to verify the driver's dispatch shape.  Each name encodes
 * the retail symbol so future ports can track which counters
 * should disappear (a real port doesn't need a counter — the
 * port's own side effects are observable). */
int  engine_battlefield_render_count_terrain_zoom16(void);  /* FUN_004bc8fc — port; bumped per frame */
int  engine_battlefield_render_count_terrain_zoom32(void);  /* FUN_004bcbdc */
int  engine_battlefield_render_count_middle(void);          /* FUN_004bd355 */
int  engine_battlefield_render_count_overlay_zoom16(void);  /* FUN_004bcbd1 (no-op) */
int  engine_battlefield_render_count_overlay_zoom32(void);  /* FUN_004bd07f */
int  engine_battlefield_render_count_late(void);            /* FUN_004bd233 */
int  engine_battlefield_render_count_final(void);           /* FUN_004bf1f9 */
void engine_battlefield_render_reset_counts(void);

/* Per-blit dispatch counter — bumped each time the terrain pass
 * actually calls `gfx_sprite_blit_16x16_raw` (i.e. cell was dirty
 * OR force-redraw was on, AND the flag-selected blob was bound).
 * Distinct from the per-frame `count_terrain_zoom16`: that one bumps
 * once per FUN_004bc8fc invocation, this one bumps once per per-cell
 * blit.  Test-only — not part of retail. */
int  engine_battlefield_render_terrain_blit_count(void);

/* Per-blit counters for the zoom-32 first pass.  Both bump only when
 * the corresponding blit is actually dispatched (after the src-offset
 * gate and any blob-selector skip).
 *
 *   first  — bumped per cell that successfully dispatches the 32×32
 *            raw blit (`gfx_sprite_blit_32x32_raw`) for the primary
 *            sprite.
 *   second — bumped per cell that successfully dispatches the clipped
 *            blit for the second sprite (gated on descriptor[+0] != 0
 *            AND descriptor[+4] ∈ {1, 2, 3} AND y_clip_mode != 5). */
int  engine_battlefield_render_terrain_zoom32_first_blit_count(void);
int  engine_battlefield_render_terrain_zoom32_second_blit_count(void);

/* Per-blit counter for the zoom-32 overlay pass (FUN_004bd07f).  Bumps
 * once per cell that actually dispatches a 32×32 raw blit (after the
 * descriptor[+1] & 4 layer gate, dirty/forced gate, AND the [1, 0x4baf0]
 * glyph_offset gate).  Distinct from the per-pass `count_overlay_zoom32`
 * which bumps once per FUN_004bd07f invocation. */
int  engine_battlefield_render_overlay_zoom32_blit_count(void);

/* Per-leaf call counters for the middle-pass.  Three of the six leaves
 * have real ports as of chip 5a.ii.a (build / sort / render);
 * the per-entity draw and the three per-cell draw leaves are still
 * stubs.  Each counter bumps once per dispatch — they let tests verify
 * the driver's per-cell dispatch shape without depending on real leaf
 * bodies.
 *
 *   build / sort / render — three setup helpers (FUN_004bd938 /
 *                           FUN_004bda92 / FUN_004bdb95) called once
 *                           each at the head of the middle pass.
 *                           Chip 5a.ii.a ports their bodies; counters
 *                           still bump once per pass for cross-checking.
 *   player_marker         — FUN_004bd574, dispatched when descriptor[+2]
 *                           has bit 0x80 set AND descriptor[+3] == 0.
 *                           Chip 5a.iii ports the body.
 *   unit_key              — FUN_004bd759(descriptor[+3]), dispatched
 *                           when bit 0x80 set AND descriptor[+3] != 0.
 *                           Chip 5a.iii ports the body.
 *   effect_chain          — FUN_004beed4(descriptor[+6]), dispatched
 *                           per cell whenever descriptor[+6] != 0
 *                           (independent of the bit-0x80 gate).
 *                           Chip 5a.iii ports the body. */
int  engine_battlefield_render_middle_build_count(void);
int  engine_battlefield_render_middle_sort_count(void);
int  engine_battlefield_render_middle_render_count(void);
int  engine_battlefield_render_middle_player_marker_count(void);
int  engine_battlefield_render_middle_unit_key_count(void);
int  engine_battlefield_render_middle_effect_chain_count(void);

/* Visible-entity list accessors (chip 5a.ii.a).  Populated by the
 * build helper (`FUN_004bd938`) at the head of every middle-pass call;
 * Y-sorted in place by the sort helper (`FUN_004bda92`); walked by the
 * render-loop helper (`FUN_004bdb95`).  After a frame render (or a
 * test-only middle-pass call) the list reflects the most recent
 * dispatch.
 *
 *   count       — number of valid entries, EXCLUSIVE of the in-band
 *                 sentinel.  Retail `DAT_004e5d38`.
 *   id(idx)     — entity id at sorted position idx ∈ [0, count).
 *                 Retail `DAT_004e5d80[idx*2]` (short).
 *   y(idx)      — entity y coord at sorted position idx ∈ [0, count).
 *                 Retail `DAT_004e5d82[idx*2]` (short).
 *
 * Out-of-range indices return 0 (defensive — retail would just
 * silently read past the buffer).  The sentinel at index `count`
 * (retail value y=9999, id=0) is NOT exposed; tests can verify it
 * indirectly by tearing down the binding and watching count reset to
 * zero. */
int engine_battlefield_render_visible_entity_count(void);
int engine_battlefield_render_visible_entity_id(int idx);
int engine_battlefield_render_visible_entity_y(int idx);

/* The number of outer iterations the bubble sort performed during the
 * most recent middle-pass dispatch (retail `_DAT_004e655c`, reset to 0
 * at the head of `FUN_004bda92`).  0 means the list was already sorted
 * (or contained 0/1 entries); a positive value indicates at least one
 * pass ran.  Capped at 500 by the safety guard. */
int engine_battlefield_render_middle_sort_passes(void);

/* Per-entity draw cross-check instrumentation (chip 5a.ii.a +
 * 5a.ii.b).  The per-entity render loop (`FUN_004bdb95`) computes
 * (px_x, px_y) from each visible entity's hex coords and then
 * dispatches the per-entity draw (`FUN_004bdc31`).  After chip 5a.ii.b,
 * the per-entity draw is a real port, but the count + last-* capture
 * stay as cross-check state:
 *
 *   count       — bumped once per call (once per visible entity per
 *                 frame, regardless of which gates fire inside).
 *   last_id     — entity id passed to the draw.
 *   last_px_x,  — staged cursor at entry (the pre-anim-offset anchor
 *   last_px_y     computed by the render loop).
 *
 * NOTE: as of chip 5a.ii.b, the per-entity draw applies an animation
 * offset to the cursor and may apply a type-9/type-10 Y bonus before
 * the actual blit.  The last_px_x / last_px_y values here capture the
 * ENTRY cursor (the render loop's output), NOT the post-anim-offset
 * value the blit uses.  Tests that want the post-adjust cursor must
 * compute it themselves from the entity's facing/frame indices and
 * the bound anim table. */
int engine_battlefield_render_middle_per_entity_count(void);
int engine_battlefield_render_middle_per_entity_last_id(void);
int engine_battlefield_render_middle_per_entity_last_px_x(void);
int engine_battlefield_render_middle_per_entity_last_px_y(void);

/* Per-entity TYPE-SPECIAL counters (chip 5a.ii.b + 5a.iv).  The
 * per-entity draw dispatches three type-special leaves at different
 * points in its body:
 *
 *   type6  — FUN_004be4df, PRE-call before the main draw.  Fires
 *            whenever entity[+0x12] == 6.  Shadow/ground effect drawn
 *            beneath the main sprite (uses a separate "secondary"
 *            sprite blob bound via `bind_entity_type6_blob`).
 *   type7  — FUN_004be7be, POST-call after the main draw.  Fires
 *            whenever entity[+0x12] == 7.  Gated by the same glyph-
 *            offset check that gates the main draw body.  Uses one of
 *            two facing-selected blobs (`bind_entity_type7_blobs`).
 *   type9_sub_e — FUN_004beab9, POST-call after the main draw.  Fires
 *                 when entity[+0x12] == 9 AND entity[+0x31] == 0x0e.
 *                 Same glyph-offset gate.  Draws TWO sprites per call
 *                 from the entity's primary blob (different slot
 *                 offsets + Y biases).
 *
 * Per-call counters (count / last_id) bump once per dispatch,
 * regardless of whether the leaf body actually blits.  Per-blit
 * counters (chip 5a.iv) bump only when the dispatcher actually
 * reaches the RLE blit (after blob bind check + glyph_offset gate
 * + y_clip_mode != 5).  For type-9-sub-0xe, blit_count can be 0,
 * 1, or 2 per call (each of the two sprites is independently gated). */
int engine_battlefield_render_middle_entity_type6_count(void);
int engine_battlefield_render_middle_entity_type6_last_id(void);
int engine_battlefield_render_middle_entity_type7_count(void);
int engine_battlefield_render_middle_entity_type7_last_id(void);
int engine_battlefield_render_middle_entity_type9_sub_count(void);
int engine_battlefield_render_middle_entity_type9_sub_last_id(void);

/* Per-blit counters for the three type-special leaves (chip 5a.iv).
 * See the per-call block above for the relationship between count and
 * blit_count.  Each bumps when its leaf reaches the RLE blit (post
 * blob bind + glyph_offset gate + y_clip_mode != 5 gate). */
int engine_battlefield_render_middle_entity_type6_blit_count(void);
int engine_battlefield_render_middle_entity_type7_blit_count(void);
int engine_battlefield_render_middle_entity_type9_sub_blit_count(void);

/* No-primary-blob counter (chip 5a.ii.b).  When an entity's primary
 * sprite blob pointer (+0x00) is NULL, retail logs via FUN_004afab9
 * and skips the main draw (the type-7 / type-9 post-calls are also
 * skipped, since they sit inside the no-blob else branch — but the
 * type-6 PRE-call DOES fire before the no-blob check).  The port
 * counts the no-blob path so tests can verify the gate. */
int engine_battlefield_render_middle_entity_no_blob_count(void);
int engine_battlefield_render_middle_entity_no_blob_last_id(void);

/* Per-entity blit dispatch counter (chip 5a.ii.b).  Bumped each time
 * the per-entity draw actually calls gfx_dispatch_dispatch_sprite_rle
 * (i.e. after the glyph-offset gate, the y_clip_mode != 5 gate).  Not
 * to be confused with `count`, which bumps once per entity dispatched
 * (regardless of whether the blit fires). */
int engine_battlefield_render_middle_entity_blit_count(void);

/* The unit_key and effect_chain leaves each capture the LAST byte they
 * were called with — descriptor[+3] for unit_key, descriptor[+6] for
 * effect_chain.  Tests use these to verify the driver passes the right
 * byte from the descriptor.  Initialized to -1 and reset to -1 by
 * `engine_battlefield_render_reset_counts`.
 *
 * Captured ONCE per dispatch (at entry), so they remain meaningful even
 * after the leaf body fans into the chain walk or skips work via gates. */
int  engine_battlefield_render_middle_unit_key_last_arg(void);
int  engine_battlefield_render_middle_effect_chain_last_arg(void);

/* Per-leaf BLIT-dispatch counters (chip 5a.iii).  Each bumps once per
 * call to `gfx_dispatch_dispatch_sprite_rle` made from the named leaf —
 * AFTER the [1, 0x4baf0] glyph_offset gate AND the y_clip_mode != 5
 * gate (the latter is applied inside the dispatcher).  Distinct from
 * the per-leaf CALL counters above which bump once per dispatch
 * regardless of how many internal gates fired.
 *
 *   player_marker  — bumps when FUN_004bd574 reaches the blit.  0 means
 *                    either anim_advance was held with a slot whose
 *                    src_off lies outside the gate, OR the blob/slot
 *                    table wasn't bound.
 *   unit_key       — bumps when FUN_004bd759 reaches the blit (key in
 *                    {0x49, 0x4c, 0x61, 0x64} AND blob bound AND gate
 *                    passes).
 *   effect_chain   — bumps PER HOP in the chain walk that reaches the
 *                    blit (so a 3-effect chain with all 3 visible can
 *                    bump this counter by 3 in one outer call). */
int engine_battlefield_render_middle_player_marker_blit_count(void);
int engine_battlefield_render_middle_unit_key_blit_count(void);
int engine_battlefield_render_middle_effect_chain_blit_count(void);

/* Animation-frame accessors (chip 5a.iii).  Both counters are owned by
 * this module and survive across frames — they only reset when
 * `engine_battlefield_render_reset_counts` is called OR (for the player-
 * marker) when the next call observes the counter outside [0, 7].
 *
 *   player_marker  — retail `DAT_004e5b18`.  0..7 wrapped.  Advances
 *                    by 1 on each call where `anim_advance` is non-zero;
 *                    stays put when `anim_advance` is zero.  Bounded by
 *                    the pair `if (n < 0) n = 0; if (7 < n) n = 0;`
 *                    sandwich on entry (so a manually pre-set value of
 *                    e.g. 9 will normalize to 0 BEFORE the conditional
 *                    advance).
 *   effect_chain   — retail `DAT_004e5b1c`.  0..15 wrapped.  Advances
 *                    by 1 on EACH subtype-5 (animated) hop in the chain
 *                    walk; held on non-animated hops. */
int engine_battlefield_render_player_marker_anim_frame(void);
int engine_battlefield_render_effect_chain_anim_frame(void);

/* Number of effect-chain hops walked during the most recent call to the
 * leaf (retail `local_c` in FUN_004beed4).  0 when the chain head was 0
 * (immediate return); positive otherwise.  Capped at 10 by the safety
 * guard.  Useful for distinguishing "chain terminated cleanly" from
 * "chain hit safety cap" in tests. */
int engine_battlefield_render_middle_effect_chain_iters(void);

/* Computed slot index the player marker used on its most recent
 * successful blit (retail `DAT_005c9288 = banner_byte * 8 + frame + 0x21`
 * at the call site).  -1 if the leaf hasn't blitted since reset.  Lets
 * tests verify slot computation without inspecting the actual blob
 * pixels. */
int engine_battlefield_render_middle_player_marker_last_slot(void);

/* Slot the unit_key leaf last picked from its key→slot map.  -1 if no
 * blit since reset.  Maps:
 *
 *   key 0x49 → slot 0x1f
 *   key 0x4c → slot 0x20
 *   key 0x61 → slot 0x21
 *   key 0x64 → slot 0x22
 *
 * Any other key bypasses the leaf entirely (no blit, no slot update). */
int engine_battlefield_render_middle_unit_key_last_slot(void);

/* Per-cell counter incremented by the middle pass for each cell it
 * visits (after gating).  This is the same `_DAT_004e5d64` global that
 * the zoom-16 terrain pass also drives.  Exposed for chip 5a.i tests
 * to verify cell-loop iteration counts (rows × cols of the over-scan
 * extension). */
int  engine_battlefield_render_per_frame_cell_counter(void);

/* TEST-ONLY: run just the middle pass with the given camera.
 *
 * Bypasses the frame driver's terrain / overlay / late / final
 * dispatch, so tests can inspect cursor and per-cell counter state
 * without contamination from sibling passes that also mutate them.
 * Sets the camera globals first (same effect as the frame driver's
 * per-frame camera write) so the over-scan extension is computed from
 * the test's chosen camera. */
void engine_battlefield_render_run_pass_middle_for_test(int camera_x, int camera_y);

/* TEST-ONLY: run just the late pass with the given camera.  Same
 * rationale as the middle-pass equivalent — bypasses the frame driver
 * so tests can inspect late-pass counters / dirty-rect / cursor without
 * sibling-pass contamination. */
void engine_battlefield_render_run_pass_late_for_test(int camera_x, int camera_y);

/* TEST-ONLY: run just the final pass with the given camera.  Bypasses
 * the frame driver so tests can inspect the debug-label overlay's
 * counters and render-state effects in isolation. */
void engine_battlefield_render_run_pass_final_for_test(int camera_x, int camera_y);

#endif /* OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H */
