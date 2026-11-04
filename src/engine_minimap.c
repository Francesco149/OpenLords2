/*
 * OpenLords2 — strategic-map minimap render (see engine_minimap.h, docs/findings/render-mapview.md).
 *
 * Ports FUN_0046a037 / FUN_00451bba / FUN_00410cbd / FUN_00410aa9 on retail's layout: the save block
 * (g_per_army_768, the s_slot_ scalars), the two grids (engine_minimap_globals.c), the food-detail toggle
 * DAT_00553e60 (engine_mapview_globals.h) and the misc sheet pointer DAT_005530c8 the caller passes.
 */

#include "engine_minimap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io.h"                    /* io_read_file_at                     */
#include "engine_mapview_globals.h" /* g_mv_food_detail (DAT_00553e60)    */
#include "gfx_paint.h"             /* gfx_paint_pixel_clipped (FUN_004039c7) */
#include "gfx_sprite_render.h"     /* gfx_sprite_render_raw (FUN_0040a01d),  */
                                   /* gfx_sprite_render_from_blob (40a21a),  */
                                   /* gfx_sprite_render_opaque_from_blob (40a567) */
#include "save.h"                  /* g_per_army_768                      */
#include "save_schema_globals.h"   /* s_slot_0057c93c / 0057c8cc / 0057a0c4 / 0056d5dc */

#define ARMY_STRIDE   0x300

/* The colour tables, contiguous in retail's .data: the band colours DAT_004d28f8 (bands 0..5, two pad bytes),
 * then the owner colours DAT_004d2900 (6 rows of 8, [owner * 8 + terrain - 10]).  Indexed without a bound, as
 * retail (an owner byte past 5 reads on); rhost points the lookup at retail's own bytes. */
static const uint8_t MINIMAP_COLORS[8 + 6 * 8 + 8] = {
    0x0f, 0x15, 0xf3, 0x09, 0xf1, 0x05, 0x05, 0x05,
    0x0a, 0x0b, 0x0c, 0x0d, 0x20, 0x0b, 0x0c, 0x0d,
    0x01, 0x0e, 0x0f, 0xf9, 0x20, 0x0e, 0x0f, 0xf9,
    0x03, 0xf2, 0xf3, 0xfb, 0x20, 0xf2, 0xf3, 0xfb,
    0x38, 0x35, 0x32, 0x2f, 0x20, 0x35, 0x32, 0x2f,
    0x05, 0xf4, 0xf5, 0xfd, 0x20, 0xf4, 0xf5, 0xfd,
    0x04, 0xfc, 0xf1, 0xf0, 0x20, 0xfc, 0xf1, 0xf0,
};
static const uint8_t *g_colors = MINIMAP_COLORS;   /* DAT_004d28f8 */

void engine_minimap_bind_color_tables(const uint8_t *band_colors) { g_colors = band_colors ? band_colors : MINIMAP_COLORS; }

/* ---------------------------------------------------------------- */
/* Little-endian record field accessors.                            */

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    memcpy(&v, rec + off, sizeof v);
    return v;
}

/* ---------------------------------------------------------------- */
/* FUN_0046a037 — load the two grids from map0{N}.pl8.              */

int engine_minimap_load_grids(const char *game_dir, int map_id)
{
    /* File index = map_id>>2 (table base "map01.pl8"); sub-map = map_id&3. */
    int file_idx = map_id >> 2;
    int sub      = map_id & 3;

    char path[1024];
    snprintf(path, sizeof path, "%s/map0%d.pl8", game_dir, file_idx + 1);

    uint32_t grid_off = 0;

    /* Region/territory-id grid: directory entry at (sub*0x50 + 0xc). */
    if (io_read_file_at(path, &grid_off, 4, sub * 0x50 + 0xc) != 4)
        return 0;
    if (io_read_file_at(path, g_minimap_region_grid, 0x4000, (long)grid_off) != 0x4000)
        return 0;

    /* Terrain-type grid: directory entry at ((sub*5 + 1)*0x10 + 0xc). */
    if (io_read_file_at(path, &grid_off, 4, ((sub * 5 + 1) * 0x10) + 0xc) != 4)
        return 0;
    if (io_read_file_at(path, g_minimap_type_grid, 0x4000, (long)grid_off) != 0x4000)
        return 0;

    return 1;
}

/* ---------------------------------------------------------------- */
/* FUN_00451bba — per-county minimap band pre-pass (counties 1..DAT_0056d5dc; no bound, as retail).          */

void engine_minimap_compute_bands(void)
{
    for (int t = 1; t <= (int)s_slot_0056d5dc; t++) {
        uint8_t *rec = &g_per_army_768[t * ARMY_STRIDE];

        /* band +1 (mode 3, wealth): signed approval +0xc / 20. */
        rec[1] = (uint8_t)((int8_t)rec[0x0c] / 20);

        /* band +2 (mode 2, food): from the +0x15d/+0x15e food balance (signed). */
        int8_t f0d = (int8_t)rec[0x15d];
        int8_t f0e = (int8_t)rec[0x15e];
        if (g_mv_food_detail != 0) {
            if (f0d >= f0e) {
                if (f0d != 0) {
                    switch (f0d) {
                    case 1: rec[2] = 2; break;
                    case 2: rec[2] = 3; break;
                    case 3: rec[2] = 4; break;
                    default: rec[2] = 5; break;
                    }
                } else {
                    rec[2] = 1;
                }
            } else {
                rec[2] = 0;
            }
        } else if (f0d >= f0e) {
            rec[2] = 6;
        } else {
            rec[2] = 0;
        }

        /* band +3 (mode 1, population): a resource-deficit count across eight (consumed, produced) pairs,
         * with a leading shortage gate. */
        int deficit = rec_i32(rec, 0x124);
        if (rec_i32(rec, 0xcc) < rec_i32(rec, 0xc4)) ++deficit;
        if (rec_i32(rec, 0xd8) < rec_i32(rec, 0xd0)) ++deficit;
        if (rec_i32(rec, 0xdc) > rec_i32(rec, 0xe4)) ++deficit;
        if (rec_i32(rec, 0xe8) > rec_i32(rec, 0xf0)) ++deficit;
        if (rec_i32(rec, 0xfc) < rec_i32(rec, 0xf4)) ++deficit;
        if (rec_i32(rec, 0x100) > rec_i32(rec, 0x108)) ++deficit;
        if (rec_i32(rec, 0x114) < rec_i32(rec, 0x10c)) ++deficit;
        if (rec_i32(rec, 0x120) < rec_i32(rec, 0x118)) ++deficit;
        if (rec_i32(rec, 0xc4) < rec_i32(rec, 0xc8) || rec_i32(rec, 0xd0) < rec_i32(rec, 0xd4))
            rec[3] = 0;
        else if (deficit != 0)
            rec[3] = 5;
        else
            rec[3] = 6;
    }
}

/* ---------------------------------------------------------------- */
/* FUN_00410cbd — plot the coloured minimap.                       */

void engine_minimap_plot(unsigned current_territory, int x0, int y0, int mode)
{
    engine_minimap_compute_bands();

    if (current_territory == 0)
        return;

    int v11 = 0;   /* retail's colour local: a mode outside 0..3 paints the last one (stack garbage at first) */
    for (int row = 0; row < 0x80; row++) {
        for (int col = 0; col < 0x80; col++) {
            unsigned idx    = (unsigned)(col + row * 0x80);
            unsigned region = g_minimap_region_grid[idx];
            uint8_t  type   = g_minimap_type_grid[idx];

            if (type == 0x24 || type < 10 || type > 0x0d)
                continue;

            uint8_t tlo = (uint8_t)(type - 10);   /* 0..3 */
            if (tlo == 0 && current_territory == region) {
                v11 = 0x20;                       /* current-territory border */
                gfx_paint_pixel_clipped(x0 + col, row + y0, 0x20);
                continue;
            }
            const uint8_t *rec = &g_per_army_768[region * ARMY_STRIDE];
            switch (mode) {
            case 0:
                v11 = g_colors[8 + 8 * rec[7] + tlo];
                break;
            case 1:
                if (rec[5] != s_slot_0057c8cc || rec[3] >= 6) continue;
                v11 = g_colors[rec[3]];
                break;
            case 2:
                if (rec[5] != s_slot_0057c8cc || rec[2] >= 6) continue;
                v11 = g_colors[rec[2]];
                break;
            case 3:
                if (rec[5] != s_slot_0057c8cc || rec[1] >= 6) continue;
                v11 = g_colors[rec[1]];
                break;
            default:
                break;
            }
            gfx_paint_pixel_clipped(x0 + col, row + y0, (uint8_t)v11);
        }
    }
}

/* ---------------------------------------------------------------- */
/* FUN_00410aa9 — compose the minimap panel (retail (x, y); the sheet = DAT_005530c8). */

void engine_minimap_render(const uint8_t *misc_cty, int x, int y)
{
    int mode = (int)s_slot_0057a0c4;
    unsigned cur = (unsigned)s_slot_0057c93c;

    /* Backdrop: the terrain-type grid blitted raw as an 128x128 sprite. */
    gfx_sprite_render_raw(g_minimap_type_grid, x - 2, y + 3, 0x80, 0x80);

    /* Panel frame sprite (mode 0 vs overlay variant). */
    if (mode < 1)
        gfx_sprite_render_opaque_from_blob(misc_cty, 0x5c, x + 0x83, y + 7);
    else
        gfx_sprite_render_opaque_from_blob(misc_cty, 0x5b, x + 0x83, y + 7);

    /* Coloured plot. */
    if (mode == 0)
        engine_minimap_plot(cur, x - 2, y + 3, 0);
    else if (mode == 1)
        engine_minimap_plot(cur, x - 2, y + 3, 1);
    else if (mode == 2)
        engine_minimap_plot(cur, x - 2, y + 3, 2);
    else if (mode == 3)
        engine_minimap_plot(cur, x - 2, y + 3, 3);

    /* Mode-toggle button icon (overlay modes only). */
    if (mode == 1)
        gfx_sprite_render_from_blob(misc_cty, 0x5d, x + 5, y + 5);
    else if (mode == 2)
        gfx_sprite_render_from_blob(misc_cty, 0x5f, x + 5, y + 5);
    else if (mode == 3)
        gfx_sprite_render_from_blob(misc_cty, 0x5e, x + 5, y + 5);
}
