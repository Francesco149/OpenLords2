# OpenLords2 — port frontier (what to port next)

> **DERIVED FILE** — `python3 tools/gen_port_ledger.py && python3 tools/gen_frontier.py`.

The unported, non-thunk engine functions already **called by ported code** — the natural next chips. A **leaf** has all of its
own callees ported, so it can land today with zero new dependencies. Sorted by how many ported callers want it.

- frontier functions: **191**
- of those, zero-dependency **leaves: 78** (recommended order below)

## Leaf shortlist — portable today (top 40 by ported-caller count)

| VA | size | ported callers | band |
|----|-----:|---------------:|------|
| 0x4af502 | 72 | 7 | asset-load IO helper cluster (0x4af*) |
| 0x4c0580 | 25 | 2 | late/tail helpers |
| 0x42e26f | 41 | 2 | strategic-map camera + scroll |
| 0x42f58c | 130 | 2 | strategic-map camera + scroll |
| 0x42eaa6 | 214 | 2 | strategic-map camera + scroll |
| 0x43fd2a | 413 | 2 | stage/post-load + char tables |
| 0x47f474 | 761 | 2 | d3d/mesh pool + preload + palette |
| 0x49caaa | 935 | 2 | npc schedule + worker walk |
| 0x480f8b | 1200 | 2 | equip / starter-items |
| 0x41423f | 11 | 1 | render-quad / blit primitives |
| 0x45242b | 11 | 1 | fade / loading / audio-mci |
| 0x4b1dd5 | 11 | 1 | battlefield render + wndproc |
| 0x401c5b | 21 | 1 | early/runtime + low-level helpers |
| 0x4360f2 | 41 | 1 | stage/post-load + char tables |
| 0x49ca32 | 46 | 1 | npc schedule + worker walk |
| 0x49bbe8 | 53 | 1 | npc schedule + worker walk |
| 0x43fed2 | 54 | 1 | stage/post-load + char tables |
| 0x46eb78 | 54 | 1 | tables (item/char) + populators |
| 0x46ead0 | 57 | 1 | tables (item/char) + populators |
| 0x43e668 | 58 | 1 | stage/post-load + char tables |
| 0x43f8ff | 58 | 1 | stage/post-load + char tables |
| 0x4401f6 | 59 | 1 | sim dispatch + records |
| 0x49bc1d | 66 | 1 | npc schedule + worker walk |
| 0x476a5d | 74 | 1 | d3d/mesh pool + preload + palette |
| 0x440038 | 81 | 1 | sim dispatch + records |
| 0x423b4f | 85 | 1 | strategic-map camera + scroll |
| 0x4b97f9 | 86 | 1 | battlefield render + wndproc |
| 0x4651b9 | 91 | 1 | tables (item/char) + populators |
| 0x4476e4 | 92 | 1 | sim dispatch + records |
| 0x447740 | 92 | 1 | sim dispatch + records |
| 0x4ae5e2 | 96 | 1 | asset-load IO helper cluster (0x4af*) |
| 0x4b3d5d | 112 | 1 | battlefield render + wndproc |
| 0x4470c4 | 114 | 1 | sim dispatch + records |
| 0x476e21 | 116 | 1 | d3d/mesh pool + preload + palette |
| 0x498fb3 | 129 | 1 | npc schedule + worker walk |
| 0x46ef3c | 130 | 1 | tables (item/char) + populators |
| 0x41f15a | 131 | 1 | render-quad / blit primitives |
| 0x4b0502 | 137 | 1 | battlefield render + wndproc |
| 0x42476b | 138 | 1 | strategic-map camera + scroll |
| 0x448d7e | 148 | 1 | sim dispatch + records |

## Full frontier by address band

### early/runtime + low-level helpers (12)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x40f5fd | 470 | 2 | 11 |  |
| 0x401c5b | 21 | 1 | 0 | ✓ |
| 0x4050c0 | 53 | 1 | 1 |  |
| 0x4093e0 | 73 | 1 | 2 |  |
| 0x40e6f2 | 111 | 1 | 2 |  |
| 0x408ea4 | 118 | 1 | 1 |  |
| 0x40f10f | 145 | 1 | 1 |  |
| 0x40d1bc | 181 | 1 | 1 |  |
| 0x40c725 | 428 | 1 | 2 |  |
| 0x40cfd2 | 490 | 1 | 2 |  |
| 0x40f1a0 | 1117 | 1 | 38 |  |
| 0x40f7d3 | 1774 | 1 | 12 |  |

### render-quad / blit primitives (12)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x41a734 | 272 | 2 | 1 |  |
| 0x41423f | 11 | 1 | 0 | ✓ |
| 0x41f15a | 131 | 1 | 0 | ✓ |
| 0x41a844 | 188 | 1 | 1 |  |
| 0x41ab67 | 215 | 1 | 0 | ✓ |
| 0x41a639 | 251 | 1 | 0 | ✓ |
| 0x410f4b | 358 | 1 | 1 |  |
| 0x41e7e1 | 385 | 1 | 14 |  |
| 0x41298a | 425 | 1 | 0 | ✓ |
| 0x412795 | 501 | 1 | 1 |  |
| 0x419c78 | 682 | 1 | 4 |  |
| 0x411b72 | 1132 | 1 | 4 |  |

### strategic-map camera + scroll (34)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x42e26f | 41 | 2 | 0 | ✓ |
| 0x42d91b | 83 | 2 | 1 |  |
| 0x42f58c | 130 | 2 | 0 | ✓ |
| 0x42eaa6 | 214 | 2 | 0 | ✓ |
| 0x4263ad | 255 | 2 | 2 |  |
| 0x4298c1 | 604 | 2 | 1 |  |
| 0x427990 | 779 | 2 | 3 |  |
| 0x42c64f | 1531 | 2 | 3 |  |
| 0x423b4f | 85 | 1 | 0 | ✓ |
| 0x428007 | 113 | 1 | 1 |  |
| 0x428078 | 113 | 1 | 1 |  |
| 0x42b780 | 119 | 1 | 5 |  |
| 0x42476b | 138 | 1 | 0 | ✓ |
| 0x42d179 | 157 | 1 | 0 | ✓ |
| 0x4247f5 | 174 | 1 | 1 |  |
| 0x42e50d | 179 | 1 | 0 | ✓ |
| 0x426216 | 185 | 1 | 0 | ✓ |
| 0x42a15f | 206 | 1 | 0 | ✓ |
| 0x4248a3 | 239 | 1 | 1 |  |
| 0x420316 | 274 | 1 | 1 |  |
| 0x4233f7 | 313 | 1 | 5 |  |
| 0x425314 | 371 | 1 | 1 |  |
| 0x423739 | 383 | 1 | 3 |  |
| 0x425487 | 437 | 1 | 1 |  |
| 0x423241 | 438 | 1 | 4 |  |
| 0x423530 | 521 | 1 | 1 |  |
| 0x429418 | 669 | 1 | 3 |  |
| 0x42dbc7 | 868 | 1 | 6 |  |
| 0x429ba4 | 878 | 1 | 0 | ✓ |
| 0x4280e9 | 904 | 1 | 4 |  |
| 0x42f60e | 2298 | 1 | 0 | ✓ |
| 0x424992 | 2434 | 1 | 1 |  |
| 0x423ba4 | 3015 | 1 | 1 |  |
| 0x42ff10 | 8140 | 1 | 55 |  |

### stage/post-load + char tables (16)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x43fd2a | 413 | 2 | 0 | ✓ |
| 0x433cc1 | 35 | 1 | 1 |  |
| 0x4360f2 | 41 | 1 | 0 | ✓ |
| 0x43fed2 | 54 | 1 | 0 | ✓ |
| 0x43e668 | 58 | 1 | 0 | ✓ |
| 0x43f8ff | 58 | 1 | 0 | ✓ |
| 0x43f7e3 | 114 | 1 | 4 |  |
| 0x43c885 | 139 | 1 | 1 |  |
| 0x43441c | 158 | 1 | 2 |  |
| 0x43e9e2 | 223 | 1 | 0 | ✓ |
| 0x43600c | 230 | 1 | 0 | ✓ |
| 0x43ad25 | 256 | 1 | 4 |  |
| 0x43ac23 | 258 | 1 | 1 |  |
| 0x43c77a | 267 | 1 | 2 |  |
| 0x43c910 | 344 | 1 | 2 |  |
| 0x43f5c5 | 542 | 1 | 10 |  |

### sim dispatch + records (13)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4401f6 | 59 | 1 | 0 | ✓ |
| 0x440038 | 81 | 1 | 0 | ✓ |
| 0x4476e4 | 92 | 1 | 0 | ✓ |
| 0x447740 | 92 | 1 | 0 | ✓ |
| 0x4470c4 | 114 | 1 | 0 | ✓ |
| 0x448d7e | 148 | 1 | 0 | ✓ |
| 0x447026 | 158 | 1 | 0 | ✓ |
| 0x4485a5 | 163 | 1 | 6 |  |
| 0x448440 | 261 | 1 | 28 |  |
| 0x4407e5 | 426 | 1 | 1 |  |
| 0x4477e7 | 503 | 1 | 0 | ✓ |
| 0x44098f | 595 | 1 | 1 |  |
| 0x44df5f | 1628 | 1 | 5 |  |

### fade / loading / audio-mci (2)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x45242b | 11 | 1 | 0 | ✓ |
| 0x451fcc | 385 | 1 | 0 | ✓ |

### tables (item/char) + populators (14)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x46a037 | 202 | 2 | 1 |  |
| 0x46eb78 | 54 | 1 | 0 | ✓ |
| 0x46ead0 | 57 | 1 | 0 | ✓ |
| 0x4651b9 | 91 | 1 | 0 | ✓ |
| 0x46ef3c | 130 | 1 | 0 | ✓ |
| 0x46ebe4 | 138 | 1 | 2 |  |
| 0x46a101 | 165 | 1 | 1 |  |
| 0x46dd34 | 209 | 1 | 1 |  |
| 0x46efbe | 235 | 1 | 0 | ✓ |
| 0x46d2a3 | 301 | 1 | 0 | ✓ |
| 0x46dbf5 | 319 | 1 | 3 |  |
| 0x46da4b | 426 | 1 | 0 | ✓ |
| 0x46ff43 | 617 | 1 | 0 | ✓ |
| 0x46e1b0 | 792 | 1 | 1 |  |

### d3d/mesh pool + preload + palette (18)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4788c6 | 140 | 2 | 9 |  |
| 0x47f474 | 761 | 2 | 0 | ✓ |
| 0x476a5d | 74 | 1 | 0 | ✓ |
| 0x476e21 | 116 | 1 | 0 | ✓ |
| 0x475d73 | 148 | 1 | 2 |  |
| 0x472a31 | 152 | 1 | 0 | ✓ |
| 0x478419 | 177 | 1 | 0 | ✓ |
| 0x477b2f | 346 | 1 | 2 |  |
| 0x477c89 | 371 | 1 | 7 |  |
| 0x476e95 | 421 | 1 | 3 |  |
| 0x47f76d | 458 | 1 | 0 | ✓ |
| 0x47fc01 | 522 | 1 | 0 | ✓ |
| 0x475b41 | 562 | 1 | 0 | ✓ |
| 0x47ed9b | 590 | 1 | 0 | ✓ |
| 0x476488 | 648 | 1 | 4 |  |
| 0x4784ca | 730 | 1 | 0 | ✓ |
| 0x476aa7 | 770 | 1 | 1 |  |
| 0x475e07 | 1665 | 1 | 7 |  |

### equip / starter-items (6)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x480f8b | 1200 | 2 | 0 | ✓ |
| 0x48970e | 138 | 1 | 4 |  |
| 0x489654 | 186 | 1 | 0 | ✓ |
| 0x485861 | 388 | 1 | 1 |  |
| 0x485675 | 492 | 1 | 1 |  |
| 0x4891ad | 596 | 1 | 0 | ✓ |

### npc schedule + worker walk (30)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4984dc | 731 | 4 | 5 |  |
| 0x49964c | 124 | 3 | 2 |  |
| 0x49915e | 259 | 3 | 2 |  |
| 0x499366 | 691 | 3 | 2 |  |
| 0x499034 | 197 | 2 | 4 |  |
| 0x499aca | 487 | 2 | 2 |  |
| 0x49caaa | 935 | 2 | 0 | ✓ |
| 0x499d7d | 26 | 1 | 1 |  |
| 0x499d97 | 26 | 1 | 1 |  |
| 0x49767d | 36 | 1 | 5 |  |
| 0x49ca32 | 46 | 1 | 0 | ✓ |
| 0x497879 | 52 | 1 | 1 |  |
| 0x49bbe8 | 53 | 1 | 0 | ✓ |
| 0x49bc1d | 66 | 1 | 0 | ✓ |
| 0x49ce51 | 109 | 1 | 1 |  |
| 0x498fb3 | 129 | 1 | 0 | ✓ |
| 0x49c995 | 157 | 1 | 3 |  |
| 0x4975d3 | 170 | 1 | 3 |  |
| 0x496f72 | 178 | 1 | 0 | ✓ |
| 0x49bae9 | 180 | 1 | 0 | ✓ |
| 0x497500 | 211 | 1 | 8 |  |
| 0x49a453 | 302 | 1 | 0 | ✓ |
| 0x498270 | 327 | 1 | 3 |  |
| 0x49b762 | 334 | 1 | 0 | ✓ |
| 0x49b973 | 374 | 1 | 2 |  |
| 0x49dfc6 | 505 | 1 | 4 |  |
| 0x49459a | 878 | 1 | 1 |  |
| 0x49a581 | 1039 | 1 | 15 |  |
| 0x49d638 | 1737 | 1 | 0 | ✓ |
| 0x492c8b | 3793 | 1 | 11 |  |

### asset-load IO helper cluster (0x4af*) (15)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4af502 | 72 | 7 | 0 | ✓ |
| 0x4ab383 | 1751 | 2 | 11 |  |
| 0x4a84ba | 77 | 1 | 1 |  |
| 0x4ae5e2 | 96 | 1 | 0 | ✓ |
| 0x4a4f5b | 168 | 1 | 0 | ✓ |
| 0x4ae539 | 169 | 1 | 0 | ✓ |
| 0x4af767 | 218 | 1 | 1 |  |
| 0x4a4e3d | 286 | 1 | 1 |  |
| 0x4ac499 | 289 | 1 | 4 |  |
| 0x4aca2b | 297 | 1 | 1 |  |
| 0x4aecf0 | 334 | 1 | 1 |  |
| 0x4a82b9 | 365 | 1 | 1 |  |
| 0x4aef7e | 408 | 1 | 1 |  |
| 0x4a1847 | 525 | 1 | 0 | ✓ |
| 0x4a8aab | 753 | 1 | 6 |  |

### battlefield render + wndproc (18)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4b0cb4 | 314 | 2 | 1 |  |
| 0x4b8c3a | 1202 | 2 | 8 |  |
| 0x4b1dd5 | 11 | 1 | 0 | ✓ |
| 0x4b756b | 26 | 1 | 1 |  |
| 0x4b92a0 | 57 | 1 | 1 |  |
| 0x4b3571 | 80 | 1 | 1 |  |
| 0x4b97f9 | 86 | 1 | 0 | ✓ |
| 0x4b3b92 | 89 | 1 | 1 |  |
| 0x4b3d5d | 112 | 1 | 0 | ✓ |
| 0x4b0502 | 137 | 1 | 0 | ✓ |
| 0x4b6a15 | 161 | 1 | 1 |  |
| 0x4b3acd | 197 | 1 | 2 |  |
| 0x4b1310 | 221 | 1 | 0 | ✓ |
| 0x4b1cf3 | 226 | 1 | 0 | ✓ |
| 0x4b35c1 | 255 | 1 | 1 |  |
| 0x4b90fb | 394 | 1 | 2 |  |
| 0x4b14ff | 872 | 1 | 0 | ✓ |
| 0x4ba26e | 1882 | 1 | 34 |  |

### late/tail helpers (1)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4c0580 | 25 | 2 | 0 | ✓ |

