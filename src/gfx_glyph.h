/*
 * OpenLords2 — glyph blitter (per-character pixel emitter).
 *
 * Three retail leaves sit between the text renderer and the framebuffer
 * pixel painters: one unclipped path and two clipped duplicates.  Each
 * is invoked by `FUN_00402a14` (not yet ported) once per character, with
 * file-static globals pre-populated by the dispatcher to describe the
 * glyph, the cursor, the clip box, and the per-row strides.
 *
 * Retail address map:
 *
 *   FUN_004b41b7 @ 0x4b41b7 (165 B) → gfx_glyph_blit
 *                                       Unclipped path; reads glyph_w
 *                                       cols per row, no source stride
 *                                       pad.  Supports two write modes
 *                                       (see set_color_modulation).
 *   FUN_004b425c @ 0x4b425c (105 B) → gfx_glyph_blit_left_clipped
 *                                       Clipped path; reads clipped_w
 *                                       cols per row with source stride
 *                                       pad to skip past the trimmed
 *                                       columns.  Color-only writes.
 *   FUN_004b42c5 @ 0x4b42c5 (105 B) → gfx_glyph_blit_right_clipped
 *                                       BYTE-IDENTICAL to 0x4b425c —
 *                                       see engine-quirks.md for the
 *                                       dedup note.  Same internal
 *                                       implementation.
 *
 * State globals (mirror retail DAT_* names):
 *
 *   color                   DAT_0057d3bc — paint color (8-bit palette)
 *   cursor_x, cursor_y      DAT_00591524 / DAT_00591528 — top-left
 *                           dest in the bound canvas
 *   glyph_offset            DAT_0058fe04 — byte offset from font_base
 *                           to the glyph's data start
 *   glyph_w, glyph_h        DAT_005c9258 / DAT_005bb478 — glyph
 *                           dimensions in pixels (rows, source row
 *                           width)
 *   clipped_w               DAT_00591514 — visible row width after
 *                           horizontal clip (== glyph_w when
 *                           unclipped); inner loop bound for the
 *                           clipped variants
 *   src_stride_pad          DAT_0059150c — bytes to advance the
 *                           source pointer at end of each row
 *                           (glyph_w - clipped_w)
 *   dst_stride_pad          _DAT_005cd400 — bytes to advance the
 *                           dest pointer at end of each row
 *                           (canvas_width - clipped_w)
 *   color_modulation        DAT_005cd40c — write-mode selector for
 *                           the UNCLIPPED path only:
 *                             == 1 → copy the source glyph byte
 *                                    verbatim (per-pixel color)
 *                             else → write the bound `color` for any
 *                                    non-zero source byte (silhouette
 *                                    fill).  The clipped variants are
 *                                    silhouette-fill only.
 *
 * Canvas binding:
 *
 *   The three blitters compute their dest pointer from gfx_paint's
 *   canvas binding (gfx_paint_canvas_pixels() / canvas_width()) —
 *   retail uses the same DAT_004ea1a8 / DAT_004eb274 that gfx_paint
 *   reads.  Bind the canvas once via gfx_paint_bind_canvas; the glyph
 *   blitter inherits it.
 *
 * Quirks preserved verbatim:
 *
 *   - Zero source byte == transparent.  Even in copy-byte mode, source
 *     0 is skipped (it is the transparent index in retail's palette).
 *   - 0-width / 0-height glyphs still execute ONE iteration of the
 *     inner / outer loop respectively (the post-test guards permit a
 *     final pass even when the counter starts at zero).  The upstream
 *     dispatcher filters this case out before calling, but the leaf
 *     itself does not — see engine-quirks.md.
 *   - left/right clipped duplicates: see engine-quirks.md for the byte-
 *     identical dedup story.
 */

#ifndef OPENLORDS2_GFX_GLYPH_H
#define OPENLORDS2_GFX_GLYPH_H

#include <stdint.h>

/* Setters — mirror the retail dispatcher's writes to the corresponding
 * DAT_* globals immediately before invoking a blitter.  Tests use these
 * to stage state; the eventual port of FUN_00402a14 will too. */
void gfx_glyph_set_color(uint8_t color);
void gfx_glyph_set_dest(int cursor_x, int cursor_y);
void gfx_glyph_set_glyph(int glyph_offset, int glyph_w, int glyph_h);
void gfx_glyph_set_clip(int clipped_w, int src_stride_pad, int dst_stride_pad);
void gfx_glyph_set_color_modulation(int mode);

/* Port of FUN_004b41b7 @ 0x4b41b7.  Unclipped glyph blit.
 *
 * Iterates `glyph_h` rows of `glyph_w` columns each.  For each source
 * byte at (`font_base` + `glyph_offset` + row*`glyph_w` + col):
 *
 *   - If source byte is 0: skip (transparent).
 *   - Else, with color_modulation == 1: write source byte to dest.
 *   - Else: write `color` to dest.
 *
 * Source pointer advances tightly (no per-row pad); dest pointer
 * advances by `dst_stride_pad` at end of each row.  No bounds check —
 * the dispatcher's upstream clip (FUN_0040464c / 0040477b) ensures
 * dest writes stay in-canvas. */
void gfx_glyph_blit(const uint8_t *font_base);

/* Port of FUN_004b425c @ 0x4b425c.  Clipped glyph blit (left-clip case).
 *
 * Iterates `glyph_h` rows of `clipped_w` columns each.  Always writes
 * `color` for non-zero source bytes — the color-modulation mode is NOT
 * honored in the clipped path (retail behavior).  Source pointer
 * advances by `src_stride_pad` at end of each row; dest pointer
 * advances by `dst_stride_pad`. */
void gfx_glyph_blit_left_clipped(const uint8_t *font_base);

/* Port of FUN_004b42c5 @ 0x4b42c5.  Clipped glyph blit (right-clip case).
 *
 * BYTE-IDENTICAL retail body to FUN_004b425c — both 105 bytes, every
 * instruction matches.  Exposed as a separate entry point only because
 * the dispatcher (FUN_00402a14) selects between them by clip-mode tag,
 * preserving retail's call topology.  Shares a static implementation
 * with gfx_glyph_blit_left_clipped. */
void gfx_glyph_blit_right_clipped(const uint8_t *font_base);

#endif /* OPENLORDS2_GFX_GLYPH_H */
