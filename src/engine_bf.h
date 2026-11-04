/* engine_bf.h — the battlefield port's shared header (docs/findings/battlefield-sim-port.md).
 *
 * The tactical battle (screen 0x12, sub-state DAT_0057c8fc == 2: units, groups, projectiles, the stances, the
 * deployment) is ported function by function on RETAIL's layout, so a port function and retail's run on the
 * identical state under rhost (shadow) and the port can replace retail one function at a time (hybrid).
 *
 *   names    every battlefield function is bf_<va> (tools/bf/parts.py assigns each to one partition); the
 *            partition header engine_bf_<part>.h declares them; not yet ported = a retail stub in
 *            engine_bf_<part>_pending.c (engine_retail_call.h)
 *   state    the save block (save.h, save_schema_globals.h: s_slot_<va>), the runtime globals other ports
 *            already own (engine_combat.h, engine_path.h, …) and the rest: engine_bf_globals.h (g_bf_<va>)
 *   records  the accessors below take the FIELD'S RETAIL VA as IDA prints it: byte_5544AC[432 * i] is
 *            BU8(i, 0x5544ac), word_554620[216 * i] is BS16(i, 0x554620), dword_55449C[108 * i] BS32(i, 0x55449c)
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "save.h"
#include "save_schema_globals.h"
#include "engine_bf_globals.h"
#include "engine_retail_call.h"

/* a T at retail VA `va` inside the port symbol `sym` that sits at retail VA `symva` (any symbol, any offset) */
#define AT(T, sym, symva, va) (*(T *)((uint8_t *)(sym) + ((int32_t)(va) - (int32_t)(symva))))
/* field `va` (slot 0's VA) of record i of a stride-`stride` table at port symbol `sym` / retail `symva` */
#define REC(T, sym, symva, stride, i, va) \
    (*(T *)((uint8_t *)(sym) + (int32_t)(i) * (int32_t)(stride) + ((int32_t)(va) - (int32_t)(symva))))

/* battlefield units: retail 0x554480, 81 × 0x1b0 (slot 0 unused; 1..80) — save.h g_per_battle_hex_432 */
#define BF_UNIT_VA     0x554480u
#define BF_UNIT_STRIDE 0x1b0
#define BU8(i, va)  REC(uint8_t,  g_per_battle_hex_432, BF_UNIT_VA, BF_UNIT_STRIDE, i, va)
#define BS8(i, va)  REC(int8_t,   g_per_battle_hex_432, BF_UNIT_VA, BF_UNIT_STRIDE, i, va)
#define BU16(i, va) REC(uint16_t, g_per_battle_hex_432, BF_UNIT_VA, BF_UNIT_STRIDE, i, va)
#define BS16(i, va) REC(int16_t,  g_per_battle_hex_432, BF_UNIT_VA, BF_UNIT_STRIDE, i, va)
#define BU32(i, va) REC(uint32_t, g_per_battle_hex_432, BF_UNIT_VA, BF_UNIT_STRIDE, i, va)
#define BS32(i, va) REC(int32_t,  g_per_battle_hex_432, BF_UNIT_VA, BF_UNIT_STRIDE, i, va)

/* battlefield groups: retail 0x566520, 81 × 0x34 — save.h g_per_battle_hex_aux_52 */
#define BF_GROUP_VA     0x566520u
#define BF_GROUP_STRIDE 0x34
#define BG8(i, va)   REC(uint8_t,  g_per_battle_hex_aux_52, BF_GROUP_VA, BF_GROUP_STRIDE, i, va)
#define BGS8(i, va)  REC(int8_t,   g_per_battle_hex_aux_52, BF_GROUP_VA, BF_GROUP_STRIDE, i, va)
#define BGU16(i, va) REC(uint16_t, g_per_battle_hex_aux_52, BF_GROUP_VA, BF_GROUP_STRIDE, i, va)
#define BG16(i, va)  REC(int16_t,  g_per_battle_hex_aux_52, BF_GROUP_VA, BF_GROUP_STRIDE, i, va)
#define BG32(i, va)  REC(int32_t,  g_per_battle_hex_aux_52, BF_GROUP_VA, BF_GROUP_STRIDE, i, va)

/* combat units (the armies' troop records the battle draws from): retail 0x57a100, 101 × 0x4c — g_per_combat_unit_76 */
#define BF_CU_VA     0x57a100u
#define BF_CU_STRIDE 0x4c
#define BC8(i, va)   REC(uint8_t,  g_per_combat_unit_76, BF_CU_VA, BF_CU_STRIDE, i, va)
#define BCS8(i, va)  REC(int8_t,   g_per_combat_unit_76, BF_CU_VA, BF_CU_STRIDE, i, va)
#define BCU16(i, va) REC(uint16_t, g_per_combat_unit_76, BF_CU_VA, BF_CU_STRIDE, i, va)
#define BC16(i, va)  REC(int16_t,  g_per_combat_unit_76, BF_CU_VA, BF_CU_STRIDE, i, va)
#define BC32(i, va)  REC(int32_t,  g_per_combat_unit_76, BF_CU_VA, BF_CU_STRIDE, i, va)

/* the battlefield cell grid: retail 0x5440e0, 80 × 80 cells × 8 B (save.h g_castles_state). A unit's cell
 * offset (dword_55449C) is a BYTE offset into it; byte_5440E7[off] is BCELL8(off, 0x5440e7). IDA's names just
 * below the grid (byte_5440D9[off] …) are the same grid at off - 8 (the previous cell): still BCELL8. */
#define BF_CELL_VA 0x5440e0u
#define BCELL8(off, va)  AT(uint8_t,  g_castles_state, BF_CELL_VA, (int32_t)(va) + (int32_t)(off))
#define BCELLS8(off, va) AT(int8_t,   g_castles_state, BF_CELL_VA, (int32_t)(va) + (int32_t)(off))
#define BCELL16(off, va) AT(int16_t,  g_castles_state, BF_CELL_VA, (int32_t)(va) + (int32_t)(off))

/* engine_bf_common.c */
void bf_rng_step(void);   /* FUN_00404a46 on retail's globals (engine_rng_step) */
void bf_sleep(uint32_t ms);                        /* Sleep, as retail calls it (rhost: its virtual clock) */
void bf_bind_sleep_fn(void (*fn)(uint32_t ms));

/* the partitions (tools/bf/parts.py) */
#include "engine_bf_move.h"
#include "engine_bf_strike.h"
#include "engine_bf_unit.h"
#include "engine_bf_action.h"
#include "engine_bf_group.h"
#include "engine_bf_stance.h"
#include "engine_bf_setup.h"
#include "engine_bf_input.h"
#include "engine_bf_extern.h"
