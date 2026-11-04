/* rh_hooks_rl_skirmish.c — a retail-layout port, partition 'skirmish' (src/rl_skirmish.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Findings: docs/findings/rl-skirmish.md.
 *
 * BINDING.  The partition's globals (src/rl_skirmish_globals.h, generated + one hand entry; rl_skirmish_globals.c is
 * not linked) are absolute symbols at their retail VAs (rh_rl_skirmish_alias.h). The gfx layer's port-side bindings
 * (fonts, the sprite renderers' log) are set by rh_hooks_rl_render.c's constructor (always linked).
 *
 * MEDIA.  Every row that draws (or calls something that does) carries RH_SH_MEDIA: its shadow compares the pixels
 * of every live surface too (docs/findings/render-under-rhost.md §3). EAX: only 0x42c4e2's is read (the custom
 * armies page tests it); every other caller drops it (HOOK_V). */
#include "rh_port.h"
#include "rl.h"
#include "rl_skirmish_globals.h"
#include "rh_rl_skirmish_alias.h"

__asm__(RH_RL_SKIRMISH_ALIASES);

#define RH_TABLE RH_HOOKS_RL_SKIRMISH
extern hook_t RH_HOOKS_RL_SKIRMISH[];
/* row indices (must follow the table's order) */
enum {
    X_rl_420147, X_rl_42051c, X_rl_4207c3, X_rl_4209c1, X_rl_420d40, X_rl_420de4, X_rl_421005, X_rl_421231,
    X_rl_42130f, X_rl_42150b, X_rl_421707, X_rl_421d09, X_rl_425314, X_rl_425487,
    X_rl_42aa90, X_rl_42ab15, X_rl_42ab82, X_rl_42ac0c, X_rl_42b780, X_rl_42b7f7, X_rl_42b919, X_rl_42b9c4,
    X_rl_42ba40, X_rl_42bf46, X_rl_42c476, X_rl_42c4e2, X_rl_42c5ad, X_rl_42c5fe, X_rl_42c64f, X_rl_42cc4a,
    X_rl_42d216, X_rl_42d2dc, X_rl_42d389, X_rl_42d4aa, X_rl_42d857,
};

/* the setup pages and the ratings screens (draw) */
HOOK_V(rl_420147, 1, rl_420147(I(0)))
HOOK_V(rl_42051c, 1, rl_42051c(I(0)))
HOOK_V(rl_4207c3, 0, rl_4207c3())
HOOK_V(rl_4209c1, 0, rl_4209c1())
HOOK_V(rl_420d40, 0, rl_420d40())
HOOK_V(rl_420de4, 0, rl_420de4())
HOOK_V(rl_421005, 0, rl_421005())
HOOK_V(rl_421231, 0, rl_421231())
HOOK_V(rl_42130f, 2, rl_42130f(I(0), I(1)))
HOOK_V(rl_42150b, 0, rl_42150b())
HOOK_V(rl_421707, 0, rl_421707())
HOOK_V(rl_421d09, 0, rl_421d09())
HOOK_V(rl_425314, 0, rl_425314())
HOOK_V(rl_425487, 0, rl_425487())
/* the battle list, the troop table, the sides, the ratings, the files */
HOOK_V(rl_42aa90, 0, rl_42aa90())
HOOK_V(rl_42ab15, 1, rl_42ab15(I(0)))
HOOK_V(rl_42ab82, 2, rl_42ab82(I(0), I(1)))
HOOK_V(rl_42ac0c, 0, rl_42ac0c())
HOOK_V(rl_42b780, 0, rl_42b780())
HOOK_V(rl_42b7f7, 0, rl_42b7f7())
HOOK_V(rl_42b919, 0, rl_42b919())
HOOK_V(rl_42b9c4, 0, rl_42b9c4())
HOOK_V(rl_42ba40, 0, rl_42ba40())
HOOK_V(rl_42bf46, 0, rl_42bf46())
HOOK_V(rl_42c476, 0, rl_42c476())
HOOK_R(rl_42c4e2, 1, rl_42c4e2(I(0)))
HOOK_V(rl_42c5ad, 0, rl_42c5ad())
HOOK_V(rl_42c5fe, 0, rl_42c5fe())
HOOK_V(rl_42c64f, 0, rl_42c64f())
HOOK_V(rl_42cc4a, 0, rl_42cc4a())
HOOK_V(rl_42d216, 0, rl_42d216())
HOOK_V(rl_42d2dc, 0, rl_42d2dc())
HOOK_V(rl_42d389, 0, rl_42d389())
HOOK_V(rl_42d4aa, 0, rl_42d4aa())
HOOK_V(rl_42d857, 0, rl_42d857())

/* rows spelled out: rhost.py reads the table literally ({ "name", 0xVAu, …) */
hook_t RH_HOOKS_RL_SKIRMISH[] = {
    { "rl_420147", 0x420147u, (void *)w_rl_420147, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42051c", 0x42051cu, (void *)w_rl_42051c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4207c3", 0x4207c3u, (void *)w_rl_4207c3, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4209c1", 0x4209c1u, (void *)w_rl_4209c1, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_420d40", 0x420d40u, (void *)w_rl_420d40, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_420de4", 0x420de4u, (void *)w_rl_420de4, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_421005", 0x421005u, (void *)w_rl_421005, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_421231", 0x421231u, (void *)w_rl_421231, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42130f", 0x42130fu, (void *)w_rl_42130f, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42150b", 0x42150bu, (void *)w_rl_42150b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_421707", 0x421707u, (void *)w_rl_421707, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_421d09", 0x421d09u, (void *)w_rl_421d09, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_425314", 0x425314u, (void *)w_rl_425314, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_425487", 0x425487u, (void *)w_rl_425487, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42aa90", 0x42aa90u, (void *)w_rl_42aa90, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42ab15", 0x42ab15u, (void *)w_rl_42ab15, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42ab82", 0x42ab82u, (void *)w_rl_42ab82, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42ac0c", 0x42ac0cu, (void *)w_rl_42ac0c, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42b780", 0x42b780u, (void *)w_rl_42b780, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42b7f7", 0x42b7f7u, (void *)w_rl_42b7f7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42b919", 0x42b919u, (void *)w_rl_42b919, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42b9c4", 0x42b9c4u, (void *)w_rl_42b9c4, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42ba40", 0x42ba40u, (void *)w_rl_42ba40, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42bf46", 0x42bf46u, (void *)w_rl_42bf46, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42c476", 0x42c476u, (void *)w_rl_42c476, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42c4e2", 0x42c4e2u, (void *)w_rl_42c4e2, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42c5ad", 0x42c5adu, (void *)w_rl_42c5ad, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42c5fe", 0x42c5feu, (void *)w_rl_42c5fe, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42c64f", 0x42c64fu, (void *)w_rl_42c64f, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42cc4a", 0x42cc4au, (void *)w_rl_42cc4a, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42d216", 0x42d216u, (void *)w_rl_42d216, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42d2dc", 0x42d2dcu, (void *)w_rl_42d2dc, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42d389", 0x42d389u, (void *)w_rl_42d389, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42d4aa", 0x42d4aau, (void *)w_rl_42d4aa, 0, 0, 0, 0, "", 0, 0 },
    { "rl_42d857", 0x42d857u, (void *)w_rl_42d857, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_RL_SKIRMISH_N = (int)(sizeof RH_HOOKS_RL_SKIRMISH / sizeof *RH_HOOKS_RL_SKIRMISH);
