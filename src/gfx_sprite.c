/*
 * OpenLords2 — sprite blitter (implementation).
 *
 * See gfx_sprite.h for the per-function map and the relationship to
 * the gfx_glyph blitter trio.  This file holds the three sprite-class
 * leaves; they consume the same staged state the glyph blitters do,
 * read via gfx_glyph_* getters.
 *
 * The unclipped retail body (FUN_004b43b1) carries an unusual partial
 * 4-byte unroll: when glyph_w is a multiple of 4, the FIRST four
 * bytes go through an unrolled block, then a `goto LAB_004b43f3`
 * drops into the byte-at-a-time tail loop for the remainder of the
 * row.  When glyph_w is NOT a multiple of 4, the unrolled block is
 * skipped entirely and the row runs byte-at-a-time.  The unroll
 * therefore amortizes nothing for rows wider than 4 bytes — almost
 * certainly an MSVC 4.20 codegen artifact rather than hand
 * optimization.  Preserved verbatim so the executed memory traffic
 * matches retail exactly.
 *
 * Both clipped leaves (FUN_004b446d / FUN_004b44d2) share one body —
 * verified byte-identical by r2 pxd.  The dispatcher's clip-mode tag
 * picks between them at call time.
 */

#include "gfx_sprite.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_paint.h"

#include <string.h>

/* Recompute the dest pointer from the gfx_paint canvas binding + the
 * staged cursor.  Mirrors the prologue of every retail blitter:
 *   DAT_004ea1a8 + DAT_00591524 + DAT_00591528 * DAT_004eb274 */
static uint8_t *dest_origin(void)
{
    uint8_t *pixels = gfx_paint_canvas_pixels();
    if (!pixels) return 0;
    int      width  = gfx_paint_canvas_width();
    return pixels + gfx_glyph_cursor_x() + gfx_glyph_cursor_y() * width;
}

/* Faithful translation of FUN_004b43b1.  The outer-loop condition
 * `(glyph_w & 3) != 0` selects between two row shapes:
 *
 *   - glyph_w not a multiple of 4 → enter the byte-at-a-time tail
 *     loop, which processes the entire row one byte at a time.
 *
 *   - glyph_w a multiple of 4 → execute the unrolled 4-byte block
 *     (one row prefix), then jump into the tail loop with the
 *     remainder counter (glyph_w - 4).  If glyph_w == 4 the
 *     remainder is 0 and the goto is skipped (one 4-byte block
 *     completes the row).
 *
 * Note that this is NOT a Duff's device: the unrolled block fires
 * AT MOST ONCE per row, regardless of how much wider than 4 the row
 * is.  Anything past byte 4 goes through the tail loop the same way
 * the non-aligned path does.  Preserved verbatim for byte-for-byte
 * memory-traffic equivalence with retail.
 *
 * Post-test guard on the row counter is the same shape as the glyph
 * blitter's inner loops: one final iteration runs even when the
 * counter starts at 0.  The dispatcher avoids this case by setting
 * y_clip_mode == 5 (skip) for zero-height sprites and bailing before
 * the blit call. */
void gfx_sprite_blit(const uint8_t *sprite_blob)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src = sprite_blob + gfx_glyph_glyph_offset();
    int            dst_pad = gfx_glyph_dst_stride_pad();
    int            rows    = gfx_glyph_glyph_h();

    do {
        int width = gfx_glyph_glyph_w();
        int tail_count;
        int run_tail;

        if ((width & 3) != 0) {
            /* Non-multiple-of-4 width: byte-at-a-time over the
             * entire row.  Skip the unrolled prefix entirely. */
            tail_count = width;
            run_tail   = 1;
        } else {
            /* Multiple-of-4 width: first 4 bytes unrolled.  Note
             * this also fires for width == 0 — writing 4 bytes per
             * row regardless.  Latent quirk; dispatcher gates it. */
            if (src[0] != 0) dst[0] = src[0];
            if (src[1] != 0) dst[1] = src[1];
            if (src[2] != 0) dst[2] = src[2];
            if (src[3] != 0) dst[3] = src[3];
            src += 4;
            dst += 4;
            tail_count = width - 4;
            /* Retail: `tail_count != 0 && (int)width > 3`.  For
             * width-zero this evaluates to (anything && false) ==
             * false — preserved (no tail run; the 4-byte over-write
             * is the visible part of the quirk). */
            run_tail = (tail_count != 0 && width > 3);
        }

        if (run_tail) {
            do {
                if (*src != 0) *dst = *src;
                src++;
                dst++;
                int next = tail_count - 1;
                int gt   = (0 < tail_count);
                tail_count = next;
                if (!(next != 0 && gt)) break;
            } while (1);
        }

        dst += dst_pad;
        int next = rows - 1;
        int gt   = (0 < rows);
        rows = next;
        if (!(next != 0 && gt)) return;
    } while (1);
}

/* Port of FUN_004b432e @ 0x4b432e (131 B).  Unclipped *opaque* blit —
 * the sibling of gfx_sprite_blit (FUN_004b43b1) that copies every
 * source byte verbatim INCLUDING zeros (no transparency test).  Used
 * for solid sprite frames such as the title-screen name-entry box
 * background (FUN_0041f321 → FUN_0040a567).
 *
 * Two structural differences from the transparent leaf:
 *
 *   - Row count is the descriptor's +2 SHORT height (DAT_005aeb70,
 *     gfx_dispatch_glyph_height_short), NOT the +0xd byte height
 *     (DAT_005bb478) the transparent/clipped leaves use.  The staging
 *     wrapper (gfx_sprite_render_opaque_from_blob) reads both off the
 *     16-byte descriptor; for the box sprite they coincide, but the
 *     opaque blit dereferences the short — preserved verbatim.
 *
 *   - When glyph_w is a multiple of 4 the row is copied as
 *     `glyph_w / 4` aligned dwords; otherwise byte-at-a-time.  Both
 *     are plain copies (memcpy of glyph_w bytes is byte-for-byte
 *     equivalent and is what the dword path amounts to).  Retail's
 *     post-test row-counter guard (one iteration even at height 0) is
 *     preserved; the wrapper gates height >= 1 before calling. */
void gfx_sprite_blit_opaque(const uint8_t *sprite_blob)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src     = sprite_blob + gfx_glyph_glyph_offset();
    int            width   = gfx_glyph_glyph_w();
    int            dst_pad  = gfx_glyph_dst_stride_pad();
    int            rows    = gfx_dispatch_glyph_height_short();

    do {
        memcpy(dst, src, (size_t)width);
        src += width;
        dst += width + dst_pad;
        int next = rows - 1;
        int gt   = (0 < rows);
        rows = next;
        if (!(next != 0 && gt)) return;
    } while (1);
}

/* Shared body for the byte-identical clipped duplicates
 * (FUN_004b446d / FUN_004b44d2).  Copy-byte (transparency-aware);
 * reads `clipped_w` cols per row with `src_stride_pad` between rows. */
static void sprite_blit_clipped(const uint8_t *sprite_blob)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src      = sprite_blob + gfx_glyph_glyph_offset();
    int            src_pad  = gfx_glyph_src_stride_pad();
    int            dst_pad  = gfx_glyph_dst_stride_pad();
    int            rows     = gfx_glyph_glyph_h();

    do {
        int cols = gfx_glyph_clipped_w();
        do {
            if (*src != 0) *dst = *src;
            src++;
            dst++;
            int next = cols - 1;
            int gt   = (0 < cols);
            cols = next;
            if (!(next != 0 && gt)) break;
        } while (1);
        src += src_pad;
        dst += dst_pad;
        int next = rows - 1;
        int gt   = (0 < rows);
        rows = next;
        if (!(next != 0 && gt)) break;
    } while (1);
}

void gfx_sprite_blit_left_clipped(const uint8_t *sprite_blob)
{
    sprite_blit_clipped(sprite_blob);
}

void gfx_sprite_blit_right_clipped(const uint8_t *sprite_blob)
{
    sprite_blit_clipped(sprite_blob);
}

/* Port of FUN_004b49f7 @ 0x4b49f7.  Fixed 16×16 opaque raw blit.
 *
 * Retail is a fully-unrolled 16 rows × 4 dwords block: each
 * `puVar2[N] = puVar1[M]` is an aligned uint32_t copy with the dest
 * stride encoded into the literal indices (N jumps by 0xa0 dwords =
 * 640 bytes between rows, M is contiguous).  The port uses memcpy
 * over uint8_t * since the source is not guaranteed to be 4-byte
 * aligned (the dispatcher's glyph_offset is byte-granular) — memcpy
 * generates equivalent codegen on x86 without invoking UB on
 * unaligned loads under ASan/UBSan host tests.
 *
 * Hardcoded 640-byte dest stride is preserved verbatim — see
 * engine-quirks #75 for the narrative. */
void gfx_sprite_blit_16x16_raw(const uint8_t *sprite_blob)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src = sprite_blob + gfx_glyph_glyph_offset();
    for (int row = 0; row < 16; row++) {
        memcpy(dst, src, 16);
        src += 16;
        dst += 640;     /* hardcoded — NOT the dynamic canvas pitch */
    }
}

/* Port of FUN_004b5333 @ 0x4b5333.  Fixed 32×32 opaque raw blit.
 *
 * Same structure as the 16×16 sibling — retail is 32 rows × 8 dword
 * copies fully unrolled, with the dest stride baked into the literal
 * indices (each row's index jumps by 0xa0 dwords = 640 bytes between
 * row groups).  Source is densely packed: each row is 32 bytes,
 * 32 rows = 1024 bytes per sprite.
 *
 * Hardcoded 640-byte dest stride preserved — same quirk #75. */
void gfx_sprite_blit_32x32_raw(const uint8_t *sprite_blob)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src = sprite_blob + gfx_glyph_glyph_offset();
    for (int row = 0; row < 32; row++) {
        memcpy(dst, src, 32);
        src += 32;
        dst += 640;     /* hardcoded — NOT the dynamic canvas pitch */
    }
}

/* Port of FUN_004b49b7 @ 0x4b49b7.  Fixed 2×2 opaque raw blit — the
 * micro-sibling of the 16×16 / 32×32 raw blits.  Used for mini-map
 * tile rendering where each world cell maps to a 2×2-pixel block.
 *
 * Retail body is 2 word-indexed copies:
 *
 *   *(short *)dst        = *(short *)src;      // 2 bytes at row 0
 *   ((short *)dst)[0x140] = ((short *)src)[1]; // 2 bytes at row 1
 *
 * `[0x140]` indexes a 16-bit element, so offset = 0x140 * 2 = 0x280
 * bytes = the same hardcoded 640-byte stride used by the larger
 * siblings (quirk #75).  Port uses memcpy over uint8_t * for unaligned
 * safety; codegen matches retail at -O2. */
void gfx_sprite_blit_2x2_raw(const uint8_t *sprite_blob)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src = sprite_blob + gfx_glyph_glyph_offset();
    memcpy(dst,       src,     2);
    memcpy(dst + 640, src + 2, 2);
}
