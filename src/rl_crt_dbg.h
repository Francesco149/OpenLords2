/* rl_crt_dbg.h — retail's debug CRT, the report / message / exit / signal path: _CrtDbgReport and its window, _amsg_exit / _NMSG_WRITE, _exit / abort / raise, _flsbuf / _getbuf, the C++ unhandled-exception filter: ported on retail's layout (rl_crt_dbg.c; docs/findings/crt-port.md
 * §Gap ports). Canonical names rl_crt_<va>; every argument / pointer is a 32-bit retail value. */
#pragma once
#include <stdint.h>
