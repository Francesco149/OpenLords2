/*
 * OpenLords2 — save-schema state snapshot for trace_studio.  See
 * state_dump.h.
 */

#include <stdio.h>
#include <string.h>

#include "save.h"
#include "state_dump.h"

uint32_t state_dump_fnv1a(const void *p, uint32_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

int state_dump_write(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return 1;
    fputs("{\"slots\":[", f);
    for (int i = 0; i < SAVE_SCHEMA_COUNT; i++) {
        const save_entry_t *e = &g_save_schema[i];
        if (i)
            fputc(',', f);
        if (e->size <= 4) {
            uint32_t v = 0;
            memcpy(&v, e->ptr, e->size);
            fprintf(f, "%lu", (unsigned long)v);
        } else {
            fprintf(f, "\"h:%08lx\"",
                    (unsigned long)state_dump_fnv1a(e->ptr, e->size));
        }
    }
    fputs("]}\n", f);
    fclose(f);
    return 0;
}
