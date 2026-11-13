---
start: bot3cen-s207-t44@38000   # custom England, the bot's long game: the human holds county 13 (odd) — check the frame is mid-turn on the map
steps:
  - in game, the map, our turn: state == 3 and screen == 0
  - the realm event 0x13d is up: modal == 0x13d
  - its scroll dismissed, back on the map: modal == 0 and screen == 0
forbid:
  - the game lost: my_counties == 0
---
# Make the one realm event the corpus never rolled happen: ev_13d (FUN_00449826)

Realm events (docs/findings/game-state-map.md §Realm events, src/engine_realm_events.c): at turn processing, per county
i the human owns after 1268, index v = (2 × (u32(0x58fd60) & 0x7f) + i) mod 256 into the u16 table 0x4d6108; a non-zero
entry is the event id (its handler; `modal` = the event scroll's id while it is up). The table has exactly one 0x13d
entry. Only odd counties can roll (all entries sit at v ≡ 7 mod 8). 22 of the 24 handlers are already in the corpus;
0x13d never rolled.

So: find which index holds 0x13d (read the table: `rd ram R dwords 4d6108 128`), which county the human owns that can
reach it, and what the RNG word must be at the roll. Then make the roll happen: the RNG advances while the game runs,
so how long you wait (and what you do) before End turn changes the draw. This is a search — `--goal "modal == 0x13d"`,
actions = different waits followed by End turn and the report scrolls' RMB, cell over (turn, modal != 0, …); decide
what the cell needs after reading the roll code (IDA `engine_realm_roll_events` 0x448819). If the start frame is not a
good mid-turn moment, pick another `bot3cen-s207-t44@F` (or another odd-county human game, e.g. `custom-strong`, where
the human holds Poitou = county 5) and say why.

Look at the event scroll when it is up (rd look) and say what it is. Report the frame, the RNG value at the roll, the
handler's effect on the county (rd ram mark → diff around the roll), and rd cov.
