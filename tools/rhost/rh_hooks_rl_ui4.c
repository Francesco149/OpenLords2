/* rh_hooks_rl_ui4.c — a retail-layout port, partition 'ui4' (src/rl_ui4.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads, or modelled on every path: compared) + the table row. RH_SH_MEDIA on every row that draws;
 * RH_SH_SAMPLE on the hot leaves. GENERATED from the partition owner's row list (keep the format).
 *
 * No row (docs/findings/rl-ui4.md):
 *   rl_40e890 / rl_40e8bb  host-level: they run a frame (0x4b99c0), rhost's frame boundary (rhost.c frame_wrap
 *              advances the frame counter, the virtual clock and the input queue) — a shadow would run two frames.
 */
#include "rh_port.h"
#include "rl.h"
#include "rl_ui4_globals.h"
#include "rh_rl_ui4_alias.h"

__asm__(RH_RL_UI4_ALIASES);


#define RH_TABLE RH_HOOKS_RL_UI4
extern hook_t RH_HOOKS_RL_UI4[];
/* row indices (must follow the table's order) */
enum {
    X_rl_401000, X_rl_401085, X_rl_4010da, X_rl_401136, X_rl_4011b0, X_rl_4011ea, X_rl_401246, X_rl_4012a2,
    X_rl_401303, X_rl_401333, X_rl_4013bc, X_rl_401666, X_rl_4015b9, X_rl_4014f0, X_rl_401f8f, X_rl_4036f9,
    X_rl_40187a, X_rl_4017bf, X_rl_4018d7, X_rl_401984, X_rl_401d26, X_rl_401a20, X_rl_401dc8, X_rl_401c70,
    X_rl_401c93, X_rl_401ca3, X_rl_401cbc, X_rl_401cda, X_rl_401cfc, X_rl_401d11, X_rl_401f49, X_rl_402009,
    X_rl_4020ae, X_rl_40210c, X_rl_4037ee, X_rl_40489e, X_rl_40490d, X_rl_404b2c, X_rl_404d6b, X_rl_404d96,
    X_rl_404dc1, X_rl_404e03, X_rl_404e4b, X_rl_404ead, X_rl_404f4c, X_rl_408fcb, X_rl_40ccfa, X_rl_40da1e,
    X_rl_40e3ee, X_rl_40e5e7, X_rl_40e6f2, X_rl_40e7e4, X_rl_40ecc4, X_rl_40f1a0, X_rl_40f5fd, X_rl_429b1d,
    X_rl_429ba4, X_rl_42a15f, X_rl_4290d0, X_rl_429153, X_rl_42925e, X_rl_4291b3, X_rl_4298c1, X_rl_427ed0,
    X_rl_428471, X_rl_42847c,
};

HOOK_R(rl_401000, 3, rl_401000(I(0), I(1), I(2)))
HOOK_R(rl_401085, 3, rl_401085(I(0), I(1), I(2)))
HOOK_R(rl_4010da, 3, rl_4010da(I(0), I(1), I(2)))
HOOK_R(rl_401136, 3, rl_401136(I(0), I(1), I(2)))
HOOK_R(rl_4011b0, 1, rl_4011b0(I(0)))
HOOK_R(rl_4011ea, 1, rl_4011ea(I(0)))
HOOK_R(rl_401246, 1, rl_401246(I(0)))
HOOK_R(rl_4012a2, 1, rl_4012a2(I(0)))
HOOK_R(rl_401303, 2, rl_401303(I(0), I(1)))
HOOK_R(rl_401333, 2, rl_401333(I(0), I(1)))
HOOK_R(rl_4013bc, 1, rl_4013bc(I(0)))
HOOK_R(rl_401666, 1, rl_401666(I(0)))
HOOK_R(rl_4015b9, 2, rl_4015b9(I(0), I(1)))
HOOK_R(rl_4014f0, 2, rl_4014f0(I(0), I(1)))
HOOK_R(rl_401f8f, 1, rl_401f8f(I(0)))
HOOK_R(rl_4036f9, 2, rl_4036f9(I(0), I(1)))
HOOK_R(rl_40187a, 1, rl_40187a(I(0)))
HOOK_R(rl_4017bf, 4, rl_4017bf(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4018d7, 2, rl_4018d7(I(0), I(1)))
HOOK_R(rl_401984, 0, rl_401984())
HOOK_V(rl_401d26, 1, rl_401d26(I(0)))   /* EAX: AL residue, no caller reads it */
HOOK_R(rl_401a20, 1, rl_401a20(I(0)))
HOOK_V(rl_401dc8, 0, rl_401dc8())   /* EAX: the caller's when the editor is off */
HOOK_V(rl_401c70, 0, rl_401c70())
HOOK_V(rl_401c93, 0, rl_401c93())
HOOK_R(rl_401ca3, 0, rl_401ca3())
HOOK_V(rl_401cbc, 0, rl_401cbc())
HOOK_R(rl_401cda, 0, rl_401cda())
HOOK_V(rl_401cfc, 0, rl_401cfc())
HOOK_R(rl_401d11, 0, rl_401d11())
HOOK_R(rl_401f49, 0, rl_401f49())
HOOK_R(rl_402009, 4, rl_402009(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4020ae, 0, rl_4020ae())
HOOK_R(rl_40210c, 2, rl_40210c(I(0), I(1)))
HOOK_R(rl_4037ee, 4, rl_4037ee(I(0), I(1), I(2), I(3)))
HOOK_V(rl_40489e, 0, rl_40489e())
HOOK_V(rl_40490d, 0, rl_40490d())
HOOK_R(rl_404b2c, 0, rl_404b2c())
HOOK_R(rl_404d6b, 2, rl_404d6b(I(0), I(1)))
HOOK_R(rl_404d96, 2, rl_404d96(I(0), I(1)))
HOOK_R(rl_404dc1, 2, rl_404dc1(I(0), I(1)))
HOOK_R(rl_404e03, 2, rl_404e03(I(0), I(1)))
HOOK_R(rl_404e4b, 4, rl_404e4b(I(0), I(1), I(2), I(3)))
HOOK_R(rl_404ead, 4, rl_404ead(I(0), I(1), I(2), I(3)))
HOOK_R(rl_404f4c, 4, rl_404f4c(I(0), I(1), I(2), I(3)))
HOOK_V(rl_408fcb, 2, rl_408fcb(I(0), I(1)))
HOOK_V(rl_40ccfa, 0, rl_40ccfa())
HOOK_R(rl_40da1e, 4, rl_40da1e(I(0), I(1), I(2), I(3)))
HOOK_R(rl_40e3ee, 4, rl_40e3ee(I(0), I(1), I(2), I(3)))
HOOK_R(rl_40e5e7, 4, rl_40e5e7(I(0), I(1), I(2), I(3)))
HOOK_V(rl_40e6f2, 4, rl_40e6f2(I(0), I(1), I(2), I(3)))   /* EAX: 0x4b36c0's (HOOK_V in rl_io) */
HOOK_R(rl_40e7e4, 0, rl_40e7e4())
HOOK_V(rl_40ecc4, 0, rl_40ecc4())
HOOK_V(rl_40f1a0, 1, rl_40f1a0(I(0)))
HOOK_V(rl_40f5fd, 1, rl_40f5fd(I(0)))
HOOK_R(rl_429b1d, 0, rl_429b1d())
HOOK_R(rl_429ba4, 1, rl_429ba4(I(0)))
HOOK_V(rl_42a15f, 0, rl_42a15f())   /* EAX: the last bound tested */
HOOK_R(rl_4290d0, 0, rl_4290d0())
HOOK_R(rl_429153, 2, rl_429153(I(0), I(1)))
HOOK_R(rl_42925e, 1, rl_42925e(I(0)))
HOOK_V(rl_4291b3, 0, rl_4291b3())
HOOK_R(rl_4298c1, 1, rl_4298c1(I(0)))
HOOK_V(rl_427ed0, 0, rl_427ed0())
HOOK_V(rl_428471, 0, rl_428471())
HOOK_R(rl_42847c, 0, rl_42847c())

hook_t RH_HOOKS_RL_UI4[] = {
    { "rl_401000", 0x401000u, (void *)w_rl_401000, 0, 0, 0, 0, "" },
    { "rl_401085", 0x401085u, (void *)w_rl_401085, 0, 0, 0, 0, "" },
    { "rl_4010da", 0x4010dau, (void *)w_rl_4010da, 0, 0, 0, 0, "" },
    { "rl_401136", 0x401136u, (void *)w_rl_401136, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_4011b0", 0x4011b0u, (void *)w_rl_4011b0, 0, 0, 0, 0, "" },
    { "rl_4011ea", 0x4011eau, (void *)w_rl_4011ea, 0, 0, 0, 0, "" },
    { "rl_401246", 0x401246u, (void *)w_rl_401246, 0, 0, 0, 0, "" },
    { "rl_4012a2", 0x4012a2u, (void *)w_rl_4012a2, 0, 0, 0, 0, "" },
    { "rl_401303", 0x401303u, (void *)w_rl_401303, 0, 0, 0, 0, "" },
    { "rl_401333", 0x401333u, (void *)w_rl_401333, 0, 0, 0, 0, "" },
    { "rl_4013bc", 0x4013bcu, (void *)w_rl_4013bc, 0, 0, 0, 0, "" },
    { "rl_401666", 0x401666u, (void *)w_rl_401666, 0, 0, 0, 0, "" },
    { "rl_4015b9", 0x4015b9u, (void *)w_rl_4015b9, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_4014f0", 0x4014f0u, (void *)w_rl_4014f0, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_401f8f", 0x401f8fu, (void *)w_rl_401f8f, 0, 0, 0, 0, "" },
    { "rl_4036f9", 0x4036f9u, (void *)w_rl_4036f9, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_40187a", 0x40187au, (void *)w_rl_40187a, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_4017bf", 0x4017bfu, (void *)w_rl_4017bf, 0, 0, 0, 0, "" },
    { "rl_4018d7", 0x4018d7u, (void *)w_rl_4018d7, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_401984", 0x401984u, (void *)w_rl_401984, 0, 0, 0, 0, "" },
    { "rl_401d26", 0x401d26u, (void *)w_rl_401d26, 0, 0, 0, 0, "" },
    { "rl_401a20", 0x401a20u, (void *)w_rl_401a20, 0, 0, 0, 0, "" },
    { "rl_401dc8", 0x401dc8u, (void *)w_rl_401dc8, 0, 0, 0, 0, "" },
    { "rl_401c70", 0x401c70u, (void *)w_rl_401c70, 0, 0, 0, 0, "" },
    { "rl_401c93", 0x401c93u, (void *)w_rl_401c93, 0, 0, 0, 0, "" },
    { "rl_401ca3", 0x401ca3u, (void *)w_rl_401ca3, 0, 0, 0, 0, "" },
    { "rl_401cbc", 0x401cbcu, (void *)w_rl_401cbc, 0, 0, 0, 0, "" },
    { "rl_401cda", 0x401cdau, (void *)w_rl_401cda, 0, 0, 0, 0, "" },
    { "rl_401cfc", 0x401cfcu, (void *)w_rl_401cfc, 0, 0, 0, 0, "" },
    { "rl_401d11", 0x401d11u, (void *)w_rl_401d11, 0, 0, 0, 0, "" },
    { "rl_401f49", 0x401f49u, (void *)w_rl_401f49, 0, 0, 0, 0, "" },
    { "rl_402009", 0x402009u, (void *)w_rl_402009, 0, 0, 0, 0, "" },
    { "rl_4020ae", 0x4020aeu, (void *)w_rl_4020ae, 0, 0, 0, 0, "" },
    { "rl_40210c", 0x40210cu, (void *)w_rl_40210c, 0, 0, 0, 0, "" },
    { "rl_4037ee", 0x4037eeu, (void *)w_rl_4037ee, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_40489e", 0x40489eu, (void *)w_rl_40489e, 0, 0, 0, 0, "" },
    { "rl_40490d", 0x40490du, (void *)w_rl_40490d, 0, 0, 0, 0, "" },
    { "rl_404b2c", 0x404b2cu, (void *)w_rl_404b2c, 0, 0, 0, 0, "" },
    { "rl_404d6b", 0x404d6bu, (void *)w_rl_404d6b, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_404d96", 0x404d96u, (void *)w_rl_404d96, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_404dc1", 0x404dc1u, (void *)w_rl_404dc1, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_404e03", 0x404e03u, (void *)w_rl_404e03, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_404e4b", 0x404e4bu, (void *)w_rl_404e4b, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_404ead", 0x404eadu, (void *)w_rl_404ead, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_404f4c", 0x404f4cu, (void *)w_rl_404f4c, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_408fcb", 0x408fcbu, (void *)w_rl_408fcb, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40ccfa", 0x40ccfau, (void *)w_rl_40ccfa, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40da1e", 0x40da1eu, (void *)w_rl_40da1e, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40e3ee", 0x40e3eeu, (void *)w_rl_40e3ee, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40e5e7", 0x40e5e7u, (void *)w_rl_40e5e7, 0, 0, 0, 0, "" },
    { "rl_40e6f2", 0x40e6f2u, (void *)w_rl_40e6f2, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40e7e4", 0x40e7e4u, (void *)w_rl_40e7e4, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40ecc4", 0x40ecc4u, (void *)w_rl_40ecc4, 0, 0, 0, 0, "" },
    { "rl_40f1a0", 0x40f1a0u, (void *)w_rl_40f1a0, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40f5fd", 0x40f5fdu, (void *)w_rl_40f5fd, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_429b1d", 0x429b1du, (void *)w_rl_429b1d, 0, 0, 0, 0, "" },
    { "rl_429ba4", 0x429ba4u, (void *)w_rl_429ba4, 0, 0, 0, 0, "" },
    { "rl_42a15f", 0x42a15fu, (void *)w_rl_42a15f, 0, 0, 0, 0, "" },
    { "rl_4290d0", 0x4290d0u, (void *)w_rl_4290d0, 0, 0, 0, 0, "" },
    { "rl_429153", 0x429153u, (void *)w_rl_429153, 0, 0, 0, 0, "" },
    { "rl_42925e", 0x42925eu, (void *)w_rl_42925e, 0, 0, 0, 0, "" },
    { "rl_4291b3", 0x4291b3u, (void *)w_rl_4291b3, 0, 0, 0, 0, "" },
    { "rl_4298c1", 0x4298c1u, (void *)w_rl_4298c1, 0, 0, 0, 0, "" },
    { "rl_427ed0", 0x427ed0u, (void *)w_rl_427ed0, 0, 0, 0, 0, "" },
    { "rl_428471", 0x428471u, (void *)w_rl_428471, 0, 0, 0, 0, "" },
    { "rl_42847c", 0x42847cu, (void *)w_rl_42847c, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_UI4_N = sizeof RH_HOOKS_RL_UI4 / sizeof *RH_HOOKS_RL_UI4;
