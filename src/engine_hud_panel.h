#ifndef OPENLORDS2_ENGINE_HUD_PANEL_H
#define OPENLORDS2_ENGINE_HUD_PANEL_H

#include <stdint.h>

#include "localization.h"
#include "engine_dirty_rect.h"

/*
 * OpenLords2 — strategic-map right-edge territory-info panel.
 *
 * Port of FUN_0040f7d3 (0x40f7d3, 1774 B) + its eight resource
 * subdrawers, called from the screen-id-0 arm FUN_0040f5fd as
 * `FUN_0040f7d3(1)` (the forced-redraw path, param_1 == 1, which skips
 * all the change-detection / screen-id gates at the function head).
 *
 * The panel sits below the minimap in the right HUD strip (x >= 0x1de)
 * and shows, for the currently-selected territory s_slot_0057c93c:
 *
 *   - OWNED branch (record owner +5 == current realm s_slot_0057c8cc):
 *       title sprites 0x37/0x38, the territory NAME (loc group 100,
 *       sub = map_id*0x14 + territory), population (+0x24) and approval
 *       (+0x0c), the "Pop"/"Wealth" labels (loc 0x3d sub 0/1), ration
 *       (+0xb9, "%"), tax rate (loc 0x15 sub = +0x15d), the realm flag
 *       shield (+0x09), a castle/town indicator (+0x124 gate, +0x08
 *       x-offset), then the resource readouts: FUN_0040fec1 builds two
 *       queues (DAT_0053f690 / DAT_00553fd0) of resource-type ids from
 *       the record's resource flags, and each queued type is drawn by
 *       one of the eight subdrawers (FUN_004100af/41023a/4103c5 for the
 *       first queue; FUN_00410502/410598/41062e/4106c4/4107d1 for the
 *       second).
 *   - NOT-OWNED branch: title sprite 0x3a, the territory name, and (if
 *       the territory has an owner) the owning realm's colour + name.
 *
 * Retail layout (2026-11-04): the panel reads the save slots and records directly and keeps its queues / rows /
 * heights in retail's globals (engine_mapview_globals.h).  The standalone entries below fill the environment.
 * (Was: state-free, every input passed in.)  All render leaves are already
 * ported — the sprite dispatch (gfx_sprite_render_from_blob = 0x40a21a,
 * gfx_sprite_render_opaque_from_blob = 0x40a567), the text label
 * renderers (gfx_text_label_render_dec = 0x402f64, _dec_centered =
 * 0x4030c6, _labeled_signed = 0x402e0c), the localized-line renderers
 * (gfx_wrap_localized_line = 0x402d37, _centered = 0x402c5e), and the
 * centered raw line (gfx_text_render_line_centered = 0x4025d7).
 *
 * Per-record geometry: the territory record block is g_per_army_768
 * (retail base DAT_0053f9b0, 0x300-byte stride), save entry 4 — the
 * same block the minimap reads.  All field offsets in this module are
 * relative to that base (e.g. the owner byte &DAT_0053f9b5 is +5).
 */

typedef struct {
    /* g_per_army_768 (retail DAT_0053f9b0, 0x300 stride, save entry 4). */
    const uint8_t *army_block;
    /* g_player_strategic_352 (retail DAT_0057bf00, 0x160 stride, entry 3) —
     * only the not-owned branch's owner-colour byte (+8) is read. */
    const uint8_t *realm_block;
    /* Realm-name string table (retail DAT_00553d54, 0x2c stride).  Only the
     * not-owned branch reads it; pass NULL to skip that one name line. */
    const uint8_t *realm_names;

    int current_territory;     /* DAT_0057c93c (s_slot_0057c93c) */
    int current_realm;         /* DAT_0057c8cc (s_slot_0057c8cc) */
    int map_id;                /* DAT_0053f034 (s_slot_0053f034) — name index */
} engine_hud_panel_state_t;

/* The panel's environment (not retail globals — retail reads the fonts / sheet / walker from fixed globals):
 * the misc sheet (DAT_005530c8), fntl2_9 (&DAT_005c9a90), font_10 (&DAT_005aeba0), fntl2_14 (&DAT_005af8f0), the
 * render / measure char maps (DAT_004d71f0 / DAT_004d71d0), the localization walker, the dirty state the right-half /
 * box marks expand (NULL: none), and the screen-2 workforce refresh (FUN_0045161e(county) and 0x4120e0; NULL: none). */
typedef struct {
    const uint8_t *chrome, *font_name, *font_res, *font_menu, *glyph;
    const unsigned char *metrics;
    localization_state_t *loc;
    engine_dirty_rect_state_t *dirty;
    void (*workforce_rows)(int county);
    void (*workforce_redraw)(void);
} engine_hud_panel_env_t;
void engine_hud_panel_bind_env(const engine_hud_panel_env_t *env);

/* Retail-layout ports (rhost rows): FUN_0040f7d3(a1) with its gates, FUN_0040fec1(county), the subdrawers
 * FUN_004100af (q1 type 1) / 0041023a (type 0) / 004103c5 (type 2) / 00410502 (q2 type 4) / 00410598 (5) /
 * 0041062e (6) / 004106c4 (7) / 004107d1 (3), FUN_0041a734(a1).  They read the county / realm / map id from the save
 * slots and draw through the bound environment. */
void engine_hud_panel_draw(int a1);
void engine_hud_panel_build_queues(int county);
void engine_hud_panel_draw_q1_type1(void);
void engine_hud_panel_draw_q1_type0(void);
void engine_hud_panel_draw_q1_type2(void);
void engine_hud_panel_draw_q2_type4(void);
void engine_hud_panel_draw_q2_type5(void);
void engine_hud_panel_draw_q2_type6(void);
void engine_hud_panel_draw_q2_type7(void);
void engine_hud_panel_draw_q2_type3(void);
void engine_hud_panel_endturn(int a1);

/* Fonts + glyph maps (one render map shared across fonts, see
 * engine-quirks #32).  font_name = fntl2_9.pl8 (retail DAT_005c9a90),
 * font_res = font_10.pl8 (DAT_005aeba0), font_menu = fntl2_14.pl8
 * (DAT_005af8f0).  chrome = misc_cty sprite blob (DAT_005530c8). */
void engine_hud_panel_render(const engine_hud_panel_state_t *st,
                             const uint8_t *chrome,
                             const uint8_t *font_name,
                             const uint8_t *font_res,
                             const uint8_t *font_menu,
                             const uint8_t *glyph_table,
                             const unsigned char *glyph_metrics,
                             localization_state_t *loc);

/*
 * Port of FUN_0041a734 (0x41a734) — the End-turn button strip, drawn
 * immediately after the territory-info panel in the screen-id-0 arm
 * FUN_0040f5fd (`FUN_0041a734(1)`, forced-redraw path).
 *
 * Retail's outer gate (DAT_0057c8fc == 0 && DAT_0052af94 > 2 [ingame]
 * && DAT_004eac50 != 2 [screen]) decides whether the strip is redrawn at
 * all; like FUN_0040f7d3's forced path, the caller owns that decision
 * (the drop-in invokes this only when those hold).  The body redraws the
 * misc_cty parchment sprite 0x3b at (0x1de, 0x1cc) and, when the current
 * realm's strategic-record field +0 is below 999, centres the localized
 * "End turn" label (loc group 4 sub 0) over it in colour 0x16.
 *
 * engine_hud_panel_render_endturn is the standalone's forced body; the
 * retail-layout engine_hud_panel_endturn adds the gate, FUN_004527a6's
 * 11×2 forced dirty box (through the bound dirty state) and the
 * DAT_004eb260 menu redraw latch (2026-11-04: these were elided).
 *
 * chrome = misc_cty (DAT_005530c8); font = fntl2_9 (DAT_005c9a90);
 * realm_block = g_player_strategic_352 (DAT_0057bf00, 0x160 stride);
 * current_realm = DAT_0057c8cc.
 */
void engine_hud_panel_render_endturn(const uint8_t *chrome,
                                     const uint8_t *font_name,
                                     const uint8_t *glyph_table,
                                     const unsigned char *glyph_metrics,
                                     localization_state_t *loc,
                                     const uint8_t *realm_block,
                                     int current_realm);

#endif /* OPENLORDS2_ENGINE_HUD_PANEL_H */
