# OpenLords2 — port frontier (what to port next)

> **DERIVED FILE** — `python3 tools/gen_port_ledger.py && python3 tools/gen_frontier.py`.

The unported, non-thunk engine functions already **called by ported code** — the natural next chips. A **leaf** has all of its
own callees ported, so it can land today with zero new dependencies. Sorted by how many ported callers want it.

- frontier functions: **246**
- of those, zero-dependency **leaves: 148** (recommended order below)

## Leaf shortlist — portable today (top 40 by ported-caller count)

| VA | size | ported callers | band |
|----|-----:|---------------:|------|
| 0x4af116 | 508 | 4 | asset-load IO helper cluster (0x4af*) |
| 0x4360f2 | 41 | 3 | stage/post-load + char tables |
| 0x42e26f | 41 | 2 | strategic-map camera + scroll |
| 0x499619 | 51 | 2 | npc schedule + worker walk |
| 0x4120e0 | 99 | 2 | render-quad / blit primitives |
| 0x4b049b | 103 | 2 | battlefield render + wndproc |
| 0x427c9b | 126 | 2 | strategic-map camera + scroll |
| 0x42f58c | 130 | 2 | strategic-map camera + scroll |
| 0x463b42 | 196 | 2 | tables (item/char) + populators |
| 0x414e06 | 354 | 2 | render-quad / blit primitives |
| 0x4aa90a | 374 | 2 | asset-load IO helper cluster (0x4af*) |
| 0x420147 | 463 | 2 | strategic-map camera + scroll |
| 0x409934 | 863 | 2 | early/runtime + low-level helpers |
| 0x480f8b | 1200 | 2 | equip / starter-items |
| 0x41423f | 11 | 1 | render-quad / blit primitives |
| 0x45242b | 11 | 1 | fade / loading / audio-mci |
| 0x45280b | 11 | 1 | fade / loading / audio-mci |
| 0x46d421 | 11 | 1 | tables (item/char) + populators |
| 0x4b1dd5 | 11 | 1 | battlefield render + wndproc |
| 0x401c5b | 21 | 1 | early/runtime + low-level helpers |
| 0x41ef42 | 21 | 1 | render-quad / blit primitives |
| 0x4c0580 | 25 | 1 | late/tail helpers |
| 0x41b013 | 31 | 1 | render-quad / blit primitives |
| 0x43611b | 38 | 1 | stage/post-load + char tables |
| 0x452775 | 49 | 1 | fade / loading / audio-mci |
| 0x497879 | 52 | 1 | npc schedule + worker walk |
| 0x478952 | 53 | 1 | d3d/mesh pool + preload + palette |
| 0x43fed2 | 54 | 1 | stage/post-load + char tables |
| 0x43f8ff | 58 | 1 | stage/post-load + char tables |
| 0x4329a4 | 61 | 1 | stage/post-load + char tables |
| 0x472986 | 63 | 1 | d3d/mesh pool + preload + palette |
| 0x40c928 | 68 | 1 | early/runtime + low-level helpers |
| 0x4bbf89 | 73 | 1 | battlefield render + wndproc |
| 0x4bbfd2 | 73 | 1 | battlefield render + wndproc |
| 0x410c71 | 76 | 1 | render-quad / blit primitives |
| 0x41e5d0 | 77 | 1 | render-quad / blit primitives |
| 0x426360 | 77 | 1 | strategic-map camera + scroll |
| 0x440038 | 81 | 1 | sim dispatch + records |
| 0x4b3714 | 84 | 1 | battlefield render + wndproc |
| 0x4b97f9 | 86 | 1 | battlefield render + wndproc |

## Full frontier by address band

### early/runtime + low-level helpers (13)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x409934 | 863 | 2 | 0 | ✓ |
| 0x401c5b | 21 | 1 | 0 | ✓ |
| 0x40c928 | 68 | 1 | 0 | ✓ |
| 0x40c8d1 | 87 | 1 | 0 | ✓ |
| 0x40df62 | 107 | 1 | 0 | ✓ |
| 0x408ea4 | 118 | 1 | 1 |  |
| 0x40e00a | 143 | 1 | 0 | ✓ |
| 0x40f10f | 145 | 1 | 1 |  |
| 0x40902b | 148 | 1 | 0 | ✓ |
| 0x40ec2c | 152 | 1 | 1 |  |
| 0x40dd92 | 312 | 1 | 4 |  |
| 0x40c725 | 428 | 1 | 1 |  |
| 0x40cd58 | 433 | 1 | 0 | ✓ |

### render-quad / blit primitives (48)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x412b33 | 824 | 3 | 6 |  |
| 0x4120e0 | 99 | 2 | 0 | ✓ |
| 0x414e06 | 354 | 2 | 0 | ✓ |
| 0x41423f | 11 | 1 | 0 | ✓ |
| 0x41ef42 | 21 | 1 | 0 | ✓ |
| 0x41b013 | 31 | 1 | 0 | ✓ |
| 0x410c71 | 76 | 1 | 0 | ✓ |
| 0x41e5d0 | 77 | 1 | 0 | ✓ |
| 0x416308 | 87 | 1 | 2 |  |
| 0x413526 | 106 | 1 | 0 | ✓ |
| 0x419243 | 110 | 1 | 0 | ✓ |
| 0x4192b1 | 163 | 1 | 1 |  |
| 0x41543f | 171 | 1 | 0 | ✓ |
| 0x41f3e9 | 184 | 1 | 1 |  |
| 0x41a844 | 188 | 1 | 0 | ✓ |
| 0x41789b | 197 | 1 | 3 |  |
| 0x414819 | 203 | 1 | 0 | ✓ |
| 0x415fb7 | 212 | 1 | 0 | ✓ |
| 0x41f4a1 | 241 | 1 | 0 | ✓ |
| 0x4180f6 | 245 | 1 | 2 |  |
| 0x41a639 | 251 | 1 | 0 | ✓ |
| 0x418fc5 | 278 | 1 | 0 | ✓ |
| 0x417bd3 | 284 | 1 | 0 | ✓ |
| 0x419789 | 289 | 1 | 3 |  |
| 0x41ad5d | 325 | 1 | 1 |  |
| 0x4154ea | 340 | 1 | 0 | ✓ |
| 0x4152ea | 341 | 1 | 0 | ✓ |
| 0x410f4b | 358 | 1 | 0 | ✓ |
| 0x4190db | 360 | 1 | 0 | ✓ |
| 0x41aea2 | 369 | 1 | 0 | ✓ |
| 0x41515c | 398 | 1 | 0 | ✓ |
| 0x418e2d | 408 | 1 | 0 | ✓ |
| 0x41298a | 425 | 1 | 0 | ✓ |
| 0x414f68 | 500 | 1 | 0 | ✓ |
| 0x412795 | 501 | 1 | 1 |  |
| 0x4181eb | 571 | 1 | 0 | ✓ |
| 0x412421 | 581 | 1 | 0 | ✓ |
| 0x417ea7 | 591 | 1 | 3 |  |
| 0x41608b | 637 | 1 | 0 | ✓ |
| 0x41593b | 673 | 1 | 1 |  |
| 0x41dedb | 770 | 1 | 0 | ✓ |
| 0x416925 | 974 | 1 | 0 | ✓ |
| 0x4198aa | 974 | 1 | 0 | ✓ |
| 0x41e1dd | 1011 | 1 | 1 |  |
| 0x419354 | 1077 | 1 | 0 | ✓ |
| 0x416cf3 | 1275 | 1 | 1 |  |
| 0x41635f | 1478 | 1 | 0 | ✓ |
| 0x418653 | 2010 | 1 | 0 | ✓ |

### strategic-map camera + scroll (48)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4263ad | 255 | 5 | 2 |  |
| 0x42e26f | 41 | 2 | 0 | ✓ |
| 0x42d91b | 83 | 2 | 1 |  |
| 0x42b780 | 119 | 2 | 5 |  |
| 0x427c9b | 126 | 2 | 0 | ✓ |
| 0x42f58c | 130 | 2 | 0 | ✓ |
| 0x42df30 | 178 | 2 | 4 |  |
| 0x42051c | 276 | 2 | 7 |  |
| 0x4233f7 | 313 | 2 | 1 |  |
| 0x420630 | 403 | 2 | 8 |  |
| 0x420147 | 463 | 2 | 0 | ✓ |
| 0x426360 | 77 | 1 | 0 | ✓ |
| 0x4262eb | 117 | 1 | 0 | ✓ |
| 0x42476b | 138 | 1 | 0 | ✓ |
| 0x42f148 | 151 | 1 | 0 | ✓ |
| 0x42d179 | 157 | 1 | 0 | ✓ |
| 0x425a6a | 158 | 1 | 0 | ✓ |
| 0x420d40 | 164 | 1 | 0 | ✓ |
| 0x42b919 | 171 | 1 | 2 |  |
| 0x4247f5 | 174 | 1 | 0 | ✓ |
| 0x42e50d | 179 | 1 | 0 | ✓ |
| 0x42a8d1 | 218 | 1 | 2 |  |
| 0x42a9ab | 218 | 1 | 2 |  |
| 0x421231 | 222 | 1 | 2 |  |
| 0x4248a3 | 239 | 1 | 0 | ✓ |
| 0x420428 | 244 | 1 | 0 | ✓ |
| 0x426120 | 246 | 1 | 0 | ✓ |
| 0x420316 | 274 | 1 | 0 | ✓ |
| 0x42b7f7 | 290 | 1 | 6 |  |
| 0x425314 | 371 | 1 | 0 | ✓ |
| 0x423739 | 383 | 1 | 2 |  |
| 0x425487 | 437 | 1 | 0 | ✓ |
| 0x423241 | 438 | 1 | 2 |  |
| 0x4207c3 | 510 | 1 | 0 | ✓ |
| 0x423530 | 521 | 1 | 0 | ✓ |
| 0x421d09 | 523 | 1 | 0 | ✓ |
| 0x421005 | 556 | 1 | 2 |  |
| 0x422ff1 | 592 | 1 | 2 |  |
| 0x422d22 | 719 | 1 | 2 |  |
| 0x425e4b | 725 | 1 | 4 |  |
| 0x42dbc7 | 868 | 1 | 7 |  |
| 0x421f14 | 1491 | 1 | 1 |  |
| 0x42c64f | 1531 | 1 | 3 |  |
| 0x421707 | 1538 | 1 | 1 |  |
| 0x4224e7 | 1835 | 1 | 0 | ✓ |
| 0x42f60e | 2298 | 1 | 0 | ✓ |
| 0x424992 | 2434 | 1 | 0 | ✓ |
| 0x423ba4 | 3015 | 1 | 0 | ✓ |

### stage/post-load + char tables (40)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4360f2 | 41 | 3 | 0 | ✓ |
| 0x43e8f4 | 151 | 2 | 1 |  |
| 0x43611b | 38 | 1 | 0 | ✓ |
| 0x43fed2 | 54 | 1 | 0 | ✓ |
| 0x43e668 | 58 | 1 | 1 |  |
| 0x43f8ff | 58 | 1 | 0 | ✓ |
| 0x4329a4 | 61 | 1 | 0 | ✓ |
| 0x439541 | 73 | 1 | 1 |  |
| 0x43b622 | 95 | 1 | 0 | ✓ |
| 0x43f7e3 | 114 | 1 | 4 |  |
| 0x4357a6 | 132 | 1 | 1 |  |
| 0x43582a | 134 | 1 | 1 |  |
| 0x43c885 | 139 | 1 | 1 |  |
| 0x4330c6 | 143 | 1 | 0 | ✓ |
| 0x43a123 | 145 | 1 | 2 |  |
| 0x432893 | 154 | 1 | 0 | ✓ |
| 0x436a88 | 154 | 1 | 0 | ✓ |
| 0x43441c | 158 | 1 | 1 |  |
| 0x43933b | 176 | 1 | 0 | ✓ |
| 0x43c57d | 183 | 1 | 1 |  |
| 0x43b412 | 185 | 1 | 0 | ✓ |
| 0x43f24b | 185 | 1 | 0 | ✓ |
| 0x43b4cb | 200 | 1 | 0 | ✓ |
| 0x4369bd | 203 | 1 | 0 | ✓ |
| 0x43600c | 230 | 1 | 0 | ✓ |
| 0x439df0 | 235 | 1 | 2 |  |
| 0x432443 | 247 | 1 | 1 |  |
| 0x433a70 | 251 | 1 | 0 | ✓ |
| 0x43253a | 262 | 1 | 0 | ✓ |
| 0x43c77a | 267 | 1 | 2 |  |
| 0x435ebc | 336 | 1 | 0 | ✓ |
| 0x4393eb | 342 | 1 | 0 | ✓ |
| 0x43c910 | 344 | 1 | 2 |  |
| 0x434174 | 383 | 1 | 5 |  |
| 0x4399b0 | 418 | 1 | 3 |  |
| 0x435cef | 456 | 1 | 1 |  |
| 0x43c2a9 | 541 | 1 | 2 |  |
| 0x43f5c5 | 542 | 1 | 9 |  |
| 0x43958a | 674 | 1 | 0 | ✓ |
| 0x43bf07 | 827 | 1 | 3 |  |

### sim dispatch + records (5)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x440038 | 81 | 1 | 0 | ✓ |
| 0x448d7e | 148 | 1 | 0 | ✓ |
| 0x4407e5 | 426 | 1 | 1 |  |
| 0x4477e7 | 503 | 1 | 0 | ✓ |
| 0x44098f | 595 | 1 | 1 |  |

### fade / loading / audio-mci (3)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x45242b | 11 | 1 | 0 | ✓ |
| 0x45280b | 11 | 1 | 0 | ✓ |
| 0x452775 | 49 | 1 | 0 | ✓ |

### tables (item/char) + populators (16)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x463b42 | 196 | 2 | 0 | ✓ |
| 0x46d421 | 11 | 1 | 0 | ✓ |
| 0x463c06 | 92 | 1 | 0 | ✓ |
| 0x463c62 | 92 | 1 | 0 | ✓ |
| 0x46a101 | 165 | 1 | 0 | ✓ |
| 0x463fa5 | 273 | 1 | 0 | ✓ |
| 0x463e91 | 276 | 1 | 0 | ✓ |
| 0x4640b6 | 339 | 1 | 0 | ✓ |
| 0x464209 | 345 | 1 | 0 | ✓ |
| 0x463cbe | 467 | 1 | 0 | ✓ |
| 0x460501 | 668 | 1 | 0 | ✓ |
| 0x461623 | 1073 | 1 | 0 | ✓ |
| 0x4611ef | 1076 | 1 | 0 | ✓ |
| 0x461efb | 1145 | 1 | 0 | ✓ |
| 0x461a54 | 1191 | 1 | 0 | ✓ |
| 0x460a1c | 2003 | 1 | 0 | ✓ |

### d3d/mesh pool + preload + palette (17)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x478952 | 53 | 1 | 0 | ✓ |
| 0x472986 | 63 | 1 | 0 | ✓ |
| 0x47703a | 126 | 1 | 0 | ✓ |
| 0x4788c6 | 140 | 1 | 1 |  |
| 0x475d73 | 148 | 1 | 1 |  |
| 0x472a31 | 152 | 1 | 0 | ✓ |
| 0x47fe0b | 156 | 1 | 1 |  |
| 0x477b2f | 346 | 1 | 2 |  |
| 0x477c89 | 371 | 1 | 8 |  |
| 0x47f76d | 458 | 1 | 0 | ✓ |
| 0x475b41 | 562 | 1 | 0 | ✓ |
| 0x47ed9b | 590 | 1 | 0 | ✓ |
| 0x476488 | 648 | 1 | 1 |  |
| 0x4784ca | 730 | 1 | 0 | ✓ |
| 0x47f474 | 761 | 1 | 0 | ✓ |
| 0x477dfc | 1225 | 1 | 6 |  |
| 0x475e07 | 1665 | 1 | 2 |  |

### equip / starter-items (6)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x480f8b | 1200 | 2 | 0 | ✓ |
| 0x4859e5 | 354 | 1 | 2 |  |
| 0x489401 | 595 | 1 | 7 |  |
| 0x4822ed | 644 | 1 | 3 |  |
| 0x488dfe | 943 | 1 | 0 | ✓ |
| 0x485bb1 | 1592 | 1 | 5 |  |

### npc schedule + worker walk (14)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x49964c | 124 | 4 | 2 |  |
| 0x49915e | 259 | 3 | 2 |  |
| 0x499366 | 691 | 3 | 2 |  |
| 0x499619 | 51 | 2 | 0 | ✓ |
| 0x4987b7 | 1536 | 2 | 4 |  |
| 0x499d97 | 26 | 1 | 1 |  |
| 0x49767d | 36 | 1 | 5 |  |
| 0x497879 | 52 | 1 | 0 | ✓ |
| 0x4990f9 | 101 | 1 | 0 | ✓ |
| 0x49c995 | 157 | 1 | 2 |  |
| 0x4983b7 | 161 | 1 | 0 | ✓ |
| 0x4975d3 | 170 | 1 | 1 |  |
| 0x499261 | 261 | 1 | 0 | ✓ |
| 0x49b973 | 374 | 1 | 1 |  |

### asset-load IO helper cluster (0x4af*) (8)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4af312 | 496 | 4 | 1 |  |
| 0x4af116 | 508 | 4 | 0 | ✓ |
| 0x4aecf0 | 334 | 2 | 1 |  |
| 0x4aa90a | 374 | 2 | 0 | ✓ |
| 0x4af841 | 152 | 1 | 1 |  |
| 0x4afe3a | 179 | 1 | 0 | ✓ |
| 0x4ae1b8 | 243 | 1 | 3 |  |
| 0x4aa6ca | 469 | 1 | 0 | ✓ |

### battlefield render + wndproc (26)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4b25fc | 58 | 2 | 1 |  |
| 0x4b049b | 103 | 2 | 0 | ✓ |
| 0x4b743b | 147 | 2 | 4 |  |
| 0x4b0cb4 | 314 | 2 | 1 |  |
| 0x4b8c3a | 1202 | 2 | 8 |  |
| 0x4b1dd5 | 11 | 1 | 0 | ✓ |
| 0x4b756b | 26 | 1 | 1 |  |
| 0x4b2234 | 36 | 1 | 3 |  |
| 0x4bbf89 | 73 | 1 | 0 | ✓ |
| 0x4bbfd2 | 73 | 1 | 0 | ✓ |
| 0x4b3571 | 80 | 1 | 1 |  |
| 0x4b3714 | 84 | 1 | 0 | ✓ |
| 0x4b97f9 | 86 | 1 | 0 | ✓ |
| 0x4bbe51 | 86 | 1 | 0 | ✓ |
| 0x4b7514 | 87 | 1 | 0 | ✓ |
| 0x4b3b92 | 89 | 1 | 0 | ✓ |
| 0x4bbea7 | 110 | 1 | 0 | ✓ |
| 0x4b3d5d | 112 | 1 | 0 | ✓ |
| 0x4bbf15 | 116 | 1 | 0 | ✓ |
| 0x4b7180 | 208 | 1 | 2 |  |
| 0x4b1310 | 221 | 1 | 0 | ✓ |
| 0x4b1cf3 | 226 | 1 | 0 | ✓ |
| 0x4b984f | 361 | 1 | 3 |  |
| 0x4b90fb | 394 | 1 | 2 |  |
| 0x4b7250 | 491 | 1 | 8 |  |
| 0x4b14ff | 872 | 1 | 0 | ✓ |

### late/tail helpers (2)

| VA | size | ported callers | unported deps | leaf |
|----|-----:|---------------:|--------------:|:----:|
| 0x4c0580 | 25 | 1 | 0 | ✓ |
| 0x4c2090 | 224 | 1 | 0 | ✓ |

