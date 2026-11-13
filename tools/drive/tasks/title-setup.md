---
start: boot-title            # the title menu after boot (no movies) — or custom-tour@600
steps:
  - on the title menus: screen == 31
  - a game in progress at some point: state == 3
final:
  - in a game or on the title at the end: state in (2, 3)
---
# Block coverage: the title menus, the custom setup and the config defaults

engine_menu_title_input 0x4ba9c8 (42 untaken blocks / 723 B), config_set_defaults 0x40ecc4 (32 / 726 B), the title
screens sub_41FF75 (21 / 434 B) and sub_41E1DD (6 / 572 B), noble_face_draw 0x4171ee (8 / 756 B), sub_41608B (11 / 599 B)
are reached but far from complete (`rd cov R --blocks --fn engine_menu_title_input,config_set_defaults,sub_41FF75,sub_41E1DD,noble_face_draw,sub_41608B`).
Read them in the IDA export, map the untaken branches to inputs (every title page and its buttons, the save / load
lists with and without saves, the campaign / Play Now / Custom choices, every custom-setup option and its drop-down,
each map, nobles and their faces / names, the Battle Master ratings, keyboard on the title), and exercise them.
custom-tour's header lists the setup options; multiplayer pages are out of scope (rhost stubs DirectPlay).

Report rd cov --blocks before / after (with --fn), the input → branch table, and title / setup facts (points).
