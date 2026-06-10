/*
 * test_engine_hud_panel.c — end-to-end validation of the strategic-map
 * territory-info panel (src/engine_hud_panel.c, port of FUN_0040f7d3 +
 * its eight resource subdrawers).
 *
 * Loads the real lastturn.sav (populates the per-territory records,
 * current territory / realm, map id), loads the misc_cty sprite bank +
 * the three fonts (fntl2_9 / font_10 / fntl2_14) + the l2.eng
 * localization blob from the retail game dir, then renders the panel
 * into a 640x480 8bpp canvas.  The raw canvas is written to
 * runs/parity/hud_panel/port.raw for a palette-applied pixel diff
 * against the captured retail freeroam frame
 * (runs/capture/load_clean_dump/frames/frame_00000.bmp) by
 * tools/panel_parity.py.
 *
 * Deterministic in-test assertions guard the structural invariants:
 *   - the current territory is owned by the current realm (so the full
 *     info panel branch renders — matching the captured frame),
 *   - the panel paints a non-trivial number of pixels in the right strip
 *     below the minimap.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "t.h"

#include "save.h"                  /* save_load, g_per_army_768,
                                    * g_player_strategic_352               */
#include "save_schema_globals.h"   /* s_slot_0053f034 / 0057c93c / 0057c8cc */
#include "engine_hud_panel.h"
#include "engine_pl8.h"            /* engine_pl8_load_strategic_banks,
                                    * engine_pl8_misc_cty                  */
#include "gfx_paint.h"
#include "gfx_glyph_table.h"       /* gfx_glyph_render_map / measure_map   */
#include "gfx_dispatch.h"          /* gfx_dispatch_set_default_font / clip */
#include "gfx_text.h"              /* gfx_text_set_screen_byte            */
#include "io.h"                    /* io_read_file_at                      */
#include "localization.h"

enum { CANVAS_W = 640, CANVAS_H = 480 };

/* Panel strip below the minimap: x >= 0x1de, y in [0x9c, 0x1ae). */
enum { PN_X = 0x1de, PN_Y = 0x9c, PN_X1 = CANVAS_W, PN_Y1 = 0x1ae };
/* End-turn button strip (FUN_0041a734): sprite 0x3b lands at (0x1de,0x1cc). */
enum { ET_X = 0x1de, ET_Y = 0x1cc, ET_X1 = CANVAS_W, ET_Y1 = CANVAS_H };

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

int test_hud_panel_render_owned(void)
{
    char gdir[1024];
    resolve_game_dir(gdir, sizeof gdir);

    char sav_path[1200];
    snprintf(sav_path, sizeof sav_path, "%s/lastturn.sav", gdir);
    if (!file_exists(sav_path))
        T_SKIP("retail lastturn.sav not found at %s", sav_path);

    /* 1. Load the real save → per-territory records + scalars. */
    const char *tmp_castles = "/tmp/openlords2_panel_castles.dat";
    if (save_load(sav_path, tmp_castles) != 0)
        T_FAIL("save_load(%s) failed", sav_path);
    int map_id = (int)s_slot_0053f034;
    int terr   = (int)s_slot_0057c93c;
    int realm  = (int)s_slot_0057c8cc;

    /* 2. Native sprite banks (misc_cty for the panel sprites). */
    if (!engine_pl8_load_strategic_banks(gdir))
        T_FAIL("engine_pl8_load_strategic_banks failed");
    const uint8_t *misc = engine_pl8_misc_cty();
    if (!misc)
        T_FAIL("engine_pl8_misc_cty NULL");

    /* 3. Fonts + localization blob (full paths via the host io shim). */
    static uint8_t font_name[0x251c];   /* fntl2_9.pl8  (DAT_005c9a90) */
    static uint8_t font_res[0x0d48];    /* font_10.pl8  (DAT_005aeba0) */
    static uint8_t font_menu[0x36b0];   /* fntl2_14.pl8 (DAT_005af8f0) */
    static uint8_t locbuf[120000];      /* l2.eng */
    char p[1400];
    snprintf(p, sizeof p, "%s/fntl2_9.pl8", gdir);
    if (io_read_file_at(p, font_name, sizeof font_name, 0) < 1)
        T_SKIP("fntl2_9.pl8 not found at %s", p);
    snprintf(p, sizeof p, "%s/font_10.pl8", gdir);
    if (io_read_file_at(p, font_res, sizeof font_res, 0) < 1)
        T_SKIP("font_10.pl8 not found at %s", p);
    snprintf(p, sizeof p, "%s/fntl2_14.pl8", gdir);
    if (io_read_file_at(p, font_menu, sizeof font_menu, 0) < 1)
        T_SKIP("fntl2_14.pl8 not found at %s", p);
    snprintf(p, sizeof p, "%s/l2.eng", gdir);
    int locn = io_read_file_at(p, locbuf, sizeof locbuf, 0);
    if (locn < 1)
        T_SKIP("l2.eng not found at %s", p);

    /* 4. Render into a 640x480 canvas. */
    static uint8_t canvas[CANVAS_W * CANVAS_H];
    memset(canvas, 0, sizeof canvas);
    gfx_paint_bind_canvas(canvas, CANVAS_W, CANVAS_H);
    gfx_text_set_screen_byte(0);                  /* DAT_004eac50 = strat map */
    gfx_dispatch_set_alt_clip(0, 0, 0);
    gfx_dispatch_set_default_font(font_menu);     /* menu-font kerning gate   */

    localization_state_t loc;
    loc.blob = locbuf;
    loc.walker_offset = 0;

    engine_hud_panel_state_t st;
    st.army_block = g_per_army_768;
    st.realm_block = g_player_strategic_352;
    st.realm_names = NULL;                         /* not-owned branch only */
    st.current_territory = terr;
    st.current_realm = realm;
    st.map_id = map_id;

    /* Structural invariant: the captured frame is the player's own
     * territory, so the OWNED branch must render. */
    int owner = g_per_army_768[terr * 0x300 + 5];
    if (owner != realm)
        T_FAIL("current territory %d owner %d != realm %d (expected owned)",
               terr, owner, realm);

    engine_hud_panel_render(&st, misc, font_name, font_res, font_menu,
                            gfx_glyph_render_map(), gfx_glyph_measure_map(),
                            &loc);

    /* End-turn button strip (FUN_0041a734(1)) — drawn right after the
     * panel in the screen-id-0 arm. */
    engine_hud_panel_render_endturn(misc, font_name, gfx_glyph_render_map(),
                                    gfx_glyph_measure_map(), &loc,
                                    g_player_strategic_352, realm);

    /* 5. Count painted pixels in the panel strip. */
    int painted = 0;
    for (int y = PN_Y; y < PN_Y1; y++)
        for (int x = PN_X; x < PN_X1; x++)
            if (canvas[x + y * CANVAS_W] != 0) painted++;
    if (painted < 100)
        T_FAIL("panel painted only %d px in the right strip", painted);

    /* End-turn strip: sprite 0x3b (opaque sand) + the "End turn" label
     * must paint the bottom parchment band. */
    int et_painted = 0;
    for (int y = ET_Y; y < ET_Y1; y++)
        for (int x = ET_X; x < ET_X1; x++)
            if (canvas[x + y * CANVAS_W] != 0) et_painted++;
    if (et_painted < 100)
        T_FAIL("end-turn strip painted only %d px", et_painted);

    /* 6. Dump the raw canvas for tools/panel_parity.py. */
    char outdir[1200], outraw[1400];
    snprintf(outdir, sizeof outdir, "%s/runs/parity/hud_panel", OPENLORDS2_ROOT);
    mkdir(outdir, 0755);
    snprintf(outraw, sizeof outraw, "%s/port.raw", outdir);
    FILE *o = fopen(outraw, "wb");
    if (o) { fwrite(canvas, 1, sizeof canvas, o); fclose(o); }

    fprintf(stderr,
            "[hud_panel] map_id=%d terr=%d realm=%d owner=%d painted=%d "
            "endturn=%d -> %s\n",
            map_id, terr, realm, owner, painted, et_painted, outraw);
    return 0;
}
