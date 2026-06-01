/*
 * test_engine_minimap.c — end-to-end validation of the strategic-map
 * minimap render (src/engine_minimap.c, ports FUN_0046a037 /
 * FUN_00451bba / FUN_00410cbd / FUN_00410aa9).
 *
 * Loads the real lastturn.sav (populates the per-territory records,
 * current territory / realm, minimap mode, territory count), loads the
 * two 128x128 grids from Map0{N}.pl8 via the ported FUN_0046a037, then
 * renders the political minimap into a 640x480 8bpp canvas.  The raw
 * canvas is written to runs/parity/minimap/port.raw for a palette-applied
 * pixel diff against the captured retail freeroam frame
 * (runs/capture/load_clean_dump/frames/frame_00000.bmp) by
 * tools/minimap_parity.py.
 *
 * The deterministic in-test assertions guard the structural invariants:
 *   - both grids load (correct directory offsets / file index),
 *   - every land cell's region id fits the 17-record host array,
 *   - the plot paints a non-trivial number of pixels in the 128x128 box.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "t.h"

#include "save.h"                  /* save_load, g_per_army_768            */
#include "save_schema_globals.h"   /* s_slot_0053f034 / 0057c93c / 0057c8cc /
                                    * 0057a0c4 / 0056d5dc                  */
#include "engine_minimap.h"
#include "gfx_paint.h"

enum { CANVAS_W = 640, CANVAS_H = 480 };

/* Minimap screen box: FUN_00410aa9(0x1e0,0x19) blits/plots at
 * (0x1e0-2, 0x19+3) = (478-2, 28) over 128x128. */
enum { MM_X = 0x1e0 - 2, MM_Y = 0x19 + 3, MM_W = 0x80, MM_H = 0x80 };

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int resolve_game_dir(char *dst, size_t cap)
{
    const char *env = getenv("OPENLORDS2_GAME_DIR");
    if (env && *env) { snprintf(dst, cap, "%s", env); return 1; }
    snprintf(dst, cap, "%s/vendor/original", OPENLORDS2_ROOT);
    return 1;
}

int test_minimap_render_political(void)
{
    char gdir[1024];
    resolve_game_dir(gdir, sizeof gdir);

    char sav_path[1200], map_path[1200];
    snprintf(sav_path, sizeof sav_path, "%s/lastturn.sav", gdir);
    snprintf(map_path, sizeof map_path, "%s/Map01.pl8", gdir);
    if (!file_exists(sav_path))
        T_SKIP("retail lastturn.sav not found at %s", sav_path);
    if (!file_exists(map_path))
        T_SKIP("retail Map0N.pl8 not found at %s", map_path);

    /* 1. Load the real save → per-territory records + minimap scalars. */
    const char *tmp_castles = "/tmp/openlords2_minimap_castles.dat";
    if (save_load(sav_path, tmp_castles) != 0)
        T_FAIL("save_load(%s) failed", sav_path);
    int map_id = (int)s_slot_0053f034;

    /* 2. Load the two grids via the ported FUN_0046a037. */
    if (!engine_minimap_load_grids(gdir, map_id))
        T_FAIL("engine_minimap_load_grids failed (map_id=%d)", map_id);

    /* 3. Structural invariant: every land cell's region id indexes a
     *    valid per-territory record (the host array is exactly 17). */
    int land = 0, max_region = -1;
    for (int i = 0; i < 0x4000; i++) {
        uint8_t type = g_minimap_type_grid[i];
        if (type == 0x24 || type < 10 || type > 0x0d)
            continue;
        land++;
        int r = g_minimap_region_grid[i];
        if (r > max_region) max_region = r;
    }
    if (land == 0)
        T_FAIL("no land cells found in minimap grids");
    if (max_region >= (13056 / 0x300))
        T_FAIL("land cell region id %d exceeds 17-record array", max_region);

    /* 4. Render the political minimap into a 640x480 canvas (no chrome
     *    sprites — NULL misc_cty blob). */
    static uint8_t canvas[CANVAS_W * CANVAS_H];
    memset(canvas, 0, sizeof canvas);
    gfx_paint_bind_canvas(canvas, CANVAS_W, CANVAS_H);
    s_slot_0057a0c4 = 0;                       /* political mode */
    engine_minimap_render(NULL, 0x1e0, 0x19);

    /* 5. Count painted pixels inside the minimap box. */
    int painted = 0;
    for (int y = MM_Y; y < MM_Y + MM_H; y++)
        for (int x = MM_X; x < MM_X + MM_W; x++)
            if (canvas[x + y * CANVAS_W] != 0) painted++;
    if (painted < MM_W * MM_H / 2)
        T_FAIL("minimap painted only %d/%d px", painted, MM_W * MM_H);

    /* 6. Dump the raw canvas for tools/minimap_parity.py. */
    char outdir[1200], outraw[1300];
    snprintf(outdir, sizeof outdir, "%s/runs/parity/minimap", OPENLORDS2_ROOT);
    mkdir(outdir, 0755);                        /* parents assumed to exist */
    snprintf(outraw, sizeof outraw, "%s/port.raw", outdir);
    FILE *o = fopen(outraw, "wb");
    if (o) {
        fwrite(canvas, 1, sizeof canvas, o);
        fclose(o);
    }

    fprintf(stderr,
            "[minimap] map_id=%d land=%d max_region=%d cur_terr=%u "
            "realm=%u count=%u painted=%d -> %s\n",
            map_id, land, max_region, (unsigned)s_slot_0057c93c,
            (unsigned)s_slot_0057c8cc, (unsigned)s_slot_0056d5dc,
            painted, outraw);
    return 0;
}
