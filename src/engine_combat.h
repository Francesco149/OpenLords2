/*
 * OpenLords2 — strategic-map army combat resolution.
 *
 * When two army stacks meet on the overworld, the engine resolves the fight
 * with a deterministic (no-RNG) casualty model and then hands off to the
 * battle animation / aftermath passes.  This module ports the resolution
 * CORE — the part that mutates the per-territory occupant records — leaving
 * the audio (ff_batl/ff_lose WAVs), the battle render (FUN_004ab383) and the
 * territory-takeover aftermath as separate, human-verification-gated chips.
 *
 * Combatants are per-territory occupant ("army") records in
 * g_per_territory_420 (stride 0x1a4, the same block the upkeep/economy passes
 * drive).  Each record carries 7 troop-class counts (+0x16c, i16 each) plus an
 * optional mercenary contingent (+0x195 class / +0x196 count / +0x197 pool
 * slot — the same merc-contract triple engine_realm_upkeep.c manages).
 *
 * Ported functions:
 *   FUN_004aa89f → engine_combat_clear_battle_result   (zero 4 result shorts)
 *   FUN_00422c12 → engine_combat_load_muster           (copy counts to scratch)
 *   FUN_004ab2aa → engine_combat_army_strength         (weighted strength)
 *   FUN_004aad07 → engine_combat_resolve_casualties    (the resolver)
 *
 * FUN_004ac6fe (merc-contract clear on annihilation) is already ported as
 * engine_unit_clear_contract in engine_realm_upkeep.c — the resolver reuses it.
 */

#ifndef OPENLORDS2_ENGINE_COMBAT_H
#define OPENLORDS2_ENGINE_COMBAT_H

#include <stdint.h>

/* Per-combatant muster scratch — the 7 troop-class counts copied out of each
 * combatant's record for the battle UI.  Retail keeps these at 0x568420
 * (attacker) and 0x56843c (defender).  Owned by this module; written by
 * engine_combat_load_muster, read only by the (not-yet-ported) battle UI. */
extern int32_t g_combat_muster_a[7];   /* retail 0x568420 — attacker */
extern int32_t g_combat_muster_b[7];   /* retail 0x56843c — defender */

/* Result of a resolved fight (retail writes these to scalar globals). */
typedef struct {
    int winner;        /* retail DAT_0057c924 — winning territory index   */
    int winner_owner;  /* retail DAT_005440cc — winner's owner id (+0x00) */
} engine_combat_result_t;

/* Port of FUN_004aa89f.  Zero the 4 battle-result accumulator shorts
 * (+0x17a..+0x180) of one combatant record. */
void engine_combat_clear_battle_result(int rec);

/* Port of FUN_00422c12.  Copy each combatant's 7 troop-class counts (+0x16c)
 * into the muster scratch arrays, folding the merc contingent (+0x196) into
 * its active class (+0x195). */
void engine_combat_load_muster(int attacker, int defender);

/* Port of FUN_004ab2aa.  Weighted army strength: sum(count[c] * weight[c])
 * over the 7 classes plus the merc contingent, then floored at 1 — or, when
 * positive, bumped by a flat +0x14 (20) "presence" bonus. */
int engine_combat_army_strength(int rec);

/* Port of FUN_004aad07.  Resolve a fight between two combatant records.  The
 * side with the larger weighted strength wins; the loser is annihilated (all
 * troop counts and the merc contingent zeroed, merc contract cleared); the
 * winner keeps a casualty-table fraction of its troops, the fraction rising
 * with the strength ratio (a close fight is pyrrhic, a blowout is unscathed).
 *
 *   attacker, defender — combatant territory indices (retail DAT_00568210/18)
 *   difficulty         — AI difficulty 0..4 (retail DAT_0055401c); only used
 *                        when quick_resolve != 0 (multiplies the DEFENDER's
 *                        strength by 160/200/250/320/400 %)
 *   quick_resolve      — retail DAT_0053e8fc; the interactive path passes 0
 *   out               — winner index + owner (may be NULL)
 */
void engine_combat_resolve_casualties(int attacker, int defender,
                                      int difficulty, int quick_resolve,
                                      engine_combat_result_t *out);

#endif /* OPENLORDS2_ENGINE_COMBAT_H */
