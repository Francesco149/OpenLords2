/*
 * OpenLords2 — the gfx layer's retail globals (definitions: gfx_globals.c).
 *
 * Retail keeps the render state in scattered .data / .bss globals: the canvas (back-buffer pointer, pitch,
 * height), the staged glyph / sprite (cursor, descriptor fields, clip results), the Bresenham registers, the
 * text renderer's mode flags / accumulators / substitution stream, the label formatter's buffer.  The port's
 * gfx modules (gfx_paint / glyph / dispatch / sprite / sprite_rle / line / text / text_label / wrap /
 * sprite_render / caret) read and write THESE non-static globals, one per retail global, so rhost can alias
 * each one at its retail VA (tools/rhost/rh_hooks_gfx.c) and run the gfx code inside retail on retail's own
 * state — the canvas then IS retail's live back buffer.  gfx_globals.c is the standalone's (and the host
 * tests') definition; rhost does not link it (build.sh never links *_globals.c).
 *
 * Types follow retail's access width (the asm, docs/findings/render-under-rhost.md): a word global is int16_t
 * (retail reads it with movsx), a byte global uint8_t, everything else a dword.  Neighbouring globals are
 * distinct retail variables (e.g. the word glyph width DAT_005c9258 and the byte collapse flag DAT_005c925a),
 * so a wider port type would clobber its neighbour once aliased.
 */
#ifndef OPENLORDS2_GFX_GLOBALS_H
#define OPENLORDS2_GFX_GLOBALS_H

#include <stdint.h>

/* gfx_paint — the canvas (retail FUN_0042ec91 stores the Lock()ed back buffer's lpSurface in Buf) */
extern uint8_t *g_gfx_canvas_pixels;      /* DAT_004ea1a8 (Buf)     back-buffer base                    */
extern int32_t  g_gfx_canvas_width;       /* DAT_004eb274 (xRight)  row pitch AND right-clip bound      */
extern int32_t  g_gfx_canvas_height;      /* DAT_004eac4c (yBottom) bottom-clip bound                   */
extern uint8_t  g_gfx_darken_remap[256];  /* DAT_004eac60 the darken lookup FUN_004b13ed builds         */

/* gfx_glyph — the staged glyph / sprite the blitters consume */
extern int32_t  g_gfx_color;              /* DAT_0057d3bc dword; the blitters write its low byte        */
extern int32_t  g_gfx_cursor_x;           /* DAT_00591524                                               */
extern int32_t  g_gfx_cursor_y;           /* DAT_00591528                                               */
extern int32_t  g_gfx_saved_cursor_x;     /* DAT_00591508  the panel passes save / restore the cursor   */
extern int32_t  g_gfx_saved_cursor_y;     /* DAT_00591504                                               */
extern int32_t  g_gfx_glyph_offset;       /* DAT_0058fe04 (Offset)  source offset into the blob         */
extern int16_t  g_gfx_glyph_w;            /* DAT_005c9258 WORD      width (movsx)                       */
extern int32_t  g_gfx_glyph_h;            /* DAT_005bb478           row count                           */
extern int32_t  g_gfx_clipped_w;          /* DAT_00591514                                               */
extern int32_t  g_gfx_src_stride_pad;     /* DAT_0059150c                                               */
extern int32_t  g_gfx_dst_stride_pad;     /* DAT_005cd400                                               */
extern int32_t  g_gfx_color_modulation;   /* DAT_005cd40c                                               */

/* gfx_dispatch — descriptor staging + clip setters */
extern int32_t  g_gfx_alt_clip_enabled;   /* DAT_005aeb84                                               */
extern int32_t  g_gfx_alt_clip_left;      /* DAT_0052afdc                                               */
extern int32_t  g_gfx_alt_clip_bottom;    /* DAT_00553254                                               */
extern int32_t  g_gfx_last_glyph_index;   /* DAT_005c9288                                               */
extern int32_t  g_gfx_last_glyph_offset;  /* DAT_0058fd88  the measurers' cache: last_glyph_index*16 + 8  */
extern int16_t  g_gfx_glyph_height_short; /* DAT_005aeb70 WORD      descriptor +2 (movsx)               */
extern uint8_t  g_gfx_flag_a;             /* DAT_00591568 BYTE      descriptor +0xc                     */
extern uint8_t  g_gfx_flag_b;             /* DAT_0057d3c0 BYTE      descriptor +0xe                     */
extern uint8_t  g_gfx_x_clip_mode;        /* DAT_005cdd20 BYTE                                          */
extern uint8_t  g_gfx_y_clip_mode;        /* DAT_0058fe18 BYTE                                          */
extern int32_t  g_gfx_left_skip;          /* DAT_0058fd50                                               */
extern int32_t  g_gfx_top_skip;           /* DAT_0058feac                                               */
extern int32_t  g_gfx_right_visible_w;    /* DAT_0058fe98                                               */
extern int32_t  g_gfx_bottom_visible_h;   /* DAT_005aeb50                                               */

/* gfx_line — the Bresenham registers */
extern int32_t  g_gfx_abs_dx;             /* DAT_005aeb40 */
extern int32_t  g_gfx_abs_dy;             /* DAT_005aeb44 */
extern int32_t  g_gfx_x_end;              /* DAT_005aeb48 */
extern int32_t  g_gfx_y_end;              /* DAT_005aeb4c */
extern int32_t  g_gfx_x_step;             /* DAT_005aeb54 */
extern int32_t  g_gfx_y_step;             /* DAT_005aeb58 */
extern int32_t  g_gfx_x_start;            /* DAT_005aeb5c */
extern int32_t  g_gfx_y_start;            /* DAT_005aeb60 */
extern int32_t  g_gfx_bres_err;           /* DAT_0058fd5c */

/* gfx_text — mode flags, substitution stream, first-position hook, accumulators */
extern int32_t  g_gfx_no_shadow;          /* DAT_005aea40 */
extern int32_t  g_gfx_outline;            /* DAT_0058fe9c */
extern int32_t  g_gfx_tight_shadow;       /* DAT_005aeb90 */
extern int32_t  g_gfx_uppercase_highlight;/* DAT_0058fe2c */
extern uint8_t  g_gfx_collapse_spaces;    /* DAT_005aeb7c BYTE */
extern uint8_t  g_gfx_screen_byte;        /* DAT_004eac50 BYTE  current screen id */
#define GFX_TEXT_SUBSTITUTION_BUF_LEN 256
extern uint8_t  g_gfx_substitution_buf[GFX_TEXT_SUBSTITUTION_BUF_LEN]; /* DAT_005bb4d0 */
extern int32_t  g_gfx_substitution_idx;   /* DAT_005bb4b4 */
extern int32_t  g_gfx_substitution_on;    /* DAT_005bb4c4 */
extern uint8_t  g_gfx_collapse_active;    /* DAT_005c925a BYTE */
extern int32_t  g_gfx_first_pos_target;   /* DAT_005bb4a8 (also the text editor's cursor) */
extern int32_t  g_gfx_first_pos_latch;    /* DAT_005aeb64 */
extern int32_t  g_gfx_first_pos_x;        /* DAT_0058fd58 */
extern int32_t  g_gfx_first_pos_y;        /* DAT_0058fd54 */
extern int32_t  g_gfx_line_x_acc;         /* DAT_005cd404 */
extern int32_t  g_gfx_line_y_acc;         /* DAT_005cd4f8 */
extern int32_t  g_gfx_char_counter;       /* DAT_0058fd78 */

/* gfx_text_label — the formatter's buffer.  Retail's is 100 bytes (DAT_0058fda0..; Offset follows at
 * DAT_0058fe04); the standalone keeps 8000 so an over-long suffix stays inside the array. */
#define GFX_TEXT_LABEL_BUFFER_LEN 8000
extern uint8_t  g_gfx_format_buffer[GFX_TEXT_LABEL_BUFFER_LEN];       /* DAT_0058fda0 */

/* gfx_wrap */
extern int32_t  g_gfx_max_line_x;         /* DAT_005aeb88 */
extern int32_t  g_gfx_word_chars;         /* DAT_005c9a7c  the word measurer's consumed-byte count */
#define GFX_WRAP_LARGE_LINE_BUF_LEN 2000
#define GFX_WRAP_SMALL_LINE_BUF_LEN  200
extern uint8_t  g_gfx_wrap_large_buf[GFX_WRAP_LARGE_LINE_BUF_LEN];    /* DAT_005c92a0 */
extern uint8_t  g_gfx_wrap_small_buf[GFX_WRAP_SMALL_LINE_BUF_LEN];    /* DAT_005aea60 */

/* gfx_sprite_render */
extern const uint8_t *g_gfx_current_blob; /* DAT_004eabec  the bound sprite sheet */
extern uint8_t  g_gfx_last_error;         /* DAT_005aeb68 BYTE  error latch (FUN_00408fb0) */
extern int32_t  g_gfx_seal_x;             /* DAT_0055ce78 */
extern int32_t  g_gfx_seal_y;             /* DAT_0057c8a0 */
extern int32_t  g_gfx_seal_mode;          /* DAT_0056956c */

/* the menu bar's redraw latches (FUN_0040c5b0 raises both) */
extern uint8_t  g_gfx_menu_redraw_a;      /* DAT_004eb260 BYTE */
extern uint8_t  g_gfx_menu_redraw_b;      /* DAT_004e65f8 BYTE */

/* gfx_caret */
extern int32_t  g_gfx_caret_blink;        /* DAT_004e5998 */

#endif /* OPENLORDS2_GFX_GLOBALS_H */
