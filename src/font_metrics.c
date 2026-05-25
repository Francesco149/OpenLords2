/*
 * Font pixel-width measurer — pure-C port of FUN_004015b9 +
 * FUN_00401f8f.  See font_metrics.h for the API doc, retail address
 * map, and the cache-field invariants.
 */

#include "font_metrics.h"

int font_metrics_char_width(font_metrics_state_t *s,
                            unsigned char c,
                            const unsigned char *font)
{
    if (c == 0) {
        return 0;
    }
    if (c == 0x20) {
        return 4;
    }
    /* Null table → treat as no glyph. */
    int glyph_idx_one_based = 0;
    if (s->char_glyph_index != NULL) {
        glyph_idx_one_based = s->char_glyph_index[c];
    }
    if (glyph_idx_one_based == 0) {
        /* Retail sets DAT_005c9288 = 0 in this branch but leaves
         * DAT_0058fd88 (last_glyph_offset) untouched.  Mirror that. */
        s->last_glyph_index = 0;
        return 0;
    }
    s->last_glyph_index  = glyph_idx_one_based - 1;
    s->last_glyph_offset = s->last_glyph_index * FONT_METRICS_GLYPH_STRIDE
                         + FONT_METRICS_WIDTH_LO_OFFSET;
    int lo = font[s->last_glyph_offset];
    int hi = font[s->last_glyph_index * FONT_METRICS_GLYPH_STRIDE
                + FONT_METRICS_WIDTH_HI_OFFSET];
    return hi * 0x100 + lo + 1;
}

int font_metrics_buffer_width(font_metrics_state_t *s,
                              const unsigned char *buf,
                              const unsigned char *font,
                              int max_pixel_width)
{
    int total = 0;
    int i = 0;
    while (buf[i] != 0) {
        total += font_metrics_char_width(s, buf[i], font);
        i++;
    }
    /* Retail: DAT_0057d3b0 = (uint)(DAT_0058fd68 <= local_10), i.e.
     * pixel_overflow = (max_pixel_width <= total). */
    s->pixel_overflow = (max_pixel_width <= total) ? 1 : 0;
    return total;
}
