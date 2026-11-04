/*
 * OpenLords2 — terrain-info ("Here Be Dragons") open orchestration.
 *
 * Port of FUN_0043caf4 @ 0x43caf4 (354 B).  Branch shape is verbatim from
 * the decompile (docs/decompiled/by-address/43caf4.c).  See the header for
 * the design + the retail global ↔ field mapping.
 */

#include <stddef.h>
#include <stdint.h>

#include "engine_terrain_info.h"

#include "call_trace.h"
#include "gfx_panel.h"
#include "gfx_line.h"
#include "gfx_wrap.h"
#include "gfx_text.h"
#include "gfx_sprite_render.h"
#include "localization.h"

/* NULL-safe hook caller. */
static void call_hook(engine_terrain_info_hook_fn fn,
                      const engine_terrain_info_open_in_t *in,
                      engine_terrain_info_open_state_t *st)
{
    if (fn) {
        fn(in, st);
    }
}

/* Army "active" field: *(int*)(army_active_plane + head_link*0x300),
 * little-endian, NULL plane ⇒ 0.  (Retail reads &DAT_0053fa20 directly.) */
static int army_active_field(const uint8_t *plane, int head_link)
{
    if (plane == NULL) {
        return 0;
    }
    const uint8_t *p = plane + (size_t)head_link * 0x300;
    return (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

void engine_terrain_info_open(const engine_terrain_info_open_in_t *in,
                              engine_terrain_info_open_state_t *st,
                              const engine_terrain_info_hooks_t *hooks)
{
    CALL_TRACE_ENTER(0x0043caf4);
    static const engine_terrain_info_hooks_t empty_hooks = {0};
    if (!hooks) {
        hooks = &empty_hooks;
    }

    /* `if (DAT_0057c8fc == 0)` — terrain-info only opens in campaign mode. */
    if (in->game_mode != 0) {
        return;
    }

    /* FUN_00429ba4(1): re-resolve the hovered cell → st->tile_index. */
    call_hook(hooks->resolve_hover, in, st);

    /* Publish the hovered cell's geometry (the globals the panel + other
     * systems read).  Division guarded against a 0 stride/width — retail
     * never sees those (tile_stride is 8, map_width 0x40 once the map is
     * loaded), but the resolver/skeleton can run before that. */
    st->copy_tile    = st->tile_index;                                /* _DAT_0057a0d4 */
    st->cell_index   = in->tile_stride ? st->tile_index / in->tile_stride : 0; /* DAT_0053f064 */
    st->col          = in->map_width ? st->cell_index % in->map_width : 0;     /* _DAT_0053e98c */
    st->row          = in->map_width ? st->cell_index / in->map_width : 0;     /* _DAT_0053e99c */
    st->compute_tile = st->tile_index;                                /* DAT_0056795c */

    /* `if (DAT_0052afa4 == 0)` — the guard.  Nonzero ⇒ snap back to the
     * strategic map, clear the guard, no panel. */
    if (st->pending_flag != 0) {
        st->screen       = 0;           /* DAT_004eac50 = 0  */
        st->pending_flag = 0;           /* DAT_0052afa4 = 0  */
        return;
    }

    /* `if (DAT_0057cb18 == 2)` — alt view-mode: snap to the map + run the
     * mode-2 jump instead of opening the panel. */
    if (in->view_mode == 2) {
        st->screen = 0;                 /* DAT_004eac50 = 0  */
        call_hook(hooks->mode2_jump, in, st);
        return;
    }

    /* FUN_0046d5fe: terrain-attribute compute → st->county_id, st->head_link
     * (and, live, the render globals DAT_005440b0 / DAT_0057cb2c / …). */
    call_hook(hooks->terrain_compute, in, st);

    if (st->county_id != 0) {
        st->county_store = st->county_id;   /* DAT_00553e68 = DAT_0052afbc */
    }
    if (in->scroll_open != 0) {
        call_hook(hooks->dismiss_scroll, in, st);   /* FUN_00476768 */
    }
    st->state_5681cc = 3;                   /* _DAT_005681cc = 3 */

    /* Army-hover branch: the tile carries an army (head_link != 0) that is
     * not already selected (!= cur_army) AND no county is under the cursor.
     * Select that army + re-render the map. */
    if (st->head_link != 0 && st->head_link != st->cur_army && st->county_id == 0) {
        st->army_ptr = army_active_field(in->army_active_plane, st->head_link); /* DAT_0053f0dc */
        if (st->army_ptr != 0) {
            st->cur_army      = st->head_link;      /* DAT_0057c93c = DAT_005681e0 */
            call_hook(hooks->army_select, in, st);  /* FUN_0043278b(DAT_0053f0dc) */
            st->army_selected = 1;
        }
        st->scene_flag = 1;                         /* DAT_004eb260 = 1 */
        call_hook(hooks->map_render, in, st);       /* FUN_004050c0 */
    }

    call_hook(hooks->panel_render, in, st);         /* FUN_0041b032 */
    call_hook(hooks->tail, in, st);                 /* 0x4b37bc */
}

/* ════════════════════════════════════════════════════════════════════════
 * Screen-4 terrain-info PANEL RENDER (FUN_0041befe + FUN_0041c208).
 * ════════════════════════════════════════════════════════════════════════ */

/* g_per_army_768 field accessors (base DAT_0053f9b0, 0x300 stride). */
static int army_u8(const uint8_t *blk, int head, int off)
{
    if (!blk) return 0;
    return blk[(size_t)head * 0x300 + off];
}
static int army_s8(const uint8_t *blk, int head, int off)
{
    if (!blk) return 0;
    return (int8_t)blk[(size_t)head * 0x300 + off];
}
static int army_s32(const uint8_t *blk, int head, int off)
{
    if (!blk) return 0;
    const uint8_t *p = blk + (size_t)head * 0x300 + off;
    return (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/* Port of FUN_0041befe's classification (the chrome-sizing half).  Picks the
 * panel row count DAT_00553d2c + the vertical offset DAT_005651c8 from the
 * terrain flag byte / type / the army record under the cursor.  Branch shape
 * verbatim from docs/decompiled/by-address/41befe.c lines 6-77. */
engine_terrain_info_panel_class_t
engine_terrain_info_classify_panel(const engine_terrain_info_attr_t *a)
{
    engine_terrain_info_panel_class_t c = {0x11, 2};
    const int f = a->flags;

    if ((f & 0x20) == 0) {
        if ((f & 4) == 0) {
            if ((f & 0x10) == 0) {
                if ((f & 0x40) == 0) {
                    if ((f & 0x80) == 0) {
                        c.row = 0x11; c.off = 2;          /* empty terrain   */
                    } else {
                        if (a->terrain_type < 0xd) {
                            c.row = 0x11;
                        } else if (army_u8(a->army_block, a->head_link, 0x1c3) == 0) {
                            if (army_u8(a->army_block, a->head_link, 0x1c2) == 0) {
                                c.row = 0xe;
                            } else {
                                c.row = 0x11;
                            }
                        } else if (a->player_id == a->owner) {
                            c.row = 10;
                        } else {
                            c.row = 0x11;
                        }
                        c.off = 2;
                    }
                } else {                                  /* flags & 0x40    */
                    c.row = army_u8(a->army_block, a->head_link, 0x1ad) == 0 ? 0x11 : 0xf;
                    c.off = 2;
                }
            } else {                                      /* flags & 0x10    */
                c.row = 0x10; c.off = 0;
            }
        } else {                                          /* flags & 4       */
            c.row = 0x11; c.off = 0;
        }
    } else {                                              /* flags & 0x20    */
        if (a->terrain_type == 0x17) {
            c.row = 0x11;
        } else if (a->terrain_type == 0x18) {
            c.row = 0x11;
        } else if (a->player_id == a->owner) {
            if (a->terrain_type == 0) {
                c.row = 0xc;
            } else if (a->terrain_type < 0x19) {
                c.row = 5;
            } else {
                c.row = 0xc;
            }
        } else {
            c.row = 0x11;
        }
        c.off = 2;
    }
    return c;
}

/* Port of FUN_0041c208's classification (the string/sprite-index half).
 * Branch shape verbatim from docs/decompiled/by-address/41c208.c lines
 * 14-154.  The army-stats arm (flags&0x80, type<0xd) reads the per-unit
 * stat fields to bump the body sub-index and records the SFX-cue arg
 * (0x426216 — deferred, audio-off).  The flags&0x20 arm reads the
 * special-feature table (a->terr_table; NULL ⇒ zeroes = deferred). */
engine_terrain_info_text_class_t
engine_terrain_info_classify_text(const engine_terrain_info_attr_t *a)
{
    engine_terrain_info_text_class_t r = {0, 0x16, 0, 0x17, 0, 0};
    const int f = a->flags;
    const int t = a->terrain_type;
    const int h = a->head_link;

    if ((f & 1) == 0) {
        if ((f & 4) == 0) {
            if ((f & 0x10) == 0) {
                if ((f & 8) == 0) {
                    if ((f & 0x20) == 0) {
                        if ((f & 0x40) == 0) {
                            if ((f & 0x80) == 0) {
                                r.idx20 = 0; r.idx1c = 0x16; r.idx8 = 0x17; r.idxc = 0;
                            } else if (t < 0xd) {
                                /* army-stats arm */
                                if (t < 4) {
                                    r.idx20 = 9;  r.idx18 = 1; r.idx1c = 0x35; r.idx8 = 0x1e; r.idxc = 0; r.alert_arg = 10;
                                } else if (t < 7) {
                                    r.idx20 = 10; r.idx18 = 3; r.idx1c = 0x3d; r.idx8 = 0x1f; r.idxc = 0; r.alert_arg = 8;
                                } else if (t < 10) {
                                    r.idx20 = 0x52; r.idx18 = 2; r.idx1c = 0x53; r.idx8 = 0x12; r.idxc = 0; r.alert_arg = 8;
                                } else { /* t < 0xd */
                                    r.idx20 = 0xb; r.idx18 = 0; r.idx1c = 0x45; r.idx8 = 0x20; r.idxc = 0; r.alert_arg = 9;
                                }
                                int base = r.idx18 * 0x18;
                                int diff = army_s32(a->army_block, h, 0x2a0 + base) -
                                           army_s32(a->army_block, h, 0x2a4 + base);
                                if (army_u8(a->army_block, h, 0x296 + base) == 0) {
                                    if (9 < diff) {
                                        if (diff < 0x19)      r.idx1c += 1;
                                        else if (diff < 0x32) r.idx1c += 2;
                                        else                  r.idx1c += 3;
                                    }
                                } else {
                                    r.idx1c += 4;
                                }
                            } else if (t < 0x1a) {
                                int b = army_u8(a->army_block, h, 0x1c3);
                                if (b == 1)      r.idx20 = 0xe;
                                else if (b == 2) r.idx20 = 0xf;
                                else             r.idx20 = 8;
                                r.idx1c = t + 7; r.idx8 = 0x1c; r.idxc = 0;
                            }
                        } else {                          /* flags & 0x40    */
                            r.idx20 = 7; r.idx1c = 0x1b; r.idx8 = 0x1b; r.idxc = 0;
                        }
                    } else {                              /* flags & 0x20    */
                        if (a->terr_table) {
                            r.idx20 = a->terr_table[t * 4 + 0];
                            r.idx1c = a->terr_table[t * 4 + 1];
                            r.idx8  = a->terr_table[t * 4 + 2];
                            r.idxc  = a->terr_table[t * 4 + 3];
                        }
                        if (r.idxc == 0x12) {
                            r.idx1c = a->f25c == 0 ? 0x3a : 0x22;
                        }
                    }
                } else {                                  /* flags & 8       */
                    if (a->derived_bit == 0) {
                        r.idx20 = 5; r.idx1c = 0x1a; r.idx8 = 0x1a;
                    } else {
                        r.idx20 = 4; r.idx1c = 0x19; r.idx8 = 0x19;
                    }
                    r.idxc = 0;
                }
            } else {                                      /* flags & 0x10    */
                if (t == 0x10) { r.idx20 = 0x30; r.idx1c = 0x31; }
                else           { r.idx20 = 0x32; r.idx1c = 0x33; }
                r.idx8 = 0x1b; r.idxc = 0;
            }
        } else {                                          /* flags & 4       */
            r.idx20 = 3; r.idx1c = 0x18; r.idx8 = 0x1d; r.idxc = 0;
        }
    } else {                                              /* flags & 1       */
        r.idx20 = 1; r.idx1c = 0x17; r.idx8 = 0x18; r.idxc = 0;
    }
    return r;
}

/* The text/sprite draw sequence of FUN_0041c208 (after the classifier).
 * The three army-roster sub-renderers (0x41cb3a local_c==0x13,
 * 0x41d299 local_c==0x15, 0x41da2f type 0xd..0x19) and the
 * selection-blob sprite (FUN_0040a3d0, flags&0x40) are DEFERRED — guarded
 * exactly where retail branches, so the common terrain paths render. */
static void render_text(const engine_terrain_info_attr_t *a,
                        const engine_terrain_info_render_ctx_t *ctx,
                        int row, int off)
{
    localization_state_t *loc = (localization_state_t *)ctx->loc;
    const int f = a->flags;
    const int t = a->terrain_type;
    const int h = a->head_link;
    const int owned = (a->player_id == a->owner);

    engine_terrain_info_text_class_t r = engine_terrain_info_classify_text(a);
    /* r.alert_arg → 0x426216(n): a UI SFX cue, deferred (audio-off). */

    /* Title line(s): the terrain name, title font, two-segment when idxc. */
    gfx_text_set_line_x_acc(0);                            /* DAT_005cd404 = 0 */
    gfx_wrap_localized_line(loc, 0x1e, r.idx20, 0x28, row * 0x10 + 0x40,
                            ctx->title_font, ctx->glyph_table, 0x3f);
    if (r.idxc != 0) {
        gfx_wrap_localized_line(loc, 0x1e, r.idxc,
                                gfx_text_line_x_acc() + 0x28, row * 0x10 + 0x40,
                                ctx->title_font, ctx->glyph_table, 0x3f);
    }

    /* County name, centered (loc group 100, sub = map_id*0x14 + head_link).
     * Gated on the panel offset DAT_005651c8 (retail FUN_0041c208 line 160). */
    if (off != 0) {
        gfx_wrap_localized_line_centered(loc, 100,
            ctx->map_id * 0x14 + h, 8, row * 0x10 + 0x18, 0x1c0,
            ctx->title_font, ctx->glyph_table, ctx->glyph_metrics, 0x3f);
    }

    /* Body block: word-wrapped, menu font.  local_c selects the layout;
     * 0x13/0x15 route to the deferred army-roster renderers. */
    if (r.idxc == 0x13) {
        /* 0x41cb3a — army roster A (deferred). */
    } else if (r.idxc == 0x15) {
        /* 0x41d299 — army roster B (deferred). */
    } else if (r.idxc == 0x12) {
        if (owned) {
            gfx_wrap_localized(loc, 0x1e, r.idx1c, 0x48, row * 0x10 + 0xb0,
                               0x150, 100, 0x28, 0x170, ctx->menu_font,
                               ctx->glyph_table, ctx->glyph_metrics, 0x3f);
        } else {
            gfx_wrap_localized(loc, 0x1e, r.idx1c, 0x68, row * 0x10 + 0x60,
                               0x130, 100, 0x28, 0x170, ctx->menu_font,
                               ctx->glyph_table, ctx->glyph_metrics, 0x3f);
        }
    } else {
        /* both owned / not-owned arms use the same geometry */
        gfx_wrap_localized(loc, 0x1e, r.idx1c, 0x68, row * 0x10 + 100,
                           0x130, 0x40, 0x28, 0x170, ctx->menu_font,
                           ctx->glyph_table, ctx->glyph_metrics, 0x3f);
    }

    /* Sub-info block. */
    if ((f & 0x80) == 0) {
        if ((f & 0x40) != 0 && army_u8(a->army_block, h, 0x1ad) != 0) {
            /* selection-blob sprite (FUN_0040a3d0) — deferred (sel_sheet). */
            if (ctx->sel_sheet) {
                gfx_sprite_render_rle_from_blob(ctx->sel_sheet, 0x81, 0x32,
                                                row * 0x10 + 0x9c);
            }
            gfx_wrap_localized(loc, 0x1e, 0x3b, 0x68, row * 0x10 + 0xa0,
                               0x140, 100, 0x28, 0x170, ctx->menu_font,
                               ctx->glyph_table, ctx->glyph_metrics, 0x3f);
        }
    } else if (t < 0xd) {
        int rdy;
        if (t < 4)       rdy = army_s8(a->army_block, h, 0x2af);
        else if (t < 7)  rdy = army_s8(a->army_block, h, 0x2df);
        else if (t < 10) rdy = army_s8(a->army_block, h, 0x2c7);
        else             rdy = army_s8(a->army_block, h, 0x297);
        gfx_wrap_localized_line(loc, 0x1e, rdy < 1 ? 0x4d : 0x4e, 0x68,
                                row * 0x10 + 0x74, ctx->menu_font,
                                ctx->glyph_table, 0x3f);
    } else if (t < 0x1a) {
        /* 0x41da2f — army roster C (deferred). */
    }

    /* Terrain icon sprite from icon_tmp.pl8 (retail loads it into the shared
     * DAT_004eabec scratch then renders sprite[idx8]).  The caller binds the
     * sheet; bind it as the global sprite blob for gfx_sprite_render. */
    if (ctx->icon_sheet) {
        gfx_sprite_render_set_blob(ctx->icon_sheet);
        gfx_sprite_render(r.idx8, 0x28, row * 0x10 + 0x60);
    }
}

void engine_terrain_info_render(const engine_terrain_info_attr_t *a,
                                const engine_terrain_info_render_ctx_t *ctx)
{
    CALL_TRACE_ENTER(0x0041befe);

    engine_terrain_info_panel_class_t pc = engine_terrain_info_classify_panel(a);
    const int row = pc.row;

    /* Parchment box (FUN_00409397 = gfx_panel_draw with the chrome sheet). */
    if (ctx->chrome) {
        gfx_panel_draw(ctx->chrome, 8, (row - pc.off) * 0x10 + 0x20,
                       0x1c, (0x1b - row) + pc.off);
    }
    /* Corner "click to continue" seal (FUN_0040d1bc = gfx_sprite_seal_draw). */
    gfx_sprite_seal_draw(ctx->system_sheet, 0x1ac, 0x1b6, 0);
    /* Inner bevel frame (FUN_00403deb). */
    gfx_line_rect_bevel_dark_br(0x20, row * 0x10 + 0x38, 400, (0x18 - row) * 0x10);

    /* The text + sprite content (FUN_0041c208). */
    render_text(a, ctx, row, pc.off);

    /* if (flags & 0x20) 0x41c996() — sub-feature stamp, deferred. */
}

/* ════════════════════════════════════════════════════════════════════════
 * Retail layout (2026-11-04, docs/findings/render-mapview.md): the screen-4 panel on retail's globals — the compute's
 * outputs (engine_mapview_globals.h g_mv_ti_*), the county records, the save slots, the gfx state — with the
 * unported sub-renderers / the sound cue / the icon file read / the present as environment seams.
 * ════════════════════════════════════════════════════════════════════════ */

#include <string.h>
#include "engine_mapview_globals.h"
#include "save.h"

static engine_terrain_info_env_t g_ti_env;

void engine_terrain_info_bind_env(const engine_terrain_info_env_t *env)
{
    if (env) g_ti_env = *env;
    else memset(&g_ti_env, 0, sizeof g_ti_env);
}

static const uint8_t *ti_rec(void) { return g_per_army_768 + (size_t)g_mv_ti_head * 0x300; }
static int32_t ti_i32(int off) { int32_t v; memcpy(&v, ti_rec() + off, 4); return v; }
static void ti_call(void (*fn)(void)) { if (fn) fn(); }

/* Port of FUN_00409397 — the chrome parchment: the border (mode 0) and its interior (16 px in, 2 tiles smaller). */
void engine_terrain_info_parchment(int x, int y, int cols, int rows)
{
    gfx_panel_draw_border_chrome(g_ti_env.chrome, 0, x, y, cols, rows);
    gfx_panel_fill_interior_chrome(g_ti_env.chrome, x + 16, y + 16, cols - 2, rows - 2);
}

/* Port of FUN_0041c996 — the special-feature stamp (flags 0x20): the owner's three / two resource sprites. */
void engine_terrain_info_stamp(void)
{
    if (g_mv_ti_type == 23 || g_mv_ti_type == 24) return;
    if ((int32_t)s_slot_0057c8cc != g_mv_ti_owner) return;
    int r = g_mv_ti_row;
    if (g_mv_ti_type != 0 && g_mv_ti_type < 25) {
        gfx_line_rect_bevel_dark_tl(40, 16 * (r + 7) + 176, 384, 64);
        gfx_sprite_render(1, 240, 16 * (r + 7) + 184);
        gfx_sprite_render(13, 304, 16 * (r + 7) + 184);
        gfx_sprite_render(22, 368, 16 * (r + 7) + 184);
        gfx_wrap_localized(g_ti_env.loc, 30, 52, 48, 16 * (r + 7) + 194, 0xc0, 100, 48, 0x80, g_ti_env.menu_font,
                           g_ti_env.glyph, g_ti_env.metrics, 63);
    } else {
        gfx_line_rect_bevel_dark_tl(40, 16 * r + 176, 384, 64);
        gfx_sprite_render(17, 304, 16 * r + 184);
        gfx_sprite_render(0, 368, 16 * r + 184);
        gfx_wrap_localized(g_ti_env.loc, 30, 52, 48, 16 * r + 194, 0xc0, 100, 48, 0x80, g_ti_env.menu_font,
                           g_ti_env.glyph, g_ti_env.metrics, 63);
    }
}

/* Port of FUN_0041c208 — the title / county name / body / sub-info / icon of the simple panel. */
void engine_terrain_info_text(void)
{
    int f = g_mv_ti_flags, t = g_mv_ti_type;
    int v1 = 0, v2 = 0, v3 = 0, v6 = 0, v7 = 0;   /* (retail: uninitialized for a flags-0x80 type > 25) */
    if (f & 1)           { v1 = 1; v2 = 23; v7 = 24; v6 = 0; }
    else if (f & 4)      { v1 = 3; v2 = 24; v7 = 29; v6 = 0; }
    else if (f & 0x10)   { if (t == 16) { v1 = 48; v2 = 49; } else { v1 = 50; v2 = 51; } v7 = 27; v6 = 0; }
    else if (f & 8)      { if (g_mv_ti_derived != 0) { v1 = 4; v2 = 25; v7 = 25; } else { v1 = 5; v2 = 26; v7 = 26; } v6 = 0; }
    else if (f & 0x20) {
        const int32_t *row = g_ti_env.special_table + 4 * t;   /* DAT_004d2ec8 */
        v1 = row[0]; v2 = row[1]; v7 = row[2]; v6 = row[3];
        if (v6 == 18) v2 = (s_slot_0053f25c != 0) ? 34 : 58;
    } else if (f & 0x40) { v1 = 7; v2 = 27; v7 = 27; v6 = 0; }
    else if ((f & 0x80) == 0) { v1 = 0; v2 = 22; v7 = 23; v6 = 0; }
    else if (t > 12) {
        if (t <= 25) {
            uint8_t b = ti_rec()[0x1c3];
            v1 = (b == 1) ? 14 : (b == 2) ? 15 : 8;
            v2 = t + 7; v7 = 28; v6 = 0;
        }
    } else {
        int alert;
        if (t > 3) {
            if (t > 6) {
                if (t > 9) { v1 = 11; v3 = 0; v2 = 69; v7 = 32; alert = 9; }
                else       { v1 = 82; v3 = 2; v2 = 83; v7 = 18; alert = 8; }
            } else         { v1 = 10; v3 = 3; v2 = 61; v7 = 31; alert = 8; }
        } else             { v1 = 9;  v3 = 1; v2 = 53; v7 = 30; alert = 10; }
        v6 = 0;
        if (g_ti_env.alert) g_ti_env.alert(alert);   /* 0x426216 */
        int v5 = ti_i32(0x2a0 + 24 * v3) - ti_i32(0x2a4 + 24 * v3);
        if (ti_rec()[0x296 + 24 * v3] != 0) v2 += 4;
        else if (v5 >= 10) { if (v5 >= 25) v2 += (v5 >= 50) ? 3 : 2; else ++v2; }
    }
    int r = g_mv_ti_row;
    g_gfx_line_x_acc = 0;
    gfx_wrap_localized_line(g_ti_env.loc, 30, v1, 40, 16 * r + 64, g_ti_env.title_font, g_ti_env.glyph, 63);
    if (v6 != 0)
        gfx_wrap_localized_line(g_ti_env.loc, 30, v6, g_gfx_line_x_acc + 40, 16 * r + 64, g_ti_env.title_font,
                                g_ti_env.glyph, 63);
    if (g_mv_ti_off != 0)
        gfx_wrap_localized_line_centered(g_ti_env.loc, 100, g_mv_ti_head + 20 * (int)s_slot_0053f034, 8, 16 * r + 24,
                                         448, g_ti_env.title_font, g_ti_env.glyph, g_ti_env.metrics, 63);
    switch (v6) {
    case 19: ti_call(g_ti_env.roster_a); break;   /* 0x41cb3a */
    case 21: ti_call(g_ti_env.roster_b); break;   /* 0x41d299 */
    case 18:
        if ((int32_t)s_slot_0057c8cc == g_mv_ti_owner)
            gfx_wrap_localized(g_ti_env.loc, 30, v2, 72, 16 * r + 176, 0x150, 100, 40, 0x170, g_ti_env.menu_font,
                               g_ti_env.glyph, g_ti_env.metrics, 63);
        else
            gfx_wrap_localized(g_ti_env.loc, 30, v2, 104, 16 * r + 96, 0x130, 100, 40, 0x170, g_ti_env.menu_font,
                               g_ti_env.glyph, g_ti_env.metrics, 63);
        break;
    default:
        gfx_wrap_localized(g_ti_env.loc, 30, v2, 104, 16 * r + 100, 0x130, 64, 40, 0x170, g_ti_env.menu_font,
                           g_ti_env.glyph, g_ti_env.metrics, 63);
        break;
    }
    if ((f & 0x80) == 0) {
        if ((f & 0x40) != 0 && ti_rec()[0x1ad] != 0) {
            gfx_sprite_render_rle_from_blob(g_mv_overlay_blob, 129, 50, 16 * r + 156);
            gfx_wrap_localized(g_ti_env.loc, 30, 59, 104, 16 * r + 160, 0x140, 100, 40, 0x170, g_ti_env.menu_font,
                               g_ti_env.glyph, g_ti_env.metrics, 63);
        }
    } else if (t > 12) {
        if (t <= 25) ti_call(g_ti_env.roster_c);   /* 0x41da2f */
    } else {
        int v4;
        if (t > 3) {
            if (t > 6) v4 = (t > 9) ? ti_rec()[0x297] : ti_rec()[0x2c7];
            else v4 = ti_rec()[0x2df];
        } else v4 = ti_rec()[0x2af];
        gfx_wrap_localized_line(g_ti_env.loc, 30, v4 > 0 ? 78 : 77, 104, 16 * r + 116, g_ti_env.menu_font,
                                g_ti_env.glyph, 63);
    }
    ti_call(g_ti_env.load_icons);   /* io_read_file_at("icon_tmp.pl8", DAT_004eabec, 0x27100, 0) */
    gfx_sprite_render(v7, 40, 16 * r + 96);
}

/* Port of FUN_0041befe — the simple panel: size it, parchment + seal + bevel, the text, the stamp. */
void engine_terrain_info_simple(void)
{
    int f = g_mv_ti_flags, t = g_mv_ti_type;
    int mine = (int32_t)s_slot_0057c8cc == g_mv_ti_owner;
    if (f & 0x20) {
        if (t == 23 || t == 24) { g_mv_ti_row = 17; g_mv_ti_off = 2; }
        else if (mine) { g_mv_ti_row = (t != 0) ? (t < 25 ? 5 : 12) : 12; g_mv_ti_off = 2; }
        else { g_mv_ti_row = 17; g_mv_ti_off = 2; }
    } else if (f & 4)    { g_mv_ti_row = 17; g_mv_ti_off = 0; }
    else if (f & 0x10)   { g_mv_ti_row = 16; g_mv_ti_off = 0; }
    else if (f & 0x40)   { g_mv_ti_row = ti_rec()[0x1ad] != 0 ? 15 : 17; g_mv_ti_off = 2; }
    else if ((f & 0x80) == 0) { g_mv_ti_row = 17; g_mv_ti_off = 2; }
    else if (t > 12) {
        if (ti_rec()[0x1c3] != 0) g_mv_ti_row = mine ? 10 : 17;
        else g_mv_ti_row = ti_rec()[0x1c2] != 0 ? 17 : 14;
        g_mv_ti_off = 2;
    } else { g_mv_ti_row = 17; g_mv_ti_off = 2; }
    engine_terrain_info_parchment(8, 16 * (g_mv_ti_row - g_mv_ti_off) + 32, 28, g_mv_ti_off + 27 - g_mv_ti_row);
    gfx_sprite_seal_draw(g_ti_env.system_sheet, 428, 438, 0);
    gfx_line_rect_bevel_dark_br(32, 16 * g_mv_ti_row + 56, 400, 16 * (24 - g_mv_ti_row));
    engine_terrain_info_text();
    if (g_mv_ti_flags & 0x20)
        engine_terrain_info_stamp();
}

/* Port of FUN_0041b032 — the screen-4 panel: full-screen dirty, the county / army panel (0x41b19d) or the simple
 * one, the present (0x452160(1)), the menu redraw latch. */
void engine_terrain_info_panel(void)
{
    /* 0x4b1de0: empty */
    if (g_ti_env.dirty) engine_dirty_rect_mark_full_screen(g_ti_env.dirty);
    if (g_mv_ti_county != 0 || g_mv_ti_52afb4 != 0)
        ti_call(g_ti_env.county_panel);   /* 0x41b19d */
    else
        engine_terrain_info_simple();
    ti_call(g_ti_env.present);            /* 0x452160(1) */
    g_gfx_menu_redraw_a = 1;
}
