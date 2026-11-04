# Coverage round 3 (2026-11-04) — the frontier's trigger conditions

Target list: the 258 unreached non-library functions with a reached caller (corpus coverage run
`20261104-173411`, 1492 functions). Per target: the condition in the caller that leads to it, then covered (by
which scenario) or why not. Authored over rhbot on wine with a live `--coverage` probe of the unreached set
(`drv.py` helpers in the session scratchpad: every probe hit = a function the corpus never reached). Scenarios:
`custom-strong`, `siege-castles`, `movie-skip`, `custom-garrison` (+18 functions, +5483 bytes over the corpus +
`game-quit`).

## Covered

| target | condition (caller → it) | scenario |
|---|---|---|
| 0x463e91, 0x463fa5 (drawer_b zoom-2 arms) | `drawer_b(3/4)` = the first / last cell of a shifted row, tile kind byte +12 == 2, view mode 2 (the kingdom overview). The overview never scrolls (x0 = 14, edge scroll inactive in mode 2), so only the map decides: France has a kind-2 tile on the left edge column, Europe on the right; Play Now, England, Scotland, Ireland, Germany, Italy, Crusades on neither | custom-strong |
| 0x468ae2 special tile | `engine_army_move_classify_step` → 7: a cell with flag 0x10 (village). Villages exist only on counties over 600 souls (`engine_realm_advance_population` → `engine_territory_update_village_tiles`, 1/2/3/4 villages at > 600/1000/1400/1600): never at Play Now's start; custom County status *strong* gives every county > 1000 at once. Our own village: entered, nothing razed; a foreign one (type 16): "slaughter villagers?" (confirm 4), razed, pop −25 % | custom-strong |
| 0x46d130 (nearest-feature ring, 2nd probe) | `engine_map_query_nearest_feature_ring`: no free road cell (flag 1, +5 piece 0) within Chebyshev 2 → a plain cell within 1. Callers: the Divide commit 0x437fd7 (the new half's cell), transfer 0x437535, arrival 0x467196. A march that completes on a roadless cell does NOT call arrival (path end = direction 8); Divide needs moves left and halves of ≥ ~50 ("Army too small" at 40) | custom-strong (Divide at (22,42)) |
| 0x4a1b29 ai_diplomacy_gift, 0x49ff71 validate | an ally answering ask-to-attack (0x4a38db) 187, ask-for-help (0x4a3551) 184, or the type-10 "Pay -" popup accepted (0x4367ff). Bars: the ally's realm **+0x14 = average county population** (pop / counties, 0x49d1e0 — not troops) ≥ 750 / 800 / 900 / 1000 (Knight / Baron / Countess / Bishop, table 0x4d8a88), attitude ≥ 10; then `rng & 0x7f` vs 2·att (attack: ≥ → 188 pay) / 4·att (help: ≥ → 185 pay). The gift sets the ally's campaign target +0xe8; its next run_campaign validates it (0x49ff71) | custom-strong |
| 0x4a3551 (the ally's answer to a plea for help) | the plea is queued only for our own county with a foreign non-allied, ungarrisoned army in it: county +0x19c (`engine_realm_collect_territory_income`, at turn processing) ≠ 0. Two seasons of insults to the Baron and the Bishop (one message per lord per turn) bring 83 of their men into Poitou | custom-strong (184) |
| 0x4367ff | the type-10 popup's yes (gift for the asked price) | custom-strong (188 → paid) |
| ev_137 0x449575 | a realm-event roll for an odd county after 1268 (game-state-map) | custom-strong (Hags curse, Poitou = 5) |
| 0x4103c5 panel_draw_q1_type2 | the HUD county panel's queue rows (0x40fec1): county +0x204 ≠ 0 = a field of type ≥ 25 (reclaiming). A barren field (type 0: trampled by a foreign army, decay_region_cell) → Farmland-Barren → the reclaim icon (329,400) → type 25 | custom-strong |
| 0x42df30 | a click while a movie plays (screen 0x22, `--movies`): "OK: SMK user ends" | movie-skip |
| 0x434308 menu_action_p3_cancel | the load box's thumbs-down (list 0x4ddd78 at (96,10) on title page 3: (463,90)) | movie-skip |
| 0x43b741 | siege preparations' "−" (button (38,210)): only does anything with a count > 0 (+3 then −1) | siege-castles |
| castles_write 0x4ae968 | 0x4784ca, called only from 0x4782c5 = the 0x2b "Battle is won / lost" scroll **running out** (++DAT_00568470 > 5000; retreat / autocalc end through 0x43be65 instead) in a strategic siege with DAT_0057a0d8 (moats filled, 0x47dd86) or DAT_0056d648 (wall sections fallen, 0x47dfe0) ≠ 0. A wall cell (kind 4, +4 height 1..3, +3 > 2) next to a kind-5 cell falls after 16 catapult-stone hits (0x492c8b) | siege-castles |
| castles_read 0x4aea70, 0x4917dd | the next siege battle of that county: 0x4787a4 with county +0x173 == 2 reads castles.dat; cells with +1 bit 0 (a docked siege tower's ramp, 0x491492) → 0x4917dd | siege-castles |
| 0x4a07f2 find_garrison_realm | engine_army_ai_order_garrison 0x4a6270: the army's keep cannot take it (castle level 0, building, or capacity 0x4d8a10 — 150 / 200 / 200 / 400 / 600 men by level — minus the garrison < the army); 0x49f12f musters each garrison to the room free at muster time, so two in a row overfill. AI timing: Summer 1271 in custom-strong's France part left to run | custom-garrison (custom-strong's first 7600 frames + End turn x10) |
| 0x489981 | the group re-target 0x48970e (every 500 ticks, every group): stance 6 (class 8, a siege tower) and DAT_00553fb0 > 6 | siege-castles |

Driving notes: catapults ordered straight from the deploy row walk up to the wall and die; moved first to (target col,
target row + 21) and then ordered onto the wall cell they stay and fire (action 12). A tower ordered onto a kind-4 cell
with height 2 docks (9 cells get +1 bit 0). The siege preparations screen 0x1d appears only once the army reaches the
castle in the same turn (poll for it; an End turn while it walks skips it and the assault comes with no engines).

## Not covered — why

| target | condition / reason | needs |
|---|---|---|
| 0x460a1c, 0x461a54, 0x461efb, 0x4611ef, 0x461623 (drawer_b), 0x460501 / 0x46079d / 0x4608de (empty-cell drawers) | view mode 1 (DAT_0057cb18 = 1): `engine_map_init_set_camera` is called with 0, 2 or the current mode only | dead |
| 0x42f60e (2298 B) | DDERR → index table: DirectDraw failures (callers 0x42dbc7, 0x42ec91) | a rhost fault-injection switch |
| 0x475e07 / 0x476488 | popup types 12 / 20: posted only by MP message handlers (0x4456c1, 0x445960, 0x4b79e3) | MP |
| 0x4b7a9a, DirectPlay dialogs, 0x4472ce's 50 delayed-action handlers, 0x43eda0 / 0x440666 / 0x4b8c3a callees, 0x4b7250 callees, 0x43fd2a / 0x43f4b7, 0x4407e5 / 0x49b973, 0x43e330 / 0x43e433 / 0x43e50d, 0x49cf20 resign | DAT_00553030 (MP session) / DAT_00553248 (host) | MP |
| title pages 6 / 8 / 10 / 11 (0x41f3e9, 0x41f77a, 0x420428, 0x420630, 0x43338e, 0x433551) | MP setup pages, page 10 = the no-CD page | MP / dead |
| 0x4bc8fc / 0x4bcbd1 (zoom-16 battlefield passes) | hex size DAT_004e656c is 32 from its only setter (0x4bc020's callers pass 32) | dead |
| 0x483ce1, 0x484b89, 0x4849fc, 0x484bee, 0x482f86 | unit order table 0x4d9170 entries 7 / 15 / 13 / 16 / 0: never written | dead |
| 0x4a599d lord move | the ally's +0xe8 = our county survives validate, but its deployment needs a muster: home pop ≤ bar/2 with a ≥ 200-man unit (pick_best_unit) or weapons ≥ 100 (0x4d8ac0; the Knight had 0 → 24 in five years) | a long game / an ally with arms |
| 0x48e774 | AI siege **defender** knights (stance 4, table 0x4d91f8): the skirmish siege maps' defender armies (first 10 surveyed) have none; the Cust. page edits only the player's army | a strategic siege of an AI castle garrisoned with knights |
| 0x4ad8cc halve_garrison | aftermath with DAT_0056d5c8 = 1, set by AI siege-attacker knights (0x48d9ce) when DAT_00553f74 ≤ DAT_0057c980 and no wall hit yet | an AI with knights besieging our castle, fought on the field |
| 0x48c3fe, 0x48b7fc | AI defender swords (0x48e4cc) with a breach record (0x48ee46) and a target outside the walls, DAT_00553e64 < 4, not (DAT_00542cd4 && DAT_00553fb0); 0x48a9d2 with DAT_0056d89c set by another AI group | AI micro (A1) |
| 0x485861 | a missile landing on a kind-15 cell (terrain 12 + a feature pick in 0x47aaa3 / 0x47b8b2): only castle maps carry kinds (field battles are all kind 0); the Periwinkle palisade and the skirmish maps have none | a castle map with kind 15 |
| 0x4b42c5 / 0x4b425c (glyph clipped) | DAT_005aeb84 is never set to 1, so glyphs clip only at x = 0 / 640; the siege counter label at the viewport edges stays inside | none found |
| 0x403949 bres_err_update | every gfx_line_draw caller draws axis-aligned lines | dead |
| 0x41e5d0 (screen 0x45, Lords of Magic ad) | DAT_0053f644 ≥ 3 from the pristine lords2.inf | dead under rhost |
| 0x42563c / 0x44015a (debug overlay), 0x4247f5 ("Dumb view on"), 0x440089 / 0x408c50 (byte_5BB4A5) | debug flags with no setter outside MP / never set | dead |
| 0x42e45e | movie subtitles for a non-English L2.eng | dead (this install) |
| 0x42f449 | the windowed palette path (DAT_0053f228 = 0) | rhost is fullscreen |
| 0x43893c / 0x43896a | buttons of 0x437107, which has no caller | dead |
| 0x4344ba / 0x4344d6 | entries 4–6 of screen 0x31's list; it renders 3 (DAT_0052afd0 = 3) | dead |
| 0x434c34 … 0x434c97, 0x434ca2 | option-page button stubs in lists 0x4dde08..0x4ddf00, reached through an index not found | — |
| 0x427d72, 0x425d35 (init), 0x425a6a, 0x4b2730 / 0x4b26b8, 0x42e50d | audio-init failure, screen 0x44, surface loss, WM_MOVE in state 0 (human-traces B8) | dead under rhost |

## Quirks seen

- Realm +0x14 is population / counties, not army strength: a lord who drafts hard (the Bishop: 1076 → 525 souls in one
  season) falls under his own diplomacy bar.
- One diplomatic message per lord per turn (`byte_57BF90`): a second action to the same lord that turn does not open.
- `tools/bot/actions.py` `diplomacy("compliment" / "insult")` never clicked the message's dispatch thumbs (302,308), so
  the RMB that followed cancelled it — bot v3's compliments were never sent. Fixed 2026-11-04 (attitude 5 → 20 checked).
