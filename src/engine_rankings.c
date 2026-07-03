/*
 * OpenLords2 — the Rankings / "Greatest Nobles" scoreboard (screen 0x20).
 * See engine_rankings.h for the retail address map + the category fields.
 *
 * Every draw mirrors its decompile line 1:1 (coordinates, colors, and the
 * line-x-accumulator chaining for the caption + winner name).  The three
 * .rdata tables are r2-extracted byte-exact.  The winner/percentage compute
 * (FUN_00415bdc) + the per-category metric (FUN_00415e42) are pure reads of
 * the strategic block; their scratch outputs (retail globals 0x522c60 /
 * 0x522c78 / 0x522c94, used only by this screen family) are file-static.
 */

#include "engine_rankings.h"

#include <stdint.h>
#include <stdlib.h>

#include "engine_math.h"             /* engine_math_percent_div (FUN_00404dc1) */
#include "gfx.h"                     /* backdrop/palette/lock, GFX_RENDER_W/H */
#include "gfx_line.h"                /* gfx_line_draw (FUN_00403a8f) */
#include "gfx_sprite_render.h"       /* gfx_sprite_render[_set_blob], seal */
#include "gfx_text.h"                /* gfx_text_render_line + line-x acc */
#include "gfx_wrap.h"                /* gfx_wrap_localized_line (FUN_00402d37) */
#include "io.h"                      /* io_read_file_at (FUN_004af8d9) */
#include "io_path.h"                 /* io_path_chdir_base (FUN_004af502) */

#define STRAT_STRIDE   0x160

static uint32_t rec_u32(const uint8_t *rec, int off)
{
    uint32_t v;
    __builtin_memcpy(&v, rec + off, sizeof v);
    return v;
}

/* ── .rdata tables (r2-extracted byte-exact) ────────────────────────────
 * k_slot_x     DAT_004d2b40 — the 5 player-column x positions (slot 0 unused)
 * k_slot_player DAT_004d2b58 — display slot → player id (the banner order)
 * k_cat_x      DAT_004d2b20 — the 7 category-tab x positions (the marker) */
static const int k_slot_x[6]     = { 0x00, 0x27, 0xa7, 0x126, 0x1a8, 0x228 };
static const int k_slot_player[6] = { 0x00,    5,    3,     1,     2,     4 };
static const int k_cat_x[8]      = { 0x36, 0x5c, 0x82, 0xa8, 0xce, 0xf4, 0x11a, 0x00 };

/* flags.pl8: the player-banner sprite sheet (retail loads it into the shared
 * DAT_004eabec at 160000 bytes; we keep a persistent buffer, reloaded per
 * render, and bind it as the sprite blob — the same pattern as engine_trade's
 * icontrad.pl8 scratch). */
#define FLAGS_SCRATCH_LEN 160000
static uint8_t *g_flags_scratch;
static int rankings_ensure_scratch(void)
{
    if (!g_flags_scratch) {
        g_flags_scratch = malloc(FLAGS_SCRATCH_LEN);
    }
    return g_flags_scratch != NULL;
}

/* Winner-compute scratch (retail globals, this-screen-only) */
static int g_pct[6];        /* DAT_00522c60[player] — 0..100 bar height */
static int g_all_tied;      /* DAT_00522c78 — every active player level */
static int g_tie_first;     /* DAT_00522c94 — >1 player shares the lead */

/* ── FUN_00415e42 — the raw per-category stat for one player ─────────────
 * All saved reads off the strategic record; 0 for an inactive slot.  The
 * "Greatest noble" category (6) is turn-gated: before year 0x4f6 nobody has
 * a title yet (everyone == 2), afterward it is 6 - the noble-rank byte. */
static uint32_t rankings_metric(const engine_rankings_ctx_t *ctx, int player)
{
    const uint8_t *rec = ctx->strategic_block + (size_t)player * STRAT_STRIDE;
    if (rec[0x04] == 0) {
        return 0;
    }
    if (ctx->category == 0) return rec[0x29];              /* counties  */
    if (ctx->category == 1) return rec[0x4c];              /* castles   */
    if (ctx->category == 2) return rec_u32(rec, 0x54);     /* troops    */
    if (ctx->category == 3) return rec_u32(rec, 0x118);    /* crowns    */
    if (ctx->category == 4) return rec[0x0c];              /* happiness */
    if (ctx->category == 5) return rec_u32(rec, 0x10);     /* people    */
    if (ctx->turn < 0x4f6) return 2;                       /* greatest noble */
    return (uint32_t)(6 - rec[0x2b]);
}

/* ── FUN_00415bdc — winner index + the 0..100 bar-height percentages ─────
 * Finds the leading player (ties resolve to the later slot, retail's `<=`),
 * expresses each player's stat as a percent of the leader, clamps [0,100],
 * then sets the two tie flags: g_all_tied when every active player is level
 * (bars flatten to 50%), g_tie_first when someone else shares the lead. */
static int rankings_compute(const engine_rankings_ctx_t *ctx)
{
    int winner = 1;
    int maxv = 0;

    for (int p = 1; p < 6; p++) {
        int v = (int)rankings_metric(ctx, p);
        if (maxv <= v) {
            winner = p;
            maxv = v;
        }
    }
    for (int p = 1; p < 6; p++) {
        int v = (int)rankings_metric(ctx, p);
        g_pct[p] = engine_math_percent_div(v, maxv);   /* FUN_00404dc1 */
    }

    int first = 999;
    g_all_tied = 1;
    for (int p = 1; p < 6; p++) {
        if (g_pct[p] < 0)   g_pct[p] = 0;
        if (g_pct[p] > 100) g_pct[p] = 100;
        if (ctx->strategic_block[(size_t)p * STRAT_STRIDE + 0x04] != 0) {
            if (first == 999) {
                first = g_pct[p];
            } else if (g_pct[p] != first) {
                g_all_tied = 0;
            }
        }
    }

    if (g_all_tied == 0) {
        g_tie_first = 0;
        for (int p = 1; p < 6; p++) {
            if (ctx->strategic_block[(size_t)p * STRAT_STRIDE + 0x04] != 0 &&
                winner != p && g_pct[p] == g_pct[winner]) {
                g_tie_first = 1;
            }
        }
    } else {
        for (int p = 1; p < 6; p++) {
            if (ctx->strategic_block[(size_t)p * STRAT_STRIDE + 0x04] != 0) {
                g_pct[p] = 0x32;
            }
        }
    }
    return winner;
}

/* Convenience: one localized single-line render bound to the ctx. */
static void rankings_loc(const engine_rankings_ctx_t *ctx, int grp, int sub,
                         int x, int y, const uint8_t *font, uint8_t color)
{
    gfx_wrap_localized_line(ctx->loc, grp, sub, x, y,
                            font, ctx->glyph_table, color);
}

/* ── FUN_0041593b body — the standings draw ─────────────────────────────
 * Caller holds the canvas lock; the backdrop is already blitted and the
 * flags blob bound.  FUN_0045240a (dirty mark) is seam-owned (OMITTED, as
 * in engine_merchant / engine_trade — the port repaints the full frame). */
static void rankings_panel(const engine_rankings_ctx_t *ctx)
{
    const uint8_t *mf = ctx->menu_font;     /* fntl2_14, DAT_005af8f0 */
    int winner = rankings_compute(ctx);     /* FUN_00415bdc — fills g_pct + flags */

    gfx_sprite_render_set_blob(g_flags_scratch);   /* the flags.pl8 banner sheet */

    /* One banner + shaded 4px score bar per player, in the .rdata slot order.
     * The bar top rises with the score (pct=100 → y=0x89; pct=0 → y=0x151);
     * the banner sits 0x5c above it.  4 columns, colors 0x10/0x12/0x14/0x12. */
    for (int slot = 1; slot < 6; slot++) {
        int player = k_slot_player[slot];
        const uint8_t *rec = ctx->strategic_block + (size_t)player * STRAT_STRIDE;
        if (rec[0x04] != 0) {
            int x = k_slot_x[slot];
            int top = (100 - g_pct[player]) * 2;
            gfx_sprite_render((int)rec[0x0a] - 1, x, top + 0x2d);   /* banner */
            int ybar = top + 0x89;
            gfx_line_draw(x + 0x17, ybar, x + 0x17, 0x158, 0x10);
            gfx_line_draw(x + 0x18, ybar, x + 0x18, 0x158, 0x12);
            gfx_line_draw(x + 0x19, ybar, x + 0x19, 0x158, 0x14);
            gfx_line_draw(x + 0x1a, ybar, x + 0x1a, 0x158, 0x12);
        }
    }

    /* The category marker (banner sprite 5) over the selected category tab. */
    gfx_sprite_render(5, k_cat_x[ctx->category], 0x173);

    /* Caption "Most <x>," (grp 0x23 sub = category) then, chained on the same
     * line, the leader's realm name — or "undecided." (sub 7) on a tie. */
    gfx_text_set_line_x_acc(0);                                  /* DAT_005cd404 = 0 */
    rankings_loc(ctx, 0x23, ctx->category, 0x148, 0x1be, mf, 0x3f);
    if (g_all_tied == 0 && g_tie_first == 0) {
        gfx_text_render_line(ctx->realm_names + (size_t)winner * 0x2c,
                             gfx_text_line_x_acc() + 0x14a, 0x1be,
                             mf, ctx->glyph_table, 0x3f);
    } else {
        rankings_loc(ctx, 0x23, 7, gfx_text_line_x_acc() + 0x14a, 0x1be, mf, 0x3f);
    }

    /* FUN_0040d1bc(W-0x1c,H-0x1c,1) — the bottom-right "continue" seal. */
    if (ctx->system_sheet) {
        gfx_sprite_seal_draw(ctx->system_sheet,
                             GFX_RENDER_W - 0x1c, GFX_RENDER_H - 0x1c, 1);
    }
}

void engine_rankings_render(const engine_rankings_ctx_t *ctx)
{
    if (!ctx->loc) {
        return;
    }

    io_path_chdir_base();                       /* FUN_004af502 */
    /* DAT_0053f050 = 0 (leave the strategic view) + FUN_004b11ce (transient
     * palette/canvas prep) are DEFERRED — both superseded by the backdrop
     * load + the final grtnoble palette apply, exactly as engine_merchant. */

    /* grtnoble.pl8: the 640×480 scoreboard backdrop, straight into the back
     * buffer (FUN_00408fcb — self-locks). */
    gfx_load_background_image("grtnoble.pl8");

    /* flags.pl8 → the resident banner sheet (FUN_004af8d9). */
    if (!rankings_ensure_scratch()) {
        return;
    }
    io_read_file_at("flags.pl8", g_flags_scratch, FLAGS_SCRATCH_LEN, 0);

    if (gfx_lock_canvas()) {
        rankings_panel(ctx);
        gfx_unlock_canvas();
    }

    /* grtnoble.256 → the active palette last (retail applies FUN_004b0ab5
     * after the draws, so the applied palette lands on the next present —
     * the frame a human/olv3 sees settled). */
    gfx_load_palette("grtnoble.256");
}
