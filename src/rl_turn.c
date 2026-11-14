/* rl_turn.c — a retail-layout port, partition 'turn' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The 0x47309e..0x4a57ab band: the event popup's scroll (render, dismiss, input), the help hints, the load
 * tail, game init (the seat shuffle, the realms setup, the NPC schedule, the custom / scenario configs), the
 * turn substate 0, the save rotation, the rankings, the roster, the AI's realm passes (garrison / reinforce,
 * the campaign helpers, the diplomacy inbox, the personality dispatchers), diplomacy, the combat pool commit.
 * Route (a) — the old port is linked in rhost and on retail's layout: rl_<va> forwards to it. Route (b) — a
 * fresh port here. Findings: docs/findings/rl-turn.md. */
#include "rl.h"
#include "engine_army_ai.h"     /* the AI's realm passes (engine_army_ai.c, linked) */
#include "engine_diplomacy.h"   /* treaties / alliances (engine_diplomacy.c, linked) */
#include "engine_roster.h"      /* the seat roster (engine_roster.c, linked) */
#include "engine_camera.h"      /* the anchor clamp (engine_camera.c, linked) */

/* route (a): the old ports' file-statics made extern for their wrappers (engine_army_ai.c, engine_combat.c) */
int  engine_army_ai_realm_enemy_strength(int player, int realm);
int  engine_army_ai_realm_own_strength(int player, int realm);
void engine_army_ai_garrison_keeps(int player);
void engine_army_ai_reinforce_realms(int player);
int  engine_army_ai_fund_campaign(int player);
void engine_army_ai_assign_deployment(int player, int occ);
void engine_army_ai_pick_target_owner(int player);
void engine_combat_pool_commit(int owner, int klass);

/* ================================================================ route (a): wrappers */

/* Port of FUN_0047ed34 (103 B) — clamp the strategic camera anchor DAT_0055ce70 / DAT_005678a0 to 0..0x41 /
 * 0..0x42 (engine_camera_clamp_anchor on the save-block scalars). */
int rl_47ed34(void)
{
    engine_camera_clamp_anchor((int *)&s_slot_0055ce70, (int *)&s_slot_005678a0);
    return 0;
}

/* Port of FUN_0049bae9 (180 B) — seat a player: has-data, the aux in-use flag, the 31-byte name, the id
 * (999 = keep), SP: the job slot (player_seat_assign). */
int rl_49bae9(int a0, int a1, int a2)
{
    player_seat_assign(a0, a1, (const void *)(uintptr_t)(uint32_t)a2);
    return 0;
}

/* Port of FUN_0049bbe8 (53 B) — the roster pair {army, seat} at seat; seat != 0 counts (player_roster_add). */
int rl_49bbe8(int a0, int a1) { player_roster_add((uint8_t)a0, a1); return 0; }

/* Port of FUN_0049bc1d (66 B) — zero the six roster pairs (player_roster_clear). */
int rl_49bc1d(void) { player_roster_clear(); return 0; }

/* Port of FUN_0049bc5f (314 B) — compact the roster to the active seats (player_roster_compact; quirk #106). */
int rl_49bc5f(void) { player_roster_compact(); return 0; }

/* Port of FUN_0049ca32 (46 B) — the five one-hot visibility bytes (player_seed_visibility_bits). */
int rl_49ca32(void) { player_seed_visibility_bits(); return 0; }

/* Port of FUN_004a1e6c (117 B) — the first active non-local realm into DAT_00553fbc
 * (player_first_active_nonlocal). */
int rl_4a1e6c(void) { player_first_active_nonlocal(); return 0; }

/* Port of FUN_004a1241 (357 B) — the best alliance candidate for a realm, 0 if none
 * (engine_diplomacy_best_ally_candidate). */
int rl_4a1241(int a0) { return engine_diplomacy_best_ally_candidate(a0); }

/* Port of FUN_004a1774 (211 B) — forge a mutual lord / vassal bond (engine_diplomacy_form_alliance). */
int rl_4a1774(int a0, int a1) { engine_diplomacy_form_alliance(a0, a1); return 0; }

/* Port of FUN_004a1847 (525 B) — the per-turn treaty reconciliation (engine_diplomacy_reconcile_treaties). */
int rl_4a1847(void) { engine_diplomacy_reconcile_treaties(); return 0; }

/* Port of FUN_004a1a54 (213 B) — break a lord / vassal bond both ways (engine_diplomacy_clear_treaty). */
int rl_4a1a54(int a0, int a1) { engine_diplomacy_clear_treaty(a0, a1); return 0; }

/* Port of FUN_004a24d1 (179 B) — every other realm's relation toward a realm drops by an amount, clamped at
 * -30 (engine_diplomacy_broadcast_penalty). */
int rl_4a24d1(int a0, int a1) { engine_diplomacy_broadcast_penalty(a0, a1); return 0; }

/* Port of FUN_0049f12f (770 B) — AI step 7: garrison the realm's keeps (engine_army_ai_garrison_keeps). */
int rl_49f12f(int a0) { engine_army_ai_garrison_keeps(a0); return 0; }

/* Port of FUN_0049f431 (782 B) — AI step 7: reinforce the threatened counties (engine_army_ai_reinforce_realms). */
int rl_49f431(int a0) { engine_army_ai_reinforce_realms(a0); return 0; }

/* Port of FUN_0049f73f (273 B) — the enemy head-count standing in a county (engine_army_ai_realm_enemy_strength). */
int rl_49f73f(int a0, int a1) { return engine_army_ai_realm_enemy_strength(a0, a1); }

/* Port of FUN_0049f850 (237 B) — the realm's own head-count standing in a county
 * (engine_army_ai_realm_own_strength). */
int rl_49f850(int a0, int a1) { return engine_army_ai_realm_own_strength(a0, a1); }

/* Port of FUN_0049fcc5 (224 B) — AI step 9: an easy AI banks campaign reserves (engine_army_ai_fund_campaign). */
int rl_49fcc5(int a0) { return engine_army_ai_fund_campaign(a0); }

/* Port of FUN_0049fda5 (460 B) — AI step 9: stamp an army with the campaign march order and its destination
 * cell (engine_army_ai_assign_deployment). */
int rl_49fda5(int a0, int a1) { engine_army_ai_assign_deployment(a0, a1); return 0; }

/* Port of FUN_004a01ea (287 B) — AI step 9: the campaign's target-owner gate +0x48
 * (engine_army_ai_pick_target_owner). */
int rl_4a01ea(int a0) { engine_army_ai_pick_target_owner(a0); return 0; }

/* Port of FUN_004a0649 (425 B) — the best adjacent enemy county to attack from a county
 * (engine_army_ai_pick_attack_target). */
int rl_4a0649(int a0, int a1) { return engine_army_ai_pick_attack_target(a0, a1); }

/* Port of FUN_004a3c67 (620 B) — an independent county's balanced dispatch
 * (engine_army_ai_dispatch_independent_balanced). */
int rl_4a3c67(int a0) { engine_army_ai_dispatch_independent_balanced(a0); return 0; }

/* Port of FUN_004a4052 (657 B) — personality 0's county dispatch (engine_army_ai_dispatch_balanced). */
int rl_4a4052(int a0) { engine_army_ai_dispatch_balanced(a0); return 0; }

/* Port of FUN_004a440f (645 B) — personality 9's county dispatch (engine_army_ai_dispatch_expansionist). */
int rl_4a440f(int a0) { engine_army_ai_dispatch_expansionist(a0); return 0; }

/* Port of FUN_004a4a3f (211 B) — a sell order at the base price (engine_army_ai_sell_order). */
int rl_4a4a3f(int a0, int a1, int a2) { engine_army_ai_sell_order(a0, a1, a2); return 0; }

/* Port of FUN_004a545f (188 B) — commit a troop class's reserve from the primed muster pool
 * (engine_combat_pool_commit). */
int rl_4a545f(int a0, int a1) { engine_combat_pool_commit(a0, a1); return 0; }

/* ================================================================ route (b): fresh ports on retail's layout */
#include "rl_turn_globals.h"
#include "rl_mpnet.h"             /* rl_475e07 + its residue model */
#include "rl_io_plat.h"         /* timeGetTime, malloc, exit, remove, rename */
#include "engine.h"             /* g_sim_mp_active (DAT_00553030), the screen / redraw bridges */
#include "engine_combat.h"      /* g_combat_flag_530d0, g_combat_flag_53f284, g_combat_anim_scratch */
#include "engine_territory.h"   /* the bound dirty state (DAT_0053e9ac: a mirror), the current slot (DAT_0052afe0) */
#include "engine_diplomacy_action.h"   /* g_diplo_action_taken (DAT_0052afa4) */
#include "engine_title_render.h"       /* g_tr_options (DAT_0053f288..DAT_0053f2b4) */
#include "engine_bf_input_globals.h"   /* g_bf_4e6900 (a key), g_bf_4eafb4 (a click) */
#include "gfx_globals.h"               /* g_gfx_screen_byte (DAT_004eac50), g_gfx_last_error (DAT_005aeb68) */
#include "engine_mapview_globals.h"    /* the terrain / feature / overlay banks DAT_00568208 .. */
#include "engine_workforce_hud.h"      /* g_wf_panel_mode (DAT_0053f050), the villager sheets DAT_0053e918 / DAT_005533c4 */
#include "rl_render_globals.h"         /* the system files' buffers (palettes, fonts, sheets) */
#include "engine_army_state.h"         /* the marches / raids / siege scan (engine_army_state.c, linked, verified) */
#include "engine_army_economy.h"       /* provision / plan (engine_army_economy.c, linked, verified) */
#include "gfx_line.h"
#include "gfx_panel.h"
#include "gfx_sprite_render.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
#include "gfx_wrap.h"
#include "localization.h"

/* other partitions' globals (their headers conflict in one unit: declared here as there) */
extern int32_t g_rl_52f004;          /* DAT_0052f004 (rl_io_globals.h) */
extern int32_t g_rl_53f0c4;          /* DAT_0053f0c4 (rl_io_globals.h): the game kind (10 / 11: the tutorial / scenario end) */
extern int32_t g_rl_53f278;          /* DAT_0053f278 (rl_io_globals.h) */
extern int32_t g_rl_544028, g_rl_544038, g_rl_544040, g_rl_544044;   /* rl_io_globals.h */
extern int32_t g_rl_553018;          /* DAT_00553018 (rl_glue_globals.h) */
extern int32_t g_rl_5533b0;          /* DAT_005533b0 (rl_glue_globals.h) */
extern int32_t g_rl_56d658;          /* DAT_0056d658 (rl_glue_globals.h) */
extern int32_t g_rl_57d338;          /* DAT_0057d338 (rl_glue_globals.h) */
extern uint8_t g_rl_53f6a0[0x300];   /* DAT_0053f6a0 (rl_glue_globals.h): the muster rows, 128 B per realm (+0x70: the total) */
extern int32_t g_rl_52afd4;          /* DAT_0052afd4 (rl_render_globals.h) */
extern int32_t g_rl_4ea830;          /* DAT_004ea830 (rl_misc_globals.h) */
extern int32_t g_rl_53f0e0;          /* DAT_0053f0e0 (rl_skirmish_globals.h) */
extern int32_t g_rl_57c968;          /* DAT_0057c968 (rl_ui2_globals.h) */
extern int32_t g_rl_56d8b4;          /* DAT_0056d8b4 (rl_io_globals.h): the MP clock base */
extern uint8_t g_rl_4dc1ce[0x12];    /* DAT_004dc1ce (rl_skirmish_globals.h): the job slots' (+8, +9) pairs */
extern int g_sim_turn_progress;      /* DAT_00553fc8 (engine_turn.h) */
extern int32_t g_rl_53f24c;          /* DAT_0053f24c (rl_ui2_globals.h): the help hints option */
extern int32_t g_bfr_engine_state;   /* DAT_0052af94 (engine_battlefield_render.h): the engine state (3 = in game) */

/* ---------------------------------------------------------------- records and retail's globals */
#define PL8(p, va)    AT(uint8_t,  g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))   /* realm record */
#define PLS8(p, va)   AT(int8_t,   g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define PL16(p, va)   AT(int16_t,  g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define PL32(p, va)   AT(int32_t,  g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define CO8(c, va)    AT(uint8_t,  g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))   /* county record */
#define CO32(c, va)   AT(int32_t,  g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define CO16(c, va)   AT(int16_t,  g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define STRVA(blk, blkva, va) ((int)(uintptr_t)&AT(uint8_t, blk, blkva, va))   /* a retail string / table, as the int arg */
#define LOCAL       ((int)s_slot_0057c8cc)                    /* DAT_0057c8cc: the local realm */
#define MP_ON       (g_sim_mp_active != 0)                    /* DAT_00553030 */
#define SCREEN      ((int)(uint8_t)engine_current_screen())   /* DAT_004eac50 (byte, movzx) */
#define SET_SCREEN(s) engine_set_current_screen(s)
#define REDRAW(m)   engine_set_demand_redraw(m)               /* DAT_0057d340 */
#define BELOW_STATUS() (engine_territory_dirty_state()->below_status_redraw)   /* DAT_0053e9ac (a mirror) */
#define POPUP_MSG   g_rl_53e910                                /* DAT_0053e910: the open popup's event code (0: none) */

static int evq_post(int a, int b, int code, int d, int e, int f, int g, int h)   /* the event queue post 0x472bc5 */
{
    return rl_472bc5(a, b, code, d, e, f, g, h);
}

/* ---------------------------------------------------------------- the event popup (scroll) */

/* Port of FUN_00476e21 (116 B) — leave the report screen 0x27: back to the screen it was opened from
 * (DAT_004f0350; a nested 0x27 → the map), the popup's page count back to 20, a redraw (1 on screen 0x17,
 * else 2). EAX: the screen byte. */
int rl_476e21(void)
{
    if (SCREEN == 0x27) {
        SET_SCREEN((uint8_t)g_rl_4f0350);
        if (SCREEN == 0x27)
            SET_SCREEN(0);
        g_rl_4f0358 = 0x14;
        REDRAW(SCREEN == 0x17 ? 1 : 2);
    }
    return SCREEN;
}

/* Port of FUN_00476768 (245 B) — dismiss the event popup: a game-ending event (0xef / 0xee / 0x9f / 0x9e) logs
 * "exited from game flow", goes to screen 0x26, stops the music stream and latches error 3; the cue is released
 * unless the event is 0xc2; the full-screen mark, the report screen's restore, the queue's cooldown and the open
 * code cleared, the scroll gate DAT_0055cd78 = 2, the redraw gates; a tutorial / scenario end (DAT_0053f0c4 11 / 10)
 * runs 0x497879 and goes to screen 0x1c. */
int rl_476768(void)
{
    if (POPUP_MSG == 0xef || POPUP_MSG == 0xee || POPUP_MSG == 0x9f || POPUP_MSG == 0x9e) {
        rl_4afab9(STRVA(g_rl_4d7094, 0x4d7094u, 0x4d7094u), 0, 0);   /* "OK :exited from game flow." */
        SET_SCREEN(0x26);
        rl_426d11(0);
        g_gfx_last_error = 3;
    }
    if (POPUP_MSG != 0xc2)
        rl_427d19();
    rl_45240a();
    int ret = rl_476e21();
    g_rl_57c954 = 0;
    POPUP_MSG = 0;
    g_rl_55cd78 = 2;
    BELOW_STATUS() = 1;
    g_combat_flag_530d0 = 1;
    if (g_rl_53f0c4 == 11 || g_rl_53f0c4 == 10) {
        ret = rl_497879();
        SET_SCREEN(0x1c);
        REDRAW(1);
    }
    return ret;   /* EAX: 0x476e21's (the screen byte), or 0x497879's */
}

/* Port of FUN_00476710 (88 B) — dismiss the popup unless the open message panel (DAT_005440a4) is 17 / 10 / 11 / 12
 * (the panels with their own buttons). */
int rl_476710(void)
{
    if (g_rl_5440a4 != 0x11 && g_rl_5440a4 != 0x0a && g_rl_5440a4 != 0x0b && g_rl_5440a4 != 0x0c)
        return rl_476768();
    return 0;   /* retail: the caller's EAX */
}

/* Port of FUN_0047685d (512 B) — the popup's input step (the in-game dispatcher): count the scroll gate
 * DAT_0055cd78 down; no popup → 0. A key (DAT_004e6900) dismisses; the message panels 17 / 10 / 11 / 12 run their
 * button tables (0x40da1e, the tables 0x4ddb50 / 0x4dda90 / 0x4ddac0, panel 12 by the event: 0xf8 / 0xfa / 0xfb);
 * else a click (DAT_004eafb4) on the 48 × 48 seal at (DAT_005681dc - 12, DAT_0056950c - 12) dismisses (and, with
 * DAT_005533b0, marks the diplomacy action taken). 1 when it took the input. */
int rl_47685d(void)
{
    if (g_rl_55cd78 != 0)
        g_rl_55cd78--;
    if (POPUP_MSG == 0)
        return 0;
    if (g_bf_4e6900 != 0) {
        rl_476768();
        return 1;
    }
    if (g_rl_5440a4 == 0x11 && rl_40da1e(0, 0, STRVA(g_rl_4dda90, 0x4dda90u, 0x4ddb50u), 2) != 0)
        return 1;
    if (g_rl_5440a4 == 0x0a && rl_40da1e(0, 0, STRVA(g_rl_4dda90, 0x4dda90u, 0x4dda90u), 2) != 0)
        return 1;
    if (g_rl_5440a4 == 0x0b && rl_40da1e(0, 0, STRVA(g_rl_4dda90, 0x4dda90u, 0x4ddac0u), 2) != 0)
        return 1;
    if (g_rl_5440a4 == 0x0c) {
        if (POPUP_MSG == 0xf8 && rl_40da1e(0, 0, STRVA(g_rl_4dda90, 0x4dda90u, 0x4ddac0u), 2) != 0)
            return 1;
        if (POPUP_MSG == 0xfa && rl_40da1e(0, 0, STRVA(g_rl_4dda90, 0x4dda90u, 0x4ddaf0u), 2) != 0)
            return 1;
        if (POPUP_MSG == 0xfb && rl_40da1e(0, 0, STRVA(g_rl_4dda90, 0x4dda90u, 0x4ddb20u), 2) != 0)
            return 1;
    }
    if (g_bf_4eafb4 == 0)
        return 0;
    if (rl_4b1deb(g_rl_5681dc - 12, g_rl_56950c - 12, 0x30, 0x30) == 0)
        return 0;
    if (g_rl_5533b0 != 0)
        g_diplo_action_taken = 1;
    rl_4b18e3();
    rl_476768();
    return 1;
}

/* Port of FUN_00476a5d (74 B) — clear the 20 report-seen flags DAT_004f0360, the return screen DAT_004f0350, the
 * page count DAT_004f0358 = 20. EAX: 19 (the loop index's last load). */
int rl_476a5d(void)
{
    for (int i = 0; i < 0x14; i++)
        g_rl_4f0360[i] = 0;
    g_rl_4f0350 = 0;
    g_rl_4f0358 = 0x14;
    return 0x13;
}

/* Port of FUN_00476da9 (120 B) — open report `a0` on screen 0x27 (unless already there): remember the screen,
 * mark the report shown (DAT_004f0298[a0]), DAT_0052f004 = 0, post its event (code a0, the report's byte
 * DAT_004d6ed8[a0] as the 5th argument) for the local realm. */
int rl_476da9(int a0)
{
    if (SCREEN == 0x27)
        return SCREEN;
    g_rl_4f0350 = SCREEN;
    SET_SCREEN(0x27);
    int v = AT(uint8_t, g_rl_4d6e88, 0x4d6e88u, 0x4d6ed8 + a0);
    /* byte_4F0298[a0]: the only caller (0x476aa7) passes the hints 200..218 = the seen flags DAT_004f0360[a0 - 200]
     * (one array in retail; the standalone keeps the two blocks apart) */
    if (a0 >= 200 && a0 < 220)
        g_rl_4f0360[a0 - 200] = 1;
    else
        AT(uint8_t, g_rl_4f0298, 0x4f0298u, 0x4f0298 + a0) = 1;
    g_rl_52f004 = 0;
    return evq_post(0, LOCAL, a0, 0, v, 0, 0, 0);
}

/* ---------------------------------------------------------------- the popup's render (the gfx layer, by name) */

extern uint8_t g_rl_4f0340[16];      /* DAT_004f0340 (rl_glue_globals.h): the end movie's name */
extern uint8_t g_rl_4eeb80[5912];    /* DAT_004eeb80 (rl_glue_globals.h): the 80 x 76 portrait buffer */
extern int32_t g_rl_53f5f4;          /* DAT_0053f5f4 (rl_io_globals.h): the high-resolution movies */
extern int32_t g_rl_553ed4;          /* DAT_00553ed4 (rl_io_globals.h): the county movie's rotation */
extern int32_t g_rl_553f0c, g_rl_553f5c, g_rl_55cd54, g_rl_57c8b8;   /* rl_glue_globals.h */

#define FONT       ((const uint8_t *)g_bf_5af8f0)        /* &DAT_005af8f0: the default 16 px font */
#define FONT_TALL  ((const uint8_t *)g_rl_5b2fa0)        /* &DAT_005b2fa0: the tall 24 px font */
#define CHROME     ((const uint8_t *)g_rl_57d3d0)        /* &DAT_0057d3d0: the chrome sheet */
#define SYSTEM     ((const uint8_t *)g_rl_5bb540)        /* &DAT_005bb540: system2.pl8 (the seal) */
#define GLYPHS     ((const uint8_t *)g_rl_4d71d0 + 0x20) /* DAT_004d71f0: the renderers' char -> glyph table */
#define METRICS    ((const unsigned char *)g_rl_4d71d0)  /* DAT_004d71d0: the measurers' table */
#define CSTR(blk, blkva, va) ((const char *)&AT(char, blk, blkva, va))
#define NAME(p)    (&g_player_aux_44[44 * (p) + 4])     /* a realm's name (aux +4) */

/* the localized-text walker: the port's localization_state_t over retail's blob / pointer, loaded and stored around
 * each call (the wrap renderers move the pointer; rl_render.c's pattern) */
static localization_state_t s_loc;
static localization_state_t *loc_in(void)
{
    s_loc.blob = g_rl_591580;
    s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
    return &s_loc;
}
static void loc_out(void)
{
    uint8_t *w = g_rl_591580 + s_loc.walker_offset;
    if (g_rl_5c9270 != w) g_rl_5c9270 = w;
}
static void wrap_line(int idx, int skip, int x, int y, const uint8_t *font)   /* 0x402d37 */
{
    gfx_wrap_localized_line(loc_in(), idx, skip, x, y, font, GLYPHS, 0x3f);
    loc_out();
}
static void wrap_line_c(int idx, int skip, int x, int y, int w, const uint8_t *font)   /* 0x402c5e */
{
    gfx_wrap_localized_line_centered(loc_in(), idx, skip, x, y, w, font, GLYPHS, METRICS, 0x3f);
    loc_out();
}
static void wrap_loc(int idx, int skip, int x, int y, int w1, int n1)   /* 0x40328e (the default font; x2 = w2 = 0) */
{
    gfx_wrap_localized(loc_in(), idx, skip, x, y, w1, n1, 0, 0, FONT, GLYPHS, METRICS, 0x3f);
    loc_out();
}
static void text_line(const uint8_t *buf, int x, int y, const uint8_t *font)   /* 0x402637 */
{
    gfx_text_render_line(buf, x, y, font, GLYPHS, 0x3f);
}
static void text_line_c(const uint8_t *buf, int x, int y, int w, const uint8_t *font)   /* 0x4025d7 */
{
    gfx_text_render_line_centered(buf, x, y, w, font, GLYPHS, METRICS, 0x3f);
}
static void label(int v, const char *suffix, int x, int y)   /* 0x402f64 ('@' pad, the default font) */
{
    gfx_text_label_render_dec(v, 0x40, suffix, x, y, FONT, GLYPHS, 0x3f);
}
static void num_units(int v, int noun, int x, int y)   /* TODO(rl-link): labor_num_units 0x41ab67 (engine_labor.c: a static) */
{
    engine_retail_call(0x41ab67u, 6, (uint32_t)v, (uint32_t)noun, (uint32_t)x, (uint32_t)y,
                       (uint32_t)(uintptr_t)FONT, 0x3fu);
}
static void button_strip(int table_va)   /* 0x40cfd2 = engine_button_render_list (linked), through retail as rl_render's:
                                            its pressed-button seam is bound to the gfx table's own dirty copy */
{
    engine_retail_call(0x40cfd2u, 4, 0u, 0u, (uint32_t)STRVA(g_rl_4dda90, 0x4dda90u, table_va), 2u);
}
static void panel(int x, int y, int cols, int rows) { gfx_panel_draw_bevel(CHROME, x, y, cols, rows); }   /* 0x4093e0 */
static void seal(int x, int y) { gfx_sprite_seal_draw(SYSTEM, x, y, 0); }                                  /* 0x40d1bc */
static void portrait_frame(int x, int y)   /* the 80 x 76 portrait: 0x403deb frame + 0x40a01d DAT_004eeb80 */
{
    gfx_line_rect_bevel_dark_br(x + 15, y + 17, 0x52, 0x4e);
    gfx_sprite_render_raw(g_rl_4eeb80, x + 16, y + 18, 0x50, 0x4c);
}

#define PX g_rl_553060   /* DAT_00553060..: the popup's box (x, y, w, h in px) */
#define PY g_rl_553064
#define PW g_rl_553024
#define PH g_rl_552ff8
#define MSG g_rl_53e910
#define ARG g_rl_5440ac  /* DAT_005440ac: the event's argument (the text variant) */
#define TICK g_rl_57c954 /* DAT_0057c954: the queue's cooldown (2000 at the open, counting down) */
#define SPEAK() rl_4b35c1(MSG, ARG)   /* 0x4b35c1: the event's voice */
#define BOX(x, y, w, h) (PX = (x), PY = (y), PW = (w), PH = (h))
#define SEAL_AT_BOX() seal(PW + PX - 48, PY + PH - 48)

/* the message panels 1 / 10 / 11's head: the cue at the open, the realm's portrait / shield / name */
static void realm_msg_head(int r, int cue_va)
{
    if (TICK == 2000) {
        g_combat_audio_latch = 0;
        rl_427990(STRVA(g_rl_4d7000, 0x4d7000u, cue_va), 1, 0);
    }
    if (TICK == 2000)
        rl_475d73(g_rl_553ee0);
    if (TICK == 1910)
        rl_4b3b92(PL8(r, 0x57bf07) - 1);
    if (TICK == 1800)
        SPEAK();
}

/* Port of FUN_0047309e (10915 B) — the event popup's render, by its panel kind DAT_005440a4: the parchment panel
 * (0x4093e0) at its box DAT_00553060 / 64 / 24 / DAT_00552ff8, the title / body texts of the event DAT_0053e910
 * (localization groups: the event, 100 the counties, 109 the realms, 77 the buildings), the seal, the cues / voice /
 * portrait by the queue's cooldown DAT_0057c954 (2000 the first frame, 1990 / 1910 / 1900 / 1800 / 90 later beats):
 * 0 / 16 / 3 the county events, 17 a keep's garrison (+ its buttons), 18 the keep build, 1 / 10 / 11 the realms'
 * messages (10 / 11 with the answer buttons; 11 shuts for a vassal), 2 the county's portrait, 4 a small tooltip by
 * the cursor, 19 the help pages (the table 0x4d6eb8 by event 291..), 5..9 the multi-line scrolls, 12 / 20 the
 * panels 0x475e07 / 0x476488, 13 / 14 the county's / the realm's fall (with the result movies DAT_0053f248 past the
 * first 10 frames: the full-screen frame, the dismiss, the movie, the end flags DAT_0053f0c4), 15 a building's
 * completion; then the seal's position (DAT_005681dc / DAT_0056950c) and the box's dirty mark. The uppercase
 * highlight DAT_0058fe2c is on for the call. */
int rl_47309e(void)
{
    g_rl_4f0374 = MSG;
    g_rl_4f0354 = ARG;
    g_gfx_uppercase_highlight = 1;
    switch (g_rl_5440a4) {
    case 0:
        if (TICK == 2000 && MSG >= 0x72 && MSG <= 0x7e) {
            g_combat_audio_latch = 0;
            rl_427990(STRVA(g_rl_4d7000, 0x4d7000u, 0x4d7000u), 1, 0);   /* "ff_capt.wav" */
        }
        if (TICK == 1910)
            SPEAK();
        BOX(0x20, 0xa0, 0x1a0, 0xe0);
        panel(PX, PY, PW / 16, PH / 16);
        if (g_rl_55ce54 != 0)
            wrap_line_c(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, PX + 16, PY + 32, PW - 32, FONT_TALL);
        else if (g_rl_57c8ac != 0)
            text_line(NAME(g_rl_57c8ac), PX + 32, PY + 32, FONT_TALL);
        else
            wrap_line_c(MSG, 0, PX + 16, PY + 32, PW - 32, FONT_TALL);
        wrap_loc(MSG, 1, PX + 32, PY + 80, PW - 64, 400);
        SEAL_AT_BOX();
        goto tail;
    case 16:
        if (TICK == 1990)
            SPEAK();
        BOX(0x20, 0xa0, 0x1a0, 0xf0);
        panel(PX, PY, PW / 16, PH / 16);
        wrap_line_c(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, PX + 16, PY + 32, PW - 32, FONT_TALL);
        wrap_line_c(MSG, 0, PX + 16, PY + 64, PW - 32, FONT_TALL);
        wrap_loc(MSG, 1, PX + 32, PY + 96, PW - 64, 400);
        SEAL_AT_BOX();
        goto tail;
    case 17: {
        if (TICK == 1990)
            SPEAK();
        int c = g_rl_55ce54, a = ARG;
        g_rl_553f5c = ARG;
        g_rl_55cd54 = g_rl_55ce54;
        g_rl_553f0c = g_rl_5678ac;
        int army = CO32(c, 0x53fb6c);
        int held = 0;
        if (army != 0)
            held = AT(int32_t, g_per_territory_420, 0x52f0b0u, 0x52f218 + 420 * army);
        BOX(0x20, 0x90, 0x1a0, 0xf0);
        panel(PX, PY, PW / 16, PH / 16);
        wrap_line_c(MSG, 0, PX + 16, PY + 32, PW - 32, FONT_TALL);
        wrap_loc(MSG, 1, PX + 32, PY + 64, PW - 64, 400);
        g_gfx_line_x_acc = 0;
        label(AT(int32_t, g_rl_4d89b8, 0x4d89b8u, 0x4d8a0c + 4 * CO8(c, 0x53fb70)) - held,
              CSTR(g_rl_4d7000, 0x4d7000u, 0x4d700c), PX + 32, PY + 112);
        wrap_line(MSG, 2, g_gfx_line_x_acc + PX + 32, PY + 112, FONT);
        g_gfx_line_x_acc = 0;
        wrap_line(MSG, 3, PX + 32, PY + 128, FONT);
        label(AT(int32_t, g_per_territory_420, 0x52f0b0u, 0x52f218 + 420 * a), CSTR(g_rl_4d7000, 0x4d7000u, 0x4d7010),
              g_gfx_line_x_acc + PX + 32, PY + 128);
        wrap_line(MSG, 4, PX + 32, PY + 176, FONT);
        button_strip(0x4ddb50);
        SEAL_AT_BOX();
        goto tail;
    }
    case 18: {
        if (TICK == 1990)
            SPEAK();
        int c = g_rl_55ce54;
        BOX(0x20, 0x90, 0x1a0, 0xf0);
        panel(PX, PY, PW / 16, PH / 16);
        wrap_line_c(MSG, 0, PX + 16, PY + 24, PW - 32, FONT_TALL);
        wrap_line_c(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, PX + 16, PY + 52, PW - 32, FONT);
        wrap_loc(MSG, 1, PX + 32, PY + 80, PW - 64, 400);
        g_gfx_line_x_acc = 0;
        if (ARG == 1) {
            wrap_loc(MSG, 7, PX + 32, PY + 128, PW - 64, 400);
        } else if (CO8(c, 0x53fb71) != 0) {
            if (CO8(c, 0x53fb70) <= CO8(c, 0x53fb71)) {
                label(0, CSTR(g_rl_4d7000, 0x4d7000u, 0x4d701c), PX + 32, PY + 128);
                wrap_line(MSG, 5, g_gfx_line_x_acc + PX + 32, PY + 128, FONT);
                wrap_loc(MSG, 4, PX + 32, PY + 160, PW - 64, 400);
            } else {
                label(AT(int32_t, g_rl_4d8a3c, 0x4d8a3cu, 0x4d8a3c + 4 * CO8(c, 0x53fb70))
                          - AT(int32_t, g_rl_4d8a3c, 0x4d8a3cu, 0x4d8a3c + 4 * CO8(c, 0x53fb71)),
                      CSTR(g_rl_4d7000, 0x4d7000u, 0x4d7018), PX + 32, PY + 128);
                wrap_line(MSG, 6, g_gfx_line_x_acc + PX + 32, PY + 128, FONT);
                wrap_loc(MSG, 3, PX + 32, PY + 160, PW - 64, 400);
            }
        } else {
            label(AT(int32_t, g_rl_4d8a3c, 0x4d8a3cu, 0x4d8a3c + 4 * CO8(c, 0x53fb70)),
                  CSTR(g_rl_4d7000, 0x4d7000u, 0x4d7014), PX + 32, PY + 128);
            wrap_line(MSG, 5, g_gfx_line_x_acc + PX + 32, PY + 128, FONT);
            wrap_loc(MSG, 2, PX + 32, PY + 160, PW - 64, 400);
        }
        goto seal_tail;
    }
    case 1: {
        int r = g_rl_553ee0;
        realm_msg_head(r, 0x4d7020);   /* "ff_msg.wav" (aFfMsgWav) */
        BOX(0x10, 0x80, 0x1c0, 0x100);
        panel(PX, PY, PW / 16, PH / 16);
        gfx_sprite_render_from_blob(CHROME, PL8(r, 0x57bf0a) + 0xff, PW + PX - 30, PY + 18);
        portrait_frame(PX, PY);
        g_gfx_line_x_acc = 0;
        wrap_line(109, 0, PX + 108, PY + 32, FONT_TALL);
        text_line(NAME(r), g_gfx_line_x_acc + PX + 108, PY + 32, FONT_TALL);
        g_gfx_line_x_acc = 0;
        wrap_line(MSG, 0, PX + 108, PY + 64, FONT);
        if (g_rl_57c8ac != 0)
            text_line(NAME(g_rl_57c8ac), g_gfx_line_x_acc + PX + 108, PY + 64, FONT);
        else if (g_rl_55ce54 != 0)
            wrap_line(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, g_gfx_line_x_acc + PX + 108, PY + 64, FONT);
        wrap_loc(MSG, ARG + 1, PX + 32, PY + 112, PW - 64, 0x1b0);
        SEAL_AT_BOX();
        goto tail;
    }
    case 2:
        if (TICK == 2000)
            rl_475d73(CO32(g_rl_55ce54, 0x53f9d4) >= 240 ? 0 : 6);
        if (TICK == 1990)
            SPEAK();
        BOX(0x10, 0x90, 0x1c0, 0x100);
        panel(PX, PY, PW / 16, PH / 16);
        portrait_frame(PX, PY);
        wrap_line_c(109, 1, PX + 108, PY + 24, 0x140, FONT_TALL);
        wrap_line_c(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, PX + 108, PY + 56, 0x140, FONT_TALL);
        wrap_loc(MSG, 1, PX + 32, PY + 112, PW - 64, 0x1b0);
        SEAL_AT_BOX();
        goto tail;
    case 3:
        if (TICK == 1990)
            SPEAK();
        BOX(0x10, 0xa0, 0x1c0, 0xf0);
        panel(PX, PY, PW / 16, PH / 16);
        wrap_line_c(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, PX, PY + 32, PW, FONT_TALL);
        g_gfx_line_x_acc = 0;
        wrap_line(MSG, 0, PX + 32, PY + 64, FONT_TALL);
        wrap_loc(MSG, 1, PX + 32, PY + 96, PW - 64, 0x1b0);
        SEAL_AT_BOX();
        goto tail;
    case 4:
        PX = g_rl_53f0a0 <= 240 ? g_rl_53f0a0 + 32 : g_rl_53f0a0 - 168;
        PY = g_rl_53f09c <= 240 ? g_rl_53f09c + 32 : g_rl_53f09c - 80;
        if (PX < 80)  PX = 80;
        if (PX > 240) PX = 240;
        if (PY < 80)  PY = 80;
        if (PY > 240) PY = 240;
        PW = 0xb0;
        PH = 0x30;
        panel(PX, PY, PW / 16, PH / 16);
        wrap_line_c(MSG, 0, PX, PY + 16, PW, FONT);
        if (TICK >= 1000)
            TICK = 100;
        if (TICK == 90)
            SPEAK();
        goto tail;
    case 19: {
        int o = 16 * (MSG - 0x123);
        PX = AT(int32_t, g_rl_4d6e88, 0x4d6e88u, 0x4d6eb8 + o);
        PY = AT(int32_t, g_rl_4d6e88, 0x4d6e88u, 0x4d6ebc + o);
        PW = AT(int32_t, g_rl_4d6e88, 0x4d6e88u, 0x4d6ec0 + o);
        PH = AT(int32_t, g_rl_4d6e88, 0x4d6e88u, 0x4d6ec4 + o);
        panel(PX, PY, PW, PH);   /* the table holds tiles */
        seal(16 * PW + PX - 48, 16 * PH + PY - 48);
        wrap_line_c(MSG, 0, PX + 16, PY + 22, 16 * PW - 32, FONT_TALL);
        g_gfx_line_y_acc = 0;
        /* the help page's paragraph count: dword_4D6A8C[event] (the event 291.. lands inside the 0x4d6e88 block) */
        for (int i = 1; AT(int32_t, g_rl_4d6e88, 0x4d6e88u, 0x4d6a8c + 4 * MSG) >= i; i++) {
            wrap_loc(MSG, i, PX + 32, PY + g_gfx_line_y_acc + 50, 16 * PW - 64, 400);
            g_gfx_line_y_acc += 4;
        }
        goto tail;
    }
    }
    if (g_rl_5440a4 >= 5 && g_rl_5440a4 <= 9) {
        if (TICK == 1990)
            SPEAK();
        else if (TICK < 1920)
            rl_4b3acd(MSG);
        int n = g_rl_5440a4 - 5;
        PX = 0x10;
        PY = 0x40;
        PW = 0x1c0;
        int k = 0;
        for (g_gfx_line_y_acc = 32; k++ <= n; g_gfx_line_y_acc += 16)
            wrap_loc(MSG, k, PX + 16, PY + 64, PW - 32, 0x1b0);
        if (g_gfx_line_y_acc <= 96)
            PY += 64;
        PH = PY + g_gfx_line_y_acc;
        panel(PX, PY, PW / 16, (PY + g_gfx_line_y_acc) / 16);
        wrap_line(MSG, 0, PX + 16, PY + 20, FONT_TALL);
        k = 0;
        for (g_gfx_line_y_acc = 64; k++ <= n; g_gfx_line_y_acc += 16)
            wrap_loc(MSG, k, PX + 16, PY + g_gfx_line_y_acc, PW - 32, 0x1b0);
        goto seal_tail;
    }
    switch (g_rl_5440a4) {
    case 10: {
        int r = g_rl_553ee0;
        realm_msg_head(r, 0x4d702c);   /* "ff_msg.wav" (aFfMsgWav_0) */
        BOX(0x10, 0x80, 0x1c0, 0xf0);
        panel(PX, PY, PW / 16, PH / 16);
        gfx_sprite_render_from_blob(CHROME, PL8(r, 0x57bf0a) + 0xff, PW + PX - 30, PY + 18);
        portrait_frame(PX, PY);
        g_gfx_line_x_acc = 0;
        wrap_line(109, 0, PX + 108, PY + 32, FONT_TALL);
        text_line(NAME(r), g_gfx_line_x_acc + PX + 108, PY + 32, FONT_TALL);
        g_gfx_line_x_acc = 0;
        wrap_line(MSG, 0, PX + 108, PY + 64, FONT);
        num_units((int32_t)s_slot_00567958, 0, g_gfx_line_x_acc + PX + 108, PY + 64);
        wrap_loc(MSG, ARG + 1, PX + 32, PY + 112, PW - 64, 0x1b0);
        button_strip(0x4dda90);
        SEAL_AT_BOX();
        break;
    }
    case 11: {
        if (PL8(LOCAL, 0x57bf81) != 0) {
            int ret = rl_476768();
            g_gfx_uppercase_highlight = 0;
            return ret;
        }
        int r = g_rl_553ee0;
        realm_msg_head(r, 0x4d7038);   /* "ff_msg.wav" (aFfMsgWav_1) */
        BOX(0x10, 0x80, 0x1c0, 0xf0);
        panel(PX, PY, PW / 16, PH / 16);
        gfx_sprite_render_from_blob(CHROME, PL8(r, 0x57bf0a) + 0xff, PW + PX - 30, PY + 18);
        portrait_frame(PX, PY);
        g_rl_57c8b8 = r;
        g_gfx_line_x_acc = 0;
        wrap_line(109, 0, PX + 108, PY + 32, FONT_TALL);
        text_line(NAME(r), g_gfx_line_x_acc + PX + 108, PY + 32, FONT_TALL);
        g_gfx_line_x_acc = 0;
        wrap_line(MSG, 0, PX + 108, PY + 64, FONT);
        wrap_loc(MSG, ARG + 1, PX + 32, PY + 112, PW - 64, 0x1b0);
        button_strip(0x4ddac0);
        SEAL_AT_BOX();
        break;
    }
    case 12:
        rl_475e07();   /* the panel 12 renderer (rl mpnet; its [ebp-8] residue: rl_475e07_res_in, quirk #240) */
        break;
    case 20:
        rl_476488();   /* the panel 20 renderer (rl mpnet2) */
        break;
    case 13:
        rl_4b1de0();
        g_gfx_menu_redraw_a = 2;
        g_gfx_menu_redraw_b = 1;
        if (g_combat_flag_53f248 != 1 || TICK <= 1990) {
            if (TICK == 2000) {
                g_combat_audio_latch = 0;
                rl_427990(STRVA(g_rl_4d7000, 0x4d7000u, 0x4d7044u), 1, 0);   /* "ff_msg.wav" (aFfMsgWav_2) */
            }
            if (TICK == 1910)
                SPEAK();
            BOX(0x20, 0xa0, 0x1a0, 0xc0);
            panel(PX, PY, PW / 16, PH / 16);
            SEAL_AT_BOX();
            wrap_line_c(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, PX + 16, PY + 32, PW - 32, FONT_TALL);
            wrap_loc(MSG, 1, PX + 32, PY + 64, PW - 64, 400);
            break;
        }
        rl_4b1310();
        panel(16, 48, 28, 23);
        gfx_line_rect_bevel_dark_br(39, 104, 402, 194);
        seal(416, 368);
        wrap_line_c(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, 16, 70, 448, FONT_TALL);
        wrap_loc(MSG, 1, 48, 0x142, 0x180, 400);
        rl_476768();
        rl_426d11(0);
        if (++g_rl_553ed4 >= 3)
            g_rl_553ed4 = 0;
        if (rl_42d91b(STRVA(g_rl_4d6e88, 0x4d6e88u, 0x4d6e88 + 16 * g_rl_553ed4), 40, 105, 0, SCREEN) == 0) {
            REDRAW(1);
            rl_499aca();
        }
        rl_4b35c1(g_rl_4f0374, g_rl_4f0354);
        break;
    case 14: {
        rl_4b1de0();
        g_gfx_menu_redraw_a = 2;
        g_gfx_menu_redraw_b = 1;
        int r = g_rl_553ee0;
        if (g_combat_flag_53f248 != 1 || TICK <= 1990) {
            if (TICK == 2000) {
                g_rl_53f0c4 = 0;
                if (MSG == 225 || (int32_t)s_slot_0056d5d8 > 0) {
                    if (MSG != 225 && LOCAL == g_rl_553ee0)
                        g_rl_53f0c4 = 11;
                    else if (MSG == 225)
                        g_rl_53f0c4 = 10;
                } else {
                    evq_post(0, LOCAL, 225, 0, 14, 0, 0, 0);
                }
                rl_475d73(g_rl_553ee0);
                if (MSG == 194)
                    rl_4b3b92(PL8(r, 0x57bf07) - 1);
            }
            if (TICK == 1900)
                SPEAK();
            BOX(0x10, 0x80, 0x1c0, 0xe0);
            panel(PX, PY, PW / 16, PH / 16);
            SEAL_AT_BOX();
            portrait_frame(PX, PY);
            text_line_c(NAME(MSG == 225 ? LOCAL : g_rl_553ee0), PX + 32, PY + 32, 416, FONT_TALL);
            wrap_line_c(MSG, 0, PX + 32, PY + 72, PW - 64, FONT);
            wrap_loc(MSG, ARG + 1, PX + 32, PY + 112, PW - 64, 0x1b0);
            break;
        }
        g_rl_53f0c4 = 0;
        rl_4b1310();
        if (g_rl_53f5f4 == 1) {
            panel(8, 32, 34, 27);
            gfx_line_rect_bevel_dark_br(24, 80, 502, 314);
            seal(504, 416);
            text_line_c(NAME(MSG == 225 ? LOCAL : g_rl_553ee0), 8, 50, 544, FONT_TALL);
            wrap_loc(MSG, ARG + 1, 24, 0x190, 0x1f0, 400);
        } else {
            panel(16, 48, 28, 24);
            gfx_line_rect_bevel_dark_br(88, 104, 298, 186);
            seal(416, 384);
            text_line_c(NAME(MSG == 225 ? LOCAL : g_rl_553ee0), 16, 70, 448, FONT_TALL);
            wrap_line_c(MSG, 0, 16, 306, 448, FONT_TALL);
            wrap_loc(MSG, ARG + 1, 48, 0x152, 0x180, 400);
        }
        if (MSG == 225 || (int32_t)s_slot_0056d5d8 > 0) {
            if (MSG != 225 && LOCAL == g_rl_553ee0)
                g_rl_53f0c4 = 11;
            else if (MSG == 225)
                g_rl_53f0c4 = 10;
        } else {
            evq_post(0, LOCAL, 225, 0, 14, 0, 0, 0);
        }
        rl_475b41(g_rl_553ee0, MSG);
        rl_476768();
        rl_426d11(0);
        if (g_rl_53f5f4 == 1) {
            if (rl_42d91b(STRVA(g_rl_4f0340, 0x4f0340u, 0x4f0340u), 25, 81, 0, SCREEN) == 0) {
                REDRAW(1);
                rl_499aca();
            }
        } else if (rl_42d91b(STRVA(g_rl_4f0340, 0x4f0340u, 0x4f0340u), 89, 105, 0, SCREEN) == 0) {
            REDRAW(1);
            rl_499aca();
        }
        rl_4b35c1(g_rl_4f0374, g_rl_4f0354);
        break;
    }
    case 15: {
        int c = g_rl_55ce54;
        if (TICK == 1990)
            SPEAK();
        if (CO16(c, 0x53fb5a) < 302)
            BOX(0x20, 0xa0, 0x1a0, 0xc0);
        else
            BOX(0x20, 0xa0, 0x1a0, 0xe0);
        panel(PX, PY, PW / 16, PH / 16);
        wrap_line_c(MSG, 0, PX + 16, PY + 32, PW - 32, FONT_TALL);
        wrap_loc(MSG, 1, PX + 32, PY + 64, PW - 64, 400);
        g_gfx_line_x_acc = 0;
        switch (CO16(c, 0x53fb5a)) {
        case 135: num_units(CO32(c, 0x53fc28), 2, PX + 32, PY + 144); wrap_line(77, 25, g_gfx_line_x_acc + PX + 32, PY + 144, FONT); break;
        case 139: num_units(CO32(c, 0x53fc28), 2, PX + 32, PY + 144); wrap_line(77, 26, g_gfx_line_x_acc + PX + 32, PY + 144, FONT); break;
        case 136: num_units(CO32(c, 0x53fc24), 4, PX + 32, PY + 144); wrap_line(77, 20, g_gfx_line_x_acc + PX + 32, PY + 144, FONT); break;
        case 137: num_units(CO32(c, 0x53fc24), 4, PX + 32, PY + 144); wrap_line(77, 21, g_gfx_line_x_acc + PX + 32, PY + 144, FONT); break;
        case 140: num_units(CO32(c, 0x53fc24), 4, PX + 32, PY + 144); wrap_line(77, 22, g_gfx_line_x_acc + PX + 32, PY + 144, FONT); break;
        case 141: num_units(CO32(c, 0x53fc24), 4, PX + 32, PY + 144); wrap_line(77, 23, g_gfx_line_x_acc + PX + 32, PY + 144, FONT); break;
        case 138:
            label(CO32(c, 0x53fca8), CSTR(g_rl_4d7000, 0x4d7000u, 0x4d7050), PX + 32, PY + 144);
            wrap_line(77, 29, g_gfx_line_x_acc + PX + 32, PY + 144, FONT);
            break;
        case 142:
            label(CO32(c, 0x53fca8), CSTR(g_rl_4d7000, 0x4d7000u, 0x4d7054), PX + 32, PY + 144);
            wrap_line(77, 30, g_gfx_line_x_acc + PX + 32, PY + 144, FONT);
            break;
        }
        goto seal_tail;
    }
    }
    goto tail;
seal_tail:
    SEAL_AT_BOX();
tail:
    g_rl_5681dc = g_gfx_seal_x;
    g_rl_56950c = g_gfx_seal_y;
    rl_475e07_res_in = PY;   /* the dirty mark's 4th push lands on 0x475e07's [ebp-8] (0x475b23; quirk #240) */
    rl_4527a6(PX, PY, PW / 16 + 1, PH / 16 + 1, 2);
    g_gfx_uppercase_highlight = 0;
    return 0;
}

/* ---------------------------------------------------------------- game init, the turn */

/* Port of FUN_004a2584 (157 B) — clear the six realms' five-slot diplomacy inboxes DAT_0053f0f0 (+0/+1/+2 and the
 * i32 at +4; +3 untouched). EAX: 5 × 5 (the last row's 5 i). */
int rl_4a2584(void)
{
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 5; j++) {
            uint8_t *e = &s_slot_0053f0f0[40 * i + 8 * j];
            e[0] = 0; e[1] = 0; e[2] = 0;
            *(int32_t *)(e + 4) = 0;
        }
    return 25;
}

/* Port of FUN_004a1c53 (537 B) — reset the realms' diplomacy: DAT_0057a0f8 = 0, the inboxes cleared, the first
 * active non-local realm; per realm 1..5: no feud (+0xeb), no lord (+0x81); toward each realm 1..5 the relation
 * (+0x84: 5 for an active AI realm, else 0), +0x8c = 0, +0x91 = 1, +0x85..+0x89 / +0x90 = 0, the word +0x92 = 0. */
int rl_4a1c53(void)
{
    s_slot_0057a0f8 = 0;
    rl_4a2584();
    rl_4a1e6c();
    for (int i = 1; i < 6; i++) {
        uint8_t v = (PL8(i, 0x57bf04) != 0 && PL8(i, 0x57bf05) == 0) ? 5 : 0;
        PL8(i, 0x57bfeb) = 0;
        PL8(i, 0x57bf81) = 0;
        for (int j = 1; j < 6; j++) {
            PL8(i, 0x57bf84 + 16 * j) = v;
            PL32(i, 0x57bf8c + 16 * j) = 0;
            PL8(i, 0x57bf91 + 16 * j) = 1;
            PL8(i, 0x57bf88 + 16 * j) = 0;
            PL8(i, 0x57bf85 + 16 * j) = 0;
            PL8(i, 0x57bf87 + 16 * j) = 0;
            PL8(i, 0x57bf86 + 16 * j) = 0;
            PL8(i, 0x57bf89 + 16 * j) = 0;
            PL8(i, 0x57bf90 + 16 * j) = 0;
            PL16(i, 0x57bf92 + 16 * j) = 0;
        }
    }
    return 352 * 5;
}

/* Port of FUN_0049b35e (205 B) — run 0x46ea61 on every realm record 1..5 that is not an active human / AI seat
 * (inactive, or active with neither +5 nor +7). */
int rl_49b35e(void)
{
    for (int i = 1; i < 6; i++) {
        if (PL8(i, 0x57bf04) != 0 && (PL8(i, 0x57bf05) != 0 || PL8(i, 0x57bf07) != 0))
            continue;
        rl_46ea61((int)(uintptr_t)&PL8(i, 0x57bf00));
    }
    return 0;
}

/* Port of FUN_0049b6d3 (143 B) — the turn's roster reset: each realm's +0 = 0 (999 when inactive), the active count
 * into DAT_00554004. */
int rl_49b6d3(void)
{
    s_slot_00554004 = 0;
    for (int i = 1; i < 6; i++) {
        PL32(i, 0x57bf00) = 0;
        if (PL8(i, 0x57bf04) != 0)
            s_slot_00554004++;
        else
            PL32(i, 0x57bf00) = 999;
    }
    return 352 * 5;
}

/* Port of FUN_0049ce51 (109 B) — advance the turn phase DAT_00569584 (the previous into DAT_0053f06c; past 7 → 1 and
 * the roster reset), the next phase DAT_0053f678 (wraps at 7 → 1), DAT_0053f658 = 0, DAT_0057c944 = 1. EAX: the
 * phase + 1. */
int rl_49ce51(void)
{
    s_slot_0053f06c = s_slot_00569584;
    s_slot_00569584++;
    if ((int32_t)s_slot_00569584 > 7) {
        s_slot_00569584 = 1;
        rl_49b6d3();
    }
    int next = (int32_t)s_slot_00569584 + 1;
    s_slot_0053f678 = (uint32_t)next;
    if ((int32_t)s_slot_0053f678 > 7)
        s_slot_0053f678 = 1;
    s_slot_0053f658 = 0;
    s_slot_0057c944 = 1;
    return next;
}

/* Port of FUN_0049b762 (334 B) — is the turn resolved: count the active realms whose +0 is below 999 into
 * DAT_00554004 (any → 0); SP → 1; MP: DAT_0053f068 1 → 1, else the vote (DAT_00553fc8: 0 → clear the ten vote
 * slots DAT_00515d60, 1, DAT_0055ce88 = DAT_00553018, DAT_00553018 = 0, DAT_0056d658 = now - 10 s; 2 → 0 and 1). */
int rl_49b762(void)
{
    s_slot_00554004 = 0;
    for (int i = 1; i < 6; i++)
        if (PL8(i, 0x57bf04) != 0 && PL32(i, 0x57bf00) < 999)
            s_slot_00554004++;
    if (s_slot_00554004 != 0)
        return 0;
    if (!MP_ON)
        return 1;
    if (g_rl_53f068 == 1)
        return 1;
    if (g_sim_turn_progress == 0) {
        for (int j = 0; j < 10; j++)
            g_combat_anim_scratch[j] = 0;
        g_sim_turn_progress = 1;
        g_rl_55ce88 = g_rl_553018;
        g_rl_553018 = 0;
        g_rl_56d658 = (int32_t)(rl_plat_timeGetTime() - 10000u);
    }
    if (g_sim_turn_progress != 2)
        return 0;
    g_sim_turn_progress = 0;
    return 1;
}

/* Port of FUN_004a3475 (220 B) — AI inbox type 4, a silent treaty break: the bond cleared (0x4a1a54), the relation
 * toward the sender -15, clamped to ±30. */
int rl_4a3475(int a0, int a1)
{
    rl_4a1a54(a0, a1);
    int8_t *r = &PLS8(a0, 0x57bf84 + 16 * a1);
    *r = (int8_t)(*r - 15);
    if (*r > 30)
        *r = 30;
    else if (*r < -30)
        *r = -30;
    return 0;
}

/* Port of FUN_00499dc3 (154 B) — apply the custom game's setup options (g_tr_options, DAT_0053f288..) to the game
 * settings: the AI count (option 0 + 2 - the human count DAT_00553f98), farming / exploration / foraging, the
 * difficulty, the speed / tier tables DAT_004dbbf8 / DAT_004dbc18, the rest as they are. EAX: option 11. */
int rl_499dc3(void)
{
    s_slot_0053f268 = (uint32_t)(g_tr_options[0] + 2 - (int32_t)s_slot_00553f98);
    s_slot_0053f25c = (uint32_t)g_tr_options[5];
    s_slot_0053f264 = (uint32_t)g_tr_options[7];
    s_slot_0053f260 = (uint32_t)g_tr_options[6];
    s_slot_0053f23c = (uint32_t)g_tr_options[2];
    s_slot_0053f26c = (uint32_t)AT(int32_t, g_rl_4dbbf8, 0x4dbbf8u, 0x4dbbf8 + 4 * g_tr_options[1]);
    g_rl_53f278 = AT(int32_t, g_rl_4dbbf8, 0x4dbbf8u, 0x4dbc18 + 4 * g_tr_options[8]);
    g_rl_53f280 = g_tr_options[10];
    g_rl_53f270 = g_tr_options[3];
    g_rl_53f274 = g_tr_options[4];
    g_rl_53f27c = g_tr_options[9];
    g_combat_flag_53f284 = g_tr_options[11];
    return g_tr_options[11];
}

/* Port of FUN_00499e5d (421 B) — apply a scenario's settings (DAT_0053f258; the table by the campaign kind
 * DAT_0053f640: 0x4d8f58 when 1, else 0x4d8e18 — 8 dwords per scenario: the map, the difficulty, …), no farming /
 * exploration / foraging, then load the map (0x46a037, 0x467770) and build the cells (0x4298c1). */
int rl_499e5d(void)
{
    s_slot_0053f26c = 0;
    g_combat_flag_53f284 = 1;
    const int32_t *t = s_slot_0053f640 == 1
        ? &AT(int32_t, g_rl_4d8f58, 0x4d8f58u, 0x4d8f58 + 32 * (int32_t)s_slot_0053f258)
        : &AT(int32_t, g_rl_4d8e18, 0x4d8e18u, 0x4d8e18 + 32 * (int32_t)s_slot_0053f258);
    s_slot_0053f034 = (uint32_t)t[0];
    s_slot_0053f23c = (uint32_t)t[1];
    g_rl_53f278 = t[2];
    g_rl_53f280 = t[3];
    g_rl_53f27c = t[4];
    g_rl_53f270 = t[5];
    g_rl_53f274 = t[6];
    s_slot_0053f268 = (uint32_t)t[7];
    s_slot_0053f25c = 0;
    s_slot_0053f264 = 0;
    s_slot_0053f260 = 0;
    rl_46a037((int)s_slot_0053f034);
    rl_467770((int)s_slot_0053f034);
    return rl_4298c1(0);
}

/* Port of FUN_00498270 (327 B) — the strategic map's init: the turn / scroll / selection globals to their start
 * values (the minimap 480 × 48, 160 / 160, 64 / 64, 8, 74 / 20), the cells built (0x4298c1), the camera on the
 * capital (0x451fcc), DAT_00553014 = 4, the territories cleared (0x46f3cd), "OK :setup_game executed.". */
int rl_498270(void)
{
    g_rl_56d650 = 0;
    g_rl_53f0e0 = 0;
    g_rl_544028 = 0;
    g_rl_544038 = 0;
    g_rl_544040 = 0;
    g_rl_544044 = 0;
    g_rl_52effc = 0;
    s_slot_005651b8 = 0x4a;
    s_slot_005651b4 = 0x14;
    s_slot_0057cb18 = 0;
    g_rl_57c968 = 0;
    s_slot_0057c8fc = 0;
    g_rl_543fd4 = 0;
    g_rl_4ea830 = 0;
    g_rl_56d698 = 0x1e0;
    g_rl_56d69c = 0x30;
    g_rl_56d694 = 0xa0;
    g_rl_56d674 = 0xa0;
    g_rl_52afd4 = 0x40;
    g_rl_5694f8 = 0x40;
    g_rl_57d338 = 8;
    g_rl_53e980 = 0;
    g_rl_52afc8 = 0;
    rl_4298c1(0);
    rl_451fcc((int)s_slot_0057cb18);
    g_rl_553014 = 4;
    rl_46f3cd();
    g_rl_57c938 = 0;
    g_rl_56d660 = 0;
    g_rl_553500 = 0;
    return rl_4afab9(STRVA(g_rl_4dbc60, 0x4dbc60u, 0x4dbc60u), 0, 0);   /* "OK :setup_game executed." */
}

/* Port of FUN_00497e65 (1035 B) — the new game's shuffles (engine_rng_step 0x404a46 → DAT_0058fd60): (1) the
 * roster's active seats (pairs 1..n, n = the non-zero army bytes of pairs 1..5) re-seated: each pair i from a random
 * start i + 1 + (rng & 3) (past n → 1) to the next free one, at most n tries; (2) the 48 bytes DAT_0057cae0 get a
 * permutation of 0..47: value i from i + 1 + (rng & 31) (wrapping at 48), skipping slot i itself and taken slots,
 * at most 50 steps (a value can be dropped: the slot stays 0); (3) the 48 bytes DAT_00553080 by rng & 7: 5 → 0,
 * 6 → 100, else 5 v + 6. */
int rl_497e65(void)
{
    int n = 0;
    for (int i = 1; i < 6; i++)
        if (g_player_roster[2 * i] != 0)
            n++;
    int army[48], seat[6];
    for (int i = 0; i < 6; i++) {
        army[i] = g_player_roster[2 * i];
        seat[i] = g_player_roster[2 * i + 1];
    }
    for (int i = 0; i < 6; i++)
        g_player_roster[2 * i] = 0;
    for (int i = 1; i <= n; i++) {
        bf_rng_step();
        int k = i + (g_bf_58fd60 & 3) + 1;
        if (k > n)
            k = 1;
        for (int t = 0; t < n; t++) {
            if (g_player_roster[2 * k] == 0) {
                g_player_roster[2 * k] = (uint8_t)army[i];
                g_player_roster[2 * k + 1] = (uint8_t)seat[i];
                break;
            }
            if (++k > n)
                k = 1;
        }
    }
    for (int i = 0; i < 48; i++)
        army[i] = i;
    for (int i = 0; i < 48; i++)
        s_slot_0057cae0[i] = 0;
    for (int i = 0; i < 48; i++) {
        bf_rng_step();
        int k = i + (g_bf_58fd60 & 0x1f) + 1;
        if (k >= 48)
            k -= 48;
        for (int t = 0; t < 50; t++) {
            if (k == i) {
                k++;
            } else if (s_slot_0057cae0[k] == 0) {
                s_slot_0057cae0[k] = (uint8_t)army[i];
                break;
            } else {
                k++;
            }
            if (k >= 48)
                k = 0;
        }
    }
    for (int i = 0; i < 48; i++) {
        bf_rng_step();
        int v = g_bf_58fd60 & 7;
        s_slot_00553080[i] = v == 5 ? 0 : v == 6 ? 100 : (uint8_t)(5 * v + 6);
    }
    return 0;
}

/* Port of FUN_00497ced (376 B) — start a new game: clear the territories / armies / counties, the NPC seats
 * (0x49caaa), the turn / season / speed settings to their start values (turn 1267, spring 3, …), the overworld
 * (0x4676e0), the counties' baselines (0x451150), the armies' starts (0x427ed0), 0x4ac904, the seat shuffle, the
 * roster compacted, the realms (0x49bd99), diplomacy (0x4a1c53), 0x42847c, 0x4ae74d, the castles (0x4ae8a9), the
 * first turn advance (0x448440), the path costs (0x46ff43), the first save, the event queue, the rankings, the
 * strategic sheets (0x499d7d, 0x4984dc, the 232 × 192 panel 0x4b18f8, 0x499aca) and the absent realms (0x49b35e). */
int rl_497ced(void)
{
    rl_46f3cd();
    rl_46f2ae();
    rl_46f1dc();
    rl_49caaa();
    g_sim_turn_progress = 0;
    g_rl_56d658 = 0;
    g_rl_53f0c4 = 0;
    s_slot_0055cd5c = 0;
    s_slot_00553edc = 0x4f3;
    s_slot_00553228 = 0x4f3;
    s_slot_0057c900 = 0x4f2;
    s_slot_00553e74 = 0x4f4;
    s_slot_00553240 = 0;
    s_slot_0056d6b0 = 0;
    s_slot_0057c934 = 3;
    s_slot_0057c990 = 3;
    s_slot_00554470 = 2;
    s_slot_0057c92c = 4;
    s_slot_0055cd40 = 0;
    s_slot_0057c918 = 0;
    s_slot_0057a0c4 = 0;
    s_slot_0053e920 = 0;
    s_slot_0056d5e0 = 0;
    s_slot_0055ce7c = 6;
    rl_4676e0();
    rl_451150();
    rl_427ed0();
    rl_4ac904();
    rl_497e65();
    rl_49bc5f();
    rl_49bd99();
    rl_4a1c53();
    rl_42847c();
    rl_4ae74d();
    rl_4ae8a9();
    rl_448440();
    rl_46ff43();
    rl_49a453();
    rl_472ad0();
    rl_49aa0e();
    rl_499d7d();
    rl_4984dc();
    rl_4b18f8(0xe8, 0xc0);
    rl_499aca();
    return rl_49b35e();
}

/* Port of FUN_004976a1 (472 B) — after a saved game is loaded: the MP clock base DAT_0056d8b4 (SP 0), redraw / turn
 * gates, the year count DAT_005440c8 += 10 and the animation deadline from it, every active realm's +0 = 1, the
 * session's seats (0x43e9e2), the cells built (0x4298c1) under the saved screen byte DAT_004eafb0, the camera
 * (0x451fcc), the minimap / feature / special tiles / cell roster (0x46a037, 0x46dbf5, 0x46dd34, 0x46da4b(0)), the
 * visibility bits; then by the frame driver's sub-state DAT_0057c8fc (2: the battlefield's sheets 0x477c89, else
 * 0x4984dc; 0: the strategic render, else 0x4233f7), the cue released, the music (0x499d7d / 0x499d97) and the
 * sounds (0x499aca / 0x477b2f); MP: the session poll (0x43f4b7 → 0x43fd2a); the full-screen mark; the rankings
 * dirty (DAT_0056d6a0). */
int rl_4976a1(void)
{
    uint32_t now = rl_plat_timeGetTime();
    g_rl_56d8b4 = MP_ON ? (int32_t)now : 0;
    g_combat_render_dirty = 1;
    g_bf_55403c = 0;
    s_slot_005440c8 += 10;
    g_combat_anim_deadline = now - (uint32_t)(((int32_t)s_slot_0053f26c - (int32_t)s_slot_005440c8) * 1000);
    g_sim_turn_progress = 0;
    for (int i = 1; i < 6; i++)
        if (PL8(i, 0x57bf04) != 0)
            PL32(i, 0x57bf00) = 1;
    rl_43e9e2();
    SET_SCREEN((uint8_t)s_slot_004eafb0);
    rl_4298c1(0);
    rl_451fcc((int)s_slot_0057cb18);
    rl_46a037((int)s_slot_0053f034);
    rl_46dbf5();
    rl_46dd34();
    rl_46da4b(0);
    rl_49ca32();
    SET_SCREEN(0);
    if (s_slot_0057c8fc == 2)
        bf_477c89();
    else
        rl_4984dc();
    if (s_slot_0057c8fc == 0)
        rl_40f5fd(1);
    else
        rl_4233f7(1);
    rl_427d19();
    if (s_slot_0057c8fc == 0)
        rl_499d7d();
    else
        bf_499d97();
    if (s_slot_0057c8fc == 0)
        rl_499aca();
    else
        bf_477b2f();
    if (MP_ON) {
        /* TODO(rl-extern): 0x43f4b7, 0x43fd2a (the MP session; 0x43fd2a is called with no argument pushed: it reads
         * the caller's saved EBX — passed here as 0x43f4b7's EAX, as rl_glue's 0x440666; MP is not in the corpus) */
        uint32_t v = engine_retail_call(0x43f4b7u, 0);
        engine_retail_call(0x43fd2au, 1, v);
    }
    rl_45240a();
    engine_army_ai_set_ranking_dirty(1);
    return 0;
}

/* Port of FUN_0049caaa (935 B) — seat the AI realms for a new game: the job slots the humans hold (aux +0x25 of
 * the non-MP-flagged seats) are taken; each non-human realm 1..5 (+5 == 0) within the AI count DAT_0053f268 gets
 * the first free job slot (+0x0a) and the first free personality (+0x07) of its slot's row (MP / DAT_0055302c 1:
 * the table 0x4dc17c, else 0x4dc180 by the map DAT_0053f034 & 3), its name from the localization (section 7, the
 * personality) into aux +4 (16 B); past the count the realm is inactive (+4 = 0). Every realm: +0x2a = +0x29 = 1,
 * +8 / +9 the slot's pair DAT_004dc1ce. EAX: 0x600 | realm 5's +9 byte (the last record offset, AL the last store). */
int rl_49caaa(void)
{
    uint8_t slot_taken[8], pers_taken[8];
    for (int i = 0; i <= 5; i++)
        slot_taken[i] = 0;
    for (int i = 0; i <= 4; i++)
        pers_taken[i] = 0;
    for (int i = 1; i <= 5; i++)
        if (g_player_aux_44[44 * i + 0x26] == 0 && g_player_aux_44[44 * i + 0x25] != 0)
            slot_taken[g_player_aux_44[44 * i + 0x25]] = 1;
    int ai = 0;
    for (int j = 1; j < 6; j++) {
        PL8(j, 0x57bf07) = 0;
        if (PL8(j, 0x57bf05) != 0)
            continue;
        if (!((int32_t)s_slot_0053f268 > ai)) {
            PL8(j, 0x57bf04) = 0;
            continue;
        }
        ai++;
        for (int i = 1; i <= 5; i++)
            if (slot_taken[i] == 0) {
                PL8(j, 0x57bf0a) = (uint8_t)i;
                slot_taken[i] = 1;
                break;
            }
        for (int i = 0; i < 4; i++) {
            int s = PL8(j, 0x57bf0a);
            uint8_t p = (MP_ON || s_slot_0055302c == 1)
                ? AT(uint8_t, g_rl_4dc070, 0x4dc070u, 0x4dc17c + 4 * s + i)
                : AT(uint8_t, g_rl_4dc070, 0x4dc070u, 0x4dc180 + 20 * (int)(s_slot_0053f034 & 3) - 4 + 4 * s + i);
            if (pers_taken[p] == 0) {
                PL8(j, 0x57bf07) = p;
                pers_taken[p] = 1;
                break;
            }
        }
        rl_4018d7(7, PL8(j, 0x57bf07));
        rl_401136((int)(uintptr_t)g_rl_5c9270, (int)(uintptr_t)&g_player_aux_44[44 * j + 4], 16);
    }
    for (int j = 1; j < 6; j++) {
        PL8(j, 0x57bf2a) = 1;
        PL8(j, 0x57bf29) = PL8(j, 0x57bf2a);
        PL8(j, 0x57bf08) = AT(uint8_t, g_rl_4dc1ce, 0x4dc1ceu, 0x4dc1ce + 2 * PL8(j, 0x57bf0a));
        PL8(j, 0x57bf09) = AT(uint8_t, g_rl_4dc1ce, 0x4dc1ceu, 0x4dc1cf + 2 * PL8(j, 0x57bf0a));
    }
    /* EAX: the last record offset 5 * 352 (0x6e0) with AL = the byte just stored (rl_io's 0x4978ad returns it) */
    return (int)((352u * 5u) & ~0xffu) | PL8(5, 0x57bf09);
}

/* Port of FUN_0049a010 (982 B) — the frame driver's sub-state 0 tick (the strategic turn): DAT_0053f658 counts
 * the phase's ticks; by the phase DAT_00569584: 7 the turn's end (the ten DAT_0053e8d0 slots, 0x43ad25, the turn
 * clock DAT_004e59d8, the turn advance 0x448440, the mercenary offers, 0x4651b9, the path costs, the rankings, the
 * treaties, the next phase, the palette fade 0x4b0cb4(36, 1, DAT_005691f0)); 5 the raids, 2 the siege scan (every
 * 100th tick from 2: a built siege → its assault 0x4a8aab, else the next phase), 1 the independents (tick 1 the
 * rations, 2 the AI, 3 the next phase + DAT_0057cad0 = now), 6 / 3 the marches; else (the players' phase) the turn
 * timer DAT_005440c8 (out → the End-turn action + the strip), the idle end-turn repeat every 10 s for an ended
 * local realm, the MP end vote's 2.5 s, the AI's tick (0x49a581) and the turn-resolved check. */
int rl_49a010(void)
{
    s_slot_0053f658++;
    switch ((int32_t)s_slot_00569584) {
    case 7:
        g_bf_57c998[0] = 0;
        for (int i = 0; i < 10; i++)
            AT(int32_t, &g_bf_53e8d0, 0x53e8d0u, 0x53e8d0 + 4 * i) = 0;
        rl_43ad25();
        g_rl_4e59d8 = (int32_t)rl_plat_timeGetTime();
        rl_448440();
        engine_combat_rotate_merc_offers();
        rl_4651b9();
        rl_46ff43();
        rl_49aa0e();
        rl_4a1847();
        rl_49ce51();
        rl_4b0cb4(0x24, 1, (int)(uintptr_t)g_rl_5691f0);
        return 0;
    case 5:
        if (s_slot_0053f658 == 1)
            engine_army_state_plan_raids();
        if (engine_army_state_arm_movement_owned(2, 6) == 0)
            rl_49ce51();
        return 0;
    case 2:
        if (s_slot_0053f658 == 1)
            rl_4a82b9();
        if (!MP_ON)
            g_combat_ctrl_52afac = 1;
        if ((int32_t)s_slot_0053f658 % 100 == 2) {
            if (engine_army_state_scan_siege_built() == 0) {
                rl_49ce51();
            } else {
                g_bf_55403c = 0;
                engine_retail_call(0x4a8aabu, 1, s_slot_0057cad4);   /* TODO(rl-link): the siege assault (engine_combat.c: an evq) */
            }
        }
        return 0;
    case 1:
        if (s_slot_0053f658 == 1) {
            engine_army_ai_set_rations_and_grant(0);
        } else if (s_slot_0053f658 == 2) {
            engine_army_ai_run_independent(0);
        } else if (s_slot_0053f658 == 3) {
            rl_49ce51();
            g_rl_57cad0 = (int32_t)rl_plat_timeGetTime();
        }
        return 0;
    case 6:
        if (s_slot_0053f658 == 1)
            engine_army_state_plan_settle_marches();
        if (engine_army_state_arm_movement(3) == 0)
            rl_49ce51();
        return 0;
    case 3:
        if (s_slot_0053f658 == 1)
            engine_army_state_plan_capital_marches();
        if (engine_army_state_arm_movement(4) == 0)
            rl_49ce51();
        return 0;
    }
    if ((int32_t)s_slot_005440c8 >= 0 && (int32_t)s_slot_0053f26c > 0) {
        int32_t dt = (int32_t)(rl_plat_timeGetTime() - g_combat_anim_deadline);
        s_slot_005440c8 = (uint32_t)((int32_t)s_slot_0053f26c - dt / 1000);
        if ((int32_t)s_slot_005440c8 < 0) {
            s_slot_005440c8 = (uint32_t)-1;
            rl_43ac23();
            engine_retail_call(0x41a734u, 1, 1u);   /* TODO(rl-link): the End-turn strip (engine_hud_panel_endturn: a panel environment) */
        }
    }
    if (PL32(LOCAL, 0x57bf00) == 999) {
        uint32_t now = rl_plat_timeGetTime();
        if ((int32_t)(now - (uint32_t)g_rl_4ee8c0) > 10000) {
            g_rl_4ee8c0 = (int32_t)now;
            rl_43ac23();
        }
    }
    if (g_bf_55403c > 1 && PL32(LOCAL, 0x57bf00) != 999) {
        int32_t dt = (int32_t)(rl_plat_timeGetTime() - (uint32_t)g_rl_57cad0);
        if (!MP_ON)
            dt = 3001;
        if (dt > 2500) {
            g_rl_553018 = g_rl_55ce88;
            g_rl_553018 = 1;
            g_bf_55403c = 0;
            engine_retail_call(0x41a734u, 1, 1u);   /* TODO(rl-link): the End-turn strip */
            s_slot_005440c8 = s_slot_0053f26c;
            g_combat_anim_deadline = rl_plat_timeGetTime();
        }
    }
    engine_retail_call(0x49a581u, 0);   /* TODO(rl-link): the AI's per-tick pass (engine_army_ai_run_per_tick: an evq) */
    if (rl_49b762() != 0)
        rl_49ce51();
    return 0;
}

/* Port of FUN_00476aa7 (770 B) — the help hints (the option DAT_0053f24c, in game: DAT_0052af94 3): wait out the
 * popup's page count DAT_004f0358, then open the first unseen hint report (DAT_004f0360[id - 200]) the situation
 * calls for: on the map (screen 0, sub-state 0) the campaign's 206, then 200 / 201 / 202; on the battlefield (sub-state
 * 2, no result pending DAT_0057a0f0) 214 / 215 in a siege (DAT_0053e8fc) or 212; screen 0x17 → 209, 0x0f with the
 * panel 8 (DAT_00553f54) → 208, 2 → 207, 0x39 → 218, 0x1b → 217, 0x10 → 210; a relation change (DAT_00553210,
 * cleared) → 211. */
int rl_476aa7(void)
{
#define SEEN(id) g_rl_4f0360[(id) - 200]
    if (g_rl_53f24c == 0 || g_bfr_engine_state != 3)
        return 0;
    if (g_rl_4f0358 != 0) {
        g_rl_4f0358--;
        return 0;
    }
    if (SCREEN == 0 && s_slot_0057c8fc == 0) {
        if (s_slot_0057cb18 == 2 && SEEN(206) == 0)
            return rl_476da9(206);
        if (SEEN(200) == 0)
            return rl_476da9(200);
        if (SEEN(201) == 0)
            return rl_476da9(201);
        if (SEEN(202) == 0)
            return rl_476da9(202);
    }
    if (SCREEN == 0 && s_slot_0057c8fc == 2) {
        if (s_slot_0057a0f0 != 0)
            return 0;
        if (s_slot_0053e8fc != 0) {
            if (SEEN(214) == 0)
                return rl_476da9(214);
            if (SEEN(215) == 0)
                return rl_476da9(215);
        } else if (SEEN(212) == 0) {
            return rl_476da9(212);
        }
    }
    if (SCREEN == 0x17 && SEEN(209) == 0)
        return rl_476da9(209);
    if (SCREEN == 0x0f && g_rl_553f54 == 8 && SEEN(208) == 0)
        return rl_476da9(208);
    if (SCREEN == 2 && SEEN(207) == 0)
        return rl_476da9(207);
    if (SCREEN == 0x39 && SEEN(218) == 0)
        return rl_476da9(218);
    if (SCREEN == 0x1b && SEEN(217) == 0)
        return rl_476da9(217);
    if (SCREEN == 0x10 && SEEN(210) == 0)
        return rl_476da9(210);
    if (g_combat_flag_553210 != 0) {
        g_combat_flag_553210 = 0;
        if (SEEN(211) == 0)
            return rl_476da9(211);
    }
    return 0;
#undef SEEN
}

/* ---------------------------------------------------------------- the realms' setup */

#define TBL32(va)     AT(int32_t,  g_rl_4dc070, 0x4dc070u, (int32_t)(va))                  /* the setup tables 0x4dc070.. */
#define TBL8(va)      AT(uint8_t,  g_rl_4dc070, 0x4dc070u, (int32_t)(va))

/* Port of FUN_0049bd99 (3068 B) — set the realms up for a new game: every county's stocks by the start-resources
 * option DAT_0053f27c (20-B rows 0x4dc0d0: +0x228 / +0x224, +0x254 / +0x250, +0x28 / +0x24, the bytes +0x0b /
 * +0x0d / +0x0c), its census and grades; every seated realm (human +5, or an AI +7) active with its record zeroed,
 * its home county (the roster's army byte) owned (+5) and job-slotted (+7); within the county count: the home
 * (+0xe4), the start cash DAT_0053f278, the start army by DAT_0053f274 (the 7 troop rows 0x4dc110 into the muster
 * rows DAT_0053f6a0 / DAT_0053f710, the county's people +0x24; spawned 0x4a9a9a, its cost +0x15c added to the cash),
 * the workforce / provision / plan twice, the keep tier DAT_0053f280 (a secondary keep 0x46826c), +0x120 / +0x130 /
 * +0x128 = 50, the six categories by DAT_0053f270 (0x4dc070), an AI's +0x150 += 20 · the difficulty DAT_0053f23c,
 * the first set field of the county's three non-2 slots +0x2f5 flagged, +0x1b0 = 1; unowned counties +100 / +100;
 * the camera on the local realm's home (0x432746, 0x46dfd5). */
int rl_49bd99(void)
{
    for (int i = 1; i <= (int32_t)s_slot_0056d5dc; i++) {
        int o = 20 * g_rl_53f27c;
        CO32(i, 0x53fbd8) = TBL32(0x4dc0d0 + o);
        CO32(i, 0x53fbd4) = CO32(i, 0x53fbd8);
        CO32(i, 0x53fc04) = TBL32(0x4dc0d4 + o);
        CO32(i, 0x53fc00) = CO32(i, 0x53fc04);
        CO32(i, 0x53f9d8) = TBL32(0x4dc0d8 + o);
        CO32(i, 0x53f9d4) = CO32(i, 0x53f9d8);
        CO8(i, 0x53f9bb) = TBL8(0x4dc0dc + o);
        CO8(i, 0x53f9bd) = TBL8(0x4dc0e0 + o);
        CO8(i, 0x53f9bc) = CO8(i, 0x53f9bd);
        engine_territory_census_army_terrain(i);
        engine_territory_grade_feature_cells(i);
    }
    for (int j = 1; j < 6; j++) {
        if (PL8(j, 0x57bf05) == 0 && PL8(j, 0x57bf07) == 0)
            continue;
        PL8(j, 0x57bf04) = 1;
        PL32(j, 0x57bf00) = 0;
        PL8(j, 0x57bf0c) = 0;
        PL32(j, 0x57bf10) = 0;
        PL32(j, 0x57bf14) = 0;
        PL32(j, 0x57bf18) = 0;
        PL8(j, 0x57bf1c) = 0;
        PL8(j, 0x57bf1d) = 0;
        PL8(j, 0x57bf1e) = 0;
        PL32(j, 0x57bf20) = 0;
        PL32(j, 0x57bf24) = 0;
        PL8(j, 0x57bf28) = 0;
        PL8(j, 0x57bf29) = 1;
        PL8(j, 0x57bf2a) = 1;
        PL8(j, 0x57bf2b) = 0;
        PL8(j, 0x57bf2c) = 0;
        for (int b = 0x57bf45; b <= 0x57bf4d; b++)
            PL8(j, b) = 0;
        PL32(j, 0x57bf50) = 0;
        PL32(j, 0x57bf54) = 0;
        PL8(j, 0x57bf58) = 0;
        PL32(j, 0x57bf5c) = 0;
        PL8(j, 0x57bf60) = 0;
        PL8(j, 0x57bf61) = 0;
        PL32(j, 0x57bf64) = 0;
        PL32(j, 0x57bf68) = 0;
        PL32(j, 0x57bf6c) = 0;
        PL32(j, 0x57bf80) = 0;   /* four times in retail (a loop with no index) */
        PL8(j, 0x57bf80) = 0;
        PL8(j, 0x57bf81) = 0;
        for (int b = 0x57bfe7; b <= 0x57bfed; b++)
            PL8(j, b) = 0;
        PL8(j, 0x57bfef) = 0;
        PL8(j, 0x57bff0) = 0;
        for (int d = 0x57bff4; d <= 0x57c03c; d += 4)
            PL32(j, d) = 0;
        for (int m = 0; m < 6; m++)
            PL32(j, 0x57c040 + 4 * m) = 0;
        PL8(j, 0x57c058) = 0;
        PL8(j, 0x57c059) = 0;
        int home = g_player_roster[2 * j];
        CO8(home, 0x53f9b5) = (uint8_t)j;
        CO8(home, 0x53f9b7) = PL8(j, 0x57bf0a);
        if (home > (int32_t)s_slot_0056d5dc)
            continue;
        PL8(j, 0x57bfe4) = (uint8_t)home;
        PL32(j, 0x57c018) = g_rl_53f278;
        if (g_rl_53f274 != 0) {
            AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f710 + 128 * j) = 0;
            for (int n = 0; n < 7; n++) {
                int32_t v = TBL32(0x4dc110 + 28 * g_rl_53f274 + 4 * n);
                AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f6a0 + 128 * j + 16 * n) = v;
                AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f710 + 128 * j) += TBL32(0x4dc110 + 28 * g_rl_53f274 + 4 * n);
                CO32(home, 0x53f9d4) += TBL32(0x4dc110 + 28 * g_rl_53f274 + 4 * n);
            }
            engine_combat_spawn_unit(j, home, 0, 0);
            CO32(home, 0x53fca4) = 0;
            PL32(j, 0x57c018) += AT(int32_t, g_per_territory_420, 0x52f0b0u, 0x52f20c + 420 * engine_territory_current_slot());
        }
        engine_territory_allocate_realm_workforce(home);
        engine_army_economy_provision(home, (int)s_slot_0057c934);
        engine_army_economy_plan_realm(home, (int)s_slot_0057c92c);
        engine_territory_allocate_realm_workforce(home);
        engine_army_economy_provision(home, (int)s_slot_0057c934);
        engine_army_economy_plan_realm(home, (int)s_slot_0057c92c);
        CO8(home, 0x53fb70) = (uint8_t)g_rl_53f280;
        if (CO8(home, 0x53fb70) != 0)
            engine_territory_stamp_secondary_keep(home, CO8(home, 0x53fb70) - 1);
        PL32(j, 0x57c020) = 50;
        PL32(j, 0x57c030) = 50;
        PL32(j, 0x57c028) = 50;
        for (int k = 0; k < 6; k++)
            PL32(j, 0x57c040 + 4 * k) = TBL32(0x4dc070 + 24 * g_rl_53f270 + 4 * k);
        if (PL8(j, 0x57bf05) == 0)
            PL32(j, 0x57c050) += 20 * (int32_t)s_slot_0053f23c;
        for (int k = 0; k < 4; k++)
            if (k != 2 && AT(int8_t, g_per_army_768, 0x53f9b0u, 0x53fc45 + 768 * home + 24 * k) != 0) {
                CO8(home, 0x53fc47 + 24 * k) = 1;
                break;
            }
        CO8(home, 0x53fb60) = 1;
    }
    for (int k = 1; k <= (int32_t)s_slot_0056d5dc; k++)
        if (CO8(k, 0x53f9b5) == 0) {
            CO32(k, 0x53fbd4) += 100;
            CO32(k, 0x53fbd8) += 100;
        }
    rl_432746(g_player_roster[2 * LOCAL]);
    return rl_46dfd5(g_player_roster[2 * LOCAL]);
}

/* ---------------------------------------------------------------- the rankings */

/* Port of FUN_0049aa0e (2197 B) — recompute the standings over the active realms 1..5 (engine_ranking.c's
 * engine_ranking_recompute, on retail's layout — the old port posts into a port queue): the active count without
 * the local realm (DAT_0056d5d8); each metric's maximum + its holder (+0x29 counties, +0x10 strength, +0x0c rating,
 * +0x58 standing, +0x118 cash, +0x2c territories, +0x54 territory value, +0x4c); each realm's score +0x50 (inactive:
 * 0) = 10 · +0x60 + +0x10 / 10 + 2 · +0x0c + 2 · +0x58 + +0x54 / 5 + 50 · +0x4c, +50 with cash > 2000 (the > 5000 /
 * > 10000 arms behind it are dead); the board DAT_00565410 ({realm, score} × 5 + a zero sentinel) bubble-sorted
 * descending; the ranks +0x2b (inactive: 0, off the board); the first / last ranked (DAT_00553d24 / DAT_00522d90);
 * a sole survivor: an AI's first victory notice 0xc3 (+0xed latched, the mood cycle +0x159), else the local 0xe1. */
int rl_49aa0e(void)
{
#define BSLOT(i)  (*(int32_t *)&s_slot_00565410[(i) * 8 + 0])
#define BSCORE(i) (*(int32_t *)&s_slot_00565410[(i) * 8 + 4])
    s_slot_00553d24 = 0;
    s_slot_00522d90 = 0;
    s_slot_0056d5d8 = 0;
    s_slot_0056d6a8 = 0;   s_slot_0056d644 = 0;
    s_slot_0053f058 = 0;   s_slot_0052f0a4 = 0;
    s_slot_00544098 = 0;   s_slot_00553c74 = 0;
    s_slot_0053e9cc = 0;   s_slot_0053e97c = 0;
    s_slot_00553028 = 0;   s_slot_00522f68 = 0;
    s_slot_00553034 = 0;   s_slot_00568da0 = 0;
    s_slot_0055ce80 = 0;   s_slot_005651c4 = 0;
    s_slot_0056d5bc = 0;   s_slot_0056d634 = 0;
    for (int p = 1; p < 6; p++) {
        if (PL8(p, 0x57bf04) == 0)
            continue;
        if ((int)s_slot_0057c8cc != p)
            s_slot_0056d5d8++;
        if ((int32_t)PL8(p, 0x57bf29) > (int32_t)s_slot_00522f68) { s_slot_00522f68 = PL8(p, 0x57bf29); s_slot_00553028 = (uint32_t)p; }
        if (PL32(p, 0x57bf10) > (int32_t)s_slot_0056d644)        { s_slot_0056d644 = (uint32_t)PL32(p, 0x57bf10); s_slot_0056d6a8 = (uint32_t)p; }
        if ((int32_t)PL8(p, 0x57bf0c) > (int32_t)s_slot_0052f0a4) { s_slot_0052f0a4 = PL8(p, 0x57bf0c); s_slot_0053f058 = (uint32_t)p; }
        if ((int32_t)PL8(p, 0x57bf58) > (int32_t)s_slot_00553c74) { s_slot_00553c74 = PL8(p, 0x57bf58); s_slot_00544098 = (uint32_t)p; }
        if (PL32(p, 0x57c018) > (int32_t)s_slot_0053e97c)        { s_slot_0053e97c = (uint32_t)PL32(p, 0x57c018); s_slot_0053e9cc = (uint32_t)p; }
        if ((int32_t)PL8(p, 0x57bf2c) > (int32_t)s_slot_0056d634) { s_slot_0056d634 = PL8(p, 0x57bf2c); s_slot_0056d5bc = (uint32_t)p; }
        if (PL32(p, 0x57bf54) > (int32_t)s_slot_005651c4)        { s_slot_005651c4 = (uint32_t)PL32(p, 0x57bf54); s_slot_0055ce80 = (uint32_t)p; }
        if ((int32_t)PL8(p, 0x57bf4c) > (int32_t)s_slot_00568da0) { s_slot_00568da0 = PL8(p, 0x57bf4c); s_slot_00553034 = (uint32_t)p; }
    }
    for (int p = 1; p < 6; p++) {
        PL32(p, 0x57bf50) = 0;
        if (PL8(p, 0x57bf04) == 0)
            continue;
        int32_t s = 0;
        s += PL8(p, 0x57bf60) * 10;
        s += PL32(p, 0x57bf10) / 10;
        s += PL8(p, 0x57bf0c) * 2;
        s += PL8(p, 0x57bf58) * 2;
        s += PL32(p, 0x57bf54) / 5;
        s += PL8(p, 0x57bf4c) * 50;
        int32_t cash = PL32(p, 0x57c018);
        if (cash > 2000)
            s += 50;
        else if (cash > 5000)
            s += 100;   /* dead (behind > 2000) */
        else if (cash > 10000)
            s += 200;   /* dead */
        PL32(p, 0x57bf50) = s;
    }
    for (int i = 0; i < 5; i++) {
        BSLOT(i) = i + 1;
        BSCORE(i) = PL32(i + 1, 0x57bf50);
    }
    BSCORE(5) = 0;
    BSLOT(5) = 0;
    for (int pass = 0; pass < 5; pass++)
        for (int i = 0; i < 5; i++)
            if (BSCORE(i + 1) > BSCORE(i)) {
                int32_t ts = BSLOT(i + 1), tc = BSCORE(i + 1);
                BSLOT(i + 1) = BSLOT(i);
                BSCORE(i + 1) = BSCORE(i);
                BSLOT(i) = ts;
                BSCORE(i) = tc;
            }
    for (int i = 0; i < 5; i++) {
        int r = BSLOT(i);
        if (PL8(r, 0x57bf04) != 0) {
            PL8(r, 0x57bf2b) = (uint8_t)(i + 1);
        } else {
            PL8(r, 0x57bf2b) = 0;
            BSLOT(i) = 0;
        }
    }
    for (int i = 0; i < 5; i++) {
        if (BSLOT(i) == 0)
            continue;
        if (s_slot_00553d24 == 0)
            s_slot_00553d24 = (uint32_t)BSLOT(i);
        s_slot_00522d90 = (uint32_t)BSLOT(i);
    }
    if (s_slot_00522d90 == s_slot_00553d24) {
        int w = (int)s_slot_00553d24;
        if (PL8(w, 0x57bfed) == 0 && PL8(w, 0x57bf05) == 0) {
            PL8(w, 0x57bfed) = 1;
            evq_post(w, 0, 0xc3, PL8(w, 0x57c059) + 4 * PL8(w, 0x57bf07) - 4, 1, 0, 0, 0);
            if (++PL8(w, 0x57c059) >= 4)
                PL8(w, 0x57c059) = 0;
        } else {
            PL8(w, 0x57bfed) = 1;
            evq_post(0, LOCAL, 0xe1, 0, 0x0e, 0, 0, 0);
        }
    }
    return 0;
#undef BSLOT
#undef BSCORE
}

/* ---------------------------------------------------------------- the AI's diplomacy inbox (brain step 1) */

#define PERS32(p, off) AT(int32_t, g_rl_4d8a58, 0x4d8a58u, 0x4d8a58 + 0xf0 * ((p) - 1) + (off))   /* personality row */
#define REL(a, b)     PLS8(a, 0x57bf84 + 16 * (b))                                             /* a's relation toward b */

/* the relation clamp every handler applies: above +30 → +30, below -30 → -30 */
static void rel_clamp(int a1, int a2)
{
    if (REL(a1, a2) > 30)
        REL(a1, a2) = 30;
    else if (REL(a1, a2) < -30)
        REL(a1, a2) = -30;
}

/* a diplomacy notice from a1 to a2: the variant = a1's mood cycle (+0x159) + 4 × its personality - 4; the cycle
 * advances, wrapping at 4 */
static void inbox_notice(int a1, int a2, int code, int e, int f)
{
    evq_post(a1, a2, code, PL8(a1, 0x57c059) + 4 * PL8(a1, 0x57bf07) - 4, e, f, 0, 0);
    if (++PL8(a1, 0x57c059) >= 4)
        PL8(a1, 0x57c059) = 0;
}

/* Port of FUN_004a29b2 (804 B) — inbox type 0, an offer of `a3` from a2 (a human, +5): against the best offer so far
 * (+0x8c) and the personality's band (+8): below best + band / 2 → -8 (0xad), below best + band → +5 (0xac), else
 * +10 (0xab); the best raised; the notice. */
int rl_4a29b2(int a1, int a2, int a3)
{
    if (PL8(a2, 0x57bf05) == 0)
        return 0;
    int band = PERS32(PL8(a1, 0x57bf07), 8);
    int32_t *best = &PL32(a1, 0x57bf8c + 16 * a2);
    int tier, code;
    if (*best + band / 2 > a3)      { tier = 0; code = 0xad; }
    else if (*best + band > a3)     { tier = 1; code = 0xac; }
    else                            { tier = 2; code = 0xab; }
    if (*best < a3)
        *best = a3;
    REL(a1, a2) = (int8_t)(REL(a1, a2) + (tier == 0 ? -8 : tier == 1 ? 5 : 10));
    rel_clamp(a1, a2);
    inbox_notice(a1, a2, code, 1, 0);
    return 0;
}

/* Port of FUN_004a2cd6 (651 B) — inbox type 1, a compliment from a2 (a human): by the pair's compliment count
 * (+0x89): 0 / 1 → +15 (0xae), 2 → +8 (0xaf), more → -4 (0xb0); the notice. */
int rl_4a2cd6(int a1, int a2)
{
    if (PL8(a2, 0x57bf05) == 0)
        return 0;
    uint8_t n = PL8(a1, 0x57bf89 + 16 * a2);
    int d, code;
    if (n < 2)       { d = 15; code = 0xae; }
    else if (n < 3)  { d = 8;  code = 0xaf; }
    else             { d = -4; code = 0xb0; }
    REL(a1, a2) = (int8_t)(REL(a1, a2) + d);
    rel_clamp(a1, a2);
    inbox_notice(a1, a2, code, 1, 0);
    return 0;
}

/* Port of FUN_004a2f61 (405 B) — inbox type 2, a human breaks the bond: the treaty cleared, -20, the notice 0xb1. */
int rl_4a2f61(int a1, int a2)
{
    if (PL8(a2, 0x57bf05) == 0)
        return 0;
    rl_4a1a54(a1, a2);
    REL(a1, a2) = (int8_t)(REL(a1, a2) - 20);
    rel_clamp(a1, a2);
    inbox_notice(a1, a2, 0xb1, 1, 0);
    return 0;
}

/* Port of FUN_004a30f6 (895 B) — inbox type 3, an alliance proposal from a2: already allied (+0x88) → 0xc4; three or
 * more realms in play (DAT_00554004): a1 bound already → 0xc5, a2 bound → 0xb2, relation > 10 → accept (0xb3),
 * < -10 → 0xb2, else accept when the RNG byte DAT_0058fd60 < 3 rel + 45; fewer realms → 0xb2. The proposal cooldown
 * +0xec counts down; 0xb2 → -2, 0xb3 → +4 and the bond (0x4a1774), else -1; the notice. */
int rl_4a30f6(int a1, int a2)
{
    int code;
    if (PL8(a1, 0x57bf88 + 16 * a2) != 0)
        code = 0xc4;
    else if ((int32_t)s_slot_00554004 > 2) {
        if (PL8(a1, 0x57bf81) != 0)
            code = 0xc5;
        else if (PL8(a2, 0x57bf81) != 0)
            code = 0xb2;
        else if (REL(a1, a2) > 10)
            code = 0xb3;
        else if (REL(a1, a2) < -10)
            code = 0xb2;
        else
            code = 3 * REL(a1, a2) + 45 <= g_bf_58fd60 ? 0xb2 : 0xb3;
    } else
        code = 0xb2;
    if (PL8(a1, 0x57bfec) != 0)
        PL8(a1, 0x57bfec)--;
    if (code == 0xb2) {
        REL(a1, a2) = (int8_t)(REL(a1, a2) - 2);
    } else if (code == 0xb3) {
        REL(a1, a2) = (int8_t)(REL(a1, a2) + 4);
        rl_4a1774(a1, a2);
    } else {
        REL(a1, a2) = (int8_t)(REL(a1, a2) - 1);
    }
    rel_clamp(a1, a2);
    inbox_notice(a1, a2, code, 1, 0);
    return 0;
}

/* Port of FUN_004a38db (908 B) — inbox type 6, a tribute demand `a3` from a2: unless a2 is a1's lord (+0x81), a1's
 * average troops (+0x14) are under the personality's floor (+0x30) or the relation is under 10 → refuse (0xba: the
 * pair's contact count +0x86 += 2); the RNG byte < 2 rel → a token gift (0xbb, 0x4a1b29 with 0); else the demand is
 * staged (DAT_0055ce50 = a3, DAT_00567958 = the pair's count +0x91 × the personality's unit +0x0c) and refused (0xbc,
 * variant 10). */
int rl_4a38db(int a1, int a2, int a3)
{
    int code = 0xba;
    if (PL8(a1, 0x57bf81) == a2 && PL32(a1, 0x57bf14) >= PERS32(PL8(a1, 0x57bf07), 0x30) && REL(a1, a2) >= 10)
        code = g_bf_58fd60 >= 2 * REL(a1, a2) ? 0xbc : 0xbb;
    if (code == 0xba) {
        PL8(a1, 0x57bf86 + 16 * a2) += 2;
        inbox_notice(a1, a2, 0xba, 1, a3);
    } else if (code == 0xbb) {
        inbox_notice(a1, a2, 0xbb, 1, a3);
        engine_retail_call(0x4a1b29u, 4, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, 0u);   /* TODO(rl-extern): the gift 0x4a1b29 */
    } else {
        s_slot_0055ce50 = (uint32_t)a3;
        s_slot_00567958 = (uint32_t)(PL8(a1, 0x57bf91 + 16 * a2) * PERS32(PL8(a1, 0x57bf07), 0x0c));
        inbox_notice(a1, a2, code, 10, a3);
    }
    return 0;
}

/* Port of FUN_004a277d (565 B) — AI brain step 1: dispatch realm a1's five-slot inbox (DAT_0053f0f0 + 40 a1: +0
 * sender, +1 type, +2 byte, +4 i32; the first empty slot ends it; its own messages skipped) to the type handlers
 * (0 0x4a29b2 with the i32, 1 / 2 / 3 / 4, 5 0x4a3551 / 6 0x4a38db with the byte), then clear the inbox (+3 kept)
 * and the pair bytes +0x90. */
int rl_4a277d(int a1)
{
    for (int i = 0; i < 5; i++) {
        uint8_t *e = &s_slot_0053f0f0[40 * a1 + 8 * i];
        int sender = e[0], type = e[1], param = e[2];
        if (e[0] == 0)
            break;
        if (sender == a1)
            continue;
        if (e[1] == 0) {
            rl_4a29b2(a1, sender, *(int32_t *)(e + 4));
            continue;
        }
        switch (type) {
        case 1: rl_4a2cd6(a1, sender); break;
        case 2: rl_4a2f61(a1, sender); break;
        case 3: rl_4a30f6(a1, sender); break;
        case 4: rl_4a3475(a1, sender); break;
        case 5: engine_retail_call(0x4a3551u, 3, (uint32_t)a1, (uint32_t)sender, (uint32_t)param); break;   /* TODO(rl-extern): the type-5 tribute 0x4a3551 */
        case 6: rl_4a38db(a1, sender, param); break;
        }
    }
    for (int j = 0; j < 5; j++) {
        uint8_t *e = &s_slot_0053f0f0[40 * a1 + 8 * j];
        e[0] = 0; e[1] = 0; e[2] = 0;
        *(int32_t *)(e + 4) = 0;
    }
    for (int k = 0; k < 6; k++)
        PL8(a1, 0x57bf90 + 16 * k) = 0;
    return 0;
}

/* ---------------------------------------------------------------- the sheet loaders, the save rotation */

/* Port of FUN_004984dc (731 B) — load the strategic map's sheets: the panels (0x4b25b0(220000)), the other modes'
 * sheets freed (0x49915e / 0x499366 / 0x49964c), the panel mode 1; the eight terrain / feature / overlay banks of
 * the season (DAT_0057c934 1..4: rows 8 × (season - 1)) and the map kind (DAT_0057cb18 2: +32) from the record
 * table 0x4da050 (16-B name, size at +0x10; a 0 size skips the bank), each after the stream / cue checks
 * (0x4b8c3a, 0x426529) into a fresh block; then misc_cty.pl8 (DAT_005530c8) and — unless DAT_0057c968 is 1 —
 * vill_gd8.pl8 into DAT_00542ce0. DAT_0057c968 = 0 on every exit; 1 = loaded, 0 = a file / memory error (logged). */
int rl_4984dc(void)
{
    rl_4af502();
    rl_4b25b0(0x35b60);
    rl_49915e();
    rl_499366();
    rl_49964c();
    g_wf_panel_mode = 1;
    int row = s_slot_0057cb18 == 2 ? 0x20 : 0;
    if ((int32_t)s_slot_0057c934 > 0 && (int32_t)s_slot_0057c934 <= 4)
        row += 8 * (int32_t)s_slot_0057c934 - 8;
    int name = 0;
    for (int i = 0; i < 8; i++) {
        int32_t size = AT(int32_t, g_rl_4d9f48, 0x4d9f48u, 0x4da060 + 20 * (i + row));
        name = STRVA(g_rl_4d9f48, 0x4d9f48u, 0x4da050 + 20 * (i + row));
        if (size == 0)
            continue;
        rl_4b8c3a();
        rl_426529();
        void *buf = rl_plat_malloc((uint32_t)size);
        if (buf == NULL)
            goto nomem;
        if (rl_4af8d9(name, (int)(uintptr_t)buf, size, 0) == 0)
            goto notfound;
        switch (i) {
        case 0: g_mv_terrain_blob0 = buf; break;
        case 1: g_mv_terrain_blob1 = buf; break;
        case 2: g_mv_terrain_blob2 = buf; break;
        case 3: g_mv_terrain_blob3 = buf; break;
        case 4: g_mv_terrain_blob4 = buf; break;
        case 5: g_mv_feature_blob = buf; break;
        case 6: g_mv_feature_alt_blob = buf; break;
        case 7: g_mv_overlay_blob = buf; break;
        }
    }
    {
        int32_t size = g_rl_4daec0;
        name = STRVA(g_bf_4daeb0, 0x4daeb0u, 0x4daeb0u);   /* "misc_cty.pl8" */
        g_tr_misc_sheet = rl_plat_malloc((uint32_t)size);
        if (g_tr_misc_sheet == NULL)
            goto nomem;
        if (rl_4af8d9(name, (int)(uintptr_t)g_tr_misc_sheet, size, 0) == 0)
            goto notfound;
    }
    if (g_rl_57c968 == 1) {
        g_rl_57c968 = 0;
        return 1;
    }
    rl_4af8d9(STRVA(g_rl_4dbcd4, 0x4dbcd4u, 0x4dbcd4u), (int)(uintptr_t)g_bf_542ce0, 0x12d8, 0);   /* "vill_gd8.pl8" */
    g_rl_57c968 = 0;
    rl_4afab9(STRVA(g_rl_4dbcd4, 0x4dbcd4u, 0x4dbce4u), 0, 0);   /* "OK :Data loaded OK." */
    return 1;
notfound:
    g_rl_57c968 = 0;
    rl_4afab9(STRVA(g_rl_4dbcd4, 0x4dbcd4u, 0x4dbcf8u), name, 0);   /* "ERR:Data load, couldn't find  " */
    return 0;
nomem:
    g_rl_57c968 = 0;
    rl_4afab9(STRVA(g_rl_4dbcd4, 0x4dbcd4u, 0x4dbd18u), name, 0);   /* "ERR:Data Mem, couldn't allocate " */
    return 0;
}

/* Port of FUN_00499859 (451 B) — load the 13 system files of the record table 0x4d9f48 (16-B name, size at +0x10)
 * into their fixed buffers (palettes, fonts, the sheets, the localization blob, …); the first missing file is
 * logged, 0. */
int rl_499859(void)
{
    static uint8_t *const dst[13] = {
        g_rl_5691f0, g_rl_5675a0, g_rl_568ee0, g_rl_5cbfb0, g_rl_5c9a90, g_rl_5aeba0, g_bf_5af8f0, g_rl_5b2fa0,
        g_rl_58fec0, g_rl_5bb540, g_rl_57d3d0, g_rl_591580, g_bf_542ce0,
    };
    rl_4af502();
    for (int i = 0; i < 13; i++) {
        int32_t size = AT(int32_t, g_rl_4d9f48, 0x4d9f48u, 0x4d9f58 + 20 * i);
        int name = STRVA(g_rl_4d9f48, 0x4d9f48u, 0x4d9f48 + 20 * i);
        if (rl_4af8d9(name, (int)(uintptr_t)dst[i], size, 0) == 0) {
            rl_4afab9(STRVA(g_rl_4dbf38, 0x4dbf38u, 0x4dbf80u), name, 0);   /* "ERR:Data load, couldn't find a system gfx file." */
            return 0;
        }
    }
    rl_4afab9(STRVA(g_rl_4dbf38, 0x4dbf38u, 0x4dbf68u), 0, 0);   /* "OK :Sys Gfx loaded OK." */
    return 1;
}

/* Port of FUN_00499034 (197 B) — load the setup screens' misc_sel.PL8 into a fresh DAT_005530c8 (the other modes'
 * sheets freed, the panel mode 3). 1 = loaded; 0 = not found / no memory (logged). */
int rl_499034(void)
{
    rl_4af502();
    rl_49915e();
    rl_499366();
    rl_49964c();
    g_wf_panel_mode = 3;
    int32_t size = AT(int32_t, g_rl_4daed8, 0x4daed8u, 0x4daee8);
    int name = STRVA(g_rl_4daed8, 0x4daed8u, 0x4daed8u);   /* "misc_sel.PL8" (misc_cty.pl8's record + 0x28) */
    g_tr_misc_sheet = rl_plat_malloc((uint32_t)size);
    if (g_tr_misc_sheet == NULL) {
        rl_4afab9(STRVA(g_rl_4dbec0, 0x4dbec0u, 0x4dbee0u), name, 0);   /* "ERR:BATTLE Start Mem, couldn't allocate." */
        return 0;
    }
    if (rl_4af8d9(name, (int)(uintptr_t)g_tr_misc_sheet, size, 0) != 0)
        return 1;
    rl_4afab9(STRVA(g_rl_4dbec0, 0x4dbec0u, 0x4dbec0u), name, 0);   /* "ERR:Start load, couldn't find." */
    return 0;
}

/* Port of FUN_00499a1c (174 B) — load system sheet `a0` (system2.pl8 / system.pl8: 16-B names at 0x4dbbd0) into
 * DAT_005bb540 (0xdd18 B); a repeated request for sheet 1 is a no-op (0). DAT_004dbbf0 = the sheet. 1 = loaded. */
int rl_499a1c(int a0)
{
    rl_4af502();
    if (a0 == 1 && g_rl_4dbbf0 == a0)
        return 0;
    g_rl_4dbbf0 = a0;
    int name = STRVA(g_rl_4dbbd0, 0x4dbbd0u, 0x4dbbd0 + 16 * a0);
    if (rl_4af8d9(name, (int)(uintptr_t)g_rl_5bb540, 0xdd18, 0) != 0) {
        rl_4afab9(STRVA(g_rl_4dbf38, 0x4dbf38u, 0x4dbfb0u), 0, 0);   /* "OK :Sys2 Gfx loaded OK." */
        return 1;
    }
    rl_4afab9(STRVA(g_rl_4dbf38, 0x4dbf38u, 0x4dbfc8u), name, 0);   /* "ERR:Data load, couldn't find a system gfx file." */
    return 0;
}

/* Port of FUN_004997bf (154 B) — (re)allocate the county screen's two villager sheets (the old ones freed:
 * 0x49964c, 0x499619): DAT_005533c4 0x249f0 B, DAT_0053e918 0x3d090 B; out of memory → 0x4b2636, the log (the
 * step that failed + 1), exit(100). */
int rl_4997bf(void)
{
    int step = 0;
    rl_49964c();
    rl_499619();
    g_wf_villani2 = rl_plat_malloc(0x249f0u);
    if (g_wf_villani2 != NULL) {
        step++;
        g_wf_villani1 = rl_plat_malloc(0x3d090u);
        if (g_wf_villani1 != NULL)
            return 1;
    }
    engine_retail_call(0x4b2636u, 0);   /* TODO(rl-extern): 0x4b2636 (the display teardown) */
    rl_4afab9(STRVA(g_rl_4dbf38, 0x4dbf38u, 0x4dbf38u), 0, step + 1);   /* "ERR:Error loading temp2, cannot alloc no :- " */
    rl_plat_exit(100);
    return 0;
}

/* Port of FUN_0049a453 (302 B) — rotate the turn saves and write the new one: the names by the session (MP: host
 * lastturn / old_turn / safeturn .sva, else .svb; SP: .sav) into DAT_004ee8d0 / OldFilename / FileName (13 B each);
 * with DAT_00553260 <= 0 safeturn is removed, old_turn → safeturn, lastturn → old_turn; then the save (0x4ade93). */
int rl_49a453(void)
{
    int base = MP_ON ? (g_tr_p4_gate_b != 0 ? 0x4dc290 : 0x4dc2c0) : 0x4dc2f0;
    int last = STRVA(g_rl_4ee8d0, 0x4ee8d0u, 0x4ee8d0u);
    int old = STRVA(g_rl_4ee8a0, 0x4ee8a0u, 0x4ee8a0u);
    int safe = STRVA(g_rl_4ee8a0, 0x4ee8a0u, 0x4ee8b0u);
    rl_401136(STRVA(g_rl_4dc290, 0x4dc290u, base), last, 13);
    rl_401136(STRVA(g_rl_4dc290, 0x4dc290u, base + 0x10), old, 13);
    rl_401136(STRVA(g_rl_4dc290, 0x4dc290u, base + 0x20), safe, 13);
    if (g_rl_553260 <= 0) {
        rl_plat_remove((const char *)(uintptr_t)safe);
        rl_plat_rename((const char *)(uintptr_t)old, (const char *)(uintptr_t)safe);
        rl_plat_rename((const char *)(uintptr_t)last, (const char *)(uintptr_t)old);
    }
    return rl_4ade93(last);
}
