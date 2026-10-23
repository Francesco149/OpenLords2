/*
 * OpenLords2 — the diplomacy-action screen (screen 0x1a).  See
 * engine_diplomacy_action.h for the retail address map + the mode table.
 *
 * Every draw mirrors FUN_00417960 (the gift panel) 1:1 — coordinates, fonts,
 * colours, and the line-x-accumulator chaining.  Nothing here computes: the
 * figures are saved-state reads (the partner's standing offer, the staged gift
 * amount).  The model (mode, arrow, seal) is engine_diplomacy_action_model.c.
 * Modes 1..6' renders (message / ally-attack) are DEFERRED — they need the
 * runtime message buffer (0x53f1e0) / the target list (0x5530b0) respectively.
 */

#include "engine_diplomacy_action.h"

#include <stdint.h>

#include "engine_button.h"          /* engine_button_render_list (FUN_0040cfd2) */
#include "save.h"                   /* g_player_strategic_352 */
#include "save_schema_globals.h"    /* s_slot_0057c8cc / _00553fbc / _0057a0f8 */

#include "gfx_panel.h"              /* gfx_panel_draw_bevel (FUN_004093e0) */
#include "gfx_sprite_render.h"      /* gfx_sprite_seal_draw (FUN_0040d1bc) */
#include "gfx_text.h"               /* gfx_text_render_line + line-x acc */
#include "gfx_text_label.h"         /* gfx_text_label_render_dec */
#include "gfx_wrap.h"               /* gfx_wrap_localized_line (FUN_00402d37) */

#define STRAT_STRIDE 0x160
#define SREC(base, s) ((base) + (size_t)(s) * STRAT_STRIDE)

/* Per-pair offsets into the strategic record (row p, stride 0x10 by other). */
#define F_BEST_OFFER 0x8c   /* i32 — the partner's cached best offer from us   */

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    __builtin_memcpy(&v, rec + off, sizeof v);
    return v;
}

/* ── The gift screen's 4 widgets (retail DAT_004dd9d0, r2-extracted) ──────
 * two amount arrows (type 4, action FUN_00436372, param 1 = up / 0 = down) and
 * the Yes/No confirm seals (type 5, action FUN_00436408, param 1 = yes / 0 =
 * no).  MUTABLE like retail's .data table — engine_menu_button_dispatch
 * animates the press flash in place and the refresh reads it back. */
static engine_button_rec_t k_gift_buttons[4] = {
    { 0x0b8, 0x0f0, 0x44, 0x20, 0x436372, 0, 0, 0, 4, 1, 0 },  /* amount + */
    { 0x0d8, 0x0f0, 0x42, 0x20, 0x436372, 0, 0, 0, 4, 0, 0 },  /* amount − */
    { 0x120, 0x118, 0x1d, 0x20, 0x436408, 0, 0, 0, 5, 1, 0 },  /* Yes seal */
    { 0x144, 0x11c, 0x1f, 0x20, 0x436408, 0, 0, 0, 5, 0, 0 },  /* No  seal */
};

/* The message / ally-attack modes' 2 widgets (retail DAT_004dda30): the same
 * Yes/No seals only.  Defined for completeness; modes 1..6 are DEFERRED so the
 * router does not yet dispatch this table. */
static engine_button_rec_t k_msg_buttons[2] = {
    { 0x120, 0x124, 0x1d, 0x20, 0x436408, 0, 0, 0, 5, 1, 0 },  /* Yes seal */
    { 0x144, 0x128, 0x1f, 0x20, 0x436408, 0, 0, 0, 5, 0, 0 },  /* No  seal */
};

engine_button_rec_t *engine_diplomacy_action_buttons(void)
{
    return (g_diplo_action_mode == 0) ? k_gift_buttons : k_msg_buttons;
}

int engine_diplomacy_action_widget_count(void)
{
    return (g_diplo_action_mode == 0) ? 4 : 2;
}

/* ── FUN_0041ab67 — a decimal value + its grp-8 unit noun (singular sub for
 * ±1, else plural sub+1), preserving the caller's line-x accumulator.  Same
 * shape as treasury_num_units / labor_num_units, bound to this ctx. */
static void diplo_num_units(const engine_diplomacy_action_ctx_t *ctx, int value,
                            int unit_sub, int x, int y, uint8_t color)
{
    int saved = gfx_text_line_x_acc();
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(value, '@', "", x, y,
                              ctx->menu_font, ctx->glyph_table, color);
    int sub = (value == 1 || value == -1) ? unit_sub : unit_sub + 1;
    gfx_wrap_localized_line(ctx->loc, 8, sub, x + gfx_text_line_x_acc(), y,
                            ctx->menu_font, ctx->glyph_table, color);
    gfx_text_set_line_x_acc(gfx_text_line_x_acc() + saved);
}

/* One localized single-line render bound to the ctx (FUN_00402d37). */
static void diplo_loc(const engine_diplomacy_action_ctx_t *ctx, int grp, int sub,
                      int x, int y)
{
    gfx_wrap_localized_line(ctx->loc, grp, sub, x, y,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
}

/* ── FUN_00417960 — the GIFT panel (mode 0), drawn over the recomposited map ─
 * Caller holds the canvas lock.  The retail lock/input bookkeeping
 * (DAT_004eb260 / DAT_004e65f8) is seam-owned and omitted, as is the dirty-
 * hotspot register FUN_004527a6 and the dirty mark FUN_0045240a. */
static void diplo_gift_render(const engine_diplomacy_action_ctx_t *ctx)
{
    int partner = (int)s_slot_00553fbc;
    int loc     = ctx->local_player;
    const uint8_t *mf = ctx->menu_font;

    /* DAT_0058fe2c = 1 — the illuminated drop-cap: every leading A..Z of the
     * panel's diplomacy text renders in the red highlight colour (index 1), as
     * retail does across the whole FUN_00417960 body.  Cleared at the tail. */
    gfx_text_set_uppercase_highlight(1);

    /* FUN_004093e0(0x40,0xa0,0x16,0xb) — the raised parchment panel. */
    if (ctx->chrome) {
        gfx_panel_draw_bevel(ctx->chrome, 0x40, 0xa0, 0x16, 0xb);
    }
    /* FUN_0040d1bc(0x178,0x126,0) — the decorative "click to continue" seal. */
    gfx_sprite_seal_draw(ctx->system_sheet, 0x178, 0x126, 0);

    /* Label (grp 0x48 sub 0xa) + the partner's realm name, name chained past
     * the label via the line-x accumulator (menu font). */
    gfx_text_set_line_x_acc(0);                          /* DAT_005cd404 = 0 */
    diplo_loc(ctx, 0x48, 0xa, 0x60, 0xb8);
    gfx_text_render_line(ctx->realm_names + (size_t)partner * 0x2c,
                         gfx_text_line_x_acc() + 0x60, 0xb8,
                         mf, ctx->glyph_table, 0x3f);

    /* Label (sub 0x17) + the partner's standing best offer from us
     * (strat[partner] +0x8c + local*0x10, "N Crowns"), chained. */
    gfx_text_set_line_x_acc(0);
    diplo_loc(ctx, 0x48, 0x17, 0x60, 0xd8);
    diplo_num_units(ctx,
                    rec_i32(SREC(ctx->strategic_block, partner),
                            F_BEST_OFFER + loc * 0x10),
                    0, gfx_text_line_x_acc() + 0x60, 0xd8, 0x3f);

    /* Label (sub 0x12) + the staged gift amount (DAT_0057a0f8), fixed x. */
    diplo_loc(ctx, 0x48, 0x12, 0x60, 0xf8);
    diplo_num_units(ctx, (int)s_slot_0057a0f8, 0, 0x100, 0xf8, 0x3f);

    /* Prompt label (sub 0x11). */
    diplo_loc(ctx, 0x48, 0x11, 0xa0, 0x120);

    gfx_text_set_uppercase_highlight(0);        /* DAT_0058fe2c = 0 */
}

/* ── FUN_0041789b — dispatch the current mode's render ──────────────────── */
void engine_diplomacy_action_render(const engine_diplomacy_action_ctx_t *ctx)
{
    if (!ctx->loc) {
        return;
    }
    switch (g_diplo_action_mode) {
    case 0:
        diplo_gift_render(ctx);                 /* FUN_00417960 */
        break;
    /* modes 1..4 → 0x417aef (message screens): DEFERRED — need the
     *   runtime message buffer 0x53f1e0 + 0x417bd3.
     * modes 5..6 → 0x417cef (ask-ally-to-attack): DEFERRED — need the
     *   target list 0x5530b0. */
    default:
        break;
    }
}

/* ── FUN_004ba26e case 0x1a — the per-frame widget strip ─────────────────
 * Mode 0 repaints the 4 gift widgets.  Modes 1..4 also re-render the scrolling
 * message (0x417bd3) — DEFERRED with those modes. */
void engine_diplomacy_action_refresh(const engine_diplomacy_action_ctx_t *ctx)
{
    engine_button_render_list(0, 0, engine_diplomacy_action_buttons(),
                              engine_diplomacy_action_widget_count(),
                              ctx->misc_sheet, ctx->system_sheet, NULL);
}
