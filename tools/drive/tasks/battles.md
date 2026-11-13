---
start: bot3-s103-t36@20000   # Play Now, the warlord bot game that fights on the field — or any start with armies near enemies; say which
steps:
  - in game on the map: state == 3 and screen == 0
  - a battle on the battlefield: screen >= 40 and screen <= 43
  - back on the map: screen == 0
forbid:
  - quit to the title: state == 2
---
# Block coverage: the battlefield's untaken branches

The battlefield (screens 0x28–0x2b, tools/bot/README.md §battlefield, tools/bot/battle.py) is reached by the corpus, but
these functions keep many untaken blocks: sub_479E90 (33 blocks / 1 KB), sub_477DFC the battle end check (33 / 602 B),
sub_492C8B (20 / 560 B), sub_459258 (91 / 550 B), engine_combat_transfer_ownership 0x4a72fe (13 / 536 B),
engine_army_move_region_event_lo 0x46873f (13 / 557 B: a march across a foreign county's resource tiles). Run
`rd cov R --blocks --fn sub_479E90,sub_477DFC,sub_492C8B,sub_459258,engine_combat_transfer_ownership,engine_army_move_region_event_lo`
first, read the untaken blocks in the IDA export (all_ida.c / all_ida.asm) and plan which battle situations reach them:
attacking vs defending, a castle on the field, units of each class, archers / crossbows firing, a retreat, mop up,
autocalc, a battle won / lost / drawn, the lord captured, the army destroyed, a county changing hands, a march through
foreign land. Fight several battles (checkpoint before each; search over orders where the outcome matters).

Report rd cov --blocks before / after (with --fn for the six), the situation → branch table, and battlefield facts.
