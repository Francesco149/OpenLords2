/* engine_merchant.c — merchant county-interior screen render (screen 8).
 * Port of retail FUN_00415fb7 @0x415fb7 (212 B).  See engine_merchant.h. */

#include "engine_merchant.h"

#include <stdint.h>

#include "gfx.h"                 /* gfx_load_palette / _background_image,
                                 * gfx_lock_canvas, gfx_system_sheet_current,
                                 * GFX_RENDER_W/H */
#include "gfx_sprite_render.h"   /* gfx_sprite_seal_draw (FUN_0040d1bc) */
#include "io_path.h"             /* io_path_chdir_base (FUN_004af502) */

/* Retail FUN_00415fb7 body, verbatim order:
 *
 *   if (param_1 == 1) FUN_004b11ce();                    // canvas full-redraw prep
 *   FUN_004af502();                                      // io cwd-revert
 *   DAT_0053f050 = 0;                                    // leave the strategic view
 *   FUN_004af8d9("merchant.256", &DAT_004ea8a0, 0x300, 0);  // read the 256-col palette
 *   FUN_00408fcb("merchant.pl8", 0x1e0);                 // 640×480 backdrop → back buffer
 *   FUN_004af8d9("mercgrid.pl8", &DAT_00542ce0, 0x12d8, 0); // click-region grid
 *   FUN_0040d1bc(DAT_004eb274-0x1c, DAT_004eac4c-0x1c, 1);  // bottom-right "continue" seal
 *   DAT_00569500 = 0; DAT_0055cd68 = 0; DAT_0056d8ac = 0;   // merchant trade state
 *   DAT_0053ef54 = 500;                                  //   "
 *   FUN_0045240a();                                      // territory full-screen dirty-mark
 *   FUN_00452160(1);                                     // rate-gated present
 *   FUN_004b0ab5(&DAT_004ea8a0);                          // ×4-expand + apply palette
 *   DAT_004eb260 = 1;                                    // scene flag (no ported consumer)
 *
 * Port mapping:
 *   FUN_004af502     → io_path_chdir_base
 *   FUN_00408fcb     → gfx_load_background_image ("merchant.pl8" → back buffer)
 *   FUN_004af8d9+4b0ab5 (palette) → gfx_load_palette ("merchant.256"; folds read +
 *                       ×4 VGA expand + apply into one call)
 *   FUN_0040d1bc     → gfx_sprite_seal_draw (system2.pl8 seal, mode 1 → sprite 0x10)
 *   FUN_0045240a/00452160/004b11ce → the port's tick lock/dirty/present tail
 *
 * Palette ORDERING: retail presents (452160) BEFORE applying the palette
 * (4b0ab5), so the applied palette lands on the NEXT present.  gfx_load_palette
 * applies immediately, so calling it here makes the very next present (and the
 * olv3 capture) map the backdrop indices with the merchant palette — the frame
 * a human/retail sees once settled.
 *
 * DEFERRED (no ported consumer; none affect the single-frame visual):
 *   - DAT_0053f050 = 0 (the strategic-data-ready gate): the port's render
 *     dispatch already selects the screen-8 arm, so the strat map never paints
 *     under the merchant; clearing g_data_ready would instead disable the map
 *     after the merchant closes.  Left set.
 *   - the merchant TRADE state (DAT_00569500 / 0055cd68 / 0056d8ac / 0053ef54)
 *     and the mercgrid.pl8 load — trading logic, ported when the merchant grid
 *     interaction lands (the labor region grid it carries is already baked).
 *   - DAT_004eb260 = 1 — scene flag with no ported consumer (as county_panels).
 */
void engine_merchant_render(int redraw_mode)
{
    (void)redraw_mode;

    io_path_chdir_base();                                /* FUN_004af502 */

    /* merchant.pl8: 24-byte PL8 header + 640×480 raw indexed backdrop,
     * straight into the back buffer (FUN_00408fcb). */
    gfx_load_background_image("merchant.pl8");

    /* The "continue" seal at the bottom-right corner (FUN_0040d1bc(W-0x1c,
     * H-0x1c, 1) — mode 1 → the 24×24 sprite 0x10 from the system sheet). */
    const uint8_t *system_sheet = gfx_system_sheet_current();
    if (system_sheet != NULL && gfx_lock_canvas()) {
        gfx_sprite_seal_draw(system_sheet,
                             GFX_RENDER_W - 0x1c, GFX_RENDER_H - 0x1c, 1);
        gfx_unlock_canvas();
    }

    /* merchant.256 → the active palette (read + ×4 VGA expand + apply). */
    gfx_load_palette("merchant.256");
}
