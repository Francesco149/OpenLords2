/* rl_crt_dbg.h — retail's debug CRT, the report / message / exit / signal path: _CrtDbgReport and its window, _amsg_exit / _NMSG_WRITE, _exit / abort / raise, _flsbuf / _getbuf, the C++ unhandled-exception filter: ported on retail's layout (rl_crt_dbg.c; docs/findings/crt-port.md
 * §Gap ports). Canonical names rl_crt_<va>; every argument / pointer is a 32-bit retail value. */
#pragma once
#include <stdint.h>

/* the report path (dbgrpt.c) */
int      rl_crt_4c3950(int type, uint32_t file, int line, uint32_t mod, uint32_t fmt, uint32_t argp); /* _CrtDbgReport (argp: &the first variadic) */
int      rl_crt_dbgreport(int type, uint32_t file, int line, uint32_t mod, uint32_t fmt, ...);     /* … as C code calls it */
int      rl_crt_4c3d40(int type, uint32_t file, uint32_t linestr, uint32_t mod, uint32_t msg);     /* _CrtMessageWindow */
void     rl_crt_4c37d0(void);                                                       /* _CrtDbgBreak */
int      rl_crt_4cb010(uint32_t text, uint32_t caption, uint32_t type);             /* __crtMessageBoxA */
/* strings / numbers / stdio */
uint32_t rl_crt_4c2090(uint32_t d, uint32_t s);                                     /* strcat */
uint32_t rl_crt_4ca5d0(int v, uint32_t buf, int radix);                             /* _itoa */
uint32_t rl_crt_4ca630(uint32_t v, uint32_t buf, uint32_t radix, int neg);          /* xtoa */
int      rl_crt_4ca900(uint32_t buf, uint32_t n, uint32_t fmt, uint32_t argp);      /* _snprintf (argp: &format + 4) */
int      rl_crt_4ca9f0(uint32_t buf, uint32_t n, uint32_t fmt, uint32_t argp);      /* _vsnprintf */
int      rl_crt_4c6650(int ch, uint32_t f);                                         /* _flsbuf */
uint32_t rl_crt_4cc4d0(uint32_t f);                                                 /* _getbuf */
int      rl_crt_4cc590(int fd);                                                     /* _isatty */
/* runtime errors / exit / signals */
uint32_t rl_crt_4ca330(int num);                                                    /* _NMSG_WRITE */
void     rl_crt_4ca2d0(void);                                                       /* _FF_MSGBANNER */
void     rl_crt_4c35c0(int num);                                                    /* _amsg_exit (never returns) */
void     rl_crt_4bf710(int code);                                                   /* _exit (never returns) */
int      rl_crt_4cad80(int sig);                                                    /* raise */
uint32_t rl_crt_4cafa0(int sig);                                                    /* siglookup */
void     rl_crt_4ccb00(void);                                                       /* abort (never returns) */
void     rl_crt_4c8700(void);                                                       /* terminate (never returns) */
int      rl_crt_4ccac0(uint32_t fn);                                                /* _ValidateExecute */
int32_t  rl_crt_4cc980(uint32_t ep);                                                /* __CxxUnhandledExceptionFilter (retail: __stdcall) */
