/* engine_bf_move_globals.c — retail globals the 'move' partition needs that engine_bf_globals.h lacks (the
 * standalone's definitions; rhost does not link *_globals.c: tools/rhost/rh_hooks_bf_move.c aliases each at its
 * VA). */
#include <stdint.h>

uint8_t g_bf_4f9680[300];   /* DAT_004f9680: the greedy path walk's (col, row) byte pairs (0x472392) */
