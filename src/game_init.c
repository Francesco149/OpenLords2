/*
 * OpenLords2 — new-game / world-state initialiser (0x497ced cluster).
 * See game_init.h for the phase plan.
 */

#include <stdint.h>
#include <string.h>

#include "game_init.h"
#include "save.h"
#include "save_schema_globals.h"  /* s_slot_<va> — the 17 calendar/world scalars */
#include "engine_turn.h"          /* g_sim_turn_progress, g_sim_turn_sync_baseline_ms */

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

/* Provisional home for retail DAT_0053f0c4 — see game_init.h. */
int g_anim_gate_53f0c4;

/*
 * Scalar-seed phase of 0x497ced — the 20 dword world-state globals
 * written between the NPC-schedule seed (FUN_0049caaa, ported) and the
 * first heavy seeder (0x4676e0, unported).  Writes are issued in retail
 * order; all
 * 20 target distinct addresses, so the order is provenance, not a
 * dependency.
 *
 * Three storage classes:
 *   - 17 are save-schema scalars → their shared s_slot_<va> homes.
 *   - 2 are the turn-handshake runtime globals owned by engine_turn.c.
 *   - 1 is the render/anim gate counter parked in this module.
 *
 * The retail asm derives several values from a base via inc/dec rather
 * than as independent constants — preserved here as expressions so the
 * structure stays legible.  Two parallel calendar triples:
 *
 *     year   base = 0x4f3 (1267)   prev = base-1 (1266)   next = base+1 (1268)
 *     season base = 3              prev = base-1 (2)      next = base+1 (4)
 *
 * plus a same-valued copy of each base (save entries 28/29).  The
 * season-next write has a retail clamp `if (next > 4) next = 1` (a 1..4
 * seasonal wrap) that is dead at the seeded value 4 — the decompiler
 * folds the unreachable block away; noted for provenance, not
 * reproduced.  Save-entry numbers in the trailing comments index the
 * contiguous calendar block (entries 20-31).
 */
void engine_game_init_seed_scalars(void)
{
    /* Turn-handshake runtime state (engine_turn.c) — reset to idle. */
    g_sim_turn_progress         = 0;            /* DAT_00553fc8 */
    g_sim_turn_sync_baseline_ms = 0;            /* _DAT_0056d658 */

    /* Render/anim gate counter (reader 0x41e1dd, unported). */
    g_anim_gate_53f0c4 = 0;                     /* DAT_0053f0c4 */

    s_slot_0055cd5c = 0;                        /* entry 30 */

    /* Calendar block — year triple + copy. */
    s_slot_00553edc = 0x4f3;                    /* entry 20: year base (1267) */
    s_slot_00553228 = s_slot_00553edc;          /* entry 28: year copy */
    s_slot_0057c900 = s_slot_00553edc - 1;      /* entry 21: year prev (1266) */
    s_slot_00553e74 = s_slot_00553edc + 1;      /* entry 22: year next (1268) */

    s_slot_00553240 = 0;                        /* entry 31 */
    s_slot_0056d6b0 = 0;                        /* entry 92 */

    /* Calendar block — season triple + copy. */
    s_slot_0057c934 = 3;                        /* entry 23: season base */
    s_slot_0057c990 = s_slot_0057c934;          /* entry 29: season copy */
    s_slot_00554470 = s_slot_0057c934 - 1;      /* entry 24: season prev (2) */
    s_slot_0057c92c = s_slot_0057c934 + 1;      /* entry 25: season next (4);
                                                 *   retail clamp >4→1 dead here */

    s_slot_0055cd40 = 0;                        /* entry 26 */
    s_slot_0057c918 = 0;                        /* entry 27 */
    s_slot_0057a0c4 = 0;                        /* entry 175 */
    s_slot_0053e920 = 0;                        /* entry 176 */
    s_slot_0056d5e0 = 0;                        /* entry 177 */
    s_slot_0055ce7c = 6;                        /* entry 190 */
}

/*
 * Leaf seeder FUN_0042847c — seed the 16-entry value table that lives at
 * save entry 82 (s_slot_005532a0[256], laid out as 16 records of 16 B).
 *
 * For each record i: dword at +0 ← k_value_table_defaults[i], dword at
 * +4 ← 0.  Bytes +8..+15 of every record are left untouched — retail
 * writes only the two dwords (the table-clear phase or the prior save
 * state owns the rest).
 *
 * Record `i` is later consumed via its first dword, indexed by the
 * current-selection cursor DAT_0056d8ac (render reader 0x41608b /
 * 0x428daf, getters 0x4a4a3f / 0x4a4b12 / 0x4a4c41).  The defaults come
 * from the retail .rdata table at DAT_004d8910 (16 dwords), extracted
 * byte-for-byte from the PE.
 *
 * Unlike the scalar-seed phase, this seeder has four retail callers
 * (0x497ced new-game, plus 0x43723a / 0x49a010 / 0x4a98fe), so it is a
 * reusable "reset table-82 to defaults" rather than a one-shot.
 */
static const uint32_t k_value_table_defaults[16] = {
    0x00, 0x02, 0x0c, 0x00,  0x01, 0x00, 0x01, 0x02,
    0x01, 0x0d, 0x10, 0x0a,  0x18, 0x17, 0x2c, 0x00,
};

void engine_game_init_seed_value_table(void)
{
    for (int i = 0; i < 0x10; i++) {            /* 16 records, stride 0x10 */
        uint8_t *rec = &s_slot_005532a0[i * 0x10];
        *(uint32_t *)(rec + 0) = k_value_table_defaults[i];  /* DAT_004d8910[i] */
        *(uint32_t *)(rec + 4) = 0;
    }
}
