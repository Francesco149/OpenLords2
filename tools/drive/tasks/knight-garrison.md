---
start: custom-tour@600        # the title (custom setup reachable) — or any start you prefer (custom-strong, a checkpoint of run siegeknights): say which and why
steps:
  - a custom game in progress: state == 3 and screen == 0
  - an AI castle garrisoned with knights: count(A, owner != me and owner >= 1 and owner <= 5 and garrisoned != 0 and knights > 0) >= 1
  - the assault on that castle taken to the battlefield: screen >= 40 and screen <= 43
  - back on the map after the battle: screen == 0
forbid:
  - quit to the title: state == 2
---
# Assault an AI castle whose garrison holds knights, on the field — sub_48E774 (0x48e774), never reached

`rd covered 0x48e774` → not reached. It is the battlefield AI's siege-DEFENDER knight behaviour: the per-group AI loop
sub_489401 dispatches on the group's stance byte (group records 0x566520, stride 52: +3 = side flag, +8 = stance) through
funcs_489572 (siege, defender) — stance 4 = table entry 0x4d91f8 = 0x48e774 (docs/findings/coverage-round3.md "Not
covered"). The skirmish siege maps' defenders have no knights and the Cust. page edits only the player's army, so it needs
a STRATEGIC siege of an AI castle whose garrison (a piece with `garrisoned != 0` — check what the field means, and which
field holds a garrison's knights) includes knights. Read 0x489401, the stance assignment for defenders (who writes +8
when the battlefield is set up from a strategic siege), and 0x48e774 in the IDA export first; name the state.

The setup is the work: AI knights need armour + weapons in the AI realm (blacksmith / armourer output, R(p).armour; facts
from campaign 4's lord-move probe, docs/findings/probes/lord-move.md: an AI's weapons climb ~12 a season, recruiting spends
them). Find out what makes an AI put knights in a garrison (the AI garrison code: who writes a garrisoned piece's knights)
and which AI lord / personality is likeliest; a custom game (difficulty, weapons "many", army size) can help. `rd seasons`
is ~0.1 s a season, so a 30–60-season game is cheap; search over seasons with a cell that sees the knights count.
Then besiege that castle with an army able to assault (siege equipment if it is stone — tools/bot/README.md §sieges,
tests/scenarios/siege-castles' header) and take the assault to the field (the battle prompt's left gauntlet). On the field
fight long enough for the defender knights' AI to act (rd cov --blocks --fn 0x48e774 shows it).

If after a serious effort it is infeasible, stop with exactly what blocks it (state, code) and the furthest checkpoint.
Report frames per step, the setup that worked, seasons taken, rd cov (functions + blocks).
