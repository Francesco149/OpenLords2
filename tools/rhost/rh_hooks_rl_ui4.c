/* rh_hooks_rl_ui4.c — a retail-layout port, partition 'ui4' (src/rl_ui4.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads, or modelled on every path: compared) + the table row. RH_SH_MEDIA on every row that draws;
 * RH_SH_SAMPLE on the hot leaves. GENERATED from the partition owner's row list (keep the format).
 *
 * No row (docs/findings/rl-ui4.md):
 *   rl_40e890 / rl_40e8bb  host-level: they run a frame (0x4b99c0), rhost's frame boundary (rhost.c frame_wrap
 *              advances the frame counter, the virtual clock and the input queue) — a shadow would run two frames.
 *   rl_42e5c0 / rl_42ec91 / rl_42f1df  host-level: they create the DirectDraw object / the surfaces + clipper / a
 *              palette (rhost's DirectDraw objects are not retail memory: the shadow cannot roll them back), and 0x42e5c0
 *              styles the window and pumps the message queue.
 */
#include "rh_port.h"
#include "rl.h"
#include "rl_ui4_globals.h"
#include "rh_rl_ui4_alias.h"

__asm__(RH_RL_UI4_ALIASES);


#define RH_TABLE RH_HOOKS_RL_UI4
extern hook_t RH_HOOKS_RL_UI4[];
#include "rh_hook_smk.h"   /* HOOK_SMK: rows whose call opens a movie (the title menu click → lom.smk) */
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
    X_rl_428471, X_rl_42847c, X_rl_425ded, X_rl_425b10, X_rl_426d11, X_rl_427d19, X_rl_42f306, X_rl_42eb7c,
    X_rl_419c78, X_rl_412b33, X_rl_412e6b, X_rl_413155, X_rl_413590, X_rl_413b30, X_rl_4140f3, X_rl_414220,
    X_rl_41593b, X_rl_415bdc, X_rl_415e42, X_rl_415fb7, X_rl_41789b, X_rl_417960, X_rl_4180f6, X_rl_4184c6,
    X_rl_418e2d, X_rl_410a5d, X_rl_41ad5d, X_rl_41aea2, X_rl_41e61d, X_rl_41fdd6,
    X_rl_40902b, X_rl_41e5d0, X_rl_420428, X_rl_4247f5, X_rl_42563c, X_rl_425a6a, X_rl_427d72, X_rl_42f60e, X_rl_428078, X_rl_42e3ba, X_rl_42e45e, X_rl_42e50d, X_rl_42f449, X_rl_43b101, X_rl_43baf9, X_rl_43e668, X_rl_49bb9d, X_rl_4b2234, X_rl_4b26b8, X_rl_4b2730, X_rl_4b74ce,
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
HOOK_SMK(rl_40e3ee, 4, rl_40e3ee(I(0), I(1), I(2), I(3)))   /* a click can open a movie (movie-skip f662: lom.smk — the SMACK handle differed without the rollback) */
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
HOOK_R(rl_425ded, 0, rl_425ded())
HOOK_R(rl_425b10, 0, rl_425b10())
HOOK_V(rl_426d11, 1, rl_426d11(I(0)))
HOOK_V(rl_427d19, 0, rl_427d19())   /* EAX: Release's (the caller's with no cue) */
HOOK_R(rl_42f306, 0, rl_42f306())
HOOK_R(rl_42eb7c, 0, rl_42eb7c())
HOOK_V(rl_419c78, 0, rl_419c78())   /* the menu bar's chrome (EAX: the screen byte / a callee's) */
HOOK_V(rl_412b33, 0, rl_412b33())   /* the labour screen */
HOOK_V(rl_412e6b, 0, rl_412e6b())   /* the mine panels */
HOOK_V(rl_413155, 0, rl_413155())   /* the smithy panel (EAX: 0x45240a's; 0x412b33 / 0x43a997 discard it) */
HOOK_V(rl_413590, 0, rl_413590())   /* the grain panel */
HOOK_V(rl_413b30, 0, rl_413b30())   /* the cattle panel */
HOOK_V(rl_4140f3, 0, rl_4140f3())   /* the reclaim panel */
HOOK_V(rl_414220, 0, rl_414220())   /* the castle panel (0x41dedb's EAX) */
HOOK_V(rl_41593b, 1, rl_41593b(I(0)))   /* the rankings screen */
HOOK_R(rl_415bdc, 0, rl_415bdc())   /* the rankings (EAX: the leader; 0x41593b reads it) */
HOOK_R(rl_415e42, 1, rl_415e42(I(0)))   /* a realm's score (EAX read by 0x415bdc) */
HOOK_V(rl_415fb7, 1, rl_415fb7(I(0)))   /* the merchant screen */
HOOK_V(rl_41789b, 0, rl_41789b())   /* the diplomacy screen by mode (a mode > 6 returns the caller's EAX) */
HOOK_V(rl_417960, 0, rl_417960())   /* the gift box */
HOOK_V(rl_4180f6, 0, rl_4180f6())   /* the armoury screen */
HOOK_V(rl_4184c6, 0, rl_4184c6())   /* the weapon assignment screen */
HOOK_V(rl_418e2d, 0, rl_418e2d())   /* the weapon assignment's numbers */
HOOK_V(rl_410a5d, 3, rl_410a5d(I(0), I(1), I(2)))   /* the county map (0x410cbd's EAX) */
HOOK_V(rl_41ad5d, 0, rl_41ad5d())   /* the shipment box */
HOOK_V(rl_41aea2, 0, rl_41aea2())   /* the shipment's goods */
HOOK_V(rl_41e61d, 0, rl_41e61d())   /* the title backdrop */
HOOK_V(rl_41fdd6, 0, rl_41fdd6())   /* an option page's drop-down list */

/* the standalone's last seams (2026-11-16): none reached by the corpus; RH_SH_HOST = host side effects (paint,
 * PostMessage, InvalidateRect, Restore on rhost's surfaces, DebugBreak / the MP transport) */
HOOK_R(rl_40902b, 2, rl_40902b(I(0), I(1)))
HOOK_V(rl_41e5d0, 0, rl_41e5d0())
HOOK_V(rl_420428, 0, rl_420428())
HOOK_V(rl_4247f5, 0, rl_4247f5())
HOOK_V(rl_42563c, 0, rl_42563c())
HOOK_V(rl_425a6a, 0, rl_425a6a())
HOOK_R(rl_427d72, 1, rl_427d72(I(0)))
HOOK_R(rl_42f60e, 1, rl_42f60e(I(0)))
HOOK_R(rl_428078, 1, rl_428078(I(0)))
HOOK_V(rl_42e3ba, 1, rl_42e3ba(I(0)))
HOOK_V(rl_42e45e, 4, rl_42e45e(I(0), I(1), I(2), I(3)))
HOOK_V(rl_42e50d, 0, rl_42e50d())
HOOK_V(rl_42f449, 0, rl_42f449())
HOOK_V(rl_43b101, 0, rl_43b101())
HOOK_V(rl_43baf9, 0, rl_43baf9())
HOOK_V(rl_43e668, 1, rl_43e668(I(0)))
HOOK_V(rl_49bb9d, 1, rl_49bb9d(I(0)))
HOOK_V(rl_4b2234, 0, rl_4b2234())
HOOK_V(rl_4b26b8, 0, rl_4b26b8())
HOOK_R(rl_4b2730, 0, rl_4b2730())
HOOK_V(rl_4b74ce, 0, rl_4b74ce())

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
    { "rl_425ded", 0x425dedu, (void *)w_rl_425ded, 0, 0, 0, 0, "" },
    { "rl_425b10", 0x425b10u, (void *)w_rl_425b10, 0, 0, 0, 0, "" },
    { "rl_426d11", 0x426d11u, (void *)w_rl_426d11, 0, 0, 0, 0, "" },
    { "rl_427d19", 0x427d19u, (void *)w_rl_427d19, 0, 0, 0, 0, "" },
    { "rl_42f306", 0x42f306u, (void *)w_rl_42f306, 0, 0, 0, 0, "" },
    { "rl_42eb7c", 0x42eb7cu, (void *)w_rl_42eb7c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_419c78", 0x419c78u, (void *)w_rl_419c78, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_412b33", 0x412b33u, (void *)w_rl_412b33, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_412e6b", 0x412e6bu, (void *)w_rl_412e6b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_413155", 0x413155u, (void *)w_rl_413155, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_413590", 0x413590u, (void *)w_rl_413590, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_413b30", 0x413b30u, (void *)w_rl_413b30, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4140f3", 0x4140f3u, (void *)w_rl_4140f3, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_414220", 0x414220u, (void *)w_rl_414220, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41593b", 0x41593bu, (void *)w_rl_41593b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_415bdc", 0x415bdcu, (void *)w_rl_415bdc, 0, 0, 0, 0, "" },
    { "rl_415e42", 0x415e42u, (void *)w_rl_415e42, 0, 0, 0, 0, "" },
    { "rl_415fb7", 0x415fb7u, (void *)w_rl_415fb7, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41789b", 0x41789bu, (void *)w_rl_41789b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_417960", 0x417960u, (void *)w_rl_417960, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4180f6", 0x4180f6u, (void *)w_rl_4180f6, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4184c6", 0x4184c6u, (void *)w_rl_4184c6, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_418e2d", 0x418e2du, (void *)w_rl_418e2d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_410a5d", 0x410a5du, (void *)w_rl_410a5d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41ad5d", 0x41ad5du, (void *)w_rl_41ad5d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41aea2", 0x41aea2u, (void *)w_rl_41aea2, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41e61d", 0x41e61du, (void *)w_rl_41e61d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41fdd6", 0x41fdd6u, (void *)w_rl_41fdd6, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_40902b", 0x40902bu, (void *)w_rl_40902b, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_41e5d0", 0x41e5d0u, (void *)w_rl_41e5d0, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_420428", 0x420428u, (void *)w_rl_420428, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4247f5", 0x4247f5u, (void *)w_rl_4247f5, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42563c", 0x42563cu, (void *)w_rl_42563c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_425a6a", 0x425a6au, (void *)w_rl_425a6a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_427d72", 0x427d72u, (void *)w_rl_427d72, 0, 0, 0, 0, "" },
    { "rl_42f60e", 0x42f60eu, (void *)w_rl_42f60e, 0, 0, 0, 0, "" },
    { "rl_428078", 0x428078u, (void *)w_rl_428078, 0, 0, 0, 0, "" },
    { "rl_42e3ba", 0x42e3bau, (void *)w_rl_42e3ba, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_42e45e", 0x42e45eu, (void *)w_rl_42e45e, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42e50d", 0x42e50du, (void *)w_rl_42e50d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_42f449", 0x42f449u, (void *)w_rl_42f449, 0, 0, 0, 0, "" },
    { "rl_43b101", 0x43b101u, (void *)w_rl_43b101, 0, 0, 0, 0, "" },
    { "rl_43baf9", 0x43baf9u, (void *)w_rl_43baf9, 0, 0, 0, 0, "" },
    { "rl_43e668", 0x43e668u, (void *)w_rl_43e668, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_49bb9d", 0x49bb9du, (void *)w_rl_49bb9d, 0, 0, 0, 0, "" },
    { "rl_4b2234", 0x4b2234u, (void *)w_rl_4b2234, 0, 0, 0, 0, "" },
    { "rl_4b26b8", 0x4b26b8u, (void *)w_rl_4b26b8, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b2730", 0x4b2730u, (void *)w_rl_4b2730, 0, 0, 0, 0, "", 0, RH_SH_HOST },
    { "rl_4b74ce", 0x4b74ceu, (void *)w_rl_4b74ce, 0, 0, 0, 0, "", 0, RH_SH_HOST },
};
const int RH_HOOKS_RL_UI4_N = sizeof RH_HOOKS_RL_UI4 / sizeof *RH_HOOKS_RL_UI4;
