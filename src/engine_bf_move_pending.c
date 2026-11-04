/* engine_bf_move_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * engine_bf_move.c, add its row to tools/rhost/rh_hooks_bf_move.c. */
#include "engine_bf.h"

int bf_48f1dd(int a0) { return (int)engine_retail_call(0x48f1ddu, 1, (uint32_t)a0); }
int bf_47095e(int a0, int a1, int a2, int a3, int a4) { return (int)engine_retail_call(0x47095eu, 5, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4); }
int bf_4710f2(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x4710f2u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int bf_471f45(void) { return (int)engine_retail_call(0x471f45u, 0); }
int bf_4b3dcd(int a0, int a1, int a2) { return (int)engine_retail_call(0x4b3dcdu, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_471718(int a0, int a1, int a2, int a3) { return (int)engine_retail_call(0x471718u, 4, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3); }
int bf_472227(int a0) { return (int)engine_retail_call(0x472227u, 1, (uint32_t)a0); }
int bf_48f079(int a0) { return (int)engine_retail_call(0x48f079u, 1, (uint32_t)a0); }
int bf_49005f(void) { return (int)engine_retail_call(0x49005fu, 0); }
int bf_4928a3(int a0) { return (int)engine_retail_call(0x4928a3u, 1, (uint32_t)a0); }
int bf_496155(int a0, int a1, int a2) { return (int)engine_retail_call(0x496155u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
int bf_4904ec(void) { return (int)engine_retail_call(0x4904ecu, 0); }
int bf_490616(int a0) { return (int)engine_retail_call(0x490616u, 1, (uint32_t)a0); }
int bf_490a44(int a0) { return (int)engine_retail_call(0x490a44u, 1, (uint32_t)a0); }
int bf_490c59(int a0) { return (int)engine_retail_call(0x490c59u, 1, (uint32_t)a0); }
int bf_490fa2(int a0) { return (int)engine_retail_call(0x490fa2u, 1, (uint32_t)a0); }
int bf_491147(int a0) { return (int)engine_retail_call(0x491147u, 1, (uint32_t)a0); }
int bf_4912ec(void) { return (int)engine_retail_call(0x4912ecu, 0); }
int bf_491492(int a0) { return (int)engine_retail_call(0x491492u, 1, (uint32_t)a0); }
int bf_491a34(int a0) { return (int)engine_retail_call(0x491a34u, 1, (uint32_t)a0); }
int bf_491b1f(int a0) { return (int)engine_retail_call(0x491b1fu, 1, (uint32_t)a0); }
int bf_491f17(int a0, int a1) { return (int)engine_retail_call(0x491f17u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_49207e(int a0, int a1) { return (int)engine_retail_call(0x49207eu, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_494d08(int a0) { return (int)engine_retail_call(0x494d08u, 1, (uint32_t)a0); }
int bf_494df9(int a0) { return (int)engine_retail_call(0x494df9u, 1, (uint32_t)a0); }
int bf_4972f9(int a0, int a1, int a2) { return (int)engine_retail_call(0x4972f9u, 3, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2); }
