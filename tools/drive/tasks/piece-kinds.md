---
start: bot3cen-s207-t44@30000   # custom England, a long game with merchants, enemy armies and (maybe) rebels — pick a mid-turn map frame
steps:
  - in game on the map: state == 3 and screen == 0
  - a piece's info panel open: screen == 4
  - back on the map: screen == 0
forbid:
  - the game lost: my_counties == 0
---
# Block coverage: the info panels and map drawing of every kind of piece

The piece info panel 0x41b19d (28 untaken blocks, 1.2 KB) and the army overlay renderer `engine_overlay_render_tile_army`
0x4071a0 (48 blocks, 0.8 KB) branch on the piece's kind (A(i).kind: 1 army, 2 rebels, 3 merchant, 4 supply cart), its
owner (ours / a lord's / neutral), state (moving, besieging, garrisoned, mercenaries aboard, health) and the zoom. Read
both in the IDA export, list what each untaken branch needs, then make the pieces exist and open their panels (RMB on an
unselected piece, per the card): a supply cart = Send Supplies from a county (`econ-tour`'s header says how), rebels =
an unhappy county (high taxes / no food: C(k).happiness), mercenaries = the raise-army panel's offer (C(k).merc_offer),
an army besieging / garrisoned / sick. Use `rd rec R A` to see the pieces.

Report `rd cov R --blocks` (new blocks by function), which piece states reached which branches, and how you made each.
