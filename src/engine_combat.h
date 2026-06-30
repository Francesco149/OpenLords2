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
extern int g_combat_audio_latch;    /* _DAT_005533f0 — voice/cue-active flag.
                                     * Combat raises it around battle cues; the
                                     * event-scroll renderer (engine.c) clears
                                     * it before a capture-event cue and the
                                     * speak helper (0x4b35c1) raises it. */
extern int g_combat_flag_530d0;     /* DAT_005530d0 — combat-focus flag; the
                                     * scroll dismiss (0x476768) raises it. */
extern int g_combat_flag_553210;    /* DAT_00553210 — region-relationship UI
                                     * dirty; the help-hint pump (0x476aa7)
                                     * consumes-and-clears it (hint 0xd3). */

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

/* Port of FUN_0046d42c @ 0x46d42c (456 B) — find the strongest friendly live
 * unit in the 4×4 cell block anchored two cells up/left of region `key`'s
 * capital cell (record +0x6c col / +0x6d row): the occupant whose owner (+0x00)
 * matches the region owner (+0x05), whose kind byte (+0x08) is 1, with the
 * greatest head-count (+0x168).  Returns 0 if the capital is within two cells of
 * the grid edge (the box must fit fully) or no qualifying unit is present.  The
 * scanned box is asymmetric (2 left/up, 1 right/down) — retail's exact bounds.
 * combat_b (FUN_004a6c68) parks the result in the defender slot DAT_00568218. */
int engine_combat_find_capital_defender(int key);

/* Port of FUN_004aaa80 @ 0x4aaa80 (344 B) — reset region `region`'s owner
 * muster pool and seed it for a staged defender of `count` head.  Clears the
 * +0x00 and +0x04 fields of all eight contingent slots, copies the owner's six
 * cashflow-category reserves (strat +0x140..+0x154) into slots 1..6 fields
 * +0x04 and +0x08, then parks `count` in slots 0 and 7 fields +0x00 and +0x08.
 * Pool row + strat reserves both indexed by the region owner (+0x05).  The
 * garrison/auto-defender builder FUN_004a50ae (unported) calls this before it
 * redistributes the pool; combat_b (FUN_004a6c68) drives that path. */
void engine_combat_prime_muster(int region, int32_t count);

/* Port of FUN_004a50ae @ 0x4a50ae (731 B) — raise a defending garrison in
 * region `region` sized at `pct` percent of its provision (+0x24) and spawn it.
 * Returns the new occupant index (DAT_0052afe0) or 0 when provision < `min_prov`
 * or no free spawn cell exists.  `mode` selects the staging: 2 = fresh garrison
 * (free spawn, strat reserves seeded, sized head distributed into troop classes
 * by size band), 1 = top-up (recruit 10-at-a-time from the primed reserves up to
 * the staged budget), 0 = plain spawn at the muster-cost supply price.  Reads
 * the percentage→cost curve (retail .rdata 0x4d8778).  Its callees
 * engine_combat_prime_muster / engine_combat_spawn_unit are ported; combat_b
 * (FUN_004a6c68) drives it to materialise a region's auto-defender. */
int engine_combat_muster_defender(int region, int pct, int min_prov, int mode);

/* Port of FUN_004a5003 (171 B).  Raise a ~50-head field unit from `region` at
 * the muster-cost supply price (the sibling of muster_defender mode 0 with a
 * fixed size and no min-provision gate).  Returns the new occupant index
 * (DAT_0052afe0) on success, else 0.  AI brain step 0xa raises its offensive
 * force this way. */
int engine_combat_raise_region_unit(int region);

/* Port of FUN_004a5389 (214 B).  Raise a keep-garrison unit from `region` sized
 * at `pct` percent of its provision (+0x24), distributing the head across troop
 * classes 5/1/3/2/6 from the owner's primed category reserves, then spawn it at
 * the muster-cost supply price.  The sibling of raise_region_unit with the size
 * passed in (not fixed at ~50) and the class-distribution pool sweep that
 * muster_defender mode 1 also uses.  Returns the new occupant index
 * (DAT_0052afe0) on success, else 0.  AI brain step 7 (FUN_0049f12f) garrisons
 * its keeps this way. */
int engine_combat_muster_garrison(int region, int pct);

/* Port of FUN_004a689d (399 B).  Pick `army`'s next deployment cell: the nearest
 * deployable, passable cell owned by `region` to the army's seat, or the
 * region's capital cell on no match.  Writes the chosen column/row through
 * `out_col`/`out_row` (retail leaves them in the find-nearest scratch
 * DAT_00569570/78, which callers copy into the army's destination fields). */
void engine_combat_find_deploy_cell(int army, int region, int *out_col, int *out_row);

/* Port of FUN_004a0917 (403 B).  Pick `owner`'s best available (idle, placed,
 * ≥ `min_head`) unit for `region`, scored by home-region affinity (same region
 * 300, adjacent 100, else 0) plus an eighth of its weighted strength.  Returns
 * the best occupant index, or 0 if none qualifies. */
int engine_combat_pick_best_unit(int owner, int region, int min_head);

/* Garrison head-count cap for keep level `keeplvl` (k_garrison_cap[keeplvl]).
 * Exposed for the AI movement planner (brain step 0xb, FUN_004a6270). */
int engine_combat_garrison_cap(int keeplvl);

/* Keep-capacity table value for `level` (k_keep_tbl[level], retail 0x4d8a3c).
 * Exposed for the event-popup type-0x12 render arm (engine.c). */
int engine_combat_keep_value(int level);

/* Port of FUN_004a07f2 (293 B).  Find the first realm owned by `owner` whose
 * keep has spare garrison capacity (keep cap minus the pending garrison's head)
 * to hold occupant `unit`.  Returns the realm index, or 0 if none. */
int engine_combat_find_garrison_realm(int owner, int unit);

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

/* Port of FUN_004a6c68 @ 0x4a6c68 (1264 B) — combat_b, the army-move executor's
 * second combat-outcome seam (matches engine_army_move_region_fn).  A live owned
 * attacker `attacker` has stepped into a region `region` held by another owner:
 * select or raise its defender (capital garrison via find_capital_defender, or a
 * fresh/top-up muster via engine_combat_muster_defender keyed by ownership +
 * difficulty), then resolve the battle (size → resolve → aftermath, with an
 * absorb on auto-resolve, the 0x13 quick-anim replay, or the 0x12 interactive
 * battlefield + s080 cue).  Undefended → transfer the region to the attacker.
 * Shares engine_combat_dispatch's resolve tail; clock/WAV are the bound combat
 * seams.  Bind alongside combat_a via engine_army_move_bind_combat_fns. */
void engine_combat_contest_region(int attacker, unsigned region,
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

/* Port of FUN_004abf77 @ 0x4abf77 (526 B, __cdecl void(int army,int region)).
 * When an army occupies a NEUTRAL region, post a one-shot population-reception
 * notification (codes 0x82..0x86) to the army's owner, chosen from the region's
 * popularity reserve and how fully the army fills the region's capacity.
 * Notification-only (no save-state mutation); a private helper of the army-
 * state machine's region-entry path (FUN_004abb36). */
void engine_combat_notify_occupation(int army, int region,
                                     engine_event_queue_state_t *evq);

/* Port of FUN_004abb36 @ 0x4abb36 (473 B, __cdecl void(int army,uint region)).
 * Reconcile an army's occupation of `region` against the region's true owner:
 * latch the army's effective-region colour (+0x10), fire the neutral-region
 * reception notice, raise the local-view dirty + combat-focus flags, and — for
 * an owned region the army was marching to (+0x151) — post the 0xaa arrival
 * notice and advance the owner's rotating notification-variant counter (+0x159).
 * Reached from the per-tick army-state machine's marching handler (FUN_0046521f). */
void engine_combat_army_enter_region(int army, unsigned region,
                                     engine_event_queue_state_t *evq);

/* Port of FUN_004ac185 @ 0x4ac185 (577 B, __cdecl int(int region)).  Spawn a
 * rebel "peasant" stack (kind 2 / type 6) on an owned region whose population
 * reserve has run dry, when a placeable cell exists within radius 1..3 of the
 * region seat.  Demolishes the region's economy, seeds the rebel head-count to
 * 30% of the region capacity (+0x24) and debits it.  Returns 1 on success, 0 if
 * the region is unowned, no cell is reachable, or the record table is full.
 * Reached from the army-state machine's settle path and (as the rebellion seam)
 * from the per-realm unrest pass. */
int engine_combat_spawn_rebellion(int region, engine_event_queue_state_t *evq,
                                  int mp_active);

/* Port of FUN_004abd0f @ 0x4abd0f (616 B, __cdecl void(int army,uint region)).
 * Reconcile an army settling on a region (the per-tick driver's state-2
 * handler).  Neutral region → latch the army's effective-region colour.  Owned
 * region newly occupied → warn the owner (codes 0x9a/0x9b/0x9c by popularity
 * reserve), possibly spawn a rebellion (reserve < 0x0a), then debit the region
 * popularity and clamp it non-negative. */
void engine_combat_army_settle_region(int army, unsigned region,
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

/* ── settlement-spawn cluster (per-turn pass FUN_004508de) ───────────────── */

/* Port of FUN_004a9a9a @ 0x4a9a9a (1047 B, __cdecl int(int,int,int,int)).
 * Spawn a fresh occupant record near army `army`'s map seat from owner
 * `owner`'s primed muster pool; returns the created occupant index (retail
 * DAT_0052afe0) or 0 when no free cell is reachable.  `attach_unit` non-zero
 * folds the army's mustered g_unit_table_20 unit (+0x1ad) in as a mercenary
 * contingent; `supply_cost` is debited from the army settlement byte pair
 * (+0x0c / +0x15).  Debits the owner's provision / cashflow, re-plans the
 * realm's economy, and refreshes the map view when the spawn belongs to the
 * viewing player.  The settlement path calls it with attach_unit = 0,
 * supply_cost = 0. */
int engine_combat_spawn_unit(int owner, int army, int attach_unit, int supply_cost);

/* Port of FUN_004a551b @ 0x4a551b (332 B, __cdecl int(int)).  Size realm
 * `key`'s due garrison from the keep-capacity tables, prime its owner's muster
 * pool, and spawn the unit via engine_combat_spawn_unit.  Returns the new
 * occupant index, or 0 when no garrison is due (capacity used up, or the staged
 * size exceeds half the realm's provision +0x24). */
int engine_combat_settlement_find_slot(int key);

/* Port of FUN_004a79a3 @ 0x4a79a3 (767 B, __cdecl void(uint,int)).  Home
 * freshly-spawned occupant `occ` onto realm `key`'s keep cell: cap the combined
 * head-count against the keep-level table (over cap → role 2 reject), else make
 * it the garrison (role 5) on the seat cell when none is pending, or merge it
 * into the pending garrison.  Re-collects territory income and latches the
 * view-dirty flag for the viewing player's selected keep. */
void engine_combat_place_garrison(unsigned occ, int key,
                                  engine_event_queue_state_t *evq);

/* Region-event "enter keep terrain" chain (FUN_004686a0 + FUN_004a78ea /
 * FUN_004a7ca2 / FUN_004a7e0a / FUN_004a7eb5).  Reached from the army-move
 * executor (outcome code 6) via engine_army_move_region_event_hi. */

/* Port of FUN_004686a0 — dispatcher: a live placed stack `occ` entering keep
 * region `region` reinforces it (owner matches the region race) or lays siege. */
void engine_combat_region_event_hi(int occ, int region,
                                   engine_event_queue_state_t *evq);

/* Port of FUN_004a78ea — reinforce friendly keep `region` with `occ`
 * (place_garrison; AI/remote path also latches the garrison-selection scalars). */
void engine_combat_reinforce_keep(unsigned occ, int region,
                                  engine_event_queue_state_t *evq);

/* Port of FUN_004a7ca2 — begin a siege of enemy-held keep `region` by `occ`,
 * subject to the defending-garrison / siege-block / already-besieged guards. */
void engine_combat_begin_siege(unsigned occ, int region,
                               engine_event_queue_state_t *evq);

/* Port of FUN_0043b917 — tear down occupant `occ`'s siege link (the inverse of
 * the siege-setup link): clear `occ`'s besieged-region byte (+0x199) and the
 * defending occupant's besieged mark (+0x19a), resolving the defender through
 * the region's per-army pending slot (+0x1bc).  No-op when `occ` isn't
 * besieging.  Shared by the turn-driver phase sweeps and other teardown paths. */
void engine_combat_break_siege_link(int occ);

/* Port of FUN_004aca2b (297 B) — advance the per-turn mercenary-offer rotation
 * (a phase-7 turn-rollover callee in the strategic turn driver 0x49a010).  Every
 * free merc-pool slot (g_unit_table_20, back-ref +0x00 == 0) clears its offer,
 * counts down (+0x06), and steps a realm cursor (+0x04, wrapping at the realm
 * count s_slot_0056d5dc); on the turn its countdown expires it republishes the
 * cursor as its offered realm (+0x03), reloads the countdown from its period
 * (+0x07), and steps the cursor once more.  Then for every realm
 * 1..s_slot_0056d5dc it stamps which pool slot is offering to it this turn into
 * the realm's mustered-unit scratch (g_per_army_768 +0x1ad, A_F1AD) — 0 when no
 * slot is.  Pure scalar/record bookkeeping; pool-slot count is s_slot_00554030. */
void engine_combat_rotate_merc_offers(void);

#endif /* OPENLORDS2_ENGINE_COMBAT_H */
