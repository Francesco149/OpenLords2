/*
 * Unit tests for engine_hud_menubar — the strategic-map top menu bar.
 *
 * The deterministic, font-free pieces are exercised here:
 *   - engine_hud_menubar_tile_strip (FUN_00409e09): the 24×24 parchment
 *     tiler — verify each column lands at col*24 with sprite index
 *     0xc4 + (col & 7), cycling through the eight parchment tiles.
 *   - engine_hud_menubar_default_table: the DAT_004dc428 layout.
 *
 * The File/Options/Help LABEL rendering (engine_hud_menubar_labels) drives
 * the localization + font + glyph machinery; it is validated end-to-end —
 * pixel-perfect vs retail — by the live --strat-map render-parity harness
 * (tools/strat_map_live_parity.py), so it is not re-stubbed here.
 *
 * Sheet layout mirrors the button test: 8-byte header, 16-byte descriptors
 * at base + idx*0x10 + 8 (glyph_offset at +4), pixel data densely packed
 * at the descriptor's glyph_offset.  The raw 24×24 blit hardcodes a
 * 640-byte dest stride (quirk #75), so the canvas is 640 wide.
 */

#include "t.h"

#include "engine_hud_menubar.h"
#include "gfx_paint.h"

enum { BIG_W = 640, BIG_H = 32 };

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

static void set_desc(uint8_t *sheet, int idx, int w, int h, uint32_t off)
{
    uint8_t *d = sheet + idx * 0x10 + 8;
    put16(d + 0, (uint16_t)w);
    put16(d + 2, (uint16_t)h);
    put32(d + 4, off);
    d[0xd] = (uint8_t)h;
}

/* A chrome sheet whose eight parchment tiles 0xc4..0xcb are each a solid
 * 24×24 block tagged with their own sprite index, so the byte at any tile
 * pixel identifies which descriptor was blitted there. */
static uint8_t g_sheet[0x20000];

static void build_parchment_sheet(void)
{
    for (int i = 0; i < (int)sizeof g_sheet; i++) g_sheet[i] = 0;
    for (int t = 0; t < 8; t++) {
        int idx = 0xc4 + t;
        uint32_t off = 0x1000 + (uint32_t)t * 0x400;   /* 24*24=576 ≤ 0x400 */
        set_desc(g_sheet, idx, 24, 24, off);
        for (int i = 0; i < 24 * 24; i++) g_sheet[off + i] = (uint8_t)idx;
    }
}

/* ──────────────────────────────────────────────────────────────── */

int test_hud_menubar_tile_strip_lands_each_column(void)
{
    canvas_clear();
    build_parchment_sheet();

    engine_hud_menubar_tile_strip(g_sheet, 0, 0, 10, 1);

    /* Columns 0..9 → sprite 0xc4 + (col & 7), placed at (col*24, 0). */
    for (int col = 0; col < 10; col++) {
        uint8_t want = (uint8_t)(0xc4 + (col & 7));
        T_ASSERT_EQ_U(at(col * 24 + 0,  0),  want);
        T_ASSERT_EQ_U(at(col * 24 + 23, 23), want);
    }
    /* The 8-tile cycle: col 8 == col 0, col 9 == col 1. */
    T_ASSERT_EQ_U(at(8 * 24, 0), at(0, 0));
    T_ASSERT_EQ_U(at(9 * 24, 0), at(1 * 24, 0));
    return 0;
}

int test_hud_menubar_tile_strip_honors_origin(void)
{
    canvas_clear();
    build_parchment_sheet();

    /* The right-edge strip retail draws: FUN_00409e09(0x250, 0, 2, 1). */
    engine_hud_menubar_tile_strip(g_sheet, 0x250, 0, 2, 1);

    T_ASSERT_EQ_U(at(0x250 + 0,  0), 0xc4);   /* col 0 */
    T_ASSERT_EQ_U(at(0x250 + 24, 0), 0xc5);   /* col 1 */
    /* Nothing before the origin. */
    T_ASSERT_EQ_U(at(0x250 - 1, 0), 0);
    return 0;
}

int test_hud_menubar_tile_strip_null_sheet_is_noop(void)
{
    canvas_clear();
    engine_hud_menubar_tile_strip(NULL, 0, 0, 10, 1);
    T_ASSERT_EQ_U(at(0, 0), 0);
    T_ASSERT_EQ_U(at(100, 0), 0);
    return 0;
}

int test_hud_menubar_tile_strip_aborts_on_bad_offset(void)
{
    canvas_clear();
    build_parchment_sheet();
    /* Poison tile 0xc6 (col 2) with an out-of-range source offset — retail
     * aborts the whole strip there, so cols 0..1 draw and 2.. do not. */
    set_desc(g_sheet, 0xc6, 24, 24, 400001);

    engine_hud_menubar_tile_strip(g_sheet, 0, 0, 10, 1);

    T_ASSERT_EQ_U(at(0 * 24, 0), 0xc4);   /* col 0 drawn */
    T_ASSERT_EQ_U(at(1 * 24, 0), 0xc5);   /* col 1 drawn */
    T_ASSERT_EQ_U(at(2 * 24, 0), 0);      /* col 2 aborted → untouched */
    T_ASSERT_EQ_U(at(3 * 24, 0), 0);
    return 0;
}

int test_hud_menubar_default_table_layout(void)
{
    int16_t buf[24];
    int16_t *t = engine_hud_menubar_default_table(buf);
    T_ASSERT(t == buf);
    for (int i = 0; i < 3; i++) {
        T_ASSERT_EQ_U(t[i * 8 + 0], 10);       /* x   */
        T_ASSERT_EQ_U(t[i * 8 + 2], 6);        /* y   */
        T_ASSERT_EQ_U(t[i * 8 + 3], i + 1);    /* loc id 1/2/3 */
    }
    return 0;
}
