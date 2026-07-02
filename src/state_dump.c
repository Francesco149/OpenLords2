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
    /* Per-block sub-hashes — 16 equal ranges per block entry (last range
     * absorbs the remainder), keyed by slot index.  Lets the studio's
     * state diff localize a block divergence to a byte range instead of
     * a whole-block hash mismatch.  The agent's stateDumpOnce emits the
     * identical structure. */
    fputs("],\"sub\":{", f);
    int first = 1;
    for (int i = 0; i < SAVE_SCHEMA_COUNT; i++) {
        const save_entry_t *e = &g_save_schema[i];
        if (e->size <= 4)
            continue;
        if (!first)
            fputc(',', f);
        first = 0;
        fprintf(f, "\"%d\":[", i);
        uint32_t step = e->size / 16;
        for (int r = 0; r < 16; r++) {
            uint32_t off = (uint32_t)r * step;
            uint32_t len = (r == 15) ? e->size - off : step;
            if (r)
                fputc(',', f);
            fprintf(f, "\"h:%08lx\"",
                    (unsigned long)state_dump_fnv1a(
                        (const unsigned char *)e->ptr + off, len));
        }
        fputc(']', f);
    }
    fputc('}', f);
    /* Per-8-byte-lane sub-hashes for cell-structured blocks (the overworld
     * map g_overworld_map is 8 B/cell): lane L hashes bytes L, L+8, L+16, …
     * so a divergence localizes to a cell FIELD (+0 terrain, +1 flag, +5
     * army-occupancy head-link, …) instead of a byte range.  Emitted for
     * every block whose size is a multiple of 8 (only truly meaningful for
     * the 8 B/cell map, harmless noise elsewhere).  The agent's
     * stateDumpOnce emits the identical structure. */
    fputs(",\"lane8\":{", f);
    first = 1;
    for (int i = 0; i < SAVE_SCHEMA_COUNT; i++) {
        const save_entry_t *e = &g_save_schema[i];
        if (e->size <= 4 || (e->size % 8) != 0)
            continue;
        if (!first)
            fputc(',', f);
        first = 0;
        fprintf(f, "\"%d\":[", i);
        const unsigned char *b = (const unsigned char *)e->ptr;
        for (int lane = 0; lane < 8; lane++) {
            uint32_t h = 2166136261u;
            for (uint32_t o = (uint32_t)lane; o < e->size; o += 8) {
                h ^= b[o];
                h *= 16777619u;
            }
            if (lane)
                fputc(',', f);
            fprintf(f, "\"h:%08lx\"", (unsigned long)h);
        }
        fputc(']', f);
    }
    fputc('}', f);
    /* Per-BIT hashes of the overworld-map cell field +2 (a per-cell render/
     * autotile flags byte: 0x1c = border-autotile code, 0x20 = render-dirty,
     * 0x40/0x80 = edge flags).  Session-24 triage: field +2 is the persistent
     * slot-0 divergence; bit b hashes ((cell[2]>>b)&1) across every cell, so a
     * mismatch localizes to a SINGLE flag bit — distinguishing a benign
     * render-dirty (0x20) or RNG special-tile divergence from a real autotile
     * (0x1c) bug.  Emitted for every 8 B/cell block (only meaningful for the
     * map).  The agent's stateDumpOnce emits the identical structure. */
    fputs(",\"f2bits\":{", f);
    first = 1;
    for (int i = 0; i < SAVE_SCHEMA_COUNT; i++) {
        const save_entry_t *e = &g_save_schema[i];
        if (e->size <= 4 || (e->size % 8) != 0)
            continue;
        if (!first)
            fputc(',', f);
        first = 0;
        fprintf(f, "\"%d\":[", i);
        const unsigned char *b = (const unsigned char *)e->ptr;
        for (int bit = 0; bit < 8; bit++) {
            uint32_t h = 2166136261u;
            for (uint32_t o = 2; o < e->size; o += 8) {
                h ^= (uint32_t)((b[o] >> bit) & 1u);
                h *= 16777619u;
            }
            if (bit)
                fputc(',', f);
            fprintf(f, "\"h:%08lx\"", (unsigned long)h);
        }
        fputc(']', f);
    }
    fputs("}}\n", f);
    fclose(f);
    return 0;
}
