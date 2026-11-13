---
start: newgame-playnow       # Play Now, Winter 1268, the map, 5000 crowns, county 2 Raw Sienna (castle level 0)
steps:
  - a castle being built in our county: C(2).castle_building != 0
  - the labor grid used (workforce or labor screen open): screen in (2, 15)
  - a castle completed: C(2).castle >= 1 and C(2).castle_building == 0
  - an upgrade started on it: C(2).castle_building == 2
forbid:
  - the game lost: my_counties == 0
---
# Build a castle and upgrade it, working the labor grid and the fields — the economy the bot never touched

tools/bot/README.md "Not yet": no labor grid or field sowing (the labor screen 0x0f / workforce 0x02: dragging labor
icons between tasks — grain, cattle, reclaim, castle, iron, stone, wood, smithy; C(k).labor_left), no castle upgrades
(docs/human-traces.md A3). A castle is planned on the castle screen (0x1b), placed on the map, then built by the castle
labor lane over seasons (C(k).castle_building 1 new / 2 upgrade, castle level C(k).castle, prior level castle_prev,
wood / stone needed: county +0x1d0 / +0x1d4). Fields: clicking a fallow field changes its usage (wheat sown in winter,
cattle pasture; the first-scroll hint says so).

Use drags for the labor icons (`dX,Y-X2,Y2`) — the human-trace list (A6) wants held-button paths exercised. Plan a
small castle so it completes within a few seasons, move labor to the castle lane, end turns until it is built (search
the turns if events / scrolls interfere), then start an upgrade. Keep the county fed (rations, cattle). Report frames,
how each screen works (points, drags), and rd cov.
