/* rl_crt.h — retail's statically linked MSVC (4.x) DEBUG C runtime on retail's layout (rl_crt.c,
 * docs/findings/crt-port.md). Canonical names rl_crt_<va>; every argument / pointer is a 32-bit retail value (the CRT's
 * state lives in retail's .data / .bss, every OS call goes through retail's IAT slot). */
#pragma once
#include <stdint.h>

/* the debug heap (dbgheap.c) + the small-block heap (sbheap.c) */
uint32_t rl_crt_4bfa00(uint32_t size);                                              /* malloc */
uint32_t rl_crt_4bfa30(uint32_t size, int use, uint32_t file, int line);            /* _malloc_dbg */
uint32_t rl_crt_4bfa90(uint32_t size, int nh, int use, uint32_t file, int line);    /* _nh_malloc_dbg */
uint32_t rl_crt_4bfb30(uint32_t size, int use, uint32_t file, int line);            /* _heap_alloc_dbg */
uint32_t rl_crt_4bfea0(uint32_t n, uint32_t sz, int use, uint32_t file, int line);  /* _calloc_dbg */
uint32_t rl_crt_4c09f0(uint32_t p, int use);                                        /* _msize_dbg */
int      rl_crt_4c0c60(uint32_t p, int c, int n);                                   /* CheckBytes */
int      rl_crt_4c1070(int flag);                                                   /* _CrtSetDbgFlag */
int      rl_crt_4c1110(uint32_t p, uint32_t n, int rw);                             /* _CrtIsValidPointer */
int      rl_crt_4c1170(uint32_t p);                                                 /* _CrtIsValidHeapPointer */
uint32_t rl_crt_4c3760(uint32_t size);                                              /* _heap_alloc_base */
int      rl_crt_alloc_hook(uint32_t hook, int type, uint32_t data, uint32_t size, int use, int32_t req, uint32_t file,
                           int line);                                               /* call _pfnAllocHook */
int      rl_crt_4c4070(int type, uint32_t data, int size, int use, int req, uint32_t file, int line); /* default alloc hook */
void     rl_crt_4c4370(uint32_t p);                                                 /* _free_base */
int      rl_crt_4c4480(void);                                                       /* _heap_init */
uint32_t rl_crt_4c45b0(void);                                                       /* __sbh_new_region */
uint32_t rl_crt_4c49d0(uint32_t p, uint32_t *hdr, uint32_t *page);                  /* __sbh_find_block */
void     rl_crt_4c4a80(uint32_t hdr, uint32_t page, uint32_t map);                  /* __sbh_free_block */
uint32_t rl_crt_4c4b10(uint32_t paras);                                             /* __sbh_alloc_block */
uint32_t rl_crt_4c4fe0(uint32_t page, uint32_t free, uint32_t paras);               /* __sbh_alloc_block_from_page */
/* string / memory leaves */
uint32_t rl_crt_4bf620(uint32_t s);                                                 /* strlen */
uint32_t rl_crt_4c2080(uint32_t d, uint32_t s);                                     /* strcpy */
int      rl_crt_4c2170(uint32_t a, uint32_t b);                                     /* strcmp */
uint32_t rl_crt_4c2200(uint32_t d, int c, uint32_t n);                              /* memset */
/* low-level I/O (the fd table __pioinfo) */
int      rl_crt_4c9e50(void);                                                       /* _ioinit */
int      rl_crt_4cccc0(void);                                                       /* _alloc_osfhnd */
int      rl_crt_4cce10(uint32_t fd, uint32_t h);                                    /* _set_osfhnd */
int      rl_crt_4ccf10(uint32_t fd);                                                /* _free_osfhnd */
int      rl_crt_4cd030(uint32_t fd);                                                /* _get_osfhandle */
void     rl_crt_4c5930(uint32_t oserr);                                             /* _dosmaperr */
int      rl_crt_4cea00(uint32_t path, int oflag, int pmode);                        /* _open */
int      rl_crt_4cea50(uint32_t path, int oflag, int shflag, int pmode);            /* _sopen */
int      rl_crt_4ceff0(uint32_t fd, uint32_t buf, uint32_t n);                      /* _read */
int      rl_crt_4cc0b0(uint32_t fd, uint32_t buf, uint32_t n);                      /* _write */
int      rl_crt_4cc3b0(uint32_t fd, int off, int origin);                           /* _lseek */
int      rl_crt_4ce640(uint32_t fd);                                                /* _close */
int      rl_crt_4cf490(uint32_t fd);                                                /* _filelength */
int      rl_crt_4cf550(uint32_t path);                                              /* _chdir */
uint32_t rl_crt_4c1b10(uint32_t buf, int n);                                        /* _getcwd */
uint32_t rl_crt_4c1b40(int drive, uint32_t buf, int n);                             /* _getdcwd */
int      rl_crt_4c1cf0(void);                                                       /* _getdrive */
int      rl_crt_4c57f0(int c);                                                      /* toupper */
uint32_t rl_crt_4cf8f0(uint32_t c);                                                 /* _mbctoupper */
int      rl_crt_4c2260(uint32_t from, uint32_t to);                                 /* rename */
int      rl_crt_4c22c0(uint32_t path);                                              /* remove */
/* time */
int32_t  rl_crt_4c2740(uint32_t t);                                                 /* time */
int32_t  rl_crt_4c6530(int yr, int mo, int dy, int hr, int mn, int sc, int dst);    /* __loctotime_t */
void     rl_crt_4c5a30(void);                                                       /* __tzset (once) */
void     rl_crt_4c5a60(void);                                                       /* _tzset */
uint32_t rl_crt_4c24e0(uint32_t t);                                                 /* localtime */
int      rl_crt_4c5dc0(uint32_t tm);                                                /* _isindst */
int      rl_crt_4c6130(int start, int type, int yr, int mo, int week, int dow, int date, int hr, int mn, int sc,
                       int ms);                                                     /* cvtdate */
uint32_t rl_crt_4c6340(uint32_t t);                                                 /* gmtime */
uint32_t rl_crt_4c2340(uint32_t tm);                                                /* asctime */
uint32_t rl_crt_4c24a0(uint32_t p, int v);                                          /* store_dt */
uint32_t rl_crt_4cbfc0(uint32_t name);                                              /* getenv */
/* startup / exit */
void     rl_crt_4c3370(void);                                                       /* start (never returns) */
void     rl_crt_4bf6a0(void);                                                       /* _cinit */
void     rl_crt_4bf870(uint32_t first, uint32_t last);                              /* _initterm */
void     rl_crt_4bf6f0(int code);                                                   /* exit */
void     rl_crt_4bf770(int code, int quick, int retcaller);                         /* doexit */
int      rl_crt_4c8e90(void);                                                       /* __initmbctable */
int      rl_crt_4c8990(int cp);                                                     /* _setmbcp */
uint32_t rl_crt_4c8cc0(int cp);                                                     /* getSystemCP */
int      rl_crt_4c9260(uint32_t c);                                                 /* _ismbblead */
int      rl_crt_4c92f0(uint32_t c, int cmask, int mmask);                           /* x_ismbbtype */
uint32_t rl_crt_4c9360(void);                                                       /* _setenvp */
uint32_t rl_crt_4c94a0(void);                                                       /* _setargv */
uint32_t rl_crt_4c9570(uint32_t cmd, uint32_t argv, uint32_t args, uint32_t pargc, uint32_t pnchars); /* parse_cmdline */
uint32_t rl_crt_4c9bf0(void);                                                       /* __crtGetEnvironmentStringsA */
void     rl_crt_4cc5e0(void);                                                       /* __initstdio */
int      rl_crt_4cc740(void);                                                       /* __endstdio */
int      rl_crt_4cd270(uint32_t stream);                                            /* fflush */
int      rl_crt_4cd2f0(uint32_t stream);                                            /* _flush */
int      rl_crt_4cd3b0(void);                                                       /* _flushall */
int      rl_crt_4cd3d0(int mode);                                                   /* flsall */
uint32_t rl_crt_4cd4f0(uint32_t fn);                                                /* _onexit */
int      rl_crt_4cd5b0(uint32_t fn);                                                /* atexit */
uint32_t rl_crt_4cd5e0(void);                                                       /* __onexitinit */
uint32_t rl_crt_4cc930(void);                                                       /* the xc initializer → 0x4cc950 */
uint32_t rl_crt_4cc950(void);                                                       /* the C++ EH filter install */
void     rl_crt_4cca20(void);                                                       /* __CxxRestoreUnhandledExceptionFilter */
