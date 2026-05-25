/*
 * OpenLords2 — font pixel-width measurer.
 *
 * Two leaves that consult a 256-byte char→glyph-index table plus a
 * font's 16-byte-per-glyph descriptor array to compute the rendered
 * pixel width of single characters and of null-terminated byte
 * buffers.
 *
 * Retail address map:
 *
 *   FUN_004015b9 @ 0x4015b9 (173 B) → font_metrics_char_width
 *   FUN_00401f8f @ 0x401f8f (122 B) → font_metrics_buffer_width
 *
 * Data model:
 *
 *   `char_glyph_index` (DAT_004d71d0) — a 256-byte lookup table
 *     mapping byte values to 1-based glyph indices.  Entry value 0
 *     means "no glyph for this byte" (renders as zero width with no
 *     side effect on the cache).  Values 1..255 are decoded to
 *     0-based glyph indices (`value - 1`).
 *
 *   `font` (DAT_005af8f0 in retail's global default) — a flat byte
 *     blob holding 16-byte-per-glyph descriptors.  The width
 *     measurer reads two bytes per glyph: descriptor offset +8 (low
 *     byte of width) and +9 (high byte of width).  Final width:
 *         width = font[glyph_idx*16 + 9] * 256
 *               + font[glyph_idx*16 + 8]
 *               + 1
 *     The "+1" is a retail constant — probably an inter-character
 *     gap.  Special cases: '\0' renders as 0, ' ' renders as 4
 *     (hardcoded — not table-driven, so spaces have fixed width
 *     regardless of font).
 *
 * Cache state (preserved verbatim from retail):
 *
 *   `last_glyph_index`  — DAT_005c9288.  The 0-based glyph index
 *     from the most recent char-width call.  Set to 0 when the
 *     char-table lookup returned 0 (no glyph).  Read by 50+ retail
 *     functions (mostly renderers) that piggy-back on the width
 *     call to also obtain the glyph descriptor.
 *
 *   `last_glyph_offset` — DAT_0058fd88.  `last_glyph_index*16 + 8`,
 *     i.e. the offset to the width bytes within the font blob.
 *     Conveniently positioned at the start of the per-glyph payload
 *     read by downstream renderers.
 *
 * Buffer-width side effect:
 *
 *   `pixel_overflow` is set to 1 by `font_metrics_buffer_width`
 *   when the accumulated width is >= `max_pixel_width`, else 0.
 *   This drives the text-input editor's clamp_state==2 path
 *   (text_input.h's `pixel_overflow` field — see the note in
 *   `text_input_clamp_cursor`).
 *
 * Pure C; no Win32 deps; no globals — all state held in
 * `font_metrics_state_t`.
 */

#ifndef OPENLORDS2_FONT_METRICS_H
#define OPENLORDS2_FONT_METRICS_H

#include <stddef.h>

#define FONT_METRICS_GLYPH_STRIDE 16   /* bytes per glyph descriptor */
#define FONT_METRICS_WIDTH_LO_OFFSET 8
#define FONT_METRICS_WIDTH_HI_OFFSET 9

typedef struct {
    /* The 256-entry char→glyph-index table.  Retail uses a single
     * global table (DAT_004d71d0); callers in our port can supply
     * any 256-byte buffer.  May be NULL — chars without a glyph
     * always return width 0. */
    const unsigned char *char_glyph_index;

    /* Cache fields populated by every char_width call.  See header
     * doc — these are read by downstream renderers via the shared
     * state struct. */
    int last_glyph_index;
    int last_glyph_offset;

    /* Last buffer_width call's overflow verdict; mirrored to the
     * text-input editor's `pixel_overflow` field by callers.  Set
     * to 1 iff total width >= max_pixel_width passed to that call. */
    int pixel_overflow;
} font_metrics_state_t;

/* Port of FUN_004015b9.  Width of a single character `c` against
 * the supplied font blob.  Updates `last_glyph_index` /
 * `last_glyph_offset` as a side effect (consult them via the
 * state struct).  Special cases:
 *   c == 0    → returns 0, cache NOT updated (matches retail).
 *   c == 0x20 → returns 4, cache NOT updated (matches retail).
 *   missing   → returns 0, last_glyph_index set to 0,
 *               last_glyph_offset untouched.
 *   present   → returns font[idx*16+9]*256 + font[idx*16+8] + 1,
 *               cache fully updated. */
int font_metrics_char_width(font_metrics_state_t *s,
                            unsigned char c,
                            const unsigned char *font);

/* Port of FUN_00401f8f.  Sum the widths of every char in the
 * null-terminated `buf`, using `font` for the glyph descriptors.
 * Returns the total.  Side effect: sets `s->pixel_overflow` to 1
 * iff the total is >= `max_pixel_width`, else 0.
 *
 * Walks per-char via `font_metrics_char_width`, so the cache fields
 * end up reflecting the LAST non-trivial char's glyph. */
int font_metrics_buffer_width(font_metrics_state_t *s,
                              const unsigned char *buf,
                              const unsigned char *font,
                              int max_pixel_width);

#endif /* OPENLORDS2_FONT_METRICS_H */
