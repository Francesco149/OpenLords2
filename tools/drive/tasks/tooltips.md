---
start: custom-strong@6000    # France, five lords, villages, varied terrain — pick a quiet mid-turn map frame
steps:
  - in game on the map: state == 3 and screen == 0
  - still in game: state == 3
forbid:
  - the game lost: my_counties == 0
---
# Block coverage: the strategic map's tooltips (hover)

engine_tooltip_classify_strategic 0x477320 (54 untaken blocks / 525 B) picks the tooltip for whatever the cursor rests
on in the strategic view (pieces of each kind and owner, castles, towns, villages, fields, industries, terrain, HUD
buttons, the minimap …). `rd cov R --blocks --fn engine_tooltip_classify_strategic` lists the untaken blocks; read the
function in the IDA export and map each to what the cursor must rest on (and any state: selection, zoom, a screen).
Then hover them all (`hX,Y`, `h@A(i)`, `h@cell(c,r)` — the cursor must rest; `rd text` shows the tooltip text); a search
can sweep the map: actions = hovers over the visible cells, cell = the tooltip kind state you name.

Report rd cov --blocks before / after (with --fn), the hovered thing → branch → tooltip text table.
