/* rh_hooks_events.c — the season pass's realm events (src/engine_realm_events.c: the turn-start event roll
 * FUN_00448819, its 24 per-event handlers and two territory probes) and the season pass's realm passes no earlier
 * table hooked (ranking snapshot, upkeep, coalition, territory census / workforce) vs retail, on identical state
 * (rh_hooks.h contract; shared plumbing in rh_port.h / rh_hooks_ai.c).
 *
 * The roll reads the cached 7-bit RNG byte DAT_0058fd60 (the port takes it as an argument); the handlers commit or
 * cancel an event on g_per_army_768 / g_player_strategic_352 (aliased save blocks) and flip map cells through the
 * linked engine_territory.  engine_realm.c's coalition scratch (engine_realm_globals.c) is aliased below. */
#include "rh_port.h"
#include "rl_link.h"
#include "save_schema_globals.h"
#include "engine_realm_events.h"
#include "engine_realm.h"
#include "engine_realm_upkeep.h"
#include "engine_ranking.h"
#include "engine_territory.h"

/* ---------------------------------------------------------------- binding: realm runtime globals = retail memory */
/* src/engine_realm_globals.c is not linked: its coalition scratch is aliased at retail's VAs (quirk #234) */
#define A(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
__asm__(A(g_coalition_tbl,0x568240) A(g_coalition_active_count,0x553ef4));
#undef A

#define RH_TABLE RH_HOOKS_EVENTS
extern hook_t RH_HOOKS_EVENTS[];
/* row indices (must follow RH_HOOKS_EVENTS's order) */
enum {
    X_events_roll,
    X_ev_87, X_ev_88, X_ev_89, X_ev_8a, X_ev_8b, X_ev_8c, X_ev_8d, X_ev_8e,
    X_ev_12e, X_ev_12f, X_ev_130, X_ev_131, X_ev_132, X_ev_133, X_ev_134, X_ev_135,
    X_ev_136, X_ev_137, X_ev_138, X_ev_139, X_ev_13a, X_ev_13b, X_ev_13c, X_ev_13d,
    X_events_claim_empty, X_events_vacate_owned,
    X_rank_snapshot_metrics, X_realm_pay_upkeep, X_realm_coalition_pass, X_terr_census_all, X_terr_allocate_all,
};

/* engine_realm_events.c — the roll (retail reads the RNG byte from DAT_0058fd60) and its handlers (argument: realm) */
HOOK_V(events_roll,       0, rl_448819())
#define EV(id) HOOK_V(ev_##id, 1, engine_realm_event_apply(I(0), 0x##id))
EV(87) EV(88) EV(89) EV(8a) EV(8b) EV(8c) EV(8d) EV(8e)
EV(12e) EV(12f) EV(130) EV(131) EV(132) EV(133) EV(134) EV(135)
EV(136) EV(137) EV(138) EV(139) EV(13a) EV(13b) EV(13c) EV(13d)
#undef EV
HOOK_R(events_claim_empty,  1, engine_realm_events_claim_empty(I(0)))
HOOK_R(events_vacate_owned, 1, engine_realm_events_vacate_owned(I(0)))
/* the season pass's other realm passes (FUN_00448440's callees no earlier table hooked) */
HOOK_V(rank_snapshot_metrics, 0, engine_ranking_snapshot_player_metrics())
HOOK_V(realm_pay_upkeep,      0, rl_4acbd4())
HOOK_V(realm_coalition_pass,  0, rl_44ae3c())
HOOK_V(terr_census_all,       0, engine_territory_census_all_realms())
HOOK_V(terr_allocate_all,     0, engine_territory_allocate_all_workforce())

hook_t RH_HOOKS_EVENTS[] = {
    { "events_roll", 0x448819u, (void *)w_events_roll, 0, 0, 0, 0, "" },
    { "ev_87", 0x448e12u, (void *)w_ev_87, 0, 0, 0, 0, "" },
    { "ev_88", 0x448ecfu, (void *)w_ev_88, 0, 0, 0, 0, "" },
    { "ev_89", 0x448f1fu, (void *)w_ev_89, 0, 0, 0, 0, "" },
    { "ev_8a", 0x448f6fu, (void *)w_ev_8a, 0, 0, 0, 0, "" },
    { "ev_8b", 0x449093u, (void *)w_ev_8b, 0, 0, 0, 0, "" },
    { "ev_8c", 0x449150u, (void *)w_ev_8c, 0, 0, 0, 0, "" },
    { "ev_8d", 0x4491a0u, (void *)w_ev_8d, 0, 0, 0, 0, "" },
    { "ev_8e", 0x4491f0u, (void *)w_ev_8e, 0, 0, 0, 0, "" },
    { "ev_12e", 0x4492e7u, (void *)w_ev_12e, 0, 0, 0, 0, "" },
    { "ev_12f", 0x44934au, (void *)w_ev_12f, 0, 0, 0, 0, "" },
    { "ev_130", 0x44938cu, (void *)w_ev_130, 0, 0, 0, 0, "" },
    { "ev_131", 0x4493c8u, (void *)w_ev_131, 0, 0, 0, 0, "" },
    { "ev_132", 0x449400u, (void *)w_ev_132, 0, 0, 0, 0, "" },
    { "ev_133", 0x449438u, (void *)w_ev_133, 0, 0, 0, 0, "" },
    { "ev_134", 0x44949bu, (void *)w_ev_134, 0, 0, 0, 0, "" },
    { "ev_135", 0x4494feu, (void *)w_ev_135, 0, 0, 0, 0, "" },
    { "ev_136", 0x449533u, (void *)w_ev_136, 0, 0, 0, 0, "" },
    { "ev_137", 0x449575u, (void *)w_ev_137, 0, 0, 0, 0, "" },
    { "ev_138", 0x44960fu, (void *)w_ev_138, 0, 0, 0, 0, "" },
    { "ev_139", 0x449688u, (void *)w_ev_139, 0, 0, 0, 0, "" },
    { "ev_13a", 0x44970du, (void *)w_ev_13a, 0, 0, 0, 0, "" },
    { "ev_13b", 0x44975du, (void *)w_ev_13b, 0, 0, 0, 0, "" },
    { "ev_13c", 0x4497adu, (void *)w_ev_13c, 0, 0, 0, 0, "" },
    { "ev_13d", 0x449826u, (void *)w_ev_13d, 0, 0, 0, 0, "" },
    { "events_claim_empty", 0x44c77au, (void *)w_events_claim_empty, 0, 0, 0, 0, "" },
    { "events_vacate_owned", 0x44c80au, (void *)w_events_vacate_owned, 0, 0, 0, 0, "" },
    { "rank_snapshot_metrics", 0x4486a9u, (void *)w_rank_snapshot_metrics, 0, 0, 0, 0, "" },
    { "realm_pay_upkeep", 0x4acbd4u, (void *)w_realm_pay_upkeep, 0, 0, 0, 0, "" },
    { "realm_coalition_pass", 0x44ae3cu, (void *)w_realm_coalition_pass, 0, 0, 0, 0, "" },
    { "terr_census_all", 0x469b51u, (void *)w_terr_census_all, 0, 0, 0, 0, "" },
    { "terr_allocate_all", 0x44f699u, (void *)w_terr_allocate_all, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_EVENTS_N = sizeof RH_HOOKS_EVENTS / sizeof *RH_HOOKS_EVENTS;
