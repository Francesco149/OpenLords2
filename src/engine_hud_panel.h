#ifndef OPENLORDS2_ENGINE_HUD_PANEL_H
#define OPENLORDS2_ENGINE_HUD_PANEL_H

#include <stdint.h>

#include "localization.h"

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
 * State-free: every input is passed in.  All render leaves are already
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

#endif /* OPENLORDS2_ENGINE_HUD_PANEL_H */
