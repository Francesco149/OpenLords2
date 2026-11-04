/* engine_bf_unit_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * engine_bf_unit.c, add its row to tools/rhost/rh_hooks_bf_unit.c. */
#include "engine_bf.h"

int bf_4825d1(void) { return (int)engine_retail_call(0x4825d1u, 0); }
int bf_4826b0(void) { return (int)engine_retail_call(0x4826b0u, 0); }
int bf_482789(void) { return (int)engine_retail_call(0x482789u, 0); }
int bf_482862(void) { return (int)engine_retail_call(0x482862u, 0); }
int bf_48293b(void) { return (int)engine_retail_call(0x48293bu, 0); }
int bf_482a14(void) { return (int)engine_retail_call(0x482a14u, 0); }
int bf_482aed(void) { return (int)engine_retail_call(0x482aedu, 0); }
int bf_482bc6(void) { return (int)engine_retail_call(0x482bc6u, 0); }
int bf_482ca8(void) { return (int)engine_retail_call(0x482ca8u, 0); }
int bf_482d8a(void) { return (int)engine_retail_call(0x482d8au, 0); }
int bf_482e6c(void) { return (int)engine_retail_call(0x482e6cu, 0); }
int bf_481abb(int a0) { return (int)engine_retail_call(0x481abbu, 1, (uint32_t)a0); }
int bf_4861e9(void) { return (int)engine_retail_call(0x4861e9u, 0); }
int bf_4861f9(void) { return (int)engine_retail_call(0x4861f9u, 0); }
int bf_486209(void) { return (int)engine_retail_call(0x486209u, 0); }
int bf_486219(void) { return (int)engine_retail_call(0x486219u, 0); }
int bf_486229(void) { return (int)engine_retail_call(0x486229u, 0); }
int bf_486239(void) { return (int)engine_retail_call(0x486239u, 0); }
int bf_486249(void) { return (int)engine_retail_call(0x486249u, 0); }
int bf_486d83(void) { return (int)engine_retail_call(0x486d83u, 0); }
int bf_4872ae(void) { return (int)engine_retail_call(0x4872aeu, 0); }
int bf_487908(void) { return (int)engine_retail_call(0x487908u, 0); }
int bf_487ce4(void) { return (int)engine_retail_call(0x487ce4u, 0); }
int bf_48804a(void) { return (int)engine_retail_call(0x48804au, 0); }
int bf_488436(void) { return (int)engine_retail_call(0x488436u, 0); }
int bf_488793(void) { return (int)engine_retail_call(0x488793u, 0); }
int bf_48895e(void) { return (int)engine_retail_call(0x48895eu, 0); }
int bf_49459a(int a0) { return (int)engine_retail_call(0x49459au, 1, (uint32_t)a0); }
