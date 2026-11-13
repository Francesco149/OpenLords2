---
start: custom-strong@6000   # France, five lords, every county > 1000 souls — the most diplomacy-rich start; pick a quiet mid-turn frame
steps:
  - in game on the map: state == 3 and screen == 0
  - an event / AI message scroll up: modal != 0
  - still in game at the end: state == 3
forbid:
  - quit to the title: state == 2
---
# Block coverage: the popup scroll renderer's untaken branches (AI messages, events, offers)

`engine_popup_scroll_render` 0x47309e (10.9 KB) is reached, but 57 blocks / 2.7 KB never run: `rd cov R --blocks --fn
engine_popup_scroll_render` lists them. Read it in the IDA export and map each untaken block to its popup type
(`popup_type` 0x5440a4: 0/2/3/13/14/18 notices, 1 AI message, 10 pay-for-help, 11 offers, 17 garrison split; 12 / 20
are multiplayer only — skip), sender (`popup_from`), message line (0x5440ac) and layout branch. Then make the game
send them: diplomacy with each lord (screen 0xb: gifts, compliments, insults, alliance requests, threats, pleas for
help via an ally — `tools/bot/actions.py` diplomacy() and docs/findings/coverage-round3.md have the clicks and the
AI's answer conditions), AI demands (tribute, surrender), events (`rd seasons R N --until "modal != 0"` stops at each
one). Answer each popup both ways where it has a choice (checkpoint, yes; load, no).

Report `rd cov R --blocks` before / after (and --fn for 0x47309e), the popup type → branch → what triggered it table,
and what still needs multiplayer or is dead.
