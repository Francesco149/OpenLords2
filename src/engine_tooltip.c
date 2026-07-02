/*
 * OpenLords2 — tooltip subsystem.  See engine_tooltip.h for the retail
 * address map and behavior notes.
 */

#include "engine_tooltip.h"

#include <string.h>

#include "call_trace.h"
#include "engine_workforce_hud.h"   /* backdrop capture/restore (4b3f0a/4b3ec0) */
#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_line.h"
#include "gfx_paint.h"
#include "gfx_sprite.h"
#include "gfx_text.h"
#include "gfx_wrap.h"

/* k_tooltip_mode @ 0x4d6fb8 (0x48 bytes; r2 px).  1 = strategic-family
 * classifier, 2 = battle classifier (deferred), 0 = no tooltips. */
static const uint8_t k_tooltip_mode[0x48] = {
    1, 1, 1, 1, 1, 1, 1, 1,  0, 1, 0, 0, 0, 0, 1, 1,   /* 0x00-0x0f */
    1, 1, 1, 1, 1, 1, 1, 0,  1, 1, 0, 0, 0, 1, 1, 0,   /* 0x10-0x1f */
    0, 1, 0, 0, 0, 1, 0, 0,  0, 2, 0, 0, 0, 0, 0, 0,   /* 0x20-0x2f */
    0, 1, 1, 1, 1, 1, 1, 1,  1, 1, 0, 0, 0, 0, 0, 0,   /* 0x30-0x3f */
    0, 0, 1, 1, 0, 0, 0, 0,                             /* 0x40-0x47 */
};

int engine_tooltip_mode_for_screen(int screen)
{
    if (screen < 0 || screen >= (int)sizeof k_tooltip_mode) {
        return 0;
    }
    return k_tooltip_mode[screen];
}

/* ── FUN_00477320 — the strategic-family classifier ───────────────────── */
int engine_tooltip_classify_strategic(const engine_tooltip_classify_in_t *in)
{
    CALL_TRACE_ENTER(0x00477320);
    int x = in->cursor_x, y = in->cursor_y;
    int owned = (in->county_owner == in->current_realm);

    if (y < 0x18 || x < 0x1de) {
        return 0;                          /* top bar / map area */
    }
    if (y < 0x9b) {
        /* Minimap band + the side icon column right of it. */
        if (x < 0x261) {
            return 1;                      /* "Kingdom view." */
        }
        if (in->panel_mode < 1) {
            if (y < 0x3e) return 2;        /* labour */
            if (y < 0x5e) return 3;        /* ration status */
            if (y < 0x7d) return 4;        /* overall happiness */
            return 5;                      /* overview map */
        }
        if (y < 0x7d) {
            if (in->panel_mode == 1) return 2;
            if (in->panel_mode == 2) return 3;
            return 4;
        }
        return 0x1f;                       /* census-map return */
    }
    if (y < 0xfa) {
        /* The 2×2 action grid (owner-gated; the x gutter maps to the
         * county-health tip 0x22). */
        if (!owned) return 0;
        if (y < 0xd2) {
            if (x < 0x223) return 6;       /* view population report */
            if (x < 0x23b) return 0x22;    /* the health of the county */
            return 7;                      /* view happiness report */
        }
        if (x < 0x223) return 8;           /* set tax rate */
        if (x < 0x23b) return 0x22;
        return 9;                          /* set rations */
    }
    if (y < 300) {
        return owned ? 10 : 0;             /* adjust labor allocation */
    }
    if (y < 0x1ae) {
        /* The two dynamic row lists (labor / production). */
        if (!owned) return 0;
        if (x < 0x230) {
            if (in->labor_row_h == 0) return 0;
            int row = (y - 0x130) / in->labor_row_h;
            if (row >= in->labor_count || !in->labor_kinds) return 0;
            if (in->labor_kinds[row] == 1) return 0xf;   /* cattle */
            if (in->labor_kinds[row] == 0) return 0x10;  /* wheat */
            if (in->labor_kinds[row] == 2) return 0x11;  /* reclaim */
            return 0;
        }
        if (in->prod_row_h == 0) return 0;
        int row = (y - 0x130) / in->prod_row_h;
        if (row >= in->prod_count || !in->prod_kinds) return 0;
        switch (in->prod_kinds[row]) {
        case 6: return 0x12;               /* wood */
        case 5: return 0x13;               /* stone */
        case 4: return 0x14;               /* iron */
        case 7: return 0x15;               /* weapons */
        case 3: return 0x16;               /* castle */
        default: return 0;
        }
    }
    if (y < 0x1cc) {
        /* The bottom icon row. */
        if (x < 0x200) return 0xb;         /* create an army */
        if (x < 0x221) return 0xc;         /* go to treasury */
        if (x < 0x241) return 0xd;         /* send supplies */
        if (x < 0x260) return 0x20;        /* build/update castle */
        return 0x21;                       /* diplomatic initiatives */
    }
    return 0xe;                            /* end your turn */
}

/* FUN_004772b6 — mode dispatch. */
static int tooltip_classify(const engine_tooltip_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x004772b6);
    int mode = engine_tooltip_mode_for_screen(ctx->screen);
    if (mode == 1 && ctx->cin) {
        return engine_tooltip_classify_strategic(ctx->cin);
    }
    /* mode 2 (FUN_004777aa, the battle screens) — deferred. */
    return 0;
}

/* FUN_00477249 — clamp the box position. */
static void tooltip_clamp(engine_tooltip_state_t *st)
{
    if (st->x < 0) st->x = 0;
    if (st->x > 0x1b7) st->x = 0x1b8;
    if (st->y < 0) st->y = 0;
    if (st->y > 0x1b7) st->y = 0x1b8;
}

/* Stage the glyph dest + dims gfx_sprite_fill_block reads (retail
 * DAT_00591524/8 + DAT_005c9258 + DAT_005aeb70).  The port's backdrop
 * capture/restore take explicit params, so only the fill needs this. */
static void tooltip_stage_rect(int x, int y, int w_groups, int h)
{
    gfx_glyph_set_dest(x, y);
    gfx_glyph_set_glyph(gfx_glyph_glyph_offset(), w_groups,
                        gfx_glyph_glyph_h());
    gfx_dispatch_set_glyph_height_short(h);
}

void engine_tooltip_tick(engine_tooltip_state_t *st,
                         const engine_tooltip_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x00476e95);
    if (ctx->enabled == 0) {
        return;
    }

    if (st->tip == 0) {
        /* ── FUN_00477131 — the 1-second idle arm. ── */
        uint32_t now = ctx->now_ms;
        if (ctx->mouse_event == 0 &&
            (int)(now - st->idle_stamp_ms) > 999) {
            st->idle_stamp_ms = now;
            st->tip = tooltip_classify(ctx);
            int cx = ctx->cin ? ctx->cin->cursor_x : 0;
            int cy = ctx->cin ? ctx->cin->cursor_y : 0;
            st->x = (cx < 0x141) ? cx + 0x1e : cx - 0xdc;
            st->y = (cy < 0xf1) ? cy + 0x1e : cy - 0x1e;
            tooltip_clamp(st);
            /* Save the 0x30-dword × 0x28-row backdrop (retail stages
             * 0x30/0x28 + residual stride 0x1c0; the port's copy takes
             * the full canvas stride). */
            engine_workforce_hud_capture_backdrop(st->backdrop, st->x, st->y,
                                                  0x30, 0x28,
                                                  gfx_paint_canvas_width());
            /* FUN_004527a6(x, y, 0xe, 4, 1) dirty mark — the port
             * repaints full frames; bookkeeping only, skipped. */
        } else if (ctx->mouse_event != 0) {
            st->idle_stamp_ms = now;       /* activity resets the window */
        }
    } else {
        /* ── FUN_004770b8 — activity clears + restores. ── */
        if (ctx->mouse_event != 0) {
            st->tip = 0;
            tooltip_clamp(st);
            /* Quirk #214 (port order compensation, see header): when a
             * full repaint already replaced the frame this tick, the
             * saved backdrop is STALE and the box is already gone —
             * restoring would stamp old-screen pixels onto the fresh
             * frame (the "65♥ rectangle" artifact).  Retail never hits
             * this: its input dispatch runs AFTER the tooltip tick, so
             * the flip's repaint lands next tick. */
            if (ctx->canvas_repainted == 0) {
                engine_workforce_hud_restore_backdrop(st->backdrop,
                                                      st->x, st->y,
                                                      0x30, 0x28,
                                                      gfx_paint_canvas_width());
            }
        } else if (ctx->canvas_repainted != 0) {
            /* Shown through a repaint (no mouse activity): the box was
             * erased and the saved backdrop no longer matches the frame
             * — re-capture from the fresh canvas before the re-render
             * below, so the eventual clear restores CURRENT pixels. */
            engine_workforce_hud_capture_backdrop(st->backdrop, st->x, st->y,
                                                  0x30, 0x28,
                                                  gfx_paint_canvas_width());
        }
    }

    if (st->tip == 0) {
        return;
    }

    /* ── The box render (every frame while shown). ── */
    if (!ctx->loc || !ctx->menu_font) {
        return;
    }
    gfx_text_set_no_shadow(1);             /* DAT_005aea40 = 1 */
    gfx_text_set_line_y_acc(0);            /* DAT_005cd4f8 = 0 */
    gfx_text_set_line_x_acc(0);            /* DAT_005cd404 = 0 */
    /* Measure pass (width 0xb4). */
    gfx_wrap_localized(ctx->loc, 0xdc, st->tip, st->x + 4, st->y + 4,
                       0xb4, 200, 0, 0,
                       ctx->menu_font, ctx->glyph_table, ctx->glyph_metrics,
                       0x3f);
    /* Box size: 12 − (0xb0 − max_line_x)/16 column groups wide; one
     * text line (y-acc ≤ 0x10) → 0x16 px tall, else 0x28. */
    int w_groups = 0xc - (0xb0 - gfx_wrap_max_line_x()) / 16;
    int h = (gfx_text_line_y_acc() < 0x11) ? 0x16 : 0x28;

    /* White fill (0x20), then the real text, then the outline. */
    tooltip_stage_rect(st->x, st->y, w_groups, h);
    gfx_sprite_fill_block(st->x, st->y, 0x20);          /* FUN_004b414a */
    gfx_wrap_localized(ctx->loc, 0xdc, st->tip, st->x + 4, st->y + 4,
                       0xb0, 200, 0, 0,
                       ctx->menu_font, ctx->glyph_table, ctx->glyph_metrics,
                       0x3f);
    gfx_text_set_no_shadow(0);             /* DAT_005aea40 = 0 */
    gfx_line_rect(st->x, st->y, w_groups << 4, h, 0x3f); /* FUN_00403cf4 */
    /* FUN_004527a6 dirty mark — bookkeeping only, skipped. */
}
