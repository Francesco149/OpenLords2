---
start: newgame-playnow
steps:
  - in game on the map: state == 3 and screen == 0
  - the options screens used: screen in (0x21, 0x39, 0x42, 0x43)
  - still in game at the end: state == 3
forbid:
  - the game lost: my_counties == 0
---
# Block coverage: the music streamer (fptc 0x426dd5) and the sound / display options

`fptc` 0x426dd5 is the music streamer's timer callback (timeSetEvent every 480 ms, run by rhost on the virtual clock):
57 untaken blocks / 2 KB. It branches on the stream state — track changes (screens / seasons / battles pick tracks),
music off / on, volume, the end of a track, a stream restart. `rd cov R --blocks --fn fptc` lists them; read it in the
IDA export (and its callers / the options handlers) to see which inputs move the state. Then exercise them: Options >
Sounds (0x42: music / effects toggles, volumes), Display (0x43), Game speed / Scroll speed (0x21), Advanced (0x39);
screens that change the music (the castle, the battlefield via a battle prompt, the title), seasons (`rd seasons`), and
waits long enough for a track to end (a track is minutes of virtual time: `rd play R ".*N"`).

Report `rd cov R --blocks` before / after (with --fn fptc), the input → stream state → branch table, and what rhost's
silent DirectSound cannot reach (if anything) and why.
