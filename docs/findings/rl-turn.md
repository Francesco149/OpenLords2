# rl turn — the event popup, game init, the turn, the AI's realm passes, on retail's layout (2026-11-04)

Partition `turn` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`), round 3: the 0x47309e..0x4a57ab
band the corpus reaches whose old ports were not on retail's layout. 62 functions, 38892 B, all in `src/rl_turn.c`;
rows in `tools/rhost/rh_hooks_rl_turn.c` (62).

## Status

All 62 ported, every one with a row; shadowed at 0 divergences over the whole corpus, hybrid PASS (§Evidence).

## Routes

**(a) wrap — 25.** The old port is linked in rhost and runs on retail's layout (the save block, the aliased runtime
globals, the synced mirrors); `rl_<va>` forwards to it.

| module | functions |
|---|---|
| engine_army_ai.c | 0x49f12f garrison_keeps, 0x49f431 reinforce_realms, 0x49f73f / 0x49f850 the enemy / own head-counts, 0x49fcc5 fund_campaign, 0x49fda5 assign_deployment, 0x4a01ea pick_target_owner, 0x4a0649 pick_attack_target, 0x4a3c67 / 0x4a4052 / 0x4a440f the dispatchers, 0x4a4a3f sell_order |
| engine_diplomacy.c | 0x4a1241 best_ally_candidate, 0x4a1774 form_alliance, 0x4a1847 reconcile_treaties, 0x4a1a54 clear_treaty, 0x4a24d1 broadcast_penalty |
| engine_roster.c | 0x49bae9 seat_assign, 0x49bbe8 roster_add, 0x49bc1d roster_clear, 0x49bc5f roster_compact, 0x49ca32 seed_visibility_bits, 0x4a1e6c first_active_nonlocal |
| engine_camera.c | 0x47ed34 clamp_anchor (on the save-block scalars DAT_0055ce70 / DAT_005678a0) |
| engine_combat.c | 0x4a545f pool_commit |

Eight of these were file-statics (engine_army_ai.c: 0x49f12f, 0x49f431, 0x49f73f, 0x49f850, 0x49fcc5, 0x49fda5,
0x4a01ea; engine_combat.c: 0x4a545f): `static` dropped, prototypes in rl_turn.c. No body changed — every wrapped
port matched retail on its first shadow.

**(b) re-port — 37.** The old port is in an unlinked module (engine.c, game_init.c, main.c, gfx.c, save.c,
engine_turn.c, engine_army_select.c, engine_npc_schedule.c, engine_pl8.c …) or threads a port struct (the event
queue: engine_ranking.c's 0x49aa0e, engine_army_ai.c's inbox handlers 0x4a277d / 0x4a29b2 / 0x4a2cd6 / 0x4a2f61 /
0x4a30f6 / 0x4a38db post into `engine_event_queue_state_t`): a fresh rl_<va> from the asm, posting through rl_472bc5.

| group | functions |
|---|---|
| the event popup | 0x47309e the render (10915 B: 20 panel kinds), 0x476768 dismiss, 0x476710, 0x47685d the input step, 0x476a5d, 0x476aa7 the help hints, 0x476da9 open a report, 0x476e21 the report screen's restore |
| game init | 0x497ced, 0x497e65 the shuffles, 0x49bd99 the realms (3068 B), 0x49caaa the AI seats, 0x4a1c53 / 0x4a2584 diplomacy reset, 0x499dc3 / 0x499e5d the custom / scenario configs, 0x498270 the strategic map init, 0x49b35e |
| load / sheets | 0x4976a1 the post-load, 0x4984dc the season's sheets, 0x499859 the system files, 0x499034 misc_sel, 0x499a1c system sheet, 0x4997bf the villager sheets |
| the turn | 0x49a010 sub-state 0, 0x49ce51 the phase, 0x49b6d3, 0x49b762 turn resolved, 0x49a453 the save rotation, 0x49aa0e the rankings |
| the AI inbox | 0x4a277d step 1, 0x4a29b2 / 0x4a2cd6 / 0x4a2f61 / 0x4a30f6 / 0x4a3475 / 0x4a38db the type handlers |

No host-level functions in the partition (0x4997bf's exit(100) is its out-of-memory path only).

## The old ports (reading aids)

- Wrapped (a): no divergence — each matched retail on its first shadow.
- Not linked (b), for the standalone's owners (not fixed: those modules are not this partition's): engine.c's 0x47309e
  ports 3 of the 20 panel kinds (0, 5..9, 16); its 0x476768 leaves the game-end arm (the log, screen 0x26, the
  stream stop, the error latch 3) and the tutorial / scenario-end handoff (0x497879, screen 0x1c) as TODOs.
  engine_ranking.c's 0x49aa0e, game_init.c's 0x497e65, engine_turn.c's 0x49b6d3 / 0x49ce51 match the verified ports
  (the inbox handlers and the rankings differ only in posting into a port struct).

## How the ports reach the rest

- Other partitions by name: rl_472bc5 (the post; terr), rl_45240a / rl_4527a6 / rl_451fcc / rl_43e9e2 / rl_43ac23 /
  rl_43ad25 / rl_448440 / rl_4651b9 / rl_432746 / rl_451150 (ctl), rl_4298c1 / rl_401136 / rl_4018d7 / rl_40da1e /
  rl_40f5fd / rl_426d11 / rl_427d19 / rl_427ed0 / rl_42847c (ui4), rl_46a037 / rl_467770 / rl_46f3cd / rl_46f2ae /
  rl_46f1dc / rl_4676e0 / rl_46ff43 / rl_46dbf5 / rl_46dd34 / rl_46da4b / rl_46dfd5 / rl_46ea61 / rl_472ad0 (terr),
  rl_4af502 / rl_4af8d9 / rl_4b25b0 / rl_4ade93 / rl_4ac904 / rl_4ae74d / rl_4ae8a9 / rl_4b35c1 / rl_4b18e3 / rl_4b1deb /
  rl_4b1de0 (host); the ported io / glue / misc / render functions; bf_477c89 / bf_477b2f / bf_499d97; bf_rng_step.
- Linked verified ports by name: engine_army_state_* (the marches / raids / siege scan), engine_army_ai_set_rations_and_grant,
  engine_army_ai_run_independent, engine_combat_rotate_merc_offers, engine_combat_spawn_unit, engine_territory_census_army_terrain
  / _grade_feature_cells / _allocate_realm_workforce / _stamp_secondary_keep, engine_army_economy_provision / _plan_realm;
  the gfx layer (panel, seal, wrap / text / label, the portrait frame, from-blob / raw sprites) with rl_render.c's
  localization-walker load / store.
- Platform shim (`rl_io_plat.h`), added: remove (0x4c22c0), rename (0x4c2260) — the save rotation.

## Globals

`src/rl_turn_globals.{h,c}` + `tools/rhost/rh_rl_turn_alias.h`: generated (`gen_skeleton.py --part-globals turn`;
`dedup_globals.py`: none shared), then hand-widened where retail indexes one name as an array (standalone storage;
exact under rhost either way): the 20 report-seen flags DAT_004f0360 (generated as 15 scalars), the popup panels' five
button tables 0x4dda90..0x4ddb80 (generated as one int16 each), the misc_sel.PL8 record 0x4daed8 (+ its size), the
lastturn name buffer DAT_004ee8d0 (16 B), the AI personality table 0x4d8a58 (4 × 0xf0; engine_army_ai.c's
g_ai_personality), the keep tiers' costs 0x4d8a3c (7 dwords). The generator's NEIGHBOUR flag on g_rl_4ee8a0
(OldFilename / FileName) was wrong. Other partitions' globals are declared locally (their headers conflict in one unit).

## Rows

HOOK_R where a caller reads EAX (compared): 0x47685d (took the input), 0x49b762 (the turn resolved), the loaders
0x4984dc / 0x499859 / 0x499034 / 0x499a1c / 0x4997bf, and 0x4a1241 / 0x49f73f / 0x49f850 / 0x49fcc5 / 0x4a0649. HOOK_V
elsewhere (the ports return retail's EAX where a path defines it: 0x476a5d 19, 0x476e21 the screen byte, 0x49ce51 the
next phase, 0x499dc3 option 11 …). RH_SH_MEDIA on 0x47309e (the render) and on the rows whose calls can draw:
0x47685d / 0x476768 / 0x476710 (the dismiss's chain), 0x476aa7 / 0x476da9, 0x4976a1 (the strategic render 0x40f5fd),
0x497ced, 0x49a010 (the End-turn strip 0x41a734).

## Quirks (retail ground truth)

1. 0x476da9 marks `byte_4F0298[report]`; its only caller (0x476aa7) passes the hints 200..218, so the store lands in
   the seen flags DAT_004f0360[report - 200] that 0x476aa7 tests (one array under two IDA names).
2. 0x47309e, the multi-line scrolls (kinds 5..9): a first wrap pass renders every paragraph at the same y (box + 64)
   just to advance the line accumulator DAT_005cd4f8, then the panel is drawn over it and the text again.
3. 0x47309e, the help pages (kind 19): the paragraph count is `dword_4D6A8C[event]` — the events are 291.., so it is
   read at 0x4d6f18.., inside the report table 0x4d6e88 (IDA's 11-entry table is a different one); the box comes
   from the 16-B rows 0x4d6eb8 + 16 (event - 291), in tiles.
4. 0x47309e kind 11 (a realm's offer): a vassal (the local realm's lord +0x81 set) never sees it: dismissed unseen.
5. 0x49a010: the MP end-vote timeout stores DAT_00553018 = DAT_0055ce88 and at once DAT_00553018 = 1 (dead store).
6. 0x49aa0e: the cash bands > 5000 (+100) / > 10000 (+200) are dead (they sit behind > 2000).
7. 0x497e65: the 48-slot permutation DAT_0057cae0 gives each value at most 50 steps; one that finds no free slot is
   dropped (its slot stays 0). The roster re-seat tries n slots from a random start.
8. 0x4976a1 (MP only): 0x43fd2a is called with no argument pushed — it reads the caller's saved EBX (cf. rl-glue
   quirk 3).
9. 0x49bd99: a realm's +0x80 dword is zeroed four times by an index-free loop, then +0x80 / +0x81 bytes again.
10. 0x499034 addresses misc_sel.PL8 as misc_cty.pl8's record + 0x28 (the 20-B name / size records at 0x4daeb0).
11. 0x4a30f6: an alliance in the marginal band (-10..10) is accepted when the RNG byte DAT_0058fd60 < 3 rel + 45.
12. 0x4a277d: a type-0 message (an offer) is dispatched by `if (type != 0) switch … else 0x4a29b2`; type 5 goes to
    0x4a3551 (unported), types ≥ 7 nowhere. The inbox's byte +3 is never cleared.
13. 0x4984dc: DAT_0057c968 1 (a reload) skips vill_gd8.pl8 and the "Data loaded OK" log; every exit clears it.
14. 0x47309e kind 19 keeps the box's size in TILES in DAT_00553024 / DAT_00552ff8 (the other kinds: pixels), so the
    shared tail's dirty mark (0x4527a6(x, y, w / 16 + 1, h / 16 + 1, 2)) covers only 2 × 2 tiles of a help page.

## Seams left

- TODO(rl-link): 0x41a734 the End-turn strip (engine_hud_panel_endturn needs its panel environment), 0x49a581 the AI's
  per-tick pass and 0x4a8aab the siege assault (both take an evq), 0x41ab67 labor_num_units (a static in
  engine_labor.c), 0x40cfd2 the button strip (as rl_render.c: its pressed-button seam is bound to the gfx table's
  dirty copy).
- TODO(rl-extern) (unported, in no partition): 0x475e07 / 0x476488 (the popup panels 12 / 20), 0x4a3551 (inbox type 5),
  0x4a1b29 (the diplomacy gift), 0x4b2636, 0x43f4b7 / 0x43fd2a (MP).

Shared-file needs (standalone storage only; exact under rhost): engine_bf_globals' g_bf_53e8d0 is one dword but
0x49a010 (as engine_bf_extern's 0x43f304) clears ten; g_bf_542ce0 is 24 B but 0x4984dc / 0x499859 load vill_gd8.pl8
(0x12d8 B) into it.

## Path coverage (block level)

First-hit coverage of every basic-block start of the 62 functions (jump targets + conditional fall-throughs from
all_ida.asm: 1338 blocks; `rhost.py coverage` with that list as its function table, retail only): **1127 / 1338 by the
corpus**. Never taken (ported from the asm, unverified):
- 0x47309e (147 / 211): the panel kinds 16 (a county's notice), 10 (a realm's demand with its buttons), 12 / 20 (retail
  panels anyway); the result-movie arms of kinds 13 / 14 (DAT_0053f248 on, past the first 10 frames: the full-screen
  frame, the movie 0x42d91b); kind 4's edge clamps; kind 17 with a garrison army; kind 18's tier arms; kind 11's
  vassal shut and its later beats; kind 1's DAT_0057c8ac name.
- the MP arms: 0x49b762's vote (11 blocks), 0x49a453's .sva / .svb names, 0x4976a1's session poll, 0x49caaa's MP table.
- 0x47685d's panel-12 / panel-10 / -11 button tables (11), 0x476aa7's hints 207..211 / 217 / 218 (10), 0x476710's
  panel exits.
- the error paths of the loaders (0x4984dc 7, 0x499034 4, 0x4997bf 3 — out of memory / a missing file), 0x49a453's
  rotation with DAT_00553260 > 0.
- the AI inbox's rarer answers: 0x4a2cd6 (8 of 20), 0x4a30f6 (13 of 29), 0x4a38db (13 of 20: the gift / the staged
  demand), 0x4a2f61 / 0x4a3475's clamps.
- 0x49aa0e: the dead cash bands and the second victory notice (0xe1); 0x49bd99: the start army without a spawn slot,
  the county's flagged slot search; 0x49f12f's garrison scaling arms (5), 0x49fda5's lord-directed move (4).

Exploration probes (rhbot: replay bot-s1-t8 with the rows in shadow, poke retail memory when the first popup opens
(f1322, event 298), run on — poked state, NOT a pure replay; evidence for paths the corpus never takes), all at 0
divergences: 0x47309e with the panel kind DAT_005440a4 poked to 16 (201 calls), 10 (121), 13 / 14 with the result
movies DAT_0053f248 1 (14 with DAT_0053f5f4 0 and 1; 103 calls each: the full-screen arm, the dismiss, the failed
SmackOpen), 4 with the cursor at (300, 10) (the clamps), 11 with a lord (the vassal shut); 0x47685d with the panels
10 / 11 / 17 / 12 (events 0xf8 / 0xfa / 0xfb) open (1382 calls each: the button tables); 0x476aa7 with the hints on
and the screens 0x17 / 0x0f (panel 8) / 2 / 0x39 / 0x1b / 0x10 and DAT_00553210 (1352 calls each); a poked inbox for
realm 2 (an alliance, a demand, a compliment, a break, an offer of 500 from realm 1): 0x4a277d (5 calls) and, in a
second run, each handler shadowed alone (0x4a29b2 / 0x4a2cd6 / 0x4a2f61 / 0x4a30f6 / 0x4a38db one call each).

## Evidence

- `rhost.py verify --target wine -j 3 --hooks '<the 62 turn rows>'` over the whole corpus (59 scenarios, 2026-11-04
  21:05–21:32): **62/62 reached with 0 divergences, 0 never called; hybrid PASS (all 62 replaced, `corpus --check`); 0
  failed runs.** 6,670,782 calls shadowed (state; pixels on the RH_SH_MEDIA rows — 0x47309e alone 100,032 calls over
  57 scenarios); depth 1..3 (the AI's helpers nest in their callers). 0x4984dc: 72 calls skipped (its retail run grows
  the heap arena; rh_state.c does not compare those), 1931 compared; the hybrid pass runs every call.
- Iterations before it: every re-port matched on its first shadow; no wrapped port needed a fix.
