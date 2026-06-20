/*
 * OpenLords2 — strategic-map screen-id-0 render arm (FUN_0040f5fd).
 * See engine_screen_strat.h.
 *
 * Factored out of main.c's --strat-load bring-up so the same faithful
 * composition is shared by the bring-up seat and the live screen-dispatch
 * arm 0.  Every leaf here is already byte-exact against retail
 * (validated per-region via tools/panel_parity.py / tools/minimap_parity.py
 * and the in-exe descriptor self-check); this module only re-expresses
 * FUN_0040f5fd's call order over those leaves.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "engine_screen_strat.h"

#include "engine_pl8.h"
#include "engine_map_init.h"
#include "engine_map_scene.h"
#include "engine_map_terrain.h"
#include "engine_map_render.h"
#include "engine_overlay_render.h"
#include "engine_minimap.h"
#include "engine_hud_menubar.h"
#include "engine_hud_panel.h"
#include "engine_dirty_rect.h"
#include "engine_screen_dispatch.h"
#include "engine.h"
#include "gfx.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_glyph_table.h"
#include "gfx_sprite_render.h"
#include "gfx_dispatch.h"
#include "config.h"
#include "localization.h"
#include "save.h"
#include "save_schema_globals.h"

/* Shared dirty-rect scratch for the body binds (the bring-up frame is a
 * single full repaint, so the rect just spans the canvas). */
static engine_dirty_rect_state_t g_strat_dirty;

/* Animation mode.  0 (default) = PINNED: the map body renders at anim frame 0
 * — the deterministic, byte-exact frame the single-shot bring-up seats
 * (--strat-map / --strat-load / --ti-cell) and the capture-parity harness
 * expect.  1 = LIVE: the live in-game loop owns the anim counters (advanced
 * each frame by engine_map_scene_advance_frame_counters at the tick's
 * FUN_004050c0 seam), so cattle/castle/water animate and the new-season
 * terrain shows after a bank reload.  The in-game tick sets this; the seats
 * never do. */
static int g_strat_anim_live;

void engine_screen_strat_set_anim_live(int live) { g_strat_anim_live = live; }

/* FUN_0040a567 ×3 — the three misc_cty parchment blits that back the
 * right-edge HUD panel (sprite 0x36 body, 0x39 / 0x3b strips), drawn
 * before the map body. */
static void strat_draw_panel_sand(void)
{
    const uint8_t *misc = engine_pl8_misc_cty();
    if (!misc)
        return;
    gfx_sprite_render_opaque_from_blob(misc, 0x36, 0x1de, 0x18);
    gfx_sprite_render_opaque_from_blob(misc, 0x39, 0x1de, 0x1ae);
    gfx_sprite_render_opaque_from_blob(misc, 0x3b, 0x1de, 0x1cc);
}

/* FUN_004050c0 — the strategic-map body (terrain pass1 + overlay pass2 +
 * feature walker), composed by engine_map_scene over the natively-built
 * cell/descriptor arrays at the load's view-mode-0 camera. */
static void strat_draw_map_body(uint8_t *canvas, int cw, int ch)
{
    int32_t *cells = engine_map_init_tile_cells();
    const int CAM_X0    = engine_map_init_cam_x0();
    const int CAM_Y0    = engine_map_init_cam_y0();
    const int COL_START = engine_map_init_col_start();
    const int ROW_START = engine_map_init_row_start();
    const int COL_COUNT = engine_map_init_col_count();
    const int ROW_COUNT = engine_map_init_row_count();
    const int X_STEP    = engine_map_init_x_step();
    const int Y_STEP    = engine_map_init_y_step();
    const int X_HALF    = engine_map_init_x_half_step();
    const int Y_VIEWPORT = engine_map_init_y_viewport();
    enum { VIEW = 0, RENDER = 0, ROT = 0 };

    g_strat_dirty.dirty_mark = 0;
    g_strat_dirty.dirty_xmin = cw - 1; g_strat_dirty.dirty_xmax = 0;
    g_strat_dirty.dirty_ymin = ch - 1; g_strat_dirty.dirty_ymax = 0;
    g_strat_dirty.below_status_redraw = 0;
    gfx_dispatch_set_alt_clip(0, 0, 0);

    engine_map_terrain_bind_tile_array(cells, 0x104);
    engine_map_terrain_bind_descriptors(g_overworld_map);
    engine_map_terrain_bind_blobs(engine_pl8_bank(0), engine_pl8_bank(1),
                                  engine_pl8_bank(2), engine_pl8_bank(3),
                                  engine_pl8_bank(4));
    engine_map_terrain_set_blob_lengths((int)engine_pl8_bank_size(0),
                                        (int)engine_pl8_bank_size(1),
                                        (int)engine_pl8_bank_size(2),
                                        (int)engine_pl8_bank_size(3),
                                        (int)engine_pl8_bank_size(4));
    engine_map_terrain_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                   COL_COUNT, ROW_COUNT, X_STEP, Y_STEP, X_HALF);
    engine_map_terrain_set_view_mode(VIEW);
    engine_map_terrain_set_render_mode(RENDER);
    /* Empty-cell redraw gate (DAT_0053e9ac).  "Sky gate" is a misnomer:
     * the empty-cell drawer (FUN_0042a7f1) was named for the map's TOP
     * edge, where off-map empty cells paint the dark sky band — but the
     * same path also fills INTERIOR empty "border" cells, which draw a
     * base-grass sprite (blob 0), not sky.  Those interior cells (low
     * sprite index <=6) draw ONLY when this gate is non-zero — retail's
     * optimisation that skips repainting them on incremental frames (they
     * persist in the back buffer).  Retail sets DAT_0053e9ac = 1 on every
     * FULL redraw (e.g. FUN_004b99c0); the drop-in clears + fully redraws
     * the map each strategic frame, so the gate must be 1.  With 0, those
     * empty cells show the black clear-colour as iso-diamond holes —
     * "missing grass tiles" at scrolls where low-index border cells are on
     * screen (the load scroll exposes them; the strat-terrain1 scroll did
     * not, which is why the earlier hard-coded 0 looked correct). */
    engine_map_terrain_set_empty_redraw_gate(1);
    engine_map_terrain_set_error_count(0);
    engine_map_terrain_reset_stats();

    engine_overlay_render_bind_tile_array(cells, 0x104);
    engine_overlay_render_bind_tile_descriptors(g_overworld_map);
    engine_overlay_render_bind_overlay_blob(engine_pl8_bank(7));
    engine_overlay_render_bind_territory_records(g_per_territory_420);
    engine_overlay_render_bind_army_records(g_per_army_768);
    engine_overlay_render_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                      COL_COUNT, ROW_COUNT, X_STEP, Y_STEP, X_HALF);
    engine_overlay_render_bind_canvas(canvas, cw, ch);
    engine_overlay_render_set_dirty_state(&g_strat_dirty);
    engine_overlay_render_set_view_mode(VIEW);
    engine_overlay_render_set_render_mode(RENDER);
    engine_overlay_render_set_animation_tick(0);
    engine_overlay_render_set_active_territory(0);
    engine_overlay_render_set_dev_flags(0, 0);
    engine_overlay_render_set_error_count(0);
    engine_overlay_render_reset_call_counts();

    engine_map_render_set_dirty_state(&g_strat_dirty);
    engine_map_render_bind_tile_array(cells, 0x104);
    engine_map_render_bind_tile_descriptors(g_overworld_map);
    engine_map_render_bind_territory_records(g_per_territory_420);
    engine_map_render_bind_blobs(engine_pl8_bank(5), engine_pl8_bank(6),
                                 engine_pl8_bank(7));
    engine_map_render_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                  COL_COUNT, ROW_COUNT, X_STEP, Y_STEP, X_HALF);
    engine_map_render_set_view_mode(VIEW);
    engine_map_render_set_render_mode(RENDER);
    engine_map_render_set_rotation_offset(ROT);
    engine_map_render_set_y_viewport_offset(Y_VIEWPORT);
    engine_map_render_set_error_count(0);
    engine_map_render_reset_call_count();

    /* PINNED mode (seats / capture parity): force anim frame 0 each render.
     * LIVE mode (in-game loop): leave the counters alone — the tick's
     * every-frame FUN_004050c0 seam advances them, so the map animates and
     * picks up a season/bank change. */
    if (!g_strat_anim_live)
        engine_map_scene_set_anim_counters(0, 0);
    engine_map_scene_render_body();
}

/* FUN_00419c78 — the top menu bar (File / Options / Help parchment +
 * bevel) plus the data-dependent realm-status tail (flag shields / year /
 * season / gold), read from the natively-loaded save. */
static void strat_draw_menu_bar(void)
{
    const uint8_t *chrome = gfx_load_chrome_sheet();
    const uint8_t *font   = gfx_load_menu_font();
    size_t loc_size = 0;
    const uint8_t *locblob = config_localization_data(&loc_size);
    if (!chrome || !font || !locblob)
        return;

    engine_set_current_screen(0);
    gfx_dispatch_set_alt_clip(0, 0, 0);
    localization_state_t loc;
    loc.blob = (unsigned char *)locblob;
    loc.walker_offset = 0;
    int16_t menu_tbl[24];
    engine_hud_menubar_render_chrome(
        chrome, font, gfx_glyph_render_map(), gfx_glyph_measure_map(),
        &loc, engine_hud_menubar_default_table(menu_tbl));

    engine_hud_menubar_status_t status;
    status.realm_block   = g_player_strategic_352;
    status.current_realm = (int)s_slot_0057c8cc;
    status.hud_gate      = (int)s_slot_0057c8fc;
    status.year          = (int)s_slot_00553edc;
    status.season        = (int)s_slot_0057c934;
    engine_hud_menubar_render_status(&status, engine_pl8_misc_cty(),
                                     font, gfx_glyph_render_map(), &loc);
}

/* FUN_0040f7d3(1) + FUN_0041a734(1) — the right-edge territory-info panel
 * (name / population / approval / tax / ration / resources) and the
 * "End turn" button strip below it. */
static void strat_draw_territory_panel(int map_id)
{
    const uint8_t *font_name = gfx_load_panel_name_font();
    const uint8_t *font_res  = gfx_load_panel_resource_font();
    const uint8_t *font_menu = gfx_load_menu_font();
    size_t loc_size = 0;
    const uint8_t *locblob = config_localization_data(&loc_size);
    if (!font_name || !font_res || !font_menu || !locblob)
        return;

    gfx_dispatch_set_alt_clip(0, 0, 0);
    gfx_dispatch_set_default_font(font_menu);   /* menu-font kerning gate */
    localization_state_t loc;
    loc.blob = (unsigned char *)locblob;
    loc.walker_offset = 0;

    engine_hud_panel_state_t pst;
    pst.army_block        = g_per_army_768;
    pst.realm_block       = g_player_strategic_352;
    pst.realm_names       = NULL;               /* not-owned branch only */
    pst.current_territory = (int)s_slot_0057c93c;
    pst.current_realm     = (int)s_slot_0057c8cc;
    pst.map_id            = map_id;

    engine_hud_panel_render(&pst, engine_pl8_misc_cty(),
                            font_name, font_res, font_menu,
                            gfx_glyph_render_map(), gfx_glyph_measure_map(),
                            &loc);

    engine_hud_panel_render_endturn(engine_pl8_misc_cty(),
                                    font_name, gfx_glyph_render_map(),
                                    gfx_glyph_measure_map(), &loc,
                                    g_player_strategic_352,
                                    (int)s_slot_0057c8cc);
}

void engine_screen_strat_render(int redraw_mode)
{
    (void)redraw_mode;   /* only the full-redraw path is ported */

    /* FUN_0040f5fd's DAT_0053f050 == 1 gate — the strategic data path
     * must have run (banks loaded) before anything is drawn. */
    if (!engine_pl8_data_ready())
        return;

    uint8_t *canvas = gfx_paint_canvas_pixels();
    int cw = gfx_paint_canvas_width();
    int ch = gfx_paint_canvas_height();
    if (!canvas || cw <= 0 || ch <= 0)
        return;

    int map_id = (int)s_slot_0053f034;

    /* FUN_004b1867 — clear the whole back buffer. */
    memset(canvas, 0, (size_t)cw * ch);

    strat_draw_panel_sand();              /* FUN_0040a567 ×3            */
    strat_draw_map_body(canvas, cw, ch);  /* FUN_004050c0               */
    strat_draw_menu_bar();                /* FUN_00419c78               */
    strat_draw_territory_panel(map_id);   /* FUN_0040f7d3 + FUN_0041a734 */
    engine_minimap_render(engine_pl8_misc_cty(), 0x1e0, 0x19); /* FUN_00410aa9 */
}
