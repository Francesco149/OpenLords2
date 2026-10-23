/*
 * OpenLords2 — the territory sweeps' runtime scratch retail keeps in .data (not save state).
 * Each is one retail global, named by its VA in the comment; engine_territory.h declares them.
 *
 * Own translation unit, like engine_realm_globals.c: the hybrid host (tools/rhost) leaves this
 * file out and binds every symbol to retail's own address instead (rh_hooks_ai.c), so port code
 * linked into rhost reads and writes retail's memory.
 */

#include <stdint.h>

#include "engine_territory.h"

/* DAT_0053e974: the per-cell sweep counter. Reset and bumped once per visited cell by the
 * cell sweeps (FUN_0046df51 / 0046dfd5: 64×64 → 0x1000; FUN_0046dbf5 / 0046dd34 / 0046797d:
 * 129×65 → 8385) and the strategic-map loader (FUN_00467770). Never read across calls, but it
 * is state: the shadow compares it (quirk #237). */
int32_t g_cell_sweep_counter;
