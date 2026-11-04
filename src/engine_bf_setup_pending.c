/* engine_bf_setup_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * engine_bf_setup.c, add its row to tools/rhost/rh_hooks_bf_setup.c. */
#include "engine_bf.h"

int bf_4778a0(void) { return (int)engine_retail_call(0x4778a0u, 0); }
int bf_477c89(void) { return (int)engine_retail_call(0x477c89u, 0); }
int bf_47aaa3(int a0) { return (int)engine_retail_call(0x47aaa3u, 1, (uint32_t)a0); }
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
int bf_47efee(void) { return (int)engine_retail_call(0x47efeeu, 0); }
int bf_46f0f0(void) { return (int)engine_retail_call(0x46f0f0u, 0); }
int bf_46f13e(void) { return (int)engine_retail_call(0x46f13eu, 0); }
int bf_46f18d(void) { return (int)engine_retail_call(0x46f18du, 0); }
int bf_47f42d(void) { return (int)engine_retail_call(0x47f42du, 0); }
int bf_47fea7(int a0, int a1, int a2) { return (int)engine_retail_call(0x47fea7u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_480599(int a0) { return (int)engine_retail_call(0x480599u, 1, (uint32_t)a0); }
int bf_480662(int a0, int a1, int a2, int a3, int a4, int a5, int a6) { return (int)engine_retail_call(0x480662u, 7, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4, (uint32_t)a5, (uint32_t)a6); }
int bf_46e4c8(int a0, int a1, int a2, int a3, int a4, int a5, int a6) { return (int)engine_retail_call(0x46e4c8u, 7, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4, (uint32_t)a5, (uint32_t)a6); }
int bf_46e70e(int a0, int a1) { return (int)engine_retail_call(0x46e70eu, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_479473(int a0, int a1) { return (int)engine_retail_call(0x479473u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_48169e(int a0, int a1) { return (int)engine_retail_call(0x48169eu, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_4816f9(int a0, int a1) { return (int)engine_retail_call(0x4816f9u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_481878(int a0, int a1, int a2) { return (int)engine_retail_call(0x481878u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_481906(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x481906u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int bf_4801f8(int a0, int a1, int a2) { return (int)engine_retail_call(0x4801f8u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_480f2e(void) { return (int)engine_retail_call(0x480f2eu, 0); }
int bf_480f8b(void) { return (int)engine_retail_call(0x480f8bu, 0); }
int bf_46c40c(int a0, int a1) { return (int)engine_retail_call(0x46c40cu, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_46c7fc(int a0) { return (int)engine_retail_call(0x46c7fcu, 1, (uint32_t)a0); }
int bf_47d6fe(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x47d6feu, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int bf_47d79c(int a0, int a1, int a2) { return (int)engine_retail_call(0x47d79cu, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_47d816(int a0, int a1) { return (int)engine_retail_call(0x47d816u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_42d806(int a0, int a1) { return (int)engine_retail_call(0x42d806u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_47ed9b(void) { return (int)engine_retail_call(0x47ed9bu, 0); }
