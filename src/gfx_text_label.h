/*
 * OpenLords2 — integer formatter + label-render wrappers.
 *
 * Sits one layer above the text-line renderer (gfx_text_render_line /
 * gfx_text_render_line_centered).  Provides the engine's "format an
 * integer + append a static label + render in one call" entry points
 * that drive the on-screen stat readouts (player rank, resource
 * counts, score displays, etc.).
 *
 * All four render wrappers share the same shape:
 *   1. Zero the first 100 bytes of the shared 8000-byte format buffer.
 *   2. Format `value` as decimal or hex digits at offset 1 (with
 *      optional sign char at offset 1, digits shifted right).
 *   3. If `prefix` != 0, overwrite buffer[0] with it (typically ' ' or
 *      '@' — see quirk #41).
 *   4. Strlen the buffer and append `suffix` past the digits.
 *   5. Call the underlying renderer on the assembled buffer.
 *   6. Clear the dispatcher's alt_clip_enabled flag.
 *
 * Retail address map:
 *
 *   FUN_004022bd @ 0x4022bd (394 B) → gfx_text_label_format_dec
 *                                     Integer → decimal digits in the
 *                                     shared buffer.  1-9 digits sized
 *                                     to magnitude; ≥10^9 silently
 *                                     writes nothing (quirk #42).
 *
 *   FUN_00402447 @ 0x402447 (400 B) → gfx_text_label_format_hex
 *                                     Integer → uppercase hex digits.
 *                                     1-7 digits; ≥0x10000000 silently
 *                                     writes nothing (quirk #42).
 *
 *   FUN_00402f64 @ 0x402f64 (177 B) → gfx_text_label_render_dec
 *                                     Render "<prefix?><dec><suffix>"
 *                                     via gfx_text_render_line.
 *
 *   FUN_00403015 @ 0x403015 (177 B) → gfx_text_label_render_hex
 *                                     Render "<prefix?><hex><suffix>"
 *                                     via gfx_text_render_line.  Used
 *                                     by the leaderboard / rank screens
 *                                     where the formatted value is a
 *                                     small (single-byte) ID.
 *
 *   FUN_004030c6 @ 0x4030c6 (202 B) → gfx_text_label_render_dec_centered
 *                                     Centered variant — routes through
 *                                     gfx_text_render_line_centered.
 *
 *   FUN_00403190 @ 0x403190 (254 B) → gfx_text_label_render_dec_signed
 *                                     Centered + signed-color variant.
 *                                     Picks `color_positive` or
 *                                     `color_negative` based on sign;
 *                                     formats with mandatory sign char
 *                                     ('+' or '-'); skips render
 *                                     entirely when both `value` and
 *                                     `zero_companion` are zero.
 *
 * Shared state:
 *
 *   The 8000-byte format buffer (DAT_0058fda0) is module-private here.
 *   Retail's same buffer is read by FUN_004afab9 and FUN_004afeed (the
 *   save-name copy paths); those callers aren't ported yet and would
 *   need this module to expose the buffer when they are.  See
 *   `gfx_text_label_buffer()` and `gfx_text_label_buffer_size()`.
 *
 * Quirks preserved verbatim — see engine-quirks.md for narratives:
 *
 *   - #41: The `prefix` parameter does NOT prepend — it overwrites
 *     buffer[0], which is the first sign / digit slot left by the
 *     formatter at offset 1.  So `prefix=' '` with value=42 yields
 *     " 42" (space at [0], digits at [1..2]); `prefix='@'` yields
 *     "@42".  Callers rely on this byte-0 sentinel character ('@' on
 *     stat readouts, ' ' on rank lists) for visual separation between
 *     adjacent label rows.
 *
 *   - #42: Values ≥10^9 (decimal) or ≥0x10000000 (hex) silently write
 *     ZERO digits — the digit-count selector falls into the default
 *     `local_8 = 0` branch and the digit-write loop never executes.
 *     The sign (if any) is still emitted.  Visual result: just "" or
 *     "-" / "+" with no number, plus the suffix.  Almost certainly a
 *     "callers never pass values this large" assumption; faithful here.
 *
 *   - #43: The dec and hex formatters both zero only the FIRST 100
 *     bytes of the 8000-byte format buffer.  Bytes 100+ retain
 *     whatever a prior caller left there.  All call sites read only
 *     bytes 0..(prefix + sign + digits + suffix) which fits within
 *     the zeroed region — but the buffer is genuinely shared mutable
 *     state across the renderer + save-name paths.
 *
 *   - #44: FUN_00403190's `param_2` (mapped to `zero_companion`) acts
 *     as a sentinel: when BOTH `value` and `zero_companion` are zero,
 *     the entire render is skipped (no formatter call, no render).
 *     This lets callers gate rendering on a sister stat being non-
 *     zero.  No call sites for this function exist in the captured
 *     decompilation — the parameter semantics are inferred from
 *     control flow; faithful but unvalidated against retail behavior.
 *
 * Pure C; no Win32 deps.
 */

#ifndef OPENLORDS2_GFX_TEXT_LABEL_H
#define OPENLORDS2_GFX_TEXT_LABEL_H

#include <stddef.h>
#include <stdint.h>

/* ──────────────────────────────────────────────────────────────────
 * Shared format buffer access (tests + future save-name paths)
 * ────────────────────────────────────────────────────────────────── */

/* The 8000-byte format buffer mirroring retail DAT_0058fda0.  Read-only
 * peek for tests; the formatters write into it.  Retail also reads
 * from this buffer in the save-name copy paths (FUN_004afab9 /
 * FUN_004afeed) — those wrappers aren't ported yet but will share the
 * same storage when they are.  Pointer is stable for the program
 * lifetime. */
uint8_t      *gfx_text_label_buffer(void);
size_t        gfx_text_label_buffer_size(void);

/* ──────────────────────────────────────────────────────────────────
 * Integer → digit formatters (write into shared buffer)
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_004022bd.  Format `value` as decimal digits into the
 * shared buffer at `offset`.  Zeros first 100 bytes of the buffer
 * first.  If `value` < 0: writes '-' at `offset` and bumps the digit
 * start to offset+1; the value is negated before digit extraction.
 * Else if `with_plus` != 0: writes '+' at `offset` and bumps similarly.
 * Else no sign byte is emitted.
 *
 * Digit count is determined by magnitude — 1 through 9 digits — and
 * the digits are written big-endian into buffer[digit_start ..
 * digit_start + count - 1].  Values ≥ 10^9 (after sign handling) emit
 * ZERO digits — see quirk #42. */
void gfx_text_label_format_dec(int value, int offset, int with_plus);

/* Port of FUN_00402447.  Same as `format_dec` but emits uppercase
 * hexadecimal digits (1-7 of them).  Values ≥ 0x10000000 emit ZERO
 * digits — see quirk #42. */
void gfx_text_label_format_hex(int value, int offset, int with_plus);

/* ──────────────────────────────────────────────────────────────────
 * Render wrappers (format + append suffix + render)
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00402f64.  Format `value` as decimal into the shared
 * buffer at offset 1; if `prefix` != 0, overwrite buffer[0] with it
 * (see quirk #41); append `suffix` past the digits; render the
 * assembled buffer via `gfx_text_render_line` at (cursor_x, cursor_y).
 * Clears the dispatcher's alt_clip_enabled at end (via the renderer).
 *
 * `with_plus` is implied 0 — positive values get no sign prefix. */
void gfx_text_label_render_dec(int value,
                               char prefix,
                               const char *suffix,
                               int cursor_x,
                               int cursor_y,
                               const uint8_t *font_base,
                               const uint8_t *char_glyph_table,
                               uint8_t color);

/* Port of FUN_00403015.  Same as `render_dec` but formats `value` as
 * uppercase hex.  Used by the leaderboard / rank screens where the
 * formatted value is a small (single-byte) ID. */
void gfx_text_label_render_hex(int value,
                               char prefix,
                               const char *suffix,
                               int cursor_x,
                               int cursor_y,
                               const uint8_t *font_base,
                               const uint8_t *char_glyph_table,
                               uint8_t color);

/* Port of FUN_004030c6.  Centered variant of `render_dec`.  Routes
 * through `gfx_text_render_line_centered` so the assembled buffer is
 * centered within `width` pixels around (cursor_x, cursor_y).
 * `char_glyph_table_metrics` is passed through to the pre-measure
 * step — usually the same table as `char_glyph_table` but kept
 * separate to mirror retail's per-call topology. */
void gfx_text_label_render_dec_centered(int value,
                                        char prefix,
                                        const char *suffix,
                                        int cursor_x,
                                        int cursor_y,
                                        int width,
                                        const uint8_t *font_base,
                                        const uint8_t *char_glyph_table,
                                        const unsigned char *char_glyph_table_metrics,
                                        uint8_t color);

/* Port of FUN_00403190.  Centered + signed-color variant.  Skips
 * render entirely when both `value` and `zero_companion` are zero
 * (see quirk #44).  Otherwise: picks `color_positive` (for value >= 0)
 * or `color_negative` (for value < 0) as the foreground color;
 * formats with `with_plus = 1` so positive values render with a '+'
 * sign; routes through `render_dec_centered`. */
void gfx_text_label_render_dec_signed(int value,
                                      int zero_companion,
                                      char prefix,
                                      const char *suffix,
                                      int cursor_x,
                                      int cursor_y,
                                      int width,
                                      const uint8_t *font_base,
                                      const uint8_t *char_glyph_table,
                                      const unsigned char *char_glyph_table_metrics,
                                      uint8_t color_positive,
                                      uint8_t color_negative);

#endif /* OPENLORDS2_GFX_TEXT_LABEL_H */
