/*
 * OpenLords2 — generic button-strip renderer.
 *
 * Port of FUN_0040cfd2 @ 0x40cfd2 — the per-screen button-list painter
 * shared by every UI screen.  Retail's per-frame UI refresh dispatcher
 * (FUN_004ba26e) calls it once per active screen state with that
 * screen's static button table (DAT_004ddd78, DAT_004dd790, …) and a
 * (origin_x, origin_y) offset, e.g. the page-3 "Loading a game."
 * save/load list draws its gauntlet action buttons + scroll arrows via
 *
 *     FUN_0040cfd2(0x60, 10, &DAT_004ddd78, 4)
 *
 * Each button stages a 16-byte sprite descriptor from one of two
 * sheets and blits a fixed-size opaque sprite through the gfx_sprite
 * raw leaves.  The sheet + blitter are chosen by the record's `kind`
 * field; the displayed frame (base / base+1) is chosen by the
 * pressed/hover state.  No clip is applied — the buttons live in fixed
 * on-screen rectangles that never straddle the canvas edge.
 *
 * The records double as the input hit-test table (the action pointer +
 * params at +0x08 / +0x10 / +0x14 are consumed by FUN_0040e3ee — see
 * engine_menu_input); the renderer ignores those fields.
 */
#ifndef OPENLORDS2_ENGINE_BUTTON_H
#define OPENLORDS2_ENGINE_BUTTON_H

#include <stdint.h>

#include "engine_dirty_rect.h"

/* Button-strip record — retail's 0x18-byte (24-byte) per-button struct,
 * the element type of the per-screen tables FUN_0040cfd2 walks.  Field
 * offsets mirror the retail layout exactly so the same struct serves
 * both the renderer and the input hit-test. */
typedef struct engine_button_rec {
    int16_t  x;        /* +0x00  x offset, added to the strip origin     */
    int16_t  y;        /* +0x02  y offset                                */
    int16_t  sprite;   /* +0x04  base sprite index in the chosen sheet   */
    int16_t  kind;     /* +0x06  sheet + blit selector (0x10/0x11/0x18/0x20) */
    uint32_t action;   /* +0x08  click action code* (render: unused)     */
    uint8_t  state;    /* +0x0c  alt-frame select for non-(4/5) types    */
    uint8_t  pressed;  /* +0x0d  pressed latch (→ sprite+1, dirty escalate) */
    uint8_t  pad_e;    /* +0x0e                                          */
    uint8_t  type;     /* +0x0f  edge type: 4/5 → use `pressed`, else `state` */
    int32_t  param0;   /* +0x10  action param (render: unused)           */
    int32_t  param1;   /* +0x14  second param (render: unused)           */
} engine_button_rec_t;

/* Port of FUN_0040cfd2.  Paint `count` buttons at (origin_x, origin_y)
 * + each record's offset.
 *
 *   misc_sheet    DAT_005530c8 — the mode-dependent misc_*.pl8 blob,
 *                 read by sprite kinds < 0x18 (0x10 / 0x11).  May be
 *                 NULL when the active screen uses only system sprites.
 *   system_sheet  DAT_005bb540 — the static system2.pl8 blob, read by
 *                 kinds >= 0x18 (0x18 / 0x20).
 *   dirty         the render-invalidation accumulator marked per button
 *                 (FUN_004527a6); pass NULL to skip the bookkeeping.
 *
 * A button whose chosen sheet is NULL is skipped (retail assumes both
 * are bound when the screen is active; the drop-in only binds the
 * sheet a screen actually needs). */
void engine_button_render_list(int origin_x, int origin_y,
                               const engine_button_rec_t *recs, int count,
                               const uint8_t *misc_sheet,
                               const uint8_t *system_sheet,
                               engine_dirty_rect_state_t *dirty);

#endif /* OPENLORDS2_ENGINE_BUTTON_H */
