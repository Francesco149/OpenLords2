/*
 * OpenLords2 — the realm passes' runtime scratch retail keeps in .data (not save
 * state).  Each is one retail global, named by its VA in the comment;
 * engine_realm.h declares them.
 *
 * Own translation unit, like engine_combat_globals.c: the hybrid host
 * (tools/rhost) leaves this file out and binds every symbol to retail's own
 * address instead (rh_hooks_events.c), so port code linked into rhost reads and
 * writes retail's memory.
 */

#include <stdint.h>

#include "engine_realm.h"

/* engine_realm.c — the coalition pass (FUN_0044b042 / FUN_0044ae51) */
int32_t g_coalition_tbl[0x11 * 0x1c / 4];   /* 0x568240: 17 groups × 0x1c B */
int32_t g_coalition_active_count;           /* DAT_00553ef4 */
