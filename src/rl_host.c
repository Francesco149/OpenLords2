/* rl_host.c — a retail-layout port, partition 'host' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * docs/findings/rl-host.md: the partition's notes (routes, quirks, seams, evidence).
 *
 * Two routes (round 3): (a) a WRAP — the old port lives in a module rhost links and works on retail's layout
 * (save block, aliased runtime globals); rl_<va> forwards to it (and models retail's EAX where the old port
 * returned void). (b) a RE-PORT — the old port keeps its own state / takes a port struct (the event queue …) or
 * lives in a module rhost does not link; rl_<va> is written here from retail's asm. */
#include "rl.h"
#include "rl_host_globals.h"
#include "rl_io_globals.h"
#include "rl_ui2_globals.h"
#include "rl_misc_globals.h"
#include "rl_render_globals.h"
#include "rl_ui3_globals.h"
#include "engine_bf_input_globals.h"
#include "gfx_globals.h"
#include "engine_mapview_globals.h"   /* g_mv_rate_tier0 / 1: DAT_0058fd08 / DAT_0057d3c8 */
#include "rl_io_plat.h"
#include "engine.h"                   /* the screen / redraw bridges */
#include "engine_combat.h"            /* g_siege_besieger (DAT_00568ed0) */
#include "engine_armory.h"            /* the armoury march globals (DAT_005679d0 …, aliased) */
#include "engine_army_economy.h"      /* engine_army_economy_stockpile_value (0x44e7b4) */
#include "engine_realm_upkeep.h"      /* engine_unit_disband (0x4aa039) */
#include "engine_battlefield_render.h"
#include "engine_map_query.h"         /* g_cell_cursor_col / _row: DAT_00569570 / 78 (rhost: a synced mirror) */
#include "engine_territory.h"         /* the dirty state (DAT_0053e9ac …: rhost mirrors) */
#include "engine_army_move.h"         /* engine_army_move_terrain_passable (0x469e0e) */
#include "engine_army_state.h"        /* engine_army_state_res_ai_step: the driver's residue window */
#include "rl_mpnet.h"                /* rl_475e07_res_in (quirk #240) */
#include "engine_trade.h"             /* g_trade_buttons / g_trade_widget_count (DAT_004dd838 / DAT_00553f58) */
#include "engine_treasury.h"          /* g_treasury_buttons / g_treasury_widget_count */
#include "engine_noble_faces.h"       /* g_noble_buttons / g_noble_action_count */
#include "engine_diplomacy_action.h"  /* g_diplo_action_mode (DAT_005651cc) */
#include "engine_title_render.h"      /* g_tr_selected_button (DAT_00553114, aliased) */
#include "engine_turn.h"              /* g_sim_turn_progress (DAT_00553fc8) */

#include <string.h>

/* rl_glue_globals.h's (its header conflicts with rl_io_globals.h / rl_render_globals.h over 0x4ea130 / 0x4dd640) */
extern uint8_t g_rl_53f6a0[0x300];   /* DAT_0053f6a0: the muster rows (6 × 128 B) */
extern uint8_t g_rl_53f230[0x8];     /* DAT_0053f230 / 34: the sim / scroll speed settings */
extern uint8_t g_rl_4e8780;          /* DAT_004e8780: the right button's state */
extern uint8_t g_rl_4eabe0;          /* DAT_004eabe0: the right button's press edge */
extern int32_t g_rl_568974;          /* DAT_00568974: the hovered map cell (byte offset) */
extern int32_t g_rl_57a0e0;          /* DAT_0057a0e0: its cell index */
extern int32_t g_rl_57d330;          /* DAT_0057d330 / 34: its column / row */
extern int32_t g_rl_57d334;
extern uint8_t g_rl_53f2b8[0xc8];    /* DAT_0053f2b8: the 4 × 200-byte blurbs (retail: 0x320 B; rl_glue sized 0xc8) */
extern int32_t g_rl_57d3c4;          /* DAT_0057d3c4: the load / save countdown */
extern uint8_t g_rl_591548;          /* DAT_00591548: the load / save error latch */
extern int32_t g_rl_5cd41c;          /* DAT_005cd41c: a load / save request */
extern int32_t g_rl_55321c;          /* DAT_0055321c: the rankings screen's button count */
extern int32_t g_rl_5533b0;          /* DAT_005533b0: the cursor is over a map cell */
extern int32_t g_rl_57d338;          /* DAT_0057d338: the map's cell pitch */
extern int32_t g_rl_52afd4;          /* DAT_0052afd4: the map's row length */

/* ---------------------------------------------------------------- retail records (save block) */
#define ARM8(i, va)   REC(uint8_t, g_per_territory_420, 0x52f0b0u, 420, i, va)      /* army (occupant) */
#define ARMS8(i, va)  REC(int8_t,  g_per_territory_420, 0x52f0b0u, 420, i, va)
#define ARM16(i, va)  REC(int16_t, g_per_territory_420, 0x52f0b0u, 420, i, va)
#define ARM32(i, va)  REC(int32_t, g_per_territory_420, 0x52f0b0u, 420, i, va)
#define CTY8(i, va)   REC(uint8_t, g_per_army_768, 0x53f9b0u, 768, i, va)           /* county */
#define CTY32(i, va)  REC(int32_t, g_per_army_768, 0x53f9b0u, 768, i, va)
#define PL8(p, va)    REC(uint8_t, g_player_strategic_352, 0x57bf00u, 352, p, va)   /* realm */
#define PL32(p, va)   REC(int32_t, g_player_strategic_352, 0x57bf00u, 352, p, va)
#define LOCAL         ((int)s_slot_0057c8cc)                                        /* DAT_0057c8cc */

/* ---------------------------------------------------------------- route (a): the linked old ports */
/* engine_army_ai.c (verified as a whole under ai_run_movement 0x4a5667) */
int engine_army_ai_find_owned_cell(int occ, unsigned region);
int engine_army_ai_find_settled_cell(int occ, unsigned region);
int engine_army_ai_set_deploy_cell(int occ, unsigned region);
int engine_army_ai_order_attack_march(int occ);
int engine_army_ai_order_advance(int occ);
int engine_army_ai_order_intercept(int occ);
int engine_army_ai_order_lord_move(int occ);
int engine_army_ai_order_garrison(int occ);
/* engine_combat.c (under combat_spawn_unit 0x4a9a9a, combat_begin_siege 0x4a7ca2, combat_rotate_merc_offers 0x4aca2b) */
void spawn_debit_provision(int key);
int  spawn_pick_roster_slot(int slot);
void spawn_debit_cashflow(int slot);
void spawn_attach_muster_unit(int occ, int unit);
void engine_combat_siege_build_board(int occ);
void engine_combat_siege_setup(int occ, int region);
int  merc_offer_holder(unsigned value);
/* engine_realm_upkeep.c (under realm_pay_upkeep 0x4acbd4) */
void unit_decay_substocks(int rec);
void unit_decay_owner(int owner);
void unit_disband_owner(int owner);
int  unit_settle_contracts(int owner);
void unit_absorb_overflow_sweep(void);
/* gfx_sprite_rle.c */
const uint8_t *rle_skip_top_rows(const uint8_t *sprite_blob);

/* ================================================================ the AI's army orders (brain step 0xb) */

/* Port of FUN_004a57ac (328 B) — dispatch army `occ` to its order handler by the order byte +0x1a (movsx):
 * 7 attack-march, 6 lord-directed move, 2 advance, 3 intercept, 4 garrison, 5 siege; any other code resets the
 * order to 2 and returns 0. EAX = the handler's (0x4a5667 reads it: 0 = no destination). */
int rl_4a57ac(int occ)
{
    switch (ARMS8(occ, 0x52f0cau)) {
    case 7: return rl_4a58f4(occ);
    case 6: return rl_4a599d(occ);
    case 2: return rl_4a5b1f(occ);
    case 3: return rl_4a5f0a(occ);
    case 4: return rl_4a6270(occ);
    case 5: return rl_4a60b9(occ);
    }
    ARM8(occ, 0x52f0cau) = 2;
    return 0;
}

/* Port of FUN_004a58f4 (169 B) — order 7 (attack-march): unless held (+0x199), the deploy cell toward the target
 * realm +0x151 (0x4a689d) → destination +0x16/+0x17, placed +0x150 = 0; 1 (0 when held). Wrap. */
int rl_4a58f4(int occ) { return engine_army_ai_order_attack_march(occ); }

/* Port of 0x4a599d (386 B) — order 6 (lord-directed move): unless held (+0x199), re-validate the campaign target
 * +0x19b (0x49ff71); cleared → order 2 + the advance handler; else an engageable army of that region within 20
 * (0x467532) → destination its cell, placed +0x150 = 0, 1; else 0. Wrap. */
int rl_4a599d(int occ) { return engine_army_ai_order_lord_move(occ); }

/* Port of FUN_004a5b1f (1003 B) — order 2 (advance): intercept a hostile army within 6 (0x4673b8), else keep /
 * re-pick the target realm (0x467eb0, 0x467f2e, 0x4a0649) and deploy toward it (0x4a64ca). Wrap. */
int rl_4a5b1f(int occ) { return engine_army_ai_order_advance(occ); }

/* Port of FUN_004a5f0a (431 B) — order 3 (intercept): march on an engageable army of region +0x11 within 30
 * (0x467532), else deploy toward that region. Wrap. */
int rl_4a5f0a(int occ) { return engine_army_ai_order_intercept(occ); }

/* Port of FUN_004a60b9 (439 B) — order 5 (siege). The target realm +0x151 our own: 0. No keep (+0x1c0), siege
 * mode 1 (+0x1c3) or a siege block (+0x1c2): hand the army over (0x437535) and order 2, 0. Else order 2, the hand
 * over, placed = 0, the nearest owned cell (0x4a6735) → destination, 1. (A re-port: the old port takes the event
 * queue struct for 0x437535.) */
int rl_4a60b9(int occ)
{
    int owner = ARMS8(occ, 0x52f0b0u);
    int region = ARM8(occ, 0x52f201u);
    if (CTY8(region, 0x53f9b5u) == owner)
        return 0;
    if (CTY8(region, 0x53fb70u) == 0 || CTY8(region, 0x53fb73u) == 1 || CTY8(region, 0x53fb72u) != 0) {
        engine_retail_call(0x437535u, 2, (uint32_t)occ, (uint32_t)region);   /* TODO(rl-link): engine_combat_transfer_army (an event-queue argument) */
        ARM8(occ, 0x52f0cau) = 2;
        return 0;
    }
    ARM8(occ, 0x52f0cau) = 2;
    engine_retail_call(0x437535u, 2, (uint32_t)occ, (uint32_t)region);       /* TODO(rl-link): as above */
    ARM8(occ, 0x52f200u) = 0;
    rl_4a6735(occ, region);
    ARM8(occ, 0x52f0c6u) = (uint8_t)g_cell_cursor_col;
    ARM8(occ, 0x52f0c7u) = (uint8_t)g_cell_cursor_row;
    return 1;
}

/* Port of FUN_004a6270 (602 B) — order 4 (garrison): the target keep holds the army (spare capacity, unbesieged,
 * ours) → its settled cell; else another garrison realm (0x4a07f2) → its settled cell, or disband into a realm
 * (0x438681). 1. Wrap. */
int rl_4a6270(int occ) { return engine_army_ai_order_garrison(occ); }

/* Port of FUN_004a64ca (217 B) — placed = 0; the settled cell (0x4a65a3) when the region has a keep with a
 * pending garrison, else the owned cell (0x4a6735); the cursor → destination. EAX: the finder's with the row in
 * the low byte (`mov al, byte ptr dword_569578`). Wrap. */
int rl_4a64ca(int occ, int region) { return engine_army_ai_set_deploy_cell(occ, (unsigned)region); }

/* Port of FUN_004a65a3 (402 B) — the nearest cell of `region` with flag 0x80 and terrain 0x15..0x19 (Manhattan
 * from the army's seat, 0x404ead) → the cell cursor DAT_00569570 / 78; none: the county's +0x74 / +0x75. EAX: the
 * row, or region << 8. Wrap. */
int rl_4a65a3(int occ, int region) { return engine_army_ai_find_settled_cell(occ, (unsigned)region); }

/* Port of FUN_004a6735 (360 B) — as 0x4a65a3 with flag 0x40, any terrain, fallback +0x6c / +0x6d. Wrap. */
int rl_4a6735(int occ, int region) { return engine_army_ai_find_owned_cell(occ, (unsigned)region); }

/* ================================================================ the siege board */

/* Port of FUN_004a7e0a (171 B) — link besieger `occ` to `region`'s defender (+0x1bc): placed, +0x199 = region,
 * the defender's +0x19a = occ; the board (0x4a7eb5); the local player's besieger raises the siege screen 0x1d
 * (redraw 2, DAT_00568ed0 = occ). EAX: occ then, else the besieger's owner (movsx). Wrap. */
int rl_4a7e0a(int occ, int region)
{
    engine_combat_siege_setup(occ, region);
    int owner = ARMS8(occ, 0x52f0b0u);
    return owner == LOCAL ? occ : owner;
}

/* Port of FUN_004a7eb5 (550 B) — zero the condition level +0x19c and the three lane triples; the viewer's own
 * besieger (+0x01 == 0) gets its race's siege lanes (DAT_004d8af8 by race) and the condition recompute
 * (0x4a80db). Wrap (EAX: nobody reads it). */
int rl_4a7eb5(int occ) { engine_combat_siege_build_board(occ); return 0; }

/* ================================================================ spawn / mercenary helpers */

/* Port of FUN_004a9eb1 (103 B) — debit owner `slot`'s six cashflow categories by its muster pool's per-slot stock,
 * then the ranking total (0x4487a9). Wrap. */
int rl_4a9eb1(int slot) { spawn_debit_cashflow(slot); return 0; }

/* Port of FUN_004a9f18 (90 B) — debit the realm's staged garrison (pool +0x70) from county `key`'s +0x24 / +0x38.
 * Wrap. */
int rl_4a9f18(int key) { spawn_debit_provision(key); return 0; }

/* Port of FUN_004a9f72 (199 B) — the least-loaded of realm `slot`'s 24 roster bytes (+0x2d), bumped by 2; its
 * index (0 for a realm outside 1..5). Wrap. */
int rl_4a9f72(int slot) { return spawn_pick_roster_slot(slot); }

/* Port of FUN_004ac7f3 (273 B) — attach mercenary unit `unit` (DAT_00568dc0, 20 B) to army `occ`: contract
 * slot / class / amount, the head count, the unit's back-reference, the owner's treasury (+0x118) charged, the
 * county +0x1ad cleared. EAX: county << 8. Wrap. */
int rl_4ac7f3(int occ, int unit)
{
    int county = ARM8(occ, 0x52f0c1u);
    spawn_attach_muster_unit(occ, unit);
    return county << 8;
}

/* Port of FUN_004acb54 (128 B) — the first free mercenary slot 1..DAT_00554030 offering `value` (+0x03); 0 if
 * none. Wrap. */
int rl_4acb54(int value) { return merc_offer_holder((unsigned)value); }

/* ================================================================ the unit sweeps (the upkeep pass 0x4acbd4) */

/* Port of FUN_004ace5e (650 B) — the morale sweep. Every live army (+0x00 owner, +0x08 == 1): clear the turn
 * scratch +0x156..+0x158; with DAT_0053f260 set, an army whose head count covers its type's stockpile value
 * (0x44e7b4) and is not held (+0x198 == 0) escalates +0x155: 1 warns (event 0x116), 2..4 decay its stocks
 * (0x4ad16c) and warn (0x117), 5+ warn (0x118) and disband (0x4aa039); otherwise +0x155 = 0. (A re-port: the old
 * port posts into the event-queue struct.) */
int rl_4ace5e(void)
{
    for (int i = 1; i < 151; i++) {
        if (ARM8(i, 0x52f0b0u) == 0 || ARM8(i, 0x52f0b8u) != 1)
            continue;
        ARM8(i, 0x52f206u) = 0;
        ARM8(i, 0x52f207u) = 0;
        ARM8(i, 0x52f208u) = 0;
        if (s_slot_0053f260 == 0) {
            ARM8(i, 0x52f205u) = 0;
            continue;
        }
        int type = ARM8(i, 0x52f0c0u);
        if (ARM32(i, 0x52f218u) < engine_army_economy_stockpile_value(type)) {
            ARM8(i, 0x52f205u) = 0;
        } else if (ARM8(i, 0x52f248u) != 0) {
            ARM8(i, 0x52f205u) = 0;
        } else if (++ARM8(i, 0x52f205u) == 1) {
            rl_472bc5(0, ARMS8(i, 0x52f0b0u), 278, 0, 16, type, 0, 0);
        } else if (ARM8(i, 0x52f205u) >= 5) {
            rl_472bc5(0, ARMS8(i, 0x52f0b0u), 280, 0, 16, type, 0, 0);
            engine_unit_disband(i);
        } else {
            rl_4ad16c(i);
            rl_472bc5(0, ARMS8(i, 0x52f0b0u), 279, 0, 16, type, 0, 0);
        }
    }
    return 0;
}

/* Port of FUN_004ad0e8 (132 B) — decay the stocks (0x4ad16c) of every live army of `owner`. Wrap. */
int rl_4ad0e8(int owner) { unit_decay_owner(owner); return 0; }

/* Port of FUN_004ad16c (196 B) — army `rec`'s seven stocks above 10 lose 10 % (0x404d6b), the loss off its head
 * count. Wrap. */
int rl_4ad16c(int rec) { unit_decay_substocks(rec); return 0; }

/* Port of FUN_004ad230 (230 B) — clear the contract (0x4ac6fe) of every live contracted army of `owner`, disband
 * (0x4aa039) those left with no men; 1 if any was contracted. Wrap. */
int rl_4ad230(int owner) { return unit_settle_contracts(owner); }

/* Port of FUN_004ad316 (132 B) — disband every live army of `owner`. Wrap. */
int rl_4ad316(int owner) { unit_disband_owner(owner); return 0; }

/* Port of FUN_004ad39a (140 B) — every live army: its pending absorb (0x4aba5a), then disband a type > 0x10.
 * Wrap. */
int rl_4ad39a(void) { unit_absorb_overflow_sweep(); return 0; }

/* ================================================================ the armoury */

/* Port of FUN_004aabd8 (303 B) — the armoury's march trigger: weapon `weapon` > 0 of county `county` gained men
 * (the realm's muster row DAT_0053f6a0 [+0xc] < [+0]) and no march running (DAT_005679d0 < 1): start one (target
 * DAT_004de6f0[weapon], at (-80, 216)) and load its troop sheet trp_<w>_<colour>.pl8 (DAT_004de450 by the local
 * player's colour +0x0a and the weapon; fallback trp_xb_b.pl8) into DAT_0057a0c0 (0x4b320 B). EAX: county << 8
 * (no weapon), race << 7 (no march), else the read's result. (A re-port: the old port works on a context struct.) */
int rl_4aabd8(int county, int weapon)
{
    int race = CTY8(county, 0x53f9b5u);
    if (weapon <= 0)
        return county << 8;
    if (AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f6acu + race * 128 + weapon * 16) >=
        AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f6a0u + race * 128 + weapon * 16))
        return race << 7;
    if (g_arm_march_active >= 1)
        return race << 7;
    g_arm_march_active = 1;
    g_arm_march_target = AT(int32_t, g_rl_4de6f0, 0x4de6f0u, 0x4de6f0u + 4 * weapon);
    g_arm_march_land = 1;
    g_arm_glint = 0;
    g_arm_march_x = -80;
    g_arm_march_y = 216;
    g_arm_march_sprite = 0;
    g_arm_march_walk = 0;
    int colour = PL8(LOCAL, 0x57bf0au);
    int r = rl_4af8d9((int)(uintptr_t)&AT(uint8_t, g_rl_4de450, 0x4de450u, 0x4de450u + 112 * colour + 16 * weapon),
                      (int)(uintptr_t)g_arm_troop, 0x4b320, 0);
    if (r == 0)
        r = rl_4af8d9((int)(uintptr_t)g_rl_4de8e8, (int)(uintptr_t)g_arm_troop, 0x4b320, 0);
    return r;
}

/* ================================================================ render helpers */

/* Port of FUN_004b495a (93 B) — skip DAT_0058feac rows of an RLE sprite (0 = skip N: 2 bytes, else N literals:
 * N + 1 bytes) from blob + DAT_0058fe04; the first row kept. Wrap. */
int rl_4b495a(int blob) { return (int)(uintptr_t)rle_skip_top_rows((const uint8_t *)(uintptr_t)blob); }

/* Port of FUN_004bc020 (231 B) — bind the battlefield hex grid's context (blobs, descriptor base, strides, the
 * viewport, zoom and the derived clip box) into DAT_004e5d3c..DAT_004e6578. EAX: the clip bottom. Wrap. */
int rl_4bc020(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10)
{
    engine_battlefield_render_bind_hex_grid_context((const uint8_t *)(uintptr_t)a0, (const uint8_t *)(uintptr_t)a1,
                                                    (uint8_t *)(uintptr_t)a2, a3, a4, a5, a6, a7, a8, a9, a10);
    return g_bfr_clip_bottom;
}

/* ================================================================ the io path helpers */

/* Port of FUN_004aef7e (408 B) — probe the CD's audio: DAT_005c9a74 = 1; clear the 40-byte path DAT_005cd490,
 * copy "c:.\wav" (0x401136, 30 B), its drive letter := DAT_0053f200; when it chdirs, open swor_p2.wav and then
 * pumkin.wav / pumkin2.wav (each closed; a file of 151000000+ bytes keeps DAT_005c9a74 = 1); always back to the
 * base dir (0x4af502), whose EAX it returns. (DAT_005c9a74 ends 1 on every path: the GOG build's probe.) */
int rl_4aef7e(void)
{
    uint8_t *path = &AT(uint8_t, g_rl_5cd420, 0x5cd420u, 0x5cd490u);
    g_rl_5c9a74 = 1;
    if (g_rl_53f684 == 1)
        g_rl_5c9a74 = 1;
    for (int i = 0; i < 40; i++)
        path[i] = 0;
    rl_401136((int)(uintptr_t)&AT(uint8_t, g_rl_4df3ac, 0x4df3acu, 0x4df3acu), (int)(uintptr_t)path, 30);
    path[0] = g_rl_53f200;
    if (rl_plat_chdir((const char *)path) != -1) {
        int fh = rl_plat_open((const char *)&AT(uint8_t, g_rl_4df3ac, 0x4df3acu, 0x4df3bcu), 0x8000, 0);   /* swor_p2.wav */
        if (fh != -1) {
            rl_plat_close(fh);
            fh = rl_plat_open((const char *)&AT(uint8_t, g_rl_4df3ac, 0x4df3acu, 0x4df3c8u), 0x8000, 0);   /* pumkin.wav */
            if (fh == -1)
                fh = rl_plat_open((const char *)&AT(uint8_t, g_rl_4df3ac, 0x4df3acu, 0x4df3d4u), 0x8000, 0);   /* pumkin2.wav */
            if (fh != -1) {
                int len = rl_plat_filelength(fh);
                rl_plat_close(fh);
                if (len >= 151000000)
                    g_rl_5c9a74 = 1;
            }
        }
    }
    return rl_4af502();
}

/* Port of FUN_004af502 (72 B) — chdir to the launch directory DAT_0058fe30; on failure log "ERR:CHD  not reserted
 * is now" with the current directory (getcwd into DAT_004e6910, 100 B). EAX: 0, or the log's. */
int rl_4af502(void)
{
    int r = rl_plat_chdir((const char *)g_rl_58fe30);
    if (r == 0)
        return 0;
    rl_plat_getcwd((char *)g_rl_4e6910, 100);
    return rl_4afab9((int)(uintptr_t)g_rl_4df4a8, (int)(uintptr_t)g_rl_4e6910, 0);
}

/* Port of FUN_004af54a (112 B) — remember the extension of name `s`: the three bytes after its first '.' into
 * DAT_004eafa4..a6 (a7: NUL). EAX: the scanned byte (no '.': 0), else the 3rd byte over its address. */
int rl_4af54a(int s)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)s;
    uint8_t c;
    do
        c = *p++;
    while (c != '.' && c != 0);
    if (c != '.')
        return c;
    uint8_t *ext = &AT(uint8_t, g_rl_4eafa4, 0x4eafa4u, 0x4eafa4u);
    ext[0] = p[0];
    ext[1] = p[1];
    ext[2] = p[2];
    ext[3] = 0;
    return (int)(((uint32_t)(uintptr_t)(p + 2) & ~0xffu) | p[2]);
}

/* Port of FUN_004af62c (73 B) — cut name `s` at its first '.' (NUL there). EAX: the address after the '.', or 0
 * (no '.'). */
int rl_4af62c(int s)
{
    uint8_t *p = (uint8_t *)(uintptr_t)s;
    uint8_t c;
    do
        c = *p++;
    while (c != '.' && c != 0);
    if (c != '.')
        return c;
    p[-1] = 0;
    return (int)(uintptr_t)p;
}

/* Port of FUN_004af675 (84 B) — append '.' + the remembered extension DAT_004eafa4..a6 to name `s` (at its NUL).
 * EAX: the extension's address. */
int rl_4af675(int s)
{
    uint8_t *p = (uint8_t *)(uintptr_t)s;
    while (*p++ != 0)
        ;
    const uint8_t *ext = &AT(uint8_t, g_rl_4eafa4, 0x4eafa4u, 0x4eafa4u);
    p[-1] = '.';
    p[0] = ext[0];
    p[1] = ext[1];
    p[2] = ext[2];
    p[3] = 0;
    return (int)(uintptr_t)p;
}

/* open `name` (O_BINARY), seek to `off`, read `n` bytes into `dst`, close: the read's count (`got` if not read) */
static int read_at(const char *name, void *dst, unsigned n, int off, int got)
{
    int fh = rl_plat_open(name, 0x8000, 0);
    if (fh != -1) {
        if (rl_plat_lseek(fh, off, 0) != -1)
            got = rl_plat_read(fh, dst, n);
        rl_plat_close(fh);
    }
    return got;
}

/* Port of FUN_004af8d9 (381 B) — io_read_file_at(name, dst, n, off): read from the current directory; nothing
 * read (<= 0) → the install tree (0x4af312 chdirs), back to the base (0x4af502); still nothing → the CD tree
 * (0x4af116), back to the base. EAX: the bytes read. */
int rl_4af8d9(int name, int dst, int n, int off)
{
    const char *fn = (const char *)(uintptr_t)name;
    void *buf = (void *)(uintptr_t)dst;
    int got = read_at(fn, buf, (unsigned)n, off, 0);
    if (got <= 0) {
        rl_4af312(name);
        got = read_at(fn, buf, (unsigned)n, off, got);
        rl_4af502();
        if (got <= 0) {
            rl_4af116(name);
            got = read_at(fn, buf, (unsigned)n, off, got);
            rl_4af502();
        }
    }
    return got;
}

/* ================================================================ palette / buffers / input / clocks */

/* Port of FUN_004b0ab5 (212 B) — the 6-bit palette `pal` (256 × RGB) × 4 into DAT_004ea1b0, entry 0 black; then
 * set it: 0x42f58c(0, 255) with the DirectPlay object DAT_004df288, else 0x42f1df(0). EAX: theirs. */
int rl_4b0ab5(int pal)
{
    const uint8_t *src = (const uint8_t *)(uintptr_t)pal;
    uint8_t *dst = &AT(uint8_t, g_rl_4ea1b0, 0x4ea1b0u, 0x4ea1b0u);
    for (int i = 0; i < 256; i++) {
        dst[3 * i] = (uint8_t)(4 * src[3 * i]);
        dst[3 * i + 1] = (uint8_t)(4 * src[3 * i + 1]);
        dst[3 * i + 2] = (uint8_t)(4 * src[3 * i + 2]);
    }
    dst[2] = 0;
    dst[1] = dst[2];
    dst[0] = dst[1];
    if (g_rl_4df288 != 0)
        return rl_42f58c(0, 255);
    return rl_42f1df(0);
}

/* Port of FUN_004b1867 (48 B) — clear the back buffer Buf (640 × 480: 0x4b3e51) when there is one. (EAX
 * otherwise: the caller's; HOOK_V.) */
int rl_4b1867(void)
{
    if (g_gfx_canvas_pixels != NULL)
        return rl_4b3e51((int)(uintptr_t)g_gfx_canvas_pixels, 307200);
    return 0;
}

/* Port of FUN_004b18e3 (21 B) — DAT_004e65e8 = 0 (the right button's double-click window). */
int rl_4b18e3(void)
{
    g_rl_4e65e8 = 0;
    return 0;
}

/* Port of FUN_004b191e (981 B) — the per-frame input state. The cursor (GetCursorPos → DAT_004e6594/98; with
 * DAT_0057cac4 its 16-bit copy DAT_004eabbe/c0) scaled from the client rect DAT_004ea180 to the 640 × 480 canvas
 * (0x404dc1 / 0x404d6b) into DAT_004e65fc / f4; the previous position / buttons kept (DAT_004ea198 / 190,
 * DAT_004eabbc / c3); the edges cleared and rebuilt from the window layer's latch DAT_004eada1 (bit 1 left, 2
 * right: the double clicks) and button state DAT_004eabc2; the 300 ms double-click windows DAT_004e65c4 / e8;
 * moved / idle time DAT_004ea4b0 / DAT_004eabfc; press / release edges (the release after a pending click
 * DAT_004e65f0 records it at DAT_004eac04 / 08 and opens the double-click window). EAX modelled. */
int rl_4b191e(void)
{
    POINT pt;
    rl_plat_GetCursorPos(&pt);
    g_rl_4e6594 = pt.x;
    g_rl_4e6598 = pt.y;
    if (g_rl_57cac4 != 0) {
        g_rl_4eabbe = (int16_t)g_rl_4e6594;
        g_rl_4eabc0 = (int16_t)g_rl_4e6598;
    }
    uint32_t t = rl_plat_timeGetTime();
    g_bf_4ea4b0 = 0;
    g_rl_4ea198 = g_bf_4e65fc;
    g_rl_4ea190 = g_bf_4e65f4;
    g_rl_4eabbc = g_bf_4e65cc;
    g_rl_4eabc3 = g_rl_4e8780;
    int w = AT(int32_t, g_rl_4ea180, 0x4ea180u, 0x4ea188u) - AT(int32_t, g_rl_4ea180, 0x4ea180u, 0x4ea180u);
    int h = AT(int32_t, g_rl_4ea180, 0x4ea180u, 0x4ea18cu) - AT(int32_t, g_rl_4ea180, 0x4ea180u, 0x4ea184u);
    int sx = rl_404dc1(g_gfx_canvas_width, w);
    int sy = rl_404dc1(g_gfx_canvas_height, h);
    g_bf_4e65fc = rl_404d6b(g_rl_4eabbe, sx);
    g_bf_4e65f4 = rl_404d6b(g_rl_4eabc0, sy);
    g_rl_4eb264 = 0;
    g_bf_4e65cc = 0;
    g_rl_4e8780 = 0;
    g_bf_4eafb4 = 0;
    g_bf_4e65d8 = g_bf_4eafb4;
    g_rl_4eabe0 = 0;
    g_bf_4e6900 = g_rl_4eabe0;
    g_rl_4eabf0 = 0;
    g_rl_4ea51c = g_rl_4eabf0;
    g_bf_4eabc5 = 0;
    g_rl_4ea4b4 = g_bf_4eabc5;
    if (g_rl_4eada1 & 2) {
        g_rl_4ea4b4 = 1;
        g_rl_4e65c4 = 0;
    }
    if (g_rl_4eada1 & 1) {
        g_bf_4eabc5 = 1;
        g_rl_4e65e8 = 0;
    }
    if (g_rl_4eabc2 & 2)
        g_rl_4e8780 = 1;
    if (g_rl_4eabc2 & 1)
        g_bf_4e65cc = 1;
    if (g_rl_4e65c4 != 0 && (int32_t)(t - (uint32_t)g_rl_4e59f8) > 300) {
        g_rl_4e65c4 = 0;
        g_rl_4ea51c = 1;
    }
    if (g_rl_4e65e8 != 0 && (int32_t)(t - (uint32_t)g_rl_4e59f8) > 300) {
        g_rl_4e65e8 = 0;
        g_rl_4eabf0 = 1;
    }
    if (g_rl_4ea198 != g_bf_4e65fc)
        g_bf_4ea4b0 = 1;
    if (g_rl_4ea190 != g_bf_4e65f4)
        g_bf_4ea4b0 = 1;
    if (g_bf_4ea4b0 == 0) {
        g_rl_4eabfc = (int32_t)(t - (uint32_t)g_rl_4e59f4);
    } else {
        g_rl_4e59f4 = (int32_t)t;
        g_rl_4eabfc = 0;
    }
    if (g_rl_4eabbc != g_bf_4e65cc) {
        g_bf_4ea4b0 = 1;
        g_rl_4eb264 = 1;
        if (g_bf_4e65cc == 1) {
            g_bf_4eafb4 = 1;
        } else if (g_bf_4e65cc == 0) {
            g_bf_4e65d8 = 1;
            if (g_rl_4e65f0 != 0) {
                g_rl_4eac04 = g_bf_4e65fc;
                g_rl_4eac08 = g_bf_4e65f4;
                g_rl_4e65f0 = 0;
                g_rl_4e65e8 = 1;
                g_rl_4e59f8 = (int32_t)t;
            }
        }
    }
    uint32_t eax = g_rl_4eabc3;
    if (g_rl_4eabc3 != g_rl_4e8780) {
        g_bf_4ea4b0 = 1;
        g_rl_4eb264 = 1;
        eax = g_rl_4e8780;
        if (g_rl_4e8780 == 1) {
            g_rl_4eabe0 = 1;
        } else if (g_rl_4e8780 == 0) {
            g_bf_4e6900 = 1;
            eax = g_rl_4e65f0;
            if (g_rl_4e65f0 != 0) {
                g_rl_4eac04 = g_bf_4e65fc;
                g_rl_4eac08 = g_bf_4e65f4;
                g_rl_4e65f0 = 0;
                g_rl_4e65c4 = 1;
                eax = t;
                g_rl_4e59f8 = (int32_t)t;
            }
        }
    }
    g_rl_4eada1 = 0;
    return (int)eax;
}

/* Port of FUN_004b1de0 (11 B) — an empty function. */
int rl_4b1de0(void)
{
    return 0;
}

/* Port of FUN_004b1deb (92 B) — the canvas cursor (DAT_004e65fc, DAT_004e65f4) inside the box (x, y, w, h)
 * (signed). */
int rl_4b1deb(int x, int y, int w, int h)
{
    return x <= g_bf_4e65fc && x + w > g_bf_4e65fc && y <= g_bf_4e65f4 && y + h > g_bf_4e65f4;
}

/* Port of FUN_004b20ed (68 B) — the frame delta: ms since DAT_004e59ec when 30 or more (then the mark moves), else
 * 0. */
int rl_4b20ed(void)
{
    uint32_t t = rl_plat_timeGetTime();
    int d = (int)(t - (uint32_t)g_rl_4e59ec);
    if (d < 30)
        return 0;
    g_rl_4e59ec = (int32_t)t;
    return d;
}

/* Port of FUN_004b25b0 (76 B) — size the panel sheet buffer DAT_004eabec to `size` B: a new size frees the old
 * (0x4b25fc) and mallocs the new (none for 0). EAX: size, or the new block. */
int rl_4b25b0(int size)
{
    int32_t *cur = &AT(int32_t, g_rl_4eabcc, 0x4eabccu, 0x4eabccu);
    if (*cur == size)
        return size;
    rl_4b25fc();
    *cur = size;
    if (*cur == 0)
        return size;
    void *p = rl_plat_malloc((uint32_t)*cur);
    g_gfx_current_blob = (const uint8_t *)p;
    return (int)(uintptr_t)p;
}

/* Port of FUN_004b27bf (143 B) — the frame counter DAT_004eada4 and the fps: every 1000+ ms (since
 * DAT_004eaba0) DAT_004ea174 = 1000 × frames / ms (unsigned), DAT_004ea0f4 += it, DAT_004ea83c++, a new mark
 * (a second clock read). EAX: ms since the previous call (DAT_004e59e0). */
int rl_4b27bf(void)
{
    g_rl_4eada4++;
    uint32_t t = rl_plat_timeGetTime();
    uint32_t d = t - (uint32_t)g_rl_4e59e0;
    g_rl_4e59e0 = (int32_t)t;
    uint32_t span = t - (uint32_t)g_rl_4eaba0;
    if ((int32_t)span > 1000) {
        g_rl_4ea174 = (int32_t)((uint32_t)g_rl_4eada4 * 1000u / span);
        g_rl_4eaba0 = (int32_t)rl_plat_timeGetTime();
        g_rl_4eada4 = 0;
        g_rl_4ea0f4 += g_rl_4ea174;
        g_rl_4ea83c++;
    }
    return (int)d;
}

/* Port of FUN_004b284e (127 B) — create DirectDraw (0x42e5c0) and the surfaces (0x42ec91), logging each step; a
 * surface failure releases them (0x42eaa6(1)). 1 on success. */
int rl_4b284e(void)
{
    const uint8_t *msg = &AT(uint8_t, g_rl_4df6b0, 0x4df6b0u, 0x4df6b0u);
    if (rl_42e5c0() == 0) {
        rl_4afab9((int)(uintptr_t)msg, 0, 0);                                   /* ERR:GFX DirectDraw … */
        return 0;
    }
    rl_4afab9((int)(uintptr_t)(msg + 0x20), 0, 0);                              /* OK:GFX DirectDraw … */
    if (rl_42ec91() != 0)
        return 1;
    rl_4afab9((int)(uintptr_t)(msg + 0x3c), 0, 0);                              /* ERR:GFX Could't create … */
    rl_42eaa6(1);   /* release the surfaces (quit) */
    return 0;
}

/* Port of FUN_004b2922 (156 B) — (re)start the graphics: release the DirectPlay object (0x4b28cd(0)), DAT_004e65ec
 * = 0x4b284e(); on success the palette 0x42eb7c, DAT_004df290 = 0, DAT_004e65a8 = 1, base01_256 (0x300 B) into
 * DAT_004ea8a0 and set (0x4b0ab5), the full-screen dirty mark (0x45240a), redraw 2; 1. Else log, 0. */
int rl_4b2922(void)
{
    const uint8_t *msg = &AT(uint8_t, g_rl_4df6b0, 0x4df6b0u, 0x4df6b0u);
    rl_4b28cd(0);
    g_rl_4e65ec = rl_4b284e();
    if (g_rl_4e65ec == 0) {
        rl_4afab9((int)(uintptr_t)(msg + 0x68), 0, 0);                          /* ERR:GFXBegin failed */
        return 0;
    }
    rl_42eb7c();
    g_rl_4df290 = 0;
    g_rl_4e65a8 = 1;
    rl_4af8d9((int)(uintptr_t)(msg + 0x80), (int)(uintptr_t)g_rl_4ea8a0, 0x300, 0);   /* base01_256 */
    rl_4b0ab5((int)(uintptr_t)g_rl_4ea8a0);
    rl_45240a();
    engine_set_demand_redraw(2);
    return 1;
}

/* Port of FUN_004b35c1 (255 B) — speak a popup's event: 170..197 kt<n>_<k>.wav (DAT_004e0458 + 256 (n-170) + 16 k),
 * 200..284 s2xxxx (DAT_004dfe18), 100..169 s1xxxx (DAT_004df9b8): DAT_005533f0 = 1, the cue released (0x427d19),
 * played (0x427990(name, 1, 0)); EAX: its. Other codes: nothing (EAX: the caller's; HOOK_V). */
int rl_4b35c1(int ev, int k)
{
    const uint8_t *name;
    if (ev >= 170 && ev <= 197)
        name = &AT(uint8_t, g_rl_4e0458, 0x4e0458u, 0x4e0458u + 256 * (ev - 170) + 16 * k);
    else if (ev >= 200 && ev <= 284)
        name = &AT(uint8_t, g_rl_4dfe18, 0x4dfe18u, 0x4dfe18u + 16 * (ev - 200));
    else if (ev >= 100 && ev <= 169)
        name = &AT(uint8_t, g_rl_4df9b8, 0x4df9b8u, 0x4df9b8u + 16 * (ev - 100));
    else
        return 0;
    g_combat_audio_latch = 1;
    rl_427d19();
    return rl_427990((int)(uintptr_t)name, 1, 0);
}

/* Port of FUN_004b3768 (84 B) — play cue s02xxx `k` (0..7: DAT_004e2058 + 16 k; DAT_005533f0 = 1). (Out of range:
 * the caller's EAX; HOOK_V.) */
int rl_4b3768(int k)
{
    if (k < 0 || k >= 8)
        return 0;
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)&AT(uint8_t, g_rl_4e2058, 0x4e2058u, 0x4e2058u + 16 * k), 1, 0);
}

/* Port of FUN_004b3e51 (111 B) — zero `n` bytes at `p`: 64-byte blocks (16 dword stores), then the tail
 * bytewise. EAX: 0. */
int rl_4b3e51(int p, int n)
{
    uint8_t *d = (uint8_t *)(uintptr_t)p;
    while (n >= 64) {
        memset(d, 0, 64);
        d += 64;
        n -= 64;
    }
    while (n > 0) {
        *d++ = 0;
        n--;
    }
    return 0;
}

/* ================================================================ the frame clocks (the frame driver's steps) */

/* Port of FUN_004bb8c0 (184 B) — the run gate: 1 (the GOG build nops out the CD checks after the reads). */
int rl_4bb8c0(void)
{
    return 1;
}

/* Port of FUN_004bb978 (619 B) — how many sim steps this frame (0 / 1 / 2): the gate (0x4bb8c0); a pending MP
 * pause DAT_0056d8b4 holds 5 s (then, MP, message 0xf: 0x43eda0); the scenario hint's rate (0x43de70: 0 none,
 * < 0 a double step after tick 100) capped at 140 → DAT_004d215c, through the 160-step cadence row
 * DAT_004e3260[rate / 10] at DAT_004d2164 (cycling); the speed setting DAT_0053f230 ((100 - s) / 10 < 10), no MP
 * debug DAT_0053f238, the screen's sim class DAT_004e31b0 (2: MP only), the battle gate DAT_0053e910 (MP only,
 * the battle kind's DAT_004e3240); MP at full speed steps every frame, else every 10 · (100 - s) / 10 + 2 ms
 * (DAT_004e5b0c). */
int rl_4bb978(void)
{
    uint32_t t = rl_plat_timeGetTime();
    int steps = 1;
    if (rl_4bb8c0() == 0)
        return 0;
    if (g_rl_56d8b4 != 0) {
        if ((int32_t)(t - (uint32_t)g_rl_56d8b4) < 5000)
            return 0;
        g_rl_56d8b4 = 0;
        if (g_sim_mp_active != 0)
            rl_43eda0(15, 0);
    }
    int el = (int)(t - (uint32_t)g_rl_4e5b0c);
    if (el < 0)
        el = 10000;
    int rate = rl_43de70();
    if (rate == 0)
        return 0;
    if (rate < 0) {
        steps = 2;
        if (g_bf_57c998[0] < 100)
            steps = 1;
        rate = 0;
    }
    if (rate > 140)
        rate = 140;
    g_rl_4d215c = rate;
    if (++g_rl_4d2164 > 159)
        g_rl_4d2164 = 0;
    if (AT(uint8_t, g_rl_4e31b0, 0x4e31b0u, 0x4e3260u + 160 * (rate / 10) + g_rl_4d2164) == 0)
        return 0;
    int sp = (100 - AT(int32_t, g_rl_53f230, 0x53f230u, 0x53f230u)) / 10;
    if (sp >= 10)
        return 0;
    if (g_bf_53f238 != 0)
        return 0;
    uint8_t cls = AT(uint8_t, g_rl_4e31b0, 0x4e31b0u, 0x4e31b0u + engine_current_screen());
    if (cls == 2) {
        if (g_sim_mp_active == 0)
            return 0;
    } else if (cls != 0) {
        return 0;
    }
    if (g_rl_53e910 != 0) {
        if (g_sim_mp_active == 0)
            return 0;
        if (AT(uint8_t, g_rl_4e31b0, 0x4e31b0u, 0x4e3240u + g_rl_5440a4) != 0)
            return 0;
    }
    if (sp == 0 && g_sim_mp_active != 0)
        return steps;
    if (10 * sp + 2 > el)
        return 0;
    g_rl_4e5b0c = (int32_t)t;
    return steps;
}

/* Port of FUN_004bbbe3 (157 B) — the edge-scroll rate: ready every 12 · (100 - DAT_0053f234) / 10 + 2 ms (+24 on
 * the strategic map, screen 16; a backwards clock counts as 10 s) since DAT_004e5b14 (then the mark moves): 1. */
int rl_4bbbe3(void)
{
    uint32_t t = rl_plat_timeGetTime();
    int el = (int)(t - (uint32_t)g_rl_4e5b14);
    if (el < 0)
        el = 10000;
    int sp = (100 - AT(int32_t, g_rl_53f230, 0x53f230u, 0x53f234u)) / 10;
    if (sp >= 10)
        return 0;
    if (engine_current_screen() == 16)
        sp += 2;
    if (12 * sp + 2 > el)
        return 0;
    g_rl_4e5b14 = (int32_t)t;
    return 1;
}

/* Port of FUN_004bbc80 (465 B) — the phase ticks: clear the per-frame pulses (DAT_00591510, DAT_005aeb30 / 38 / 3c,
 * DAT_0058fd08, DAT_0057d3c8, DAT_0057d3ac, DAT_0058fcb0); every 20 ms (or a backwards clock) a sub-tick
 * (DAT_005aeb2c, DAT_0058fcb0 = 1); every 4th sub-tick DAT_00591510 and the divided pulses: /2 DAT_0057d3ac,
 * /4 DAT_0057d3c8, /8 DAT_0058fd08, /13 DAT_005aeb28 (never cleared here), /16 DAT_005aeb3c, /24 DAT_005aeb38,
 * /32 DAT_005aeb30. EAX: the clock (sub-tick), else the ms since the last. */
int rl_4bbc80(void)
{
    g_rl_591510 = 0;
    g_rl_5aeb30 = 0;
    g_rl_5aeb38 = g_rl_5aeb30;
    g_rl_5aeb3c = g_rl_5aeb38;
    g_mv_rate_tier0 = g_rl_5aeb3c;
    g_mv_rate_tier1 = g_mv_rate_tier0;
    g_rl_57d3ac = g_mv_rate_tier1;
    g_rl_58fcb0 = 0;
    uint32_t t = rl_plat_timeGetTime();
    int d = (int)(t - (uint32_t)g_rl_4e5b10);
    uint32_t eax = (uint32_t)d;
    if (d >= 20 || d < 0) {
        g_rl_4e5b10 = (int32_t)t;
        eax = t;
        g_rl_5aeb2c++;
        g_rl_58fcb0 = 1;
    }
    if (g_rl_5aeb2c >= 4) {
        g_rl_5aeb2c = 0;
        g_rl_591510 = 1;
        if (++g_rl_58feb4 >= 2) { g_rl_58feb4 = 0; g_rl_57d3ac = 1; }
        if (++g_rl_58feb0 >= 4) { g_rl_58feb0 = 0; g_mv_rate_tier1 = 1; }
        if (++g_rl_58feb8 >= 8) { g_rl_58feb8 = 0; g_mv_rate_tier0 = 1; }
        if (++g_rl_5aea54 >= 13) { g_rl_5aea54 = 0; g_rl_5aeb28 = 1; }
        if (++g_rl_5aea44 >= 16) { g_rl_5aea44 = 0; g_rl_5aeb3c = 1; }
        if (++g_rl_5aea48 >= 24) { g_rl_5aea48 = 0; g_rl_5aeb38 = 1; }
        if (++g_rl_5aea4c >= 32) { g_rl_5aea4c = 0; g_rl_5aeb30 = 1; }
    }
    return (int)eax;
}

/* ================================================================ the army move-confirm (strategic map) */

#define OVW8(off, va) AT(uint8_t, g_overworld_map, 0x522f90u, (int32_t)(va) + (int32_t)(off))   /* 64 × 64 × 8 B */
#define SEL           g_mv_active_county                                                       /* DAT_0057c8c4 */

/* Port of FUN_004a8426 (148 B) — army `occ`'s siege link (+0x199 → the county's defender +0x1bc): cleared when
 * the county has no defender or the defender's +0x198 no longer names that county; DAT_0052afac++. EAX: 84 × the
 * defender (link kept), else 84 × occ. */
int rl_4a8426(int occ)
{
    int county = ARM8(occ, 0x52f249u);
    int def = CTY32(county, 0x53fb6cu);
    if (def == 0)
        ARM8(occ, 0x52f249u) = 0;
    int eax = 84 * def;
    if (ARM8(def, 0x52f248u) != county) {
        eax = 84 * occ;
        ARM8(occ, 0x52f249u) = 0;
    }
    g_combat_ctrl_52afac++;
    return eax;
}

/* Port of FUN_004a8d9c (111 B) — the selected army's path overlay off (DAT_0057d358 = 0), its seat into
 * DAT_0056d5ac / a8, and 0x46f68e(local player, army, col, row). EAX: its. */
int rl_4a8d9c(void)
{
    g_rl_57d358 = 0;
    AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5acu) = ARMS8(SEL, 0x52f0bau);
    AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5a8u) = ARMS8(SEL, 0x52f0bbu);
    return rl_46f68e(LOCAL, SEL, AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5acu),
                     AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5a8u));
}

/* Port of FUN_004a8e0b (943 B) — the move-confirm hover (render tick): a hovered map cell (DAT_00568974 < 0xfff0000)
 * decays DAT_00553ecc, marks the local player's route cells (0x4a91ba) and, on a new cell, classifies it for the
 * selected army: its path distance (DAT_00500c30[cell], 100+: 0x470678 + 1) must exceed 1; then the targets — an
 * enemy county's keep (flag 0x40: DAT_005651a4), town (0x10: DAT_0057cb1c), a passable 0x20 cell (0x469e0e:
 * DAT_00544020), a settlement (flag 0x80, terrain > 0x14: own DAT_005689ac / enemy DAT_0057c8c8), a friendly army
 * (cell +5, live, not the selected: DAT_00553f44); a reachable cell (0x4701ac) beyond the army's moves (+0x154 −
 * +0x153 < distance) drops them; DAT_0057d358 = reachable, the cell into DAT_0056d5ac / a8. */
int rl_4a8e0b(void)
{
    if (g_rl_568974 >= 0xfff0000) {
        g_rl_57d358 = 0;
        return 0;
    }
    if (g_rl_553ecc != 0)
        g_rl_553ecc--;
    rl_4a91ba();
    if (g_rl_5691e0 == g_rl_568974)
        return 0;
    g_rl_5691e0 = g_rl_568974;
    rl_472986();
    int dist = g_rl_500c30[g_rl_57a0e0];
    if (dist >= 100)
        dist = rl_470678(g_rl_57a0e0) + 1;
    if (dist <= 1) {
        g_rl_57d358 = 0;
        return 0;
    }
    g_rl_57cb1c = 0;
    g_rl_544020 = 0;
    g_rl_5651a4 = 0;
    g_rl_553f44 = 0;
    g_rl_5689ac = 0;
    g_rl_57c8c8 = 0;
    int c = OVW8(g_rl_568974, 0x522f97u);
    if ((OVW8(g_rl_568974, 0x522f91u) & 0x40) && CTY8(c, 0x53f9b5u) != ARMS8(SEL, 0x52f0b0u))
        g_rl_5651a4 = c;
    if ((OVW8(g_rl_568974, 0x522f91u) & 0x10) && CTY8(c, 0x53f9b5u) != ARMS8(SEL, 0x52f0b0u))
        g_rl_57cb1c = c;
    if ((OVW8(g_rl_568974, 0x522f91u) & 0x20) && engine_army_move_terrain_passable(g_rl_568974) &&
        CTY8(c, 0x53f9b5u) != ARMS8(SEL, 0x52f0b0u))
        g_rl_544020 = c;
    if ((OVW8(g_rl_568974, 0x522f91u) & 0x80) && OVW8(g_rl_568974, 0x522f90u) > 0x14) {
        if (CTY8(c, 0x53f9b5u) == ARMS8(SEL, 0x52f0b0u))
            g_rl_5689ac = c;
        else
            g_rl_57c8c8 = c;
    }
    if (OVW8(g_rl_568974, 0x522f95u) != 0) {
        int a = OVW8(g_rl_568974, 0x522f95u);
        if (ARMS8(a, 0x52f0b0u) == LOCAL && SEL != a && ARM8(a, 0x52f0b8u) != 4)
            g_rl_553f44 = a;
    }
    if (rl_4701ac(LOCAL, g_rl_57d330, g_rl_57d334) == 0) {
        g_rl_57d358 = 0;
        return 0;
    }
    if (ARMS8(SEL, 0x52f204u) - ARMS8(SEL, 0x52f203u) < dist) {
        g_rl_57cb1c = 0;
        g_rl_544020 = 0;
        g_rl_553f44 = 0;
        g_rl_5689ac = 0;
        g_rl_57c8c8 = 0;
        g_rl_5651a4 = 0;
    }
    g_rl_57d358 = 1;
    AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5acu) = g_rl_57d330;
    AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5a8u) = g_rl_57d334;
    return 0;
}

/* Port of FUN_004a91ba (152 B) — flag 0x40 on cell +2 of each of the local player's route cells (count
 * DAT_0053f070[p], (col, row) byte pairs at DAT_00553550 + 300 p). EAX: the local player. */
int rl_4a91ba(void)
{
    for (int i = 0; i < AT(uint8_t, g_rl_53f070, 0x53f070u, 0x53f070u + LOCAL); i++) {
        int col = AT(uint8_t, g_rl_553550, 0x553550u, 0x553550u + 300 * LOCAL + 2 * i);
        int row = AT(uint8_t, g_rl_553550, 0x553550u, 0x553551u + 300 * LOCAL + 2 * i);
        OVW8(8 * (col + (row << 6)), 0x522f92u) |= 0x40;
    }
    return LOCAL;
}

/* Port of FUN_004a9252 (288 B) — the move-confirm click: the dirty gate DAT_0053e9ac = 1; with a reachable hover
 * (DAT_0057d358 == 1): DAT_0059151c = 1, the screen saved (DAT_0057c90c), the cell (DAT_004ee898); a town / 0x20
 * target asks first (dialog 4 / 10 → 0x4a983b), a friendly army 5 (→ 0x4a975d), an own settlement 7 (→ 0x4a9372),
 * an enemy one 8 (→ 0x4a960f); none: the move now (0x4a983b). (EAX: the dialog's / the caller's: HOOK_V.) */
int rl_4a9252(void)
{
    engine_territory_dirty_state()->below_status_redraw = 1;
    if (g_rl_57d358 != 1)
        return 0;
    g_bf_59151c[0] = 1;
    g_bf_57c90c = engine_current_screen();
    g_rl_4ee898 = g_rl_568974;
    if (g_rl_57cb1c != 0)
        return rl_40e6f2(4, 160, 160, 0x4a983b);
    if (g_rl_544020 != 0)
        return rl_40e6f2(10, 160, 160, 0x4a983b);
    if (g_rl_553f44 != 0)
        return rl_40e6f2(5, 160, 160, 0x4a975d);
    if (g_rl_5689ac != 0)
        return rl_40e6f2(7, 160, 160, 0x4a9372);
    if (g_rl_57c8c8 != 0)
        return rl_40e6f2(8, 160, 160, 0x4a960f);
    return rl_4a983b();
}

/* the move order itself (0x4a98fe) or, MP, its message `msg` (0x43eda0) */
static void move_or_send(int msg, int merge)
{
    if (g_sim_mp_active != 0)
        rl_43eda0(msg, 0);
    else
        rl_4a98fe(LOCAL, SEL, AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5acu), AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5a8u),
                  merge);
}

/* Port of FUN_004a9372 (669 B) — dialog 7's answer (garrison an own settlement DAT_005689ac): the screen back
 * (DAT_0057c90c), DAT_00553f44 = 0; confirmed (DAT_0059151c): its defender (+0x1bc) besieged → event 0x121; a
 * mode-1 county with no keep built → 0xa4; a siege block → 0xa5; no room (the keep cap DAT_004d8a0c[level] − the
 * defender's men <= 0) → 0x11b; the army bigger than the room → 0xa6 (army, 0x11, county, cell); both contracted
 * → 0xa7; else the move (MP: message 0x29). */
int rl_4a9372(void)
{
    engine_set_current_screen((uint8_t)g_bf_57c90c);
    g_rl_553f44 = 0;
    if (g_bf_59151c[0] == 0)
        return 0;
    int owner = ARMS8(SEL, 0x52f0b0u);
    int county = g_rl_5689ac;
    int men = ARM32(SEL, 0x52f218u);
    int room = AT(int32_t, g_rl_4d89b8, 0x4d89b8u, 0x4d8a0cu + 4 * CTY8(county, 0x53fb70u));
    int def = CTY32(county, 0x53fb6cu);
    if (def != 0)
        room -= ARM32(def, 0x52f218u);
    if (def != 0 && ARM8(def, 0x52f24au) != 0)
        rl_472bc5(0, owner, 289, 0, 0, 0, 0, 0);
    else if (CTY8(county, 0x53fb73u) == 1 && CTY8(county, 0x53fb71u) == 0)
        rl_472bc5(0, owner, 164, 0, 0, 0, 0, 0);
    else if (CTY8(county, 0x53fb72u) != 0)
        rl_472bc5(0, owner, 165, 0, 0, 0, 0, 0);
    else if (room <= 0)
        rl_472bc5(0, owner, 283, 0, 0, 0, 0, 0);
    else if (men > room)
        rl_472bc5(0, owner, 166, SEL, 17, county, 0, g_rl_4ee898);
    else if (ARM8(SEL, 0x52f246u) != 0 && ARM8(def, 0x52f246u) != 0)
        rl_472bc5(0, owner, 167, 0, 0, 0, 0, 0);
    else
        move_or_send(41, 0);
    return 0;
}

/* Port of FUN_004a960f (334 B) — dialog 8's answer (an enemy settlement DAT_0057c8c8): the screen back, DAT_00553f44
 * = 0; confirmed: its defender besieged → event 0x113; a defender → the move (MP: 0x29); no defender: a mode county
 * (+0x1c3) → 0x11c, else 0x11d. */
int rl_4a960f(void)
{
    int def = CTY32(g_rl_57c8c8, 0x53fb6cu);
    engine_set_current_screen((uint8_t)g_bf_57c90c);
    g_rl_553f44 = 0;
    if (g_bf_59151c[0] == 0)
        return 0;
    if (def != 0 && ARM8(def, 0x52f24au) != 0)
        rl_472bc5(0, LOCAL, 275, 0, 0, 0, 0, 0);
    else if (CTY32(g_rl_57c8c8, 0x53fb6cu) != 0)
        move_or_send(41, 0);
    else if (CTY8(g_rl_57c8c8, 0x53fb73u) != 0)
        rl_472bc5(0, LOCAL, 284, 0, 0, 0, 0, 0);
    else
        rl_472bc5(0, LOCAL, 285, 0, 0, 0, 0, 0);
    return 0;
}

/* Port of FUN_004a975d (222 B) — dialog 5's answer (merge with the friendly army DAT_00553f44): the screen back;
 * confirmed: both armies' men <= 1500 → the move merging into it (MP: message 0x2c), else event 0x112. */
int rl_4a975d(void)
{
    engine_set_current_screen((uint8_t)g_bf_57c90c);
    if (g_bf_59151c[0] == 0)
        return 0;
    if (ARM32(SEL, 0x52f218u) + ARM32(g_rl_553f44, 0x52f218u) <= 1500)
        move_or_send(44, g_rl_553f44);
    else
        rl_472bc5(0, LOCAL, 274, 0, 0, 0, 0, 0);
    return 0;
}

/* Port of FUN_004a983b (195 B) — dialogs 4 / 10's answer and the plain move: the screen back, DAT_00553f44 = 0; a
 * besieged keep target (DAT_005651a4's defender) → event 0x11e; else confirmed: the move (MP: message 0x29). */
int rl_4a983b(void)
{
    engine_set_current_screen((uint8_t)g_bf_57c90c);
    g_rl_553f44 = 0;
    if (g_rl_5651a4 != 0 && CTY32(g_rl_5651a4, 0x53fb6cu) != 0)
        rl_472bc5(0, LOCAL, 286, 0, 0, 0, 0, 0);
    else if (g_bf_59151c[0] != 0)
        move_or_send(41, 0);
    return 0;
}

/* Port of FUN_004a98fe (412 B) — order army `occ` to (col, row): rebuild the cost grid (0x46ff43), fill the path
 * field from its seat (0x46f700), trace to the target (0x4701ac); reachable: copy the route (0x4707be), the target
 * into +0x14 / +0x16 and +0x15 / +0x17, +0x14c = 2, a besieger's siege link broken (0x43b917), the merge target
 * +0x152 = `merge` (its low byte), the turn scratch +0x156..+0x158 = 1. EAX: 84 × occ, or the trace's 0. (`player`
 * is not read.) */
int rl_4a98fe(int player, int occ, int col, int row, int merge)
{
    (void)player;
    rl_46ff43();
    rl_46f700(0, ARMS8(occ, 0x52f0bau), ARMS8(occ, 0x52f0bbu), 0);
    int r = rl_4701ac(0, col, row);
    if (r == 0)
        return 0;
    rl_4707be(0, occ);
    ARM8(occ, 0x52f0c4u) = (uint8_t)col;
    ARM8(occ, 0x52f0c6u) = ARM8(occ, 0x52f0c4u);
    ARM8(occ, 0x52f0c5u) = (uint8_t)row;
    ARM8(occ, 0x52f0c7u) = ARM8(occ, 0x52f0c5u);
    ARM8(occ, 0x52f1fcu) = 2;
    if (ARM8(occ, 0x52f0b8u) == 1)
        engine_combat_break_siege_link(occ);
    ARM8(occ, 0x52f202u) = (uint8_t)merge;
    ARM8(occ, 0x52f206u) = 1;
    ARM8(occ, 0x52f207u) = 1;
    ARM8(occ, 0x52f208u) = 1;
    return 84 * occ;
}

/* ================================================================ game init / save / load */

/* Port of FUN_004ac904 (295 B) — the mercenary pool: DAT_00554030 = the scenario's count DAT_004de820[DAT_0056d5dc]
 * (out of 0..12: 1), the unit table cleared (0x46f2ae), each slot 1..n seeded from the template rows
 * DAT_004de75c.. (amount, class, the two period bytes, the two rate bytes, cost, upkeep). EAX: the count. */
int rl_4ac904(void)
{
    s_slot_00554030 = (uint32_t)AT(int32_t, g_rl_4de75c, 0x4de75cu, 0x4de820u + 4 * (int32_t)s_slot_0056d5dc);
    if ((int32_t)s_slot_00554030 > 12)
        s_slot_00554030 = 1;
    if ((int32_t)s_slot_00554030 < 0)
        s_slot_00554030 = 1;
    rl_46f2ae();
    for (int i = 1; i <= (int32_t)s_slot_00554030; i++) {
        uint8_t *u = &g_unit_table_20[20 * i];
        *(int32_t *)(u + 8) = AT(int32_t, g_rl_4de75c, 0x4de75cu, 0x4de75cu + 4 * i);
        u[5] = AT(uint8_t, g_rl_4de75c, 0x4de75cu, 0x4de78fu + i);
        u[2] = AT(uint8_t, g_rl_4de75c, 0x4de75cu, 0x4de79fu + i);
        u[4] = AT(uint8_t, g_rl_4de75c, 0x4de75cu, 0x4de79fu + i);
        u[6] = AT(uint8_t, g_rl_4de75c, 0x4de75cu, 0x4de7afu + i);
        u[7] = AT(uint8_t, g_rl_4de75c, 0x4de75cu, 0x4de7afu + i);
        *(int32_t *)(u + 12) = AT(int32_t, g_rl_4de75c, 0x4de75cu, 0x4de7bcu + 4 * i);
        *(int32_t *)(u + 16) = AT(int32_t, g_rl_4de75c, 0x4de75cu, 0x4de7ecu + 4 * i);
    }
    return (int)s_slot_00554030;
}

/* Port of FUN_004ae74d (144 B) — clear the realm history ring (400 turns × 16 realms: +0 and the byte at +4) and
 * the counters DAT_00568da8, DAT_0055300c, DAT_00553f30. EAX: 16 (the last inner index). */
int rl_4ae74d(void)
{
    for (int i = 0; i < 400; i++)
        for (int j = 1; j < 17; j++) {
            AT(int32_t, g_realm_history, 0x56d8c0u, 0x56d8b8u + 128 * i + 8 * j) = 0;
            AT(uint8_t, g_realm_history, 0x56d8c0u, 0x56d8bcu + 128 * i + 8 * j) = 0;
        }
    s_slot_00568da8 = 0;
    s_slot_0055300c = 0;
    s_slot_00553f30 = 0;
    return 16;
}

/* Port of FUN_004ae8a9 (191 B) — a blank castles.dat: 16 zeroed 0x3200-byte scenario blocks (through
 * DAT_004eb2b0). Can't create it: log, the error latch DAT_005aeb68 = 1. EAX: close's, or the log's. */
int rl_4ae8a9(void)
{
    int fh = rl_plat_open((const char *)&AT(uint8_t, g_rl_4df17c, 0x4df17cu, 0x4df194u), 0x8301, 0x180);
    if (fh == -1) {
        int r = rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4df17c, 0x4df17cu, 0x4df1a0u), 0, 0);
        g_gfx_last_error = 1;
        return r;
    }
    for (int i = 0; i < 0x3200; i++)
        g_castles_scratch[i] = 0;
    for (int j = 1; j < 17; j++)
        rl_plat_write(fh, g_castles_scratch, 0x3200);
    return rl_plat_close(fh);
}

/* Port of FUN_004ade93 (339 B) — save_write(name): to the base dir (0x4af502); create `name`, open castles.dat;
 * write the save blocks (DAT_004de960: (address, size) pairs, up to 225, size 0 ends) then the 16 castles blocks
 * (through DAT_004eb2b0); close both, clear the line editor (0x4020ae), DAT_0059155c = 0, the below-status mark
 * (0x45270a). 0; 1 if a file does not open. */
int rl_4ade93(int name)
{
    rl_4af502();
    int fh = rl_plat_open((const char *)(uintptr_t)name, 0x8301, 0x180);
    if (fh == -1)
        return 1;
    int cf = rl_plat_open((const char *)g_rl_4df120, 0x8000, 0);
    if (cf == -1) {
        rl_plat_close(fh);
        return 1;
    }
    for (int i = 0; i < 225 && AT(int32_t, g_rl_4de960, 0x4de960u, 0x4de964u + 8 * i) != 0; i++)
        rl_plat_write(fh, (const void *)(uintptr_t)AT(uint32_t, g_rl_4de960, 0x4de960u, 0x4de960u + 8 * i),
                      (unsigned)AT(int32_t, g_rl_4de960, 0x4de960u, 0x4de964u + 8 * i));
    for (int j = 1; j < 17; j++) {
        rl_plat_read(cf, g_castles_scratch, 0x3200);
        rl_plat_write(fh, g_castles_scratch, 0x3200);
    }
    rl_plat_close(cf);
    rl_plat_close(fh);
    rl_4020ae();
    g_rl_59155c = 0;
    rl_45270a();
    return 0;
}

/* Port of FUN_004adfe6 (377 B) — save_load(name): to the base dir, DAT_0057d3b4 = 0, the event queue reset
 * (0x472ae0); open `name` (else log "ERR:Couldn't load", 1) and create castles.dat (else 1); read the save blocks
 * (DAT_004de960) and copy the 16 castles blocks out; close both, clear the line editor, DAT_0057d3b4 = 1,
 * DAT_0059155c = 0, the post-load map rebuild (0x4976a1). 0. */
int rl_4adfe6(int name)
{
    rl_4af502();
    g_rl_57d3b4 = 0;
    rl_472ae0();
    int fh = rl_plat_open((const char *)(uintptr_t)name, 0x8000, 0);
    if (fh == -1) {
        rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4df120, 0x4df120u, 0x4df12cu), name, 0);
        return 1;
    }
    int cf = rl_plat_open((const char *)&AT(uint8_t, g_rl_4df120, 0x4df120u, 0x4df148u), 0x8301, 0x180);
    if (cf == -1) {
        rl_plat_close(fh);
        return 1;
    }
    for (int i = 0; i < 225 && AT(int32_t, g_rl_4de960, 0x4de960u, 0x4de964u + 8 * i) != 0; i++)
        rl_plat_read(fh, (void *)(uintptr_t)AT(uint32_t, g_rl_4de960, 0x4de960u, 0x4de960u + 8 * i),
                     (unsigned)AT(int32_t, g_rl_4de960, 0x4de960u, 0x4de964u + 8 * i));
    for (int j = 1; j < 17; j++) {
        rl_plat_read(fh, g_castles_scratch, 0x3200);
        rl_plat_write(cf, g_castles_scratch, 0x3200);
    }
    rl_plat_close(cf);
    rl_plat_close(fh);
    rl_4020ae();
    g_rl_57d3b4 = 1;
    g_rl_59155c = 0;
    rl_4976a1();
    return 0;
}

/* Port of FUN_004ae310 (553 B) — the settings' defaults: the year 2028, the name "Player1" (0x401136, 31 B),
 * speeds 90 / 60, the option dwords DAT_0053f208..DAT_0053f284, DAT_0053f640 / 44 = 0; the setup defaults
 * (0x4ae539); the four localized blurbs (string 226, k = 0..3: 0x4017bf into DAT_0053f2b8 + 200 k, 200 B, zeroed
 * first) cut at their first control byte. EAX: 75 (the last row's index arithmetic). */
int rl_4ae310(void)
{
    g_rl_53f204 = 2028;
    rl_401136((int)(uintptr_t)g_rl_4df17c, (int)(uintptr_t)g_bf_53f1e0, 31);
    AT(int32_t, g_rl_53f230, 0x53f230u, 0x53f230u) = 90;
    AT(int32_t, g_rl_53f230, 0x53f230u, 0x53f234u) = 60;
    g_rl_53f24c = 1;
    g_rl_53f250 = 1;
    g_rl_53f210 = 1;
    g_rl_53f20c = 1;
    g_rl_53f218 = 1;
    g_bf_53f214 = 1;
    g_rl_53f20c = 1;
    g_combat_flag_53f248 = 1;
    g_rl_53f254 = 1;
    g_rl_53f21c = 100;
    g_rl_53f224 = 4;
    g_rl_53f220 = 100;
    g_bf_53f238 = 0;
    g_combat_flag_53f284 = 1;
    g_rl_53f240 = 1;
    s_slot_0053f23c = 0;
    s_slot_0053f268 = 4;
    s_slot_0053f25c = 0;
    s_slot_0053f260 = 0;
    s_slot_0053f264 = 0;
    g_rl_53f208 = 1;
    g_rl_53f228 = 1;
    g_rl_53f22c = 8;
    s_slot_0053f640 = 0;
    g_rl_53f644 = 0;
    engine_retail_call(0x4ae539u, 0);   /* TODO(rl-extern): 0x4ae539 (the setup defaults; engine_title_render.c's port is unverified) */
    for (int i = 0; i < 4; i++) {
        uint8_t *s = &AT(uint8_t, g_rl_53f2b8, 0x53f2b8u, 0x53f2b8u + 200 * i);
        for (int j = 0; j < 200; j++)
            s[j] = 0;
        rl_4017bf((int)(uintptr_t)s, 226, i, 200);
        int cut = 0;
        for (int j = 0; j < 200; j++) {
            if (s[j] < 0x20)
                cut = 1;
            if (cut)
                s[j] = 0;
        }
    }
    return 75;
}

/* the load / save name DAT_004ead60 (16 B) and the MP copy DAT_0053f660 (16 B) */
#define LOADNAME ((int)(uintptr_t)g_rl_4ead60)
#define MPNAME   ((int)(uintptr_t)g_rl_53f660)

/* Port of FUN_004ad9f0 (774 B) — the load / save pump (the title / options screens). The countdown DAT_0057d3c4
 * reaching 0 runs the request (unless the error latch DAT_00591548 just clears): screen 31 (load a game: MP
 * message 0x1d, 250 ms, its own file DAT_0053f660 → DAT_00553d30[p] = 1 failed / 2 loaded; SP DAT_004ead60) then
 * 0x4adcf6 (the scenario check) starts the game (DAT_005aeb8c = 1, 0x4329ec) or latches an error (400 frames);
 * screen 53 / 54 (load / save from the menu, the screen restored: MP messages 0x18 / 0x17); redraw 2. A request
 * DAT_005cd41c arms it (150 frames): the extension (MP: "mpg" / "mps" by DAT_00553248; SP "sav"), the name from
 * DAT_004ea130 (12 B) + it into DAT_004ead60, copied to DAT_0053f660 (14 B); the save cue s040_02, or (load) a
 * missing file (0x4af767) latches the error, the cue s040_01. */
int rl_4ad9f0(void)
{
    if (g_rl_57d3c4 != 0 && --g_rl_57d3c4 == 0) {
        if (g_rl_591548 != 0) {
            g_rl_591548 = 0;
        } else {
            int scr = engine_current_screen();
            if (scr == 31) {
                for (int i = 0; i < 6; i++)
                    g_rl_553d30[i] = 0;
                if (g_sim_mp_active != 0) {
                    rl_43eda0(29, 0);
                    bf_sleep(250);
                    if (rl_4adfe6(MPNAME) != 0)
                        g_rl_553d30[LOCAL] = 1;
                    else
                        g_rl_553d30[LOCAL] = 2;
                } else {
                    g_rl_591548 = (uint8_t)rl_4adfe6(LOADNAME);
                }
                if (rl_4adcf6() != 0) {
                    g_rl_5aeb8c = 1;
                    rl_4329ec();
                } else {
                    g_rl_591548 = 1;
                    g_rl_57d3c4 = 400;
                }
            } else if (scr == 53) {
                engine_set_current_screen((uint8_t)g_bf_57c90c);
                if (g_sim_mp_active != 0)
                    rl_43eda0(24, 0);
                else
                    g_rl_591548 = (uint8_t)rl_4adfe6(LOADNAME);
            } else if (scr == 54) {
                engine_set_current_screen((uint8_t)g_bf_57c90c);
                if (g_sim_mp_active != 0)
                    rl_43eda0(23, 0);
                else
                    g_rl_591548 = (uint8_t)rl_4ade93(LOADNAME);
            }
            engine_set_demand_redraw(2);
        }
    }
    if (g_rl_5cd41c == 0)
        return 0;
    g_rl_5cd41c = 0;
    g_rl_57d3c4 = 150;
    int ext;
    if (g_sim_mp_active != 0)
        ext = g_rl_553248 != 0 ? 0x4df068 : 0x4df06c;
    else
        ext = 0x4df070;
    rl_401136((int)(uintptr_t)&AT(uint8_t, g_rl_4df068, 0x4df068u, ext), (int)(uintptr_t)g_rl_4eafa4, 3);
    rl_401136((int)(uintptr_t)g_rl_4ea130, LOADNAME, 12);
    rl_4af675(LOADNAME);
    rl_401136(LOADNAME, MPNAME, 14);
    if (engine_current_screen() == 54) {
        g_combat_audio_latch = 1;
        rl_427990((int)(uintptr_t)&AT(uint8_t, g_rl_4df068, 0x4df068u, 0x4df074u), 1, 0);   /* s040_02.wav */
    }
    if (engine_current_screen() != 54) {
        if (rl_4af767(LOADNAME) == 0) {
            g_rl_591548 = 1;
            g_rl_57d3c4 = 400;
        }
        g_combat_audio_latch = 1;
        rl_427990((int)(uintptr_t)&AT(uint8_t, g_rl_4df068, 0x4df068u, 0x4df080u), 1, 0);   /* s040_01.wav */
    }
    return 0;
}

/* ================================================================ the per-screen refresh (the frame driver's render step) */

/* linked ports on port structs / bindings of their own (rh_hooks_screens.c binders, the gfx table's dirty copy):
 * through retail until they move to retail's layout — TODO(rl-link) */
static void buttons(int x, int y, const void *recs, int n)   /* engine_button_render_list 0x40cfd2 (rl_render.c's reason) */
{ engine_retail_call(0x40cfd2u, 4, (uint32_t)x, (uint32_t)y, (uint32_t)(uintptr_t)recs, (uint32_t)n); }
static void rcall0(uint32_t va) { engine_retail_call(va, 0); }
static void rcall1(uint32_t va, int a) { engine_retail_call(va, 1, (uint32_t)a); }
static void rcall2(uint32_t va, int a, int b) { engine_retail_call(va, 2, (uint32_t)a, (uint32_t)b); }
#define BTN(sym, va) ((const void *)&AT(uint8_t, &(sym), (va), (va)))   /* a button-record table (retail .data) */

/* Port of FUN_004ba26e (1882 B) — the per-screen refresh of the frame driver (engine_diplomacy_action_refresh): by
 * screen DAT_004eac50 its panel's dynamic parts and button strip (0x40cfd2 with the screen's record table and
 * count): 2 (0x412421), 15 (0x413526 on page 8), 21, 25 (+ the ration slider 0x411fde), 8 (0x41608b), 12 (trade
 * 0x41635f), 24 (send supplies 0x41aea2), 23 (the armoury overlay, y 16 with a mercenary offer), 17 (0x419354),
 * 10 / 13 (the armoury march 0x418fc5 / 0x419243 / 0x4190db, counts 0x4181eb, assignments 0x418e2d), 27
 * (0x4198aa), 9, 11, 26 (diplomacy by mode DAT_005651cc: 0x417bd3(mode − 1) for 1..4), 16 (the move-confirm hover
 * 0x4a8e0b), 30 (the confirm dialog 0x40ccfa), 18, 29, 31 (the title pages by DAT_005530f0: buttons, options, the
 * file list, choose-title, setup / custom, play-now, skirmish …), 46 / 47 (0x4360f2, 0x43f24b), 33 (0x40cd58), 53 /
 * 54 (the file list), 57, 66, 67, 68, 49. */
int rl_4ba26e(void)
{
    switch (engine_current_screen()) {
    case 2: rl_412421(); break;
    case 15: if (g_rl_553f54 == 8) rl_413526(); break;
    case 21: buttons(0, 0, BTN(g_rl_4dd790, 0x4dd790u), 2); break;
    case 25:
        buttons(0, 0, BTN(g_rl_4dd7c0, 0x4dd7c0u), 2);
        rcall0(0x411fdeu);   /* TODO(rl-link): engine_county_ration_slider_render (engine_county_panels.c: a port struct) */
        break;
    case 8: rl_41608b(); buttons(0, 0, BTN(g_rl_4dd808, 0x4dd808u), g_rl_569500); break;
    case 12:
        rcall0(0x41635fu);   /* TODO(rl-link): engine_trade_content (engine_trade.c: a port struct) */
        buttons(48, 80, g_trade_buttons, g_trade_widget_count);
        break;
    case 24: rl_41aea2(); buttons(0, 0, BTN(g_rl_4dd538, 0x4dd538u), 6); break;
    case 23:
        if (CTY8((int)s_slot_0057c93c, 0x53fb5du) != 0)
            buttons(0, 16, g_arm_overlay_buttons, g_arm_mode);
        else
            buttons(0, 0, g_arm_overlay_buttons, g_arm_mode);
        break;
    case 17: rl_419354(); buttons(0, 0, BTN(g_rl_4dd388, 0x4dd388u), g_rl_55321c); break;
    case 10:
        rcall0(0x418fc5u);   /* TODO(rl-link): engine_armory_march_restore (engine_armory.c, its screen binder) */
        rcall0(0x419243u);   /* TODO(rl-link): engine_armory_refresh */
        rcall0(0x4190dbu);   /* TODO(rl-link): engine_armory_march_step */
        break;
    case 13:
        rcall0(0x418fc5u);   /* TODO(rl-link): as above */
        rcall0(0x419243u);   /* TODO(rl-link) */
        rcall0(0x4181ebu);   /* TODO(rl-link): engine_armory_counts_render */
        rl_418e2d();
        rcall0(0x4190dbu);   /* TODO(rl-link) */
        buttons(96, 4, BTN(g_rl_4dd8c8, 0x4dd8c8u), 4);
        break;
    case 27: rl_4198aa(); buttons(0, 0, BTN(g_rl_4ddb80, 0x4ddb80u), 2); break;
    case 9: buttons(0, 0, g_treasury_buttons, g_treasury_widget_count); break;
    case 11: buttons(0, 0, g_noble_buttons, g_noble_action_count); break;
    case 26:
        if (g_diplo_action_mode == 0) {
            buttons(0, 0, BTN(g_rl_4dd9d0, 0x4dd9d0u), 4);
        } else if (g_diplo_action_mode > 4) {
            buttons(0, 0, BTN(g_rl_4dda60, 0x4dda60u), 2);
        } else {
            rl_417bd3(g_diplo_action_mode - 1);
            buttons(0, 0, BTN(g_rl_4dda30, 0x4dda30u), 2);
        }
        break;
    case 16: rl_4a8e0b(); break;
    case 30: rl_40ccfa(); buttons(g_rl_52af90, g_rl_52af98, BTN(g_rl_4dd310, 0x4dd310u), 2); break;
    case 18: buttons(32, 48, BTN(g_rl_4ddbb0, 0x4ddbb0u), g_combat_ctrl_408); break;
    case 29: buttons(0, 0, BTN(g_rl_4ddf10, 0x4ddf10u), 6); break;
    case 31:
        switch (g_rl_5530f0) {
        case 1: rcall0(0x41eaa3u); break;   /* TODO(rl-link): engine_title_render_buttons (engine_title_render.c) */
        case 2: rcall0(0x41ece6u); break;   /* TODO(rl-link): engine_title_render_options_buttons */
        case 3:
            rcall2(0x4149ecu, g_rl_4e65dc, 1);   /* TODO(rl-link): engine_title_render_filelist_content */
            buttons(96, 10, BTN(g_rl_4ddd78, 0x4ddd78u), 4);
            break;
        case 4:
            rcall0(0x41f321u);   /* TODO(rl-link): the choose-title name box */
            rcall0(0x41f1ddu);   /* TODO(rl-link): … shields */
            rcall0(0x41f01fu);   /* TODO(rl-link): … buttons */
            break;
        case 7:
            rcall0(0x41f98bu);   /* TODO(rl-link): engine_title_render_setup_country_list */
            rcall1(0x41f86du, 999);   /* TODO(rl-link): engine_title_render_setup_tabs */
            rl_41ff75(0);
            break;
        case 8:
            rcall0(0x41f98bu);   /* TODO(rl-link): as above */
            rcall1(0x41f86du, 999);   /* TODO(rl-link) */
            rl_41ff75(0);
            rl_420147(0);
            break;
        case 13: rl_414e06(g_rl_4e65dc); buttons(0, 0, BTN(g_rl_4dddd8, 0x4dddd8u), 2); break;
        case 6: rcall0(0x41f4a1u); break;   /* TODO(rl-extern): 0x41f4a1 */
        case 5: rcall0(0x41f5e8u); break;   /* TODO(rl-link): engine_title_render_play_now_buttons */
        case 12:
        case 11:
            rl_41ff75(1);
            rl_420147(1);
            rl_420d40();
            if (g_rl_56899c == 1)
                buttons(0, 0, BTN(g_rl_4de000, 0x4de000u), (int)s_slot_0056d5c0);
            break;
        }
        break;
    case 46:
    case 47: rl_4360f2(); rl_43f24b(); break;
    case 33: rl_40cd58(); buttons(g_rl_58fe20, g_rl_58fe24, BTN(g_rl_4ddbe0, 0x4ddbe0u), 2); break;
    case 53:
    case 54:
        rcall2(0x4149ecu, g_rl_4e65dc, 0);   /* TODO(rl-link): engine_title_render_filelist_content */
        buttons(16, 144, BTN(g_rl_4ddd78, 0x4ddd78u), 4);
        break;
    case 57: buttons(0, 0, BTN(g_rl_4ddc10, 0x4ddc10u), g_rl_56d8a0); break;
    case 66: buttons(0, 0, BTN(g_rl_4ddc70, 0x4ddc70u), g_rl_553f00); break;
    case 67: buttons(0, 0, BTN(g_rl_4ddcb8, 0x4ddcb8u), g_rl_5533ac); break;
    case 68: buttons(0, 0, BTN(g_rl_4ddfa0, 0x4ddfa0u), 4); break;
    case 49: buttons(0, 0, BTN(g_rl_4ddce8, 0x4ddce8u), g_rl_52afd0); break;
    }
    return 0;
}

/* ================================================================ the menu / panel input (the in-game dispatcher's step) */

#define HOT(va) ((int)(uintptr_t)&AT(uint8_t, g_rl_4dc7e8, 0x4dc7e8u, (va)))   /* a hotspot table (DAT_004dc7e8..) */
#define HOT2(va) ((int)(uintptr_t)&AT(uint8_t, g_rl_4dcb18, 0x4dcb18u, (va)))  /* … (DAT_004dcb18..) */
#define BTNI(sym, va) ((int)(uintptr_t)BTN(sym, va))
static int click(int x, int y, int tbl, int n) { return rl_40e3ee(x, y, tbl, n); }    /* engine_menu_hotspot_click */
static int hover(int x, int y, int tbl, int n) { return rl_40e5e7(x, y, tbl, n); }    /* engine_menu_hotspot_hover */
static int bclick(int x, int y, int tbl, int n) { return rl_40da1e(x, y, tbl, n); }   /* the button strip's click */
/* the line editor: its length, then its text into `dst` (`n` B) */
static void editor_out(int dst, int n) { g_rl_5aeb78 = 1; rl_401f49(); rl_40210c(dst, n); }

/* Port of FUN_004ba9c8 (3832 B) — the menu / panel input (engine_menu_title_input): the line editor's focus
 * DAT_005aeb78 dropped without a popup (DAT_0057a0cc <= 0); by screen DAT_004eac50 its hotspot tables (0x40e3ee
 * click, 0x40e5e7 hover → DAT_00553114) and button strips (0x40da1e), the merchant / armoury zones (0x4357a6 /
 * 0x43582a), the editor's text into its field (0x401f49 / 0x40210c: a diplomacy letter, the save name DAT_004ea130,
 * the player name, the MP game name DAT_00553e80 lowered by 0x401246), the load / save pump (0x4ad9f0); the title
 * pages by DAT_005530f0 (1 main: a pending choice DAT_00568464 → param 2 and the page action 0x432b05; 9 the option
 * detail 0x433ce4 / its pick 0x433a23, a click elsewhere returns to page DAT_00553e5c). 1: the click was taken. */
int rl_4ba9c8(void)
{
    if (g_rl_57a0cc <= 0)
        g_rl_5aeb78 = 0;
    switch (engine_current_screen()) {
    case 15:
        if (g_rl_553f54 == 8) {
            if (g_rl_55cd78 != 0)
                engine_set_demand_redraw(2);
            if (click(0, 24, HOT(0x4dca10u), 6) != 0)
                return 1;
        }
        break;
    case 21: if (bclick(0, 0, BTNI(g_rl_4dd790, 0x4dd790u), 2) != 0) return 1; break;
    case 25: if (bclick(0, 0, BTNI(g_rl_4dd7c0, 0x4dd7c0u), 2) != 0) return 1; break;
    case 32: if (click(0, 0, HOT(0x4dc890u), 7) != 0) return 1; break;
    case 8:
        if (engine_retail_call(0x4357a6u, 0) != 0)   /* TODO(rl-link): engine_merchant_zone_at (engine_merchant.c, its screen binder) */
            return 1;
        if (bclick(0, 0, BTNI(g_rl_4dd808, 0x4dd808u), g_rl_569500) != 0)
            return 1;
        break;
    case 23:
        if (CTY8((int)s_slot_0057c93c, 0x53fb5du) != 0) {
            if (bclick(0, 16, (int)(uintptr_t)g_arm_overlay_buttons, g_arm_mode) != 0)
                return 1;
        } else if (bclick(0, 0, (int)(uintptr_t)g_arm_overlay_buttons, g_arm_mode) != 0) {
            return 1;
        }
        break;
    case 17: if (bclick(0, 0, BTNI(g_rl_4dd388, 0x4dd388u), g_rl_55321c) != 0) return 1; break;
    case 10:
        if (rl_43582a() != 0)
            return 1;
        if (click(0, 0, HOT(0x4dc938u), 9) != 0)
            return 1;
        break;
    case 11:
        if (bclick(0, 0, (int)(uintptr_t)g_noble_buttons, g_noble_action_count) != 0)
            return 1;
        if (rl_4369bd() != 0)
            return 1;
        break;
    case 26:
        if (g_diplo_action_mode == 0) {
            if (bclick(0, 0, BTNI(g_rl_4dd9d0, 0x4dd9d0u), 4) != 0)
                return 1;
        } else if (g_diplo_action_mode > 4) {
            if (bclick(0, 0, BTNI(g_rl_4dda60, 0x4dda60u), 2) != 0)
                return 1;
        } else {
            if (bclick(0, 0, BTNI(g_rl_4dda30, 0x4dda30u), 2) != 0)
                return 1;
            editor_out((int)(uintptr_t)&AT(uint8_t, g_bf_53f1e0, 0x53f1e0u, 0x53f1f0u + 200 * g_diplo_action_mode), 199);
        }
        break;
    case 12: if (bclick(48, 80, (int)(uintptr_t)g_trade_buttons, g_trade_widget_count) != 0) return 1; break;
    case 24:
        if (click(64, 48, HOT(0x4dc7e8u), 2) != 0)
            return 1;
        if (bclick(0, 0, BTNI(g_rl_4dd538, 0x4dd538u), 6) != 0)
            return 1;
        break;
    case 13:
        if (rl_43582a() != 0)
            return 1;
        if (click(0, 0, HOT(0x4dc938u), 7) != 0)
            return 1;
        if (bclick(96, 4, BTNI(g_rl_4dd8c8, 0x4dd8c8u), 4) != 0)
            return 1;
        break;
    case 27:
        if (click(0, 0, HOT(0x4dc818u), 5) != 0)
            return 1;
        if (bclick(0, 0, BTNI(g_rl_4ddb80, 0x4ddb80u), 2) != 0)
            return 1;
        break;
    case 9: if (bclick(0, 0, (int)(uintptr_t)g_treasury_buttons, g_treasury_widget_count) != 0) return 1; break;
    case 30: if (bclick(g_rl_52af90, g_rl_52af98, BTNI(g_rl_4dd310, 0x4dd310u), 2) != 0) return 1; break;
    case 18: if (bclick(32, 48, BTNI(g_rl_4ddbb0, 0x4ddbb0u), g_combat_ctrl_408) != 0) return 1; break;
    case 29:
        if (bclick(0, 0, BTNI(g_rl_4ddf10, 0x4ddf10u), 6) != 0)
            return 1;
        if (click(0, 0, HOT2(0x4dcb18u), 2) != 0)
            return 1;
        break;
    case 31:
        if (g_rl_55cd78 != 0)
            engine_set_demand_redraw(2);
        switch (g_rl_5530f0) {
        case 1:
            if (g_rl_568464 != 0) {
                AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu) = 2;
                rl_432b05();
                return 1;
            }
            if (click(0, 0, HOT2(0x4dcb48u), 4) != 0)
                return 1;
            g_tr_selected_button = hover(0, 0, HOT2(0x4dcb48u), 4);
            break;
        case 2:
            if (click(0, 0, HOT2(0x4dcba8u), 5) != 0)
                return 1;
            g_tr_selected_button = hover(0, 0, HOT2(0x4dcba8u), 5);
            break;
        case 3:
            editor_out((int)(uintptr_t)g_rl_4ea130, 64);
            rl_4ad9f0();
            if (bclick(96, 10, BTNI(g_rl_4ddd78, 0x4ddd78u), 4) != 0)
                return 1;
            break;
        case 4:
            bf_rng_step();
            if (g_sim_mp_active == 0 || g_rl_553248 != 0) {
                if (click(0, 0, HOT2(0x4dcc20u), 7) != 0)
                    return 1;
            } else if (click(0, 0, HOT2(0x4dcc20u), 5) != 0) {
                return 1;
            }
            g_tr_selected_button = hover(0, 0, HOT2(0x4dcc20u), 7);
            g_rl_5aeb78 = 1;
            rl_401f49();
            rl_4bbe51();
            if (g_rl_53e910 == 0 && g_rl_55cd78 == 0)
                rl_40210c((int)(uintptr_t)g_bf_53f1e0, 31);
            break;
        case 7:
            if (click(0, 0, HOT2(0x4dcd28u), 3) != 0)
                return 1;
            if (click(0, 0, HOT2(0x4dcd88u), 7) != 0)
                return 1;
            if (click(0, 0, HOT2(0x4dce30u), 12) != 0)
                return 1;
            break;
        case 8:
            editor_out((int)(uintptr_t)g_rl_553e80, 64);
            rl_401246((int)(uintptr_t)g_rl_553e80);
            if (g_rl_553248 != 0) {
                if (click(0, 0, HOT2(0x4dcd28u), 4) != 0)
                    return 1;
            } else if (click(0, 0, HOT2(0x4dcd28u), 1) != 0) {
                return 1;
            }
            if (g_rl_553248 == 0)
                return 0;
            if (click(0, 0, HOT2(0x4dcd88u), 7) != 0)
                return 1;
            if (click(0, 0, HOT2(0x4dce30u), 12) != 0)
                return 1;
            engine_retail_call(0x4bbf89u, 0);   /* TODO(rl-extern): 0x4bbf89 */
            engine_retail_call(0x4bbfd2u, 0);   /* TODO(rl-extern): 0x4bbfd2 */
            break;
        case 9:
            g_tr_selected_button = rl_433ce4();
            if (g_bf_4eafb4 != 0 && g_tr_selected_button != 0) {
                rl_433a23();
                return 1;
            }
            if (click(0, 0, HOT2(0x4dce30u), 12) != 0)
                return 1;
            if (g_bf_4eafb4 != 0) {
                g_rl_5530f0 = g_rl_553e5c;
                engine_set_demand_redraw(2);
                return 1;
            }
            break;
        case 6:
            if (g_rl_553248 == 0)
                return 0;
            if (click(0, 0, HOT2(0x4dccc8u), 2) != 0)
                return 1;
            g_tr_selected_button = hover(0, 0, HOT2(0x4dccc8u), 2);
            break;
        case 5:
            if (click(0, 0, HOT2(0x4dccf8u), 2) != 0)
                return 1;
            g_tr_selected_button = hover(0, 0, HOT2(0x4dccf8u), 2);
            break;
        case 13: if (bclick(0, 0, BTNI(g_rl_4dddd8, 0x4dddd8u), 2) != 0) return 1; break;
        case 12:
        case 11:
            if (g_rl_5530f0 == 11) {
                editor_out((int)(uintptr_t)g_rl_553e80, 64);
                rl_401246((int)(uintptr_t)g_rl_553e80);
            }
            if (g_rl_56899c == 1) {
                if (bclick(0, 0, BTNI(g_rl_4de000, 0x4de000u), (int)s_slot_0056d5c0) != 0)
                    return 1;
                if (click(0, 0, (int)(uintptr_t)g_rl_4dd130, 20) != 0)
                    return 1;
            } else if (click(0, 0, (int)(uintptr_t)g_rl_4dcf50, 20) != 0) {
                return 1;
            }
            if (g_rl_5530f0 == 11) {
                if (g_rl_553248 == 0)
                    return 0;
                engine_retail_call(0x4bbf89u, 0);   /* TODO(rl-extern): 0x4bbf89 */
            }
            break;
        }
        break;
    case 33: if (bclick(g_rl_58fe20, g_rl_58fe24, BTNI(g_rl_4ddbe0, 0x4ddbe0u), 2) != 0) return 1; break;
    case 53:
    case 54:
        editor_out((int)(uintptr_t)g_rl_4ea130, 64);
        rl_4ad9f0();
        if (bclick(16, 144, BTNI(g_rl_4ddd78, 0x4ddd78u), 4) != 0)
            return 1;
        break;
    case 57: if (bclick(0, 0, BTNI(g_rl_4ddc10, 0x4ddc10u), g_rl_56d8a0) != 0) return 1; break;
    case 66: if (bclick(0, 0, BTNI(g_rl_4ddc70, 0x4ddc70u), g_rl_553f00) != 0) return 1; break;
    case 67: if (bclick(0, 0, BTNI(g_rl_4ddcb8, 0x4ddcb8u), g_rl_5533ac) != 0) return 1; break;
    case 68: if (bclick(0, 0, BTNI(g_rl_4ddfa0, 0x4ddfa0u), 4) != 0) return 1; break;
    case 49: if (bclick(0, 0, BTNI(g_rl_4ddce8, 0x4ddce8u), g_rl_52afd0) != 0) return 1; break;
    }
    return 0;
}

/* ================================================================ the frame driver */

/* the per-unit driver 0x4822ed models a siege order's stack residue from its retail entry esp (engine_bf_unit.c
 * bf_unit_e0_4822ed: the order handler's ebp, a removed unit's sprite frame); its hook adapter sets it, a port-to-port
 * call must: 4b99c0's e0 - 0x1c (push ebp, 8 B of locals, ebx / esi / edi, the call). rl_host_e0_4b99c0 = the frame
 * driver's retail entry esp — set by its row's adapter (rhost; under the port's frame guard: retail's value, quirk #239). Without it field-orders'
 * hybrid ended on another state (shadow --shadow-all: f8789 / f9601, a unit's +0x10 0x88 vs 0xd0). */
uint32_t rl_host_e0_4b99c0;
extern uint32_t bf_unit_e0_4822ed;   /* engine_bf_unit.c */

/* Port of FUN_004b99c0 (2222 B) — one frame (engine_tick; rhost's frame = one call). The MP-end notice (event 298
 * once: DAT_005533d4), the system sheet on the battlefield (DAT_0052af94 == 3: 0x499a1c(1)), the pre-tick steps
 * (0x498458, the frame counter DAT_005bb4c8, the frame delta DAT_004ea128, the phase ticks, the census / result /
 * MP steps 0x481b9a / 0x482111 / 0x4782c5 / 0x451f1a); then the sim steps 0x4bb978 asks for, each: the tick
 * DAT_0057c998, the MP cadence (messages 0xb / 0xc / 0xa, the link poll 0x440666 on the battlefield; the turn vote
 * 0x49b8b0 elsewhere, a turn in progress DAT_00553fc8 holding DAT_0056d6a4), the delayed actions (0x4472ce), the RNG
 * step (SP); the strategic sim (0x40490d, 0x49a010, 0x4650b0) or the battle (0x478952, below screen 0x2b the unit /
 * group passes, the end check 0x477dfc); a DAT_0052af94 of 10 / 11 leaves at once. Then the input state, the screen
 * change DAT_0057d340 (0x40f1a0), the scene (0x4247f5 / the map 0x4050c0 / the battlefield scroll + frame), the
 * hovered cell (DAT_005533b0 → DAT_0057a0e0 / DAT_0057d330 / 34, the region test DAT_00553f48) or the battlefield's
 * 0x47ed9b, every render step of the frame (menu bar, panels, refresh 0x4ba26e, popups, hints, tooltips, the
 * cursor kind 0x4b1cf3 by screen and state), the input dispatch 0x42ff10, the camera clamps, the fps, the palette
 * fade; a sprite error (DAT_005aeb68 == 4, not screen 0x22) reloads the sheets. (Host-level seams: rhost's frame
 * loop calls it — the row is an entry patch.) */
int rl_4b99c0(void)
{
    if (g_rl_53f228 == 1 && g_rl_53f22c != 8 && g_rl_5533d4 == 0 && g_bfr_engine_state == 3) {
        g_rl_5533d4 = 1;
        rl_472bc5(0, LOCAL, 298, 0, 0, 0, 0, 0);
    }
    if (g_bfr_engine_state == 3)
        rl_499a1c(1);
    rl_4b0aaa(0xfe);
    rl_498458();
    g_rl_5bb4c8++;
    g_rl_4ea128 = rl_4b20ed();
    rl_4bbc80();
    rl_481b9a();
    rl_482111();
    rl_4782c5();
    rl_451f1a();
    int steps = rl_4bb978();
    for (int i = 0; i < steps; i++) {
        g_bf_57c998[0]++;
        int tick = g_bf_57c998[0];
        if (PL8(LOCAL, 0x57bf04u) != 0) {
            if (tick % 1000 == 0)
                rl_43eda0(11, 0);
            if ((int)s_slot_0057c8fc == 2) {
                if (g_bf_57c998[0] % 130 == 0)
                    rl_43eda0(12, 0);
                else if (g_bf_57c998[0] % 10 == 0)
                    rl_43eda0(10, 0);
                else if (g_bf_57c998[0] % 130 == 45)
                    steps = rl_440666();
            } else {
                if (g_bf_57c998[0] % 10 == 0)
                    rl_43eda0(10, 0);
                rl_49b8b0();
                if (g_rl_56d6a4 != 0) {
                    if (g_sim_turn_progress == 0)
                        g_rl_56d6a4 = 0;
                    steps = 0;
                }
            }
        }
        rl_4472ce(g_bf_57c998[0]);
        if (g_sim_mp_active == 0)
            bf_rng_step();
        if (g_bfr_engine_state == 10 || g_bfr_engine_state == 11)
            return 0;
        if ((int)s_slot_0057c8fc == 0 && steps != 0) {
            rl_40490d();
            rl_49a010();
            engine_army_state_res_ai_step();    /* 0x49a010's frames: the driver's residue window (quirk #238) */
            engine_army_state_run(rl_evq_load(), g_sim_mp_active);   /* 0x4650b0 (row state_run) */
            rl_evq_store();
        } else if ((int)s_slot_0057c8fc == 2 && steps != 0) {
            bf_478952();
            if (engine_current_screen() < 0x2b) {
                rl_40490d();
                bf_488dfe();
                bf_unit_e0_4822ed = rl_host_e0_4b99c0 ? rl_host_e0_4b99c0 - 0x1c : 0;   /* 0x4822ed's entry esp (below) */
                bf_4822ed();
                bf_485bb1();
                bf_489401();
                bf_4859e5();
            }
            engine_retail_call(0x477dfcu, 0);   /* TODO(rl-link): engine_combat_battlefield_end_tick (an event-queue argument) */
        }
    }
    rl_49a3e6();
    rl_4b1de0();
    rl_4b191e();
    if (g_bf_57d340 != 0) {
        rl_40f1a0(g_bf_57d340);
        engine_set_demand_redraw(0);
    }
    if (g_rl_56d650 != 0) {
        engine_retail_call(0x4247f5u, 0);   /* TODO(rl-extern): 0x4247f5 */
    } else if ((int)s_slot_0057c8fc == 0) {
        if (engine_current_screen() == 0 || engine_current_screen() == 16) {
            engine_retail_call(0x4050c0u, 0);   /* TODO(rl-link): engine_map_scene_tick (engine_map_scene.c, its binder) */
            rl_475e07_res_in = 0x405ac5;        /* the scene's imprint on 0x475e07's [ebp-8] (quirk #240) */
        }
    } else if ((int)s_slot_0057c8fc == 2 && engine_current_screen() >= 0x28 && engine_current_screen() < 0x2b) {
        if (engine_territory_dirty_state()->below_status_redraw != 0) {
            engine_retail_call(0x4bc1d1u, 1, 80u);   /* TODO(rl-link): engine_battlefield_render_minimap_scroll */
            rl_45240a();
        } else {
            engine_retail_call(0x4bc1d1u, 1, 4u);    /* TODO(rl-link): as above */
            rl_4527a6(480, 24, 10, 10, 1);
        }
        engine_retail_call(0x4bc142u, 2, s_slot_0055ce70, s_slot_005678a0);   /* TODO(rl-link): engine_battlefield_render_frame */
    }
    rl_4b0aaa(0xf7);
    if ((int)s_slot_0057c8fc == 0) {
        g_rl_553ed0 = g_rl_5533b0;
        g_rl_5533b0 = rl_429ba4(0);
        if (g_rl_5533b0 != 0 && g_rl_57d338 != 0) {
            g_rl_57a0e0 = g_rl_568974 / g_rl_57d338;
            g_rl_57d330 = g_rl_57a0e0 % g_rl_52afd4;
            g_rl_57d334 = g_rl_57a0e0 / g_rl_52afd4;
        }
        g_rl_553f48 = rl_46d2a3(g_rl_57d330, g_rl_57d334, 2);
    } else if ((int)s_slot_0057c8fc == 2) {
        bf_47ed9b();
    }
    rl_4b0aaa(4);
    rl_472a31();
    rl_419c78();
    rl_40c725();
    rl_41423f();
    rl_420316();
    if (g_rl_57cac4 != 0)
        rl_45270a();
    engine_retail_call(0x40f7d3u, 1, 0u);   /* TODO(rl-link): strat_draw_territory_panel (engine_hud_panel.c, its binder) */
    rl_423530();
    rl_410f4b();
    if (AT(uint8_t, g_rl_4e31b0, 0x4e31b0u, 0x4e31f8u + engine_current_screen()) != 0)
        engine_retail_call(0x41a734u, 1, 0u);   /* TODO(rl-link): engine_hud_panel_render_endturn */
    if (g_bf_53f238 != 0) {
        if ((int)s_slot_0057c8fc == 2)
            engine_retail_call(0x423b4fu, 0);   /* TODO(rl-link): engine_mp_render_status_line (engine_mp_render.c) */
        else
            g_bf_53f238 = 0;
    }
    rl_4b0aaa(0xfd);
    rl_42dbc7(0);
    rl_4ba26e();
    rl_412795();
    rl_41298a();
    rl_423739();
    rl_4b0aaa(0x20);
    rl_476aa7();
    rl_472e46();
    rl_423ba4(2, 342);
    rl_42476b(10, 30);
    rl_44098f();
    rl_4b0aaa(0xf9);
    rl_41a639();
    engine_retail_call(0x41f15au, 0);   /* TODO(rl-link): engine_mp_render_player_indicator (engine_mp_render.c) */
    engine_retail_call(0x476e95u, 0);   /* TODO(rl-link): engine_tooltip_tick (engine_tooltip.c, its context) */
    rl_4b0aaa(0x20);
    rl_4b1dd5();
    rl_42a15f();
    int scr = engine_current_screen();
    if (scr < 0x28 || scr >= 0x2b) {
        rl_4b1cf3(g_rl_4e3098[engine_current_screen()]);
    } else if (engine_current_screen() == 0x2a) {
        rl_4b1cf3(0);
    } else if (g_bf_5533b8 != 0) {
        rl_4b1cf3(5);
    } else if (g_bf_553078 != 0) {
        if (s_slot_0053e8bc != 0) {
            if (((int)s_slot_00568968 == 6 && g_bf_553eec < 6) || ((int)s_slot_00568968 == 4 && g_bf_553eec < 4))
                rl_4b1cf3(5);
            else
                rl_4b1cf3(4);
        } else if (g_bf_553f90 != 0) {
            rl_4b1cf3(6);
        } else if (g_bf_57cac8 != 0) {
            rl_4b1cf3(6);
        } else if (g_bf_5651d0 != 0) {
            rl_4b1cf3(4);
        } else if (g_bf_4e65f4 >= 184) {
            rl_4b1cf3(0);
        } else {
            rl_4b1cf3(4);
        }
    } else if (g_bf_5651d0 == 0) {
        rl_4b1cf3(0);
    } else if (g_bf_553f90 != 0) {
        rl_4b1cf3(6);
    } else {
        rl_4b1cf3(0);
    }
    rl_45242b();
    rl_4248a3(480, 200);
    if (g_rl_568960 == 0)
        rl_424992();
    else if (g_rl_568960 == 1)
        rl_425314();
    else
        rl_425487();
    rl_41a844();
    rl_452160(0);
    rl_4b0aaa(0xfd);
    if (g_rl_565488 != 0) {
        rl_45240a();
        engine_territory_dirty_state()->below_status_redraw = 1;
    }
    rl_448d7e((int)s_slot_0057c93c);
    if (g_rl_59153c != 0)
        g_rl_59153c++;
    rl_42ff10();
    if (g_rl_5bb534 != 0) {
        rl_4343f8();
        g_rl_5bb534 = 0;
    }
    if (g_rl_5c9274 != 0)
        return 0;
    rl_429b1d();
    rl_47ed34();
    rl_426529();
    rl_4b27bf();
    rl_4b12fb();
    rl_4b0e03();
    if (g_gfx_last_error != 4)
        return 0;
    g_gfx_last_error = 0;
    if (engine_current_screen() == 0x22)
        return 0;
    rl_4afab9((int)(uintptr_t)g_rl_4e3bc8, 0, 0);
    if (g_bfr_engine_state != 3)
        rl_499034();
    else if ((int)s_slot_0057c8fc == 2) {
        bf_4987b7();
        bf_480f8b();
    } else {
        rl_4984dc();
    }
    return 0;
}

/* ================================================================ the F8 old-turn reload (0x43f7e3) */

/* Port of FUN_0043ff63 (96 B) — clear the MP resend buffers byte_515B30: 7 peers × 40 bytes, stride 41 (written by
 * the packet resend 0x43eac1). EAX: 41 * 6 (the last row's offset; nobody reads it). */
int rl_43ff63(void)
{
    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 40; j++)
            g_rl_515b30[41 * i + j] = 0;
    return 41 * 6;
}

/* Port of FUN_00446f06 (126 B) — reset the .rec replay buffer: the counters dword_51591C / dword_515B24 /
 * dword_512F6C / dword_512FA0 = 0, each of the 400 × 56 B records' state byte (+0x36) and time (+0) = 0.
 * EAX: 7 * 399 (IDA's `result = 7 * i`; nobody reads it). */
int rl_446f06(void)
{
    g_rl_51591c = 0;
    g_rl_515b24 = 0;
    g_rl_512f6c = 0;
    g_rl_512fa0 = 0;
    for (int i = 0; i < 400; i++) {
        AT(uint8_t, g_rl_50d7c0, 0x50d7c0u, 0x50d7f6 + 56 * i) = 0;
        AT(int32_t, g_rl_50d7c0, 0x50d7c0u, 0x50d7c0 + 56 * i) = 0;
    }
    return 7 * 399;
}

/* Port of FUN_0043ff08 (91 B) — clear byte_569530[6][10]. EAX: 5 * 5. */
int rl_43ff08(void)
{
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 10; j++)
            g_rl_569530[10 * i + j] = 0;
    return 5 * 5;
}

/* Port of FUN_0043ffc3 (117 B) — clear the local player's 600-byte state-hash region byte_565700[600 * DAT_0057c8cc]
 * (3 × 200; the hash 0x440231 fills it). EAX: the last row's offset 600 * player + 400. */
int rl_43ffc3(void)
{
    int r = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 200; j++) {
            r = 600 * LOCAL + 200 * i;
            g_rl_565700[r + j] = 0;
        }
    return r;
}

/* Port of FUN_0043f7e3 (114 B) — F8 (the window procedure, after message 18) and 0x442b08: reload the previous
 * turn's MP snapshot — save_load("old_turn.sva") on the host (DAT_00553248), else "old_turn.svb" (k_mp_turn_save_names
 * 0x4d5f48 / 0x4d5f58; the load's result is ignored); the render-dirty mode DAT_0052afcc = 1; clear the resend buffers,
 * the replay buffer, byte_569530, the state hash; the battle reset 0x43f304; DAT_005440c8 = DAT_0053f26c + 10;
 * DAT_00568d9c = timeGetTime, also EAX (0x442b08 returns it). */
int rl_43f7e3(void)
{
    rl_4adfe6((int)(uintptr_t)&AT(uint8_t, g_rl_4d5f28, 0x4d5f28u, g_rl_553248 != 0 ? 0x4d5f48 : 0x4d5f58));
    g_combat_render_dirty = 1;
    rl_43ff63();
    rl_446f06();
    rl_43ff08();
    rl_43ffc3();
    bf_43f304();
    s_slot_005440c8 = s_slot_0053f26c + 10;
    g_combat_anim_deadline = rl_plat_timeGetTime();
    return (int)g_combat_anim_deadline;
}

/* ================================================================ the window procedure */

static int defwnd(int w, int m, int wp, int lp)
{ return (int)rl_plat_DefWindowProcA((HWND)(uintptr_t)w, (UINT)m, (WPARAM)(uint32_t)wp, (LPARAM)lp); }
#define WMSG(off) ((int)(uintptr_t)&AT(uint8_t, g_rl_4df6b0, 0x4df6b0u, (off)))   /* the window layer's log lines */
static RECT *client_rect(void) { return (RECT *)(void *)g_rl_4ea180; }           /* DAT_004ea180 (IDA: Point) */

/* Port of FUN_004b29be (2962 B) — the window procedure (__stdcall; rhost's window layer calls it through the
 * registered class: an entry patch). WM_DESTROY: shutdown (0x42dfe2, DirectPlay 0x4b28cd(1), 0x425d35, 0x4b7180(1),
 * PostQuitMessage). WM_MOVE / WM_SIZE: minimized (pause 0x4b26b8) or restored (0x4b26f8, active), the client rect
 * DAT_004ea180 (fullscreen: the screen; else the client area in screen coordinates), redraw 2, a full repaint
 * (0x451f70) or the movie (0x42e50d on screen 0x22). WM_PAINT: the movie's (0x42e3ba) or Begin/EndPaint (+ a repaint
 * when inactive); 1. WM_CLOSE: the error latch 3. WM_ACTIVATEAPP: active (foreground, not minimized) → log; with the
 * graphics up (DAT_004e65d0) restore the surfaces (0x4b2730) or re-initialize (0x4b2922), unpause; a repaint.
 * WM_DISPLAYCHANGE: depth / width / height. WM_KEYDOWN: the editor keys (0x401c70.., Enter 0x401c5b + 0x43600c), Ctrl,
 * Esc (0x4343f8 on the battlefield, else the quit latch 1: returns 0), the speed digits (Ctrl: 0x43c885, else
 * 0x43c910), F1 help, F2 the overlay cycle DAT_00568960, the debug keys (F3..F9, F12; DAT_0053f684), F5 re-centres
 * a window. WM_KEYUP Ctrl, WM_CHAR (battlefield h / v: 0x43c77a; a popup's timer to 5 s; the editor 0x401a20),
 * WM_SYSKEYUP Alt+1..5 / Alt+X; the mouse: position DAT_004eabbe / c0, buttons DAT_004eabc2, double clicks
 * DAT_004eada1; WM_QUERYNEWPALETTE / WM_PALETTECHANGED (another window's) re-set the palette (0x42f1df(1), windowed).
 * Everything else, and the handled messages, pass to DefWindowProcA. */
int rl_4b29be(int hwnd, int msg, int wparam, int lparam)
{
    switch ((uint32_t)msg) {
    case 2:   /* WM_DESTROY */
        AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u) = 0;   /* hWndMain */
        rl_42dfe2();
        rl_4b28cd(1);
        rl_425d35();
        rl_4b7180(1);
        rl_plat_PostQuitMessage(0);
        break;
    case 3:   /* WM_MOVE */
    case 5:   /* WM_SIZE */
        if (rl_plat_IsIconic((HWND)(uintptr_t)hwnd)) {
            rl_4afab9(WMSG(0x4df73cu), 0, 0);
            engine_retail_call(0x4b26b8u, 0);   /* TODO(rl-extern): 0x4b26b8 (pause) */
        } else {
            rl_4b26f8();
            g_rl_4e65a8 = 1;
        }
        if (g_rl_53f228 != 0) {
            int cy = rl_plat_GetSystemMetrics(1);
            int cx = rl_plat_GetSystemMetrics(0);
            rl_plat_SetRect(client_rect(), 0, 0, cx, cy);
        } else {
            rl_plat_GetClientRect((HWND)(uintptr_t)hwnd, client_rect());
            rl_plat_ClientToScreen((HWND)(uintptr_t)hwnd, (POINT *)(void *)client_rect());
            rl_plat_ClientToScreen((HWND)(uintptr_t)hwnd, (POINT *)(void *)(g_rl_4ea180 + 8));
        }
        rl_4afab9(WMSG(0x4df750u), 0, 0);
        engine_set_demand_redraw(2);
        if (g_rl_4df290 != 1) {
            if (g_bfr_engine_state != 0)
                rl_451f70();
            else if (engine_current_screen() == 0x22)
                engine_retail_call(0x42e50du, 0);   /* TODO(rl-extern): 0x42e50d */
        }
        break;
    case 15: {   /* WM_PAINT */
        if (rl_42e26f() != 0) {
            engine_retail_call(0x42e3bau, 1, (uint32_t)hwnd);   /* TODO(rl-extern): 0x42e3ba (the movie's paint) */
        } else {
            PAINTSTRUCT ps;
            rl_plat_BeginPaint((HWND)(uintptr_t)hwnd, &ps);
            rl_plat_EndPaint((HWND)(uintptr_t)hwnd, &ps);
            if (g_rl_4e65a8 == 0)
                rl_451f70();
        }
        return 1;
    }
    case 16:   /* WM_CLOSE */
        g_gfx_last_error = 3;
        break;
    case 28:   /* WM_ACTIVATEAPP */
        if (wparam != 0 && (int)(uintptr_t)rl_plat_GetForegroundWindow() == hwnd &&
            !rl_plat_IsIconic((HWND)(uintptr_t)hwnd))
            g_rl_4e65a8 = 1;
        else
            g_rl_4e65a8 = 0;
        if (g_rl_4e65a8 != 0)
            rl_4afab9(WMSG(0x4df764u), 0, 0);
        else
            rl_4afab9(WMSG(0x4df774u), 0, 0);
        if (g_rl_4e65d0 != 0 && g_rl_4e65a8 != 0) {
            if (engine_retail_call(0x4b2730u, 0) != 0) {   /* TODO(rl-extern): 0x4b2730 (gfx_restore) */
                rl_4b26f8();
            } else if ((int)(uintptr_t)rl_plat_GetForegroundWindow() == hwnd) {
                rl_4afab9(WMSG(0x4df788u), 0, 0);
                if (rl_4b2922() != 0)
                    rl_4b26f8();
            }
        }
        if (g_rl_4df290 != 1 && g_bfr_engine_state != 0)
            rl_451f70();
        break;
    case 126:   /* WM_DISPLAYCHANGE */
        g_rl_4eabd8 = wparam;
        g_rl_4e65a0 = (int32_t)((uint32_t)lparam & 0xffffu);
        g_rl_4ea838 = (int32_t)(((uint32_t)lparam >> 16) & 0xffffu);
        break;
    case 0x100:   /* WM_KEYDOWN */
        g_rl_4eabb4 = 1;
        switch ((uint32_t)wparam) {
        case 8: rl_401c70(); break;
        case 13: rl_401c5b(); rl_43600c(); break;
        case 17: g_rl_4df3a8 = 1; break;
        case 27:
            if (g_bfr_engine_state == 3)
                rl_4343f8();
            else
                g_gfx_last_error = 1;
            return 0;
        case 35: rl_401d11(); rl_4360f2(); break;
        case 36: rl_401cfc(); break;
        case 37:
            if (g_bf_553e68 > 0)
                g_bf_553e68--;
            rl_401cbc();
            break;
        case 39:
            if (g_bf_553e68 < 80)
                g_bf_553e68++;
            rl_401cda();
            break;
        case 45: rl_401ca3(); break;
        case 46: rl_401c93(); break;
        case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57:
            if (g_rl_4df3a8 != 0)
                rl_43c885(wparam);
            else
                rl_43c910(wparam);
            break;
        case 112: rl_plat_WinHelpA((HWND)(uintptr_t)hwnd, (const char *)(uintptr_t)WMSG(0x4df7acu), 3, 1); break;
        case 113:
            if (g_rl_568960 == 0)
                g_rl_568960 = 1;
            else if (g_rl_568960 == 1)
                g_rl_568960 = 2;
            else
                g_rl_568960 = 0;
            if (engine_current_screen() == 9)
                g_rl_553e60 = g_rl_553e60 == 0;
            break;
        case 114:
            if (g_rl_53f684 == 1)
                g_rl_55402c = g_rl_55402c == 0;
            break;
        case 115:
            if (g_rl_53f684 == 1) {
                if ((int)s_slot_0057c8fc == 0) {
                    AT(int32_t, g_per_army_768, 0x53f9b0u, 0x5402d4u)++;
                } else {
                    for (int i = 1; i <= 20; i++)
                        BS16(i, 0x55461au)++;
                }
            }
            break;
        case 116:
            if (g_rl_53f228 == 0) {
                RECT r;
                rl_plat_GetClientRect((HWND)(uintptr_t)hwnd, &r);
                rl_plat_SetRect(&r, 0, 0, g_rl_4eb250, g_rl_4eb254);
                LONG ex = rl_plat_GetWindowLongA((HWND)(uintptr_t)hwnd, -20);
                BOOL menu = rl_plat_GetMenu((HWND)(uintptr_t)hwnd) != NULL;
                LONG st = rl_plat_GetWindowLongA((HWND)(uintptr_t)hwnd, -16);
                rl_plat_AdjustWindowRectEx(&r, (DWORD)st, menu, (DWORD)ex);
                int x = (g_rl_4e65a0 - 640) / 2;
                int y = (g_rl_4ea838 - 480) / 2;
                rl_plat_SetWindowPos((HWND)(uintptr_t)hwnd, NULL, x, y, r.right - r.left, r.bottom - r.top, 0x14);
            }
            break;
        case 117:
            if (g_rl_53f684 != 0)
                rl_43eda0(19, 0);
            break;
        case 118:
            if (g_rl_53f684 == 1)
                rl_43eda0(26, 0);
            break;
        case 119:
            rl_43eda0(18, 0);
            rl_43f7e3();
            break;
        case 120:
            if (engine_current_screen() == 32 && g_sim_mp_active != 0)
                g_rl_52eff8 = g_rl_52eff8 == 0;
            break;
        case 123:
            g_rl_553018 = 1;
            if (engine_current_screen() == 9 && (int)s_slot_0057cb18 == 2)
                g_rl_553018 = g_rl_553018 == 0;
            break;
        }
        break;
    case 0x101:   /* WM_KEYUP */
        if (wparam == 17)
            g_rl_4df3a8 = 0;
        break;
    case 0x102:   /* WM_CHAR */
        if ((int)s_slot_0057c8fc != 2 || g_rl_57a0cc != 0) {
            if (g_rl_57a0cc != 0 && g_rl_57a0cc < 5000)
                g_rl_57a0cc = 5000;
        } else if (wparam == 'h' || wparam == 'H') {
            rl_43c77a(0);
        } else if (wparam == 'v' || wparam == 'V') {
            rl_43c77a(1);
        }
        rl_401a20(wparam);
        break;
    case 0x105:   /* WM_SYSKEYUP */
        switch ((uint32_t)wparam) {
        case '1': rl_408ea4(1); break;
        case '2': rl_4477e7(2); break;
        case '3': rl_4477e7(3); break;
        case '4': rl_4477e7(4); break;
        case '5': rl_4477e7(5); break;
        case 'X': rl_plat_PostMessageA((HWND)(uintptr_t)hwnd, 0x100, 0x1b, 0); break;
        }
        break;
    case 0x200:   /* WM_MOUSEMOVE */
        g_rl_4eabbe = (int16_t)lparam;
        g_rl_4eabc0 = (int16_t)((uint32_t)lparam >> 16);
        break;
    case 0x201: g_rl_4eabc2 |= 1; break;
    case 0x202: g_rl_4eabc2 &= (uint8_t)~1u; break;
    case 0x203: g_rl_4eada1 |= 1; break;
    case 0x204: g_rl_4eabc2 |= 2; break;
    case 0x205: g_rl_4eabc2 &= (uint8_t)~2u; break;
    case 0x206: g_rl_4eada1 |= 2; break;
    case 0x30f:   /* WM_QUERYNEWPALETTE */
    case 0x311:   /* WM_PALETTECHANGED */
        if ((uint32_t)msg == 0x311 && wparam == hwnd)
            break;
        if (g_rl_53f228 == 0 && AT(int32_t, g_rl_4eabd0, 0x4eabd0u, 0x4eabd0u) != 0)
            rl_42f1df(1);
        break;
    }
    return defwnd(hwnd, msg, wparam, lparam);
}
