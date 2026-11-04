/* engine_bf_strike_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * engine_bf_strike.c, add its row to tools/rhost/rh_hooks_bf_strike.c. */
#include "engine_bf.h"

int bf_483337(void) { return (int)engine_retail_call(0x483337u, 0); }
int bf_426120(int a0) { return (int)engine_retail_call(0x426120u, 1, (uint32_t)a0); }
int bf_4262cf(int a0) { return (int)engine_retail_call(0x4262cfu, 1, (uint32_t)a0); }
int bf_46e767(int a0, int a1, int a2, int a3, int a4) { return (int)engine_retail_call(0x46e767u, 5, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4); }
int bf_493cb9(int a0, int a1, int a2, int a3, int a4) { return (int)engine_retail_call(0x493cb9u, 5, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4); }
int bf_46efbe(int a0) { return (int)engine_retail_call(0x46efbeu, 1, (uint32_t)a0); }
int bf_46f525(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x46f525u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int bf_492c8b(void) { return (int)engine_retail_call(0x492c8bu, 0); }
int bf_46eb78(int a0) { return (int)engine_retail_call(0x46eb78u, 1, (uint32_t)a0); }
int bf_47dfe0(int a0) { return (int)engine_retail_call(0x47dfe0u, 1, (uint32_t)a0); }
int bf_47da54(int a0, int a1, int a2) { return (int)engine_retail_call(0x47da54u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_48551d(int a0, int a1) { return (int)engine_retail_call(0x48551du, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_485675(int a0, int a1, int a2) { return (int)engine_retail_call(0x485675u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_485b47(int a0, int a1) { return (int)engine_retail_call(0x485b47u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_496f72(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x496f72u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int bf_485861(int a0, int a1) { return (int)engine_retail_call(0x485861u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_493b61(int a0) { return (int)engine_retail_call(0x493b61u, 1, (uint32_t)a0); }
int bf_494075(void) { return (int)engine_retail_call(0x494075u, 0); }
int bf_494125(void) { return (int)engine_retail_call(0x494125u, 0); }
int bf_4941c5(void) { return (int)engine_retail_call(0x4941c5u, 0); }
int bf_494265(void) { return (int)engine_retail_call(0x494265u, 0); }
int bf_4956cc(int a0, int a1, int a2) { return (int)engine_retail_call(0x4956ccu, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
