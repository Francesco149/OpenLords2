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

/* Port of FUN_004a16f7 @ 0x4a16f7 (125 B, __cdecl int(uint,uint)).  Decide
 * whether army owner `aggressor` engages a stack/region owned by `defender`
 * when they collide on the strategic map.  Returns 1 (engage) when the defender
 * is neutral (0) or a genuinely different player, 0 when the defender is the
 * aggressor itself or the aggressor's own lord (+0x81).  In the lord case it
 * also bumps the per-lord contact counter (+0x86 + defender*0x10).  Pure: reads
 * only the aggressor's strategic record. */
int engine_diplomacy_should_engage(int aggressor, int defender);

/* Port of FUN_004a1847 @ 0x4a1847 (525 B, __cdecl void).  Per-turn lord/vassal
 * treaty reconciliation over players 1..5 (a phase-7 turn-rollover callee of the
 * strategic turn driver 0x49a010).  Wipes the treaty matrix (+0x85, stride 0x10)
 * then re-derives it from each player's declared lord (+0x81): mutual,
 * live-lord bonds are re-pinned on both ends (matrix set both ways); one-sided
 * or dead-lord bonds are severed.  An inactive (+0x04 == 0) or just-severed
 * player is marked "settled" so no later player in the pass can bond to it.
 * Pure: touches only g_player_strategic_352. */
void engine_diplomacy_reconcile_treaties(void);

#endif /* OPENLORDS2_ENGINE_DIPLOMACY_H */
