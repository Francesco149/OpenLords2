---
start: fresh                  # a skirmish siege we DEFEND (Single → Skirmish → Siege; ai-sally's probe has the clicks)
steps:
  - on the battlefield of a siege: screen >= 40 and screen <= 43 and u32(0x53e8fc) != 0
  - the AI attacker's surrender flag armed: u32(0x56d5c8) == 1
  - still in the game: state == 3
forbid:
  - quit to the title: state == 2
---
# Make the AI attacker's lord give up a siege — sub_48D9CE (0x48d9ce) arming DAT_0056d5c8, never armed in the corpus

`rd covered 0x48d9ce` → reached by siege-defence only, never with the flag set; `rd cov --blocks --fn 0x48d9ce` shows the
arming block unreached. The orchestrator's reading (IDA export, all_ida.c:73890 sub_48D9CE; check it):

- 0x48d9ce = the siege ATTACKER's stance-4 group handler (table 0x4d91d0, docs/findings/bf-stance.md). Its group
  clock word_56653C[26·g] counts to 200; if the group has no target (byte_56652D[52·g] == 0) it resets and, when
  **u32(0x553f74) ≤ u32(0x57c980) and u32(0x53e990) == 0**, sets u32(0x56d5c8) = 1 and u32(0x5656f8) = the group's owner.
- 0x553f74 / 0x57c980 are recomputed by sub_481B9A (all_ida.c:69750): over the field units with class (unit +0x12,
  0x554492) **< 7** and unit +0x13 (0x554493) == 0 — +0x13 is set at spawn (0x46e4c8) when the owner realm's +0x05
  (human) is set, so these are the **AI's** men: 0x553f74 = all AI men of class < 7, 0x57c980 = AI knights (class 6).
  Classes 7..10 are the siege engines — NOT counted. So the flag arms when every AI man left is a knight, or when
  **no AI soldier is left but its engines** (0 ≤ 0).
- 0x53e990: 0 at a fresh siege; siege-defence's River Island battle (AI catapults) keeps it 0 until the first catapult
  hit on our walls (≈ 9700 frames after the battle opens), then 1, 2 …; the Castle Acre battle sets it 4 at ~2000
  frames (sub_477DFC all_ida.c:65653 when 0x553ff0 == 0 — read it), then it climbs. Name what it is.
- Once armed: sub_477DFC (all_ida.c:65627) ends the battle (byte_4EAC50 = 43, 0x57c924 = the AI side), and the score
  (all_ida.c:19332) zeroes the surrendering side. Watch both.

Easiest likely setup: the skirmish Cust. page (ai-sally probe: the Attacker / Defender shields, the army editor) — we
DEFEND; the AI attacker gets **knights only** (+ engines if allowed), so the count condition holds from the start; or a
small AI foot army + engines that our archers / crossbows kill before the first wall hit. Then wait for the stance-4
group's clock with no target (keep our units out of its reach: inside the walls). Read the Cust. page with `rd text` /
`rd hot` / `rd look`.

Report frames per step, the setup (clicks), the state named, rd cov (functions + blocks) — and whether the battle-end
branch and the score branch ran.
