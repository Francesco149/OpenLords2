/*
 * tests/terrain_render_harness.c — strategic-map PASS-1 (base terrain)
 * render parity harness.
 *
 * Drives engine_map_terrain with retail's captured live map state
 * (runs/capture/strat-terrain1, via tools/terrain_render_parity.py) into
 * an offscreen 8bpp canvas, then writes the raw indexed buffer out for a
 * palette-applied diff against the captured clean retail frame.
 *
 * Inputs (positional, supplied by terrain_render_parity.py):
 *   argv[1]   dump_dir   — holds tile_cells.bin, descriptors.bin,
 *                          terr_blob0..4.bin
 *   argv[2]   out_raw    — output path for the 640x480 8bpp indexed buffer
 *   argv[3..] scalars    — cam_x0 cam_y0 col_start row_start col_count
 *                          row_count x_step y_step x_half_step view_mode
 *                          render_mode
 *
 * On success writes <out_raw> and prints a stats line.
 */

#include "engine_map_terrain.h"
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
    if (argc < 3 + 11) {
        fprintf(stderr,
            "usage: %s <dump_dir> <out_raw> cam_x0 cam_y0 col_start "
            "row_start col_count row_count x_step y_step x_half_step "
            "view_mode render_mode\n", argv[0]);
        return 2;
    }
    const char *dump_dir = argv[1];
    const char *out_raw  = argv[2];

    int s[11];
    for (int i = 0; i < 11; i++)
        s[i] = (int)strtol(argv[3 + i], NULL, 0);
    int cam_x0 = s[0], cam_y0 = s[1], col_start = s[2], row_start = s[3];
    int col_count = s[4], row_count = s[5], x_step = s[6], y_step = s[7];
    int x_half_step = s[8], view_mode = s[9], render_mode = s[10];

    size_t cl, dl, b0l, b1l, b2l, b3l, b4l;
    uint8_t *cells = slurp(dump_dir, "tile_cells.bin",  &cl);
    uint8_t *descr = slurp(dump_dir, "descriptors.bin", &dl);
    uint8_t *b0 = slurp(dump_dir, "terr_blob0.bin", &b0l);
    uint8_t *b1 = slurp(dump_dir, "terr_blob1.bin", &b1l);
    uint8_t *b2 = slurp(dump_dir, "terr_blob2.bin", &b2l);
    uint8_t *b3 = slurp(dump_dir, "terr_blob3.bin", &b3l);
    uint8_t *b4 = slurp(dump_dir, "terr_blob4.bin", &b4l);

    static uint8_t canvas[CANVAS_W * CANVAS_H];
    memset(canvas, 0, sizeof canvas);
    gfx_paint_bind_canvas(canvas, CANVAS_W, CANVAS_H);

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

    FILE *o = fopen(out_raw, "wb");
    if (!o) { fprintf(stderr, "open %s: failed\n", out_raw); return 2; }
    if (fwrite(canvas, 1, sizeof canvas, o) != sizeof canvas) {
        fprintf(stderr, "write %s: short\n", out_raw); return 2;
    }
    fclose(o);

    printf("drawA=%d sky=%d drawB_skipped=%d oob=%d error_count=%d "
           "blobs=%zu,%zu,%zu,%zu,%zu\n",
           engine_map_terrain_drawa_count(),
           engine_map_terrain_sky_count(),
           engine_map_terrain_drawb_skipped(),
           engine_map_terrain_oob_copies(),
           engine_map_terrain_error_count(),
           b0l, b1l, b2l, b3l, b4l);
    return 0;
}
