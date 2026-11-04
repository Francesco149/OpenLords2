/*
 * OpenLords2 — the Court / Treasury screen's retail globals (engine_treasury.h declares them).
 *
 * Own translation unit: the hybrid host (tools/rhost) leaves this file out and aliases each symbol at its retail
 * VA instead (rh_hooks_screens.c), so the linked port reads and writes retail's memory.
 */
#include "engine_treasury.h"

/* DAT_004dd928[0]: the "continue" seal (system2.pl8 sprite 0x40, kind 0x20 = 32×32, type 5 press-latch; action
 * 0x4351c4 = advance to the Rankings) — r2-extracted byte-exact. */
engine_button_rec_t g_treasury_buttons[1] = {
    { 0x150, 0x154, 0x40, 0x20, 0x4351c4, 0, 0, 0, 5, 1, 0 },
};

/* DAT_00568468: the router widget count. */
int32_t g_treasury_widget_count;
