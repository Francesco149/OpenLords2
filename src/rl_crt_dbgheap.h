/* rl_crt_dbgheap.h — retail's debug CRT, the debug heap's check / realloc / leak-dump paths and the sbh internals they reach (_CrtCheckMemory, _realloc_dbg, _CrtDumpMemoryLeaks, __sbh_decommit_pages / resize / heap_check, _callnewh): ported on retail's layout (rl_crt_dbgheap.c; docs/findings/crt-port.md
 * §Gap ports). Canonical names rl_crt_<va>; every argument / pointer is a 32-bit retail value. */
#pragma once
#include <stdint.h>

/* dbgheap.c */
uint32_t rl_crt_4bff40(uint32_t p, uint32_t size, int use, uint32_t file, int line);            /* _realloc_dbg */
uint32_t rl_crt_4bff80(uint32_t p, uint32_t size, int use, uint32_t file, int line, int frealloc); /* realloc_help */
int      rl_crt_4c0cf0(void);                                                                  /* _CrtCheckMemory */
void     rl_crt_4c1370(uint32_t state);                                                        /* _CrtMemCheckpoint */
void     rl_crt_4c15f0(uint32_t state);                                                        /* _CrtMemDumpAllObjectsSince */
void     rl_crt_4c18b0(uint32_t hdr);                                                          /* _printMemBlockData */
int      rl_crt_4c19b0(void);                                                                  /* _CrtDumpMemoryLeaks */
/* the base heap (malloc.c / realloc.c / expand.c / heapchk.c / handler.cpp) */
uint32_t rl_crt_4c3220(uint32_t d, uint32_t s, uint32_t n);                                    /* memcpy (memmove semantics) */
int      rl_crt_4c3650(uint32_t size);                                                         /* _callnewh */
uint32_t rl_crt_4c3690(uint32_t size);                                                         /* _malloc_base */
uint32_t rl_crt_4c36c0(uint32_t size, int nh);                                                 /* _nh_malloc_base */
uint32_t rl_crt_4c4090(uint32_t p, uint32_t n);                                                /* _expand_base */
uint32_t rl_crt_4c4160(uint32_t p, uint32_t n);                                                /* _realloc_base */
int      rl_crt_4c43e0(void);                                                                  /* _heapchk */
/* sbheap.c */
void     rl_crt_4c47c0(uint32_t hdr);                                                          /* __sbh_release_region */
void     rl_crt_4c4850(int n);                                                                 /* __sbh_decommit_pages */
int      rl_crt_4c52e0(uint32_t hdr, uint32_t page, uint32_t map, uint32_t paras);             /* __sbh_resize_block */
int      rl_crt_4c54a0(void);                                                                  /* __sbh_heap_check */
