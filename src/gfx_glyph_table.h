/*
 * OpenLords2 — embedded character→glyph index table.
 *
 * Retail keeps a single 256-byte char→glyph map in initialised .data
 * and reaches it through two pointers that differ by 0x20:
 *
 *   DAT_004d71d0  — the "measure" map, indexed by the raw char code.
 *                   FUN_004015b9 (font_metrics) does `(&DAT_004d71d0)[c]`.
 *   DAT_004d71f0  — the "render" map, == DAT_004d71d0 + 0x20.
 *                   FUN_00402a14 (gfx_dispatch_glyph) receives an
 *                   already-decremented char (`c - 0x20`) from its caller
 *                   and does `(&DAT_004d71f0)[c - 0x20]`, which lands on
 *                   the same byte as the measure map's `[c]`.
 *
 * Both pointers are bytes into one table, so the identity
 *   render[c - 0x20] == measure[c]
 * holds.  The table lives at 0x4d71d0 in Lords2.exe — inside file-backed
 * .data, so it is a fixed binary constant (NOT a runtime-loaded asset).
 * A drop-in replacement cannot read it from the exe at runtime (we ARE
 * the exe), so it is embedded here, extracted once from the GOG build.
 *
 * Value semantics: a non-zero entry is a 1-based glyph index into the
 * font PL8's descriptor array (consumer subtracts 1); 0 means "no glyph"
 * (the renderer emits a fixed 4-pixel advance instead).
 */
#ifndef OPENLORDS2_GFX_GLYPH_TABLE_H
#define OPENLORDS2_GFX_GLYPH_TABLE_H

#include <stdint.h>

/* Measure map — DAT_004d71d0.  Index with the raw char code. */
const uint8_t *gfx_glyph_measure_map(void);

/* Render map — DAT_004d71f0 (== measure map + 0x20).  Index with the
 * decremented char code (`c - 0x20`), as the glyph dispatcher does. */
const uint8_t *gfx_glyph_render_map(void);

#endif /* OPENLORDS2_GFX_GLYPH_TABLE_H */
