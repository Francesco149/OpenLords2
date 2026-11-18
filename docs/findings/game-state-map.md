# Strategic game-state map (the bot's reader)

Where retail `Lords2.exe` (GOG 2.0.0.3, ImageBase 0x400000) keeps the strategic game state, as read
by `tools/bot/l2state.py` (`snapshot(read) -> dict`). Verified 2026-10-23 in rhost (wine target) on
`newgame-playnow` / `endturn-2` plus perturbation runs; every record block below is a save-schema
entry (`save-format.md`), so this is also the `.sav` layout.

Evidence tags: **live** = confirmed in rhost against the UI (screenshot) or by a UI action moving
exactly that byte; **port** = read by a byte-exact (render-verified) port module in `src/`, not
re-checked live; **decomp** = read off the Ghidra decompile of the named function only; **notes** =
older docs / Frida tools only.

## Naming trap

The port's names for the two big blocks are swapped relative to their meaning:
`g_per_army_768` (0x53f9b0, 0x300 stride) is the **county** record; `g_per_territory_420`
(0x52f0b0, 0x1a4 stride) is the **map-piece** record (armies, merchants, rebels, supply carts).
`tools/cheat_data.py` / `openlords2_mcp.py` read troop counts at county +0x16c — that is the
rations-panel food grid, not troops (troops are piece +0x16c).

## Scalars

| field | VA | type | evidence |
|---|---|---|---|
| screen id | 0x4eac50 | u8 | live (0 map, 0x27 scroll, 0x23 turn processing, 0x14/15/16/19 county panels, 0x17 raise army, 0x0a armory, 0x09 court, 0x18 supplies, 0x1b castle, 0x0b/0x1a diplomacy, 0x08 merchant, 0x0c trade, 0x04 terrain/army info, 0x10 army move) |
| engine state | 0x52af94 | i32 | live: 2 boot/title, 3 in game |
| game mode | 0x57c8fc | i32 | live 0 = strategic; 2 = tactical combat per FUN_00440231 (notes) |
| modal event scroll | 0x53e910 | i32 | notes (ui-action-anchors: event scrolls at screen 0 that ignore RMB); the event id = its loc group (ids reach 0x120) — bot v2 |
| popup type | 0x5440a4 | i32 | 0/2/3/13/14/18 notices, 1 AI message, 10 pay-for-help, 11 AI alliance offer, 17 garrison capacity (yes/no) — live 3; decomp FUN_0047685d |
| popup sender / line | 0x553ee0 / 0x5440ac | i32 | sending realm (AI messages); message line = loc grp id, sub arg+1 — decomp FUN_0047309e |
| popup seal origin x / y | 0x5681dc / 0x56950c | i32 | a click in its 48-px box at −12 dismisses — decomp |
| confirm dialog (0x1e) line | 0x57cb14 | i32 | loc grp 10: 7 garrison, 8 besiege, 13 lift siege … — live 7/8 |
| mercenary bands | 0x554030 (count) / 0x568dc0 + b*20 | i32 / 20 B | +0x00 u16 hired-by piece (0 free), +0x03 county offered in, +0x05 troop class, +0x08 men, +0x0c hire cost, +0x10 wage figure (/4 ≈ the panel's seasonal wage) — live (FUN_004ac904, FUN_004aca2b) |
| season | 0x57c934 | i32 | live: 1 Spring .. 4 Winter |
| year | 0x553edc | i32 | live: AD; Winter 1268 → Spring 1268 → Summer 1268 (rolls entering Winter) |
| turn counter | 0x553240 | i32 | live: 1 at Winter 1268, 3 at the end of endturn-2 (FUN_00448440 ++) |
| local player | 0x57c8cc | i32 | live: realm the HUD/panels show (1) |
| selected county | 0x57c93c | i32 | live: minimap clicks set it |
| county count | 0x56d5dc | i32 | live: 4 on Play Now (reads 14 before a game starts) |
| county-name page | 0x53f034 | i32 | live: 17 on Play Now; names = loc grp 100 sub page*20+k |
| players not ready | 0x554004 | i32 | live (FUN_0049b762) |
| difficulty | 0x53f23c | i32 | notes (cheat_data.py upkeep divisor) |
| camera scroll col/row | 0x5651b4 / 0x5651b8 | i32 | notes (ai_pov.py); iso scroll space, not cell coords |
| piece under last map query | 0x52afbc | i32 | decomp (FUN_0046d5fe) |
| trade: merchant piece | 0x553c64 | i32 | live: = merchant piece id after clicking its cart |
| L2.eng blob | 0x591580 | 120000 B | live: "Textfile" header; group g offset = 24-bit LE at +8+g*4 |
| base prices | 0x5532a0 + g*0x10 | i32 | live: sell price; buy = base + max(1, base*markup/100) (FUN_00428daf; grain 2/4, cattle 12/24, swords 23/46). goods g = loc grp 6: 1 grain 2 cattle 4 ale 6 iron 7 stone 8 timber 9 pikes 10 bows 11 maces 12 crossbows 13 swords 14 mail |

## Realm record — 0x57bf00 + p*0x160 (p = 1..5)

| off | type | field | evidence |
|---|---|---|---|
| +0x00 | i32 | turn-ready: <999 playing, 999 human ended turn, 1000 AI done, 999 unused slot | live (End turn: 1 → 999 → [screen 0x23, 0] → 1) |
| +0x04 | u8 | in game (non-zero) | live (3/4 for the two Play Now realms, 0 others) |
| +0x05 | u8 | human-controlled | live |
| +0x07 | u8 | AI personality 1..4 (= loc grp 7 title: 1 Knight 2 Baron 3 Countess 4 Bishop) | live-consistent (Knight = 1) + per-realm-ai.md |
| +0x0a | u8 | banner colour 1..5 (1 red, 2 yellow) | port (armory/rankings) + minimap |
| +0x0c | u8 | avg happiness (recomputed in turn processing) | live (72 → 79 at turn end) |
| +0x10 / +0x18 | i32 | total population / previous (turn-processing snapshot) | live |
| +0x29 | u8 | counties owned (stale within a turn) | port (rankings cat 0); IDA 0x49d1e0 recount, 0x4a72fe +1 per conquest |
| +0x2b | u8 | rank | port (rankings) |
| +0x2c | u8 | armies owned (stale) | port (FUN_0049d1e0) |
| +0x4c | u8 | castles (stale) | port (rankings cat 1) |
| +0x50 | i32 | composite score | port (FUN_0049aa0e) |
| +0x54 | i32 | men in armies (stale) | port (rankings cat 2) |
| +0x81 | u8 | alliance partner | port (engine_diplomacy form_alliance) |
| +0x84 + q*0x10 | i8 | attitude toward realm q, ±30 | live (100-crown gift: Knight→human 5 → 15 after End turn) |
| +0x85 + q*0x10 | u8 | allied with q | port (form_alliance) |
| +0x88 + q*0x10 | u8 | war declared on q | port (engine_diplomacy) |
| +0x90 + q*0x10 | u8 | q sent a message this turn | live (gift set Knight +0xa0) |
| +0xfc | i32 | army wages per season | live (15 after a 60-man draft; Court "Total army wages") |
| +0x118 / +0x11c | i32 | crowns / previous turn | live (5000; gift 100 → 4900) |
| +0x120 / +0x128 / +0x130 | i32 | iron / stone / wood (each + 4 = previous turn) | live (Court screen; wood +86/turn) |
| +0x138 | i32 | weapons total (+0x13c previous) | live ("50 Total weapons") |
| +0x140 + w*4 | i32 | weapons w: 0 crossbows 1 maces 2 swords 3 pikes 4 bows 5 armour | live (Court icons, trade "you have 25 swords") |
| +0x15c | i32 | tax revenue expected | live (tax 0 → 3 %: 0 → 62) |
| name | — | C-string at 0x553d50 + p*0x2c + 4 (16 B) | live ("The Knight") |

## County record — 0x53f9b0 + k*0x300 (k = 1..n)

| off | type | field | evidence |
|---|---|---|---|
| +0x05 | u8 | owner realm (0 neutral) | live |
| +0x08 | s8 | HUD farm↔industry split = % of workers in industry | live (◄► ±4; wood workers 108/435 = 25 %) |
| +0x09 | u8 | health 0 diseased .. 4 perfect (loc grp 0x14) | live ("Good" = 3) |
| +0x0c / +0x0d / +0x18 | s8 | happiness now / last season / average | live (72 / 65 / 72) |
| +0x0f | s8 | tax→happiness term (+ realm +0x28) | live (5 → 2 at 3 %) |
| +0x11 | s8 | ration→happiness term (rations panel) | live (Normal +1, Double +4) |
| +0x12..+0x15, +0x17 | s8 | happiness from taxes / ration / health / army / events | live (happiness panel; army −7 after a draft) |
| +0x194 | i32 | happiness from ale | port (happiness panel) |
| +0x24 / +0x28 | i32 | population now / last season | live (435 / 417; 578 at endturn-2 end) |
| +0x30 / +0x34 | i32 | births / deaths | live (+63 / −45) |
| +0x38 | i32 | army change (−men drafted) | live (−60) |
| +0x3c / +0x40 / +0x58 | i32/i32/u8 | emigrants / immigrants / emigration target | port (population panel) |
| +0x6c / +0x6d | u8 | county-town cell col/row (bottom-right of the 2x2 flag-0x40 cells) | live (cell grid) |
| +0x74 / +0x75 | u8 | castle cell col/row (2x2 flag-0x80 anchor) | live (cell grid) |
| +0xb9 | u8 | tax % | live (tax panel ↑) |
| +0xc0 | i32 | tax income this season | live (62 crowns at 3 %) |
| +0xc4 + t*0xc | 3×i32 | workers {assigned, needed (−1 n/a), max useful} for task t: 0 grain 1 cattle 2 reclaim 3 castle 4 iron 5 stone 6 wood 7 smithy; t=8 (+0x124) idle | live (327 cattle + 108 wood = 435; ◄► moves 18); FUN_00412b33 |
| +0x15d / +0x15e | u8 | ration achieved / wanted, 0 none .. 5 triple (loc grp 0x15) | live (ration ↑ → Double) |
| +0x15f | s8 | grain↔meat slider, 0 all grain .. 100 all meat | live (slider click → 50) |
| +0x16c / +0x170 / +0x174 | i32 | fed by dairy / grain / meat | live (rations panel 505 / 0 / 370) |
| +0x178 / +0x17c | i32 | grain eaten / cattle eaten | live (rations panel 0 / 37) |
| +0x224 / +0x22c | i32 | grain (sacks) / projected grain change | live (trade "you have 0"), HUD |
| +0x250 / +0x258 | i32 | cattle (animals) / projected herd change | live (HUD 101 +10) |
| +0x1ff +0x200 +0x201 +0x203 +0x204 +0x205 | u8 | field census: fallow, cattle+reclaim(type 0xf..0x16), wheat, barren/flooded/parched, reclaiming(0x19+), total | live + FUN_00469b8d |
| +0x1c0 | u8 | castle level 0 none 1 palisade 2 motte & bailey 3 Norman keep 4 stone 5 royal; target level while building | live ("already of the type"; build motte → 2) |
| +0x1c1 | u8 | level before the current construction | live (→ 1) |
| +0x1c3 | u8 | building: 1 building (new or upgrade; +0x1c0 = the level being built) / 2 repairing siege damage (only writer sub_4784CA after a siege battle; completion posts event 163) | live (rd probe castle-labor, 2026-11-13) / quirk #210 |
| +0x1cc / +0x1d8 | i32 | construction labour left / total | live (400 / 400 for a motte) |
| +0x1d0 / +0x1d4 | i32 | stone / wood still needed | live (wood 350 → 264 after a turn's 86) |
| +0x294 + L*0x18 | lane | industry lane L: 0 wood 1 iron 2 smithy 3 stone. +0 s8 efficiency %, +1 exists, +3 operating, +0x14 i32 output next season | live (+3/+0x14 = HUD wood +86/+115); +0/+1 port |
| +0x280 / +0x284 / +0x288 / +0x28c | i32 | wood for smiths / iron for smiths / wood for builders / stone for builders | port (labor panel), +0x288 live (350) |
| +0x290 | u8 | smithy weapon (w order above) | port (labor panel) |
| +0x1ad | u8 | mercenary band offered here | live (FUN_004acb54) — bot v2 |
| +0x1bc | i32 | castle garrison piece (0 = none) | live (garrison 5 / leave 0) — bot v2 |

Field list: county k's 20 i32 cell byte-offsets at **0x53ea00 + k*0x50** (0 = none); the field state is
the cell type byte (cell +0) — table DAT_004d2ec8 / loc grp 0x1e: 0 barren, 1 fallow, 2 wheat unused,
3/7/11 sown, 4/8/12 ripening, 5/9/13 harvest next, 6/10/14 ready, 15..18 + 25..28 being reclaimed,
19..22 cattle (empty / ample / some overcrowding / overcrowded), 23 flooded, 24 parched (live: matches
the census bytes for all four counties and the map).

## Map piece record — 0x52f0b0 + i*0x1a4 (i = 1..150)

| off | type | field | evidence |
|---|---|---|---|
| +0x00 | i8 | owner (0 = dead record; 6 = merchants) | live |
| +0x01 | u8 | owner is human | live (1 for a drafted human army) |
| +0x08 | u8 | kind 1 army, 2 rebels, 3 merchant, 4 supply cart | live 1/3; decomp FUN_0041b19d 2/4 |
| +0x0a / +0x0b | u8 | cell col/row (county = cell +7) | live (march 30,48 → 25,48) |
| +0x11 | u8 | home county | live ("An army from Raw Sienna") |
| +0x14 / +0x15 | u8 | march goal cell | live |
| +0x14c | u8 | movement sub-state (2 = executing) | live |
| +0x14f | u8 | name index (armies: loc grp 0x5d+owner; merchants: grp 5) | live ("The Lions.") |
| +0x153 | s8 | moves used; moves left = 15 − this | live (15 → 2) |
| +0x155 | u8 | army health 0 healthy .. 4 dying | decomp FUN_0041b19d (advanced mode) |
| +0x15c | i32 | wages per season | live (15) |
| +0x164 | i16 | year formed | live (1268) |
| +0x166 | u8 | merchant buy markup % (100 at spawn) | live (prices) |
| +0x167 | u8 | merchant: county served; supply cart: destination | live (merchant) / decomp |
| +0x168 | i32 | total men | live (60) |
| +0x16c + c*2 | i16 | troops c: 0 peasants 1 crossbowmen 2 macemen 3 swordsmen 4 pikemen 5 archers 6 knights (cart: +0x16c grain, +0x170 cattle) | live (info panel); class = weapon w + 1 |
| +0x195 / +0x196 / +0x197 | u8 | mercenary class / count / nation (loc grp 0x10) | decomp FUN_004224e7 |
| +0x198 | u8 | in a castle (2 = own garrison) | live (garrison / leave) — bot v2 |
| +0x199 | u8 | siege link = county besieged (0 = not besieging) | port (engine_army_state); live — bot v2 |
| +0x19c | u8 | seasons until the assault (engines being built) | live (5 → 4 …) — bot v2 |
| +0x1a | u8 | order code (5 = garrison) | port |

Combat strength weights per class (DAT_004d4b98): 2 16 8 13 9 13 22 (`combat-resolution.md`).

## Map cells — 0x522f90 + (row*64 + col)*8

| off | field | evidence |
|---|---|---|
| +0 | type (field state / castle kind 20 none, 21 palisade.. / industry 0..12) | live |
| +1 | flags: 1 road, 4 sea, 8 mountain/wood, 0x10 village, 0x20 field, 0x40 county town, 0x80 castle or industry | live + FUN_0041c208 |
| +5 | piece index on the cell | live (merchants 1..3) |
| +7 | county key (0x20 sea, 0 off-map) | live; neighbours = 4-adjacent cells with different keys |

Minimap: 128x128 county-key grid at **0x52aff0**; pixel (x, y) is screen (480+x, 25+y) and an LMB
there selects that county (FUN_0043253a) — live for all four Play Now counties.

## Battlefield (screens 0x28–0x2b, game mode 2) — bot v3

| field | VA | type | evidence |
|---|---|---|---|
| unit records 1..80 | 0x5544ac + i*0x1b0 | 432 B | live: +0 owner realm (6 = a county's militia), −0x0c / −0x0a i16 hex col / row (what the mover writes), −0x1a class (0 peasants … 5 archers), +5 action (5 idle at deploy, 3 marching, 8 charging after "Mop up"), +0x14c i16 formation |
| camera col / row | 0x55ce70 / 0x5678a0 | i32 | live: (32,33) at entry; the minimap centres it on (hex − 7) |
| grid origin x / y, hex size | 0x4e6558 / 0x4e6554, 0x4e656c | i32 | live: (0, 24), 32 px; 15 × 14 hexes visible (0x4e5d5c / 0x4e5d4c) |
| paused | 0x53f238 | i32 | live: −1 on entering the field, 0 running (the hourglass FUN_0043b9a1 toggles it) |
| men left, attacker / defender | 0x53f028 / 0x553c58 | i32 | live (83 / 179 → 0 / 146); attacker = piece 0x568210, defender = piece 0x568218 |
| units selected | 0x553078 | i32 | live (21 after a box drag) |
| enemy unit under the cursor | 0x5533b8 | i32 | decomp FUN_0047ed9b; an RMB then attacks it |
| end-screen counter | 0x568470 | i32 | live: counts up on 0x2b, the exit at > 5000; an RMB sets 5001 |
| siege battle | 0x53e8fc | i32 | decomp FUN_004a8aab (1 for an assault) |

## Diplomacy and allies — bot v3

| field | VA | type | evidence |
|---|---|---|---|
| realm +0x14 | 0x57bf14 + p*0x160 | i32 | avg troop strength (rankings); an ally helps only from 750 / 800 / 900 / 1000 by personality (table 0x4d8a88, FUN_004a3551) — live: the Countess 735 |
| realm +0x81 | | u8 | alliance partner — live (1 ↔ 5 after the Countess's offer) |
| realm +0x84 + q*0x10 | | i8 | attitude toward q; an ally answers a plea with the pay-for-help popup (type 10, msg 185) only from 10 up, and when 4·attitude ≤ the season's roll (0x58fd60, 0..127) — else 184 (help) / 183 (refused) |
| realm +0x91 + q*0x10 | | u8 | gifts counter; the help price = it × 500/1000/1600/1500 by personality (0x4d8a64) — decomp |

## Food (why bot games starve)

`fed by dairy` = 5 × cattle (live: 505/101, 380/76, 325/65); a head eats 1 at normal rations (fed = population), ½
at half, 2 at double. Retail covers any shortfall with meat — slaughtered cattle, at the default meat slider of 100 % —
so a county whose population outgrows its herd's dairy eats the herd within a few seasons and starves (v2 bot2cen-s72:
101 → 0 cattle in eight seasons at double rations; v3 bot3cen-s201 at normal: 65 → 4 once the population passed 650).

## Realm events

`engine_realm_roll_events` (0x448819): per county i after 1268, owner human, index v = (2 × the second RNG word's low 7
bits (0x58fd60) + i) mod 256 into the event table 0x4d6108 (256 × u16; 26 non-zero entries, all at v ≡ 7 mod 8). So
**only odd-numbered counties ever get an event** (~20 % a season each). Play Now's human county is 2 (Raw Sienna): it
never rolls; the custom England start is county 13.

## Not found / gaps

- Army morale (no field shown by the UI; piece +0x166 holds the origin county's happiness at
  draft — unverified meaning), the advanced-mode foraging state.
- Rebels and supply carts were not produced live (kind 2/4 layout is decompile-only).
- Siege state beyond the link byte; mercenary offers (g_unit_table_20 at 0x568dc0).
- Cell → screen projection for clicking armies/castles (camera at 0x5651b4 is in iso scroll space).
