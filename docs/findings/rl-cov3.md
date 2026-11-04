# rl cov3 — what coverage round 3 newly reaches, on retail's layout (2026-11-04)

Partition `cov3` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): the 11 functions
(3594 B) the round-3 scenarios (`coverage-round3.md`: movie-skip, custom-strong, custom-garrison, siege-castles)
newly reach. Every one had an old port (pre-rhost) or none; none was verified against retail. `src/rl_cov3.c`,
rows `tools/rhost/rh_hooks_rl_cov3.c`, globals `src/rl_cov3_globals.{h,c}` (generated; the personality table
merged by hand into one block).

## Routes

| VA | what | route | old port |
|---|---|---|---|
| 0x42df30 | skip the playing movie (a click on screen 0x22) | b | none (engine.c names it in a comment) |
| 0x434308 | the load box's Cancel (title page 3) | b | engine_menu_input.c (not linked) |
| 0x4367ff | the type-10 popup's yes: pay the asked price | b | none |
| 0x43b741 | siege preparations' "−" (one engine fewer) | b | none |
| 0x46d130 | the clamped box scan: a free cell, raw flags 0 / 2 | a | engine_map_query.c `…_box_any_rawflag_only_bit1` |
| 0x4917dd | a castle cell's building footprint | a | engine_tile.c `engine_tile_expand_footprint_at` |
| 0x49ff71 | validate an AI campaign target | a | engine_army_ai.c `…_validate_campaign_target` |
| 0x4a1b29 | ai_diplomacy_gift | a | engine_army_ai.c `ai_diplomacy_gift` (+ EAX model) |
| 0x4a3551 | an AI's answer to a plea for help (inbox type 5) | b | engine_army_ai.c `ai_inbox_tribute` (see below) |
| 0x4ae968 | castles_write_scenario | b | save.c (not linked; stdio on a path) |
| 0x4aea70 | castles_read_scenario | b | save.c (not linked) |

Route (a) for the two engine_army_ai.c functions: their `static` dropped (the definitions only; the prototypes
live in rl_cov3.c), so rl_49ff71 / rl_4a1b29 and the AI table's own caller (ai_run_campaign 0x49f977's row) run
the same code. 0x4917dd wraps engine_tile on retail's arrays (g_castles_state, the path / stage scratches, the
footprint list). 0x4a3551 is (b): the old `ai_inbox_tribute` posts into the port's event-queue struct (not
retail's layout), takes the roll as a threaded argument and clamps the personality; the port calls the event
post rl_472bc5 (partition terr) by name.

## Rows

HOOK_R wherever retail defines EAX: 0x46d130 / 0x49ff71 (read by their callers), 0x4a1b29 (modelled from the
pre-state), 0x4917dd (its argument on both paths), 0x4a3551 (352 × ai, or the gift's), the castle file functions
(close's result, or the log's). HOOK_V: the three router actions (entry-patched: list 0x4ddd78's thumbs-down, the
type-10 popup's buttons, the siege screen's "−" buttons) and 0x42df30 (no caller reads EAX). 0x42df30 is
HOOK_SMK_V + RH_SH_MEDIA: it closes the SMACK and 0x42e060 may open the next boot movie (smackw32's state rolled
back, `rh_hook_smk.h`). 0x4367ff carries RH_SH_MEDIA (0x476768 restores the screen under the popup).

## Quirks (retail ground truth)

1. Realm +0x81 is the alliance partner, mutual — not a lord (engine_army_ai.c names it STRAT_LORD): in
   custom-strong realm 1's +0x81 = 5 and realm 5's = 1. 0x4367ff pays the price to the local player's +0x81
   (`ai_diplomacy_gift(5, 1, 13, 1000)` at f7524); 0x4a3551 helps only when the asker is its +0x81.
2. 0x4a3551 / 0x4a38db index the personality table DAT_004d8a58 (4 rows × 0xf0) as `(3p − 3) × 0x50` with no
   bound: personality 0 would read the 240 B before it. The bar is realm +0x14 = average county population.
3. 0x4a3551: the roll DAT_0058fd60 is the one cached that frame (in custom-strong 0x15 at f11182): help (184)
   needs 4 × relation > roll, the price (185) 4 × relation ≤ roll with relation ≥ 10 — so a roll below 40 never
   asks a price.
4. 0x4ae968 opens castles.dat `O_WRONLY | O_CREAT | O_BINARY` (0x8101, pmode 0600): a missing file is created.
   The open comes before the packing: a failed open leaves DAT_004eb2b0 untouched.
5. 0x4ae968 / 0x4aea70: a failed seek logs and latches DAT_005aeb68 = 1 but never closes the file (an fd leak).
6. 0x4aea70 does not check the read's count: a short castles.dat unpacks the scratch's stale tail.
7. 0x4aea70's fixups read the cell's +7 before the bit tests; the footprint (0x4917dd) of an earlier cell writes
   +1..+4 of later cells, which the loop then overwrites with the file's +3 / +1 (row-major order).
8. 0x4a1b29's EAX: 352 × the giver realm when it cannot pay; else the giver byte in AL over the relation's movsx
   after the −4 (0xffffff00 | giver when negative and not clamped; giver when clamped or ≥ 0). No caller reads it.
9. 0x434308's EAX is the moved value (DAT_00553e5c, or DAT_0057c90c's low byte); the router drops it.
10. 0x42df30 has four call sites in a row in 0x42ff10's screen-0x22 branch (one per input flag: DAT_00553fc8,
    DAT_004e6900, DAT_004e65d8, DAT_004eabb4). movie-skip's four clicks each call it once, from the third
    (0x431d55, DAT_004e65d8): intro, imptitle, credits, lom.smk; skipping intro / imptitle starts the next boot
    movie at once (0x42e060 → 0x42d91b, trapped at f154 / f340).

## Old-port bugs (not fixed there: save.c is not linked; the AI tribute handler is route b)

- save.c `castles_write_scenario`: opens "r+b" (fails when castles.dat is missing; retail creates it) and packs
  the scratch before opening (retail packs only after a successful open).
- save.c `castles_read_scenario`: a short read returns 1 without unpacking (retail unpacks whatever the scratch
  holds); a failed seek closes the file (retail leaks it).
- engine_army_ai.c `ai_inbox_tribute` / `ai_inbox_demand`: the personality row clamped to 1..4 (retail: no bound).

## Seams

- Replaced (one-line call sites, after verification): engine_bf_input.c bf_4784ca → rl_4ae968; engine_bf_setup.c
  bf_4787a4 → rl_4aea70 (its TODO(bf-link) line went with it; bf_4784ca's header comment still names the old
  TODO).
- Other partitions by name: rl_4af116 / rl_4af312 / rl_4afab9 / rl_4b2575 (io), rl_42e060 (render), rl_43eda0 /
  rl_43b7c4 (glue); still stubs (retail under rhost): rl_4af502 (host), rl_476768 (turn), rl_472bc5 (terr).
- TODO(rl-extern): none.

## Evidence

- `verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (64 scenarios, 2026-11-04 22:12–00:00): 364/364
  rl rows reached with 0 divergences, hybrid PASS (all 364 replaced, `corpus --check`). The 11 cov3 rows: 36
  shadowed calls / 4 scenarios — 0x42df30 4 (movie-skip, depth 3), 0x434308 1, 0x4367ff 2, 0x43b741 1, 0x46d130 2,
  0x4917dd 9 (depth 3: inside 0x4aea70), 0x49ff71 12, 0x4a1b29 2, 0x4a3551 1, 0x4ae968 1, 0x4aea70 1. 4 failed runs,
  not cov3's: movie-skip crashes in passes 1 and 2 (below).
- The four round-3 scenarios, cov3 rows only: 11/11, 0 divergences, hybrid PASS, 0 failed runs.
- The seams: `--hooks '^(bf_4784ca|bf_4787a4|rl_4ae968|rl_4aea70|rl_4917dd)$' --only siege-castles`: 5/5, 0
  divergences; hybrid PASS with bf_4784ca / bf_4787a4 calling the ports directly.
- Exploration (poked state, not corpus evidence): custom-strong with AI 5's relation toward realm 1 poked 30 → 0 at
  f11181 takes 0x4a3551's refusal path 183 — shadowed, 0 divergences. Path 185 (the price) needs a roll ≥ 40 at
  that call; the roll is drawn inside the frame (a poke at the frame boundary is overwritten): unexercised.

Unverified (ported from the asm, never reached): 0x4a3551's 185 path; 0x4367ff's / 0x43b741's MP messages; the
castle file functions' open / seek failures; 0x434308's other arm (screen ≠ 0x1f: the screen back to DAT_0057c90c;
movie-skip cancels on the title, screen 0x1f: title page DAT_005530f0 = DAT_00553e5c).

## rhost limit hit (shared: rh_hook_smk.h / rh_smack.c, partition io's row)

movie-skip crashes when rl_42d91b (io; HOOK_SMK) is shadowed — bisected over all 353 other rl rows: that row alone.
f662, the title menu (0x432b05) opening lom.smk: an AV inside smackw32 (0x18008a3f, reading 0x19220014, its heap)
on the port side (esp in the hook machinery's zone). Earlier opens in the same run (imptitle / credits from
0x42e060 at f154 / f340) pass. Likely the state HOOK_SMK does not roll back (rl-io.md: rh_smack's span bump pointer
and waveOut queue are statics). Without cov3's rows the crash is the same (pre-existing on master's rows).
