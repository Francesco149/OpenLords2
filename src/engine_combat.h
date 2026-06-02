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
 *   FUN_004a6a30 → engine_combat_size_battle           (battle sizing/setup)
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

/* Battle-setup outputs filled by engine_combat_size_battle for the
 * not-yet-ported battlefield UI / render (retail combat-UI scalars). */
extern int g_combat_both_garrison;  /* DAT_004ee89c — both combatants garrisons */
extern int g_combat_local_role;     /* DAT_005533d0 — 0 none/1 local attacks/2 defends */
extern int g_combat_ctrl_408;       /* DAT_00554408 — 2 when the local player attacks */
extern int g_combat_ctrl_5b0;       /* DAT_0056d5b0 — 2 when the local player attacks */
extern int g_combat_size_a;         /* DAT_00568934 — attacker battle size (≥1, 6 if 0)*/
extern int g_combat_size_b;         /* DAT_00568938 — defender battle size */
extern int g_combat_owner_a;        /* DAT_005681ec — attacker owner (signed) */
extern int g_combat_owner_b;        /* DAT_005681d8 — defender owner (signed) */

/* Bind the optional battle-cue hook (retail FUN_00427990 ff_batl.wav); NULL
 * (the default) skips the cue.  The cue-playback path is deferred. */
void engine_combat_bind_battle_sfx(void (*fn)(void));

/* Port of FUN_004a6a30 @ 0x4a6a30 (568 B) — battle sizing / setup, run by the
 * combat dispatch FUN_004a7158 before a fight is resolved or shown.  Reads the
 * two combatant records' garrison flag (+0x01), troop count (+0x02) and owner
 * (+0x00) and fills the combat-UI output scalars above.  Returns 0 when NEITHER
 * side is garrisoned (the caller then resolves the fight head-less, no battle
 * UI); returns 1 otherwise, having flagged whether both sides are garrisons,
 * which side the local player controls (driving the control flags), and each
 * side's battle size (a zero count is floored to 6).  `local_player` is retail
 * DAT_0057c8cc.  The ff_batl.wav cue is a deferred bound seam.  Field semantics
 * (+0x01 garrison / +0x02 count) are provisional; the records are the same
 * g_per_territory_420 occupants the resolver validates. */
int engine_combat_size_battle(int attacker, int defender, int local_player);

#endif /* OPENLORDS2_ENGINE_COMBAT_H */
