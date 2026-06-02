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

/* Port of FUN_004a80db @ 0x4a80db (478 B) — post-battle supply/condition
 * recompute for a surviving occupant.  For each of 3 supply groups, express the
 * present stock as a percent of (rate × count) clamped to 100, and store the
 * shortfall averaged over the head-count (rounded up) into the condition byte
 * (+0x19c).  Run by the aftermath FUN_004ab383 on the battle winner. */
void engine_combat_recompute_condition(int rec);

/* Port of FUN_004ad8cc @ 0x4ad8cc (277 B) — halve a conquered occupant's
 * surviving troops (each class count >10 is halved, ≤10 wiped), re-sum the
 * head-count with the merc contingent, clear two scratch flags, and recompute
 * the per-turn wage.  Run by the aftermath when a garrison is captured. */
void engine_combat_halve_garrison(int rec);

/* ===================================================================== *
 *  Combat-initiation SCC (FUN_004ac3c6 / 437535 / 4a7158 / 4ab383 /      *
 *  4a72fe) — the 5 mutually-recursive bodies reached from the turn       *
 *  driver when a realm loses its last territory or an army marches into  *
 *  a defended region.  The auto-resolve path (neither side garrisoned)   *
 *  is pure save-state logic; the interactive path hands off to the       *
 *  battlefield renderer through the seams below.                          *
 * ===================================================================== */

#include "engine_event_queue.h"   /* engine_event_queue_state_t */

/* Bind the optional deferred-audio seams.  All default NULL (silent):
 *   wav_fn   — retail FUN_00427990(name, loop, z): the s080 / ff_lose cues
 *   click_fn — retail FUN_00499aca(): the territory-captured click
 * The battlefield-render clock (retail timeGetTime) used only on the
 * interactive/animation handoff is bound separately; NULL → 0. */
void engine_combat_bind_wav_fn(void (*fn)(const char *name, int loop, int z));
void engine_combat_bind_click_fn(void (*fn)(void));
void engine_combat_bind_clock_fn(uint32_t (*fn)(void));

/* Render-handoff state mirror (retail DAT_0056d5c8): 1 only while the
 * interactive battlefield render is active (set by the renderer between the
 * resolver and the aftermath).  0 on the headless auto-resolve path, where the
 * aftermath fully disbands the loser instead of halving a captured garrison.
 * Exposed so the future battle-UI wiring (and the host tests) can drive it. */
extern int g_combat_render_active;

/* Current strategic-map selection (retail DAT_00553f8c): the territory whose
 * +0x10 region key seeds the combat region scalar (s_slot_0056822c) at dispatch
 * entry.  A runtime UI cursor, not save state; injected for the diff/tests. */
extern int g_combat_active_terr;

/* Port of FUN_004aa181 @ 0x4aa181 (1353 B, __cdecl void(int dst, uint src)).
 * Merge occupant `src`'s contingent into `dst` (g_per_territory_420): sum the
 * head-count / 7 troop-class counts / 3 supply-group triples, keep the larger
 * battle-presence accumulator, fold the survive/move linkage (re-homing the
 * survivor back-reference through the region pending slot), inherit src's merc
 * triple, recompute dst's condition, and disband src.  No-op when the combined
 * head-count is >= 0x5dd; refused (owner notified, event 0xa7) when both carry a
 * mercenary contingent.  `evq` carries the refusal notification.  Reached via
 * the garrison-placement path FUN_004a79a3. */
void engine_combat_merge_stack(int dst, int src, engine_event_queue_state_t *evq);

/* Port of FUN_004ac3c6 @ 0x4ac3c6 (211 B, __cdecl void(int)).  Demolish realm
 * `key`: zero its army-economy header fields, re-run its workforce / provision /
 * production / income plan, then — if it has a pending territory transfer
 * (+0x1bc) — hand that territory to engine_combat_transfer_army. */
void engine_combat_demolish_realm(int key, engine_event_queue_state_t *evq,
                                  int mp_active);

/* Port of FUN_00437535 @ 0x437535 (390 B, __cdecl void(uint,int)).  Move army
 * occupant `occupant` into a free cell near realm `key`'s seat; if the move
 * lands on a defended region, dispatch the resulting fight.  When no free cell
 * exists the occupant is disbanded instead. */
void engine_combat_transfer_army(unsigned occupant, int key,
                                 engine_event_queue_state_t *evq, int mp_active);

/* Port of FUN_004a7158 @ 0x4a7158 (402 B, __cdecl int(int,int)).  Combat
 * dispatch: park the combatant indices in the combat scalars, size the fight,
 * then either auto-resolve it head-less (returns 0) or hand off to the
 * interactive battlefield render (returns 1, screen-state + s080 cue). */
int  engine_combat_dispatch(int attacker, int defender,
                            engine_event_queue_state_t *evq, int mp_active);

/* Port of FUN_004ab383 @ 0x4ab383 (1751 B, __cdecl void(int)).  Battle
 * aftermath: apply the winner's resupply / the loser's capture-or-disband,
 * transfer the conquered region's ownership, post the result notifications,
 * adjust diplomacy, then re-plan every realm's economy and re-run the AI.
 * `render` (retail param) is non-zero only when invoked from the battlefield
 * renderer (the animation-teardown block); the dispatch always passes 0. */
void engine_combat_aftermath(int render, engine_event_queue_state_t *evq,
                             int mp_active);

/* Port of FUN_004a72fe @ 0x4a72fe (1516 B, __cdecl void(uint,uint)).  Transfer
 * region `key`'s ownership to player `new_owner`: recompute the new owner's
 * standings aggregate, post the appropriate captured/lost/eliminated
 * notification (codes 0x72..0x81), reassign the region owner and decay its
 * popularity — or, if the new owner already conquered before and owns no
 * neighbour, demolish the region instead (recurses to
 * engine_combat_demolish_realm). */
void engine_combat_transfer_ownership(unsigned new_owner, unsigned key,
                                      engine_event_queue_state_t *evq,
                                      int mp_active);

/* Bind the resign-time interactive UI cleanup seam (retail 0x47fe0b,
 * game-mode-gated on s_slot_0057c8fc == 2).  NULL default → no-op. */
void engine_combat_bind_resign_ui_fn(void (*fn)(int player));

/* Port of FUN_0049cf20 (704 B) — remove player `player` from the game:
 * demolish every realm they own (army records 1..16), disband every live
 * placed unit, post the eliminated/resigned notification (code 0xe0 / 0x102),
 * then wipe their strategic record and reset their per-player aux slot.  Sets
 * the combat screen-mode handoff (DAT_0057d340 = 2). */
void engine_combat_resign_player(int player, engine_event_queue_state_t *evq,
                                 int mp_active);

/* Port of FUN_0049b2a3 (187 B) — the turn orchestrator's multiplayer
 * player-resignation sweep.  No-op unless `mp_active` (retail DAT_00553030).
 * For each player slot 1..5 that is in-game (strat +0x05), live (status
 * +0x07 == 0) and has a zero aux +0x00, calls engine_combat_resign_player,
 * decrements the active-player count (s_slot_00553f98) and marks the slot
 * resigned (status +0x07 = 6). */
void engine_combat_mp_resign_sweep(engine_event_queue_state_t *evq, int mp_active);

#endif /* OPENLORDS2_ENGINE_COMBAT_H */
