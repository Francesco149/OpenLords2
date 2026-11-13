# Probe: lord-move

- **Run:** `lordmove` · **task:** `tools/drive/tasks/lord-move.md` · **scenario:** `tests/scenarios/lord-move/` (blessed wine + win, equal)
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 4, 2026-11-13

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

I reached engine_army_ai_order_lord_move (0x4a599d) for the first time in the corpus: an allied lord's army got the "move to help" order and the function ran at frame 18737.

**Run `lordmove`**, started from custom-strong at frame 13360, movie 19028 frames.
- `rd check` on tools/drive/tasks/lord-move.md: PASS. Step 1 (on the map) at f13361, step 2 (R(me).ally != 0) at f14118, step 3 (still in game) at f14119. my_counties stayed 1 the whole time.
- `rd verify`: MATCH (fresh replay and live session agree at f19028).
- I looked at f18737: the map in Winter 1271, Cordova ours, with a yellow-flag army in the county.

**The task's premise didn't hold.** At f13360 nobody is allied: R(me).ally = 0, every realm's +0x81 is 0, it is still turn 1, Winter 1268. The header's "Knight allied" and the rl-cov3.md note (realm 5 allied) must describe a different recording. I made the alliance myself.

**The code path (from the IDA export):**
- The order comes from engine_army_ai_run_campaign 0x49f977 (AI step 9) → assign_deployment 0x49fda5. It sets piece +0x1a = 6 only when the ally's +0xe8 (target county) is set and that county belongs to the ally's partner (+0x81), i.e. us.
- validate 0x49ff71 keeps +0xe8 only while the county's +0x19c (foreign army present) ≠ 0.
- +0xe8 is set by ai_diplomacy_gift 0x4a1b29, called from the plea answer 0x4a3551 (answer 184). That answer needs the ally's +0x14 (average county population) ≥ its personality's bar (table 0x4d8a88: 750 / 800 / 900 / 1000), attitude ≥ 10, and 4·attitude > the roll at 0x58fd60.
- The muster needs one of three things:
  - home county population ≤ bar/2 and an army of ≥ 200 men (pick_best_unit);
  - the free-funding path 0x49fcc5: Countess or Bishop only, realm +0x29 ≤ 1, year < 1273, then muster_defender (home population ≥ 100);
  - weapons total, realm +0x138, ≥ table 0x4d8ac0 (Knight 100, Baron 120, Countess/Bishop 200).
- AI step 11, run_movement 0x4a5667, then dispatches every army whose order is 6 to 0x4a599d.

**What worked, in frames:**
1. f13386–13657: on the diplomacy screen (`c623,444`, faces at x=90, y = 90 / 190 / 290 / 390 for Bishop / Countess / Knight / Baron) I offered the Knight an alliance (`c410,268`, thumbs-up `c302,307`) and insulted the Bishop and the Baron (`c410,218`).
2. The alliance offer is a roll: accepted when 3·attitude + 45 > roll (attitude 5). One season refused it. A search over the wait before End turn found an acceptance: R(me).ally = 4 at f14118.
3. f14148–14667: I sent the Knight a 100-crown gift (`c410,118`, ten clicks on the +10 arrow `c199,256`, then `c302,295`), taking his attitude to us from 9 to 19, and insulted the Bishop again. The season's timer ended the turn before the Baron's second insult went out.
4. `rd seasons` until f17657 (turn 10). Two seasons after the alliance (by turn 6) a Bishop army of 308 men was sitting in Cordova (c5_foreign = 308). The Knight's weapons climbed from 13 to 68 and his average population to 1184.
5. f17660–17787: plea for help to the Knight (`c410,318`, click Cordova on the mini-map `c135,265`, thumbs `c334,260`). It was answered 184 at f18015: the Knight's +0xe8 = 5, attitude 15.
6. Seasons until his weapons passed 100 (still 83 at turn 12, f18465). At f18727 assign_deployment gave A(14), the Knight's new 407-man army, order 6. His weapons dropped to 10 and +0xe8 cleared. At f18737 dispatch called lord_move.

Checkpoints: kn_offered f13657, kn_allied f15277, t10 f17657, helped f18015.

**State I named** (in this run's vars.json): `kn_att` = s8(0x57bf84+0x160·4+0x10), the Knight's attitude to us; `kn_e8` = u8(0x57bfe8+0x160·4), his campaign target; `kn_weap` = s32(0x57c038+0x160·4), his weapons total; `c5_foreign` = s32(0x53f9b0+5·0x300+0x19c); `lordmove_n` = count of pieces with +0x1a == 6. Also `ally_att` / `ally_e8`, the same two fields for the Countess, from my first attempt.

**Game facts learned:**
- On my first try I allied the Countess. Her pleas were refused (183) because her average population sat at 688–888, under her bar of 900. Her army drafts keep it down.
- The Knight's weapons rise about 12 a season (57 → 112 by turn 7 in one timeline), but recruiting spends them: 62 → 13 in one season.
- Once +0xe8 is set it stays across seasons as long as the foreign army stays in our county, so a plea can be sent early. The muster then fires on the first season the weapons pass 100.
- Gift from us: +10 attitude when the amount ≥ the last gift + table 0x4d8a60 (Knight 100, Countess 200).
- A "help" answer costs the ally 4 attitude toward us.
- Insults: each one costs about 15 attitude (5 → −15 → −30 at the clamp).
- The help answer's price popup (185) would cost table 0x4d8a64 × gift count (Knight 500, Countess 1600).
- One message per lord per season.
- A season ends on its own timer: a long chain of diplomacy clicks can run past it.

**`rd covered` / `rd cov`:**
- 846 functions; 1 new: 0x4a599d engine_army_ai_order_lord_move, 386 B, first at f18737.
- `--blocks`: 12 new blocks (327 B):

| Function | New blocks | First frame |
|---|---|---|
| 0x4a599d lord_move | 7 (220 B) | f18737 |
| 0x4a30f6 alliance-offer answer, accept path 179 | 3 | f14117 |
| 0x49fda5 assign_deployment, order-6 arm | 1 | f18727 |
| 0x4a57ac dispatch_order, case 6 | 1 | f18737 |

**Where the tool fell short:**
- `seasons --until` on a turn-processing value stops mid-season, sometimes on a message scroll.
- The diplomacy screens (0x0b, 0x1a) have no ui.json names or `rd hot` hotspots, so I clicked by sight.
- Expressions have no list comprehensions, so per-realm values needed one expression each.
- I could not read the scenario header (tests/scenarios is not readable here), so the claimed start state could not be checked before starting.

---
model claude-opus-5-5, effort high, 121 turns, $2.40, 23 min
