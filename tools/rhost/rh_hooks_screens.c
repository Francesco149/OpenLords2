/* rh_hooks_screens.c — the screen renderers above the gfx layer (src/engine_tooltip.c, engine_workforce_hud.c's
 * backdrop walk, engine_mp_render.c, …) vs retail, on identical state AND identical pixels (rh_hooks.h contract;
 * shared plumbing in rh_port.h / rh_hooks_ai.c; the gfx layer below: rh_hooks_gfx.c). Write-up:
 * docs/findings/render-screens.md.
 *
 * BINDING.  Module globals the ports keep in src/<module>_globals.c (not linked) are aliased below at their retail
 * VAs.  Everything else — the env (fonts, sheets, the localized-text walker, the old ports' callbacks, the battlefield
 * tables), the per-module context / state adapters over retail's globals, the mirrors — is the link layer's:
 * src/rl_link_screens.c (docs/findings/rl-link.md).  Each row calls its retail-layout entry rl_<va>, so the row
 * verifies the src entry, env included.
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
#include "engine_treasury.h"
#include "engine_screen_strat.h"
#include "gfx.h"
#include "gfx_panel.h"
#include "text_input.h"
#include "save.h"
#include "save_schema_globals.h"
#include "rl.h"   /* the rows' entries (rl_link.h) and the bridges' retail-layout targets */

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
/* engine_workforce_hud_globals.c */
A(g_wf_grid,0x5540a0) A(g_wf_icon_sheet,0x5530c8) A(g_wf_hover_category,0x53e9a0) A(g_wf_hover_cells,0x553040)
A(g_wf_panel_mode,0x53f050) A(g_wf_y_origin,0x57c908) A(g_wf_villani1,0x53e918) A(g_wf_villani2,0x5533c4)
/* engine_treasury_globals.c */
A(g_treasury_buttons,0x4dd928) A(g_treasury_widget_count,0x568468)
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

/* ---------------------------------------------------------------- bridges across the link cut (io.c / io_path.c / gfx.c /
 * engine_screen_strat.c are not linked under rhost): the ports' file loads, backdrop loader, palette, menu bar → their
 * retail-layout ports, bracketed like the screens env's own callbacks (src/rl_link_screens.c: the walker) */
#define SCR_OUT(...) do { rl_link_scr_callout_enter(); __VA_ARGS__; rl_link_scr_callout_leave(); } while (0)
int io_path_chdir_base(void) { int r; SCR_OUT(r = rl_4af502()); return r; }
int io_read_file_at(const char *path, void *buf, size_t len, long offset)
{ int r; SCR_OUT(r = rl_4af8d9((int)(uintptr_t)path, (int)(uintptr_t)buf, (int)len, (int)offset)); return r; }
/* the backdrop loader FUN_00408fcb(name, 480) reads the .pl8 straight into the back buffer (always retail's live one) */
int gfx_load_background_image(const char *pl8_path) { int r; SCR_OUT(r = rl_408fcb((int)(uintptr_t)pl8_path, 480)); return r; }
/* the palette: the .256 into the mirror DAT_004ea8a0, FUN_004b0ab5 applies it (reached only by the armory open render) */
int gfx_load_palette(const char *path)
{
    io_read_file_at(path, (void *)(uintptr_t)0x4ea8a0u, 0x300, 0);
    SCR_OUT(rl_4b0ab5(0x4ea8a0));
    return 1;
}
void engine_screen_strat_render_menubar(void) { SCR_OUT(rl_419c78()); }   /* the menu bar FUN_00419c78 (the labor screen's head) */
int  gfx_lock_canvas(void)   { return 1; }
void gfx_unlock_canvas(void) { }

/* the env (fonts, sheets, walker, the old ports' callbacks, the battlefield tables, the per-module contexts):
 * src/rl_link_screens.c — rl_link_screens_bind once, every row through its retail-layout entry rl_<va> */
__attribute__((constructor)) static void scr_bind_register(void) { rh_port_register_bind(rl_link_screens_bind); }

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
    X_scr_wf_sprite_index, X_scr_wf_fill_linear, X_scr_wf_fill_dual, X_scr_wf_compute_row, X_scr_wf_compute_rows,
    X_scr_wf_draw_row, X_scr_wf_draw_all, X_scr_wf_render, X_scr_treasury,
};

HOOK_V(scr_tooltip_tick,           0, rl_476e95())
HOOK_V(scr_tooltip_arm,            0, rl_477131())
HOOK_V(scr_tooltip_clear,          0, rl_4770b8())
HOOK_V(scr_tooltip_clamp,          0, rl_477249())
HOOK_R(scr_tooltip_classify,       0, rl_4772b6())
HOOK_R(scr_tooltip_strategic,      0, rl_477320())
HOOK_R(scr_tooltip_battle,         0, rl_4777aa())
HOOK_V(scr_backdrop_capture,       2, rl_4b3f0a(I(0), I(1)))
HOOK_V(scr_backdrop_restore,       2, rl_4b3ec0(I(0), I(1)))
HOOK_V(scr_mp_indicator,           0, rl_41f15a())
HOOK_V(scr_mp_status,              0, rl_423b4f())
HOOK_V(scr_noble_render,           0, rl_416cf3())
HOOK_V(scr_noble_face,             2, rl_4171ee(I(0), I(1)))
HOOK_V(scr_title_scene,            0, rl_41e7e1())
HOOK_V(scr_title_main,             0, rl_41ea14())
HOOK_V(scr_title_buttons,          0, rl_41eaa3())
HOOK_V(scr_title_options,          0, rl_41ec8a())
HOOK_V(scr_title_options_buttons,  0, rl_41ece6())
HOOK_V(scr_title_choose,           0, rl_41ef57())
HOOK_V(scr_title_choose_buttons,   0, rl_41f01f())
HOOK_V(scr_title_shields,          0, rl_41f1dd())
HOOK_V(scr_title_name_box,         0, rl_41f321())
HOOK_V(scr_title_play_now,         0, rl_41f592())
HOOK_V(scr_title_play_now_buttons, 0, rl_41f5e8())
HOOK_V(scr_title_defaults,         0, rl_4ae539())
HOOK_V(scr_title_clamp_players,    1, rl_4ae5e2(I(0)))
HOOK_V(scr_county_tax,             0, rl_41152f())
HOOK_V(scr_county_rations,         0, rl_411b72())
HOOK_V(scr_county_slider,          0, rl_411fde())
HOOK_V(scr_county_delta,           6, rl_41ac95(I(0), I(1), I(2), I(3), I(4), I(5)))
HOOK_V(scr_county_year,            4, rl_41a900(I(0), I(1), I(2), I(3)))
HOOK_V(scr_county_units,           6, rl_41ab67(I(0), I(1), I(2), I(3), I(4), I(5)))
HOOK_V(scr_county_chrome_panel,    4, rl_409397(I(0), I(1), I(2), I(3)))
HOOK_V(scr_county_tax_up,          1, rl_43aa83(I(0)))
HOOK_V(scr_county_tax_down,        1, rl_43ab25(I(0)))
HOOK_V(scr_county_wanted_up,       1, rl_43a23f(I(0)))
HOOK_V(scr_county_wanted_down,     1, rl_43a307(I(0)))
HOOK_V(scr_bf_frame,               2, rl_4bc142(I(0), I(1)))
HOOK_V(scr_bf_terrain16,           0, rl_4bc8fc())
HOOK_V(scr_bf_terrain32,           0, rl_4bcbdc())
HOOK_V(scr_bf_overlay16,           0, rl_4bcbd1())
HOOK_V(scr_bf_overlay32,           0, rl_4bd07f())
HOOK_V(scr_bf_middle,              0, rl_4bd355())
HOOK_V(scr_bf_build_list,          0, rl_4bd938())
HOOK_V(scr_bf_sort_list,           0, rl_4bda92())
HOOK_V(scr_bf_render_list,         0, rl_4bdb95())
HOOK_V(scr_bf_entity,              1, rl_4bdc31(I(0)))
HOOK_V(scr_bf_type6,               1, rl_4be4df(I(0)))
HOOK_V(scr_bf_type7,               1, rl_4be7be(I(0)))
HOOK_V(scr_bf_type9e,              1, rl_4beab9(I(0)))
HOOK_V(scr_bf_marker,              0, rl_4bd574())
HOOK_V(scr_bf_unit_key,            1, rl_4bd759(I(0)))
HOOK_V(scr_bf_effect,              1, rl_4beed4(I(0)))
HOOK_V(scr_bf_late,                0, rl_4bd233())
HOOK_V(scr_bf_creature,            1, rl_4be070(I(0)))
HOOK_V(scr_bf_selection,           0, rl_4be33f())
HOOK_V(scr_bf_final,               0, rl_4bf1f9())
HOOK_V(scr_bf_minimap,             2, rl_4bc51a(I(0), I(1)))
HOOK_V(scr_bf_scroll,              1, rl_4bc1d1(I(0)))
HOOK_V(scr_bf_bind_minimap,        6, rl_4bc107(I(0), I(1), I(2), I(3), I(4), I(5)))
HOOK_R(scr_bf_in_grid,             2, rl_4bc222(I(0), I(1)))
HOOK_R(scr_bf_px_col,              1, rl_4bc2a5(I(0)))
HOOK_R(scr_bf_px_row,              1, rl_4bc2c5(I(0)))
HOOK_R(scr_bf_col_right,           1, rl_4bc2e5(I(0)))
HOOK_R(scr_bf_col_left,            1, rl_4bc346(I(0)))
HOOK_R(scr_bf_row_bottom,          1, rl_4bc3ac(I(0)))
HOOK_R(scr_bf_row_top,             1, rl_4bc40d(I(0)))
HOOK_R(scr_bf_col_delta,           2, rl_4bc471(I(0), I(1)))
HOOK_R(scr_bf_row_delta,           2, rl_4bc4bb(I(0), I(1)))
HOOK_R(scr_bf_zoom,                0, rl_4bc505())
HOOK_V(scr_trade_render,           0, rl_416308())
HOOK_V(scr_trade_content,          0, rl_41635f())
HOOK_V(scr_arm_counts,             0, rl_4181eb())
HOOK_V(scr_arm_racks,              0, rl_418426())
HOOK_V(scr_arm_refresh,            0, rl_419243())
HOOK_V(scr_arm_march_restore,      0, rl_418fc5())
HOOK_V(scr_arm_march_step,         0, rl_4190db())
HOOK_V(scr_arm_muster,             2, rl_4aa90a(I(0), I(1)))
HOOK_V(scr_arm_unit_noun,          6, rl_41ac3e(I(0), I(1), I(2), I(3), I(4), I(5)))
HOOK_V(scr_arm_overlay,            0, rl_418653())
HOOK_V(scr_arm_conscript,          2, rl_435ebc(I(0), I(1)))
HOOK_R(scr_arm_slider,             0, rl_435cef())
HOOK_V(scr_arm_render,             1, rl_417ea7(I(0)))
HOOK_V(scr_title_setup,            1, rl_41f6c7(I(0)))
HOOK_V(scr_title_tabs,             1, rl_41f86d(I(0)))
HOOK_V(scr_title_country,          0, rl_41f98b())
HOOK_V(scr_title_roster,           0, rl_41fbcb())
HOOK_R(scr_county_grid,            0, rl_438ceb())
HOOK_R(scr_county_rations_in,      0, rl_43a379())
HOOK_V(scr_county_population,      0, rl_4110b1())
HOOK_V(scr_county_happiness,       0, rl_4116fb())
HOOK_R(scr_county_graph,           3, rl_4156a7(I(0), I(1), I(2)))
HOOK_V(scr_labor_castle,           4, rl_41dedb(I(0), I(1), I(2), I(3)))
HOOK_V(scr_title_filelist,         1, rl_4148e4(I(0)))
HOOK_V(scr_title_filelist_content, 2, rl_4149ec(I(0), I(1)))
HOOK_R(scr_wf_sprite_index,        2, rl_4517ca(I(0), I(1)))
HOOK_V(scr_wf_fill_linear,         3, rl_4519ca(I(0), I(1), I(2)))
HOOK_V(scr_wf_fill_dual,           5, rl_451a5d(I(0), I(1), I(2), I(3), I(4)))
HOOK_V(scr_wf_compute_row,         4, rl_4518a5(I(0), I(1), I(2), I(3)))
HOOK_V(scr_wf_compute_rows,        1, rl_45161e(I(0)))
HOOK_V(scr_wf_draw_row,            3, rl_4126c9(I(0), I(1), I(2)))
HOOK_V(scr_wf_draw_all,            0, rl_412666())
HOOK_V(scr_wf_render,              1, rl_412143(I(0)))
HOOK_V(scr_treasury,               0, rl_416925())

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
    { "scr_wf_sprite_index", 0x4517cau, (void *)w_scr_wf_sprite_index, 0, 0, 0, 0, "", 0, 0 },
    { "scr_wf_fill_linear", 0x4519cau, (void *)w_scr_wf_fill_linear, 0, 0, 0, 0, "", 0, 0 },
    { "scr_wf_fill_dual", 0x451a5du, (void *)w_scr_wf_fill_dual, 0, 0, 0, 0, "", 0, 0 },
    { "scr_wf_compute_row", 0x4518a5u, (void *)w_scr_wf_compute_row, 0, 0, 0, 0, "", 0, 0 },
    { "scr_wf_compute_rows", 0x45161eu, (void *)w_scr_wf_compute_rows, 0, 0, 0, 0, "", 0, 0 },
    { "scr_wf_draw_row", 0x4126c9u, (void *)w_scr_wf_draw_row, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_wf_draw_all", 0x412666u, (void *)w_scr_wf_draw_all, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_wf_render", 0x412143u, (void *)w_scr_wf_render, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "scr_treasury", 0x416925u, (void *)w_scr_treasury, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_SCREENS_N = sizeof RH_HOOKS_SCREENS / sizeof *RH_HOOKS_SCREENS;
