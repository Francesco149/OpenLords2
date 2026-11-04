/* rh_hooks_gfx.c — the port's gfx layer (src/gfx_paint / glyph / sprite / sprite_rle / dispatch / line / sprite_render /
 * panel / text / text_label / wrap / caret) and the first renderers above it (engine_hud_menubar's strip + labels,
 * engine_button's strip) vs retail, on identical state AND identical pixels (rh_hooks.h contract; shared plumbing in
 * rh_port.h / rh_hooks_ai.c). Write-up: docs/findings/render-under-rhost.md.
 *
 * BINDING.  The gfx modules keep retail's render globals in src/gfx_globals.c (not linked): aliased below at their
 * retail VAs, so the port reads and writes retail's own memory.  The canvas is three of them — DAT_004ea1a8 (Buf, the
 * back buffer's lpSurface FUN_0042ec91 caches), DAT_004eb274 (xRight, pitch) and DAT_004eac4c (yBottom) — so the
 * port draws into retail's LIVE back buffer (a surface in rhost's media pool, rh_ddraw.c).  The port's own bindings
 * (not retail globals): the default / tall font blobs retail compares by address (&DAT_005af8f0, &DAT_005b2fa0) and
 * the sprite renderers' error log (retail FUN_004afab9).
 *
 * MEDIA.  Every drawing row carries RH_SH_MEDIA: its shadow also rolls back and compares the pixels of every live
 * surface (rh_state.c), so a render divergence is reported AT the call with its first differing pixel
 * ("px(X,Y)@VA rRR/pPP"); the hybrid pass checks every presented frame on top. The hot leaves carry RH_SH_SAMPLE
 * (a shadow costs ~400 us, a blit ~1 us): per (row, caller, screen) the first 64 calls and every power-of-two-th.
 *
 * STATE THE PORT MEASURES / WALKS.  The measurers' cache (DAT_005c9288 / DAT_0058fd88 / DAT_005c9a7c) is aliased like
 * the rest; the caret's editor (DAT_005cd550 / DAT_005bb4a8 / DAT_005c9280), the localized-text walker (blob
 * &DAT_00591580, pointer DAT_005c9270) and the dirty rectangle (FUN_004527a6's six globals) are port structs loaded
 * from / stored to retail's globals around the call.
 *
 * Widths follow retail's asm (gfx_globals.h): the glyph width DAT_005c9258 and the descriptor height DAT_005aeb70 are
 * words, the clip modes DAT_005cdd20 / DAT_0058fe18 bytes, the color DAT_0057d3bc a dword. */
#include "rh_port.h"
#include "gfx_globals.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_sprite.h"
#include "gfx_sprite_rle.h"
#include "gfx_dispatch.h"
#include "gfx_line.h"
#include "gfx_sprite_render.h"
#include "gfx_panel.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
#include "gfx_wrap.h"
#include "gfx_caret.h"
#include "text_input.h"
#include "localization.h"
#include "font_metrics.h"
#include "engine_hud_menubar.h"
#include "engine_button.h"
#include "engine_dirty_rect.h"

/* ---------------------------------------------------------------- binding: gfx globals = retail memory */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(
/* gfx_paint */
A(g_gfx_canvas_pixels,0x4ea1a8) A(g_gfx_canvas_width,0x4eb274) A(g_gfx_canvas_height,0x4eac4c) A(g_gfx_darken_remap,0x4eac60)
/* gfx_glyph */
A(g_gfx_color,0x57d3bc) A(g_gfx_cursor_x,0x591524) A(g_gfx_cursor_y,0x591528) A(g_gfx_glyph_offset,0x58fe04)
A(g_gfx_saved_cursor_x,0x591508) A(g_gfx_saved_cursor_y,0x591504)
A(g_gfx_glyph_w,0x5c9258) A(g_gfx_glyph_h,0x5bb478) A(g_gfx_clipped_w,0x591514) A(g_gfx_src_stride_pad,0x59150c)
A(g_gfx_dst_stride_pad,0x5cd400) A(g_gfx_color_modulation,0x5cd40c)
/* gfx_dispatch */
A(g_gfx_alt_clip_enabled,0x5aeb84) A(g_gfx_alt_clip_left,0x52afdc) A(g_gfx_alt_clip_bottom,0x553254)
A(g_gfx_last_glyph_index,0x5c9288) A(g_gfx_last_glyph_offset,0x58fd88) A(g_gfx_glyph_height_short,0x5aeb70) A(g_gfx_flag_a,0x591568) A(g_gfx_flag_b,0x57d3c0)
A(g_gfx_x_clip_mode,0x5cdd20) A(g_gfx_y_clip_mode,0x58fe18) A(g_gfx_left_skip,0x58fd50) A(g_gfx_top_skip,0x58feac)
A(g_gfx_right_visible_w,0x58fe98) A(g_gfx_bottom_visible_h,0x5aeb50)
/* gfx_line */
A(g_gfx_abs_dx,0x5aeb40) A(g_gfx_abs_dy,0x5aeb44) A(g_gfx_x_end,0x5aeb48) A(g_gfx_y_end,0x5aeb4c) A(g_gfx_x_step,0x5aeb54)
A(g_gfx_y_step,0x5aeb58) A(g_gfx_x_start,0x5aeb5c) A(g_gfx_y_start,0x5aeb60) A(g_gfx_bres_err,0x58fd5c)
/* gfx_text */
A(g_gfx_no_shadow,0x5aea40) A(g_gfx_outline,0x58fe9c) A(g_gfx_tight_shadow,0x5aeb90) A(g_gfx_uppercase_highlight,0x58fe2c)
A(g_gfx_collapse_spaces,0x5aeb7c) A(g_gfx_screen_byte,0x4eac50) A(g_gfx_substitution_buf,0x5bb4d0)
A(g_gfx_substitution_idx,0x5bb4b4) A(g_gfx_substitution_on,0x5bb4c4) A(g_gfx_collapse_active,0x5c925a)
A(g_gfx_first_pos_target,0x5bb4a8) A(g_gfx_first_pos_latch,0x5aeb64) A(g_gfx_first_pos_x,0x58fd58)
A(g_gfx_first_pos_y,0x58fd54) A(g_gfx_line_x_acc,0x5cd404) A(g_gfx_line_y_acc,0x5cd4f8) A(g_gfx_char_counter,0x58fd78)
/* gfx_text_label, gfx_wrap */
A(g_gfx_format_buffer,0x58fda0) A(g_gfx_max_line_x,0x5aeb88) A(g_gfx_word_chars,0x5c9a7c) A(g_gfx_wrap_large_buf,0x5c92a0) A(g_gfx_wrap_small_buf,0x5aea60)
/* gfx_sprite_render, gfx_caret */
A(g_gfx_current_blob,0x4eabec) A(g_gfx_last_error,0x5aeb68) A(g_gfx_seal_x,0x55ce78) A(g_gfx_seal_y,0x57c8a0)
A(g_gfx_seal_mode,0x56956c) A(g_gfx_caret_blink,0x4e5998)
A(g_gfx_menu_redraw_a,0x4eb260) A(g_gfx_menu_redraw_b,0x4e65f8)
);
#undef A

/* ---------------------------------------------------------------- the port's own bindings */
/* the sprite renderers' / panel passes' error log → retail FUN_004afab9(fmt, 0, n) (unported: status.txt) */
static void seam_log(uint32_t fmt, int n)
{ rh_port_retail_enter(); ((void (__cdecl *)(uint32_t, int, int))(uintptr_t)0x4afab9u)(fmt, 0, n); rh_port_retail_leave(); }
/* the button strip's dirty rectangle: the port's state over retail's six globals (FUN_004527a6's), loaded / stored
 * around the call and around the pressed path's retail mark FUN_00452775 */
static engine_dirty_rect_state_t s_bd;
static engine_dirty_rect_state_t *bd_load(void)
{
    s_bd.dirty_mark = (int)G32(0x50d09cu); s_bd.dirty_xmin = (int)G32(0x50a4c4u); s_bd.dirty_xmax = (int)G32(0x50a4c8u);
    s_bd.dirty_ymin = (int)G32(0x50a4bcu); s_bd.dirty_ymax = (int)G32(0x50a4c0u); s_bd.below_status_redraw = (int)G32(0x53e9acu);
    return &s_bd;
}
static void bd_store(void)
{
    PUT(0x50d09cu, s_bd.dirty_mark); PUT(0x50a4c4u, s_bd.dirty_xmin); PUT(0x50a4c8u, s_bd.dirty_xmax);
    PUT(0x50a4bcu, s_bd.dirty_ymin); PUT(0x50a4c0u, s_bd.dirty_ymax); PUT(0x53e9acu, s_bd.below_status_redraw);
}
static void seam_pressed_mark(int x, int y, int w, int h, int force)   /* FUN_00452775 (unported) */
{
    bd_store(); rh_port_retail_enter();
    ((void (__cdecl *)(int, int, int, int, int))(uintptr_t)0x452775u)(x, y, w, h, force);
    rh_port_retail_leave(); bd_load();
}
static void gfx_bind(void)
{
    static int done;
    if (done) return;
    done = 1;
    gfx_dispatch_set_default_font((const uint8_t *)(uintptr_t)0x5af8f0u);   /* &DAT_005af8f0: the default font blob */
    gfx_sprite_render_set_log(seam_log);
    gfx_wrap_set_tall_font_marker((const uint8_t *)(uintptr_t)0x5b2fa0u);   /* &DAT_005b2fa0: the tall (24 px) font blob */
    engine_button_bind_pressed_mark(seam_pressed_mark);
}
__attribute__((constructor)) static void gfx_bind_register(void) { rh_port_register_bind(gfx_bind); }
#define G(call) do { gfx_bind(); call; } while (0)
#define P(k) ((const uint8_t *)(uintptr_t)a[k])
#define C(k) ((uint8_t)a[k])
#define PAL ((const uint8_t *)(uintptr_t)0x4ea1b0u)    /* DAT_004ea1b0: the palette mirror, 256 × RGB */
#define GLYPH_TABLE ((const uint8_t *)(uintptr_t)0x4d71f0u)   /* DAT_004d71f0: char → 1-based glyph (DAT_004d71d0 + 0x20) */
#define METRICS_TABLE ((const unsigned char *)(uintptr_t)0x4d71d0u)   /* DAT_004d71d0: the measurers' char → glyph table */
#define S(k) ((const char *)(uintptr_t)a[k])
#define U(k) ((const uint8_t *)(uintptr_t)a[k])
/* the localized-text walker: the port's localization_state_t over retail's blob &DAT_00591580 (l2_eng.dat) and walker
 * pointer DAT_005c9270, loaded / stored around the call */
static localization_state_t s_loc;
static localization_state_t *loc_load(void)
{ s_loc.blob = (unsigned char *)(uintptr_t)0x591580u; s_loc.walker_offset = (size_t)(G32(0x5c9270u) - 0x591580u); return &s_loc; }
static void loc_store(void) { PUT(0x5c9270u, 0x591580u + (uint32_t)s_loc.walker_offset); }
#define LOC(call) do { gfx_bind(); localization_state_t *loc = loc_load(); call; loc_store(); } while (0)
/* the caret: the port's editor / measurer over retail's (buf DAT_005cd550, cursor DAT_005bb4a8, insert DAT_005c9280;
 * the measurer cache DAT_005c9288 / DAT_0058fd88) */
static void caret(const uint8_t *font, uint8_t color)
{
    static text_input_state_t ed;
    int cur = (int)G32(0x5bb4a8u);
    ed.cursor = cur; ed.insert_mode = G8(0x5c9280u);
    if (cur >= 0 && cur < TEXT_INPUT_BUF_SIZE) ed.buf[cur] = G8(0x5cd550u + (uint32_t)cur);
    font_metrics_state_t fm = { .char_glyph_index = (const unsigned char *)(uintptr_t)0x4d71d0u,
                                .last_glyph_index = (int)G32(0x5c9288u), .last_glyph_offset = (int)G32(0x58fd88u) };
    gfx_caret_render(&ed, &fm, font, color);
    PUT(0x5c9288u, fm.last_glyph_index); PUT(0x58fd88u, fm.last_glyph_offset);
}
#define SYSTEM_SHEET ((const uint8_t *)(uintptr_t)0x5bb540u)  /* &DAT_005bb540: system2.pl8 (the seal) */
#define CHROME_SHEET ((const uint8_t *)(uintptr_t)0x57d3d0u)  /* &DAT_0057d3d0: the chrome sheet the panel twins bake in */

/* ---------------------------------------------------------------- FUN_004b432e: the blob in EAX
 * The opaque blit reads its blob from EAX (`mov esi, [ebp+8]; mov esi, eax`); every call site loads EAX with the
 * value it pushes. The retail side runs through this thunk, which restores that (EAX = [esp+4]). */
uint32_t rh_gfx_opaque_entry;   /* the row's retail entry (va, or the entry-patch trampoline) */
static __attribute__((naked, used)) void opaque_retail(void)
{ __asm__("movl 4(%esp), %eax\n\tjmp *_rh_gfx_opaque_entry"); }

#define RH_TABLE RH_HOOKS_GFX
extern hook_t RH_HOOKS_GFX[];

/* row indices (must follow RH_HOOKS_GFX's order) */
enum {
    X_gfx_paint_pixel, X_gfx_paint_2x1, X_gfx_paint_2x1_under, X_gfx_paint_pixel_clipped, X_gfx_paint_2x1_clipped,
    X_gfx_paint_darken_rect,
    X_gfx_glyph_blit, X_gfx_glyph_blit_left, X_gfx_glyph_blit_right,
    X_gfx_sprite_blit, X_gfx_sprite_blit_left, X_gfx_sprite_blit_right, X_gfx_sprite_fill_block, X_gfx_sprite_opaque,
    X_gfx_sprite_raw16, X_gfx_sprite_raw24, X_gfx_sprite_raw32, X_gfx_sprite_raw2,
    X_gfx_rle_blit, X_gfx_rle_blit_left, X_gfx_rle_blit_right,
    X_gfx_clip_x, X_gfx_clip_y, X_gfx_dispatch_glyph, X_gfx_dispatch_sprite, X_gfx_dispatch_sprite_clipped,
    X_gfx_line_draw, X_gfx_line_dotted, X_gfx_line_rect, X_gfx_line_fill_rect,
    X_gfx_bevel_dark_br, X_gfx_bevel_palette_b, X_gfx_bevel_dark_tl,
    X_gfx_render_raw, X_gfx_render_from_blob, X_gfx_render_rle_from_blob, X_gfx_render_opaque_from_blob, X_gfx_seal_draw,
    X_gfx_render, X_gfx_render_trunc_bottom, X_gfx_render_trunc_top,
    X_gfx_panel_border, X_gfx_panel_fill, X_gfx_panel_draw, X_gfx_panel_border_chrome, X_gfx_panel_fill_chrome,
    X_gfx_panel_bevel,
    X_gfx_text_line, X_gfx_text_line_centered, X_gfx_fmt_dec, X_gfx_fmt_hex, X_gfx_label_dec, X_gfx_label_hex,
    X_gfx_label_dec_centered, X_gfx_label_dec_signed, X_gfx_label_labeled_signed,
    X_gfx_wrap_line, X_gfx_wrap_line_centered, X_gfx_wrap_buffer, X_gfx_wrap_localized, X_gfx_caret,
    X_hud_menubar_tile_strip, X_button_render_list, X_hud_menubar_labels,
};

/* gfx_paint.c */
HOOK_V(gfx_paint_pixel,          3, gfx_paint_pixel(I(0), I(1), C(2)))
HOOK_V(gfx_paint_2x1,            3, gfx_paint_2x1(I(0), I(1), C(2)))
HOOK_V(gfx_paint_2x1_under,      3, gfx_paint_2x1_under(I(0), I(1), C(2)))
HOOK_V(gfx_paint_pixel_clipped,  3, gfx_paint_pixel_clipped(I(0), I(1), C(2)))
HOOK_V(gfx_paint_2x1_clipped,    3, gfx_paint_2x1_clipped(I(0), I(1), C(2)))
HOOK_V(gfx_paint_darken_rect,    4, gfx_paint_darken_rect(PAL, I(0), I(1), I(2), I(3)))
/* gfx_glyph.c */
HOOK_V(gfx_glyph_blit,           1, gfx_glyph_blit(P(0)))
HOOK_V(gfx_glyph_blit_left,      1, gfx_glyph_blit_left_clipped(P(0)))
HOOK_V(gfx_glyph_blit_right,     1, gfx_glyph_blit_right_clipped(P(0)))
/* gfx_sprite.c */
HOOK_V(gfx_sprite_blit,          1, gfx_sprite_blit(P(0)))
HOOK_V(gfx_sprite_blit_left,     1, gfx_sprite_blit_left_clipped(P(0)))
HOOK_V(gfx_sprite_blit_right,    1, gfx_sprite_blit_right_clipped(P(0)))
HOOK_V(gfx_sprite_fill_block,    3, gfx_sprite_fill_block(I(0), I(1), C(2)))
/* gfx_sprite_opaque: the generic body, with the retail side routed through opaque_retail (EAX = the blob) */
static uint32_t p_gfx_sprite_opaque(const uint32_t *a) { gfx_sprite_blit_opaque(P(0)); return 0; }
static void w_gfx_sprite_opaque(void);
static __attribute__((used, noinline)) uint32_t __cdecl b_gfx_sprite_opaque(uint32_t e0)
{
    hook_t *h = &RH_TABLE[X_gfx_sprite_opaque];
    if (h->entry != (uint32_t)(uintptr_t)opaque_retail) { rh_gfx_opaque_entry = h->entry; h->entry = (uint32_t)(uintptr_t)opaque_retail; }
    return rh_port_run(h, (void *)w_gfx_sprite_opaque, p_gfx_sprite_opaque, 0, e0);
}
RH_ENTRY(w_gfx_sprite_opaque, b_gfx_sprite_opaque)
HOOK_V(gfx_sprite_raw16,         1, gfx_sprite_blit_16x16_raw(P(0)))
HOOK_V(gfx_sprite_raw24,         1, gfx_sprite_blit_24x24_raw(P(0)))
HOOK_V(gfx_sprite_raw32,         1, gfx_sprite_blit_32x32_raw(P(0)))
HOOK_V(gfx_sprite_raw2,          1, gfx_sprite_blit_2x2_raw(P(0)))
/* gfx_sprite_rle.c */
HOOK_V(gfx_rle_blit,             1, gfx_sprite_rle_blit(P(0)))
HOOK_V(gfx_rle_blit_left,        1, gfx_sprite_rle_blit_left_clipped(P(0)))
HOOK_V(gfx_rle_blit_right,       1, gfx_sprite_rle_blit_right_clipped(P(0)))
/* gfx_dispatch.c */
HOOK_V(gfx_clip_x,               2, gfx_dispatch_clip_x(I(0), I(1)))
HOOK_V(gfx_clip_y,               2, gfx_dispatch_clip_y(I(0), I(1)))
HOOK_R(gfx_dispatch_glyph,       2, (gfx_bind(), gfx_dispatch_glyph(P(0), GLYPH_TABLE, C(1))))
HOOK_V(gfx_dispatch_sprite,      4, gfx_dispatch_sprite(P(0), I(1), I(2), I(3)))
HOOK_V(gfx_dispatch_sprite_clipped, 8, gfx_dispatch_sprite_clipped(P(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7)))
/* gfx_line.c */
HOOK_V(gfx_line_draw,            5, gfx_line_draw(I(0), I(1), I(2), I(3), C(4)))
HOOK_V(gfx_line_dotted,          5, gfx_line_draw_dotted(I(0), I(1), I(2), I(3), C(4)))
HOOK_V(gfx_line_rect,            5, gfx_line_rect(I(0), I(1), I(2), I(3), C(4)))
HOOK_V(gfx_line_fill_rect,       5, gfx_line_fill_rect(I(0), I(1), I(2), I(3), C(4)))
HOOK_V(gfx_bevel_dark_br,        4, gfx_line_rect_bevel_dark_br(I(0), I(1), I(2), I(3)))
HOOK_V(gfx_bevel_palette_b,      4, gfx_line_rect_bevel_palette_b(I(0), I(1), I(2), I(3)))
HOOK_V(gfx_bevel_dark_tl,        4, gfx_line_rect_bevel_dark_tl(I(0), I(1), I(2), I(3)))
/* gfx_sprite_render.c (the error log: FUN_004afab9 through seam_log) */
HOOK_V(gfx_render_raw,           5, gfx_sprite_render_raw(P(0), I(1), I(2), (int16_t)a[3], (int16_t)a[4]))
HOOK_V(gfx_render_from_blob,     4, G(gfx_sprite_render_from_blob(P(0), I(1), I(2), I(3))))
HOOK_V(gfx_render_rle_from_blob, 4, G(gfx_sprite_render_rle_from_blob(P(0), I(1), I(2), I(3))))
HOOK_V(gfx_render_opaque_from_blob, 4, G(gfx_sprite_render_opaque_from_blob(P(0), I(1), I(2), I(3))))
HOOK_V(gfx_seal_draw,            3, gfx_sprite_seal_draw(SYSTEM_SHEET, I(0), I(1), I(2)))
HOOK_V(gfx_render,               3, G(gfx_sprite_render(I(0), I(1), I(2))))
HOOK_V(gfx_render_trunc_bottom,  4, G(gfx_sprite_render_truncate_bottom(I(0), I(1), I(2), (int16_t)a[3])))
HOOK_V(gfx_render_trunc_top,     4, G(gfx_sprite_render_truncate_top(I(0), I(1), I(2), I(3))))
/* gfx_panel.c */
HOOK_V(gfx_panel_border,         6, G(gfx_panel_draw_border(P(0), I(1), I(2), I(3), I(4), I(5))))
HOOK_V(gfx_panel_fill,           5, G(gfx_panel_fill_interior(P(0), I(1), I(2), I(3), I(4))))
HOOK_V(gfx_panel_draw,           5, G(gfx_panel_draw(P(0), I(1), I(2), I(3), I(4))))
HOOK_V(gfx_panel_border_chrome,  5, G(gfx_panel_draw_border_chrome(CHROME_SHEET, I(0), I(1), I(2), I(3), I(4))))
HOOK_V(gfx_panel_fill_chrome,    4, G(gfx_panel_fill_interior_chrome(CHROME_SHEET, I(0), I(1), I(2), I(3))))
HOOK_V(gfx_panel_bevel,          4, G(gfx_panel_draw_bevel(CHROME_SHEET, I(0), I(1), I(2), I(3))))
/* gfx_text.c, gfx_text_label.c — retail's char tables are globals: render DAT_004d71f0, measure DAT_004d71d0 */
HOOK_V(gfx_text_line,            5, G(gfx_text_render_line(U(0), I(1), I(2), P(3), GLYPH_TABLE, C(4))))
HOOK_V(gfx_text_line_centered,   6, G(gfx_text_render_line_centered(U(0), I(1), I(2), I(3), P(4), GLYPH_TABLE, METRICS_TABLE, C(5))))
HOOK_V(gfx_fmt_dec,              3, gfx_text_label_format_dec(I(0), I(1), I(2)))
HOOK_V(gfx_fmt_hex,              3, gfx_text_label_format_hex(I(0), I(1), I(2)))
HOOK_V(gfx_label_dec,            7, G(gfx_text_label_render_dec(I(0), (char)a[1], S(2), I(3), I(4), P(5), GLYPH_TABLE, C(6))))
HOOK_V(gfx_label_hex,            7, G(gfx_text_label_render_hex(I(0), (char)a[1], S(2), I(3), I(4), P(5), GLYPH_TABLE, C(6))))
HOOK_V(gfx_label_dec_centered,   8, G(gfx_text_label_render_dec_centered(I(0), (char)a[1], S(2), I(3), I(4), I(5), P(6), GLYPH_TABLE,
                                                                         METRICS_TABLE, C(7))))
HOOK_V(gfx_label_dec_signed,    10, G(gfx_text_label_render_dec_signed(I(0), I(1), (char)a[2], S(3), I(4), I(5), I(6), P(7), GLYPH_TABLE,
                                                                       METRICS_TABLE, C(8), C(9))))
HOOK_V(gfx_wrap_line,            6, LOC(gfx_wrap_localized_line(loc, I(0), I(1), I(2), I(3), P(4), GLYPH_TABLE, C(5))))
HOOK_V(gfx_wrap_line_centered,   7, LOC(gfx_wrap_localized_line_centered(loc, I(0), I(1), I(2), I(3), I(4), P(5), GLYPH_TABLE,
                                                                         METRICS_TABLE, C(6))))
HOOK_V(gfx_wrap_buffer,          6, G(gfx_wrap_buffer(U(0), I(1), I(2), I(3), P(4), GLYPH_TABLE, METRICS_TABLE, C(5))))
HOOK_V(gfx_wrap_localized,      10, LOC(gfx_wrap_localized(loc, I(0), I(1), I(2), I(3), I(4), I(5), I(6), I(7), P(8), GLYPH_TABLE,
                                                           METRICS_TABLE, C(9))))
HOOK_V(gfx_caret,                2, caret(P(0), C(1)))
/* engine_hud_menubar.c */
HOOK_V(hud_menubar_tile_strip,   4, G(engine_hud_menubar_tile_strip(CHROME_SHEET, I(0), I(1), I(2), I(3))))
/* FUN_0040c5b0: hovered = DAT_00522cb4, the default font, color 0x3f (retail's literals) */
HOOK_V(hud_menubar_labels,       2, LOC(engine_hud_menubar_labels((int16_t *)(uintptr_t)a[0], I(1), (int)G32(0x522cb4u),
                                                                  (const uint8_t *)(uintptr_t)0x5af8f0u, GLYPH_TABLE, METRICS_TABLE,
                                                                  loc, 0x3f, bd_load()); bd_store()))
/* engine_button.c — the misc sheet is the pointer DAT_005530c8, the system sheet the blob &DAT_005bb540 */
HOOK_V(button_render_list,       4, G(engine_button_render_list(I(0), I(1), (const engine_button_rec_t *)(uintptr_t)a[2], I(3),
                                                                (const uint8_t *)(uintptr_t)G32(0x5530c8u), SYSTEM_SHEET, bd_load());
                                      bd_store()))
HOOK_V(gfx_label_labeled_signed, 9, G(gfx_text_label_render_labeled_signed(I(0), I(1), U(2), S(3), I(4), I(5), P(6), GLYPH_TABLE,
                                                                           C(7), C(8))))

hook_t RH_HOOKS_GFX[] = {
    { "gfx_paint_pixel", 0x4b3f54u, (void *)w_gfx_paint_pixel, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_paint_2x1", 0x4b3f7au, (void *)w_gfx_paint_2x1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_paint_2x1_under", 0x4b3fa3u, (void *)w_gfx_paint_2x1_under, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_paint_pixel_clipped", 0x4039c7u, (void *)w_gfx_paint_pixel_clipped, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_paint_2x1_clipped", 0x403a2cu, (void *)w_gfx_paint_2x1_clipped, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_paint_darken_rect", 0x4b13edu, (void *)w_gfx_paint_darken_rect, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_glyph_blit", 0x4b41b7u, (void *)w_gfx_glyph_blit, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_glyph_blit_left", 0x4b425cu, (void *)w_gfx_glyph_blit_left, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_glyph_blit_right", 0x4b42c5u, (void *)w_gfx_glyph_blit_right, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_sprite_blit", 0x4b43b1u, (void *)w_gfx_sprite_blit, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_sprite_blit_left", 0x4b446du, (void *)w_gfx_sprite_blit_left, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_sprite_blit_right", 0x4b44d2u, (void *)w_gfx_sprite_blit_right, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_sprite_fill_block", 0x4b414au, (void *)w_gfx_sprite_fill_block, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_sprite_opaque", 0x4b432eu, (void *)w_gfx_sprite_opaque, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_sprite_raw16", 0x4b49f7u, (void *)w_gfx_sprite_raw16, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_sprite_raw24", 0x4b4cb8u, (void *)w_gfx_sprite_raw24, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_sprite_raw32", 0x4b5333u, (void *)w_gfx_sprite_raw32, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_sprite_raw2", 0x4b49b7u, (void *)w_gfx_sprite_raw2, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_rle_blit", 0x4b4537u, (void *)w_gfx_rle_blit, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_rle_blit_left", 0x4b4814u, (void *)w_gfx_rle_blit_left, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_rle_blit_right", 0x4b48b8u, (void *)w_gfx_rle_blit_right, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_clip_x", 0x40464cu, (void *)w_gfx_clip_x, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "gfx_clip_y", 0x40477bu, (void *)w_gfx_clip_y, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "gfx_dispatch_glyph", 0x402a14u, (void *)w_gfx_dispatch_glyph, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_dispatch_sprite", 0x404442u, (void *)w_gfx_dispatch_sprite, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_dispatch_sprite_clipped", 0x404547u, (void *)w_gfx_dispatch_sprite_clipped, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_line_draw", 0x403a8fu, (void *)w_gfx_line_draw, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_line_dotted", 0x403c49u, (void *)w_gfx_line_dotted, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_line_rect", 0x403cf4u, (void *)w_gfx_line_rect, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_line_fill_rect", 0x40437du, (void *)w_gfx_line_fill_rect, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_bevel_dark_br", 0x403debu, (void *)w_gfx_bevel_dark_br, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_bevel_palette_b", 0x403ee4u, (void *)w_gfx_bevel_palette_b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_bevel_dark_tl", 0x403fddu, (void *)w_gfx_bevel_dark_tl, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_render_raw", 0x40a01du, (void *)w_gfx_render_raw, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_render_from_blob", 0x40a21au, (void *)w_gfx_render_from_blob, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_render_rle_from_blob", 0x40a3d0u, (void *)w_gfx_render_rle_from_blob, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_render_opaque_from_blob", 0x40a567u, (void *)w_gfx_render_opaque_from_blob, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_seal_draw", 0x40d1bcu, (void *)w_gfx_seal_draw, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_render", 0x40a682u, (void *)w_gfx_render, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_render_trunc_bottom", 0x40a80eu, (void *)w_gfx_render_trunc_bottom, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_render_trunc_top", 0x40a9b0u, (void *)w_gfx_render_trunc_top, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_panel_border", 0x40946du, (void *)w_gfx_panel_border, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_panel_fill", 0x4097c5u, (void *)w_gfx_panel_fill, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_panel_draw", 0x409346u, (void *)w_gfx_panel_draw, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_panel_border_chrome", 0x409934u, (void *)w_gfx_panel_border_chrome, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_panel_fill_chrome", 0x409c93u, (void *)w_gfx_panel_fill_chrome, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_panel_bevel", 0x4093e0u, (void *)w_gfx_panel_bevel, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "gfx_text_line", 0x402637u, (void *)w_gfx_text_line, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_text_line_centered", 0x4025d7u, (void *)w_gfx_text_line_centered, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_fmt_dec", 0x4022bdu, (void *)w_gfx_fmt_dec, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "gfx_fmt_hex", 0x402447u, (void *)w_gfx_fmt_hex, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "gfx_label_dec", 0x402f64u, (void *)w_gfx_label_dec, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_label_hex", 0x403015u, (void *)w_gfx_label_hex, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_label_dec_centered", 0x4030c6u, (void *)w_gfx_label_dec_centered, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_label_dec_signed", 0x403190u, (void *)w_gfx_label_dec_signed, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_label_labeled_signed", 0x402e0cu, (void *)w_gfx_label_labeled_signed, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_wrap_line", 0x402d37u, (void *)w_gfx_wrap_line, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_wrap_line_centered", 0x402c5eu, (void *)w_gfx_wrap_line_centered, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_wrap_buffer", 0x40352fu, (void *)w_gfx_wrap_buffer, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_wrap_localized", 0x40328eu, (void *)w_gfx_wrap_localized, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "gfx_caret", 0x40acceu, (void *)w_gfx_caret, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "hud_menubar_tile_strip", 0x409e09u, (void *)w_hud_menubar_tile_strip, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "button_render_list", 0x40cfd2u, (void *)w_button_render_list, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
    { "hud_menubar_labels", 0x40c5b0u, (void *)w_hud_menubar_labels, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },
};
const int RH_HOOKS_GFX_N = sizeof RH_HOOKS_GFX / sizeof *RH_HOOKS_GFX;
