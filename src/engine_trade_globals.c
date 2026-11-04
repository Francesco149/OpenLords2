/*
 * OpenLords2 — the trade grid's retail globals (engine_trade.h declares them).
 *
 * Own translation unit: the hybrid host (tools/rhost) leaves this file out and aliases each symbol at its retail
 * VA instead (rh_hooks_screens.c), so the linked port reads and writes retail's memory.
 */
#include "engine_trade.h"

/* DAT_004dd838: the up/down buy/sell arrow widgets (r2-extracted byte-exact).  Buy mode shows the first 4 (up /
 * down / buy-all / sell-all, kind 0x18 = 24×24 system sprites); sell mode adds two more (kind 0x20 = 32×32).  The
 * action field is the retail callee address the router dispatches. */
engine_button_rec_t g_trade_buttons[6] = {
    { 0x60, 0x88, 0x15, 0x18, 0x435339, 0, 0, 0, 4, 0, 0 },
    { 0x78, 0x88, 0x17, 0x18, 0x43543d, 0, 0, 0, 4, 0, 0 },
    { 0x98, 0x88, 0x48, 0x18, 0x4355db, 0, 0, 0, 4, 0, 0 },
    { 0xb0, 0x88, 0x46, 0x18, 0x435541, 0, 0, 0, 4, 0, 0 },
    { 0x110, 0xa8, 0x1d, 0x20, 0x435286, 0, 0, 0, 4, 0, 0 },
    { 0x138, 0xa8, 0x1f, 0x20, 0x4352f2, 0, 0, 0, 4, 0, 0 },
};

/* DAT_00553f58: the router's widget count (4 buy / 6 sell), set by the content draw. */
int32_t g_trade_widget_count;
