/*
 * tests/scene_render_harness.c — strategic-map COMPOSED scene parity harness.
 *
 * Composes the two already-ported map clusters into ONE offscreen 8bpp
 * canvas, in retail draw order, against the captured live map state
 * (runs/capture/strat-terrain1, via tools/scene_render_parity.py):
 *
 *   1. pass-1 base terrain   engine_map_terrain_frame()   (FUN_0040526e)
 *   2. feature walker        engine_map_render_frame()     (FUN_00405487)
 *
 * Pass 2 (the overlay cluster FUN_00405602) is not ported yet; the HUD
 * chrome (menu bar, right realm panel) is drawn by separate functions.
 * The composed buffer is written raw for a palette-applied diff against the
 * captured clean retail frame.  This is the next milestone after pass-1
 * was validated standalone (terrain_render_harness): it shows base terrain
 * + map features (cattle/wagons/units) together, leaving overlay + HUD as
 * the only remaining gap.
 *
 * Inputs (positional, supplied by tools/scene_render_parity.py from the
 * capture's globals_dump.json) — same 14 scalars as map_render_harness:
 *   argv[1]  dump_dir   — tile_cells/descriptors/territory_records,
 *                         terr_blob0..4, blob_primary/alt/overlay
 *   argv[2]  out_raw    — output path for the 640x480 8bpp indexed buffer
 *   argv[3..16] scalars — cam_x0 cam_y0 col_start row_start col_count
 *                         row_count x_step y_step x_half_step view_mode
 *                         render_mode rotation overlay_base y_viewport
 */

#include "engine_dirty_rect.h"
#include "engine_map_render.h"
#include "engine_map_terrain.h"
#include "engine_overlay_render.h"
#include "gfx_dispatch.h"
#include "gfx_paint.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CANVAS_W = 0x280, CANVAS_H = 0x1e0 };   /* 640 x 480 */

static uint8_t *slurp(const char *dir, const char *name, size_t *out_len)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "open %s: failed\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fprintf(stderr, "size %s: failed\n", path); exit(2); }
    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf) { fprintf(stderr, "oom %ld\n", n); exit(2); }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "read %s: short\n", path); exit(2);
    }
    fclose(f);
    if (out_len) *out_len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 3 + 15) {
        fprintf(stderr,
            "usage: %s <dump_dir> <out_raw> "
            "cam_x0 cam_y0 col_start row_start col_count row_count "
            "x_step y_step x_half_step view_mode render_mode rotation "
            "overlay_base y_viewport arrow_marker_base\n", argv[0]);
        return 2;
    }
    const char *dump_dir = argv[1];
    const char *out_raw  = argv[2];

    int s[15];
    for (int i = 0; i < 15; i++)
        s[i] = (int)strtol(argv[3 + i], NULL, 0);
    int cam_x0 = s[0], cam_y0 = s[1], col_start = s[2], row_start = s[3];
    int col_count = s[4], row_count = s[5], x_step = s[6], y_step = s[7];
    int x_half_step = s[8], view_mode = s[9], render_mode = s[10];
    int rotation = s[11], overlay_base = s[12], y_viewport = s[13];
    int arrow_marker_base = s[14];

    /* Shared map state used by both clusters. */
    size_t cl, dl, tl, b0l, b1l, b2l, b3l, b4l, pl, al, ol;
    uint8_t *cells = slurp(dump_dir, "tile_cells.bin",        &cl);
    uint8_t *descr = slurp(dump_dir, "descriptors.bin",       &dl);
    uint8_t *terr  = slurp(dump_dir, "territory_records.bin", &tl);
    uint8_t *b0 = slurp(dump_dir, "terr_blob0.bin", &b0l);
    uint8_t *b1 = slurp(dump_dir, "terr_blob1.bin", &b1l);
    uint8_t *b2 = slurp(dump_dir, "terr_blob2.bin", &b2l);
    uint8_t *b3 = slurp(dump_dir, "terr_blob3.bin", &b3l);
    uint8_t *b4 = slurp(dump_dir, "terr_blob4.bin", &b4l);
    uint8_t *prim = slurp(dump_dir, "blob_primary.bin", &pl);
    uint8_t *alt  = slurp(dump_dir, "blob_alt.bin",     &al);
    uint8_t *ovl  = slurp(dump_dir, "blob_overlay.bin", &ol);
    size_t aml;
    uint8_t *army = slurp(dump_dir, "army_records.bin", &aml);

    static uint8_t canvas[CANVAS_W * CANVAS_H];
    memset(canvas, 0, sizeof canvas);
    gfx_paint_bind_canvas(canvas, CANVAS_W, CANVAS_H);

    /* ---- Cluster 1: pass-1 base terrain ---- */
    engine_map_terrain_bind_tile_array((const int32_t *)cells, 0x104);
    engine_map_terrain_bind_descriptors(descr);
    engine_map_terrain_bind_blobs(b0, b1, b2, b3, b4);
    engine_map_terrain_set_blob_lengths((int)b0l, (int)b1l, (int)b2l,
                                        (int)b3l, (int)b4l);
    engine_map_terrain_bind_camera(cam_x0, cam_y0, col_start, row_start,
                                   col_count, row_count, x_step, y_step,
                                   x_half_step);
    engine_map_terrain_set_view_mode(view_mode);
    engine_map_terrain_set_render_mode(render_mode);
    engine_map_terrain_set_sky_gate_flag(0);
    engine_map_terrain_set_error_count(0);
    engine_map_terrain_reset_stats();
    engine_map_terrain_frame();

    /* Shared dirty-rect scratch + clip (overlay + feature walker). */
    engine_dirty_rect_state_t dirty = {
        .dirty_mark = 0,
        .dirty_xmin = CANVAS_W - 1, .dirty_xmax = 0,
        .dirty_ymin = CANVAS_H - 1, .dirty_ymax = 0,
        .below_status_redraw = 0,
    };
    gfx_dispatch_set_alt_clip(/*enabled=*/0, /*left=*/0, /*bottom=*/0);

    /* ---- Cluster 2: overlay (army / territory markers, arrows) ---- */
    engine_overlay_render_bind_tile_array((const int32_t *)cells, 0x104);
    engine_overlay_render_bind_tile_descriptors(descr);
    engine_overlay_render_bind_overlay_blob(ovl);
    engine_overlay_render_bind_territory_records(terr);
    engine_overlay_render_bind_army_records(army);
    engine_overlay_render_bind_camera(cam_x0, cam_y0, col_start, row_start,
                                      col_count, row_count, x_step, y_step,
                                      x_half_step);
    engine_overlay_render_bind_canvas(canvas, CANVAS_W, CANVAS_H);
    engine_overlay_render_set_dirty_state(&dirty);
    engine_overlay_render_set_view_mode(view_mode);
    engine_overlay_render_set_render_mode(render_mode);
    engine_overlay_render_set_overlay_base_index(overlay_base);
    engine_overlay_render_set_arrow_marker_base_index(arrow_marker_base);
    engine_overlay_render_set_animation_tick(0);
    engine_overlay_render_set_active_territory(0);
    engine_overlay_render_set_dev_flags(0, 0);
    engine_overlay_render_set_error_count(0);
    engine_overlay_render_reset_call_counts();
    engine_overlay_render_frame();

    /* ---- Cluster 3: feature walker (drawn over terrain + overlay) ---- */
    engine_map_render_set_dirty_state(&dirty);

    engine_map_render_bind_tile_array((const int32_t *)cells, 0x104);
    engine_map_render_bind_tile_descriptors(descr);
    engine_map_render_bind_territory_records(terr);
    engine_map_render_bind_blobs(prim, alt, ovl);
    engine_map_render_bind_camera(cam_x0, cam_y0, col_start, row_start,
                                  col_count, row_count, x_step, y_step,
                                  x_half_step);
    engine_map_render_set_view_mode(view_mode);
    engine_map_render_set_render_mode(render_mode);
    engine_map_render_set_rotation_offset(rotation);
    engine_map_render_set_overlay_base_index(overlay_base);
    engine_map_render_set_y_viewport_offset(y_viewport);
    engine_map_render_set_error_count(0);
    engine_map_render_reset_call_count();
    engine_map_render_frame();

    FILE *o = fopen(out_raw, "wb");
    if (!o) { fprintf(stderr, "open %s: failed\n", out_raw); return 2; }
    if (fwrite(canvas, 1, sizeof canvas, o) != sizeof canvas) {
        fprintf(stderr, "write %s: short\n", out_raw); return 2;
    }
    fclose(o);

    printf("terrain: drawA=%d sky=%d oob=%d err=%d | "
           "overlay: army=%d city=%d err=%d | "
           "features: err=%d call=%d\n",
           engine_map_terrain_drawa_count(),
           engine_map_terrain_sky_count(),
           engine_map_terrain_oob_copies(),
           engine_map_terrain_error_count(),
           engine_overlay_render_army_call_count(),
           engine_overlay_render_city_call_count(),
           engine_overlay_render_error_count(),
           engine_map_render_error_count(),
           engine_map_render_call_count());
    return 0;
}
