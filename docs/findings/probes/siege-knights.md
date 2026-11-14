# Probe: siege-knights (attempt 2 — blocked, not filed)

- **Run:** `siegeknights2` (furthest checkpoint `t33`, f32928) · **task:** `tools/drive/tasks/siege-knights.md` · **scenario:** none
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 5, 2026-11-13, $5.23
- Attempt 1 (campaign 2, run `siegeknights`, checkpoint `t27`): AI knights take years; that start's county died in 2 seasons.

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `siegeknights2`: blocked.** Only step 1 holds: a custom game in progress (state 3, screen 0), from frame 13360. Steps 2–5 were never reached: no AI army with knights besieged us, there was no siege battle on the field, and the knights' flag `u32(0x56d5c8)` was never set. I did not run `rd check` or `rd verify`. `rd cov`: 911 retail functions reached, 0 new to the corpus. I did not run `rd cov --blocks`.

**Where the run stands:** loaded back to checkpoint `t33` (f32928) and then 210 frames of ration-panel clicks (now f33138). It is Winter 1276, turn 33. Cordova (county 5) has 656 people, a castle level 3 and a garrison of 75 men (A13). Realm 5 (the Baron) is at attitude −30 toward us, vendetta stage 1. Other checkpoints still there: `farm`, `split`, `ordered`, `t30`.

**What I tried, in order:**
1. **Started from `siegeknights:t27`.** That start can't be saved: county 13 has 9 people and 0 happiness, mobs were already forming, and it went neutral within 2 seasons. I abandoned it.
2. **Switched to `custom-strong`** (France map, five lords, we are Cordova). Reasons: step 1 already holds there, it has the most lords, and every county starts big.
3. **Fed the county** (f16262–17035): moved all 861 idle workers to grain, sowed 5 wheat fields in Winter 1270. Later moved the ration slider to bread (meat 100% → 11%) at f19629.
4. **Picked the AI.** Realm 5 (the Baron) is the only armour economy: armour 12 → 82 by 1275, and it recruits knights (39–80 at a time). Realms 2 and 3 never made armour; realm 4 only reached 4.
5. **Insulted realms 4 and 5.** My first insult (f19830) went to realm 4 by mistake: the Knight's face is realm 4. Realm 4 dropped to −15. The Baron (realm 5, bottom face) was insulted at f20254 and f24146 and reached −30 at f24771.
6. **Raised a garrison:** 201 men (50 swords, 50 pikes, 50 bows, 51 peasants). At 201 the army would not enter the keep and no split prompt appeared. I split off the 51 peasants (f25900) and the 150-man army garrisoned at turn 21.
7. **Marched the peasants toward the Baron's Germany** to provoke him. The first long order stopped after 4 cells. The second ended in a lost battle at f28093 (51 → 5 men). At that same frame the Baron's vendetta stage went 0 → 1; I don't know what triggered it.
8. **Realm 3 besieged us** with 305 men and no knights (turns 30–33). Auto-resolve won, leaving 75 in the garrison.
9. **Then the county died.** Realm 4's army (286 men, angry from my mistaken insult) sat in Cordova from 1274: the herd went to 0 and health fell to Sick. Population went 888 → 20 by turn 43, and the county went neutral at turn 46 with the castle still garrisoned (75 men).
10. **A 290 s search over seasons** (4 sessions, 5.9M frames) toward step 2 could not get past turn 45, because the county is lost at turn 46.

The Baron did choose county 5 as his target twice and launched an attack around turn 41, spending his armour 31 → 0 on knights. But his knight armies (A26: 275 men, 39 knights) stayed fighting realms 3 and 4 in the north and never came south.

**Game facts from the decompile:**
- **0x48d9ce (`sub_48D9CE`):**
  - It is a per-unit AI step that counts `word_56653C[26·unit]` up to 200 while the unit has no target (`byte_56652D == 0`).
  - At 200 it sets `dword_56D5C8 = 1` and `dword_5656F8` = the unit's owner, if both of these hold:
    - `dword_553F74 ≤ dword_57C980`: in the setup loop at all_ida.c:69770, 0x553F74 is the men of side-0 units, and 0x57C980 is the side-0 knights (type 6). Type 0 is peasants, types 1/5 are ranged (into 0x56D6AC). So the condition means every side-0 man left is a knight.
    - `dword_53E990 == 0`.
- **0x4ab383 aftermath:** `engine_combat_halve_garrison` is called on the LOSER whenever the flag is 1. The loser then survives if it keeps ≥ 50 men; otherwise message 288 and the army is disbanded. After that comes `engine_diplomacy_battle_adjust(loser owner, winner owner, 20)`.
- **`engine_diplomacy_battle_adjust` 0x4a1ee1 (a1, a2, amount):**
  - Lowers a1's attitude to a2, clamped to −30..30.
  - When the result is −30 and a2 is human, each call moves the vendetta byte at +0x87+0x10·q one stage: 0→1 (message 189), 1→2 (190), 2→3 (191; sets the war byte +0x88+0x10·q and the target +0xEB).
  - If a2's ally byte (+0x81) is a1, it goes straight to war.
  - Callers: battle loser (−20); a human army stepping onto a passable cell of a foreign county (all_ida.c:57050, −10 per step, cell decayed); line 58089 (−20); running into a kind-4 piece (−5).
- **AI attacks:** a vendetta is not needed. `engine_army_ai_launch_attack` 0x4a0015 takes `most_hostile_player`, which is the vendetta target, else the realm with the lowest attitude below −10. It then calls `select_target`, which with a4 = that realm scores only its counties, at any distance. An army is raised when the muster county (realm +0xE5) has ≥ 400 people and its 53F9BC byte is ≥ 25. The cooldown is realm +0x15a (≈ 10 seasons, Baron). The current target county is +0x4b.

**Notes for the card:**
- In custom-strong the diplomacy faces Bishop/Countess/Knight/Baron are personalities 3/4/1/2. Realm 4 is the Knight, realm 5 the Baron.
- A 201-man army is silently refused by a level-3 keep. The card says a split prompt appears; none did.
- "At 0 people a county stays yours while its castle is garrisoned" did not hold: it went neutral at 4 people and happiness 8 with 75 men inside.
- The rations panel caps how many people bread feeds (300 of 656 with 1552 grain in store). I don't know the cause.
- A hostile AI army camping in your county eats the herd and depopulates it. Don't insult anyone you can't fight.

**Map / UI mechanics:**
- A minimap click leaves army-move mode (screen 16); edge-scrolling keeps it.
- Screen position from cell: x = 30(col−row) + 1920 − 60·ocol, y = 15(col+row) + 46 − 15·orow, where (ocol, orow) is the "scroll origin" `rd cell` prints.
- Edge-scroll speed: about 0.21 ocol a frame at the right edge, and about 0.44 orow a frame at the bottom edge (top edge in reverse).
- A march onto woods is refused silently, and the screen goes 16 → 0 anyway.

**What the tool lacked:**
- `rd cell` could also print the scroll needed to bring a cell on screen.
- A `seasons` mode that keeps a county alive (food / ration checks).
- `rd text` for frames in the past, to see what triggered an event.

**What to try next:** from a Spain-type start the Baron is too far away. Pick a start where we hold a castle next to the armour-building AI. Get its attitude below −10 with insults only, and leave every other lord alone. Then wait for the knight army with the cooldown and muster rules above in mind.

---
model claude-opus-5-5, effort high, 194 turns, $5.23, 78 min


# Campaign 6 attempt (run `siegeknights3`, API driver, 2026-11-14) — blocked

The driver's report, as written:

---

**Run `siegeknights3`: blocked.** Only step 1 holds. No AI army with knights besieged us, so the flag u32(0x56d5c8) was never set and halve_garrison (0x4ad8cc) was not reached. I did not run `rd check`, since steps 2–5 do not hold in the current movie, and I did not run `rd verify`.

**Where the run stands:** I loaded it back to checkpoint `fed`, so the movie ends at f22084: custom-strong, turn 17, Winter 1272. Our county is Cordova (county 5): 727 people, 4 wheat fields sown, meat ration 23%, a garrison of 187 men (A11). The Baron (realm 5) is at attitude −30 toward us with vendetta stage 2. Other checkpoints still there: t3, t6, won1, won2, sown.

**rd cov:** 903 retail functions reached, 0 new to the corpus. I did not run `--blocks`.

**What I tried:**
1. **Started from `siegeknights2:t30`** (f31520). Step 2's predicate already held there: realm 3's A16 (305 men, no knights) was besieging Cordova.
   - The siege battle prompt came at f32753 (turn 33). The left gauntlet put us on the battlefield at f32756 (screen 41), so step 3 also holds on that branch.
   - The flag cannot be set in that battle. The attacker has no knights, so it has no stance-4 group (see the first code fact below).
   - That county then dies regardless: realm 4's 286-man army camps in it, health drops to 0, and the county goes neutral at turn 46. I abandoned that branch.
2. **Started over from `custom-strong`** (f13360):
   - **Insults:** two insults to the Baron (f13530, f15012) put him at −30 by turn 4. Diplomacy is the map icon c623,444, then face (90,390), insult (410,218), Dispatch (303,307).
   - **Garrison:** raised 187 men (50 swords, 50 pikes, 50 bows, 37 peasants) and marched them into the level-3 keep. The march is a left click on the army, then a left click on the castle cell. They garrisoned at turn 2.
   - **Vendetta:** Baron raiders (40–46 peasants) come to Cordova and eat the herd. I beat two of them with freshly raised peasant armies (order: left click our army, left click the enemy piece; then auto-resolve). The Baron's vendetta stage went 0→1 at f16694 and 1→2 at f20951. One more win should give stage 3 (war, target = us).
   - **Food:** the diet starts at meat_pct 100, and with the herd gone the people starve with grain unused in store. The ration panel is c600,218; dragging its slider `d324,230-222,230` sets meat to 23%. I sowed 4 wheat fields in Winter (field click, then @field_wheat) and moved 570 idle workers to grain in the Town Center (box drag, then a click on the wheat field).
   - **Still collapsing:** health fell to Sick and then 0, and crowns went 1000 → 16. In the season run from `fed` (since cut from the movie), the armies deserted by turn 26 and population was down to 24. The Baron chose Cordova as his target at turn 13 and again at turn 24, but only raiders arrived, and no knight army came before the county failed.

**Game facts from the decompile — these correct the task card:**
- **Knights are required after all.** A stance-4 group, whose handler 0x48d9ce is the only code that sets 0x56d5c8, is created only for unit class 6, the knights (sub_480662, all_ida.c:69239). The siege army I fought had attacker stances 1/2/3/5/6 and no 4.
- **"Only engines left" cannot work.** 0x477DFC ends the battle as soon as the attacker's class<7 men (0x53F028) reach 0. There is only a narrow window: 0x481B9A counts once per frame, and 0x489401 runs before 0x477DFC in each sub-step. It would need a stance-4 group anyway.
- **Killing the AI's engines kills the flag.** Each siege sub-step with both sides alive, 0x477DFC checks 0x53E990 == 0 and 0x553FF0 == 0. If the attacker's kit 0x55401C ≤ 2, it sets 0x53E990 = 0x553FB0 = 4, and 0x48d9ce then refuses the flag. If the kit is > 2, the battle ends as a defender win instead.
  - 0x553FF0 was 5 at the start, matching the AI's 5 engine units.
- **AI besiegers do bring engines.** Realm 3's army had 3 class-7 and 2 class-8 units, with kit 0x55401C = 2. Our garrison had 3 class-10 units.
- **Battlefield unit layout:** units at 0x554490 + 432·i, with class at +2, the side flag +3 (0 for the AI) and owner at +0x1c. Units 1–46 were the AI's, 47–70 ours.

**What the task needs:** a Baron army with knights besieging our castle, and on the field its non-knights killed first while its engines are left alive. Before that, the county has to stay alive for 20+ seasons, which none of my attempts managed.

**What the tool lacked:**
- `rd seasons` stopped mid-processing of an End turn ("the bot's settling stopped as it held"). My next panel clicks then landed while the season was still ending.
- There is no county-upkeep helper (diet, food, bankruptcy) for long `seasons` runs.
- `@raise_more` is wrong when the mercenary box is shown: the arrow moves to y=174 and Continue to (490,362).

---
model claude-opus-5-5, effort high, 141 turns, $3.33, 39 min
