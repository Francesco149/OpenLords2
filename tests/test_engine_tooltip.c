/*
 * Unit tests for engine_tooltip — the tooltip subsystem (FUN_00476e95
 * family): the per-screen mode table, the strategic hotspot classifier
 * (FUN_00477320), and the arm/clear/render tick.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "t.h"
#include "engine_tooltip.h"
#include "gfx_dispatch.h"
#include "gfx_paint.h"
#include "gfx_glyph_table.h"
#include "gfx_text.h"
#include "io.h"
#include "localization.h"

/* ─── mode table ────────────────────────────────────────────────────── */

int test_tooltip_mode_table(void)
{
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x00), 1);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x08), 0);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x14), 1);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x15), 1);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x16), 1);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x17), 0);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x19), 1);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x29), 2);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(-1), 0);
    T_ASSERT_EQ_I(engine_tooltip_mode_for_screen(0x100), 0);
    return 0;
}

/* ─── FUN_00477320 — strategic classifier ───────────────────────────── */

static engine_tooltip_classify_in_t tip_at(int x, int y)
{
    engine_tooltip_classify_in_t in;
    memset(&in, 0, sizeof in);
    in.cursor_x = x;
    in.cursor_y = y;
    in.county_owner = 3;
    in.current_realm = 3;
    return in;
}

int test_tooltip_classify_regions(void)
{
    engine_tooltip_classify_in_t in;
    in = tip_at(100, 100);                       /* map area */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0);
    in = tip_at(0x200, 10);                      /* top bar */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0);
    in = tip_at(0x200, 0x50);                    /* minimap */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 1);
    /* Side icon column, panel_mode 0: four bands. */
    in = tip_at(0x268, 0x30); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 2);
    in = tip_at(0x268, 0x50); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 3);
    in = tip_at(0x268, 0x70); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 4);
    in = tip_at(0x268, 0x90); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 5);
    /* panel_mode 1: census-return band at the bottom. */
    in = tip_at(0x268, 0x90); in.panel_mode = 1;
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x1f);
    return 0;
}

int test_tooltip_classify_action_grid(void)
{
    engine_tooltip_classify_in_t in;
    in = tip_at(0x205, 0xc4);                    /* population */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 6);
    in = tip_at(0x257, 0xc4);                    /* happiness */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 7);
    in = tip_at(0x205, 0xe2);                    /* tax */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 8);
    in = tip_at(0x257, 0xe2);                    /* rations */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 9);
    in = tip_at(0x230, 0xc4);                    /* the gutter → health */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x22);
    in = tip_at(0x205, 0xc4); in.county_owner = 1;   /* not owned */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0);
    return 0;
}

int test_tooltip_classify_bottom_rows(void)
{
    engine_tooltip_classify_in_t in;
    in = tip_at(0x200, 0x110);                   /* workforce band */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 10);
    in = tip_at(0x1f0, 0x1b8); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0xb);
    in = tip_at(0x210, 0x1b8); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0xc);
    in = tip_at(0x230, 0x1b8); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0xd);
    in = tip_at(0x250, 0x1b8); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x20);
    in = tip_at(0x268, 0x1b8); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x21);
    in = tip_at(0x200, 0x1d0); T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0xe);
    return 0;
}

int test_tooltip_classify_dynamic_lists(void)
{
    /* Zero row heights leave both list branches inert. */
    engine_tooltip_classify_in_t in = tip_at(0x200, 0x140);
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0);

    static const uint8_t labor[3] = { 1, 0, 2 };
    in = tip_at(0x200, 0x130 + 0);               /* row 0, kind 1 → cattle */
    in.labor_row_h = 0x10; in.labor_count = 3; in.labor_kinds = labor;
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0xf);
    in.cursor_y = 0x130 + 0x10;                  /* row 1, kind 0 → wheat */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x10);
    in.cursor_y = 0x130 + 0x20;                  /* row 2, kind 2 → reclaim */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x11);
    in.cursor_y = 0x130 + 0x30;                  /* past the list */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0);

    static const uint8_t prod[5] = { 6, 5, 4, 7, 3 };
    in = tip_at(0x240, 0x130);
    in.prod_row_h = 0x10; in.prod_count = 5; in.prod_kinds = prod;
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x12);
    in.cursor_y = 0x130 + 0x40;                  /* row 4, kind 3 → castle */
    T_ASSERT_EQ_I(engine_tooltip_classify_strategic(&in), 0x16);
    return 0;
}

/* ─── The tick (arm / clear / render) ───────────────────────────────── */

enum { CV_W = 640, CV_H = 480 };
static uint8_t tt_canvas[CV_W * CV_H];
static uint8_t tt_locbuf[120000];

static int tt_load_loc(void)
{
    char p[1400];
    const char *env = getenv("OPENLORDS2_GAME_DIR");
    if (env && *env) {
        snprintf(p, sizeof p, "%s/l2.eng", env);
    } else {
        snprintf(p, sizeof p, "%s/vendor/original/L2.eng", OPENLORDS2_ROOT);
    }
    return io_read_file_at(p, tt_locbuf, sizeof tt_locbuf, 0) > 0;
}

static uint8_t tt_font[0x36b0];
static int tt_load_font(void)
{
    char p[1400];
    const char *env = getenv("OPENLORDS2_GAME_DIR");
    if (env && *env) {
        snprintf(p, sizeof p, "%s/fntl2_14.pl8", env);
    } else {
        snprintf(p, sizeof p, "%s/vendor/original/fntl2_14.pl8",
                 OPENLORDS2_ROOT);
    }
    return io_read_file_at(p, tt_font, sizeof tt_font, 0) > 0;
}

int test_tooltip_tick_arms_after_idle_and_clears_on_activity(void)
{
    if (!tt_load_loc() || !tt_load_font())
        T_SKIP("retail l2.eng / fntl2_14.pl8 not available");

    memset(tt_canvas, 7, sizeof tt_canvas);      /* recognizable backdrop */
    gfx_paint_bind_canvas(tt_canvas, CV_W, CV_H);
    gfx_text_set_screen_byte(0);
    gfx_dispatch_set_alt_clip(0, 0, 0);
    gfx_dispatch_set_default_font(tt_font);

    localization_state_t loc;
    loc.blob = tt_locbuf;
    loc.walker_offset = 0;

    engine_tooltip_classify_in_t cin;
    memset(&cin, 0, sizeof cin);
    cin.cursor_x = 0x205; cin.cursor_y = 0xc4;   /* population button */
    cin.county_owner = 3; cin.current_realm = 3;

    engine_tooltip_state_t st;
    memset(&st, 0, sizeof st);

    engine_tooltip_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.enabled = 1;
    ctx.screen = 0;
    ctx.cin = &cin;
    ctx.loc = &loc;
    ctx.menu_font = tt_font;
    ctx.glyph_table = gfx_glyph_render_map();
    ctx.glyph_metrics = gfx_glyph_measure_map();

    /* t=0: idle window not elapsed → no tip. */
    ctx.now_ms = 0;
    engine_tooltip_tick(&st, &ctx);
    T_ASSERT_EQ_I(st.tip, 0);

    /* t=1500: idle > 999 ms → tip 6 armed, box right+below the cursor. */
    ctx.now_ms = 1500;
    engine_tooltip_tick(&st, &ctx);
    T_ASSERT_EQ_I(st.tip, 6);
    T_ASSERT_EQ_I(st.x, 0x205 - 0xdc);           /* x ≥ 0x141 → flip left */
    T_ASSERT_EQ_I(st.y, 0xc4 + 0x1e);

    /* The white box painted (color 0x20 pixels present in the box rect). */
    int white = 0;
    for (int y = st.y; y < st.y + 0x16; y++)
        for (int x = st.x; x < st.x + 0x40; x++)
            if (tt_canvas[x + y * CV_W] == 0x20) white++;
    if (white < 100)
        T_FAIL("tooltip box not painted (white=%d)", white);

    /* Mouse activity clears the tip and restores the backdrop. */
    ctx.mouse_event = 1;
    ctx.now_ms = 1600;
    engine_tooltip_tick(&st, &ctx);
    T_ASSERT_EQ_I(st.tip, 0);
    int restored = 1;
    for (int y = st.y; y < st.y + 0x28 && restored; y++)
        for (int x = st.x; x < st.x + 0x30 * 4; x++)
            if (tt_canvas[x + y * CV_W] != 7) { restored = 0; break; }
    if (!restored)
        T_FAIL("backdrop not restored after clear");

    gfx_paint_bind_canvas(NULL, 0, 0);
    return 0;
}

int test_tooltip_tick_disabled_and_activity_reset(void)
{
    engine_tooltip_state_t st;
    memset(&st, 0, sizeof st);
    engine_tooltip_classify_in_t cin;
    memset(&cin, 0, sizeof cin);
    cin.cursor_x = 0x205; cin.cursor_y = 0xc4;
    cin.county_owner = 3; cin.current_realm = 3;
    engine_tooltip_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.cin = &cin;
    ctx.screen = 0;

    /* Disabled → nothing, ever. */
    ctx.enabled = 0;
    ctx.now_ms = 5000;
    engine_tooltip_tick(&st, &ctx);
    T_ASSERT_EQ_I(st.tip, 0);

    /* Enabled but continuous mouse activity keeps resetting the window. */
    ctx.enabled = 1;
    ctx.mouse_event = 1;
    ctx.now_ms = 6000;
    engine_tooltip_tick(&st, &ctx);
    T_ASSERT_EQ_I(st.tip, 0);
    T_ASSERT_EQ_I((int)st.idle_stamp_ms, 6000);

    /* No loc/font bound → the arm still classifies, render skipped. */
    ctx.mouse_event = 0;
    ctx.now_ms = 7500;
    gfx_paint_bind_canvas(NULL, 0, 0);
    engine_tooltip_tick(&st, &ctx);
    T_ASSERT_EQ_I(st.tip, 6);
    return 0;
}
