/* rl_crt_dbgheap.c — retail's statically linked MSVC 4.x DEBUG CRT: the debug heap's check / realloc / leak-dump paths and the sbh internals they reach (_CrtCheckMemory, _realloc_dbg, _CrtDumpMemoryLeaks, __sbh_decommit_pages / resize / heap_check, _callnewh).
 * No scenario reaches these (error / dead paths): each is ported from retail's code (IDA pseudocode + disassembly) and
 * verified by synthetic carriers (tests/probes/crt-dbgheap.probe, `rhost.py probe`). Conventions as rl_crt.c: retail's
 * globals by VA, every OS call through retail's IAT slot, an unported callee by bare VA (engine_retail_call). */
#include "rl_crt_dbgheap.h"
#include "rl_crt.h"
#include "engine_retail_call.h"

#include <string.h>
#include <windows.h>
