/*
 * OpenLords2 — the strategic map view's retail globals (definitions: engine_mapview_globals.c).
 *
 * The three map passes (engine_map_terrain = FUN_0040526e, engine_overlay_render = FUN_00405602,
 * engine_map_render = FUN_00405487), their driver (engine_map_scene = FUN_004050c0) and the minimap share ONE set
 * of retail globals: the camera, the row walker, the current cell, the error counter, the sprite-blob pointers,
 * the animation phases.  Before, each module kept its own file-static copy, bound through setters; now every
 * module reads and writes these non-static globals, one per retail global, so rhost aliases each at its VA
 * (tools/rhost/rh_hooks_mapview.c) and the linked passes run inside retail on retail's own state.  The setters
 * the standalone binds through (engine_screen_strat.c, main.c's seats, the host tests) write these globals.
 * rhost does not link this file (build.sh never links *_globals.c).
 *
 * Widths are retail's (the asm): the cell-flag / territory latches and the dev toggles are bytes; everything else a
 * dword.  A blob pointer is a retail DWORD (rhost is i686; the 64-bit host tests' wider pointer is never aliased).
 *
 * Globals another module already defines on retail's layout are used through that symbol, not duplicated:
 *   DAT_0052afdc camera x0           = g_gfx_alt_clip_left (gfx_globals.h; the alt clip's left edge IS the map's)
 *   DAT_005c9288 sprite index        = g_gfx_last_glyph_index, the staged descriptor = gfx_globals.h
 *   DAT_00591508 / 591504 cursor save = g_gfx_saved_cursor_x / _y
 *   DAT_005651b4 / 005651b8 col / row start, DAT_0057cb18 view mode, DAT_0053f264 render mode,
 *   DAT_0057c93c current county = save slots (save_schema_globals.h); read through the casts below, written
 *   through the s_slot_ names
 */
#ifndef OPENLORDS2_ENGINE_MAPVIEW_GLOBALS_H
#define OPENLORDS2_ENGINE_MAPVIEW_GLOBALS_H

#include <stdint.h>

#include "gfx_globals.h"
#include "save_schema_globals.h"

/* ---- the camera (set by the view-mode setup; FUN_0042ec91's caller chain) */
#define g_mv_cam_x0   g_gfx_alt_clip_left     /* DAT_0052afdc first cell's x                       */
extern int32_t g_mv_cam_y0;                   /* DAT_00553244 top phantom row's y                  */
#define g_mv_col_start ((int32_t)s_slot_005651b4)  /* DAT_005651b4 first visible column (save slot)  */
#define g_mv_row_start ((int32_t)s_slot_005651b8)  /* DAT_005651b8 first visible row (save slot)     */
extern int32_t g_mv_col_count;                /* DAT_0053e8ac columns per row                      */
extern int32_t g_mv_row_count;                /* DAT_0056d67c rows (the frame walks (n-2)/2 pairs) */
extern int32_t g_mv_x_step;                   /* DAT_00568220 cell width                           */
extern int32_t g_mv_y_step;                   /* DAT_0053f65c row height                           */
extern int32_t g_mv_x_half;                   /* DAT_00567950 half a cell (the shifted rows)       */
extern int32_t g_mv_y_viewport;               /* DAT_0057c97c feature y bias {30, 14, 6} per view  */
#define g_mv_view_mode   ((int32_t)s_slot_0057cb18)  /* DAT_0057cb18 0 near / 1 far / 2 mid (save slot) */
#define g_mv_render_mode ((int32_t)s_slot_0053f264)  /* DAT_0053f264 1 = only flag-0x20 cells (save slot) */

/* ---- the row walker (all three passes) */
extern int32_t g_mv_cur_row;                  /* DAT_0056d594                                      */
extern int32_t g_mv_cur_col;                  /* DAT_0056d598                                      */
extern int32_t g_mv_col_y_accum;              /* DAT_0056957c y walked below the first full row    */
extern int32_t g_mv_cell;                     /* DAT_00567954 the current cell value               */
extern int32_t g_mv_error_count;              /* DAT_0058fea8 bad-descriptor count (reset per tick) */

/* ---- the sprite banks (retail DWORD pointers) */
extern const uint8_t *g_mv_terrain_blob0;     /* DAT_00568208 terrain bank 0 (+ the empty cells)   */
extern const uint8_t *g_mv_terrain_blob1;     /* DAT_0053e914                                      */
extern const uint8_t *g_mv_terrain_blob2;     /* DAT_0057d344                                      */
extern const uint8_t *g_mv_terrain_blob3;     /* DAT_0055409c                                      */
extern const uint8_t *g_mv_terrain_blob4;     /* DAT_0056898c                                      */
extern const uint8_t *g_mv_feature_blob;      /* DAT_00553224 the feature walker's records 0..3    */
extern const uint8_t *g_mv_feature_alt_blob;  /* DAT_0056d8bc … record type 4                      */
extern const uint8_t *g_mv_overlay_blob;      /* DAT_0055ce5c RLE overlay sheet (cities, armies)   */

/* ---- the terrain pass */
extern uint8_t g_mv_cell_county;              /* DAT_0057d384 BYTE descriptor +7 of the drawn cell  */
extern uint8_t g_mv_cell_flags;               /* DAT_0057d374 BYTE descriptor +1 of the drawn cell  */
extern int32_t g_mv_overhang_off;             /* DAT_005aeb6c the overhang strip's source offset   */
extern int32_t g_mv_county_votes[33];         /* DAT_0052f020 cells drawn per county (FUN_004050c0) */

/* ---- the feature / overlay passes */
extern int32_t g_mv_rotation;                 /* DAT_00522f7c orientation offset                   */
extern int32_t g_mv_active_county;            /* DAT_0057c8c4 the city drawer's size cap county    */
extern uint8_t g_mv_dev_cell_value;           /* DAT_005bb4a4 BYTE dev overlay: path-field value   */
extern uint8_t g_mv_dev_desc_value;           /* DAT_005bb4a5 BYTE dev overlay: descriptor +5      */
extern int32_t g_mv_anim_tick;                /* DAT_0053e9a4 0..15 the army flag animation        */
extern int32_t g_mv_rate_tier0;               /* DAT_0058fd08 the army walk-cycle rate gates       */
extern int32_t g_mv_rate_tier1;               /* DAT_0057d3c8                                      */
extern int32_t g_mv_rate_tier2;               /* DAT_0057d3ac                                      */
extern int32_t g_mv_rate_tier3;               /* DAT_00591510                                      */

/* ---- the scene's animation clock (FUN_004050c0) */
extern int32_t g_mv_anim_378;                 /* DAT_0057d378 frame counter, wraps at 0x80          */
extern int32_t g_mv_anim_388;                 /* DAT_0057d388 frame counter, wraps at 0x60          */
extern int32_t g_mv_redraw_phase;             /* DAT_0057d380 0..3                                  */
extern int32_t g_mv_sub_378_1;                /* DAT_0057d3a8 = 378 >> 1                            */
extern int32_t g_mv_sub_378_2;                /* DAT_0057d394 = 378 >> 2                            */
extern int32_t g_mv_sub_378_3;                /* DAT_0057d39c = 378 >> 3                            */
extern int32_t g_mv_overlay_base;             /* DAT_0057d390 = 378 >> 4 (the overlay frame)        */
extern int32_t g_mv_sub_388_1;                /* DAT_0057d3a4 = 388 >> 1                            */
extern int32_t g_mv_sub_388_2;                /* DAT_0057d3a0 = 388 >> 2                            */
extern int32_t g_mv_sub_388_3;                /* DAT_0057d398 = 388 >> 3                            */
extern int32_t g_mv_arrow_base;               /* DAT_0057d38c = 388 >> 4 (the arrow frame)          */
extern uint32_t g_mv_tick_now;                /* DAT_005ceffc the rate gate's last GetTickCount     */
extern uint32_t g_mv_tick_prev;               /* DAT_005ceff8 … the one before                      */

/* ---- the minimap */
extern int32_t g_mv_food_detail;              /* DAT_00553e60 the options' food-band detail toggle  */

/* ---- the county panel (engine_hud_panel: FUN_0040f7d3 / FUN_0040fec1 / the subdrawers / FUN_0041a734) */
extern int32_t g_mv_panel_queue1[4];          /* DAT_0053f690 resource types, left column          */
extern int32_t g_mv_panel_queue2[4];          /* DAT_00553fd0 … right column                       */
extern int32_t g_mv_panel_count1;             /* DAT_00553c60                                      */
extern int32_t g_mv_panel_count2;             /* DAT_005440b4                                      */
extern int32_t g_mv_panel_row1;               /* DAT_00553038                                      */
extern int32_t g_mv_panel_row2;               /* DAT_0056d68c                                      */
extern int32_t g_mv_panel_height1;            /* DAT_0053e970 60 / 45                              */
extern int32_t g_mv_panel_height2;            /* DAT_0053f04c 60 / 45 / 30                         */
extern int32_t g_mv_panel_shown;              /* DAT_004e59b8 the county the panel last drew (999) */
extern int32_t g_mv_strat_ready;              /* DAT_0053f050 the strategic data path ran (1)      */
extern int32_t g_mv_endturn_shown;            /* DAT_004e59a4 the End-turn strip's last key        */
#define g_mv_endturn_key ((int32_t)s_slot_00554004)  /* DAT_00554004 its key (save slot)           */

/* ---- the terrain-info panel (engine_terrain_info: FUN_0041b032 / 0041befe / 0041c208 / 0041c996) */
extern int32_t g_mv_ti_flags;                 /* DAT_005440b0 the compute's terrain flag byte        */
extern int32_t g_mv_ti_type;                  /* DAT_0057cb2c the cell's terrain / feature type      */
extern int32_t g_mv_ti_head;                  /* DAT_005681e0 the county record under the cursor     */
extern int32_t g_mv_ti_owner;                 /* DAT_005681d0 its owner realm                        */
extern int32_t g_mv_ti_derived;               /* DAT_005651bc the compute's derived bit              */
extern int32_t g_mv_ti_row;                   /* DAT_00553d2c the panel's first row                  */
extern int32_t g_mv_ti_off;                   /* DAT_005651c8 the panel's offset (0 / 2)             */
extern int32_t g_mv_ti_county;                /* DAT_0052afbc the county id (0: none)                */
extern int32_t g_mv_ti_52afb4;                /* DAT_0052afb4 (non-zero: the county / army panel too) */

#endif /* OPENLORDS2_ENGINE_MAPVIEW_GLOBALS_H */
