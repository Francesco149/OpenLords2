/* rl_crt_env.h — retail's debug CRT, the locale / environment / wide-char / file paths (_isctype, __crtLCMapStringA, CPtoLCID / setSBCS, atol, wcstombs / wctomb, __crtsetenv / __wtomb_environ, _fcloseall / fclose, _commit, _chsize / _setmode, _validdrive): ported on retail's layout (rl_crt_env.c; docs/findings/crt-port.md
 * §Gap ports). Canonical names rl_crt_<va>; every argument / pointer is a 32-bit retail value. */
#pragma once
#include <stdint.h>
