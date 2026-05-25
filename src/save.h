/*
 * OpenLords2 — save-file I/O + schema table.
 *
 * Port of retail's persistence layer:
 *
 *   FUN_004ade93 @ 0x4ade93  → save_write
 *   FUN_004adfe6 @ 0x4adfe6  → save_load
 *   FUN_004ae8a9 @ 0x4ae8a9  → castles_init_blank
 *   FUN_004ae968 @ 0x4ae968  → castles_write_scenario
 *   FUN_004aea70 @ 0x4aea70  → castles_read_scenario
 *
 * See `docs/findings/save-format.md` for the full schema map.  TL;DR:
 *
 *   .sav file (471828 bytes) =
 *     267028 B  — 225 (ptr, size) entries from g_save_schema[]
 *   + 204800 B  — 16 × 12800 B castle blocks (verbatim Castles.dat)
 *
 * The schema is **strictly positional** — no headers, no field tags,
 * no versioning.  Round-trip fidelity = "same bytes in the same order".
 */

#ifndef OPENLORDS2_SAVE_H
#define OPENLORDS2_SAVE_H

#include <stddef.h>
#include <stdint.h>

/* One entry in the schema table.  Matches retail's `(void *, u32)` pair
 * at `DAT_004de960`. */
typedef struct save_entry {
    void    *ptr;
    uint32_t size;
} save_entry_t;

#define SAVE_SCHEMA_COUNT   225          /* entries walked by retail's loop (limit 0xe1) */
#define SAVE_SCHEMA_BYTES   267028       /* sum of all entry sizes */

#define CASTLES_BLOCK_BYTES 12800        /* 0x3200 — one scenario's worth */
#define CASTLES_BLOCK_COUNT 16           /* 16 scenarios per Castles.dat */
#define CASTLES_FILE_BYTES  (CASTLES_BLOCK_BYTES * CASTLES_BLOCK_COUNT)

#define SAVE_FILE_BYTES     (SAVE_SCHEMA_BYTES + CASTLES_FILE_BYTES)  /* 471828 */

extern const save_entry_t g_save_schema[SAVE_SCHEMA_COUNT];

/* Named big-block storage (entries 0..12).  See save-format.md for the
 * provisional meanings of each block.  The smaller (4-byte..1360-byte)
 * scalar tail (entries 13..224) lives in save_schema_table.h as
 * file-static storage — we'll split those into named globals as we
 * port subsystems that touch them. */

extern uint8_t g_overworld_map[32768];          /* entry 0  retail 0x522f90  (128×128 hex tiles?) */
extern uint8_t g_castles_state[51200];          /* entry 1  retail 0x5440e0  (6400 × 8 B castle records — live scenario) */
extern uint8_t g_player_aux_44[264];            /* entry 2  retail 0x553d50  (6 × 44 B per-player aux) */
extern uint8_t g_player_strategic_352[2112];    /* entry 3  retail 0x57bf00  (6 × 352 B per-player strategic state) */
extern uint8_t g_per_army_768[13056];           /* entry 4  retail 0x53f9b0  (17 × 768 B — 16 used + slot 0) */
extern uint8_t g_per_territory_420[63420];      /* entry 5  retail 0x52f0b0  (151 × 420 B — 150 territories + slot 0) */
extern uint8_t g_unit_table_20[260];            /* entry 6  retail 0x568dc0  (13 × 20 B — unit/army types?) */
extern uint8_t g_per_battle_hex_432[34992];     /* entry 7  retail 0x554480  (81 × 432 B — 80 battle hexes + slot 0) */
extern uint8_t g_per_combat_unit_76[7676];      /* entry 8  retail 0x57a100  (101 × 76 B — 100 combat units + slot 0) */
extern uint8_t g_per_battle_hex_aux_52[4212];   /* entry 9  retail 0x566520  (81 × 52 B — 80 battle hexes + slot 0) */
extern uint8_t g_castles_shadow[51200];         /* entry 10 retail 0x56d8c0  (parallel to entry 1 — previous-turn / AI shadow?) */
extern uint8_t g_misc_128[128];                 /* entry 11 retail 0x568480  (unknown 128 B) */
extern uint8_t g_misc_1360[1360];               /* entry 12 retail 0x53ea00  (unknown 1360 B) */

/* Castles.dat scratch (12800 B, retail DAT_004eb2b0).  Used by the
 * .sav copy loops and by the per-scenario read/write helpers. */
extern uint8_t g_castles_scratch[CASTLES_BLOCK_BYTES];

/* Runtime tile scratches.  NOT persisted to .sav (not in the schema);
 * recomputed on demand by engine_tile_refresh_path_scratch /
 * engine_tile_refresh_stage_scratch (and as the tail of the castle-load
 * footprint expansion).  Sized 6400 cells = 80×80 grid.
 *
 *   g_path_scratch   ← retail DAT_00507250  (6400 × u16  = 12800 B)
 *   g_stage_scratch  ← retail DAT_004f7d80  (6400 × u8   =  6400 B)
 */
extern uint16_t g_path_scratch [6400];
extern uint8_t  g_stage_scratch[6400];

/* Recent-footprint list + overflow slot.  Persisted as save schema
 * entry 110 (160 B at retail 0x00553c80).  Layout — preserved verbatim
 * so the .sav byte-stream is unchanged — is:
 *
 *   [  0 .. 152)  g_footprint_list   — 19 × (int32, int32) pairs.
 *                                       Only [i][0] is consumed by the
 *                                       sparse insert; [i][1] is opaque
 *                                       to the engine_tile helpers but
 *                                       carries through round-trip.
 *   [152 .. 156)  g_last_footprint_tile — overflow fallback when all 19
 *                                       list slots are full.
 *   [156 .. 160)  4 trailing bytes      — unread/unwritten by any
 *                                       observed code path, but in the
 *                                       persisted slot so they survive.
 *
 * Storage lives in save.c as a single 160-byte buffer named so that
 * gen_save_schema.py wires it into schema slot 110; the typed names are
 * macros that overlay structured fields onto that buffer.
 */
extern uint8_t g_footprint_storage_160[160];
#define g_footprint_list       ((int32_t (*)[2])(&g_footprint_storage_160[  0]))
#define g_last_footprint_tile  (*(int32_t *     )(&g_footprint_storage_160[152]))

/* All four return 0 on success, non-zero on failure (matches retail's
 * `return 1;` convention for the save/load functions).
 *
 * `castles_path` is the path to a Castles.dat file.  Retail opens
 * "Castles.dat" in the CWD; callers can supply any path here.
 *
 * Side-effect calls that retail makes after save/load
 * (FUN_004020ae, FUN_0045270a, FUN_004976a1) are NOT invoked — they
 * touch subsystems we haven't ported yet (AI scheduler, message-bus
 * resets).  Higher-level wrappers will reintroduce them once those
 * subsystems exist. */

int save_write(const char *sav_path, const char *castles_path);
int save_load (const char *sav_path, const char *castles_path);

int castles_init_blank    (const char *castles_path);
int castles_write_scenario(const char *castles_path, int scenario_idx);
int castles_read_scenario (const char *castles_path, int scenario_idx);

#endif /* OPENLORDS2_SAVE_H */
