/*
 * OpenLords2 — the title / menu renderer's retail globals (engine_title_render.h declares them).
 *
 * Own translation unit: the hybrid host (tools/rhost) leaves this file out and aliases each symbol at its retail
 * VA instead (rh_hooks_screens.c), so the linked port reads and writes retail's memory.  Widths are retail's (the
 * asm of every reader / writer): dwords, except the shield-slot bytes.
 */
#include "engine_title_render.h"

int32_t g_tr_selected_button = 1;        /* DAT_00553114 */
int32_t g_tr_page;                       /* DAT_005530f0 */
int32_t g_tr_scene_flag_a;               /* DAT_005651ac */
int32_t g_tr_p4_gate_a;                  /* DAT_00553030 (the multiplayer flag) */
int32_t g_tr_p4_gate_b;                  /* DAT_00553248 */
int32_t g_tr_p4_continue_disabled;       /* DAT_0057c940 */
uint8_t g_tr_p4_shield_slots[6];         /* DAT_0057cb40 */
int32_t g_tr_p4_selected_shield;         /* DAT_0057c8cc (the current player) */
int32_t g_tr_p4_shield_flash;            /* DAT_0057d354 */
int32_t g_tr_p4_flash_timer;             /* DAT_00568984 */
uint8_t g_tr_p4_name_buf[32];            /* DAT_0053f1e0 */
int32_t g_tr_options[12];                /* DAT_0053f288..DAT_0053f2b4 */
const uint8_t *g_tr_misc_sheet;          /* DAT_005530c8 */
int32_t g_tr_country_scroll;             /* DAT_0053e90c */
int32_t g_tr_country_sel;                /* DAT_0057cacc */
int32_t g_tr_country_count;              /* DAT_00554018 */
int32_t g_tr_country_dirty;              /* DAT_0057c948 */
int32_t g_tr_tabs_dirty;                 /* DAT_0055ce94 */
int32_t g_tr_p9_tab;                     /* DAT_0055306c */
int32_t g_tr_p9_count;                   /* DAT_00553fb4 */
int32_t g_tr_p9_bookmark = 7;            /* DAT_00553e5c */
