/* rh_hooks_rl_ctl.c — a retail-layout port, partition 'ctl' (src/rl_ctl.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). docs/findings/rl-ctl.md. */
#include "rh_port.h"
#include "rl.h"
#include "rl_ctl_globals.h"
#include "rh_rl_ctl_alias.h"

__asm__(RH_RL_CTL_ALIASES);

#define RH_TABLE RH_HOOKS_RL_CTL
extern hook_t RH_HOOKS_RL_CTL[];
#include "rh_hook_smk.h"   /* HOOK_SMK_V: the rows that open / close a movie */
/* row indices (must follow the table's order) */
enum {
    X_rl_452306, X_rl_4523e6, X_rl_45240a, X_rl_452436, X_rl_452489, X_rl_4524dc, X_rl_452628, X_rl_45270a,
    X_rl_43cc56, X_rl_4527a6, X_rl_452775,
    X_rl_449d6e, X_rl_44b4d0, X_rl_44b391, X_rl_44b302, X_rl_44b042, X_rl_44ba35, X_rl_44bfd5, X_rl_44c093, X_rl_44d281, X_rl_44d303, X_rl_450fb4, X_rl_44c6c4, X_rl_4651b9, X_rl_44ae51, X_rl_43278b, X_rl_432746,
    X_rl_451f1a, X_rl_451fcc, X_rl_452160, X_rl_432640, X_rl_432967, X_rl_431f59, X_rl_432221,
    X_rl_4329ec, X_rl_43ad25, X_rl_43ac23, X_rl_448440, X_rl_4514f8, X_rl_451150, X_rl_43e9e2, X_rl_448648,
    X_rl_43caf4, X_rl_43723a, X_rl_4371ce, X_rl_43d309, X_rl_43ce1a,
    X_rl_432b05, X_rl_432cc8, X_rl_432fab, X_rl_432ee6, X_rl_433155, X_rl_433461, X_rl_4335f0, X_rl_4337b4, X_rl_433905, X_rl_433998, X_rl_433ba2, X_rl_433a23, X_rl_433ce4, X_rl_43400b, X_rl_4342f3, X_rl_434346,
    X_rl_434e1f, X_rl_435211, X_rl_4351c4, X_rl_43524e, X_rl_4358b0, X_rl_43582a, X_rl_43593a, X_rl_4359bc, X_rl_435a0c, X_rl_435a61, X_rl_435b4d, X_rl_435ae8, X_rl_435c89, X_rl_435cbf, X_rl_43611b, X_rl_436141, X_rl_436a88, X_rl_43a1e9, X_rl_43a2b2, X_rl_43aa32, X_rl_43aad5, X_rl_43a8f2, X_rl_43a922, X_rl_43a939, X_rl_43a846, X_rl_43a5a9, X_rl_43b04c,
};

/* the dirty-rect marks: EAX modelled (0x452306's fourth argument) */
HOOK_R(rl_452306, 4, rl_452306(I(0), I(1), I(2), I(3)))
HOOK_R(rl_4523e6, 0, rl_4523e6())
HOOK_R(rl_45240a, 0, rl_45240a())
HOOK_R(rl_452436, 2, rl_452436(I(0), I(1)))
HOOK_R(rl_452489, 2, rl_452489(I(0), I(1)))
HOOK_R(rl_4524dc, 2, rl_4524dc(I(0), I(1)))
HOOK_R(rl_452628, 3, rl_452628(I(0), I(1), I(2)))
HOOK_R(rl_45270a, 0, rl_45270a())
HOOK_R(rl_43cc56, 0, rl_43cc56())
HOOK_R(rl_4527a6, 5, rl_4527a6(I(0), I(1), I(2), I(3), I(4)))
HOOK_R(rl_452775, 5, rl_452775(I(0), I(1), I(2), I(3), I(4)))

HOOK_R(rl_449d6e, 1, rl_449d6e(I(0)))
HOOK_R(rl_44b4d0, 1, rl_44b4d0(I(0)))
HOOK_R(rl_44b391, 1, rl_44b391(I(0)))
HOOK_R(rl_44b302, 1, rl_44b302(I(0)))
HOOK_V(rl_44b042, 0, rl_44b042())
HOOK_V(rl_44ba35, 0, rl_44ba35())
HOOK_V(rl_44bfd5, 1, rl_44bfd5(I(0)))
HOOK_V(rl_44c093, 1, rl_44c093(I(0)))
HOOK_R(rl_44d281, 2, rl_44d281(I(0), I(1)))
HOOK_R(rl_44d303, 2, rl_44d303(I(0), I(1)))
HOOK_R(rl_450fb4, 1, rl_450fb4(I(0)))
HOOK_V(rl_44c6c4, 2, rl_44c6c4(I(0), I(1)))
HOOK_V(rl_4651b9, 0, rl_4651b9())
HOOK_V(rl_44ae51, 0, rl_44ae51())
HOOK_R(rl_43278b, 1, rl_43278b(I(0)))
HOOK_R(rl_432746, 1, rl_432746(I(0)))

HOOK_V(rl_451f1a, 0, rl_451f1a())
HOOK_R(rl_451fcc, 1, rl_451fcc(I(0)))
HOOK_V(rl_452160, 1, rl_452160(I(0)))
HOOK_V(rl_432640, 0, rl_432640())
HOOK_R(rl_432967, 0, rl_432967())
HOOK_V(rl_431f59, 0, rl_431f59())
HOOK_R(rl_432221, 0, rl_432221())

HOOK_V(rl_4329ec, 0, rl_4329ec())
HOOK_SMK_V(rl_43ad25, 0, rl_43ad25())   /* screen 34 stops its movie (0x42df30): smackw32 rolled back */
HOOK_V(rl_43ac23, 0, rl_43ac23())
HOOK_V(rl_448440, 0, rl_448440())
HOOK_R(rl_4514f8, 1, rl_4514f8(I(0)))
HOOK_V(rl_451150, 0, rl_451150())
HOOK_V(rl_43e9e2, 0, rl_43e9e2())
HOOK_V(rl_448648, 1, rl_448648(I(0)))

HOOK_V(rl_43caf4, 0, rl_43caf4())
HOOK_V(rl_43723a, 0, rl_43723a())
HOOK_V(rl_4371ce, 0, rl_4371ce())
HOOK_V(rl_43d309, 2, rl_43d309(I(0), I(1)))
HOOK_V(rl_43ce1a, 0, rl_43ce1a())

HOOK_SMK_V(rl_432b05, 0, rl_432b05())   /* param 4 plays lom.smk: smackw32 rolled back (rh_hook_smk.h) */
HOOK_V(rl_432cc8, 0, rl_432cc8())
HOOK_V(rl_432fab, 1, rl_432fab(I(0)))
HOOK_V(rl_432ee6, 0, rl_432ee6())
HOOK_V(rl_433155, 0, rl_433155())
HOOK_V(rl_433461, 0, rl_433461())
HOOK_V(rl_4335f0, 0, rl_4335f0())
HOOK_V(rl_4337b4, 0, rl_4337b4())
HOOK_V(rl_433905, 0, rl_433905())
HOOK_V(rl_433998, 0, rl_433998())
HOOK_V(rl_433ba2, 2, rl_433ba2(I(0), I(1)))
HOOK_V(rl_433a23, 0, rl_433a23())
HOOK_R(rl_433ce4, 0, rl_433ce4())
HOOK_R(rl_43400b, 1, rl_43400b(I(0)))
HOOK_V(rl_4342f3, 0, rl_4342f3())
HOOK_V(rl_434346, 0, rl_434346())

HOOK_V(rl_434e1f, 0, rl_434e1f())
HOOK_V(rl_435211, 0, rl_435211())
HOOK_V(rl_4351c4, 0, rl_4351c4())
HOOK_V(rl_43524e, 0, rl_43524e())
HOOK_V(rl_4358b0, 0, rl_4358b0())
HOOK_R(rl_43582a, 0, rl_43582a())
HOOK_V(rl_43593a, 0, rl_43593a())
HOOK_V(rl_4359bc, 0, rl_4359bc())
HOOK_V(rl_435a0c, 0, rl_435a0c())
HOOK_V(rl_435a61, 0, rl_435a61())
HOOK_V(rl_435b4d, 0, rl_435b4d())
HOOK_V(rl_435ae8, 0, rl_435ae8())
HOOK_V(rl_435c89, 0, rl_435c89())
HOOK_V(rl_435cbf, 0, rl_435cbf())
HOOK_V(rl_43611b, 0, rl_43611b())
HOOK_V(rl_436141, 0, rl_436141())
HOOK_V(rl_436a88, 0, rl_436a88())
HOOK_V(rl_43a1e9, 0, rl_43a1e9())
HOOK_V(rl_43a2b2, 0, rl_43a2b2())
HOOK_V(rl_43aa32, 0, rl_43aa32())
HOOK_V(rl_43aad5, 0, rl_43aad5())
HOOK_V(rl_43a8f2, 0, rl_43a8f2())
HOOK_V(rl_43a922, 0, rl_43a922())
HOOK_V(rl_43a939, 0, rl_43a939())
HOOK_V(rl_43a846, 0, rl_43a846())
HOOK_V(rl_43a5a9, 3, rl_43a5a9(I(0), I(1), I(2)))
HOOK_V(rl_43b04c, 0, rl_43b04c())

hook_t RH_HOOKS_RL_CTL[] = {
    { "rl_452306", 0x452306u, (void *)w_rl_452306, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_4523e6", 0x4523e6u, (void *)w_rl_4523e6, 0, 0, 0, 0, "" },
    { "rl_45240a", 0x45240au, (void *)w_rl_45240a, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_452436", 0x452436u, (void *)w_rl_452436, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_452489", 0x452489u, (void *)w_rl_452489, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_4524dc", 0x4524dcu, (void *)w_rl_4524dc, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_452628", 0x452628u, (void *)w_rl_452628, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_45270a", 0x45270au, (void *)w_rl_45270a, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_43cc56", 0x43cc56u, (void *)w_rl_43cc56, 0, 0, 0, 0, "" },
    { "rl_4527a6", 0x4527a6u, (void *)w_rl_4527a6, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_452775", 0x452775u, (void *)w_rl_452775, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_449d6e", 0x449d6eu, (void *)w_rl_449d6e, 0, 0, 0, 0, "" },
    { "rl_44b4d0", 0x44b4d0u, (void *)w_rl_44b4d0, 0, 0, 0, 0, "" },
    { "rl_44b391", 0x44b391u, (void *)w_rl_44b391, 0, 0, 0, 0, "" },
    { "rl_44b302", 0x44b302u, (void *)w_rl_44b302, 0, 0, 0, 0, "" },
    { "rl_44b042", 0x44b042u, (void *)w_rl_44b042, 0, 0, 0, 0, "" },
    { "rl_44ba35", 0x44ba35u, (void *)w_rl_44ba35, 0, 0, 0, 0, "" },
    { "rl_44bfd5", 0x44bfd5u, (void *)w_rl_44bfd5, 0, 0, 0, 0, "" },
    { "rl_44c093", 0x44c093u, (void *)w_rl_44c093, 0, 0, 0, 0, "" },
    { "rl_44d281", 0x44d281u, (void *)w_rl_44d281, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_44d303", 0x44d303u, (void *)w_rl_44d303, 0, 0, 0, 0, "", 0, RH_SH_SAMPLE },
    { "rl_450fb4", 0x450fb4u, (void *)w_rl_450fb4, 0, 0, 0, 0, "" },
    { "rl_44c6c4", 0x44c6c4u, (void *)w_rl_44c6c4, 0, 0, 0, 0, "" },
    { "rl_4651b9", 0x4651b9u, (void *)w_rl_4651b9, 0, 0, 0, 0, "" },
    { "rl_44ae51", 0x44ae51u, (void *)w_rl_44ae51, 0, 0, 0, 0, "" },
    { "rl_43278b", 0x43278bu, (void *)w_rl_43278b, 0, 0, 0, 0, "" },
    { "rl_432746", 0x432746u, (void *)w_rl_432746, 0, 0, 0, 0, "" },
    { "rl_451f1a", 0x451f1au, (void *)w_rl_451f1a, 0, 0, 0, 0, "" },
    { "rl_451fcc", 0x451fccu, (void *)w_rl_451fcc, 0, 0, 0, 0, "" },
    { "rl_452160", 0x452160u, (void *)w_rl_452160, 0, 0, 0, 0, "", 0, RH_SH_MEDIA | RH_SH_SAMPLE },   /* the per-frame present: a pixel shadow a frame is the corpus's slowest row */
    { "rl_432640", 0x432640u, (void *)w_rl_432640, 0, 0, 0, 0, "" },
    { "rl_432967", 0x432967u, (void *)w_rl_432967, 0, 0, 0, 0, "" },
    { "rl_431f59", 0x431f59u, (void *)w_rl_431f59, 0, 0, 0, 0, "" },
    { "rl_432221", 0x432221u, (void *)w_rl_432221, 0, 0, 0, 0, "" },
    { "rl_4329ec", 0x4329ecu, (void *)w_rl_4329ec, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43ad25", 0x43ad25u, (void *)w_rl_43ad25, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43ac23", 0x43ac23u, (void *)w_rl_43ac23, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_448440", 0x448440u, (void *)w_rl_448440, 0, 0, 0, 0, "" },
    { "rl_4514f8", 0x4514f8u, (void *)w_rl_4514f8, 0, 0, 0, 0, "" },
    { "rl_451150", 0x451150u, (void *)w_rl_451150, 0, 0, 0, 0, "" },
    { "rl_43e9e2", 0x43e9e2u, (void *)w_rl_43e9e2, 0, 0, 0, 0, "" },
    { "rl_448648", 0x448648u, (void *)w_rl_448648, 0, 0, 0, 0, "" },
    { "rl_43caf4", 0x43caf4u, (void *)w_rl_43caf4, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43723a", 0x43723au, (void *)w_rl_43723a, 0, 0, 0, 0, "" },
    { "rl_4371ce", 0x4371ceu, (void *)w_rl_4371ce, 0, 0, 0, 0, "" },
    { "rl_43d309", 0x43d309u, (void *)w_rl_43d309, 0, 0, 0, 0, "" },
    { "rl_43ce1a", 0x43ce1au, (void *)w_rl_43ce1a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_432b05", 0x432b05u, (void *)w_rl_432b05, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_432cc8", 0x432cc8u, (void *)w_rl_432cc8, 0, 0, 0, 0, "" },
    { "rl_432fab", 0x432fabu, (void *)w_rl_432fab, 0, 0, 0, 0, "" },
    { "rl_432ee6", 0x432ee6u, (void *)w_rl_432ee6, 0, 0, 0, 0, "" },
    { "rl_433155", 0x433155u, (void *)w_rl_433155, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_433461", 0x433461u, (void *)w_rl_433461, 0, 0, 0, 0, "" },
    { "rl_4335f0", 0x4335f0u, (void *)w_rl_4335f0, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4337b4", 0x4337b4u, (void *)w_rl_4337b4, 0, 0, 0, 0, "" },
    { "rl_433905", 0x433905u, (void *)w_rl_433905, 0, 0, 0, 0, "" },
    { "rl_433998", 0x433998u, (void *)w_rl_433998, 0, 0, 0, 0, "" },
    { "rl_433ba2", 0x433ba2u, (void *)w_rl_433ba2, 0, 0, 0, 0, "" },
    { "rl_433a23", 0x433a23u, (void *)w_rl_433a23, 0, 0, 0, 0, "" },
    { "rl_433ce4", 0x433ce4u, (void *)w_rl_433ce4, 0, 0, 0, 0, "" },
    { "rl_43400b", 0x43400bu, (void *)w_rl_43400b, 0, 0, 0, 0, "" },
    { "rl_4342f3", 0x4342f3u, (void *)w_rl_4342f3, 0, 0, 0, 0, "" },
    { "rl_434346", 0x434346u, (void *)w_rl_434346, 0, 0, 0, 0, "" },
    { "rl_434e1f", 0x434e1fu, (void *)w_rl_434e1f, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_435211", 0x435211u, (void *)w_rl_435211, 0, 0, 0, 0, "" },
    { "rl_4351c4", 0x4351c4u, (void *)w_rl_4351c4, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43524e", 0x43524eu, (void *)w_rl_43524e, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_4358b0", 0x4358b0u, (void *)w_rl_4358b0, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43582a", 0x43582au, (void *)w_rl_43582a, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43593a", 0x43593au, (void *)w_rl_43593a, 0, 0, 0, 0, "" },
    { "rl_4359bc", 0x4359bcu, (void *)w_rl_4359bc, 0, 0, 0, 0, "" },
    { "rl_435a0c", 0x435a0cu, (void *)w_rl_435a0c, 0, 0, 0, 0, "" },
    { "rl_435a61", 0x435a61u, (void *)w_rl_435a61, 0, 0, 0, 0, "" },
    { "rl_435b4d", 0x435b4du, (void *)w_rl_435b4d, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_435ae8", 0x435ae8u, (void *)w_rl_435ae8, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_435c89", 0x435c89u, (void *)w_rl_435c89, 0, 0, 0, 0, "" },
    { "rl_435cbf", 0x435cbfu, (void *)w_rl_435cbf, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43611b", 0x43611bu, (void *)w_rl_43611b, 0, 0, 0, 0, "" },
    { "rl_436141", 0x436141u, (void *)w_rl_436141, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_436a88", 0x436a88u, (void *)w_rl_436a88, 0, 0, 0, 0, "" },
    { "rl_43a1e9", 0x43a1e9u, (void *)w_rl_43a1e9, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a2b2", 0x43a2b2u, (void *)w_rl_43a2b2, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43aa32", 0x43aa32u, (void *)w_rl_43aa32, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43aad5", 0x43aad5u, (void *)w_rl_43aad5, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a8f2", 0x43a8f2u, (void *)w_rl_43a8f2, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a922", 0x43a922u, (void *)w_rl_43a922, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a939", 0x43a939u, (void *)w_rl_43a939, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a846", 0x43a846u, (void *)w_rl_43a846, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43a5a9", 0x43a5a9u, (void *)w_rl_43a5a9, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
    { "rl_43b04c", 0x43b04cu, (void *)w_rl_43b04c, 0, 0, 0, 0, "", 0, RH_SH_MEDIA },
};
const int RH_HOOKS_RL_CTL_N = sizeof RH_HOOKS_RL_CTL / sizeof *RH_HOOKS_RL_CTL;
