# Probe: ai-recall

- **Run:** `airecall` · **task:** `tools/drive/tasks/ai-recall.md` · **scenario:** `tests/scenarios/ai-recall/` (blessed wine + win, equal)
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 6, 2026-11-14

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads):

---

**Run `airecall`: done.** `rd check tools/drive/tasks/ai-recall.md` passes, and `rd verify` gives MATCH (rng 612cddac/73d27068, sem 51bedff6092e6e90). The movie is 7598 frames and ends on the field battlefield (screen 41, state 3). **sub_48B7FC (0x48b7fc) is reached for the first time, at f7597.**

**Why a plain skirmish from `fresh` cannot work, and the fix**
- The flag needs `byte_553080[u32(0x5653f8)] >= ++u32(0x553f28)`. The 48-byte table at 0x553080 is filled with random values only by `engine_game_init_seat_shuffle` (0x497e65), which runs only when a strategic game starts.
- A skirmish does not call it, so from `fresh` the table is all zeros. With 0x5653f8 = 0 in a skirmish, the check reads 0 ≥ 1 and always fails (0x553070 += 3 instead).
- So the flag can never be set in a cold skirmish. That is a second reason, besides the siege table, why ai-sally could not reach it.
- **The fix:** start a Play Now game first, then File > New Game → yes to get back to the title. The table survives the trip (`byte_553080[0]` = 0x10 = 16), and then I started the skirmish.
- The forbid (state == 2) is only judged from step 1 on, and the trip to the title (f837) comes before step 1 (f973).

**The movie**
- f1–122: Single (320,103) → Play Now (320,103) → Original campaign (190,129) → Continue (432,227). In game at f122.
- f262–777: right-clicked away the hint scrolls.
- f780–837: File → New Game (48,44) → thumbs-up (240,218). Back on title page 1 at f837.
- f857–970: Single → Skirmish (320,175) → shield 1 (144,175) → Continue. Skirmish page 12, with the default "A Bridge Too Far" and Equal battle; we defend against the Baron. Checkpoint `skirm12`.
- f971: Go (593,448).
- **Step 1, f973:** the field battlefield opens (screen 41, `u32(0x53e8fc)` = 0).
- f995: un-pause with the hourglass (496,464). After that I gave no orders: everything that follows is the AI's own play.
- **Step 2, f6998:** the recall flag `u32(0x56d89c)` = 1 (checkpoint `flag`).
  - Our archers (group 11, at (38,4)) had hit the AI peasants (group 6, stance 2): its target bit came up at f6801.
  - `0x553f28` = 1, `0x4eea98` = −2 (the limit `0x57c8b4` is 5), `0x553070` = 0.
  - `0x553f74` / 8 = 100, which is ≤ `0x56d6ac` = 275 (the AI's ranged men).
  - The remembered attacker centre DAT_00522f70 / DAT_00522f74 = (38,4), our archers' cell.
- **Step 3, f6999:** still in the game (state 3).
- **f7597:** an AI stance-1 group with no target calls sub_48B7FC, which clears the flag (1 → 0 at f7598).
  - The two stance-1 groups are group 4 (crossbows) and group 5 (archers).
  - Group 4 had its target bit set at f7498 but not at f7548. Group 5 got a target at f7548. So I take it to be group 4, after its target ran out.
  - The ratio was ≤ 5 at that moment, and `0x553070` = 0.
- I looked at f7597. The camera is still on the bridge, so the units are not in view; the minimap shows the AI's blue groups right up against our red lines.

**State I named** (this run's vars.json)
- `recall` (s32 0x56d89c): the AI recall flag.
- `recall_tries` (s32 0x553f28): the recall counter, checked against `byte_553080[0x5653f8]`.
- `ai_ratio` (s32 0x4eea98): AI strength over ours, % − 100.
- `ai_caution` (s32 0x553070).
- `ai_tgtmask`: a bitmask of AI groups 1–6 whose target timer +0xc is non-zero.

**Game facts**
- The skirmish's starting groups:
  - AI (+0 = 2, +1 = 0): 1 swords st3, 2 type-2 st3, 3 type-4 st2, 4 crossbows (type 1) st1, 5 archers (type 5) st1, 6 peasants st2.
  - Ours: groups 7–12, the same mix, with +1 = 1.
- `0x4eea98` swings between about −10 and +21 every 200 frames, so the ≤ 5 test passes roughly half the time.
- In an Equal skirmish the AI crosses the bridge and walks into our archers on its own. No orders were needed.
- The skirmish's battle index `0x5653f8` stays 0, because sub_497500 zeroes it.

**rd cov**
- Functions: 826 reached, 1 new to the corpus: sub_48B7FC (453 B), first at f7597.
- Blocks: 9507 reached, 12 new (418 B):
  - sub_48B7FC: 10 blocks (396 B), first at f7597.
  - sub_48B5FC: 1 block (12 B), first at f6997.
  - sub_48A9D2: 1 block (10 B), first at f7597.

**What the tool lacked**
- There is no `--until` predicate for "function X reached"; I relied on the flag being cleared, since sub_48B7FC is the only code that clears it.
- `rd look` cannot move the battlefield camera without adding input to the movie, so the picture of the key moment does not show the units.

---
model claude-opus-5-5, effort high, 69 turns, $1.26, 10 min
