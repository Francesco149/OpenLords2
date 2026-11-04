/*
 * OpenLords2 — tooltip subsystem.  See engine_tooltip.h for the retail
 * address map and behavior notes.
 */

#include "engine_tooltip.h"

#include <string.h>

#include "call_trace.h"
#include "engine_workforce_hud.h"   /* backdrop capture/restore (4b3f0a/4b3ec0) */
#include "gfx_globals.h"            /* retail's render globals the tick stages directly */
#include "gfx_line.h"
#include "gfx_sprite.h"
#include "gfx_wrap.h"

/* k_tooltip_mode @ 0x4d6fb8 (0x48 bytes; r2 px).  1 = strategic-family
 * classifier, 2 = battle classifier, 0 = no tooltips. */
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

/* ── FUN_004777aa — the battle-screen classifier (the battlefield's right HUD) ── */
int engine_tooltip_classify_battle(const engine_tooltip_classify_in_t *in)
{
    CALL_TRACE_ENTER(0x004777aa);
    int x = in->cursor_x, y = in->cursor_y;
    if (y < 0x18 || x < 0x1de) return 0;
    if (y < 0xb8)  return 0x17;
    if (y < 0x19c) return 0x18;
    if (y < 0x1c1) return 0x19;
    if (x < 0x200) return 0x1a;
    if (x < 0x220) return 0x1b;
    if (x < 0x240) return 0x1c;
    if (x >= 0x260) return 0x1e;
    return 0x1d;
}

/* FUN_004772b6 — the mode dispatch (k_tooltip_mode[screen]). */
int engine_tooltip_classify(const engine_tooltip_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x004772b6);
    int mode = engine_tooltip_mode_for_screen(ctx->screen);
    int tip = 0;
    if (mode == 1) {
        tip = ctx->cin ? engine_tooltip_classify_strategic(ctx->cin) : 0;
    } else if (mode == 2) {
        tip = ctx->cin ? engine_tooltip_classify_battle(ctx->cin) : 0;
    }
    return tip;
}

/* FUN_00477249 — clamp the box position. */
void engine_tooltip_clamp(engine_tooltip_state_t *st)
{
    if (st->x < 0) st->x = 0;
    if (st->x >= 0x1b8) st->x = 0x1b8;
    if (st->y < 0) st->y = 0;
    if (st->y >= 0x1b8) st->y = 0x1b8;
}

/* Stage the 192×40 backdrop rectangle the capture / restore walk (retail: DAT_00591524 / DAT_00591528 = the box
 * position, DAT_005c9258 = 0x30 dwords, DAT_005aeb70 = 0x28 rows; the residual stride 0x1c0 = 640 - 192). */
static void tooltip_stage_backdrop(const engine_tooltip_state_t *st)
{
    g_gfx_cursor_x = st->x;
    g_gfx_cursor_y = st->y;
    g_gfx_glyph_w = 0x30;
    g_gfx_glyph_height_short = 0x28;
}

/* FUN_00477131 — the 1-second idle arm. */
void engine_tooltip_arm(engine_tooltip_state_t *st, const engine_tooltip_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x00477131);
    uint32_t now = ctx->clock ? ctx->clock() : ctx->now_ms;   /* timeGetTime() */
    if (ctx->mouse_event != 0) {
        st->idle_stamp_ms = now;           /* activity restarts the window */
        return;
    }
    if ((int)(now - st->idle_stamp_ms) < 1000) {
        return;
    }
    st->idle_stamp_ms = now;
    st->tip = engine_tooltip_classify(ctx);
    int cx = ctx->cin ? ctx->cin->cursor_x : 0;   /* DAT_004e65fc */
    int cy = ctx->cin ? ctx->cin->cursor_y : 0;   /* DAT_004e65f4 */
    st->x = (cx <= 0x140) ? cx + 0x1e : cx - 0xdc;
    st->y = (cy <= 0xf0) ? cy + 0x1e : cy - 0x1e;
    engine_tooltip_clamp(st);
    tooltip_stage_backdrop(st);
    engine_workforce_hud_capture_backdrop(st->backdrop, 0x1c0);   /* FUN_004b3f0a(&DAT_004e6980, 0x1c0) */
    if (ctx->dirty) {
        engine_dirty_rect_mark_box_tiled(ctx->dirty, st->x, st->y, 0xe, 4, 1);   /* FUN_004527a6 */
    }
}

/* FUN_004770b8 — activity clears the tip and restores the backdrop. */
void engine_tooltip_clear(engine_tooltip_state_t *st, const engine_tooltip_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x004770b8);
    if (ctx->mouse_event != 0) {
        st->tip = 0;
        engine_tooltip_clamp(st);
        tooltip_stage_backdrop(st);
        /* Quirk #214 (port order compensation, see the header): when a full repaint already replaced the frame
         * this tick, the saved backdrop is STALE and the box is already gone — restoring would stamp old-screen
         * pixels onto the fresh frame (the "65♥ rectangle" artifact).  Retail never hits this: its input
         * dispatch runs AFTER the tooltip tick, so the flip's repaint lands next tick.  (0 under rhost.) */
        if (ctx->canvas_repainted == 0) {
            engine_workforce_hud_restore_backdrop(st->backdrop, 0x1c0);   /* FUN_004b3ec0(&DAT_004e6980, 0x1c0) */
        }
        if (ctx->dirty) {
            engine_dirty_rect_mark_box_tiled(ctx->dirty, st->x, st->y, 0xe, 4, 1);
        }
    } else if (ctx->canvas_repainted != 0) {
        /* Port only (quirk #214): shown through a repaint (no mouse activity) — the box was erased and the saved
         * backdrop no longer matches the frame; re-capture from the fresh canvas before the re-render, so the
         * eventual clear restores CURRENT pixels. */
        tooltip_stage_backdrop(st);
        engine_workforce_hud_capture_backdrop(st->backdrop, 0x1c0);
    }
}

void engine_tooltip_tick(engine_tooltip_state_t *st,
                         const engine_tooltip_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x00476e95);
    if (ctx->enabled == 0) {
        return;
    }
    if (st->tip != 0) {
        engine_tooltip_clear(st, ctx);
    } else {
        engine_tooltip_arm(st, ctx);
    }
    if (st->tip == 0) {
        return;
    }

    /* ── The box render (every frame while shown). ── */
    if (!ctx->loc || !ctx->menu_font) {
        return;
    }
    g_gfx_no_shadow = 1;                   /* DAT_005aea40 */
    g_gfx_line_y_acc = 0;                  /* DAT_005cd4f8 */
    g_gfx_line_x_acc = 0;                  /* DAT_005cd404 */
    /* Measure pass (width 0xb4). */
    gfx_wrap_localized(ctx->loc, 0xdc, st->tip, st->x + 4, st->y + 4,
                       0xb4, 200, 0, 0,
                       ctx->menu_font, ctx->glyph_table, ctx->glyph_metrics,
                       0x3f);
    /* Box size: 12 − (0xb0 − max_line_x)/16 column groups wide; one text line (y-acc ≤ 0x10) → 0x16 px tall,
     * else 0x28.  Staged as the fill's glyph width / height (DAT_005c9258 / DAT_005aeb70). */
    int w_groups = 0xc - (0xb0 - g_gfx_max_line_x) / 16;
    int16_t h = (g_gfx_line_y_acc <= 0x10) ? 0x16 : 0x28;
    g_gfx_glyph_w = (int16_t)w_groups;
    g_gfx_glyph_height_short = h;

    /* White fill (0x20), then the real text, then the outline. */
    gfx_sprite_fill_block(st->x, st->y, 0x20);          /* FUN_004b414a */
    gfx_wrap_localized(ctx->loc, 0xdc, st->tip, st->x + 4, st->y + 4,
                       0xb0, 200, 0, 0,
                       ctx->menu_font, ctx->glyph_table, ctx->glyph_metrics,
                       0x3f);
    g_gfx_no_shadow = 0;
    g_gfx_glyph_w = (int16_t)w_groups;
    g_gfx_glyph_height_short = h;
    gfx_line_rect(st->x, st->y, (int16_t)w_groups << 4, h, 0x3f); /* FUN_00403cf4 */
    if (ctx->dirty) {
        engine_dirty_rect_mark_box_tiled(ctx->dirty, st->x, st->y, 0xe, 4, 1);   /* FUN_004527a6 */
    }
}
