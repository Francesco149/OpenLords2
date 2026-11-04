/*
 * OpenLords2 — the per-task labor-allocation detail screen (0x0f).  See
 * engine_labor.h for the retail address map, the per-county field table,
 * and the deferred pieces.
 *
 * Every draw call mirrors its decompile line 1:1 (coordinates, fonts,
 * colors, and the line-x-accumulator dance included).  County-record
 * offsets are kept RAW (= DAT_0053fXXX − 0x53f9b0) with the source
 * DAT_ address cited; the .rdata tables are r2-extracted byte-exact.
 */

#include "engine_labor.h"
#include "engine_labor_region_grid.h"   /* k_labor_panel_region_grid (DAT_00542cf8) */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "call_trace.h"
#include "engine_army_economy.h"      /* engine_army_economy_prod_scale */
#include "engine_screen_strat.h"      /* engine_screen_strat_render_menubar */
#include "engine_workforce_hud.h"     /* engine_workforce_row_sprite_index */
#include "gfx_dispatch.h"             /* gfx_dispatch_set_glyph_height_short */
#include "gfx_glyph.h"                /* gfx_glyph_set_glyph_w */
#include "gfx_line.h"                 /* gfx_line_rect, _bevel_dark_br */
#include "gfx_panel.h"                /* gfx_panel_draw */
#include "gfx_sprite.h"               /* gfx_sprite_fill_block */
#include "gfx_sprite_render.h"        /* sprite blits + seal */
#include "gfx_text.h"                 /* gfx_text_line_x_acc / _set */
#include "gfx_text_label.h"           /* render_dec / labeled_signed */
#include "gfx_wrap.h"                 /* localized_line[_centered] */
#include "io.h"                       /* io_read_file_at (FUN_004af8d9) */
#include "io_path.h"                  /* io_path_chdir_base (FUN_004af502) */

#define COUNTY(base, key) ((base) + (size_t)(key) * 0x300)

/* ── .rdata tables (r2-extracted from Lords2.exe, scr.color=false) ──────
 * Indexed by task (1..9) unless noted; the code guards the task range so
 * the slot-0 padding entry is never read for the per-task arrays. */
/* DAT_004d2950 sound/anim cue { 0,7,4,6,0,10,8,9,0,0 } per task drives
 * 0x426216 — audio playback is deferred (audio.h no-op), so the
 * table is intentionally not read here. */
static const int k_labor_task_icon[11] =   /* DAT_004d2974 (task 9 → 0x10) */
    { 0, 0, 2, 3, 5, 6, 7, 8, 9, 0x0a, 0x0a };
static const int k_labor_panel_rows[10] =  /* DAT_004d29a0 panel tile height */
    { 0, 13, 13, 9, 11, 9, 9, 9, 9, 9 };

/* Indexed by the county's weapon selector byte (+0x290), 0..5. */
static const int k_labor_weapon_unitsub[6] =  /* DAT_004d29c8 loc-grp-8 sub */
    { 0x18, 0x16, 0x1a, 0x12, 0x14, 0x1c };
static const int k_labor_hearth_yoff[6] =     /* DAT_004d29e0 hearth y-offset */
    { 0x88, 0x8e, 0x77, 0x7d, 0x7c, 0xb6 };
static const int k_labor_weapon_rate[6] =     /* DAT_004d8990 (+0, stride 8) */
    { 6, 4, 3, 6, 0x0d, 4 };
static const int k_labor_weapon_cost[6] =     /* DAT_004d8994 (+4, stride 8) */
    { 0x0a, 4, 0x0a, 3, 0, 0x12 };

/* Indexed by the castle build-kind byte (+0x1c0), 0..6. */
static const int k_labor_castle_a[7] =        /* DAT_004d8a0c */
    { 0x9c4, 0x96, 0xc8, 0xc8, 0x190, 0x258, 0 };
static const int k_labor_castle_b[7] =        /* DAT_004d8a24 */
    { 0, 0x32, 0x4b, 0x64, 0x7d, 0x96, 0 };

/* ── The selected-task global (retail DAT_00553f54) ─────────────────── */
static int g_labor_task;
int  engine_labor_task(void)         { return g_labor_task; }
void engine_labor_set_task(int task) { g_labor_task = task; }

/* ── County-record field accessors ─────────────────────────────────── */
static int32_t cty_i32(const uint8_t *rec, int off)
{
    int32_t v;
    memcpy(&v, rec + off, 4);
    return v;
}
static int cty_i16(const uint8_t *rec, int off)
{
    int16_t v;
    memcpy(&v, rec + off, 2);
    return v;
}
static int cty_u8(const uint8_t *rec, int off) { return rec[off]; }
static int cty_s8(const uint8_t *rec, int off) { return (int)(int8_t)rec[off]; }

/* ── Asset scratch buffer (retail's shared DAT_004eabec) ────────────────
 * iconvill.pl8 (160000) / smithy.pl8 / hearth.pl8 (200000 each) load in
 * turn into one persistent buffer, exactly as retail reuses DAT_004eabec. */
#define LABOR_SCRATCH_ALLOC 200000
static uint8_t *g_labor_scratch;
static int labor_ensure_scratch(void)
{
    if (!g_labor_scratch) {
        g_labor_scratch = malloc(LABOR_SCRATCH_ALLOC);
    }
    return g_labor_scratch != NULL;
}

/* ── FUN_0041ab67 — a decimal value + its loc-group-8 unit noun ─────────
 * Renders `value` (render_dec, '@' blank-sign slot, empty suffix), then
 * loc group 8 sub `unit_sub` for ±1 (singular) else `unit_sub + 1`
 * (plural) at the number's advance.  Preserves the caller's line-x
 * accumulator around the pair (retail saves/restores DAT_005cd404). */
static void labor_num_units(const engine_labor_ctx_t *ctx, int value,
                            int unit_sub, int x, int y,
                            const uint8_t *font, uint8_t color)
{
    int saved = gfx_text_line_x_acc();          /* iVar1 = DAT_005cd404 */
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(value, '@', "", x, y,   /* &DAT_004d41f4 = "" */
                              font, ctx->glyph_table, color);
    int sub = (value == 1 || value == -1) ? unit_sub : unit_sub + 1;
    gfx_wrap_localized_line(ctx->loc, 8, sub,
                            x + gfx_text_line_x_acc(), y,
                            font, ctx->glyph_table, color);
    gfx_text_set_line_x_acc(gfx_text_line_x_acc() + saved);
}

/* ─── Sub-renderer forward declarations ─────────────────────────────── */
static void labor_panel_grain(const engine_labor_ctx_t *ctx, const uint8_t *rec);
static void labor_panel_cattle(const engine_labor_ctx_t *ctx, const uint8_t *rec);
static void labor_panel_reclaim(const engine_labor_ctx_t *ctx, const uint8_t *rec);
static void labor_panel_castle(const engine_labor_ctx_t *ctx);
static void labor_panel_mine(const engine_labor_ctx_t *ctx, const uint8_t *rec);
static void labor_panel_smithy(const engine_labor_ctx_t *ctx, const uint8_t *rec);

/* ─── FUN_00412b33 — the screen-0x0f dispatcher / common chrome ──────── */
void engine_labor_render(const engine_labor_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x00412b33);
    if (!ctx->county_block || !ctx->loc) {
        return;
    }
    int task = ctx->task;                        /* DAT_00553f54 */
    if (task == 0) {
        return;
    }
    const uint8_t *rec = COUNTY(ctx->county_block, ctx->county);

    /* FUN_004af502() cwd revert; FUN_004b1de0() verified no-op;
     * FUN_0045240a() full-screen dirty mark — redraw bookkeeping owned by
     * the caller's seam (the port repaints the full frame), so omitted
     * here as in the county panels. */
    io_path_chdir_base();

    /* FUN_004af8d9("iconvill.pl8", DAT_004eabec, 160000, 0). */
    if (!labor_ensure_scratch()) {
        return;
    }
    io_read_file_at("iconvill.pl8", g_labor_scratch, 160000, 0);

    /* DAT_005c9258 = 0x19 — glyph_w scratch; the icon cell below passes
     * its size to gfx_panel_draw explicitly (which re-stages glyph state
     * per tile), so this seed is inert for pixels — same as the county
     * panels' seal-scratch stores.  Omitted. */
    if (task == 8) {
        /* Blacksmith: the top menu bar redraws over the smithy scene
         * (FUN_00419c78), then the smithy sub-renderer paints the rest.
         * DAT_0056d6a0/005530d0/00567d0c are redraw/menubar-active flags
         * owned by the seam here; the fire.wav + cue-8 (0x427990 /
         * 0x426216) are audio no-ops. */
        engine_screen_strat_render_menubar();
    } else {
        /* k_labor_sound_cue[task] via 0x426216 — audio no-op. */

        /* FUN_00409397(0x30, 0x60, 0x19, rows) — the parchment panel;
         * proven equivalent to gfx_panel_draw(chrome, ...) (county panels,
         * byte-exact).  25 × rows tiles of 16 px cover the workforce grid. */
        if (ctx->chrome) {
            gfx_panel_draw(ctx->chrome, 0x30, 0x60, 0x19, k_labor_panel_rows[task]);
        }
        /* FUN_00403cf4(0x40, 0x68, 0x32, 0x32, 0x3f) — the icon frame. */
        gfx_line_rect(0x40, 0x68, 0x32, 0x32, 0x3f);

        /* FUN_0040a682(icon, 0x41, 0x69) — the task icon sprite from the
         * iconvill blob (task 9 → sprite 0x10). */
        int icon = k_labor_task_icon[task];
        if (task == 9) {
            icon = 0x10;
        }
        gfx_sprite_render_set_blob(g_labor_scratch);
        gfx_sprite_render(icon, 0x41, 0x69);

        /* FUN_00402d37(0x4a, task, 0x80, 0x6a, title, 0x3f) — task name. */
        gfx_wrap_localized_line(ctx->loc, 0x4a, task, 0x80, 0x6a,
                                ctx->title_font, ctx->glyph_table, 0x3f);

        /* Worker count — red 0xf9 when allocated < wanted (understaffed),
         * amber 0xfc when available < allocated (over-allocated), else
         * 0x3f.  Triple at +0xc4/+0xc8/+0xcc, per task at +(task-1)*0xc. */
        int base = 0xc4 + (task * 3 - 3) * 4;
        int allocated = cty_i32(rec, base);
        int wanted    = cty_i32(rec, base + 4);
        int available = cty_i32(rec, base + 8);
        uint8_t color = 0x3f;
        if (allocated < wanted) {
            color = 0xf9;
        } else if (available < allocated) {
            color = 0xfc;
        }
        /* FUN_0041ab67(allocated, task*2+0x1e, 0x80, 0x88, menu, color). */
        labor_num_units(ctx, allocated, task * 2 + 0x1e, 0x80, 0x88,
                        ctx->menu_font, color);
    }

    /* Task-specific sub-renderer (tasks 5-7 share the mine renderer). */
    if (task < 5 || 7 < task) {
        if (task == 1) {
            labor_panel_grain(ctx, rec);
        } else if (task == 2) {
            labor_panel_cattle(ctx, rec);
        } else if (task == 3) {
            labor_panel_reclaim(ctx, rec);
        } else if (task == 4) {
            labor_panel_castle(ctx);
        } else if (task == 8) {
            labor_panel_smithy(ctx, rec);
        }
    } else {
        labor_panel_mine(ctx, rec);
    }

    /* FUN_0040d1bc — the "click to continue" seal.  Blacksmith fixes it
     * at (0x1c0, 0x1c0); every other task drops it below the panel at
     * y = rows*0x10 + 0x44. */
    if (task == 8) {
        gfx_sprite_seal_draw(ctx->system_sheet, 0x1c0, 0x1c0, 0);
    } else {
        gfx_sprite_seal_draw(ctx->system_sheet, 0x1a4,
                             k_labor_panel_rows[task] * 0x10 + 0x44, 0);
    }
    /* DAT_004eb260 = 1 — redraw-latch, owned by the seam. */
}

/* ─── FUN_00413590 — grain farming ──────────────────────────────────── */
static void labor_panel_grain(const engine_labor_ctx_t *ctx, const uint8_t *rec)
{
    CALL_TRACE_ENTER(0x00413590);
    const uint8_t *menu = ctx->menu_font;

    /* Fields worked (DAT_0053fbd4 +0x224), unit sub 2. */
    labor_num_units(ctx, cty_i32(rec, 0x224), 2, 0x130, 0x88, menu, 0x3f);

    /* Advanced: yield = (base + 100) / 0x1d as a loc-grp-0x16 name
     * (DAT_0053fbb8 +0x208). */
    if (ctx->advanced_mode == 1) {
        gfx_wrap_localized_line(ctx->loc, 0x16,
                                (cty_i32(rec, 0x208) + 100) / 0x1d,
                                0x80, 0x98, menu, ctx->glyph_table, 0x3f);
    }

    /* Seed sown (DAT_0053fc28 +0x278): "none" (grp 0x4d sub 0x18) or the
     * amount + a kind label keyed by the i16 at +0x1aa (0x87 / 0x8b). */
    gfx_text_set_line_x_acc(0);
    if (cty_i32(rec, 0x278) == 0) {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0x18, 0x40, 0xb0,
                                menu, ctx->glyph_table, 0x3f);
    } else {
        labor_num_units(ctx, cty_i32(rec, 0x278), 2, 0x40, 0xb0, menu, 0x3f);
        int kind = cty_i16(rec, 0x1aa);            /* DAT_0053fb5a */
        if (kind == 0x87) {
            gfx_wrap_localized_line(ctx->loc, 0x4d, 0x19,
                                    0x40 + gfx_text_line_x_acc(), 0xb0,
                                    menu, ctx->glyph_table, 0x3f);
        } else if (kind == 0x8b) {
            gfx_wrap_localized_line(ctx->loc, 0x4d, 0x1a,
                                    0x40 + gfx_text_line_x_acc(), 0xb0,
                                    menu, ctx->glyph_table, 0x3f);
        }
    }

    /* Advanced: last season's bushel change (DAT_0053fbfc +0x24c). */
    if (ctx->advanced_mode == 1) {
        gfx_text_set_line_x_acc(0);
        int bushels = cty_i32(rec, 0x24c);
        if (bushels < 1) {
            if (bushels < 0) {
                labor_num_units(ctx, -bushels, 2, 0x40, 0xc0, menu, 0x3f);
                gfx_wrap_localized_line(ctx->loc, 0x4d, 0x11,
                                        0x40 + gfx_text_line_x_acc(), 0xc0,
                                        menu, ctx->glyph_table, 0x3f);
            } else {
                gfx_wrap_localized_line(ctx->loc, 0x4d, 0x12, 0x40, 0xc0,
                                        menu, ctx->glyph_table, 0x3f);
            }
        } else {
            labor_num_units(ctx, bushels, 2, 0x40, 0xc0, menu, 0x3f);
            gfx_wrap_localized_line(ctx->loc, 0x4d, 0x10,
                                    0x40 + gfx_text_line_x_acc(), 0xc0,
                                    menu, ctx->glyph_table, 0x3f);
        }
    }

    /* Season-dependent harvest block. */
    if (ctx->season == 1) {                        /* DAT_0057c92c */
        gfx_text_set_line_x_acc(0);
        labor_num_units(ctx, cty_i32(rec, 0x230), 2, 0x40, 0xd8, menu, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x4d, 1,
                                0x40 + gfx_text_line_x_acc(), 0xd8,
                                menu, ctx->glyph_table, 0x3f);
        gfx_text_set_line_x_acc(0);
        /* DAT_0053fbe0 (+0x230) * DAT_0057c8e0 (prod_scale). */
        labor_num_units(ctx, cty_i32(rec, 0x230) * ctx->prod_scale, 2,
                        0x40, 0xe8, menu, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x4d, 2,
                                0x40 + gfx_text_line_x_acc(), 0xe8,
                                menu, ctx->glyph_table, 0x3f);
    } else {
        int label_val;
        if (ctx->season == 2) {
            label_val = 3;
        } else if (ctx->season == 3) {
            label_val = 2;
        } else {
            label_val = 1;
        }
        int amount = (ctx->season == 4) ? cty_i32(rec, 0x248)   /* DAT_0053fbf8 */
                                        : cty_i32(rec, 0x2fc);  /* DAT_0053fcac */
        gfx_text_set_line_x_acc(0);
        labor_num_units(ctx, amount, 2, 0x40, 0xd8, menu, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x4d, 3,
                                0x40 + gfx_text_line_x_acc(), 0xd8,
                                menu, ctx->glyph_table, 0x3f);
        labor_num_units(ctx, label_val, 0x42,
                        0x40 + gfx_text_line_x_acc(), 0xd8, menu, 0x3f);
        gfx_text_set_line_x_acc(0);
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0, 0x40, 0xe8,
                                menu, ctx->glyph_table, 0x3f);
        /* DAT_0053fbf0 (+0x240). */
        labor_num_units(ctx, cty_i32(rec, 0x240), 2,
                        0x40 + gfx_text_line_x_acc(), 0xe8, menu, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x4d, 4,
                                0x40 + gfx_text_line_x_acc(), 0xe8,
                                menu, ctx->glyph_table, 0x3f);
    }

    /* Tithe row (DAT_0053fb28 +0x178, drawn negated) — "0 " when zero. */
    gfx_wrap_localized_line(ctx->loc, 0x4d, 0x1b, 0x40, 0x108,
                            menu, ctx->glyph_table, 0x3f);
    if (cty_i32(rec, 0x178) == 0) {
        gfx_text_label_render_dec(0, '@', " ", 0x130, 0x108,   /* DAT_004d3ea4 */
                                  menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_label_render_labeled_signed(
            -cty_i32(rec, 0x178), 0, (const uint8_t *)" ", " ",  /* eac / ea8 */
            0x128, 0x108, menu, ctx->glyph_table, 0x3f, 0xf9);
    }

    /* Seed-for-next row (DAT_0053fbdc +0x22c). */
    gfx_wrap_localized_line(ctx->loc, 0x4d, 0x1c, 0x40, 0x118,
                            menu, ctx->glyph_table, 0x3f);
    if (cty_i32(rec, 0x22c) == 0) {
        gfx_text_label_render_dec(0, '@', " ", 0x130, 0x118,   /* DAT_004d3eb0 */
                                  menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_label_render_labeled_signed(
            cty_i32(rec, 0x22c), 0, (const uint8_t *)" ", " ",  /* eb8 / eb4 */
            0x128, 0x118, menu, ctx->glyph_table, 0x3f, 0xf9);
    }
}

/* ─── FUN_00413b30 — cattle farming ─────────────────────────────────── */
static void labor_panel_cattle(const engine_labor_ctx_t *ctx, const uint8_t *rec)
{
    CALL_TRACE_ENTER(0x00413b30);
    const uint8_t *menu = ctx->menu_font;

    /* Herd size (DAT_0053fc00 +0x250), unit sub 4. */
    labor_num_units(ctx, cty_i32(rec, 0x250), 4, 0x130, 0x88, menu, 0x3f);

    /* Age band label from +0x25c (DAT_0053fc0c): 10 / 20 / 30 / other. */
    int age = cty_i32(rec, 0x25c);
    if (age == 10) {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 8, 0x40, 0xa0, menu, ctx->glyph_table, 0x3f);
    } else if (age == 0x14) {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 9, 0x40, 0xa0, menu, ctx->glyph_table, 0x3f);
    } else if (age == 0x1e) {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 10, 0x40, 0xa0, menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0xb, 0x40, 0xa0, menu, ctx->glyph_table, 0x3f);
    }

    /* Breeding stock (DAT_0053fc24 +0x274) + kind label from +0x1aa. */
    gfx_text_set_line_x_acc(0);
    if (cty_i32(rec, 0x274) == 0) {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0x13, 0x40, 0xb0, menu, ctx->glyph_table, 0x3f);
    } else {
        labor_num_units(ctx, cty_i32(rec, 0x274), 4, 0x40, 0xb0, menu, 0x3f);
        int kind = cty_i16(rec, 0x1aa);            /* DAT_0053fb5a */
        int sub = -1;
        if (kind == 0x88) sub = 0x14;
        else if (kind == 0x89) sub = 0x15;
        else if (kind == 0x8c) sub = 0x16;
        else if (kind == 0x8d) sub = 0x17;
        if (sub >= 0) {
            gfx_wrap_localized_line(ctx->loc, 0x4d, sub,
                                    0x40 + gfx_text_line_x_acc(), 0xb0,
                                    menu, ctx->glyph_table, 0x3f);
        }
    }

    /* Advanced: last season's herd change (DAT_0053fc20 +0x270). */
    if (ctx->advanced_mode == 1) {
        gfx_text_set_line_x_acc(0);
        int births = cty_i32(rec, 0x270);
        if (births < 1) {
            if (births < 0) {
                labor_num_units(ctx, -births, 4, 0x40, 0xc0, menu, 0x3f);
                gfx_wrap_localized_line(ctx->loc, 0x4d, 0x11,
                                        0x40 + gfx_text_line_x_acc(), 0xc0,
                                        menu, ctx->glyph_table, 0x3f);
            } else {
                gfx_wrap_localized_line(ctx->loc, 0x4d, 0x12, 0x40, 0xc0,
                                        menu, ctx->glyph_table, 0x3f);
            }
        } else {
            labor_num_units(ctx, births, 4, 0x40, 0xc0, menu, 0x3f);
            gfx_wrap_localized_line(ctx->loc, 0x4d, 0x10,
                                    0x40 + gfx_text_line_x_acc(), 0xc0,
                                    menu, ctx->glyph_table, 0x3f);
        }
    }

    /* Dairy in / out (DAT_0053fc18 +0x268, DAT_0053fc1c +0x26c). */
    gfx_wrap_localized_line(ctx->loc, 0x4d, 5, 0x40, 0xd8, menu, ctx->glyph_table, 0x3f);
    labor_num_units(ctx, cty_i32(rec, 0x268), 4, 0x130, 0xd8, menu, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x4d, 6, 0x40, 0xe8, menu, ctx->glyph_table, 0x3f);
    labor_num_units(ctx, cty_i32(rec, 0x26c), 4, 0x130, 0xe8, menu, 0x3f);

    /* Net dairy (in − out): "even" (grp sub 0x1b) or the signed delta. */
    gfx_wrap_localized_line(ctx->loc, 0x4d, 7, 0x40, 0xf8, menu, ctx->glyph_table, 0x3f);
    if (cty_i32(rec, 0x268) == cty_i32(rec, 0x26c)) {
        gfx_text_label_render_dec(0, '@', " ", 0x130, 0xf8,   /* DAT_004d3ebc */
                                  menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_label_render_labeled_signed(
            cty_i32(rec, 0x268) - cty_i32(rec, 0x26c), 0,
            (const uint8_t *)" ", " ",              /* ec4 / ec0 */
            0x128, 0xf8, menu, ctx->glyph_table, 0x3f, 0xf9);
    }

    /* Tithe row (DAT_0053fb2c +0x17c, drawn negated). */
    gfx_wrap_localized_line(ctx->loc, 0x4d, 0x1b, 0x40, 0x108, menu, ctx->glyph_table, 0x3f);
    if (cty_i32(rec, 0x17c) == 0) {
        gfx_text_label_render_dec(0, '@', " ", 0x130, 0x108,   /* DAT_004d3ec8 */
                                  menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_label_render_labeled_signed(
            -cty_i32(rec, 0x17c), 0, (const uint8_t *)" ", " ",  /* ed0 / ecc */
            0x128, 0x108, menu, ctx->glyph_table, 0x3f, 0xf9);
    }

    /* Net herd change row (DAT_0053fc08 +0x258). */
    gfx_wrap_localized_line(ctx->loc, 0x4d, 0x1c, 0x40, 0x118, menu, ctx->glyph_table, 0x3f);
    if (cty_i32(rec, 0x258) == 0) {
        gfx_text_label_render_dec(0, '@', " ", 0x130, 0x118,   /* DAT_004d3ed4 */
                                  menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_label_render_labeled_signed(
            cty_i32(rec, 0x258), 0, (const uint8_t *)" ", " ",  /* edc / ed8 */
            0x128, 0x118, menu, ctx->glyph_table, 0x3f, 0xf9);
    }
}

/* ─── FUN_004140f3 — field reclamation ──────────────────────────────── */
static void labor_panel_reclaim(const engine_labor_ctx_t *ctx, const uint8_t *rec)
{
    CALL_TRACE_ENTER(0x004140f3);
    const uint8_t *menu = ctx->menu_font;

    /* Progress state byte (DAT_0053fbb4 +0x204): the value, then a
     * done/in-progress label (grp 0x4d sub 0xc / 0xd). */
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(cty_u8(rec, 0x204), '@', "",   /* DAT_004d3ee0 = "" */
                              0x40, 0xb8, menu, ctx->glyph_table, 0x3f);
    if (cty_u8(rec, 0x204) == 1) {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0xc,
                                0x40 + gfx_text_line_x_acc(), 0xb8,
                                menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0xd,
                                0x40 + gfx_text_line_x_acc(), 0xb8,
                                menu, ctx->glyph_table, 0x3f);
    }

    /* Active reclaimers (DAT_0053fbc4 +0x214): "none" (sub 0xf) or the
     * count with the "men" unit noun (sub 0x42). */
    if (cty_i32(rec, 0x214) == 0) {
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0xf, 0x40, 200,
                                menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_set_line_x_acc(0);
        gfx_wrap_localized_line(ctx->loc, 0x4d, 0xe, 0x40, 200,
                                menu, ctx->glyph_table, 0x3f);
        labor_num_units(ctx, cty_i32(rec, 0x214), 0x42,
                        0x40 + gfx_text_line_x_acc(), 200, menu, 0x3f);
    }
}

/* ─── FUN_0041dedb — one castle-status block ────────────────────────────
 * Shared with the county summary; here `county` = the current county,
 * `bx / by / row` = (-0x20, 0x40, 0). */
static void labor_castle_block(const engine_labor_ctx_t *ctx, int county,
                               int bx, int by, int row)
{
    CALL_TRACE_ENTER(0x0041dedb);
    const uint8_t *menu = ctx->menu_font;
    const uint8_t *rec = COUNTY(ctx->county_block, county);
    int kind = cty_u8(rec, 0x1c0);                 /* DAT_0053fb70 */
    int y0 = row * 0x10 + by;

    /* "Castle wall" label + the wall-strength percent (table B, " %"). */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x47, 0x10, bx + 0x60, y0 + 0x68,
                            menu, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(k_labor_castle_b[kind], ' ', " %",   /* DAT_004d4290 */
                              bx + gfx_text_line_x_acc() + 0x60, y0 + 0x68,
                              menu, ctx->glyph_table, 0x3f);

    /* "Cost" label + the total cost (table A, " ") + a units suffix. */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x47, 0xb, bx + 0x60, y0 + 0x78,
                            menu, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(k_labor_castle_a[kind], ' ', " ",   /* DAT_004d4294 */
                              bx + gfx_text_line_x_acc() + 0x60, y0 + 0x78,
                              menu, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x47, 0xc,
                            bx + gfx_text_line_x_acc() + 0x60, y0 + 0x78,
                            menu, ctx->glyph_table, 0x3f);

    /* Stone stockpiled (DAT_0053fb80 +0x1d0), unit sub 0xe. */
    gfx_text_set_line_x_acc(0);
    labor_num_units(ctx, cty_i32(rec, 0x1d0), 0xe, bx + 0x60, y0 + 0x90, menu, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x47, 6,
                            bx + gfx_text_line_x_acc() + 0x60, y0 + 0x90,
                            menu, ctx->glyph_table, 0x3f);

    /* Labour applied (DAT_0053fb84 +0x1d4), unit sub 0x10. */
    gfx_text_set_line_x_acc(0);
    labor_num_units(ctx, cty_i32(rec, 0x1d4), 0x10, bx + 0x60, y0 + 0xa0, menu, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x47, 7,
                            bx + gfx_text_line_x_acc() + 0x60, y0 + 0xa0,
                            menu, ctx->glyph_table, 0x3f);

    /* Build progress (DAT_0053fb56 +0x1a6): "not building" (sub 0x11) or
     * the money committed (DAT_0053fa98 +0xe8, sub 0x26) + the "%" count. */
    int building = cty_u8(rec, 0x1a6);
    if (building == 0) {
        gfx_wrap_localized_line(ctx->loc, 0x47, 0x11, bx + 0x60, y0 + 0xb0,
                                menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_set_line_x_acc(0);
        labor_num_units(ctx, cty_i32(rec, 0xe8), 0x26, bx + 0x60, y0 + 0xb0, menu, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x47, 8,
                                bx + gfx_text_line_x_acc() + 0x60, y0 + 0xb0,
                                menu, ctx->glyph_table, 0x3f);
        labor_num_units(ctx, building, 0x42,
                        bx + gfx_text_line_x_acc() + 0x60, y0 + 0xb0, menu, 0x3f);
    }
}

/* ─── FUN_00414220 — castle building ────────────────────────────────── */
static void labor_panel_castle(const engine_labor_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x00414220);
    labor_castle_block(ctx, ctx->county, -0x20, 0x40, 0);
}

/* ─── FUN_00412e6b — iron / stone / wood mining (tasks 5/6/7) ────────── */
static void labor_panel_mine(const engine_labor_ctx_t *ctx, const uint8_t *rec)
{
    CALL_TRACE_ENTER(0x00412e6b);
    const uint8_t *menu = ctx->menu_font;
    int task = ctx->task;

    /* slot = the resource's storage lane; unit = its loc-grp-8 noun. */
    int slot = 0, unit = 0;
    if (task == 5) {
        slot = 1; unit = 0xc;
    } else if (task == 6) {
        slot = 3; unit = 0xe;
    } else if (task == 7) {
        slot = 0; unit = 0x10;
    } else if (task == 8) {
        slot = 2;
        unit = k_labor_weapon_unitsub[cty_u8(rec, 0x290)];  /* DAT_0053fc40 */
    }

    /* Advanced: the resource's price/quality readout (DAT_0053fc44 +0x294,
     * signed byte, per lane) as "N%". */
    if (ctx->advanced_mode == 1) {
        gfx_text_set_line_x_acc(0);
        gfx_wrap_localized_line(ctx->loc, 0x4c, 0, 0x40, 0xa0,
                                menu, ctx->glyph_table, 0x3f);
        gfx_text_label_render_dec(cty_s8(rec, 0x294 + slot * 0x18), '@', "%",  /* DAT_004d3e74 */
                                  0x40 + gfx_text_line_x_acc(), 0xa0,
                                  menu, ctx->glyph_table, 0x3f);
    }

    /* Stockpile (DAT_0053fc58 +0x2a8, per lane) + "in store" label. */
    gfx_text_set_line_x_acc(0);
    labor_num_units(ctx, cty_i32(rec, 0x2a8 + slot * 0x18), unit, 0x40, 0xb0, menu, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x4c, 1,
                            0x40 + gfx_text_line_x_acc(), 0xb0,
                            menu, ctx->glyph_table, 0x3f);

    /* Per-resource production-rate rows. */
    if (task == 5) {
        gfx_text_set_line_x_acc(0);
        labor_num_units(ctx, cty_i32(rec, 0x284), unit, 0x40, 0xc0, menu, 0x3f);  /* DAT_0053fc34 */
        gfx_wrap_localized_line(ctx->loc, 0x4c, 2,
                                0x40 + gfx_text_line_x_acc(), 0xc0,
                                menu, ctx->glyph_table, 0x3f);
    } else if (task == 6) {
        gfx_text_set_line_x_acc(0);
        labor_num_units(ctx, cty_i32(rec, 0x28c), unit, 0x40, 0xc0, menu, 0x3f);  /* DAT_0053fc3c */
        gfx_wrap_localized_line(ctx->loc, 0x4c, 3,
                                0x40 + gfx_text_line_x_acc(), 0xc0,
                                menu, ctx->glyph_table, 0x3f);
    } else if (task == 7) {
        gfx_text_set_line_x_acc(0);
        labor_num_units(ctx, cty_i32(rec, 0x280), unit, 0x40, 0xc0, menu, 0x3f);  /* DAT_0053fc30 */
        gfx_wrap_localized_line(ctx->loc, 0x4c, 2,
                                0x40 + gfx_text_line_x_acc(), 0xc0,
                                menu, ctx->glyph_table, 0x3f);
        gfx_text_set_line_x_acc(0);
        labor_num_units(ctx, cty_i32(rec, 0x288), unit, 0x40, 0xd0, menu, 0x3f);  /* DAT_0053fc38 */
        gfx_wrap_localized_line(ctx->loc, 0x4c, 3,
                                0x40 + gfx_text_line_x_acc(), 0xd0,
                                menu, ctx->glyph_table, 0x3f);
    }
}

/* ─── FUN_00413155 — blacksmith / weapon production ─────────────────── */
static void labor_panel_smithy(const engine_labor_ctx_t *ctx, const uint8_t *rec)
{
    CALL_TRACE_ENTER(0x00413155);
    const uint8_t *menu = ctx->menu_font;
    int weapon = cty_u8(rec, 0x290);               /* DAT_0053fc40 */

    if (!labor_ensure_scratch()) {
        return;
    }

    /* FUN_004af502() cwd revert; smithy.pl8 full-screen backdrop at
     * (0, 0x18). */
    io_path_chdir_base();
    io_read_file_at("smithy.pl8", g_labor_scratch, 200000, 0);
    gfx_sprite_render_set_blob(g_labor_scratch);
    gfx_sprite_render(0, 0, 0x18);

    /* FUN_004b414a(0, 0x1a8, 0) — a solid block sized from the seeded
     * glyph dims (glyph_w 0x1e groups × height_short 0x38). */
    gfx_glyph_set_glyph_w(0x1e);
    gfx_dispatch_set_glyph_height_short(0x38);
    gfx_sprite_fill_block(0, 0x1a8, 0);

    /* hearth.pl8 — the anvil sprite (weapon + 0xb) at the weapon's
     * baseline (0x1a8 − hearth y-offset). */
    io_read_file_at("hearth.pl8", g_labor_scratch, 200000, 0);
    gfx_sprite_render_rle_from_blob(g_labor_scratch, weapon + 0xb, 0,
                                    0x1a8 - k_labor_hearth_yoff[weapon]);

    /* FUN_00409397(0, 0x180, 0x1e, 6) — the readout parchment strip. */
    if (ctx->chrome) {
        gfx_panel_draw(ctx->chrome, 0, 0x180, 0x1e, 6);
    }

    /* Title (grp 0x4a sub 8) + the centered weapon caption (grp 0x4b). */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x4a, 8, 0x10, 0x186,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line_centered(ctx->loc, 0x4b, 0, 0, 0x1cc, 0x1cc,
                                     menu, ctx->glyph_table, ctx->glyph_metrics, 0x3f);

    int unit = k_labor_weapon_unitsub[weapon];     /* DAT_004d29c8 */

    /* Gold row: advanced also shows the stockpiled-weapons percent. */
    if (ctx->advanced_mode == 1) {
        gfx_text_set_line_x_acc(0);
        gfx_text_label_render_dec(cty_i32(rec, 0x118), '@', " ",   /* DAT_0053fac8 / e90 */
                                  0x10, 0x1a4, menu, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x4c, 4,
                                gfx_text_line_x_acc() + 0x10, 0x1a4,
                                menu, ctx->glyph_table, 0x3f);
        gfx_text_label_render_dec(cty_s8(rec, 0x2c4), '@', "%",   /* DAT_0053fc74 / e94 */
                                  gfx_text_line_x_acc() + 0x10, 0x1a4,
                                  menu, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x4c, 5,
                                gfx_text_line_x_acc() + 0x10, 0x1a4,
                                menu, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_set_line_x_acc(0);
        gfx_text_label_render_dec(cty_i32(rec, 0x118), '@', " ",   /* DAT_004d3e98 */
                                  0x10, 0x1a4, menu, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x4c, 8,
                                gfx_text_line_x_acc() + 0x10, 0x1a4,
                                menu, ctx->glyph_table, 0x3f);
    }

    /* Production rate (DAT_0053fc88 +0x2d8) with the weapon unit noun. */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x4c, 6, 0x10, 0x1b4,
                            menu, ctx->glyph_table, 0x3f);
    labor_num_units(ctx, cty_i32(rec, 0x2d8), unit,
                    gfx_text_line_x_acc() + 0x10, 0x1b4, menu, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x4c, 7,
                            gfx_text_line_x_acc() + 0x10, 0x1b4,
                            menu, ctx->glyph_table, 0x3f);

    /* Cost/rate icons + figures (misc_cty sprites 0x2c / 0x2e). */
    gfx_line_rect_bevel_dark_br(0x140, 0x186, 0x8e, 0x20);
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x2c, 0x146, 0x187);
    }
    gfx_text_label_render_dec(k_labor_weapon_cost[weapon], '@', "",   /* DAT_004d8994 / e9c */
                              0x16a, 0x18e, menu, ctx->glyph_table, 0x3f);
    if (ctx->misc_sheet) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x2e, 0x189, 0x18a);
    }
    gfx_text_label_render_dec(k_labor_weapon_rate[weapon], '@', "",   /* DAT_004d8990 / ea0 */
                              0x1b4, 0x18e, menu, ctx->glyph_table, 0x3f);

    /* FUN_0045240a() full-screen dirty mark — seam-owned, omitted (see
     * engine_labor_render). */
}

/* ─── FUN_0043a123 — the workforce-grid open hit-test ───────────────── */
int engine_labor_open_hit_test(const engine_labor_open_in_t *in)
{
    CALL_TRACE_ENTER(0x0043a123);
    if (in->lmb_click_fired == 0) {                /* DAT_004eabf0 == 0 */
        return 0;
    }
    /* FUN_004398f5(cursor_x, cursor_y) — which panel category cell (0..8),
     * 0 = miss.  Indexes the baked task-region grid (retail DAT_00542cf8)
     * with panel-relative, camera-shifted coords. */
    int cell = 0;
    if (in->cursor_x >= 0x40 && in->cursor_x < 0x1a8) {
        if (in->cursor_y >= in->camera_y && in->cursor_y < in->camera_y + 0x140) {
            int col = (in->cursor_x - 0x40) >> 3;
            int row = (in->cursor_y - in->camera_y) >> 3;
            cell = k_labor_panel_region_grid[col + row * LABOR_PANEL_GRID_W];
            if (cell > 7) {
                cell = 8;
            }
        }
    }
    if (cell == 0) {
        return 0;
    }
    /* Inlined port of FUN_0045183a(county, cell-1) — is that task present for the county? */
    int idx = cell - 1;
    const uint8_t *rec = COUNTY(in->county_block, in->county);
    int present = 1;
    if (idx == 0) {
        /* Capital descriptors at +0x2dd / +0x2ad gate task 1's presence. */
        present = (rec[0x2dd] != 0 || rec[0x2ad] != 0) ? 1 : 0;
    }
    if (present == 0) {
        return 0;
    }
    /* FUN_004517ca(county, cell-1) — the task index for that cell; +1 to
     * the 1-based selector. */
    int task = engine_workforce_row_sprite_index(in->county, idx);
    g_labor_task = task + 1;
    return 1;
}
