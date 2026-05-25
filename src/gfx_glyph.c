/*
 * OpenLords2 — glyph blitter (implementation).
 *
 * See gfx_glyph.h for the per-function map and state model.  Each of
 * the three exported entry points matches its retail body verbatim
 * (loop shape, transparency test, write mode).  The two clipped
 * entry points share a static helper because their retail bodies
 * are byte-identical — preserving them as distinct names matches
 * the dispatcher's call site selection without duplicating logic.
 */

#include "gfx_glyph.h"
#include "gfx_paint.h"

/* State globals — mirror retail DAT_* names in comments. */
static uint8_t g_color;             /* DAT_0057d3bc */
static int     g_cursor_x;          /* DAT_00591524 */
static int     g_cursor_y;          /* DAT_00591528 */
static int     g_glyph_offset;      /* DAT_0058fe04 */
static int     g_glyph_w;           /* DAT_005c9258 */
static int     g_glyph_h;           /* DAT_005bb478 */
static int     g_clipped_w;         /* DAT_00591514 */
static int     g_src_stride_pad;    /* DAT_0059150c */
static int     g_dst_stride_pad;    /* _DAT_005cd400 */
static int     g_color_modulation;  /* DAT_005cd40c */

void gfx_glyph_set_color(uint8_t color)
{
    g_color = color;
}

void gfx_glyph_set_dest(int cursor_x, int cursor_y)
{
    g_cursor_x = cursor_x;
    g_cursor_y = cursor_y;
}

void gfx_glyph_set_glyph(int glyph_offset, int glyph_w, int glyph_h)
{
    g_glyph_offset = glyph_offset;
    g_glyph_w      = glyph_w;
    g_glyph_h      = glyph_h;
}

void gfx_glyph_set_glyph_h(int glyph_h)
{
    g_glyph_h = glyph_h;
}

void gfx_glyph_set_clip(int clipped_w, int src_stride_pad, int dst_stride_pad)
{
    g_clipped_w       = clipped_w;
    g_src_stride_pad  = src_stride_pad;
    g_dst_stride_pad  = dst_stride_pad;
}

void gfx_glyph_set_color_modulation(int mode)
{
    g_color_modulation = mode;
}

int gfx_glyph_cursor_x(void) { return g_cursor_x; }
int gfx_glyph_cursor_y(void) { return g_cursor_y; }
int gfx_glyph_glyph_w(void)  { return g_glyph_w; }
int gfx_glyph_glyph_h(void)  { return g_glyph_h; }

/* Shared dest-pointer computation: pixels + cursor_x + cursor_y*width.
 * Retail formula at the top of every blitter:
 *   DAT_004ea1a8 + DAT_00591524 + DAT_00591528 * DAT_004eb274
 */
static uint8_t *dest_origin(void)
{
    uint8_t *pixels = gfx_paint_canvas_pixels();
    if (!pixels) return 0;
    int      width  = gfx_paint_canvas_width();
    return pixels + g_cursor_x + g_cursor_y * width;
}

/* Faithful translation of the retail inner-loop shape.  The
 * decompiler emits:
 *
 *   iVar3 = iVar4 + -1;
 *   bVar1 = 0 < iVar4;
 *   iVar4 = iVar3;
 *   while (iVar3 != 0 && bVar1);
 *
 * which means "decrement, then loop while post-decrement value is
 * non-zero AND pre-decrement was strictly positive."  With a
 * `do {...} while (...)` body this always runs at least one
 * iteration — even when the initial counter is 0 or negative.
 * Preserved as written.  See engine-quirks.md.
 */

void gfx_glyph_blit(const uint8_t *font_base)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src = font_base + g_glyph_offset;
    int rows = g_glyph_h;

    if (g_color_modulation == 1) {
        /* Copy-byte mode: each non-zero source pixel keeps its own
         * palette index in dest.  Used for pre-shaded glyphs. */
        do {
            int cols = g_glyph_w;
            do {
                if (*src != 0) *dst = *src;
                src++;
                dst++;
                int next = cols - 1;
                int gt   = (0 < cols);
                cols = next;
                if (!(next != 0 && gt)) break;
            } while (1);
            dst += g_dst_stride_pad;
            int next = rows - 1;
            int gt   = (0 < rows);
            rows = next;
            if (!(next != 0 && gt)) break;
        } while (1);
    } else {
        /* Color-fill mode: every non-zero source pixel becomes the
         * bound color.  Used for normal text rendering. */
        do {
            int cols = g_glyph_w;
            do {
                if (*src != 0) *dst = g_color;
                src++;
                dst++;
                int next = cols - 1;
                int gt   = (0 < cols);
                cols = next;
                if (!(next != 0 && gt)) break;
            } while (1);
            dst += g_dst_stride_pad;
            int next = rows - 1;
            int gt   = (0 < rows);
            rows = next;
            if (!(next != 0 && gt)) break;
        } while (1);
    }
}

/* Shared body for the byte-identical clipped duplicates
 * (FUN_004b425c / FUN_004b42c5).  Color-fill only; uses clipped_w for
 * the inner count and adds src_stride_pad at end of each row. */
static void glyph_blit_clipped(const uint8_t *font_base)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src = font_base + g_glyph_offset;
    int rows = g_glyph_h;

    do {
        int cols = g_clipped_w;
        do {
            if (*src != 0) *dst = g_color;
            src++;
            dst++;
            int next = cols - 1;
            int gt   = (0 < cols);
            cols = next;
            if (!(next != 0 && gt)) break;
        } while (1);
        src += g_src_stride_pad;
        dst += g_dst_stride_pad;
        int next = rows - 1;
        int gt   = (0 < rows);
        rows = next;
        if (!(next != 0 && gt)) break;
    } while (1);
}

void gfx_glyph_blit_left_clipped(const uint8_t *font_base)
{
    glyph_blit_clipped(font_base);
}

void gfx_glyph_blit_right_clipped(const uint8_t *font_base)
{
    glyph_blit_clipped(font_base);
}
