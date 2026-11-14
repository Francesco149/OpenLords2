/* rh_hooks_rl_mpmsg.c — a retail-layout port, partition 'mpmsg' (src/rl_mpmsg.c) vs retail, on identical state
 * (rh_hooks.h contract; plumbing rh_port.h; the shared binding: rh_hooks_bf.c). One row per ported
 * function: HOOK_V(rl_<va>, N, rl_<va>(I(0), ...)) (void, or a return no caller reads) / HOOK_R (a return a
 * caller reads) + the table row. The payload builders / receivers (ids 1..22) have no call site: retail reaches them
 * through the tables 0x4d57f0 (0x43eda0) / 0x4d5980 (0x43eac1) — entry-patched rows; nobody reads their EAX.
 * 0x440dd2 reads an uninitialized local ([ebp-0xc] = e0 - 0x10): in / out through rl_440dd2_res.
 * docs/findings/rl-mpmsg.md. */
#include "rh_port.h"
#include "rl.h"
#include "rl_mpmsg_globals.h"
#include "rh_rl_mpmsg_alias.h"

__asm__(RH_RL_MPMSG_ALIASES);

#define RH_TABLE RH_HOOKS_RL_MPMSG
extern hook_t RH_HOOKS_RL_MPMSG[];

/* row indices (must follow the table's order) */
enum {
    X_rl_43e330, X_rl_43e433, X_rl_43e50d, X_rl_43e5c1, X_rl_43e98b, X_rl_43eac1, X_rl_43f01d, X_rl_43f4b7,
    X_rl_43f8ff, X_rl_43fd2a, X_rl_43fed2, X_rl_440038, X_rl_44015a, X_rl_4401ba, X_rl_4401f6, X_rl_440231,
    X_rl_4407e5, X_rl_440dd2, X_rl_441066, X_rl_4410bd, X_rl_44121a, X_rl_4412cc, X_rl_4413a4, X_rl_44164d,
    X_rl_441905, X_rl_4419f9, X_rl_441b4e, X_rl_441b86, X_rl_441d2a, X_rl_441e1f, X_rl_441f2a, X_rl_441f50,
    X_rl_441f76, X_rl_442014, X_rl_4420ad, X_rl_4421c3, X_rl_442309, X_rl_442361, X_rl_4423b9, X_rl_44240a,
    X_rl_442448, X_rl_4424d6, X_rl_4425bf, X_rl_4427d1, X_rl_4429f2, X_rl_442a25, X_rl_442afd, X_rl_442b08,
    X_rl_442bad, X_rl_442c01, X_rl_442c3f, X_rl_442c82, X_rl_442cb9, X_rl_442cd6,
};
static int rl_440dd2_tmp;

/* the tick minima / lookups / checksums / copies / the sync checks: their callers read EAX */
HOOK_R(rl_43e330, 1, rl_43e330(I(0)))
HOOK_R(rl_43e433, 0, rl_43e433())
HOOK_R(rl_43e50d, 0, rl_43e50d())
HOOK_R(rl_43e5c1, 0, rl_43e5c1())   /* EAX = DAT_004d2160 (not read; exact) */
HOOK_R(rl_43e98b, 1, rl_43e98b(I(0)))
HOOK_V(rl_43eac1, 1, rl_43eac1(I(0)))
HOOK_V(rl_43f01d, 0, rl_43f01d())
HOOK_V(rl_43f4b7, 0, rl_43f4b7())
HOOK_V(rl_43f8ff, 0, rl_43f8ff())
HOOK_V(rl_43fd2a, 0, rl_43fd2a())   /* __usercall (EAX in = EAX out when MP is off); nobody reads it */
HOOK_V(rl_43fed2, 0, rl_43fed2())
HOOK_R(rl_440038, 2, rl_440038(I(0), I(1)))
HOOK_R(rl_44015a, 3, rl_44015a(I(0), I(1), I(2)))
HOOK_R(rl_4401ba, 2, rl_4401ba(I(0), I(1)))
HOOK_R(rl_4401f6, 2, rl_4401f6(I(0), I(1)))
HOOK_V(rl_440231, 0, rl_440231())
HOOK_R(rl_4407e5, 0, rl_4407e5())
HOOK_R(rl_440dd2, 1, (rl_440dd2_res = rh_res_get(0x10), rl_440dd2_tmp = rl_440dd2(I(0)), rh_res_put(0x10, rl_440dd2_res),
                      rl_440dd2_tmp))
/* builders / receivers (entry-patched) */
HOOK_V(rl_441066, 0, rl_441066())
HOOK_V(rl_4410bd, 0, rl_4410bd())
HOOK_V(rl_44121a, 0, rl_44121a())
HOOK_V(rl_4412cc, 0, rl_4412cc())
HOOK_V(rl_4413a4, 0, rl_4413a4())
HOOK_V(rl_44164d, 0, rl_44164d())
HOOK_V(rl_441905, 0, rl_441905())
HOOK_V(rl_4419f9, 0, rl_4419f9())
HOOK_V(rl_441b4e, 0, rl_441b4e())
HOOK_V(rl_441b86, 0, rl_441b86())
HOOK_V(rl_441d2a, 0, rl_441d2a())
HOOK_V(rl_441e1f, 0, rl_441e1f())
HOOK_V(rl_441f2a, 0, rl_441f2a())
HOOK_V(rl_441f50, 0, rl_441f50())
HOOK_V(rl_441f76, 0, rl_441f76())
HOOK_V(rl_442014, 0, rl_442014())
HOOK_V(rl_4420ad, 0, rl_4420ad())
HOOK_V(rl_4421c3, 0, rl_4421c3())
HOOK_V(rl_442309, 0, rl_442309())
HOOK_V(rl_442361, 0, rl_442361())
HOOK_V(rl_4423b9, 0, rl_4423b9())
HOOK_V(rl_44240a, 0, rl_44240a())
HOOK_V(rl_442448, 0, rl_442448())
HOOK_V(rl_4424d6, 0, rl_4424d6())
HOOK_V(rl_4425bf, 0, rl_4425bf())
HOOK_V(rl_4427d1, 0, rl_4427d1())
HOOK_V(rl_4429f2, 0, rl_4429f2())
HOOK_V(rl_442a25, 0, rl_442a25())
HOOK_V(rl_442afd, 0, rl_442afd())
HOOK_V(rl_442b08, 0, rl_442b08())
HOOK_V(rl_442bad, 0, rl_442bad())
HOOK_V(rl_442c01, 0, rl_442c01())
HOOK_V(rl_442c3f, 0, rl_442c3f())
HOOK_V(rl_442c82, 0, rl_442c82())
HOOK_V(rl_442cb9, 0, rl_442cb9())
HOOK_V(rl_442cd6, 0, rl_442cd6())

hook_t RH_HOOKS_RL_MPMSG[] = {
    { "rl_43e330", 0x43e330u, (void *)w_rl_43e330, 0, 0, 0, 0, "" },
    { "rl_43e433", 0x43e433u, (void *)w_rl_43e433, 0, 0, 0, 0, "" },
    { "rl_43e50d", 0x43e50du, (void *)w_rl_43e50d, 0, 0, 0, 0, "" },
    { "rl_43e5c1", 0x43e5c1u, (void *)w_rl_43e5c1, 0, 0, 0, 0, "" },
    { "rl_43e98b", 0x43e98bu, (void *)w_rl_43e98b, 0, 0, 0, 0, "" },
    { "rl_43eac1", 0x43eac1u, (void *)w_rl_43eac1, 0, 0, 0, 0, "" },
    { "rl_43f01d", 0x43f01du, (void *)w_rl_43f01d, 0, 0, 0, 0, "" },
    { "rl_43f4b7", 0x43f4b7u, (void *)w_rl_43f4b7, 0, 0, 0, 0, "" },
    { "rl_43f8ff", 0x43f8ffu, (void *)w_rl_43f8ff, 0, 0, 0, 0, "" },
    { "rl_43fd2a", 0x43fd2au, (void *)w_rl_43fd2a, 0, 0, 0, 0, "" },
    { "rl_43fed2", 0x43fed2u, (void *)w_rl_43fed2, 0, 0, 0, 0, "" },
    { "rl_440038", 0x440038u, (void *)w_rl_440038, 0, 0, 0, 0, "" },
    { "rl_44015a", 0x44015au, (void *)w_rl_44015a, 0, 0, 0, 0, "" },
    { "rl_4401ba", 0x4401bau, (void *)w_rl_4401ba, 0, 0, 0, 0, "" },
    { "rl_4401f6", 0x4401f6u, (void *)w_rl_4401f6, 0, 0, 0, 0, "" },
    { "rl_440231", 0x440231u, (void *)w_rl_440231, 0, 0, 0, 0, "" },
    { "rl_4407e5", 0x4407e5u, (void *)w_rl_4407e5, 0, 0, 0, 0, "" },
    { "rl_440dd2", 0x440dd2u, (void *)w_rl_440dd2, 0, 0, 0, 0, "" },
    { "rl_441066", 0x441066u, (void *)w_rl_441066, 0, 0, 0, 0, "" },
    { "rl_4410bd", 0x4410bdu, (void *)w_rl_4410bd, 0, 0, 0, 0, "" },
    { "rl_44121a", 0x44121au, (void *)w_rl_44121a, 0, 0, 0, 0, "" },
    { "rl_4412cc", 0x4412ccu, (void *)w_rl_4412cc, 0, 0, 0, 0, "" },
    { "rl_4413a4", 0x4413a4u, (void *)w_rl_4413a4, 0, 0, 0, 0, "" },
    { "rl_44164d", 0x44164du, (void *)w_rl_44164d, 0, 0, 0, 0, "" },
    { "rl_441905", 0x441905u, (void *)w_rl_441905, 0, 0, 0, 0, "" },
    { "rl_4419f9", 0x4419f9u, (void *)w_rl_4419f9, 0, 0, 0, 0, "" },
    { "rl_441b4e", 0x441b4eu, (void *)w_rl_441b4e, 0, 0, 0, 0, "" },
    { "rl_441b86", 0x441b86u, (void *)w_rl_441b86, 0, 0, 0, 0, "" },
    { "rl_441d2a", 0x441d2au, (void *)w_rl_441d2a, 0, 0, 0, 0, "" },
    { "rl_441e1f", 0x441e1fu, (void *)w_rl_441e1f, 0, 0, 0, 0, "" },
    { "rl_441f2a", 0x441f2au, (void *)w_rl_441f2a, 0, 0, 0, 0, "" },
    { "rl_441f50", 0x441f50u, (void *)w_rl_441f50, 0, 0, 0, 0, "" },
    { "rl_441f76", 0x441f76u, (void *)w_rl_441f76, 0, 0, 0, 0, "" },
    { "rl_442014", 0x442014u, (void *)w_rl_442014, 0, 0, 0, 0, "" },
    { "rl_4420ad", 0x4420adu, (void *)w_rl_4420ad, 0, 0, 0, 0, "" },
    { "rl_4421c3", 0x4421c3u, (void *)w_rl_4421c3, 0, 0, 0, 0, "" },
    { "rl_442309", 0x442309u, (void *)w_rl_442309, 0, 0, 0, 0, "" },
    { "rl_442361", 0x442361u, (void *)w_rl_442361, 0, 0, 0, 0, "" },
    { "rl_4423b9", 0x4423b9u, (void *)w_rl_4423b9, 0, 0, 0, 0, "" },
    { "rl_44240a", 0x44240au, (void *)w_rl_44240a, 0, 0, 0, 0, "" },
    { "rl_442448", 0x442448u, (void *)w_rl_442448, 0, 0, 0, 0, "" },
    { "rl_4424d6", 0x4424d6u, (void *)w_rl_4424d6, 0, 0, 0, 0, "" },
    { "rl_4425bf", 0x4425bfu, (void *)w_rl_4425bf, 0, 0, 0, 0, "" },
    { "rl_4427d1", 0x4427d1u, (void *)w_rl_4427d1, 0, 0, 0, 0, "" },
    { "rl_4429f2", 0x4429f2u, (void *)w_rl_4429f2, 0, 0, 0, 0, "" },
    { "rl_442a25", 0x442a25u, (void *)w_rl_442a25, 0, 0, 0, 0, "" },
    { "rl_442afd", 0x442afdu, (void *)w_rl_442afd, 0, 0, 0, 0, "" },
    { "rl_442b08", 0x442b08u, (void *)w_rl_442b08, 0, 0, 0, 0, "" },
    { "rl_442bad", 0x442badu, (void *)w_rl_442bad, 0, 0, 0, 0, "" },
    { "rl_442c01", 0x442c01u, (void *)w_rl_442c01, 0, 0, 0, 0, "" },
    { "rl_442c3f", 0x442c3fu, (void *)w_rl_442c3f, 0, 0, 0, 0, "" },
    { "rl_442c82", 0x442c82u, (void *)w_rl_442c82, 0, 0, 0, 0, "" },
    { "rl_442cb9", 0x442cb9u, (void *)w_rl_442cb9, 0, 0, 0, 0, "" },
    { "rl_442cd6", 0x442cd6u, (void *)w_rl_442cd6, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_RL_MPMSG_N = sizeof RH_HOOKS_RL_MPMSG / sizeof *RH_HOOKS_RL_MPMSG;
