/* rh_bf_extern_alias.h — GENERATED (tools/bf/gen_skeleton.py): every engine_bf_extern_globals.h symbol at its retail VA. */
#pragma once
#define BFA(n, v) ".globl _" #n "\n\t.set _" #n ", " #v "\n\t"
#define RH_BF_EXTERN_ALIASES \
    BFA(g_bf_4daec4,0x4daec4) \

