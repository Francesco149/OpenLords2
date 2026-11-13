# Probe: battles

- **Run:** `battles` · **task:** `tools/drive/tasks/battles.md` · **scenario:** `tests/scenarios/battles/` (blessed wine + win, equal)
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 4, 2026-11-13

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `battles`: `check` PASS, `verify` MATCH.** The movie is 81,228 frames long. It reaches 37 retail blocks no corpus scenario reaches (805 B). 23 of them are in sub_479E90. sub_477DFC, sub_492C8B, sub_459258, engine_combat_transfer_ownership and engine_army_move_region_event_lo are unchanged.

**Start:** `bot3-s103-t36@49860`, not the card's @20000. At @20000 my only army never moved and I couldn't find it on screen. A scan of the full scenario showed battle prompts at frames 21408, 41161 and 49987, so I started on the map just before the last one.

**Steps** (task file `tools/drive/tasks/battles.md`):
- Step 1, on the map: f49861.
- Step 2, on the battlefield: f49990.
- Step 3, back on the map: f71872.
- The title screen was never reached. The movie ends on the map at f81228 (turn 38).

**What I did**
- **Battle 1 (checkpoint `prompt1`, f49987):** the AI (realm 2, 41 mixed men with a knight) attacked my 104 peasants. I took the field and pressed Mop up. It stalled at 5 vs 41, then 5 vs 28, so at f68800 I pressed Autocalc and confirmed. Autocalc lost the battle and my army was destroyed (0 men). Back on the map at f71872 (checkpoint `after1`).
- **Raised a mixed army (f71875–72253):** 82 men: 13 peasants, 27 archers, 42 swordsmen.
- **Battle 2 (checkpoint `prompt2`, f72414):** this army attacked the enemy's 6-man remnant, A(10), in county 1. On the field I:
  - box-selected archers and swordsmen together while paused;
  - recentred on the enemy with the minimap, still paused;
  - unpaused and waited until `0x5533b8` (enemy under the cursor) was set;
  - LEFT-clicked the enemy at f77456. That mixed attack order is the new sub_479E90 path.
- The battle ended at f77799 ("won", screen 43) with 1 enemy man still alive. I right-clicked to end it, then Continue: map at f77810 (checkpoint `won2`).
- **March through foreign fields:** I ordered A(8) onto Burnt Umber's pasture (county 1, owned by realm 2). The destination was stored as (16,40), but the army never moved over 2.5 k frames plus two more seasons (`order` stayed 0). I didn't find out why, so engine_army_move_region_event_lo is still unreached.

**Coverage of the six functions** (unreached blocks before → after)

| function | before | after |
|---|---|---|
| sub_479E90 | 33 blocks / 1048 B | 10 / 395 B |
| sub_477DFC | 33 / 602 B | 33 |
| sub_492C8B | 20 / 560 B | 20 |
| sub_459258 | 91 / 550 B | 91 |
| engine_combat_transfer_ownership | 13 / 536 B | 13 |
| engine_army_move_region_event_lo | 13 / 557 B | 13 |

The other new blocks: sub_48B5FC 8, sub_48A9D2 2, and one each in sub_41298A, engine_battlefield_render_screen_in_grid, sub_43BD02 and sub_46C4A3.

**Situation → branch**

| situation | branch (VA) | status |
|---|---|---|
| Attack order with a selection holding both ranged and melee units (`0x5545ea` ≠ 0 means ranged): the ranged units are split into a new group | 479E90 0x47a053–0x47a2d6 | reached here |
| Order on a cell of kind 15 (`53E874` = 1, probably a castle wall) with ranged units | 479E90 0x47a3e3–0x47a651 | needs a siege battle |
| Order to units in state 10 standing on terrain 6/4/5 (probably walls), which routes through sub_47A814 to sub_492C8B | 479E90 / sub_492C8B | needs a siege, likely defending a castle |
| `5533D0` ≠ 1 | 477DFC 0x477e12–0x477ece | multiplayer / skirmish only (see facts) |
| Surrender flag `56D5C8` (an AI siege attacker with knights) | 477DFC 0x477f04–0x477f82 | not reached |
| Siege, side-wins on `55401C` > 2 | 477DFC 0x478097 / 0x4780b4 | not reached |
| Siege won with `52AFAC` > 1 (skip, then the 5000-tick result) | 477DFC 0x478134–0x4782b6 | not reached |
| My realm captures a county and passes its previous county record (messages 117–126) | transfer_ownership | not reached |
| A march step onto a resource tile of a county I don't own | region_event_lo | the march order didn't execute |
| Sprite clipping at the view edges (a blitter switch) | sub_459258 | not targeted |

**Battlefield facts**
- **Orders are LEFT clicks.** An RMB release deselects. DRIVER.md says "RMB = order"; that is wrong and should be fixed. tools/bot/battle.py already says LMB.
- The order fires only while the battle runs. Hover first, until `u32(0x5533b8)` (enemy under the cursor) ≠ 0. The order counter is word array 0x553100, indexed by player.
- The selection survives the pause toggle (hourglass, 496,464). While paused, a minimap click recentres even with a selection; while running with a selection, it is a march order.
- Panel buttons at y=464: hourglass 496, retreat 528, siege action 560, Mop up 592, Autocalc 624.
- `5533D0` (the combat role) is 2 only when the human defends AND the attacker piece's +1 byte is set (`engine_combat_size_battle`). An AI attacking me still gives role 1, so those 477DFC blocks look multiplayer/skirmish only.
- Mop up can stall indefinitely with a few enemy units (a knight) left.
- A battle can be declared won (screen 43) with 1 enemy man still on the field.
- Autocalc lost 41 peasants vs 6 mixed men, wiping out the army.
- Strategic map: selecting an army opens screen 16. The march destination is set on the click, but here the army did not move.

**What the tool lacked**
- A way to centre the strategic map on a piece. Pieces off screen needed manual edge-scrolling (about 0.7 rows a frame at a border).
- Nothing that says why a march order is accepted but never executed.
- A battlefield helper like `rd cell` for unit positions (hex ↔ pixel).

The rest needs a siege battle with my units on the walls, an AI siege with knights, and a county capture.

---
model claude-opus-5-5, effort high, 153 turns, $3.26, 41 min
