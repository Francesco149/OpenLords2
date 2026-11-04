/*
 * OpenLords2 — combat-mode (battlefield) renderer (implementation).
 *
 * See engine_battlefield_render.h for the function map; docs/findings/battlefield-render.md for the cluster
 * survey and docs/findings/render-screens.md for the retail-layout conversion (rhost rows).
 *
 * Every pass and leaf follows its retail function statement by statement on retail's own globals
 * (engine_battlefield_render_globals.c): the walk's descriptor cursor DAT_004e6588 and the gfx cursor
 * DAT_00591524 / DAT_00591528 are the loop variables, exactly as retail keeps them, so their end values are
 * retail's.  The per-pass / per-leaf counters are test-only instrumentation (not retail state).
 */

#include "engine_battlefield_render.h"

#include "engine_dirty_rect.h"
#include "gfx_dispatch.h"
#include "gfx_globals.h"
#include "gfx_sprite.h"
#include "gfx_sprite_rle.h"
#include "gfx_text_label.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Port-only bindings (retail addresses: the hybrid host binds them once)
 * ────────────────────────────────────────────────────────────────── */

static const uint8_t *g_anim_table_z16;            /* &DAT_004e3bf0: (dx, dy) dwords, 17 per facing */
static const uint8_t *g_anim_table_z32;            /* &DAT_004e4030 */
static uint8_t       *g_entity_base;               /* &DAT_00554480, 0x1b0 a record */
static int            g_entity_stride;
static int            g_entity_max_id;             /* 0x50 */
static const uint8_t *g_player_marker_slot_table;  /* &DAT_0052f0b2, 0x1a4 a player */
static int            g_player_marker_slot_stride;
static const uint8_t *g_effect_chain_base;         /* &DAT_0057a100, 0x4c a record */
static int            g_effect_chain_stride;
static int            g_effect_chain_max_id;
static const uint8_t *g_effect_chain_anim_table;   /* &DAT_004e4470: (dx, dy) dwords × 16 */
static const uint8_t *g_late_player_vis_base;      /* &DAT_0057bfee, 0x160 a player */
static int            g_late_player_vis_stride;
static const uint8_t *g_minimap_entity_owner_table;   /* &DAT_005544ac (the entity records' +0x2c) */
static int            g_minimap_entity_owner_stride;
static const uint8_t *g_minimap_player_slot_table;    /* &DAT_0057bf0a, 0x160 a player */
static int            g_minimap_player_slot_stride;
static const uint8_t *g_debug_label_font;          /* &DAT_005af8f0 */
static const uint8_t *g_debug_label_char_glyph_table;
static const char    *g_debug_label_suffix;        /* &DAT_004e4520 */

/* The sprite-sheet pointers retail keeps INSIDE records (entity +0x04, effect +0x00) are 32-bit fields; a 64-bit
 * host test cannot store a pointer there, so it binds the sheet instead.  NULL (the hybrid host, i686) → the field. */
static const uint8_t *g_entity_type6_blob;
static const uint8_t *g_effect_chain_shared_blob;

static engine_battlefield_world_dirty_hook_t g_world_dirty_hook;   /* 0x46c4a3 (the strategic-map mark) */
static engine_dirty_rect_state_t *g_dirty_state;   /* FUN_004527a6's rectangle; its below_status_redraw is DAT_0053e9ac */
static int g_force_redraw_own;                     /* DAT_0053e9ac when none is bound */
static int *g_force_redraw_ref;                    /* DAT_0053e9ac: the dirty state's below_status_redraw (rhost) */
static void (*g_log)(uint32_t fmt, int n);
static const int32_t *g_effect_cam_col, *g_effect_cam_row;   /* DAT_0055ce70 / DAT_005678a0 */

static int *force_redraw(void)
{
    return g_force_redraw_ref ? g_force_redraw_ref : &g_force_redraw_own;
}

/* ──────────────────────────────────────────────────────────────────
 * Test-only instrumentation
 * ────────────────────────────────────────────────────────────────── */

static int g_count_terrain_zoom16, g_count_terrain_zoom32, g_count_middle, g_count_overlay_zoom16;
static int g_count_overlay_zoom32, g_count_late, g_count_final;
static int g_terrain_blit_count, g_terrain_z32_first_blit_count, g_terrain_z32_second_blit_count;
static int g_overlay_z32_blit_count;
static int g_middle_build_count, g_middle_sort_count, g_middle_render_count;
static int g_middle_player_marker_count, g_middle_unit_key_count, g_middle_effect_chain_count;
static int g_middle_unit_key_last_arg = -1, g_middle_effect_chain_last_arg = -1;
static int g_middle_per_entity_count, g_middle_per_entity_last_id = -1;
static int g_middle_per_entity_last_px_x, g_middle_per_entity_last_px_y;
static int g_middle_entity_blit_count;
static int g_middle_entity_type6_count, g_middle_entity_type6_last_id = -1;
static int g_middle_entity_type7_count, g_middle_entity_type7_last_id = -1;
static int g_middle_entity_type9_sub_count, g_middle_entity_type9_sub_last_id = -1;
static int g_middle_entity_no_blob_count, g_middle_entity_no_blob_last_id = -1;
static int g_middle_entity_type6_blit_count, g_middle_entity_type7_blit_count, g_middle_entity_type9_sub_blit_count;
static int g_player_marker_last_slot = -1, g_player_marker_blit_count;
static int g_unit_key_last_slot = -1, g_unit_key_blit_count;
static int g_effect_chain_iters, g_effect_chain_blit_count;
static int g_late_per_cell_count, g_late_creature_draw_count, g_late_creature_blit_count;
static int g_late_selection_marker_count, g_late_selection_marker_blit_count, g_late_world_dirty_hook_count;
static int g_final_per_cell_count, g_final_label_render_count;
static int g_minimap_per_cell_count, g_minimap_blit_count;

/* ──────────────────────────────────────────────────────────────────
 * Shared staging
 * ────────────────────────────────────────────────────────────────── */

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static const uint8_t *rd_ptr(const uint8_t *p) { const uint8_t *v; memcpy(&v, p, sizeof v); return v; }
/* A sheet pointer retail keeps in a 32-bit record field: i686 reads it; a 64-bit host has none (NULL: draws
 * nothing — its tests bind the sheet instead). */
static const uint8_t *rec_sheet(const uint8_t *p)
{
#if UINTPTR_MAX == 0xffffffffu
    return (const uint8_t *)(uintptr_t)(uint32_t)rd32(p);
#else
    (void)p;
    return NULL;
#endif
}

/* DAT_005c9288 = idx; stage the 16-byte descriptor at blob + 16 idx + 8 (DAT_005c9258 / DAT_005aeb70 / Offset /
 * DAT_00591568 / DAT_005bb478 / DAT_0057d3c0).  Returns the retail gate `0 < Offset <= 310000`. */
static int stage(const uint8_t *blob, int idx)
{
    g_gfx_last_glyph_index = idx;
    if (!blob) {
        return 0;   /* a port guard: an unbound sheet (host tests) draws nothing; retail's are never NULL here */
    }
    gfx_dispatch_stage_sprite_descriptor(blob, idx);
    return g_gfx_glyph_offset <= 310000 && g_gfx_glyph_offset > 0;
}

static void mark(int x, int y, int w, int h)   /* FUN_004527a6(x, y, w, h, 2) */
{
    if (g_dirty_state) {
        engine_dirty_rect_mark_box_tiled(g_dirty_state, x, y, w, h, 2);
    }
}

static void clip(void)   /* FUN_0040464c / FUN_0040477b against the battlefield window */
{
    gfx_dispatch_clip_x(g_bfr_clip_left, g_bfr_clip_right);
    gfx_dispatch_clip_y(g_bfr_clip_top, g_bfr_clip_bottom);
}

/* (dx, dy) of an entity's anim cell: table[34 row + 2 col], [+1] (dwords; row / col signed bytes) */
static void anim_xy(const uint8_t *table, const uint8_t *rec, int32_t *dx, int32_t *dy)
{
    int k = 34 * (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING]
          + 2 * (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME];
    *dx = table ? rd32(table + 4 * k) : 0;
    *dy = table ? rd32(table + 4 * (k + 1)) : 0;
}

static uint8_t *entity(int id) { return g_entity_base + (size_t)id * (size_t)g_entity_stride; }

/* The zoom-dependent centering of FUN_004bdc31 / FUN_004be4df / FUN_004be7be.  Retail leaves its two locals
 * uninitialized at any other zoom (the stack residue); the port's zoom is always 0x10 or 0x20. */
static void entity_offset(const uint8_t *rec, int32_t *ox, int32_t *oy)
{
    int32_t dx, dy, hw = g_gfx_glyph_w >> 1;
    *ox = 0;
    *oy = 0;
    if (g_bfr_zoom == 0x20) {
        anim_xy(g_anim_table_z32, rec, &dx, &dy);
        *ox = 0x10 - hw + dx;
        *oy = 0x10 - hw + dy - 8;
    } else if (g_bfr_zoom == 0x10) {
        anim_xy(g_anim_table_z16, rec, &dx, &dy);
        *ox = 8 - hw + dx;
        *oy = 8 - hw + dy - 4;
    }
}

/* The boundary-bit rule of both terrain passes: first / last column or row */
static void mark_boundary(int col, int row)
{
    if (col == 0 || row == 0 || g_bfr_grid_cols - 1 == col || g_bfr_grid_rows - 1 == row) {
        g_bfr_desc_base[g_bfr_desc_cursor + 2] |= 2;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * FUN_004bc8fc — terrain, zoom 16
 * ────────────────────────────────────────────────────────────────── */

static void pass_terrain_zoom16(void)
{
    g_count_terrain_zoom16 += 1;
    if (!g_bfr_desc_base) {
        return;
    }
    g_bfr_desc_cursor = g_bfr_desc_stride * (g_bfr_camera_x + g_bfr_camera_y * g_bfr_row_stride);
    g_bfr_cell_counter = 0;
    int row = 0;
    for (g_gfx_cursor_y = g_bfr_viewport_y; row < g_bfr_grid_rows; g_gfx_cursor_y += 0x10) {
        int col = 0;
        g_gfx_cursor_x = g_bfr_viewport_x;
        while (col < g_bfr_grid_cols) {
            uint8_t *d = g_bfr_desc_base + g_bfr_desc_cursor;
            uint8_t flags = d[2];
            if (*force_redraw() != 0 || (flags & 1) != 0) {
                d[2] &= (uint8_t)~1u;
                g_gfx_last_glyph_index = d[3];
                if ((flags & 0x1c) == 0) {
                    stage(g_bfr_blob_primary, d[3]);
                    gfx_sprite_blit_16x16_raw(g_bfr_blob_primary);
                    g_terrain_blit_count += 1;
                } else if ((flags & 0x1c) == 4) {
                    stage(g_bfr_blob_alt, d[3]);
                    gfx_sprite_blit_16x16_raw(g_bfr_blob_alt);
                    g_terrain_blit_count += 1;
                }
            }
            mark_boundary(col, row);
            col++;
            g_bfr_desc_cursor += g_bfr_desc_stride;
            g_bfr_cell_counter += 1;
            g_gfx_cursor_x += 0x10;
        }
        row++;
        g_bfr_desc_cursor += g_bfr_row_wrap;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * FUN_004bcbdc — terrain, zoom 32 (+ the clipped second sprite)
 * ────────────────────────────────────────────────────────────────── */

static void pass_terrain_zoom32(void)
{
    g_count_terrain_zoom32 += 1;
    if (!g_bfr_desc_base) {
        return;
    }
    g_bfr_desc_cursor = g_bfr_desc_stride * (g_bfr_camera_x + g_bfr_camera_y * g_bfr_row_stride);
    int row = 0;
    for (g_gfx_cursor_y = g_bfr_viewport_y; row < g_bfr_grid_rows; g_gfx_cursor_y += 0x20) {
        int col = 0;
        for (g_gfx_cursor_x = g_bfr_viewport_x; col < g_bfr_grid_cols; g_gfx_cursor_x += 0x20) {
            uint8_t *d = g_bfr_desc_base + g_bfr_desc_cursor;
            uint8_t flags = d[2];
            int boundary = 1;   /* LABEL_29; the gates that jump to LABEL_4 skip it */
            if (*force_redraw() != 0 || (flags & 1) != 0) {
                if ((d[1] & 4) != 0) {
                    boundary = 0;
                } else {
                    d[2] &= (uint8_t)~1u;
                    g_gfx_last_glyph_index = d[3];
                    uint8_t second = d[0];
                    if ((flags & 0x1c) == 0) {
                        if (!stage(g_bfr_blob_primary, d[3])) {
                            boundary = -1;
                        } else {
                            gfx_sprite_blit_32x32_raw(g_bfr_blob_primary);
                            g_terrain_z32_first_blit_count += 1;
                        }
                    } else if ((flags & 0x1c) == 4) {
                        if (!stage(g_bfr_blob_alt, d[3])) {
                            boundary = -1;
                        } else {
                            gfx_sprite_blit_32x32_raw(g_bfr_blob_alt);
                            g_terrain_z32_first_blit_count += 1;
                        }
                    }
                    if (boundary > 0 && second != 0) {
                        uint8_t mode = d[4];
                        if (mode == 0 || mode > 3) {
                            boundary = 0;
                        } else {
                            int idx = (second >= 0x10) ? 0x9a : second + 0x8b;
                            stage(g_bfr_blob_alt, idx);
                            clip();
                            int visible = g_gfx_y_clip_mode != 5;
                            gfx_dispatch_apply_skip_and_dispatch_sprite(g_bfr_blob_alt);
                            if (visible) {
                                g_terrain_z32_second_blit_count += 1;
                            }
                        }
                    }
                }
            }
            if (boundary > 0) {
                mark_boundary(col, row);
            }
            col++;
            g_bfr_desc_cursor += g_bfr_desc_stride;
        }
        row++;
        g_bfr_desc_cursor += g_bfr_row_wrap;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * FUN_004bcbd1 — overlay, zoom 16 (an empty function); FUN_004bd07f — overlay, zoom 32
 * ────────────────────────────────────────────────────────────────── */

static void pass_overlay_zoom16(void)
{
    g_count_overlay_zoom16 += 1;
}

static void pass_overlay_zoom32(void)
{
    g_count_overlay_zoom32 += 1;
    if (!g_bfr_desc_base) {
        return;
    }
    g_bfr_desc_cursor = g_bfr_desc_stride * (g_bfr_camera_x + g_bfr_camera_y * g_bfr_row_stride);
    int row = 0;
    for (g_gfx_cursor_y = g_bfr_viewport_y; row < g_bfr_grid_rows; g_gfx_cursor_y += 0x20) {
        int col = 0;
        g_gfx_cursor_x = g_bfr_viewport_x;
        while (col < g_bfr_grid_cols) {
            uint8_t *d = g_bfr_desc_base + g_bfr_desc_cursor;
            if ((*force_redraw() != 0 || (d[2] & 1) != 0) && (d[1] & 4) != 0) {
                d[2] &= (uint8_t)~1u;
                if (stage(g_bfr_blob_primary, d[3])) {
                    gfx_sprite_blit_32x32_raw(g_bfr_blob_primary);
                    g_overlay_z32_blit_count += 1;
                }
            }
            col++;
            g_bfr_desc_cursor += g_bfr_desc_stride;
            g_gfx_cursor_x += 0x20;
        }
        row++;
        g_bfr_desc_cursor += g_bfr_row_wrap;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * The middle pass's entity leaves
 * ────────────────────────────────────────────────────────────────── */

/* FUN_004be4df — the type-6 pre-call: the entity's second sheet (+0x04), frame +0x11, a 6×6 mark */
void engine_battlefield_render_entity_type6(int id)
{
    g_middle_entity_type6_count += 1;
    g_middle_entity_type6_last_id = id;
    const uint8_t *rec = entity(id);
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    const uint8_t *blob = g_entity_type6_blob ? g_entity_type6_blob
                                              : rec_sheet(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_BLOB);
    if (!stage(blob, rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME])) {
        return;
    }
    int32_t ox, oy;
    entity_offset(rec, &ox, &oy);
    g_gfx_cursor_x += ox;
    g_gfx_cursor_y += oy;
    mark(g_gfx_cursor_x, g_gfx_cursor_y, 6, 6);
    clip();
    if (g_gfx_y_clip_mode != 5) {
        g_middle_entity_type6_blit_count += 1;
    }
    gfx_dispatch_dispatch_sprite_rle(blob);
    g_gfx_cursor_x = sx;
    g_gfx_cursor_y = sy;
}

/* FUN_004be7be — the type-7 post-call: the facing's sheet (DAT_00522f78 for facing >= 4, else DAT_00553f6c) */
void engine_battlefield_render_entity_type7(int id)
{
    g_middle_entity_type7_count += 1;
    g_middle_entity_type7_last_id = id;
    const uint8_t *rec = entity(id);
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    const uint8_t *blob = ((int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING] >= 4) ? g_bfr_type7_blob_high
                                                                                      : g_bfr_type7_blob_low;
    if (!stage(blob, rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME])) {
        return;
    }
    int32_t ox, oy;
    entity_offset(rec, &ox, &oy);
    g_gfx_cursor_x += ox;
    g_gfx_cursor_y += oy;
    mark(g_gfx_cursor_x, g_gfx_cursor_y, 6, 6);
    clip();
    if (g_gfx_y_clip_mode != 5) {
        g_middle_entity_type7_blit_count += 1;
    }
    gfx_dispatch_dispatch_sprite_rle(blob);
    g_gfx_cursor_x = sx;
    g_gfx_cursor_y = sy;
}

/* FUN_004beab9 — the type-9 / subtype-0x0e post-call: frames +0x11 + 22 (44 px up) and + 13 (58 px down) off the
 * entity's own sheet, zoom-32 anim offsets always */
void engine_battlefield_render_entity_type9e(int id)
{
    g_middle_entity_type9_sub_count += 1;
    g_middle_entity_type9_sub_last_id = id;
    const uint8_t *rec = entity(id);
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    const uint8_t *blob = rd_ptr(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB);
    static const int k_frame[2] = { 22, 13 }, k_dy[2] = { -44, 58 };
    for (int k = 0; k < 2; k++) {
        if (!stage(blob, rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] + k_frame[k])) {
            return;
        }
        int32_t dx, dy;
        anim_xy(g_anim_table_z32, rec, &dx, &dy);
        g_gfx_cursor_x += dx;
        g_gfx_cursor_y += dy;
        g_gfx_cursor_x += 0x10 - (g_gfx_glyph_w >> 1);
        g_gfx_cursor_y += 0x10 - (g_gfx_glyph_w >> 1);
        g_gfx_cursor_y += 0xc;
        g_gfx_cursor_y += k_dy[k];
        mark(g_gfx_cursor_x, g_gfx_cursor_y, 6, 6);
        clip();
        if (g_gfx_y_clip_mode != 5) {
            g_middle_entity_type9_sub_blit_count += 1;
        }
        gfx_dispatch_dispatch_sprite_rle(blob);
        g_gfx_cursor_x = sx;
        g_gfx_cursor_y = sy;
    }
}

/* FUN_004bdc31 — one entity: the type-6 pre-call, the main frame (+0x10 off the sheet at +0x00, anim-offset,
 * types 9 / 10 a little lower), a 5×5 (type < 7) or 9×9 mark, the cursor restored, the type-7 / type-9e post-calls */
void engine_battlefield_render_entity(int id)
{
    g_middle_per_entity_count += 1;
    g_middle_per_entity_last_id = id;
    g_middle_per_entity_last_px_x = g_gfx_cursor_x;
    g_middle_per_entity_last_px_y = g_gfx_cursor_y;
    uint8_t *rec = entity(id);
    if (rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE] == 6) {
        engine_battlefield_render_entity_type6(id);
    }
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    const uint8_t *blob = rd_ptr(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB);
    if (blob == NULL) {
        g_middle_entity_no_blob_count += 1;
        g_middle_entity_no_blob_last_id = id;
        if (g_log) {
            g_log(0x4e44f0u, rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE] + 1);   /* "ERR: tile map show …" */
        }
        return;
    }
    if (!stage(blob, rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_FRAME])) {
        return;
    }
    int32_t ox, oy;
    entity_offset(rec, &ox, &oy);
    g_gfx_cursor_x += ox;
    g_gfx_cursor_y += oy;
    uint8_t type = rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE];
    if (type == 9) {
        g_gfx_cursor_y += 0xc;
    } else if (type == 10) {
        g_gfx_cursor_y += 8;
    }
    if (type < 7) {
        mark(g_gfx_cursor_x, g_gfx_cursor_y, 5, 5);
    } else {
        mark(g_gfx_cursor_x, g_gfx_cursor_y, 9, 9);
    }
    clip();
    if (g_gfx_y_clip_mode != 5) {
        g_middle_entity_blit_count += 1;
    }
    gfx_dispatch_dispatch_sprite_rle(blob);
    g_gfx_cursor_x = sx;
    g_gfx_cursor_y = sy;
    if (rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE] == 7) {
        engine_battlefield_render_entity_type7(id);
    }
    if (rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE] == 9 && (int8_t)rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SUBTYPE] == 0xe) {
        engine_battlefield_render_entity_type9e(id);
    }
}

/* FUN_004bd938 — the visible list: every live entity (+0x2c) whose cell (+0x20 / +0x22, words) lies in the
 * viewport's one-cell-larger window, as (id, row) word pairs, then the (0, 9999) sentinel */
void engine_battlefield_render_build_list(void)
{
    g_middle_build_count += 1;
    int c0 = g_bfr_camera_x - 1, c1 = g_bfr_camera_x + g_bfr_grid_cols;
    int r0 = g_bfr_camera_y - 1, r1 = g_bfr_camera_y + g_bfr_grid_rows;
    g_bfr_visible_count = 0;
    for (int i = 1; g_entity_base && i <= g_entity_max_id; i++) {
        const uint8_t *rec = entity(i);
        int16_t col = rd16(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_X);
        int16_t row = rd16(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y);
        if (rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ALIVE] != 0 && col >= c0 && col <= c1 && row >= r0 && row <= r1) {
            g_bfr_visible[2 * g_bfr_visible_count] = (int16_t)i;
            g_bfr_visible[2 * g_bfr_visible_count + 1] = row;
            g_bfr_visible_count++;
        }
    }
    g_bfr_visible[2 * g_bfr_visible_count + 1] = 9999;
    g_bfr_visible[2 * g_bfr_visible_count] = 0;
}

/* FUN_004bda92 — bubble the list by row (last-swap bound, at most 500 passes) */
void engine_battlefield_render_sort_list(void)
{
    g_middle_sort_count += 1;
    int guard = 500;
    int bound = g_bfr_visible_count - 1;
    g_bfr_sort_passes = 0;
    while (bound > 0) {
        if (--guard <= 0) {
            break;
        }
        g_bfr_sort_passes++;
        int n = bound;
        bound = 0;
        for (int i = 0; i < n; i++) {
            if (g_bfr_visible[2 * i + 3] < g_bfr_visible[2 * i + 1]) {
                bound = i;
                int16_t y = g_bfr_visible[2 * i + 1], id = g_bfr_visible[2 * i];
                g_bfr_visible[2 * i + 1] = g_bfr_visible[2 * i + 3];
                g_bfr_visible[2 * i] = g_bfr_visible[2 * i + 2];
                g_bfr_visible[2 * i + 3] = y;
                g_bfr_visible[2 * i + 2] = id;
            }
        }
    }
}

/* FUN_004bdb95 — draw the list: the cursor at the entity's cell, then FUN_004bdc31 */
void engine_battlefield_render_render_list(void)
{
    g_middle_render_count += 1;
    for (int i = 0; i < g_bfr_visible_count; i++) {
        int id = g_bfr_visible[2 * i];
        const uint8_t *rec = entity(id);
        g_gfx_cursor_x = g_bfr_viewport_x + g_bfr_zoom * (rd16(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_X) - g_bfr_camera_x);
        g_gfx_cursor_y = g_bfr_viewport_y + g_bfr_zoom * (rd16(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y) - g_bfr_camera_y);
        engine_battlefield_render_entity(id);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * The middle pass's cell leaves
 * ────────────────────────────────────────────────────────────────── */

/* FUN_004bd574 — the animated player marker: frame DAT_004e5b18 (advanced when DAT_00591510, kept in 0..7) of
 * the marker player's slot group (DAT_0052f0b2[player · 0x1a4] · 8 + 33), centred, a 4×4 mark */
void engine_battlefield_render_player_marker(void)
{
    g_middle_player_marker_count += 1;
    if (g_bfr_marker_advance != 0) {
        g_bfr_marker_frame++;
    }
    if (g_bfr_marker_frame < 0) {
        g_bfr_marker_frame = 0;
    }
    if (g_bfr_marker_frame >= 8) {
        g_bfr_marker_frame = 0;
    }
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    int slot = g_player_marker_slot_table
             ? g_player_marker_slot_table[(size_t)g_bfr_marker_player * (size_t)g_player_marker_slot_stride] : 0;
    int idx = g_bfr_marker_frame + 8 * slot - 8 + 41;
    if (!stage(g_bfr_marker_blob, idx)) {
        return;
    }
    g_player_marker_last_slot = idx;
    g_gfx_cursor_x += 0x10 - (g_gfx_glyph_w >> 1);
    g_gfx_cursor_y += 0x10 - (g_gfx_glyph_w >> 1);
    mark(g_gfx_cursor_x, g_gfx_cursor_y, 4, 4);
    clip();
    if (g_gfx_y_clip_mode != 5) {
        g_player_marker_blit_count += 1;
    }
    gfx_dispatch_dispatch_sprite_rle(g_bfr_marker_blob);
    g_gfx_cursor_x = sx;
    g_gfx_cursor_y = sy;
}

/* FUN_004bd759 — a unit-key letter ('I' 'L' 'a' 'd' → frames 31..34 of DAT_00553250), centred, a 4×4 mark */
void engine_battlefield_render_unit_key(int key)
{
    g_middle_unit_key_count += 1;
    g_middle_unit_key_last_arg = key;
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    int idx;
    switch (key) {
    case 'I': idx = 31; break;
    case 'L': idx = 32; break;
    case 'a': idx = 33; break;
    case 'd': idx = 34; break;
    default: return;
    }
    if (!stage(g_bfr_unit_key_blob, idx)) {
        return;
    }
    g_unit_key_last_slot = idx;
    g_gfx_cursor_x += 0x10 - (g_gfx_glyph_w >> 1);
    g_gfx_cursor_y += 0x10 - (g_gfx_glyph_w >> 1);
    mark(g_gfx_cursor_x, g_gfx_cursor_y, 4, 4);
    clip();
    if (g_gfx_y_clip_mode != 5) {
        g_unit_key_blit_count += 1;
    }
    gfx_dispatch_dispatch_sprite_rle(g_bfr_unit_key_blob);
    g_gfx_cursor_x = sx;
    g_gfx_cursor_y = sy;
}

/* FUN_004beed4 — walk an effect chain (records at DAT_0057a100, 0x4c each: +0 sheet, +4 next, +8 live, +9 kind,
 * +0xa / +0xc pixel position, +0x12 frame), at most 10 links; kind 7 draws nothing, kind 5 adds the 16-step jitter
 * DAT_004e4470[DAT_004e5b1c].  QUIRK (retail): a frame whose descriptor fails the offset gate returns WITHOUT
 * restoring the cursor (an earlier link's position stays in DAT_00591524 / DAT_00591528). */
void engine_battlefield_render_effect_chain(int head)
{
    g_middle_effect_chain_count += 1;
    g_middle_effect_chain_last_arg = head;
    g_effect_chain_iters = 0;
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    int iters = 0;
    while (head != 0 && g_effect_chain_base) {
        if (iters >= 10) {
            break;
        }
        g_bfr_effect_id = head;
        if (head < 0 || head > g_effect_chain_max_id) {
            break;   /* a port guard (retail indexes blindly); the hybrid binds max to the whole word range */
        }
        const uint8_t *rec = g_effect_chain_base + (size_t)head * (size_t)g_effect_chain_stride;
        if (rec[ENGINE_BATTLEFIELD_EFFECT_OFFSET_TYPE] == 0) {
            break;
        }
        int32_t cam_col = g_effect_cam_col ? *g_effect_cam_col : g_bfr_camera_x;
        int32_t cam_row = g_effect_cam_row ? *g_effect_cam_row : g_bfr_camera_y;
        int32_t x = rd16(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_X_OFFSET) - 0x20 * cam_col;
        int32_t y = rd16(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_Y_OFFSET) - 0x20 * cam_row;
        uint8_t kind = rec[ENGINE_BATTLEFIELD_EFFECT_OFFSET_SUBTYPE];
        if (kind != 7) {
            if (kind == 5) {
                if (++g_bfr_effect_frame >= 0x10) {
                    g_bfr_effect_frame = 0;
                }
                if (g_effect_chain_anim_table) {
                    x += rd32(g_effect_chain_anim_table + 8 * g_bfr_effect_frame);
                    y += rd32(g_effect_chain_anim_table + 8 * g_bfr_effect_frame + 4);
                }
            }
            const uint8_t *blob = g_effect_chain_shared_blob ? g_effect_chain_shared_blob : rec_sheet(rec);
            if (blob == NULL) {
                g_gfx_last_glyph_index = rd16(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_SLOT);
                goto next;   /* a port guard (host tests: no sheet); retail's records always carry one */
            }
            if (!stage(blob, rd16(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_SLOT))) {
                return;   /* the quirk: no cursor restore */
            }
            g_gfx_cursor_x = g_bfr_scroll_pad + x + g_bfr_viewport_x;
            g_gfx_cursor_y = g_bfr_scroll_pad + y + g_bfr_viewport_y;
            clip();
            mark(g_gfx_cursor_x, g_gfx_cursor_y, 4, 4);
            if (g_gfx_y_clip_mode != 5) {
                g_effect_chain_blit_count += 1;
            }
            gfx_dispatch_dispatch_sprite_rle(blob);
        }
    next:;
        int next = rd16(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_NEXT_ID);
        if (next == head) {
            break;
        }
        head = next;
        iters++;
        g_effect_chain_iters = iters;
    }
    g_gfx_cursor_x = sx;
    g_gfx_cursor_y = sy;
}

/* ──────────────────────────────────────────────────────────────────
 * FUN_004bd355 — the middle pass: the entity list, then the cell leaves over the over-scan window (one cell west /
 * north of the viewport, two past it, clamped to the grid)
 * ────────────────────────────────────────────────────────────────── */

static void pass_middle(void)
{
    g_count_middle += 1;
    engine_battlefield_render_build_list();
    engine_battlefield_render_sort_list();
    engine_battlefield_render_render_list();
    if (!g_bfr_desc_base) {
        return;
    }
    int c0 = g_bfr_camera_x - 1, x0 = g_bfr_viewport_x - g_bfr_zoom;
    if (g_bfr_camera_x - 1 < 0) {
        c0 = 0;
        x0 = g_bfr_viewport_x;
    }
    int r0 = g_bfr_camera_y - 1, y0 = g_bfr_viewport_y - g_bfr_zoom;
    if (g_bfr_camera_y - 1 < 0) {
        r0 = 0;
        y0 = g_bfr_viewport_y;
    }
    int cols = g_bfr_grid_cols + 2;
    if (c0 + g_bfr_grid_cols + 2 >= g_bfr_row_stride) {
        cols = g_bfr_row_stride - c0;
    }
    int rows = g_bfr_grid_rows + 2;
    if (r0 + g_bfr_grid_rows + 2 >= g_bfr_total_rows) {
        rows = g_bfr_total_rows - r0;
    }
    int wrap = g_bfr_desc_stride * (g_bfr_row_stride - cols);
    g_bfr_desc_cursor = g_bfr_desc_stride * (c0 + r0 * g_bfr_row_stride);
    g_bfr_cell_counter = 0;
    int row = 0;
    for (g_gfx_cursor_y = y0; row < rows; g_gfx_cursor_y += g_bfr_zoom) {
        int col = 0;
        g_gfx_cursor_x = x0;
        while (cols > col) {
            uint8_t *d = g_bfr_desc_base + g_bfr_desc_cursor;
            if ((int8_t)d[2] < 0) {
                if (d[3] != 0) {
                    engine_battlefield_render_unit_key(d[3]);
                } else {
                    engine_battlefield_render_player_marker();
                    g_bfr_desc_base[g_bfr_desc_cursor + 2] |= 1;
                }
            }
            uint8_t head = g_bfr_desc_base[g_bfr_desc_cursor + 6];
            if (head != 0) {
                engine_battlefield_render_effect_chain(head);
            }
            col++;
            g_bfr_desc_cursor += g_bfr_desc_stride;
            g_bfr_cell_counter += 1;
            g_gfx_cursor_x += g_bfr_zoom;
        }
        row++;
        g_bfr_desc_cursor += wrap;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * The late pass: creatures visible to the local player, then the selection marker
 * ────────────────────────────────────────────────────────────────── */

/* FUN_004be070 — a cell's creature: frame 46 + (+0x1a0 shifted by DAT_00566514, left unless DAT_0056651c < 0) of
 * DAT_005530c8, at the zoom-32 anim offset (+8, −12; types 7..9 / 11+: −32, type 9 +16), clipped plain blit */
void engine_battlefield_render_creature(int id)
{
    g_late_creature_draw_count += 1;
    const uint8_t *rec = entity(id);
    int32_t sx = g_gfx_cursor_x, sy = g_gfx_cursor_y;
    int32_t dx, dy;
    anim_xy(g_anim_table_z32, rec, &dx, &dy);
    uint8_t type = rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE];
    if (type >= 7) {
        if (type == 10) {
            g_gfx_cursor_x += dx + 8;
            g_gfx_cursor_y += dy - 0xc;
        } else {
            g_gfx_cursor_x += dx + 8;
            g_gfx_cursor_y += dy - 0x20;
            if (type == 9) {
                g_gfx_cursor_y += 0x10;
            }
        }
    } else {
        g_gfx_cursor_x += dx + 8;
        g_gfx_cursor_y += dy - 0xc;
    }
    int32_t raw = rd16(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_SPRITE_SLOT);
    int sh = (uint8_t)g_bfr_slot_shift & 31;
    int32_t v = (g_bfr_slot_shift_sign >= 0) ? (int32_t)((uint32_t)raw << sh) : (raw >> sh);
    if (!stage(g_bfr_creature_blob, 46 + v)) {
        return;
    }
    clip();
    if (g_gfx_y_clip_mode != 5) {
        g_late_creature_blit_count += 1;
    }
    gfx_dispatch_apply_skip_and_dispatch_sprite(g_bfr_creature_blob);
    g_gfx_cursor_x = sx;
    g_gfx_cursor_y = sy;
}

/* FUN_004be33f — the selection marker at cell (DAT_00553f14, DAT_00553f18): frame 63 of DAT_005530c8, a 3×3 mark,
 * then the strategic-map mark 0x46c4a3(x − 1, y − 1, 3, 3).  The cursor is NOT restored. */
void engine_battlefield_render_selection_marker(void)
{
    g_late_selection_marker_count += 1;
    g_gfx_cursor_x = g_bfr_viewport_x + 0x20 * (g_bfr_sel_x - g_bfr_camera_x) + 2;
    g_gfx_cursor_y = g_bfr_viewport_y + 0x20 * (g_bfr_sel_y - g_bfr_camera_y) + 9;
    mark(g_gfx_cursor_x, g_gfx_cursor_y, 3, 3);
    if (!stage(g_bfr_creature_blob, 63)) {
        return;
    }
    clip();
    if (g_gfx_y_clip_mode != 5) {
        g_late_selection_marker_blit_count += 1;
    }
    gfx_dispatch_apply_skip_and_dispatch_sprite(g_bfr_creature_blob);
    if (g_world_dirty_hook) {
        g_late_world_dirty_hook_count += 1;
        g_world_dirty_hook(g_bfr_sel_x - 1, g_bfr_sel_y - 1, 3, 3);
    }
}

/* FUN_004bd233 — the late pass: per visible cell, the creature id at +5 when its visibility bits (+0x0a) meet the
 * local player's mask (DAT_0057bfee[DAT_0057c8cc · 0x160]); then the selection marker while DAT_00553f84 > 0 */
static void pass_late(void)
{
    g_count_late += 1;
    if (g_bfr_desc_base) {
        g_bfr_desc_cursor = g_bfr_desc_stride * (g_bfr_camera_x + g_bfr_camera_y * g_bfr_row_stride);
        int row = 0;
        for (g_gfx_cursor_y = g_bfr_viewport_y; row < g_bfr_grid_rows; g_gfx_cursor_y += 0x20) {
            int col = 0;
            g_gfx_cursor_x = g_bfr_viewport_x;
            while (col < g_bfr_grid_cols) {
                g_late_per_cell_count += 1;
                uint8_t id = g_bfr_desc_base[g_bfr_desc_cursor + ENGINE_BATTLEFIELD_DESC_OFFSET_LATE_CREATURE_ID];
                if (id != 0 && g_entity_base && g_late_player_vis_base) {
                    uint8_t mask = g_late_player_vis_base[(size_t)g_bfr_late_player * (size_t)g_late_player_vis_stride];
                    if ((entity(id)[ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_VISIBILITY_MASK] & mask) != 0) {
                        engine_battlefield_render_creature(id);
                    }
                }
                col++;
                g_bfr_desc_cursor += g_bfr_desc_stride;
                g_gfx_cursor_x += 0x20;
            }
            row++;
            g_bfr_desc_cursor += g_bfr_row_wrap;
        }
    }
    if (g_bfr_sel_gate > 0) {
        engine_battlefield_render_selection_marker();
    }
}

/* ──────────────────────────────────────────────────────────────────
 * FUN_004bf1f9 — the final pass (debug, DAT_005bb4a5): per cell with a creature, the byte sum of its record's 414
 * bytes from +0x12 (0x440089: an unsigned byte — EAX is zero above AL there), shown as 1..100 (0x3f), 101..200
 * (−100, 0xf9) or 201+ (−200, 0x20)
 * ────────────────────────────────────────────────────────────────── */

static int final_byte_sum(const uint8_t *base, int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum = (uint8_t)(sum + base[i]);
    }
    return sum;
}

static void pass_final(void)
{
    g_count_final += 1;
    if (g_bfr_debug_label == 0 || !g_bfr_desc_base) {
        return;
    }
    g_bfr_desc_cursor = g_bfr_desc_stride * (g_bfr_camera_x + g_bfr_camera_y * g_bfr_row_stride);
    for (int i = 0; i < g_bfr_grid_rows; i++) {
        for (int j = 0; j < g_bfr_grid_cols; j++) {
            g_final_per_cell_count += 1;
            uint8_t id = g_bfr_desc_base[g_bfr_desc_cursor + 5];
            if (id != 0 && g_entity_base) {
                int v = final_byte_sum(entity(id) + 0x12, 0x19e), color;
                if (v <= 200) {
                    if (v <= 100) {
                        color = 0x3f;
                    } else {
                        v -= 100;
                        color = 0xf9;
                    }
                } else {
                    v -= 200;
                    color = 0x20;
                }
                if (v != 999 && v != 0 && g_debug_label_font) {   /* (the font: a port guard) */
                    g_final_label_render_count += 1;
                    gfx_text_label_render_dec(v, ' ', g_debug_label_suffix ? g_debug_label_suffix : "",
                                              g_bfr_viewport_x + g_bfr_zoom * j + 1, g_bfr_viewport_y + g_bfr_zoom * i + 1,
                                              g_debug_label_font, g_debug_label_char_glyph_table, (uint8_t)color);
                }
            }
            g_bfr_desc_cursor += g_bfr_desc_stride;
        }
        g_bfr_desc_cursor += g_bfr_row_wrap;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * FUN_004bc142 — the frame driver
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_frame(int camera_x, int camera_y)
{
    g_bfr_camera_x = camera_x;
    g_bfr_camera_y = camera_y;
    if (g_bfr_zoom == 0x10) {
        pass_terrain_zoom16();
    } else if (g_bfr_zoom == 0x20) {
        pass_terrain_zoom32();
    }
    pass_middle();
    if (g_bfr_zoom == 0x10) {
        pass_overlay_zoom16();
    } else if (g_bfr_zoom == 0x20) {
        pass_overlay_zoom32();
    }
    pass_late();
    pass_final();
    int *fr = force_redraw();
    if (*fr != 0) {
        *fr -= 1;
    }
}

/* the passes alone (rows + tests) */
void engine_battlefield_render_terrain_zoom16(void) { pass_terrain_zoom16(); }
void engine_battlefield_render_terrain_zoom32(void) { pass_terrain_zoom32(); }
void engine_battlefield_render_overlay_zoom16(void) { pass_overlay_zoom16(); }
void engine_battlefield_render_overlay_zoom32(void) { pass_overlay_zoom32(); }
void engine_battlefield_render_middle(void)         { pass_middle(); }
void engine_battlefield_render_late(void)           { pass_late(); }
void engine_battlefield_render_final(void)          { pass_final(); }

void engine_battlefield_render_run_pass_middle_for_test(int camera_x, int camera_y)
{
    g_bfr_camera_x = camera_x;
    g_bfr_camera_y = camera_y;
    pass_middle();
}

void engine_battlefield_render_run_pass_late_for_test(int camera_x, int camera_y)
{
    g_bfr_camera_x = camera_x;
    g_bfr_camera_y = camera_y;
    pass_late();
}

void engine_battlefield_render_run_pass_final_for_test(int camera_x, int camera_y)
{
    g_bfr_camera_x = camera_x;
    g_bfr_camera_y = camera_y;
    pass_final();
}

/* ──────────────────────────────────────────────────────────────────
 * The minimap: FUN_004bc51a (a strip of rows, 2×2 per cell) and FUN_004bc1d1 (the rolling scan)
 * ────────────────────────────────────────────────────────────────── */

static void minimap_cell(const uint8_t *blob, int idx)
{
    g_gfx_last_glyph_index = idx;
    if (!blob) {
        return;   /* the port guard of stage() */
    }
    gfx_dispatch_stage_sprite_descriptor(blob, idx);
    gfx_sprite_blit_2x2_raw(blob);
    g_minimap_blit_count += 1;
}

void engine_battlefield_render_minimap_strip(int start_row, int count)
{
    if (!g_bfr_desc_base) {
        return;
    }
    g_bfr_desc_cursor = start_row * g_bfr_row_stride * g_bfr_desc_stride;
    g_gfx_cursor_y = g_bfr_mm_y_offset + 2 * start_row;
    for (int i = start_row; i < start_row + count; i++) {
        int col = 0;
        g_gfx_cursor_x = g_bfr_mm_origin_x;
        while (col < g_bfr_row_stride) {
            g_minimap_per_cell_count += 1;
            uint8_t *d = g_bfr_desc_base + g_bfr_desc_cursor;
            uint8_t flags = d[2], id = d[5];
            uint8_t bnd = flags & 2;
            if (*force_redraw() != 0 || bnd != 0 || (flags & 1) != 0) {
                if (id != 0) {
                    d[2] &= (uint8_t)~2u;
                    int owner = g_minimap_entity_owner_table
                              ? g_minimap_entity_owner_table[(size_t)id * (size_t)g_minimap_entity_owner_stride] : 0;
                    if (owner == 6) {
                        g_gfx_last_glyph_index = 6;
                    } else {
                        g_gfx_last_glyph_index = g_minimap_player_slot_table
                            ? g_minimap_player_slot_table[(size_t)owner * (size_t)g_minimap_player_slot_stride] : 0;
                    }
                    if (g_gfx_last_glyph_index != 0) {
                        minimap_cell(g_bfr_mm_default, g_gfx_last_glyph_index);
                    }
                } else if (bnd != 0) {
                    d[2] &= (uint8_t)~2u;
                    minimap_cell(g_bfr_mm_default, 0);
                } else if (col != 0 || g_bfr_engine_state != 3) {
                    g_gfx_last_glyph_index = d[3];
                    if ((flags & 0x1c) == 0) {
                        minimap_cell(g_bfr_mm_blob_primary, d[3]);
                    } else if ((flags & 0x1c) == 4) {
                        minimap_cell(g_bfr_mm_blob_alt, d[3]);
                    }
                } else {
                    minimap_cell(g_bfr_mm_default, 0);
                }
            }
            col++;
            g_bfr_desc_cursor += g_bfr_desc_stride;
            g_gfx_cursor_x += 2;
        }
        g_gfx_cursor_y += 2;
    }
}

void engine_battlefield_render_minimap_scroll(int rows)
{
    g_bfr_mm_scan_pos += rows;
    if (g_bfr_total_rows - rows < g_bfr_mm_scan_pos) {
        g_bfr_mm_scan_pos = 0;
    }
    if (g_bfr_mm_mode == 2) {
        engine_battlefield_render_minimap_strip(g_bfr_mm_scan_pos, rows);
    }
}

void engine_battlefield_render_minimap_render_strip_for_test(int start_row, int count)
{
    engine_battlefield_render_minimap_strip(start_row, count);
}

int  engine_battlefield_render_minimap_scan_pos(void)        { return g_bfr_mm_scan_pos; }
void engine_battlefield_render_reset_minimap_scan_pos(void)  { g_bfr_mm_scan_pos = 0; }
int  engine_battlefield_render_minimap_per_cell_count(void)  { return g_minimap_per_cell_count; }
int  engine_battlefield_render_minimap_blit_count(void)      { return g_minimap_blit_count; }

/* ──────────────────────────────────────────────────────────────────
 * FUN_004bc020 / FUN_004bc107 — the two context binders
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_bind_hex_grid_context(
    const uint8_t *blob_primary, const uint8_t *blob_alt, uint8_t *desc_base, int row_stride_in_cells,
    int total_rows, int cell_stride_bytes, int viewport_x, int viewport_y, int visible_cols, int visible_rows,
    int zoom)
{
    g_bfr_desc_base = desc_base;
    g_bfr_blob_primary = blob_primary;
    g_bfr_blob_alt = blob_alt;
    g_bfr_row_stride = row_stride_in_cells;
    g_bfr_total_rows = total_rows;
    g_bfr_desc_stride = cell_stride_bytes;
    g_bfr_grid_cols = visible_cols;
    g_bfr_grid_rows = visible_rows;
    g_bfr_viewport_x = viewport_x;
    g_bfr_viewport_y = viewport_y;
    g_bfr_zoom = zoom;
    g_bfr_scroll_pad = zoom / 2;
    g_bfr_band_width = zoom / 4;
    g_bfr_zoom_sq = zoom * zoom;
    g_bfr_row_wrap = cell_stride_bytes * (row_stride_in_cells - visible_cols);
    g_bfr_clip_left = viewport_x;
    g_bfr_clip_right = viewport_x + zoom * visible_cols;
    g_bfr_clip_top = viewport_y;
    g_bfr_clip_bottom = viewport_y + zoom * visible_rows;
}

void engine_battlefield_render_bind_minimap_context(const uint8_t *blob_primary, const uint8_t *blob_alt,
                                                    const uint8_t *default_desc, int origin_x, int y_offset,
                                                    int mode_gate)
{
    g_bfr_mm_blob_primary = blob_primary;
    g_bfr_mm_blob_alt = blob_alt;
    g_bfr_mm_default = default_desc;
    g_bfr_mm_origin_x = origin_x;
    g_bfr_mm_y_offset = y_offset;
    g_bfr_mm_mode = mode_gate;
}

/* ──────────────────────────────────────────────────────────────────
 * The screen → cell quantizers (FUN_004bc222 … FUN_004bc505)
 * ────────────────────────────────────────────────────────────────── */

int engine_battlefield_render_screen_in_grid(int px, int py)
{
    if (px < g_bfr_viewport_x) return 0;
    if (py < g_bfr_viewport_y) return 0;
    if (g_bfr_viewport_x + g_bfr_zoom * g_bfr_grid_cols > px) {
        return g_bfr_viewport_y + g_bfr_zoom * g_bfr_grid_rows > py;
    }
    return 0;
}

int engine_battlefield_render_pixel_to_col(int px) { return (px - g_bfr_viewport_x) / g_bfr_zoom; }
int engine_battlefield_render_pixel_to_row(int py) { return (py - g_bfr_viewport_y) / g_bfr_zoom; }

int engine_battlefield_render_pixel_to_col_right_biased(int px)
{
    int col = (px - g_bfr_viewport_x) / g_bfr_zoom;
    if (3 * g_bfr_band_width <= (px - g_bfr_viewport_x) % g_bfr_zoom) col++;
    return col <= 0 ? 0 : col;
}

int engine_battlefield_render_pixel_to_col_left_biased(int px)
{
    int col = (px - g_bfr_viewport_x) / g_bfr_zoom;
    if (g_bfr_band_width > (px - g_bfr_viewport_x) % g_bfr_zoom) col--;
    return g_bfr_row_stride <= col ? g_bfr_row_stride - 1 : col;
}

int engine_battlefield_render_pixel_to_row_bottom_biased(int py)
{
    int row = (py - g_bfr_viewport_y) / g_bfr_zoom;
    if (3 * g_bfr_band_width <= (py - g_bfr_viewport_y) % g_bfr_zoom) row++;
    return row <= 0 ? 0 : row;
}

int engine_battlefield_render_pixel_to_row_top_biased(int py)
{
    int row = (py - g_bfr_viewport_y) / g_bfr_zoom;
    if ((py - g_bfr_viewport_y) % g_bfr_zoom < g_bfr_band_width) row--;
    return row >= g_bfr_total_rows ? g_bfr_total_rows - 1 : row;
}

int engine_battlefield_render_col_delta(int px_a, int px_b)
{
    return (px_b - g_bfr_viewport_x) / g_bfr_zoom - (px_a - g_bfr_viewport_x) / g_bfr_zoom;
}

int engine_battlefield_render_row_delta(int py_a, int py_b)
{
    return (py_b - g_bfr_viewport_y) / g_bfr_zoom - (py_a - g_bfr_viewport_y) / g_bfr_zoom;
}

int engine_battlefield_render_get_zoom(void) { return g_bfr_zoom; }

/* ──────────────────────────────────────────────────────────────────
 * Binders (write the retail-layout globals / the port-only bindings)
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_set_zoom(int zoom)              { g_bfr_zoom = zoom; }
int  engine_battlefield_render_zoom(void)                      { return g_bfr_zoom; }
void engine_battlefield_render_set_force_redraw(int counter)   { *force_redraw() = counter; }
int  engine_battlefield_render_force_redraw(void)              { return *force_redraw(); }
void engine_battlefield_render_bind_force_redraw(int *counter) { g_force_redraw_ref = counter; }
int  engine_battlefield_render_camera_x(void)                  { return g_bfr_camera_x; }
int  engine_battlefield_render_camera_y(void)                  { return g_bfr_camera_y; }

void engine_battlefield_render_bind_descriptor_table(uint8_t *base, int stride)
{ g_bfr_desc_base = base; g_bfr_desc_stride = stride; }
void engine_battlefield_render_bind_grid(int cols, int rows, int row_wrap)
{ g_bfr_grid_cols = cols; g_bfr_grid_rows = rows; g_bfr_row_wrap = row_wrap; }
void engine_battlefield_render_bind_viewport_offset(int x, int y)   { g_bfr_viewport_x = x; g_bfr_viewport_y = y; }
void engine_battlefield_render_bind_camera_row_stride(int row_stride) { g_bfr_row_stride = row_stride; }
void engine_battlefield_render_bind_total_rows(int total_rows)        { g_bfr_total_rows = total_rows; }
void engine_battlefield_render_bind_blobs(const uint8_t *primary, const uint8_t *alt)
{ g_bfr_blob_primary = primary; g_bfr_blob_alt = alt; }
void engine_battlefield_render_bind_cell_band_width(int band_width) { g_bfr_band_width = band_width; }
void engine_battlefield_render_bind_clip_rect(int left, int top, int right, int bottom)
{ g_bfr_clip_left = left; g_bfr_clip_top = top; g_bfr_clip_right = right; g_bfr_clip_bottom = bottom; }
void engine_battlefield_render_bind_entity_table(uint8_t *base, int stride, int max_id)
{ g_entity_base = base; g_entity_stride = stride; g_entity_max_id = max_id; }
void engine_battlefield_render_bind_anim_offset_tables(const uint8_t *z16, const uint8_t *z32)
{ g_anim_table_z16 = z16; g_anim_table_z32 = z32; }
void engine_battlefield_render_set_dirty_state(engine_dirty_rect_state_t *state) { g_dirty_state = state; }
void engine_battlefield_render_bind_player_marker_blob(const uint8_t *blob) { g_bfr_marker_blob = blob; }
void engine_battlefield_render_bind_player_marker_slot_table(const uint8_t *table, int stride)
{ g_player_marker_slot_table = table; g_player_marker_slot_stride = stride; }
void engine_battlefield_render_set_player_marker_player_id(int player_id) { g_bfr_marker_player = player_id; }
void engine_battlefield_render_set_player_marker_anim_advance(int advance) { g_bfr_marker_advance = advance; }
void engine_battlefield_render_bind_unit_key_blob(const uint8_t *blob) { g_bfr_unit_key_blob = blob; }
void engine_battlefield_render_bind_effect_chain_table(const uint8_t *base, int stride, int max_id)
{ g_effect_chain_base = base; g_effect_chain_stride = stride; g_effect_chain_max_id = max_id; }
void engine_battlefield_render_bind_effect_chain_anim_table(const uint8_t *table) { g_effect_chain_anim_table = table; }
void engine_battlefield_render_bind_effect_chain_shared_blob(const uint8_t *blob) { g_effect_chain_shared_blob = blob; }
void engine_battlefield_render_bind_scroll_pad(int pad) { g_bfr_scroll_pad = pad; }
void engine_battlefield_render_bind_entity_type6_blob(const uint8_t *blob) { g_entity_type6_blob = blob; }
void engine_battlefield_render_bind_entity_type7_blobs(const uint8_t *low, const uint8_t *high)
{ g_bfr_type7_blob_low = low; g_bfr_type7_blob_high = high; }
void engine_battlefield_render_bind_creature_sprite_blob(const uint8_t *blob) { g_bfr_creature_blob = blob; }
void engine_battlefield_render_bind_late_player_visibility_mask(const uint8_t *base, int stride, int player_id)
{ g_late_player_vis_base = base; g_late_player_vis_stride = stride; g_bfr_late_player = player_id; }
void engine_battlefield_render_bind_late_visibility_table(const uint8_t *base, int stride)
{ g_late_player_vis_base = base; g_late_player_vis_stride = stride; }
void engine_battlefield_render_bind_creature_slot_shift(int amount, int sign)
{ g_bfr_slot_shift = amount; g_bfr_slot_shift_sign = sign; }
void engine_battlefield_render_bind_selection_marker(int x, int y, int gate)
{ g_bfr_sel_x = x; g_bfr_sel_y = y; g_bfr_sel_gate = gate; }
void engine_battlefield_render_bind_world_map_dirty_hook(engine_battlefield_world_dirty_hook_t hook)
{ g_world_dirty_hook = hook; }
void engine_battlefield_render_bind_debug_label_enabled(int enabled) { g_bfr_debug_label = (uint8_t)enabled; }
void engine_battlefield_render_bind_debug_label_font(const uint8_t *font_base, const uint8_t *char_glyph_table)
{ g_debug_label_font = font_base; g_debug_label_char_glyph_table = char_glyph_table; }
void engine_battlefield_render_bind_debug_label_suffix(const char *suffix) { g_debug_label_suffix = suffix; }
void engine_battlefield_render_bind_log(void (*log)(uint32_t fmt, int n)) { g_log = log; }
void engine_battlefield_render_bind_effect_camera(const int32_t *col, const int32_t *row)
{ g_effect_cam_col = col; g_effect_cam_row = row; }

void engine_battlefield_render_bind_minimap_blob_primary(const uint8_t *blob) { g_bfr_mm_blob_primary = blob; }
void engine_battlefield_render_bind_minimap_blob_alt(const uint8_t *blob)     { g_bfr_mm_blob_alt = blob; }
void engine_battlefield_render_bind_minimap_default_descriptor(const uint8_t *blob) { g_bfr_mm_default = blob; }
void engine_battlefield_render_bind_minimap_origin(int origin_x, int y_offset)
{ g_bfr_mm_origin_x = origin_x; g_bfr_mm_y_offset = y_offset; }
void engine_battlefield_render_set_minimap_mode_gate(int gate_value)   { g_bfr_mm_mode = gate_value; }
void engine_battlefield_render_set_minimap_engine_state(int engine_state) { g_bfr_engine_state = engine_state; }
void engine_battlefield_render_bind_minimap_entity_owner_table(const uint8_t *table, int stride)
{ g_minimap_entity_owner_table = table; g_minimap_entity_owner_stride = stride; }
void engine_battlefield_render_bind_minimap_player_slot_table(const uint8_t *table, int stride)
{ g_minimap_player_slot_table = table; g_minimap_player_slot_stride = stride; }

/* ──────────────────────────────────────────────────────────────────
 * Instrumentation accessors (test-only)
 * ────────────────────────────────────────────────────────────────── */

int engine_battlefield_render_terrain_blit_count(void)               { return g_terrain_blit_count; }
int engine_battlefield_render_terrain_zoom32_first_blit_count(void)  { return g_terrain_z32_first_blit_count; }
int engine_battlefield_render_terrain_zoom32_second_blit_count(void) { return g_terrain_z32_second_blit_count; }
int engine_battlefield_render_overlay_zoom32_blit_count(void)        { return g_overlay_z32_blit_count; }
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
int engine_battlefield_render_middle_unit_key_last_arg(void)     { return g_middle_unit_key_last_arg; }
int engine_battlefield_render_middle_effect_chain_last_arg(void) { return g_middle_effect_chain_last_arg; }
int engine_battlefield_render_visible_entity_count(void) { return g_bfr_visible_count; }
int engine_battlefield_render_visible_entity_id(int idx)
{ return (idx < 0 || idx >= g_bfr_visible_count) ? 0 : g_bfr_visible[2 * idx]; }
int engine_battlefield_render_visible_entity_y(int idx)
{ return (idx < 0 || idx >= g_bfr_visible_count) ? 0 : g_bfr_visible[2 * idx + 1]; }
int engine_battlefield_render_middle_sort_passes(void)        { return g_bfr_sort_passes; }
int engine_battlefield_render_middle_per_entity_count(void)    { return g_middle_per_entity_count; }
int engine_battlefield_render_middle_per_entity_last_id(void)  { return g_middle_per_entity_last_id; }
int engine_battlefield_render_middle_per_entity_last_px_x(void) { return g_middle_per_entity_last_px_x; }
int engine_battlefield_render_middle_per_entity_last_px_y(void) { return g_middle_per_entity_last_px_y; }
int engine_battlefield_render_middle_entity_type6_count(void)      { return g_middle_entity_type6_count; }
int engine_battlefield_render_middle_entity_type6_last_id(void)    { return g_middle_entity_type6_last_id; }
int engine_battlefield_render_middle_entity_type7_count(void)      { return g_middle_entity_type7_count; }
int engine_battlefield_render_middle_entity_type7_last_id(void)    { return g_middle_entity_type7_last_id; }
int engine_battlefield_render_middle_entity_type9_sub_count(void)  { return g_middle_entity_type9_sub_count; }
int engine_battlefield_render_middle_entity_type9_sub_last_id(void) { return g_middle_entity_type9_sub_last_id; }
int engine_battlefield_render_middle_entity_no_blob_count(void)    { return g_middle_entity_no_blob_count; }
int engine_battlefield_render_middle_entity_no_blob_last_id(void)  { return g_middle_entity_no_blob_last_id; }
int engine_battlefield_render_middle_entity_blit_count(void)       { return g_middle_entity_blit_count; }
int engine_battlefield_render_middle_entity_type6_blit_count(void) { return g_middle_entity_type6_blit_count; }
int engine_battlefield_render_middle_entity_type7_blit_count(void) { return g_middle_entity_type7_blit_count; }
int engine_battlefield_render_middle_entity_type9_sub_blit_count(void) { return g_middle_entity_type9_sub_blit_count; }
int engine_battlefield_render_per_frame_cell_counter(void) { return g_bfr_cell_counter; }
int engine_battlefield_render_middle_player_marker_blit_count(void) { return g_player_marker_blit_count; }
int engine_battlefield_render_middle_unit_key_blit_count(void)      { return g_unit_key_blit_count; }
int engine_battlefield_render_middle_effect_chain_blit_count(void)  { return g_effect_chain_blit_count; }
int engine_battlefield_render_player_marker_anim_frame(void)  { return g_bfr_marker_frame; }
int engine_battlefield_render_effect_chain_anim_frame(void)   { return g_bfr_effect_frame; }
int engine_battlefield_render_middle_effect_chain_iters(void) { return g_effect_chain_iters; }
int engine_battlefield_render_middle_player_marker_last_slot(void) { return g_player_marker_last_slot; }
int engine_battlefield_render_middle_unit_key_last_slot(void)      { return g_unit_key_last_slot; }
int engine_battlefield_render_late_per_cell_count(void)            { return g_late_per_cell_count; }
int engine_battlefield_render_late_creature_draw_count(void)       { return g_late_creature_draw_count; }
int engine_battlefield_render_late_creature_blit_count(void)       { return g_late_creature_blit_count; }
int engine_battlefield_render_late_selection_marker_count(void)    { return g_late_selection_marker_count; }
int engine_battlefield_render_late_selection_marker_blit_count(void) { return g_late_selection_marker_blit_count; }
int engine_battlefield_render_late_world_dirty_hook_count(void)    { return g_late_world_dirty_hook_count; }
int engine_battlefield_render_final_per_cell_count(void)     { return g_final_per_cell_count; }
int engine_battlefield_render_final_label_render_count(void) { return g_final_label_render_count; }

void engine_battlefield_render_reset_counts(void)
{
    g_count_terrain_zoom16 = g_count_terrain_zoom32 = g_count_middle = g_count_overlay_zoom16 = 0;
    g_count_overlay_zoom32 = g_count_late = g_count_final = 0;
    g_terrain_blit_count = g_terrain_z32_first_blit_count = g_terrain_z32_second_blit_count = 0;
    g_overlay_z32_blit_count = 0;
    g_middle_build_count = g_middle_sort_count = g_middle_render_count = 0;
    g_middle_player_marker_count = g_middle_unit_key_count = g_middle_effect_chain_count = 0;
    g_middle_unit_key_last_arg = g_middle_effect_chain_last_arg = -1;
    g_bfr_cell_counter = 0;
    g_bfr_visible_count = 0;
    g_bfr_sort_passes = 0;
    g_middle_per_entity_count = 0;
    g_middle_per_entity_last_id = -1;
    g_middle_per_entity_last_px_x = g_middle_per_entity_last_px_y = 0;
    g_middle_entity_blit_count = 0;
    g_middle_entity_type6_count = g_middle_entity_type7_count = g_middle_entity_type9_sub_count = 0;
    g_middle_entity_type6_last_id = g_middle_entity_type7_last_id = g_middle_entity_type9_sub_last_id = -1;
    g_middle_entity_no_blob_count = 0;
    g_middle_entity_no_blob_last_id = -1;
    g_player_marker_blit_count = 0;
    g_player_marker_last_slot = -1;
    g_bfr_marker_frame = 0;
    g_unit_key_blit_count = 0;
    g_unit_key_last_slot = -1;
    g_bfr_effect_frame = 0;
    g_effect_chain_iters = 0;
    g_effect_chain_blit_count = 0;
    g_bfr_effect_id = 0;
    g_middle_entity_type6_blit_count = g_middle_entity_type7_blit_count = g_middle_entity_type9_sub_blit_count = 0;
    g_late_per_cell_count = g_late_creature_draw_count = g_late_creature_blit_count = 0;
    g_late_selection_marker_count = g_late_selection_marker_blit_count = g_late_world_dirty_hook_count = 0;
    g_final_per_cell_count = g_final_label_render_count = 0;
    g_minimap_per_cell_count = g_minimap_blit_count = 0;
}
