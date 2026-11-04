/*
 * OpenLords2 — the strategic map view's retail globals: the standalone's / host tests' definitions
 * (engine_mapview_globals.h).  rhost does not link this file: tools/rhost/rh_hooks_mapview.c defines each symbol
 * as an ABSOLUTE symbol at its retail VA.  Zero-initialized, as retail's .bss.
 */
#include "engine_mapview_globals.h"

int32_t g_mv_cam_y0; /* DAT_00553244 */
int32_t g_mv_col_count; /* DAT_0053e8ac */
int32_t g_mv_row_count; /* DAT_0056d67c */
int32_t g_mv_x_step; /* DAT_00568220 */
int32_t g_mv_y_step; /* DAT_0053f65c */
int32_t g_mv_x_half; /* DAT_00567950 */
int32_t g_mv_y_viewport; /* DAT_0057c97c */
int32_t g_mv_cur_row; /* DAT_0056d594 */
int32_t g_mv_cur_col; /* DAT_0056d598 */
int32_t g_mv_col_y_accum; /* DAT_0056957c */
int32_t g_mv_cell; /* DAT_00567954 */
int32_t g_mv_error_count; /* DAT_0058fea8 */
const uint8_t *g_mv_terrain_blob0; /* DAT_00568208 */
const uint8_t *g_mv_terrain_blob1; /* DAT_0053e914 */
const uint8_t *g_mv_terrain_blob2; /* DAT_0057d344 */
const uint8_t *g_mv_terrain_blob3; /* DAT_0055409c */
const uint8_t *g_mv_terrain_blob4; /* DAT_0056898c */
const uint8_t *g_mv_feature_blob; /* DAT_00553224 */
const uint8_t *g_mv_feature_alt_blob; /* DAT_0056d8bc */
const uint8_t *g_mv_overlay_blob; /* DAT_0055ce5c */
uint8_t g_mv_cell_county; /* DAT_0057d384 */
uint8_t g_mv_cell_flags; /* DAT_0057d374 */
int32_t g_mv_overhang_off; /* DAT_005aeb6c */
int32_t g_mv_county_votes[33]; /* DAT_0052f020 */
int32_t g_mv_rotation; /* DAT_00522f7c */
int32_t g_mv_active_county; /* DAT_0057c8c4 */
uint8_t g_mv_dev_cell_value; /* DAT_005bb4a4 */
uint8_t g_mv_dev_desc_value; /* DAT_005bb4a5 */
int32_t g_mv_anim_tick; /* DAT_0053e9a4 */
int32_t g_mv_rate_tier0; /* DAT_0058fd08 */
int32_t g_mv_rate_tier1; /* DAT_0057d3c8 */
int32_t g_mv_rate_tier2; /* DAT_0057d3ac */
int32_t g_mv_rate_tier3; /* DAT_00591510 */
int32_t g_mv_anim_378; /* DAT_0057d378 */
int32_t g_mv_anim_388; /* DAT_0057d388 */
int32_t g_mv_redraw_phase; /* DAT_0057d380 */
int32_t g_mv_sub_378_1; /* DAT_0057d3a8 */
int32_t g_mv_sub_378_2; /* DAT_0057d394 */
int32_t g_mv_sub_378_3; /* DAT_0057d39c */
int32_t g_mv_overlay_base; /* DAT_0057d390 */
int32_t g_mv_sub_388_1; /* DAT_0057d3a4 */
int32_t g_mv_sub_388_2; /* DAT_0057d3a0 */
int32_t g_mv_sub_388_3; /* DAT_0057d398 */
int32_t g_mv_arrow_base; /* DAT_0057d38c */
uint32_t g_mv_tick_now; /* DAT_005ceffc */
uint32_t g_mv_tick_prev; /* DAT_005ceff8 */
int32_t g_mv_food_detail; /* DAT_00553e60 */
int32_t g_mv_panel_queue1[4]; /* DAT_0053f690 */
int32_t g_mv_panel_queue2[4]; /* DAT_00553fd0 */
int32_t g_mv_panel_count1; /* DAT_00553c60 */
int32_t g_mv_panel_count2; /* DAT_005440b4 */
int32_t g_mv_panel_row1; /* DAT_00553038 */
int32_t g_mv_panel_row2; /* DAT_0056d68c */
int32_t g_mv_panel_height1; /* DAT_0053e970 */
int32_t g_mv_panel_height2; /* DAT_0053f04c */
int32_t g_mv_panel_shown; /* DAT_004e59b8 */
int32_t g_mv_strat_ready; /* DAT_0053f050 */
int32_t g_mv_endturn_shown; /* DAT_004e59a4 */
int32_t g_mv_ti_flags; /* DAT_005440b0 */
int32_t g_mv_ti_type; /* DAT_0057cb2c */
int32_t g_mv_ti_head; /* DAT_005681e0 */
int32_t g_mv_ti_owner; /* DAT_005681d0 */
int32_t g_mv_ti_derived; /* DAT_005651bc */
int32_t g_mv_ti_row; /* DAT_00553d2c */
int32_t g_mv_ti_off; /* DAT_005651c8 */
int32_t g_mv_ti_county; /* DAT_0052afbc */
int32_t g_mv_ti_52afb4; /* DAT_0052afb4 */
