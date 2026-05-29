/*
 * OpenLords2 — strategic-mode overworld map renderer (implementation).
 *
 * See engine_map_render.h for the per-function map; see
 * docs/findings/map-render.md for the format documentation.
 */

#include "engine_map_render.h"

#include "call_trace.h"
#include "engine_dirty_rect.h"
#include "gfx_dispatch.h"
#include "gfx_glyph.h"

#include <stddef.h>

/* ──────────────────────────────────────────────────────────────────
 * Palette tables — six 128-byte signed-byte (X, Y) displacement
 * tables.  Retail bakes them as static-const at 0x4d8108..0x4d8388
 * in .rdata.  Indexed by `[view_mode][axis][rotation * 16 + frame]`.
 *
 * NOTE: these are EXACT copies of the retail .rdata bytes — verified
 * via r2 `pxw 0x80 @ <addr>`.  Each table is 8 rows of 16 entries.
 * ────────────────────────────────────────────────────────────────── */

static const int8_t g_palette_tables[3][2][128] = {
    /* view-mode 0 */
    {
        /* X table @ 0x4d8108 */
        {
             0,-28,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,
             0,-48,-44,-40,-36,-32,-28,-24,-20,-16,-12, -8, -4,  0,  4,  0,
             0,-28,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,
             0, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12,  8,  4,  0,
             0, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
        /* Y table @ 0x4d8188 */
        {
             0, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,-12,-13,-14,  0, -7, -8, -9,-10,-11,  0, -2, -3, -4, -5, -6,
             0,-28,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,
             0,-12,-13,-14,  0, -7, -8, -9,-10,-11,  0, -2, -3, -4, -5, -6,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
             0, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,
        },
    },
    /* view-mode 1 */
    {
        /* X table @ 0x4d8208 */
        {
             0,-13,-12, -8, -7, -7, -8, -8, -9,-10,-11,-12,-13,-13,-12,  0,
             0,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,  0,
             0,-13,-12, -8, -7, -7, -8, -8, -9,-10,-11,-12,-13,-13,-12,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, 13, 12,  8,  7,  7,  8,  8,  9, 10, 11, 12, 13, 13, 12,  0,
             0, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,  0,
             0, 13, 12,  8,  7,  7,  8,  8,  9, 10, 11, 12, 13, 13, 12,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
        /* Y table @ 0x4d8288 */
        {
             0,  6,  4,  4,  4,  3,  3,  3,  3,  3,  2,  2,  2,  1,  1,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, -1, -1, -2, -2, -2, -3, -3, -3, -3, -4, -4, -4, -4, -6, -6,
             0, -6, -4, -4, -4, -3, -3, -3, -3, -3, -2, -2, -2, -1, -1,  0,
             0, -1, -1, -2, -2, -2, -3, -3, -3, -3, -4, -4, -4, -4, -6, -6,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  6,  4,  4,  4,  3,  3,  3,  3,  3,  2,  2,  2,  1,  1,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
    },
    /* view-mode 2 */
    {
        /* X table @ 0x4d8308 */
        {
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
        /* Y table @ 0x4d8388 */
        {
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
    },
};

/* ──────────────────────────────────────────────────────────────────
 * Module state — all retail DAT_* globals the cluster touches that
 * are NOT already owned by gfx_dispatch / gfx_glyph / engine_dirty_rect.
 * ────────────────────────────────────────────────────────────────── */

/* Tile array binding (DAT_0055cea0, stride DAT_0055cea0 ... 0x104). */
static const int32_t *g_tile_cells;
static int            g_tile_row_stride_bytes;

/* Tile descriptor table (DAT_0052290 region, byte-indexed). */
static const uint8_t *g_tile_descriptors;

/* Territory record array (DAT_0052f0b0, 151 × 0x1a4 B, slot 0 unused). */
static const uint8_t *g_territory_records;

/* Sprite blob bases (DAT_00553224 primary, DAT_0056d8bc alt,
 * DAT_0055ce5c overlay-RLE). */
static const uint8_t *g_blob_primary;
static const uint8_t *g_blob_alt;
static const uint8_t *g_blob_overlay;

/* Camera (DAT_0052afdc, DAT_00553244, DAT_005651b4, DAT_005651b8,
 * DAT_0053e8ac, DAT_0056d67c, DAT_00568220, DAT_0053f65c, DAT_00567950). */
static int g_cam_x0;
static int g_cam_y0;
static int g_cam_col_start;
static int g_cam_row_start;
static int g_cam_col_count;
static int g_cam_row_count;
static int g_cam_x_step;
static int g_cam_y_step;
static int g_cam_x_half_step;

/* View mode, render mode, rotation offset, overlay-base, Y viewport offset. */
static int g_view_mode;          /* DAT_0057cb18 */
static int g_render_mode;        /* DAT_0053f264 */
static int g_rotation_offset;    /* DAT_00522f7c */
static int g_overlay_base_index; /* DAT_0057d390 */
static int g_y_viewport_offset;  /* DAT_0057c97c */

/* Walker state — retail DAT_0056d594 (current row), DAT_0056d598
 * (current col), DAT_0056957c (column-Y accumulator).  Mutated by
 * the iterators across calls. */
static int g_cur_row;
static int g_cur_col;
static int g_col_y_accum;

/* Current cell scalar — retail DAT_00567954.  The iterators load it
 * from the 2-D tile array; the per-tile drawer reads it as a byte
 * offset into the per-tile-bytecode descriptor region. */
static int g_current_cell;

/* Dirty-rect state pointer (binding for test friendliness). */
static engine_dirty_rect_state_t *g_dirty_state;

/* Render error counter (retail _DAT_0058fea8). */
static int g_error_count;

/* Test instrumentation — call count for the per-tile drawer.  Bumped
 * once per draw_tile invocation regardless of how many records its
 * linked list yields. */
static int g_call_count;

/* Retail constants — alt-clip Y top stays at 0x18 always; alt-clip
 * Y bottom differs by view-mode (0x1d8 for view-mode 1, 0x1da otherwise).
 * The alt-clip X right is the retail dispatcher's GFX_DISPATCH_ALT_CLIP_RIGHT
 * (0x1de); X left is the retail global DAT_0052afdc which we already
 * route through gfx_dispatch's alt_clip_left (synced via this
 * module's g_cam_x0 mirror — see below). */
#define MAP_RENDER_CLIP_Y_TOP                0x18
#define MAP_RENDER_CLIP_Y_BOTTOM_VIEW_1      0x1d8
#define MAP_RENDER_CLIP_Y_BOTTOM_DEFAULT     0x1da
#define MAP_RENDER_CLIP_X_RIGHT              0x1de

/* Retail src_offset upper bound from validation gate (line 84 of
 * FUN_00408438) — 310000.  Sprites with offsets above this trip the
 * error counter and abort the entire FUN_00408438 invocation. */
#define MAP_RENDER_SRC_OFFSET_MAX            310000

/* Bottom phantom row's column-X start — retail (line 25 of
 * FUN_00405862) hard-codes the test "col < 0x40" (= 64) to gate the
 * extra trailing cell.  Documented as a quirk. */
#define MAP_RENDER_TRAILING_COL_GATE         0x40

/* ──────────────────────────────────────────────────────────────────
 * Bind / config setters
 * ────────────────────────────────────────────────────────────────── */

void engine_map_render_bind_tile_array(const int32_t *cells, int row_stride_bytes)
{
    g_tile_cells            = cells;
    g_tile_row_stride_bytes = row_stride_bytes;
}

void engine_map_render_bind_tile_descriptors(const uint8_t *descriptors)
{
    g_tile_descriptors = descriptors;
}

void engine_map_render_bind_blobs(const uint8_t *primary,
                                  const uint8_t *alt,
                                  const uint8_t *overlay)
{
    g_blob_primary = primary;
    g_blob_alt     = alt;
    g_blob_overlay = overlay;
}

void engine_map_render_bind_territory_records(const uint8_t *records)
{
    g_territory_records = records;
}

void engine_map_render_bind_camera(int x0, int y0,
                                   int col_start, int row_start,
                                   int col_count, int row_count,
                                   int x_step, int y_step,
                                   int x_half_step)
{
    g_cam_x0           = x0;
    g_cam_y0           = y0;
    g_cam_col_start    = col_start;
    g_cam_row_start    = row_start;
    g_cam_col_count    = col_count;
    g_cam_row_count    = row_count;
    g_cam_x_step       = x_step;
    g_cam_y_step       = y_step;
    g_cam_x_half_step  = x_half_step;
}

void engine_map_render_set_y_accumulator(int y_acc)        { g_col_y_accum = y_acc; }
void engine_map_render_set_view_mode(int mode)             { g_view_mode = mode; }
void engine_map_render_set_rotation_offset(int rot)        { g_rotation_offset = rot; }
void engine_map_render_set_render_mode(int mode)           { g_render_mode = mode; }
void engine_map_render_set_overlay_base_index(int idx)     { g_overlay_base_index = idx; }
void engine_map_render_set_dirty_state(engine_dirty_rect_state_t *s) { g_dirty_state = s; }

int  engine_map_render_error_count(void)                   { return g_error_count; }
void engine_map_render_set_error_count(int n)              { g_error_count = n; }

int  engine_map_render_call_count(void)                    { return g_call_count; }
void engine_map_render_reset_call_count(void)              { g_call_count = 0; }

/* Internal helper — the y-viewport-offset setter (not in header until
 * we need it).  Currently exposed via the camera bind isn't quite
 * right; the field is independent.  Add a per-axis setter. */
void engine_map_render_set_y_viewport_offset(int y_off)    { g_y_viewport_offset = y_off; }

/* ──────────────────────────────────────────────────────────────────
 * Per-tile drawer — port of FUN_00408438 @ 0x408438 (2072 B).
 *
 * For each record in the per-tile linked list:
 *   1. Resolve sprite blob (primary or alt-by-type-4).
 *   2. Compute (dx, dy) from view-mode palette tables.
 *   3. Apply phase + type position adjusts; pick dirty box size.
 *   4. Save cursor; stage primary descriptor; validate src/w; on bad
 *      values bump error counter and ABORT the whole loop.
 *   5. Mark dirty rect.
 *   6. Set clip, dispatch first-family blitter.
 *   7. (Optional overlay for type==1 with byte!=0, or type==2):
 *      stage overlay descriptor, validate, clip, dispatch RLE family,
 *      mark 48-px dirty.
 *   8. Restore cursor; if next-link == self, return.
 * ────────────────────────────────────────────────────────────────── */

void engine_map_render_tile(int phase)
{
    CALL_TRACE_ENTER(0x00408438);
    g_call_count += 1;

    /* Retail line 18 — head of linked list, indexed by the current
     * cell value through the descriptor table at byte offset +5.
     * The iterators stage the cell value into `g_current_cell`
     * (retail `DAT_00567954`) just before invoking us. */
    int cell_byte_index = g_current_cell;
    int local_28 = (uint8_t)g_tile_descriptors[cell_byte_index + 5];

    /* Render-mode gate: when in mode 1, only tiles with descriptor
     * flag byte bit 0x20 set are drawn.  Other modes draw all. */
    if (g_render_mode == 1
        && (g_tile_descriptors[cell_byte_index + 2] & 0x20) == 0)
    {
        return;
    }

    while (local_28 != 0) {
        const uint8_t *rec = g_territory_records + local_28 * 0x1a4;
        uint8_t rec_overlay_byte = rec[0x02];
        uint8_t rec_sprite_idx   = rec[0x07];
        uint8_t rec_type         = rec[0x08];
        uint8_t rec_orient_byte  = rec[0x09];
        int8_t  rec_frame_off    = (int8_t)rec[0x149];

        /* Pick blob base: type 4 uses alt, all others use primary. */
        const uint8_t *blob = (rec_type == 4) ? g_blob_alt : g_blob_primary;

        /* Rotation index — retail line 26 — orientation minus the
         * global rotation offset, wrapping into [0, 7] by adding 8
         * when negative.  Note: retail does the wrap ONCE only, so
         * inputs that produce values outside [-8, 7] are observable
         * (logged as a quirk). */
        int rot = (int)(int8_t)rec_orient_byte - g_rotation_offset;
        if (rot < 0) rot += 8;

        /* Palette lookup — view-mode × axis × (rot * 16 + frame_off).
         * `rot * 16 + frame_off` is signed-byte-clipped via the int8_t
         * frame_offset's sign extension. */
        int v = (g_view_mode >= 0 && g_view_mode <= 2) ? g_view_mode : 0;
        int idx = rot * 0x10 + (int)rec_frame_off;
        int local_1c = (int)g_palette_tables[v][0][idx & 0x7f];
        int local_20 = (int)g_palette_tables[v][1][idx & 0x7f];

        /* Phase adjust.  Retail FUN_00408438 line 48-56. */
        if (phase == 1) {
            local_1c += -2;
        } else if (phase == 2) {
            local_1c += g_cam_x_half_step + -2;
        } else {
            local_1c += g_cam_x_half_step;
        }

        /* Y viewport offset (retail DAT_0057c97c). */
        local_20 += g_y_viewport_offset;

        /* Type-driven position adjust — selects 64-px dirty box for
         * decorated types 1/2/3/4; default 32-px for type 0. */
        int local_24 = 0;
        if (rec_type == 3) {
            local_20 += -2;
            local_1c += -4;
            local_24 = 3;
        } else if (rec_type == 1) {
            local_20 += -4;
            local_24 = 3;
        } else if (rec_type == 2) {
            local_20 += -4;
            local_24 = 3;
        } else if (rec_type == 4) {
            local_20 += -2;
            local_1c += -4;
            local_24 = 3;
        }

        /* Stage primary descriptor (sprite_index from record's +0x07
         * byte). */
        gfx_dispatch_set_last_glyph_index((int)rec_sprite_idx);
        gfx_dispatch_stage_sprite_descriptor(blob, (int)rec_sprite_idx);

        /* Read back the staged descriptor's metrics. */
        int sprite_w = gfx_glyph_glyph_w();
        int h_short  = gfx_dispatch_glyph_height_short();
        int src_off  = gfx_glyph_glyph_offset();

        /* Validation gate — invalid descriptor aborts the whole
         * function (NOT just this record's iteration). */
        if (src_off > MAP_RENDER_SRC_OFFSET_MAX) {
            g_error_count += 1;
            return;
        }
        if (sprite_w < 1) {
            g_error_count += 1;
            return;
        }

        /* Save current cursor (the camera-walker's anchor for this
         * cell), then set the sprite-anchor cursor. */
        int anchor_x = gfx_glyph_cursor_x();
        int anchor_y = gfx_glyph_cursor_y();
        int cur_x = anchor_x + local_1c - (sprite_w >> 1);
        int cur_y = anchor_y + local_20 - h_short;
        gfx_glyph_set_dest(cur_x, cur_y);

        /* Mark dirty rect — picks 32/48/64-px box by `local_24`.
         * Note: only 32 (default) and 64 (local_24==3) paths are
         * reachable from this function; the 48 case is dead. */
        int tile_x = gfx_glyph_cursor_x() >> 4;
        int tile_y = gfx_glyph_cursor_y() >> 4;
        if (local_24 == 3) {
            engine_dirty_rect_mark_tile_64(g_dirty_state, tile_x, tile_y);
        } else if (local_24 == 2) {
            engine_dirty_rect_mark_tile_48(g_dirty_state, tile_x, tile_y);
        } else {
            engine_dirty_rect_mark_tile_32(g_dirty_state, tile_x, tile_y);
        }

        /* Save the (post-anchor, pre-clip) cursor for the overlay
         * path.  Retail re-reads DAT_00591524/8 here. */
        int pre_clip_x = gfx_glyph_cursor_x();
        int pre_clip_y = gfx_glyph_cursor_y();

        /* Clip setup — alt-clip-aware X, view-mode-aware Y bottom. */
        gfx_dispatch_clip_x(g_cam_x0, MAP_RENDER_CLIP_X_RIGHT);
        if (g_view_mode == 1) {
            gfx_dispatch_clip_y(MAP_RENDER_CLIP_Y_TOP,
                                MAP_RENDER_CLIP_Y_BOTTOM_VIEW_1);
        } else {
            gfx_dispatch_clip_y(MAP_RENDER_CLIP_Y_TOP,
                                MAP_RENDER_CLIP_Y_BOTTOM_DEFAULT);
        }

        /* Apply top/left skip + dispatch first-family blitter. */
        gfx_dispatch_apply_skip_and_dispatch_sprite(blob);

        /* Overlay sprite — only for record types 1 (with overlay
         * byte set) or 2. */
        int overlay_idx = 999;  /* sentinel — 999 means "no overlay" */
        int overlay_x = 0, overlay_y = 0;
        if (rec_type == 1 && rec_overlay_byte != 0) {
            overlay_idx = (int)rec_overlay_byte * 8 + -8 + g_overlay_base_index;
            if (g_view_mode == 0) {
                overlay_x = pre_clip_x + 0x12;
                overlay_y = pre_clip_y + -0x15;
            } else if (g_view_mode == 2) {
                overlay_x = pre_clip_x + 0x18;
                overlay_y = pre_clip_y + -0x15;
            } else {
                overlay_x = pre_clip_x;
                overlay_y = pre_clip_y;
            }
            gfx_glyph_set_dest(overlay_x, overlay_y);
            engine_dirty_rect_mark_tile_32(g_dirty_state,
                                           overlay_x >> 4,
                                           overlay_y >> 4);
        } else if (rec_type == 2) {
            overlay_idx = g_overlay_base_index + 0x79;
            if (g_view_mode == 0) {
                overlay_x = pre_clip_x + 0x12;
                overlay_y = pre_clip_y + -0x12;
            } else if (g_view_mode == 2) {
                overlay_x = pre_clip_x + 4;
                overlay_y = pre_clip_y + -4;
            } else {
                overlay_x = pre_clip_x;
                overlay_y = pre_clip_y;
            }
            gfx_glyph_set_dest(overlay_x, overlay_y);
            engine_dirty_rect_mark_tile_32(g_dirty_state,
                                           overlay_x >> 4,
                                           overlay_y >> 4);
        }

        if (overlay_idx != 999) {
            gfx_dispatch_set_last_glyph_index(overlay_idx);
            gfx_dispatch_stage_sprite_descriptor(g_blob_overlay, overlay_idx);

            int o_sprite_w = gfx_glyph_glyph_w();
            int o_src_off  = gfx_glyph_glyph_offset();
            if (o_src_off > MAP_RENDER_SRC_OFFSET_MAX) {
                g_error_count += 1;
                return;
            }
            if (o_sprite_w < 1) {
                g_error_count += 1;
                return;
            }

            gfx_dispatch_clip_x(g_cam_x0, MAP_RENDER_CLIP_X_RIGHT);
            if (g_view_mode == 1) {
                gfx_dispatch_clip_y(MAP_RENDER_CLIP_Y_TOP,
                                    MAP_RENDER_CLIP_Y_BOTTOM_VIEW_1);
            } else {
                gfx_dispatch_clip_y(MAP_RENDER_CLIP_Y_TOP,
                                    MAP_RENDER_CLIP_Y_BOTTOM_DEFAULT);
            }

            gfx_dispatch_dispatch_sprite_rle(g_blob_overlay);

            engine_dirty_rect_mark_tile_48(g_dirty_state,
                                           gfx_glyph_cursor_x() >> 4,
                                           gfx_glyph_cursor_y() >> 4);
        }

        /* Restore cursor (the anchor) before walking the linked list. */
        gfx_glyph_set_dest(anchor_x, anchor_y);

        /* Linked-list termination — next-link == self ⇒ done. */
        int next = (int)(int16_t)(rec[0x04] | ((int16_t)rec[0x05] << 8));
        if (next == local_28) {
            return;
        }
        local_28 = next;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Phase-0 row iterator — port of FUN_00405862 @ 0x405862 (333 B).
 *
 * Three phases:
 *   (gated col_start > 0) extra left cell at (col_start - 1, cur_row)
 *                         with cursor_x = x0 - x_step;
 *   `col_count` cells at (col_start..col_start + col_count - 1, cur_row)
 *                         starting at cursor_x = x0;
 *   (gated col < 0x40)    extra right cell at (col_start + col_count, cur_row).
 *
 * All cells phase 0.  Advances cur_row, cursor_y, col_y_accum each by
 * one Y step at end.
 * ────────────────────────────────────────────────────────────────── */

void engine_map_render_row_phase0(void)
{
    CALL_TRACE_ENTER(0x00405862);
    if (g_cam_col_start > 0) {
        /* Extra left cell — retail reads from `&DAT_0055ce9c` (= base
         * - 4), effectively cell at col = col_start - 1. */
        gfx_glyph_set_cursor_x(g_cam_x0 - g_cam_x_step);
        g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                                  + (g_cam_col_start - 1) * 4
                                  + g_cur_row * g_tile_row_stride_bytes);
        if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
            engine_map_render_tile(0);
        }
    }

    gfx_glyph_set_cursor_x(g_cam_x0);
    g_cur_col = g_cam_col_start;

    for (int c = 0; c < g_cam_col_count; c++) {
        g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                                  + g_cur_col * 4
                                  + g_cur_row * g_tile_row_stride_bytes);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
            engine_map_render_tile(0);
        }
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
    }

    /* Extra trailing right cell — gated on col < 0x40.  Retail uses
     * the post-loop value of cur_col (== col_start + col_count). */
    if (g_cur_col < MAP_RENDER_TRAILING_COL_GATE) {
        g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                                  + g_cur_col * 4
                                  + g_cur_row * g_tile_row_stride_bytes);
        if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
            engine_map_render_tile(0);
        }
    }

    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_cam_y_step);
    g_cur_row    += 1;
    g_col_y_accum += g_cam_y_step;
}

/* ──────────────────────────────────────────────────────────────────
 * Phase-1 row iterator — port of FUN_004059af @ 0x4059af (314 B).
 *
 * First cell at col_start, drawn phase 1; cursor_x then += x_half_step.
 * Next col_count - 1 cells at col_start+1..col_start+col_count-1,
 * drawn phase 0.  One trailing cell at col_start + col_count, drawn
 * phase 0.  Advances cur_row, cursor_y, col_y_accum.
 * ────────────────────────────────────────────────────────────────── */

void engine_map_render_row_phase1(void)
{
    CALL_TRACE_ENTER(0x004059af);
    gfx_glyph_set_cursor_x(g_cam_x0);
    g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                              + g_cam_col_start * 4
                              + g_cur_row * g_tile_row_stride_bytes);
    g_cur_col = g_cam_col_start + 1;
    if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
        engine_map_render_tile(1);
    }
    gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_half_step);

    for (int c = 0; c < g_cam_col_count - 1; c++) {
        g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                                  + g_cur_col * 4
                                  + g_cur_row * g_tile_row_stride_bytes);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
            engine_map_render_tile(0);
        }
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
    }

    /* Final trailing cell — unconditional. */
    g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                              + g_cur_col * 4
                              + g_cur_row * g_tile_row_stride_bytes);
    g_cur_col += 1;
    if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
        engine_map_render_tile(0);
    }

    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_cam_y_step);
    g_cur_row    += 1;
    g_col_y_accum += g_cam_y_step;
}

/* ──────────────────────────────────────────────────────────────────
 * Frame driver — port of FUN_00405487 @ 0x405487 (379 B).
 *
 * Top phantom row (phase 0 cells, no extra cells) → cursor_y += y_step
 * → phase1_row → (row_count - 2) / 2 × (phase0_row, phase1_row)
 * → bottom phantom row (mirror of top, walking the SAME `cur_row` left
 *   over from the loop).
 * ────────────────────────────────────────────────────────────────── */

void engine_map_render_frame(void)
{
    CALL_TRACE_ENTER(0x00405487);
    gfx_glyph_set_cursor_y(g_cam_y0);
    gfx_glyph_set_cursor_x(g_cam_x0);
    g_cur_row     = g_cam_row_start;
    g_col_y_accum = 0;
    g_cur_col     = g_cam_col_start;

    /* Top phantom row — col_count cells, phase 0.  Cursor walks the
     * normal X stride; no extra left/right cells (retail line 14-21). */
    for (int c = 0; c < g_cam_col_count; c++) {
        g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                                  + g_cur_col * 4
                                  + g_cur_row * g_tile_row_stride_bytes);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
            engine_map_render_tile(0);
        }
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
    }

    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_cam_y_step);
    g_cur_row += 1;

    engine_map_render_row_phase1();

    int pair_count = (g_cam_row_count - 2) / 2;
    for (int r = 0; r < pair_count; r++) {
        engine_map_render_row_phase0();
        engine_map_render_row_phase1();
    }

    /* Bottom phantom row — mirror of top, on the same cur_row left
     * over from the loop. */
    gfx_glyph_set_cursor_x(g_cam_x0);
    g_cur_col = g_cam_col_start;

    for (int c = 0; c < g_cam_col_count; c++) {
        g_current_cell = *(const int32_t *)((const uint8_t *)g_tile_cells
                                  + g_cur_col * 4
                                  + g_cur_row * g_tile_row_stride_bytes);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_MAP_RENDER_CELL_EMPTY) {
            engine_map_render_tile(0);
        }
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
    }
}
