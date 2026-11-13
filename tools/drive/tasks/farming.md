---
start: newgame-playnow       # Play Now, Winter 1268, Raw Sienna — or custom-strong with Advanced farming on (say which)
steps:
  - in game on the map: state == 3 and screen == 0
  - the Town Center / labor screens used: screen in (2, 5, 6, 15)
  - still in game at the end: state == 3
forbid:
  - the game lost: my_counties == 0
---
# Block coverage: the grain and cattle labor panels through the farming year

`labor_panel_grain` 0x413590 (21 untaken blocks, 929 B) and `labor_panel_cattle` 0x413b30 (20 blocks, 802 B) — the
Town Center's farm panels — branch on the season, the fields' stages (sown, ripening, harvest, flooded, parched,
reclaiming), the worker balance (C(k).grain_workers / cattle_workers vs needed), surplus / shortage, herd crowding,
and Advanced farming. `rd cov R --blocks --fn labor_panel_grain,labor_panel_cattle` lists them; read both in the IDA
export and plan which county states reach which branch. Then farm through the year: sow wheat in winter (a fallow
field's terrain info → `@field_wheat`), move workers between grain and cattle (drags on the Town Center), let fields
ripen and harvest, overcrowd and thin a herd, starve a lane of workers, and open both panels in each state (`rd
seasons R 1` per season; checkpoint before each experiment). castle-labor and terrain-info (tests/scenarios headers)
show the screens.

Report `rd cov R --blocks` before / after (with --fn for both panels), the county state → branch table, and the farming
facts you verified (yields, sowing / harvest seasons, crowding).
