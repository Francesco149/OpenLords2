---
start: custom-tour@600        # the title (custom setup reachable) — or any custom-game start you prefer; say which and why
steps:
  - a custom game in progress: state == 3 and screen == 0
  - an AI army with knights besieging a county of ours: count(A, owner != me and owner >= 1 and owner <= 5 and kind == 1 and knights > 0 and besieging != 0 and C(besieging).owner == me) >= 1
  - the siege battle on the battlefield: screen >= 40 and screen <= 43
  - the AI knights' wall flag set (halve_garrison armed): u32(0x56d5c8) == 1
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
