---
start: custom-strong@6000     # France, five lords, every county > 1000 souls, villages — a varied map; pick a mid-turn frame on the map
steps:
  - in game on the map: state == 3 and screen == 0
  - the terrain info screen used: screen == 4
  - back on the map: screen == 0
forbid:
  - the game lost: my_counties == 0
---
# Block coverage: the terrain info screen over every kind of tile

The terrain info screen (screen 4, the map's "?" / terrain icon; `engine_terrain_info_classify_text` 0x41c208 and
0x41cb3a pick the text by tile kind) has 47 + 24 untaken blocks (~2.2 KB). Read both in the IDA export to list the tile
kinds / cell flags each branch wants (cell record: `CELLS` 0x522f90 + (row*64+col)*8, `docs/findings/game-state-map.md`,
`tools/bot/l2state.py` _field_class: barren, fallow, wheat stages, reclaiming, cattle crowding, flooded, parched, villages,
roads, rivers, forests, mountains, castles, towns …), find cells of each kind on this map (read the grid), bring them on
screen (`tools/bot/mapview.py` has cell ↔ screen from the viewport; the minimap recentres on a county), and ask for
terrain info on each. A search can do the clicking: goal = a block-level state you name, actions = clicks over the map.

Report `rd cov R --blocks` (new blocks by function), the tile kinds you reached and their texts (`rd text`), and how
the screen works (points).
