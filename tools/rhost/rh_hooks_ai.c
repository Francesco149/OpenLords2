/* rh_hooks_ai.c — the AI realm turn (src/engine_army_ai.c, engine_army_economy.c, engine_realm.c) vs
 * retail, on identical state (rh_hooks.h contract; shared plumbing: rh_port.h).
 *
 * This file also holds the plumbing every port-hook table shares (rh_port.h), one copy for rhost.exe:
 *
 * BINDING.  The port's save-block globals (save.c / save_schema_globals.c: g_per_army_768,
 * g_player_strategic_352, every s_slot_<VA>, ...) are NOT linked: the asm block below defines each one as an
 * ABSOLUTE symbol at its retail VA, so every linked port module reads and writes retail's own memory —
 * no copies, no sync (these aliases are global to rhost.exe: no other table may link save.c). rh_port_bind_once()
 * checks the aliases against retail's own save-schema table (DAT_004de960: 225 (ptr, size) pairs) and points
 * the modules' runtime pointer seams (*_bind_*) at the retail tables. What remains are the modules'
 * FILE-STATIC / module mirrors of retail globals (config scalars, scratch registers): rh_port_sync_in() loads
 * them from retail before every port call, rh_port_sync_out() stores them back after it. Functions that take
 * the event queue get a port queue loaded from / stored to retail's six queue globals (EVQ).
 *
 * LINK.  build.sh PORT_OBJS links the AI closure minus the modules that drag in the whole engine (game_init,
 * engine, main, engine_turn, ...); the few symbols that cross that cut are bridged at the bottom of this file
 * (game_init) and of rh_hooks_combat.c (engine / engine_turn). */
#include "rh_port.h"
#include "save.h"
#include "save_schema_globals.h"
#include "engine_army_ai.h"
#include "engine_army_economy.h"
#include "engine_realm.h"
#include "engine_event_queue.h"
#include "engine_territory.h"
#include "engine_map_query.h"
#include "engine_army_state.h"
#include "engine_path_field.h"
#include "engine_path.h"
#include "engine_ranking.h"
#include "engine_combat.h"
#include "engine_army_move.h"
#include "engine_realm_upkeep.h"
#include "engine_diplomacy.h"
#include "game_init.h"

/* ---------------------------------------------------------------- binding: save globals = retail memory */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(
A(g_overworld_map,0x522f90) A(g_castles_state,0x5440e0) A(g_player_aux_44,0x553d50) A(g_player_strategic_352,0x57bf00)
A(g_per_army_768,0x53f9b0) A(g_per_territory_420,0x52f0b0) A(g_unit_table_20,0x568dc0) A(g_per_battle_hex_432,0x554480)
A(g_per_combat_unit_76,0x57a100) A(g_per_battle_hex_aux_52,0x566520) A(g_realm_history,0x56d8c0) A(g_misc_128,0x568480)
A(g_misc_1360,0x53ea00) A(g_footprint_storage_160,0x553c80) A(g_player_roster,0x568db0) A(g_castles_scratch,0x4eb2b0)
A(g_path_scratch,0x507250) A(g_stage_scratch,0x4f7d80)
/* engine.c */
A(g_sim_mp_active,0x553030)
/* engine_territory_globals.c: the cell sweeps' counter (quirk #237) */
A(g_cell_sweep_counter,0x53e974)
/* save_schema_globals.c: every s_slot_<VA> is retail VA <VA> */
A(s_slot_004eafb0,0x4eafb0) A(s_slot_004eb2ac,0x4eb2ac) A(s_slot_00522d30,0x522d30) A(s_slot_00522d90,0x522d90) A(s_slot_00522f54,0x522f54)
A(s_slot_00522f5c,0x522f5c) A(s_slot_00522f60,0x522f60) A(s_slot_00522f68,0x522f68) A(s_slot_00522f6c,0x522f6c) A(s_slot_00522f70,0x522f70)
A(s_slot_00522f74,0x522f74) A(s_slot_0052af9c,0x52af9c) A(s_slot_0052f00c,0x52f00c) A(s_slot_0052f0a4,0x52f0a4) A(s_slot_0053e86c,0x53e86c)
A(s_slot_0053e8a4,0x53e8a4) A(s_slot_0053e8bc,0x53e8bc) A(s_slot_0053e8fc,0x53e8fc) A(s_slot_0053e91c,0x53e91c) A(s_slot_0053e920,0x53e920)
A(s_slot_0053e924,0x53e924) A(s_slot_0053e97c,0x53e97c) A(s_slot_0053e984,0x53e984) A(s_slot_0053e990,0x53e990) A(s_slot_0053e9a8,0x53e9a8)
A(s_slot_0053e9bc,0x53e9bc) A(s_slot_0053e9cc,0x53e9cc) A(s_slot_0053e9d0,0x53e9d0) A(s_slot_0053e9d4,0x53e9d4) A(s_slot_0053ef5c,0x53ef5c)
A(s_slot_0053f010,0x53f010) A(s_slot_0053f028,0x53f028) A(s_slot_0053f034,0x53f034) A(s_slot_0053f058,0x53f058) A(s_slot_0053f06c,0x53f06c)
A(s_slot_0053f07c,0x53f07c) A(s_slot_0053f084,0x53f084) A(s_slot_0053f0d4,0x53f0d4) A(s_slot_0053f0f0,0x53f0f0) A(s_slot_0053f23c,0x53f23c)
A(s_slot_0053f258,0x53f258) A(s_slot_0053f25c,0x53f25c) A(s_slot_0053f260,0x53f260) A(s_slot_0053f264,0x53f264) A(s_slot_0053f268,0x53f268)
A(s_slot_0053f26c,0x53f26c) A(s_slot_0053f640,0x53f640) A(s_slot_0053f64c,0x53f64c) A(s_slot_0053f654,0x53f654) A(s_slot_0053f658,0x53f658)
A(s_slot_0053f678,0x53f678) A(s_slot_0053f680,0x53f680) A(s_slot_00542cc8,0x542cc8) A(s_slot_00543fc0,0x543fc0) A(s_slot_00544050,0x544050)
A(s_slot_00544098,0x544098) A(s_slot_0054409c,0x54409c) A(s_slot_005440a8,0x5440a8) A(s_slot_005440c8,0x5440c8) A(s_slot_0055300c,0x55300c)
A(s_slot_00553028,0x553028) A(s_slot_0055302c,0x55302c) A(s_slot_00553034,0x553034) A(s_slot_00553070,0x553070) A(s_slot_0055307c,0x55307c)
A(s_slot_00553080,0x553080) A(s_slot_005530b0,0x5530b0) A(s_slot_005530b4,0x5530b4) A(s_slot_00553150,0x553150) A(s_slot_00553228,0x553228)
A(s_slot_0055322c,0x55322c) A(s_slot_00553238,0x553238) A(s_slot_00553240,0x553240) A(s_slot_00553268,0x553268) A(s_slot_00553270,0x553270)
A(s_slot_00553274,0x553274) A(s_slot_005532a0,0x5532a0) A(s_slot_005533c0,0x5533c0) A(s_slot_005533d0,0x5533d0) A(s_slot_00553400,0x553400)
A(s_slot_00553528,0x553528) A(s_slot_00553540,0x553540) A(s_slot_00553c58,0x553c58) A(s_slot_00553c64,0x553c64) A(s_slot_00553c70,0x553c70)
A(s_slot_00553c74,0x553c74) A(s_slot_00553d20,0x553d20) A(s_slot_00553d24,0x553d24) A(s_slot_00553d28,0x553d28) A(s_slot_00553e64,0x553e64)
A(s_slot_00553e74,0x553e74) A(s_slot_00553edc,0x553edc) A(s_slot_00553ee4,0x553ee4) A(s_slot_00553ef0,0x553ef0) A(s_slot_00553efc,0x553efc)
A(s_slot_00553f28,0x553f28) A(s_slot_00553f30,0x553f30) A(s_slot_00553f34,0x553f34) A(s_slot_00553f38,0x553f38) A(s_slot_00553f3c,0x553f3c)
A(s_slot_00553f50,0x553f50) A(s_slot_00553f74,0x553f74) A(s_slot_00553f98,0x553f98) A(s_slot_00553f9c,0x553f9c) A(s_slot_00553fb0,0x553fb0)
A(s_slot_00553fbc,0x553fbc) A(s_slot_00553fc0,0x553fc0) A(s_slot_00553ff0,0x553ff0) A(s_slot_00554004,0x554004) A(s_slot_0055401c,0x55401c)
A(s_slot_00554020,0x554020) A(s_slot_00554030,0x554030) A(s_slot_00554034,0x554034) A(s_slot_00554094,0x554094) A(s_slot_00554180,0x554180)
A(s_slot_00554470,0x554470) A(s_slot_0055447c,0x55447c) A(s_slot_0055cd40,0x55cd40) A(s_slot_0055cd5c,0x55cd5c) A(s_slot_0055cd64,0x55cd64)
A(s_slot_0055cd74,0x55cd74) A(s_slot_0055cd90,0x55cd90) A(s_slot_0055ce50,0x55ce50) A(s_slot_0055ce70,0x55ce70) A(s_slot_0055ce7c,0x55ce7c)
A(s_slot_0055ce80,0x55ce80) A(s_slot_005651b4,0x5651b4) A(s_slot_005651b8,0x5651b8) A(s_slot_005651c4,0x5651c4) A(s_slot_005653f4,0x5653f4)
A(s_slot_005653f8,0x5653f8) A(s_slot_00565404,0x565404) A(s_slot_00565410,0x565410) A(s_slot_00566514,0x566514) A(s_slot_0056651c,0x56651c)
A(s_slot_005678a0,0x5678a0) A(s_slot_00567958,0x567958) A(s_slot_00567960,0x567960) A(s_slot_00567970,0x567970) A(s_slot_00567d10,0x567d10)
A(s_slot_005681d8,0x5681d8) A(s_slot_005681ec,0x5681ec) A(s_slot_00568210,0x568210) A(s_slot_00568218,0x568218) A(s_slot_0056822c,0x56822c)
A(s_slot_00568234,0x568234) A(s_slot_00568420,0x568420) A(s_slot_00568470,0x568470) A(s_slot_00568934,0x568934) A(s_slot_00568938,0x568938)
A(s_slot_00568964,0x568964) A(s_slot_00568968,0x568968) A(s_slot_0056897c,0x56897c) A(s_slot_005689a8,0x5689a8) A(s_slot_00568da0,0x568da0)
A(s_slot_00568da4,0x568da4) A(s_slot_00568da8,0x568da8) A(s_slot_00569504,0x569504) A(s_slot_00569518,0x569518) A(s_slot_00569584,0x569584)
A(s_slot_00569588,0x569588) A(s_slot_0056d590,0x56d590) A(s_slot_0056d5a4,0x56d5a4) A(s_slot_0056d5bc,0x56d5bc) A(s_slot_0056d5c0,0x56d5c0)
A(s_slot_0056d5cc,0x56d5cc) A(s_slot_0056d5d8,0x56d5d8) A(s_slot_0056d5dc,0x56d5dc) A(s_slot_0056d5e0,0x56d5e0) A(s_slot_0056d624,0x56d624)
A(s_slot_0056d634,0x56d634) A(s_slot_0056d644,0x56d644) A(s_slot_0056d648,0x56d648) A(s_slot_0056d6a8,0x56d6a8) A(s_slot_0056d6ac,0x56d6ac)
A(s_slot_0056d6b0,0x56d6b0) A(s_slot_0056d89c,0x56d89c) A(s_slot_0056d8a4,0x56d8a4) A(s_slot_0056d8ac,0x56d8ac) A(s_slot_0057a0c4,0x57a0c4)
A(s_slot_0057a0d8,0x57a0d8) A(s_slot_0057a0f0,0x57a0f0) A(s_slot_0057a0f8,0x57a0f8) A(s_slot_0057c8cc,0x57c8cc) A(s_slot_0057c8fc,0x57c8fc)
A(s_slot_0057c900,0x57c900) A(s_slot_0057c910,0x57c910) A(s_slot_0057c918,0x57c918) A(s_slot_0057c924,0x57c924) A(s_slot_0057c92c,0x57c92c)
A(s_slot_0057c934,0x57c934) A(s_slot_0057c93c,0x57c93c) A(s_slot_0057c944,0x57c944) A(s_slot_0057c980,0x57c980) A(s_slot_0057c990,0x57c990)
A(s_slot_0057cad4,0x57cad4) A(s_slot_0057cae0,0x57cae0) A(s_slot_0057cb18,0x57cb18) A(s_slot_0057d33c,0x57d33c) A(s_slot_0058fe08,0x58fe08)
A(s_slot_0058fe28,0x58fe28)
);
#undef A
#include "save_schema_table.h"   /* g_save_schema: the port's (ptr, size) table — checked against retail's */

/* ---------------------------------------------------------------- binding: runtime pointer seams */
/* engine_dirty_rect: retail keeps the dirty rectangle in six scattered globals (engine_dirty_rect.h) */
static engine_dirty_rect_state_t s_dirty;
void rh_combat_bind_seams(void);   /* rh_hooks_combat.c */
void rh_battle_bind_seams(void);   /* rh_hooks_battle.c */
void rh_economy_bind_seams(void);  /* rh_hooks_economy.c */
void rh_bf_bind_seams(void);       /* rh_hooks_bf.c */
static void (*s_binds[32])(void);
static int s_nbinds;
void rh_port_register_bind(void (*fn)(void)) { if (s_nbinds < 32) s_binds[s_nbinds++] = fn; }
void rh_port_bind_once(void)
{
    static int done;
    if (done) return;
    done = 1;
    const uint32_t *rs = (const uint32_t *)(uintptr_t)0x4de960u;   /* retail DAT_004de960: (ptr, size) × 225 */
    for (int i = 0; i < SAVE_SCHEMA_COUNT; i++)
        if (rs[2 * i] != (uint32_t)(uintptr_t)g_save_schema[i].ptr || rs[2 * i + 1] != g_save_schema[i].size)
            rh_fatal("rh_port: save-schema entry %d: retail %08x+%u, port alias %08x+%u", i, rs[2 * i], rs[2 * i + 1],
                     (uint32_t)(uintptr_t)g_save_schema[i].ptr, g_save_schema[i].size);
    /* engine_game_init_bind_runtime_tables, minus what the port derives itself (strat cost) */
    engine_army_economy_bind_army_records(g_per_army_768);
    engine_army_economy_bind_cell_roster((const int32_t *)g_misc_1360);
    engine_army_economy_bind_cells(g_overworld_map);
    engine_army_economy_bind_resource_pool(g_player_strategic_352 + 0x120);
    engine_territory_bind_records(g_per_territory_420);
    engine_territory_bind_cells(g_overworld_map);
    engine_territory_bind_cell_roster((int32_t *)g_misc_1360);
    /* the AI score table DAT_0053fa40 lives INSIDE the county records (+0x90, 0x180 i16 a row): unbound, the
     * score clear of FUN_0046d9da was a silent no-op under rhost (move_decay_cell diverged in bot3cen-s207/209) */
    engine_territory_bind_cell_score_table((int16_t *)(g_per_army_768 + 0x90));
    engine_territory_bind_terrain_tiles((int32_t *)(uintptr_t)0x55cea0u);   /* DAT_0055cea0: 129×65 tile grid */
    engine_territory_bind_army_records(g_per_army_768);
    engine_map_query_bind_cell_descriptors(g_overworld_map);
    engine_army_state_bind_cells(g_overworld_map);
    engine_territory_bind_cursor_cache_flag((int *)(uintptr_t)0x57cac4u);   /* DAT_0057cac4 */
    engine_territory_set_dirty_state(&s_dirty);                           /* synced: six scattered globals */
    engine_path_field_bind_terrain((const int16_t *)(uintptr_t)0x4f3ffeu);  /* the strategic move-cost grid */
    engine_path_field_bind_storage((int16_t *)(uintptr_t)0x500c30u, (int16_t *)(uintptr_t)0x4f0380u,
                                   (int32_t *)(uintptr_t)0x4f9820u, (int32_t *)(uintptr_t)0x503020u,
                                   (uint8_t *)(uintptr_t)0x53f070u, (uint8_t *)(uintptr_t)0x553550u);
    engine_army_move_bind_cells(g_overworld_map);
    rh_combat_bind_seams();
    rh_battle_bind_seams();
    rh_economy_bind_seams();
    rh_bf_bind_seams();
    for (int i = 0; i < s_nbinds; i++) s_binds[i]();   /* the tables' own binds (mapview, gfx, screens …) */
    rh_log("rh_port: %d save-schema aliases match retail's table; seams bound (%d table binds)\n", SAVE_SCHEMA_COUNT, s_nbinds);
}

/* ---------------------------------------------------------------- syncing: file-static mirrors */
/* file-static mirrors: engine_army_economy config scalars + eval / supply scratch (setters), engine_path's distance
 * globals, engine_map_query's cell cursor, engine_territory's current slot + dirty rect, engine_army_ai's ranking cookie, engine_path_field's bookkeeping,
 * engine_army_state's record cursor + abort flag + live count + raider cursor (engine_combat's runtime globals are aliased:
 * rh_hooks_combat.c) */
/* the mirrored VAs, and their values when sync_in loaded the mirrors: sync_out stores a mirror only when the PORT
 * changed it (PUTM), so a port module that writes the same retail global directly (an alias: the battlefield port's
 * g_bf_<va>, src/engine_bf_globals.h) is not overwritten by a stale mirror */
static const uint32_t k_mirror_va[] = {
    0x55cd6cu, 0x5656f0u, 0x57c904u, 0x56d628u, 0x591560u, 0x58fea0u, 0x569570u, 0x569578u, 0x52afe0u, 0x56d6a0u,
    0x50d09cu, 0x50a4c4u, 0x50a4c8u, 0x50a4bcu, 0x50a4c0u, 0x53e9acu, 0x504020u, 0x504024u, 0x4f4070u, 0x500c20u,
    0x4f2768u, 0x507230u, 0x507234u, 0x5533e8u, 0x50a49eu, 0x553f08u, 0x4e59dcu,
};
static uint32_t s_mirror_in[sizeof k_mirror_va / sizeof *k_mirror_va];
static int mirror_changed(uint32_t va, uint32_t v, uint32_t mask)
{
    for (int k = 0; k < (int)(sizeof k_mirror_va / sizeof *k_mirror_va); k++)
        if (k_mirror_va[k] == va) return (v & mask) != (s_mirror_in[k] & mask);
    return 1;
}
#define PUTM(va, v)  do { uint32_t m_ = (uint32_t)(v); if (mirror_changed(va, m_, 0xffffffffu)) PUT(va, m_); } while (0)
#define PUTM8(va, v) do { uint8_t m_ = (uint8_t)(v); if (mirror_changed(va, m_, 0xffu)) PUT8(va, m_); } while (0)
void rh_port_sync_in(void)
{
    engine_army_economy_set_prices((int)G32(0x553f60u), (int)G32(0x567594u), (int)G32(0x57cb30u));
    engine_army_economy_set_production_tuning((int)G32(0x57c8e0u), (int)G32(0x552ffcu), (int)G32(0x57d34cu), (int)G32(0x5533bcu));
    engine_army_economy_set_yield_divisors((int)G32(0x553538u), (int)G32(0x553218u));
    engine_army_economy_set_eval_scratch((int)G32(0x55cd6cu), (int)G32(0x5656f0u));
    engine_army_economy_set_supply_pools((int)G32(0x57c904u), (int)G32(0x56d628u));
    g_path_dist_dx = (int32_t)G32(0x591560u); g_path_dist_dy = (int32_t)G32(0x58fea0u);   /* engine_path globals */
    g_cell_cursor_col = (int32_t)G32(0x569570u); g_cell_cursor_row = (int32_t)G32(0x569578u); /* engine_map_query */
    engine_territory_set_current_slot((int)G32(0x52afe0u));
    engine_army_ai_set_ranking_dirty((int)G32(0x56d6a0u));
    s_dirty.dirty_mark = (int)G32(0x50d09cu); s_dirty.dirty_xmin = (int)G32(0x50a4c4u); s_dirty.dirty_xmax = (int)G32(0x50a4c8u);
    s_dirty.dirty_ymin = (int)G32(0x50a4bcu); s_dirty.dirty_ymax = (int)G32(0x50a4c0u); s_dirty.below_status_redraw = (int)G32(0x53e9acu);
    g_pf_src_col = (int32_t)G32(0x504020u); g_pf_src_row = (int32_t)G32(0x504024u); g_pf_src_cell = (int32_t)G32(0x4f4070u);
    g_pf_head = (int32_t)G32(0x500c20u); g_pf_tail = (int32_t)G32(0x4f2768u);
    g_pf_cur_col = (int32_t)G32(0x507230u); g_pf_cur_row = (int32_t)G32(0x507234u);
    g_army_state_index = (int)G32(0x5533e8u); g_army_state_abort = G8(0x50a49eu);
    g_army_state_live_count = (int)G32(0x553f08u); g_raider_target_cursor = (int)G32(0x4e59dcu);
    for (int k = 0; k < (int)(sizeof k_mirror_va / sizeof *k_mirror_va); k++) s_mirror_in[k] = G32(k_mirror_va[k]);
}
/* store only what changed (PUT): under the page-tracked shadow every first write to a page is a fault (~30 us
 * under wine) — sync_out alone touched ~15 pages a call */
void rh_port_sync_out(void)
{
    PUTM(0x55cd6cu, (uint32_t)engine_army_economy_eval_cost());
    PUTM(0x5656f0u, (uint32_t)engine_army_economy_eval_revenue());
    PUTM(0x57c904u, (uint32_t)engine_army_economy_supply_pool_a());
    PUTM(0x56d628u, (uint32_t)engine_army_economy_supply_pool_b());
    PUTM(0x591560u, (uint32_t)g_path_dist_dx); PUTM(0x58fea0u, (uint32_t)g_path_dist_dy);
    PUTM(0x569570u, (uint32_t)g_cell_cursor_col); PUTM(0x569578u, (uint32_t)g_cell_cursor_row);
    PUTM(0x52afe0u, (uint32_t)engine_territory_current_slot());
    PUTM(0x56d6a0u, (uint32_t)engine_army_ai_ranking_dirty());
    PUTM(0x50d09cu, (uint32_t)s_dirty.dirty_mark); PUTM(0x50a4c4u, (uint32_t)s_dirty.dirty_xmin); PUTM(0x50a4c8u, (uint32_t)s_dirty.dirty_xmax);
    PUTM(0x50a4bcu, (uint32_t)s_dirty.dirty_ymin); PUTM(0x50a4c0u, (uint32_t)s_dirty.dirty_ymax); PUTM(0x53e9acu, (uint32_t)s_dirty.below_status_redraw);
    PUTM(0x504020u, (uint32_t)g_pf_src_col); PUTM(0x504024u, (uint32_t)g_pf_src_row); PUTM(0x4f4070u, (uint32_t)g_pf_src_cell);
    PUTM(0x500c20u, (uint32_t)g_pf_head); PUTM(0x4f2768u, (uint32_t)g_pf_tail);
    PUTM(0x507230u, (uint32_t)g_pf_cur_col); PUTM(0x507234u, (uint32_t)g_pf_cur_row);
    PUTM(0x5533e8u, (uint32_t)g_army_state_index); PUTM8(0x50a49eu, g_army_state_abort);
    PUTM(0x553f08u, (uint32_t)g_army_state_live_count); PUTM(0x4e59dcu, (uint32_t)g_raider_target_cursor);
}

/* engine_event_queue: the port's queue is one struct; retail's is six globals (FUN_00472bc5 / FUN_00472ae0).
 * Functions that post take a queue: the adapter loads it from retail, calls, and stores it back. */
static engine_event_queue_state_t s_evq;
static int s_evq_open;   /* an EVQ row is running: a call back into retail stores / reloads the queue too */
engine_event_queue_state_t *rh_evq_load(void)
{
    s_evq_open = 1;
    memcpy(&s_evq.scratch, (const void *)(uintptr_t)0x553510u, sizeof s_evq.scratch);   /* DAT_00553510: 24 B staging */
    memcpy(s_evq.ring, (const void *)(uintptr_t)0x568480u, sizeof s_evq.ring);           /* DAT_00568480: 50 × 24 B */
    s_evq.write_idx = (int)G32(0x543fd0u); s_evq.read_idx = (int)G32(0x553ec4u);
    s_evq.notify_flag = (int)G32(0x565484u); s_evq.consumer_cooldown = (int)G32(0x57c954u);
    return &s_evq;
}
void rh_evq_store(void)
{
    s_evq_open = 0;
    if (memcmp((void *)(uintptr_t)0x553510u, &s_evq.scratch, sizeof s_evq.scratch))
        memcpy((void *)(uintptr_t)0x553510u, &s_evq.scratch, sizeof s_evq.scratch);
    if (memcmp((void *)(uintptr_t)0x568480u, s_evq.ring, sizeof s_evq.ring))
        memcpy((void *)(uintptr_t)0x568480u, s_evq.ring, sizeof s_evq.ring);
    PUT(0x543fd0u, (uint32_t)s_evq.write_idx); PUT(0x553ec4u, (uint32_t)s_evq.read_idx);
    PUT(0x565484u, (uint32_t)s_evq.notify_flag); PUT(0x57c954u, (uint32_t)s_evq.consumer_cooldown);
}
void rh_port_retail_enter(void)
{
    rh_port_sync_out();
    if (s_evq_open) { rh_evq_store(); s_evq_open = 1; }
}
void rh_port_retail_leave(void)
{
    if (s_evq_open) rh_evq_load();
    rh_port_sync_in();
}

/* ---------------------------------------------------------------- the generic wrapper body */
/* the retail side, at the caller's own stack position: esp = e0 + 4 (its pushed arguments), `call` puts the
 * return address at e0 — the frame retail would have had; the caller's return address is put back after */
uint32_t rh_c_floor;   /* esp of the hook machinery that is running retail through rh_call_at (0: none) */
/* the retail caller's ebp at the wrapper's entry: retail's side runs with it, as in a pure run — its prologue's
 * `push ebp` then leaves the caller's frame pointer in the slot below e0 (residue a later call may read), not the
 * hook machinery's */
uint32_t rh_entry_ebp;
__attribute__((naked)) uint32_t rh_call_at(uint32_t va, uint32_t e0, uint32_t ebp0)
{
    __asm__("pushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
            "pushl _rh_c_floor\n\t"
            "movl 12(%ebp), %edi\n\t"       /* e0 */
            "movl (%edi), %ebx\n\t"         /* the caller's return address */
            "movl %esp, %esi\n\t"
            "movl %esp, _rh_c_floor\n\t"
            "movl 8(%ebp), %ecx\n\t"
            "movl 16(%ebp), %eax\n\t"       /* the caller's ebp */
            "leal 4(%edi), %esp\n\t"
            "movl %eax, %ebp\n\t"
            "call *%ecx\n\t"
            "movl %esi, %esp\n\t"
            "movl %ebx, (%edi)\n\t"
            "popl _rh_c_floor\n\t"
            "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret");
}

#define RES_N 16   /* residue window: the 16 dwords below e0 */
#define PUT_N 8    /* residue values one port call may put */
typedef struct port_ctx {
    hook_t *h; uint32_t e0, ebp0; uint32_t a[10]; rh_port_fn port; uint32_t ret_r, ret_p;
    uint32_t res[RES_N]; int nput; int put_off[PUT_N]; uint32_t put_v[PUT_N]; int put_cmp[PUT_N];
    struct port_ctx *outer;
} port_ctx_t;
static port_ctx_t *g_cur;   /* the port side running now (adapters read / write the residue through it) */

uint32_t rh_res_get(int off)
{
    if (!g_cur || off < 4 || off > 4 * RES_N || (off & 3)) rh_fatal("rh_res_get(0x%x): no hook / bad offset", off);
    return g_cur->res[RES_N - off / 4];
}
static void res_put(int off, uint32_t v, int cmp)
{
    if (!g_cur || off < 4 || off > 4 * RES_N || (off & 3) || g_cur->nput == PUT_N) rh_fatal("rh_res_put(0x%x): no hook / bad offset", off);
    g_cur->put_off[g_cur->nput] = off; g_cur->put_cmp[g_cur->nput] = cmp; g_cur->put_v[g_cur->nput++] = v;
}
void rh_res_put(int off, uint32_t v) { res_put(off, v, 1); }
void rh_res_set(int off, uint32_t v) { res_put(off, v, 0); }

static void ctx_retail(void *c)
{
    port_ctx_t *x = c;
    x->ret_r = rh_call_at(x->h->entry, x->e0, x->ebp0);
}
static void ctx_port(void *c)
{
    port_ctx_t *x = c;
    rh_port_bind_once();
    rh_port_sync_in();
    x->nput = 0; x->outer = g_cur; g_cur = x;
    x->ret_p = x->port(x->a);
    g_cur = x->outer;
    rh_port_sync_out();
}
uint32_t rh_port_run(hook_t *h, void *self, rh_port_fn port, int has_ret, uint32_t e0)
{
    if (h->wrapper != self) rh_fatal("rh_port: row %s is not its wrapper's — enum and table out of order", h->name);
    port_ctx_t x = { .h = h, .e0 = e0, .ebp0 = rh_entry_ebp, .port = port };   /* first: a nested hook overwrites it */
    memcpy(x.a, (const void *)(uintptr_t)(e0 + 4), 10 * 4);             /* retail may reuse its argument slots */
    memcpy(x.res, (const void *)(uintptr_t)(e0 - 4 * RES_N), sizeof x.res);
    if (h->mode == H_REPLACE) {
        h->calls++; ctx_port(&x);
        for (int i = 0; i < x.nput; i++) G32(e0 - (uint32_t)x.put_off[i]) = x.put_v[i];
        return x.ret_p;
    }
    if (rh_in_shadow()) { ctx_retail(&x); return x.ret_r; }
    if (!rh_hook_sample(h, G32(e0))) { ctx_retail(&x); return x.ret_r; }   /* RH_SH_SAMPLE rows: not this call */
    char why[400];
    int r = rh_shadow_ex_f(h->name, ctx_retail, ctx_port, &x, why, sizeof why, h->flags);   /* rolls the host layers too */
    if (r >= 0) {   /* the return value and the residue the port models: retail's are the reference */
        size_t n = r ? strlen(why) : (size_t)snprintf(why, sizeof why, "%s:", h->name);
        if (has_ret && x.ret_r != x.ret_p && n < sizeof why) {
            n += (size_t)snprintf(why + n, sizeof why - n, " ret r%08x/p%08x", x.ret_r, x.ret_p);
            r = 1;
        }
        for (int i = 0; i < x.nput; i++) {
            uint32_t rv = G32(e0 - (uint32_t)x.put_off[i]);
            if (x.put_cmp[i] && rv != x.put_v[i] && n < sizeof why) {
                n += (size_t)snprintf(why + n, sizeof why - n, " res e0-%x r%08x/p%08x", x.put_off[i], rv, x.put_v[i]);
                r = 1;
            }
        }
    }
    rh_hook_count(h, r, why);
    return x.ret_r;
}

#define RH_TABLE RH_HOOKS_AI
extern hook_t RH_HOOKS_AI[];

/* row indices (must follow RH_HOOKS_AI's order) */
enum {
    X_eco_stock_a_value, X_eco_units_b, X_eco_units_c, X_eco_appraisal_value, X_eco_stockpile_value,
    X_eco_rate_class, X_eco_production_output, X_eco_yield_standard, X_eco_yield_phase4,
    X_ai_seed_workforce, X_ai_consume_build_demand, X_ai_first_owned_realm, X_ai_step_noop, X_ai_most_hostile,
    X_eco_eval_candidate, X_eco_select_candidate, X_eco_settle_stock_a, X_eco_plan_production, X_eco_provision,
    X_eco_plan_realm, X_eco_seed_fullest_cell, X_eco_pick_fullest_cell, X_eco_distribute_stock, X_eco_season_pass,
    X_eco_sum_supply_pools, X_eco_resource_quota, X_eco_supply_target, X_eco_plan_resource, X_eco_stamp_resource_cell,
    X_eco_apply_resource_quota, X_eco_resource_pass, X_eco_plan_secondary,
    X_ai_rebalance_workforce, X_ai_advance_tier, X_ai_execute_trade, X_ai_buy_order, X_ai_buy_order_scaled,
    X_ai_balance_high_cells, X_ai_tax_sweep, X_ai_reconcile_resources, X_ai_dispatch_compact, X_ai_dispatch_indep_compact,
    X_ai_run_player, X_ai_run_independent, X_ai_run_turn, X_ai_seed_buy_requests, X_ai_mark_build_affordable,
    X_ai_run_build_orders, X_ai_run_keep_builder, X_ai_pick_muster_realms, X_ai_set_rations_grant, X_ai_select_target,
    X_ai_launch_attack, X_ai_run_campaign, X_ai_run_garrison,
    X_realm_replan_economies, X_realm_clear_turn_scratch, X_realm_update_mood, X_realm_advance_population,
    X_realm_migrate_population, X_realm_escalate_unrest, X_realm_accrue_income, X_realm_recompute_income,
    X_realm_owner_tally, X_realm_advance_rating, X_realm_tax_approval, X_realm_settle_provisions,
    X_realm_spread_development, X_realm_complete_settlements, X_realm_recount_armies, X_realm_owns_neighbor,
    X_realm_territory_income, X_realm_record_history,
    X_ai_run_per_tick, X_ai_run_movement, X_ai_run_diplomacy, X_ai_propose_diplomacy,
    X_terr_census_terrain, X_terr_grade_cells, X_terr_allocate_workforce, X_terr_stamp_secondary_keep,
    X_rank_player_total, X_rank_player_aggregate, X_combat_muster_garrison, X_combat_spawn_unit,
    X_combat_pick_best_unit, X_unit_total_wage, X_diplo_should_engage, X_army_movement_owned,
};

/* engine_army_economy.c */
HOOK_R(eco_stock_a_value,       1, engine_army_economy_stock_a_value(I(0)))
HOOK_R(eco_units_b,             2, engine_army_economy_units_b(I(0), I(1)))
HOOK_R(eco_units_c,             2, engine_army_economy_units_c(I(0), I(1)))
HOOK_R(eco_appraisal_value,     1, engine_army_economy_appraisal_value(I(0)))
HOOK_R(eco_stockpile_value,     1, engine_army_economy_stockpile_value(I(0)))
HOOK_R(eco_rate_class,          2, engine_army_economy_rate_class(I(0), I(1)))
HOOK_R(eco_production_output,   3, engine_army_economy_production_output(I(0), I(1), I(2)))
HOOK_R(eco_yield_standard,      3, engine_army_economy_yield_standard(I(0), I(1), I(2)))
HOOK_R(eco_yield_phase4,        3, engine_army_economy_yield_phase4(I(0), I(1), I(2)))
/* engine_army_ai.c */
HOOK_V(ai_seed_workforce,       1, engine_army_ai_seed_workforce_ratios(I(0)))
HOOK_V(ai_consume_build_demand, 1, engine_army_ai_consume_build_demand(I(0)))
HOOK_R(ai_first_owned_realm,    1, engine_army_ai_first_owned_realm(I(0)))
HOOK_V(ai_step_noop,            1, engine_army_ai_step_noop())
HOOK_R(ai_most_hostile,         1, engine_army_ai_most_hostile_player(I(0)))
/* engine_army_economy.c — the realm economy model */
HOOK_V(eco_eval_candidate,      5, engine_army_economy_eval_candidate(I(0), I(1), I(2), I(3), I(4)))
HOOK_V(eco_select_candidate,    2, engine_army_economy_select_candidate(I(0), I(1)))
HOOK_V(eco_settle_stock_a,      0, engine_army_economy_settle_stock_a())
HOOK_V(eco_plan_production,     2, engine_army_economy_plan_production(I(0), I(1)))
HOOK_V(eco_provision,           2, engine_army_economy_provision(I(0), I(1)))
HOOK_V(eco_plan_realm,          2, engine_army_economy_plan_realm(I(0), I(1)))
HOOK_V(eco_seed_fullest_cell,   1, engine_army_economy_seed_fullest_cell(I(0)))
HOOK_V(eco_pick_fullest_cell,   1, engine_army_economy_pick_fullest_cell(I(0)))
HOOK_R(eco_distribute_stock,    1, engine_army_economy_distribute_stock(I(0)))
HOOK_V(eco_season_pass,         0, engine_army_economy_apply_season_pass())
HOOK_V(eco_sum_supply_pools,    1, engine_army_economy_sum_supply_pools(I(0)))
HOOK_R(eco_resource_quota,      4, engine_army_economy_resource_quota(I(0), I(1), I(2), I(3)))
HOOK_R(eco_supply_target,       4, engine_army_economy_supply_target(I(0), I(1), I(2), I(3)))
HOOK_V(eco_plan_resource,       5, engine_army_economy_plan_resource(I(0), I(1), I(2), I(3), I(4)))
HOOK_V(eco_stamp_resource_cell, 2, engine_army_economy_stamp_resource_cell(I(0), I(1)))
HOOK_V(eco_apply_resource_quota, 5, engine_army_economy_apply_resource_quota(I(0), I(1), I(2), I(3), I(4)))
HOOK_V(eco_resource_pass,       0, engine_army_economy_resource_pass())
HOOK_V(eco_plan_secondary,      1, engine_army_economy_plan_secondary(I(0)))
/* engine_army_ai.c — trade executor, personality dispatchers, per-player brain steps */
HOOK_V(ai_rebalance_workforce,  3, engine_army_ai_rebalance_workforce(I(0), I(1), I(2)))
HOOK_V(ai_advance_tier,         2, engine_army_ai_advance_tier(I(0), I(1)))
HOOK_V(ai_execute_trade,        7, engine_army_ai_execute_trade(I(0), I(1), I(2), I(3), I(4), I(5), I(6)))
HOOK_V(ai_buy_order,            3, engine_army_ai_buy_order(I(0), I(1), I(2)))
HOOK_V(ai_buy_order_scaled,     3, engine_army_ai_buy_order_scaled(I(0), I(1), I(2)))
HOOK_V(ai_balance_high_cells,   2, engine_army_ai_balance_high_cells(I(0), I(1)))
HOOK_V(ai_tax_sweep,            2, engine_army_ai_tax_sweep(I(0), I(1)))
HOOK_V(ai_reconcile_resources,  2, engine_army_ai_reconcile_resources(I(0), I(1)))
HOOK_V(ai_dispatch_compact,     1, engine_army_ai_dispatch_compact(I(0)))
HOOK_V(ai_dispatch_indep_compact, 1, engine_army_ai_dispatch_independent_compact(I(0)))
HOOK_V(ai_run_player,           1, engine_army_ai_run_player(I(0)))
HOOK_V(ai_run_independent,      1, engine_army_ai_run_independent(I(0)))
HOOK_V(ai_run_turn,             0, engine_army_ai_run_turn())
HOOK_V(ai_seed_buy_requests,    1, engine_army_ai_seed_buy_requests(I(0)))
HOOK_V(ai_mark_build_affordable, 2, engine_army_ai_mark_build_affordable(I(0), I(1)))
HOOK_V(ai_run_build_orders,     1, engine_army_ai_run_build_orders(I(0)))
HOOK_V(ai_run_keep_builder,     1, engine_army_ai_run_keep_builder(I(0)))
HOOK_V(ai_pick_muster_realms,   1, engine_army_ai_pick_muster_realms(I(0)))
HOOK_V(ai_set_rations_grant,    1, engine_army_ai_set_rations_and_grant(I(0)))
HOOK_R(ai_select_target,        4, engine_army_ai_select_target(I(0), I(1), I(2), I(3)))
HOOK_V(ai_launch_attack,        1, engine_army_ai_launch_attack(I(0)))
HOOK_V(ai_run_campaign,         1, engine_army_ai_run_campaign(I(0)))
HOOK_V(ai_run_garrison,         1, engine_army_ai_run_garrison(I(0)))
/* engine_realm.c — the turn's realm passes (retail reads the RNG bytes / queue from globals) */
HOOK_V(realm_replan_economies,  0, engine_realm_replan_economies())
HOOK_V(realm_clear_turn_scratch, 0, engine_realm_clear_turn_scratch())
HOOK_V(realm_update_mood,       0, EVQ(engine_realm_update_mood(G32(0x58fd60u), G32(0x58fd7cu), q)))
HOOK_V(realm_advance_population, 0, engine_realm_advance_population())
HOOK_V(realm_migrate_population, 0, engine_realm_migrate_population())
HOOK_V(realm_escalate_unrest,   0, EVQ(engine_realm_escalate_unrest(q, MP_ACTIVE)))
HOOK_V(realm_accrue_income,     0, engine_realm_accrue_income())
HOOK_V(realm_recompute_income,  1, engine_realm_recompute_realm_income(I(0)))
HOOK_V(realm_owner_tally,       1, engine_realm_recompute_owner_tally(I(0)))
HOOK_V(realm_advance_rating,    0, engine_realm_advance_rating())
HOOK_V(realm_tax_approval,      0, engine_realm_apply_tax_approval())
HOOK_V(realm_settle_provisions, 0, engine_realm_settle_provisions())
HOOK_V(realm_spread_development, 0, engine_realm_spread_development())
HOOK_V(realm_complete_settlements, 0, EVQ(engine_realm_complete_settlements(q)))
HOOK_V(realm_recount_armies,    0, engine_realm_recount_armies())
HOOK_R(realm_owns_neighbor,     2, engine_realm_player_owns_neighbor((unsigned)a[0], I(1)))
HOOK_V(realm_territory_income,  0, engine_realm_collect_territory_income())
HOOK_V(realm_record_history,    0, engine_realm_record_history())
/* engine_army_ai.c — the per-tick brain FSM and its queue-posting steps (the brain's RNG byte is the global's value) */
HOOK_V(ai_run_per_tick,         0, EVQ(engine_army_ai_run_per_tick(q, MP_ACTIVE, G32(0x58fd60u))))
HOOK_V(ai_run_movement,         1, EVQ(engine_army_ai_run_movement(I(0), q, MP_ACTIVE)))
HOOK_V(ai_run_diplomacy,        1, EVQ(engine_army_ai_run_diplomacy(I(0), q)))
HOOK_V(ai_propose_diplomacy,    1, EVQ(engine_army_ai_propose_diplomacy(I(0), q)))
/* the AI turn's callees in the neighbouring modules (territory, ranking, combat, upkeep, diplomacy, army state) */
HOOK_V(terr_census_terrain,     1, engine_territory_census_army_terrain(I(0)))
HOOK_V(terr_grade_cells,        1, engine_territory_grade_feature_cells(I(0)))
HOOK_V(terr_allocate_workforce, 1, engine_territory_allocate_realm_workforce(I(0)))
HOOK_V(terr_stamp_secondary_keep, 2, engine_territory_stamp_secondary_keep(I(0), I(1)))
HOOK_V(rank_player_total,       1, engine_ranking_recompute_player_total(I(0)))
HOOK_V(rank_player_aggregate,   1, engine_ranking_recompute_player_aggregate(a[0]))
HOOK_R(combat_muster_garrison,  2, engine_combat_muster_garrison(I(0), I(1)))
HOOK_R(combat_spawn_unit,       4, engine_combat_spawn_unit(I(0), I(1), I(2), I(3)))
HOOK_R(combat_pick_best_unit,   3, engine_combat_pick_best_unit(I(0), I(1), I(2)))
HOOK_R(unit_total_wage,         1, engine_unit_total_wage(I(0)))
HOOK_R(diplo_should_engage,     2, engine_diplomacy_should_engage(I(0), I(1)))
HOOK_R(army_movement_owned,     2, engine_army_state_arm_movement_owned(a[0], I(1)))

hook_t RH_HOOKS_AI[] = {
    { "eco_stock_a_value", 0x44e5bbu, (void *)w_eco_stock_a_value, 0, 0, 0, 0, "" },
    { "eco_units_b", 0x44e603u, (void *)w_eco_units_b, 0, 0, 0, 0, "" },
    { "eco_units_c", 0x44e653u, (void *)w_eco_units_c, 0, 0, 0, 0, "" },
    { "eco_appraisal_value", 0x44e6a3u, (void *)w_eco_appraisal_value, 0, 0, 0, 0, "" },
    { "eco_stockpile_value", 0x44e7b4u, (void *)w_eco_stockpile_value, 0, 0, 0, 0, "" },
    { "eco_rate_class", 0x44cf6fu, (void *)w_eco_rate_class, 0, 0, 0, 0, "" },
    { "eco_production_output", 0x44cfe1u, (void *)w_eco_production_output, 0, 0, 0, 0, "" },
    { "eco_yield_standard", 0x44d15au, (void *)w_eco_yield_standard, 0, 0, 0, 0, "" },
    { "eco_yield_phase4", 0x44d1e5u, (void *)w_eco_yield_phase4, 0, 0, 0, 0, "" },
    { "ai_seed_workforce", 0x45158bu, (void *)w_ai_seed_workforce, 0, 0, 0, 0, "" },
    { "ai_consume_build_demand", 0x450ccdu, (void *)w_ai_consume_build_demand, 0, 0, 0, 0, "" },
    { "ai_first_owned_realm", 0x49d5e0u, (void *)w_ai_first_owned_realm, 0, 0, 0, 0, "" },
    { "ai_step_noop", 0x49f96cu, (void *)w_ai_step_noop, 0, 0, 0, 0, "" },
    { "ai_most_hostile", 0x4a0309u, (void *)w_ai_most_hostile, 0, 0, 0, 0, "" },
    { "eco_eval_candidate", 0x44da99u, (void *)w_eco_eval_candidate, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },   /* ~130k calls in one frame of a custom game start (custom-tour f3388): shadowed every call, that frame passed the 60 s stall watchdog */
    { "eco_select_candidate", 0x44dd4du, (void *)w_eco_select_candidate, 0, 0, 0, 0, "" },
    { "eco_settle_stock_a", 0x44d60du, (void *)w_eco_settle_stock_a, 0, 0, 0, 0, "" },
    { "eco_plan_production", 0x44d374u, (void *)w_eco_plan_production, 0, 0, 0, 0, "" },
    { "eco_provision", 0x44df5fu, (void *)w_eco_provision, 0, 0, 0, 0, "" },
    { "eco_plan_realm", 0x4485a5u, (void *)w_eco_plan_realm, 0, 0, 0, 0, "" },
    { "eco_seed_fullest_cell", 0x44c5fcu, (void *)w_eco_seed_fullest_cell, 0, 0, 0, 0, "" },
    { "eco_pick_fullest_cell", 0x44c53bu, (void *)w_eco_pick_fullest_cell, 0, 0, 0, 0, "" },
    { "eco_distribute_stock", 0x44c278u, (void *)w_eco_distribute_stock, 0, 0, 0, 0, "" },
    { "eco_season_pass", 0x44c8aeu, (void *)w_eco_season_pass, 0, 0, 0, 0, "" },
    { "eco_sum_supply_pools", 0x44f15bu, (void *)w_eco_sum_supply_pools, 0, 0, 0, 0, "" },
    { "eco_resource_quota", 0x44ef4eu, (void *)w_eco_resource_quota, 0, 0, 0, 0, "" },
    { "eco_supply_target", 0x44f248u, (void *)w_eco_supply_target, 0, 0, 0, 0, "" },
    { "eco_plan_resource", 0x44f318u, (void *)w_eco_plan_resource, 0, 0, 0, 0, "" },
    { "eco_stamp_resource_cell", 0x44edc2u, (void *)w_eco_stamp_resource_cell, 0, 0, 0, 0, "" },
    { "eco_apply_resource_quota", 0x44ea92u, (void *)w_eco_apply_resource_quota, 0, 0, 0, 0, "" },
    { "eco_resource_pass", 0x44e852u, (void *)w_eco_resource_pass, 0, 0, 0, 0, "" },
    { "eco_plan_secondary", 0x450e46u, (void *)w_eco_plan_secondary, 0, 0, 0, 0, "" },
    { "ai_rebalance_workforce", 0x4502cau, (void *)w_ai_rebalance_workforce, 0, 0, 0, 0, "" },
    { "ai_advance_tier", 0x428c42u, (void *)w_ai_advance_tier, 0, 0, 0, 0, "" },
    { "ai_execute_trade", 0x4284ceu, (void *)w_ai_execute_trade, 0, 0, 0, 0, "" },
    { "ai_buy_order", 0x4a4b12u, (void *)w_ai_buy_order, 0, 0, 0, 0, "" },
    { "ai_buy_order_scaled", 0x4a4c41u, (void *)w_ai_buy_order_scaled, 0, 0, 0, 0, "" },
    { "ai_balance_high_cells", 0x4a4694u, (void *)w_ai_balance_high_cells, 0, 0, 0, 0, "" },
    { "ai_tax_sweep", 0x4a4782u, (void *)w_ai_tax_sweep, 0, 0, 0, 0, "" },
    { "ai_reconcile_resources", 0x49e39bu, (void *)w_ai_reconcile_resources, 0, 0, 0, 0, "" },
    { "ai_dispatch_compact", 0x4a42e3u, (void *)w_ai_dispatch_compact, 0, 0, 0, 0, "" },
    { "ai_dispatch_indep_compact", 0x4a3ed3u, (void *)w_ai_dispatch_indep_compact, 0, 0, 0, 0, "" },
    { "ai_run_player", 0x49dd01u, (void *)w_ai_run_player, 0, 0, 0, 0, "" },
    { "ai_run_independent", 0x49dfc6u, (void *)w_ai_run_independent, 0, 0, 0, 0, "" },
    { "ai_run_turn", 0x49a990u, (void *)w_ai_run_turn, 0, 0, 0, 0, "" },
    { "ai_seed_buy_requests", 0x49e1bfu, (void *)w_ai_seed_buy_requests, 0, 0, 0, 0, "" },
    { "ai_mark_build_affordable", 0x49ed13u, (void *)w_ai_mark_build_affordable, 0, 0, 0, 0, "" },
    { "ai_run_build_orders", 0x49e77du, (void *)w_ai_run_build_orders, 0, 0, 0, 0, "" },
    { "ai_run_keep_builder", 0x49edc7u, (void *)w_ai_run_keep_builder, 0, 0, 0, 0, "" },
    { "ai_pick_muster_realms", 0x4a0aaau, (void *)w_ai_pick_muster_realms, 0, 0, 0, 0, "" },
    { "ai_set_rations_grant", 0x49d638u, (void *)w_ai_set_rations_grant, 0, 0, 0, 0, "" },
    { "ai_select_target", 0x4a03f2u, (void *)w_ai_select_target, 0, 0, 0, 0, "" },
    { "ai_launch_attack", 0x4a0015u, (void *)w_ai_launch_attack, 0, 0, 0, 0, "" },
    { "ai_run_campaign", 0x49f977u, (void *)w_ai_run_campaign, 0, 0, 0, 0, "" },
    { "ai_run_garrison", 0x49f93du, (void *)w_ai_run_garrison, 0, 0, 0, 0, "" },
    { "realm_replan_economies", 0x448545u, (void *)w_realm_replan_economies, 0, 0, 0, 0, "" },
    { "realm_clear_turn_scratch", 0x448d16u, (void *)w_realm_clear_turn_scratch, 0, 0, 0, 0, "" },
    { "realm_update_mood", 0x449889u, (void *)w_realm_update_mood, 0, 0, 0, 0, "" },
    { "realm_advance_population", 0x449ef3u, (void *)w_realm_advance_population, 0, 0, 0, 0, "" },
    { "realm_migrate_population", 0x44a6bau, (void *)w_realm_migrate_population, 0, 0, 0, 0, "" },
    { "realm_escalate_unrest", 0x44aa41u, (void *)w_realm_escalate_unrest, 0, 0, 0, 0, "" },
    { "realm_accrue_income", 0x44b59bu, (void *)w_realm_accrue_income, 0, 0, 0, 0, "" },
    { "realm_recompute_income", 0x44b80bu, (void *)w_realm_recompute_income, 0, 0, 0, 0, "" },
    { "realm_owner_tally", 0x44b99au, (void *)w_realm_owner_tally, 0, 0, 0, 0, "" },
    { "realm_advance_rating", 0x44baeau, (void *)w_realm_advance_rating, 0, 0, 0, 0, "" },
    { "realm_tax_approval", 0x44bd9fu, (void *)w_realm_tax_approval, 0, 0, 0, 0, "" },
    { "realm_settle_provisions", 0x44bf04u, (void *)w_realm_settle_provisions, 0, 0, 0, 0, "" },
    { "realm_spread_development", 0x44bf7au, (void *)w_realm_spread_development, 0, 0, 0, 0, "" },
    { "realm_complete_settlements", 0x4508deu, (void *)w_realm_complete_settlements, 0, 0, 0, 0, "" },
    { "realm_recount_armies", 0x451061u, (void *)w_realm_recount_armies, 0, 0, 0, 0, "" },
    { "realm_owns_neighbor", 0x467eb0u, (void *)w_realm_owns_neighbor, 0, 0, 0, 0, "" },
    { "realm_territory_income", 0x4ad6c0u, (void *)w_realm_territory_income, 0, 0, 0, 0, "" },
    { "realm_record_history", 0x4ae7ddu, (void *)w_realm_record_history, 0, 0, 0, 0, "" },
    { "ai_run_per_tick", 0x49a581u, (void *)w_ai_run_per_tick, 0, 0, 0, 0, "" },
    { "ai_run_movement", 0x4a5667u, (void *)w_ai_run_movement, 0, 0, 0, 0, "" },
    { "ai_run_diplomacy", 0x4a0c1du, (void *)w_ai_run_diplomacy, 0, 0, 0, 0, "" },
    { "ai_propose_diplomacy", 0x4a13a6u, (void *)w_ai_propose_diplomacy, 0, 0, 0, 0, "" },
    { "terr_census_terrain", 0x469b8du, (void *)w_terr_census_terrain, 0, 0, 0, 0, "" },
    { "terr_grade_cells", 0x44d913u, (void *)w_terr_grade_cells, 0, 0, 0, 0, "" },
    { "terr_allocate_workforce", 0x44f6e7u, (void *)w_terr_allocate_workforce, 0, 0, 0, 0, "" },
    { "terr_stamp_secondary_keep", 0x46826cu, (void *)w_terr_stamp_secondary_keep, 0, 0, 0, 0, "" },
    { "rank_player_total", 0x4487a9u, (void *)w_rank_player_total, 0, 0, 0, 0, "" },
    { "rank_player_aggregate", 0x49d1e0u, (void *)w_rank_player_aggregate, 0, 0, 0, 0, "" },
    { "combat_muster_garrison", 0x4a5389u, (void *)w_combat_muster_garrison, 0, 0, 0, 0, "" },
    { "combat_spawn_unit", 0x4a9a9au, (void *)w_combat_spawn_unit, 0, 0, 0, 0, "" },
    { "combat_pick_best_unit", 0x4a0917u, (void *)w_combat_pick_best_unit, 0, 0, 0, 0, "" },
    { "unit_total_wage", 0x4ad495u, (void *)w_unit_total_wage, 0, 0, 0, 0, "" },
    { "diplo_should_engage", 0x4a16f7u, (void *)w_diplo_should_engage, 0, 0, 0, 0, "" },
    { "army_movement_owned", 0x4a4e3du, (void *)w_army_movement_owned, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_AI_N = sizeof RH_HOOKS_AI / sizeof *RH_HOOKS_AI;

/* ---------------------------------------------------------------- bridges across the link cut (game_init.c) */
/* retail DAT_0053f6a0: the per-player troop muster pool (6 rows × 0x80) */
uint8_t *engine_game_init_troop_pool_row(int player) { return (uint8_t *)(uintptr_t)0x53f6a0u + player * 0x80; }
/* game_init.c army_near (FUN_00428007 / FUN_00428078): the same probe loop over the linked engine_map_query */
static int army_near(int id, int (*probe)(int, int, int, int *, int *), int *out_col, int *out_row)
{
    int col = g_per_army_768[0x6c + id * 0x300], row = g_per_army_768[0x6d + id * 0x300];
    for (int r = 1; r <= 3; r++)
        if (probe(col, row, r, out_col, out_row)) return 1;
    return 0;
}
int engine_game_init_army_near_rawflag_bit0(int id, int *out_col, int *out_row)
{
    return army_near(id, engine_map_query_box_any_rawflag_bit0, out_col, out_row);
}
int engine_game_init_army_near_rawflag_only_bit1(int id, int *out_col, int *out_row)
{
    return army_near(id, engine_map_query_box_any_rawflag_only_bit1, out_col, out_row);
}
