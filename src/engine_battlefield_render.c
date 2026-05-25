/*
 * OpenLords2 — combat-mode (battlefield) renderer (implementation).
 *
 * See engine_battlefield_render.h for the function map and chip plan;
 * see docs/findings/battlefield-render.md for the cluster survey,
 * sprite primitive notes, and storage layout.
 */

#include "engine_battlefield_render.h"

#include "engine_dirty_rect.h"
#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_sprite.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Module state — retail globals owned by this cluster.
 * ────────────────────────────────────────────────────────────────── */

/* Camera (retail DAT_004e657c, DAT_004e6580).  Set by the frame
 * driver from its parameters.  Read by every pass that needs camera
 * coords; the cursor (gfx_glyph cursor_x/y) is set per-cell from
 * (viewport + 16 * col, viewport + 16 * row). */
static int g_camera_x;
static int g_camera_y;

/* Zoom level (retail DAT_004e656c).  Read by the frame driver to
 * pick the terrain + overlay passes. */
static int g_zoom;

/* Force-redraw counter (retail DAT_0053e9ac).  Driver decrements
 * post-frame when non-zero; terrain pass overrides per-cell dirty
 * bits when non-zero. */
static int g_force_redraw;

/* Descriptor table (retail DAT_004e6574 base, DAT_004e5d50 stride).
 * Non-const because the terrain pass mutates the flag byte (clears
 * dirty bit, sets boundary bit). */
static uint8_t *g_desc_base;
static int      g_desc_stride;

/* Visible grid dimensions (retail DAT_004e5d5c cols, DAT_004e5d4c rows)
 * and the descriptor row-wrap (retail DAT_004e5d70) — the offset
 * added at end of each visible row to reach the next visible row's
 * first cell. */
static int g_grid_cols;
static int g_grid_rows;
static int g_grid_row_wrap;

/* Screen viewport offset (retail DAT_004e6558 x, DAT_004e6554 y) —
 * pixel coords of the top-left of the visible grid. */
static int g_viewport_x;
static int g_viewport_y;

/* Camera row stride in cells (retail DAT_004e6578) — used to compute
 * the per-frame descriptor cursor.  Also the upper-bound clamp in
 * `pixel_to_col_left_biased`. */
static int g_camera_row_stride;

/* Total row count of the underlying grid (retail DAT_004e6570).
 * Used by the middle pass (chip 5a.i) to clamp its over-scan
 * extension.  Not consumed by terrain / overlay. */
static int g_total_rows;

/* Cell sub-band width in pixels (retail DAT_004e5d3c).  Used by the
 * left/right-biased screen→hex variants to pick which neighbor a
 * pixel attributes to when it falls in a band of the cell.  See the
 * header for the band → action table. */
static int g_cell_band_width;

/* Sprite blobs (retail DAT_004e5d78 primary, DAT_004e6560 alt) —
 * each cell's flags byte (bits 0x04..0x10) selects between them. */
static const uint8_t *g_blob_primary;
static const uint8_t *g_blob_alt;

/* Clip rect for the zoom-32 second-sprite path.  Retail:
 *   left   = DAT_004e6564   right  = DAT_004e5d54
 *   top    = DAT_004e5d48   bottom = DAT_004e5d6c
 */
static int g_clip_left;
static int g_clip_top;
static int g_clip_right;
static int g_clip_bottom;

/* Per-frame ephemeral state (mirrors retail running statics).
 *   DAT_004e6588  — descriptor cursor byte offset, advanced per cell.
 *   _DAT_004e5d64 — per-cell counter, incremented per cell (purpose
 *                   beyond debug instrumentation not yet known —
 *                   retail doesn't read it back inside this pass). */
static int g_desc_cursor_offset;
static int g_per_frame_cell_counter;

/* Per-blit dispatch counter — bumped each time the terrain pass
 * actually calls `gfx_sprite_blit_16x16_raw`. */
static int g_terrain_blit_count;

/* Per-blit dispatch counters for the zoom-32 first pass. */
static int g_terrain_z32_first_blit_count;
static int g_terrain_z32_second_blit_count;

/* Per-blit dispatch counter for the zoom-32 overlay pass (chip 5b). */
static int g_overlay_z32_blit_count;

/* Per-leaf call counters for the middle pass.  Each leaf (real or
 * stub) bumps its own counter; the driver itself bumps
 * `g_count_middle` (already declared above).
 *
 * As of chip 5a.ii.a, build/sort/render have real bodies; they still
 * bump their counters once per dispatch for cross-check instrumentation
 * (`g_middle_build_count` etc.). */
static int g_middle_build_count;
static int g_middle_sort_count;
static int g_middle_render_count;
static int g_middle_player_marker_count;
static int g_middle_unit_key_count;
static int g_middle_effect_chain_count;

/* Last byte the unit-key / effect-chain stubs were called with —
 * descriptor[+3] / descriptor[+6] of the most recent dispatch.  -1
 * means "no call yet" (matches the reset value). */
static int g_middle_unit_key_last_arg     = -1;
static int g_middle_effect_chain_last_arg = -1;

/* Entity table binding (chip 5a.ii.a).  `g_entity_base` points to the
 * start of entity 0's record; entity[id] is at
 * `g_entity_base + id * g_entity_stride`.  Retail uses an absolute
 * address (`0x00554480`) — we accept a runtime pointer so tests can
 * stand up fixtures.  NULL ⇒ visible list stays empty. */
static uint8_t *g_entity_base;
static int      g_entity_stride;
static int      g_entity_max_id;

/* Visible-entity list (retail DAT_004e5d80 / DAT_004e5d82 / DAT_004e5d38).
 * In retail the (id, y) pairs are interleaved as 4-byte cells in a
 * single block; for the port we use two parallel arrays since the
 * layout isn't observable outside the module.  Capacity is one past the
 * retail max_id (0x50) to accommodate the in-band sentinel that the
 * build helper writes at index `count`. */
#define VISIBLE_ENTITY_CAPACITY 0x52
static int16_t g_visible_entity_id[VISIBLE_ENTITY_CAPACITY];
static int16_t g_visible_entity_y [VISIBLE_ENTITY_CAPACITY];
static int     g_visible_entity_count;          /* DAT_004e5d38 */
static int     g_middle_sort_passes;            /* _DAT_004e655c */

/* Per-entity draw capture (chip 5a.ii.a + 5a.ii.b).  After chip
 * 5a.ii.b the per-entity draw is a real port; these counters stay as
 * cross-check state.  `last_px_x` / `last_px_y` capture the ENTRY
 * cursor — i.e. the value the render loop staged, before the per-entity
 * draw applies any anim-offset / type-9-Y / clip adjustments. */
static int g_middle_per_entity_count;
static int g_middle_per_entity_last_id     = -1;
static int g_middle_per_entity_last_px_x   = 0;
static int g_middle_per_entity_last_px_y   = 0;

/* Per-entity blit-dispatch counter (chip 5a.ii.b).  Bumped each time
 * the body actually calls gfx_dispatch_dispatch_sprite_rle (after
 * the glyph-offset gate + the y_clip_mode != 5 gate). */
static int g_middle_entity_blit_count;

/* Per-entity TYPE-SPECIAL stubs (chip 5a.ii.b).  Each captures a count
 * and the last entity id passed.  Chip 5a.iv ports the real bodies. */
static int g_middle_entity_type6_count;
static int g_middle_entity_type6_last_id = -1;
static int g_middle_entity_type7_count;
static int g_middle_entity_type7_last_id = -1;
static int g_middle_entity_type9_sub_count;
static int g_middle_entity_type9_sub_last_id = -1;

/* No-blob counter (chip 5a.ii.b).  Entities with NULL primary sprite
 * blob skip the main draw body.  Retail logs via FUN_004afab9 — we
 * count instead. */
static int g_middle_entity_no_blob_count;
static int g_middle_entity_no_blob_last_id = -1;

/* Anim-offset tables (chip 5a.ii.b).  Retail DAT_004e3bf0 (z16),
 * DAT_004e4030 (z32). */
static const uint8_t *g_anim_table_z16;
static const uint8_t *g_anim_table_z32;

/* Dirty-rect state (chip 5a.ii.b).  NULL ⇒ skip the per-entity 5×5 /
 * 9×9 box mark. */
static engine_dirty_rect_state_t *g_dirty_state;

/* Per-pass call counters.  See the header for what each one
 * represents.  All seven pass entry points (5 stubs + 1 real no-op
 * + 1 implicitly the frame driver) bump exactly one counter per
 * dispatch.  Test-only — not part of retail. */
static int g_count_terrain_zoom16;  /* FUN_004bc8fc */
static int g_count_terrain_zoom32;  /* FUN_004bcbdc */
static int g_count_middle;          /* FUN_004bd355 */
static int g_count_overlay_zoom16;  /* FUN_004bcbd1 (real no-op port) */
static int g_count_overlay_zoom32;  /* FUN_004bd07f */
static int g_count_late;            /* FUN_004bd233 */
static int g_count_final;           /* FUN_004bf1f9 */

/* ──────────────────────────────────────────────────────────────────
 * Bind / config setters
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_set_zoom(int zoom)                  { g_zoom = zoom; }
int  engine_battlefield_render_zoom(void)                          { return g_zoom; }

void engine_battlefield_render_set_force_redraw(int counter)       { g_force_redraw = counter; }
int  engine_battlefield_render_force_redraw(void)                  { return g_force_redraw; }

int  engine_battlefield_render_camera_x(void)                      { return g_camera_x; }
int  engine_battlefield_render_camera_y(void)                      { return g_camera_y; }

void engine_battlefield_render_bind_descriptor_table(uint8_t *base, int stride)
{
    g_desc_base   = base;
    g_desc_stride = stride;
}

void engine_battlefield_render_bind_grid(int cols, int rows, int row_wrap)
{
    g_grid_cols     = cols;
    g_grid_rows     = rows;
    g_grid_row_wrap = row_wrap;
}

void engine_battlefield_render_bind_viewport_offset(int x, int y)
{
    g_viewport_x = x;
    g_viewport_y = y;
}

void engine_battlefield_render_bind_camera_row_stride(int row_stride)
{
    g_camera_row_stride = row_stride;
}

void engine_battlefield_render_bind_total_rows(int total_rows)
{
    g_total_rows = total_rows;
}

void engine_battlefield_render_bind_blobs(const uint8_t *primary, const uint8_t *alt)
{
    g_blob_primary = primary;
    g_blob_alt     = alt;
}

void engine_battlefield_render_bind_cell_band_width(int band_width)
{
    g_cell_band_width = band_width;
}

void engine_battlefield_render_bind_clip_rect(int left, int top, int right, int bottom)
{
    g_clip_left   = left;
    g_clip_top    = top;
    g_clip_right  = right;
    g_clip_bottom = bottom;
}

void engine_battlefield_render_bind_entity_table(uint8_t *base, int stride, int max_id)
{
    g_entity_base   = base;
    g_entity_stride = stride;
    g_entity_max_id = max_id;
}

void engine_battlefield_render_bind_anim_offset_tables(const uint8_t *z16,
                                                       const uint8_t *z32)
{
    g_anim_table_z16 = z16;
    g_anim_table_z32 = z32;
}

void engine_battlefield_render_set_dirty_state(engine_dirty_rect_state_t *state)
{
    g_dirty_state = state;
}

int engine_battlefield_render_terrain_blit_count(void)
{
    return g_terrain_blit_count;
}

int engine_battlefield_render_terrain_zoom32_first_blit_count(void)
{
    return g_terrain_z32_first_blit_count;
}

int engine_battlefield_render_terrain_zoom32_second_blit_count(void)
{
    return g_terrain_z32_second_blit_count;
}

int engine_battlefield_render_overlay_zoom32_blit_count(void)
{
    return g_overlay_z32_blit_count;
}

/* ──────────────────────────────────────────────────────────────────
 * Per-pass stubs.
 *
 * Each stub stands in for a retail pass function that has not been
 * ported yet.  All they do is bump a per-pass call counter so the
 * frame driver's dispatch shape can be exercised by unit tests.
 *
 * `pass_overlay_zoom16` is the one stub that is also a faithful port
 * of the retail body — FUN_004bcbd1's retail decompilation is
 * literally `return;`.  It still bumps a counter for dispatch
 * verification, which is a test-only embellishment over retail.
 * ────────────────────────────────────────────────────────────────── */

static void pass_overlay_zoom16(void)   { g_count_overlay_zoom16 += 1; }
static void pass_late(void)             { g_count_late += 1; }
static void pass_final(void)            { g_count_final += 1; }

/* ──────────────────────────────────────────────────────────────────
 * Middle-pass leaves.
 *
 * Six retail functions invoked by the middle pass driver.  As of chip
 * 5a.ii.b, build/sort/render AND the per-entity draw (FUN_004bdc31)
 * are real ports.  The three TYPE-SPECIAL leaves dispatched FROM
 * FUN_004bdc31 (FUN_004be4df type-6 pre-call, FUN_004be7be type-7
 * post-call, FUN_004beab9 type-9-subtype-0xe post-call) remain
 * counter-stubs for chip 5a.iv.  The three PER-CELL draw leaves
 * (FUN_004bd574 / FUN_004bd759 / FUN_004beed4) remain counter-stubs
 * for chip 5a.iii.
 *
 * Each real port also bumps its per-leaf counter for cross-check
 * instrumentation — once per dispatch, regardless of how many entities
 * the body processes.
 * ────────────────────────────────────────────────────────────────── */

/* Per-entity TYPE-SPECIAL stubs (chip 5a.iv ports the real bodies). */
static void entity_type6_pre_stub(int id)
{
    g_middle_entity_type6_count   += 1;
    g_middle_entity_type6_last_id  = id;
}

static void entity_type7_post_stub(int id)
{
    g_middle_entity_type7_count   += 1;
    g_middle_entity_type7_last_id  = id;
}

static void entity_type9_subtype_e_post_stub(int id)
{
    g_middle_entity_type9_sub_count   += 1;
    g_middle_entity_type9_sub_last_id  = id;
}

/* No-blob path stub.  Retail calls FUN_004afab9 (a debug logger) when
 * an entity's primary blob is NULL; the port counts the call instead.
 * The string at retail's 0x4e44f0 is presumably "<entity> has no
 * sprite blob, type %d" or similar — irrelevant for behavioral
 * fidelity. */
static void entity_no_blob_stub(int id)
{
    g_middle_entity_no_blob_count   += 1;
    g_middle_entity_no_blob_last_id  = id;
}

/* Read a 32-bit little-endian int from a byte table at the given byte
 * offset.  memcpy is the strict-aliasing-safe path; on the host x86 it
 * compiles to a single mov. */
static int read_le32(const uint8_t *base, int byte_offset)
{
    int32_t v;
    memcpy(&v, base + byte_offset, sizeof v);
    return (int)v;
}

/* FUN_004bdc31 (1087 B) — per-entity draw (base path).
 *
 * Top-down shape:
 *
 *   1. If entity type == 6: type-6 PRE-call (stub for chip 5a.ii.b).
 *   2. Save the entry cursor (this is also the value the render loop
 *      staged + the value captured by g_middle_per_entity_last_px_*).
 *   3. Read entity's primary sprite blob ptr.  If NULL: no-blob stub
 *      and RETURN — skips both the main draw and the type-7 / type-9
 *      post-calls.  Type-6 pre-call ALREADY fired above.
 *   4. Stage sprite header from blob: (width, h_short, glyph_offset,
 *      flag_a, row_count, flag_b) — same layout as terrain / overlay
 *      passes.
 *   5. Glyph-offset gate: `[1, 0x4baf0]` — out-of-band aborts the
 *      whole drawing block, skipping the main draw AND the type-7 /
 *      type-9 post-calls.  The type-6 pre-call still fired.
 *   6. Anim-offset lookup based on zoom.  Reads (x_off, y_off) from
 *      the bound anim table at offset `facing * 0x88 + frame * 8`,
 *      using SIGNED 8-bit facing / frame indices from the entity
 *      record.  Adds a sprite-width-based centering adjustment.  At
 *      unsupported zoom levels (anything other than 0x10 / 0x20) the
 *      offsets stay at 0 (deterministic — retail reads uninitialized
 *      locals here, but the game never exercises this path).
 *   7. Apply offsets to cursor.  Then type-9 Y bonus (+12) or type-10
 *      Y bonus (+8) AFTER the anim offset — both bonuses are
 *      zoom-independent.
 *   8. Mark dirty rect: 5×5 cells for type < 7, 9×9 for type ≥ 7,
 *      at the FULLY-ADJUSTED cursor.  Skipped if no dirty_state bound.
 *   9. Set clip rect.  Dispatch RLE blit (with y_clip_mode != 5 gate
 *      applied inside the dispatcher).
 *  10. Restore the entry cursor (so subsequent entities draw at the
 *      render-loop-computed anchor, not this entity's adjusted anchor).
 *  11. Type-7 POST-call: if type == 7 (stub).
 *  12. Type-9 + subtype-0x0e POST-call: if type == 9 AND
 *      entity[+0x31] == 0x0e (stub). */
static void middle_leaf_draw_entity(int id)
{
    g_middle_per_entity_count    += 1;
    g_middle_per_entity_last_id   = id;
    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();
    g_middle_per_entity_last_px_x = entry_x;
    g_middle_per_entity_last_px_y = entry_y;

    uint8_t *rec = g_entity_base + id * g_entity_stride;
    uint8_t type = rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE];

    /* (1) Type-6 PRE-call.  Fires regardless of blob / glyph-offset
     * gates — it's outside both. */
    if (type == 6) {
        entity_type6_pre_stub(id);
    }

    /* (3) Primary blob lookup.  Read as a host-width pointer from the
     * entity record's +0x00 field.  Retail's record is 32-bit and the
     * field is a 32-bit pointer; on 64-bit hosts (where this code is
     * tested) the field is 8 bytes — the entity stride (0x1b0) has
     * plenty of slack for the extra 4 bytes.  NULL ⇒ stub + early
     * return; the cursor wasn't modified yet so no restore is needed. */
    const uint8_t *blob;
    memcpy(&blob,
           rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB,
           sizeof blob);
    if (blob == NULL) {
        entity_no_blob_stub(id);
        return;
    }

    /* (4) Stage sprite header.  Matches the terrain / overlay
     * dispatcher: 16-byte slot per sprite, header at slot + 8. */
    uint8_t frame_idx = rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_FRAME];
    gfx_dispatch_set_last_glyph_index((int)frame_idx);
    gfx_dispatch_stage_sprite_descriptor(blob, (int)frame_idx);

    /* (5) Glyph-offset gate.  Both ends inclusive on the [1, 0x4baf0]
     * band; out-of-band aborts the whole drawing block.  Type-6 fired
     * above; type-7 / type-9 post-calls do NOT fire on this path. */
    int glyph_off = gfx_glyph_glyph_offset();
    if (glyph_off < 1 || glyph_off > 0x4baf0) {
        return;
    }

    /* (6) Anim-offset lookup.  Facing and frame are SIGNED 8-bit
     * values in retail (`(char)` cast in the decompile) — negative
     * values index BACKWARDS into the anim table.  Preserve verbatim. */
    int8_t facing = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING];
    int8_t af     = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME];
    int    sprite_w = gfx_glyph_glyph_w();
    int    half_w   = sprite_w >> 1;

    int off_x = 0;
    int off_y = 0;
    int entry_off = (int)facing * ENGINE_BATTLEFIELD_ANIM_FACING_STRIDE
                  + (int)af     * ENGINE_BATTLEFIELD_ANIM_FRAME_STRIDE;
    if (g_zoom == 0x20 && g_anim_table_z32) {
        off_x = read_le32(g_anim_table_z32, entry_off + 0) + (16 - half_w);
        off_y = read_le32(g_anim_table_z32, entry_off + 4) - half_w + 8;
    } else if (g_zoom == 0x10 && g_anim_table_z16) {
        off_x = read_le32(g_anim_table_z16, entry_off + 0) + (8  - half_w);
        off_y = read_le32(g_anim_table_z16, entry_off + 4) - half_w + 4;
    }
    /* else: unsupported zoom or unbound table — offsets stay at 0. */

    /* (7) Apply anim offsets + type-9 / type-10 Y bonus.  Bonuses
     * are added AFTER the anim offset and are zoom-independent. */
    int new_x = entry_x + off_x;
    int new_y = entry_y + off_y;
    if (type == 9) {
        new_y += 0xc;
    } else if (type == 10) {
        new_y += 8;
    }
    gfx_glyph_set_cursor_x(new_x);
    gfx_glyph_set_cursor_y(new_y);

    /* (8) Dirty-rect mark: 5×5 for type < 7, 9×9 for type ≥ 7.
     * Escalation flag = 2 (forces full-repaint behaviour in the dirty
     * box helper). */
    if (g_dirty_state) {
        int box = (type < 7) ? 5 : 9;
        engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                         new_x, new_y,
                                         box, box,
                                         /*force_full=*/2);
    }

    /* (9) Set clip + dispatch RLE blit. */
    gfx_dispatch_clip_x(g_clip_left, g_clip_right);
    gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

    /* Track whether the dispatcher actually blits (gated by
     * y_clip_mode != 5 inside the helper). */
    int y_mode_before = gfx_dispatch_clip_y_mode();
    gfx_dispatch_dispatch_sprite_rle(blob);
    if (y_mode_before != 5) {
        g_middle_entity_blit_count += 1;
    }

    /* (10) Restore entry cursor. */
    gfx_glyph_set_cursor_x(entry_x);
    gfx_glyph_set_cursor_y(entry_y);

    /* (11)/(12) Type-7 / type-9-subtype-0xe POST-calls. */
    if (type == 7) {
        entity_type7_post_stub(id);
    }
    if (type == 9
        && rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SUBTYPE] == 0x0e) {
        entity_type9_subtype_e_post_stub(id);
    }
}

/* FUN_004bd938 (346 B) — visible-entity list build.
 *
 * Walks the entity table for ids 1..max_id, including each alive entity
 * whose hex coords fall in the over-scan window centered on the camera.
 * Output is written to g_visible_entity_{id,y}[0..count-1]; an in-band
 * sentinel (id=0, y=9999) is written at index `count`, used by the
 * sort helper as a guaranteed-max comparand on the right edge of the
 * sort's inner loop.
 *
 * Filter window matches the middle pass's cell-loop over-scan extent:
 *
 *   ex ∈ [cam_x - 1, cam_x + cols]   (inclusive both ends)
 *   ey ∈ [cam_y - 1, cam_y + rows]   (inclusive both ends)
 *
 * Retail bounds:
 *   iVar1 = DAT_004e657c - 1      (cam_x - 1)
 *   iVar2 = DAT_004e5d5c + DAT_004e657c  (cols + cam_x)
 *   iVar3 = DAT_004e6580 - 1      (cam_y - 1)
 *   iVar4 = DAT_004e5d4c + DAT_004e6580  (rows + cam_y)
 *
 * The build helper does NOT clamp the over-scan window against the
 * underlying grid extent (the cell loop does — see chip 5a.i).
 * Entities outside the grid can't exist in practice, so the missing
 * clamp is observably equivalent here. */
static void middle_leaf_build_entity_list(void)
{
    g_middle_build_count += 1;
    g_visible_entity_count = 0;

    if (!g_entity_base) {
        /* Sentinel still gets written so the sort helper's right-edge
         * read sees a defined comparand. */
        g_visible_entity_id[0] = 0;
        g_visible_entity_y [0] = 9999;
        return;
    }

    int x_lo = g_camera_x - 1;
    int x_hi = g_grid_cols + g_camera_x;
    int y_lo = g_camera_y - 1;
    int y_hi = g_grid_rows + g_camera_y;

    for (int id = 1; id <= g_entity_max_id; id++) {
        uint8_t *rec = g_entity_base + id * g_entity_stride;

        if (rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ALIVE] == 0) {
            continue;
        }

        int16_t ex = *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_X);
        int16_t ey = *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y);

        if (ex < x_lo || ex > x_hi || ey < y_lo || ey > y_hi) {
            continue;
        }

        /* Capacity guard — retail relies on max_id ≤ 0x50 fitting in
         * the buffer; our VISIBLE_ENTITY_CAPACITY (0x52) accommodates
         * max_id + sentinel.  Tests with smaller max_id always fit. */
        if (g_visible_entity_count >= VISIBLE_ENTITY_CAPACITY - 1) {
            break;
        }

        g_visible_entity_id[g_visible_entity_count] = (int16_t)id;
        g_visible_entity_y [g_visible_entity_count] = ey;
        g_visible_entity_count += 1;
    }

    g_visible_entity_y [g_visible_entity_count] = 9999;
    g_visible_entity_id[g_visible_entity_count] = 0;
}

/* FUN_004bda92 (259 B) — bubble sort by y ascending, with last-swap
 * early termination and a 500-iter outer safety cap.
 *
 * The inner loop reads y[i+1] for i = 0..high-1; on the final pass with
 * `high = count - 1`, the read at i = count-1 dereferences the sentinel
 * (y=9999, id=0) — which is always ≥ any real y, so no swap occurs at
 * the sentinel boundary and the sort behaves identically with or
 * without it.
 *
 * `last-swap early termination`: each outer pass tracks the largest
 * index where a swap actually happened; the next outer pass only needs
 * to walk that far.  A pass with no swap leaves `last = 0`, terminating
 * the loop early via the `0 < local_1c` predicate.
 *
 * The 500-iter outer cap is defensive — with max_id ≤ 80 the worst case
 * is 79 passes, well under the cap.  We preserve the cap verbatim to
 * match retail's behavior on any future regression. */
static void middle_leaf_sort_entity_list(void)
{
    g_middle_sort_count += 1;
    g_middle_sort_passes = 0;

    int high      = g_visible_entity_count - 1;
    int safety    = 500;

    while (high > 0 && safety > 0) {
        safety -= 1;
        g_middle_sort_passes += 1;

        int last_swap = 0;
        for (int i = 0; i < high; i++) {
            if (g_visible_entity_y[i + 1] < g_visible_entity_y[i]) {
                last_swap = i;
                int16_t y_tmp  = g_visible_entity_y[i];
                int16_t id_tmp = g_visible_entity_id[i];
                g_visible_entity_y [i] = g_visible_entity_y [i + 1];
                g_visible_entity_id[i] = g_visible_entity_id[i + 1];
                g_visible_entity_y [i + 1] = y_tmp;
                g_visible_entity_id[i + 1] = id_tmp;
            }
        }
        high = last_swap;
    }
}

/* FUN_004bdb95 (156 B) — per-entity render loop.
 *
 * Walks the sorted visible-entity list, computes the per-entity
 * pixel-space anchor (px_x, px_y) from each entity's hex coords +
 * camera + viewport + zoom, stages it as the cursor, and dispatches
 * the per-entity draw (FUN_004bdc31 in retail; the stub in chip
 * 5a.ii.a).
 *
 *   px_x = (entity_x - cam_x) * zoom + viewport_x
 *   px_y = (entity_y - cam_y) * zoom + viewport_y
 *
 * Retail writes DAT_00591524 / DAT_00591528 (cursor_x/y) directly; we
 * stage through gfx_glyph_set_cursor_*.  The cursor write happens
 * BEFORE the per-entity draw call, so the stub sees the staged value
 * via gfx_glyph_cursor_x/y(). */
static void middle_leaf_render_entity_list(void)
{
    g_middle_render_count += 1;

    if (!g_entity_base) {
        /* No entity table bound — sorted list is empty. */
        return;
    }

    for (int i = 0; i < g_visible_entity_count; i++) {
        int id = (int)g_visible_entity_id[i];
        uint8_t *rec = g_entity_base + id * g_entity_stride;

        int16_t ex = *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_X);
        int16_t ey = *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y);

        int px_x = (ex - g_camera_x) * g_zoom + g_viewport_x;
        int px_y = (ey - g_camera_y) * g_zoom + g_viewport_y;

        gfx_glyph_set_cursor_x(px_x);
        gfx_glyph_set_cursor_y(px_y);

        middle_leaf_draw_entity(id);
    }
}

static void middle_leaf_draw_player_marker(void)
{
    g_middle_player_marker_count += 1;
}

static void middle_leaf_draw_unit_key(uint8_t key)
{
    g_middle_unit_key_count += 1;
    g_middle_unit_key_last_arg = key;
}

static void middle_leaf_draw_effect_chain(uint8_t head)
{
    g_middle_effect_chain_count += 1;
    g_middle_effect_chain_last_arg = head;
}

/* ──────────────────────────────────────────────────────────────────
 * Terrain zoom-16 pass — port of FUN_004bc8fc @ 0x4bc8fc (725 B).
 *
 * Nested row/col loop over the visible grid.  For each cell:
 *
 *   1. Read flag byte at descriptor[+2].
 *   2. If force-redraw counter non-zero OR dirty bit set:
 *      a. Clear dirty bit on the descriptor.
 *      b. Read sprite index from descriptor[+3].
 *      c. Pick blob:  (flags & 0x1c) == 0 → primary, == 4 → alt,
 *         anything else → no blit dispatch (still mark + advance).
 *      d. Stage cursor at (viewport_x + col*0x10, viewport_y + row*0x10).
 *      e. Stage sprite metadata via gfx_dispatch_stage_sprite_descriptor.
 *      f. Stage the last-glyph-index global (matches retail's direct
 *         DAT_005c9288 = sprite_idx write).
 *      g. Dispatch 16×16 raw blit.
 *   3. Boundary marking: if cell is at any grid edge (first/last row
 *      or first/last col), OR bit 0x02 into the flags byte.  This
 *      forces re-paint of edge cells next frame.
 *   4. Advance descriptor cursor by `descriptor_stride`.
 *   5. End of row: advance descriptor cursor by `row_wrap`.
 *
 * The cursor (gfx_glyph cursor_x/y) is staged per-cell.  Cursor X
 * resets to `viewport_x` at the start of each row and advances by
 * 0x10 (16 pixels) per cell.  Cursor Y starts at `viewport_y` and
 * advances by 0x10 per row.
 * ────────────────────────────────────────────────────────────────── */

static void pass_terrain_zoom16(void)
{
    g_count_terrain_zoom16 += 1;

    if (!g_desc_base) {
        /* No descriptor table bound — nothing to iterate.  Retail
         * would dereference a NULL global here; in our port we just
         * short-circuit so tests can exercise the frame driver
         * without binding the full state. */
        return;
    }

    /* Per-frame state initialization.  Retail:
     *   DAT_004e6588 = (cam_x + cam_y * row_stride) * desc_stride;
     *   _DAT_004e5d64 = 0;
     *   DAT_00591528 = DAT_004e6554;  (cursor_y = viewport_y)
     */
    g_desc_cursor_offset = (g_camera_x + g_camera_y * g_camera_row_stride)
                         * g_desc_stride;
    g_per_frame_cell_counter = 0;
    int cursor_y = g_viewport_y;

    for (int row = 0; row < g_grid_rows; row++) {
        /* Retail: DAT_00591524 = DAT_004e6558; (cursor_x = viewport_x) */
        int cursor_x = g_viewport_x;

        for (int col = 0; col < g_grid_cols; col++) {
            uint8_t *flag_ptr = &g_desc_base[g_desc_cursor_offset + 2];
            uint8_t  flags    = *flag_ptr;

            if (g_force_redraw != 0 || (flags & 0x01) != 0) {
                /* Clear dirty bit; the cell will be redrawn now. */
                *flag_ptr = flags & 0xfe;

                uint8_t sprite_idx = g_desc_base[g_desc_cursor_offset + 3];
                uint8_t blob_sel   = flags & 0x1c;

                const uint8_t *blob = NULL;
                if (blob_sel == 0) {
                    blob = g_blob_primary;
                } else if (blob_sel == 4) {
                    blob = g_blob_alt;
                }
                /* No fall-through default: any other selector skips
                 * dispatch entirely, matching retail's strict if/else-if
                 * chain. */

                if (blob) {
                    /* Stage cursor — retail writes DAT_00591524 /
                     * DAT_00591528 inline; our equivalent is gfx_glyph's
                     * cursor_x / cursor_y setters. */
                    gfx_glyph_set_cursor_x(cursor_x);
                    gfx_glyph_set_cursor_y(cursor_y);

                    /* Stage the last-glyph-index global — retail's
                     * DAT_005c9288 = sprite_idx direct write. */
                    gfx_dispatch_set_last_glyph_index(sprite_idx);

                    /* Stage the 6-field sprite metadata from the blob
                     * header at slot `sprite_idx`.  Inlined in retail
                     * (six direct DAT_* writes); functionally identical
                     * to the dispatcher's stage helper which reads the
                     * same 16-byte slot layout at +0xc..+0xe. */
                    gfx_dispatch_stage_sprite_descriptor(blob, sprite_idx);

                    /* Dispatch the raw 16×16 blit.  Reads cursor +
                     * glyph_offset from gfx_glyph; reads the source
                     * bytes from `blob + glyph_offset`. */
                    gfx_sprite_blit_16x16_raw(blob);
                    g_terrain_blit_count += 1;
                }
            }

            /* Boundary marking — any cell at the grid edge gets bit
             * 0x02 set on its flags byte.  Retail's chain of four
             * mutually-exclusive if/else-if blocks (col==0, row==0,
             * col==cols-1, row==rows-1) all write the same value;
             * we collapse to one logical OR.  Note: the assignment
             * reads the post-clear flag byte, so the boundary bit
             * coexists with the cleared dirty bit. */
            if (col == 0 || row == 0
                || col == g_grid_cols - 1
                || row == g_grid_rows - 1) {
                g_desc_base[g_desc_cursor_offset + 2] |= 0x02;
            }

            /* Advance descriptor cursor + per-cell counter + cursor_x. */
            g_desc_cursor_offset    += g_desc_stride;
            g_per_frame_cell_counter += 1;
            cursor_x                += 0x10;
        }

        /* End-of-row: skip past off-screen cells to next visible row's
         * first cell, and advance cursor_y. */
        g_desc_cursor_offset += g_grid_row_wrap;
        cursor_y             += 0x10;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Terrain zoom-32 pass — port of FUN_004bcbdc @ 0x4bcbdc (1187 B).
 *
 * Sibling of pass_terrain_zoom16 (FUN_004bc8fc) at twice the cell
 * pitch (0x20 px) and with two additions:
 *
 *   1. EARLY-SKIP gate on descriptor[+1] & 4.  A dirty (or forced)
 *      cell that has this bit set is silently dropped: NO drawing
 *      AND NO boundary marking — the cell advances without touching
 *      anything.  Clean cells (not dirty AND not forced) still get
 *      their boundary marking.  This is a NEW behavior compared to
 *      zoom-16, where every cell unconditionally got boundary marking.
 *
 *   2. SECOND sprite-per-cell, gated on descriptor[+0] != 0.  The
 *      second sprite uses the CLIPPED blit family — runs through
 *      gfx_dispatch_clip_x / clip_y / apply_skip_and_dispatch_sprite —
 *      against the bound clip rect, and always picks from the ALT blob
 *      (regardless of the per-cell blob selector that picked the FIRST
 *      sprite).  Sprite index is descriptor[+0] + 0x8b, clamped at
 *      0x9a when descriptor[+0] >= 0x10.  The mode byte at
 *      descriptor[+4] gates the dispatch: only values 1..3 are valid;
 *      0 or > 3 aborts the cell entirely (skips remaining drawing AND
 *      skips boundary marking — matches retail's `goto LAB_004bcc53`).
 *
 * Both sprites also share a `[1, 0x4baf0]` glyph_offset gate: if the
 * staged src offset falls outside that band, the cell aborts in the
 * same way (goto LAB_004bcc53 — skips boundary marking AND skips the
 * second sprite path).  This is the retail "bad blob slot" failsafe.
 *
 * Per-cell pixel pitch is 0x20 (32 px), so cursor_x / cursor_y advance
 * by 0x20 per cell / per row, matching the 32×32 sprite size.
 *
 * Note: unlike zoom-16, this pass does NOT increment a per-cell
 * counter analogue (retail's _DAT_004e5d64) — it's just not present
 * in FUN_004bcbdc.
 * ────────────────────────────────────────────────────────────────── */

static void pass_terrain_zoom32(void)
{
    g_count_terrain_zoom32 += 1;

    if (!g_desc_base) {
        return;
    }

    g_desc_cursor_offset = (g_camera_x + g_camera_y * g_camera_row_stride)
                         * g_desc_stride;
    int cursor_y = g_viewport_y;

    for (int row = 0; row < g_grid_rows; row++) {
        int cursor_x = g_viewport_x;

        for (int col = 0; col < g_grid_cols; col++) {
            uint8_t *desc     = &g_desc_base[g_desc_cursor_offset];
            uint8_t  flags    = desc[2];

            int do_boundary = 0;

            if (g_force_redraw == 0 && (flags & 0x01) == 0) {
                /* CLEAN cell — fall straight to boundary marking. */
                do_boundary = 1;
            } else if ((desc[1] & 0x04) == 0) {
                /* DIRTY (or forced) cell, skip-bit clear → drawing path.
                 *
                 * Note on control flow: every premature exit inside this
                 * block (the `[1, 0x4baf0]` glyph_offset gate, the second-
                 * sprite mode-byte gate) jumps STRAIGHT to the per-cell
                 * advance, SKIPPING boundary marking.  We mirror that
                 * with an early `continue` after the cell advance below
                 * — controlled by a local `abort` flag instead of goto. */

                int abort_cell = 0;

                /* Clear dirty bit. */
                desc[2] = (uint8_t)(flags & 0xfe);

                int sprite_idx = desc[3];
                uint8_t bVar1  = desc[0];   /* second-sprite chooser */
                uint8_t blob_sel = (uint8_t)(flags & 0x1c);

                /* First sprite — 32×32 raw blit, picks blob by
                 * (flags & 0x1c).  No fall-through default (matches
                 * the zoom-16 sibling's quirk #76). */
                const uint8_t *first_blob = NULL;
                if (blob_sel == 0) {
                    first_blob = g_blob_primary;
                } else if (blob_sel == 4) {
                    first_blob = g_blob_alt;
                }

                if (first_blob) {
                    /* Stage cursor + sprite metadata for the FIRST blit. */
                    gfx_glyph_set_cursor_x(cursor_x);
                    gfx_glyph_set_cursor_y(cursor_y);
                    gfx_dispatch_set_last_glyph_index(sprite_idx);
                    gfx_dispatch_stage_sprite_descriptor(first_blob, sprite_idx);

                    /* [1, 0x4baf0] glyph_offset gate — see the quirk on
                     * the bad-blob-slot failsafe.  An out-of-band offset
                     * aborts the entire cell (skips boundary + second
                     * sprite).  Retail constant is 0x4baf0 = 309488; the
                     * Ghidra display rounds it to 310000 which is wrong
                     * by 512.  Confirmed via r2 at 0x4bcd76. */
                    int glyph_off = gfx_glyph_glyph_offset();
                    if (glyph_off > 0x4baf0 || glyph_off < 1) {
                        abort_cell = 1;
                    } else {
                        gfx_sprite_blit_32x32_raw(first_blob);
                        g_terrain_z32_first_blit_count += 1;
                    }
                }
                /* else: no first blob match — STILL run the second-sprite
                 * path; retail's strict `if/else-if` for the first sprite
                 * just falls through, and the second-sprite gate is
                 * independent (`if (bVar1 != 0) ...`). */

                /* Second sprite — clipped blit from the alt blob, gated
                 * on descriptor[+0] != 0 AND descriptor[+4] ∈ {1, 2, 3}. */
                if (!abort_cell && bVar1 != 0) {
                    uint8_t mode = desc[4];
                    if (mode == 0 || mode > 3) {
                        abort_cell = 1;
                    } else {
                        /* Pick second-sprite slot: bVar1 in [1, 0x10)
                         * maps to [0x8c, 0x9b); bVar1 >= 0x10 clamps
                         * to 0x9a.  Slot 0x9a is the "default overlay"
                         * sprite; the lower band is per-byte alternates. */
                        int second_idx;
                        if (bVar1 < 0x10) {
                            second_idx = (int)bVar1 + 0x8b;
                        } else {
                            second_idx = 0x9a;
                        }

                        gfx_glyph_set_cursor_x(cursor_x);
                        gfx_glyph_set_cursor_y(cursor_y);
                        gfx_dispatch_set_last_glyph_index(second_idx);
                        gfx_dispatch_stage_sprite_descriptor(g_blob_alt, second_idx);

                        /* Set the clip — clip_x sets x-mode tag,
                         * left_skip + clipped_w; clip_y sets y-mode
                         * tag, top_skip + row_count.  Both consume
                         * the staged cursor + width/height. */
                        gfx_dispatch_clip_x(g_clip_left, g_clip_right);
                        gfx_dispatch_clip_y(g_clip_top, g_clip_bottom);

                        /* Bake top_skip * width + left_skip into the
                         * staged glyph_offset, then dispatch one of
                         * the three sprite blitters based on x_clip_mode.
                         * Gated on y_clip_mode != 5 (i.e. visible). */
                        int y_mode_before = gfx_dispatch_clip_y_mode();
                        gfx_dispatch_apply_skip_and_dispatch_sprite(g_blob_alt);
                        if (y_mode_before != 5) {
                            g_terrain_z32_second_blit_count += 1;
                        }
                    }
                }

                if (!abort_cell) {
                    do_boundary = 1;
                }
                /* else: drawing-path abort — skip boundary marking. */
            }
            /* else: DIRTY + skip-bit set → no drawing, no boundary. */

            if (do_boundary) {
                if (col == 0 || row == 0
                    || col == g_grid_cols - 1
                    || row == g_grid_rows - 1) {
                    g_desc_base[g_desc_cursor_offset + 2] |= 0x02;
                }
            }

            g_desc_cursor_offset += g_desc_stride;
            cursor_x             += 0x20;
        }

        g_desc_cursor_offset += g_grid_row_wrap;
        cursor_y             += 0x20;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Overlay zoom-32 pass — port of FUN_004bd07f @ 0x4bd07f (436 B).
 *
 * Second-layer pass that runs after middle pass, before late/final.
 * Per cell, drawing requires BOTH:
 *
 *   (a) (force-redraw != 0) OR (descriptor[+2] & 0x01)  — dirty/forced
 *   (b) (descriptor[+1] & 0x04)                          — layer bit
 *
 * (b) is the SAME bit that terrain zoom-32 uses as its EARLY-SKIP
 * gate.  Terrain only draws when (b) is CLEAR; overlay only draws when
 * (b) is SET.  Net effect: per cell, at most ONE of the two passes
 * runs the descriptor-mutating drawing block per frame.  The dirty bit
 * is cleared exactly once (by whichever pass actually drew).  A cell
 * with the layer bit set but currently clean has its dirty bit
 * untouched by either pass — same fate as a normal clean cell in
 * terrain-z32.
 *
 * Always reads from PRIMARY blob (retail DAT_004e5d78); the alt blob
 * is not consulted here.  No (flags & 0x1c) selector.
 *
 * Same [1, 0x4baf0] glyph_offset gate as terrain-z32.  Behavior on an
 * out-of-band offset differs in a subtle way from chip 4: here the
 * gate sits AFTER the dirty-clear but the pass has no boundary marking
 * or second-sprite path to skip, so the only observable side-effect
 * of an aborted draw is "dirty cleared but no pixels written".
 *
 * No boundary marking.  No per-cell counter analog of zoom-16's
 * _DAT_004e5d64.  Cell pitch 32 px (cursor_x/y advance by 0x20).
 * ────────────────────────────────────────────────────────────────── */

static void pass_overlay_zoom32(void)
{
    g_count_overlay_zoom32 += 1;

    if (!g_desc_base) {
        return;
    }

    g_desc_cursor_offset = (g_camera_x + g_camera_y * g_camera_row_stride)
                         * g_desc_stride;
    int cursor_y = g_viewport_y;

    for (int row = 0; row < g_grid_rows; row++) {
        int cursor_x = g_viewport_x;

        for (int col = 0; col < g_grid_cols; col++) {
            uint8_t *desc  = &g_desc_base[g_desc_cursor_offset];
            uint8_t  flags = desc[2];

            int dirty_or_forced = (g_force_redraw != 0) || (flags & 0x01);
            int layer_bit       = desc[1] & 0x04;

            if (dirty_or_forced && layer_bit) {
                /* Clear dirty bit. */
                desc[2] = (uint8_t)(flags & 0xfe);

                int sprite_idx = desc[3];

                if (g_blob_primary) {
                    gfx_glyph_set_cursor_x(cursor_x);
                    gfx_glyph_set_cursor_y(cursor_y);
                    gfx_dispatch_set_last_glyph_index(sprite_idx);
                    gfx_dispatch_stage_sprite_descriptor(g_blob_primary, sprite_idx);

                    /* [1, 0x4baf0] glyph_offset gate.  Out-of-band offsets
                     * just skip the blit; the dirty clear above already
                     * happened (matches retail — the gate sits inside
                     * the dirty-clearing branch). */
                    int glyph_off = gfx_glyph_glyph_offset();
                    if (glyph_off >= 1 && glyph_off <= 0x4baf0) {
                        gfx_sprite_blit_32x32_raw(g_blob_primary);
                        g_overlay_z32_blit_count += 1;
                    }
                }
            }

            g_desc_cursor_offset += g_desc_stride;
            cursor_x             += 0x20;
        }

        g_desc_cursor_offset += g_grid_row_wrap;
        cursor_y             += 0x20;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Middle pass — port of FUN_004bd355 @ 0x4bd355 (543 B), chip 5a.i.
 *
 * Always-on, zoom-independent pass that runs between the terrain and
 * overlay layers.  Three significant differences from the terrain
 * passes:
 *
 *   1. Three setup helpers fire ONCE per frame at the head:
 *        - middle_leaf_build_entity_list   (FUN_004bd938)
 *        - middle_leaf_sort_entity_list    (FUN_004bda92)
 *        - middle_leaf_render_entity_list  (FUN_004bdb95)
 *      They build a list of visible entities, Y-sort it, and render
 *      each one.  Chip 5a.i wires them as counter-stubs; chip 5a.ii
 *      ports their bodies.
 *
 *   2. The cell loop iterates an OVER-SCAN EXTENSION of the visible
 *      grid: starts one cell west / one cell north of the viewport,
 *      ends up to two cells past the south-east corner.  The four
 *      extension widths are clamped against the underlying grid extent
 *      (`camera_row_stride` for columns, `total_rows` for rows) so the
 *      cursor never walks past the last valid cell.  Net visible
 *      iteration is `(cols+2) × (rows+2)` cells when no clamp fires,
 *      reduced by the clamps near grid edges.  This is wider than the
 *      terrain/overlay passes, which iterate exactly the visible grid
 *      — likely to accommodate large unit/effect sprites that overlap
 *      from off-screen cells into the visible area.
 *
 *   3. Per-cell dispatch is gated on TWO independent descriptor bits:
 *
 *        descriptor[+2] & 0x80:
 *          - descriptor[+3] == 0  → player-marker draw (FUN_004bd574)
 *                                   AND descriptor[+2] |= 1 (re-mark
 *                                   the cell dirty so it gets re-drawn
 *                                   next frame too).
 *          - descriptor[+3] != 0  → unit-key sprite draw
 *                                   (FUN_004bd759(descriptor[+3])).
 *                                   NO re-dirty here.
 *
 *        descriptor[+6] != 0:
 *          - effect-chain walk (FUN_004beed4(descriptor[+6])),
 *            unconditional, evaluated AFTER the bit-0x80 dispatch.
 *
 * The asymmetric re-dirty (only the player-marker branch sets bit 0x01)
 * is a quirk: it ensures the player marker is re-painted every frame
 * (terrain pass picks it up on the next frame and clears it again),
 * which matters because retail's player marker animates through 8 frames
 * (the +0x21..+0x28 slot range driven by an internal frame counter).
 * The unit-key sprite does NOT re-dirty because it's static per frame
 * (re-renders only when the underlying cell becomes dirty for some
 * other reason).
 *
 * Per-cell counter `_DAT_004e5d64` (g_per_frame_cell_counter): the
 * middle pass reads + bumps this between the two dispatch gates, in
 * the same place the zoom-16 terrain pass does.  We preserve the
 * mid-loop placement (between bit-0x80 and effect-chain) verbatim.
 *
 * Cell pitch is `g_zoom` pixels (16 or 32).  Cursor (g_cursor_x /
 * cursor_y) is updated per-cell so the leaves can read them, but
 * since the leaves are stubs in chip 5a.i they don't observe it yet.
 * Retail's DAT_00591524 / DAT_00591528 are the same fields gfx_glyph
 * exposes.
 * ────────────────────────────────────────────────────────────────── */

static void pass_middle(void)
{
    g_count_middle += 1;

    /* Setup helpers fire BEFORE the descriptor-table short-circuit:
     * retail's FUN_004bd355 calls them unconditionally at the head, and
     * since they walk their own entity table they do not depend on the
     * battlefield descriptor table being bound.  Tests that bind only
     * the partial state still observe all three counters bump. */
    middle_leaf_build_entity_list();
    middle_leaf_sort_entity_list();
    middle_leaf_render_entity_list();

    if (!g_desc_base) {
        return;
    }

    /* Compute the over-scan extension.
     *
     *   start_col = cam_x - 1,  start_px_x = viewport_x - zoom
     *      if start_col < 0: start_col = 0, start_px_x += zoom (cancel)
     *
     *   start_row = cam_y - 1,  start_px_y = viewport_y - zoom
     *      if start_row < 0: start_row = 0, start_px_y += zoom (cancel)
     *
     *   num_cols  = grid_cols + 2,  clamped so start_col + num_cols
     *               does not exceed camera_row_stride.
     *
     *   num_rows  = grid_rows + 2,  clamped so start_row + num_rows
     *               does not exceed total_rows.
     */
    int start_col  = g_camera_x - 1;
    int start_px_x = g_viewport_x - g_zoom;
    if (start_col < 0) {
        start_col  = 0;
        start_px_x = start_px_x + g_zoom;
    }
    int start_row  = g_camera_y - 1;
    int start_px_y = g_viewport_y - g_zoom;
    if (start_row < 0) {
        start_row  = 0;
        start_px_y = start_px_y + g_zoom;
    }
    int num_cols = g_grid_cols + 2;
    if (g_camera_row_stride <= num_cols + start_col) {
        num_cols = g_camera_row_stride - start_col;
    }
    int num_rows = g_grid_rows + 2;
    if (g_total_rows <= num_rows + start_row) {
        num_rows = g_total_rows - start_row;
    }

    /* End-of-row skip (in descriptor bytes) to jump from the last
     * iterated cell of one row to the first iterated cell of the next.
     * Retail: `(DAT_004e6578 - local_2c) * DAT_004e5d50`. */
    int end_of_row_skip = (g_camera_row_stride - num_cols) * g_desc_stride;

    /* Initial descriptor cursor: (start_row * row_stride + start_col)
     * * desc_stride.  Retail: DAT_004e6588 = ... */
    g_desc_cursor_offset = (g_camera_row_stride * start_row + start_col)
                         * g_desc_stride;

    /* Reset the per-cell counter — retail _DAT_004e5d64 = 0; */
    g_per_frame_cell_counter = 0;

    /* Cursor Y starts at start_px_y (retail DAT_00591528 = local_20). */
    gfx_glyph_set_cursor_y(start_px_y);

    for (int row = 0; row < num_rows; row++) {
        /* Cursor X resets to start_px_x at the head of each row
         * (retail DAT_00591524 = local_c).  Read it back at end of
         * row via gfx_glyph_cursor_x(). */
        gfx_glyph_set_cursor_x(start_px_x);

        for (int col = 0; col < num_cols; col++) {
            uint8_t *desc  = &g_desc_base[g_desc_cursor_offset];
            uint8_t  flags = desc[2];

            if (flags & 0x80) {
                uint8_t key = desc[3];
                if (key == 0) {
                    middle_leaf_draw_player_marker();
                    /* Re-dirty: descriptor[+2] |= 1.  Re-read from the
                     * descriptor in case the leaf mutated flags
                     * (matches retail's `*(...) = *(...) | 1`, which
                     * re-reads inside the OR). */
                    desc[2] = (uint8_t)(desc[2] | 0x01);
                } else {
                    middle_leaf_draw_unit_key(key);
                }
            }

            uint8_t chain_head = desc[6];
            if (chain_head != 0) {
                middle_leaf_draw_effect_chain(chain_head);
            }

            /* Advance descriptor cursor + per-cell counter + cursor_x.
             * Retail order: cursor += stride, counter++, x += zoom. */
            g_desc_cursor_offset     += g_desc_stride;
            g_per_frame_cell_counter += 1;
            gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_zoom);
        }

        /* End-of-row: jump cursor to next visible row's first cell,
         * advance cursor_y by zoom. */
        g_desc_cursor_offset += end_of_row_skip;
        gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_zoom);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Frame driver — port of FUN_004bc142 @ 0x4bc142 (143 B).
 *
 * Retail decompilation (verbatim shape):
 *
 *   void FUN_004bc142(undefined4 param_1, undefined4 param_2) {
 *       DAT_004e657c = param_1;
 *       DAT_004e6580 = param_2;
 *       if (DAT_004e656c == 0x10) { FUN_004bc8fc(); }
 *       else if (DAT_004e656c == 0x20) { FUN_004bcbdc(); }
 *       FUN_004bd355();
 *       if (DAT_004e656c == 0x10) { FUN_004bcbd1(); }
 *       else if (DAT_004e656c == 0x20) { FUN_004bd07f(); }
 *       FUN_004bd233();
 *       FUN_004bf1f9();
 *       if (DAT_0053e9ac != 0) { DAT_0053e9ac--; }
 *   }
 *
 * No conditional fall-through default: any zoom value that is
 * neither 0x10 nor 0x20 simply skips both zoom-dispatched passes.
 * Middle / late / final always fire, and force-redraw is always
 * decremented post-frame when non-zero.
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_frame(int camera_x, int camera_y)
{
    g_camera_x = camera_x;
    g_camera_y = camera_y;

    /* Zoom-dependent terrain pass. */
    if (g_zoom == 0x10) {
        pass_terrain_zoom16();
    } else if (g_zoom == 0x20) {
        pass_terrain_zoom32();
    }

    /* Always: middle pass. */
    pass_middle();

    /* Zoom-dependent overlay pass.  Note: zoom-16's pass is retail
     * FUN_004bcbd1, a true no-op — but it IS dispatched.  Skipping
     * the dispatch entirely would be subtly different from retail
     * if the pass ever grows a body (or if the counter is observed). */
    if (g_zoom == 0x10) {
        pass_overlay_zoom16();
    } else if (g_zoom == 0x20) {
        pass_overlay_zoom32();
    }

    /* Always: late + final passes. */
    pass_late();
    pass_final();

    /* Decrement force-redraw counter post-frame when non-zero.
     * The check-before-decrement matters: a 0 counter must NOT
     * underflow into negative territory (retail signed int). */
    if (g_force_redraw != 0) {
        g_force_redraw -= 1;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Per-pass call-counter accessors
 * ────────────────────────────────────────────────────────────────── */

int engine_battlefield_render_count_terrain_zoom16(void)  { return g_count_terrain_zoom16; }
int engine_battlefield_render_count_terrain_zoom32(void)  { return g_count_terrain_zoom32; }
int engine_battlefield_render_count_middle(void)          { return g_count_middle; }
int engine_battlefield_render_count_overlay_zoom16(void)  { return g_count_overlay_zoom16; }
int engine_battlefield_render_count_overlay_zoom32(void)  { return g_count_overlay_zoom32; }
int engine_battlefield_render_count_late(void)            { return g_count_late; }
int engine_battlefield_render_count_final(void)           { return g_count_final; }

int engine_battlefield_render_middle_build_count(void)          { return g_middle_build_count; }
int engine_battlefield_render_middle_sort_count(void)           { return g_middle_sort_count; }
int engine_battlefield_render_middle_render_count(void)         { return g_middle_render_count; }
int engine_battlefield_render_middle_player_marker_count(void)  { return g_middle_player_marker_count; }
int engine_battlefield_render_middle_unit_key_count(void)       { return g_middle_unit_key_count; }
int engine_battlefield_render_middle_effect_chain_count(void)   { return g_middle_effect_chain_count; }

int engine_battlefield_render_middle_unit_key_last_arg(void)
{
    return g_middle_unit_key_last_arg;
}
int engine_battlefield_render_middle_effect_chain_last_arg(void)
{
    return g_middle_effect_chain_last_arg;
}

int engine_battlefield_render_visible_entity_count(void)
{
    return g_visible_entity_count;
}

int engine_battlefield_render_visible_entity_id(int idx)
{
    if (idx < 0 || idx >= g_visible_entity_count) {
        return 0;
    }
    return (int)g_visible_entity_id[idx];
}

int engine_battlefield_render_visible_entity_y(int idx)
{
    if (idx < 0 || idx >= g_visible_entity_count) {
        return 0;
    }
    return (int)g_visible_entity_y[idx];
}

int engine_battlefield_render_middle_sort_passes(void)
{
    return g_middle_sort_passes;
}

int engine_battlefield_render_middle_per_entity_count(void)
{
    return g_middle_per_entity_count;
}
int engine_battlefield_render_middle_per_entity_last_id(void)
{
    return g_middle_per_entity_last_id;
}
int engine_battlefield_render_middle_per_entity_last_px_x(void)
{
    return g_middle_per_entity_last_px_x;
}
int engine_battlefield_render_middle_per_entity_last_px_y(void)
{
    return g_middle_per_entity_last_px_y;
}

int engine_battlefield_render_middle_entity_type6_count(void)
{
    return g_middle_entity_type6_count;
}
int engine_battlefield_render_middle_entity_type6_last_id(void)
{
    return g_middle_entity_type6_last_id;
}
int engine_battlefield_render_middle_entity_type7_count(void)
{
    return g_middle_entity_type7_count;
}
int engine_battlefield_render_middle_entity_type7_last_id(void)
{
    return g_middle_entity_type7_last_id;
}
int engine_battlefield_render_middle_entity_type9_sub_count(void)
{
    return g_middle_entity_type9_sub_count;
}
int engine_battlefield_render_middle_entity_type9_sub_last_id(void)
{
    return g_middle_entity_type9_sub_last_id;
}

int engine_battlefield_render_middle_entity_no_blob_count(void)
{
    return g_middle_entity_no_blob_count;
}
int engine_battlefield_render_middle_entity_no_blob_last_id(void)
{
    return g_middle_entity_no_blob_last_id;
}

int engine_battlefield_render_middle_entity_blit_count(void)
{
    return g_middle_entity_blit_count;
}

int engine_battlefield_render_per_frame_cell_counter(void)
{
    return g_per_frame_cell_counter;
}

void engine_battlefield_render_run_pass_middle_for_test(int camera_x, int camera_y)
{
    g_camera_x = camera_x;
    g_camera_y = camera_y;
    pass_middle();
}

/* ──────────────────────────────────────────────────────────────────
 * Coordinate helpers — ports of FUN_004bc222 / 2a5 / 2c5 / 2e5 / 346.
 *
 * Tiny pure functions consumed by the input layer to translate screen
 * pixel coords into the battlefield's hex column / row.  Each reads
 * the same module statics the render passes do (viewport, zoom, grid
 * dims, camera_row_stride, cell_band_width).
 * ────────────────────────────────────────────────────────────────── */

int engine_battlefield_render_screen_in_grid(int px, int py)
{
    if (px < g_viewport_x) return 0;
    if (py < g_viewport_y) return 0;
    if (px < g_grid_cols * g_zoom + g_viewport_x) {
        if (py < g_grid_rows * g_zoom + g_viewport_y) {
            return 1;
        }
        return 0;
    }
    return 0;
}

int engine_battlefield_render_pixel_to_col(int px)
{
    return (px - g_viewport_x) / g_zoom;
}

int engine_battlefield_render_pixel_to_row(int py)
{
    return (py - g_viewport_y) / g_zoom;
}

int engine_battlefield_render_pixel_to_col_right_biased(int px)
{
    int col = (px - g_viewport_x) / g_zoom;
    if (g_cell_band_width * 3 <= (px - g_viewport_x) % g_zoom) {
        col += 1;
    }
    if (col < 1) {
        col = 0;
    }
    return col;
}

int engine_battlefield_render_pixel_to_col_left_biased(int px)
{
    int col = (px - g_viewport_x) / g_zoom;
    if ((px - g_viewport_x) % g_zoom < g_cell_band_width) {
        col += -1;
    }
    if (g_camera_row_stride <= col) {
        col = g_camera_row_stride - 1;
    }
    return col;
}

void engine_battlefield_render_reset_counts(void)
{
    g_count_terrain_zoom16             = 0;
    g_count_terrain_zoom32             = 0;
    g_count_middle                     = 0;
    g_count_overlay_zoom16             = 0;
    g_count_overlay_zoom32             = 0;
    g_count_late                       = 0;
    g_count_final                      = 0;
    g_terrain_blit_count               = 0;
    g_terrain_z32_first_blit_count     = 0;
    g_terrain_z32_second_blit_count    = 0;
    g_overlay_z32_blit_count           = 0;
    g_middle_build_count               = 0;
    g_middle_sort_count                = 0;
    g_middle_render_count              = 0;
    g_middle_player_marker_count       = 0;
    g_middle_unit_key_count            = 0;
    g_middle_effect_chain_count        = 0;
    g_middle_unit_key_last_arg         = -1;
    g_middle_effect_chain_last_arg     = -1;
    g_per_frame_cell_counter           = 0;

    g_visible_entity_count             = 0;
    g_middle_sort_passes               = 0;
    g_middle_per_entity_count          = 0;
    g_middle_per_entity_last_id        = -1;
    g_middle_per_entity_last_px_x      = 0;
    g_middle_per_entity_last_px_y      = 0;

    g_middle_entity_blit_count         = 0;
    g_middle_entity_type6_count        = 0;
    g_middle_entity_type6_last_id      = -1;
    g_middle_entity_type7_count        = 0;
    g_middle_entity_type7_last_id      = -1;
    g_middle_entity_type9_sub_count    = 0;
    g_middle_entity_type9_sub_last_id  = -1;
    g_middle_entity_no_blob_count      = 0;
    g_middle_entity_no_blob_last_id    = -1;
}
