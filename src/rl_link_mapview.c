/* rl_link_mapview.c — the mapview family of the link layer (docs/findings/rl-link.md): the environment the strategic
 * map view's old renderers draw through (engine_map_scene / _map_terrain / _map_render / _overlay_render / _minimap /
 * _hud_panel / _terrain_info), moved from rhost's tools/rhost/rh_hooks_mapview.c (mv_bind, ti_env), and a
 * retail-layout entry rl_<va> for their retail functions.  rhost's rows call these entries; retail-layout code calls
 * them instead of engine_retail_call(VA).
 *
 * The modules' state IS retail's (engine_mapview_globals.h: aliased at retail's VAs under rhost); the bind below sets
 * once what is not a retail global — the tile grid, the path field's i16 grid (the city size / dev overlay), the
 * palette (0x4d8108) and minimap colour (0x4d28f8) tables, the army label's font + char maps, the sprite drawers'
 * error log (0x4afab9), the scene's clock (GetTickCount), the dirty rectangle (rl_link_dirty, a mirror).
 *
 * WALKER.  rl_link.c's (rl_link_loc_load / _store), shared with the hud panel.  A callback out of an entry into
 * retail-layout code hands over only the walker (rl_link.c §the pattern).
 *
 * MIRRORS.  A drawing entry runs between rl_link_port_enter / _leave; the pure leaves (the terrain blit / strip
 * kernels over an explicit pointer, the 4-byte fill) touch no mirror and skip it.  Three of the family's VAs have
 * their own retail-layout ports (0x42a8d1 / 0x42a9ab rl_glue.c, 0x41c996 rl_render.c) and 0x409397 is the screens
 * family's (rl_link_screens.c): no entry here. */
#include "rl_link.h"
#include <stdint.h>
#include "rl.h"
#include "rl_io_plat.h"        /* rl_plat_GetTickCount */
#include "rl_link_globals.h"
#include "engine_mapview_globals.h"
#include "engine_map_scene.h"
#include "engine_map_terrain.h"
#include "engine_map_render.h"
#include "engine_overlay_render.h"
#include "engine_minimap.h"
#include "engine_territory.h"
#include "engine_hud_panel.h"
#include "engine_terrain_info.h"
#include "gfx_dispatch.h"
#include "gfx_sprite_render.h"
#include "gfx_wrap.h"

/* the partitions' globals (their headers conflict when included together: DUP symbols typed per partition) */
extern int32_t g_rl_55cea0[8385];     /* DAT_0055cea0: the 129 × 65 strategic tile grid (rl_ui2_globals.h) */
extern int16_t g_rl_500c30[0x1000];   /* DAT_00500c30: the local player's path-cost field (rl_host_globals.h) */
extern int32_t g_bf_5530c8, g_bf_4eabec;   /* the mode's misc sheet pointer; the current sheet / scratch */
extern uint8_t g_bf_5af8f0[14000];    /* fntl2_14 */
extern uint8_t g_rl_5b2fa0[34000];    /* fntl2_22: the tall title font */
extern uint8_t g_rl_5bb540[0xdd18];   /* system2.pl8 */
extern uint8_t g_rl_57d3d0[65536];    /* the chrome sheet (panels.pl8) */
extern uint8_t g_rl_4d71d0[0x140];    /* the measure char map; the render map DAT_004d71f0 = +0x20 */

#define FONT_DEFAULT  ((const uint8_t *)g_bf_5af8f0)
#define FONT_TALL     ((const uint8_t *)g_rl_5b2fa0)
#define GLYPH_TABLE   ((const uint8_t *)g_rl_4d71d0 + 0x20)
#define METRICS_TABLE ((const unsigned char *)g_rl_4d71d0)
#define P(v) ((const uint8_t *)(uintptr_t)(uint32_t)(v))

/* ---------------------------------------------------------------- the bind */
/* a callback into retail-layout code: only the walker changes hands */
#define CALLOUT(...) do { rl_link_loc_store(); __VA_ARGS__; rl_link_loc_load(); } while (0)
static void seam_log(uint32_t fmt, int n) { CALLOUT(rl_4afab9((int)fmt, 0, n)); }   /* FUN_004afab9(fmt, 0, n): status.txt */
static uint32_t seam_clock(void) { return (uint32_t)rl_plat_GetTickCount(); }  /* retail's GetTickCount import */

void rl_link_mapview_bind(void)
{
    static int done;
    if (done) return;
    done = 1;
    const int32_t *cells = g_rl_55cea0;
    engine_map_terrain_bind_tile_array(cells, 0x104);
    engine_map_render_bind_tile_array(cells, 0x104);
    engine_overlay_render_bind_tile_array(cells, 0x104);
    engine_overlay_render_bind_per_cell_size_table(g_rl_500c30);
    engine_map_render_bind_palette((const int8_t *)g_rl_4d8108);
    engine_minimap_bind_color_tables(g_rl_4d28f8);
    engine_overlay_render_bind_text_tables(FONT_DEFAULT, GLYPH_TABLE, METRICS_TABLE);
    engine_overlay_render_set_log(seam_log);
    engine_map_scene_bind_clock(seam_clock);
    engine_dirty_rect_state_t *d = engine_territory_dirty_state();   /* rl_link_dirty */
    engine_map_terrain_set_dirty_state(d);
    engine_map_render_set_dirty_state(d);
    engine_overlay_render_set_dirty_state(d);
    engine_map_terrain_bind_empty_gate(&d->below_status_redraw);     /* DAT_0053e9ac */
    gfx_dispatch_set_default_font(FONT_DEFAULT);
    gfx_sprite_render_set_log(seam_log);
    gfx_wrap_set_tall_font_marker(FONT_TALL);
}

/* ---------------------------------------------------------------- the terrain-info panel's environment */
/* the chrome sheet the panel twins bake in, the seal's system sheet, fntl2_22 / fntl2_14, the char maps, the walker,
 * retail's special-feature table (DAT_004d2ec8), the dirty rectangle, and the retail-layout ports of what the old
 * port does not draw itself */
static void seam_ti_county(void)   { CALLOUT(rl_41b19d()); }   /* the county / army panel */
static void seam_ti_roster_a(void) { CALLOUT(rl_41cb3a()); }
static void seam_ti_roster_b(void) { CALLOUT(rl_41d299()); }
static void seam_ti_roster_c(void) { CALLOUT(rl_41da2f()); }
static void seam_ti_alert(int cue) { CALLOUT(rl_426216(cue)); }
static void seam_ti_icons(void)    /* FUN_004af8d9("icon_tmp.pl8" @0x4d4230, DAT_004eabec, 0x27100, 0) */
{ CALLOUT(rl_4af8d9((int)(uintptr_t)g_rl_4d4230, g_bf_4eabec, 0x27100, 0)); }
static void seam_ti_present(void)  { CALLOUT(rl_452160(1)); }  /* the present */
void rl_link_mapview_ti_env(void)
{
    rl_link_mapview_bind();
    engine_terrain_info_env_t env = {
        .chrome = g_rl_57d3d0, .system_sheet = g_rl_5bb540, .title_font = FONT_TALL, .menu_font = FONT_DEFAULT,
        .glyph = GLYPH_TABLE, .metrics = METRICS_TABLE, .loc = rl_link_loc_load(),
        .special_table = (const int32_t *)(const void *)g_rl_4d2ec8, .dirty = engine_territory_dirty_state(),
        .county_panel = seam_ti_county, .roster_a = seam_ti_roster_a, .roster_b = seam_ti_roster_b,
        .roster_c = seam_ti_roster_c, .alert = seam_ti_alert, .load_icons = seam_ti_icons, .present = seam_ti_present,
    };
    engine_terrain_info_bind_env(&env);
}

/* ---------------------------------------------------------------- entries.  Retail's EAX: nobody reads it — 0. */
#define MV(...) do { int live_ = rl_link_port_enter(); rl_link_mapview_bind(); __VA_ARGS__; rl_link_port_leave(live_); } while (0)
#define PANEL(...) do { int live_ = rl_link_port_enter(); rl_link_mapview_bind(); rl_link_panel_env(); __VA_ARGS__; \
                        rl_link_loc_store(); rl_link_port_leave(live_); } while (0)
#define TI(...) do { int live_ = rl_link_port_enter(); rl_link_mapview_ti_env(); __VA_ARGS__; rl_link_loc_store(); rl_link_port_leave(live_); } while (0)

/* engine_map_scene.c */
int rl_4050c0(void) { MV(engine_map_scene_tick_frame()); return 0; }
/* engine_map_terrain.c */
int rl_40526e(void) { MV(engine_map_terrain_frame()); return 0; }
int rl_405ae9(void) { MV(engine_map_terrain_row_aligned()); return 0; }
int rl_405c2f(void) { MV(engine_map_terrain_row_shifted()); return 0; }
int rl_4063c1(int a0) { MV(engine_map_terrain_drawer_a(a0)); return 0; }
int rl_406673(int a0) { MV(engine_map_terrain_drawer_b(a0)); return 0; }
int rl_42a7f1(int a0) { MV(engine_map_terrain_empty_cell(a0)); return 0; }
int rl_452820(int a0, int a1) { engine_map_terrain_blit_452820(P(a0), a1); return 0; }
int rl_45337b(int a0, int a1) { engine_map_terrain_blit_45337b(P(a0), a1); return 0; }
int rl_4538f6(int a0, int a1) { engine_map_terrain_blit_4538f6(P(a0), a1); return 0; }
int rl_460501(int a0, int a1) { engine_map_terrain_blit_460501(P(a0), a1); return 0; }
int rl_46079d(int a0, int a1) { engine_map_terrain_blit_46079d(P(a0), a1); return 0; }
int rl_4608de(int a0, int a1) { engine_map_terrain_blit_4608de(P(a0), a1); return 0; }
int rl_463b42(int a0, int a1) { engine_map_terrain_blit_463b42(P(a0), a1); return 0; }
int rl_463c06(int a0, int a1) { engine_map_terrain_blit_463c06(P(a0), a1); return 0; }
int rl_463c62(int a0, int a1) { engine_map_terrain_blit_463c62(P(a0), a1); return 0; }
int rl_453e71(int a0, int a1) { engine_map_terrain_strip_453e71(P(a0), a1); return 0; }
int rl_455f04(int a0, int a1) { engine_map_terrain_strip_455f04(P(a0), a1); return 0; }
int rl_456fd8(int a0, int a1) { engine_map_terrain_strip_456fd8(P(a0), a1); return 0; }
int rl_4580a9(int a0, int a1, int a2) { engine_map_terrain_strip_4580a9(P(a0), a1, a2); return 0; }
int rl_459258(int a0, int a1, int a2) { engine_map_terrain_strip_459258(P(a0), a1, a2); return 0; }
int rl_460a1c(int a0, int a1) { engine_map_terrain_strip_460a1c(P(a0), a1); return 0; }
int rl_4611ef(int a0, int a1) { engine_map_terrain_strip_4611ef(P(a0), a1); return 0; }
int rl_461623(int a0, int a1) { engine_map_terrain_strip_461623(P(a0), a1); return 0; }
int rl_461a54(int a0, int a1, int a2) { engine_map_terrain_strip_461a54(P(a0), a1, a2); return 0; }
int rl_461efb(int a0, int a1, int a2) { engine_map_terrain_strip_461efb(P(a0), a1, a2); return 0; }
int rl_463cbe(int a0, int a1) { engine_map_terrain_strip_463cbe(P(a0), a1); return 0; }
int rl_463e91(int a0, int a1) { engine_map_terrain_strip_463e91(P(a0), a1); return 0; }
int rl_463fa5(int a0, int a1) { engine_map_terrain_strip_463fa5(P(a0), a1); return 0; }
int rl_4640b6(int a0, int a1, int a2) { engine_map_terrain_strip_4640b6(P(a0), a1, a2); return 0; }
int rl_464209(int a0, int a1, int a2) { engine_map_terrain_strip_464209(P(a0), a1, a2); return 0; }
/* engine_map_render.c */
int rl_405487(void) { MV(engine_map_render_frame()); return 0; }
int rl_405862(void) { MV(engine_map_render_row_phase0()); return 0; }
int rl_4059af(void) { MV(engine_map_render_row_phase1()); return 0; }
int rl_408438(int a0) { MV(engine_map_render_tile(a0)); return 0; }
/* engine_overlay_render.c */
int rl_405602(void) { MV(engine_overlay_render_frame()); return 0; }
int rl_405eb5(void) { MV(engine_overlay_render_row_phase0()); return 0; }
int rl_405fac(void) { MV(engine_overlay_render_row_phase1()); return 0; }
int rl_4071a0(int a0) { MV(engine_overlay_render_tile_army(a0)); return 0; }
int rl_4081a6(int a0) { MV(engine_overlay_render_tile_city(a0)); return 0; }
int rl_408c50(void) { MV(engine_overlay_render_debug_text()); return 0; }
int rl_407f82(int a0, int a1, int a2) { MV(engine_overlay_render_army_label(a0, a1, a2)); return 0; }
int rl_4b4056(int x, int y, int c) { engine_overlay_render_fill4(x, y, c); return 0; }
/* engine_minimap.c — the misc sheet is the pointer DAT_005530c8 */
int rl_410aa9(int x, int y) { MV(engine_minimap_render(P(g_bf_5530c8), x, y)); return 0; }
int rl_410cbd(int county, int x, int y, int mode) { MV(engine_minimap_plot((unsigned)county, x, y, mode)); return 0; }
int rl_451bba(void) { MV(engine_minimap_compute_bands()); return 0; }
/* engine_hud_panel.c (rl_40f7d3 / rl_41a734: rl_link.c) */
int rl_40fec1(int a0) { PANEL(engine_hud_panel_build_queues(a0)); return 0; }
int rl_4100af(void) { PANEL(engine_hud_panel_draw_q1_type1()); return 0; }
int rl_41023a(void) { PANEL(engine_hud_panel_draw_q1_type0()); return 0; }
int rl_4103c5(void) { PANEL(engine_hud_panel_draw_q1_type2()); return 0; }
int rl_410502(void) { PANEL(engine_hud_panel_draw_q2_type4()); return 0; }
int rl_410598(void) { PANEL(engine_hud_panel_draw_q2_type5()); return 0; }
int rl_41062e(void) { PANEL(engine_hud_panel_draw_q2_type6()); return 0; }
int rl_4106c4(void) { PANEL(engine_hud_panel_draw_q2_type7()); return 0; }
int rl_4107d1(void) { PANEL(engine_hud_panel_draw_q2_type3()); return 0; }
/* engine_terrain_info.c (the screen-4 panel) */
int rl_41b032(void) { TI(engine_terrain_info_panel()); return 0; }
int rl_41befe(void) { TI(engine_terrain_info_simple()); return 0; }
int rl_41c208(void) { TI(engine_terrain_info_text()); return 0; }
