/*
 * OpenLords2 — generic button-strip renderer (implementation).
 *
 * See engine_button.h for the per-field record map and the relationship
 * to the per-frame UI refresh dispatcher (FUN_004ba26e).  This is a
 * faithful translation of FUN_0040cfd2 @ 0x40cfd2.
 *
 * Per button the retail body:
 *   1. sets the running cursor to (record.x + origin_x, record.y +
 *      origin_y) — DAT_00591524 / DAT_00591528;
 *   2. picks the displayed sprite index off the pressed/hover state;
 *   3. selects one of two sheets by `kind` (< 0x18 → misc, else system);
 *   4. stages the 16-byte descriptor at sheet + idx*0x10 + 8 into the
 *      shared blitter-state globals;
 *   5. dispatches a fixed-size opaque raw blit by `kind`;
 *   6. marks a 3×3-tile dirty box at the button origin.
 *
 * The descriptor read pokes six globals in retail:
 *   desc+0x0 short → glyph_w        (DAT_005c9258)
 *   desc+0x2 short → height_short   (DAT_005aeb70)
 *   desc+0x4 dword → glyph_offset   (DAT_0058fe04)
 *   desc+0xc byte  → flag_a         (DAT_00591568)
 *   desc+0xd byte  → glyph_h        (DAT_005bb478)
 *   desc+0xe byte  → flag_b         (DAT_0057d3c0)
 * The fixed-size raw leaves below read only glyph_offset + the cursor;
 * glyph_w / height_short / glyph_h are staged for fidelity but the blit
 * dimensions are baked in, and flag_a / flag_b have no consumer (same
 * status the glyph dispatcher documents) so they are left unstaged.
 */

#include "engine_button.h"

#include <stddef.h>

#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_sprite.h"

/* PL8 sprite directory: 8-byte header, then 16-byte descriptors. */
#define DESC_STRIDE 0x10
#define DESC_HEADER 8

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void engine_button_render_list(int origin_x, int origin_y,
                               const engine_button_rec_t *recs, int count,
                               const uint8_t *misc_sheet,
                               const uint8_t *system_sheet,
                               engine_dirty_rect_state_t *dirty)
{
    for (int i = 0; i < count; i++) {
        const engine_button_rec_t *r = &recs[i];

        int cursor_x = r->x + origin_x;   /* DAT_00591524 */
        int cursor_y = r->y + origin_y;   /* DAT_00591528 */

        /* Displayed frame: edge types 4/5 toggle on the `pressed` latch;
         * every other type toggles on the `state` byte.  Either way the
         * "active" frame is the next sprite index. */
        int idx;
        if (r->type == 4 || r->type == 5) {
            idx = r->sprite + (r->pressed ? 1 : 0);
        } else if (r->state == 0) {
            idx = r->sprite;
        } else {
            idx = r->sprite + 1;
        }

        /* Sheet select by `kind`: < 0x18 reads the mode-dependent misc
         * sheet (misc_*.pl8); 0x18 and up read the static system sheet
         * (system2.pl8). */
        int kind = r->kind;
        const uint8_t *sheet = (kind < 0x18) ? misc_sheet : system_sheet;
        if (sheet == NULL) {
            continue;
        }

        /* Stage the descriptor (see file header for the field map). */
        const uint8_t *desc = sheet + idx * DESC_STRIDE + DESC_HEADER;
        int glyph_w      = rd16(desc + 0);
        int height_short = rd16(desc + 2);
        int glyph_off    = (int)rd32(desc + 4);
        int glyph_h      = desc[0xd];

        gfx_dispatch_set_last_glyph_index(idx);             /* DAT_005c9288 */
        gfx_glyph_set_dest(cursor_x, cursor_y);             /* cursor x/y   */
        gfx_glyph_set_glyph(glyph_off, glyph_w, glyph_h);   /* off/w/h      */
        gfx_dispatch_set_glyph_height_short(height_short);  /* DAT_005aeb70 */

        switch (kind) {
        case 0x10: gfx_sprite_blit_16x16_raw(sheet); break;
        case 0x11: gfx_sprite_blit_24x24_raw(sheet); break;
        case 0x18: gfx_sprite_blit_24x24_raw(sheet); break;
        case 0x20: gfx_sprite_blit_32x32_raw(sheet); break;
        default:   break;
        }

        /* Per-button dirty mark — a 3×3-tile (48×48 px) box at the button
         * origin.  Retail's pressed path (0x452775, unported) additionally
         * raises the full-redraw scene flag (DAT_005651ac) before marking;
         * the title scene already raises that flag every full render and
         * these buttons are never drawn pressed, so the pressed/unpressed
         * paths fold into the one mark here (FUN_004527a6, force=2). */
        if (dirty) {
            engine_dirty_rect_mark_box_tiled(dirty, cursor_x, cursor_y,
                                             3, 3, 2);
        }
    }
}
