/*
 * OpenLords2 — county control-panel detail screens (population / happiness
 * / tax / rations).  See engine_county_panels.h for the function ↔ retail
 * address map, the per-county field table, and the deferred pieces.
 *
 * Every draw call mirrors its decompile line 1:1 (coordinates, fonts,
 * colors, and the line-x-accumulator dance included); comments cite the
 * retail callee.
 */

#include "engine_county_panels.h"

#include <string.h>

#include "call_trace.h"
#include "engine_army_economy.h"
#include "engine_combat.h"            /* g_combat_flag_530d0 (DAT_005530d0), g_combat_flag_567d0c */
#include "engine_math.h"
#include "engine_realm.h"
#include "engine_territory.h"
#include "gfx_globals.h"
#include "gfx_line.h"
#include "gfx_panel.h"
#include "gfx_sprite_render.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
#include "gfx_wrap.h"
#include "io.h"
#include "io_path.h"
#include "save.h"
#include "save_schema_globals.h"

#define COUNTY(base, key) ((base) + (size_t)(key) * 0x300)

/* ── FUN_00409397 — the chrome panel: the chrome border (mode 0) + the chrome interior inset one tile ─── */
void engine_county_chrome_panel(const uint8_t *chrome, int x, int y, int cols, int rows)
{
    gfx_panel_draw_border_chrome(chrome, 0, x, y, cols, rows);        /* FUN_00409934(0, …) */
    gfx_panel_fill_interior_chrome(chrome, x + 0x10, y + 0x10, cols - 2, rows - 2);   /* FUN_00409c93 */
}

/* The four panels' common head: FUN_004b1de0() (an empty function), the map recomposite FUN_004050c0 (bound:
 * ctx->map_scene; NULL — the standalone — the caller has composed it), the chrome panel, the full-screen dirty mark
 * FUN_0045240a (ctx->dirty; NULL → none). */
static void county_panel_open(const engine_county_panels_ctx_t *ctx, int x, int y, int cols, int rows)
{
    if (ctx->map_scene) {
        ctx->map_scene();
    }
    if (ctx->chrome) {
        engine_county_chrome_panel(ctx->chrome, x, y, cols, rows);
    }
    if (ctx->dirty) {
        engine_dirty_rect_mark_full_screen(ctx->dirty);
    }
}

/* ── FUN_004156a7's baked tables (extracted from .rdata, r2 pxw) ────────
 * k_county_graph_bucket @ 0x4d29f8: seasons-recorded → bucket (threshold
 * pairs for engine_math_lookup_threshold, default 0x16).
 * k_county_graph_yscale @ 0x4d2a48: series max → y divisor (default 100).
 * k_county_graph_barw   @ 0x4d2ac0: bucket → per-season column stride. */
static const int k_county_graph_bucket[10 * 2] = {
    0x0a, 2,  0x14, 4,  0x1e, 6,  0x28, 8,  0x32, 10,
    0x3c, 12, 0x50, 14, 0x64, 16, 0x82, 18, 0xc8, 20,
};
static const int k_county_graph_yscale[15 * 2] = {
    0x065, 1,  0x0c8, 2,  0x12c, 3,  0x190, 4,  0x1f4, 5,
    0x258, 6,  0x320, 8,  0x3e8, 10, 0x4b0, 12, 0x5dc, 15,
    0x7d0, 20, 0x9c4, 25, 0xbb8, 30, 0xfa0, 40, 0x1388, 50,
};
static const int k_county_graph_barw[24] = {
    0, 0, 0x28, 0x28, 0x14, 0x14, 0xc, 0xc, 0xa, 0xa, 8, 8,
    6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1,
};

/* History-ring cell read.  Retail: (&DAT_0056d8b8)[slot*0x20 + county*2]
 * int-scaled = block(0x56d8c0) + slot*0x80 + (county-1)*8; +0 = i32
 * population (mode 0), +4 = u8 happiness (mode 1).  County keys are ≥ 1
 * (key 0 = "Here Be Dragons"); a key-0 read would fall 8 bytes below the
 * block, so guard it to 0 instead (retail would read a neighboring
 * global — never reachable from the panel flips, which are owner-gated). */
static int history_value(const engine_county_panels_ctx_t *ctx,
                         int slot, int mode)
{
    long off = (long)slot * 0x80 + ((long)ctx->county - 1) * 8;
    if (!ctx->history || off < 0) {
        return 0;
    }
    if (mode == 1) {
        return ctx->history[off + 4];
    }
    int32_t v;
    memcpy(&v, ctx->history + off, 4);
    return (int)v;
}

int engine_county_graph_render(const engine_county_panels_ctx_t *ctx,
                               int x, int y, int mode)
{
    CALL_TRACE_ENTER(0x004156a7);
    /* FUN_004af502() + FUN_004af8d9("graphs.pl8", DAT_004eabec, 200000, 0): with ctx->graphs_load the graph loads the
     * sheet into ctx->graphs (the hybrid host: the buffer DAT_004eabec points at); else the caller has. */
    if (ctx->graphs_load && ctx->graphs) {
        io_path_chdir_base();
        io_read_file_at("graphs.pl8", (void *)ctx->graphs, 0x30d40, 0);
    }

    /* FUN_00403deb(x, y, 0x192, 0x9b) — the sunken graph frame. */
    gfx_line_rect_bevel_dark_br(x, y, 0x192, 0x9b);

    /* FUN_0040a682(mode ? 1 : 0, x+1, y+1) — the axis background sprite. */
    if (ctx->graphs) {
        gfx_sprite_render_set_blob(ctx->graphs);
        gfx_sprite_render(mode == 0 ? 0 : 1, x + 1, y + 1);
    }

    int bucket = engine_math_lookup_threshold(ctx->hist_count,
                                              k_county_graph_bucket, 10, 0x16);
    int stride = k_county_graph_barw[bucket];

    /* Pass 1 — series max. */
    int slot = ctx->hist_head;
    int max = 0;
    for (int i = 0; i < ctx->hist_count; i++) {
        int v = history_value(ctx, slot, mode);
        if (++slot > 399) {
            slot = 0;
        }
        if (v > max) {
            max = v;
        }
    }

    int yscale = engine_math_lookup_threshold(max, k_county_graph_yscale,
                                              15, 100);

    /* Pass 2 — the bars.  Even seasons draw sprite bucket+1, odd draw
     * bucket (the alternating column shades); height = value / yscale,
     * bottom-anchored at y + 0x9a via the truncate-bottom renderer. */
    slot = ctx->hist_head;
    for (int i = 0; i < ctx->hist_count; i++) {
        int v = history_value(ctx, slot, mode);
        if (++slot > 399) {
            slot = 0;
        }
        v /= yscale;
        int bar_y = (y - v) + 0x9a;
        if (ctx->graphs) {
            gfx_sprite_render_truncate_bottom(((i & 1) == 0) ? bucket + 1
                                                             : bucket,
                                              stride * i + stride / 2 + x + 1,
                                              bar_y, 100 - v);
        }
    }
    return max;
}

void engine_county_delta_label(const engine_county_panels_ctx_t *ctx,
                               int value, int x, int y,
                               const uint8_t *font,
                               uint8_t color_pos, uint8_t color_neg)
{
    CALL_TRACE_ENTER(0x0041ac95);
    int saved = gfx_text_line_x_acc();          /* iVar1 = DAT_005cd404 */
    gfx_text_set_line_x_acc(0);
    /* FUN_00402637("(", x, y, font, 0x3f) */
    gfx_text_render_line((const uint8_t *)"(", x, y,
                         font, ctx->glyph_table, 0x3f);
    gfx_text_set_line_x_acc(gfx_text_line_x_acc() - 4);
    /* FUN_00402e0c(value, 1, "", "", x+acc, y, font, pos, neg) */
    gfx_text_label_render_labeled_signed(value, 1,
                                         (const uint8_t *)"", "",
                                         x + gfx_text_line_x_acc(), y,
                                         font, ctx->glyph_table,
                                         color_pos, color_neg);
    /* FUN_0040a21a(misc, 0x17, x+acc, y-2) — the happiness face icon. */
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x17,
                            x + gfx_text_line_x_acc(), y - 2);
    }
    /* FUN_00402637(")", x+acc+0x14, y, font, 0x3f) */
    gfx_text_render_line((const uint8_t *)")",
                         x + gfx_text_line_x_acc() + 0x14, y,
                         font, ctx->glyph_table, 0x3f);
    gfx_text_set_line_x_acc(gfx_text_line_x_acc() + saved);
}

/* ── FUN_0041a900 — a year (or any signed number) with its era word ──────
 * Every mode zeroes the line accumulator first; the number takes the one-space suffix " " (DAT_004d41d4…f0, all
 * " ").  mode 0 / 2: |value| then the era word (loc grp 0x1a sub 1 for value >= 0, else 0) at the number's advance,
 * menu font; mode 1: the era word first, then the number, the title font — the number one pixel higher when value
 * >= 0; mode 3: |value| only, menu font; any other mode draws nothing. */
void engine_county_year_number(const engine_county_panels_ctx_t *ctx, int value, int x, int y, int mode)
{
    CALL_TRACE_ENTER(0x0041a900);
    g_gfx_line_x_acc = 0;
    int pos = value >= 0, mag = pos ? value : -value;
    switch (mode) {
    case 0:
    case 2:
        gfx_text_label_render_dec(mag, ' ', " ", x, y, ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x1a, pos, g_gfx_line_x_acc + x, y, ctx->menu_font, ctx->glyph_table, 0x3f);
        break;
    case 1:
        gfx_wrap_localized_line(ctx->loc, 0x1a, pos, x, y, ctx->title_font, ctx->glyph_table, 0x3f);
        gfx_text_label_render_dec(mag, ' ', " ", g_gfx_line_x_acc + x, pos ? y - 1 : y, ctx->title_font,
                                  ctx->glyph_table, 0x3f);
        break;
    case 3:
        gfx_text_label_render_dec(mag, ' ', " ", x, y, ctx->menu_font, ctx->glyph_table, 0x3f);
        break;
    default:
        break;
    }
}

/* ── FUN_0041ab67 — "N <noun>" (loc grp 8, sub `sub` for ±1, else sub+1): the number ('@' blank sign slot), then the
 * noun at its advance; the caller's line accumulator is preserved around the pair (retail saves / restores
 * DAT_005cd404). */
void engine_county_num_units(const engine_county_panels_ctx_t *ctx, int value, int sub, int x, int y,
                             const uint8_t *font, uint8_t color)
{
    CALL_TRACE_ENTER(0x0041ab67);
    int saved = g_gfx_line_x_acc;
    g_gfx_line_x_acc = 0;
    gfx_text_label_render_dec(value, '@', "", x, y, font, ctx->glyph_table, color);
    int s2 = (value == 1 || value == -1) ? sub : sub + 1;
    gfx_wrap_localized_line(ctx->loc, 8, s2, g_gfx_line_x_acc + x, y, font, ctx->glyph_table, color);
    g_gfx_line_x_acc += saved;
}

/* Field accessors over the county record. */
static int32_t cty_i32(const uint8_t *rec, int off)
{
    int32_t v;
    memcpy(&v, rec + off, 4);
    return v;
}
static int cty_s8(const uint8_t *rec, int off)
{
    return (int)(int8_t)rec[off];
}

/* ── FUN_004110b1 — the population report (screen 0x14, loc grp 0x49) ── */
void engine_county_panel_population(const engine_county_panels_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x004110b1);
    if (!ctx->county_block || !ctx->loc) {
        return;
    }
    const uint8_t *rec = COUNTY(ctx->county_block, ctx->county);

    if (ctx->map_scene) {
        ctx->map_scene();                                  /* FUN_004050c0 */
    }
    g_gfx_cursor_x = 0x10;                                 /* DAT_00591524 / DAT_00591528 */
    g_gfx_cursor_y = 0x30;
    if (ctx->chrome) {
        engine_county_chrome_panel(ctx->chrome, 0x10, 0x30, 0x1c, 0x17);
    }
    if (ctx->dirty) {
        engine_dirty_rect_mark_full_screen(ctx->dirty);   /* FUN_0045240a */
    }

    /* Title: "Population in" + the county name (loc grp 100, entry
     * map_id*0x14 + county), both in the large title font. */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x49, 0, 0x14, 0x38,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 100,
                            ctx->map_id * 0x14 + ctx->county,
                            gfx_text_line_x_acc() + 0x16, 0x38,
                            ctx->title_font, ctx->glyph_table, 0x3f);

    /* The population history graph + its year axis labels. */
    int max = engine_county_graph_render(ctx, 0x20, 0x54, 0);
    engine_county_year_number(ctx, ctx->hist_start_year, 0x20, 0xf0, 3);
    gfx_text_render_line((const uint8_t *)"-",       /* &DAT_004d3d88 */
                         gfx_text_line_x_acc() + 0x20, 0xf0,
                         ctx->menu_font, ctx->glyph_table, 0x3f);
    engine_county_year_number(ctx, ctx->year, 0x58, 0xf0, 3);

    /* "Greatest population N people." */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x49, 8, 0xb0, 0xf0,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(max, '@', "",
                              gfx_text_line_x_acc() + 0xb0, 0xf0,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x49, 9,
                            gfx_text_line_x_acc() + 0xb0, 0xf0,
                            ctx->menu_font, ctx->glyph_table, 0x3f);

    /* "Last season" (title font row). */
    gfx_wrap_localized_line(ctx->loc, 0x49, 1, 0x30, 0x10a,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(cty_i32(rec, 0x28), '@', "", 0x150, 0x10a,
                              ctx->title_font, ctx->glyph_table, 0x3f);

    /* Births / Deaths (negated) / Army — signed value rows. */
    gfx_wrap_localized_line(ctx->loc, 0x49, 2, 0x30, 0x12a,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_labeled_signed(cty_i32(rec, 0x30), 0,
                                         (const uint8_t *)"", "",
                                         0x150, 0x12a, ctx->menu_font,
                                         ctx->glyph_table, 0x3f, 0xf9);
    gfx_wrap_localized_line(ctx->loc, 0x49, 3, 0x30, 0x13a,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_labeled_signed(-cty_i32(rec, 0x34), 0,
                                         (const uint8_t *)"", "",
                                         0x150, 0x13a, ctx->menu_font,
                                         ctx->glyph_table, 0x3f, 0xf9);
    gfx_wrap_localized_line(ctx->loc, 0x49, 4, 0x30, 0x14a,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_labeled_signed(cty_i32(rec, 0x38), 0,
                                         (const uint8_t *)"", "",
                                         0x150, 0x14a, ctx->menu_font,
                                         ctx->glyph_table, 0x3f, 0xf9);

    /* Emigration row: "No emigration." or "Emigrants to <county> −N". */
    gfx_text_set_line_x_acc(0);
    if (cty_i32(rec, 0x3c) == 0) {
        gfx_wrap_localized_line(ctx->loc, 0x49, 10, 0x30, 0x15a,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
    } else {
        gfx_wrap_localized_line(ctx->loc, 0x49, 5, 0x30, 0x15a,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 100,
                                ctx->map_id * 0x14 + rec[0x58],
                                gfx_text_line_x_acc() + 0x30, 0x15a,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_text_label_render_labeled_signed(-cty_i32(rec, 0x3c), 0,
                                             (const uint8_t *)"", "",
                                             0x150, 0x15a, ctx->menu_font,
                                             ctx->glyph_table, 0x3f, 0xf9);
    }

    /* Immigration row. */
    gfx_text_set_line_x_acc(0);
    if (cty_i32(rec, 0x40) == 0) {
        gfx_wrap_localized_line(ctx->loc, 0x49, 0xb, 0x30, 0x16a,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
    } else {
        gfx_wrap_localized_line(ctx->loc, 0x49, 6, 0x30, 0x16a,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_text_label_render_labeled_signed(cty_i32(rec, 0x40), 0,
                                             (const uint8_t *)"", "",
                                             0x150, 0x16a, ctx->menu_font,
                                             ctx->glyph_table, 0x3f, 0xf9);
    }

    /* "This Season" (title font row) + the continue-seal. */
    gfx_wrap_localized_line(ctx->loc, 0x49, 7, 0x30, 0x182,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(cty_i32(rec, 0x24), '@', "", 0x150, 0x182,
                              ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_sprite_seal_draw(ctx->system_sheet, 0x1b4, 0x184, 0);
    g_gfx_menu_redraw_a = 1;                               /* DAT_004eb260 */
}

/* ── FUN_0041152f — the tax panel (screen 0x15, loc grp 0x56) ────────── */
void engine_county_panel_tax(const engine_county_panels_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x0041152f);
    if (!ctx->county_block || !ctx->loc) {
        return;
    }
    const uint8_t *rec = COUNTY(ctx->county_block, ctx->county);
    int owner = rec[0x05];

    county_panel_open(ctx, 0x50, 0x90, 0x14, 9);
    /* FUN_0040a21a(misc, 0x3e, 0x140, 0xa0) — the tax-chest icon. */
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x3e, 0x140, 0xa0);
    }

    /* "Tax rate  N%" */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x56, 1, 0x60, 0xa8,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(rec[0xb9], ' ', "%", 0x100, 0xa8,
                              ctx->menu_font, ctx->glyph_table, 0x3f);

    /* "People pay  N Crowns." — the number trails the label's advance. */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x56, 2, 0x60, 0xc8,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    engine_county_num_units(ctx, (int)cty_i32(rec, 0xc0), 0,
                            gfx_text_line_x_acc() + 0x60, 0xc8, ctx->menu_font, 0x3f);

    /* "This county (±N☺)" — realm base (+0x28 of the realm record) plus
     * the county's own tax-happiness delta (+0x0f). */
    gfx_wrap_localized_line(ctx->loc, 0x56, 3, 0x60, 0xe8,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    int realm_base = 0;
    if (ctx->realm_block) {
        realm_base = (int)(int8_t)ctx->realm_block[(size_t)owner * 0x160
                                                   + 0x28];
    }
    engine_county_delta_label(ctx, realm_base + cty_s8(rec, 0x0f),
                              0xf0, 0xe8, ctx->menu_font, 0x3f, 0xf9);

    /* "Other counties (±N☺)" */
    gfx_wrap_localized_line(ctx->loc, 0x56, 4, 0x60, 0x100,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    engine_county_delta_label(ctx, cty_s8(rec, 0x16),
                              0xf0, 0x100, ctx->menu_font, 0x3f, 0xf9);

    gfx_sprite_seal_draw(ctx->system_sheet, 0x174, 0x104, 0);
    g_gfx_menu_redraw_a = 1;                               /* DAT_004eb260 */
}

/* ── FUN_004116fb — the happiness report (screen 0x16, loc grp 0x55) ── */
void engine_county_panel_happiness(const engine_county_panels_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x004116fb);
    if (!ctx->county_block || !ctx->loc) {
        return;
    }
    const uint8_t *rec = COUNTY(ctx->county_block, ctx->county);

    if (ctx->map_scene) {
        ctx->map_scene();                                  /* FUN_004050c0 */
    }
    g_gfx_cursor_x = 0x10;                                 /* DAT_00591524 / DAT_00591528 */
    g_gfx_cursor_y = 0x30;
    if (ctx->chrome) {
        engine_county_chrome_panel(ctx->chrome, 0x10, 0x30, 0x1c, 0x18);   /* FUN_00409397 */
    }
    if (ctx->dirty) {
        engine_dirty_rect_mark_full_screen(ctx->dirty);   /* FUN_0045240a */
    }

    /* Title: "Happiness in <county>". */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x55, 0, 0x14, 0x38,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 100,
                            ctx->map_id * 0x14 + ctx->county,
                            gfx_text_line_x_acc() + 0x16, 0x38,
                            ctx->title_font, ctx->glyph_table, 0x3f);

    /* The happiness history graph (mode 1) + year axis. */
    engine_county_graph_render(ctx, 0x20, 0x54, 1);
    engine_county_year_number(ctx, ctx->hist_start_year, 0x20, 0xf0, 3);
    gfx_text_render_line((const uint8_t *)"-",       /* &DAT_004d3dc4 */
                         gfx_text_line_x_acc() + 0x20, 0xf0,
                         ctx->menu_font, ctx->glyph_table, 0x3f);
    engine_county_year_number(ctx, ctx->year, 0x58, 0xf0, 3);

    /* "Average happiness N☺". */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x55, 8, 0xb0, 0xf1,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(cty_s8(rec, 0x18), '@', "",
                              gfx_text_line_x_acc() + 0xb0, 0xf1,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x17,
                            gfx_text_line_x_acc() + 0xb0, 0xef);
    }

    /* "Last season N☺" (title font). */
    gfx_wrap_localized_line(ctx->loc, 0x55, 1, 0x30, 0x10c,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(cty_s8(rec, 0x0d), '@', "", 0x150, 0x10c,
                              ctx->title_font, ctx->glyph_table, 0x3f);
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x17,
                            gfx_text_line_x_acc() + 0x150, 0x10f);
    }

    /* The signed source rows: taxes / ration / health / army / ale /
     * events. */
    static const struct { int sub, off, wide, y; } rows[] = {
        { 2, 0x12,  0, 0x12a },   /* From taxes  */
        { 3, 0x13,  0, 0x13a },   /* From ration */
        { 4, 0x14,  0, 0x14a },   /* From health */
        { 5, 0x15,  0, 0x15a },   /* From army   */
        { 6, 0x194, 1, 0x16a },   /* From ale (i32) */
        { 9, 0x17,  0, 0x17a },   /* From events */
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        gfx_wrap_localized_line(ctx->loc, 0x55, rows[i].sub, 0x30, rows[i].y,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        int v = rows[i].wide ? (int)cty_i32(rec, rows[i].off)
                             : cty_s8(rec, rows[i].off);
        gfx_text_label_render_labeled_signed(v, 0, (const uint8_t *)"", "",
                                             0x150, rows[i].y,
                                             ctx->menu_font, ctx->glyph_table,
                                             0x3f, 0xf9);
    }

    /* "This Season N☺" (title font) + seal. */
    gfx_wrap_localized_line(ctx->loc, 0x55, 7, 0x30, 0x192,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(cty_s8(rec, 0x0c), '@', "", 0x150, 0x192,
                              ctx->title_font, ctx->glyph_table, 0x3f);
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x17,
                            gfx_text_line_x_acc() + 0x150, 0x195);
    }
    gfx_sprite_seal_draw(ctx->system_sheet, 0x1b4, 0x194, 0);
    g_gfx_menu_redraw_a = 1;                               /* DAT_004eb260 */
}

/* ── FUN_00411b72 — the rations panel (screen 0x19, loc grp 0x57) ────── */
void engine_county_panel_rations(const engine_county_panels_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x00411b72);
    if (!ctx->county_block || !ctx->loc) {
        return;
    }
    const uint8_t *rec = COUNTY(ctx->county_block, ctx->county);
    int extra = (ctx->advanced_mode == 1) ? 2 : 0;

    county_panel_open(ctx, 0x80, 0x60, 0x12, extra + 0xf);

    /* Centered "Ration" title across the panel. */
    gfx_wrap_localized_line_centered(ctx->loc, 0x57, 0, 0x80, 0x68, 0x120,
                                     ctx->title_font, ctx->glyph_table,
                                     ctx->glyph_metrics, 0x3f);

    /* "Wanted: <level>" (level names = loc grp 0x15). */
    gfx_wrap_localized_line(ctx->loc, 0x57, 1, 0x90, 0x88,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x15, cty_s8(rec, 0x15e), 0xf0, 0x88,
                            ctx->menu_font, ctx->glyph_table, 0x3f);

    /* "Achieved: <level>" — red when the achieved level differs. */
    uint8_t achieved_color = (rec[0x15d] == rec[0x15e]) ? 0x3f : 0xf9;
    gfx_wrap_localized_line(ctx->loc, 0x57, 2, 0x90, 0xa1,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x15, cty_s8(rec, 0x15d), 0xf0, 0xa1,
                            ctx->menu_font, ctx->glyph_table, achieved_color);
    engine_county_delta_label(ctx, cty_s8(rec, 0x11), 0x154, 0xa1,
                              ctx->menu_font, 0x3f, 0xf9);

    /* "Health: <level>" (health names = loc grp 0x14) + delta. */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x57, 3, 0x90, 0xba,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x14, cty_s8(rec, 0x09), 0xf0, 0xba,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    engine_county_delta_label(ctx, cty_s8(rec, 0x10), 0x154, 0xba,
                              ctx->menu_font, 0x3f, 0xf9);

    /* The arrow / band chrome (sprites off misc_cty): the ration stepper
     * row at y=0xdc and the grain↔beef balance row at y=0x100. */
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x21, 0x90, 0xdc);
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x26, 0x172, 0xdc);
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x21, 0xe0, 0x100);
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x26, 0x11c, 0x100);
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x2a, 0x158, 0x100);
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x18, 0x90, 0x11a);
    }

    /* The food grid: "Fed" / "Eaten" rows, centered numbers (width 0x40).
     * Columns: grain (0xd0), cattle (0x10a), and the growing readout
     * (0x144). */
    gfx_wrap_localized_line(ctx->loc, 0x57, 5, 0xa0, 0x11e,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x57, 4, 0x90, 0x134,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    static const struct { int off, x, y; } cells[] = {
        { 0x178, 0x0d0, 0x134 },
        { 0x170, 0x0d0, 0x11e },
        { 0x17c, 0x10a, 0x134 },
        { 0x174, 0x10a, 0x11e },
        { 0x16c, 0x144, 0x11e },
    };
    for (size_t i = 0; i < sizeof cells / sizeof cells[0]; i++) {
        gfx_text_label_render_dec_centered((int)cty_i32(rec, cells[i].off),
                                           ' ', "", cells[i].x, cells[i].y,
                                           0x40, ctx->menu_font,
                                           ctx->glyph_table,
                                           ctx->glyph_metrics, 0x3f);
    }

    /* Continue-seal; the panel grows 2 rows in advanced mode and the
     * seal + foraging line follow it down. */
    gfx_sprite_seal_draw(ctx->system_sheet, 0x184,
                         (extra + 0xf) * 0x10 + 0x44, 0);

    if (ctx->advanced_mode == 1) {
        /* "N men foraging in the county." (the two foraging pools
         * summed). */
        gfx_text_set_line_x_acc(0);
        gfx_text_label_render_dec((int)(cty_i32(rec, 0x19c)
                                        + cty_i32(rec, 0x198)),
                                  ' ', "", 0x88, 0x150,
                                  ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x57, 8,
                                gfx_text_line_x_acc() + 0x88, 0x150,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
    }
    g_gfx_menu_redraw_a = 1;                               /* DAT_004eb260 */
}

/* ── FUN_00438ceb — the 2×2 action-grid hit-test ─────────────────────── */
int engine_county_action_grid_classify(const engine_county_grid_in_t *in)
{
    CALL_TRACE_ENTER(0x00438ceb);
    if ((in->county_owner != in->current_realm && in->spectate == 0) ||
        in->lmb_pressed == 0) {
        return 0;
    }
    if (in->cursor_x <= 0x1e7 || in->cursor_x >= 0x276 ||
        in->cursor_y <= 0xb5 || in->cursor_y >= 0xf1) {
        return 0;
    }
    if (in->cursor_y < 0xd4) {
        if (in->cursor_x < 0x224) {
            return 0x14;               /* FUN_0043a8f2 — population */
        }
        if (in->cursor_x > 0x237) {
            return 0x16;               /* FUN_0043a922 — happiness */
        }
    } else {
        if (in->cursor_x < 0x224) {
            return 0x15;               /* FUN_0043a939 — tax */
        }
        if (in->cursor_x > 0x237) {
            return 0x19;               /* FUN_0043a846 — rations */
        }
    }
    /* The dead 0x13-px gutter between columns: retail falls through its
     * if-ladder WITHOUT returning 1 — the press is not consumed. */
    return 0;
}

/* ── FUN_0043a846's voice-cue pick ───────────────────────────────────── */
int engine_county_rations_voice_cue(const uint8_t *rec)
{
    if (rec[0x15d] == 0) {
        return 2;                              /* s021_02.wav */
    }
    if (cty_i32(rec, 0x250) != 0 &&
        cty_i32(rec, 0x17c) == 0 && cty_i32(rec, 0x178) == 0) {
        return 1;                              /* s021_01.wav */
    }
    return 0;
}

/* ── FUN_0043a379 — rations arrows / absolute band ───────────────────── */
static int rations_hit(const engine_county_rations_in_t *in,
                       int x, int y, int w, int h)
{
    /* FUN_004b1deb — half-open cursor-in-rect. */
    return (in->cursor_x >= x && in->cursor_x < x + w &&
            in->cursor_y >= y && in->cursor_y < y + h);
}

int engine_county_rations_input(const engine_county_rations_in_t *in,
                                int *out_value, int *out_is_drag,
                                int *out_changed)
{
    CALL_TRACE_ENTER(0x0043a379);
    *out_changed = 0;
    if (in->county_owner != in->current_realm) {
        return 0;
    }
    if (in->lmb_released != 0) {
        return 0;
    }
    /* Entry gate: a double-click repeat edge, OR LMB held with mouse
     * activity this frame (press edge / drag both qualify — the sampler
     * sets mouse_event on any button change or motion). */
    if (in->click_repeat == 0 &&
        (in->lmb_held == 0 || in->hold_repeat_gate == 0)) {
        return 0;
    }

    int delta = 0;
    int band_value = 0;
    int hit_minus = rations_hit(in, 0xc8, 0xdc, 0x18, 0x18);
    int hit_plus  = rations_hit(in, 0x145, 0xdc, 0x18, 0x18);
    int hit_band  = rations_hit(in, 0xe0, 0xdc, 0x66, 0x18);
    if (hit_minus) {
        delta = -1;
    }
    if (hit_plus) {
        delta = 1;
    }
    if (hit_band) {
        band_value = in->cursor_x - 0xe0;
    }
    if (!hit_minus && !hit_plus && !hit_band) {
        return 0;
    }

    int value = in->ration_pct;
    int is_drag;
    if (delta == 0) {
        value = band_value;                    /* absolute set (drag) */
        is_drag = 1;                           /* DAT_00591550 = 1 */
    } else {
        if (in->lmb_pressed != 0 || in->click_repeat != 0) {
            value += delta;                    /* arrow step (edge only) */
        }
        is_drag = 0;
    }
    *out_is_drag = is_drag;                    /* written on every hit (retail: before the no-change return) */
    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }
    if (value == in->ration_pct) {
        return 1;                              /* consumed, no change */
    }
    *out_value   = value;                      /* DAT_0059154c */
    *out_is_drag = is_drag;
    *out_changed = 1;
    return 1;
}

/* ── The panel arrow-button tables (see header) ─────────────────────────
 * Mutable like retail's .data tables — engine_menu_button_dispatch
 * animates state/pressed/repeat in place. */
static engine_button_rec_t k_county_tax_buttons[2] = {
    { 0x0c0, 0xa2, 0x15, 0x18, 0x43aa32, 0, 0, 0, 4, 0, 0 },
    { 0x0e0, 0xa2, 0x17, 0x18, 0x43aad5, 0, 0, 0, 4, 0, 0 },
};
static engine_button_rec_t k_county_wanted_buttons[2] = {
    { 0x158, 0x82, 0x15, 0x18, 0x43a1e9, 0, 0, 0, 4, 0, 0 },
    { 0x170, 0x82, 0x17, 0x18, 0x43a2b2, 0, 0, 0, 4, 0, 0 },
};

engine_button_rec_t *engine_county_tax_buttons(void)
{
    return k_county_tax_buttons;
}

engine_button_rec_t *engine_county_wanted_buttons(void)
{
    return k_county_wanted_buttons;
}

void engine_county_tax_step(int county, int dir)
{
    CALL_TRACE_ENTER(dir > 0 ? 0x0043aa83 : 0x0043ab25);
    uint8_t *rec = g_per_army_768 + (size_t)county * 0x300;
    if (dir > 0) {
        if (rec[0xb9] < 0x32) {
            rec[0xb9] += 1;
        }
    } else {
        if (rec[0xb9] != 0) {
            rec[0xb9] -= 1;
        }
    }
    engine_realm_recompute_realm_income(county);            /* 44b80b */
    /* FUN_0041152f() re-render + DAT_005530d0/DAT_004eb260 — caller. */
}

void engine_county_wanted_step(int county, int dir)
{
    CALL_TRACE_ENTER(dir > 0 ? 0x0043a23f : 0x0043a307);
    uint8_t *rec = g_per_army_768 + (size_t)county * 0x300;
    if (dir > 0) {
        if ((int8_t)rec[0x15e] < 5) {
            rec[0x15e] += 1;
        }
    } else {
        if ((int8_t)rec[0x15e] > 0) {
            rec[0x15e] -= 1;
        }
    }
    engine_army_economy_provision(county, (int)s_slot_0057c934); /* 44df5f */
    engine_army_economy_plan_realm(county, (int)s_slot_0057c92c); /* 4485a5 */
    /* FUN_00411b72() re-render + the redraw flags — caller. */
}

/* FUN_0043aa83 / FUN_0043ab25 and FUN_0043a23f / FUN_0043a307 whole: the step, the panel's re-render, the redraw
 * flags (DAT_005530d0 and — the rations — DAT_00567d0c, then DAT_004eb260). */
void engine_county_tax_step_render(const engine_county_panels_ctx_t *ctx, int dir)
{
    engine_county_tax_step(ctx->county, dir);
    engine_county_panel_tax(ctx);
    g_combat_flag_530d0 = 1;
    g_gfx_menu_redraw_a = 1;
}

void engine_county_wanted_step_render(const engine_county_panels_ctx_t *ctx, int dir)
{
    engine_county_wanted_step(ctx->county, dir);
    engine_county_panel_rations(ctx);
    g_combat_flag_530d0 = 1;
    g_combat_flag_567d0c = 1;
    g_gfx_menu_redraw_a = 1;
}

void engine_county_ration_slider_render(const engine_county_panels_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x00411fde);
    const uint8_t *rec = ctx->county_block
                             ? COUNTY(ctx->county_block, ctx->county)
                             : (const uint8_t *)0;

    /* FUN_00409c93(0xdc, 0xd8, 8, 2) — repaint the parchment interior
     * under the slider band (erases the previous knob position).  The
     * plain interior-tile fill, NOT a bevel frame. */
    if (ctx->chrome) {
        gfx_panel_fill_interior_chrome(ctx->chrome, 0xdc, 0xd8, 8, 2);
    }
    /* Side arrows off the SYSTEM sheet (0x5bb540) at the FUN_0043a379
     * hit rects. */
    if (ctx->system_sheet) {
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x4a, 200, 0xdc);
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x4b, 0x144, 0xdc);
    }
    /* The 100-px track: dark top rule, light fill, pale bottom rule. */
    gfx_line_draw(0xe0, 0xe5, 0x143, 0xe5, 0x10);
    gfx_line_fill_rect(0xe0, 0xe6, 100, 4, 0x3f);
    gfx_line_draw(0xe0, 0xea, 0x143, 0xea, 0x1f);
    /* The knob at wanted-percent (+0x15f). */
    if (ctx->system_sheet && rec) {
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x4c,
                            (int)(int8_t)rec[0x15f] + 0xdc, 0xd8);
    }
}

/* FUN_00438ceb whole: the hit-test, then the flip (FUN_0043a8f2 population / 0043a939 tax / 0043a922 happiness /
 * 0043a846 rations — `flip`, by screen code; NULL → none); 1 when a flip ran. */
int engine_county_action_grid_click(const engine_county_grid_in_t *in, void (*flip)(int screen))
{
    int screen = engine_county_action_grid_classify(in);
    if (screen == 0) {
        return 0;
    }
    if (flip) {
        flip(screen);
    }
    return 1;
}

/* FUN_0043a5a9 whole: the change, then — the current county — the panel's re-render (FUN_00411b72) and the redraw
 * flags DAT_005530d0 / DAT_004eb260. */
int engine_county_ration_set_render(const engine_county_panels_ctx_t *ctx, int county, int value, int is_drag)
{
    int cur = engine_county_ration_set(county, value, is_drag);
    if (cur) {
        engine_county_panel_rations(ctx);
        g_combat_flag_530d0 = 1;
        g_gfx_menu_redraw_a = 1;
    }
    return cur;
}

/* FUN_0043a379 whole: the decision (engine_county_rations_input), the drag flag DAT_00591550 on every hit, the new
 * value DAT_0059154c and the change itself — in multiplayer the broadcast 0x43eda0(0x31, 0) instead (`mp_notify`). */
int engine_county_rations_click(const engine_county_panels_ctx_t *ctx, const engine_county_rations_in_t *in,
                                int32_t *drag_out, int32_t *value_out, int mp_active, void (*mp_notify)(void))
{
    int value = 0, is_drag = -1, changed = 0;
    int r = engine_county_rations_input(in, &value, &is_drag, &changed);
    if (is_drag >= 0) {
        *drag_out = is_drag;
    }
    if (changed) {
        *value_out = value;
        if (mp_active) {
            if (mp_notify) {
                mp_notify();
            }
        } else {
            engine_county_ration_set_render(ctx, ctx->county, value, is_drag);
        }
    }
    return r;
}

/* ── FUN_0043a5a9 — apply the ration change ──────────────────────────── */
int engine_county_ration_set(int county, int value, int is_drag)
{
    CALL_TRACE_ENTER(0x0043a5a9);
    uint8_t *rec = g_per_army_768 + (size_t)county * 0x300;

    int8_t cur = (int8_t)rec[0x15f];
    int step = (value < cur) ? -1 : (cur < value) ? 1 : 0;

    rec[0x15f] = (uint8_t)value;
    int32_t fed_before = cty_i32(rec, 0x17c);
    engine_army_economy_provision(county, (int)s_slot_0057c934); /* 44df5f */

    /* Arrow-step snap: when the county is active, food WAS being fed,
     * the step didn't reach an endpoint, and the fed outcome (+0x17c)
     * did not change — keep stepping the same direction (clamped) until
     * it does (or 100 tries).  A drag (`is_drag`) that lands on such a
     * dead value instead REVERTS (retail's do/while exits only when the
     * outcome changed, or `value` reached with param_3 nonzero — falling
     * out of the loop restores the original). */
    if (cty_i32(rec, 0x250) != 0 && cty_i32(rec, 0x17c) != 0 &&
        step != 0 && value != 0 && value != 100 &&
        cty_i32(rec, 0x17c) == fed_before) {
        rec[0x15f] = (uint8_t)cur;
        int reverted = 1;
        for (int tries = 1; tries <= 100; tries++) {
            int v = (int)(int8_t)rec[0x15f] + step;
            if (v < 0) {
                v = 0;
            } else if (v > 100) {
                v = 100;
            }
            rec[0x15f] = (uint8_t)v;
            engine_army_economy_provision(county, (int)s_slot_0057c934);
            if (cty_i32(rec, 0x17c) != fed_before) {
                reverted = 0;
                break;                         /* outcome flipped — keep */
            }
            if (v == value && is_drag != 0) {
                /* drag reached its target without changing the outcome —
                 * revert (retail falls out of the do/while here). */
                rec[0x15f] = (uint8_t)cur;
                engine_army_economy_provision(county,
                                              (int)s_slot_0057c934);
                reverted = 0;
                break;
            }
        }
        (void)reverted;
    }

    /* Tail: allocate → plan_realm, twice (retail order). */
    engine_territory_allocate_realm_workforce(county);          /* 44f6e7 */
    engine_army_economy_plan_realm(county, (int)s_slot_0057c92c); /* 4485a5 */
    engine_territory_allocate_realm_workforce(county);
    engine_army_economy_plan_realm(county, (int)s_slot_0057c92c);

    return (int)s_slot_0057c93c == county;
}
