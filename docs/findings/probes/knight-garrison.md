# Probe: knight-garrison

- **Run:** `knightgarrison` · **task:** `tools/drive/tasks/knight-garrison.md` · **scenario:** `tests/scenarios/knight-garrison/`
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 5, 2026-11-13

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

Run knightgarrison: PASS on tools/drive/tasks/knight-garrison.md, verify MATCH, 23,715 frames. Start: custom-strong at frame 13360, not custom-tour@600. siegeknights:t27 was too weak (my only county lost in 2 seasons, the game fell back to the title); custom-strong is already a custom France game with five lords, and an AI knight garrison appears there after 4 idle seasons.

Step frames:
1. Custom game in progress: f13361.
2. AI castle garrisoned with knights: f14931 (turn 5, Winter 1269, 4 seasons). Germany, county 9, the Baron (realm 5), castle level 3 at cell (28,12). Garrison A(10): 196 men = 37 peasants, 50 swordsmen, 106 archers, 3 knights; garrisoned = 9.
3. Assault on the battlefield: f19054 (screen 41, turn 10, Spring 1270).
4. Back on the map: f23655 (final padding to f23715).

Setup that worked:
- Army: raised 150 peasants in Cordova (county 5) with the default draft and Create (580,410).
- March, turns 6-10: an army does NOT keep marching into the next season (order resets, dest kept). I re-ordered it each season: RMB the army, @army_move, edge-scroll to the castle, hover, click. About 15 moves a season.
- Siege: "Besiege castle?" thumbs-up. Siege preparations said "0 seasons" with no engines; Proceed (312,410) brought the battle prompt (screen 18) at once; left gauntlet took the field.
- Battle: ran unpaused about 4,500 frames with no contact. Then Retreat (528,464), confirm yes, result Continue. My army was lost; my county kept.
- Looked at the battlefield frames (castle courtyard, Baron's troops, battle running).

Coverage:
- rd cov: 1026 functions, 1 NEW: 0x48e774 sub_48E774 (324 B, first f19119).
- rd cov --blocks: 15377 blocks, 27 NEW (534 B): 0x48e774 x11, 0x47c4ba x5, 0x4aa6ca x3, one each in 0x47e926, 0x4a8aab, 0x4816f9, engine_combat_resolve_casualties, 0x46d2a3, 0x490a44, 0x47e668, 0x47e7b2.
- Still unreached in 0x48e774: 0x48e7d2, 0x48e7ff, 0x48e815, 0x48e835, 0x48e842, 0x48e85e (72 B). Not investigated; guess: they need the knights to engage.

State named (from the IDA export):
- A(i).garrisoned (piece +0x198) = the county whose castle it garrisons. Written only by engine_combat_place_garrison 0x4a79a3. Callers: engine_combat_reinforce_keep 0x4a78ea (via 0x4686a0, when an army enters its own realm's castle cell), castle completion, one UI path.
- If a garrison exists, the arriving army is merged (engine_combat_merge_stack) only while the total fits capacity table 0x4d8a0c[level] = 150/200/200/400/600 for levels 1-5; otherwise it stops outside. So knights get in when a small knight-carrying AI army enters its own castle (or an empty, newly taken one).
- A taken castle loses its garrison (0x53fb6c cleared, ownership transferred).
- Stance: ~line 69239 of all_ida.c sets group +8 from troop class; class 6 (knights) -> stance 4, so siege-defender knights run 0x48e774.

Game facts:
- Scroll origin (OC,OR as rd cell prints it): cell (c,r) is at x = 30(c-r) - 60*OC + 1920, y = 15(c+r) - 15*OR + 46.
- Edge-scroll works in move mode (right edge OC+, top edge OR-) and speeds up while held.
- Minimap (custom-strong): county capital (c,r) at about x = 548 + 2.03(c-r), y = 11.6 + 1.24(c+r). A click selects the county and recentres but leaves move mode (16 -> 0).
- A marching army gets envoy "violation of our borders" scrolls (screen 39: RMB, or a map popup: LMB) and a one-time invasions hint.
- Raise army starts at 150 drafted in custom-strong.

Tool gaps:
- No way to recentre on an arbitrary cell/piece. @cell / @A only work on screen, and a spec with an off-screen token does nothing at all. Each march order took several scroll-and-measure calls; a "scroll to cell" token would save most of them.
- seasons --until stops mid-season (fine here).
- 4,000 frames wasted before learning marches stop each season; rewound out of the movie.

Checkpoints: kn (step 2 holds), t10, siegeprep, prompt (f19051, the battle prompt).

---
model claude-opus-5-5, effort high, 139 turns, $2.96, 31 min
