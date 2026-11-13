---
start: custom-strong        # France, five lords — custom-strong's header: the Knight allied, the plea for help answered
steps:
  - in game on the map: state == 3 and screen == 0
  - an ally of ours: R(me).ally != 0
  - still in game: state == 3
forbid:
  - the game lost: my_counties == 0
---
# Reach engine_army_ai_order_lord_move 0x4a599d — an ally's lord deploys to help us (never reached)

`rd covered engine_army_ai_order_lord_move` → not reached. docs/findings/coverage-round3.md "Not covered": the ally's
campaign target (realm +0xe8) = our county survives validate (0x49ff71), but its deployment needs a muster: the ally's
home population ≤ bar / 2 with a ≥ 200-man unit (pick_best_unit) or weapons ≥ 100 (table 0x4d8ac0; the Knight had 0 →
24 in five years). The gift path (ai_diplomacy_gift 0x4a1b29, from a plea answered / pay-for-help) sets +0xe8. Read
0x4a57ac (its caller), 0x4a599d, 0x49ff71 and the muster code in the IDA export; name the state (the ally's +0xe8, its
weapons, its units), then make the conditions true: an ally (custom-strong's prefix already has one — read its header),
the ally armed (send it weapons? gifts of crowns let an AI buy at its merchant?), pleas for help each season (`rd
seasons` between), until its lord marches. Searches over seasons with the right cell are the tool. If after a serious
effort it is infeasible, stop with exactly what blocks it (state, code) and the furthest checkpoint.

Report frames, the setup that worked (or the blocker), rd covered / rd cov --blocks results.
