/* rh_hooks_screens.c — the screen renderers above the gfx layer (src/engine_tooltip.c, engine_workforce_hud.c's
 * backdrop walk, engine_mp_render.c, …) vs retail, on identical state AND identical pixels (rh_hooks.h contract;
 * shared plumbing in rh_port.h / rh_hooks_ai.c; the gfx layer below: rh_hooks_gfx.c). Write-up:
 * docs/findings/render-screens.md.
 *
 * BINDING.  Module globals the ports keep in src/<module>_globals.c (not linked) are aliased below at their retail
 * VAs.  Pure in/out ports (the tooltip's state struct, the classifier's inputs) get a retail-layout adapter: the
 * struct is loaded from retail's globals before the call and stored back (store-if-changed) after it.  The dirty
 * rectangle is engine_territory's bound state (engine_territory_dirty_state(): rh_hooks_ai.c's s_dirty, synced
 * around every row and every seam into retail), so a mark made by the port and one made by retail code it calls
 * land in the same place.  The localized-text walker is the port's localization_state_t over retail's blob
 * &DAT_00591580 + walker pointer DAT_005c9270 (as in rh_hooks_gfx.c), loaded / stored around the call.
 *
 * The gfx bindings the ports rely on (the default / tall font blobs retail compares by address, the sprite
 * renderers' error log) are set here too: a --hooks run of these rows alone never runs rh_hooks_gfx.c's gfx_bind.
 *
 * MEDIA.  Every drawing row carries RH_SH_MEDIA (its shadow also compares every surface pixel). */
#include "rh_port.h"
#include "gfx_globals.h"
#include "gfx_dispatch.h"
#include "gfx_sprite_render.h"
#include "gfx_wrap.h"
#include "localization.h"
#include "engine_dirty_rect.h"
#include "engine_territory.h"
#include "engine_tooltip.h"
#include "engine_workforce_hud.h"
#include "engine_mp_render.h"
#include "engine_noble_faces.h"
#include "engine_title_render.h"
#include "engine_county_panels.h"
#include "engine_battlefield_render.h"
#include "engine_trade.h"
#include "engine_armory.h"
#include "engine_labor.h"
#include "engine_screen_strat.h"
#include "gfx.h"
#include "gfx_panel.h"
#include "text_input.h"
#include "save.h"
#include "save_schema_globals.h"

/* ---------------------------------------------------------------- binding: module globals = retail memory */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(
/* engine_noble_faces_globals.c */
A(g_noble_buttons,0x4dd940) A(g_noble_action_count,0x553ee8)
/* engine_title_render_globals.c */
A(g_tr_selected_button,0x553114) A(g_tr_page,0x5530f0) A(g_tr_scene_flag_a,0x5651ac) A(g_tr_p4_gate_a,0x553030)
A(g_tr_p4_gate_b,0x553248) A(g_tr_p4_continue_disabled,0x57c940) A(g_tr_p4_shield_slots,0x57cb40)
A(g_tr_p4_selected_shield,0x57c8cc) A(g_tr_p4_shield_flash,0x57d354) A(g_tr_p4_flash_timer,0x568984)
A(g_tr_p4_name_buf,0x53f1e0) A(g_tr_options,0x53f288) A(g_tr_misc_sheet,0x5530c8) A(g_tr_country_scroll,0x53e90c)
A(g_tr_country_sel,0x57cacc) A(g_tr_country_count,0x554018) A(g_tr_country_dirty,0x57c948) A(g_tr_tabs_dirty,0x55ce94)
A(g_tr_p9_tab,0x55306c) A(g_tr_p9_count,0x553fb4) A(g_tr_p9_bookmark,0x553e5c) A(g_tr_fl_count,0x4eb25c)
A(g_tr_fl_first,0x4ea1a0) A(g_tr_fl_selected,0x4e65dc) A(g_tr_fl_name,0x4ea130) A(g_tr_fl_show_status,0x57d3c4)
A(g_tr_fl_flag,0x591548)
/* engine_battlefield_render_globals.c */
A(g_bfr_camera_x,0x4e657c) A(g_bfr_camera_y,0x4e6580) A(g_bfr_zoom,0x4e656c) A(g_bfr_desc_base,0x4e6574)
A(g_bfr_desc_stride,0x4e5d50) A(g_bfr_desc_cursor,0x4e6588) A(g_bfr_grid_cols,0x4e5d5c) A(g_bfr_grid_rows,0x4e5d4c)
A(g_bfr_row_wrap,0x4e5d70) A(g_bfr_viewport_x,0x4e6558) A(g_bfr_viewport_y,0x4e6554) A(g_bfr_row_stride,0x4e6578)
A(g_bfr_total_rows,0x4e6570) A(g_bfr_band_width,0x4e5d3c) A(g_bfr_scroll_pad,0x4e5d44) A(g_bfr_zoom_sq,0x4e6550)
A(g_bfr_cell_counter,0x4e5d64) A(g_bfr_blob_primary,0x4e5d78) A(g_bfr_blob_alt,0x4e6560) A(g_bfr_clip_left,0x4e6564)
A(g_bfr_clip_right,0x4e5d54) A(g_bfr_clip_top,0x4e5d48) A(g_bfr_clip_bottom,0x4e5d6c) A(g_bfr_visible,0x4e5d80)
A(g_bfr_visible_count,0x4e5d38) A(g_bfr_sort_passes,0x4e655c) A(g_bfr_marker_frame,0x4e5b18) A(g_bfr_effect_frame,0x4e5b1c)
A(g_bfr_effect_id,0x568994) A(g_bfr_mm_blob_primary,0x4e6584) A(g_bfr_mm_blob_alt,0x4e6568) A(g_bfr_mm_default,0x4e5d40)
A(g_bfr_mm_origin_x,0x4e5d68) A(g_bfr_mm_y_offset,0x4e5d60) A(g_bfr_mm_mode,0x4e5d58) A(g_bfr_mm_scan_pos,0x4e5d74)
A(g_bfr_marker_blob,0x566518) A(g_bfr_marker_player,0x568218) A(g_bfr_marker_advance,0x591510) A(g_bfr_unit_key_blob,0x553250)
A(g_bfr_type7_blob_low,0x553f6c) A(g_bfr_type7_blob_high,0x522f78) A(g_bfr_creature_blob,0x5530c8)
A(g_bfr_late_player,0x57c8cc) A(g_bfr_slot_shift,0x566514) A(g_bfr_slot_shift_sign,0x56651c) A(g_bfr_sel_x,0x553f14)
A(g_bfr_sel_y,0x553f18) A(g_bfr_sel_gate,0x553f84) A(g_bfr_debug_label,0x5bb4a5) A(g_bfr_engine_state,0x52af94)
/* engine_trade_globals.c */
A(g_trade_buttons,0x4dd838) A(g_trade_widget_count,0x553f58)
/* engine_armory_globals.c */
A(g_arm_pct,0x56d65c) A(g_arm_conscripts,0x543fd8) A(g_arm_happy_cost,0x565400) A(g_arm_mode,0x522f58)
A(g_arm_merc,0x55446c) A(g_arm_weapon,0x553f20) A(g_arm_march_active,0x5679d0) A(g_arm_march_x,0x56d630)
A(g_arm_march_y,0x56d62c) A(g_arm_march_target,0x568228) A(g_arm_march_land,0x5681f8) A(g_arm_march_sprite,0x52f008)
A(g_arm_march_walk,0x57cb10) A(g_arm_glint,0x57c8e4) A(g_arm_interior,0x56d5b8) A(g_arm_torch,0x5533c4)
A(g_arm_troop,0x57a0c0) A(g_arm_overlay_buttons,0x4dd340)
/* engine.c is not linked: its multiplayer flag (the setup defaults read it) */
A(g_sim_mp_active,0x553030)
);
#undef A

#define FONT_DEFAULT ((const uint8_t *)(uintptr_t)0x5af8f0u)   /* &DAT_005af8f0: fntl2_14 */
#define FONT_TALL    ((const uint8_t *)(uintptr_t)0x5b2fa0u)   /* &DAT_005b2fa0: the tall (24 px) title font */
#define GLYPH_TABLE  ((const uint8_t *)(uintptr_t)0x4d71f0u)   /* DAT_004d71f0: char → 1-based glyph (render) */
#define METRICS_TABLE ((const unsigned char *)(uintptr_t)0x4d71d0u)   /* DAT_004d71d0: the measurers' table */
#define SYSTEM_SHEET ((const uint8_t *)(uintptr_t)0x5bb540u)   /* &DAT_005bb540: system2.pl8 */
#define CHROME_SHEET ((const uint8_t *)(uintptr_t)0x57d3d0u)   /* &DAT_0057d3d0: the chrome sheet (panels.pl8) */
#define MISC_SHEET   ((const uint8_t *)(uintptr_t)G32(0x5530c8u))   /* DAT_005530c8: the mode's misc_*.pl8 */
#define CUR_BLOB     ((uint8_t *)(uintptr_t)G32(0x4eabecu))     /* DAT_004eabec: the current sprite sheet / scratch */
#define LOC_BLOB     0x591580u                                  /* &DAT_00591580: l2_eng.dat */
#define LOC_WALKER   0x5c9270u                                  /* DAT_005c9270: the walker pointer */

/* the localized-text walker: the port's localization_state_t over retail's blob + walker pointer, loaded / stored
 * around a row (s_loc_live: the rows holding it — a hybrid row nested in a seam reloads and stores it too); a seam into retail code stores it first and reloads it after, so
 * retail's walk (a page renderer, the background loader) and the port's land in the same pointer */
static localization_state_t s_loc;
static int s_loc_live;
static localization_state_t *loc_load(void)
{
    s_loc.blob = (unsigned char *)(uintptr_t)LOC_BLOB; s_loc.walker_offset = (size_t)(G32(LOC_WALKER) - LOC_BLOB);
    s_loc_live++;
    return &s_loc;
}
static void loc_store(void) { PUT(LOC_WALKER, LOC_BLOB + (uint32_t)s_loc.walker_offset); if (s_loc_live) s_loc_live--; }
static void scr_retail_enter(void)
{ if (s_loc_live) PUT(LOC_WALKER, LOC_BLOB + (uint32_t)s_loc.walker_offset); rh_port_retail_enter(); }
static void scr_retail_leave(void)
{ rh_port_retail_leave(); if (s_loc_live) s_loc.walker_offset = (size_t)(G32(LOC_WALKER) - LOC_BLOB); }

/* ---------------------------------------------------------------- bridges across the link cut (io.c / io_path.c) */
/* the ports' file loads → retail's own loader (FUN_004af8d9) and base-directory chdir (FUN_004af502) */
int io_path_chdir_base(void)
{ scr_retail_enter(); int r = ((int (__cdecl *)(void))(uintptr_t)0x4af502u)(); scr_retail_leave(); return r; }
int io_read_file_at(const char *path, void *buf, size_t len, long offset)
{
    scr_retail_enter();
    int r = ((int (__cdecl *)(const char *, void *, uint32_t, int32_t))(uintptr_t)0x4af8d9u)(path, buf, (uint32_t)len, (int32_t)offset);
    scr_retail_leave();
    return r;
}
/* engine_map_scene.c is not linked: FUN_004050c0 (the anim-phase tick) is retail's */
void engine_map_scene_tick(uint32_t now_ms)
{ (void)now_ms; scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)0x4050c0u)(); scr_retail_leave(); }

/* gfx.c (the standalone's DirectDraw shim) is not linked: the backdrop loader is retail's FUN_00408fcb(name, 480) (it
 * reads the .pl8 straight into the back buffer); the canvas is always retail's live back buffer (no lock) */
int gfx_load_background_image(const char *pl8_path)
{
    scr_retail_enter();
    int r = ((int (__cdecl *)(const char *, int))(uintptr_t)0x408fcbu)(pl8_path, 480);
    scr_retail_leave();
    return r;
}
/* the standalone's palette loader = retail's pair: the .256 into the mirror DAT_004ea8a0, FUN_004b0ab5 applies it
 * (reached only by the unhooked armory open render) */
int gfx_load_palette(const char *path)
{
    io_read_file_at(path, (void *)(uintptr_t)0x4ea8a0u, 0x300, 0);
    scr_retail_enter();
    ((int (__cdecl *)(uint32_t))(uintptr_t)0x4b0ab5u)(0x4ea8a0u);
    scr_retail_leave();
    return 1;
}
/* engine_screen_strat.c is not linked: the menu bar FUN_00419c78 (the labor screen's head) is retail's */
void engine_screen_strat_render_menubar(void)
{ scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)0x419c78u)(); scr_retail_leave(); }
int  gfx_lock_canvas(void)   { return 1; }
void gfx_unlock_canvas(void) { }

/* ---------------------------------------------------------------- the port's own bindings */
static uint32_t seam_clock(void) { return ((DWORD (WINAPI *)(void))iat_lookup("timeGetTime"))(); }
static void seam_log(uint32_t fmt, int n)   /* the sprite renderers' error log → retail FUN_004afab9(fmt, 0, n) */
{ scr_retail_enter(); ((void (__cdecl *)(uint32_t, int, int))(uintptr_t)0x4afab9u)(fmt, 0, n); scr_retail_leave(); }

/* the title scene's background loader FUN_0041e61d and the page renderers not on retail's layout yet (retail's) */
static void seam_title_background(void)
{ scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)0x41e61du)(); scr_retail_leave(); }
static void seam_title_page(int page)
{
    static const struct { uint32_t va; int arg; } k[14] = {
        [6] = { 0x41f3e9u, -1 }, [8] = { 0x41f77au, 999 },
        [9] = { 0x41fdd6u, -1 }, [10] = { 0x420428u, -1 }, [11] = { 0x420630u, 999 }, [12] = { 0x42051cu, 999 },
        [13] = { 0x42150bu, -1 },
    };
    if (page < 0 || page > 13 || !k[page].va) return;
    scr_retail_enter();
    if (k[page].arg < 0) ((void (__cdecl *)(void))(uintptr_t)k[page].va)();
    else ((void (__cdecl *)(int))(uintptr_t)k[page].va)(k[page].arg);
    scr_retail_leave();
}
/* the setup page's preview blit FUN_00410c71(mode, x, y) and seat setup FUN_0049caaa (unported here) */
static void seam_title_preview(int mode, int x, int y)
{ scr_retail_enter(); ((void (__cdecl *)(int, int, int))(uintptr_t)0x410c71u)(mode, x, y); scr_retail_leave(); }
static void seam_title_roster_prep(void)
{ scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)0x49caaau)(); scr_retail_leave(); }
/* the roster's seats over retail's strategic records (+5 type, +7 portrait, +8 name colour, +0xa shield) and the
 * seat-enabled bytes DAT_005530e0; the names DAT_00553d54 (0x2c a seat) */
static void seats_load(void)
{
    engine_title_setup_player_t seats[6];
    for (int i = 0; i < 6; i++) {
        const uint8_t *r = g_player_strategic_352 + (size_t)i * 0x160;
        seats[i] = (engine_title_setup_player_t){ r[5], r[7], r[8], r[0xa], G8(0x5530e0u + (uint32_t)i) };
    }
    engine_title_render_set_players(seats, (const uint8_t *)(uintptr_t)0x553d54u);
}
/* the title editor over retail's (buffer DAT_005cd550, cursor DAT_005bb4a8, insert DAT_005c9280): the name box reads
 * the cursor, its char and the mode */
static text_input_state_t s_ed;
static void ed_load(void)
{
    int cur = (int)G32(0x5bb4a8u);
    s_ed.cursor = cur; s_ed.insert_mode = G8(0x5c9280u);
    if (cur >= 0 && cur < TEXT_INPUT_BUF_SIZE) s_ed.buf[cur] = G8(0x5cd550u + (uint32_t)cur);
}

/* the battlefield selection marker's strategic-map mark 0x46c4a3(x, y, w, h) (unported) */
static void seam_world_dirty(int x, int y, int w, int h)
{ scr_retail_enter(); ((void (__cdecl *)(int, int, int, int))(uintptr_t)0x46c4a3u)(x, y, w, h); scr_retail_leave(); }

static void scr_bind(void)
{
    static int done;
    if (done) return;
    done = 1;
    gfx_dispatch_set_default_font(FONT_DEFAULT);
    gfx_wrap_set_tall_font_marker(FONT_TALL);
    gfx_sprite_render_set_log(seam_log);
    engine_mp_render_bind_status_asset(SYSTEM_SHEET);
    engine_mp_render_bind_text(FONT_TALL, GLYPH_TABLE, (unsigned char *)(uintptr_t)LOC_BLOB);
    engine_mp_render_bind_loc(&s_loc);
    engine_mp_render_bind_clock(seam_clock);
    engine_title_render_bind_fonts(FONT_TALL, FONT_DEFAULT);
    engine_title_render_bind_glyph_tables(GLYPH_TABLE, METRICS_TABLE);
    engine_title_render_bind_caret_metrics_table(METRICS_TABLE);
    engine_title_render_bind_loc_state(&s_loc);
    engine_title_render_set_dirty_state(engine_territory_dirty_state());
    engine_title_render_bind_panel_fn(gfx_panel_draw);
    engine_title_render_bind_panel_asset(NULL);                 /* → DAT_004eabec at call time */
    engine_title_render_bind_system_asset(SYSTEM_SHEET);
    engine_title_render_bind_background_fn(seam_title_background);
    engine_title_render_bind_retail_pages(seam_title_page);
    engine_title_render_set_choose_title_editor(&s_ed);
    engine_title_render_bind_chrome_asset(CHROME_SHEET);
    engine_title_render_bind_palette((const uint8_t *)(uintptr_t)0x4ea1b0u);   /* DAT_004ea1b0: the darken's palette */
    engine_title_render_bind_country_values((const uint8_t *)(uintptr_t)0x50a460u);
    engine_title_render_bind_filelist_entries((const uint8_t *)(uintptr_t)0x4e8790u);
    engine_title_render_bind_preview_fn(seam_title_preview);
    engine_title_render_bind_roster_prep(seam_title_roster_prep);
    /* the battlefield renderer's fixed retail tables + seams (its state is aliased) */
    engine_battlefield_render_bind_entity_table((uint8_t *)(uintptr_t)0x554480u, 0x1b0, 0x50);
    engine_battlefield_render_bind_anim_offset_tables((const uint8_t *)(uintptr_t)0x4e3bf0u, (const uint8_t *)(uintptr_t)0x4e4030u);
    engine_battlefield_render_bind_player_marker_slot_table((const uint8_t *)(uintptr_t)0x52f0b2u, 0x1a4);
    engine_battlefield_render_bind_effect_chain_table((const uint8_t *)(uintptr_t)0x57a100u, 0x4c, 0x7fff);
    engine_battlefield_render_bind_effect_chain_anim_table((const uint8_t *)(uintptr_t)0x4e4470u);
    engine_battlefield_render_bind_late_visibility_table((const uint8_t *)(uintptr_t)0x57bfeeu, 0x160);
    engine_battlefield_render_bind_minimap_entity_owner_table((const uint8_t *)(uintptr_t)0x5544acu, 0x1b0);
    engine_battlefield_render_bind_minimap_player_slot_table((const uint8_t *)(uintptr_t)0x57bf0au, 0x160);
    engine_battlefield_render_bind_debug_label_font(FONT_DEFAULT, GLYPH_TABLE);
    engine_battlefield_render_bind_debug_label_suffix((const char *)(uintptr_t)0x4e4520u);
    engine_battlefield_render_bind_log(seam_log);
    engine_battlefield_render_bind_effect_camera((const int32_t *)(uintptr_t)0x55ce70u, (const int32_t *)(uintptr_t)0x5678a0u);
    engine_battlefield_render_set_dirty_state(engine_territory_dirty_state());
    engine_battlefield_render_bind_force_redraw(&engine_territory_dirty_state()->below_status_redraw);   /* DAT_0053e9ac */
    engine_battlefield_render_bind_world_map_dirty_hook(seam_world_dirty);
    engine_armory_bind_retail_buffers((uint8_t *)(uintptr_t)0x542ce0u);   /* &DAT_00542ce0: the click grid */
    engine_armory_set_dirty_state(engine_territory_dirty_state());
}
#define B(call) do { scr_bind(); call; } while (0)
#define P(k) ((const uint8_t *)(uintptr_t)a[k])
#define C(k) ((uint8_t)a[k])
#define LOC(call) do { scr_bind(); loc_load(); call; loc_store(); } while (0)

/* ---------------------------------------------------------------- engine_tooltip: the state struct over retail's */
/* DAT_004eac00 tip, DAT_004eb268 / DAT_004eb270 box position, DAT_004ea830 idle stamp, DAT_004e6980 the 192×40
 * backdrop; the context: DAT_0053f250 "Tool tips", DAT_004eac50 screen (byte), DAT_004ea4b0 mouse activity; the
 * classifier's inputs: the cursor DAT_004e65fc / DAT_004e65f4, the panel sub-mode DAT_0057a0c4, the current county
 * DAT_0057c93c's owner byte (the 0x300 county records at DAT_0053f9b0, +5), the current realm DAT_0057c8cc, the
 * labor rows (DAT_0053e970 height, DAT_00553c60 count, DAT_0053f690 kinds) and production rows (DAT_0053f04c,
 * DAT_005440b4, DAT_00553fd0). */
#define TT_TIP 0x4eac00u
#define TT_X 0x4eb268u
#define TT_Y 0x4eb270u
#define TT_STAMP 0x4ea830u
#define TT_BACKDROP 0x4e6980u
static engine_tooltip_state_t s_tt;
static engine_tooltip_classify_in_t s_tcin;
static engine_tooltip_ctx_t s_tctx;
static engine_tooltip_state_t *tt_load(void)
{
    scr_bind();
    s_tt.tip = (int)G32(TT_TIP); s_tt.x = (int)G32(TT_X); s_tt.y = (int)G32(TT_Y); s_tt.idle_stamp_ms = G32(TT_STAMP);
    memcpy(s_tt.backdrop, (const void *)(uintptr_t)TT_BACKDROP, sizeof s_tt.backdrop);
    s_tcin.cursor_x = (int)G32(0x4e65fcu); s_tcin.cursor_y = (int)G32(0x4e65f4u);
    s_tcin.panel_mode = (int)G32(0x57a0c4u);
    uint32_t county = G32(0x57c93cu);
    s_tcin.county_owner = county < 0x100u ? G8(0x53f9b5u + 0x300u * county) : -1;   /* byte_53F9B5[768 * county] */
    s_tcin.current_realm = (int)G32(0x57c8ccu);
    s_tcin.labor_row_h = (int)G32(0x53e970u); s_tcin.labor_count = (int)G32(0x553c60u);
    s_tcin.labor_kinds = (const int32_t *)(uintptr_t)0x53f690u;
    s_tcin.prod_row_h = (int)G32(0x53f04cu); s_tcin.prod_count = (int)G32(0x5440b4u);
    s_tcin.prod_kinds = (const int32_t *)(uintptr_t)0x553fd0u;
    s_tctx.enabled = (int)G32(0x53f250u); s_tctx.screen = G8(L2_SCREEN); s_tctx.mouse_event = (int)G32(0x4ea4b0u);
    s_tctx.canvas_repainted = 0; s_tctx.now_ms = 0; s_tctx.clock = seam_clock; s_tctx.cin = &s_tcin;
    s_tctx.loc = loc_load(); s_tctx.menu_font = FONT_DEFAULT; s_tctx.glyph_table = GLYPH_TABLE;
    s_tctx.glyph_metrics = METRICS_TABLE; s_tctx.dirty = engine_territory_dirty_state();
    return &s_tt;
}
static void tt_store(void)
{
    PUT(TT_TIP, s_tt.tip); PUT(TT_X, s_tt.x); PUT(TT_Y, s_tt.y); PUT(TT_STAMP, s_tt.idle_stamp_ms);
    if (memcmp((const void *)(uintptr_t)TT_BACKDROP, s_tt.backdrop, sizeof s_tt.backdrop))
        memcpy((void *)(uintptr_t)TT_BACKDROP, s_tt.backdrop, sizeof s_tt.backdrop);
    loc_store();
}
#define TT(call) do { engine_tooltip_state_t *st = tt_load(); call; tt_store(); } while (0)
static int tt_classify(void)  { int r; TT((void)st; r = engine_tooltip_classify(&s_tctx)); return r; }
static int tt_strategic(void) { tt_load(); loc_store(); return engine_tooltip_classify_strategic(&s_tcin); }
static int tt_battle(void)    { tt_load(); loc_store(); return engine_tooltip_classify_battle(&s_tcin); }

/* ---------------------------------------------------------------- engine_noble_faces: the context over retail's */
static engine_noble_faces_ctx_t s_nf;
static const engine_noble_faces_ctx_t *nf_ctx(void)
{
    scr_bind();
    s_nf.strategic_block = g_player_strategic_352;               /* DAT_0057bf00 */
    s_nf.realm_names = (const uint8_t *)(uintptr_t)0x553d54u;    /* DAT_00553d50 + 4, stride 0x2c */
    s_nf.local_player = (int)s_slot_0057c8cc;
    s_nf.chrome = CHROME_SHEET; s_nf.misc_sheet = MISC_SHEET; s_nf.system_sheet = SYSTEM_SHEET;
    s_nf.title_font = FONT_TALL; s_nf.menu_font = FONT_DEFAULT;
    s_nf.glyph_table = GLYPH_TABLE; s_nf.glyph_metrics = METRICS_TABLE;
    s_nf.loc = loc_load(); s_nf.faces_scratch = CUR_BLOB; s_nf.dirty = engine_territory_dirty_state();
    return &s_nf;
}
#define NF(call) do { const engine_noble_faces_ctx_t *nf = nf_ctx(); call; loc_store(); } while (0)

/* engine_title_render: the module's state is aliased; the editor and the walker are loaded around the call */
#define TR(call) do { scr_bind(); ed_load(); seats_load(); loc_load(); call; loc_store(); } while (0)

/* ---------------------------------------------------------------- engine_county_panels: the context over retail's */
static int seam_map_scene(void)   /* FUN_004050c0: the map recomposite the panels open with — it may re-pick DAT_0057c93c */
{ scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)0x4050c0u)(); scr_retail_leave(); return (int)s_slot_0057c93c; }
static engine_county_panels_ctx_t s_cp;
static engine_county_panels_ctx_t *cp_ctx(void)
{
    scr_bind();
    s_cp.county_block = g_per_army_768; s_cp.realm_block = g_player_strategic_352; s_cp.history = g_realm_history;
    s_cp.hist_head = (int)s_slot_00568da8; s_cp.hist_count = (int)s_slot_00553f30;
    s_cp.hist_start_year = (int)s_slot_00553228; s_cp.year = (int)s_slot_00553edc;
    s_cp.county = (int)s_slot_0057c93c; s_cp.map_id = (int)s_slot_0053f034; s_cp.advanced_mode = (int)s_slot_0053f260;
    s_cp.chrome = CHROME_SHEET; s_cp.misc_sheet = MISC_SHEET; s_cp.graphs = CUR_BLOB; s_cp.system_sheet = SYSTEM_SHEET;
    s_cp.title_font = FONT_TALL; s_cp.menu_font = FONT_DEFAULT;
    s_cp.glyph_table = GLYPH_TABLE; s_cp.glyph_metrics = METRICS_TABLE;
    s_cp.loc = loc_load(); s_cp.map_scene = seam_map_scene; s_cp.dirty = engine_territory_dirty_state();
    s_cp.graphs_load = 1;
    return &s_cp;
}
#define CP(call) do { engine_county_panels_ctx_t *cp = cp_ctx(); call; loc_store(); } while (0)
/* FUN_00438ceb's flips (unported here: the panel openers) by screen code */
static void seam_county_flip(int screen)
{
    uint32_t va = screen == 0x14 ? 0x43a8f2u : screen == 0x15 ? 0x43a939u : screen == 0x16 ? 0x43a922u : 0x43a846u;
    scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)va)(); scr_retail_leave();
}
static void seam_county_mp_ration(void)   /* the multiplayer broadcast 0x43eda0(0x31, 0) */
{ scr_retail_enter(); ((void (__cdecl *)(int, int))(uintptr_t)0x43eda0u)(0x31, 0); scr_retail_leave(); }
/* the inputs over retail's: the press / repeat / held / released bytes DAT_004eafb4 / DAT_004eabc5 / DAT_004e65cc /
 * DAT_004e65d8, the mouse activity DAT_004ea4b0, the cursor, the spectator flag DAT_0053f684 */
static int county_grid(void)
{
    const uint8_t *rec = g_per_army_768 + (size_t)s_slot_0057c93c * 0x300;
    engine_county_grid_in_t in = { G8(0x4eafb4u), (int)G32(0x4e65fcu), (int)G32(0x4e65f4u), rec[5],
                                   (int)s_slot_0057c8cc, (int)G32(0x53f684u) };
    return engine_county_action_grid_click(&in, seam_county_flip);
}
static int county_rations_in(engine_county_panels_ctx_t *cp)
{
    const uint8_t *rec = g_per_army_768 + (size_t)s_slot_0057c93c * 0x300;
    engine_county_rations_in_t in = { G8(0x4e65d8u), G8(0x4eafb4u), G8(0x4eabc5u), G8(0x4e65ccu), (int)G32(0x4ea4b0u),
                                      (int)G32(0x4e65fcu), (int)G32(0x4e65f4u), rec[5], (int)s_slot_0057c8cc,
                                      (int8_t)rec[0x15f] };
    return engine_county_rations_click(cp, &in, (int32_t *)(uintptr_t)0x591550u, (int32_t *)(uintptr_t)0x59154cu,
                                       (int)G32(0x553030u), seam_county_mp_ration);
}

/* ---------------------------------------------------------------- engine_trade: the context over retail's */
static engine_trade_ctx_t s_tr;
static engine_trade_ctx_t *tr_ctx(void)
{
    scr_bind();
    s_tr.county_block = g_per_army_768; s_tr.strategic_block = g_player_strategic_352;
    s_tr.territory_block = g_per_territory_420; s_tr.merchant_offers = s_slot_005532a0;
    s_tr.county = (int)s_slot_0057c93c; s_tr.trade_county = (int)s_slot_00553c64;
    s_tr.local_player = (int)s_slot_0057c8cc; s_tr.good = (int)s_slot_0056d8ac;
    s_tr.chrome = CHROME_SHEET; s_tr.system_sheet = SYSTEM_SHEET; s_tr.title_font = FONT_TALL; s_tr.menu_font = FONT_DEFAULT;
    s_tr.glyph_table = GLYPH_TABLE; s_tr.glyph_metrics = METRICS_TABLE; s_tr.loc = loc_load();
    s_tr.scratch = CUR_BLOB; s_tr.misc_sheet = MISC_SHEET; s_tr.dirty = engine_territory_dirty_state();
    return &s_tr;
}
#define TRD(call) do { engine_trade_ctx_t *tr = tr_ctx(); call; loc_store(); } while (0)

/* ---------------------------------------------------------------- engine_armory: the context over retail's */
/* the open render's retail-only steps (unported here) */
static void seam_arm_palette_prep(void)
{ scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)0x4b11ceu)(); scr_retail_leave(); }
static void seam_arm_teardown(void)
{ scr_retail_enter(); ((void (__cdecl *)(void))(uintptr_t)0x4996c8u)(); scr_retail_leave(); }
static void seam_arm_input_tick(int arg)
{ scr_retail_enter(); ((void (__cdecl *)(int))(uintptr_t)0x452160u)(arg); scr_retail_leave(); }
static void seam_arm_palette_apply(const uint8_t *pal)
{ scr_retail_enter(); ((void (__cdecl *)(const uint8_t *))(uintptr_t)0x4b0ab5u)(pal); scr_retail_leave(); }
static const engine_armory_retail_t k_arm_retail = {
    seam_arm_palette_prep, seam_arm_teardown, seam_arm_input_tick, seam_arm_palette_apply,
    (uint8_t *)(uintptr_t)0x4ea8a0u, (const char *)(uintptr_t)0x4d2c88u,
};
static engine_armory_ctx_t s_ar;
static engine_armory_ctx_t *ar_ctx(void)
{
    scr_bind();
    s_ar.strategic_block = g_player_strategic_352; s_ar.troop_pool = (const uint8_t *)(uintptr_t)0x53f6a0u;
    s_ar.local_player = (int)s_slot_0057c8cc; s_ar.county = (int)s_slot_0057c93c;
    s_ar.county_rec = g_per_army_768 + (size_t)s_slot_0057c93c * 0x300; s_ar.map_id = (int)s_slot_0053f034;
    s_ar.unit_table = g_unit_table_20; s_ar.system_sheet = SYSTEM_SHEET; s_ar.chrome = CHROME_SHEET;
    s_ar.menu_font = FONT_DEFAULT; s_ar.title_font = FONT_TALL; s_ar.glyph_table = GLYPH_TABLE;
    s_ar.glyph_metrics = METRICS_TABLE; s_ar.loc = loc_load();
    s_ar.open_arg = 0; s_ar.retail = &k_arm_retail;
    return &s_ar;
}
#define AR(call) do { engine_armory_ctx_t *ar = ar_ctx(); call; loc_store(); } while (0)
/* FUN_004aa90a(county, population): the pool row is the county's OWNER's */
static void arm_muster(int county, int population)
{
    int owner = g_per_army_768[(size_t)county * 0x300 + 5];
    engine_armory_muster_init(owner, population, g_player_strategic_352 + (size_t)owner * 0x160);
}
/* FUN_00435cef: the cursor DAT_004e65fc / DAT_004e65f4, the press / repeat / held bytes DAT_004eafb4 / DAT_004eabc5 /
 * DAT_004e65cc */
static int arm_slider(void)
{
    return engine_armory_slider_click(g_per_army_768 + (size_t)s_slot_0057c93c * 0x300, (int)G32(0x4e65fcu),
                                      (int)G32(0x4e65f4u), G8(0x4eafb4u), G8(0x4eabc5u), G8(0x4e65ccu));
}

/* ---------------------------------------------------------------- engine_labor: the context over retail's */
static engine_labor_ctx_t s_lb;
static engine_labor_ctx_t *lb_ctx(void)
{
    scr_bind();
    s_lb.county_block = g_per_army_768; s_lb.county = (int)s_slot_0057c93c;
    s_lb.advanced_mode = (int)s_slot_0053f25c; s_lb.season = (int)s_slot_0057c92c;
    s_lb.chrome = CHROME_SHEET; s_lb.misc_sheet = MISC_SHEET; s_lb.system_sheet = SYSTEM_SHEET;
    s_lb.title_font = FONT_TALL; s_lb.menu_font = FONT_DEFAULT; s_lb.glyph_table = GLYPH_TABLE;
    s_lb.glyph_metrics = METRICS_TABLE; s_lb.loc = loc_load();
    return &s_lb;
}
#define LB(call) do { engine_labor_ctx_t *lb = lb_ctx(); call; loc_store(); } while (0)

#define RH_TABLE RH_HOOKS_SCREENS
extern hook_t RH_HOOKS_SCREENS[];

/* row indices (must follow RH_HOOKS_SCREENS's order) */
enum {
    X_scr_tooltip_tick, X_scr_tooltip_arm, X_scr_tooltip_clear, X_scr_tooltip_clamp, X_scr_tooltip_classify,
    X_scr_tooltip_strategic, X_scr_tooltip_battle, X_scr_backdrop_capture, X_scr_backdrop_restore,
    X_scr_mp_indicator, X_scr_mp_status,
    X_scr_noble_render, X_scr_noble_face,
    X_scr_title_scene, X_scr_title_main, X_scr_title_buttons, X_scr_title_options, X_scr_title_options_buttons,
    X_scr_title_choose, X_scr_title_choose_buttons, X_scr_title_shields, X_scr_title_name_box, X_scr_title_play_now,
    X_scr_title_play_now_buttons, X_scr_title_defaults, X_scr_title_clamp_players,
    X_scr_county_tax, X_scr_county_rations, X_scr_county_slider, X_scr_county_delta, X_scr_county_year,
    X_scr_county_units, X_scr_county_chrome_panel, X_scr_county_tax_up, X_scr_county_tax_down, X_scr_county_wanted_up,
    X_scr_county_wanted_down,
    X_scr_bf_frame, X_scr_bf_terrain16, X_scr_bf_terrain32, X_scr_bf_overlay16, X_scr_bf_overlay32, X_scr_bf_middle,
    X_scr_bf_build_list, X_scr_bf_sort_list, X_scr_bf_render_list, X_scr_bf_entity, X_scr_bf_type6, X_scr_bf_type7,
    X_scr_bf_type9e, X_scr_bf_marker, X_scr_bf_unit_key, X_scr_bf_effect, X_scr_bf_late, X_scr_bf_creature,
    X_scr_bf_selection, X_scr_bf_final, X_scr_bf_minimap, X_scr_bf_scroll, X_scr_bf_bind_minimap,
    X_scr_bf_in_grid, X_scr_bf_px_col, X_scr_bf_px_row, X_scr_bf_col_right, X_scr_bf_col_left, X_scr_bf_row_bottom,
    X_scr_bf_row_top, X_scr_bf_col_delta, X_scr_bf_row_delta, X_scr_bf_zoom,
    X_scr_trade_render, X_scr_trade_content,
    X_scr_arm_counts, X_scr_arm_racks, X_scr_arm_refresh, X_scr_arm_march_restore, X_scr_arm_march_step,
    X_scr_arm_muster, X_scr_arm_unit_noun, X_scr_arm_overlay, X_scr_arm_conscript, X_scr_arm_slider,
    X_scr_arm_render,
    X_scr_title_setup, X_scr_title_tabs, X_scr_title_country, X_scr_title_roster,
    X_scr_county_grid, X_scr_county_rations_in,
    X_scr_county_population, X_scr_county_happiness, X_scr_county_graph,
    X_scr_labor_castle,
    X_scr_title_filelist, X_scr_title_filelist_content,
};

/* engine_tooltip.c */
HOOK_V(scr_tooltip_tick,        0, TT(engine_tooltip_tick(st, &s_tctx)))
HOOK_V(scr_tooltip_arm,         0, TT(engine_tooltip_arm(st, &s_tctx)))
HOOK_V(scr_tooltip_clear,       0, TT(engine_tooltip_clear(st, &s_tctx)))
HOOK_V(scr_tooltip_clamp,       0, TT(engine_tooltip_clamp(st)))
HOOK_R(scr_tooltip_classify,    0, tt_classify())
HOOK_R(scr_tooltip_strategic,   0, tt_strategic())
HOOK_R(scr_tooltip_battle,      0, tt_battle())
/* engine_workforce_hud.c: the backdrop walk (dest, residual stride) */
HOOK_V(scr_backdrop_capture,    2, engine_workforce_hud_capture_backdrop((uint32_t *)(uintptr_t)a[0], I(1)))
HOOK_V(scr_backdrop_restore,    2, engine_workforce_hud_restore_backdrop((const uint32_t *)(uintptr_t)a[0], I(1)))
/* engine_mp_render.c — FUN_0041f15a: MP active DAT_00553030, the flash timer DAT_00568984, the slot DAT_0053e8b4 */
HOOK_V(scr_mp_indicator,        0, B(engine_mp_render_player_indicator((int)G32(0x553030u), (int *)(uintptr_t)0x568984u, 0,
                                                                     (int)G32(0x53e8b4u))))
HOOK_V(scr_mp_status,           0, LOC(engine_mp_render_status_line()))
/* engine_noble_faces.c */
HOOK_V(scr_noble_render,        0, NF(engine_noble_faces_render(nf)))
HOOK_V(scr_noble_face,          2, NF(engine_noble_faces_draw_face(nf, I(0), I(1))))
/* engine_title_render.c — FUN_0041e7e1 reads the screen byte DAT_004eac50 and the page DAT_005530f0 itself */
HOOK_V(scr_title_scene,         0, TR(engine_title_render_scene(G8(L2_SCREEN), g_tr_page)))
HOOK_V(scr_title_main,          0, TR(engine_title_render_main_menu()))
HOOK_V(scr_title_buttons,       0, TR(engine_title_render_buttons()))
HOOK_V(scr_title_options,       0, TR(engine_title_render_options()))
HOOK_V(scr_title_options_buttons, 0, TR(engine_title_render_options_buttons()))
HOOK_V(scr_title_choose,        0, TR(engine_title_render_choose_title()))
HOOK_V(scr_title_choose_buttons, 0, TR(engine_title_render_choose_title_buttons()))
HOOK_V(scr_title_shields,       0, TR(engine_title_render_choose_title_shields()))
HOOK_V(scr_title_name_box,      0, TR(engine_title_render_choose_title_name_box()))
HOOK_V(scr_title_play_now,      0, TR(engine_title_render_play_now()))
HOOK_V(scr_title_play_now_buttons, 0, TR(engine_title_render_play_now_buttons()))
HOOK_V(scr_title_defaults,      0, engine_title_render_setup_apply_defaults())
HOOK_V(scr_title_clamp_players, 1, engine_title_render_setup_clamp_player_count(I(0)))
/* engine_county_panels.c */
HOOK_V(scr_county_tax,          0, CP(engine_county_panel_tax(cp)))
HOOK_V(scr_county_rations,      0, CP(engine_county_panel_rations(cp)))
HOOK_V(scr_county_slider,       0, CP(engine_county_ration_slider_render(cp)))
HOOK_V(scr_county_delta,        6, CP(engine_county_delta_label(cp, I(0), I(1), I(2), P(3), C(4), C(5))))
HOOK_V(scr_county_year,         4, CP(engine_county_year_number(cp, I(0), I(1), I(2), I(3))))
HOOK_V(scr_county_units,        6, CP(engine_county_num_units(cp, I(0), I(1), I(2), I(3), P(4), C(5))))
HOOK_V(scr_county_chrome_panel, 4, B(engine_county_chrome_panel(CHROME_SHEET, I(0), I(1), I(2), I(3))))
/* the steps take the county as their argument */
HOOK_V(scr_county_tax_up,       1, CP(cp->county = I(0); engine_county_tax_step_render(cp, 1)))
HOOK_V(scr_county_tax_down,     1, CP(cp->county = I(0); engine_county_tax_step_render(cp, -1)))
HOOK_V(scr_county_wanted_up,    1, CP(cp->county = I(0); engine_county_wanted_step_render(cp, 1)))
HOOK_V(scr_county_wanted_down,  1, CP(cp->county = I(0); engine_county_wanted_step_render(cp, -1)))
/* engine_battlefield_render.c — the state is aliased; B() binds the fixed tables once */
HOOK_V(scr_bf_frame,            2, B(engine_battlefield_render_frame(I(0), I(1))))
HOOK_V(scr_bf_terrain16,        0, B(engine_battlefield_render_terrain_zoom16()))
HOOK_V(scr_bf_terrain32,        0, B(engine_battlefield_render_terrain_zoom32()))
HOOK_V(scr_bf_overlay16,        0, B(engine_battlefield_render_overlay_zoom16()))
HOOK_V(scr_bf_overlay32,        0, B(engine_battlefield_render_overlay_zoom32()))
HOOK_V(scr_bf_middle,           0, B(engine_battlefield_render_middle()))
HOOK_V(scr_bf_build_list,       0, B(engine_battlefield_render_build_list()))
HOOK_V(scr_bf_sort_list,        0, B(engine_battlefield_render_sort_list()))
HOOK_V(scr_bf_render_list,      0, B(engine_battlefield_render_render_list()))
HOOK_V(scr_bf_entity,           1, B(engine_battlefield_render_entity(I(0))))
HOOK_V(scr_bf_type6,            1, B(engine_battlefield_render_entity_type6(I(0))))
HOOK_V(scr_bf_type7,            1, B(engine_battlefield_render_entity_type7(I(0))))
HOOK_V(scr_bf_type9e,           1, B(engine_battlefield_render_entity_type9e(I(0))))
HOOK_V(scr_bf_marker,           0, B(engine_battlefield_render_player_marker()))
HOOK_V(scr_bf_unit_key,         1, B(engine_battlefield_render_unit_key(I(0))))
HOOK_V(scr_bf_effect,           1, B(engine_battlefield_render_effect_chain(I(0))))
HOOK_V(scr_bf_late,             0, B(engine_battlefield_render_late()))
HOOK_V(scr_bf_creature,         1, B(engine_battlefield_render_creature(I(0))))
HOOK_V(scr_bf_selection,        0, B(engine_battlefield_render_selection_marker()))
HOOK_V(scr_bf_final,            0, B(engine_battlefield_render_final()))
HOOK_V(scr_bf_minimap,          2, B(engine_battlefield_render_minimap_strip(I(0), I(1))))
HOOK_V(scr_bf_scroll,           1, B(engine_battlefield_render_minimap_scroll(I(0))))
HOOK_V(scr_bf_bind_minimap,     6, engine_battlefield_render_bind_minimap_context(P(0), P(1), P(2), I(3), I(4), I(5)))
HOOK_R(scr_bf_in_grid,          2, engine_battlefield_render_screen_in_grid(I(0), I(1)))
HOOK_R(scr_bf_px_col,           1, engine_battlefield_render_pixel_to_col(I(0)))
HOOK_R(scr_bf_px_row,           1, engine_battlefield_render_pixel_to_row(I(0)))
HOOK_R(scr_bf_col_right,        1, engine_battlefield_render_pixel_to_col_right_biased(I(0)))
HOOK_R(scr_bf_col_left,         1, engine_battlefield_render_pixel_to_col_left_biased(I(0)))
HOOK_R(scr_bf_row_bottom,       1, engine_battlefield_render_pixel_to_row_bottom_biased(I(0)))
HOOK_R(scr_bf_row_top,          1, engine_battlefield_render_pixel_to_row_top_biased(I(0)))
HOOK_R(scr_bf_col_delta,        2, engine_battlefield_render_col_delta(I(0), I(1)))
HOOK_R(scr_bf_row_delta,        2, engine_battlefield_render_row_delta(I(0), I(1)))
HOOK_R(scr_bf_zoom,             0, engine_battlefield_render_get_zoom())
/* engine_trade.c — FUN_00416308 always computes (the standalone's one-shot flag set) */
HOOK_V(scr_trade_render,        0, TRD(g_trade_needs_compute = 1; engine_trade_render(tr)))
HOOK_V(scr_trade_content,       0, TRD(engine_trade_content(tr)))
/* engine_armory.c — FUN_00419243 reads the torch frame DAT_005aea54; FUN_004190db the 20 / 80 ms ticks
 * DAT_0058fcb0 / DAT_00591510 */
HOOK_V(scr_arm_counts,          0, AR(engine_armory_counts_render(ar)))
HOOK_V(scr_arm_racks,           0, AR(engine_armory_racks(ar)))
HOOK_V(scr_arm_refresh,         0, B(engine_armory_refresh((int)G32(0x5aea54u))))
HOOK_V(scr_arm_march_restore,   0, B(engine_armory_march_restore()))
HOOK_V(scr_arm_march_step,      0, B(engine_armory_march_step((int)G32(0x58fcb0u), (int)G32(0x591510u))))
HOOK_V(scr_arm_muster,          2, B(arm_muster(I(0), I(1))))
HOOK_V(scr_arm_unit_noun,       6, AR(engine_armory_unit_noun(ar, I(0), I(1), I(2), I(3), P(4), C(5))))
HOOK_V(scr_arm_overlay,         0, AR(engine_armory_overlay_render(ar)))
HOOK_V(scr_arm_conscript,       2, engine_armory_conscript_update(g_per_army_768 + (size_t)I(0) * 0x300, I(1)))
HOOK_R(scr_arm_slider,          0, (scr_bind(), arm_slider()))
HOOK_V(scr_arm_render,          1, AR(ar->open_arg = I(0); engine_armory_render(ar)))
/* engine_title_render.c — the setup page (FUN_0041f6c7 / FUN_0041f86d take the suppressed tab) */
HOOK_V(scr_title_setup,         1, TR(engine_title_render_setup_screen(I(0))))
HOOK_V(scr_title_tabs,          1, TR(engine_title_render_setup_tabs(I(0))))
HOOK_V(scr_title_country,       0, TR(engine_title_render_setup_country_list()))
HOOK_V(scr_title_roster,        0, TR(engine_title_render_setup_roster()))
/* engine_county_panels.c — the two input handlers, whole */
HOOK_R(scr_county_grid,         0, (scr_bind(), county_grid()))
static int county_rations_row(void) { int r; CP(r = county_rations_in(cp)); return r; }
HOOK_R(scr_county_rations_in,   0, county_rations_row())
HOOK_V(scr_county_population,   0, CP(engine_county_panel_population(cp)))
HOOK_V(scr_county_happiness,    0, CP(engine_county_panel_happiness(cp)))
static int county_graph_row(const uint32_t *a) { int r; CP(r = engine_county_graph_render(cp, I(0), I(1), I(2))); return r; }
HOOK_R(scr_county_graph,        3, county_graph_row(a))
/* engine_labor.c — FUN_0041dedb(county, x, y, row) */
HOOK_V(scr_labor_castle,        4, LB(engine_labor_castle_block(lb, I(0), I(1), I(2), I(3))))
/* engine_title_render.c — the save-file list (page 3; FUN_004149ec's second argument: 1 = the title layout) */
HOOK_V(scr_title_filelist,      1, TR(engine_title_render_filelist(I(0))))
HOOK_V(scr_title_filelist_content, 2, TR(engine_title_render_filelist_content(I(0), I(1))))

hook_t RH_HOOKS_SCREENS[] = {
    { "scr_tooltip_tick", 0x476e95u, (void *)w_scr_tooltip_tick, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_tooltip_arm", 0x477131u, (void *)w_scr_tooltip_arm, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_tooltip_clear", 0x4770b8u, (void *)w_scr_tooltip_clear, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_tooltip_clamp", 0x477249u, (void *)w_scr_tooltip_clamp, 0, 0, 0, 0, "", 0, 0 },
    { "scr_tooltip_classify", 0x4772b6u, (void *)w_scr_tooltip_classify, 0, 0, 0, 0, "", 0, 0 },
    { "scr_tooltip_strategic", 0x477320u, (void *)w_scr_tooltip_strategic, 0, 0, 0, 0, "", 0, 0 },
    { "scr_tooltip_battle", 0x4777aau, (void *)w_scr_tooltip_battle, 0, 0, 0, 0, "", 0, 0 },
    { "scr_backdrop_capture", 0x4b3f0au, (void *)w_scr_backdrop_capture, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_backdrop_restore", 0x4b3ec0u, (void *)w_scr_backdrop_restore, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_mp_indicator", 0x41f15au, (void *)w_scr_mp_indicator, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_mp_status", 0x423b4fu, (void *)w_scr_mp_status, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_noble_render", 0x416cf3u, (void *)w_scr_noble_render, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_noble_face", 0x4171eeu, (void *)w_scr_noble_face, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_scene", 0x41e7e1u, (void *)w_scr_title_scene, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_main", 0x41ea14u, (void *)w_scr_title_main, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_buttons", 0x41eaa3u, (void *)w_scr_title_buttons, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_options", 0x41ec8au, (void *)w_scr_title_options, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_options_buttons", 0x41ece6u, (void *)w_scr_title_options_buttons, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_choose", 0x41ef57u, (void *)w_scr_title_choose, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_choose_buttons", 0x41f01fu, (void *)w_scr_title_choose_buttons, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_shields", 0x41f1ddu, (void *)w_scr_title_shields, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_name_box", 0x41f321u, (void *)w_scr_title_name_box, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_play_now", 0x41f592u, (void *)w_scr_title_play_now, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_play_now_buttons", 0x41f5e8u, (void *)w_scr_title_play_now_buttons, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_defaults", 0x4ae539u, (void *)w_scr_title_defaults, 0, 0, 0, 0, "", 0, 0 },
    { "scr_title_clamp_players", 0x4ae5e2u, (void *)w_scr_title_clamp_players, 0, 0, 0, 0, "", 0, 0 },
    { "scr_county_tax", 0x41152fu, (void *)w_scr_county_tax, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_rations", 0x411b72u, (void *)w_scr_county_rations, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_slider", 0x411fdeu, (void *)w_scr_county_slider, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_delta", 0x41ac95u, (void *)w_scr_county_delta, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_year", 0x41a900u, (void *)w_scr_county_year, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_units", 0x41ab67u, (void *)w_scr_county_units, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_chrome_panel", 0x409397u, (void *)w_scr_county_chrome_panel, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_tax_up", 0x43aa83u, (void *)w_scr_county_tax_up, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_tax_down", 0x43ab25u, (void *)w_scr_county_tax_down, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_wanted_up", 0x43a23fu, (void *)w_scr_county_wanted_up, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_wanted_down", 0x43a307u, (void *)w_scr_county_wanted_down, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_frame", 0x4bc142u, (void *)w_scr_bf_frame, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_terrain16", 0x4bc8fcu, (void *)w_scr_bf_terrain16, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_terrain32", 0x4bcbdcu, (void *)w_scr_bf_terrain32, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_overlay16", 0x4bcbd1u, (void *)w_scr_bf_overlay16, 0, 0, 0, 0, "", 0, 0 },
    { "scr_bf_overlay32", 0x4bd07fu, (void *)w_scr_bf_overlay32, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_middle", 0x4bd355u, (void *)w_scr_bf_middle, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_build_list", 0x4bd938u, (void *)w_scr_bf_build_list, 0, 0, 0, 0, "", 0, 0 },
    { "scr_bf_sort_list", 0x4bda92u, (void *)w_scr_bf_sort_list, 0, 0, 0, 0, "", 0, 0 },
    { "scr_bf_render_list", 0x4bdb95u, (void *)w_scr_bf_render_list, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_entity", 0x4bdc31u, (void *)w_scr_bf_entity, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "scr_bf_type6", 0x4be4dfu, (void *)w_scr_bf_type6, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_type7", 0x4be7beu, (void *)w_scr_bf_type7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_type9e", 0x4beab9u, (void *)w_scr_bf_type9e, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_marker", 0x4bd574u, (void *)w_scr_bf_marker, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_unit_key", 0x4bd759u, (void *)w_scr_bf_unit_key, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_effect", 0x4beed4u, (void *)w_scr_bf_effect, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "scr_bf_late", 0x4bd233u, (void *)w_scr_bf_late, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_creature", 0x4be070u, (void *)w_scr_bf_creature, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "scr_bf_selection", 0x4be33fu, (void *)w_scr_bf_selection, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_final", 0x4bf1f9u, (void *)w_scr_bf_final, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_minimap", 0x4bc51au, (void *)w_scr_bf_minimap, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_scroll", 0x4bc1d1u, (void *)w_scr_bf_scroll, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_bf_bind_minimap", 0x4bc107u, (void *)w_scr_bf_bind_minimap, 0, 0, 0, 0, "", 0, 0 },
    { "scr_bf_in_grid", 0x4bc222u, (void *)w_scr_bf_in_grid, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_px_col", 0x4bc2a5u, (void *)w_scr_bf_px_col, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_px_row", 0x4bc2c5u, (void *)w_scr_bf_px_row, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_col_right", 0x4bc2e5u, (void *)w_scr_bf_col_right, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_col_left", 0x4bc346u, (void *)w_scr_bf_col_left, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_row_bottom", 0x4bc3acu, (void *)w_scr_bf_row_bottom, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_row_top", 0x4bc40du, (void *)w_scr_bf_row_top, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_col_delta", 0x4bc471u, (void *)w_scr_bf_col_delta, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_row_delta", 0x4bc4bbu, (void *)w_scr_bf_row_delta, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_bf_zoom", 0x4bc505u, (void *)w_scr_bf_zoom, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "scr_trade_render", 0x416308u, (void *)w_scr_trade_render, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_trade_content", 0x41635fu, (void *)w_scr_trade_content, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_counts", 0x4181ebu, (void *)w_scr_arm_counts, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_racks", 0x418426u, (void *)w_scr_arm_racks, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_refresh", 0x419243u, (void *)w_scr_arm_refresh, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_march_restore", 0x418fc5u, (void *)w_scr_arm_march_restore, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_march_step", 0x4190dbu, (void *)w_scr_arm_march_step, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_muster", 0x4aa90au, (void *)w_scr_arm_muster, 0, 0, 0, 0, "", 0, 0 },
    { "scr_arm_unit_noun", 0x41ac3eu, (void *)w_scr_arm_unit_noun, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_overlay", 0x418653u, (void *)w_scr_arm_overlay, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_arm_conscript", 0x435ebcu, (void *)w_scr_arm_conscript, 0, 0, 0, 0, "", 0, 0 },
    { "scr_arm_slider", 0x435cefu, (void *)w_scr_arm_slider, 0, 0, 0, 0, "", 0, 0 },
    { "scr_arm_render", 0x417ea7u, (void *)w_scr_arm_render, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_setup", 0x41f6c7u, (void *)w_scr_title_setup, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_tabs", 0x41f86du, (void *)w_scr_title_tabs, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_country", 0x41f98bu, (void *)w_scr_title_country, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_roster", 0x41fbcbu, (void *)w_scr_title_roster, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_grid", 0x438cebu, (void *)w_scr_county_grid, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_rations_in", 0x43a379u, (void *)w_scr_county_rations_in, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_population", 0x4110b1u, (void *)w_scr_county_population, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_happiness", 0x4116fbu, (void *)w_scr_county_happiness, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_county_graph", 0x4156a7u, (void *)w_scr_county_graph, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_labor_castle", 0x41dedbu, (void *)w_scr_labor_castle, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_filelist", 0x4148e4u, (void *)w_scr_title_filelist, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_title_filelist_content", 0x4149ecu, (void *)w_scr_title_filelist_content, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_SCREENS_N = sizeof RH_HOOKS_SCREENS / sizeof *RH_HOOKS_SCREENS;
