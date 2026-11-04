/* engine_bf_setup_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * engine_bf_setup.c, add its row to tools/rhost/rh_hooks_bf_setup.c. */
#include "engine_bf.h"

int bf_4778a0(void) { return (int)engine_retail_call(0x4778a0u, 0); }
int bf_477c89(void) { return (int)engine_retail_call(0x477c89u, 0); }
int bf_47b8b2(int a0) { return (int)engine_retail_call(0x47b8b2u, 1, (uint32_t)a0); }
int bf_47c4ba(int a0) { return (int)engine_retail_call(0x47c4bau, 1, (uint32_t)a0); }
int bf_4787a4(void) { return (int)engine_retail_call(0x4787a4u, 0); }
int bf_47cec1(int a0) { return (int)engine_retail_call(0x47cec1u, 1, (uint32_t)a0); }
int bf_47d474(int a0) { return (int)engine_retail_call(0x47d474u, 1, (uint32_t)a0); }
int bf_47d4b8(int a0) { return (int)engine_retail_call(0x47d4b8u, 1, (uint32_t)a0); }
int bf_47d5ec(int a0) { return (int)engine_retail_call(0x47d5ecu, 1, (uint32_t)a0); }
int bf_47d6b0(int a0) { return (int)engine_retail_call(0x47d6b0u, 1, (uint32_t)a0); }
int bf_47dc9c(void) { return (int)engine_retail_call(0x47dc9cu, 0); }
int bf_47dcce(void) { return (int)engine_retail_call(0x47dcceu, 0); }
int bf_47e1dc(void) { return (int)engine_retail_call(0x47e1dcu, 0); }
int bf_47e230(void) { return (int)engine_retail_call(0x47e230u, 0); }
int bf_47e263(void) { return (int)engine_retail_call(0x47e263u, 0); }
int bf_47e387(void) { return (int)engine_retail_call(0x47e387u, 0); }
int bf_47e52d(void) { return (int)engine_retail_call(0x47e52du, 0); }
int bf_47e668(void) { return (int)engine_retail_call(0x47e668u, 0); }
int bf_47e7b2(void) { return (int)engine_retail_call(0x47e7b2u, 0); }
int bf_47e926(void) { return (int)engine_retail_call(0x47e926u, 0); }
int bf_48eefd(void) { return (int)engine_retail_call(0x48eefdu, 0); }
int bf_497024(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x497024u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int bf_497247(int a0, int a1, int a2) { return (int)engine_retail_call(0x497247u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_49719e(int a0, int a1, int a2) { return (int)engine_retail_call(0x49719eu, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_42d806(int a0, int a1) { return (int)engine_retail_call(0x42d806u, 2, (uint32_t)a0, (uint32_t)a1); }
