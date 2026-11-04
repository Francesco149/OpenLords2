/*
 * OpenLords2 — the Armory's retail globals (engine_armory.h declares them).
 *
 * Own translation unit: the hybrid host (tools/rhost) leaves this file out and aliases each symbol at its retail
 * VA instead (rh_hooks_screens.c), so the linked port reads and writes retail's memory.  The sheet pointers are
 * retail's (0x4996c8 allocates them); the standalone's render allocates its own on first use.
 */
#include "engine_armory.h"

int32_t g_arm_pct;            /* DAT_0056d65c */
int32_t g_arm_conscripts;     /* DAT_00543fd8 */
int32_t g_arm_happy_cost;     /* DAT_00565400 */
int32_t g_arm_mode;           /* DAT_00522f58 */
int32_t g_arm_merc;           /* DAT_0055446c */
int32_t g_arm_weapon;         /* DAT_00553f20 */
int32_t g_arm_march_active;   /* DAT_005679d0 */
int32_t g_arm_march_x;        /* DAT_0056d630 */
int32_t g_arm_march_y;        /* DAT_0056d62c */
int32_t g_arm_march_target;   /* DAT_00568228 */
int32_t g_arm_march_land;     /* DAT_005681f8 */
int32_t g_arm_march_sprite;   /* DAT_0052f008 */
int32_t g_arm_march_walk;     /* DAT_0057cb10 */
int32_t g_arm_glint;          /* DAT_0057c8e4 */
uint8_t *g_arm_interior;      /* DAT_0056d5b8 */
uint8_t *g_arm_torch;         /* DAT_005533c4 */
uint8_t *g_arm_troop;         /* DAT_0057a0c0 */

/* DAT_004dd340: the overlay strip — the confirm seal (type 5 press-latch) + the mercenary Yes/No gauntlets */
engine_button_rec_t g_arm_overlay_buttons[3] = {
    { 0x1e0, 0x150, 0x21, 0x18, 0x435cbf, 0, 0, 0, 5, 0, 0 },
    { 0x160, 0x100, 0x1d, 0x20, 0x435c89, 0, 0, 0, 5, 1, 0 },
    { 0x190, 0x104, 0x1f, 0x20, 0x435c89, 0, 0, 0, 5, 0, 0 },
};
