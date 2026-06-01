/*
 * OpenLords2 — strategic-map .pl8 sprite-bank loader.
 *
 * Port of retail FUN_004984dc @ 0x4984dc (the season loader): mallocs
 * and raw-reads the 8 strategic-map sprite banks for the current
 * season / view-mode, plus the two trailing single loads misc_cty.pl8
 * and vill_gd8.pl8.  .pl8 banks are NOT compressed — the file bytes are
 * read verbatim into a heap buffer and the iso-tile blitters
 * (engine_map_terrain.c / engine_map_render.c) interpret them directly
 * (see docs/findings/map-render.md).  The file-read leaf FUN_004af8d9
 * is already ported as io_read_file_at (io.c / io.h).
 *
 * Bank → retail global → renderer role (slot order):
 *   0 base    → DAT_00568208 → terrain blob 0 (also the sky blob)
 *   1 mtns    → DAT_0053e914 → terrain blob 1
 *   2 roads   → DAT_0057d344 → terrain blob 2
 *   3 town    → DAT_0055409c → terrain blob 3
 *   4 castle  → DAT_0056898c → terrain blob 4
 *   5 sprite1a→ DAT_00553224 → feature walker `primary`
 *   6 sprite1b→ DAT_0056d8bc → feature walker `alt`
 *   7 flags1a → DAT_0055ce5c → feature/overlay `overlay`
 *
 * Banks 0..4 are seasonal (1a/1b/1c/1d) and gain a view-mode-2 variant;
 * banks 5/6/7 are season-invariant.  The table (name + malloc size) is
 * generated from retail into src/gen/pl8_table.h by
 * tools/extract_pl8_table.py.
 */

#ifndef OPENLORDS2_ENGINE_PL8_H
#define OPENLORDS2_ENGINE_PL8_H

#include <stddef.h>
#include <stdint.h>

#define ENGINE_PL8_BANK_COUNT 8

/* Load the 8 strategic-map banks (+ misc_cty, vill_gd8) for the current
 * season (s_slot_0057c934) and view-mode (s_slot_0057cb18), mirroring
 * retail FUN_004984dc.  `dir` is prepended to each table filename
 * (NULL/"" → bare filename, retail's cwd-relative behavior — the live
 * drop-in anchors cwd at the game dir).  Returns 1 on full success, 0
 * on any malloc/read failure (retail's uVar11); a partial load is freed
 * before returning 0.  Re-loading frees the previous load first. */
int engine_pl8_load_strategic_banks(const char *dir);

/* Free everything the last successful (or partial) load allocated.
 * Idempotent. */
void engine_pl8_free_strategic_banks(void);

/* Bank pointer / valid content length by slot (0..7).  The length is
 * the bytes the read filled (the .pl8 file size) — the heap buffer is
 * malloc'd to the slightly larger retail-table ceiling, but only this
 * prefix is meaningful data.  NULL / 0 before a load or for an
 * out-of-range slot. */
const uint8_t *engine_pl8_bank(int slot);
size_t         engine_pl8_bank_size(int slot);

/* misc_cty.pl8 / vill_gd8.pl8 buffers (loaded after the 8 banks). */
const uint8_t *engine_pl8_misc_cty(void);
const uint8_t *engine_pl8_vill_gd8(void);

/* Retail DAT_0053f050 — set to 1 by FUN_004984dc once the strategic
 * sprite data is resident; the strategic-map screen-dispatch arm
 * (FUN_0040f5fd) gates its composition on it.  Mirrored here until that
 * arm is wired. */
int engine_pl8_data_ready(void);

#endif /* OPENLORDS2_ENGINE_PL8_H */
