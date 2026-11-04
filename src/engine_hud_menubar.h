/*
 * OpenLords2 — strategic-map top menu bar (HUD chrome + realm status).
 *
 * Ports the in-game menu-bar renderer FUN_00419c78 @ 0x419c78 — the
 * parchment strip + bevel frame + the "File / Options / Help" labels, plus
 * the data-dependent realm-status readouts (flag shields / year / season /
 * gold) that sit across the top 24 rows of the strategic-map back buffer.
 *
 * The chrome front depends only on loaded assets (the chrome sprite sheet
 * panels.pl8 → DAT_0057d3d0, the menu font fntl2_14.pl8 → DAT_005af8f0, and
 * the localization blob), NOT on game state, so it renders identically on
 * any data path.  The status tail reads the per-realm strategic block +
 * the season/year/realm slot globals — populated by the save-load path.
 *
 * Retail call order inside FUN_00419c78:
 *   FUN_00409e09(0,    0, 0x19, 1)   parchment tiles, cols 0..24   [chrome]
 *   FUN_00409e09(0x250,0, 2,    1)   parchment tiles, right edge   [chrome]
 *   FUN_00403fdd(0, 0, 0x280, 0x18)  the 1-px bevel frame          [chrome]
 *   per-realm FUN_0040a21a flag-shield loop (DAT_0057bf04/0a)      [status]
 *   FUN_0040c5b0(&DAT_004dc428, 3)   the three menu labels         [chrome]
 *   FUN_0041a900(DAT_00553edc,…,3) + FUN_00402d37(0x1d,season,…)   [status]
 *   FUN_0041ab67(DAT_0057c018[realm],…)  the "N Crowns." readout   [status]
 *
 * Leaf drawers fully ported here:
 *   FUN_00409e09 @ 0x409e09 (368 B) → engine_hud_menubar_tile_strip
 *   FUN_0040c5b0 @ 0x40c5b0 (373 B) → engine_hud_menubar_labels
 * The status tail composes already-ported leaves (gfx_dispatch_sprite for
 * FUN_0040a21a's flag draw, gfx_text_label_render_dec for the FUN_0041a900
 * mode-3 / FUN_0041ab67 numbers, gfx_wrap_localized_line for the
 * FUN_00402d37 season + " Crowns." strings).  Validated pixel-perfect
 * (0 px over the status region) vs retail's immediate-load frame — see
 * docs/findings/map-render.md.
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
 * offset, exactly like retail (0x4afab9 log + FUN_00408fb0(1) latch,
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

/* ──────────────────────────────────────────────────────────────────
 * Data-dependent status tail of FUN_00419c78
 * ────────────────────────────────────────────────────────────────── */

/* The per-realm game state the status tail reads, mirroring the retail
 * globals FUN_00419c78 consults.  `realm_block` is the 6×0x160 per-player
 * strategic block (retail DAT_0057bf00 / g_player_strategic_352); the
 * status tail reads four fields per record (filter +0x00, active +0x04,
 * flag-id +0x0a, gold +0x118).  The scalars are the matching retail
 * globals: current realm (DAT_0057c8cc), the HUD draw gate (DAT_0057c8fc,
 * 0 ⇒ draw flags + year + season), the year (DAT_00553edc) and the season
 * index (DAT_0057c934, a loc-group-0x1d sub-string index). */
typedef struct {
    const uint8_t *realm_block;   /* g_player_strategic_352 (6 × 0x160) */
    int            current_realm; /* DAT_0057c8cc */
    int            hud_gate;      /* DAT_0057c8fc (0 ⇒ draw gated pieces) */
    int            year;          /* DAT_00553edc */
    int            season;        /* DAT_0057c934 */
} engine_hud_menubar_status_t;

/* Port of the data-dependent tail of FUN_00419c78 — the realm-status
 * readouts that sit right of the File/Options/Help labels across the top
 * menu bar: the per-realm flag shields, the "1268" year, the "Winter"
 * season name, and the "1000 Crowns." gold readout.  Renders into the
 * currently-bound gfx canvas (call after engine_hud_menubar_render_chrome).
 *
 * `flag_sheet` is the misc_cty.pl8 sprite sheet (retail DAT_005530c8);
 * `font` is the menu font (DAT_005af8f0); `glyph_table` the char→glyph
 * render map (DAT_004d71f0); `loc` the localization state over l2.eng.
 *
 * Composes the already-ported leaves: gfx_dispatch_sprite (the flag draw,
 * FUN_0040a21a's success arm), gfx_text_label_render_dec (the year/gold
 * numbers, FUN_0041a900 mode 3 / FUN_0041ab67) and gfx_wrap_localized_line
 * (the season + " Crowns." strings, FUN_00402d37). */
void engine_hud_menubar_render_status(const engine_hud_menubar_status_t *st,
                                      const uint8_t *flag_sheet,
                                      const uint8_t *font,
                                      const uint8_t *glyph_table,
                                      localization_state_t *loc);

#endif /* OPENLORDS2_ENGINE_HUD_MENUBAR_H */
