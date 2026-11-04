/* rh_hooks_bf_group.c — the battlefield port, partition 'group' (src/engine_bf_group.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the battlefield's binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(bf_<va>, N, bf_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. Rows only for PORTED functions (a row on a pending stub compares retail
 * with itself). */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_BF_GROUP
extern hook_t RH_HOOKS_BF_GROUP[];
/* row indices (must follow the table's order) */
enum { X_bf_478952, X_bf_4859e5, X_bf_46ef3c, X_bf_485bb1, X_bf_488dfe, X_bf_489401, X_bf_4891ad, X_bf_489654,
       X_bf_48970e, X_bf_489798, X_bf_489896, X_bf_489981, X_bf_489a62, X_bf_489b8d, X_bf_489f9d, X_bf_48a1c9,
       X_bf_48a38e, X_bf_47fc01, X_bf_4819cc, X_bf_48ef45, X_bf_48a9c7, X_bf_48b37b, X_bf_48a9d2, X_bf_48acd2,
       X_bf_48b02b, X_bf_48b386, X_bf_48b546, X_bf_48b5fc, X_bf_48b7fc, X_bf_48b9c1, X_bf_48bac4, X_bf_48bbe2,
       X_bf_48bcfb, X_bf_48becd, X_bf_48bff9, X_bf_48c06f, X_bf_48c363, X_bf_48c3fe, X_bf_48c4bd, X_bf_48c56f,
       X_bf_48c8af, X_bf_48c974 };

HOOK_V(bf_478952, 0, bf_478952())
HOOK_V(bf_4859e5, 0, bf_4859e5())
HOOK_V(bf_46ef3c, 0, bf_46ef3c())
HOOK_V(bf_485bb1, 0, bf_485bb1())
HOOK_V(bf_488dfe, 0, bf_488dfe())
HOOK_V(bf_489401, 0, bf_489401())
HOOK_V(bf_4891ad, 1, bf_4891ad(I(0)))
HOOK_R(bf_489654, 1, bf_489654(I(0)))
HOOK_V(bf_48970e, 1, bf_48970e(I(0)))
HOOK_V(bf_489798, 1, bf_489798(I(0)))
HOOK_V(bf_489896, 1, bf_489896(I(0)))
HOOK_V(bf_489981, 1, bf_489981(I(0)))
HOOK_R(bf_489a62, 3, bf_489a62(I(0), I(1), I(2)))
HOOK_V(bf_489b8d, 4, bf_489b8d(I(0), I(1), I(2), I(3)))
HOOK_R(bf_489f9d, 1, bf_489f9d(I(0)))
HOOK_V(bf_48a1c9, 1, bf_48a1c9(I(0)))
HOOK_R(bf_48a38e, 1, bf_48a38e(I(0)))
HOOK_V(bf_47fc01, 0, bf_47fc01())
HOOK_V(bf_4819cc, 1, bf_4819cc(I(0)))
HOOK_R(bf_48ef45, 3, bf_48ef45(I(0), I(1), I(2)))
HOOK_V(bf_48a9c7, 0, bf_48a9c7())
HOOK_V(bf_48b37b, 0, bf_48b37b())
HOOK_V(bf_48a9d2, 0, bf_48a9d2())
HOOK_V(bf_48acd2, 0, bf_48acd2())
HOOK_V(bf_48b02b, 0, bf_48b02b())
HOOK_V(bf_48b386, 0, bf_48b386())
HOOK_V(bf_48b546, 0, bf_48b546())
HOOK_V(bf_48b5fc, 1, bf_48b5fc(I(0)))
HOOK_V(bf_48b7fc, 0, bf_48b7fc())
HOOK_V(bf_48b9c1, 1, bf_48b9c1(I(0)))
HOOK_V(bf_48bac4, 1, bf_48bac4(I(0)))
HOOK_V(bf_48bbe2, 2, bf_48bbe2(I(0), I(1)))
HOOK_V(bf_48bcfb, 1, bf_48bcfb(I(0)))
HOOK_V(bf_48becd, 1, bf_48becd(I(0)))
HOOK_V(bf_48bff9, 1, bf_48bff9(I(0)))
HOOK_V(bf_48c06f, 0, bf_48c06f())
HOOK_V(bf_48c363, 1, bf_48c363(I(0)))
HOOK_V(bf_48c3fe, 1, bf_48c3fe(I(0)))
HOOK_V(bf_48c4bd, 1, bf_48c4bd(I(0)))
HOOK_V(bf_48c56f, 1, bf_48c56f(I(0)))
HOOK_V(bf_48c8af, 1, bf_48c8af(I(0)))
HOOK_R(bf_48c974, 1, bf_48c974(I(0)))

hook_t RH_HOOKS_BF_GROUP[] = {
    { "bf_478952", 0x478952u, (void *)w_bf_478952, 0, 0, 0, 0, "" },
    { "bf_4859e5", 0x4859e5u, (void *)w_bf_4859e5, 0, 0, 0, 0, "" },
    { "bf_46ef3c", 0x46ef3cu, (void *)w_bf_46ef3c, 0, 0, 0, 0, "" },
    { "bf_485bb1", 0x485bb1u, (void *)w_bf_485bb1, 0, 0, 0, 0, "" },
    { "bf_488dfe", 0x488dfeu, (void *)w_bf_488dfe, 0, 0, 0, 0, "" },
    { "bf_489401", 0x489401u, (void *)w_bf_489401, 0, 0, 0, 0, "" },
    { "bf_4891ad", 0x4891adu, (void *)w_bf_4891ad, 0, 0, 0, 0, "" },
    { "bf_489654", 0x489654u, (void *)w_bf_489654, 0, 0, 0, 0, "" },
    { "bf_48970e", 0x48970eu, (void *)w_bf_48970e, 0, 0, 0, 0, "" },
    { "bf_489798", 0x489798u, (void *)w_bf_489798, 0, 0, 0, 0, "" },
    { "bf_489896", 0x489896u, (void *)w_bf_489896, 0, 0, 0, 0, "" },
    { "bf_489981", 0x489981u, (void *)w_bf_489981, 0, 0, 0, 0, "" },
    { "bf_489a62", 0x489a62u, (void *)w_bf_489a62, 0, 0, 0, 0, "" },
    { "bf_489b8d", 0x489b8du, (void *)w_bf_489b8d, 0, 0, 0, 0, "" },
    { "bf_489f9d", 0x489f9du, (void *)w_bf_489f9d, 0, 0, 0, 0, "" },
    { "bf_48a1c9", 0x48a1c9u, (void *)w_bf_48a1c9, 0, 0, 0, 0, "" },
    { "bf_48a38e", 0x48a38eu, (void *)w_bf_48a38e, 0, 0, 0, 0, "" },
    { "bf_47fc01", 0x47fc01u, (void *)w_bf_47fc01, 0, 0, 0, 0, "" },
    { "bf_4819cc", 0x4819ccu, (void *)w_bf_4819cc, 0, 0, 0, 0, "" },
    { "bf_48ef45", 0x48ef45u, (void *)w_bf_48ef45, 0, 0, 0, 0, "" },
    { "bf_48a9c7", 0x48a9c7u, (void *)w_bf_48a9c7, 0, 0, 0, 0, "" },
    { "bf_48b37b", 0x48b37bu, (void *)w_bf_48b37b, 0, 0, 0, 0, "" },
    { "bf_48a9d2", 0x48a9d2u, (void *)w_bf_48a9d2, 0, 0, 0, 0, "" },
    { "bf_48acd2", 0x48acd2u, (void *)w_bf_48acd2, 0, 0, 0, 0, "" },
    { "bf_48b02b", 0x48b02bu, (void *)w_bf_48b02b, 0, 0, 0, 0, "" },
    { "bf_48b386", 0x48b386u, (void *)w_bf_48b386, 0, 0, 0, 0, "" },
    { "bf_48b546", 0x48b546u, (void *)w_bf_48b546, 0, 0, 0, 0, "" },
    { "bf_48b5fc", 0x48b5fcu, (void *)w_bf_48b5fc, 0, 0, 0, 0, "" },
    { "bf_48b7fc", 0x48b7fcu, (void *)w_bf_48b7fc, 0, 0, 0, 0, "" },
    { "bf_48b9c1", 0x48b9c1u, (void *)w_bf_48b9c1, 0, 0, 0, 0, "" },
    { "bf_48bac4", 0x48bac4u, (void *)w_bf_48bac4, 0, 0, 0, 0, "" },
    { "bf_48bbe2", 0x48bbe2u, (void *)w_bf_48bbe2, 0, 0, 0, 0, "" },
    { "bf_48bcfb", 0x48bcfbu, (void *)w_bf_48bcfb, 0, 0, 0, 0, "" },
    { "bf_48becd", 0x48becdu, (void *)w_bf_48becd, 0, 0, 0, 0, "" },
    { "bf_48bff9", 0x48bff9u, (void *)w_bf_48bff9, 0, 0, 0, 0, "" },
    { "bf_48c06f", 0x48c06fu, (void *)w_bf_48c06f, 0, 0, 0, 0, "" },
    { "bf_48c363", 0x48c363u, (void *)w_bf_48c363, 0, 0, 0, 0, "" },
    { "bf_48c3fe", 0x48c3feu, (void *)w_bf_48c3fe, 0, 0, 0, 0, "" },
    { "bf_48c4bd", 0x48c4bdu, (void *)w_bf_48c4bd, 0, 0, 0, 0, "" },
    { "bf_48c56f", 0x48c56fu, (void *)w_bf_48c56f, 0, 0, 0, 0, "" },
    { "bf_48c8af", 0x48c8afu, (void *)w_bf_48c8af, 0, 0, 0, 0, "" },
    { "bf_48c974", 0x48c974u, (void *)w_bf_48c974, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_BF_GROUP_N = sizeof RH_HOOKS_BF_GROUP / sizeof *RH_HOOKS_BF_GROUP;
