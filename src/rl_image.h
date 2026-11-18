/* rl_image.h — GENERATED (tools/gen_image_defs.py): see rl_image.c. */
#pragma once
#include <stdint.h>

typedef struct { const char *name; uint32_t va, vsize, raw, ch; } rl_image_section_t;
/* name NULL: by ordinal */
typedef struct { uint32_t slot; const char *dll, *name; uint16_t ord, hint; } rl_import_t;

#define RL_IMAGE_BASE      0x00400000u
#define RL_IMAGE_SIZE      0x001e9000u   /* SizeOfImage */
#define RL_IMAGE_ENTRY     0x004c3370u   /* start: rl_crt_start is its port */
#define RL_IMAGE_SEC_ALIGN 0x1000u
#define RL_IMAGE_IAT_LO    0x005cf35cu   /* data directory 12: the IAT */
#define RL_IMAGE_IAT_HI    0x005cf604u
#define RL_IMAGE_NSECTIONS 6
#define RL_NIMPORTS        162

extern const rl_image_section_t rl_image_sections[RL_IMAGE_NSECTIONS];
extern const rl_import_t rl_imports[RL_NIMPORTS];
