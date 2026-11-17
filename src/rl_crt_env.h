/* rl_crt_env.h — retail's debug CRT, the locale / environment / wide-char / file paths (_isctype, __crtLCMapStringA, CPtoLCID / setSBCS, atol, wcstombs / wctomb, __crtsetenv / __wtomb_environ, _fcloseall / fclose, _commit, _chsize / _setmode, _validdrive): ported on retail's layout (rl_crt_env.c; docs/findings/crt-gap-env.md).
 * Canonical names rl_crt_<va>; every argument / pointer is a 32-bit retail value. */
#pragma once
#include <stdint.h>

/* ctype / mbctype */
int      rl_crt_4c1c80(uint32_t drive);                                             /* _validdrive */
int      rl_crt_4c5710(int c, int type);                                            /* _isctype */
int      rl_crt_4c1df0(uint32_t s);                                                 /* atol */
int      rl_crt_4c8d50(int cp);                                                     /* CPtoLCID */
void     rl_crt_4c8df0(void);                                                       /* setSBCS */
int      rl_crt_4ccb80(uint32_t s1, uint32_t s2, uint32_t n);                       /* _mbsnbicoll */
uint32_t rl_crt_4ce7f0(uint32_t s, int c);                                          /* strchr */
uint32_t rl_crt_4ce550(uint32_t s, uint32_t c);                                     /* _mbschr */
/* NLS wrappers */
int      rl_crt_4cb350(uint32_t type, uint32_t src, int n, uint32_t ctype, uint32_t cp, uint32_t lcid); /* __crtGetStringTypeA */
int      rl_crt_4cbb80(uint32_t s, int n);                                          /* strncnt (aw_map.c) */
int      rl_crt_4cb860(uint32_t lcid, uint32_t flags, uint32_t src, int n, uint32_t dst, int ndst, uint32_t cp); /* __crtLCMapStringA */
int      rl_crt_4cde00(uint32_t s, int n);                                          /* strncnt (aw_cmp.c) */
int      rl_crt_4cd9a0(uint32_t lcid, uint32_t flags, uint32_t s1, int n1, uint32_t s2, int n2, uint32_t cp); /* __crtCompareStringA */
/* wide characters */
uint32_t rl_crt_4ccb30(uint32_t s);                                                 /* wcslen */
uint32_t rl_crt_4cbf50(uint32_t s, uint32_t n);                                     /* wcsncnt */
int      rl_crt_4cc770(uint32_t dst, uint32_t wc);                                  /* wctomb */
uint32_t rl_crt_4cbbf0(uint32_t dst, uint32_t src, uint32_t n);                     /* wcstombs */
/* the environment */
uint32_t rl_crt_4ce280(uint32_t env);                                               /* copy_environ */
int      rl_crt_4ce1e0(uint32_t name, uint32_t n);                                  /* findenv */
int      rl_crt_4cde70(uint32_t str, int primary);                                  /* __crtsetenv */
int      rl_crt_4ccbf0(void);                                                       /* __wtomb_environ */
/* streams and fds */
void     rl_crt_4ce750(uint32_t f);                                                 /* _freebuf */
int      rl_crt_4ce380(uint32_t f);                                                 /* fclose */
int      rl_crt_4cd1b0(void);                                                       /* _fcloseall */
int      rl_crt_4ce470(uint32_t fd);                                                /* _commit */
int      rl_crt_4cf9c0(uint32_t fd, int mode);                                      /* _setmode */
int      rl_crt_4cf640(uint32_t fd, int size);                                      /* _chsize */
