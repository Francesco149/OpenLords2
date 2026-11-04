/*
 * OpenLords2 — multiplayer render-phase overlays.  See engine_mp_render.c.
 */

#ifndef OPENLORDS2_ENGINE_MP_RENDER_H
#define OPENLORDS2_ENGINE_MP_RENDER_H

#include <stdint.h>

#include "localization.h"

/* Bind the menu sprite asset used by the player indicator: retail draws from the CURRENT sprite sheet
 * DAT_004eabec (g_gfx_current_blob), which this sets. */
void engine_mp_render_bind_menu_asset(const uint8_t *asset);

/* Bind the status sprite asset (retail DAT_005bb540) used by the status
 * line. */
void engine_mp_render_bind_status_asset(const uint8_t *asset);

/* Bind the text inputs for the status line: title font (DAT_005b2fa0),
 * render-side char→glyph table (DAT_004d71f0), and the localization
 * blob (DAT_00591580). */
void engine_mp_render_bind_text(const uint8_t *title_font,
                                const uint8_t *glyph_table,
                                unsigned char *loc_blob);

/* Point the status line at an external localization state (the hybrid host's: retail's blob + walker); NULL →
 * the module's own (engine_mp_render_bind_text). */
void engine_mp_render_bind_loc(localization_state_t *loc);

/* Bind the indicator's clock (timeGetTime, read inside the gate as retail does); NULL → the `now_ms` argument. */
void engine_mp_render_bind_clock(uint32_t (*clock)(void));

/* Port of FUN_0041f15a.  While `mp_active` and `*timer_ms != 0`, blit
 * the per-slot indicator sprite; clear `*timer_ms` once 10 s elapsed
 * (the blit still happens on the clearing frame).  `now_ms` is the
 * current timeGetTime() value. */
void engine_mp_render_player_indicator(int mp_active, int *timer_ms,
                                       uint32_t now_ms, int slot);

/* Port of FUN_00423b4f.  Draw the MP status localization line + sprite. */
void engine_mp_render_status_line(void);

#endif /* OPENLORDS2_ENGINE_MP_RENDER_H */
