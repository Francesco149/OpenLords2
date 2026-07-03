/*
 * OpenLords2 — the Treasury / Court screen (screen 0x09).  See
 * engine_treasury.h for the retail address map + the per-record offsets.
 *
 * Every draw mirrors its decompile line 1:1 (coordinates, fonts, colors,
 * and the line-x-accumulator chaining).  Record offsets are kept RAW with
 * the retail DAT_ base cited; the one .rdata button table is r2-extracted
 * byte-exact.  Nothing here computes — all figures are saved-state reads.
 */

#include "engine_treasury.h"

#include <stdint.h>

#include "engine_button.h"          /* engine_button_render_list (FUN_0040cfd2) */
#include "gfx.h"                     /* lock/unlock canvas */
#include "gfx_line.h"               /* gfx_line_rect_bevel_dark_br (FUN_00403deb) */
#include "gfx_panel.h"              /* gfx_panel_draw_bevel (FUN_004093e0) */
#include "gfx_sprite_render.h"      /* gfx_sprite_render_from_blob, seal */
#include "gfx_text.h"               /* gfx_text_render_line + line-x acc */
#include "gfx_text_label.h"         /* gfx_text_label_render_dec[_centered] */
#include "gfx_wrap.h"               /* gfx_wrap_localized_line */

#define STRAT_REC(base, key) ((base) + (size_t)(key) * 0x160)   /* strategic */

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    __builtin_memcpy(&v, rec + off, sizeof v);
    return v;
}

/* DAT_00568468 — the screen-9 router widget count (retail sets it to 1 at
 * the top of FUN_00416925).  Read by the per-frame refresh + the click
 * router. */
static int g_treasury_widget_count;

/* The single per-frame widget: retail's DAT_004dd928[0] — the "continue"
 * seal button (system2.pl8 sprite 0x40, kind 0x20 = 32×32, type 5
 * press-latch; action FUN_004351c4 = advance to Rankings 0x20, deferred).
 * r2-extracted byte-exact.  Widget count DAT_00568468 is 1, so only this
 * first record renders (the table's 2nd record, FUN_00436141, is unused).
 * MUTABLE like retail's .data table — engine_menu_button_dispatch animates
 * the press flash in place and the renderer reads it back. */
static engine_button_rec_t k_treasury_buttons[1] = {
    { 0x150, 0x154, 0x40, 0x20, 0x4351c4, 0, 0, 0, 5, 1, 0 },
};

engine_button_rec_t *engine_treasury_buttons(void) { return k_treasury_buttons; }
int engine_treasury_widget_count(void) { return g_treasury_widget_count; }

/* ── FUN_0041ab67 — decimal value + its grp-8 unit noun ─────────────────
 * (singular sub for ±1, else plural sub+1).  Preserves the caller's
 * line-x accumulator around the pair.  Identical to trade_num_units /
 * labor_num_units, bound to this screen's ctx. */
static void treasury_num_units(const engine_treasury_ctx_t *ctx, int value,
                               int unit_sub, int x, int y,
                               const uint8_t *font, uint8_t color)
{
    int saved = gfx_text_line_x_acc();
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(value, '@', "", x, y,
                              font, ctx->glyph_table, color);
    int sub = (value == 1 || value == -1) ? unit_sub : unit_sub + 1;
    gfx_wrap_localized_line(ctx->loc, 8, sub,
                            x + gfx_text_line_x_acc(), y,
                            font, ctx->glyph_table, color);
    gfx_text_set_line_x_acc(gfx_text_line_x_acc() + saved);
}

/* Convenience: one localized single-line render bound to the ctx. */
static void treasury_loc(const engine_treasury_ctx_t *ctx, int grp, int sub,
                         int x, int y, const uint8_t *font, uint8_t color)
{
    gfx_wrap_localized_line(ctx->loc, grp, sub, x, y,
                            font, ctx->glyph_table, color);
}

/* ── FUN_00416925 body — the Court/Treasury panel draw ──────────────────
 * Caller holds the canvas lock.  FUN_0045240a (full-screen dirty mark) is
 * OMITTED — seam-owned, as in engine_trade / engine_labor (the port
 * repaints the full frame, so per-widget marks are inert for pixels). */
static void treasury_panel(const engine_treasury_ctx_t *ctx)
{
    const uint8_t *tf = ctx->title_font;    /* fntl2_22, DAT_005b2fa0 */
    const uint8_t *mf = ctx->menu_font;     /* fntl2_14, DAT_005af8f0 */
    const uint8_t *strat = STRAT_REC(ctx->strategic_block, ctx->local_player);

    g_treasury_widget_count = 1;            /* DAT_00568468 = 1 */

    /* FUN_004093e0(0x40,0x30,0x18,0x16) — the raised bevel panel. */
    if (ctx->chrome) {
        gfx_panel_draw_bevel(ctx->chrome, 0x40, 0x30, 0x18, 0x16);
    }
    /* FUN_0040d1bc(0x198,0x158,0) — the decorative "click to continue" seal. */
    if (ctx->system_sheet) {
        gfx_sprite_seal_draw(ctx->system_sheet, 0x198, 0x158, 0);
    }

    /* "Greatest nobles." caption (grp 0x46 sub 6, menu font, bottom). */
    treasury_loc(ctx, 0x46, 6, 0x88, 0x15c, mf, 0x3f);

    /* "Court of" + the player's realm name (grp 0x46 sub 5, title font);
     * the name (FUN_00402637) chains past the label via the line-x acc. */
    gfx_text_set_line_x_acc(0);                                  /* DAT_005cd404 = 0 */
    treasury_loc(ctx, 0x46, 5, 0x50, 0x44, tf, 0x3f);
    gfx_text_render_line(ctx->realm_names + (size_t)ctx->local_player * 0x2c,
                         gfx_text_line_x_acc() + 0x52, 0x44,
                         tf, ctx->glyph_table, 0x3f);

    /* FUN_00403deb(0x50,0x68,0x160,0xe0) — the sunken resource-list frame. */
    gfx_line_rect_bevel_dark_br(0x50, 0x68, 0x160, 0xe0);

    /* Gold (grp 0x46 sub 0) + value + "Crowns." unit, value at fixed x=0xe0. */
    gfx_text_set_line_x_acc(0);
    treasury_loc(ctx, 0x46, 0, 0x60, 0x72, tf, 0x3f);
    treasury_num_units(ctx, rec_i32(strat, 0x118), 0, 0xe0, 0x72, tf, 0x3f);

    /* Iron / Stone / Wood (grp 0x46 sub 2/3/4): label + value + icon.  The
     * value is a plain decimal (space pad + trailing space, NO unit); the
     * icon (0x2c/0x2d/0x2e from misc_cty.pl8) sits to its right at x=0x130. */
    gfx_text_set_line_x_acc(0);
    treasury_loc(ctx, 0x46, 2, 0x60, 0x90, tf, 0x3f);
    gfx_text_label_render_dec(rec_i32(strat, 0x120), ' ', " ", 0xe0, 0x90,
                              tf, ctx->glyph_table, 0x3f);
    gfx_sprite_render_from_blob(ctx->misc_sheet, 0x2c, 0x130, 0x90);

    gfx_text_set_line_x_acc(0);
    treasury_loc(ctx, 0x46, 3, 0x60, 0xae, tf, 0x3f);
    gfx_text_label_render_dec(rec_i32(strat, 0x128), ' ', " ", 0xe0, 0xae,
                              tf, ctx->glyph_table, 0x3f);
    gfx_sprite_render_from_blob(ctx->misc_sheet, 0x2d, 0x130, 0xae);

    gfx_text_set_line_x_acc(0);
    treasury_loc(ctx, 0x46, 4, 0x60, 0xcc, tf, 0x3f);
    gfx_text_label_render_dec(rec_i32(strat, 0x130), ' ', " ", 0xe0, 0xcc,
                              tf, ctx->glyph_table, 0x3f);
    gfx_sprite_render_from_blob(ctx->misc_sheet, 0x2e, 0x130, 0xcc);

    /* The 6 weapon stocks: icon 0x30+i on the row at y=0xf0, count centered
     * below at y=0x10a (menu font, width 0x38).  Stock at strat +0x140 + i*4. */
    for (int i = 0; i < 6; i++) {
        gfx_sprite_render_from_blob(ctx->misc_sheet, i + 0x30,
                                    i * 0x38 + 0x64, 0xf0);
        gfx_text_label_render_dec_centered(rec_i32(strat, 0x140 + i * 4),
                                           ' ', " ", i * 0x38 + 0x54, 0x10a,
                                           0x38, mf, ctx->glyph_table,
                                           ctx->glyph_metrics, 0x3f);
    }

    /* "Total army wages of" (sub 7) + value [+0xfc]; "Tax revenues
     * expected," (sub 8) + value [+0x15c] — both menu font, value chained
     * after the label (x = 0x64 + line-x acc). */
    gfx_text_set_line_x_acc(0);
    treasury_loc(ctx, 0x46, 7, 0x64, 0x124, mf, 0x3f);
    treasury_num_units(ctx, rec_i32(strat, 0xfc), 0,
                       gfx_text_line_x_acc() + 0x64, 0x124, mf, 0x3f);

    gfx_text_set_line_x_acc(0);
    treasury_loc(ctx, 0x46, 8, 0x64, 0x136, mf, 0x3f);
    treasury_num_units(ctx, rec_i32(strat, 0x15c), 0,
                       gfx_text_line_x_acc() + 0x64, 0x136, mf, 0x3f);
}

void engine_treasury_render(const engine_treasury_ctx_t *ctx)
{
    if (!ctx->loc) {
        return;
    }
    if (gfx_lock_canvas()) {
        treasury_panel(ctx);
        /* Retail's FUN_00416925 does NOT paint the seal button — that is
         * FUN_004ba26e case 9, run every tick.  The port dispatches its
         * per-frame refresh BEFORE the demand seam, so on the OPEN frame the
         * refresh sees widget count 0 (render hasn't set it yet); paint the
         * button here too so the open frame carries it (idempotent with the
         * next tick's refresh, which repaints it over the retained panel). */
        engine_button_render_list(0, 0, k_treasury_buttons,
                                  g_treasury_widget_count,
                                  ctx->misc_sheet, ctx->system_sheet, NULL);
        gfx_unlock_canvas();
    }
}

void engine_treasury_refresh(const engine_treasury_ctx_t *ctx)
{
    if (!ctx->loc) {
        return;
    }
    if (gfx_lock_canvas()) {
        engine_button_render_list(0, 0, k_treasury_buttons,
                                  g_treasury_widget_count,
                                  ctx->misc_sheet, ctx->system_sheet, NULL);
        gfx_unlock_canvas();
    }
}
