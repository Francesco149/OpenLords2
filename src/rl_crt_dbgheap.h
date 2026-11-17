/* rl_crt_dbgheap.h — retail's debug CRT, the debug heap's check / realloc / leak-dump paths and the sbh internals they reach (_CrtCheckMemory, _realloc_dbg, _CrtDumpMemoryLeaks, __sbh_decommit_pages / resize / heap_check, _callnewh): ported on retail's layout (rl_crt_dbgheap.c; docs/findings/crt-port.md
 * §Gap ports). Canonical names rl_crt_<va>; every argument / pointer is a 32-bit retail value. */
#pragma once
#include <stdint.h>
