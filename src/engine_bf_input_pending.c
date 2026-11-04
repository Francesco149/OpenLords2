/* engine_bf_input_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * engine_bf_input.c, add its row to tools/rhost/rh_hooks_bf_input.c. */
#include "engine_bf.h"

int bf_4238b8(void) { return (int)engine_retail_call(0x4238b8u, 0); }
int bf_4239d5(void) { return (int)engine_retail_call(0x4239d5u, 0); }
int bf_43b9a1(void) { return (int)engine_retail_call(0x43b9a1u, 0); }
int bf_43ba29(void) { return (int)engine_retail_call(0x43ba29u, 0); }
int bf_43bb68(void) { return (int)engine_retail_call(0x43bb68u, 0); }
int bf_43bbd7(int a0, int a1) { return (int)engine_retail_call(0x43bbd7u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_43bd02(void) { return (int)engine_retail_call(0x43bd02u, 0); }
int bf_43bd67(void) { return (int)engine_retail_call(0x43bd67u, 0); }
int bf_43bdcd(void) { return (int)engine_retail_call(0x43bdcdu, 0); }
int bf_43bf07(void) { return (int)engine_retail_call(0x43bf07u, 0); }
int bf_43c247(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) { return (int)engine_retail_call(0x43c247u, 8, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4, (uint32_t)a5, (uint32_t)a6, (uint32_t)a7); }
int bf_43c2a9(void) { return (int)engine_retail_call(0x43c2a9u, 0); }
int bf_43c4c6(int a0) { return (int)engine_retail_call(0x43c4c6u, 1, (uint32_t)a0); }
int bf_43c55c(int a0) { return (int)engine_retail_call(0x43c55cu, 1, (uint32_t)a0); }
int bf_43c57d(void) { return (int)engine_retail_call(0x43c57du, 0); }
int bf_43c634(int a0, int a1) { return (int)engine_retail_call(0x43c634u, 2, (uint32_t)a0, (uint32_t)a1); }
int bf_4784ca(void) { return (int)engine_retail_call(0x4784cau, 0); }
int bf_478987(int a0) { return (int)engine_retail_call(0x478987u, 1, (uint32_t)a0); }
int bf_478f0b(int a0) { return (int)engine_retail_call(0x478f0bu, 1, (uint32_t)a0); }
int bf_479a71(int a0) { return (int)engine_retail_call(0x479a71u, 1, (uint32_t)a0); }
int bf_479b58(int a0, int a1, int a2, int a3, int a4, int a5, int a6) { return (int)engine_retail_call(0x479b58u, 7, (uint32_t)a0, (uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4, (uint32_t)a5, (uint32_t)a6); }
int bf_479cf7(void) { return (int)engine_retail_call(0x479cf7u, 0); }
int bf_47a76d(int a0) { return (int)engine_retail_call(0x47a76du, 1, (uint32_t)a0); }
int bf_47f76d(void) { return (int)engine_retail_call(0x47f76du, 0); }
int bf_499cb1(int a0) { return (int)engine_retail_call(0x499cb1u, 1, (uint32_t)a0); }
