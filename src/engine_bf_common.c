/* engine_bf_common.c — helpers every battlefield partition shares (docs/findings/battlefield-sim-port.md). */
#include "engine_bf.h"
#include "engine.h"
#include "engine_tile.h"

#include <windows.h>

/* 0x404a46 on retail's globals (the port: engine_rng_step): the LFSR pair DAT_0058fe28 / DAT_0058fe08 and its
 * six masked outputs. A battlefield function that calls the RNG calls this. */
void bf_rng_step(void)
{
    engine_rng_in_t in = { s_slot_0058fe28, s_slot_0058fe08 };
    engine_rng_out_t o;
    engine_rng_step(&in, &o);
    s_slot_0058fe28 = o.new_state_a;
    s_slot_0058fe08 = o.new_state_b;
    g_bf_5aea58 = (int32_t)o.rng_15bit_a;
    g_bf_58fd7c = (int32_t)o.rng_7bit_a;
    g_bf_58fd70 = (int32_t)o.rng_3bit_a;
    g_bf_5aea5c = (int32_t)o.rng_15bit_b;
    g_bf_58fd60 = (int32_t)o.rng_7bit_b;
    g_bf_58fd6c = (int32_t)o.rng_3bit_b;
}

/* Sleep(ms) as retail calls it (the MP arms' message spacing). rhost binds retail's own import (its virtual clock
 * advances); the standalone sleeps. */
static void (*s_sleep_fn)(uint32_t ms);
void bf_bind_sleep_fn(void (*fn)(uint32_t ms)) { s_sleep_fn = fn; }
void bf_sleep(uint32_t ms)
{
    if (s_sleep_fn)
        s_sleep_fn(ms);
    else
        Sleep(ms);
}

/* engine_tile's ports on retail's arrays (the cell grid g_castles_state, the path / stage scratches, the footprint
 * list), with retail's EAX: FUN_00471c1f leaves the last cell index (6399), FUN_00471d30 the last cell's offset
 * 0xc7f8 with its stage byte in AL, FUN_0048ee46 its argument. */
int bf_tile_refresh_path(void)
{
    engine_tile_refresh_path_scratch(g_castles_state, g_path_scratch);
    return 6399;
}
int bf_tile_refresh_stage(void)
{
    engine_tile_refresh_stage_scratch(g_castles_state, g_stage_scratch);
    return 0xc700 | g_stage_scratch[6399];
}
int bf_tile_push_footprint(int off)
{
    engine_tile_push_footprint_record(g_footprint_list, &g_last_footprint_tile, off);
    return off;
}
int bf_tile_neighbor_flags(int col, int row, int orient)
{
    engine_tile_set_footprint_neighbor_flags(g_castles_state, col, row, orient);
    return 0;
}
