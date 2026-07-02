/* engine_merchant.h — merchant county-interior screen (g_sim_current_screen
 * == 8), retail FUN_00415fb7 @0x415fb7.
 *
 * This is the DEFAULT county-interior view: army_select's castle-click
 * branch (county != 0, kind == 3, own garrison — FUN_0043ce1a) hard-sets
 * screen 8 and requests a demand-redraw, and the per-frame render dispatch
 * (retail FUN_0040f1a0 case '\b') paints this screen.  It is the entry to
 * the whole county-interior screen family (merchant 8 ↔ workforce grid 2 ↔
 * armory 0x0a/0x17 …), switched via the widget router FUN_004ba9c8.
 *
 * The render is a full-screen backdrop: merchant.256 palette + the
 * merchant.pl8 640×480 indexed scene into the back buffer + the bottom-
 * right "continue" seal.  mercgrid.pl8 (the market-rug click grid) is loaded
 * for hit-testing: its +0x18 payload is an 80×60 (stride 0x50) byte map of
 * GOOD ids (1..14) — distinct from the workforce CATEGORY grid the labor
 * screen bakes (a common address, different data per screen).  Clicking a
 * good zone opens the trade grid (screen 0x0c, engine_trade.c). */

#ifndef OPENLORDS2_ENGINE_MERCHANT_H
#define OPENLORDS2_ENGINE_MERCHANT_H

/* Paint the merchant screen into the back buffer.  `redraw_mode` is
 * retail's param_1 (DAT_0057d340 / the demand-redraw level); only the
 * full-redraw path is modelled (the back-buffer reload repaints the whole
 * frame regardless), so the value is accepted for dispatch-signature parity
 * and otherwise unused. */
void engine_merchant_render(int redraw_mode);

/* The good id (1..14) under the cursor on the market rug, or 0 for no zone
 * (or the grid not yet loaded).  Port of FUN_004357a6's grid lookup —
 * indexes mercgrid.pl8's 80×60 payload at (cursor_x>>3, cursor_y>>3).
 * cursor_x/y are the game-space cursor (DAT_004e65fc / DAT_004e65f4). */
int engine_merchant_zone_at(int cursor_x, int cursor_y);

#endif /* OPENLORDS2_ENGINE_MERCHANT_H */
