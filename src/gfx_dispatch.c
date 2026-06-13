/*
 * OpenLords2 — glyph dispatcher + clip setters (implementation).
 *
 * See gfx_dispatch.h for the per-function map, state model, and quirk
 * narrative.  Each function below matches its retail body essentially
 * statement-for-statement; control flow and order-of-writes match the
 * Ghidra decomp.  Where retail consults a DAT_* global, we either
 * read it from dispatcher-private state (this file's statics) or via
 * a getter on gfx_glyph / gfx_paint.
 */

#include "gfx_dispatch.h"

#include "gfx_drawcap.h"
#include "gfx_glyph.h"
#include "gfx_paint.h"
#include "gfx_sprite.h"
#include "gfx_sprite_rle.h"

/* Dispatcher-private state.  Variables grouped by which retail DAT_*
 * symbol they shadow; comments call out the offset/size encoded in
 * the original binary.  All zero-initialized at startup — matches
 * the BSS layout retail relies on. */

static const uint8_t *g_default_font;      /* DAT_005af8f0 — pointer
                                              compared verbatim. */

static int g_alt_clip_enabled;             /* DAT_005aeb84 */
static int g_alt_clip_left;                /* DAT_0052afdc */
static int g_alt_clip_bottom;              /* DAT_00553254 */

/* The alt-clip rect's top and right edges are inline constants in
 * retail FUN_00402a14 — not loaded from globals.  Preserve the
 * literals here so the per-call dispatch reads them from the same
 * "constant" place. */
#define GFX_DISPATCH_ALT_CLIP_TOP   0x18
#define GFX_DISPATCH_ALT_CLIP_RIGHT 0x1de

static int g_last_glyph_index;             /* DAT_005c9288 (dispatcher's
                                              own writer; font_metrics
                                              has a parallel one). */
static int g_glyph_height_short;           /* DAT_005aeb70 — short at
                                              descriptor +2; consumed by
                                              clip_y. */
static uint8_t g_flag_a;                   /* DAT_00591568 — byte at +0xc */
static uint8_t g_flag_b;                   /* DAT_0057d3c0 — byte at +0xe */

static int g_x_clip_mode;                  /* DAT_005cdd20 */
static int g_y_clip_mode;                  /* DAT_0058fe18 */

static int g_left_skip;                    /* DAT_0058fd50 */
static int g_top_skip;                     /* DAT_0058feac */
static int g_right_visible_w;              /* DAT_0058fe98 — initialized
                                              to glyph_w in clip_x; if
                                              right-clip kicks in, set to
                                              the visible portion. */
static int g_bottom_visible_h;             /* DAT_005aeb50 — same idea
                                              for vertical. */

/* ──────────────────────────────────────────────────────────────────
 * Configuration setters
 * ────────────────────────────────────────────────────────────────── */

void gfx_dispatch_set_alt_clip(int enabled, int left, int bottom)
{
    g_alt_clip_enabled = enabled;
    g_alt_clip_left    = left;
    g_alt_clip_bottom  = bottom;
}

void gfx_dispatch_set_alt_clip_enabled(int enabled)
{
    g_alt_clip_enabled = enabled;
}

void gfx_dispatch_set_default_font(const uint8_t *font_base)
{
    g_default_font = font_base;
}

/* ──────────────────────────────────────────────────────────────────
 * Test-facing readers + stagers
 * ────────────────────────────────────────────────────────────────── */

int gfx_dispatch_clip_x_mode(void)        { return g_x_clip_mode; }
int gfx_dispatch_clip_y_mode(void)        { return g_y_clip_mode; }
int gfx_dispatch_left_skip(void)          { return g_left_skip; }
int gfx_dispatch_top_skip(void)           { return g_top_skip; }
int gfx_dispatch_last_glyph_index(void)   { return g_last_glyph_index; }
int gfx_dispatch_glyph_height_short(void) { return g_glyph_height_short; }

void gfx_dispatch_set_glyph_height_short(int h) { g_glyph_height_short = h; }
void gfx_dispatch_set_clip_x_mode(int mode)     { g_x_clip_mode = mode; }
void gfx_dispatch_set_last_glyph_index(int idx) { g_last_glyph_index = idx; }

/* ──────────────────────────────────────────────────────────────────
 * Horizontal clip setter — port of FUN_0040464c
 * ────────────────────────────────────────────────────────────────── */

void gfx_dispatch_clip_x(int left, int right)
{
    int glyph_w  = gfx_glyph_glyph_w();
    int cursor_x = gfx_glyph_cursor_x();

    g_x_clip_mode      = 0;
    g_left_skip        = 0;
    g_right_visible_w  = glyph_w;

    if (glyph_w < 1) {
        g_x_clip_mode = 5;
    } else if (cursor_x < left) {
        if (left < glyph_w + cursor_x) {
            /* Glyph straddles the left edge — shave off the off-canvas
             * columns by advancing the source pointer and snapping the
             * dest cursor to `left`. */
            g_x_clip_mode = 1;
            g_left_skip   = left - cursor_x;
            cursor_x      = left;
            gfx_glyph_set_dest(cursor_x, gfx_glyph_cursor_y());
        } else {
            g_x_clip_mode = 5;
        }
    } else if (right - glyph_w < cursor_x) {
        if (cursor_x < right) {
            /* Glyph extends past the right edge — narrow the visible
             * portion to fit inside the clip rect. */
            g_x_clip_mode      = 2;
            g_right_visible_w  = right - cursor_x;
        } else {
            g_x_clip_mode = 5;
        }
    }

    int clipped_w;
    if (g_x_clip_mode == 5) {
        clipped_w = 0;
    } else {
        clipped_w = g_right_visible_w - g_left_skip;
    }

    int src_pad = glyph_w - clipped_w;
    /* Dst stride uses the canvas pitch (gfx_paint_canvas_width), NOT
     * the clip rect's right — see header doc. */
    int dst_pad = gfx_paint_canvas_width() - clipped_w;

    gfx_glyph_set_clip(clipped_w, src_pad, dst_pad);
}

/* ──────────────────────────────────────────────────────────────────
 * Vertical clip setter — port of FUN_0040477b
 * ────────────────────────────────────────────────────────────────── */

void gfx_dispatch_clip_y(int top, int bottom)
{
    int h_short  = g_glyph_height_short;
    int cursor_y = gfx_glyph_cursor_y();

    g_y_clip_mode       = 0;
    g_top_skip          = 0;
    g_bottom_visible_h  = h_short;

    if (h_short < 1) {
        g_y_clip_mode = 5;
    } else if (cursor_y < top) {
        if (top < h_short + cursor_y) {
            g_y_clip_mode = 3;
            g_top_skip    = top - cursor_y;
            cursor_y      = top;
            gfx_glyph_set_dest(gfx_glyph_cursor_x(), cursor_y);
        } else {
            g_y_clip_mode = 5;
        }
    } else if (bottom - h_short < cursor_y) {
        if (cursor_y < bottom) {
            g_y_clip_mode       = 4;
            g_bottom_visible_h  = bottom - cursor_y;
        } else {
            g_y_clip_mode = 5;
        }
    }

    int visible_rows;
    if (g_y_clip_mode == 5) {
        visible_rows = 0;
    } else {
        visible_rows = g_bottom_visible_h - g_top_skip;
    }

    /* Overwrite the blitter's row count (gfx_glyph's g_glyph_h /
     * retail DAT_005bb478) with the visible (post-clip) value.  The
     * dispatcher's earlier set_glyph staged the per-glyph advance byte
     * here as a placeholder; this is the real value the blitter
     * consumes.  Use set_glyph_h to avoid touching offset / width. */
    gfx_glyph_set_glyph_h(visible_rows);

    /* Propagate X-clip "skip" — if the glyph is off-canvas horizontally,
     * the Y-clip's no-clip 0 promotes to skip 5. */
    if (g_x_clip_mode == 5) {
        g_y_clip_mode = 5;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Glyph dispatcher — port of FUN_00402a14
 * ────────────────────────────────────────────────────────────────── */

int gfx_dispatch_glyph(const uint8_t *font_base,
                       const uint8_t *char_glyph_table,
                       unsigned char  char_code)
{
    /* Trace Studio v3 draw-step capture: one draw per glyph (meta = char code +
     * pre-advance cursor).  Early-out when not armed. */
    gfx_drawcap_draw(GFX_DRAWCAP_OP_GLYPH, (int)char_code,
                     gfx_glyph_cursor_x(), gfx_glyph_cursor_y());

    int idx_one_based = 0;
    if (char_glyph_table != 0) {
        idx_one_based = char_glyph_table[char_code];
    }

    if (idx_one_based == 0) {
        g_last_glyph_index = 0;
        return 0;
    }

    g_last_glyph_index = idx_one_based - 1;

    /* Descriptor slot inside font_base.  Retail formula:
     *   psVar2 = font + idx*16 + 8
     * Fields read:
     *   psVar2[0]    short  → width
     *   psVar2[1]    short  → height (consumed by clip_y)
     *   psVar2+4     dword  → source byte offset
     *   psVar2[+0xc] byte   → flag_a (no current consumer)
     *   psVar2[+0xd] byte   → per-glyph cursor_y advance (also serves
     *                          as the initial row count, soon
     *                          overwritten by clip_y)
     *   psVar2[+0xe] byte   → flag_b (no current consumer)
     */
    const uint8_t *slot = font_base + g_last_glyph_index * 16 + 8;

    int glyph_w     = (int)(uint16_t)(slot[0] | ((uint16_t)slot[1] << 8));
    int glyph_h_s   = (int)(uint16_t)(slot[2] | ((uint16_t)slot[3] << 8));
    int src_offset  = (int)( (uint32_t)slot[4]
                           | ((uint32_t)slot[5] << 8)
                           | ((uint32_t)slot[6] << 16)
                           | ((uint32_t)slot[7] << 24));
    g_flag_a = slot[0xc];
    int adv_byte = slot[0xd];
    g_flag_b = slot[0xe];

    /* Stage glyph descriptor for the blitter.  The initial row count
     * is the per-glyph advance byte; clip_y will overwrite it with the
     * visible row count via gfx_glyph_set_glyph_h. */
    gfx_glyph_set_glyph(src_offset, glyph_w, adv_byte);
    g_glyph_height_short = glyph_h_s;

    /* Advance cursor_y by the advance byte BEFORE running the clip
     * setup.  The clip setters consult the post-advance cursor_y. */
    int cursor_x = gfx_glyph_cursor_x();
    int cursor_y = gfx_glyph_cursor_y() + adv_byte;

    /* Default-font character-range tweaks — three independent shaves
     * of one pixel each.  The ranges overlap none of each other but
     * collectively cover lowercase a-m / s-w / a chunk of high-byte
     * characters (extended Latin?).  See engine-quirks.md. */
    if (font_base == g_default_font && g_default_font != 0) {
        if (char_code > 0x60 && char_code < 0x6e) cursor_y -= 1;
        if (char_code > 0x72 && char_code < 0x78) cursor_y -= 1;
        if (char_code > 0x7f && char_code < 0x85) cursor_y -= 1;
    }

    gfx_glyph_set_dest(cursor_x, cursor_y);

    /* Run the two clip setters.  In the alt-clip branch the rect is
     * (alt_clip_left, ALT_TOP, ALT_RIGHT, alt_clip_bottom).  In the
     * normal branch it's the bound canvas. */
    if (!g_alt_clip_enabled) {
        gfx_dispatch_clip_x(0, gfx_paint_canvas_width());
        gfx_dispatch_clip_y(0, gfx_paint_canvas_height());
    } else {
        gfx_dispatch_clip_x(g_alt_clip_left, GFX_DISPATCH_ALT_CLIP_RIGHT);
        gfx_dispatch_clip_y(GFX_DISPATCH_ALT_CLIP_TOP, g_alt_clip_bottom);
    }

    /* Apply the clip skips to the source pointer offset.  Retail:
     *   DAT_0058fe04 += DAT_005c9258 * DAT_0058feac;   (top_skip rows)
     *   DAT_0058fe04 += DAT_0058fd50;                  (left_skip cols)
     *
     * The multiply uses glyph_w (FULL row width), not clipped_w —
     * because the source rows are still full-width in the font blob.
     *
     * We re-stage via set_glyph with the post-clip row count
     * (clip_y already wrote it via gfx_glyph_set_glyph_h, but the
     * combined setter is the only way to update glyph_offset). */
    int new_offset = src_offset + glyph_w * g_top_skip + g_left_skip;
    /* Read the post-clip row count back via gfx_glyph (clip_y wrote
     * it).  Pass through set_glyph keeping width unchanged. */
    int row_count = gfx_glyph_glyph_h();
    gfx_glyph_set_glyph(new_offset, glyph_w, row_count);

    /* Dispatch one of the three blitters, gated on Y-clip != skip. */
    if (g_y_clip_mode != 5) {
        if (g_x_clip_mode == 1) {
            gfx_glyph_blit_left_clipped(font_base);
        } else if (g_x_clip_mode == 2) {
            gfx_glyph_blit_right_clipped(font_base);
        } else {
            /* No-clip OR x_clip == 5 — retail dispatches the unclipped
             * blitter in both cases (the 5 case is dead because the Y
             * propagation rule turns y_mode into 5 too, and we gate on
             * y_mode != 5 above).  Match retail's source-level branch. */
            gfx_glyph_blit(font_base);
        }
    }

    return glyph_w + 1;
}

/* ──────────────────────────────────────────────────────────────────
 * Sprite dispatchers — port of FUN_00404442 / FUN_00404547
 *
 * The two retail leaves differ only in which clip rectangle they
 * pass to the (shared) clip setters:
 *
 *   FUN_00404442 — canvas-clip: (0, 0, canvas_w, canvas_h).
 *   FUN_00404547 — caller-supplied (left, top, right, bottom).
 *
 * Everything else is identical: same 16-byte descriptor layout as
 * glyphs (read via the same field offsets), same clip-setter calls,
 * same source-pointer adjustment for top_skip / left_skip, same
 * dispatch table indexed by x_clip_mode.  The only difference from
 * the glyph dispatcher is the absence of the char→glyph table
 * indirection (sprite index is direct), the per-glyph cursor_y
 * advance step (the dispatcher's caller supplies a final x/y already),
 * and the default-font character-range tweaks.
 * ────────────────────────────────────────────────────────────────── */

void gfx_dispatch_stage_sprite_descriptor(const uint8_t *sprite_blob,
                                          int            sprite_index)
{
    const uint8_t *slot = sprite_blob + sprite_index * 16 + 8;

    int sprite_w  = (int)(uint16_t)(slot[0] | ((uint16_t)slot[1] << 8));
    int h_short   = (int)(uint16_t)(slot[2] | ((uint16_t)slot[3] << 8));
    int src_off   = (int)( (uint32_t)slot[4]
                         | ((uint32_t)slot[5] << 8)
                         | ((uint32_t)slot[6] << 16)
                         | ((uint32_t)slot[7] << 24));
    g_flag_a = slot[0xc];
    int row_count = slot[0xd];
    g_flag_b = slot[0xe];

    gfx_glyph_set_glyph(src_off, sprite_w, row_count);
    g_glyph_height_short = h_short;
}

void gfx_dispatch_apply_skip_and_dispatch_sprite(const uint8_t *sprite_blob)
{
    int sprite_w   = gfx_glyph_glyph_w();
    int src_off    = gfx_glyph_glyph_offset();
    int new_offset = src_off + sprite_w * g_top_skip + g_left_skip;
    int row_count  = gfx_glyph_glyph_h();
    gfx_glyph_set_glyph(new_offset, sprite_w, row_count);

    if (g_y_clip_mode != 5) {
        if (g_x_clip_mode == 1) {
            gfx_sprite_blit_left_clipped(sprite_blob);
        } else if (g_x_clip_mode == 2) {
            gfx_sprite_blit_right_clipped(sprite_blob);
        } else {
            gfx_sprite_blit(sprite_blob);
        }
    }
}

void gfx_dispatch_dispatch_sprite_rle(const uint8_t *sprite_blob)
{
    if (g_y_clip_mode == 5) return;
    if (g_x_clip_mode == 1) {
        gfx_sprite_rle_blit_left_clipped(sprite_blob);
    } else if (g_x_clip_mode == 2) {
        gfx_sprite_rle_blit_right_clipped(sprite_blob);
    } else {
        gfx_sprite_rle_blit(sprite_blob);
    }
}

void gfx_dispatch_sprite(const uint8_t *sprite_blob,
                         int            sprite_index,
                         int            x,
                         int            y)
{
    gfx_drawcap_draw(GFX_DRAWCAP_OP_SPRITE, sprite_index, x, y);
    gfx_dispatch_stage_sprite_descriptor(sprite_blob, sprite_index);
    gfx_glyph_set_dest(x, y);

    gfx_dispatch_clip_x(0, gfx_paint_canvas_width());
    gfx_dispatch_clip_y(0, gfx_paint_canvas_height());

    gfx_dispatch_apply_skip_and_dispatch_sprite(sprite_blob);
}

void gfx_dispatch_sprite_clipped(const uint8_t *sprite_blob,
                                 int            sprite_index,
                                 int            x,
                                 int            y,
                                 int            left,
                                 int            top,
                                 int            right,
                                 int            bottom)
{
    gfx_drawcap_draw(GFX_DRAWCAP_OP_SPRITE, sprite_index, x, y);
    gfx_dispatch_stage_sprite_descriptor(sprite_blob, sprite_index);
    gfx_glyph_set_dest(x, y);

    gfx_dispatch_clip_x(left, right);
    gfx_dispatch_clip_y(top, bottom);

    gfx_dispatch_apply_skip_and_dispatch_sprite(sprite_blob);
}
