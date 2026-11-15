/* rh_hooks_rl_mpmsg3.c — a retail-layout port, partition 'mpmsg3' (src/rl_mpmsg3.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). docs/findings/rl-mpmsg3.md.
 * The payload builders (DAT_004d57f0[id]), receivers (DAT_004d5980[id]) and delayed-action handlers
 * (DAT_004d5c98[type]) have no E8 call site: entry-patched rows, HOOK_V (0x43eda0 / 0x43eac1 / 0x4472ce drop EAX).
 * 0x43e6a2 (the DirectPlay system events; called by 0x4b8c3a's receive loop, rl_io's port) and 0x43e8f4: no caller
 * reads EAX. */
#include "rh_port.h"
#include "rl.h"
#include "rl_mpmsg3_globals.h"
#include "rh_rl_mpmsg3_alias.h"
__asm__(RH_RL_MPMSG3_ALIASES);

#define RH_TABLE RH_HOOKS_RL_MPMSG3
extern hook_t RH_HOOKS_RL_MPMSG3[];
/* row indices (must follow the table's order) */
enum {
    X_rl_43e6a2, X_rl_43e8f4, X_rl_44255f, X_rl_442579, X_rl_442add, X_rl_442ae8, X_rl_442d00, X_rl_442d60,
    X_rl_442dad, X_rl_442e0d, X_rl_442e5a, X_rl_442eec, X_rl_443043, X_rl_443083, X_rl_4430d5, X_rl_443143,
    X_rl_44334e, X_rl_44338b, X_rl_44356d, X_rl_443587, X_rl_4435ce, X_rl_44363b, X_rl_443e51, X_rl_443f09,
    X_rl_443fab, X_rl_44400f, X_rl_444346, X_rl_4443aa, X_rl_44462f, X_rl_4446a6, X_rl_44470a, X_rl_44475c,
    X_rl_4447ab, X_rl_44480f, X_rl_444860, X_rl_4448c4, X_rl_444b07, X_rl_444b17, X_rl_444b27, X_rl_444bd0,
    X_rl_444c66, X_rl_444cb5, X_rl_444e3f, X_rl_444f96, X_rl_445063, X_rl_4451cc, X_rl_445458, X_rl_4454a9,
    X_rl_44562e, X_rl_4456c1, X_rl_4457a1, X_rl_445851, X_rl_445d97, X_rl_445e0e, X_rl_4460f8, X_rl_446171,
    X_rl_4461f4, X_rl_44622c, X_rl_44637e, X_rl_446389, X_rl_4463d9, X_rl_4464e4, X_rl_446683, X_rl_44678d,
    X_rl_446ba0, X_rl_446bba, X_rl_446bec, X_rl_446c1f, X_rl_447a59, X_rl_447ac5, X_rl_447d72, X_rl_447d9d,
    X_rl_447e0b, X_rl_447f82, X_rl_448076, X_rl_448137, X_rl_44825d, X_rl_448276, X_rl_44828f, X_rl_44829f,
    X_rl_4482e9, X_rl_448308, X_rl_4483d1, X_rl_4436bb, X_rl_4436f9, X_rl_444915, X_rl_444948,
};

HOOK_V(rl_43e6a2, 2, rl_43e6a2(I(0), I(1)))
HOOK_V(rl_43e8f4, 1, rl_43e8f4(I(0)))
HOOK_V(rl_44255f, 0, rl_44255f())
HOOK_V(rl_442579, 0, rl_442579())
HOOK_V(rl_442add, 0, rl_442add())
HOOK_V(rl_442ae8, 0, rl_442ae8())
HOOK_V(rl_442d00, 0, rl_442d00())
HOOK_V(rl_442d60, 0, rl_442d60())
HOOK_V(rl_442dad, 0, rl_442dad())
HOOK_V(rl_442e0d, 0, rl_442e0d())
HOOK_V(rl_442e5a, 0, rl_442e5a())
HOOK_V(rl_442eec, 0, rl_442eec())
HOOK_V(rl_443043, 0, rl_443043())
HOOK_V(rl_443083, 0, rl_443083())
HOOK_V(rl_4430d5, 0, rl_4430d5())
HOOK_V(rl_443143, 0, rl_443143())
HOOK_V(rl_44334e, 0, rl_44334e())
HOOK_V(rl_44338b, 0, rl_44338b())
HOOK_V(rl_44356d, 0, rl_44356d())
HOOK_V(rl_443587, 0, rl_443587())
HOOK_V(rl_4435ce, 0, rl_4435ce())
HOOK_V(rl_44363b, 0, rl_44363b())
HOOK_V(rl_443e51, 0, rl_443e51())
HOOK_V(rl_443f09, 0, rl_443f09())
HOOK_V(rl_443fab, 0, rl_443fab())
HOOK_V(rl_44400f, 0, rl_44400f())
HOOK_V(rl_444346, 0, rl_444346())
HOOK_V(rl_4443aa, 0, rl_4443aa())
HOOK_V(rl_44462f, 0, rl_44462f())
HOOK_V(rl_4446a6, 0, rl_4446a6())
HOOK_V(rl_44470a, 0, rl_44470a())
HOOK_V(rl_44475c, 0, rl_44475c())
HOOK_V(rl_4447ab, 0, rl_4447ab())
HOOK_V(rl_44480f, 0, rl_44480f())
HOOK_V(rl_444860, 0, rl_444860())
HOOK_V(rl_4448c4, 0, rl_4448c4())
HOOK_V(rl_444b07, 0, rl_444b07())
HOOK_V(rl_444b17, 0, rl_444b17())
HOOK_V(rl_444b27, 0, rl_444b27())
HOOK_V(rl_444bd0, 0, rl_444bd0())
HOOK_V(rl_444c66, 0, rl_444c66())
HOOK_V(rl_444cb5, 0, rl_444cb5())
HOOK_V(rl_444e3f, 0, rl_444e3f())
HOOK_V(rl_444f96, 0, rl_444f96())
HOOK_V(rl_445063, 0, rl_445063())
HOOK_V(rl_4451cc, 0, rl_4451cc())
HOOK_V(rl_445458, 0, rl_445458())
HOOK_V(rl_4454a9, 0, rl_4454a9())
HOOK_V(rl_44562e, 0, rl_44562e())
HOOK_V(rl_4456c1, 0, rl_4456c1())
HOOK_V(rl_4457a1, 0, rl_4457a1())
HOOK_V(rl_445851, 0, rl_445851())
HOOK_V(rl_445d97, 0, rl_445d97())
HOOK_V(rl_445e0e, 0, rl_445e0e())
HOOK_V(rl_4460f8, 0, rl_4460f8())
HOOK_V(rl_446171, 0, rl_446171())
HOOK_V(rl_4461f4, 0, rl_4461f4())
HOOK_V(rl_44622c, 0, rl_44622c())
HOOK_V(rl_44637e, 0, rl_44637e())
HOOK_V(rl_446389, 0, rl_446389())
HOOK_V(rl_4463d9, 0, rl_4463d9())
HOOK_V(rl_4464e4, 0, rl_4464e4())
HOOK_V(rl_446683, 0, rl_446683())
HOOK_V(rl_44678d, 0, rl_44678d())
HOOK_V(rl_446ba0, 0, rl_446ba0())
HOOK_V(rl_446bba, 0, rl_446bba())
HOOK_V(rl_446bec, 0, rl_446bec())
HOOK_V(rl_446c1f, 0, rl_446c1f())
HOOK_V(rl_447a59, 0, rl_447a59())
HOOK_V(rl_447ac5, 0, rl_447ac5())
HOOK_V(rl_447d72, 0, rl_447d72())
HOOK_V(rl_447d9d, 0, rl_447d9d())
HOOK_V(rl_447e0b, 0, rl_447e0b())
HOOK_V(rl_447f82, 0, rl_447f82())
HOOK_V(rl_448076, 0, rl_448076())
HOOK_V(rl_448137, 0, rl_448137())
HOOK_V(rl_44825d, 0, rl_44825d())
HOOK_V(rl_448276, 0, rl_448276())
HOOK_V(rl_44828f, 0, rl_44828f())
HOOK_V(rl_44829f, 0, rl_44829f())
HOOK_V(rl_4482e9, 0, rl_4482e9())
HOOK_V(rl_448308, 0, rl_448308())
HOOK_V(rl_4483d1, 0, rl_4483d1())
HOOK_V(rl_4436bb, 0, rl_4436bb())
HOOK_V(rl_4436f9, 0, rl_4436f9())
HOOK_V(rl_444915, 0, rl_444915())
HOOK_V(rl_444948, 0, rl_444948())

hook_t RH_HOOKS_RL_MPMSG3[] = {
    { "rl_43e6a2", 0x43e6a2u, (void *)w_rl_43e6a2, 0, 0, 0, 0, "" },
    { "rl_43e8f4", 0x43e8f4u, (void *)w_rl_43e8f4, 0, 0, 0, 0, "" },
    { "rl_44255f", 0x44255fu, (void *)w_rl_44255f, 0, 0, 0, 0, "" },
    { "rl_442579", 0x442579u, (void *)w_rl_442579, 0, 0, 0, 0, "" },
    { "rl_442add", 0x442addu, (void *)w_rl_442add, 0, 0, 0, 0, "" },
    { "rl_442ae8", 0x442ae8u, (void *)w_rl_442ae8, 0, 0, 0, 0, "" },
    { "rl_442d00", 0x442d00u, (void *)w_rl_442d00, 0, 0, 0, 0, "" },
    { "rl_442d60", 0x442d60u, (void *)w_rl_442d60, 0, 0, 0, 0, "" },
    { "rl_442dad", 0x442dadu, (void *)w_rl_442dad, 0, 0, 0, 0, "" },
    { "rl_442e0d", 0x442e0du, (void *)w_rl_442e0d, 0, 0, 0, 0, "" },
    { "rl_442e5a", 0x442e5au, (void *)w_rl_442e5a, 0, 0, 0, 0, "" },
    { "rl_442eec", 0x442eecu, (void *)w_rl_442eec, 0, 0, 0, 0, "" },
    { "rl_443043", 0x443043u, (void *)w_rl_443043, 0, 0, 0, 0, "" },
    { "rl_443083", 0x443083u, (void *)w_rl_443083, 0, 0, 0, 0, "" },
    { "rl_4430d5", 0x4430d5u, (void *)w_rl_4430d5, 0, 0, 0, 0, "" },
    { "rl_443143", 0x443143u, (void *)w_rl_443143, 0, 0, 0, 0, "" },
    { "rl_44334e", 0x44334eu, (void *)w_rl_44334e, 0, 0, 0, 0, "" },
    { "rl_44338b", 0x44338bu, (void *)w_rl_44338b, 0, 0, 0, 0, "" },
    { "rl_44356d", 0x44356du, (void *)w_rl_44356d, 0, 0, 0, 0, "" },
    { "rl_443587", 0x443587u, (void *)w_rl_443587, 0, 0, 0, 0, "" },
    { "rl_4435ce", 0x4435ceu, (void *)w_rl_4435ce, 0, 0, 0, 0, "" },
    { "rl_44363b", 0x44363bu, (void *)w_rl_44363b, 0, 0, 0, 0, "" },
    { "rl_443e51", 0x443e51u, (void *)w_rl_443e51, 0, 0, 0, 0, "" },
    { "rl_443f09", 0x443f09u, (void *)w_rl_443f09, 0, 0, 0, 0, "" },
    { "rl_443fab", 0x443fabu, (void *)w_rl_443fab, 0, 0, 0, 0, "" },
    { "rl_44400f", 0x44400fu, (void *)w_rl_44400f, 0, 0, 0, 0, "" },
    { "rl_444346", 0x444346u, (void *)w_rl_444346, 0, 0, 0, 0, "" },
    { "rl_4443aa", 0x4443aau, (void *)w_rl_4443aa, 0, 0, 0, 0, "" },
    { "rl_44462f", 0x44462fu, (void *)w_rl_44462f, 0, 0, 0, 0, "" },
    { "rl_4446a6", 0x4446a6u, (void *)w_rl_4446a6, 0, 0, 0, 0, "" },
    { "rl_44470a", 0x44470au, (void *)w_rl_44470a, 0, 0, 0, 0, "" },
    { "rl_44475c", 0x44475cu, (void *)w_rl_44475c, 0, 0, 0, 0, "" },
    { "rl_4447ab", 0x4447abu, (void *)w_rl_4447ab, 0, 0, 0, 0, "" },
    { "rl_44480f", 0x44480fu, (void *)w_rl_44480f, 0, 0, 0, 0, "" },
    { "rl_444860", 0x444860u, (void *)w_rl_444860, 0, 0, 0, 0, "" },
    { "rl_4448c4", 0x4448c4u, (void *)w_rl_4448c4, 0, 0, 0, 0, "" },
    { "rl_444b07", 0x444b07u, (void *)w_rl_444b07, 0, 0, 0, 0, "" },
    { "rl_444b17", 0x444b17u, (void *)w_rl_444b17, 0, 0, 0, 0, "" },
    { "rl_444b27", 0x444b27u, (void *)w_rl_444b27, 0, 0, 0, 0, "" },
    { "rl_444bd0", 0x444bd0u, (void *)w_rl_444bd0, 0, 0, 0, 0, "" },
    { "rl_444c66", 0x444c66u, (void *)w_rl_444c66, 0, 0, 0, 0, "" },
    { "rl_444cb5", 0x444cb5u, (void *)w_rl_444cb5, 0, 0, 0, 0, "" },
    { "rl_444e3f", 0x444e3fu, (void *)w_rl_444e3f, 0, 0, 0, 0, "" },
    { "rl_444f96", 0x444f96u, (void *)w_rl_444f96, 0, 0, 0, 0, "" },
    { "rl_445063", 0x445063u, (void *)w_rl_445063, 0, 0, 0, 0, "" },
    { "rl_4451cc", 0x4451ccu, (void *)w_rl_4451cc, 0, 0, 0, 0, "" },
    { "rl_445458", 0x445458u, (void *)w_rl_445458, 0, 0, 0, 0, "" },
    { "rl_4454a9", 0x4454a9u, (void *)w_rl_4454a9, 0, 0, 0, 0, "" },
    { "rl_44562e", 0x44562eu, (void *)w_rl_44562e, 0, 0, 0, 0, "" },
    { "rl_4456c1", 0x4456c1u, (void *)w_rl_4456c1, 0, 0, 0, 0, "" },
    { "rl_4457a1", 0x4457a1u, (void *)w_rl_4457a1, 0, 0, 0, 0, "" },
    { "rl_445851", 0x445851u, (void *)w_rl_445851, 0, 0, 0, 0, "" },
    { "rl_445d97", 0x445d97u, (void *)w_rl_445d97, 0, 0, 0, 0, "" },
    { "rl_445e0e", 0x445e0eu, (void *)w_rl_445e0e, 0, 0, 0, 0, "" },
    { "rl_4460f8", 0x4460f8u, (void *)w_rl_4460f8, 0, 0, 0, 0, "" },
    { "rl_446171", 0x446171u, (void *)w_rl_446171, 0, 0, 0, 0, "" },
    { "rl_4461f4", 0x4461f4u, (void *)w_rl_4461f4, 0, 0, 0, 0, "" },
    { "rl_44622c", 0x44622cu, (void *)w_rl_44622c, 0, 0, 0, 0, "" },
    { "rl_44637e", 0x44637eu, (void *)w_rl_44637e, 0, 0, 0, 0, "" },
    { "rl_446389", 0x446389u, (void *)w_rl_446389, 0, 0, 0, 0, "" },
    { "rl_4463d9", 0x4463d9u, (void *)w_rl_4463d9, 0, 0, 0, 0, "" },
    { "rl_4464e4", 0x4464e4u, (void *)w_rl_4464e4, 0, 0, 0, 0, "" },
    { "rl_446683", 0x446683u, (void *)w_rl_446683, 0, 0, 0, 0, "" },
    { "rl_44678d", 0x44678du, (void *)w_rl_44678d, 0, 0, 0, 0, "" },
    { "rl_446ba0", 0x446ba0u, (void *)w_rl_446ba0, 0, 0, 0, 0, "" },
    { "rl_446bba", 0x446bbau, (void *)w_rl_446bba, 0, 0, 0, 0, "" },
    { "rl_446bec", 0x446becu, (void *)w_rl_446bec, 0, 0, 0, 0, "" },
    { "rl_446c1f", 0x446c1fu, (void *)w_rl_446c1f, 0, 0, 0, 0, "" },
    { "rl_447a59", 0x447a59u, (void *)w_rl_447a59, 0, 0, 0, 0, "" },
    { "rl_447ac5", 0x447ac5u, (void *)w_rl_447ac5, 0, 0, 0, 0, "" },
    { "rl_447d72", 0x447d72u, (void *)w_rl_447d72, 0, 0, 0, 0, "" },
    { "rl_447d9d", 0x447d9du, (void *)w_rl_447d9d, 0, 0, 0, 0, "" },
    { "rl_447e0b", 0x447e0bu, (void *)w_rl_447e0b, 0, 0, 0, 0, "" },
    { "rl_447f82", 0x447f82u, (void *)w_rl_447f82, 0, 0, 0, 0, "" },
    { "rl_448076", 0x448076u, (void *)w_rl_448076, 0, 0, 0, 0, "" },
    { "rl_448137", 0x448137u, (void *)w_rl_448137, 0, 0, 0, 0, "" },
    { "rl_44825d", 0x44825du, (void *)w_rl_44825d, 0, 0, 0, 0, "" },
    { "rl_448276", 0x448276u, (void *)w_rl_448276, 0, 0, 0, 0, "" },
    { "rl_44828f", 0x44828fu, (void *)w_rl_44828f, 0, 0, 0, 0, "" },
    { "rl_44829f", 0x44829fu, (void *)w_rl_44829f, 0, 0, 0, 0, "" },
    { "rl_4482e9", 0x4482e9u, (void *)w_rl_4482e9, 0, 0, 0, 0, "" },
    { "rl_448308", 0x448308u, (void *)w_rl_448308, 0, 0, 0, 0, "" },
    { "rl_4483d1", 0x4483d1u, (void *)w_rl_4483d1, 0, 0, 0, 0, "" },
    { "rl_4436bb", 0x4436bbu, (void *)w_rl_4436bb, 0, 0, 0, 0, "" },
    { "rl_4436f9", 0x4436f9u, (void *)w_rl_4436f9, 0, 0, 0, 0, "" },
    { "rl_444915", 0x444915u, (void *)w_rl_444915, 0, 0, 0, 0, "" },
    { "rl_444948", 0x444948u, (void *)w_rl_444948, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_MPMSG3_N = sizeof RH_HOOKS_RL_MPMSG3 / sizeof *RH_HOOKS_RL_MPMSG3;
