/*
 * OpenLords2 — the gfx layer's retail globals: the standalone's / host tests' definitions (gfx_globals.h).
 *
 * rhost does not link this file: tools/rhost/rh_hooks_gfx.c defines each symbol as an ABSOLUTE symbol at its
 * retail VA instead, so the linked gfx modules read and write retail's own memory.  Zero-initialized, as
 * retail's .bss.
 */
#include "gfx_globals.h"

uint8_t *g_gfx_canvas_pixels; /* DAT_004ea1a8 (Buf)     back-buffer base */
int32_t g_gfx_canvas_width; /* DAT_004eb274 (xRight)  row pitch AND right-clip bound */
int32_t g_gfx_canvas_height; /* DAT_004eac4c (yBottom) bottom-clip bound */
uint8_t g_gfx_darken_remap[256]; /* DAT_004eac60 the darken lookup FUN_004b13ed builds */
int32_t g_gfx_color; /* DAT_0057d3bc dword; the blitters write its low byte */
int32_t g_gfx_cursor_x; /* DAT_00591524 */
int32_t g_gfx_cursor_y; /* DAT_00591528 */
int32_t g_gfx_saved_cursor_x; /* DAT_00591508 the panel passes save / restore the cursor */
int32_t g_gfx_saved_cursor_y; /* DAT_00591504 */
int32_t g_gfx_glyph_offset; /* DAT_0058fe04 (Offset)  source offset into the blob */
int16_t g_gfx_glyph_w; /* DAT_005c9258 WORD      width (movsx) */
int32_t g_gfx_glyph_h; /* DAT_005bb478           row count */
int32_t g_gfx_clipped_w; /* DAT_00591514 */
int32_t g_gfx_src_stride_pad; /* DAT_0059150c */
int32_t g_gfx_dst_stride_pad; /* DAT_005cd400 */
int32_t g_gfx_color_modulation; /* DAT_005cd40c */
int32_t g_gfx_alt_clip_enabled; /* DAT_005aeb84 */
int32_t g_gfx_alt_clip_left; /* DAT_0052afdc */
int32_t g_gfx_alt_clip_bottom; /* DAT_00553254 */
int32_t g_gfx_last_glyph_index; /* DAT_005c9288 */
int32_t g_gfx_last_glyph_offset; /* DAT_0058fd88 the measurers' cache: last_glyph_index*16 + 8 */
int16_t g_gfx_glyph_height_short; /* DAT_005aeb70 WORD      descriptor +2 (movsx) */
uint8_t g_gfx_flag_a; /* DAT_00591568 BYTE      descriptor +0xc */
uint8_t g_gfx_flag_b; /* DAT_0057d3c0 BYTE      descriptor +0xe */
uint8_t g_gfx_x_clip_mode; /* DAT_005cdd20 BYTE */
uint8_t g_gfx_y_clip_mode; /* DAT_0058fe18 BYTE */
int32_t g_gfx_left_skip; /* DAT_0058fd50 */
int32_t g_gfx_top_skip; /* DAT_0058feac */
int32_t g_gfx_right_visible_w; /* DAT_0058fe98 */
int32_t g_gfx_bottom_visible_h; /* DAT_005aeb50 */
int32_t g_gfx_abs_dx; /* DAT_005aeb40 */
int32_t g_gfx_abs_dy; /* DAT_005aeb44 */
int32_t g_gfx_x_end; /* DAT_005aeb48 */
int32_t g_gfx_y_end; /* DAT_005aeb4c */
int32_t g_gfx_x_step; /* DAT_005aeb54 */
int32_t g_gfx_y_step; /* DAT_005aeb58 */
int32_t g_gfx_x_start; /* DAT_005aeb5c */
int32_t g_gfx_y_start; /* DAT_005aeb60 */
int32_t g_gfx_bres_err; /* DAT_0058fd5c */
int32_t g_gfx_no_shadow; /* DAT_005aea40 */
int32_t g_gfx_outline; /* DAT_0058fe9c */
int32_t g_gfx_tight_shadow; /* DAT_005aeb90 */
int32_t g_gfx_uppercase_highlight; /* DAT_0058fe2c */
uint8_t g_gfx_collapse_spaces; /* DAT_005aeb7c BYTE */
uint8_t g_gfx_screen_byte; /* DAT_004eac50 BYTE  current screen id */
uint8_t g_gfx_substitution_buf[GFX_TEXT_SUBSTITUTION_BUF_LEN]; /* DAT_005bb4d0 */
int32_t g_gfx_substitution_idx; /* DAT_005bb4b4 */
int32_t g_gfx_substitution_on; /* DAT_005bb4c4 */
uint8_t g_gfx_collapse_active; /* DAT_005c925a BYTE */
int32_t g_gfx_first_pos_target; /* DAT_005bb4a8 (also the text editor's cursor) */
int32_t g_gfx_first_pos_latch; /* DAT_005aeb64 */
int32_t g_gfx_first_pos_x; /* DAT_0058fd58 */
int32_t g_gfx_first_pos_y; /* DAT_0058fd54 */
int32_t g_gfx_line_x_acc; /* DAT_005cd404 */
int32_t g_gfx_line_y_acc; /* DAT_005cd4f8 */
int32_t g_gfx_char_counter; /* DAT_0058fd78 */
uint8_t g_gfx_format_buffer[GFX_TEXT_LABEL_BUFFER_LEN]; /* DAT_0058fda0 */
int32_t g_gfx_max_line_x; /* DAT_005aeb88 */
int32_t g_gfx_word_chars; /* DAT_005c9a7c the word measurer's consumed-byte count */
uint8_t g_gfx_wrap_large_buf[GFX_WRAP_LARGE_LINE_BUF_LEN]; /* DAT_005c92a0 */
uint8_t g_gfx_wrap_small_buf[GFX_WRAP_SMALL_LINE_BUF_LEN]; /* DAT_005aea60 */
const uint8_t *g_gfx_current_blob; /* DAT_004eabec  the bound sprite sheet */
uint8_t g_gfx_last_error; /* DAT_005aeb68 BYTE  error latch (FUN_00408fb0) */
int32_t g_gfx_seal_x; /* DAT_0055ce78 */
int32_t g_gfx_seal_y; /* DAT_0057c8a0 */
int32_t g_gfx_seal_mode; /* DAT_0056956c */
uint8_t g_gfx_menu_redraw_a; /* DAT_004eb260 BYTE */
uint8_t g_gfx_menu_redraw_b; /* DAT_004e65f8 BYTE */
int32_t g_gfx_caret_blink; /* DAT_004e5998 */
