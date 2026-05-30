/*
 * OpenLords2 — new-game / world-state initialiser (0x497ced cluster).
 * See game_init.h for the phase plan.
 */

#include <string.h>

#include "game_init.h"
#include "save.h"

/* Fixed-length record zeroer — retail FUN_0046ea28 (len 0x300),
 * FUN_0046eb09 (len 0x14) and FUN_0046ead0 (len 0x1a4) are three
 * instances of this same `memset(base, 0, len)` leaf, one per block
 * stride.  Each retail clearer below calls its matching length. */
static void clear_record(void *base, size_t len)
{
    memset(base, 0, len);
}

/* FUN_0046f1dc — clear the 17 per-army records (stride 0x300). */
static void clear_per_army(void)
{
    for (int i = 0; i < 0x11; i++)               /* i < 17 */
        clear_record(&g_per_army_768[i * 0x300], 0x300);  /* FUN_0046ea28 */
}

/* FUN_0046f2ae — clear the 13 unit-type records (stride 0x14). */
static void clear_unit_table(void)
{
    for (int i = 0; i < 0xd; i++)                /* i < 13 */
        clear_record(&g_unit_table_20[i * 0x14], 0x14);   /* FUN_0046eb09 */
}

/* FUN_0046f3cd — clear the 151 per-territory records (stride 0x1a4). */
static void clear_territories(void)
{
    for (int i = 0; i < 0x97; i++)               /* i < 151 */
        clear_record(&g_per_territory_420[i * 0x1a4], 0x1a4); /* FUN_0046ead0 */
}

/* Table-clear phase of 0x497ced — the first three calls of the
 * new-game initialiser, in retail order (territories, unit-table,
 * per-army).  NB: 0x497ced issues them as
 *   FUN_0046f3cd(); FUN_0046f2ae(); FUN_0046f1dc();
 * so the territory block is cleared first. */
void engine_game_init_clear_tables(void)
{
    clear_territories();   /* FUN_0046f3cd */
    clear_unit_table();    /* FUN_0046f2ae */
    clear_per_army();      /* FUN_0046f1dc */
}
