/*
 * OpenLords2 — strategic-map .pl8 sprite-bank loader (FUN_004984dc).
 * See engine_pl8.h for the bank→global→role map and the data flow.
 */

#include "engine_pl8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen/pl8_table.h"
#include "io.h"
#include "save_schema_globals.h"   /* s_slot_0057c934 / s_slot_0057cb18 */

/* Heap banks, in slot order (see header).  g_bank[i] receives the file
 * loaded for table index i + offset; this is exactly the permutation
 * retail's pointer-assignment ladder spells out into 8 named globals. */
static uint8_t *g_bank[ENGINE_PL8_BANK_COUNT];
static size_t   g_bank_size[ENGINE_PL8_BANK_COUNT];
static uint8_t *g_misc_cty;
static uint8_t *g_vill_gd8;
static int      g_data_ready;       /* retail DAT_0053f050 */

/* Build "<dir>/<name>" (or bare "<name>" when dir is NULL/empty) into
 * `out` (caller-sized).  Returns out. */
static char *join_path(char *out, size_t cap, const char *dir,
                       const char *name)
{
    if (dir && dir[0]) {
        size_t dl = strlen(dir);
        int slash = (dl > 0 && (dir[dl - 1] == '/' || dir[dl - 1] == '\\'));
        snprintf(out, cap, "%s%s%s", dir, slash ? "" : "/", name);
    } else {
        snprintf(out, cap, "%s", name);
    }
    return out;
}

void engine_pl8_free_strategic_banks(void)
{
    for (int i = 0; i < ENGINE_PL8_BANK_COUNT; i++) {
        free(g_bank[i]);
        g_bank[i] = NULL;
        g_bank_size[i] = 0;
    }
    free(g_misc_cty);  g_misc_cty = NULL;
    free(g_vill_gd8);  g_vill_gd8 = NULL;
    g_data_ready = 0;
}

int engine_pl8_load_strategic_banks(const char *dir)
{
    char path[512];

    engine_pl8_free_strategic_banks();
    g_data_ready = 1;                          /* DAT_0053f050 = 1 */

    /* Seasonal / view-mode table offset (retail local_18). */
    int view_mode = (int)s_slot_0057cb18;
    int season    = (int)s_slot_0057c934;
    int off = (view_mode == 2) ? 0x20 : 0;
    if (season > 0 && season < 5)
        off += season * 8 - 8;

    /* The 8 banks. */
    for (int i = 0; i < ENGINE_PL8_BANK_COUNT; i++) {
        const Pl8TableEntry *e = &PL8_STRATEGIC_TABLE[i + off];
        size_t sz = e->size;
        if (sz == 0)
            continue;                          /* retail keeps prior ptr */
        uint8_t *buf = malloc(sz);
        if (!buf)
            goto fail;                         /* LAB_0049878e */
        join_path(path, sizeof path, dir, e->name);
        int got = io_read_file_at(path, buf, sz, 0);
        if (got == 0) {
            free(buf);
            goto fail;                         /* LAB_0049876a */
        }
        g_bank[i] = buf;
        /* Valid content length = bytes the read filled (the .pl8 file
         * size).  The malloc size `sz` is a retail-table ceiling that
         * over-allocates by a few dozen bytes; that tail is never read
         * by the iso-tile blitters and is uninitialized in retail too
         * (the captured blobs show 0xcd heap fill there). */
        g_bank_size[i] = (size_t)got;
    }

    /* misc_cty.pl8 → DAT_005530c8. */
    g_misc_cty = malloc(PL8_MISC_CTY_SIZE);
    if (!g_misc_cty)
        goto fail;
    join_path(path, sizeof path, dir, PL8_MISC_CTY_NAME);
    if (io_read_file_at(path, g_misc_cty, PL8_MISC_CTY_SIZE, 0) == 0)
        goto fail;

    /* vill_gd8.pl8 → &DAT_00542ce0, a fixed 0x12d8 read. */
    g_vill_gd8 = malloc(PL8_VILL_GD8_SIZE);
    if (!g_vill_gd8)
        goto fail;
    join_path(path, sizeof path, dir, PL8_VILL_GD8_NAME);
    if (io_read_file_at(path, g_vill_gd8, PL8_VILL_GD8_SIZE, 0) == 0)
        goto fail;

    return 1;

fail:
    engine_pl8_free_strategic_banks();
    return 0;
}

const uint8_t *engine_pl8_bank(int slot)
{
    if (slot < 0 || slot >= ENGINE_PL8_BANK_COUNT) return NULL;
    return g_bank[slot];
}

size_t engine_pl8_bank_size(int slot)
{
    if (slot < 0 || slot >= ENGINE_PL8_BANK_COUNT) return 0;
    return g_bank_size[slot];
}

const uint8_t *engine_pl8_misc_cty(void) { return g_misc_cty; }
const uint8_t *engine_pl8_vill_gd8(void) { return g_vill_gd8; }
int            engine_pl8_data_ready(void) { return g_data_ready; }
