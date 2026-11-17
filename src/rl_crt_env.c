/* rl_crt_env.c — retail's statically linked MSVC 4.x DEBUG CRT: the locale / environment / wide-char / file paths (_isctype, __crtLCMapStringA, CPtoLCID / setSBCS, atol, wcstombs / wctomb, __crtsetenv / __wtomb_environ, _fcloseall / fclose, _commit, _chsize / _setmode, _validdrive).
 * No scenario reaches these (error / dead paths): each is ported from retail's code (IDA pseudocode + disassembly) and
 * verified by synthetic carriers (tests/probes/crt-env.probe, `rhost.py probe`). Conventions as rl_crt.c: retail's
 * globals by VA, every OS call through retail's IAT slot, an unported callee by bare VA (engine_retail_call). */
#include "rl_crt_env.h"
#include "rl_crt.h"
#include "engine_retail_call.h"

#include <string.h>
#include <windows.h>
