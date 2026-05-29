/*
 * OpenLords2 — multiplayer render-phase overlays.
 *
 * Two small render-phase leaves the per-tick driver (FUN_004b99c0)
 * fires every frame; both are no-ops outside multiplayer / their timer
 * window.  They rest entirely on already-ported gfx primitives
 * (gfx_sprite_render_from_blob, gfx_wrap_localized_line), so they port
 * as pure C with the assets/fonts injected via the binders below.
 *
 *   FUN_0041f15a @ 0x41f15a (131 B) → engine_mp_render_player_indicator
 *     While MP is active AND the per-player flash timer is set, blit
 *     sprite 0xd7 from the menu asset at a per-slot X.  The timer is
 *     cleared once 10 s have elapsed (the blit still happens on the
 *     clearing frame).
 *
 *   FUN_00423b4f @ 0x423b4f (85 B) → engine_mp_render_status_line
 *     Draw localization line 0x20 (title font, no-shadow) at
 *     (0x10e, 0x1ae) and sprite 0x4d from the status asset at
 *     (400, 0x1c4).
 *
 * Timer note: retail reads timeGetTime() inside FUN_0041f15a.  Per the
 * engine_turn pattern, the pure function takes `now_ms` + the timer as
 * an in/out param; the Win32 wrapper (engine.c, when this lands in the
 * render path) supplies timeGetTime and the live globals.
 */

#include <stddef.h>

#include "engine_mp_render.h"
#include "localization.h"
#include "gfx_text.h"
#include "gfx_wrap.h"
#include "gfx_sprite_render.h"

/* ─── bound assets / fonts ──────────────────────────────────────── */

static const uint8_t *g_menu_asset;     /* DAT_004eabec (player indicator) */
static const uint8_t *g_status_asset;   /* DAT_005bb540 (status sprite)    */
static const uint8_t *g_title_font;     /* DAT_005b2fa0 */
static const uint8_t *g_glyph_table;    /* DAT_004d71f0 */
static localization_state_t g_loc;      /* DAT_00591580 */

void engine_mp_render_bind_menu_asset(const uint8_t *asset)   { g_menu_asset = asset; }
void engine_mp_render_bind_status_asset(const uint8_t *asset) { g_status_asset = asset; }
void engine_mp_render_bind_text(const uint8_t *title_font,
                                const uint8_t *glyph_table,
                                unsigned char *loc_blob)
{
    g_title_font = title_font;
    g_glyph_table = glyph_table;
    g_loc.blob = loc_blob;
    g_loc.walker_offset = 0;
}

/* ─── render ────────────────────────────────────────────────────── */

/* Port of FUN_0041f15a. */
void engine_mp_render_player_indicator(int mp_active, int *timer_ms,
                                       uint32_t now_ms, int slot)
{
    if (mp_active != 0 && *timer_ms != 0) {
        if (10000 < (int)(now_ms - (uint32_t)*timer_ms)) {
            *timer_ms = 0;
        }
        gfx_sprite_render_from_blob(g_menu_asset, 0xd7,
                                    (slot - 1) * 0x58 + 0x74, 0x70);
    }
}

/* Port of FUN_00423b4f. */
void engine_mp_render_status_line(void)
{
    gfx_text_set_no_shadow(1);
    gfx_wrap_localized_line(&g_loc, /*loc_index=*/0x20, /*skip=*/0,
                            /*x=*/0x10e, /*y=*/0x1ae,
                            g_title_font, g_glyph_table, /*color=*/0x20);
    gfx_sprite_render_from_blob(g_status_asset, 0x4d, 400, 0x1c4);
    gfx_text_set_no_shadow(0);
}
