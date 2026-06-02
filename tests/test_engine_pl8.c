/*
 * test_engine_pl8.c — deterministic byte-match validation of the
 * strategic-map .pl8 sprite-bank loader (src/engine_pl8.c, port of
 * retail FUN_004984dc).
 *
 * The loader mallocs + raw-reads each .pl8 bank named in the generated
 * table (src/gen/pl8_table.h) for the current season / view-mode.  We
 * drive it with the SAME state retail held when we captured the live
 * map blobs — season 1d (s_slot_0057c934 = 4), normal view
 * (s_slot_0057cb18 = 0), table offset 24 — then compare each loaded
 * bank, byte for byte, against the captured retail in-memory blobs in
 *   runs/capture/strat-terrain1/dump/
 * (terr_blob0..4 = banks 0..4; blob_primary/alt/overlay = banks
 * 5/6/7).  A match proves the table (filenames + sizes), the season/
 * view offset arithmetic, AND the bank→slot ladder all reproduce
 * retail's loaded sprite data exactly — the pivot from the harness's
 * captured blobs to the port's own native load.
 *
 * The captured dump blobs are heap OVER-reads (the capture estimated a
 * larger window than the true bank size), so each terr_blob.bin is a
 * superset; we compare the first `bank_size` bytes (the malloc/read
 * length retail used), which IS the whole .pl8 file content.
 *
 * Requires OPENLORDS2_GAME_DIR (the retail .pl8 files) and the capture
 * dump; SKIPs cleanly when either is absent.
 *
 * Provides a faithful POSIX io_read_file_at here (retail's
 * __open/__lseek/__read leaf FUN_004af8d9): the production io.c is
 * Win32-only and excluded from the host test binary.
 */

#include "t.h"
#include "engine_pl8.h"
#include "gen/pl8_table.h"
#include "save_schema_globals.h"

#include "engine_dirty_rect.h"
#include "engine_map_render.h"
#include "engine_map_scene.h"
#include "engine_map_terrain.h"
#include "engine_overlay_render.h"
#include "gfx_dispatch.h"
#include "gfx_paint.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#  include <io.h>
#  define OS_OPEN  _open
#  define OS_READ  _read
#  define OS_LSEEK _lseek
#  define OS_CLOSE _close
#  ifndef O_BINARY
#    define O_BINARY 0
#  endif
#else
#  include <unistd.h>
#  define OS_OPEN  open
#  define OS_READ  read
#  define OS_LSEEK lseek
#  define OS_CLOSE close
#  define O_BINARY 0
#endif

/* Faithful port of the file-read leaf FUN_004af8d9 (tier 1 only). */
int io_read_file_at(const char *path, void *buf, size_t len, long offset)
{
    if (!path || !buf || len == 0) return 0;
    int fd = OS_OPEN(path, O_RDONLY | O_BINARY);
    if (fd < 0) return 0;
    int got = 0;
    if (OS_LSEEK(fd, offset, SEEK_SET) != (long)-1) {
        long n = (long)OS_READ(fd, buf, (unsigned)len);
        if (n > 0) got = (int)n;
    }
    OS_CLOSE(fd);
    return got;
}

static const char *path_in(const char *dir, const char *name)
{
    static char buf[1280];
    snprintf(buf, sizeof buf, "%s/%s", dir, name);
    return buf;
}

static uint8_t *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t rd = fread(b, 1, (size_t)n, f);
    fclose(f);
    *len_out = rd;
    return b;
}

/* slot → captured dump filename (the live retail in-memory blob). */
static const char *DUMP_NAME[ENGINE_PL8_BANK_COUNT] = {
    "terr_blob0.bin", "terr_blob1.bin", "terr_blob2.bin",
    "terr_blob3.bin", "terr_blob4.bin",
    "blob_primary.bin", "blob_alt.bin", "blob_overlay.bin",
};

int test_engine_pl8_banks_match_captured_blobs(void)
{
    const char *gamedir = getenv("OPENLORDS2_GAME_DIR");
    if (!gamedir || !gamedir[0])
        T_SKIP("OPENLORDS2_GAME_DIR unset (need retail .pl8 files)");

    char dumpdir[1024];
    snprintf(dumpdir, sizeof dumpdir,
             "%s/runs/capture/strat-terrain1/dump", OPENLORDS2_ROOT);
    struct stat st;
    char probe[1280];
    snprintf(probe, sizeof probe, "%s/terr_blob0.bin", dumpdir);
    if (stat(probe, &st) != 0)
        T_SKIP("capture dump missing (%s)", dumpdir);

    /* Drive the same state the capture held: season 1d, normal view. */
    s_slot_0057c934 = 4;   /* season → offset 24 (the *1d block) */
    s_slot_0057cb18 = 0;   /* view-mode normal */

    int ok = engine_pl8_load_strategic_banks(gamedir);
    if (!ok) {
        engine_pl8_free_strategic_banks();
        T_FAIL("engine_pl8_load_strategic_banks failed (missing/short "
               ".pl8 files under %s?)", gamedir);
    }

    int rc = 0;
    for (int i = 0; i < ENGINE_PL8_BANK_COUNT; i++) {
        const uint8_t *bank = engine_pl8_bank(i);
        size_t bsz = engine_pl8_bank_size(i);
        if (!bank || bsz == 0) {
            fprintf(stderr, "  fail: bank %d not loaded\n", i);
            rc = 1; continue;
        }
        char path[1280];
        snprintf(path, sizeof path, "%s/%s", dumpdir, DUMP_NAME[i]);
        size_t dl = 0;
        uint8_t *dump = slurp(path, &dl);
        if (!dump) {
            fprintf(stderr, "  fail: cannot read %s\n", path);
            rc = 1; continue;
        }
        /* The loaded buffer is malloc(table_size); the .pl8 file is a
         * little shorter, so only the file-length prefix is valid data
         * (the tail is malloc slack on both sides).  Compare the bytes
         * the read actually filled, bounded by the dump window. */
        size_t cmp = bsz < dl ? bsz : dl;
        size_t first_diff = (size_t)-1;
        for (size_t k = 0; k < cmp; k++) {
            if (bank[k] != dump[k]) { first_diff = k; break; }
        }
        if (first_diff != (size_t)-1) {
            fprintf(stderr, "  fail: bank %d (%s) differs at byte %zu "
                    "(port 0x%02x vs retail 0x%02x), cmp=%zu bsz=%zu "
                    "dump=%zu\n", i, DUMP_NAME[i], first_diff,
                    bank[first_diff], dump[first_diff], cmp, bsz, dl);
            rc = 1;
        } else {
            fprintf(stderr, "  ok: bank %d (%s) matches %zu bytes\n",
                    i, DUMP_NAME[i], cmp);
        }
        free(dump);
    }

    engine_pl8_free_strategic_banks();
    return rc;
}

/* ── End-to-end integration: native-loaded banks render the same frame ──
 *
 * The byte-match test proves the loaded bank BYTES equal retail's; this
 * proves the loaded banks, fed to the actual renderer with the loader's
 * `bytes-read` blob lengths (tighter than the capture's heap over-read
 * lengths the scene harness uses), produce a PIXEL-IDENTICAL strategic-
 * map frame.  The terrain blitter gates each copy on the blob length
 * (`s + sz > blob_len` → skip), so a too-short length would silently
 * drop sprites — this asserts it does not (no real sprite data lives
 * past the .pl8 file end).
 *
 * Renders the composed 3-cluster scene (engine_map_scene_render_body,
 * FUN_004050f6) into a canvas with the captured strat-terrain1 scalars,
 * once with the dump blobs + dump lengths and once with the engine_pl8
 * banks + bytes-read lengths, and compares the two canvases.
 */
enum { SCENE_W = 0x280, SCENE_H = 0x1e0 };   /* 640 x 480 */

/* Captured strat-terrain1 scalars (globals_dump.json). */
static const int SC_cam_x0 = 0,  SC_cam_y0 = 9,   SC_col_start = 31;
static const int SC_row_start = 78, SC_col_count = 8, SC_row_count = 30;
static const int SC_x_step = 60, SC_y_step = 15,  SC_x_half = 30;
static const int SC_view = 0,    SC_render = 0,    SC_rot = 0;
static const int SC_overlay_base = 0, SC_y_viewport = 30, SC_arrow_base = 0;

/* Full 3-cluster scene render (mirror of tests/scene_render_harness.c)
 * into `canvas`, using the supplied terrain blobs + lengths and feature
 * blobs.  Returns the four draw-count stats packed for comparison. */
static void render_scene(uint8_t *canvas,
                         const uint8_t *b[5], const int blen[5],
                         const uint8_t *prim, const uint8_t *alt,
                         const uint8_t *ovl,
                         const uint8_t *cells, const uint8_t *descr,
                         const uint8_t *terr,  const uint8_t *army,
                         int counts[5])
{
    memset(canvas, 0, (size_t)SCENE_W * SCENE_H);
    gfx_paint_bind_canvas(canvas, SCENE_W, SCENE_H);

    engine_map_terrain_bind_tile_array((const int32_t *)cells, 0x104);
    engine_map_terrain_bind_descriptors(descr);
    engine_map_terrain_bind_blobs(b[0], b[1], b[2], b[3], b[4]);
    engine_map_terrain_set_blob_lengths(blen[0], blen[1], blen[2],
                                        blen[3], blen[4]);
    engine_map_terrain_bind_camera(SC_cam_x0, SC_cam_y0, SC_col_start,
                                   SC_row_start, SC_col_count, SC_row_count,
                                   SC_x_step, SC_y_step, SC_x_half);
    engine_map_terrain_set_view_mode(SC_view);
    engine_map_terrain_set_render_mode(SC_render);
    engine_map_terrain_set_empty_redraw_gate(0);
    engine_map_terrain_set_error_count(0);
    engine_map_terrain_reset_stats();

    engine_dirty_rect_state_t dirty = {
        .dirty_mark = 0,
        .dirty_xmin = SCENE_W - 1, .dirty_xmax = 0,
        .dirty_ymin = SCENE_H - 1, .dirty_ymax = 0,
        .below_status_redraw = 0,
    };
    gfx_dispatch_set_alt_clip(0, 0, 0);

    engine_overlay_render_bind_tile_array((const int32_t *)cells, 0x104);
    engine_overlay_render_bind_tile_descriptors((uint8_t *)(uintptr_t)descr);
    engine_overlay_render_bind_overlay_blob(ovl);
    engine_overlay_render_bind_territory_records(terr);
    engine_overlay_render_bind_army_records(army);
    engine_overlay_render_bind_camera(SC_cam_x0, SC_cam_y0, SC_col_start,
                                      SC_row_start, SC_col_count, SC_row_count,
                                      SC_x_step, SC_y_step, SC_x_half);
    engine_overlay_render_bind_canvas(canvas, SCENE_W, SCENE_H);
    engine_overlay_render_set_dirty_state(&dirty);
    engine_overlay_render_set_view_mode(SC_view);
    engine_overlay_render_set_render_mode(SC_render);
    engine_overlay_render_set_animation_tick(0);
    engine_overlay_render_set_active_territory(0);
    engine_overlay_render_set_dev_flags(0, 0);
    engine_overlay_render_set_error_count(0);
    engine_overlay_render_reset_call_counts();

    engine_map_render_set_dirty_state(&dirty);
    engine_map_render_bind_tile_array((const int32_t *)cells, 0x104);
    engine_map_render_bind_tile_descriptors(descr);
    engine_map_render_bind_territory_records(terr);
    engine_map_render_bind_blobs(prim, alt, ovl);
    engine_map_render_bind_camera(SC_cam_x0, SC_cam_y0, SC_col_start,
                                  SC_row_start, SC_col_count, SC_row_count,
                                  SC_x_step, SC_y_step, SC_x_half);
    engine_map_render_set_view_mode(SC_view);
    engine_map_render_set_render_mode(SC_render);
    engine_map_render_set_rotation_offset(SC_rot);
    engine_map_render_set_y_viewport_offset(SC_y_viewport);
    engine_map_render_set_error_count(0);
    engine_map_render_reset_call_count();

    engine_map_scene_set_anim_counters(SC_overlay_base << 4,
                                       SC_arrow_base << 4);
    engine_map_scene_render_body();

    counts[0] = engine_map_terrain_drawa_count();
    counts[1] = engine_map_terrain_drawb_count();
    counts[2] = engine_map_terrain_empty_draw_count();
    counts[3] = engine_overlay_render_army_call_count();
    counts[4] = engine_map_render_call_count();
}

int test_engine_pl8_native_load_renders_identical_frame(void)
{
    const char *gamedir = getenv("OPENLORDS2_GAME_DIR");
    if (!gamedir || !gamedir[0])
        T_SKIP("OPENLORDS2_GAME_DIR unset (need retail .pl8 files)");

    char dumpdir[1024];
    snprintf(dumpdir, sizeof dumpdir,
             "%s/runs/capture/strat-terrain1/dump", OPENLORDS2_ROOT);
    struct stat st;
    char probe[1280];
    snprintf(probe, sizeof probe, "%s/terr_blob0.bin", dumpdir);
    if (stat(probe, &st) != 0)
        T_SKIP("capture dump missing (%s)", dumpdir);

    /* Shared game state (NOT from .pl8 — these are the live map arrays). */
    size_t cl, dl, tl, aml;
    uint8_t *cells = slurp(path_in(dumpdir, "tile_cells.bin"), &cl);
    uint8_t *descr = slurp(path_in(dumpdir, "descriptors.bin"), &dl);
    uint8_t *terr  = slurp(path_in(dumpdir, "territory_records.bin"), &tl);
    uint8_t *army  = slurp(path_in(dumpdir, "army_records.bin"), &aml);
    if (!cells || !descr || !terr || !army)
        T_FAIL("could not read captured game-state arrays");

    /* Dump blobs (the scene harness's reference inputs) + over-read lens. */
    size_t b0l, b1l, b2l, b3l, b4l, pl, al, ol;
    uint8_t *d0 = slurp(path_in(dumpdir, "terr_blob0.bin"), &b0l);
    uint8_t *d1 = slurp(path_in(dumpdir, "terr_blob1.bin"), &b1l);
    uint8_t *d2 = slurp(path_in(dumpdir, "terr_blob2.bin"), &b2l);
    uint8_t *d3 = slurp(path_in(dumpdir, "terr_blob3.bin"), &b3l);
    uint8_t *d4 = slurp(path_in(dumpdir, "terr_blob4.bin"), &b4l);
    uint8_t *dp = slurp(path_in(dumpdir, "blob_primary.bin"), &pl);
    uint8_t *da = slurp(path_in(dumpdir, "blob_alt.bin"), &al);
    uint8_t *doo = slurp(path_in(dumpdir, "blob_overlay.bin"), &ol);

    /* Native load (season 1d / view 0, the captured frame's state). */
    s_slot_0057c934 = 4;
    s_slot_0057cb18 = 0;
    if (!engine_pl8_load_strategic_banks(gamedir)) {
        engine_pl8_free_strategic_banks();
        T_FAIL("engine_pl8_load_strategic_banks failed");
    }

    static uint8_t canvas_dump[SCENE_W * SCENE_H];
    static uint8_t canvas_pl8[SCENE_W * SCENE_H];
    int counts_dump[5], counts_pl8[5];

    const uint8_t *db[5] = { d0, d1, d2, d3, d4 };
    int dlen[5] = { (int)b0l, (int)b1l, (int)b2l, (int)b3l, (int)b4l };
    render_scene(canvas_dump, db, dlen, dp, da, doo,
                 cells, descr, terr, army, counts_dump);

    const uint8_t *nb[5];
    int nlen[5];
    for (int i = 0; i < 5; i++) {
        nb[i] = engine_pl8_bank(i);
        nlen[i] = (int)engine_pl8_bank_size(i);
    }
    render_scene(canvas_pl8, nb, nlen,
                 engine_pl8_bank(5), engine_pl8_bank(6), engine_pl8_bank(7),
                 cells, descr, terr, army, counts_pl8);

    int rc = 0;
    if (memcmp(canvas_dump, canvas_pl8, sizeof canvas_dump) != 0) {
        size_t n = 0;
        for (size_t k = 0; k < sizeof canvas_dump; k++)
            if (canvas_dump[k] != canvas_pl8[k]) n++;
        fprintf(stderr, "  fail: %zu/%zu pixels differ between dump-bound "
                "and native-pl8-loaded renders\n", n, sizeof canvas_dump);
        rc = 1;
    }
    for (int i = 0; i < 5; i++) {
        if (counts_dump[i] != counts_pl8[i]) {
            fprintf(stderr, "  fail: draw-count[%d] dump=%d pl8=%d\n",
                    i, counts_dump[i], counts_pl8[i]);
            rc = 1;
        }
    }
    if (rc == 0)
        fprintf(stderr, "  ok: native-loaded banks render byte-identical "
                "(drawA=%d drawB=%d sky=%d army=%d feat=%d)\n",
                counts_pl8[0], counts_pl8[1], counts_pl8[2],
                counts_pl8[3], counts_pl8[4]);

    engine_pl8_free_strategic_banks();
    free(cells); free(descr); free(terr); free(army);
    free(d0); free(d1); free(d2); free(d3); free(d4);
    free(dp); free(da); free(doo);
    return rc;
}

/* Offset arithmetic: season/view-mode → table base index, exercised
 * without touching the filesystem (pure table indexing sanity). */
int test_engine_pl8_season_offset(void)
{
    /* season 1 (1a) view 0 → offset 0 → bank 0 = base1a.pl8 */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[0].name, "base1a.pl8");
    /* season 4 (1d) view 0 → offset 24 → bank 0 = base1d.pl8 */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[0 + 24].name, "base1d.pl8");
    /* banks 5/6/7 are season-invariant across the 1d block */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[5 + 24].name, "sprite1a.pl8");
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[6 + 24].name, "sprite1b.pl8");
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[7 + 24].name, "flags1a.pl8");
    /* view-mode 2 → offset 0x20 → bank 0 = base2a.pl8 */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[0 + 0x20].name, "base2a.pl8");
    return 0;
}
