/* rh_hooks_mapview.c — the strategic map view's screen renderers (src/engine_map_scene / engine_map_terrain /
 * engine_overlay_render / engine_map_render / engine_minimap / engine_hud_panel / engine_terrain_info) vs retail, on identical state AND identical pixels
 * (rh_hooks.h contract; shared plumbing in rh_port.h / rh_hooks_ai.c). Write-up: docs/findings/render-mapview.md.
 *
 * BINDING.  The passes keep retail's globals in src/engine_mapview_globals.c (not linked): aliased below at their
 * retail VAs, as are the minimap's terrain grid (engine_minimap_globals.c; its county grid is rh_hooks_economy.c's).
 * The gfx state they stage / draw through is rh_hooks_gfx.c's aliases (the canvas = retail's back buffer).  The
 * port's own bindings (not retail globals), set once (mv_bind): the tile array DAT_0055cea0, the path field's i16
 * grid DAT_00500c30 (the city size / dev overlay), retail's palette tables (0x4d8108) and minimap colours
 * (0x4d28f8), the default font + char maps of the army label, the army drawer's error log (FUN_004afab9), the
 * scene's clock (retail's GetTickCount import), and the dirty rectangle: the passes mark through the dirty-rect
 * state rh_hooks_ai.c syncs (engine_territory_dirty_state — the owning port symbol of FUN_004527a6's six globals,
 * DAT_0053e9ac the empty-cell redraw gate among them).
 *
 * MEDIA + SAMPLE.  Every row carries RH_SH_MEDIA (its shadow also compares every surface pixel); the hot ones
 * RH_SH_SAMPLE (per row / caller / screen: the first 64 calls and every power-of-two-th). */
#include "rh_port.h"
#include "engine_mapview_globals.h"
#include "engine_map_scene.h"
#include "engine_map_terrain.h"
#include "engine_map_render.h"
#include "engine_overlay_render.h"
#include "engine_minimap.h"
#include "engine_territory.h"
#include "engine_hud_panel.h"
#include "gfx_dispatch.h"
#include "gfx_sprite_render.h"
#include "gfx_wrap.h"
#include "engine_terrain_info.h"
#include "rl_link.h"

/* ---------------------------------------------------------------- binding: mapview globals = retail memory */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(
/* camera */
A(g_mv_cam_y0,0x553244) A(g_mv_col_count,0x53e8ac) A(g_mv_row_count,0x56d67c) A(g_mv_x_step,0x568220)
A(g_mv_y_step,0x53f65c) A(g_mv_x_half,0x567950) A(g_mv_y_viewport,0x57c97c)
/* walker */
A(g_mv_cur_row,0x56d594) A(g_mv_cur_col,0x56d598) A(g_mv_col_y_accum,0x56957c) A(g_mv_cell,0x567954)
A(g_mv_error_count,0x58fea8)
/* banks */
A(g_mv_terrain_blob0,0x568208) A(g_mv_terrain_blob1,0x53e914) A(g_mv_terrain_blob2,0x57d344)
A(g_mv_terrain_blob3,0x55409c) A(g_mv_terrain_blob4,0x56898c) A(g_mv_feature_blob,0x553224)
A(g_mv_feature_alt_blob,0x56d8bc) A(g_mv_overlay_blob,0x55ce5c)
/* terrain */
A(g_mv_cell_county,0x57d384) A(g_mv_cell_flags,0x57d374) A(g_mv_overhang_off,0x5aeb6c) A(g_mv_county_votes,0x52f020)
/* features / overlay */
A(g_mv_rotation,0x522f7c) A(g_mv_active_county,0x57c8c4) A(g_mv_dev_cell_value,0x5bb4a4) A(g_mv_dev_desc_value,0x5bb4a5)
A(g_mv_anim_tick,0x53e9a4) A(g_mv_rate_tier0,0x58fd08) A(g_mv_rate_tier1,0x57d3c8) A(g_mv_rate_tier2,0x57d3ac)
A(g_mv_rate_tier3,0x591510)
/* scene */
A(g_mv_anim_378,0x57d378) A(g_mv_anim_388,0x57d388) A(g_mv_redraw_phase,0x57d380) A(g_mv_sub_378_1,0x57d3a8)
A(g_mv_sub_378_2,0x57d394) A(g_mv_sub_378_3,0x57d39c) A(g_mv_overlay_base,0x57d390) A(g_mv_sub_388_1,0x57d3a4)
A(g_mv_sub_388_2,0x57d3a0) A(g_mv_sub_388_3,0x57d398) A(g_mv_arrow_base,0x57d38c) A(g_mv_tick_now,0x5ceffc)
A(g_mv_tick_prev,0x5ceff8)
/* minimap */
A(g_mv_food_detail,0x553e60) A(g_minimap_type_grid,0x569590)
/* county panel */
A(g_mv_panel_queue1,0x53f690) A(g_mv_panel_queue2,0x553fd0) A(g_mv_panel_count1,0x553c60) A(g_mv_panel_count2,0x5440b4)
A(g_mv_panel_row1,0x553038) A(g_mv_panel_row2,0x56d68c) A(g_mv_panel_height1,0x53e970) A(g_mv_panel_height2,0x53f04c)
A(g_mv_panel_shown,0x4e59b8) A(g_mv_strat_ready,0x53f050) A(g_mv_endturn_shown,0x4e59a4)
/* terrain-info panel */
A(g_mv_ti_flags,0x5440b0) A(g_mv_ti_type,0x57cb2c) A(g_mv_ti_head,0x5681e0) A(g_mv_ti_owner,0x5681d0)
A(g_mv_ti_derived,0x5651bc) A(g_mv_ti_row,0x553d2c) A(g_mv_ti_off,0x5651c8) A(g_mv_ti_county,0x52afbc)
A(g_mv_ti_52afb4,0x52afb4)
);
#undef A

/* (io_read_file_at — the minimap's map file reader engine_minimap_load_grids, never run under rhost — is bridged to
 * retail's loader 0x4af8d9 in rh_hooks_screens.c) */

/* ---------------------------------------------------------------- the environment: src/rl_link_mapview.c */
/* rl_link_mapview_bind (the tile grid, the path field, palette / minimap colour tables, the army label's font, the
 * log, the clock, the dirty rectangle) and rl_link_mapview_ti_env (the terrain-info panel's: its retail callbacks are
 * rl_ ports); every row with a retail-layout entry calls it (rl_<va>: the row verifies the src entry, env included).
 * The four without one: the empty-cell edges 0x42a8d1 / 0x42a9ab and the special stamp 0x41c996 (their own rl_ ports
 * in rl_glue.c / rl_render.c), the parchment 0x409397 (the screens family's rl_409397) — their rows keep the old port. */
__attribute__((constructor)) static void mv_bind_register(void) { rh_port_register_bind(rl_link_mapview_bind); }
#define M(call) do { rl_link_mapview_bind(); call; } while (0)
#define TI(call) do { rl_link_mapview_ti_env(); call; rl_link_loc_store(); } while (0)

#define RH_TABLE RH_HOOKS_MAPVIEW
extern hook_t RH_HOOKS_MAPVIEW[];

/* row indices (must follow RH_HOOKS_MAPVIEW's order) */
enum {
    X_mv_scene_tick,
    X_mv_terrain_frame, X_mv_terrain_row_aligned, X_mv_terrain_row_shifted, X_mv_terrain_drawer_a,
    X_mv_terrain_drawer_b, X_mv_terrain_empty, X_mv_terrain_empty_left, X_mv_terrain_empty_right,
    X_mv_blit_452820, X_mv_blit_45337b, X_mv_blit_4538f6, X_mv_blit_460501, X_mv_blit_46079d, X_mv_blit_4608de,
    X_mv_blit_463b42, X_mv_blit_463c06, X_mv_blit_463c62,
    X_mv_strip_453e71, X_mv_strip_455f04, X_mv_strip_456fd8, X_mv_strip_4580a9, X_mv_strip_459258,
    X_mv_strip_460a1c, X_mv_strip_4611ef, X_mv_strip_461623, X_mv_strip_461a54, X_mv_strip_461efb,
    X_mv_strip_463cbe, X_mv_strip_463e91, X_mv_strip_463fa5, X_mv_strip_4640b6, X_mv_strip_464209,
    X_mv_feature_frame, X_mv_feature_row0, X_mv_feature_row1, X_mv_feature_tile,
    X_mv_overlay_frame, X_mv_overlay_row0, X_mv_overlay_row1, X_mv_overlay_army, X_mv_overlay_city,
    X_mv_overlay_debug, X_mv_army_label, X_mv_fill4,
    X_mv_minimap_render, X_mv_minimap_plot, X_mv_minimap_bands,
    X_mv_panel_draw, X_mv_panel_queues, X_mv_panel_q1_type1, X_mv_panel_q1_type0, X_mv_panel_q1_type2,
    X_mv_panel_q2_type4, X_mv_panel_q2_type5, X_mv_panel_q2_type6, X_mv_panel_q2_type7, X_mv_panel_q2_type3,
    X_mv_panel_endturn,
    X_mv_ti_panel, X_mv_ti_simple, X_mv_ti_text, X_mv_ti_stamp, X_mv_ti_parchment,
};

/* engine_map_scene.c */
HOOK_V(mv_scene_tick,            0, rl_4050c0())
/* engine_map_terrain.c */
HOOK_V(mv_terrain_frame,         0, rl_40526e())
HOOK_V(mv_terrain_row_aligned,   0, rl_405ae9())
HOOK_V(mv_terrain_row_shifted,   0, rl_405c2f())
HOOK_V(mv_terrain_drawer_a,      1, rl_4063c1(I(0)))
HOOK_V(mv_terrain_drawer_b,      1, rl_406673(I(0)))
HOOK_V(mv_terrain_empty,         1, rl_42a7f1(I(0)))
HOOK_V(mv_terrain_empty_left,    0, M(engine_map_terrain_empty_left()))
HOOK_V(mv_terrain_empty_right,   0, M(engine_map_terrain_empty_right()))
HOOK_V(mv_blit_452820,           2, rl_452820(I(0), I(1)))
HOOK_V(mv_blit_45337b,           2, rl_45337b(I(0), I(1)))
HOOK_V(mv_blit_4538f6,           2, rl_4538f6(I(0), I(1)))
HOOK_V(mv_blit_460501,           2, rl_460501(I(0), I(1)))
HOOK_V(mv_blit_46079d,           2, rl_46079d(I(0), I(1)))
HOOK_V(mv_blit_4608de,           2, rl_4608de(I(0), I(1)))
HOOK_V(mv_blit_463b42,           2, rl_463b42(I(0), I(1)))
HOOK_V(mv_blit_463c06,           2, rl_463c06(I(0), I(1)))
HOOK_V(mv_blit_463c62,           2, rl_463c62(I(0), I(1)))
HOOK_V(mv_strip_453e71,          2, rl_453e71(I(0), I(1)))
HOOK_V(mv_strip_455f04,          2, rl_455f04(I(0), I(1)))
HOOK_V(mv_strip_456fd8,          2, rl_456fd8(I(0), I(1)))
HOOK_V(mv_strip_4580a9,          3, rl_4580a9(I(0), I(1), I(2)))
HOOK_V(mv_strip_459258,          3, rl_459258(I(0), I(1), I(2)))
HOOK_V(mv_strip_460a1c,          2, rl_460a1c(I(0), I(1)))
HOOK_V(mv_strip_4611ef,          2, rl_4611ef(I(0), I(1)))
HOOK_V(mv_strip_461623,          2, rl_461623(I(0), I(1)))
HOOK_V(mv_strip_461a54,          3, rl_461a54(I(0), I(1), I(2)))
HOOK_V(mv_strip_461efb,          3, rl_461efb(I(0), I(1), I(2)))
HOOK_V(mv_strip_463cbe,          2, rl_463cbe(I(0), I(1)))
HOOK_V(mv_strip_463e91,          2, rl_463e91(I(0), I(1)))
HOOK_V(mv_strip_463fa5,          2, rl_463fa5(I(0), I(1)))
HOOK_V(mv_strip_4640b6,          3, rl_4640b6(I(0), I(1), I(2)))
HOOK_V(mv_strip_464209,          3, rl_464209(I(0), I(1), I(2)))
/* engine_map_render.c */
HOOK_V(mv_feature_frame,         0, rl_405487())
HOOK_V(mv_feature_row0,          0, rl_405862())
HOOK_V(mv_feature_row1,          0, rl_4059af())
HOOK_V(mv_feature_tile,          1, rl_408438(I(0)))
/* engine_overlay_render.c */
HOOK_V(mv_overlay_frame,         0, rl_405602())
HOOK_V(mv_overlay_row0,          0, rl_405eb5())
HOOK_V(mv_overlay_row1,          0, rl_405fac())
HOOK_V(mv_overlay_army,          1, rl_4071a0(I(0)))
HOOK_V(mv_overlay_city,          1, rl_4081a6(I(0)))
HOOK_V(mv_overlay_debug,         0, rl_408c50())
HOOK_V(mv_army_label,            3, rl_407f82(I(0), I(1), I(2)))
HOOK_V(mv_fill4,                 3, rl_4b4056(I(0), I(1), I(2)))
/* engine_minimap.c — the misc sheet is the pointer DAT_005530c8 */
HOOK_V(mv_minimap_render,        2, rl_410aa9(I(0), I(1)))
HOOK_V(mv_minimap_plot,          4, rl_410cbd(I(0), I(1), I(2), I(3)))
HOOK_V(mv_minimap_bands,         0, rl_451bba())
/* engine_hud_panel.c */
HOOK_V(mv_panel_draw,            1, M(rl_40f7d3(I(0))))
HOOK_V(mv_panel_queues,          1, rl_40fec1(I(0)))
HOOK_V(mv_panel_q1_type1,        0, rl_4100af())
HOOK_V(mv_panel_q1_type0,        0, rl_41023a())
HOOK_V(mv_panel_q1_type2,        0, rl_4103c5())
HOOK_V(mv_panel_q2_type4,        0, rl_410502())
HOOK_V(mv_panel_q2_type5,        0, rl_410598())
HOOK_V(mv_panel_q2_type6,        0, rl_41062e())
HOOK_V(mv_panel_q2_type7,        0, rl_4106c4())
HOOK_V(mv_panel_q2_type3,        0, rl_4107d1())
HOOK_V(mv_panel_endturn,         1, M(rl_41a734(I(0))))
/* engine_terrain_info.c (the screen-4 panel) */
HOOK_V(mv_ti_panel,              0, rl_41b032())
HOOK_V(mv_ti_simple,             0, rl_41befe())
HOOK_V(mv_ti_text,               0, rl_41c208())
HOOK_V(mv_ti_stamp,              0, TI(engine_terrain_info_stamp()))
HOOK_V(mv_ti_parchment,          4, TI(engine_terrain_info_parchment(I(0), I(1), I(2), I(3))))

#define MS (RH_SH_MEDIA | RH_SH_SAMPLE)
hook_t RH_HOOKS_MAPVIEW[] = {
    { "mv_scene_tick", 0x4050c0u, (void *)w_mv_scene_tick, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_frame", 0x40526eu, (void *)w_mv_terrain_frame, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_row_aligned", 0x405ae9u, (void *)w_mv_terrain_row_aligned, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_row_shifted", 0x405c2fu, (void *)w_mv_terrain_row_shifted, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_drawer_a", 0x4063c1u, (void *)w_mv_terrain_drawer_a, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_drawer_b", 0x406673u, (void *)w_mv_terrain_drawer_b, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_empty", 0x42a7f1u, (void *)w_mv_terrain_empty, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_empty_left", 0x42a8d1u, (void *)w_mv_terrain_empty_left, 0, 0, 0, 0, "", 0, MS },
    { "mv_terrain_empty_right", 0x42a9abu, (void *)w_mv_terrain_empty_right, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_452820", 0x452820u, (void *)w_mv_blit_452820, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_45337b", 0x45337bu, (void *)w_mv_blit_45337b, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_4538f6", 0x4538f6u, (void *)w_mv_blit_4538f6, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_460501", 0x460501u, (void *)w_mv_blit_460501, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_46079d", 0x46079du, (void *)w_mv_blit_46079d, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_4608de", 0x4608deu, (void *)w_mv_blit_4608de, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_463b42", 0x463b42u, (void *)w_mv_blit_463b42, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_463c06", 0x463c06u, (void *)w_mv_blit_463c06, 0, 0, 0, 0, "", 0, MS },
    { "mv_blit_463c62", 0x463c62u, (void *)w_mv_blit_463c62, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_453e71", 0x453e71u, (void *)w_mv_strip_453e71, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_455f04", 0x455f04u, (void *)w_mv_strip_455f04, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_456fd8", 0x456fd8u, (void *)w_mv_strip_456fd8, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_4580a9", 0x4580a9u, (void *)w_mv_strip_4580a9, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_459258", 0x459258u, (void *)w_mv_strip_459258, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_460a1c", 0x460a1cu, (void *)w_mv_strip_460a1c, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_4611ef", 0x4611efu, (void *)w_mv_strip_4611ef, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_461623", 0x461623u, (void *)w_mv_strip_461623, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_461a54", 0x461a54u, (void *)w_mv_strip_461a54, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_461efb", 0x461efbu, (void *)w_mv_strip_461efb, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_463cbe", 0x463cbeu, (void *)w_mv_strip_463cbe, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_463e91", 0x463e91u, (void *)w_mv_strip_463e91, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_463fa5", 0x463fa5u, (void *)w_mv_strip_463fa5, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_4640b6", 0x4640b6u, (void *)w_mv_strip_4640b6, 0, 0, 0, 0, "", 0, MS },
    { "mv_strip_464209", 0x464209u, (void *)w_mv_strip_464209, 0, 0, 0, 0, "", 0, MS },
    { "mv_feature_frame", 0x405487u, (void *)w_mv_feature_frame, 0, 0, 0, 0, "", 0, MS },
    { "mv_feature_row0", 0x405862u, (void *)w_mv_feature_row0, 0, 0, 0, 0, "", 0, MS },
    { "mv_feature_row1", 0x4059afu, (void *)w_mv_feature_row1, 0, 0, 0, 0, "", 0, MS },
    { "mv_feature_tile", 0x408438u, (void *)w_mv_feature_tile, 0, 0, 0, 0, "", 0, MS },
    { "mv_overlay_frame", 0x405602u, (void *)w_mv_overlay_frame, 0, 0, 0, 0, "", 0, MS },
    { "mv_overlay_row0", 0x405eb5u, (void *)w_mv_overlay_row0, 0, 0, 0, 0, "", 0, MS },
    { "mv_overlay_row1", 0x405facu, (void *)w_mv_overlay_row1, 0, 0, 0, 0, "", 0, MS },
    { "mv_overlay_army", 0x4071a0u, (void *)w_mv_overlay_army, 0, 0, 0, 0, "", 0, MS },
    { "mv_overlay_city", 0x4081a6u, (void *)w_mv_overlay_city, 0, 0, 0, 0, "", 0, MS },
    { "mv_overlay_debug", 0x408c50u, (void *)w_mv_overlay_debug, 0, 0, 0, 0, "", 0, MS },
    { "mv_army_label", 0x407f82u, (void *)w_mv_army_label, 0, 0, 0, 0, "", 0, MS },
    { "mv_fill4", 0x4b4056u, (void *)w_mv_fill4, 0, 0, 0, 0, "", 0, MS },
    { "mv_minimap_render", 0x410aa9u, (void *)w_mv_minimap_render, 0, 0, 0, 0, "", 0, MS },
    { "mv_minimap_plot", 0x410cbdu, (void *)w_mv_minimap_plot, 0, 0, 0, 0, "", 0, MS },
    { "mv_minimap_bands", 0x451bbau, (void *)w_mv_minimap_bands, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_draw", 0x40f7d3u, (void *)w_mv_panel_draw, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_queues", 0x40fec1u, (void *)w_mv_panel_queues, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q1_type1", 0x4100afu, (void *)w_mv_panel_q1_type1, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q1_type0", 0x41023au, (void *)w_mv_panel_q1_type0, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q1_type2", 0x4103c5u, (void *)w_mv_panel_q1_type2, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q2_type4", 0x410502u, (void *)w_mv_panel_q2_type4, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q2_type5", 0x410598u, (void *)w_mv_panel_q2_type5, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q2_type6", 0x41062eu, (void *)w_mv_panel_q2_type6, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q2_type7", 0x4106c4u, (void *)w_mv_panel_q2_type7, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_q2_type3", 0x4107d1u, (void *)w_mv_panel_q2_type3, 0, 0, 0, 0, "", 0, MS },
    { "mv_panel_endturn", 0x41a734u, (void *)w_mv_panel_endturn, 0, 0, 0, 0, "", 0, MS },
    { "mv_ti_panel", 0x41b032u, (void *)w_mv_ti_panel, 0, 0, 0, 0, "", 0, MS },
    { "mv_ti_simple", 0x41befeu, (void *)w_mv_ti_simple, 0, 0, 0, 0, "", 0, MS },
    { "mv_ti_text", 0x41c208u, (void *)w_mv_ti_text, 0, 0, 0, 0, "", 0, MS },
    { "mv_ti_stamp", 0x41c996u, (void *)w_mv_ti_stamp, 0, 0, 0, 0, "", 0, MS },
    { "mv_ti_parchment", 0x409397u, (void *)w_mv_ti_parchment, 0, 0, 0, 0, "", 0, MS },
};
const int RH_HOOKS_MAPVIEW_N = sizeof RH_HOOKS_MAPVIEW / sizeof *RH_HOOKS_MAPVIEW;
