/*
 * tests/map_render_harness.c — strategic-map render PARITY harness.
 *
 * Drives the already-ported engine_map_render cluster with retail's
 * LIVE map state (captured at a real strategic-map frame by
 * `frida_capture.py --dump-globals/--dump-regions`) into an offscreen
 * 8bpp canvas, then writes the raw indexed buffer out for a palette-
 * applied diff against the captured retail frame.
 *
 * This is the map-render analogue of the title-render parity check:
 * it validates the tile-walker (FUN_00405487 / 405862 / 4059af /
 * 408438) against ground-truth data BEFORE the unported screen-dispatch
 * composition (FUN_0040f5fd) + the .pl8 sprite-bank loaders are tackled.
 *
 * Inputs (all positional, supplied by tools/map_render_parity.py which
 * reads them from the capture's globals_dump.json):
 *
 *   argv[1]  dump_dir   — directory holding the 6 .bin region dumps:
 *                         tile_cells.bin, descriptors.bin,
 *                         territory_records.bin, blob_primary.bin,
 *                         blob_alt.bin, blob_overlay.bin
 *   argv[2]  out_raw    — output path for the 640x480 8bpp indexed buffer
 *   argv[3..17] scalars — cam_x0 cam_y0 col_start row_start col_count
 *                         row_count x_step y_step x_half_step view_mode
 *                         render_mode rotation overlay_base y_viewport
 *
 * On success writes <out_raw> (CANVAS_W*CANVAS_H bytes) and prints
 * `error_count=<n> call_count=<n>` to stdout.
 */

#include "engine_dirty_rect.h"
#include "engine_map_render.h"
#include "gfx_dispatch.h"
#include "gfx_paint.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CANVAS_W = 0x280, CANVAS_H = 0x1e0 };   /* 640 x 480, retail strat res */

#ifdef MAP_RENDER_DEBUG
static int g_dbg_n;
void map_render_debug_dispatch(int rec_type, int sprite_idx, int blob_is_alt,
                               int sprite_w, int h_short, int src_off,
                               int cur_x, int cur_y)
{
    if (g_dbg_n++ < 40)
        fprintf(stderr,
            "[dbg] type=%d idx=%d alt=%d w=%d h=%d src=%d cur=(%d,%d)\n",
            rec_type, sprite_idx, blob_is_alt, sprite_w, h_short, src_off,
            cur_x, cur_y);
}
#endif

/* Read an entire file into a malloc'd buffer; exit on failure. */
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
    if (argc < 3 + 14) {
        fprintf(stderr,
            "usage: %s <dump_dir> <out_raw> "
            "cam_x0 cam_y0 col_start row_start col_count row_count "
            "x_step y_step x_half_step view_mode render_mode rotation "
            "overlay_base y_viewport\n", argv[0]);
        return 2;
    }
    const char *dump_dir = argv[1];
    const char *out_raw  = argv[2];

    int s[14];
    for (int i = 0; i < 14; i++)
        s[i] = (int)strtol(argv[3 + i], NULL, 0);
    int cam_x0 = s[0], cam_y0 = s[1], col_start = s[2], row_start = s[3];
    int col_count = s[4], row_count = s[5], x_step = s[6], y_step = s[7];
    int x_half_step = s[8], view_mode = s[9], render_mode = s[10];
    int rotation = s[11], overlay_base = s[12], y_viewport = s[13];

    /* Load the captured regions. */
    size_t cells_len, descr_len, terr_len, prim_len, alt_len, ovl_len;
    uint8_t *cells = slurp(dump_dir, "tile_cells.bin",        &cells_len);
    uint8_t *descr = slurp(dump_dir, "descriptors.bin",       &descr_len);
    uint8_t *terr  = slurp(dump_dir, "territory_records.bin", &terr_len);
    uint8_t *prim  = slurp(dump_dir, "blob_primary.bin",      &prim_len);
    uint8_t *alt   = slurp(dump_dir, "blob_alt.bin",          &alt_len);
    uint8_t *ovl   = slurp(dump_dir, "blob_overlay.bin",      &ovl_len);

    /* Offscreen 8bpp canvas — palette index 0 background (matches the
     * test scaffold; whatever the tile-walker doesn't draw stays 0). */
    static uint8_t canvas[CANVAS_W * CANVAS_H];
    memset(canvas, 0, sizeof canvas);
    gfx_paint_bind_canvas(canvas, CANVAS_W, CANVAS_H);

    /* Dirty-rect state — local, mirrors the unit-test scaffold. */
    engine_dirty_rect_state_t dirty = {
        .dirty_mark = 0,
        .dirty_xmin = CANVAS_W - 1, .dirty_xmax = 0,
        .dirty_ymin = CANVAS_H - 1, .dirty_ymax = 0,
        .below_status_redraw = 0,
    };
    engine_map_render_set_dirty_state(&dirty);

    /* The drawer sets the real clip via clip_x/clip_y; alt-clip stays
     * disabled so it clips to the bound canvas (retail map region). */
    gfx_dispatch_set_alt_clip(/*enabled=*/0, /*left=*/0, /*bottom=*/0);

    /* Bind retail's live map state. */
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

    printf("error_count=%d call_count=%d "
           "cells=%zu descr=%zu terr=%zu prim=%zu alt=%zu ovl=%zu\n",
           engine_map_render_error_count(),
           engine_map_render_call_count(),
           cells_len, descr_len, terr_len, prim_len, alt_len, ovl_len);
    return 0;
}
