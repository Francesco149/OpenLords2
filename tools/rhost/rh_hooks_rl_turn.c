/* rh_hooks_rl_turn.c — a retail-layout port, partition 'turn' (src/rl_turn.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads: compared). Rows that draw (or call a drawer with computed arguments) carry RH_SH_MEDIA; the per-frame
 * rows 0x47685d / 0x476aa7 / 0x49a010 (and 0x476da9) draw nothing themselves (retail drawers with state-compared inputs;
 * the hybrid pass checks every presented frame) and go without it. Findings: docs/findings/rl-turn.md.
 *
 * Route (a) rows run a linked old port (engine_army_ai.c, engine_diplomacy.c, engine_roster.c, engine_camera.c,
 * engine_combat.c) through its rl_<va> wrapper; their mirrors are synced by rh_port_run (rh_hooks_ai.c). */
#include "rh_port.h"
#include "rl.h"
#include "rl_turn_globals.h"
#include "rh_rl_turn_alias.h"

/* the partition's globals (src/rl_turn_globals.h; rl_turn_globals.c is not linked) at their retail VAs */
__asm__(RH_RL_TURN_ALIASES);

#define RH_TABLE RH_HOOKS_RL_TURN
extern hook_t RH_HOOKS_RL_TURN[];
/* row indices (must follow the table's order) */
enum {
    X_rl_47ed34, X_rl_49bae9, X_rl_49bbe8, X_rl_49bc1d, X_rl_49bc5f, X_rl_49ca32, X_rl_4a1e6c,
    X_rl_4a1241, X_rl_4a1774, X_rl_4a1847, X_rl_4a1a54, X_rl_4a24d1,
    X_rl_49f12f, X_rl_49f431, X_rl_49f73f, X_rl_49f850, X_rl_49fcc5, X_rl_49fda5, X_rl_4a01ea, X_rl_4a0649,
    X_rl_4a3c67, X_rl_4a4052, X_rl_4a440f, X_rl_4a4a3f, X_rl_4a545f,
    X_rl_476e21, X_rl_476768, X_rl_476710, X_rl_47685d, X_rl_476a5d, X_rl_476da9, X_rl_4a2584, X_rl_4a1c53, X_rl_49b35e, X_rl_49b6d3, X_rl_49ce51, X_rl_49b762, X_rl_4a3475, X_rl_499dc3, X_rl_499e5d, X_rl_498270,
    X_rl_4984dc,
    X_rl_499859,
    X_rl_499034,
    X_rl_499a1c,
    X_rl_4997bf,
    X_rl_49a453,
    X_rl_497e65,
    X_rl_497ced,
    X_rl_4976a1,
    X_rl_49caaa,
    X_rl_49a010,
    X_rl_476aa7,
    X_rl_4a277d,
    X_rl_4a29b2,
    X_rl_4a2cd6,
    X_rl_4a2f61,
    X_rl_4a30f6,
    X_rl_4a38db,
    X_rl_49aa0e,
    X_rl_49bd99,
    X_rl_47309e,
};

HOOK_V(rl_47ed34, 0, rl_47ed34())
HOOK_V(rl_49bae9, 3, rl_49bae9(I(0), I(1), I(2)))
HOOK_V(rl_49bbe8, 2, rl_49bbe8(I(0), I(1)))
HOOK_V(rl_49bc1d, 0, rl_49bc1d())
HOOK_V(rl_49bc5f, 0, rl_49bc5f())
HOOK_V(rl_49ca32, 0, rl_49ca32())
HOOK_V(rl_4a1e6c, 0, rl_4a1e6c())
HOOK_R(rl_4a1241, 1, rl_4a1241(I(0)))
HOOK_V(rl_4a1774, 2, rl_4a1774(I(0), I(1)))
HOOK_V(rl_4a1847, 0, rl_4a1847())
HOOK_V(rl_4a1a54, 2, rl_4a1a54(I(0), I(1)))
HOOK_V(rl_4a24d1, 2, rl_4a24d1(I(0), I(1)))
HOOK_V(rl_49f12f, 1, rl_49f12f(I(0)))
HOOK_V(rl_49f431, 1, rl_49f431(I(0)))
HOOK_R(rl_49f73f, 2, rl_49f73f(I(0), I(1)))
HOOK_R(rl_49f850, 2, rl_49f850(I(0), I(1)))
HOOK_R(rl_49fcc5, 1, rl_49fcc5(I(0)))
HOOK_V(rl_49fda5, 2, rl_49fda5(I(0), I(1)))
HOOK_V(rl_4a01ea, 1, rl_4a01ea(I(0)))
HOOK_R(rl_4a0649, 2, rl_4a0649(I(0), I(1)))
HOOK_V(rl_4a3c67, 1, rl_4a3c67(I(0)))
HOOK_V(rl_4a4052, 1, rl_4a4052(I(0)))
HOOK_V(rl_4a440f, 1, rl_4a440f(I(0)))
HOOK_V(rl_4a4a3f, 3, rl_4a4a3f(I(0), I(1), I(2)))
HOOK_V(rl_4a545f, 2, rl_4a545f(I(0), I(1)))
HOOK_V(rl_476e21, 0, rl_476e21())
HOOK_V(rl_476768, 0, rl_476768())
HOOK_V(rl_476710, 0, rl_476710())
HOOK_R(rl_47685d, 0, rl_47685d())
HOOK_V(rl_476a5d, 0, rl_476a5d())
HOOK_V(rl_476da9, 1, rl_476da9(I(0)))
HOOK_V(rl_4a2584, 0, rl_4a2584())
HOOK_V(rl_4a1c53, 0, rl_4a1c53())
HOOK_V(rl_49b35e, 0, rl_49b35e())
HOOK_V(rl_49b6d3, 0, rl_49b6d3())
HOOK_V(rl_49ce51, 0, rl_49ce51())
HOOK_R(rl_49b762, 0, rl_49b762())
HOOK_V(rl_4a3475, 2, rl_4a3475(I(0), I(1)))
HOOK_V(rl_499dc3, 0, rl_499dc3())
HOOK_V(rl_499e5d, 0, rl_499e5d())
HOOK_V(rl_498270, 0, rl_498270())
HOOK_R(rl_4984dc, 0, rl_4984dc())
HOOK_R(rl_499859, 0, rl_499859())
HOOK_R(rl_499034, 0, rl_499034())
HOOK_R(rl_499a1c, 1, rl_499a1c(I(0)))
HOOK_R(rl_4997bf, 0, rl_4997bf())
HOOK_V(rl_49a453, 0, rl_49a453())
HOOK_V(rl_497e65, 0, rl_497e65())
HOOK_V(rl_497ced, 0, rl_497ced())
HOOK_V(rl_4976a1, 0, rl_4976a1())
HOOK_R(rl_49caaa, 0, rl_49caaa())
/* 0x49a010's frames leave the driver's residue window (the driver 0x4650b0 runs next at the same esp, quirk #238):
 * replace mode writes the model (rh_res_set: not compared, retail's imprint varies with the path) */
#include "engine_army_state.h"
static void ai_step_res(void)
{
    rl_49a010();
    engine_army_state_res_ai_step();
    for (int k = 0; k < 5; k++) rh_res_set(0x30 + 4 * k, g_army_state_res[k]);
}
HOOK_V(rl_49a010, 0, ai_step_res())
HOOK_V(rl_476aa7, 0, rl_476aa7())
HOOK_V(rl_4a277d, 1, rl_4a277d(I(0)))
HOOK_V(rl_4a29b2, 3, rl_4a29b2(I(0), I(1), I(2)))
HOOK_V(rl_4a2cd6, 2, rl_4a2cd6(I(0), I(1)))
HOOK_V(rl_4a2f61, 2, rl_4a2f61(I(0), I(1)))
HOOK_V(rl_4a30f6, 2, rl_4a30f6(I(0), I(1)))
HOOK_V(rl_4a38db, 3, rl_4a38db(I(0), I(1), I(2)))
HOOK_V(rl_49aa0e, 0, rl_49aa0e())
HOOK_V(rl_49bd99, 0, rl_49bd99())
HOOK_V(rl_47309e, 0, rl_47309e())

hook_t RH_HOOKS_RL_TURN[] = {
    { "rl_47ed34", 0x47ed34u, (void *)w_rl_47ed34, 0, 0, 0, 0, "" },
    { "rl_49bae9", 0x49bae9u, (void *)w_rl_49bae9, 0, 0, 0, 0, "" },
    { "rl_49bbe8", 0x49bbe8u, (void *)w_rl_49bbe8, 0, 0, 0, 0, "" },
    { "rl_49bc1d", 0x49bc1du, (void *)w_rl_49bc1d, 0, 0, 0, 0, "" },
    { "rl_49bc5f", 0x49bc5fu, (void *)w_rl_49bc5f, 0, 0, 0, 0, "" },
    { "rl_49ca32", 0x49ca32u, (void *)w_rl_49ca32, 0, 0, 0, 0, "" },
    { "rl_4a1e6c", 0x4a1e6cu, (void *)w_rl_4a1e6c, 0, 0, 0, 0, "" },
    { "rl_4a1241", 0x4a1241u, (void *)w_rl_4a1241, 0, 0, 0, 0, "" },
    { "rl_4a1774", 0x4a1774u, (void *)w_rl_4a1774, 0, 0, 0, 0, "" },
    { "rl_4a1847", 0x4a1847u, (void *)w_rl_4a1847, 0, 0, 0, 0, "" },
    { "rl_4a1a54", 0x4a1a54u, (void *)w_rl_4a1a54, 0, 0, 0, 0, "" },
    { "rl_4a24d1", 0x4a24d1u, (void *)w_rl_4a24d1, 0, 0, 0, 0, "" },
    { "rl_49f12f", 0x49f12fu, (void *)w_rl_49f12f, 0, 0, 0, 0, "" },
    { "rl_49f431", 0x49f431u, (void *)w_rl_49f431, 0, 0, 0, 0, "" },
    { "rl_49f73f", 0x49f73fu, (void *)w_rl_49f73f, 0, 0, 0, 0, "" },
    { "rl_49f850", 0x49f850u, (void *)w_rl_49f850, 0, 0, 0, 0, "" },
    { "rl_49fcc5", 0x49fcc5u, (void *)w_rl_49fcc5, 0, 0, 0, 0, "" },
    { "rl_49fda5", 0x49fda5u, (void *)w_rl_49fda5, 0, 0, 0, 0, "" },
    { "rl_4a01ea", 0x4a01eau, (void *)w_rl_4a01ea, 0, 0, 0, 0, "" },
    { "rl_4a0649", 0x4a0649u, (void *)w_rl_4a0649, 0, 0, 0, 0, "" },
    { "rl_4a3c67", 0x4a3c67u, (void *)w_rl_4a3c67, 0, 0, 0, 0, "" },
    { "rl_4a4052", 0x4a4052u, (void *)w_rl_4a4052, 0, 0, 0, 0, "" },
    { "rl_4a440f", 0x4a440fu, (void *)w_rl_4a440f, 0, 0, 0, 0, "" },
    { "rl_4a4a3f", 0x4a4a3fu, (void *)w_rl_4a4a3f, 0, 0, 0, 0, "" },
    { "rl_4a545f", 0x4a545fu, (void *)w_rl_4a545f, 0, 0, 0, 0, "" },
    { "rl_476e21", 0x476e21u, (void *)w_rl_476e21, 0, 0, 0, 0, "" },
    { "rl_476768", 0x476768u, (void *)w_rl_476768, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_476710", 0x476710u, (void *)w_rl_476710, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_47685d", 0x47685du, (void *)w_rl_47685d, 0, 0, 0, 0, "" },
    { "rl_476a5d", 0x476a5du, (void *)w_rl_476a5d, 0, 0, 0, 0, "" },
    { "rl_476da9", 0x476da9u, (void *)w_rl_476da9, 0, 0, 0, 0, "" },
    { "rl_4a2584", 0x4a2584u, (void *)w_rl_4a2584, 0, 0, 0, 0, "" },
    { "rl_4a1c53", 0x4a1c53u, (void *)w_rl_4a1c53, 0, 0, 0, 0, "" },
    { "rl_49b35e", 0x49b35eu, (void *)w_rl_49b35e, 0, 0, 0, 0, "" },
    { "rl_49b6d3", 0x49b6d3u, (void *)w_rl_49b6d3, 0, 0, 0, 0, "" },
    { "rl_49ce51", 0x49ce51u, (void *)w_rl_49ce51, 0, 0, 0, 0, "" },
    { "rl_49b762", 0x49b762u, (void *)w_rl_49b762, 0, 0, 0, 0, "" },
    { "rl_4a3475", 0x4a3475u, (void *)w_rl_4a3475, 0, 0, 0, 0, "" },
    { "rl_499dc3", 0x499dc3u, (void *)w_rl_499dc3, 0, 0, 0, 0, "" },
    { "rl_499e5d", 0x499e5du, (void *)w_rl_499e5d, 0, 0, 0, 0, "" },
    { "rl_498270", 0x498270u, (void *)w_rl_498270, 0, 0, 0, 0, "" },
    { "rl_4984dc", 0x4984dcu, (void *)w_rl_4984dc, 0, 0, 0, 0, "" },
    { "rl_499859", 0x499859u, (void *)w_rl_499859, 0, 0, 0, 0, "" },
    { "rl_499034", 0x499034u, (void *)w_rl_499034, 0, 0, 0, 0, "" },
    { "rl_499a1c", 0x499a1cu, (void *)w_rl_499a1c, 0, 0, 0, 0, "" },
    { "rl_4997bf", 0x4997bfu, (void *)w_rl_4997bf, 0, 0, 0, 0, "" },
    { "rl_49a453", 0x49a453u, (void *)w_rl_49a453, 0, 0, 0, 0, "" },
    { "rl_497e65", 0x497e65u, (void *)w_rl_497e65, 0, 0, 0, 0, "" },
    { "rl_497ced", 0x497cedu, (void *)w_rl_497ced, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4976a1", 0x4976a1u, (void *)w_rl_4976a1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_49caaa", 0x49caaau, (void *)w_rl_49caaa, 0, 0, 0, 0, "" },
    { "rl_49a010", 0x49a010u, (void *)w_rl_49a010, 0, 0, 0, 0, "" },
    { "rl_476aa7", 0x476aa7u, (void *)w_rl_476aa7, 0, 0, 0, 0, "" },
    { "rl_4a277d", 0x4a277du, (void *)w_rl_4a277d, 0, 0, 0, 0, "" },
    { "rl_4a29b2", 0x4a29b2u, (void *)w_rl_4a29b2, 0, 0, 0, 0, "" },
    { "rl_4a2cd6", 0x4a2cd6u, (void *)w_rl_4a2cd6, 0, 0, 0, 0, "" },
    { "rl_4a2f61", 0x4a2f61u, (void *)w_rl_4a2f61, 0, 0, 0, 0, "" },
    { "rl_4a30f6", 0x4a30f6u, (void *)w_rl_4a30f6, 0, 0, 0, 0, "" },
    { "rl_4a38db", 0x4a38dbu, (void *)w_rl_4a38db, 0, 0, 0, 0, "" },
    { "rl_49aa0e", 0x49aa0eu, (void *)w_rl_49aa0e, 0, 0, 0, 0, "" },
    { "rl_49bd99", 0x49bd99u, (void *)w_rl_49bd99, 0, 0, 0, 0, "" },
    { "rl_47309e", 0x47309eu, (void *)w_rl_47309e, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_RL_TURN_N = sizeof RH_HOOKS_RL_TURN / sizeof *RH_HOOKS_RL_TURN;
