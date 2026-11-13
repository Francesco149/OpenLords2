# Probe: tooltips

- **Run:** `tooltips` · **task:** `tools/drive/tasks/tooltips.md` · **scenario:** `tests/scenarios/tooltips/` (blessed wine + win, equal)
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 4, 2026-11-13

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

The run `tooltips` passes and replays correctly: 31 of the function's 54 untaken blocks are now covered. The other 23 are 17 dead jumps and 6 blocks that need a condition this start cannot give quickly.

**Run:** `tooltips`, from custom-strong@6000, 8775 frames. `rd check` → PASS (step 1 "in game on the map" at f6038, step 2 "still in game" at f6039; forbid `my_counties == 0` never held). `rd verify` → MATCH (rng 2056bdec/70fd65bd). Checkpoint `hud_done` is at f8775. I looked at f7881: the labour-mode minimap tooltip "Labour, red if needed, purple if idle." is on screen. Nothing was poked; there is no conquest and no End turn.

**Coverage, `rd cov --blocks --fn engine_tooltip_classify_strategic`:**
- Before: 54 untaken blocks (525 B).
- After: 23 left (143 B).
- This run adds 32 blocks new to the corpus (392 B): 31 in this function, plus 1 in `engine_tick_event_popup_step` (0x472e46).
- `rd cov` at function level: 786 functions reached, 0 new.

**How the function works** (0x477320, read from the IDA export plus raw code bytes):
- `dword_4E65FC` is the cursor x and `dword_4E65F4` the cursor y. Only the right-hand panel counts: x ≥ 478, y ≥ 24.
- The tooltip appears after 1000 ms of rest (`sub_477131`), so I hovered with `mX,Y .*70`.
- The result goes to `dword_4EAC00`, the tooltip id (worth naming as a var).
- `dword_57A0C4` is the minimap overlay mode: 0 normal, 1 labour, 2 rations, 3 happiness. Clicking a side button sets it (`sub_43AB76`).
- The rows at y 300..429 come from per-county "present" lists (built by FUN_0040fec1).
  - Left list (`dword_53F690`): cattle +0x200 → type 1, grain +0x201 → type 0, reclaiming +0x204 → type 2.
  - Right list (`dword_553FD0`): flags +0x297 / +0x2af / +0x2df / +0x2c7 / +0x1c3 → types 6 / 4 / 5 / 7 / 3.
  - The row heights (`dword_53E970` / `dword_53F04C`) and counts (`dword_553C60` / `dword_5440B4`) are what the classifier reads.

**Hovered thing → branch → id (frame) → tooltip text:**

| Hover | Branch | Id (frame) | Text |
|---|---|---|---|
| minimap (540,80) | x < 609 | 1 (f6105) | "Kingdom view. Click on a county." |
| side button (625,40), mode 0 | 24 ≤ y < 62 | 2 (f6164) | not captured |
| side button (625,78), mode 0 | 62 ≤ y < 94 | 3 (f6223) | "Ration status" |
| side button (625,110), mode 0 | 94 ≤ y < 125 | 4 (f6282) | "Overall happiness" |
| magnifier (625,140), mode 0 | y ≥ 125 | 5 (f6341) | not captured |
| (625,140), mode 1 (after c625,40) | mode ≥ 1, y ≥ 125 | 31 (f7797) | "Return census map to empire mode" |
| (625,40), mode 1 | — | 2 (f7856) | "Labour, red if needed, purple if idle." |
| (625,40), mode 2 (after c625,78) | — | 3 (f8013) | not captured |
| (625,40), mode 3 (after c625,110) | — | 4 (f8157) | "Overall happiness" |
| population (500,180), own county | y < 210, x < 547 | 6 | "View population report" |
| (558,180) | 547 ≤ x ≤ 570 | 34 | "The health of the county" |
| (600,180) | x > 570 | 7 | "View happiness report" |
| tax (500,230) | y ≥ 210, x < 547 | 8 | "Set tax rate" |
| (558,230) | 547 ≤ x ≤ 570 | 34 | "The health of the county" (second block) |
| (600,230) | x > 570 | 9 | "Set rations" |
| labour arrow (550,275) | 250 ≤ y < 300 | 10 | "Adjust labor allocation" |
| cattle row (520,320) | left list, type 1 | 15 | "Cattle, and change next season" |
| grain row (520,380), also (520,420) | left list, type 0 | 16 | "Wheat, and change next season" |
| (520,428) | left row 2 ≥ count 2 | 0 | — |
| wood row (600,320) | right list, type 6 | 18 | "Wood produced next season" |
| (600,390), (600,428) | right row ≥ count 1 | 0 | — |
| (490,445) | 430 ≤ y < 460, x < 512 | 11 | "Create an army" |
| (525,445) | 512..544 | 12 | "Go to treasury" |
| (560,445) | 545..576 | 13 | "Send supplies" |
| (590,445) | 577..607 | 32 | "Build or update a castle" |
| (625,445) | x ≥ 608 | 33 | "Diplomatic initiatives" |
| (550,470) | y ≥ 460 | 14 | "End your turn" |
| (520,200), (550,275), (520,350) with county 10 selected (owner 2, via minimap c555,75) | `C(sel).owner != me` | 0 | — (the not-your-county return-0 blocks) |

**The 23 blocks still untaken:**
- **17 dead blocks:** 5-byte jumps right after a return that nothing jumps to (0x4773da, 0x47748c, 0x4774ab, 0x4774ba, 0x4774d9, 0x4774f8, 0x4775e1, 0x477601, 0x477621, 0x47767e, 0x47769e, 0x4776be, 0x4776de, 0x47772f, 0x47774e, 0x47776d, 0x47778c).
- **0x477606 / 0x477617 (left type 2, id 17, "reclaiming"):** these need an own county with +0x204 ≠ 0. My only county, Poitou (5), has 0 reclaiming and 0 barren fields. Only county 12 has the flag, and it is neutral (capital (34,35), no garrison, about 1278 people).
- **0x477694 (right type 5, id 19):** only county 1 has +0x2df set, and it is held by realm 4 with a 196-man garrison.
- **0x477596 / 0x477633 (row height 0):** these need an own county with an empty left or right list.
- **0x4776fe (a type outside 3..7):** apparently unreachable.

I did not attempt a several-season conquest of county 12 for 2 blocks (27 B): the outcome was uncertain, and the reclaiming could finish before the county was taken.

**Game facts learned:**
- Tooltips are still classified and drawn while an event scroll is up (`modal` 179).
- A minimap click selects a county (`sel_county`) and recentres the map.
- After you dismiss a scroll with a left click, the next RMB on the map does nothing, as the card says.
- The side buttons switch overlays: button 1 labour, 2 rations, 3 happiness, and the bottom button returns ("Return census map to empire mode").

**What the tool lacked:**
- No disassembly view: I decoded block contents by hand from `rd ram read` of the code bytes.
- `rd eval` cannot do list comprehensions, so finding a county index took a binary search over `count(C, … and I < N)`.
- There is no per-frame tooltip text capture: later text overdraws earlier tooltips, so three texts (ids 2 and 5 in mode 0, id 3 in mode 2) were not captured.
- A var for `dword_4EAC00` (tooltip id) and one for `dword_57A0C4` (minimap mode) would help.

---
model claude-opus-5-5, effort high, 45 turns, $0.81, 7 min
