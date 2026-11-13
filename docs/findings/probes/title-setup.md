# Probe: title-setup

- **Run:** `titlesetup` · **task:** `tools/drive/tasks/title-setup.md` · **scenario:** `tests/scenarios/title-setup/` (blessed wine + win, equal)
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 4, 2026-11-13

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `titlesetup`** (started from boot-title at f1500). The movie is 16228 frames long. `rd check tools/drive/tasks/title-setup.md` gives PASS: step 1 (screen == 31) holds at f1501, step 2 (state == 3) at f3995, and the final condition (state in (2,3)) holds at f16228, in the "new campaign" game. `rd verify` gives MATCH (rng 4fd1abf2/6a2f1e03, sem 7319ef96ea090bb3). The run was not poked.

Only part of the target was reached: 13 of the 110 untaken target blocks are now covered. Eight are in sub_41608B and three in engine_menu_title_input. Two counted in sub_41608B's 11 → 3 drop are not in the "new" list; probably a corpus re-measure in between (the earlier cov reran a stale baseline). config_set_defaults, sub_41FF75, sub_41E1DD and noble_face_draw did not move.

**Coverage, `rd cov --blocks --fn …`** (blocks reached by neither the corpus nor this run):

| function | before | after | blocks this run reaches first |
|---|---|---|---|
| engine_menu_title_input | 42 / 723 B | 39 / 673 B | 4bb52a, 4bb414, 4bb42d |
| config_set_defaults | 32 / 726 B | 32 | none |
| sub_41FF75 | 21 / 434 B | 21 | none |
| sub_41E1DD | 6 / 572 B | 6 | none |
| noble_face_draw | 8 / 756 B | 8 | none |
| sub_41608B | 11 / 599 B | 3 / 19 B | 8 blocks, 580 B |

Across the whole program the run reaches 10459 blocks, 39 of them new to the corpus (1143 B, +0.14 %). Plain `rd cov` finds 847 functions and none new. The other functions with new blocks:
- engine_title_render_filelist_scroll: 5
- engine_territory_seed_army_features: 6
- menu_action_p4_button: 2
- menu_action_page5: 2
- engine_ingame_input_dispatch: 2
- engine_territory_load_neighbour_keys: 2
- one each: engine_unit_recompute_wage, engine_title_render_filelist_content, engine_load_pump_tick, engine_title_render_choose_title_buttons, engine_title_render_play_now_buttons, menu_action_page1, engine_title_render_option_detail_hover, sub_434174, ev_138

**Steps (frame: what happened)**
- f1500–1536: Single player (320,103) opens the "Your options" page (`title_page` 1→2).
- f1572–1734: Load a game (320,139) opens the file list (page 3) with no saves. I clicked the arrows (491,165)/(491,198), typed "x" and pressed ENTER, which tried to load "xords2" and gave "File error. Operation canceled". Then thumbs-up (415,88) and thumbs-down (463,88), which returns to page 2.
- f1780–1906: Skirmish (320,175) opens page 4 (title and shield). Shield 4, typed a name, Continue (432,227), giving the skirmish page 12 (`word_4DCF50` table).
- f1906–2358: list items, list arrows, Defender/Attacker, Siege/Equal/Unequal, the "user.skr" box (565,399) and the shields/arrows. The user.skr box opens page 13, the skirmish file list.
- f2358–2492: page 13: arrows, a click on the file, then the close seal (513,370), back to page 12. This gives block 4bb52a.
- f2492–2924 (checkpoint skirm12): Cust. (547,448) gives the custom skirmish (`dword_56899C` = 1, table `4DD130`). I clicked the unit arrows, Normal/Hard (342,163)/(342,386) and Defender (283,439).
- f2970: Back (489,448) returns to the title page 1.
- f3088: page 4's Back (208,227) also returns to page 1. An earlier attempt clicked (320,211) next, which on page 1 is Exit game: the session quit, and that command was dropped from the movie.
- f3278: Single, then Custom game, shields 1 and 2, Continue: custom setup, page 7.
- f3314–3554: I opened the Advanced Farming drop-down (page 9). Clicking another tab while it is open switches drop-downs (block 4bb414). Clicking outside closes it (block 4bb42d). Then I picked Difficulty = hard and Starting castle = royal.
- f3554–3943: country list scrolling and selection (Ireland), Defaults (359,208), clicks on noble shields and faces, and Nobles = two.
- f3995: Start (281,208): state 3, Ireland, my county 9.
- f4117–4492: hint scrolls dismissed with RMB.
- f4518–4718: File > Save (48,84) gives screen 54. Thumbs-up saved "xords2".
- f4764–4856: diplomacy icon (623,444) gives screen 11 with the Knight's face. Send an insult gives screen 26; Dispatch (305,307).
- f4902: icon (527,444) is the Court, screen 9.
- f4938–8806: `rd seasons` for 3 + 10 seasons until merchants were in county 9.
- f8811: clicking merchant A(1) opens screen 8.
- f8872–15182: merchant price tooltips. After waiting about 450 frames I hovered timber, then cow, grain, sword, stone, iron, mace, helmet, pike and the cart, about 510 frames each. This gives sub_41608B's 8 blocks.
- f15187–15324: seal, File > New Game, confirm yes (screen 30): back to the title (state 2).
- f15406–15664: Load a game, now with saves: lastturn, old turn, safeturn, xords2. Clicked an entry, the arrows, xords2, thumbs-up: game loaded (state 3; checkpoint "loaded").
- f15820: New Game again, back to the title.
- f15886: Lords of Magic? (320,175): nothing changes on screen in this start without movies.
- f15886–16006: SPACE and ENTER on the title do nothing. Single, then Play Now opens page 5 ("Expansion pack installed, choose:-").
- f16076: The new campaign (448,130), then page 4, shield 5, Continue: state 3 on the Australia map (N. Territory), 5000 crowns.

**Input → branch table**
- **engine_menu_title_input** handles every screen, with `dword_5530F0` as the title page (I named it `title_page`, s32 at 0x5530f0):
  - Page 1 hotspots `4DCB48`: Single / Multiple / LoM / Exit at y = 103 / 139 / 175 / 211.
  - Page 2 `4DCBA8`: Play Now, Load a game, Skirmish, Custom game, Back at y = 103..247, step 36.
  - Page 3 is the load file list (buttons hit-tested by `sub_40DA1E`; ENTER also loads).
  - Page 4 `4DCC20`: 5 shields at x = 144 + 88·i, y 175; Back (208,227) goes to page 1; Continue (432,227).
  - Page 5 `4DCCF8`: Original campaign (192,130) / The new campaign (448,130).
  - Page 7 `4DCD28` / `4DCD88` / `4DCE30`: Cancel / Start / Defaults, country list, 12 option tabs.
  - Page 9 is the open drop-down. Block 4bb414 = clicking a tab while it is open; block 4bb42d = clicking anywhere else (`dword_5530F0 = dword_553E5C`).
  - Page 12 is the skirmish page, page 13 the skirmish file list (block 4bb52a).
- **What is left in engine_menu_title_input:**
  - 4bb05c is page 1 with `dword_568464` (a DirectPlay lobby launch).
  - 4bb17f..1b1 is page 4's `dword_553030` multiplayer branch.
  - 4bb2c3..3a8 is page 8 and 4bb5dd..6a1 is page 11. Both are multiplayer chat pages (text input into `byte_553E80`).
  - 4bb45d..496 is page 6, which needs `dword_553248` (the DirectPlay host).
  - All of these are out of scope (the DirectPlay flags are set only in 0x4b7xxx).
  - I did not map bab37, 4bb038 and 4bb851/86c/876. My guess, assuming the switch cases are laid out in source order: return-1 blocks of in-game screens never clicked (cases 15/21, case 29's two hotspots, and case 49 or 57–68).
- **config_set_defaults** only parses L2.INI / sierra.ini at boot (cdrom, sound channels, physical memory, video speed, CPU). Its branches depend on the host's ini files, not on input, so a trace cannot reach them.
- **sub_41FF75** draws multiplayer chat lines (`dword_553030 && dword_553FC4`, pages 7/8/11/12). It needs DirectPlay, so it is out of scope.
- **sub_41E1DD** is screen 28 (0x1c), victory/defeat. The untaken branches need `dword_55302C == 2` or `dword_53F258 >= 8` (setup2.wav, the campaign's end text), so a full game must be won. Not attempted.
- **noble_face_draw** is the in-game diplomacy screen, not the title. Its untaken blocks are `byte_57BF05 != 0` (face sprite 12, maybe an eliminated lord), the clamp of `+0x0A` to 1..5, and the attitude bar colours above +10 / below −10. I reached the face with neutral attitude only and sent one insult; the attitude did not change before the end.
- **sub_41608B** is the merchant screen's price tooltip. It draws only after a countdown (`dword_53EF54`, 500 frames per draw) and when the hovered good changes (`dword_56D8AC`). Still untaken: 416296 (v0 ≤ 0), 4162aa (good 4) and 4160bd. None of my hovers set good 4.

**Facts learned**
- A Custom game asks for title and shield first (page 4), then goes to setup page 7.
- Skirmish goes through page 4 to page 12. Cust. switches to the custom-army page (unit arrows, Normal/Hard difficulty per side).
- Play Now on this install asks "Expansion pack installed" (page 5). The new campaign is the Australia map (N. Territory).
- Back on page 4 returns to title page 1, not page 2. On page 1, (320,211) is Exit game and quits the process.
- In-game File menu: New Game (48,44), Load (48,64), Save (48,84), Quit (48,104). New Game asks on screen 30 (thumbs (240,218)) and goes straight to title page 1.
- Saving writes into the session's game dir, and the save shows up in the title's load list: lastturn, old turn, safeturn, xords2. The fresh replay still matched.
- The in-game save screen is 54 ("Saving a conquest"): thumbs-up (335,220), then about 90 frames of "Saving game".
- Bottom map icons: (527,444) Court (screen 9), (623,444) diplomacy (screen 11). Insult arrow (415,217) opens screen 26; Dispatch is at (305,307).
- In a custom Ireland game, merchants reached the player's county 9 after about 12 seasons. Clicking one opens screen 8. The tooltip shows buy/sell prices, e.g. "Timber 1/2".

**What the tool lacked**
- No disassembly or block-to-source map, so mapping 10–27 B blocks to C branches is guesswork (a `rd cov --fn X --disasm` would help).
- `rd hot` does not list `sub_40DA1E` buttons (load list, page 13, the skirmish unit arrows).
- `rd eval` has no list comprehensions.
- A click that quits the game drops the command and its live savestates.
- There is no DirectPlay stub path for the multiplayer pages.

---
model claude-opus-5-5, effort high, 121 turns, $2.65, 35 min
