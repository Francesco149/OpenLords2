/*
 * OpenLords2 — strategic-map minimap render (see engine_minimap.h).
 *
 * Ports FUN_0046a037 / FUN_00451bba / FUN_00410cbd / FUN_00410aa9.
 */

#include "engine_minimap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io.h"                    /* io_read_file_at                     */
#include "gfx_paint.h"             /* gfx_paint_pixel_clipped (FUN_004039c7) */
#include "gfx_sprite_render.h"     /* gfx_sprite_render_raw (FUN_0040a01d),  */
                                   /* gfx_sprite_render_from_blob (40a21a),  */
                                   /* gfx_sprite_render_opaque_from_blob (40a567) */
#include "save.h"                  /* g_per_army_768                      */
#include "save_schema_globals.h"   /* s_slot_0057c93c / 0057c8cc / 0057a0c4 /
                                    * 0056d5dc                            */

/* The two 128x128 grids (retail DAT_0052aff0 / DAT_00569590). */
uint8_t g_minimap_region_grid[0x4000];
uint8_t g_minimap_type_grid[0x4000];

/* Per-territory record stride / field offsets (g_per_army_768, 0x300). */
#define ARMY_STRIDE   0x300
#define ARMY_COUNT    (13056 / ARMY_STRIDE)   /* 17 records (slot 0 reserved) */

/* retail DAT_00553e60 — a global food-detail toggle written by the
 * options screen (FUN_004b29be) and reset by init (FUN_00497500).  Not
 * save-backed; default 0.  Only gates the mode-2 (food) band detail in
 * engine_minimap_compute_bands, so it has no effect on the political
 * (mode 0) render.  Defined here as the first porter of a reader; a
 * later port of 4b29be/497500 should promote it to a shared global. */
static int s_food_detail_toggle /* DAT_00553e60 */ = 0;

/* Owner→colour table (retail DAT_004d2900): 6 rows of 8, indexed
 * [colour_class*8 + (terrain_type-10)].  terrain_type-10 is 0..3 in the
 * live path so only the first 4 of each row are reachable; the trailing
 * four (0x20,...) mirror the white-border highlight index. */
static const uint8_t MINIMAP_OWNER_COLOR[6 * 8] = {
    0x0a, 0x0b, 0x0c, 0x0d, 0x20, 0x0b, 0x0c, 0x0d,
    0x01, 0x0e, 0x0f, 0xf9, 0x20, 0x0e, 0x0f, 0xf9,
    0x03, 0xf2, 0xf3, 0xfb, 0x20, 0xf2, 0xf3, 0xfb,
    0x38, 0x35, 0x32, 0x2f, 0x20, 0x35, 0x32, 0x2f,
    0x05, 0xf4, 0xf5, 0xfd, 0x20, 0xf4, 0xf5, 0xfd,
    0x04, 0xfc, 0xf1, 0xf0, 0x20, 0xfc, 0xf1, 0xf0,
};

/* Overlay band→colour table (retail DAT_004d28f8): bands 0..5. */
static const uint8_t MINIMAP_BAND_COLOR[6] = {
    0x0f, 0x15, 0xf3, 0x09, 0xf1, 0x05,
};

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
/* FUN_00451bba — per-territory minimap band pre-pass.             */

void engine_minimap_compute_bands(void)
{
    int count = (int)s_slot_0056d5dc;
    if (count >= ARMY_COUNT)
        count = ARMY_COUNT - 1;          /* host array is exactly 17 records */

    for (int t = 1; t <= count; t++) {
        uint8_t *rec = &g_per_army_768[t * ARMY_STRIDE];

        /* band +1 (mode 3, wealth): signed field +0xc / 20. */
        rec[1] = (uint8_t)((int8_t)rec[0x0c] / 20);

        /* band +2 (mode 2, food): from the +0x15d/+0x15e food balance. */
        int8_t f0d = (int8_t)rec[0x15d];
        int8_t f0e = (int8_t)rec[0x15e];
        if (s_food_detail_toggle == 0) {
            rec[2] = (f0d < f0e) ? 0 : 6;
        } else if (f0d < f0e) {
            rec[2] = 0;
        } else if (rec[0x15d] == 0) {
            rec[2] = 1;
        } else if (rec[0x15d] == 1) {
            rec[2] = 2;
        } else if (rec[0x15d] == 2) {
            rec[2] = 3;
        } else if (rec[0x15d] == 3) {
            rec[2] = 4;
        } else {
            rec[2] = 5;
        }

        /* band +3 (mode 1, population): a resource-deficit count across
         * eight (consumed, produced) pairs, with a leading shortage gate. */
        int deficit = rec_i32(rec, 0x124);
        static const int pair[8][2] = {
            {0xcc, 0xc4}, {0xd8, 0xd0}, {0xe4, 0xdc}, {0xf0, 0xe8},
            {0xfc, 0xf4}, {0x108, 0x100}, {0x114, 0x10c}, {0x120, 0x118},
        };
        for (int i = 0; i < 8; i++)
            if (rec_i32(rec, pair[i][0]) < rec_i32(rec, pair[i][1]))
                deficit++;

        int shortage = (rec_i32(rec, 0xc4) < rec_i32(rec, 0xc8)) ||
                       (rec_i32(rec, 0xd0) < rec_i32(rec, 0xd4));

        if (shortage)
            rec[3] = 0;
        else if (deficit == 0)
            rec[3] = 6;
        else
            rec[3] = 5;
    }
}

/* ---------------------------------------------------------------- */
/* FUN_00410cbd — plot the coloured minimap.                       */

void engine_minimap_plot(unsigned current_territory, int x0, int y0, int mode)
{
    engine_minimap_compute_bands();

    if (current_territory == 0)
        return;

    unsigned realm = (unsigned)s_slot_0057c8cc;

    for (int row = 0; row < 0x80; row++) {
        for (int col = 0; col < 0x80; col++) {
            unsigned idx    = (unsigned)(col + row * 0x80);
            unsigned region = g_minimap_region_grid[idx];
            uint8_t  type   = g_minimap_type_grid[idx];

            if (type == 0x24 || type < 10 || type > 0x0d)
                continue;

            uint8_t tlo = (uint8_t)(type - 10);   /* 0..3 */
            uint8_t color;

            if (tlo == 0 && current_territory == region) {
                color = 0x20;                     /* current-territory border */
            } else {
                const uint8_t *rec = &g_per_army_768[region * ARMY_STRIDE];
                if (mode == 0) {
                    color = MINIMAP_OWNER_COLOR[rec[7] * 8 + tlo];
                } else if (mode == 1) {
                    if (rec[5] == realm && rec[3] < 6)
                        color = MINIMAP_BAND_COLOR[rec[3]];
                    else
                        continue;
                } else if (mode == 2) {
                    if (rec[5] != realm || rec[2] > 5)
                        continue;
                    color = MINIMAP_BAND_COLOR[rec[2]];
                } else { /* mode == 3 */
                    if (rec[5] != realm || rec[1] > 5)
                        continue;
                    color = MINIMAP_BAND_COLOR[rec[1]];
                }
            }

            gfx_paint_pixel_clipped(col + x0, y0 + row, color);
        }
    }
}

/* ---------------------------------------------------------------- */
/* FUN_00410aa9 — compose the minimap panel.                       */

void engine_minimap_render(const uint8_t *misc_cty, int x, int y)
{
    int mode = (int)s_slot_0057a0c4;
    unsigned cur = (unsigned)s_slot_0057c93c;

    /* Backdrop: the terrain-type grid blitted raw as an 128x128 sprite. */
    gfx_sprite_render_raw(g_minimap_type_grid, x - 2, y + 3, 0x80, 0x80);

    /* Panel frame sprite (mode 0 vs overlay variant). */
    if (misc_cty) {
        if (mode < 1)
            gfx_sprite_render_opaque_from_blob(misc_cty, 0x5c, x + 0x83, y + 7);
        else
            gfx_sprite_render_opaque_from_blob(misc_cty, 0x5b, x + 0x83, y + 7);
    }

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
    if (misc_cty) {
        if (mode == 1)
            gfx_sprite_render_from_blob(misc_cty, 0x5d, x + 5, y + 5);
        else if (mode == 2)
            gfx_sprite_render_from_blob(misc_cty, 0x5f, x + 5, y + 5);
        else if (mode == 3)
            gfx_sprite_render_from_blob(misc_cty, 0x5e, x + 5, y + 5);
    }
}
