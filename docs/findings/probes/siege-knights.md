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

# Attempt 6 (run `siegeknights6`, API driver, 2026-11-14) — the battle from siegeknights5:prompt; blocked by ram mode

The driver's report, as written:

---

**Run `siegeknights6`: blocked. Steps 1–3 hold, steps 4–5 were not reached.** The surrender flag u32(0x56d5c8) was never set, so halve_garrison (0x4ad8cc) is still unreached. The movie is 109,936 frames, `rd verify` gives MATCH, and the battle is still running at the end with no breach. I did not run `rd check`, because it would fail on steps 4 and 5.

**Start:** `siegeknights5:prompt` (f77113, screen 18), as attempt 6 asks. This is the Baron's A15 besieging our royal castle in county 10: 516 peasants, 330 archers, 70 knights, 2 siege towers and 1 ram.

**Step frames:**
- Step 1 (game on the map): carried over from siegeknights5, f1757.
- Step 2 (AI besieging us): carried over from siegeknights5, f76851.
- Step 3 (on the battlefield): left gauntlet at f77116, screen 41 from then on.

**Where the AI army stands at the end:**

| | start of battle | end (f109936) |
|---|---|---|
| AI non-knights, u32(0x553f74) | 916 | 210 (144 archers left) |
| AI knights, u32(0x57c980) | 70 | 66 |
| Our garrison | 450 | 375 |
| Engines alive, u32(0x553ff0) | 3 | 3, but towers at 7 and 1 hp, ram at 25 |
| Breaches, u32(0x53e990) | 0 | 0 |

**Checkpoints:**
- `nopeas` f95878: all peasants dead, engines whole.
- `t2` f98246, `t3` f102787.
- `end` f109936.

**Why the AI never assaults (decompile, named state):**
- **Ram mode, u32(0x4eeb6c).** It is set at battle setup (all_ida.c:67608) as rand < v3. v3 is 128 if the army has a ram (piece +0x17E), minus 20 per tower (+0x17C), minus 10 per catapult (+0x17A). Here it is 1, the same for every press frame 0–60 on the prompt. It is cleared only when no class-9 unit (the ram) is left (all_ida.c:69865).
- **What ram mode does.** Advance orders (sub_48D013, sub_48BCFB) send every group to a staging point: (u32(0x55cdd0)+2, u32(0x55cdd4)+12) = (39,65), about 12 rows south of the gate.
- **The real attack branches need a breach.** They all require u32(0x553fb0) > 2 and u32(0x53e990) != 0. So in a battle with no breach, the AI just stands and shoots.
- **What u32(0x553fb0) counts.** It is +1 per moat or wall-edge cell that turns into ground (sub_47DD86): peasants dying in the moat fill it. At 8 or more the ram's stance 7 (0x48dfbb) sends the ram to the gate. Above 10 the towers' stance 6 (0x48ddc7) look for a wall to dock at.
- **Stance tables.** Siege-attacker stances come from the table at 0x4d91d0: 1 = 0x48d16e (archers), 2 = 0x48d412 (peasants), 4 = 0x48d9ce (knights), 6 = 0x48ddc7 (towers), 7 = 0x48dfbb (ram).
- **The flag.** 0x48d9ce sets it when 0x553f74 <= 0x57c980 and 0x53e990 == 0, on the knights group's 200-tick clock while +0xd is 0. Once set, sub_477DFC ends the battle at once as a surrender, and the aftermath (all_ida.c:85755 / 85798) calls halve_garrison whichever side won.
- **Engines must stay alive.** If 0x553ff0 reaches 0 with no breach and kit 0x55401C > 2, the defender wins at once (all_ida.c:65653).

**What worked on the field:**
- **Peasants:** our crossbows and archers, marched by minimap click onto the south-west tower (33–35, 54–56), killed all 516 while the peasants waited at the moat bank. This took about 14,000 frames, and we lost only about 30 men (f81000–f95700).
- **Archers:** moving our shooters to the south-east tower (42–45, 54–55) brought the AI archers from 297 down to about 144.

**What blocked it:**
1. **Dead peasants filled the moat** (0x553fb0 went 0 → 12 from f84600), which woke the engines. The towers stuck at the moat bank and never docked.
2. **Tower 33 was in the ram's way.** Our units shot it on their own; once it died (f102351) the ram walked to the gate at (39,56) and started ramming. Gate damage u32(0x568da4) climbed about 400 per 20 frames, passed 20000, and gave the +4 breach at f104529. I loaded back to `nopeas` and avoided this on the second branch.
3. **Our units auto-target the nearest enemy, and the engines are always nearest.** An explicit attack order on an AI archer out of range makes our units leave the wall and pile up at the closed gatehouse, where they do nothing.
4. **The AI archers stay put.** They hold (45–50, 63–65), which only the south-east tower can reach, and tower 33 at (41,57) is closer to that tower than they are. The remaining 144 cannot be killed without also killing the engines.

**Advice for the next attempt:**
- **Best start:** a siege where the attacker has no ram, so ram mode is off and the AI comes to the walls. Or a castle without a moat (no moat filling).
- **Resuming from `nopeas`:** kill the ram first while it is stuck (but slowly: about 1 hp per 300 frames). Leave a tower with hp that is out of our range. Then shoot archers from the south-east tower rim around (48,55).
- **A tool would make the battle playable:** a battlefield helper that orders a unit to attack a given unit index, plus a "hold position / fire only" order.

**Game facts learned:**
- Battlefield group IDs get reused: a new drag selection takes a free group number, such as an emptied AI group 5.
- Unit +0x164 is the target unit and +0x21 the state (1 move, 3 approach, 5 fire, 14 ramming, 17 AI attack).
- Our class-10 tower crews can be selected and wander off; one died.
- ESC on the battlefield opens "Exit the game?". Its thumbs-down at (285,222) only registers if mouse moves follow the click.

**rd cov:**
- Functions: 1060 reached, 0 new.
- Blocks: 15987 reached, 101 new to the corpus (1608 B). The main ones:
  - siege AI stances: sub_48DDC7 (13 blocks, f79561), sub_48D412 (5), sub_48D9CE (2, f79361 — the flag store itself not reached), sub_48DFBB (2, f88040)
  - movement / targeting: sub_48BCFB (4), sub_48D013 (1)
  - other battlefield code: sub_49459A (6), sub_492C8B (7), sub_478987 (3), sub_490FA2 / sub_491147 (2 each)
  - sub_453E71 (29 blocks, f17480), from the inherited setup.

**What the tool lacked:**
- `rd cell` and `@A` do not work on the battlefield. I worked around it with `rd ram define` expressions that turn a unit index into screen coordinates (`bf_tgt`, `bf_tx`, `bf_ty` in this run's vars).
- There is no "hold position" or attack-unit-N command.
- `--until` fires on the transient screen 42 during a drag, which stopped two plays early.

---
model claude-opus-5-5, effort high, 116 turns, $3.60, 27 min

# Attempt 7 (run `siegeknights7`, API driver, 2026-11-14) — REACHED (filed as tests/scenarios/siege-knights)

The driver's report, as written:

---

**Run `siegeknights7`: halve_garrison (0x4ad8cc) is reached now, at f119997.** The surrender flag u32(0x56d5c8) went to 1 at f118880, the aftermath ran, and we are back on the map. `rd verify` gives MATCH and the movie is 120,363 frames.

`rd check` gives **FAIL**, but only because of where the run starts. It judges steps only after this run's start_frame (109936), and steps 1–2 happened earlier, in the movie this run inherits. Within my own frames, step 1 holds at f120303 and steps 4 and 5 happened (flag f118880, map f120303). A PASS would need a run that starts before f76851 and replays the whole battle.

**Start:** `siegeknights6:end` (f109936), not custom-tour@600. That checkpoint was a stalemate on the battlefield of the attempt-5/6 siege:
- The besieger is the Baron's A15 (realm 5) at our royal castle in county 10.
- AI men u32(0x553f74) = 210: 144 archers and 66 knights (u32(0x57c980) = 66).
- Engines u32(0x553ff0) = 3: tower 33 at 7 hp, tower 34 at 1 hp, the ram (unit 1) at 25 hp.
- No breach (u32(0x53e990) = 0), ram mode u32(0x4eeb6c) = 1.

I chose it because it was the only cheap way to make progress on a $6 budget. Setting up a new game would take about 47 seasons.

**Step frames (whole movie):**
- Step 1, custom game on the map: f1757 (siegeknights5).
- Step 2, AI army besieging us: f76851 (siegeknights5).
- Step 3, on the battlefield: f77116 (siegeknights6). Screen 41 from then until f118880.
- Step 4, flag set: **f118880**. Non-knights reached 0 at f118812 (AI men 66 = knights 66, breaches 0, engines 3). The knights group's 200-tick check then set the flag, and sub_477DFC ended the battle on screen 43.
- Step 5, back on the map: **f120303**. "The siege is lifted / Your remaining enemies have fled" scroll was closed with RMB at f119998. The result screen 19 was closed with `c@result_continue`.
- halve_garrison runs at f119997, inside the aftermath (0x4ab383).

**Checkpoints:** s0 f111136, a111 f116116 (111 AI men left), flag f118880, map f120363.

**What broke the stalemate (unit records 0x554480 + 0x1b0·u):**
- Field offsets: class +0x12, side +0x13, col / row +0x20 / +0x22, order +0x31, target +0x174 (u8), group +0x178, men +0x1a0.
- Screen position on this camera: x = (col−33)·32+16, y = (row−52)·32+65.

What happened at the start:
- Our shooters were locked on the ram, which was out of range; the AI archers (order 17) were shooting at our unit 44 behind the battlements and doing no damage.
- The AI archers stood at the ram-mode staging point (u32(0x55cdd0)+10, u32(0x55cdd4)+10) = (47,63). The knights stood at (39,68).

The inputs, in three steps:
1. **Attack order:** `d290,45-395,125 .*10 h432,417 c432,417`. This drag-selected our archers 44 and crossbows 45 and 46 and gave them an attack order on AI archer unit 6. 44 and 45 walked to the south-east tower rim, (45,56) and (43,56), and shot the archers one after another. That killed about 2 men per 100 frames.
2. **Keep tower 34 alive:** in a first try, unit 46 retargeted tower 34 (1 hp) and killed it. That freed the ram's path to the gate at (39,56); it rammed at 20 gate damage a frame (breach at 20000, about 1000 frames). I loaded s0 and replayed step 1 exactly. Then I drag-selected 46 alone (`d188,76-230,120`) and marched it into the courtyard with a minimap click (`c560,90`). With 46 gone, both towers and the ram stayed alive, and the moat-side tower kept the ram blocked.
3. **Waited** about 6,700 frames. Unit 44 died and 45 kept firing (30 → 25 men). AI men went 210 → 66 with no breach, all 3 engines alive, and the knights untouched.

**Game facts learned:**
- engine_rng_step's dword_58FD7C is 0..127, so ram mode = rand < 128 − 20·towers − 10·catapults (69 % with 2 towers and a ram).
- In ram mode the AI archer group parks at ram staging +10,+10 and does nothing more. Its targets come from sub_48C974 (nearest enemy within 40, order 17).
- 0x553f74 counts every AI man, knights included, so the flag condition means every AI non-knight is dead.
- Our ranged units with an explicit attack order find a wall position in range by themselves. They only pile up at the gatehouse when the route they take is bad, and that depends on which units are in the group.
- Once the archers are dead the knights do not need to reach the walls. The 200-tick clock fires with them standing at (39,68).
- The result: screen 43 shows "The siege is lifted" (closed with RMB, not a left click), then screen 19 (`c@result_continue`), then the map.

**rd cov:**
- Functions: 1070 reached, **1 new**: 004ad8cc engine_combat_halve_garrison (277 B, first f119997).
- Blocks: 16149 reached, **117 new** (2122 B). The ones this run added: halve_garrison 8, sub_477DFC 3 (the surrender end, f118879), sub_4784CA 3, engine_combat_aftermath 0x4ab383 2, sub_478419 3, sub_48D9CE 4 (now including the flag store).
- The rest come from the inherited siegeknights5/6 prefix: sub_48DDC7 13, sub_453E71 29, sub_49459A 6, and others.

**What the tool lacked:**
- `rd check` judges steps only after the run's own start, so a run resumed from another run's checkpoint cannot PASS steps that happened in the inherited prefix. An option to judge the inherited prefix would help.
- `rd cell` / `@A` do not work on the battlefield. I worked out screen positions from the unit records and a picture.
- There is no "attack unit N" order and no way to keep a unit from auto-targeting an engine.
- The `cam_col` / `cam_row` vars do not hold the battlefield camera.

---
model claude-opus-5-5, effort high, 64 turns, $2.02, 24 min
