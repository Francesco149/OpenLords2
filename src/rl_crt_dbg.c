/* rl_crt_dbg.c — retail's statically linked MSVC 4.x DEBUG CRT: the report / message / exit / signal path: _CrtDbgReport and its window, _amsg_exit / _NMSG_WRITE, _exit / abort / raise, _flsbuf / _getbuf, the C++ unhandled-exception filter.
 * No scenario reaches these (error / dead paths): each is ported from retail's code (IDA pseudocode + disassembly) and
 * verified by synthetic carriers (tests/probes/crt-dbg.probe, `rhost.py probe`). Conventions as rl_crt.c: retail's
 * globals by VA, every OS call through retail's IAT slot, an unported callee by bare VA (engine_retail_call). */
#include "rl_crt_dbg.h"
#include "rl_crt.h"
#include "engine_retail_call.h"

#include <string.h>
#include <windows.h>
