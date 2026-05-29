/*
 * OpenLords2 — framebuffer pixel painters (implementation).
 *
 * See gfx_paint.h for the function map.  Each routine matches the
 * retail body byte-for-byte (no extra clipping, no extra branches).
 */

#include "gfx_paint.h"

/* Retail globals — file-static here.  Runtime binds these from the
 * locked DDraw back-buffer; tests bind from a host-allocated
 * uint8_t array.
 *
 *   DAT_004ea1a8 → g_pixels       (back-buffer base pointer)
 *   DAT_004eb274 → g_width        (row pitch AND right-clip bound)
 *   DAT_004eac4c → g_height       (bottom-clip bound)
 */
static uint8_t *g_pixels;
static int      g_width;
static int      g_height;

void gfx_paint_bind_canvas(uint8_t *pixels, int width, int height)
{
    g_pixels = pixels;
    g_width  = width;
    g_height = height;
}

uint8_t *gfx_paint_canvas_pixels(void) { return g_pixels; }
int      gfx_paint_canvas_width(void)  { return g_width;  }
int      gfx_paint_canvas_height(void) { return g_height; }

void gfx_paint_pixel(int x, int y, uint8_t color)
{
    if (!g_pixels) return;
    g_pixels[x + y * g_width] = color;
}

void gfx_paint_2x1(int x, int y, uint8_t color)
{
    if (!g_pixels) return;
    uint8_t *p = g_pixels + x + y * g_width;
    p[0] = color;
    p[1] = color;
}

void gfx_paint_2x1_under(int x, int y, uint8_t color)
{
    if (!g_pixels) return;
    uint8_t *p = g_pixels + x + y * g_width;
    if (p[0] == 0) p[0] = color;
    if (p[1] == 0) p[1] = color;
}

/* Port of FUN_004b13ed @ 0x4b13ed (274 B).  Darken a w×h canvas rect by
 * remapping every pixel through a palette-derived lookup: for each of the
 * 256 palette entries, the remap target is 0x3f - (((R+G+B)/3) >> 3), so
 * a pixel's new index is a dark-gray ramp value keyed off its source
 * color's luminance.  Used to dim disabled-player roster rows on the
 * game-setup screen.  The rect is clipped only by the caller (retail does
 * no bounds check); no-op if the canvas is unbound.
 *
 * `pal_rgb` is retail's DAT_004ea1b0[256*3] (R,G,B per entry); the caller
 * passes the active palette mirror (gfx.c's g_palette_rgb).  Retail
 * hardcodes the 0x280 row stride; the bound canvas width is that stride
 * at runtime (640) and is used here so the op stays correct under test. */
void gfx_paint_darken_rect(const uint8_t *pal_rgb, int x, int y, int w, int h)
{
    if (!g_pixels || !pal_rgb) return;

    uint8_t remap[256];
    for (int c = 0; c < 256; c++) {
        int avg = (pal_rgb[c * 3] + pal_rgb[c * 3 + 1] + pal_rgb[c * 3 + 2]) / 3;
        remap[c] = (uint8_t)(0x3f - (avg >> 3));
    }
    for (int yy = y; yy < y + h; yy++) {
        uint8_t *row = g_pixels + yy * g_width;
        for (int xx = x; xx < x + w; xx++) {
            row[xx] = remap[row[xx]];
        }
    }
}

void gfx_paint_pixel_clipped(int x, int y, uint8_t color)
{
    if (x >= 0 && x < g_width && y >= 0 && y < g_height) {
        gfx_paint_pixel(x, y, color);
    }
}

void gfx_paint_2x1_clipped(int x, int y, uint8_t color)
{
    if (x >= 0 && x < g_width && y >= 0 && y < g_height) {
        gfx_paint_2x1(x, y, color);
    }
}
