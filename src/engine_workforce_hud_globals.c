/*
 * OpenLords2 — the workforce HUD's retail globals (engine_workforce_hud.h declares them).
 *
 * Own translation unit: the hybrid host (tools/rhost) leaves this file out and aliases each symbol at its retail
 * VA instead (rh_hooks_screens.c), so the linked port reads and writes retail's memory.
 */
#include "engine_workforce_hud.h"

uint8_t g_wf_grid[8 * 25];           /* DAT_005540a0 */
const uint8_t *g_wf_icon_sheet;      /* DAT_005530c8 */
uint8_t g_wf_hover_category;         /* DAT_0053e9a0 */
uint8_t g_wf_hover_cells[25];        /* DAT_00553040 */
int32_t g_wf_panel_mode;             /* DAT_0053f050 */
int32_t g_wf_y_origin;               /* DAT_0057c908 */
uint8_t *g_wf_villani1;              /* DAT_0053e918 */
uint8_t *g_wf_villani2;              /* DAT_005533c4 */
