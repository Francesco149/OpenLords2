/* rl_va.h — calling a port by RETAIL VA: the dispatches that go through a retail code pointer held in data (the MP
 * handler tables 0x4d57f0 / 0x4d5980, the delayed actions 0x4d5c98, the confirm dialog's callback DAT_00552ff4, the
 * hot-spot records' actions, the pull-down menus' item actions, the tile drawers). The map (src/rl_va_map.c) is
 * generated from rhost's row tables by tools/gen_rl_va_map.py: every VA whose row is a plain rl_<va> call.
 * A VA with no entry goes to engine_retail_call (retail under rhost; the standalone's entry thunk; abort in a build
 * without either) and is counted (rl_va_misses). */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t va;
    int nargs;                                /* the port's argument count */
    uint32_t (*fn)(const uint32_t *a);        /* adapter: a[0..nargs-1] */
} rl_va_entry_t;

extern const rl_va_entry_t rl_va_map[];
extern const int rl_va_map_n;
extern uint32_t rl_va_misses;
extern void (*rl_va_on_miss)(uint32_t va);   /* called on a VA's first miss (rhost logs it) */

const rl_va_entry_t *rl_va_lookup(uint32_t va);
uint32_t rl_va_call(uint32_t va, int n, ...);   /* n <= 10 int arguments; slots past n read 0 */
