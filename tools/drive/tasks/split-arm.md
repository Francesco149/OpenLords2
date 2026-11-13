---
start: newgame-playnow       # Play Now, Winter 1268, the map, 5000 crowns, county 2 Raw Sienna
steps:
  - an army raised: my_armies >= 1
  - the army split into two: my_armies >= 2
  - a soldier class other than peasants in an army of ours: count(A, owner == me and kind == 1 and (crossbowmen + macemen + swordsmen + pikemen + archers + knights) > 0, 1, 30) >= 1
forbid:
  - the game lost: my_counties == 0
---
# Split an army, and arm troops with weapons — two army flows no corpus game uses

tools/bot/README.md "Not yet": the bot never splits armies (screen 0x11 "split army") or arms troops (screen 0x0d "arm
troops": peasants drafted into an army become crossbowmen / macemen / swordsmen / pikemen / archers / knights when the
realm has weapons — R(me).crossbows .. armour; weapons come from the merchant (screen 8, buy) or the smithy). The bot's
own flows (tools/bot/actions.py: raise_army, the merchant) show the raise-army panel (0x17) and buying.

Find the UI for each (rd hot / rd text / rd look on the army's info panel and the court / armory screens 9 / 0xa;
`docs/findings/gameplay-and-screens.md`, `docs/findings/render-screens.md`, IDA names of the hotspot actions) and do it:
raise an army in Raw Sienna, split it, buy weapons if needed and arm part of an army. Name the state you find (army
selection, the split slider) in your run's vars. Report frames per step, how each screen works (points, sliders), and
rd cov — the point is retail code no corpus scenario reaches.
