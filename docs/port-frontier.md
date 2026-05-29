# OpenLords2 — port frontier (what to port next)

> **DERIVED FILE** — `python3 tools/gen_port_ledger.py && python3 tools/gen_frontier.py`.

The unported, non-thunk engine functions already **called by ported code** — the natural next chips. A **leaf** has all of its
own callees ported, so it can land today with zero new dependencies. Sorted by how many ported callers want it.

- frontier functions: **148**
- of those, zero-dependency **leaves: 53** (recommended order below)

## Leaf shortlist — portable today (top 40 by ported-caller count)

| VA | size | ported callers | band |
|----|-----:|---------------:|------|
| 0x42e26f | 41 | 2 | strategic-map camera + scroll |
| 0x42f58c | 130 | 2 | strategic-map camera + scroll |
| 0x4af116 | 508 | 2 | asset-load IO helper cluster (0x4af*) |
| 0x480f8b | 1200 | 2 | equip / starter-items |
| 0x41423f | 11 | 1 | render-quad / blit primitives |
| 0x45242b | 11 | 1 | fade / loading / audio-mci |
| 0x4b1dd5 | 11 | 1 | battlefield render + wndproc |
| 0x401c5b | 21 | 1 | early/runtime + low-level helpers |
| 0x4c0580 | 25 | 1 | late/tail helpers |
| 0x433cc1 | 35 | 1 | stage/post-load + char tables |
| 0x4360f2 | 41 | 1 | stage/post-load + char tables |
| 0x478952 | 53 | 1 | d3d/mesh pool + preload + palette |
| 0x43fed2 | 54 | 1 | stage/post-load + char tables |
| 0x43f8ff | 58 | 1 | stage/post-load + char tables |
| 0x476a5d | 74 | 1 | d3d/mesh pool + preload + palette |
| 0x423b4f | 85 | 1 | strategic-map camera + scroll |
| 0x4b97f9 | 86 | 1 | battlefield render + wndproc |
| 0x427d19 | 89 | 1 | strategic-map camera + scroll |
| 0x4651b9 | 91 | 1 | tables (item/char) + populators |
| 0x4ae5e2 | 96 | 1 | asset-load IO helper cluster (0x4af*) |
| 0x4b049b | 103 | 1 | battlefield render + wndproc |
| 0x4b3d5d | 112 | 1 | battlefield render + wndproc |
| 0x4262eb | 117 | 1 | strategic-map camera + scroll |
| 0x41f15a | 131 | 1 | render-quad / blit primitives |
| 0x42476b | 138 | 1 | strategic-map camera + scroll |
| 0x448d7e | 148 | 1 | sim dispatch + records |
| 0x472a31 | 152 | 1 | d3d/mesh pool + preload + palette |
| 0x42d179 | 157 | 1 | strategic-map camera + scroll |
| 0x4a4f5b | 168 | 1 | asset-load IO helper cluster (0x4af*) |
| 0x42e50d | 179 | 1 | strategic-map camera + scroll |
| 0x426216 | 185 | 1 | strategic-map camera + scroll |
| 0x42a15f | 206 | 1 | strategic-map camera + scroll |
| 0x4c2090 | 224 | 1 | late/tail helpers |
| 0x4b1cf3 | 226 | 1 | battlefield render + wndproc |
| 0x41a639 | 251 | 1 | render-quad / blit primitives |
| 0x46d2a3 | 301 | 1 | tables (item/char) + populators |
| 0x49b762 | 334 | 1 | npc schedule + worker walk |
| 0x43fd2a | 413 | 1 | stage/post-load + char tables |
| 0x41298a | 425 | 1 | render-quad / blit primitives |
| 0x47f76d | 458 | 1 | d3d/mesh pool + preload + palette |

## Full frontier by address band

### early/runtime + low-level helpers (9)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x401c5b | 21 | 1 | 0 | ✓ |
| 0x4050c0 | 53 | 1 | 1 |  |
| 0x40e6f2 | 111 | 1 | 2 |  |
| 0x408ea4 | 118 | 1 | 1 |  |
| 0x40f10f | 145 | 1 | 1 |  |
| 0x40c725 | 428 | 1 | 2 |  |
| 0x40f5fd | 470 | 1 | 12 |  |
| 0x40f1a0 | 1117 | 1 | 38 |  |
| 0x40f7d3 | 1774 | 1 | 12 |  |

### render-quad / blit primitives (10)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x41a734 | 272 | 2 | 1 |  |
| 0x41423f | 11 | 1 | 0 | ✓ |
| 0x41f15a | 131 | 1 | 0 | ✓ |
| 0x41a844 | 188 | 1 | 1 |  |
| 0x41a639 | 251 | 1 | 0 | ✓ |
| 0x410f4b | 358 | 1 | 1 |  |
| 0x41e7e1 | 385 | 1 | 14 |  |
| 0x41298a | 425 | 1 | 0 | ✓ |
| 0x412795 | 501 | 1 | 1 |  |
| 0x419c78 | 682 | 1 | 4 |  |

### strategic-map camera + scroll (31)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x42e26f | 41 | 2 | 0 | ✓ |
| 0x42f58c | 130 | 2 | 0 | ✓ |
| 0x4263ad | 255 | 2 | 3 |  |
| 0x423b4f | 85 | 1 | 0 | ✓ |
| 0x427d19 | 89 | 1 | 0 | ✓ |
| 0x4262eb | 117 | 1 | 0 | ✓ |
| 0x42b780 | 119 | 1 | 6 |  |
| 0x42476b | 138 | 1 | 0 | ✓ |
| 0x42f148 | 151 | 1 | 1 |  |
| 0x42d179 | 157 | 1 | 0 | ✓ |
| 0x4247f5 | 174 | 1 | 1 |  |
| 0x42e50d | 179 | 1 | 0 | ✓ |
| 0x426216 | 185 | 1 | 0 | ✓ |
| 0x42a15f | 206 | 1 | 0 | ✓ |
| 0x4248a3 | 239 | 1 | 1 |  |
| 0x420316 | 274 | 1 | 1 |  |
| 0x425314 | 371 | 1 | 1 |  |
| 0x423739 | 383 | 1 | 3 |  |
| 0x425487 | 437 | 1 | 1 |  |
| 0x423530 | 521 | 1 | 1 |  |
| 0x4298c1 | 604 | 1 | 1 |  |
| 0x429418 | 669 | 1 | 3 |  |
| 0x427990 | 779 | 1 | 7 |  |
| 0x42dbc7 | 868 | 1 | 9 |  |
| 0x429ba4 | 878 | 1 | 0 | ✓ |
| 0x4280e9 | 904 | 1 | 4 |  |
| 0x42c64f | 1531 | 1 | 3 |  |
| 0x42f60e | 2298 | 1 | 0 | ✓ |
| 0x424992 | 2434 | 1 | 1 |  |
| 0x423ba4 | 3015 | 1 | 1 |  |
| 0x42ff10 | 8140 | 1 | 59 |  |

### stage/post-load + char tables (15)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x43eda0 | 637 | 7 | 4 |  |
| 0x433cc1 | 35 | 1 | 0 | ✓ |
| 0x4360f2 | 41 | 1 | 0 | ✓ |
| 0x43fed2 | 54 | 1 | 0 | ✓ |
| 0x43f8ff | 58 | 1 | 0 | ✓ |
| 0x43f7e3 | 114 | 1 | 4 |  |
| 0x43c885 | 139 | 1 | 1 |  |
| 0x43441c | 158 | 1 | 2 |  |
| 0x43600c | 230 | 1 | 1 |  |
| 0x43ad25 | 256 | 1 | 4 |  |
| 0x43ac23 | 258 | 1 | 3 |  |
| 0x43c77a | 267 | 1 | 3 |  |
| 0x43c910 | 344 | 1 | 2 |  |
| 0x43fd2a | 413 | 1 | 0 | ✓ |
| 0x43f5c5 | 542 | 1 | 11 |  |

### sim dispatch + records (5)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x448d7e | 148 | 1 | 0 | ✓ |
| 0x448440 | 261 | 1 | 28 |  |
| 0x4407e5 | 426 | 1 | 1 |  |
| 0x4477e7 | 503 | 1 | 0 | ✓ |
| 0x44098f | 595 | 1 | 1 |  |

### fade / loading / audio-mci (1)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x45242b | 11 | 1 | 0 | ✓ |

### tables (item/char) + populators (7)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4651b9 | 91 | 1 | 0 | ✓ |
| 0x46a101 | 165 | 1 | 1 |  |
| 0x46a037 | 202 | 1 | 2 |  |
| 0x4650b0 | 265 | 1 | 2 |  |
| 0x46d2a3 | 301 | 1 | 0 | ✓ |
| 0x467770 | 525 | 1 | 0 | ✓ |
| 0x46ff43 | 617 | 1 | 0 | ✓ |

### d3d/mesh pool + preload + palette (12)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x478952 | 53 | 1 | 0 | ✓ |
| 0x476a5d | 74 | 1 | 0 | ✓ |
| 0x4788c6 | 140 | 1 | 10 |  |
| 0x472a31 | 152 | 1 | 0 | ✓ |
| 0x476e95 | 421 | 1 | 3 |  |
| 0x47f76d | 458 | 1 | 0 | ✓ |
| 0x47ed9b | 590 | 1 | 0 | ✓ |
| 0x472e46 | 595 | 1 | 2 |  |
| 0x4784ca | 730 | 1 | 0 | ✓ |
| 0x47f474 | 761 | 1 | 0 | ✓ |
| 0x476aa7 | 770 | 1 | 1 |  |
| 0x477dfc | 1225 | 1 | 8 |  |

### equip / starter-items (6)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x480f8b | 1200 | 2 | 0 | ✓ |
| 0x4859e5 | 354 | 1 | 2 |  |
| 0x489401 | 595 | 1 | 7 |  |
| 0x4822ed | 644 | 1 | 3 |  |
| 0x488dfe | 943 | 1 | 0 | ✓ |
| 0x485bb1 | 1592 | 1 | 5 |  |

### npc schedule + worker walk (19)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4984dc | 731 | 3 | 7 |  |
| 0x499034 | 197 | 2 | 5 |  |
| 0x4987b7 | 1536 | 2 | 7 |  |
| 0x49767d | 36 | 1 | 5 |  |
| 0x49ce51 | 109 | 1 | 1 |  |
| 0x49964c | 124 | 1 | 2 |  |
| 0x49c995 | 157 | 1 | 3 |  |
| 0x4975d3 | 170 | 1 | 3 |  |
| 0x497500 | 211 | 1 | 8 |  |
| 0x49915e | 259 | 1 | 2 |  |
| 0x498270 | 327 | 1 | 4 |  |
| 0x49b762 | 334 | 1 | 0 | ✓ |
| 0x49b973 | 374 | 1 | 2 |  |
| 0x4976a1 | 472 | 1 | 18 |  |
| 0x49dfc6 | 505 | 1 | 4 |  |
| 0x499366 | 691 | 1 | 2 |  |
| 0x49a581 | 1039 | 1 | 16 |  |
| 0x49d638 | 1737 | 1 | 0 | ✓ |
| 0x49aa0e | 2197 | 1 | 0 | ✓ |

### asset-load IO helper cluster (0x4af*) (17)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4afab9 | 897 | 31 | 2 |  |
| 0x4af502 | 72 | 5 | 1 |  |
| 0x4af312 | 496 | 2 | 1 |  |
| 0x4af116 | 508 | 2 | 0 | ✓ |
| 0x4a84ba | 77 | 1 | 1 |  |
| 0x4ae5e2 | 96 | 1 | 0 | ✓ |
| 0x4a4f5b | 168 | 1 | 0 | ✓ |
| 0x4af767 | 218 | 1 | 3 |  |
| 0x4ae1b8 | 243 | 1 | 4 |  |
| 0x4a4e3d | 286 | 1 | 1 |  |
| 0x4ac499 | 289 | 1 | 4 |  |
| 0x4aca2b | 297 | 1 | 1 |  |
| 0x4a82b9 | 365 | 1 | 1 |  |
| 0x4aef7e | 408 | 1 | 1 |  |
| 0x4a1847 | 525 | 1 | 0 | ✓ |
| 0x4a8aab | 753 | 1 | 6 |  |
| 0x4ab383 | 1751 | 1 | 11 |  |

### battlefield render + wndproc (14)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4b0cb4 | 314 | 2 | 1 |  |
| 0x4b1dd5 | 11 | 1 | 0 | ✓ |
| 0x4b756b | 26 | 1 | 1 |  |
| 0x4b25fc | 58 | 1 | 1 |  |
| 0x4b25b0 | 76 | 1 | 1 |  |
| 0x4b3571 | 80 | 1 | 1 |  |
| 0x4b97f9 | 86 | 1 | 0 | ✓ |
| 0x4b049b | 103 | 1 | 0 | ✓ |
| 0x4b3d5d | 112 | 1 | 0 | ✓ |
| 0x4b7180 | 208 | 1 | 3 |  |
| 0x4b1cf3 | 226 | 1 | 0 | ✓ |
| 0x4b14ff | 872 | 1 | 0 | ✓ |
| 0x4b8c3a | 1202 | 1 | 9 |  |
| 0x4ba26e | 1882 | 1 | 34 |  |

### late/tail helpers (2)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4c0580 | 25 | 1 | 0 | ✓ |
| 0x4c2090 | 224 | 1 | 0 | ✓ |

