/*
 * OpenLords2 — per-realm strategic turn-state updates (the 0x448440
 * turn-advance engine's per-realm passes).  See engine_realm.c.
 *
 * These operate on the per-army/realm record block (g_per_army_768, save
 * entry 4, retail 0x53f9b0, 0x300-byte stride, slot 0 reserved) and the
 * per-player strategic block (g_player_strategic_352, entry 3, retail
 * 0x57bf00, 0x160 stride), both already named save globals — referenced
 * directly here (this module is one of their per-turn mutators), not bound.
 */

#ifndef OPENLORDS2_ENGINE_REALM_H
#define OPENLORDS2_ENGINE_REALM_H

/* Port of FUN_0044baea @ 0x44baea (693 B, __stdcall void) — the per-realm
 * approval-rating roll-forward, one of the per-turn passes the turn-advance
 * orchestrator (0x448440) runs.  For every realm 1..s_slot_0056d5dc:
 *
 *   - snapshot the current rating (+0x0c) into "previous" (+0x0d), then fold
 *     in this turn's three signed adjustment sources (+0x0e, +0x10, +0x11) —
 *     each added in turn as a signed byte (8-bit wraparound between steps, as
 *     retail writes the byte back each time);
 *   - mirror the three raw adjustments into the display fields (+0x12/+0x14/
 *     +0x13) and clear the per-turn deltas (+0x15, +0x17, +0x219, +0x194);
 *   - a zero-population realm with a non-human owner resets to 50 (+0x0c =
 *     +0x17 = 0x32); an UNOWNED realm below 75 nudges up by 5;
 *   - decay the separate +0x2f4 countdown by 5 (floor at the != 0 guard);
 *   - clamp the rating to 0..100 (signed);
 *   - accumulate it into the lifetime sum (+0x1c) and store the running
 *     average +0x18 = sum / s_slot_00553240 (the turn counter).
 *
 * PRECONDITION: s_slot_00553240 (the turn counter, save entry 31) must be
 * >= 1 — retail's caller 0x448440 increments it before invoking this pass, so
 * the +0x18 average division is never by zero.  Field semantics (rating /
 * adjustment sources) are provisional; ported byte-exact from the decompile,
 * r2-verified for the signed arithmetic and the divide. */
void engine_realm_advance_rating(void);

/* Port of FUN_0044bf04 @ 0x44bf04 (118 B, __stdcall void) — the per-realm
 * provisioning settle pass (another 0x448440 turn-engine pass).  For every
 * realm 1..s_slot_0056d5dc it re-runs engine_army_economy_provision at the
 * season-prev phase (s_slot_00554470), then snapshots the settled economy
 * fields +0x178 → +0x18c and +0x17c → +0x190 (the per-realm history mirrors).
 * No-ops on its economy callee when the army-record block is unbound; the two
 * field copies operate on g_per_army_768 directly. */
void engine_realm_settle_provisions(void);

#endif /* OPENLORDS2_ENGINE_REALM_H */
