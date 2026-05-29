/*
 * Unit tests for engine_button — the generic button-strip renderer
 * (port of FUN_0040cfd2).
 *
 * Each test builds a synthetic PL8-shaped sheet (8-byte header, 16-byte
 * descriptors at base+idx*0x10+8, pixel data at the descriptor's
 * glyph_offset) and a 640-wide canvas (the raw blits hardcode a 640-byte
 * dest stride — quirk #75), renders a button strip, and checks where the
 * sprite landed + which sheet/frame was chosen.
 */

#include "t.h"

#include "engine_button.h"
#include "engine_dirty_rect.h"
#include "gfx_paint.h"

enum { BIG_W = 640, BIG_H = 64 };

static uint8_t g_canvas[BIG_W * BIG_H];

static void canvas_clear(void)
{
    for (int i = 0; i < (int)sizeof g_canvas; i++) g_canvas[i] = 0;
    gfx_paint_bind_canvas(g_canvas, BIG_W, BIG_H);
}

static uint8_t at(int x, int y) { return g_canvas[x + y * BIG_W]; }

static void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

/* Write a 16-byte descriptor for sprite `idx` into `sheet`:
 *   +0 short glyph_w, +2 short height_short, +4 dword glyph_offset,
 *   +0xd byte glyph_h.  (flag bytes left zero.) */
static void set_desc(uint8_t *sheet, int idx, int w, int h, uint32_t off)
{
    uint8_t *d = sheet + idx * 0x10 + 8;
    put16(d + 0, (uint16_t)w);
    put16(d + 2, (uint16_t)h);
    put32(d + 4, off);
    d[0xd] = (uint8_t)h;
}

/* Fill an n×n densely-packed tile at `sheet + off` with byte
 * (r*n + c + 1). */
static void fill_tile(uint8_t *sheet, uint32_t off, int n)
{
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            sheet[off + r * n + c] = (uint8_t)(r * n + c + 1);
}

/* ──────────────────────────────────────────────────────────────── */

int test_engine_button_kind20_blits_32x32_at_origin(void)
{
    canvas_clear();
    static uint8_t sheet[0x4000];
    for (int i = 0; i < (int)sizeof sheet; i++) sheet[i] = 0;
    set_desc(sheet, /*idx=*/1, 32, 32, /*off=*/0x1000);
    fill_tile(sheet, 0x1000, 32);

    /* kind 0x20 (32×32, system sheet), type 0 → state-based, state 0 →
     * idx = sprite = 1.  Strip origin (0x10, 0x08); rec offset (4, 2). */
    engine_button_rec_t recs[1] = {
        { 4, 2, 1, 0x20, 0, 0, 0, 0, 0, 0, 0 },
    };
    engine_button_render_list(0x10, 0x08, recs, 1,
                              /*misc=*/NULL, /*system=*/sheet, NULL);

    int bx = 0x10 + 4, by = 0x08 + 2;
    T_ASSERT_EQ_U(at(bx + 0,  by + 0),  1);                 /* src (0,0)   */
    T_ASSERT_EQ_U(at(bx + 31, by + 31), (uint8_t)(31 * 32 + 31 + 1));
    T_ASSERT_EQ_U(at(bx - 1,  by + 0),  0);                 /* untouched   */
    T_ASSERT_EQ_U(at(bx + 32, by + 0),  0);
    return 0;
}

int test_engine_button_kind18_blits_24x24(void)
{
    canvas_clear();
    static uint8_t sheet[0x4000];
    for (int i = 0; i < (int)sizeof sheet; i++) sheet[i] = 0;
    set_desc(sheet, /*idx=*/3, 24, 24, /*off=*/0x2000);
    fill_tile(sheet, 0x2000, 24);

    /* kind 0x18 → 24×24, reads the system sheet (>= 0x18). */
    engine_button_rec_t recs[1] = {
        { 0, 0, 3, 0x18, 0, 0, 0, 0, 0, 0, 0 },
    };
    engine_button_render_list(0, 0, recs, 1, NULL, sheet, NULL);

    T_ASSERT_EQ_U(at(0, 0),  1);
    T_ASSERT_EQ_U(at(23, 23), (uint8_t)(23 * 24 + 23 + 1));
    T_ASSERT_EQ_U(at(24, 0),  0);   /* just past the 24-wide footprint   */
    T_ASSERT_EQ_U(at(0, 24),  0);
    return 0;
}

int test_engine_button_kind_below_0x18_uses_misc_sheet(void)
{
    canvas_clear();
    static uint8_t misc[0x4000], sys[0x4000];
    for (int i = 0; i < (int)sizeof misc; i++) { misc[i] = 0; sys[i] = 0; }
    /* idx 1 in BOTH sheets, distinct pixel tags. */
    set_desc(misc, 1, 16, 16, 0x1000); fill_tile(misc, 0x1000, 16);
    set_desc(sys,  1, 16, 16, 0x1000);
    for (int i = 0; i < 256; i++) sys[0x1000 + i] = 0xee;   /* wrong sheet */

    /* kind 0x10 (< 0x18) → must read `misc`, NOT `system`. */
    engine_button_rec_t recs[1] = {
        { 0, 0, 1, 0x10, 0, 0, 0, 0, 0, 0, 0 },
    };
    engine_button_render_list(0, 0, recs, 1, misc, sys, NULL);

    T_ASSERT_EQ_U(at(0, 0),  1);                  /* misc tile, not 0xee  */
    T_ASSERT_EQ_U(at(15, 15), (uint8_t)(15 * 16 + 15 + 1));
    return 0;
}

int test_engine_button_type4_pressed_picks_next_frame(void)
{
    canvas_clear();
    static uint8_t sheet[0x4000];
    for (int i = 0; i < (int)sizeof sheet; i++) sheet[i] = 0;
    /* sprite base 1 (released frame) and 2 (pressed frame). */
    set_desc(sheet, 1, 16, 16, 0x1000);
    set_desc(sheet, 2, 16, 16, 0x1800);
    for (int i = 0; i < 256; i++) sheet[0x1000 + i] = 0x11;  /* frame 1 */
    for (int i = 0; i < 256; i++) sheet[0x1800 + i] = 0x22;  /* frame 2 */

    /* type 4, pressed → idx = sprite + 1 = 2 → the 0x22 frame.  kind
     * 0x10 keeps it 16×16 from the (single) sheet. */
    engine_button_rec_t recs[1] = {
        { 0, 0, 1, 0x10, 0, /*state=*/0, /*pressed=*/1, 0, /*type=*/4, 0, 0 },
    };
    engine_button_render_list(0, 0, recs, 1, sheet, sheet, NULL);

    T_ASSERT_EQ_U(at(0, 0), 0x22);
    return 0;
}

int test_engine_button_type0_state_picks_next_frame(void)
{
    canvas_clear();
    static uint8_t sheet[0x4000];
    for (int i = 0; i < (int)sizeof sheet; i++) sheet[i] = 0;
    set_desc(sheet, 1, 16, 16, 0x1000);
    set_desc(sheet, 2, 16, 16, 0x1800);
    for (int i = 0; i < 256; i++) sheet[0x1000 + i] = 0x11;
    for (int i = 0; i < 256; i++) sheet[0x1800 + i] = 0x22;

    /* type 0 (not 4/5), state != 0 → idx = sprite + 1. */
    engine_button_rec_t recs[1] = {
        { 0, 0, 1, 0x10, 0, /*state=*/1, /*pressed=*/0, 0, /*type=*/0, 0, 0 },
    };
    engine_button_render_list(0, 0, recs, 1, sheet, sheet, NULL);

    T_ASSERT_EQ_U(at(0, 0), 0x22);
    return 0;
}

int test_engine_button_null_sheet_skips(void)
{
    canvas_clear();
    /* kind 0x10 needs the misc sheet; pass NULL → button skipped, no
     * crash, canvas untouched. */
    engine_button_rec_t recs[1] = {
        { 0, 0, 1, 0x10, 0, 0, 0, 0, 0, 0, 0 },
    };
    engine_button_render_list(0, 0, recs, 1, /*misc=*/NULL, /*system=*/NULL,
                              NULL);
    T_ASSERT_EQ_U(at(0, 0), 0);
    return 0;
}

int test_engine_button_marks_dirty_per_button(void)
{
    canvas_clear();
    static uint8_t sheet[0x4000];
    for (int i = 0; i < (int)sizeof sheet; i++) sheet[i] = 0;
    set_desc(sheet, 1, 32, 32, 0x1000);
    fill_tile(sheet, 0x1000, 32);

    engine_dirty_rect_state_t dirty = {
        0, 0x7fff, -0x7fff, 0x7fff, -0x7fff, 0,
    };

    engine_button_rec_t recs[1] = {
        { 4, 2, 1, 0x20, 0, 0, 0, 0, 0, 0, 0 },
    };
    engine_button_render_list(0x10, 0x08, recs, 1, NULL, sheet, &dirty);

    /* A 3×3-tile box at (0x14, 0x0a) should have been marked dirty. */
    T_ASSERT(dirty.dirty_mark != 0);
    return 0;
}
