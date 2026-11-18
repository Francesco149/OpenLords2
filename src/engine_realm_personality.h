/*
 * OpenLords2 — AI realms' banner colour / personality seat setup.
 *
 *   FUN_0049caaa @ 0x49caaa (935 B) → engine_realm_personality_init
 *
 * Runs once during strategic / new-game setup.  For each of the five
 * realms (seats 1..5) that is not yet in use (+0x05 == 0), up to the
 * opponent limit, it:
 *
 *   1. Hands the realm a free banner colour (1..5) — skipping colours a
 *      player with a fixed colour has already reserved.
 *   2. Picks that colour's first not-yet-taken AI personality (1..4:
 *      Knight, Baron, Countess, Bishop) from a per-colour preference
 *      table (one of four table variants in single-player), and records
 *      it.
 *   3. Copies the personality's localized 16-byte name ("The Knight")
 *      from L2.ENG section 7 into the realm's aux record.
 *
 * After all seats are walked it makes a finalize pass that writes each
 * realm's two colour-pair bytes (+0x08 the title roster's name text
 * colour, +0x09) from a per-colour table and seeds +0x29 (counties
 * owned) / +0x2a with 1.
 *
 * Lands ahead of its 13 retail call sites (strategic-setup /
 * new-game / load paths, e.g. 0x441649, 0x4427d1, 0x4978ad — all
 * unported), so today it is exercised only by the host suite — the
 * same disposition as the player-roster trio it sits beside.
 *
 * --- Storage --------------------------------------------------------
 *
 *   g_player_strategic_352  (save.h, retail 0x57bf00, 6 × 352 B)
 *       Per-realm strategic record, stride 0x160.  Fields touched:
 *         [+0x04] cleared to 0 when the opponent limit is hit
 *         [+0x05] in-use flag — 0 ⇒ this seat gets an AI setup
 *         [+0x07] AI personality 1..4  (retail DAT_0057bf07)
 *         [+0x08] colour-pair byte a   (retail DAT_0057bf08; name text colour)
 *         [+0x09] colour-pair byte b   (retail DAT_0057bf09)
 *         [+0x0a] banner colour 1..5   (retail DAT_0057bf0a)
 *         [+0x29] counties owned, seeded 1 (retail DAT_0057bf29)
 *         [+0x2a] seeded 1             (retail DAT_0057bf2a)
 *
 *   g_player_aux_44  (save.h, retail 0x553d50, 6 × 44 B)
 *       Per-realm aux record, stride 0x2c.  Fields touched:
 *         [+0x04..0x13] 16-byte localized personality name (copy target)
 *         [+0x25] fixed banner colour   (retail DAT_00553d75)
 *         [+0x26] (retail DAT_00553d76): 0 with a non-zero [+0x25]
 *                 ⇒ pre-reserve that colour
 *
 *   s_slot_0053f268  opponent limit  (schema entry 173, u32)
 *   s_slot_0055302c  variant select (schema entry 197, u32): == 1 ⇒
 *                    preference-table variant 0
 *   s_slot_0053f034  variant code (schema entry 123, u32);
 *                    `& 3` picks one of four preference-table variants
 *   g_sim_mp_active  retail DAT_00553030 — MP master switch (engine.h);
 *                    when set, the preference table degrades to variant 0
 *
 * --- Localization ---------------------------------------------------
 *
 * Retail reads the global walker DAT_005c9270 directly after seeking
 * (see localization.h §"Walker pointer").  Our port runs the seek with
 * a local localization_state_t over a blob supplied by
 * engine_realm_personality_set_localization() and copies 0x10 bytes from
 * `blob + walker_offset`.  When no blob has been set the copy is
 * skipped — retail always has L2.ENG resident (WinMain loads it before
 * any strategic setup), so this guard only matters pre-load; see
 * engine-quirks #107.
 *
 * Pure C; no Win32 deps.
 */

#ifndef OPENLORDS2_ENGINE_REALM_PERSONALITY_H
#define OPENLORDS2_ENGINE_REALM_PERSONALITY_H

#include <stddef.h>
#include <stdint.h>

#include "engine.h"               /* g_sim_mp_active */
#include "save.h"                 /* g_player_strategic_352, g_player_aux_44 */
#include "save_schema_globals.h"  /* s_slot_0053f268 / 0055302c / 0053f034 */

/* Point the personality-name copy at a loaded L2.ENG image.  The engine
 * bring-up wires this to config_localization_data() once a strategic
 * caller of engine_realm_personality_init lands; host tests set a crafted
 * blob directly.  `size` is currently advisory (the copy is a fixed
 * 0x10-byte read, bounds-checked only by the seek). */
void engine_realm_personality_set_localization(const uint8_t *blob, size_t size);

/* Port of FUN_0049caaa.  Assigns banner colours, AI personalities, names
 * and colour pairs to the not-yet-active realms (seats 1..5).  Reads and
 * writes the globals documented above. */
void engine_realm_personality_init(void);

#endif /* OPENLORDS2_ENGINE_REALM_PERSONALITY_H */
