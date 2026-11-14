---
start: custom-tour@600        # the title (custom setup reachable) — or any custom-game start you prefer; say which and why
steps:
  - a custom game in progress: state == 3 and screen == 0
  - an AI army besieging a county of ours: count(A, owner != me and owner >= 1 and owner <= 5 and kind == 1 and besieging != 0 and C(besieging).owner == me) >= 1
  - the siege battle on the battlefield: screen >= 40 and screen <= 43
  - the AI attacker's surrender flag set (halve_garrison armed): u32(0x56d5c8) == 1
  - back on the map after the battle: screen == 0
forbid:
  - quit to the title: state == 2
---
# Make an AI lord besiege our castle with knights, fight it on the field — halve_garrison (0x4ad8cc), never reached

`rd covered engine_combat_halve_garrison` → not reached by the corpus. docs/findings/coverage-round3.md "Not covered":
`0x4ad8cc halve_garrison` runs in a battle's aftermath (FUN_004ab383) with DAT_0056d5c8 = 1, which only the battlefield's
AI siege-attacker knights set (0x48d9ce: when DAT_00553f74 ≤ DAT_0057c980 and no wall hit yet). So: an AI lord with
knights in an army, besieging a castle of ours, the assault taken to the field (the battle prompt's left gauntlet), the
knights reaching our walls. Read 0x48d9ce / 0x4ab383 / 0x4ad8cc in the IDA export first and name the state.

The setup is the work: knights need armour / weapons in the AI's realm and an aggressive AI (custom setup: difficulty,
nobles, weapons "many", army size — `custom-tour`'s header lists the options and their pages; `tools/bot/actions.py`
custom_game shows the clicks). Then play seasons (End turn; keep the county fed and a castle standing) until an AI army
with knights besieges us — a search over seasons is the tool: goal = step 2's predicate, actions = End turn with
different waits + dismissing scrolls, avoid = losing the county; checkpoint often. On the field (tools/bot/README.md
§battlefield) defend so the assault reaches the walls. `rd covered` / `rd cov --blocks` measure it.

Report frames per step, the setup that worked, how long it took in seasons, and rd cov (functions + blocks).

**Campaign 2's attempt (blocked, run `siegeknights`, furthest checkpoint `t27` at f39101 — you may start from it with
`rd new R --from siegeknights:t27`, or start over):** AI knights take years (armour only from a smithy); an AI targets a
garrisoned castle only under a vendetta (attitude −30 + 3 battles); 0x48d9ce arms the flag only when every AI unit left
on the field is a knight, before any wall breach. Since then: `rd seasons` (~0.1 s a season) makes a 30–60-season game
cheap, and campaign 4 (docs/findings/probes/lord-move.md) found that each diplomacy insult costs ~15 attitude (−30 is
the clamp, two seasons — one message per lord per season), that an AI's weapons climb ~12 a season and recruiting
spends them. So: pick the AI with the best armour economy, insult it to −30, give it battles (attack its armies), let
it arm, keep our castle garrisoned and fed; on the field, kill its non-knights first (archers / foot) so only knights
remain before any breach.

**Campaign 6 (docs/findings/probes/ai-surrender.md) — knights ARE required, plus three conditions found:** stance 4
(0x48d9ce) is given only to a class-6 (knights) group (all_ida.c:69239); sub_477DFC ends the battle as soon as a side has
0 men of class < 7, so every AI non-knight must die while an AI knight lives (the orchestrator's "engines only" path is
void); u32(0x553ff0) (class 7..9 engines on the field, either side) must stay > 0 or 0x53e990 becomes 4; u32(0x53e990) =
the breach count (+1 per courtyard wall cell to rubble, +4 gate broken by a ram — 0x568da4 past 20000 —, +4 a docked
tower, +3 at all_ida.c:75454). The knights' group must be untargeted when its 200-tick clock fires. Skirmish attackers
are fixed presets (the Cust. page edits only the human's army); none is knights-only. Skirmish battles never run the
aftermath 0x4ab383 — halve_garrison needs a campaign siege of OUR castle by an AI with knights, fought on the field.

**Campaign 7 (attempt 4) — the tools the third attempt lacked now exist:** `rd seasons R N --feed` keeps our counties
fed and calm before each End turn (rations / the grain-meat slider / tax: attempt 3's county ate its herd at meat 100 %
with grain unused and starved); `--until` no longer stops inside turn processing; `@raise_more` / `@raise_continue` follow
the mercenary box. Attempt 3's run `siegeknights3` (checkpoints t3 t6 won1 won2 sown fed: the Baron at −30, vendetta
stage 2 by turn 17) is a valid start (`--from siegeknights3:won2`) — one more won battle against the Baron should give
vendetta stage 3; then `rd seasons --feed --until "<step 2>"`. Or follow attempt 2's advice (probes/siege-knights.md):
a start next to the armour-building AI. Check the besieger has knights (unit class 6) before taking the field.

**Attempt 5 (2026-11-14 night) — change the geometry, not the patience.** Attempt 4 (probes/siege-knights.md) got the
Baron's knight army A26 launched at us (turn 46) but it died crossing realm 2's land. So: start a NEW custom game and
pick the map / our start county so that the AI with the best armour economy (the one with smithies / armour stock —
read realm records and county buildings early with `rd eval`) is our direct neighbour with no third realm's county on
the path between its muster county (realm +0xE5) and our castle. Survey several custom starts cheaply first (new game,
read realm owners per county + adjacency + each AI's armour / weapons, discard bad ones) before investing seasons.
Then: insult it to −30 (two seasons), win battles against it for vendetta stages, `rd seasons --feed` (now feeds right:
6 per sack, buys grain), keep a garrisoned castle (build one if the start has none). Before taking the field, check
the besieger carries class-6 knights AND engines. On the field kill its non-knights first, keep its engines alive, no
breach. Checkpoint every stage so a later attempt can resume.
