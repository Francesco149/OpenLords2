/*
 * OpenLords2 — NPC worker schedule initialiser.
 *
 *   FUN_0049caaa @ 0x49caaa (935 B) → engine_npc_schedule_init
 *
 * Runs once during strategic / new-game setup.  For each of the five
 * AI players (seats 1..5) that has no human/owner record yet, it:
 *
 *   1. Hands the player a free "job slot" (1..5) — skipping slots a
 *      fixed-job player has already reserved.
 *   2. Picks that job's first un-taken "work location" from a per-job
 *      candidate table (the candidate ordering varies by game
 *      difficulty in single-player), and records it.
 *   3. Copies the job's localized 16-byte name from L2.ENG section 7
 *      into the player's aux record.
 *
 * After all seats are walked it makes a finalize pass that writes each
 * player's fixed work-position (x,y) from a per-job position table and
 * sets two "schedule valid" flags.
 *
 * Lands ahead of its 13 retail call sites (strategic-setup /
 * new-game / load paths, e.g. 0x441649, 0x4427d1, 0x4978ad — all
 * unported), so today it is exercised only by the host suite — the
 * same disposition as the player-roster trio it sits beside.
 *
 * --- Storage --------------------------------------------------------
 *
 *   g_player_strategic_352  (save.h, retail 0x57bf00, 6 × 352 B)
 *       Per-player strategic record, stride 0x160.  Fields touched:
 *         [+0x04] cleared to 0 when the worker-count limit is hit
 *         [+0x05] owner/human flag — 0 ⇒ this seat is an unassigned NPC
 *         [+0x07] assigned work-location id (retail DAT_0057bf07)
 *         [+0x08] work-position x       (retail DAT_0057bf08)
 *         [+0x09] work-position y       (retail DAT_0057bf09)
 *         [+0x0a] assigned job-slot id  (retail DAT_0057bf0a)
 *         [+0x29] schedule-valid flag   (retail DAT_0057bf29)
 *         [+0x2a] schedule-valid flag   (retail DAT_0057bf2a)
 *
 *   g_player_aux_44  (save.h, retail 0x553d50, 6 × 44 B)
 *       Per-player aux record, stride 0x2c.  Fields touched:
 *         [+0x04..0x13] 16-byte localized job-name (memcpy target)
 *         [+0x25] fixed job-slot id      (retail DAT_00553d75)
 *         [+0x26] "has fixed job" flag   (retail DAT_00553d76);
 *                 0 + non-zero [+0x25] ⇒ pre-reserve that job slot
 *
 *   s_slot_0053f268  worker-count limit  (schema entry 173, u32)
 *   s_slot_0055302c  difficulty/variant select (schema entry 197, u32)
 *   s_slot_0053f034  difficulty variant code (schema entry 123, u32);
 *                    `& 3` picks one of four candidate-table variants
 *   g_sim_mp_active  retail DAT_00553030 — MP master switch (engine.h);
 *                    when set, the candidate table degrades to variant 0
 *
 * --- Localization ---------------------------------------------------
 *
 * Retail reads the global walker DAT_005c9270 directly after seeking
 * (see localization.h §"Walker pointer").  Our port runs the seek with
 * a local localization_state_t over a blob supplied by
 * engine_npc_schedule_set_localization() and copies 0x10 bytes from
 * `blob + walker_offset`.  When no blob has been set the copy is
 * skipped — retail always has L2.ENG resident (WinMain loads it before
 * any strategic setup), so this guard only matters pre-load; see
 * engine-quirks #107.
 *
 * Pure C; no Win32 deps.
 */

#ifndef OPENLORDS2_ENGINE_NPC_SCHEDULE_H
#define OPENLORDS2_ENGINE_NPC_SCHEDULE_H

#include <stddef.h>
#include <stdint.h>

#include "engine.h"               /* g_sim_mp_active */
#include "save.h"                 /* g_player_strategic_352, g_player_aux_44 */
#include "save_schema_globals.h"  /* s_slot_0053f268 / 0055302c / 0053f034 */

/* Point the per-NPC name copy at a loaded L2.ENG image.  The engine
 * bring-up wires this to config_localization_data() once a strategic
 * caller of engine_npc_schedule_init lands; host tests set a crafted
 * blob directly.  `size` is currently advisory (the copy is a fixed
 * 0x10-byte read, bounds-checked only by the seek). */
void engine_npc_schedule_set_localization(const uint8_t *blob, size_t size);

/* Port of FUN_0049caaa.  Assigns job slots, work locations, names and
 * work-positions to the unassigned AI players (seats 1..5).  Reads and
 * writes the globals documented above. */
void engine_npc_schedule_init(void);

#endif /* OPENLORDS2_ENGINE_NPC_SCHEDULE_H */
