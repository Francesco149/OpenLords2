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

# Attempt 4 (run `siegeknights4`, API driver, 2026-11-14) — blocked

The driver's report, as written:

---

The task is not done. The run `siegeknights4` is blocked after step 2: the surrender flag u32(0x56d5c8) was never set, so halve_garrison (0x4ad8cc) is still not reached. The Baron's knight armies do get sent at us, but they never get past the middle of the map, and our county dies within about 10 more seasons.

**Run state**
- **Start:** `siegeknights3:won2` (f20951, custom-strong, turn 16, Autumn 1271). I picked it because the Baron (realm 5) is already at −30 toward us with vendetta stage 2. We hold Cordova (county 5), with a 187-man garrison (A11).
- **Movie:** ends at f33497, checkpoint `t42` (turn 42). The run was replayed, not poked.
- **`rd check`:** FAIL.
  - Step 1 at f20952.
  - Step 2 at f27051, but this is realm 3's siege of Cordova (no knights). It was auto-resolved at the f27728 prompt; we won and the garrison fell 187 → 18.
  - Steps 3–5 were not reached. The forbid never fired in the kept movie.
- **`rd cov`:** 896 functions, 0 new. I did not run `--blocks` or `rd verify`, because there is no trace worth filing.

**What I did**
1. `rd seasons 33 --feed` from won2: vendetta stage 2 → 3 at turn 29 (pinned target +0xEB = 1, us). The `--until` claimed it held at f36398, but a scan shows the predicate was never true. That is a tool bug.
2. Marched the 118 peasants (A16) into the keep (garrison 136), then ran more seasons. The game ended at turn 57 (f40946): Cordova's population fell to about 13 and we lost our last county.
3. Tried `siegeknights3:fed` (4 wheat fields, 240 grain workers) with `seasons --feed`. The county went neutral at turn 33, with 800–990 grain left in store.
4. Rebuilt the won2 line (seasons 26 → `t42`), traced the Baron's armies, and ran two 4-session searches:
   - From `t47` (254 s, 5.5M frames). Goal: a Baron knights army in county 5. Actions: End turn after waits of 0 / 40 / 120 / 230 frames, RMB, auto-resolve, thumbs-down, a map click.
   - From `t42` (293 s, 6.1M frames), same goal and actions.
   - Neither found it. No Baron knight army ever got below row 32.

**Game facts learned**
- **The Baron's attack on us:** at turn 46 his cooldown (realm +0x15a) ran out. Launch_attack set +0x4b = 5 and raised A26 at (32,3) with target byte piece +0x151 = 5 (us).
  - A26 grew to 417 men: 330 peasants, 54 archers, 6 pikes, 27 knights.
  - It marched (32,3) → (31,13) → (29,24) → east along row 26. Realm 3's raiders use that same route toward Cordova, via (36,24) and county 4.
  - It vanished at (35,26) at f36478, probably in a battle in realm 2's county 1, where realm 2's 758-man A30 and its garrison sit.
- **Other Baron knight armies** (A29: 523 men, 38 knights; A21: 456 men, 20 knights) target realm 3's counties 2, 12 and 13.
- **Why Cordova dies:**
  - The people don't eat the stored grain. In the won2 line, grain stayed at 414 for 20 seasons while the population fell 911 → 78. In the fed line, 800+ sacks sat unused.
  - The ration panel at turn 42 showed "Wanted Quarter", "Fed 168 / Eaten 28 grain" for 653 people, with 256 sacks in store and 0 grain workers.
  - Health 0 then kills hundreds a season. `--feed` only changes ration, meat % and tax; it does not fix this.
- **The battle looks out of reach anyway:** even if A26 had arrived, its ~390 non-knights would have to die while its engines live and before any breach. Our garrison was 18 (136 after the march), and a keep holds at most 200.
- **Code (port `src/engine_army_ai.c`):** target = the most hostile player (the pinned +0xEB, else the lowest attitude below −10). It is scored by Chebyshev distance from the muster county's capital (+40 against a defended keep, must be under 128). The army is raised at the muster county (realm +0xE5; needs ≥ 400 people and happiness ≥ 25). Order 7 re-points it every season at the nearest deployable cell of target +0x151 (0x4a58f4 / 0x4a689d).
- **0x48d9ce condition:** 0x553F74 and 0x57C980 are recounted every frame (all_ida.c:69761–69800). The condition is live: every remaining AI side-0 man of class < 7 must be a knight.

**What would work next:** a start where our castle sits right next to the armour AI, so its knight army doesn't cross other lords' land. Alternatively, find out why Cordova's grain goes uneaten (the "Fed" cap) so the county lives 20+ more seasons.

**What the tool lacked**
- `seasons --until` reports "held" when the predicate was never true.
- `--feed` can't solve the grain-not-eaten collapse (no worker or sowing upkeep).
- There is no way to see who destroyed a piece (battle log / `rd text` for past frames).
- `rd eval` has no list comprehensions (`--each` works).

---
model claude-opus-5-5, effort high, 50 turns, $1.46, 18 min

**Orchestrator, after:** the false "held" was `Live.prog`'s id reuse (fixed: pinned programs); the uneaten grain was `--feed` choosing quarter rations from sacks counted as heads (fixed: 6 fed a sack; health then held 2–3 for 20 seasons on the same line).

# Attempt 5 (run `siegeknights5`, API driver, 2026-11-14) — steps 1–3 held, blocked on the battlefield

The driver's report, as written:

---

**Run `siegeknights5`: blocked. Steps 1–3 hold; steps 4–5 were not reached.** The surrender flag u32(0x56d5c8) was never set, so halve_garrison (0x4ad8cc) is still not reached. The movie ends at f86534 on the battlefield (screen 41) with the siege battle still running and nothing breached. I ran out of budget before running `rd check`, `rd verify` or `rd cov`, so the frame numbers below are from my own commands, and there are no coverage numbers.

**Setup that worked (custom-tour@600)**
- Page 7 clicks: Single (320,103), Custom (320,211), Continue (432,227).
- Options, each tab then its value:
  - Weapons "many"
  - Army Size "large"
  - Difficulty "hard"
  - Starting Castle "royal"
  - County Status "strong"
  - Crowns 5000
- Map **France** (548,197), *then* Nobles "two" (218,442 → 218,401), then Start (281,204).
  - Picking a map resets Nobles to five, so set the nobles last.
  - The noble roster order is fixed per map. France puts the **Baron** first, so with two nobles the only AI is the Baron (personality 2).
- Game start f1757. We hold county 10, capital (26,20), royal castle at cell (26,22). The Baron holds county 1, capital (51,51). Every other county is neutral, so no third lord is in the way.

**Step frames**
- **Step 1 (custom game on the map):** f1757.
- **Step 2 (AI army besieging us):** held at f76851, turn 47. The Baron's A15 besieges county 10: 916 men (516 peasants, 330 archers, 70 knights) with 3 engines.
- **Siege prompt:** "The Siege commences", screen 18, at f77113.
- **Step 3 (on the battlefield):** left gauntlet at f77116, screen 41 from f77116.

**Checkpoints:** start (Scotland), france, insult1, hostile, raised, won1, won2, war, gar450, siege, prompt, field (f77118). Load `prompt` or `field` to retry the battle.

**How it got there (about 47 seasons)**
- Seasons 1–16 with `rd seasons --feed`.
- Garrisoned the starting 300-man army A7: select it, then click the castle cell (26,22), then confirm "Garrison castle?" with thumbs (240,218).
- Two insults at f20907 and f21302: the Baron's attitude to us, s8(0x57c254), went 5 → −15 → −30.
- **Raiders arrived at once.** In a first try without a field army, Baron raiders camped in our county from turn 20, ate the herd, and the population fell 1630 → 109 by turn 49. I loaded `hostile` and raised a field army A10 instead (150 men: 100 crossbows, 50 peasants).
- **Three won raider battles gave the full vendetta.** Each was A10 left-clicked onto a raider, then auto-resolve. The vendetta byte u8(0x57c257) went 1 at f29044, 2 at f43423, 3 at f63202, with the war byte and target +0xEB = 1 (us).
- Garrisoned A10 too, so 450 men are in the royal keep.
- About 5 seasons later the Baron's knight army besieged us.

**The battlefield (where it stands)**
- **Three counters stay fine:**
  - u32(0x553ff0) (engines on the field) = 3, still alive.
  - The attacker's kit u32(0x55401c) = 4.
  - The breach count u32(0x53e990) = 0 throughout.
- **The AI never attacks unprovoked:** after 5,000 ticks it still sits about 25 hexes south (rows 56–68) of the walls.
- **The obstacle:** the AI's non-knight men (u32(0x553f74)) never dropped, 916 against 70 knights (u32(0x57c980)), so the flag condition was never close. 846 non-knights must die before any breach.
- **Our melee cannot get out:** sent out, it stops in the closed gatehouse and gets shot (450 → 422).
- **The siege-action button (560,464), sub_43BBE7 → sub_496B9F, opens the gate, but that adds +4 to 0x53e990 (a breach) and kills the flag.** So no sally is possible.
- The movie ends with our keep archers (units 41–43) marching toward the gatehouse to shoot from the walls (f86534).
- Not yet tried: bait the AI with ranged hits so it assaults into fire from the walls, without hitting the knights. My reading is that the moat may stop its towers and ram, so the infantry might die at the moat without a breach — not checked.

**Game facts learned**
- **AI armour comes only from merchants.** engine_army_ai_reconcile_resources (0x49e39b) buys good v3 from a merchant in the county. v3 is chosen by the county byte +0x290 (byte_53FC40), which engine_army_ai_run_build_orders sets from the per-personality table 0x4d8aa8 + 240·(personality−1) at offsets 0/4/8/12/16/20, in a 10-step cycle (realm +0x6c).
  - Value 5 = armour (good 14). Only personality 2, the Baron, has it often: 3,5,4,4,3,5.
  - Personality 4 (the Bishop) never has it, and its armour stayed 0 for 30 seasons (my first, Scotland game).
  - Realm armour is dword 0x57c054 + 0x160·p.
- **Royal castle siege, unit layout:** units at 0x554490 + 432·i.
  - AI: unit 1 is class 9, units 2–4 knights, 5–15 archers, 16–32 peasants, 33–34 class 8.
  - Ours: units 35–40 are class 10 on the towers, plus archers, crossbows, peasants, swords, pikes.
  - Royal castles have a moat.
- **A raider's arrival depopulates a county within seasons.** Kill raiders with a field army as they arrive; each win also advances the vendetta.
- `C(k).castle` = 5 for a royal castle, which holds 450 men without complaint.

**What the tool lacked**
- `rd eval` has no tuples.
- `rd seasons --feed` leaves the ration panel open (screen 25) when `--until` stops it.
- There is no battlefield helper for selecting by unit class (classes / groups → screen boxes).
- There is no way to order attacks on a specific enemy unit without hand-computing the camera.

---
model claude-opus-5-5, effort high, 177 turns, $5.10, 43 min
