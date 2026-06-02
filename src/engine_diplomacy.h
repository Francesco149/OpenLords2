/*
 * OpenLords2 — inter-player diplomacy.
 *
 * The diplomacy state lives in each player's strategic record
 * (g_player_strategic_352, stride 0x160): a lord/vassal id (+0x81), a per-other-
 * player relation sub-record array starting at +0x84 (stride 0x10: relation i8
 * at +0x84, treaty flag +0x85, war-escalation counter +0x87, war-declared flag
 * +0x88), a personality byte (+0x07), a feud target (+0xeb) and a notification
 * mood-cycle counter (+0x159).
 *
 * Ported functions:
 *   FUN_004a1a54 → engine_diplomacy_clear_treaty       (break a lord/vassal bond)
 *   FUN_004a24d1 → engine_diplomacy_broadcast_penalty  (penalise one player with all)
 *   FUN_004a1ee1 → engine_diplomacy_battle_adjust      (post-battle relation hit)
 *
 * engine_diplomacy_battle_adjust is reached from the combat aftermath
 * (FUN_004ab383): when an AI player loses (or wins against) a territory it
 * downgrades its relation toward the other side, breaking any treaty, possibly
 * declaring war, and posting the diplomacy notifications via the already-ported
 * event queue.  Only the strategic block is mutated; the notifications go to the
 * event-queue state threaded in (engine.c file-statics are not linked here).
 */

#ifndef OPENLORDS2_ENGINE_DIPLOMACY_H
#define OPENLORDS2_ENGINE_DIPLOMACY_H

#include "engine_event_queue.h"

/* Port of FUN_004a1a54 @ 0x4a1a54 (213 B, __cdecl void(uint,uint)).  Break a
 * lord/vassal bond in BOTH directions: for each side whose lord (+0x81) is the
 * other, clear that side's treaty flag toward the other (+0x85), its lord id
 * (+0x81) and its +0x1e flag. */
void engine_diplomacy_clear_treaty(int p1, int p2);

/* Port of FUN_004a24d1 @ 0x4a24d1 (179 B, __cdecl void(int,char)).  Subtract
 * `amount` from every other in-game realm's relation (+0x84) toward `target`,
 * flooring each at -30 (0xe2).  No upper clamp. */
void engine_diplomacy_broadcast_penalty(int target, int amount);

/* Port of FUN_004a1ee1 @ 0x4a1ee1 (1520 B, __cdecl void(uint,uint,char)).  Apply
 * a post-battle relation hit from AI player `p1` toward `p2` (delta `amount`).
 * No-op unless p1/p2 are valid distinct slots 1..5 and p1 is an active AI realm
 * (+0x04 set, +0x05 clear).  If p2 was p1's lord, break the treaty, flag war
 * (unless p1's personality is 4), penalise p2 with everyone, post the betrayal
 * notification (0xb6) and add 15 to the delta.  The relation toward p2 is then
 * reduced and clamped to [-30,+30]; if it bottoms out at -30 against a human p2
 * with no war yet flagged, an escalating war-declaration sequence fires
 * (0xbd → 0xbe → 0xbf).  Notifications are posted through `evq`. */
void engine_diplomacy_battle_adjust(int p1, int p2, int amount,
                                    engine_event_queue_state_t *evq);

#endif /* OPENLORDS2_ENGINE_DIPLOMACY_H */
