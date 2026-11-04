/*
 * OpenLords2 — RLE sprite blitter (implementation).
 *
 * See gfx_sprite_rle.h for the per-function map and the relationship
 * to the (raw-pixel) first family in gfx_sprite.c.  This file holds
 * the three RLE blitter leaves plus the private row-skip helper they
 * all share in the prologue.
 *
 * The three blitter bodies follow retail control flow statement-for-
 * statement.  Where retail computes the dest pointer via the bare
 *   DAT_004ea1a8 + DAT_00591524 + DAT_00591528 * DAT_004eb274
 * we route through gfx_paint + gfx_glyph getters so the canvas binding
 * and cursor live in their owner modules.
 */

#include "gfx_sprite_rle.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_paint.h"

/* Recompute the dest pointer from the gfx_paint canvas binding + the
 * staged cursor.  Mirrors the prologue of every retail RLE blitter:
 *   DAT_004ea1a8 + DAT_00591524 + DAT_00591528 * DAT_004eb274 */
static uint8_t *dest_origin(void)
{
    uint8_t *pixels = gfx_paint_canvas_pixels();
    if (!pixels) return 0;
    int      width  = gfx_paint_canvas_width();
    return pixels + gfx_glyph_cursor_x() + gfx_glyph_cursor_y() * width;
}

/* Port of FUN_004b495a.  Walk past `top_skip` rows of RLE source.
 *
 * Pre-test outer loop bound by `top_skip`; the inner loop walks one
 * row of `sprite_w` pixels' worth of RLE opcodes.  Returns the pointer
 * to the start of the first un-skipped row.
 *
 * Both the skip-opcode (0x00) and literal-opcode (0x01..) arms decrement
 * the per-row pixel counter.  Literal opcodes advance the src pointer
 * by `opcode + 1` (1 opcode byte + N data bytes); skip opcodes advance
 * by exactly 2 (1 opcode + 1 count byte).
 *
 * `top_skip == 0` is a no-op — the outer `while (rows > 0)` test means
 * no rows are walked, src returns pointing at the natural row 0.
 *
 * Retail keeps the opcode in EAX across a row (`xor eax, eax` per row, then
 * `mov al, [esi]` per opcode): a literal opcode 0xff leaves EAX = 0x100 after
 * its `add eax, 1`, and every later opcode of that row reads as 0x100 + byte.
 * The encoder never emits 0xff (literal runs are 1..16), but `acc` models it. */
const uint8_t *rle_skip_top_rows(const uint8_t *sprite_blob)
{
    const uint8_t *src = sprite_blob + gfx_glyph_glyph_offset();

    int rows     = gfx_dispatch_top_skip();

    while (rows > 0) {
        rows -= 1;
        int pixels = gfx_glyph_glyph_w();   /* movsx, re-read per row */
        uint32_t acc = 0;
        while (pixels > 0) {
            acc = (acc & ~0xffu) | *src;
            if ((uint8_t)acc == 0) {
                acc = (acc & ~0xffu) | src[1];
                pixels -= (int)acc;
                src    += 2;
            } else {
                pixels -= (int)acc;
                acc    += 1;
                src    += acc;                 /* opcode byte + N data bytes */
            }
        }
    }
    return src;
}

/* Faithful translation of FUN_004b4537 — the unclipped RLE blit.
 *
 * Retail emits a 16-way switch over the opcode value (cases 0x01..0x10
 * plus a default arm).  Each arm copies N literal bytes (N inferred
 * from the case label) and advances src by N+1, dst by N, and the
 * per-row pixel counter by -N.  The 0x00 arm decompresses to "skip N
 * pixels" (advance dst by N).  Preserved verbatim as a switch
 * statement — the compiler is free to fold equal arms.
 *
 * Note on the `else` arm: in retail it copies 4+4+4+2+1 = 15 bytes
 * (opcode 0x0F or any value not explicitly matched).  Combined with
 * the 0x10 arm (16 bytes), this means a byte value of 0x11..0xFF in
 * the RLE stream is misinterpreted as a 15-byte literal — one byte
 * of "extra" data ends up unaddressed in the dest.  No retail blob is
 * known to emit such values, but the misalignment is reachable. */
void gfx_sprite_rle_blit(const uint8_t *sprite_blob)
{
    uint8_t *dst = dest_origin();
    if (!dst) return;

    const uint8_t *src = rle_skip_top_rows(sprite_blob);
    int rows = gfx_glyph_glyph_h();

    /* Retail's `do { ... } while (iVar5 != 0 && bVar3);` outer loop has
     * the same post-test shape as the glyph/sprite blitter outer loops:
     * one iteration runs even when rows starts at 0.  Preserved. */
    do {
        int pixels = gfx_glyph_glyph_w();
        while (pixels > 0) {
            uint8_t opcode = *src;
            switch (opcode) {
            case 0x00: {
                /* Transparency run: advance dst by N, src by 2. */
                uint8_t skip = src[1];
                dst    += skip;
                src    += 2;
                pixels -= skip;
                break;
            }
            case 0x01:
                dst[0] = src[1];
                src += 2; dst += 1; pixels -= 1;
                break;
            case 0x02:
                dst[0] = src[1]; dst[1] = src[2];
                src += 3; dst += 2; pixels -= 2;
                break;
            case 0x03:
                /* Retail copies a 3-run as TWO words (`mov ax,[esi+1];
                 * mov [edi],ax; mov ax,[esi+3]; mov [edi+2],ax`): 4 bytes,
                 * so dst[3] gets src[4] — the NEXT opcode byte — before dst
                 * advances by 3.  A following literal overwrites it; a
                 * following skip (opcode 0x00) leaves a 0 pixel there, and a
                 * 3-run that ends the row leaves the next row's opcode one
                 * pixel past the row (rhost media shadow, f1321 endturn-2). */
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                src += 4; dst += 3; pixels -= 3;
                break;
            case 0x04:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                src += 5; dst += 4; pixels -= 4;
                break;
            case 0x05:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5];
                src += 6; dst += 5; pixels -= 5;
                break;
            case 0x06:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6];
                src += 7; dst += 6; pixels -= 6;
                break;
            case 0x07:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7];
                src += 8; dst += 7; pixels -= 7;
                break;
            case 0x08:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                src += 9; dst += 8; pixels -= 8;
                break;
            case 0x09:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9];
                src += 10; dst += 9; pixels -= 9;
                break;
            case 0x0a:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9]; dst[9] = src[10];
                src += 11; dst += 10; pixels -= 10;
                break;
            case 0x0b:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9]; dst[9] = src[10]; dst[10] = src[11];
                src += 12; dst += 11; pixels -= 11;
                break;
            case 0x0c:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9]; dst[9] = src[10]; dst[10] = src[11]; dst[11] = src[12];
                src += 13; dst += 12; pixels -= 12;
                break;
            case 0x0d:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9]; dst[9] = src[10]; dst[10] = src[11]; dst[11] = src[12];
                dst[12] = src[13];
                src += 14; dst += 13; pixels -= 13;
                break;
            case 0x0e:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9]; dst[9] = src[10]; dst[10] = src[11]; dst[11] = src[12];
                dst[12] = src[13]; dst[13] = src[14];
                src += 15; dst += 14; pixels -= 14;
                break;
            case 0x10:
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9]; dst[9] = src[10]; dst[10] = src[11]; dst[11] = src[12];
                dst[12] = src[13]; dst[13] = src[14]; dst[14] = src[15]; dst[15] = src[16];
                src += 17; dst += 16; pixels -= 16;
                break;
            default:
                /* Retail's else arm — covers opcode 0x0F AND anything
                 * not explicitly matched (0x11..0xFF).  Always emits a
                 * 15-byte literal.  See quirk on opcode 0x0F folding. */
                dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[4];
                dst[4] = src[5]; dst[5] = src[6]; dst[6] = src[7]; dst[7] = src[8];
                dst[8] = src[9]; dst[9] = src[10]; dst[10] = src[11]; dst[11] = src[12];
                dst[12] = src[13]; dst[13] = src[14]; dst[14] = src[15];
                src += 16; dst += 15; pixels -= 15;
                break;
            }
        }
        /* Advance dest to the start of the next row.  Retail uses
         *   pcVar7 = pcVar7 + _DAT_005cd400
         * which the wrapper has set to (0x280 - sprite_w) — i.e. the
         * canvas-pitch-minus-row-width gap.  The wrapper hard-codes
         * 0x280 (= 640) instead of consulting canvas_width because
         * retail's canvas pitch IS 640 (the 640×480 framebuffer).
         * Use the gfx_glyph dst_stride_pad accessor — clip_x writes
         * the same field via canvas_width, so for any canvas-clipped
         * call dst_stride_pad == canvas_w - clipped_w (== canvas_w
         * - sprite_w when no clip applies). */
        dst += gfx_glyph_dst_stride_pad();

        int next = rows - 1;
        int gt   = (0 < rows);
        rows = next;
        if (!(next != 0 && gt)) return;
    } while (1);
}

/* Port of FUN_004b4814.  Left-clipped RLE blit.
 *
 * Pre-shifts the per-row dest pointer LEFT by `left_skip`.  Tracks two
 * counters per row:
 *   - iVar8 (= pixels): natural row-pixel budget (sprite_w).  Drives
 *     the per-row loop; decremented by both opcode-skip and literal
 *     advances.
 *   - iVar6 (= clip_counter): starts at left_skip, decrements with
 *     EVERY advance (skip or literal).  Literal writes are gated on
 *     `clip_counter < 1` — i.e. they fire only once the cumulative
 *     advance has consumed the left-skip allotment.
 *
 * `left_skip` lives in DAT_0059150c — the same global the first family
 * uses as src_stride_pad.  For an x_clip_mode == 1 dispatch these are
 * arithmetically equal (src_stride_pad = sprite_w - clipped_w =
 * sprite_w - (sprite_w - left_skip) = left_skip), so reading via
 * gfx_glyph_src_stride_pad() is faithful. */
void gfx_sprite_rle_blit_left_clipped(const uint8_t *sprite_blob)
{
    uint8_t *dst_row_origin = dest_origin();
    if (!dst_row_origin) return;

    const uint8_t *src = rle_skip_top_rows(sprite_blob);
    int rows           = gfx_glyph_glyph_h();
    int canvas_w       = gfx_paint_canvas_width();
    int left_skip      = gfx_glyph_src_stride_pad();

    do {
        int pixels        = gfx_glyph_glyph_w();
        uint8_t *dst      = dst_row_origin - left_skip;
        int clip_counter  = left_skip;

        while (pixels > 0) {
            uint8_t opcode = *src;
            if (opcode == 0) {
                uint8_t skip_count = src[1];
                dst           += skip_count;
                src           += 2;
                clip_counter  -= skip_count;
                pixels        -= skip_count;
            } else {
                src += 1;
                int run = opcode;
                do {
                    if (clip_counter < 1) {
                        *dst = *src;
                    }
                    src           += 1;
                    dst           += 1;
                    pixels        -= 1;
                    clip_counter  -= 1;
                    int next = run - 1;
                    int gt   = (0 < run);
                    run = next;
                    if (!(next != 0 && gt)) break;
                } while (1);
            }
        }
        dst_row_origin += canvas_w;

        int next = rows - 1;
        int gt   = (0 < rows);
        rows = next;
        if (!(next != 0 && gt)) return;
    } while (1);
}

/* Port of FUN_004b48b8.  Right-clipped RLE blit.
 *
 * Tracks two counters per row:
 *   - iVar8 (= pixels): natural row-pixel budget (sprite_w).  Drives
 *     the per-row loop.
 *   - iVar6 (= visible_budget): starts at clipped_w, decrements with
 *     EVERY advance (skip or literal).  Literal writes are gated on
 *     `visible_budget > 0` — i.e. they fire only while the visible
 *     portion of the row hasn't been exhausted.
 *
 * Once visible_budget reaches 0, writes stop; the source pointer keeps
 * walking until pixels == 0 (so the next row's RLE start lines up). */
void gfx_sprite_rle_blit_right_clipped(const uint8_t *sprite_blob)
{
    uint8_t *dst_row_origin = dest_origin();
    if (!dst_row_origin) return;

    const uint8_t *src     = rle_skip_top_rows(sprite_blob);
    int rows               = gfx_glyph_glyph_h();
    int canvas_w           = gfx_paint_canvas_width();
    int clipped_w          = gfx_glyph_clipped_w();

    do {
        int pixels         = gfx_glyph_glyph_w();
        int visible_budget = clipped_w;
        uint8_t *dst       = dst_row_origin;

        while (pixels > 0) {
            uint8_t opcode = *src;
            if (opcode == 0) {
                uint8_t skip_count = src[1];
                dst             += skip_count;
                src             += 2;
                visible_budget  -= skip_count;
                pixels          -= skip_count;
            } else {
                src += 1;
                int run = opcode;
                do {
                    if (visible_budget > 0) {
                        *dst = *src;
                    }
                    src             += 1;
                    dst             += 1;
                    visible_budget  -= 1;
                    pixels          -= 1;
                    int next = run - 1;
                    int gt   = (0 < run);
                    run = next;
                    if (!(next != 0 && gt)) break;
                } while (1);
            }
        }
        dst_row_origin += canvas_w;

        int next = rows - 1;
        int gt   = (0 < rows);
        rows = next;
        if (!(next != 0 && gt)) return;
    } while (1);
}
