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
#include "gfx_text_label.h"

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
 * blob skip the main draw body.  Retail logs via 0x4afab9 — we
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

/* ─── chip 5a.iii: per-cell draw leaves bindings ───────────────── */

/* Player-marker (FUN_004bd574) bindings.  See header for the
 * complete role of each. */
static const uint8_t *g_player_marker_blob;        /* DAT_00566518 */
static const uint8_t *g_player_marker_slot_table;  /* &DAT_0052f0b2 */
static int            g_player_marker_slot_stride; /* 0x1a4 */
static int            g_player_marker_player_id;   /* DAT_00568218 */
static int            g_player_marker_anim_advance;/* DAT_00591510 */
static int            g_player_marker_anim_frame;  /* DAT_004e5b18 */
static int            g_player_marker_last_slot   = -1;
static int            g_player_marker_blit_count;

/* Unit-key (FUN_004bd759) bindings. */
static const uint8_t *g_unit_key_blob;             /* DAT_00553250 */
static int            g_unit_key_last_slot        = -1;
static int            g_unit_key_blit_count;

/* Effect-chain (FUN_004beed4) bindings. */
static const uint8_t *g_effect_chain_base;         /* &DAT_0057a100 */
static int            g_effect_chain_stride;       /* 0x4c */
static int            g_effect_chain_max_id;       /* table size / stride - 1 */
static const uint8_t *g_effect_chain_anim_table;   /* DAT_004e4470 */
static const uint8_t *g_effect_chain_shared_blob;  /* port-only — see header */
static int            g_effect_chain_anim_frame;   /* DAT_004e5b1c */
static int            g_effect_chain_current_id;   /* DAT_00568994 */
static int            g_effect_chain_iters;        /* local_c */
static int            g_effect_chain_blit_count;
static int            g_scroll_pad;                /* DAT_004e5d44 */

/* ─── chip 5a.iv: per-entity type-special leaves bindings ──────── */

/* Type-6 PRE-call shared blob (see header for the layout-collision
 * rationale).  All type-6 entities use this single bound blob; slot
 * comes from entity record's +0x11. */
static const uint8_t *g_entity_type6_blob;        /* port-only — see header */

/* Type-7 POST-call facing-selected blob pair.  facing < 4 → low;
 * facing >= 4 → high. */
static const uint8_t *g_entity_type7_blob_low;    /* DAT_00553f6c */
static const uint8_t *g_entity_type7_blob_high;   /* DAT_00522f78 */

/* Per-blit counters for the three type-special leaves. */
static int g_middle_entity_type6_blit_count;
static int g_middle_entity_type7_blit_count;
static int g_middle_entity_type9_sub_blit_count;

/* ─── Chip 5c — late-pass bindings + counters ─────────────────────── */

static const uint8_t *g_creature_sprite_blob;          /* DAT_005530c8 */

/* Per-player visibility mask: byte at base[player_id * stride]. */
static const uint8_t *g_late_player_vis_base;          /* &DAT_0057bfee */
static int            g_late_player_vis_stride;        /* 0x160 */
static int            g_late_player_id;                /* DAT_0057c8cc */

/* Sprite slot shift control (creature draw): slot_short = raw shifted
 * by `amount` left (sign >= 0) or right (sign < 0). */
static int g_creature_slot_shift_amount;               /* DAT_00566514 */
static int g_creature_slot_shift_sign;                 /* DAT_0056651c */

/* Selection marker special position. */
static int g_selection_marker_x;                        /* DAT_00553f14 */
static int g_selection_marker_y;                        /* DAT_00553f18 */
static int g_selection_marker_gate;                     /* DAT_00553f84 */

/* World-map dirty hook (substitute for retail 0x46c4a3 direct call). */
static engine_battlefield_world_dirty_hook_t g_world_dirty_hook;

/* Late-pass counters. */
static int g_late_per_cell_count;
static int g_late_creature_draw_count;
static int g_late_creature_blit_count;
static int g_late_selection_marker_count;
static int g_late_selection_marker_blit_count;
static int g_late_world_dirty_hook_count;

/* ─── Chip 5d — final-pass bindings + counters ────────────────────── */

static int            g_debug_label_enabled;          /* DAT_005bb4a5 */
static const uint8_t *g_debug_label_font;             /* DAT_005af8f0 */
static const uint8_t *g_debug_label_char_glyph_table; /* port-split */
static const char    *g_debug_label_suffix;           /* DAT_004e4520 */

/* Final-pass counters. */
static int g_final_per_cell_count;
static int g_final_label_render_count;

/* ─── chip 7: mini-map / hex-grid overview state ─────────────────── */

/* Mini-map sprite blobs.
 *   primary   retail DAT_004e6584  — terrain flag (& 0x1c) == 0
 *   alt       retail DAT_004e6568  — terrain flag (& 0x1c) == 4
 *   default   retail DAT_004e5d40  — boundary cells, first-col-ingame
 *                                     override, entity dispatch
 */
static const uint8_t *g_minimap_blob_primary;
static const uint8_t *g_minimap_blob_alt;
static const uint8_t *g_minimap_default_descriptor;

/* Mini-map screen origin (retail DAT_004e5d68 x, _DAT_004e5d60 y).
 * cursor_x is reset to origin_x at start of each row; cursor_y begins
 * at start_row * 2 + y_offset for each strip. */
static int g_minimap_origin_x;
static int g_minimap_y_offset;

/* Mini-map mode gate (retail DAT_004e5d58).  Strip-render only fires
 * from minimap_scroll when this equals 2. */
static int g_minimap_mode_gate;

/* Engine-state gate (retail DAT_0052af94).  When equal to 3 (= ingame)
 * AND we're on the first col of a row in the strip render, the path
 * switches to the default-descriptor slot-0 override. */
static int g_minimap_engine_state;

/* Entity-owner table (retail &DAT_005544ac, stride 0x1b0).  Indexed by
 * descriptor[+5]; byte at offset 0 of each record is the owner type. */
static const uint8_t *g_minimap_entity_owner_table;
static int            g_minimap_entity_owner_stride;

/* Per-owner mini-map sprite-slot table (retail &DAT_0057bf0a,
 * stride 0x160).  Indexed by the owner-type byte; byte at offset 0 is
 * the sprite slot to use from the default descriptor. */
static const uint8_t *g_minimap_player_slot_table;
static int            g_minimap_player_slot_stride;

/* Rolling scan cursor (retail DAT_004e5d74).  Bumped by minimap_scroll;
 * the strip render starts at this row. */
static int g_minimap_scan_pos;

/* Test-only counters. */
static int g_minimap_per_cell_count;
static int g_minimap_blit_count;

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

void engine_battlefield_render_bind_player_marker_blob(const uint8_t *blob)
{
    g_player_marker_blob = blob;
}

void engine_battlefield_render_bind_player_marker_slot_table(const uint8_t *table,
                                                              int stride)
{
    g_player_marker_slot_table  = table;
    g_player_marker_slot_stride = stride;
}

void engine_battlefield_render_set_player_marker_player_id(int player_id)
{
    g_player_marker_player_id = player_id;
}

void engine_battlefield_render_set_player_marker_anim_advance(int advance)
{
    g_player_marker_anim_advance = advance;
}

void engine_battlefield_render_bind_unit_key_blob(const uint8_t *blob)
{
    g_unit_key_blob = blob;
}

void engine_battlefield_render_bind_effect_chain_table(const uint8_t *base,
                                                       int stride, int max_id)
{
    g_effect_chain_base   = base;
    g_effect_chain_stride = stride;
    g_effect_chain_max_id = max_id;
}

void engine_battlefield_render_bind_effect_chain_anim_table(const uint8_t *table)
{
    g_effect_chain_anim_table = table;
}

void engine_battlefield_render_bind_effect_chain_shared_blob(const uint8_t *blob)
{
    g_effect_chain_shared_blob = blob;
}

void engine_battlefield_render_bind_scroll_pad(int pad)
{
    g_scroll_pad = pad;
}

void engine_battlefield_render_bind_entity_type6_blob(const uint8_t *blob)
{
    g_entity_type6_blob = blob;
}

void engine_battlefield_render_bind_entity_type7_blobs(const uint8_t *low,
                                                       const uint8_t *high)
{
    g_entity_type7_blob_low  = low;
    g_entity_type7_blob_high = high;
}

void engine_battlefield_render_bind_creature_sprite_blob(const uint8_t *blob)
{
    g_creature_sprite_blob = blob;
}

void engine_battlefield_render_bind_late_player_visibility_mask(
    const uint8_t *base, int stride, int player_id)
{
    g_late_player_vis_base   = base;
    g_late_player_vis_stride = stride;
    g_late_player_id         = player_id;
}

void engine_battlefield_render_bind_creature_slot_shift(int amount, int sign)
{
    g_creature_slot_shift_amount = amount;
    g_creature_slot_shift_sign   = sign;
}

void engine_battlefield_render_bind_selection_marker(int x, int y, int gate)
{
    g_selection_marker_x    = x;
    g_selection_marker_y    = y;
    g_selection_marker_gate = gate;
}

void engine_battlefield_render_bind_world_map_dirty_hook(
    engine_battlefield_world_dirty_hook_t hook)
{
    g_world_dirty_hook = hook;
}

void engine_battlefield_render_bind_debug_label_enabled(int enabled)
{
    g_debug_label_enabled = enabled;
}

void engine_battlefield_render_bind_debug_label_font(
    const uint8_t *font_base, const uint8_t *char_glyph_table)
{
    g_debug_label_font             = font_base;
    g_debug_label_char_glyph_table = char_glyph_table;
}

void engine_battlefield_render_bind_debug_label_suffix(const char *suffix)
{
    g_debug_label_suffix = suffix;
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
/* pass_late and pass_final are real ports — defined below. */

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

/* Read a 32-bit little-endian int from a byte table at the given byte
 * offset.  memcpy is the strict-aliasing-safe path; on the host x86 it
 * compiles to a single mov. */
static int read_le32(const uint8_t *base, int byte_offset)
{
    int32_t v;
    memcpy(&v, base + byte_offset, sizeof v);
    return (int)v;
}

/* Read the (anim_x_off, anim_y_off) pair from the bound anim-offset
 * table at the given zoom level.  Returns 0 in both out-params for
 * unsupported zooms or unbound tables.  Shared by all three type-
 * special leaves (chip 5a.iv) — they each apply the same zoom-
 * conditional anim-offset lookup as the per-entity draw, but with
 * different X/Y centering biases. */
static void type_special_anim_offset(int8_t facing, int8_t anim_frame,
                                      int *out_x, int *out_y)
{
    *out_x = 0;
    *out_y = 0;
    int entry_off = (int)facing     * ENGINE_BATTLEFIELD_ANIM_FACING_STRIDE
                  + (int)anim_frame * ENGINE_BATTLEFIELD_ANIM_FRAME_STRIDE;
    if (g_zoom == 0x20 && g_anim_table_z32) {
        *out_x = read_le32(g_anim_table_z32, entry_off + 0);
        *out_y = read_le32(g_anim_table_z32, entry_off + 4);
    } else if (g_zoom == 0x10 && g_anim_table_z16) {
        *out_x = read_le32(g_anim_table_z16, entry_off + 0);
        *out_y = read_le32(g_anim_table_z16, entry_off + 4);
    }
}

/* FUN_004be4df (735 B) — type-6 PRE-call.  Shadow / ground-effect draw
 * beneath the main sprite.  Uses a shared bound blob (port substitute
 * for the entity's secondary-blob field at +0x04 — see header).  Slot
 * is the entity's secondary frame index (+0x11).  Centering follows
 * the per-entity zoom logic with an additional anim-offset application
 * (without the type-9/10 Y bonuses).  6×6 dirty box. */
static void entity_type6_pre_call(int id)
{
    g_middle_entity_type6_count   += 1;
    g_middle_entity_type6_last_id  = id;

    if (!g_entity_type6_blob) {
        return;
    }

    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();

    uint8_t *rec = g_entity_base + id * g_entity_stride;
    int slot = (int)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME];

    gfx_dispatch_set_last_glyph_index(slot);
    gfx_dispatch_stage_sprite_descriptor(g_entity_type6_blob, slot);

    int glyph_off = gfx_glyph_glyph_offset();
    if (glyph_off >= 1 && glyph_off <= 0x4baf0) {
        int8_t facing = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING];
        int8_t af     = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME];
        int    sprite_w = gfx_glyph_glyph_w();
        int    half_w   = sprite_w >> 1;

        int off_x = 0, off_y = 0;
        type_special_anim_offset(facing, af, &off_x, &off_y);

        if (g_zoom == 0x20) {
            off_x += (0x10 - half_w);
            off_y += (   8 - half_w);
        } else if (g_zoom == 0x10) {
            off_x += (   8 - half_w);
            off_y += (   4 - half_w);
        }
        /* else: unsupported zoom — offsets stay at anim-only values.
         * Retail leaves locals uninitialised here — see quirk #88. */

        int new_x = entry_x + off_x;
        int new_y = entry_y + off_y;
        gfx_glyph_set_cursor_x(new_x);
        gfx_glyph_set_cursor_y(new_y);

        if (g_dirty_state) {
            engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                             new_x, new_y, 6, 6, 2);
        }

        gfx_dispatch_clip_x(g_clip_left, g_clip_right);
        gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

        int y_mode_before = gfx_dispatch_clip_y_mode();
        gfx_dispatch_dispatch_sprite_rle(g_entity_type6_blob);
        if (y_mode_before != 5) {
            g_middle_entity_type6_blit_count += 1;
        }
    }

    gfx_glyph_set_cursor_x(entry_x);
    gfx_glyph_set_cursor_y(entry_y);
}

/* FUN_004be7be (763 B) — type-7 POST-call.  Same body shape as type-6
 * but with the blob picked by entity facing direction (facing < 4 →
 * low, else → high).  Slot is the entity's secondary frame index. */
static void entity_type7_post_call(int id)
{
    g_middle_entity_type7_count   += 1;
    g_middle_entity_type7_last_id  = id;

    uint8_t *rec = g_entity_base + id * g_entity_stride;
    int8_t facing = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING];

    const uint8_t *blob = (facing < 4)
        ? g_entity_type7_blob_low
        : g_entity_type7_blob_high;
    if (!blob) {
        return;
    }

    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();

    int slot = (int)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME];

    gfx_dispatch_set_last_glyph_index(slot);
    gfx_dispatch_stage_sprite_descriptor(blob, slot);

    int glyph_off = gfx_glyph_glyph_offset();
    if (glyph_off >= 1 && glyph_off <= 0x4baf0) {
        int8_t af = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME];
        int    sprite_w = gfx_glyph_glyph_w();
        int    half_w   = sprite_w >> 1;

        int off_x = 0, off_y = 0;
        type_special_anim_offset(facing, af, &off_x, &off_y);

        if (g_zoom == 0x20) {
            off_x += (0x10 - half_w);
            off_y += (   8 - half_w);
        } else if (g_zoom == 0x10) {
            off_x += (   8 - half_w);
            off_y += (   4 - half_w);
        }

        int new_x = entry_x + off_x;
        int new_y = entry_y + off_y;
        gfx_glyph_set_cursor_x(new_x);
        gfx_glyph_set_cursor_y(new_y);

        if (g_dirty_state) {
            engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                             new_x, new_y, 6, 6, 2);
        }

        gfx_dispatch_clip_x(g_clip_left, g_clip_right);
        gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

        int y_mode_before = gfx_dispatch_clip_y_mode();
        gfx_dispatch_dispatch_sprite_rle(blob);
        if (y_mode_before != 5) {
            g_middle_entity_type7_blit_count += 1;
        }
    }

    gfx_glyph_set_cursor_x(entry_x);
    gfx_glyph_set_cursor_y(entry_y);
}

/* FUN_004beab9 (1051 B) — type-9-subtype-0xe POST-call.  Draws TWO
 * sprites per call from the entity's PRIMARY blob (entity[+0x00]):
 *
 *   First  sprite: slot = secondary_frame + 0x16, cursor_y -= 16.
 *   Second sprite: slot = secondary_frame + 0xd,  cursor_y += 0x56,
 *                  anchored at the SAVED entry cursor (not the
 *                  post-first-sprite cursor).  Only fires if the first
 *                  passed the glyph-offset gate.
 *
 * Both sprites apply the z32 anim offset table UNCONDITIONALLY — there
 * is no `if (zoom == 0x10)` branch in retail, so at zoom 16 the leaf
 * still indexes the z32 table.  Likely "subtype 0xe only occurs at
 * zoom 32" — preserved verbatim and filed as a quirk.
 *
 * Centering uses sprite WIDTH on both axes (no +8 Y bonus like
 * type-6/7), then per-sprite Y bias is applied AFTER.  6×6 dirty
 * box on each sprite. */
static void entity_type9_subtype_e_post_call(int id)
{
    g_middle_entity_type9_sub_count   += 1;
    g_middle_entity_type9_sub_last_id  = id;

    uint8_t *rec = g_entity_base + id * g_entity_stride;

    const uint8_t *blob;
    memcpy(&blob,
           rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB,
           sizeof blob);
    if (!blob) {
        return;
    }

    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();

    uint8_t sf = rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME];
    int8_t facing = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING];
    int8_t af     = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME];

    /* First sprite: slot = secondary_frame + 0x16. */
    int slot1 = (int)sf + 0x16;
    gfx_dispatch_set_last_glyph_index(slot1);
    gfx_dispatch_stage_sprite_descriptor(blob, slot1);

    int glyph_off = gfx_glyph_glyph_offset();
    if (glyph_off >= 1 && glyph_off <= 0x4baf0) {
        int sprite_w = gfx_glyph_glyph_w();
        int half_w   = sprite_w >> 1;

        /* z32 anim table indexed unconditionally — see leaf doc. */
        int anim_x = 0, anim_y = 0;
        if (g_anim_table_z32) {
            int entry_off =
                (int)facing * ENGINE_BATTLEFIELD_ANIM_FACING_STRIDE
              + (int)af     * ENGINE_BATTLEFIELD_ANIM_FRAME_STRIDE;
            anim_x = read_le32(g_anim_table_z32, entry_off + 0);
            anim_y = read_le32(g_anim_table_z32, entry_off + 4);
        }

        int new_x = entry_x + anim_x + (0x10 - half_w);
        int new_y = entry_y + anim_y - half_w - 0x10;
        gfx_glyph_set_cursor_x(new_x);
        gfx_glyph_set_cursor_y(new_y);

        if (g_dirty_state) {
            engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                             new_x, new_y, 6, 6, 2);
        }

        gfx_dispatch_clip_x(g_clip_left, g_clip_right);
        gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

        int y_mode_before = gfx_dispatch_clip_y_mode();
        gfx_dispatch_dispatch_sprite_rle(blob);
        if (y_mode_before != 5) {
            g_middle_entity_type9_sub_blit_count += 1;
        }

        /* Second sprite: slot = secondary_frame + 0xd, anchored at the
         * SAVED entry cursor (not the first-sprite-adjusted cursor).
         * Gated on the SECOND slot's own glyph_offset window. */
        int slot2 = (int)sf + 0xd;
        gfx_dispatch_set_last_glyph_index(slot2);
        gfx_dispatch_stage_sprite_descriptor(blob, slot2);

        int glyph_off2 = gfx_glyph_glyph_offset();
        if (glyph_off2 >= 1 && glyph_off2 <= 0x4baf0) {
            int sprite_w2 = gfx_glyph_glyph_w();
            int half_w2   = sprite_w2 >> 1;

            int anim_x2 = anim_x;     /* same anim offsets, but recomputed */
            int anim_y2 = anim_y;     /* if facing/frame changed — they */
            /* didn't, since the body reads from the same entity. */

            int new2_x = entry_x + anim_x2 + (0x10 - half_w2);
            int new2_y = entry_y + anim_y2 - half_w2 + 0x56;
            gfx_glyph_set_cursor_x(new2_x);
            gfx_glyph_set_cursor_y(new2_y);

            if (g_dirty_state) {
                engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                                 new2_x, new2_y, 6, 6, 2);
            }

            gfx_dispatch_clip_x(g_clip_left, g_clip_right);
            gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

            int y_mode_before2 = gfx_dispatch_clip_y_mode();
            gfx_dispatch_dispatch_sprite_rle(blob);
            if (y_mode_before2 != 5) {
                g_middle_entity_type9_sub_blit_count += 1;
            }
        }
    }

    gfx_glyph_set_cursor_x(entry_x);
    gfx_glyph_set_cursor_y(entry_y);
}

/* No-blob path stub.  Retail calls 0x4afab9 (a debug logger) when
 * an entity's primary blob is NULL; the port counts the call instead.
 * The string at retail's 0x4e44f0 is presumably "<entity> has no
 * sprite blob, type %d" or similar — irrelevant for behavioral
 * fidelity. */
static void entity_no_blob_stub(int id)
{
    g_middle_entity_no_blob_count   += 1;
    g_middle_entity_no_blob_last_id  = id;
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
        entity_type6_pre_call(id);
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
        entity_type7_post_call(id);
    }
    if (type == 9
        && rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SUBTYPE] == 0x0e) {
        entity_type9_subtype_e_post_call(id);
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

/* Shared render-and-dispatch helper for player_marker and unit_key.
 *
 * Both leaves share the same shape AFTER slot selection: stage sprite
 * descriptor, apply the [1, 0x4baf0] glyph-offset gate, re-center cursor
 * by `(16 - sprite_w/2)` on BOTH axes (yes, the retail body uses
 * sprite_w on both axes — likely a copy-paste bug since hex tiles
 * aren't square, but preserved verbatim — see quirk on player_marker /
 * unit_key Y-axis centering), mark a 4×4 tile dirty box at the new
 * cursor (force_full = 2), set clip rect, dispatch RLE blit.  Cursor is
 * restored to the entry value by the caller (each leaf saves/restores).
 *
 * Returns 1 if the blit was dispatched (i.e. blob present AND gate
 * passed AND y_clip_mode != 5), 0 otherwise. */
static int player_marker_or_unit_key_render(const uint8_t *blob, int slot)
{
    if (!blob) {
        return 0;
    }

    gfx_dispatch_set_last_glyph_index(slot);
    gfx_dispatch_stage_sprite_descriptor(blob, slot);

    int glyph_off = gfx_glyph_glyph_offset();
    if (glyph_off < 1 || glyph_off > 0x4baf0) {
        return 0;
    }

    /* Re-center.  Both axes use sprite_w (not sprite_h) — retail
     * verbatim; see the corresponding quirk. */
    int sprite_w = gfx_glyph_glyph_w();
    int adj      = 0x10 - (sprite_w >> 1);
    int new_x    = gfx_glyph_cursor_x() + adj;
    int new_y    = gfx_glyph_cursor_y() + adj;
    gfx_glyph_set_cursor_x(new_x);
    gfx_glyph_set_cursor_y(new_y);

    if (g_dirty_state) {
        engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                         new_x, new_y, 4, 4, /*force_full=*/2);
    }

    gfx_dispatch_clip_x(g_clip_left, g_clip_right);
    gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

    int y_mode_before = gfx_dispatch_clip_y_mode();
    gfx_dispatch_dispatch_sprite_rle(blob);
    return (y_mode_before != 5) ? 1 : 0;
}

/* FUN_004bd574 (485 B) — player-marker draw.  Animated 8-frame sprite
 * cycle drawn on the active player's marker hex.  Frame index lives in
 * a module-static (retail DAT_004e5b18); advance gated on the bound
 * `anim_advance` input (retail DAT_00591510).
 *
 * Slot computation: the per-player slot_table holds a single byte per
 * player at offset `player_id * stride` (retail stride 0x1a4).  That
 * byte is multiplied by 8 (to skip past the previous banner band's
 * 8-frame cycle) and added to (frame + 0x21) to pick the slot in the
 * marker sprite blob.
 *
 * Cursor save/restore wraps the whole render — leaf is side-effect free
 * on cursor regardless of whether the blit actually fires. */
static void middle_leaf_draw_player_marker(void)
{
    g_middle_player_marker_count += 1;

    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();

    /* Animation-frame advance.  The two clamps sit BEFORE the
     * post-advance wrap test in retail, so a manually pre-set value
     * outside [0, 7] normalizes to 0 BEFORE the conditional advance
     * looks at it.  This means: setting g_player_marker_anim_frame = 9
     * then calling with advance=0 yields a final frame of 0 (not 9). */
    if (g_player_marker_anim_advance != 0) {
        g_player_marker_anim_frame += 1;
    }
    if (g_player_marker_anim_frame < 0) {
        g_player_marker_anim_frame = 0;
    }
    if (7 < g_player_marker_anim_frame) {
        g_player_marker_anim_frame = 0;
    }

    /* Slot computation.  If no slot table bound, banner_byte defaults
     * to 0 — slot becomes `frame + 0x21`, matching the "first banner
     * band" baseline.  Tests that don't care about slot identity can
     * leave the table unbound. */
    int banner_byte = 0;
    if (g_player_marker_slot_table && g_player_marker_slot_stride > 0) {
        banner_byte = (int)g_player_marker_slot_table
            [g_player_marker_player_id * g_player_marker_slot_stride];
    }
    int slot = banner_byte * 8 + g_player_marker_anim_frame + 0x21;

    if (player_marker_or_unit_key_render(g_player_marker_blob, slot)) {
        g_player_marker_blit_count += 1;
        g_player_marker_last_slot   = slot;
    }

    gfx_glyph_set_cursor_x(entry_x);
    gfx_glyph_set_cursor_y(entry_y);
}

/* FUN_004bd759 (479 B) — unit-key sprite draw.  Static (non-animated)
 * letter sprite drawn over a unit's hex to identify it.  Slot is a
 * 4-entry switch on the key byte:
 *
 *   key 0x49 ('I') → slot 0x1f       any other key → no render
 *   key 0x4c ('L') → slot 0x20                       (leaf returns)
 *   key 0x61 ('a') → slot 0x21
 *   key 0x64 ('d') → slot 0x22
 *
 * The four mapped keys are 'I' / 'L' / 'a' / 'd' (initials of the
 * unit classes — likely "Infantry / Lord / archer / defender"?).  Any
 * other byte silently drops without blitting OR touching cursor —
 * matches retail's early `if (...) return;` (before the cursor save). */
static void middle_leaf_draw_unit_key(uint8_t key)
{
    g_middle_unit_key_count += 1;
    g_middle_unit_key_last_arg = key;

    int slot;
    switch (key) {
        case 0x49: slot = 0x1f; break;
        case 0x4c: slot = 0x20; break;
        case 0x61: slot = 0x21; break;
        case 0x64: slot = 0x22; break;
        default:                  return;
    }

    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();

    if (player_marker_or_unit_key_render(g_unit_key_blob, slot)) {
        g_unit_key_blit_count += 1;
        g_unit_key_last_slot   = slot;
    }

    gfx_glyph_set_cursor_x(entry_x);
    gfx_glyph_set_cursor_y(entry_y);
}

/* Read a 16-bit little-endian signed short from a byte table at the
 * given offset.  Used by the effect-chain leaf to read per-record
 * x / y offsets and next-id / sprite-slot shorts. */
static int read_le16s(const uint8_t *base, int byte_offset)
{
    int16_t v;
    memcpy(&v, base + byte_offset, sizeof v);
    return (int)v;
}

/* FUN_004beed4 (805 B) — effect-chain walk + draw.
 *
 * Walks a linked list of effect records starting at `head` (the byte
 * stored in descriptor[+6]).  At most 10 hops per call; chain ends on
 * id=0, type=0, or a self-loop (next-id == this id).
 *
 * Per record:
 *   - Subtype 7 → skip render (chain walk continues).
 *   - Subtype 5 → advance shared 0..15 anim counter, add per-frame
 *                 (x_off, y_off) jitter from the anim table.
 *   - Other    → render at static (x, y) offset.
 *
 * Sprite blob + slot come from the record itself (each effect can have
 * its own art source).  Cursor is positioned at
 * `viewport + (chain_xy - camera*0x20 [+ anim_jitter]) + scroll_pad`.
 *
 * Retail QUIRK preserved: when the [1, 0x4baf0] glyph-offset gate fires
 * out-of-band, the leaf early-returns WITHOUT restoring the saved entry
 * cursor — leaving cursor_x/cursor_y in whatever state the per-record
 * position computation left them.  Filed as a quirk.  Tests assert this. */
static void middle_leaf_draw_effect_chain(uint8_t head)
{
    g_middle_effect_chain_count   += 1;
    g_middle_effect_chain_last_arg = (int)head;
    g_effect_chain_iters           = 0;

    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();

    int id = (int)head;

    if (!g_effect_chain_base) {
        gfx_glyph_set_cursor_x(entry_x);
        gfx_glyph_set_cursor_y(entry_y);
        return;
    }

    while (1) {
        if (id == 0) {
            gfx_glyph_set_cursor_x(entry_x);
            gfx_glyph_set_cursor_y(entry_y);
            return;
        }
        if (9 < g_effect_chain_iters) {
            /* 10-hop safety cap. */
            gfx_glyph_set_cursor_x(entry_x);
            gfx_glyph_set_cursor_y(entry_y);
            return;
        }

        g_effect_chain_current_id = id;

        if (id < 0 || id > g_effect_chain_max_id) {
            /* Out-of-range — defensive guard not present in retail (retail
             * blindly indexes); short-circuit to chain end. */
            gfx_glyph_set_cursor_x(entry_x);
            gfx_glyph_set_cursor_y(entry_y);
            return;
        }

        const uint8_t *rec = g_effect_chain_base + id * g_effect_chain_stride;

        if (rec[ENGINE_BATTLEFIELD_EFFECT_OFFSET_TYPE] == 0) {
            gfx_glyph_set_cursor_x(entry_x);
            gfx_glyph_set_cursor_y(entry_y);
            return;
        }

        int eff_x = read_le16s(rec, ENGINE_BATTLEFIELD_EFFECT_OFFSET_X_OFFSET)
                  + g_camera_x * -0x20;
        int eff_y = read_le16s(rec, ENGINE_BATTLEFIELD_EFFECT_OFFSET_Y_OFFSET)
                  + g_camera_y * -0x20;

        uint8_t subtype = rec[ENGINE_BATTLEFIELD_EFFECT_OFFSET_SUBTYPE];

        if (subtype != 7) {
            /* Subtype 5: animated — advance shared 0..15 counter, add
             * (x_off, y_off) jitter from the anim table. */
            if (subtype == 5) {
                g_effect_chain_anim_frame += 1;
                if (0xf < g_effect_chain_anim_frame) {
                    g_effect_chain_anim_frame = 0;
                }
                if (g_effect_chain_anim_table) {
                    int j_off = g_effect_chain_anim_frame * 8;
                    eff_x += read_le32(g_effect_chain_anim_table, j_off + 0);
                    eff_y += read_le32(g_effect_chain_anim_table, j_off + 4);
                }
            }

            /* Blob + slot.  Retail reads a per-record blob pointer at
             * +0x00; the port uses a single shared blob (see the
             * `bind_effect_chain_shared_blob` notes on the layout
             * collision with next_id on 64-bit hosts).  Slot is still
             * per-record. */
            const uint8_t *rec_blob = g_effect_chain_shared_blob;
            int slot = read_le16s(rec, ENGINE_BATTLEFIELD_EFFECT_OFFSET_SLOT);

            if (rec_blob) {
                gfx_dispatch_set_last_glyph_index(slot);
                gfx_dispatch_stage_sprite_descriptor(rec_blob, slot);

                int glyph_off = gfx_glyph_glyph_offset();
                if (glyph_off < 1 || glyph_off > 0x4baf0) {
                    /* QUIRK: early-return WITHOUT cursor restore.  Retail
                     * `return;` skips the epilogue that restores
                     * DAT_00591524 / DAT_00591528. */
                    return;
                }

                gfx_glyph_set_cursor_x(g_viewport_x + eff_x + g_scroll_pad);
                gfx_glyph_set_cursor_y(g_viewport_y + eff_y + g_scroll_pad);

                gfx_dispatch_clip_x(g_clip_left, g_clip_right);
                gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

                /* Dirty box — 4×4 regardless of subtype.  Retail has two
                 * branches (subtype 5 vs other) that BOTH call the same
                 * dirty_rect_mark_box_tiled(cursor, cursor, 4, 4, 2).
                 * We collapse to one. */
                if (g_dirty_state) {
                    engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                                     gfx_glyph_cursor_x(),
                                                     gfx_glyph_cursor_y(),
                                                     4, 4, /*force_full=*/2);
                }

                int y_mode_before = gfx_dispatch_clip_y_mode();
                gfx_dispatch_dispatch_sprite_rle(rec_blob);
                if (y_mode_before != 5) {
                    g_effect_chain_blit_count += 1;
                }
            }
        }
        /* else: subtype 7 — skip render, fall through to chain walk. */

        /* Chain walk: re-read the next-id from THIS record (read via
         * g_effect_chain_current_id to mirror retail's DAT_00568994
         * indirection — a recursive call below could theoretically have
         * mutated `id`, but `g_effect_chain_current_id` is sticky). */
        const uint8_t *cur_rec =
            g_effect_chain_base
          + g_effect_chain_current_id * g_effect_chain_stride;
        int next_id = read_le16s(cur_rec,
                                 ENGINE_BATTLEFIELD_EFFECT_OFFSET_NEXT_ID);

        if (next_id == id) {
            /* Self-loop guard — chain points to itself; abort. */
            gfx_glyph_set_cursor_x(entry_x);
            gfx_glyph_set_cursor_y(entry_y);
            return;
        }

        id = next_id;
        g_effect_chain_iters += 1;
    }
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
 * Chip 5c — late-pass driver + per-cell creature draw + selection
 * marker (FUN_004bd233 / FUN_004be070 / FUN_004be33f).
 * ────────────────────────────────────────────────────────────────── */

/* Read a 16-bit little-endian signed short from a byte table at the
 * given byte offset.  Defined here so the late-pass leaves don't have
 * to share the chip 5a.iii effect-chain helper above. */
static int late_read_le16s(const uint8_t *base, int byte_offset)
{
    int16_t v;
    memcpy(&v, base + byte_offset, sizeof v);
    return (int)v;
}

/* FUN_004be070 (719 B) — per-cell creature draw leaf.
 *
 * Dispatched by the late-pass driver per cell with a non-zero
 * descriptor[+5] (entity id) AND a non-zero visibility-mask AND.
 *
 * Body shape (in retail order):
 *
 *   1. Save entry cursor (entry_x, entry_y).
 *   2. Read the anim-offset (off_x, off_y) from the per-(facing,
 *      anim_frame) lookup — HARDCODED z32 table regardless of zoom
 *      (DAT_004e4030, the `g_anim_table_z32` binding).  See quirk below.
 *   3. Apply X offset: cursor_x += off_x + 8 (always).
 *      Apply Y offset by entity type (read from entity record +0x12):
 *        type <  7   → cursor_y += off_y - 12.
 *        type == 10  → cursor_y += off_y - 12  (same as type<7 — dup branch).
 *        else        → cursor_y += off_y - 0x20.
 *          if type == 9 (additional) → cursor_y += 0x10  (net −16).
 *   4. Slot computation: read signed short from entity record +0x1a0.
 *      Shifted by `g_creature_slot_shift_amount` (right if sign<0, else
 *      left), plus 0x2e.
 *   5. Stage sprite via the creature sprite blob at the computed slot
 *      (16-byte stride — same layout the terrain pass uses).
 *   6. Glyph-offset gate [1, 0x4baf0].
 *   7. On gate PASS: set clip rect (using bound module clip), apply
 *      stage src-offset bump, dispatch the clipped-RLE blit family.
 *      Restore cursor to ENTRY.
 *      On gate FAIL: restore cursor to POST-OFFSET values (NOT entry).
 *      See quirk on the lopsided restore. */
static void late_leaf_draw_creature(int entity_id)
{
    g_late_creature_draw_count += 1;

    if (!g_entity_base || entity_id < 0 || entity_id > g_entity_max_id) {
        /* Defensive bound check — retail blindly indexes.  Without an
         * entity table bound, we have nothing to draw from. */
        return;
    }

    int entry_x = gfx_glyph_cursor_x();
    int entry_y = gfx_glyph_cursor_y();

    const uint8_t *rec = g_entity_base + entity_id * g_entity_stride;

    /* Anim offset — hardcoded z32 table regardless of zoom (quirk #95). */
    int off_x = 0, off_y = 0;
    if (g_anim_table_z32) {
        int8_t facing = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING];
        int8_t af     = (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME];
        int entry_off = (int)facing * ENGINE_BATTLEFIELD_ANIM_FACING_STRIDE
                      + (int)af     * ENGINE_BATTLEFIELD_ANIM_FRAME_STRIDE;
        off_x = read_le32(g_anim_table_z32, entry_off + 0);
        off_y = read_le32(g_anim_table_z32, entry_off + 4);
    }

    /* Apply offsets to cursor.  X always gets `off_x + 8`; Y depends on
     * type byte (read as unsigned). */
    uint8_t type = rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE];

    int new_x = entry_x + off_x + 8;
    int new_y;
    if (type < 7) {
        new_y = entry_y + off_y - 12;
    } else if (type == 10) {
        /* Duplicate of the `type < 7` branch — retail has it as a
         * separate else-if; preserved verbatim.  See quirk on dup. */
        new_y = entry_y + off_y - 12;
    } else {
        new_y = entry_y + off_y - 0x20;
        if (type == 9) {
            /* Type-9 gets a +0x10 Y bonus on top of the -0x20 base
             * (net -0x10 = -16 from entry). */
            new_y += 0x10;
        }
    }

    gfx_glyph_set_cursor_x(new_x);
    gfx_glyph_set_cursor_y(new_y);

    /* Slot computation. */
    int raw_slot = late_read_le16s(rec,
                       ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_SPRITE_SLOT);
    int shift    = g_creature_slot_shift_amount & 0x1f;
    int slot_shifted = (g_creature_slot_shift_sign < 0)
        ? (raw_slot >> shift)
        : (raw_slot << shift);
    int slot = slot_shifted + 0x2e;

    /* Stage sprite descriptor from the creature blob.  NULL blob ⇒
     * stage call would deref NULL — skip and restore cursor to ENTRY
     * (matches retail which would also crash here in practice, so any
     * defensive shape is acceptable; choose entry-restore to be
     * symmetric with the gate-pass path). */
    if (!g_creature_sprite_blob) {
        gfx_glyph_set_cursor_x(entry_x);
        gfx_glyph_set_cursor_y(entry_y);
        return;
    }

    gfx_dispatch_set_last_glyph_index(slot);
    gfx_dispatch_stage_sprite_descriptor(g_creature_sprite_blob, slot);

    int glyph_off = gfx_glyph_glyph_offset();

    /* Quirk #96: lopsided cursor restore.  On gate PASS, cursor goes
     * back to entry; on gate FAIL, cursor STAYS at (new_x, new_y).  The
     * driver only resets cursor_x at the start of each ROW (cursor_y
     * never resets within the frame), so a fail-state leaks into the
     * rest of the row's creature draws AND every subsequent row's
     * cursor_y. */
    if (glyph_off >= 1 && glyph_off <= 0x4baf0) {
        gfx_dispatch_clip_x(g_clip_left, g_clip_right);
        gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

        int y_mode_before = gfx_dispatch_clip_y_mode();
        gfx_dispatch_dispatch_sprite_rle(g_creature_sprite_blob);
        if (y_mode_before != 5) {
            g_late_creature_blit_count += 1;
        }

        /* Restore to ENTRY (retail iVar4 = iVar2; iVar1 = iVar3). */
        gfx_glyph_set_cursor_x(entry_x);
        gfx_glyph_set_cursor_y(entry_y);
    }
    /* else: cursor stays at (new_x, new_y) — quirk #96. */
}

/* FUN_004be33f (416 B) — selection-marker leaf.  Draws a fixed-slot
 * (0x3f) sprite at the active hero/army's world position, plus marks
 * a 3×3 box of world-map cells dirty.
 *
 * Cursor: ((marker_x - cam_x) * 0x20 + viewport_x + 2,
 *          (marker_y - cam_y) * 0x20 + viewport_y + 9).
 *
 * Dirty mark: 3×3 tile box at the computed cursor, force_full=2.
 * Runs UNCONDITIONALLY (above the glyph-offset gate).
 *
 * Glyph-offset gate [1, 0x4baf0]:
 *   PASS  → set clip + dispatch RLE blit + invoke world-map dirty hook.
 *   FAIL  → skip both.
 *
 * Cursor is NOT restored — the leaf leaves cursor at the marker
 * position.  Caller (driver) doesn't read cursor after this leaf, so
 * no observable effect. */
static void late_leaf_draw_selection_marker(void)
{
    g_late_selection_marker_count += 1;

    int px_x = (g_selection_marker_x - g_camera_x) * 0x20 + g_viewport_x + 2;
    int px_y = (g_selection_marker_y - g_camera_y) * 0x20 + g_viewport_y + 9;

    gfx_glyph_set_cursor_x(px_x);
    gfx_glyph_set_cursor_y(px_y);

    if (g_dirty_state) {
        engine_dirty_rect_mark_box_tiled(g_dirty_state,
                                         px_x, px_y, 3, 3, /*force_full=*/2);
    }

    if (!g_creature_sprite_blob) {
        return;
    }

    /* Fixed slot 0x3f. */
    int slot = 0x3f;
    gfx_dispatch_set_last_glyph_index(slot);
    gfx_dispatch_stage_sprite_descriptor(g_creature_sprite_blob, slot);

    int glyph_off = gfx_glyph_glyph_offset();
    if (glyph_off >= 1 && glyph_off <= 0x4baf0) {
        gfx_dispatch_clip_x(g_clip_left, g_clip_right);
        gfx_dispatch_clip_y(g_clip_top,  g_clip_bottom);

        int y_mode_before = gfx_dispatch_clip_y_mode();
        gfx_dispatch_dispatch_sprite_rle(g_creature_sprite_blob);
        if (y_mode_before != 5) {
            g_late_selection_marker_blit_count += 1;
        }

        /* World-map dirty hook: 3×3 box anchored at (marker - 1, -1) so
         * the dirty box covers the marker cell + one ring of neighbors. */
        if (g_world_dirty_hook) {
            g_world_dirty_hook(g_selection_marker_x - 1,
                               g_selection_marker_y - 1, 3, 3);
            g_late_world_dirty_hook_count += 1;
        }
    }
}

/* FUN_004bd233 (290 B) — late-pass driver.
 *
 * Walks visible cells (`rows × cols`, NO over-scan unlike middle pass);
 * for each cell with a non-zero descriptor[+5] AND a non-zero
 * visibility-mask AND, dispatches the per-cell creature draw.  After
 * the cell loop, dispatches the selection marker if the gate (retail
 * DAT_00553f84) is positive.
 *
 * Per-cell pitch is HARDCODED 0x20 (32 px) — does NOT respect the
 * bound zoom.  At zoom 16, the late pass misaligns with the
 * terrain/middle/overlay passes (cursor advances at zoom-32 spacing
 * over a zoom-16 grid).  This is quirk #97: the late pass is
 * effectively zoom-32-only despite being dispatched unconditionally.
 *
 * Descriptor cursor advances by `desc_stride` per cell, `row_wrap`
 * per row — same convention as terrain/middle passes.  Initial offset
 * is `(cam_y * row_stride + cam_x) * desc_stride`. */
static void pass_late(void)
{
    g_count_late += 1;

    if (!g_desc_base) {
        /* Without descriptors there's no cell-loop body to run.  The
         * selection marker still fires below (it doesn't need the
         * descriptor table). */
    } else {
        int desc_cursor = (g_camera_row_stride * g_camera_y + g_camera_x)
                        * g_desc_stride;

        /* Initial cursor_y from viewport (retail DAT_00591528 = ...). */
        gfx_glyph_set_cursor_y(g_viewport_y);

        for (int row = 0; row < g_grid_rows; row++) {
            /* Reset cursor_x per row from viewport. */
            gfx_glyph_set_cursor_x(g_viewport_x);

            for (int col = 0; col < g_grid_cols; col++) {
                g_late_per_cell_count += 1;

                uint8_t entity_byte = g_desc_base[
                    desc_cursor
                    + ENGINE_BATTLEFIELD_DESC_OFFSET_LATE_CREATURE_ID];

                if (entity_byte != 0) {
                    /* Visibility mask AND.  Both bytes must be present
                     * AND non-zero in the AND. */
                    uint8_t per_player_byte = 0;
                    if (g_late_player_vis_base) {
                        per_player_byte = g_late_player_vis_base[
                            g_late_player_id * g_late_player_vis_stride];
                    }
                    uint8_t per_entity_byte = 0;
                    if (entity_byte <= g_entity_max_id && g_entity_base) {
                        per_entity_byte = g_entity_base[
                            entity_byte * g_entity_stride
                            + ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_VISIBILITY_MASK];
                    }
                    if ((per_player_byte & per_entity_byte) != 0) {
                        late_leaf_draw_creature((int)entity_byte);
                    }
                }

                desc_cursor += g_desc_stride;
                /* Per-cell pitch: HARDCODED 0x20.  See quirk #97. */
                gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + 0x20);
            }

            desc_cursor += g_grid_row_wrap;
            gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + 0x20);
        }
    }

    /* Selection marker — gated on the marker gate (retail
     * `if (0 < DAT_00553f84)`). */
    if (g_selection_marker_gate > 0) {
        late_leaf_draw_selection_marker();
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Chip 5d — final pass (FUN_004bf1f9).
 *
 * Debug-mode overlay drawing a per-creature numeric label on each
 * visible cell.  Gated entirely on the debug-mode flag (retail
 * DAT_005bb4a5) — pass is a no-op when zero.
 *
 * Per-cell shape:
 *   1. Skip when entity_id (descriptor[+5]) is 0.
 *   2. Compute a one-byte sum of the first 0x19e bytes of the entity
 *      record (port of retail FUN_00440089(0x19e, ...)).  Sign-extend
 *      the sum (signed char → int).
 *   3. Three-way bucket:
 *        sum < 0x65  → value = sum,       pen = 0x3f.
 *        sum < 0xc9  → value = sum - 100, pen = 0xf9.
 *        else        → value = sum - 200, pen = 0x20.
 *   4. Skip render when value == 0 OR value == 999.
 *   5. Render decimal label at
 *        (col * zoom + viewport_x + 1, row * zoom + viewport_y + 1)
 *      with prefix=' ', suffix from `g_debug_label_suffix`, font from
 *      `g_debug_label_font` + `g_debug_label_char_glyph_table`, color
 *      from the bucket pen.
 *
 * Cell pitch is ZOOM-AWARE (uses `g_zoom`) — unlike the late pass's
 * hardcoded 0x20.  Same visible-grid extent (rows × cols, no over-
 * scan). */

/* Sum the first `len` bytes of `base`, returning the low byte cast
 * to signed char (matches retail FUN_00440089).  Used for the
 * per-creature record digest the final pass renders. */
static int final_byte_sum_signed(const uint8_t *base, int len)
{
    uint8_t acc = 0;
    for (int i = 0; i < len; i++) {
        acc = (uint8_t)(acc + base[i]);
    }
    return (int)(int8_t)acc;
}

static void pass_final(void)
{
    g_count_final += 1;

    /* Outer gate — pass is a no-op when debug flag is off.  Even the
     * per-cell counter does NOT bump (retail's `if` wraps the whole
     * body). */
    if (!g_debug_label_enabled) {
        return;
    }

    if (!g_desc_base || !g_entity_base) {
        return;
    }

    int desc_cursor = (g_camera_row_stride * g_camera_y + g_camera_x)
                    * g_desc_stride;

    for (int row = 0; row < g_grid_rows; row++) {
        for (int col = 0; col < g_grid_cols; col++) {
            g_final_per_cell_count += 1;

            uint8_t entity_byte = g_desc_base[
                desc_cursor
                + ENGINE_BATTLEFIELD_DESC_OFFSET_LATE_CREATURE_ID];

            if (entity_byte != 0
                && entity_byte <= g_entity_max_id) {
                const uint8_t *rec = g_entity_base
                                   + entity_byte * g_entity_stride;

                /* Signed-byte sum of first 0x19e bytes of the entity
                 * record (port of retail FUN_00440089). */
                int sum = final_byte_sum_signed(rec, 0x19e);

                /* Three-way bucket.  Retail uses signed `<` so negative
                 * sums (high bit set in the byte) hit the first
                 * branch unchanged. */
                int value;
                uint8_t pen;
                if (sum < 0xc9) {
                    if (sum < 0x65) {
                        value = sum;
                        pen   = 0x3f;
                    } else {
                        value = sum - 100;
                        pen   = 0xf9;
                    }
                } else {
                    value = sum - 200;
                    pen   = 0x20;
                }

                /* Magic-value filter — skip when 0 or 999. */
                if (value != 0 && value != 999
                    && g_debug_label_font
                    && g_debug_label_char_glyph_table) {
                    int px_x = col * g_zoom + g_viewport_x + 1;
                    int px_y = row * g_zoom + g_viewport_y + 1;
                    const char *suffix = g_debug_label_suffix
                        ? g_debug_label_suffix : "";

                    gfx_text_label_render_dec(
                        value, ' ', suffix,
                        px_x, px_y,
                        g_debug_label_font,
                        g_debug_label_char_glyph_table,
                        pen);
                    g_final_label_render_count += 1;
                }
            }

            desc_cursor += g_desc_stride;
        }
        desc_cursor += g_grid_row_wrap;
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

int engine_battlefield_render_middle_entity_type6_blit_count(void)
{
    return g_middle_entity_type6_blit_count;
}
int engine_battlefield_render_middle_entity_type7_blit_count(void)
{
    return g_middle_entity_type7_blit_count;
}
int engine_battlefield_render_middle_entity_type9_sub_blit_count(void)
{
    return g_middle_entity_type9_sub_blit_count;
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

void engine_battlefield_render_run_pass_late_for_test(int camera_x, int camera_y)
{
    g_camera_x = camera_x;
    g_camera_y = camera_y;
    pass_late();
}

void engine_battlefield_render_run_pass_final_for_test(int camera_x, int camera_y)
{
    g_camera_x = camera_x;
    g_camera_y = camera_y;
    pass_final();
}

/* ──────────────────────────────────────────────────────────────────
 * Coordinate helpers — ports of FUN_004bc222 / 2a5 / 2c5 / 2e5 / 346
 * (chip 6) and FUN_004bc3ac / 40d / 471 / 4bb / 505 (chip 6b).
 *
 * Tiny pure functions consumed by the input layer to translate screen
 * pixel coords into the battlefield's hex column / row, plus signed
 * deltas in cells between two screen pixels and a zoom accessor.  Each
 * reads the same module statics the render passes do (viewport, zoom,
 * grid dims, camera_row_stride, total_rows, cell_band_width).
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

int engine_battlefield_render_pixel_to_row_bottom_biased(int py)
{
    int row = (py - g_viewport_y) / g_zoom;
    if (g_cell_band_width * 3 <= (py - g_viewport_y) % g_zoom) {
        row += 1;
    }
    if (row < 1) {
        row = 0;
    }
    return row;
}

int engine_battlefield_render_pixel_to_row_top_biased(int py)
{
    int row = (py - g_viewport_y) / g_zoom;
    if ((py - g_viewport_y) % g_zoom < g_cell_band_width) {
        row += -1;
    }
    if (g_total_rows <= row) {
        row = g_total_rows - 1;
    }
    return row;
}

int engine_battlefield_render_col_delta(int px_a, int px_b)
{
    return (px_b - g_viewport_x) / g_zoom
         - (px_a - g_viewport_x) / g_zoom;
}

int engine_battlefield_render_row_delta(int py_a, int py_b)
{
    return (py_b - g_viewport_y) / g_zoom
         - (py_a - g_viewport_y) / g_zoom;
}

int engine_battlefield_render_get_zoom(void)
{
    return g_zoom;
}

int engine_battlefield_render_middle_player_marker_blit_count(void)
{
    return g_player_marker_blit_count;
}
int engine_battlefield_render_middle_unit_key_blit_count(void)
{
    return g_unit_key_blit_count;
}
int engine_battlefield_render_middle_effect_chain_blit_count(void)
{
    return g_effect_chain_blit_count;
}

int engine_battlefield_render_player_marker_anim_frame(void)
{
    return g_player_marker_anim_frame;
}
int engine_battlefield_render_effect_chain_anim_frame(void)
{
    return g_effect_chain_anim_frame;
}

int engine_battlefield_render_middle_effect_chain_iters(void)
{
    return g_effect_chain_iters;
}

int engine_battlefield_render_middle_player_marker_last_slot(void)
{
    return g_player_marker_last_slot;
}
int engine_battlefield_render_middle_unit_key_last_slot(void)
{
    return g_unit_key_last_slot;
}

/* ─── Chip 5c accessors ─────────────────────────────────────────── */

int engine_battlefield_render_late_per_cell_count(void)
{
    return g_late_per_cell_count;
}
int engine_battlefield_render_late_creature_draw_count(void)
{
    return g_late_creature_draw_count;
}
int engine_battlefield_render_late_creature_blit_count(void)
{
    return g_late_creature_blit_count;
}
int engine_battlefield_render_late_selection_marker_count(void)
{
    return g_late_selection_marker_count;
}
int engine_battlefield_render_late_selection_marker_blit_count(void)
{
    return g_late_selection_marker_blit_count;
}
int engine_battlefield_render_late_world_dirty_hook_count(void)
{
    return g_late_world_dirty_hook_count;
}

/* ─── Chip 5d accessors ─────────────────────────────────────────── */

int engine_battlefield_render_final_per_cell_count(void)
{
    return g_final_per_cell_count;
}
int engine_battlefield_render_final_label_render_count(void)
{
    return g_final_label_render_count;
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

    /* chip 5a.iii state */
    g_player_marker_blit_count         = 0;
    g_player_marker_last_slot          = -1;
    g_player_marker_anim_frame         = 0;
    g_unit_key_blit_count              = 0;
    g_unit_key_last_slot               = -1;
    g_effect_chain_anim_frame          = 0;
    g_effect_chain_iters               = 0;
    g_effect_chain_blit_count          = 0;
    g_effect_chain_current_id          = 0;

    /* chip 5a.iv state */
    g_middle_entity_type6_blit_count     = 0;
    g_middle_entity_type7_blit_count     = 0;
    g_middle_entity_type9_sub_blit_count = 0;

    /* chip 5c state */
    g_late_per_cell_count                = 0;
    g_late_creature_draw_count           = 0;
    g_late_creature_blit_count           = 0;
    g_late_selection_marker_count        = 0;
    g_late_selection_marker_blit_count   = 0;
    g_late_world_dirty_hook_count        = 0;

    /* chip 5d state */
    g_final_per_cell_count               = 0;
    g_final_label_render_count           = 0;

    /* chip 7 state — counters only.  Scan position has its own reset
     * via engine_battlefield_render_reset_minimap_scan_pos. */
    g_minimap_per_cell_count             = 0;
    g_minimap_blit_count                 = 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Chip 7 — mini-map / hex-grid overview renderer.
 *
 * Ports of FUN_004b49b7 (the 2×2 raw blit primitive — landed in
 * gfx_sprite.c), FUN_004bc020 / 4bc107 (bulk binders), FUN_004bc1d1
 * (rolling-scroll trigger), and FUN_004bc51a (strip render).
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_bind_minimap_blob_primary(const uint8_t *blob)
{
    g_minimap_blob_primary = blob;
}

void engine_battlefield_render_bind_minimap_blob_alt(const uint8_t *blob)
{
    g_minimap_blob_alt = blob;
}

void engine_battlefield_render_bind_minimap_default_descriptor(const uint8_t *blob)
{
    g_minimap_default_descriptor = blob;
}

void engine_battlefield_render_bind_minimap_origin(int origin_x, int y_offset)
{
    g_minimap_origin_x = origin_x;
    g_minimap_y_offset = y_offset;
}

void engine_battlefield_render_set_minimap_mode_gate(int gate_value)
{
    g_minimap_mode_gate = gate_value;
}

void engine_battlefield_render_set_minimap_engine_state(int engine_state)
{
    g_minimap_engine_state = engine_state;
}

void engine_battlefield_render_bind_minimap_entity_owner_table(const uint8_t *table,
                                                                int stride)
{
    g_minimap_entity_owner_table  = table;
    g_minimap_entity_owner_stride = stride;
}

void engine_battlefield_render_bind_minimap_player_slot_table(const uint8_t *table,
                                                               int stride)
{
    g_minimap_player_slot_table  = table;
    g_minimap_player_slot_stride = stride;
}

void engine_battlefield_render_bind_hex_grid_context(
    const uint8_t *blob_primary,
    const uint8_t *blob_alt,
    uint8_t       *desc_base,
    int            row_stride_in_cells,
    int            total_rows,
    int            cell_stride_bytes,
    int            viewport_x,
    int            viewport_y,
    int            visible_cols,
    int            visible_rows,
    int            zoom)
{
    /* Retail FUN_004bc020 writes these fields directly to the shared
     * globals.  We dispatch through our existing per-field binders so
     * the bookkeeping (and any future binder-side validation) stays in
     * one place.  Order chosen to match retail's write order. */
    engine_battlefield_render_bind_blobs(blob_primary, blob_alt);
    engine_battlefield_render_bind_descriptor_table(desc_base, cell_stride_bytes);
    engine_battlefield_render_bind_camera_row_stride(row_stride_in_cells);
    engine_battlefield_render_bind_total_rows(total_rows);
    engine_battlefield_render_bind_viewport_offset(viewport_x, viewport_y);
    engine_battlefield_render_bind_grid(visible_cols, visible_rows, /*row_wrap=*/0);
    engine_battlefield_render_set_zoom(zoom);

    /* Derived values.  Retail computes:
     *   DAT_004e5d44 = zoom / 2                  → scroll_pad
     *   DAT_004e5d3c = (zoom + ...) >> 2         → cell_band_width (= zoom/4)
     *   _DAT_004e6550 = zoom * zoom              → unused elsewhere; skip
     *   DAT_004e5d70 = (row_stride - visible_cols) * cell_stride  → row-wrap;
     *                                              already derived inside
     *                                              bind_camera_row_stride
     *                                              + bind_grid + bind_desc_table
     *   DAT_004e6564 = viewport_x                → clip_left
     *   DAT_004e5d54 = visible_cols*zoom + vx    → clip_right
     *   DAT_004e5d48 = viewport_y                → clip_top
     *   DAT_004e5d6c = visible_rows*zoom + vy    → clip_bottom
     */
    engine_battlefield_render_bind_scroll_pad(zoom / 2);
    engine_battlefield_render_bind_cell_band_width(zoom / 4);
    engine_battlefield_render_bind_clip_rect(viewport_x,
                                             viewport_y,
                                             visible_cols * zoom + viewport_x,
                                             visible_rows * zoom + viewport_y);
}

void engine_battlefield_render_bind_minimap_context(
    const uint8_t *blob_primary,
    const uint8_t *blob_alt,
    const uint8_t *default_desc,
    int            origin_x,
    int            y_offset,
    int            mode_gate)
{
    /* Retail FUN_004bc107 writes 6 globals in order.  Same dispatch-
     * through-per-field-binder shape as the hex-grid context above. */
    engine_battlefield_render_bind_minimap_blob_primary(blob_primary);
    engine_battlefield_render_bind_minimap_blob_alt(blob_alt);
    engine_battlefield_render_bind_minimap_default_descriptor(default_desc);
    engine_battlefield_render_bind_minimap_origin(origin_x, y_offset);
    engine_battlefield_render_set_minimap_mode_gate(mode_gate);
}

/* Mini-map strip render — port of FUN_004bc51a @ 0x4bc51a (994 B).
 *
 * Walks `count` rows starting at `start_row` of the bound descriptor
 * table.  For each cell:
 *
 *   (a) Read flags = descriptor[+2], entity_id = descriptor[+5].
 *   (b) Process iff (force_redraw OR boundary-bit OR dirty-bit).
 *   (c) Dispatch one of four paths:
 *
 *         entity_id != 0    → look up sprite slot via owner-type
 *                             → player-slot indirection; type==6
 *                             short-circuits to slot 6.  Slot 0
 *                             ⇒ skip blit.  Blob = default desc.
 *                             Clear boundary bit.
 *         flags & 0x02      → boundary cell.  Slot 0 of default desc.
 *                             Clear boundary bit.
 *         first-col + state=3
 *                           → slot 0 of default desc (override).
 *         (flags & 0x1c)==0 → terrain.  Slot = descriptor[+3], blob =
 *                             minimap primary.
 *         (flags & 0x1c)==4 → terrain.  Slot = descriptor[+3], blob =
 *                             minimap alt.
 *         other 0x1c values → no draw (cell dropped).
 *
 *   (d) Advance cursor.  Descriptor cursor += cell_stride; screen
 *       cursor_x += 2 per cell; cursor_y += 2 per row.
 *
 * Each blit goes through the staging dispatcher
 * (`gfx_dispatch_stage_sprite_descriptor`) into the 2×2 raw blit
 * (`gfx_sprite_blit_2x2_raw`).  The dispatcher's cursor is the same
 * shared one used by every other sprite blit (`gfx_glyph_set_cursor_x/y`). */
static void minimap_dispatch_blit(const uint8_t *blob, int slot)
{
    if (!blob) return;
    gfx_dispatch_stage_sprite_descriptor(blob, slot);
    gfx_sprite_blit_2x2_raw(blob);
    g_minimap_blit_count += 1;
}

static void minimap_render_strip(int start_row, int count)
{
    if (!g_desc_base) return;
    if (count <= 0)   return;

    int desc_cursor = g_desc_stride * g_camera_row_stride * start_row;
    int cursor_y    = start_row * 2 + g_minimap_y_offset;

    for (int row = start_row; row < start_row + count; row++) {
        int cursor_x = g_minimap_origin_x;

        for (int col = 0; col < g_camera_row_stride; col++) {
            g_minimap_per_cell_count += 1;

            uint8_t flags     = g_desc_base[desc_cursor + 2];
            uint8_t entity_id = g_desc_base[desc_cursor + 5];

            int gate = (g_force_redraw != 0)
                    || (flags & 0x02)
                    || (flags & 0x01);
            if (gate) {
                gfx_glyph_set_cursor_x(cursor_x);
                gfx_glyph_set_cursor_y(cursor_y);

                if (entity_id == 0) {
                    /* Terrain / boundary path. */
                    if ((flags & 0x02) == 0) {
                        /* Not boundary. */
                        if (col == 0 && g_minimap_engine_state == 3) {
                            /* First-col-ingame override → default desc slot 0. */
                            minimap_dispatch_blit(g_minimap_default_descriptor, 0);
                        } else {
                            /* Normal terrain — blob selected by flags & 0x1c. */
                            int slot = g_desc_base[desc_cursor + 3];
                            int sel  = flags & 0x1c;
                            if (sel == 0) {
                                minimap_dispatch_blit(g_minimap_blob_primary, slot);
                            } else if (sel == 4) {
                                minimap_dispatch_blit(g_minimap_blob_alt, slot);
                            }
                            /* sel ∈ {8,12,16,20,24,28} → cell dropped. */
                        }
                    } else {
                        /* Boundary cell — clear bit 2 + render default. */
                        g_desc_base[desc_cursor + 2] = flags & 0xfd;
                        minimap_dispatch_blit(g_minimap_default_descriptor, 0);
                    }
                } else {
                    /* Entity cell.  Clear boundary bit; look up slot. */
                    g_desc_base[desc_cursor + 2] = flags & 0xfd;

                    int slot;
                    if (!g_minimap_entity_owner_table) {
                        slot = 0;
                    } else {
                        uint8_t owner = g_minimap_entity_owner_table[
                            entity_id * g_minimap_entity_owner_stride];
                        if (owner == 6) {
                            slot = 6;
                        } else if (!g_minimap_player_slot_table) {
                            slot = 0;
                        } else {
                            slot = g_minimap_player_slot_table[
                                owner * g_minimap_player_slot_stride];
                        }
                    }
                    if (slot != 0) {
                        minimap_dispatch_blit(g_minimap_default_descriptor, slot);
                    }
                }
            }

            desc_cursor += g_desc_stride;
            cursor_x    += 2;
        }
        cursor_y += 2;
    }
}

void engine_battlefield_render_minimap_scroll(int rows)
{
    g_minimap_scan_pos += rows;
    if (g_total_rows - rows < g_minimap_scan_pos) {
        g_minimap_scan_pos = 0;
    }
    if (g_minimap_mode_gate == 2) {
        minimap_render_strip(g_minimap_scan_pos, rows);
    }
}

void engine_battlefield_render_minimap_render_strip_for_test(int start_row,
                                                              int count)
{
    minimap_render_strip(start_row, count);
}

int engine_battlefield_render_minimap_scan_pos(void)
{
    return g_minimap_scan_pos;
}

void engine_battlefield_render_reset_minimap_scan_pos(void)
{
    g_minimap_scan_pos = 0;
}

int engine_battlefield_render_minimap_per_cell_count(void)
{
    return g_minimap_per_cell_count;
}

int engine_battlefield_render_minimap_blit_count(void)
{
    return g_minimap_blit_count;
}
