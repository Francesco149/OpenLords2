/* rl_link_screens.c — the screens family of the link layer (docs/findings/rl-link.md): the environment the old screen
 * renderers draw through (engine_tooltip / _mp_render / _noble_faces / _title_render / _county_panels /
 * _battlefield_render / _trade / _armory / _labor / _treasury / _workforce_hud), moved from rhost's
 * tools/rhost/rh_hooks_screens.c (scr_bind and the per-module context builders), and a retail-layout entry rl_<va>
 * for every one of their retail functions.  rhost's rows call these entries; retail-layout code calls them instead of
 * engine_retail_call(VA).
 *
 * WALKER.  The localized-text walker: the port's localization_state_t over retail's blob &DAT_00591580 + walker
 * pointer DAT_005c9270, loaded / stored around an entry; s_loc_live counts the entries holding it (one nested in a
 * callback reloads and stores it too).  A callback into retail-layout code (a page renderer, the background loader)
 * stores it first and reloads it after, so its walk and the old port's land in the same pointer.
 *
 * MIRRORS.  A drawing entry runs between rl_link_port_enter / _leave (the dirty rectangle is a mirror:
 * engine_territory_dirty_state() = rl_link_dirty); the pure leaves (the battlefield's pixel ↔ cell helpers, the
 * workforce row arithmetic, the backdrop walk, the setup defaults) touch no mirror and skip it. */
#include "rl_link.h"
#include <string.h>
#include "rl.h"
#include "rl_io_plat.h"        /* rl_plat_timeGetTime */
#include "rl_link_globals.h"
#include "engine.h"            /* g_sim_mp_active (DAT_00553030) */
#include "engine_bf.h"         /* AT */
#include "save.h"
#include "save_schema_globals.h"
#include "gfx_globals.h"       /* g_gfx_first_pos_target: the editor cursor DAT_005bb4a8 */
#include "gfx_dispatch.h"
#include "gfx_sprite_render.h"
#include "gfx_wrap.h"
#include "gfx_panel.h"
#include "localization.h"
#include "text_input.h"
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

/* the partitions' globals (their headers conflict when included together: DUP symbols typed per partition) */
extern int32_t g_bf_4e65fc, g_bf_4e65f4, g_bf_4ea4b0, g_bf_5530c8, g_bf_4eabec;   /* cursor x / y, mouse activity, sheets */
extern uint8_t g_bf_4eac50, g_bf_4eafb4, g_bf_4eabc5, g_bf_4e65cc, g_bf_4e65d8;   /* the screen; press / repeat / held / up */
extern uint8_t g_bf_59154c[0xc], g_bf_542ce0[24], g_bf_5af8f0[14000];
extern int32_t g_rl_53f250, g_rl_4eac00, g_rl_4eb268, g_rl_4eb270, g_rl_4ea830, g_rl_53e970, g_rl_553c60, g_rl_53f04c,
               g_rl_5440b4, g_rl_53e8b4, g_rl_53f684, g_rl_5aea54, g_rl_58fcb0, g_rl_591510, g_rl_57c908;
extern int32_t g_rl_553fd0[5];
extern uint8_t g_rl_4e6980[7680], g_rl_53f690[0x320], g_rl_5c9280, g_rl_5cd550[2000], g_rl_5530e0[8], g_rl_53f6a0[0x300],
               g_rl_5b2fa0[34000], g_rl_5bb540[0xdd18], g_rl_57d3d0[65536], g_rl_4ea8a0[768], g_rl_4ea1b0[0x300],
               g_rl_50a460[60], g_rl_4e8790[6500], g_rl_591580[120000], g_rl_4d71d0[0x140];
extern uint8_t *g_rl_5c9270;

#define FONT_DEFAULT  ((const uint8_t *)g_bf_5af8f0)       /* &DAT_005af8f0: fntl2_14 */
#define FONT_TALL     ((const uint8_t *)g_rl_5b2fa0)       /* &DAT_005b2fa0: the tall (24 px) title font */
#define GLYPH_TABLE   ((const uint8_t *)g_rl_4d71d0 + 0x20)   /* DAT_004d71f0: char → 1-based glyph (render) */
#define METRICS_TABLE ((const unsigned char *)g_rl_4d71d0) /* DAT_004d71d0: the measurers' table */
#define SYSTEM_SHEET  ((const uint8_t *)g_rl_5bb540)       /* &DAT_005bb540: system2.pl8 */
#define CHROME_SHEET  ((const uint8_t *)g_rl_57d3d0)       /* &DAT_0057d3d0: the chrome sheet (panels.pl8) */
#define MISC_SHEET    ((const uint8_t *)(uintptr_t)(uint32_t)g_bf_5530c8)   /* DAT_005530c8: the mode's misc_*.pl8 */
#define CUR_BLOB      ((uint8_t *)(uintptr_t)(uint32_t)g_bf_4eabec)         /* DAT_004eabec: the current sheet / scratch */
#define REALM_NAMES   ((const uint8_t *)g_player_aux_44 + 4)                /* DAT_00553d54: 0x2c a realm */
#define COUNTY(c)     (g_per_army_768 + (size_t)(c) * 0x300)                /* DAT_0053f9b0, 0x300 a county */
#define D591550       AT(int32_t, g_bf_59154c, 0x59154cu, 0x591550u)

/* ---------------------------------------------------------------- the walker */
static localization_state_t s_loc;
static int s_loc_live;
static localization_state_t *loc_load(void)
{
    s_loc.blob = g_rl_591580; s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
    s_loc_live++;
    return &s_loc;
}
static void loc_store(void)
{
    uint8_t *w = g_rl_591580 + s_loc.walker_offset;
    if (g_rl_5c9270 != w) g_rl_5c9270 = w;
    if (s_loc_live) s_loc_live--;
}
/* a callback out of an entry into retail-layout code (rl_<va>): that code runs with the mirrors live, as the entry
 * does (it reads / writes them by name: g_cell_cursor_col, rl_link_dirty …), so only the walker changes hands — a
 * mirror store / reload here would drop the callee's writes.  Its own calls into retail are bracketed by
 * engine_retail_call's binder (rl_link_retail_enter / _leave). */
void rl_link_scr_callout_enter(void)
{
    if (s_loc_live) { uint8_t *w = g_rl_591580 + s_loc.walker_offset; if (g_rl_5c9270 != w) g_rl_5c9270 = w; }
}
void rl_link_scr_callout_leave(void)
{
    if (s_loc_live) s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
}
#define SCR_OUT(...) do { rl_link_scr_callout_enter(); __VA_ARGS__; rl_link_scr_callout_leave(); } while (0)

/* ---------------------------------------------------------------- the callbacks the env hands the old ports */
static uint32_t seam_clock(void) { return (uint32_t)rl_plat_timeGetTime(); }
static void seam_log(uint32_t fmt, int n) { SCR_OUT(rl_4afab9((int)fmt, 0, n)); }   /* the sprite renderers' error log */
static void seam_title_background(void) { SCR_OUT(rl_41e61d()); }                   /* the title scene's background loader */
static void seam_title_page(int page)   /* the page renderers the old port does not draw itself */
{
    switch (page) {
    case 6:  SCR_OUT(rl_41f3e9()); break;
    case 8:  SCR_OUT(rl_41f77a(999)); break;
    case 9:  SCR_OUT(rl_41fdd6()); break;
    case 10: SCR_OUT(rl_420428()); break;
    case 11: SCR_OUT(rl_420630(999)); break;
    case 12: SCR_OUT(rl_42051c(999)); break;
    case 13: SCR_OUT(rl_42150b()); break;
    default: break;
    }
}
static void seam_title_preview(int mode, int x, int y) { SCR_OUT(rl_410c71(mode, x, y)); }   /* the setup preview blit */
/* the roster's seats over retail's strategic records (+5 type, +7 portrait, +8 name colour, +0xa shield) and the
 * seat-enabled bytes DAT_005530e0; the names DAT_00553d54 (0x2c a seat) */
static void seats_load(void)
{
    engine_title_setup_player_t seats[6];
    for (int i = 0; i < 6; i++) {
        const uint8_t *r = g_player_strategic_352 + (size_t)i * 0x160;
        seats[i] = (engine_title_setup_player_t){ r[5], r[7], r[8], r[0xa], g_rl_5530e0[i] };
    }
    engine_title_render_set_players(seats, REALM_NAMES);
}
/* 0x49caaa (re)assigns the seats (type / portrait / name colour): the roster reads the records after it, so reload */
static void seam_title_roster_prep(void) { SCR_OUT(rl_49caaa()); seats_load(); }
/* the title editor over retail's (buffer DAT_005cd550, cursor DAT_005bb4a8, insert DAT_005c9280): the name box reads
 * the cursor, its char and the mode */
static text_input_state_t s_ed;
static void ed_load(void)
{
    int cur = (int)g_gfx_first_pos_target;
    s_ed.cursor = cur; s_ed.insert_mode = g_rl_5c9280;
    if (cur >= 0 && cur < TEXT_INPUT_BUF_SIZE) s_ed.buf[cur] = g_rl_5cd550[cur];
}
static void seam_world_dirty(int x, int y, int w, int h) { SCR_OUT(rl_46c4a3(x, y, w, h)); }   /* the strategic-map mark */
static int seam_wf_ensure(void) { int r; SCR_OUT(r = rl_4997bf()); return r; }   /* the workforce buffer allocator */

void rl_link_screens_bind(void)
{
    static int done;
    if (done) return;
    done = 1;
    gfx_dispatch_set_default_font(FONT_DEFAULT);
    gfx_wrap_set_tall_font_marker(FONT_TALL);
    gfx_sprite_render_set_log(seam_log);
    engine_mp_render_bind_status_asset(SYSTEM_SHEET);
    engine_mp_render_bind_text(FONT_TALL, GLYPH_TABLE, g_rl_591580);
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
    engine_title_render_bind_palette(g_rl_4ea1b0);              /* DAT_004ea1b0: the darken's palette */
    engine_title_render_bind_country_values(g_rl_50a460);
    engine_title_render_bind_filelist_entries(g_rl_4e8790);
    engine_title_render_bind_preview_fn(seam_title_preview);
    engine_title_render_bind_roster_prep(seam_title_roster_prep);
    /* the battlefield renderer's fixed retail tables + callbacks (its state: the module's globals) */
    engine_battlefield_render_bind_entity_table(g_per_battle_hex_432, 0x1b0, 0x50);              /* DAT_00554480 */
    engine_battlefield_render_bind_anim_offset_tables(g_rl_4e3bf0, g_rl_4e4030);
    engine_battlefield_render_bind_player_marker_slot_table(g_per_territory_420 + 2, 0x1a4);     /* DAT_0052f0b2 */
    engine_battlefield_render_bind_effect_chain_table(g_per_combat_unit_76, 0x4c, 0x7fff);       /* DAT_0057a100 */
    engine_battlefield_render_bind_effect_chain_anim_table(g_rl_4e4470);
    engine_battlefield_render_bind_late_visibility_table(g_player_strategic_352 + 0xee, 0x160);  /* DAT_0057bfee */
    engine_battlefield_render_bind_minimap_entity_owner_table(g_per_battle_hex_432 + 0x2c, 0x1b0);   /* DAT_005544ac */
    engine_battlefield_render_bind_minimap_player_slot_table(g_player_strategic_352 + 0xa, 0x160);   /* DAT_0057bf0a */
    engine_battlefield_render_bind_debug_label_font(FONT_DEFAULT, GLYPH_TABLE);
    engine_battlefield_render_bind_debug_label_suffix((const char *)g_rl_4e4520);
    engine_battlefield_render_bind_log(seam_log);
    engine_battlefield_render_bind_effect_camera((const int32_t *)&s_slot_0055ce70, (const int32_t *)&s_slot_005678a0);
    engine_battlefield_render_set_dirty_state(engine_territory_dirty_state());
    engine_battlefield_render_bind_force_redraw(&engine_territory_dirty_state()->below_status_redraw);   /* DAT_0053e9ac */
    engine_battlefield_render_bind_world_map_dirty_hook(seam_world_dirty);
    engine_armory_bind_retail_buffers(g_bf_542ce0);             /* &DAT_00542ce0: the click grid */
    engine_armory_set_dirty_state(engine_territory_dirty_state());
    engine_workforce_hud_bind_text(&s_loc, FONT_TALL, FONT_DEFAULT, GLYPH_TABLE);
    engine_workforce_hud_bind_system_sheet(SYSTEM_SHEET);
    engine_workforce_hud_bind_retail(engine_territory_dirty_state(), seam_wf_ensure);
}

/* an entry: the mirrors (a live caller's stored first), the env bound once, the call */
#define SCR(...) do { int live_ = rl_link_port_enter(); rl_link_screens_bind(); __VA_ARGS__; rl_link_port_leave(live_); } while (0)
#define LOC(...) SCR(loc_load(); __VA_ARGS__; loc_store())
#define P(v) ((const uint8_t *)(uintptr_t)(uint32_t)(v))

/* ---------------------------------------------------------------- engine_tooltip: the state struct over retail's */
/* DAT_004eac00 tip, DAT_004eb268 / DAT_004eb270 box position, DAT_004ea830 idle stamp, DAT_004e6980 the 192×40
 * backdrop; the context: DAT_0053f250 "Tool tips", DAT_004eac50 screen (byte), DAT_004ea4b0 mouse activity; the
 * classifier's inputs: the cursor DAT_004e65fc / DAT_004e65f4, the panel sub-mode DAT_0057a0c4, the current county
 * DAT_0057c93c's owner byte (the 0x300 county records at DAT_0053f9b0, +5), the current realm DAT_0057c8cc, the
 * labor rows (DAT_0053e970 height, DAT_00553c60 count, DAT_0053f690 kinds) and production rows (DAT_0053f04c,
 * DAT_005440b4, DAT_00553fd0). */
static engine_tooltip_state_t s_tt;
static engine_tooltip_classify_in_t s_tcin;
static engine_tooltip_ctx_t s_tctx;
static engine_tooltip_state_t *tt_load(void)
{
    s_tt.tip = (int)g_rl_4eac00; s_tt.x = (int)g_rl_4eb268; s_tt.y = (int)g_rl_4eb270; s_tt.idle_stamp_ms = (uint32_t)g_rl_4ea830;
    memcpy(s_tt.backdrop, g_rl_4e6980, sizeof s_tt.backdrop);
    s_tcin.cursor_x = (int)g_bf_4e65fc; s_tcin.cursor_y = (int)g_bf_4e65f4;
    s_tcin.panel_mode = (int)s_slot_0057a0c4;
    uint32_t county = s_slot_0057c93c;
    s_tcin.county_owner = county < 0x100u ? COUNTY(county)[5] : -1;   /* byte_53F9B5[768 * county] */
    s_tcin.current_realm = (int)s_slot_0057c8cc;
    s_tcin.labor_row_h = (int)g_rl_53e970; s_tcin.labor_count = (int)g_rl_553c60;
    s_tcin.labor_kinds = (const int32_t *)(void *)g_rl_53f690;
    s_tcin.prod_row_h = (int)g_rl_53f04c; s_tcin.prod_count = (int)g_rl_5440b4;
    s_tcin.prod_kinds = g_rl_553fd0;
    s_tctx.enabled = (int)g_rl_53f250; s_tctx.screen = g_bf_4eac50; s_tctx.mouse_event = (int)g_bf_4ea4b0;
    s_tctx.canvas_repainted = 0; s_tctx.now_ms = 0; s_tctx.clock = seam_clock; s_tctx.cin = &s_tcin;
    s_tctx.loc = loc_load(); s_tctx.menu_font = FONT_DEFAULT; s_tctx.glyph_table = GLYPH_TABLE;
    s_tctx.glyph_metrics = METRICS_TABLE; s_tctx.dirty = engine_territory_dirty_state();
    return &s_tt;
}
static void tt_store(void)
{
    if (g_rl_4eac00 != s_tt.tip) g_rl_4eac00 = s_tt.tip;
    if (g_rl_4eb268 != s_tt.x) g_rl_4eb268 = s_tt.x;
    if (g_rl_4eb270 != s_tt.y) g_rl_4eb270 = s_tt.y;
    if ((uint32_t)g_rl_4ea830 != s_tt.idle_stamp_ms) g_rl_4ea830 = (int32_t)s_tt.idle_stamp_ms;
    if (memcmp(g_rl_4e6980, s_tt.backdrop, sizeof s_tt.backdrop)) memcpy(g_rl_4e6980, s_tt.backdrop, sizeof s_tt.backdrop);
    loc_store();
}
#define TT(...) SCR(engine_tooltip_state_t *st = tt_load(); (void)st; __VA_ARGS__; tt_store())
int rl_476e95(void) { TT(engine_tooltip_tick(st, &s_tctx)); return 0; }
int rl_477131(void) { TT(engine_tooltip_arm(st, &s_tctx)); return 0; }
int rl_4770b8(void) { TT(engine_tooltip_clear(st, &s_tctx)); return 0; }
int rl_477249(void) { TT(engine_tooltip_clamp(st)); return 0; }
int rl_4772b6(void) { int r = 0; TT(r = engine_tooltip_classify(&s_tctx)); return r; }
int rl_477320(void) { int r = 0; SCR(tt_load(); loc_store(); r = engine_tooltip_classify_strategic(&s_tcin)); return r; }
int rl_4777aa(void) { int r = 0; SCR(tt_load(); loc_store(); r = engine_tooltip_classify_battle(&s_tcin)); return r; }

/* ---------------------------------------------------------------- engine_workforce_hud: the backdrop walk, the grid */
int rl_4b3f0a(int dest, int stride) { engine_workforce_hud_capture_backdrop((uint32_t *)(uintptr_t)(uint32_t)dest, stride); return 0; }
int rl_4b3ec0(int src, int stride) { engine_workforce_hud_restore_backdrop((const uint32_t *)(uintptr_t)(uint32_t)src, stride); return 0; }
int rl_4517ca(int a0, int a1) { return engine_workforce_row_sprite_index(a0, a1); }
int rl_4519ca(int a0, int a1, int a2) { engine_workforce_fill_row_linear(a0, a1, a2); return 0; }
int rl_451a5d(int a0, int a1, int a2, int a3, int a4)
{ engine_workforce_fill_row_dual(a0, (uint8_t)a1, (uint8_t)a2, a3, a4); return 0; }
int rl_4518a5(int a0, int a1, int a2, int a3) { engine_workforce_compute_row(a0, a1, a2, a3); return 0; }
int rl_45161e(int county) { engine_workforce_compute_rows(county); return 0; }
int rl_4126c9(int a0, int a1, int a2) { SCR(engine_workforce_draw_row(a0, a1, a2)); return 0; }
int rl_412666(void) { SCR(engine_workforce_draw_all_rows((int)g_rl_57c908)); return 0; }   /* its y origin DAT_0057c908 */
int rl_412143(int full) { LOC(engine_workforce_hud_render((int)s_slot_0057c93c, 0, full)); return 0; }   /* the county DAT_0057c93c */

/* ---------------------------------------------------------------- engine_mp_render */
/* FUN_0041f15a: MP active DAT_00553030, the flash timer DAT_00568984, the slot DAT_0053e8b4 */
int rl_41f15a(void)
{ SCR(engine_mp_render_player_indicator(g_sim_mp_active, (int *)&g_tr_p4_flash_timer, 0, (int)g_rl_53e8b4)); return 0; }
int rl_423b4f(void) { LOC(engine_mp_render_status_line()); return 0; }

/* ---------------------------------------------------------------- engine_noble_faces: the context over retail's */
static engine_noble_faces_ctx_t s_nf;
static const engine_noble_faces_ctx_t *nf_ctx(void)
{
    s_nf.strategic_block = g_player_strategic_352;               /* DAT_0057bf00 */
    s_nf.realm_names = REALM_NAMES;
    s_nf.local_player = (int)s_slot_0057c8cc;
    s_nf.chrome = CHROME_SHEET; s_nf.misc_sheet = MISC_SHEET; s_nf.system_sheet = SYSTEM_SHEET;
    s_nf.title_font = FONT_TALL; s_nf.menu_font = FONT_DEFAULT;
    s_nf.glyph_table = GLYPH_TABLE; s_nf.glyph_metrics = METRICS_TABLE;
    s_nf.loc = loc_load(); s_nf.faces_scratch = CUR_BLOB; s_nf.dirty = engine_territory_dirty_state();
    return &s_nf;
}
#define NF(...) SCR(const engine_noble_faces_ctx_t *nf = nf_ctx(); __VA_ARGS__; loc_store())
int rl_416cf3(void) { NF(engine_noble_faces_render(nf)); return 0; }
int rl_4171ee(int a0, int a1) { NF(engine_noble_faces_draw_face(nf, a0, a1)); return 0; }

/* ---------------------------------------------------------------- engine_title_render: the module's state is retail's;
 * the editor, the seats and the walker are loaded around the call */
#define TR(...) SCR(ed_load(); seats_load(); loc_load(); __VA_ARGS__; loc_store())
int rl_41e7e1(void) { TR(engine_title_render_scene(g_bf_4eac50, g_tr_page)); return 0; }   /* the screen byte, the page */
int rl_41ea14(void) { TR(engine_title_render_main_menu()); return 0; }
int rl_41eaa3(void) { TR(engine_title_render_buttons()); return 0; }
int rl_41ec8a(void) { TR(engine_title_render_options()); return 0; }
int rl_41ece6(void) { TR(engine_title_render_options_buttons()); return 0; }
int rl_41ef57(void) { TR(engine_title_render_choose_title()); return 0; }
int rl_41f01f(void) { TR(engine_title_render_choose_title_buttons()); return 0; }
int rl_41f1dd(void) { TR(engine_title_render_choose_title_shields()); return 0; }
int rl_41f321(void) { TR(engine_title_render_choose_title_name_box()); return 0; }
int rl_41f592(void) { TR(engine_title_render_play_now()); return 0; }
int rl_41f5e8(void) { TR(engine_title_render_play_now_buttons()); return 0; }
int rl_4ae539(void) { engine_title_render_setup_apply_defaults(); return 0; }
int rl_4ae5e2(int n) { engine_title_render_setup_clamp_player_count(n); return 0; }
/* the setup page (FUN_0041f6c7 / FUN_0041f86d take the suppressed tab) */
int rl_41f6c7(int tab) { TR(engine_title_render_setup_screen(tab)); return 0; }
int rl_41f86d(int tab) { TR(engine_title_render_setup_tabs(tab)); return 0; }
int rl_41f98b(void) { TR(engine_title_render_setup_country_list()); return 0; }
int rl_41fbcb(void) { TR(engine_title_render_setup_roster()); return 0; }
/* the save-file list (page 3; FUN_004149ec's second argument: 1 = the title layout) */
int rl_4148e4(int a0) { TR(engine_title_render_filelist(a0)); return 0; }
int rl_4149ec(int a0, int a1) { TR(engine_title_render_filelist_content(a0, a1)); return 0; }

/* ---------------------------------------------------------------- engine_county_panels: the context over retail's */
/* FUN_004050c0: the map recomposite the panels open with — it may re-pick DAT_0057c93c */
static int seam_map_scene(void)
{ SCR_OUT(engine_retail_call(0x4050c0u, 0)); return (int)s_slot_0057c93c; }   /* TODO(rl-link): the mapview family (engine_map_scene) */
static engine_county_panels_ctx_t s_cp;
static engine_county_panels_ctx_t *cp_ctx(void)
{
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
#define CP(...) SCR(engine_county_panels_ctx_t *cp = cp_ctx(); __VA_ARGS__; loc_store())
/* FUN_00438ceb's flips (the panel openers) by screen code */
static void seam_county_flip(int screen)
{
    switch (screen) {
    case 0x14: SCR_OUT(rl_43a8f2()); break;
    case 0x15: SCR_OUT(rl_43a939()); break;
    case 0x16: SCR_OUT(rl_43a922()); break;
    default:   SCR_OUT(rl_43a846()); break;
    }
}
static void seam_county_mp_ration(void) { SCR_OUT(rl_43eda0(0x31, 0)); }   /* the multiplayer broadcast */
int rl_4110b1(void) { CP(engine_county_panel_population(cp)); return 0; }
int rl_41152f(void) { CP(engine_county_panel_tax(cp)); return 0; }
int rl_4116fb(void) { CP(engine_county_panel_happiness(cp)); return 0; }
int rl_411b72(void) { CP(engine_county_panel_rations(cp)); return 0; }
int rl_411fde(void) { CP(engine_county_ration_slider_render(cp)); return 0; }
int rl_41ac95(int a0, int a1, int a2, int a3, int a4, int a5)
{ CP(engine_county_delta_label(cp, a0, a1, a2, P(a3), (uint8_t)a4, (uint8_t)a5)); return 0; }
int rl_41a900(int a0, int a1, int a2, int a3) { CP(engine_county_year_number(cp, a0, a1, a2, a3)); return 0; }
int rl_41ab67(int a0, int a1, int a2, int a3, int a4, int a5)
{ CP(engine_county_num_units(cp, a0, a1, a2, a3, P(a4), (uint8_t)a5)); return 0; }
int rl_409397(int x, int y, int cols, int rows) { SCR(engine_county_chrome_panel(CHROME_SHEET, x, y, cols, rows)); return 0; }
/* the steps take the county as their argument */
int rl_43aa83(int county) { CP(cp->county = county; engine_county_tax_step_render(cp, 1)); return 0; }
int rl_43ab25(int county) { CP(cp->county = county; engine_county_tax_step_render(cp, -1)); return 0; }
int rl_43a23f(int county) { CP(cp->county = county; engine_county_wanted_step_render(cp, 1)); return 0; }
int rl_43a307(int county) { CP(cp->county = county; engine_county_wanted_step_render(cp, -1)); return 0; }
int rl_4156a7(int a0, int a1, int a2) { int r = 0; CP(r = engine_county_graph_render(cp, a0, a1, a2)); return r; }
/* the two input handlers, whole: the press / repeat / held / released bytes DAT_004eafb4 / DAT_004eabc5 /
 * DAT_004e65cc / DAT_004e65d8, the mouse activity DAT_004ea4b0, the cursor, the spectator flag DAT_0053f684 */
int rl_438ceb(void)
{
    int r = 0;
    SCR(const uint8_t *rec = COUNTY(s_slot_0057c93c);
        engine_county_grid_in_t in = { g_bf_4eafb4, (int)g_bf_4e65fc, (int)g_bf_4e65f4, rec[5], (int)s_slot_0057c8cc,
                                       (int)g_rl_53f684 };
        r = engine_county_action_grid_click(&in, seam_county_flip));
    return r;
}
int rl_43a379(void)
{
    int r = 0;
    CP(const uint8_t *rec = COUNTY(s_slot_0057c93c);
       engine_county_rations_in_t in = { g_bf_4e65d8, g_bf_4eafb4, g_bf_4eabc5, g_bf_4e65cc, (int)g_bf_4ea4b0,
                                         (int)g_bf_4e65fc, (int)g_bf_4e65f4, rec[5], (int)s_slot_0057c8cc, (int8_t)rec[0x15f] };
       r = engine_county_rations_click(cp, &in, &D591550, (int32_t *)(void *)g_bf_59154c, g_sim_mp_active,
                                       seam_county_mp_ration));
    return r;
}

/* ---------------------------------------------------------------- the battlefield renderer (its state: the module's) */
int rl_4bc142(int a0, int a1) { SCR(engine_battlefield_render_frame(a0, a1)); return 0; }
int rl_4bc8fc(void) { SCR(engine_battlefield_render_terrain_zoom16()); return 0; }
int rl_4bcbdc(void) { SCR(engine_battlefield_render_terrain_zoom32()); return 0; }
int rl_4bcbd1(void) { SCR(engine_battlefield_render_overlay_zoom16()); return 0; }
int rl_4bd07f(void) { SCR(engine_battlefield_render_overlay_zoom32()); return 0; }
int rl_4bd355(void) { SCR(engine_battlefield_render_middle()); return 0; }
int rl_4bd938(void) { SCR(engine_battlefield_render_build_list()); return 0; }
int rl_4bda92(void) { SCR(engine_battlefield_render_sort_list()); return 0; }
int rl_4bdb95(void) { SCR(engine_battlefield_render_render_list()); return 0; }
int rl_4bdc31(int a0) { SCR(engine_battlefield_render_entity(a0)); return 0; }
int rl_4be4df(int a0) { SCR(engine_battlefield_render_entity_type6(a0)); return 0; }
int rl_4be7be(int a0) { SCR(engine_battlefield_render_entity_type7(a0)); return 0; }
int rl_4beab9(int a0) { SCR(engine_battlefield_render_entity_type9e(a0)); return 0; }
int rl_4bd574(void) { SCR(engine_battlefield_render_player_marker()); return 0; }
int rl_4bd759(int a0) { SCR(engine_battlefield_render_unit_key(a0)); return 0; }
int rl_4beed4(int a0) { SCR(engine_battlefield_render_effect_chain(a0)); return 0; }
int rl_4bd233(void) { SCR(engine_battlefield_render_late()); return 0; }
int rl_4be070(int a0) { SCR(engine_battlefield_render_creature(a0)); return 0; }
int rl_4be33f(void) { SCR(engine_battlefield_render_selection_marker()); return 0; }
int rl_4bf1f9(void) { SCR(engine_battlefield_render_final()); return 0; }
int rl_4bc51a(int a0, int a1) { SCR(engine_battlefield_render_minimap_strip(a0, a1)); return 0; }
int rl_4bc1d1(int a0) { SCR(engine_battlefield_render_minimap_scroll(a0)); return 0; }
int rl_4bc107(int a0, int a1, int a2, int a3, int a4, int a5)
{ engine_battlefield_render_bind_minimap_context(P(a0), P(a1), P(a2), a3, a4, a5); return 0; }
int rl_4bc222(int a0, int a1) { return engine_battlefield_render_screen_in_grid(a0, a1); }
int rl_4bc2a5(int a0) { return engine_battlefield_render_pixel_to_col(a0); }
int rl_4bc2c5(int a0) { return engine_battlefield_render_pixel_to_row(a0); }
int rl_4bc2e5(int a0) { return engine_battlefield_render_pixel_to_col_right_biased(a0); }
int rl_4bc346(int a0) { return engine_battlefield_render_pixel_to_col_left_biased(a0); }
int rl_4bc3ac(int a0) { return engine_battlefield_render_pixel_to_row_bottom_biased(a0); }
int rl_4bc40d(int a0) { return engine_battlefield_render_pixel_to_row_top_biased(a0); }
int rl_4bc471(int a0, int a1) { return engine_battlefield_render_col_delta(a0, a1); }
int rl_4bc4bb(int a0, int a1) { return engine_battlefield_render_row_delta(a0, a1); }
int rl_4bc505(void) { return engine_battlefield_render_get_zoom(); }

/* ---------------------------------------------------------------- engine_trade: the context over retail's */
static engine_trade_ctx_t s_tr;
static engine_trade_ctx_t *tr_ctx(void)
{
    s_tr.county_block = g_per_army_768; s_tr.strategic_block = g_player_strategic_352;
    s_tr.territory_block = g_per_territory_420; s_tr.merchant_offers = s_slot_005532a0;
    s_tr.county = (int)s_slot_0057c93c; s_tr.trade_county = (int)s_slot_00553c64;
    s_tr.local_player = (int)s_slot_0057c8cc; s_tr.good = (int)s_slot_0056d8ac;
    s_tr.chrome = CHROME_SHEET; s_tr.system_sheet = SYSTEM_SHEET; s_tr.title_font = FONT_TALL; s_tr.menu_font = FONT_DEFAULT;
    s_tr.glyph_table = GLYPH_TABLE; s_tr.glyph_metrics = METRICS_TABLE; s_tr.loc = loc_load();
    s_tr.scratch = CUR_BLOB; s_tr.misc_sheet = MISC_SHEET; s_tr.dirty = engine_territory_dirty_state();
    return &s_tr;
}
#define TRD(...) SCR(engine_trade_ctx_t *tr = tr_ctx(); __VA_ARGS__; loc_store())
int rl_416308(void) { TRD(g_trade_needs_compute = 1; engine_trade_render(tr)); return 0; }   /* always computes */
int rl_41635f(void) { TRD(engine_trade_content(tr)); return 0; }

/* ---------------------------------------------------------------- engine_armory: the context over retail's */
static void seam_arm_palette_prep(void) { SCR_OUT(rl_4b11ce()); }
static void seam_arm_teardown(void) { SCR_OUT(rl_4996c8()); }
static void seam_arm_input_tick(int arg) { SCR_OUT(rl_452160(arg)); }
static void seam_arm_palette_apply(const uint8_t *pal) { SCR_OUT(rl_4b0ab5((int)(uintptr_t)pal)); }
static engine_armory_retail_t s_arm_retail;
static engine_armory_ctx_t s_ar;
static engine_armory_ctx_t *ar_ctx(void)
{
    s_arm_retail = (engine_armory_retail_t){ seam_arm_palette_prep, seam_arm_teardown, seam_arm_input_tick,
                                             seam_arm_palette_apply, g_rl_4ea8a0, (const char *)g_rl_4d2c88 };
    s_ar.strategic_block = g_player_strategic_352; s_ar.troop_pool = g_rl_53f6a0;
    s_ar.local_player = (int)s_slot_0057c8cc; s_ar.county = (int)s_slot_0057c93c;
    s_ar.county_rec = COUNTY(s_slot_0057c93c); s_ar.map_id = (int)s_slot_0053f034;
    s_ar.unit_table = g_unit_table_20; s_ar.system_sheet = SYSTEM_SHEET; s_ar.chrome = CHROME_SHEET;
    s_ar.menu_font = FONT_DEFAULT; s_ar.title_font = FONT_TALL; s_ar.glyph_table = GLYPH_TABLE;
    s_ar.glyph_metrics = METRICS_TABLE; s_ar.loc = loc_load();
    s_ar.open_arg = 0; s_ar.retail = &s_arm_retail;
    return &s_ar;
}
#define AR(...) SCR(engine_armory_ctx_t *ar = ar_ctx(); __VA_ARGS__; loc_store())
int rl_4181eb(void) { AR(engine_armory_counts_render(ar)); return 0; }
int rl_418426(void) { AR(engine_armory_racks(ar)); return 0; }
int rl_419243(void) { SCR(engine_armory_refresh((int)g_rl_5aea54)); return 0; }   /* the torch frame DAT_005aea54 */
int rl_418fc5(void) { SCR(engine_armory_march_restore()); return 0; }
int rl_4190db(void) { SCR(engine_armory_march_step((int)g_rl_58fcb0, (int)g_rl_591510)); return 0; }   /* the 20 / 80 ms ticks */
/* FUN_004aa90a(county, population): the pool row is the county's OWNER's */
int rl_4aa90a(int county, int population)
{
    SCR(int owner = COUNTY(county)[5];
        engine_armory_muster_init(owner, population, g_player_strategic_352 + (size_t)owner * 0x160));
    return 0;
}
int rl_41ac3e(int a0, int a1, int a2, int a3, int a4, int a5)
{ AR(engine_armory_unit_noun(ar, a0, a1, a2, a3, P(a4), (uint8_t)a5)); return 0; }
int rl_418653(void) { AR(engine_armory_overlay_render(ar)); return 0; }
int rl_435ebc(int county, int n) { engine_armory_conscript_update(COUNTY(county), n); return 0; }
/* FUN_00435cef: the cursor DAT_004e65fc / DAT_004e65f4, the press / repeat / held bytes */
int rl_435cef(void)
{
    int r = 0;
    SCR(r = engine_armory_slider_click(COUNTY(s_slot_0057c93c), (int)g_bf_4e65fc, (int)g_bf_4e65f4, g_bf_4eafb4,
                                       g_bf_4eabc5, g_bf_4e65cc));
    return r;
}
int rl_417ea7(int arg) { AR(ar->open_arg = arg; engine_armory_render(ar)); return 0; }

/* ---------------------------------------------------------------- engine_labor: the context over retail's */
static engine_labor_ctx_t s_lb;
static engine_labor_ctx_t *lb_ctx(void)
{
    s_lb.county_block = g_per_army_768; s_lb.county = (int)s_slot_0057c93c;
    s_lb.advanced_mode = (int)s_slot_0053f25c; s_lb.season = (int)s_slot_0057c92c;
    s_lb.chrome = CHROME_SHEET; s_lb.misc_sheet = MISC_SHEET; s_lb.system_sheet = SYSTEM_SHEET;
    s_lb.title_font = FONT_TALL; s_lb.menu_font = FONT_DEFAULT; s_lb.glyph_table = GLYPH_TABLE;
    s_lb.glyph_metrics = METRICS_TABLE; s_lb.loc = loc_load();
    return &s_lb;
}
/* FUN_0041dedb(county, x, y, row) */
int rl_41dedb(int a0, int a1, int a2, int a3)
{ SCR(engine_labor_ctx_t *lb = lb_ctx(); engine_labor_castle_block(lb, a0, a1, a2, a3); loc_store()); return 0; }

/* ---------------------------------------------------------------- engine_treasury: the context over retail's */
int rl_416925(void)
{
    SCR(static engine_treasury_ctx_t ts;
        ts.strategic_block = g_player_strategic_352; ts.realm_names = REALM_NAMES;
        ts.local_player = (int)s_slot_0057c8cc; ts.chrome = CHROME_SHEET; ts.misc_sheet = MISC_SHEET;
        ts.system_sheet = SYSTEM_SHEET; ts.title_font = FONT_TALL; ts.menu_font = FONT_DEFAULT;
        ts.glyph_table = GLYPH_TABLE; ts.glyph_metrics = METRICS_TABLE; ts.loc = loc_load();
        ts.dirty = engine_territory_dirty_state();
        engine_treasury_panel(&ts); loc_store());
    return 0;
}
