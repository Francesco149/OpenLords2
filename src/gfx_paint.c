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
