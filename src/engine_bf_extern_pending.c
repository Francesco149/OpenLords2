/* engine_bf_extern_pending.c — retail stubs for the partition's functions not ported yet (engine_retail_call: rhost
 * runs retail's code; the standalone stops with the VA). Porting a function = delete its stub here, define it in
 * engine_bf_extern.c, add its row to tools/rhost/rh_hooks_bf_extern.c. */
#include "engine_bf.h"

int bf_4987b7(void) { return (int)engine_retail_call(0x4987b7u, 0); }
