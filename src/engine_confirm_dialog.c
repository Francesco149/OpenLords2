/*
 * OpenLords2 — generic Yes/No confirm-dialog modal (screen 0x1e).  See
 * engine_confirm_dialog.h for the retail address map + design notes.
 */

#include "engine_confirm_dialog.h"

#include <stddef.h>

#include "engine_button.h"
#include "gfx_panel.h"
#include "gfx_wrap.h"
#include "gfx_glyph_table.h"

/* r2-verified byte-for-byte at retail 0x4dd310 (2 * 0x18-byte records).
 * `action` is an unused placeholder (both records dispatch through the
 * same fire handler below); `param0` is the only field that actually
 * differs (1 = Yes, 0 = No). */
static const engine_button_rec_t k_confirm_buttons_template[2] = {
    /* Yes */ { .x = 64,  .y = 46, .sprite = 29, .kind = 0x20,
                .action = 0, .state = 0, .pressed = 0, .pad_e = 0, .type = 5,
                .param0 = 1, .param1 = 0 },
    /* No  */ { .x = 112, .y = 50, .sprite = 31, .kind = 0x20,
                .action = 0, .state = 0, .pressed = 0, .pad_e = 0, .type = 5,
                .param0 = 0, .param1 = 0 },
};

/* Non-const: engine_menu_button_dispatch mutates the per-button
 * press-animation state (pressed/state/pad_e) in place across calls —
 * same convention as engine_title_render.c's g_filelist_buttons.  Reset
 * from the template each time the dialog opens (a fresh dialog starts
 * with both buttons unpressed, matching retail's per-open table reload). */
static engine_button_rec_t g_confirm_buttons[2];

static int g_msg_id;                                  /* DAT_0057cb14 */
static int g_origin_x, g_origin_y;                     /* DAT_0052af90/98 */
static int g_prev_screen;                              /* DAT_0057c90c */
static int g_answer;                                   /* DAT_0059151c */
static engine_confirm_dialog_callback_fn g_callback;   /* DAT_00552ff4 */

void engine_confirm_dialog_open(int msg_id, int ox, int oy, int prev_screen,
                                engine_confirm_dialog_callback_fn callback,
                                int *out_screen, int *out_demand_redraw)
{
    g_prev_screen = prev_screen;
    g_answer      = 0;
    g_origin_x    = ox;
    g_origin_y    = oy;
    g_callback    = callback;
    g_msg_id      = msg_id;
    g_confirm_buttons[0] = k_confirm_buttons_template[0];
    g_confirm_buttons[1] = k_confirm_buttons_template[1];

    /* FUN_004b36c0(msg_id) — audio seam ("plays S010_XX.wav"); no NULL
     * hook plumbed yet (matches every other not-yet-wired SFX seam in
     * this codebase). */

    *out_screen        = ENGINE_CONFIRM_DIALOG_SCREEN;
    *out_demand_redraw = 2;
}

int engine_confirm_dialog_answer(void)
{
    return g_answer;
}

int engine_confirm_dialog_prev_screen(void)
{
    return g_prev_screen;
}

/* Port of FUN_00434e1f.  Both button records dispatch here; `param0` is
 * the fired record's answer value. */
static void confirm_button_fire(uint32_t action, int param0, int param1)
{
    (void)action;
    (void)param1;
    g_answer = param0;
    if (g_callback) {
        g_callback();
    }
}

int engine_confirm_dialog_input_step(const engine_menu_click_in_t *in)
{
    int fired = engine_menu_button_dispatch(g_origin_x, g_origin_y,
                                            g_confirm_buttons, 2,
                                            in, confirm_button_fire);
    return fired != 0;
}

void engine_confirm_dialog_render(localization_state_t *loc,
                                  const uint8_t *chrome,
                                  const uint8_t *menu_font,
                                  const uint8_t *system_sheet,
                                  const unsigned char *glyph_metrics)
{
    (void)glyph_metrics;

    /* FUN_0040ccfa: FUN_004b1de0() is a verified no-op (elided);
     * DAT_004eb260 = 1 is present-time bookkeeping with no reader
     * (elided, same precedent as engine_hud_panel.h). */
    gfx_panel_draw_bevel(chrome, g_origin_x - 0x10, g_origin_y - 0x10, 0xe, 8);
    gfx_wrap_localized_line(loc, /*loc_index=*/10, /*skip_strings=*/g_msg_id,
                            g_origin_x + 0x10, g_origin_y + 0x10,
                            menu_font, gfx_glyph_render_map(), 0x3f);

    /* FUN_0040cfd2(ox,oy,&DAT_004dd310,2) — the button pair.  Rendering
     * ignores action/param0/param1, so the shared const table is fine
     * here (unlike the input-tick hit-test, which mutates per-button
     * animation state). */
    engine_button_render_list(g_origin_x, g_origin_y, g_confirm_buttons, 2,
                              NULL, system_sheet, NULL);
}
