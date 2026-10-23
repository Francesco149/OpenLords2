/* rh_hooks_economy.c — the economy / UI-action side vs retail, on identical state (rh_hooks.h contract; shared
 * plumbing in rh_port.h / rh_hooks_ai.c): the trade grid's model (src/engine_trade_model.c: the per-good figures
 * FUN_00428daf, the ale delta FUN_00435673), the diplomacy action queue FUN_004a2621 (gifts / compliments, shared
 * with the MP command path), the keep builder FUN_00436d02 (the human's build-castle action and the AI's), the
 * caravan executor FUN_004292af and the army disband FUN_00438681; and the screens' button actions, reached only
 * through the router tables (entry-patched): the trade arrows / seals FUN_00435339 … FUN_004352f2, the gift arrow
 * FUN_00436372 and the diplomacy seal FUN_00436408 (src/engine_diplomacy_action_model.c, every mode), the
 * county-action tab strip FUN_0043ae30 and the Send Supplies arrows / all-toggle; the Send Supplies retarget
 * FUN_0043b412 and commit FUN_0043b145 (src/engine_sendsupplies_model.c); the merchant rug FUN_004357a6 and the
 * trade grid's open FUN_0043530e.
 *
 * BINDING.  The figures, the caravan staging order and the UI targets are retail globals the port keeps in
 * src/engine_economy_globals.c (not linked): aliased below at their retail VAs.  The trade arrows' press-repeat
 * accel (engine_menu_input.c, cut) is bridged as a read of DAT_00591554; their cue binds to retail's WAV loader. The
 * button actions read the router's param0 (DAT_0059154c); the seal's lords2.inf messages are DAT_0053f1e0 (config.c,
 * cut: bridged) and its editor clear goes through a port text_input_state_t loaded from / stored to retail's globals.
 */
#include "rh_port.h"
#include "save_schema_globals.h"
#include "engine_trade.h"
#include "engine_diplomacy.h"
#include "engine_army_ai.h"
#include "engine_menu_input.h"
#include "engine_diplomacy_action.h"
#include "config.h"
#include "text_input.h"
#include "engine_sendsupplies.h"

/* ---------------------------------------------------------------- binding: economy runtime globals = retail memory */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(
/* engine_economy_globals.c */
A(g_caravan_order,0x5678c0)
A(g_trade_amount,0x53e9e4) A(g_trade_offer,0x57c8c0) A(g_trade_max,0x55cd70) A(g_trade_afford,0x57a0fc)
A(g_trade_net,0x554170) A(g_trade_total,0x553f64) A(g_trade_ale,0x53e8c0) A(g_trade_category,0x568948)
A(g_trade_error,0x57c994) A(g_diplo_action_mode,0x5651cc) A(g_diplo_action_taken,0x52afa4)
A(g_send_target,0x553c78) A(g_castle_tier,0x56d898)
/* engine_minimap.c (not linked): the 128×128 county-region grid the thumbnail retarget reads */
A(g_minimap_region_grid,0x52aff0)
);
#undef A

/* ---------------------------------------------------------------- bridges across the link cut (engine_menu_input.c, config.c) */
int engine_menu_button_accel(void) { return (int)G32(0x591554u); }   /* DAT_00591554: the press-repeat accel */
const uint8_t *config_player_prefs(size_t *n)                         /* DAT_0053f1e0: lords2.inf, 0x468 B */
{ if (n) *n = 0x468; return (const uint8_t *)(uintptr_t)0x53f1e0u; }

/* the button router's param0 for the action it calls (DAT_0059154c: the seal's yes / no, the arrow's up / down) */
#define PARAM0 ((int)G32(0x59154cu))

/* text_input: the port's editor is one struct, retail's the scattered globals FUN_004020ae clears (buf DAT_005cd550,
 * cursor DAT_005bb4a8, length DAT_0058fe94, pixel overflow DAT_0057d3b0, anchor DAT_0058fd54) */
static text_input_state_t s_ed;
static text_input_state_t *ed_load(void)
{
    memcpy(s_ed.buf, (const void *)(uintptr_t)0x5cd550u, sizeof s_ed.buf);
    s_ed.cursor = (int)G32(0x5bb4a8u); s_ed.length = (int)G32(0x58fe94u);
    s_ed.pixel_overflow = (int)G32(0x57d3b0u); s_ed.anchor_offset = (int)G32(0x58fd54u);
    return &s_ed;
}
static void ed_store(void)
{
    if (memcmp((void *)(uintptr_t)0x5cd550u, s_ed.buf, sizeof s_ed.buf)) memcpy((void *)(uintptr_t)0x5cd550u, s_ed.buf, sizeof s_ed.buf);
    PUT(0x5bb4a8u, s_ed.cursor); PUT(0x58fe94u, s_ed.length); PUT(0x57d3b0u, s_ed.pixel_overflow); PUT(0x58fd54u, s_ed.anchor_offset);
}
static void diplo_seal(void) { text_input_state_t *ed = ed_load(); EVQ(engine_diplomacy_action_seal(PARAM0, ed, q)); ed_store(); }
static void county_action(void) { EVQ(engine_county_action_open(PARAM0, q)); }

/* ---------------------------------------------------------------- seams → retail (unported callees) */
typedef int (__cdecl *r_wav_t)(const char *, int, int);
static void seam_wav(const char *n, int loop, int z)
{ rh_port_retail_enter(); ((r_wav_t)0x427990u)(n, loop, z); rh_port_retail_leave(); }   /* FUN_00427990 */

typedef int (__cdecl *r_i_t)(int);
typedef int (__cdecl *r_ii_t)(int, int);
typedef int (__cdecl *r_v_t)(void);
static void seam_trade_render(void) { rh_port_retail_enter(); ((r_v_t)0x416308u)(); rh_port_retail_leave(); }   /* FUN_00416308 */
static void seam_voice(int n) { rh_port_retail_enter(); ((r_i_t)0x4b3714u)(n); rh_port_retail_leave(); }   /* FUN_004b3714 */
/* the county-action strip's armory entry: engine_armory is not linked (no model yet) — retail's conscripts at the
 * slider % (FUN_00435ebc(county, DAT_0056d65c)), its muster pool (FUN_004aa90a(county, DAT_00543fd8)), march /
 * merc off (DAT_005679d0 / DAT_0055446c) */
static void seam_armory(int county)
{
    rh_port_retail_enter();
    ((r_ii_t)0x435ebcu)(county, (int)G32(0x56d65cu));
    ((r_ii_t)0x4aa90au)(county, (int)G32(0x543fd8u));
    PUT(0x5679d0u, 0); PUT(0x55446cu, 0);
    rh_port_retail_leave();
}

/* called once from rh_port_bind_once (rh_hooks_ai.c) */
void rh_economy_bind_seams(void)
{
    engine_trade_bind_wav_fn(seam_wav);
    engine_trade_bind_open_render_fn(seam_trade_render);
    engine_county_action_bind_wav_fn(seam_wav);
    engine_county_action_bind_voice_fn(seam_voice);
    engine_county_action_bind_armory_fn(seam_armory);
}

#define RH_TABLE RH_HOOKS_ECONOMY
extern hook_t RH_HOOKS_ECONOMY[];
/* row indices (must follow RH_HOOKS_ECONOMY's order) */
enum {
    X_trade_compute, X_trade_ale_delta, X_diplo_queue_action, X_ai_build_keep, X_ai_dispatch_caravan,
    X_ai_disband_into_realm,
    X_trade_arrow_up, X_trade_arrow_down, X_trade_buy_all, X_trade_sell_all, X_trade_seal_confirm, X_trade_seal_cancel,
    X_diplo_arrow, X_diplo_seal,
    X_county_action, X_ss_arrow_up, X_ss_arrow_down, X_ss_toggle_all, X_ss_target_select, X_ss_commit,
    X_merchant_zone_click, X_trade_open,
};

/* engine_trade_model.c — the open render's compute (reads the current good / county / player) and the arrows' tail */
HOOK_V(trade_compute,          0, engine_trade_compute_current())
HOOK_V(trade_ale_delta,        0, engine_trade_ale_delta_recompute())
/* engine_diplomacy.c — the gift / compliment confirm's queue (retail's 5th argument is a char) */
HOOK_V(diplo_queue_action,     5, engine_diplomacy_queue_action(I(0), I(1), I(2), I(3), (uint8_t)a[4]))
/* engine_army_ai.c — keep build (UI 0x436b59, MP 0x447d22, AI 0x49edc7), caravan, disband */
HOOK_V(ai_build_keep,          2, engine_army_ai_build_keep(I(0), I(1)))
HOOK_V(ai_dispatch_caravan,    3, engine_army_ai_dispatch_caravan(I(0), I(1), (uint8_t)a[2]))
HOOK_V(ai_disband_into_realm,  1, engine_army_ai_disband_into_realm(I(0)))
/* pointer-table entries (router table DAT_004dd838: no E8 call site, the entry is patched) — the trade grid's
 * arrows and seals (engine_trade_model.c) */
HOOK_V(trade_arrow_up,         0, engine_trade_arrow_up())
HOOK_V(trade_arrow_down,       0, engine_trade_arrow_down())
HOOK_V(trade_buy_all,          0, engine_trade_buy_all())
HOOK_V(trade_sell_all,         0, engine_trade_sell_all())
HOOK_V(trade_seal_confirm,     0, engine_trade_seal_confirm())
HOOK_V(trade_seal_cancel,      0, engine_trade_seal_cancel())
/* … the gift screen's amount arrow and the Yes / No seal of every mode (DAT_004dd9d0 / DAT_004dda30;
 * engine_diplomacy_action_model.c) */
HOOK_V(diplo_arrow,            0, engine_diplomacy_action_arrow(PARAM0))
HOOK_V(diplo_seal,             0, diplo_seal())
/* … the county-action tab strip (DAT_004dc688..: FUN_0043ae30) and the Send Supplies screen (DAT_004dd538 /
 * DAT_004dc7e8; engine_sendsupplies_model.c): the arrows / all-toggle read the good index from param0 */
HOOK_V(county_action,          0, county_action())
HOOK_V(ss_arrow_up,            0, engine_sendsupplies_arrow_up(PARAM0))
HOOK_V(ss_arrow_down,          0, engine_sendsupplies_arrow_down(PARAM0))
HOOK_V(ss_toggle_all,          0, engine_sendsupplies_toggle_all(PARAM0))
/* (E8 call sites) the thumbnail retarget (origin args; the cursor DAT_004e65fc / DAT_004e65f4, the press edge
 * DAT_004eafb4) and the commit (FUN_0043b04c, MP 0x447db6) */
HOOK_R(ss_target_select,       2, engine_sendsupplies_target_select(I(0), I(1), (int)G32(0x4e65fcu), (int)G32(0x4e65f4u),
                                                                     G8(0x4eafb4u)))
HOOK_V(ss_commit,              3, engine_sendsupplies_commit(I(0), I(1), (uint8_t)a[2]))
/* the merchant scene's rug (FUN_004ba9c8 → FUN_004357a6: the mercgrid payload DAT_00542cf8, the cursor, the press
 * edge) and the trade grid's open FUN_0043530e (its open render FUN_00416308 is retail's: the seam) */
HOOK_R(merchant_zone_click,    0, engine_merchant_zone_click((const uint8_t *)(uintptr_t)0x542cf8u, (int)G32(0x4e65fcu),
                                                             (int)G32(0x4e65f4u), G8(0x4eafb4u)))
HOOK_V(trade_open,             0, engine_trade_open())

hook_t RH_HOOKS_ECONOMY[] = {
    { "trade_compute", 0x428dafu, (void *)w_trade_compute, 0, 0, 0, 0, "" },
    { "trade_ale_delta", 0x435673u, (void *)w_trade_ale_delta, 0, 0, 0, 0, "" },
    { "diplo_queue_action", 0x4a2621u, (void *)w_diplo_queue_action, 0, 0, 0, 0, "" },
    { "ai_build_keep", 0x436d02u, (void *)w_ai_build_keep, 0, 0, 0, 0, "" },
    { "ai_dispatch_caravan", 0x4292afu, (void *)w_ai_dispatch_caravan, 0, 0, 0, 0, "" },
    { "ai_disband_into_realm", 0x438681u, (void *)w_ai_disband_into_realm, 0, 0, 0, 0, "" },
    { "trade_arrow_up", 0x435339u, (void *)w_trade_arrow_up, 0, 0, 0, 0, "" },
    { "trade_arrow_down", 0x43543du, (void *)w_trade_arrow_down, 0, 0, 0, 0, "" },
    { "trade_buy_all", 0x4355dbu, (void *)w_trade_buy_all, 0, 0, 0, 0, "" },
    { "trade_sell_all", 0x435541u, (void *)w_trade_sell_all, 0, 0, 0, 0, "" },
    { "trade_seal_confirm", 0x435286u, (void *)w_trade_seal_confirm, 0, 0, 0, 0, "" },
    { "trade_seal_cancel", 0x4352f2u, (void *)w_trade_seal_cancel, 0, 0, 0, 0, "" },
    { "diplo_arrow", 0x436372u, (void *)w_diplo_arrow, 0, 0, 0, 0, "" },
    { "diplo_seal", 0x436408u, (void *)w_diplo_seal, 0, 0, 0, 0, "" },
    { "county_action", 0x43ae30u, (void *)w_county_action, 0, 0, 0, 0, "" },
    { "ss_arrow_up", 0x43b27au, (void *)w_ss_arrow_up, 0, 0, 0, 0, "" },
    { "ss_arrow_down", 0x43b1cau, (void *)w_ss_arrow_down, 0, 0, 0, 0, "" },
    { "ss_toggle_all", 0x43b32au, (void *)w_ss_toggle_all, 0, 0, 0, 0, "" },
    { "ss_target_select", 0x43b412u, (void *)w_ss_target_select, 0, 0, 0, 0, "" },
    { "ss_commit", 0x43b145u, (void *)w_ss_commit, 0, 0, 0, 0, "" },
    { "merchant_zone_click", 0x4357a6u, (void *)w_merchant_zone_click, 0, 0, 0, 0, "" },
    { "trade_open", 0x43530eu, (void *)w_trade_open, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_ECONOMY_N = sizeof RH_HOOKS_ECONOMY / sizeof *RH_HOOKS_ECONOMY;
