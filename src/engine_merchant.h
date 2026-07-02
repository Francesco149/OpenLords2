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
 * right "continue" seal.  mercgrid.pl8 (the click-region grid, whose data
 * at +0x18 is the labor region grid) is loaded by retail for hit-testing;
 * the baked engine_labor_region_grid.h already carries that payload, so the
 * render path does not need it. */

#ifndef OPENLORDS2_ENGINE_MERCHANT_H
#define OPENLORDS2_ENGINE_MERCHANT_H

/* Paint the merchant screen into the back buffer.  `redraw_mode` is
 * retail's param_1 (DAT_0057d340 / the demand-redraw level); only the
 * full-redraw path is modelled (the back-buffer reload repaints the whole
 * frame regardless), so the value is accepted for dispatch-signature parity
 * and otherwise unused. */
void engine_merchant_render(int redraw_mode);

#endif /* OPENLORDS2_ENGINE_MERCHANT_H */
