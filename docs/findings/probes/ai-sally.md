# Probe: ai-sally

- **Runs:** `aisally` (attempt 1: blocked, its finding below) · `aisally2` (attempt 2: sub_48C3FE reached) · **task:** `tools/drive/tasks/ai-sally.md` · **scenario:** `tests/scenarios/ai-sally/` (from `aisally2`)
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 5, 2026-11-13
- **The `rd verify` MISMATCH the driver reports was an rhost bug, fixed after:** a live / snap-test session ran retail's
  whole stack 16 bytes lower than a plain replay (game_tramp called retail's entry through a C frame), and 0x488793
  stores an uninitialized local (a dead unit's frame byte, unit slot 20 at siege-castles@13263) that held a stack
  pointer. With the fix the run verifies MATCH (fresh replay ≡ live at f16104, checkpoint loads included).

## Attempt 1 (`aisally`)

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `aisally`: the target was not reached.** Neither sub_48C3FE nor sub_48B7FC ran. I found the exact condition that blocks it and named the state below.

**Start and frames**
- Start: siege-castles@13290. That is battle 1 of the scenario, the assault on the Knight's castle Periwinkle (county 4), and the frame is just after the breach record appears.
- Movie: 16104 frames. Step 1 is at f13291 and step 2 at f13292 (screen 41). The run ends on the battlefield at f16104.
- My inputs:
  - f14790: recentre on our peasants with the minimap.
  - Box-select the peasants, pause, recentre on the swords with the minimap, unpause.
  - Hover at (237,258) until `enemy_under_cursor` = 42 (an AI sword).
  - f14898: left click, which orders the attack.
- `rd check` gives PASS. **`rd verify` gives MISMATCH**: the rng matches (3d6c4723/5f9775c6) but the semantic hash differs (fresh cd7d1af01cc3b05f vs live 4642bd5cbbe3784f). I did not find the cause; it may come from the checkpoint loads or the pause/drag inputs. **Do not file this run as it is.**
- `rd cov --blocks` reaches 14504 retail blocks; 2 are new to the corpus (36 B): sub_47A814 at f15458 and entity_type7_post_call 0x4be7be at f14793. sub_48E4CC still has 10 blocks nobody reaches (217 B, 0x48e584 to 0x48e6dc).

**State named (addresses; I did not define them as vars)**
- Group records: 0x566520 + 52·g.
  - +0 side (1 = us, 2 = AI); +3 attacking-side flag; +8 stance; +0x22 unit count.
  - +0xa target group; +0xc target timer (set to 50, counts down); +0xd busy.
  - +0x1c clock (a step every 100); +0x1e / +0x20 the group's col / row.
- Units: 0x554490 + 432·i.
  - +2 type (0 peasant, 3 sword, 5 archer, 7 catapult); +5 hit flag; +6 attacker unit; +0x10 / +0x12 col / row.
  - +0x18 / +0x1a the target cell; +0x1c side; +0x21 state (12 = catapult firing); +0x168 group; +0x190 men.
- Cell kind: byte 0x5440e7 + 640·row + 8·col. 1 = ground, 4 = wall, 5 = courtyard, 9 = rubble (fallen wall).
- Breach list: 0x553c80, 20 pairs of [cell offset, claiming group].
- 0x4eea98: AI strength over ours, in percent, minus 100. It is recomputed every ~100 ticks with a −10..+21 random term. It is not a battle clock as docs/findings/bf-stance.md says.
- 0x552ff0 = 260, a constant set at 0x4983b7.
- C(k)+0x1c3 == 2: this county's castles.dat breach state carries into its next siege battle. C(k)+0x1c4: repair %; at 100 the state is cleared.

**What I confirmed in state**
- In battle 1 the AI swords are group 13 (stance 3, 3 units, 4 men each).
  - From f14790 they walk to the breach and claim it: 0x553c84 = 13, and the breach cell is (30,48), offset 30960.
  - Our units in the courtyard (0x553e64) = 0.
  - Strength ratio 85–114 against 260, so it passes.
  - Not (0x542cd4 && 0x553fb0): 0x542cd4 = 0, so it passes.
- **The one failing condition is v3 = 1: the swords group never gets a target (+0xc stays 0).**
  - The target is only set by the hit flag, written in one place (all_ida.c ~76140). That is a projectile of type ≤ 2 (arrow or bolt) from the other side hitting the unit.
  - Melee does not set it: our peasants attacking the swords from f14898 to f16104 left +0xc at 0, and only made the group busy.
  - Catapult stones do not set it either. The catapults' target cell became (30,47) and their stones landed on the swords, with no damage and no target.
- **So the call needs our archers or crossbowmen, standing on kind < 4 ground outside the walls, shooting the stance-3 group, while a breach record exists and the strength ratio stays ≤ 260.**

**Why siege-castles cannot give that**
- Battle 1: our side is peasants, 2 catapults and a tower, with no archers.
- Battle 2 (f19220): there are breach records (31592, read from castles.dat) and swords (group 14, 5 units), but no archers. The ratio is about 402 > 260 (59 peasants against 131 men: 107 archers, 20 swords, 4 crossbows), so every AI group takes the sub_48C8AF branch instead.
- Raising a second army also failed (that branch was abandoned when I reloaded an earlier checkpoint).
  - From f18920 I raised a 72-man army in Raw Sienna (25 swords, 25 bows, 22 peasants), A(5).
  - The route to Periwinkle is a 5-turn detour north; the first army, A(6), took turns 2–7. Ordered onto the castle, A(5) stopped at (28,38) in a neutral county.
  - By turn 14 the castle was repaired (C(4)+0x1c4 = 100, +0x1c3 = 0), which removes the breach records.

**Next attempt**
- Take an army with archers, a catapult and over ~1/3.6 of the garrison's weighted strength to an AI castle whose garrison has swords.
- Bring the wall down next to the courtyard (16 hits) or dock a tower.
- Then order the archers onto a stance-3 unit from ground outside the walls.
- A skirmish siege map with archers added on the Cust. page may be the shortest route, if a defender army there has swords. field-orders' map does not run 0x48e4cc.

**Game facts**
- Battlefield projectiles (records at 0x57a104, stride 76) of type ≤ 2 mark the unit they hit as hit and record the attacker. Type 3 (stones) only damage walls.
- AI groups take a target only from ranged hits.
- Group numbers are re-assigned when the player orders a selection: the peasants became group 3, stance 3.
- On the battlefield a minimap click recentres while the battle is paused, and the selection survives the pause.

**What the tool lacked**
- `rd cell` and `@A()` do not work on the battlefield; I had to map positions from the minimap and `enemy_under_cursor` by hand.
- `eval` does not allow a tuple in one expression.
- On the strategic map, scrolling at the viewport's right edge (x=476) does nothing; you have to use x=639.

---
model claude-opus-5-5, effort high, 116 turns, $3.37, 26 min


## Attempt 2 (`aisally2`, filed)

The driver's report, as written:

---

**Run `aisally2`: sub_48C3FE (0x48c3fe) is reached, first at f3134. sub_48B7FC (0x48b7fc) is not reached** — it only runs in a field battle, so this siege run cannot reach it (explained below).

The movie is 3291 frames. `rd check` gives PASS and `rd verify` gives MATCH (rng 6d20429b/20443314, sem 2772be040d123fce). The run ends on the battlefield (screen 41) and never returns to the title. I looked at f3134: the AI swords are at the docked tower and our archers have been shooting them.

**Which start, and why.** I started from `fresh` with a skirmish siege, not knight-garrison@19051. At knight-garrison@19051 our besieging army A(12) is 150 peasants and nothing else. The swords group only takes a target when one of our arrows or bolts hits it, so peasants can never set it off.
- The skirmish setup: Single → Skirmish → shield 1 → Continue → Siege (480,305) → "The Palisade", the Countess defending.
- The Attacker / Defender switches: (400,281) made us the attacker. On the Cust. page, the shield next to "Defender" (283,439) swaps which side defends; I clicked it twice, so we stayed the attacker.
- The default custom army already had what was needed, so I did not edit it: 360 peasants, 20 swords, 50 crossbows, 70 archers and 2 siege towers. Go (600,448).

**Steps and frames**
- **f488:** the battlefield opens (screen 41). Task step 1 is at f488 and step 2 at f489.
- **f520–702:** I recentred with the minimap, box-selected one siege tower and unpaused (496,464). With a selection, a minimap click is a march order: tower unit 36 went to (42,48) and unit 35 to (31,48).
- **f1640:** tower 36 docks at cell (44,48), which writes the first breach record, 0x553c80 = 31072 (checkpoint `dock1`).
- **~f1790:** the AI swords (group 12, stance 3) claim that breach: 0x553c84 = 12.
- **f2592:** the second tower docks at (30,48) (checkpoint `dock2`).
- **f2594–2736:** I paused, recentred on our archers (group 3), box-selected them, recentred on the swords and unpaused. I hovered (240,360) until `enemy_under_cursor` = 47, then left-clicked: the archers attack (checkpoint `order`).
- **~f3091:** group 12 gets a target: +0xa = 3 (our archers), +0xc = 50.
  - Our archers stand outside the wall at (32,59), on ground.
  - Our units inside (0x553e64) = 0.
  - The strength ratio (0x4eea98) is 33–54, well under the limit (0x552ff0) of 260.
  - 0x542cd4 = 0 in a skirmish (it is `0x55401c == 1`), even though 0x553fb0 = 500.
- **f3134:** group 12's clock (+0x1c) reaches 100 while +0xc is still about 45, so sub_48E4CC calls **sub_48C3FE**, which reaches **sub_496D76**.

**State named** (in my run's vars.json):
- `bf_breach0_group` (s32 0x553c84): the group that claimed the first breach record.
- `bf_units_inside` (s32 0x553e64): our units inside the walls.
- `bf_g12_target_timer` (expr `u8(0x566520+52*12+0xc)`): group 12's target timer.

Other state I read:
- Group records at 0x566520 + 52·g: +8 stance, +0xa target group, +0xc target timer, +0xd busy, +0x1c clock, +0x1e / +0x20 col / row.
- Units at 0x554490 + 432·i: +2 type (8 = siege tower), +0x10 / +0x12 col / row, +0x168 group, +0x190 men.
- Cell bytes at 0x5440e0 + 640·row + 8·col: +7 kind, +4 height.

**Game facts**
- **A docked siege tower is enough for the breach record.** No catapults needed. sub_491492 runs when a type-8 unit docks: it pushes a breach record at the tower's cell and adds 4 to 0x553fb0.
- **Where towers dock:** ordered onto a palisade wall cell of kind 4 and height 2, a tower docked one or two cells away from where I sent it.
- **A skirmish siege avoids the trap in the 0x542cd4 && 0x553fb0 test.** 0x553fb0 is 500 from battle setup anyway, and 0x542cd4 is 0 here.
- **Defenders:** the Countess's Palisade garrison has stance-3 groups 12 (swords, type 3) and 13 (type 2); swords is the one that claimed the breach.
- **The target timer:** +0xc counts down about one per tick and every new arrow hit sets it back to 50. So steady archer fire keeps it alive across the clock's 100-tick check.
- **The Cust. page edits only the human's army.** It shows the army list and the "Defender" shield swaps which side defends.

**rd cov**
- Functions: 606 reached; 2 are new to the corpus (699 B): sub_48C3FE (191 B) and sub_496D76 (508 B), both first at f3134.
- Blocks: 7026 reached; 41 new to the corpus (932 B):
  - sub_496D76: 19 blocks
  - sub_48C3FE: 3 blocks
  - sub_48E4CC: 4 blocks, at f3134
  - sub_492C8B: 5 blocks, at f1927
  - sub_488436: 3 blocks
  - sub_491147: 2 blocks
  - sub_497437: 2 blocks
  - sub_47095E: 1 block
  - sub_490FA2: 1 block
  - the title's shield assign: 1 block
- sub_48E4CC still has 6 blocks nobody reaches (84 B, 0x48e5f8–0x48e6b7).

**Why sub_48B7FC is out of reach here.** Its caller sub_48A9D2 is stance 1 in the field-battle stance table (0x4d91b8). That table only runs when the battle is not a siege (0x53e8fc == 0), and a skirmish ends at the title, which this task forbids, so a second battle cannot follow in this run. What a separate field-battle run needs (read from the code, not tried):
- An AI stance-2/3/4 group (sub_48ACD2 / sub_48B02B) is hit by our arrows, so it has a target.
- Then two checks must pass:
  - ++0x553f28 ≤ byte 0x553080[0x5653f8];
  - 0x553f74/8 ≤ 0x56d6ac.
- When they do, it sets 0x56d89c = 1.
- On its next 200-tick step, an AI stance-1 group then needs:
  - the ratio 0x4eea98 ≤ 0x57c8b4;
  - 0x553070 == 0;
  - no target of its own (+0xc == 0).
  Then it calls sub_48B7FC.
- In short: an Equal skirmish where our archers shoot an AI stance-2/3/4 group and never hit its stance-1 group.

**What the tool lacked**
- `rd cell` / `@A()` do not work on the battlefield. I worked out screen positions by hand: minimap = 480 + 2·col, 24 + 2·row; the view is 15 × 14 cells of 32 px, with its top-left at the clicked centre − 7.
- There is no `--until` predicate for "function X reached"; I had to run `cov` afterwards.

---
model claude-opus-5-5, effort high, 65 turns, $1.47, 15 min
