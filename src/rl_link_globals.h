/* rl_link_globals.h — retail globals the link layer (rl_link.c) mirrors that no partition declared (hand).
 * Storage in rl_link_globals.c for a build without rhost; rhost puts each at its retail VA (rh_hooks_ai.c). */
#pragma once
#include <stdint.h>

extern int32_t g_rl_55cd6c;                  /* .data 0x55cd6c: the economy eval's cost scratch */
extern int32_t g_rl_5656f0;                  /* .data 0x5656f0: the economy eval's revenue scratch */
extern int32_t g_rl_57c904;                  /* .data 0x57c904: supply pool a */
extern int32_t g_rl_56d628;                  /* .data 0x56d628: supply pool b */
extern uint8_t g_rl_50a49e;                  /* .data 0x50a49e: the army-state walk's abort flag */
extern int32_t g_rl_553f08;                  /* .data 0x553f08: the army-state walk's live count */
extern int32_t g_rl_4e59dc;                  /* .data 0x4e59dc: the raider target cursor */
/* the screens env's fixed tables (src/rl_link_screens.c: the battlefield renderer's, the armory's) */
extern uint8_t g_rl_4e3bf0[0x440];            /* .data 0x4e3bf0: the battlefield anim offsets at zoom 16, (dx, dy) dwords */
extern uint8_t g_rl_4e4030[0x440];            /* .data 0x4e4030: … at zoom 32 */
extern uint8_t g_rl_4e4470[0x80];             /* .data 0x4e4470: the effect chain's anim offsets, (dx, dy) × 16 */
extern uint8_t g_rl_4e4520[0x60];             /* .data 0x4e4520: the debug label's suffix string (s_bf_debug_cell_label, a 16 B slot " "); the 0x60 copy runs on over errno 0x4e4530 / _doserrno 0x4e4534 .. */
extern uint8_t g_rl_4d2c88[0x100];            /* .data 0x4d2c88: the armory interiors arm_it_<c>.pl8, 16 B each */
/* the mapview env's fixed tables (src/rl_link_mapview.c) */
extern uint8_t g_rl_4d8108[0x300];            /* .data 0x4d8108: the feature renderer's palette tables, [3][2][128] i8 */
extern uint8_t g_rl_4d28f8[0x38];             /* .data 0x4d28f8: the minimap's band colours (the port reads 0x40: +0x38 = dword_4D2930) */
extern uint8_t g_rl_4d2ec8[0x1d0];            /* .data 0x4d2ec8: the terrain-info special-feature table, 29 × 4 dwords */
extern uint8_t g_rl_4d4230[0x10];             /* .data 0x4d4230: "icon_tmp.pl8" */
extern uint8_t g_rl_4d9278[0x300];            /* .data 0x4d9278: the result movies bat_win<n>.smk, field 6 × 4 then siege (+0x180), 16 B each */
