/*
 * OpenLords2 — the battle / army-movement runtime scalars retail keeps in .data
 * (not save state).  Each is one retail global, named by its VA in the comment;
 * engine_combat.h / engine_army_move.h declare them.
 *
 * They live in their own translation unit so the hybrid host (tools/rhost) can
 * leave this file out and bind every symbol to retail's own address instead
 * (rh_hooks_combat.c), exactly like save_schema_globals.c: port code linked into
 * rhost then reads and writes retail's memory, with no copies to keep in sync.
 * Widths match retail (u8 where retail's global is a byte).
 */

#include <stdint.h>

#include "engine_combat.h"
#include "engine_army_move.h"

/* engine_combat.c */
int      g_combat_both_garrison;     /* DAT_004ee89c */
int      g_combat_ctrl_408;          /* DAT_00554408 */
int      g_combat_ctrl_5b0;          /* DAT_0056d5b0 */
int      g_combat_render_active;     /* DAT_0056d5c8 */
int      g_combat_active_terr;       /* DAT_00553f8c */
int      g_combat_winner_owner;      /* DAT_005440cc */
int      g_combat_audio_latch;       /* DAT_005533f0 */
int      g_combat_flag_530d0;        /* DAT_005530d0 */
int      g_combat_flag_553210;       /* DAT_00553210 */
int      g_combat_flag_567d0c;       /* DAT_00567d0c */
int      g_combat_flag_53f284;       /* DAT_0053f284 */
int      g_combat_render_dirty;      /* DAT_0052afcc */
uint32_t g_combat_anim_clock;        /* DAT_005689a0 */
uint32_t g_combat_battle_clock;      /* DAT_0057a0d0 */
uint32_t g_combat_anim_t0;           /* DAT_0053e994 */
uint32_t g_combat_anim_deadline;     /* DAT_00568d9c */
int      g_combat_anim_scratch[10];  /* DAT_00515d60 */
int      g_siege_sel_attacker;       /* DAT_0055cd58 */
int      g_siege_sel_region;         /* DAT_0052f0a8 */
int      g_siege_besieger;           /* DAT_00568ed0 */
int      g_siege_region;             /* DAT_00554000 */

/* engine_army_move.c */
uint8_t  g_army_move_cell_flags;     /* DAT_0050a4a4 */
uint8_t  g_army_move_cell_region;    /* DAT_0050a49c */
uint8_t  g_army_move_cell_terrain;   /* DAT_0050a49d */
int      g_army_move_cell_off;       /* DAT_0050a4a0 */
uint32_t g_army_move_wp_col;         /* FUN_00465d28 [ebp-0xc]: stack residue, not a global */
uint32_t g_army_move_wp_row;         /* FUN_00465d28 [ebp-0x10] */
uint32_t g_army_move_dir;            /* FUN_00465d28 [ebp-0x4] */
uint32_t g_army_move_code;           /* FUN_00465d28 [ebp-0x8] */
