/*
 * OpenLords2 — the diplomacy-action screen's MODEL (screen 0x1a): the mode,
 * the gift amount arrow and the Yes/No seal.  The render (the gift panel, the
 * mode dispatch, the widget strip) is engine_diplomacy_action.c.
 *
 * Split from engine_diplomacy_action.c so the hybrid host (tools/rhost) links
 * the model without the gfx closure.  The mode and the action latch are retail
 * globals (engine_economy_globals.c; rhost aliases them at retail's VAs).
 *
 * Retail address map:
 *   FUN_00436372 @ 0x436372 ( 150 B) → engine_diplomacy_action_arrow
 *   FUN_00436408 @ 0x436408 (1015 B) → engine_diplomacy_action_seal (retail layout);
 *                                      engine_diplomacy_action_confirm (mode 0, the
 *                                      engine.c router's Yes path)
 */

#include "engine_diplomacy_action.h"

#include <stdint.h>

#include "config.h"                 /* config_player_prefs (lords2.inf, DAT_0053f1e0) */
#include "engine.h"                 /* g_sim_mp_active, engine_set_current_screen / _demand_redraw */
#include "engine_diplomacy.h"       /* engine_diplomacy_queue_action (FUN_004a2621) */
#include "save.h"                   /* g_player_strategic_352, g_per_army_768 */
#include "save_schema_globals.h"    /* s_slot_0057c8cc / _00553fbc / _0057a0f8 / _005530b0 / _00567d10 */
#include "strutil.h"                /* strutil_bounded_copy (FUN_00401136) */

#define SREC(s) (g_player_strategic_352 + (size_t)(s) * 0x160)
#define AREC(k) (g_per_army_768 + (size_t)(k) * 0x300)
#define F_CASH       0x118  /* i32 — a player's treasury (DAT_0057c018)          */
#define F_ACTIVE     0x05   /* u8  — the player is in the game (DAT_0057bf05)     */
#define F_LORD       0x81   /* u8  — the player's liege (BYTE1 of DAT_0057bf80)   */
#define A_OWNER      0x05   /* u8  — the county's owner (DAT_0053f9b5)            */
#define A_KEEP_WORK  0x19c  /* i32 — the county's keep build work (DAT_0053fb4c)  */

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    __builtin_memcpy(&v, rec + off, sizeof v);
    return v;
}

int  engine_diplomacy_action_mode(void)          { return g_diplo_action_mode; }
void engine_diplomacy_action_set_mode(int mode)  { g_diplo_action_mode = mode; }

/* ── FUN_00436372 — the gift amount arrow (param0: 1 = up, 0 = down) ─────── */
void engine_diplomacy_action_arrow(int param0)
{
    int amt = (int)s_slot_0057a0f8;
    if (param0 == 1) {
        amt += 0xa;
    } else if (param0 == 0) {
        amt -= 0xa;
    }
    if (amt < 0) {
        amt = 0;
    }
    int32_t cash = rec_i32(SREC((int)s_slot_0057c8cc), F_CASH);
    if (cash < amt) {
        amt = cash;                             /* can't stage more than we hold */
    }
    s_slot_0057a0f8 = (uint32_t)amt;
    engine_set_demand_redraw(2);                /* DAT_0057d340 = 2 */
}

/* ── FUN_00436408 confirm side (mode 0) ─────────────────────────────────── */
void engine_diplomacy_action_confirm(void)
{
    g_diplo_action_taken = 1;                   /* DAT_0052afa4 = 1 */

    if (g_diplo_action_mode != 0) {
        /* Non-gift modes: engine_diplomacy_action_seal (the router does not
         * dispatch them yet). */
        return;
    }
    /* mode 0 (gift): keep the staged amount, enqueue the action + transfer the
     * gold to the partner (target 0).  Single-player only — the retail MP
     * broadcast (FUN_0043eda0, gated on DAT_00553030) is a drop-in deferral. */
    engine_diplomacy_queue_action((int)s_slot_0057c8cc, (int)s_slot_00553fbc,
                                  0, (int)s_slot_0057a0f8, 0);
}

/* a refusal notice to the local player (FUN_00472bc5(0, local, code, 0…)) */
static void diplo_notice(engine_event_queue_state_t *q, int code)
{
    uint8_t has_data[7];
    for (int i = 0; i < 6; i++)
        has_data[i] = SREC(i)[F_ACTIVE];
    has_data[6] = 0;
    engine_event_queue_post(q, 0, (int)s_slot_0057c8cc, code, 0, 0, 0, 0, 0,
                            has_data, (int)s_slot_0057c8cc);
}

/* ── FUN_00436408 — the Yes / No seal, in retail layout ─────────────────────
 * No (yes == 0, DAT_0059154c): back to the Noble Faces roster (screen 0x0b).
 * Yes: clear the text editor (FUN_004020ae; `ed` may be NULL), close to the
 * map, latch DAT_0052afa4, and by mode (DAT_005651cc):
 *   1..4  copy the chosen message (lords2.inf +0xd8, or the three canned texts
 *         at +0x1a0 / +0x268 / +0x330) into the local player's message slot
 *         (save entry 149, stride 202) — the inbox entry carries no gold;
 *   5 / 6 validate the target county (DAT_005530b0): none 0xf0, unowned 0xf1;
 *         5 (ask to raise a keep) needs ours 0xf2 and building 0xf3; 6 (ask to
 *         attack) refuses ours / our liege's 0xf4 — each a notice, no action;
 *   3     to a partner who is in the game and has a liege: notice 0xdb;
 * then queue the action (FUN_004a2621) — single player; the MP send
 * FUN_0043eda0(0x48 / 0x49, 0) is deferred (rhost: DirectPlay is stubbed). */
void engine_diplomacy_action_seal(int yes, text_input_state_t *ed,
                                  engine_event_queue_state_t *q)
{
    if (yes == 0) {
        engine_set_current_screen(0x0b);
        engine_set_demand_redraw(2);
        return;
    }
    if (ed) {
        text_input_clear(ed);                   /* FUN_004020ae */
    }
    engine_set_current_screen(0);
    engine_set_demand_redraw(2);
    g_diplo_action_taken = 1;                   /* DAT_0052afa4 */

    int mode    = g_diplo_action_mode;
    int local   = (int)s_slot_0057c8cc;
    int partner = (int)s_slot_00553fbc;
    int target  = (int)s_slot_005530b0;
    const uint8_t *prefs = config_player_prefs(NULL);
    if (mode >= 1 && mode <= 4 && prefs) {
        int off = (mode == 1) ? 0xd8 : 0x1a0 + (mode - 2) * 200;
        strutil_bounded_copy(s_slot_00567d10 + local * 202, prefs + off, 200);
    }
    if (mode == 5 || mode == 6) {
        if (target == 0)              { diplo_notice(q, 0xf0); return; }
        int owner = AREC(target)[A_OWNER];
        if (owner == 0)               { diplo_notice(q, 0xf1); return; }
        if (mode == 5) {
            if (owner != local)       { diplo_notice(q, 0xf2); return; }
            if (rec_i32(AREC(target), A_KEEP_WORK) == 0) { diplo_notice(q, 0xf3); return; }
        } else if (owner == local || SREC(local)[F_LORD] == owner) {
            diplo_notice(q, 0xf4);
            return;
        }
    }
    if (mode == 3 && SREC(partner)[F_ACTIVE] != 0 && SREC(partner)[F_LORD] != 0) {
        diplo_notice(q, 0xdb);
        return;
    }
    if (mode != 0) {
        s_slot_0057a0f8 = 0;
    }
    if (g_sim_mp_active == 0) {
        engine_diplomacy_queue_action(local, partner, mode, (int)s_slot_0057a0f8, target);
    }
}
