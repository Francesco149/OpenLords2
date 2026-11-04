/*
 * OpenLords2 — the Noble Faces screen's retail globals (engine_noble_faces.h declares them).
 *
 * Own translation unit: the hybrid host (tools/rhost) leaves this file out and aliases each symbol at its retail
 * VA instead (rh_hooks_screens.c), so the linked port reads and writes retail's memory.
 */
#include "engine_noble_faces.h"

/* DAT_004dd940: the 6 diplomacy-action buttons — x=0x190, y=0x66+i*0x32, sprite 0x40, kind 0x20 (system sheet),
 * type 5 (press-latch); the actions are the 0x1a-mode entry functions 0x436141 … 0x43631e. */
engine_button_rec_t g_noble_buttons[6] = {
    { 0x190, 0x066, 0x40, 0x20, 0x436141, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x098, 0x40, 0x20, 0x43618b, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x0ca, 0x40, 0x20, 0x4361da, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x0fc, 0x40, 0x20, 0x436229, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x12e, 0x40, 0x20, 0x4362ca, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x160, 0x40, 0x20, 0x43631e, 0, 0, 0, 5, 0, 0 },
};

/* DAT_00553ee8: the live action-button count (0/3/4/6 by the relationship state), set by the open render. */
int32_t g_noble_action_count;
