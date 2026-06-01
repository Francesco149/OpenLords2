/*
 * OpenLords2 — strategic-map top menu bar (HUD chrome).
 *
 * Ports the state-free front of the in-game menu-bar renderer
 * FUN_00419c78 @ 0x419c78 — the parchment strip + bevel frame + the
 * "File / Options / Help" labels that sit across the top 24 rows of the
 * strategic-map back buffer.  These three pieces depend only on loaded
 * assets (the chrome sprite sheet panels.pl8 → DAT_0057d3d0, the menu
 * font fntl2_14.pl8 → DAT_005af8f0, and the localization blob), NOT on
 * any per-realm game state, so they render identically regardless of the
 * save/new-game data path.
 *
 * Retail call order inside FUN_00419c78 (the parts ported here):
 *   FUN_00409e09(0,    0, 0x19, 1)   parchment tiles, cols 0..24
 *   FUN_00409e09(0x250,0, 2,    1)   parchment tiles, cols 25..26 (right edge)
 *   FUN_00403fdd(0, 0, 0x280, 0x18)  the 1-px bevel frame (gfx_line, ported)
 *   FUN_0040c5b0(&DAT_004dc428, 3)   the three menu labels
 *
 * DEFERRED — the data-dependent tail of FUN_00419c78 (cited as bare hex
 * per the ledger gotcha, not yet ported): the per-realm flag-shield loop
 * (0x40a21a over DAT_0057bf04/0a), the year/season indicators
 * (0x41a900 + 0x402d37 reading DAT_00553edc / DAT_0057c934) and the gold
 * "N Crowns." readout (0x41ab67 reading DAT_0057c018[realm]).  Those need
 * the live new-game/load path to populate the realm structs
 * (DAT_0057bf00, 0x160 stride) — absent in the bring-up shim — so they
 * are left for when that state lands.  See docs/findings/map-render.md.
 *
 * Two leaf drawers ARE fully ported here:
 *   FUN_00409e09 @ 0x409e09 (368 B) → engine_hud_menubar_tile_strip
 *   FUN_0040c5b0 @ 0x40c5b0 (373 B) → engine_hud_menubar_labels
 */

#ifndef OPENLORDS2_ENGINE_HUD_MENUBAR_H
#define OPENLORDS2_ENGINE_HUD_MENUBAR_H

#include <stdint.h>

#include "localization.h"

/* Port of FUN_00409e09.  Tile a `cols`×`rows` grid of 24×24 opaque
 * sprites from the chrome sheet starting at (x, y); each tile uses
 * sprite index 0xc4 + (col & 7) so a long strip cycles through the eight
 * parchment-texture tiles 0xc4..0xcb.  The blit is the already-ported
 * fixed 24×24 raw copy (gfx_sprite_blit_24x24_raw) staged off the sheet
 * directory (sheet + 8 + idx*0x10).  Aborts on an out-of-range source
 * offset, exactly like retail (FUN_004afab9 log + FUN_00408fb0(1) latch,
 * elided — a valid sheet never trips it). */
void engine_hud_menubar_tile_strip(const uint8_t *chrome_sheet,
                                    int x, int y, int cols, int rows);

/* Port of FUN_0040c5b0.  Draw `count` menu labels from `table`, a packed
 * array of int16_t[8] records (DAT_004dc428, stride 0x10).  Per record
 * the fields consulted are [0] x (rewritten with the running cursor),
 * [1] x2 (rewritten), [2] y, [3] localization entry id.  Labels lay out
 * left-to-right: the renderer's running x accumulator starts at the first
 * record's x, each label draws at the current accumulator, and 0x20 px of
 * spacing is added after each (on top of the drawn-text advance).
 *
 * `hovered` is the 1-based index of the highlighted item (retail
 * DAT_00522cb4; 0 = none).  The hovered label gets a solid highlight box
 * (gfx_sprite_fill_block) sized from the measured text width and renders
 * in color 0x18; all others render in `color`.
 *
 * `loc` is the localization state (over the l2.eng blob); `font` is the
 * menu font (DAT_005af8f0); `glyph_table` / `glyph_metrics` are the
 * char→glyph render / measure maps (DAT_004d71f0 / DAT_004d71d0). */
void engine_hud_menubar_labels(int16_t *table, int count, int hovered,
                               const uint8_t *font,
                               const uint8_t *glyph_table,
                               const unsigned char *glyph_metrics,
                               localization_state_t *loc, uint8_t color);

/* Compose the state-free menu-bar chrome: the two parchment strips, the
 * bevel frame, and the File/Options/Help labels — the no-game-state front
 * of FUN_00419c78.  `menu_table` is a mutable 3×int16_t[8] buffer seeded
 * from DAT_004dc428 (the labels fn rewrites the x fields).  Draws into the
 * currently-bound gfx canvas. */
void engine_hud_menubar_render_chrome(const uint8_t *chrome_sheet,
                                      const uint8_t *menu_font,
                                      const uint8_t *glyph_table,
                                      const unsigned char *glyph_metrics,
                                      localization_state_t *loc,
                                      int16_t *menu_table);

/* The DAT_004dc428 menu-item layout, extracted from retail: three items
 * at x=10, y=6 with localization ids 1/2/3 (File/Options/Help).  Returns a
 * pointer to a freshly-initialized mutable copy in a caller-supplied
 * 24-int16_t (3×8) buffer. */
int16_t *engine_hud_menubar_default_table(int16_t buf[24]);

#endif /* OPENLORDS2_ENGINE_HUD_MENUBAR_H */
