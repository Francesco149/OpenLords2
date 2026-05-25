/*
 * OpenLords2 — sprite blitter (per-sprite pixel emitter).
 *
 * Three retail leaves form the sprite-class blit family — the
 * counterpart of the gfx_glyph trio for non-glyph sprites (UI panels,
 * icons, building tiles, troop sprites, etc.).  Each consumes the same
 * staged state the glyph blitters do (cursor + source pointer +
 * width/height + the three clip outputs), populated by the sprite
 * dispatcher (gfx_dispatch_sprite).  The only behavioral difference
 * from the glyph blitters is the WRITE mode: sprites always copy the
 * source byte verbatim (per-pixel palette index), with the source
 * byte's zero value reserved as the transparent index — i.e. the
 * unclipped sprite path is functionally equivalent to the glyph
 * blitter's color_modulation == 1 branch, but with a different inner-
 * loop shape (see quirk on the 4-byte partial unroll).  The clipped
 * sprite path matches the glyph clipped path's loop structure but
 * copies bytes instead of writing a single color.
 *
 * Retail address map:
 *
 *   FUN_004b43b1 @ 0x4b43b1 (188 B) → gfx_sprite_blit
 *                                       Unclipped path.  Iterates
 *                                       glyph_h rows of glyph_w cols.
 *                                       Inner loop has an unusual
 *                                       partial 4-byte unroll: the
 *                                       FIRST four bytes of a row are
 *                                       handled by the unrolled body
 *                                       when glyph_w is a multiple
 *                                       of 4, then a byte-at-a-time
 *                                       tail handles the remainder.
 *                                       For glyph_w not a multiple of
 *                                       4, the entire row goes through
 *                                       the byte-at-a-time tail.
 *                                       Preserved verbatim — see
 *                                       engine-quirks.md.
 *   FUN_004b446d @ 0x4b446d (101 B) → gfx_sprite_blit_left_clipped
 *                                       Clipped path; reads clipped_w
 *                                       cols per row with src_stride_pad
 *                                       to skip past the trimmed cols.
 *   FUN_004b44d2 @ 0x4b44d2 (101 B) → gfx_sprite_blit_right_clipped
 *                                       BYTE-IDENTICAL to FUN_004b446d
 *                                       (verified by r2 pxd: same 101
 *                                       bytes byte-for-byte) — same
 *                                       dedup pattern as the clipped
 *                                       glyph pair.  Selected by the
 *                                       x_clip_mode tag to mirror retail
 *                                       call topology; shares its
 *                                       implementation.
 *   FUN_004b49f7 @ 0x4b49f7 (705 B) → gfx_sprite_blit_16x16_raw
 *                                       Fixed-size 16-wide × 16-tall
 *                                       OPAQUE raw blit (no transparency
 *                                       gate — every source byte
 *                                       written, including zeroes).
 *                                       Unlike the four blits above
 *                                       this one does not consume the
 *                                       width/height staging — both
 *                                       dimensions are hardcoded.
 *                                       Used by the battlefield (combat-
 *                                       mode) terrain renderer + 5
 *                                       non-battlefield callers (UI
 *                                       tile fills at 0x4097a / 0x4092c
 *                                       families).
 *
 * State globals — all reused from gfx_glyph.c via its getter API.
 * See gfx_glyph.h for the canonical retail DAT_* names.  The sprite
 * dispatcher (gfx_dispatch_sprite / gfx_dispatch_sprite_clipped) writes
 * the same set of globals the glyph dispatcher writes; the blitters
 * here read them back through gfx_glyph_* accessors.
 *
 * Canvas binding: same as gfx_glyph — bind once via
 * gfx_paint_bind_canvas; the sprite blitter computes its dest pointer
 * from gfx_paint_canvas_pixels() + cursor_x + cursor_y * canvas_width.
 *
 * Quirks preserved verbatim — see engine-quirks.md for narratives:
 *
 *   - 4-byte partial unroll only handles the FIRST 4 bytes (when
 *     glyph_w is a multiple of 4), then falls through to the same
 *     byte-at-a-time loop as the non-aligned path.  A genuine Duff's
 *     device would loop the unrolled body; this one fires it exactly
 *     once and then walks the rest byte-at-a-time.  Likely an MSVC
 *     4.20 codegen artifact rather than a deliberate hand optimization.
 *
 *   - 0-width sprite writes 4 bytes per row anyway.  The outer
 *     while-test `(glyph_w & 3) != 0` is false for glyph_w == 0, so
 *     the unrolled body fires.  The dispatcher gates this case off
 *     via the y_clip_mode == 5 (skip) tag, so the call never lands in
 *     retail.  Latent bug preserved.
 *
 *   - Inner loop's post-test guards (the `iVar4 < 1 ? bail : continue`
 *     pattern) permit one final iteration when row_count starts at 0.
 *     Same shape as the glyph blitter's inner loop — same quirk.
 *
 *   - Clipped variants byte-identical: see the analogous note for the
 *     clipped glyph pair in engine-quirks.md.  Same dedup pattern, same
 *     reason.
 */

#ifndef OPENLORDS2_GFX_SPRITE_H
#define OPENLORDS2_GFX_SPRITE_H

#include <stdint.h>

/* Port of FUN_004b43b1 @ 0x4b43b1.  Unclipped sprite blit.
 *
 * Iterates `glyph_h` rows of `glyph_w` columns each, copying each
 * non-zero source byte to dest verbatim (zero == transparent).  No
 * per-row source stride pad — source rows are densely packed at
 * glyph_w bytes each.  Dest advances by `dst_stride_pad` at end of
 * row.  No bounds check; the upstream clip setters ensure dest writes
 * stay in-canvas.
 *
 * `sprite_blob` is the base of the sprite-blob memory block — the
 * dispatcher staged `glyph_offset` relative to this base, and the
 * blitter computes the absolute source pointer as
 * `sprite_blob + glyph_offset`. */
void gfx_sprite_blit(const uint8_t *sprite_blob);

/* Port of FUN_004b446d @ 0x4b446d.  Clipped sprite blit (left-clip case).
 *
 * Iterates `glyph_h` rows of `clipped_w` columns each, copying each
 * non-zero source byte to dest.  Source pointer advances by
 * `src_stride_pad` at end of row to skip past the trimmed columns;
 * dest pointer advances by `dst_stride_pad`. */
void gfx_sprite_blit_left_clipped(const uint8_t *sprite_blob);

/* Port of FUN_004b44d2 @ 0x4b44d2.  Clipped sprite blit (right-clip case).
 *
 * BYTE-IDENTICAL retail body to FUN_004b446d — verified by r2 (101
 * bytes, every byte matches).  Exposed as a separate entry point
 * only because the dispatcher selects between them by clip-mode tag,
 * preserving retail's call topology.  Shares the static
 * implementation with gfx_sprite_blit_left_clipped. */
void gfx_sprite_blit_right_clipped(const uint8_t *sprite_blob);

/* Port of FUN_004b49f7 @ 0x4b49f7.  Fixed-size 16×16 opaque raw blit.
 *
 * Copies 16 rows × 16 bytes (= 256 bytes total) from
 * `sprite_blob + glyph_offset` to the dest origin computed from the
 * canvas binding + cursor.  Source is densely packed at 16 bytes per
 * row (no source stride pad).  Dest row stride is **hardcoded to 640
 * bytes** (retail dword indexing `puVar2[0xa0] = puVar1[4]` — see
 * engine-quirks).  No transparency check — every byte is written,
 * including zeroes.  No width/height staging consulted; the
 * dimensions are baked in.
 *
 * Used by the battlefield terrain renderer (FUN_004bc8fc /
 * FUN_004bcbdc) and 5 UI helpers (tile fills around 0x40920..0x4097a).
 *
 * `sprite_blob` is the same shape as the other sprite blits — a base
 * pointer that the dispatcher offsets by `glyph_offset` to reach the
 * sprite data.  Caller is responsible for ensuring the source has at
 * least 16×16 = 256 bytes available from that offset. */
void gfx_sprite_blit_16x16_raw(const uint8_t *sprite_blob);

/* Port of FUN_004b5333 @ 0x4b5333.  Fixed-size 32×32 opaque raw blit.
 *
 * Sibling of `gfx_sprite_blit_16x16_raw` — same shape, larger size.
 * Copies 32 rows × 32 bytes (= 1024 bytes total) from
 * `sprite_blob + glyph_offset` to the dest origin computed from the
 * canvas binding + cursor.  Source is densely packed at 32 bytes per
 * row.  Dest row stride is **hardcoded to 640 bytes** (retail dword
 * indexing `puVar2[0xa0] = puVar1[8]` — see engine-quirks #75 for the
 * shared narrative).  No transparency check; every source byte is
 * written, including zeroes.  No width/height staging consulted; the
 * dimensions are baked in.
 *
 * Retail body is 32 unrolled 8-dword row copies — 0x10e0 bytes total.
 * The port collapses to a row loop with memcpy; codegen is equivalent
 * at -O2 on every modern compiler, and the source bytes are not
 * guaranteed 4-byte aligned so memcpy avoids UB on unaligned loads
 * under ASan/UBSan host tests.
 *
 * Used by the battlefield zoom-32 terrain pass (FUN_004bcbdc).
 *
 * `sprite_blob` is the same shape as the other sprite blits — a base
 * pointer that the dispatcher offsets by `glyph_offset` to reach the
 * sprite data.  Caller is responsible for ensuring the source has at
 * least 32×32 = 1024 bytes available from that offset. */
void gfx_sprite_blit_32x32_raw(const uint8_t *sprite_blob);

/* Port of FUN_004b49b7 @ 0x4b49b7 (64 B).  Fixed-size 2×2 opaque raw
 * blit.  The micro-sibling of the 16×16 and 32×32 raw blits — used to
 * paint the strategic-map mini-map / overview where each world cell
 * occupies a 2×2-pixel block.
 *
 * Copies 4 bytes from `sprite_blob + glyph_offset` to the dest origin
 * computed from the canvas binding + cursor:
 *
 *   dst[0..1]    = src[0..1]   // row 0, two pixels
 *   dst[640..641] = src[2..3]  // row 1, two pixels  (same hardcoded
 *                              //                      640-byte stride
 *                              //                      as the 16×16/
 *                              //                      32×32 siblings)
 *
 * Retail body is 2 dword-indexed copies (`*puVar2 = *puVar1;
 * puVar2[0x140] = puVar1[1]`).  No transparency check.  No width/
 * height staging consulted; dimensions are baked in.
 *
 * Used by the strategic-map mini-map row-strip renderer (FUN_004bc51a)
 * once per cell across all 80×80 world cells.
 *
 * `sprite_blob` is the same shape as the other sprite blits — a base
 * pointer that the dispatcher offsets by `glyph_offset` to reach the
 * 4 source bytes.  Caller is responsible for ensuring the source has
 * at least 4 bytes available from that offset. */
void gfx_sprite_blit_2x2_raw(const uint8_t *sprite_blob);

#endif /* OPENLORDS2_GFX_SPRITE_H */
