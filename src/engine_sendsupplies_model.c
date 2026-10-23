/*
 * OpenLords2 — the Send Supplies screen's MODEL (screen 0x18) and the
 * county-action tab dispatcher that opens it: the staging, the arrows, the
 * all-toggle, the thumbnail retarget, the commit.  The render (the panel, the
 * goods table, the button tables) is engine_sendsupplies.c.
 *
 * Split from engine_sendsupplies.c so the hybrid host (tools/rhost) links the
 * model without the gfx / minimap closure.  The destination county is a retail
 * global (engine_economy_globals.c; rhost aliases it at retail's VA).
 *
 * Retail address map:
 *   FUN_0043ae30 @ 0x43ae30 (540 B) → engine_county_action_open (the five tabs
 *                                     of the county-action strip, DAT_0059154c
 *                                     = 1..5); its param-3 body is
 *                                     engine_sendsupplies_seed
 *   FUN_00436a88 @ 0x436a88 (154 B) → (in engine_county_action_open) the castle tab
 *   FUN_0043611b @ 0x43611b ( 38 B) → (in engine_county_action_open) the diplomacy tab
 *   FUN_0043b27a / FUN_0043b1ca     → engine_sendsupplies_arrow_up / _down
 *   FUN_0043b32a @ 0x43b32a (232 B) → engine_sendsupplies_toggle_all
 *   FUN_0043b412 @ 0x43b412 (185 B) → engine_sendsupplies_target_select
 *   FUN_0043b145 @ 0x43b145 (133 B) → engine_sendsupplies_commit
 */

#include "engine_sendsupplies.h"

#include <stdint.h>
#include <string.h>

#include "engine.h"                /* engine_set_current_screen / _demand_redraw */
#include "engine_army_ai.h"        /* dispatch_caravan / caravan_order (FUN_004292af, DAT_005678c0) */
#include "engine_army_economy.h"   /* provision / plan_realm (FUN_0044df5f / FUN_004485a5) */
#include "engine_combat.h"         /* g_combat_audio_latch (DAT_005533f0) */
#include "engine_minimap.h"        /* g_minimap_region_grid (DAT_0052aff0) */
#include "engine_territory.h"      /* workforce / grade cells (FUN_0044f6e7 / FUN_0044d913), dirty state */
#include "save.h"                  /* g_per_army_768 (county records), g_player_strategic_352 */
#include "save_schema_globals.h"   /* s_slot_0057c8cc / _0057c93c / _0057c934 / _0057c92c / _0057a0f8 */

/* County record stride/offsets (retail DAT_0053f9b0 family, stride 0x300). */
#define CTY_REC(k)          (&g_per_army_768[(size_t)(k) * 0x300])
#define CTY_OWNER           0x05    /* DAT_0053f9b5 — owner (u8) */
#define CTY_ARMY_CUE        0x1ad   /* DAT_0053fb5d — the county's army voice cue + 1 (u8) */
#define CTY_KEEP_LEVEL      0x1c0   /* DAT_0053fb70 — keep level (u8) */
#define CTY_GRAIN_STOCK     0x224   /* DAT_0053fbd4 — grain (ARMY_PROVISION_RAW) */
#define CTY_CATTLE_STOCK    0x250   /* DAT_0053fc00 — cattle (ARMY_STOCK_A)      */

int engine_sendsupplies_target(void) { return g_send_target; }

static int32_t cty_i32(int county, int off)
{
    int32_t v;
    memcpy(&v, CTY_REC(county) + off, sizeof v);
    return v;
}

/* ── FUN_0043ae30 param-3 body — stage the county's stock ──────────────── */
void engine_sendsupplies_seed(int player, int source_county)
{
    int32_t *ord = engine_army_ai_caravan_order(player);
    ord[0] = cty_i32(source_county, CTY_GRAIN_STOCK);   /* grain have  */
    ord[1] = 0;                                         /* grain send  */
    ord[4] = cty_i32(source_county, CTY_CATTLE_STOCK);  /* cattle have */
    ord[5] = 0;                                         /* cattle send */
    g_send_target = source_county;                      /* DAT_00553c78 */
}

/* ── the county-action tabs' seams (default: none) ────────────────────── */
static void (*g_ca_wav_fn)(const char *, int, int);   /* FUN_00427990 */
static void (*g_ca_voice_fn)(int);                     /* 0x4b3714: the s016_NN army cue */
static void (*g_ca_armory_fn)(int);                    /* the armory's entry (below) */
void engine_county_action_bind_wav_fn(void (*fn)(const char *, int, int)) { g_ca_wav_fn = fn; }
void engine_county_action_bind_voice_fn(void (*fn)(int))                  { g_ca_voice_fn = fn; }
void engine_county_action_bind_armory_fn(void (*fn)(int))                 { g_ca_armory_fn = fn; }

/* "not your county" (FUN_00472bc5(0, local, 0x70, 0…)) */
static void ca_not_yours(engine_event_queue_state_t *q)
{
    uint8_t has_data[7];
    for (int i = 0; i < 6; i++)
        has_data[i] = g_player_strategic_352[(size_t)i * 0x160 + 0x05];
    has_data[6] = 0;
    engine_event_queue_post(q, 0, (int)s_slot_0057c8cc, 0x70, 0, 0, 0, 0, 0,
                            has_data, (int)s_slot_0057c8cc);
}

/* ── FUN_0043ae30 — the county-action tab strip (action = DAT_0059154c) ────
 *   1 Armory      owner-gated (else notice 0x70): the armory's entry
 *                 (FUN_00435ebc conscripts at the slider %, FUN_004aa90a the
 *                 muster pool, march / merc off — a seam: engine_armory is not
 *                 a model yet), screen 0x17, demand 1, the county's army cue;
 *   2 Treasury    screen 9, demand 1;
 *   3 Send        owner-gated: stage (engine_sendsupplies_seed), screen 0x18,
 *                 the below-status redraw, demand 2, the s033_01 cue;
 *   4 Castle      FUN_00436a88, owner-gated: the castle screen's tier
 *                 (DAT_0056d898 = keep level - 1, or 0), screen 0x1b, demand 1;
 *   5 Diplomacy   FUN_0043611b: screen 0x0b, the gift amount reset, demand 2. */
void engine_county_action_open(int action, engine_event_queue_state_t *q)
{
    int county = (int)s_slot_0057c93c;
    int local  = (int)s_slot_0057c8cc;
    switch (action) {
    case 2:
        engine_set_current_screen(9);
        engine_set_demand_redraw(1);
        return;
    case 1:
        if (CTY_REC(county)[CTY_OWNER] != (uint8_t)local) { ca_not_yours(q); return; }
        if (g_ca_armory_fn) g_ca_armory_fn(county);
        engine_set_current_screen(0x17);
        engine_set_demand_redraw(1);
        if (CTY_REC(county)[CTY_ARMY_CUE] != 0 && g_ca_voice_fn)
            g_ca_voice_fn(CTY_REC(county)[CTY_ARMY_CUE] - 1);
        return;
    case 3:
        if (CTY_REC(county)[CTY_OWNER] != (uint8_t)local) { ca_not_yours(q); return; }
        engine_set_current_screen(0x18);
        engine_sendsupplies_seed(local, county);
        if (engine_territory_dirty_state())
            engine_territory_dirty_state()->below_status_redraw = 1;   /* DAT_0053e9ac */
        engine_set_demand_redraw(2);
        g_combat_audio_latch = 1;                                      /* DAT_005533f0 */
        if (g_ca_wav_fn) g_ca_wav_fn("s033_01.wav", 1, 0);
        return;
    case 4:
        if (CTY_REC(county)[CTY_OWNER] != (uint8_t)local) { ca_not_yours(q); return; }
        g_castle_tier = CTY_REC(county)[CTY_KEEP_LEVEL] ? CTY_REC(county)[CTY_KEEP_LEVEL] - 1 : 0;
        engine_set_current_screen(0x1b);
        engine_set_demand_redraw(1);
        return;
    case 5:
        engine_set_current_screen(0x0b);
        s_slot_0057a0f8 = 0;
        engine_set_demand_redraw(2);
        return;
    default:
        return;
    }
}

/* ── Arrows / all-toggle (retail FUN_0043b27a / b1ca / b32a) ────────────
 * `good` is retail DAT_0059154c (0 = grain, 2 = cattle); have = ord[good*2],
 * to-send = ord[good*2+1].  Steps ±1 below 11, ±10 at 11+ (retail's `< 0xb`
 * branch).  All operate on the LOCAL player's staging order (DAT_0057c8cc). */
void engine_sendsupplies_arrow_up(int good)     /* have → to-send */
{
    int32_t *ord = engine_army_ai_caravan_order((int)s_slot_0057c8cc);
    int h = good * 2, s = good * 2 + 1;
    if (ord[h] < 0xb) {
        if (ord[h] != 0) { ord[h] -= 1;  ord[s] += 1;  }
    } else {             ord[h] -= 10; ord[s] += 10; }
}

void engine_sendsupplies_arrow_down(int good)   /* to-send → have */
{
    int32_t *ord = engine_army_ai_caravan_order((int)s_slot_0057c8cc);
    int h = good * 2, s = good * 2 + 1;
    if (ord[s] < 0xb) {
        if (ord[s] != 0) { ord[s] -= 1;  ord[h] += 1;  }
    } else {             ord[s] -= 10; ord[h] += 10; }
}

void engine_sendsupplies_toggle_all(int good)   /* send-all / return-all */
{
    int32_t *ord = engine_army_ai_caravan_order((int)s_slot_0057c8cc);
    int h = good * 2, s = good * 2 + 1;
    if (ord[s] == 0) {
        if (ord[h] != 0) { ord[s] += ord[h]; ord[h] = 0; }
    } else {             ord[h] += ord[s]; ord[s] = 0; }
}

/* ── FUN_0043b412 — retarget the shipment by clicking the thumbnail at
 * (ox, oy) (the only caller passes 0x60, 0x68) ──────────────────────────── */
int engine_sendsupplies_target_select(int ox, int oy, int cursor_x, int cursor_y,
                                      int lmb_pressed)
{
    /* Retail gates on DAT_004eafb4 (the LMB-press edge). */
    if (!lmb_pressed) {
        return 0;
    }
    if (cursor_x < ox || cursor_x >= ox + 0x80) {
        return 0;
    }
    if (cursor_y < oy || cursor_y >= oy + 0x80) {
        return 0;
    }
    /* retail index: DAT_004e65fc + ((DAT_004e65f4 - oy) * 0x80 - ox). */
    uint8_t cell = g_minimap_region_grid[cursor_x + (cursor_y - oy) * 0x80 - ox];
    if (cell == 0) {
        return 0;
    }
    g_send_target = cell;
    return 1;
}

/* ── FUN_0043b145 — commit: dispatch the caravan + recompute the source ─── */
void engine_sendsupplies_commit(int player, int source, uint8_t target)
{
    engine_army_ai_dispatch_caravan(player, source, target);      /* FUN_004292af */
    engine_army_economy_provision(source, (int)s_slot_0057c934);  /* FUN_0044df5f */
    engine_territory_allocate_realm_workforce(source);            /* FUN_0044f6e7 */
    engine_territory_grade_feature_cells(source);                 /* FUN_0044d913 */
    engine_army_economy_plan_realm(source, (int)s_slot_0057c92c); /* FUN_004485a5 */
    engine_territory_allocate_realm_workforce(source);            /* (retail repeats */
    engine_territory_grade_feature_cells(source);                 /*  the recompute  */
    engine_army_economy_plan_realm(source, (int)s_slot_0057c92c); /*  triplet twice) */
}
