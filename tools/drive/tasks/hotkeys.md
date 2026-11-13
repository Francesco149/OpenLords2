---
start: bot3cen-s207-t44@36000   # custom England mid-game: two counties, armies, the map — pick a frame where it is our turn on the map
steps:
  - in game on the map: state == 3 and screen == 0
  - still in game at the end: state == 3
forbid:
  - the game lost: my_counties == 0
---
# Block coverage: the in-game input dispatcher's untaken branches (keyboard shortcuts and friends)

`engine_ingame_input_dispatch` 0x42ff10 is reached by the corpus but 190 of its basic blocks (2,756 bytes) never run
(`rd cov R --blocks` lists per function). Read it in the IDA export (`/mnt/ephemeral/openlords2/decompiled-ida/all_ida.c`,
search `engine_ingame_input_dispatch`; the asm in all_ida.asm) and list which inputs / states reach the untaken
branches: virtual keys (WM_KEYDOWN wParam), WM_CHAR codes, mouse buttons in particular screens, modifier states.
Then exercise as many as you can from the map and from the screens they open — `kNAME` / `eM:W:L` raw events for keys
the card has no name for — backing out after each (RMB / ESC) so the run stays in a sane state. Avoid the ones that
quit the game or open a save over an existing file; say which you skipped.

The deliverable is coverage, not an end state: report `rd cov R --blocks` (new blocks, bytes, by function) before and
after, which input reached which branch (what each shortcut does in the game), and anything that changes game state.
