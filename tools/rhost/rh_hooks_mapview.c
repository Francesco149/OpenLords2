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

/* ---------------------------------------------------------------- the port's own bindings */
static void seam_log(uint32_t fmt, int n)   /* FUN_004afab9(fmt, 0, n) (unported: status.txt) */
{ rh_port_retail_enter(); ((void (__cdecl *)(uint32_t, int, int))(uintptr_t)0x4afab9u)(fmt, 0, n); rh_port_retail_leave(); }
static uint32_t seam_clock(void) { return ((DWORD (WINAPI *)(void))iat_lookup("GetTickCount"))(); }
static void mv_bind(void)
{
    static int done;
    if (done) return;
    done = 1;
    const int32_t *cells = (const int32_t *)(uintptr_t)0x55cea0u;   /* DAT_0055cea0: 129 × 65 tile grid */
    engine_map_terrain_bind_tile_array(cells, 0x104);
    engine_map_render_bind_tile_array(cells, 0x104);
    engine_overlay_render_bind_tile_array(cells, 0x104);
    engine_overlay_render_bind_per_cell_size_table((const int16_t *)(uintptr_t)0x500c30u);
    engine_map_render_bind_palette((const int8_t *)(uintptr_t)0x4d8108u);
    engine_minimap_bind_color_tables((const uint8_t *)(uintptr_t)0x4d28f8u);
    engine_overlay_render_bind_text_tables((const uint8_t *)(uintptr_t)0x5af8f0u, (const uint8_t *)(uintptr_t)0x4d71f0u,
                                           (const unsigned char *)(uintptr_t)0x4d71d0u);
    engine_overlay_render_set_log(seam_log);
    engine_map_scene_bind_clock(seam_clock);
    engine_dirty_rect_state_t *d = engine_territory_dirty_state();   /* rh_hooks_ai.c's synced s_dirty */
    engine_map_terrain_set_dirty_state(d);
    engine_map_render_set_dirty_state(d);
    engine_overlay_render_set_dirty_state(d);
    engine_map_terrain_bind_empty_gate(&d->below_status_redraw);   /* DAT_0053e9ac */
    /* the gfx layer's own bindings (rh_hooks_gfx.c gfx_bind does the same; whichever row runs first) */
    gfx_dispatch_set_default_font((const uint8_t *)(uintptr_t)0x5af8f0u);   /* &DAT_005af8f0 */
    gfx_sprite_render_set_log(seam_log);
    gfx_wrap_set_tall_font_marker((const uint8_t *)(uintptr_t)0x5b2fa0u);   /* &DAT_005b2fa0 */
}
__attribute__((constructor)) static void mv_bind_register(void) { rh_port_register_bind(mv_bind); }

/* the county panel's environment: retail's sheet pointer / fonts / char maps, the localization walker (the port's
 * localization_state_t over retail's blob &DAT_00591580 and walker DAT_005c9270, loaded / stored around the call
 * and around the screen-2 workforce seams), the synced dirty state */
static localization_state_t s_loc;
static void loc_load(void) { s_loc.blob = (unsigned char *)(uintptr_t)0x591580u; s_loc.walker_offset = (size_t)(G32(0x5c9270u) - 0x591580u); }
static void loc_store(void) { PUT(0x5c9270u, 0x591580u + (uint32_t)s_loc.walker_offset); }
static void seam_wf_rows(int county)   /* FUN_0045161e (ported in engine_workforce_hud.c, not linked here) */
{ loc_store(); rh_port_retail_enter(); ((int (__cdecl *)(int))(uintptr_t)0x45161eu)(county); rh_port_retail_leave(); loc_load(); }
static void seam_wf_redraw(void)       /* 0x4120e0 (unported): the workforce backdrop + rows + seal */
{ loc_store(); rh_port_retail_enter(); ((int (__cdecl *)(void))(uintptr_t)0x4120e0u)(); rh_port_retail_leave(); loc_load(); }
static void panel_env(void)
{
    mv_bind();
    loc_load();
    engine_hud_panel_env_t env = {
        .chrome = (const uint8_t *)(uintptr_t)G32(0x5530c8u),        /* DAT_005530c8 misc sheet */
        .font_name = (const uint8_t *)(uintptr_t)0x5c9a90u,           /* fntl2_9 */
        .font_res = (const uint8_t *)(uintptr_t)0x5aeba0u,            /* font_10 */
        .font_menu = (const uint8_t *)(uintptr_t)0x5af8f0u,           /* fntl2_14 */
        .glyph = (const uint8_t *)(uintptr_t)0x4d71f0u, .metrics = (const unsigned char *)(uintptr_t)0x4d71d0u,
        .loc = &s_loc, .dirty = engine_territory_dirty_state(),
        .workforce_rows = seam_wf_rows, .workforce_redraw = seam_wf_redraw,
    };
    engine_hud_panel_bind_env(&env);
}
#define PANEL(call) do { panel_env(); call; loc_store(); } while (0)

/* the terrain-info panel's environment: the chrome sheet the panel twins bake in (&DAT_0057d3d0), the seal's system
 * sheet (&DAT_005bb540), fntl2_22 / fntl2_14, the char maps, the walker, retail's special-feature table
 * (DAT_004d2ec8), the synced dirty state, and retail for what is unported */
#define RCALL(call) do { loc_store(); rh_port_retail_enter(); call; rh_port_retail_leave(); loc_load(); } while (0)
static void seam_ti_county(void)   { RCALL(((void (__cdecl *)(void))(uintptr_t)0x41b19du)()); }   /* the county / army panel */
static void seam_ti_roster_a(void) { RCALL(((void (__cdecl *)(void))(uintptr_t)0x41cb3au)()); }
static void seam_ti_roster_b(void) { RCALL(((void (__cdecl *)(void))(uintptr_t)0x41d299u)()); }
static void seam_ti_roster_c(void) { RCALL(((void (__cdecl *)(void))(uintptr_t)0x41da2fu)()); }
static void seam_ti_alert(int cue) { RCALL(((int (__cdecl *)(int))(uintptr_t)0x426216u)(cue)); }  /* FUN_00426216 */
static void seam_ti_icons(void)    /* FUN_004af8d9("icon_tmp.pl8" @0x4d4230, DAT_004eabec, 0x27100, 0) */
{ RCALL(((int (__cdecl *)(uint32_t, uint32_t, uint32_t, int))(uintptr_t)0x4af8d9u)(0x4d4230u, G32(0x4eabecu), 0x27100u, 0)); }
static void seam_ti_present(void)  { RCALL(((int (__cdecl *)(int))(uintptr_t)0x452160u)(1)); }    /* the present */
static void ti_env(void)
{
    mv_bind();
    loc_load();
    engine_terrain_info_env_t env = {
        .chrome = (const uint8_t *)(uintptr_t)0x57d3d0u, .system_sheet = (const uint8_t *)(uintptr_t)0x5bb540u,
        .title_font = (const uint8_t *)(uintptr_t)0x5b2fa0u, .menu_font = (const uint8_t *)(uintptr_t)0x5af8f0u,
        .glyph = (const uint8_t *)(uintptr_t)0x4d71f0u, .metrics = (const unsigned char *)(uintptr_t)0x4d71d0u,
        .loc = &s_loc, .special_table = (const int32_t *)(uintptr_t)0x4d2ec8u, .dirty = engine_territory_dirty_state(),
        .county_panel = seam_ti_county, .roster_a = seam_ti_roster_a, .roster_b = seam_ti_roster_b,
        .roster_c = seam_ti_roster_c, .alert = seam_ti_alert, .load_icons = seam_ti_icons, .present = seam_ti_present,
    };
    engine_terrain_info_bind_env(&env);
}
#define TI(call) do { ti_env(); call; loc_store(); } while (0)
#define M(call) do { mv_bind(); call; } while (0)
#define P(k) ((const uint8_t *)(uintptr_t)a[k])

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
HOOK_V(mv_scene_tick,            0, M(engine_map_scene_tick_frame()))
/* engine_map_terrain.c */
HOOK_V(mv_terrain_frame,         0, M(engine_map_terrain_frame()))
HOOK_V(mv_terrain_row_aligned,   0, M(engine_map_terrain_row_aligned()))
HOOK_V(mv_terrain_row_shifted,   0, M(engine_map_terrain_row_shifted()))
HOOK_V(mv_terrain_drawer_a,      1, M(engine_map_terrain_drawer_a(I(0))))
HOOK_V(mv_terrain_drawer_b,      1, M(engine_map_terrain_drawer_b(I(0))))
HOOK_V(mv_terrain_empty,         1, M(engine_map_terrain_empty_cell(I(0))))
HOOK_V(mv_terrain_empty_left,    0, M(engine_map_terrain_empty_left()))
HOOK_V(mv_terrain_empty_right,   0, M(engine_map_terrain_empty_right()))
HOOK_V(mv_blit_452820,           2, engine_map_terrain_blit_452820(P(0), I(1)))
HOOK_V(mv_blit_45337b,           2, engine_map_terrain_blit_45337b(P(0), I(1)))
HOOK_V(mv_blit_4538f6,           2, engine_map_terrain_blit_4538f6(P(0), I(1)))
HOOK_V(mv_blit_460501,           2, engine_map_terrain_blit_460501(P(0), I(1)))
HOOK_V(mv_blit_46079d,           2, engine_map_terrain_blit_46079d(P(0), I(1)))
HOOK_V(mv_blit_4608de,           2, engine_map_terrain_blit_4608de(P(0), I(1)))
HOOK_V(mv_blit_463b42,           2, engine_map_terrain_blit_463b42(P(0), I(1)))
HOOK_V(mv_blit_463c06,           2, engine_map_terrain_blit_463c06(P(0), I(1)))
HOOK_V(mv_blit_463c62,           2, engine_map_terrain_blit_463c62(P(0), I(1)))
HOOK_V(mv_strip_453e71,          2, engine_map_terrain_strip_453e71(P(0), I(1)))
HOOK_V(mv_strip_455f04,          2, engine_map_terrain_strip_455f04(P(0), I(1)))
HOOK_V(mv_strip_456fd8,          2, engine_map_terrain_strip_456fd8(P(0), I(1)))
HOOK_V(mv_strip_4580a9,          3, engine_map_terrain_strip_4580a9(P(0), I(1), I(2)))
HOOK_V(mv_strip_459258,          3, engine_map_terrain_strip_459258(P(0), I(1), I(2)))
HOOK_V(mv_strip_460a1c,          2, engine_map_terrain_strip_460a1c(P(0), I(1)))
HOOK_V(mv_strip_4611ef,          2, engine_map_terrain_strip_4611ef(P(0), I(1)))
HOOK_V(mv_strip_461623,          2, engine_map_terrain_strip_461623(P(0), I(1)))
HOOK_V(mv_strip_461a54,          3, engine_map_terrain_strip_461a54(P(0), I(1), I(2)))
HOOK_V(mv_strip_461efb,          3, engine_map_terrain_strip_461efb(P(0), I(1), I(2)))
HOOK_V(mv_strip_463cbe,          2, engine_map_terrain_strip_463cbe(P(0), I(1)))
HOOK_V(mv_strip_463e91,          2, engine_map_terrain_strip_463e91(P(0), I(1)))
HOOK_V(mv_strip_463fa5,          2, engine_map_terrain_strip_463fa5(P(0), I(1)))
HOOK_V(mv_strip_4640b6,          3, engine_map_terrain_strip_4640b6(P(0), I(1), I(2)))
HOOK_V(mv_strip_464209,          3, engine_map_terrain_strip_464209(P(0), I(1), I(2)))
/* engine_map_render.c */
HOOK_V(mv_feature_frame,         0, M(engine_map_render_frame()))
HOOK_V(mv_feature_row0,          0, M(engine_map_render_row_phase0()))
HOOK_V(mv_feature_row1,          0, M(engine_map_render_row_phase1()))
HOOK_V(mv_feature_tile,          1, M(engine_map_render_tile(I(0))))
/* engine_overlay_render.c */
HOOK_V(mv_overlay_frame,         0, M(engine_overlay_render_frame()))
HOOK_V(mv_overlay_row0,          0, M(engine_overlay_render_row_phase0()))
HOOK_V(mv_overlay_row1,          0, M(engine_overlay_render_row_phase1()))
HOOK_V(mv_overlay_army,          1, M(engine_overlay_render_tile_army(I(0))))
HOOK_V(mv_overlay_city,          1, M(engine_overlay_render_tile_city(I(0))))
HOOK_V(mv_overlay_debug,         0, M(engine_overlay_render_debug_text()))
HOOK_V(mv_army_label,            3, M(engine_overlay_render_army_label(I(0), I(1), I(2))))
HOOK_V(mv_fill4,                 3, engine_overlay_render_fill4(I(0), I(1), I(2)))
/* engine_minimap.c — the misc sheet is the pointer DAT_005530c8 */
HOOK_V(mv_minimap_render,        2, M(engine_minimap_render((const uint8_t *)(uintptr_t)G32(0x5530c8u), I(0), I(1))))
HOOK_V(mv_minimap_plot,          4, M(engine_minimap_plot((unsigned)a[0], I(1), I(2), I(3))))
HOOK_V(mv_minimap_bands,         0, M(engine_minimap_compute_bands()))
/* engine_hud_panel.c */
HOOK_V(mv_panel_draw,            1, PANEL(engine_hud_panel_draw(I(0))))
HOOK_V(mv_panel_queues,          1, PANEL(engine_hud_panel_build_queues(I(0))))
HOOK_V(mv_panel_q1_type1,        0, PANEL(engine_hud_panel_draw_q1_type1()))
HOOK_V(mv_panel_q1_type0,        0, PANEL(engine_hud_panel_draw_q1_type0()))
HOOK_V(mv_panel_q1_type2,        0, PANEL(engine_hud_panel_draw_q1_type2()))
HOOK_V(mv_panel_q2_type4,        0, PANEL(engine_hud_panel_draw_q2_type4()))
HOOK_V(mv_panel_q2_type5,        0, PANEL(engine_hud_panel_draw_q2_type5()))
HOOK_V(mv_panel_q2_type6,        0, PANEL(engine_hud_panel_draw_q2_type6()))
HOOK_V(mv_panel_q2_type7,        0, PANEL(engine_hud_panel_draw_q2_type7()))
HOOK_V(mv_panel_q2_type3,        0, PANEL(engine_hud_panel_draw_q2_type3()))
HOOK_V(mv_panel_endturn,         1, PANEL(engine_hud_panel_endturn(I(0))))
/* engine_terrain_info.c (the screen-4 panel) */
HOOK_V(mv_ti_panel,              0, TI(engine_terrain_info_panel()))
HOOK_V(mv_ti_simple,             0, TI(engine_terrain_info_simple()))
HOOK_V(mv_ti_text,               0, TI(engine_terrain_info_text()))
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
