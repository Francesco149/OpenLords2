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
