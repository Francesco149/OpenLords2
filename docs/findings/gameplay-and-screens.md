# Gameplay knowledge + the in-game screen map

> 2026-07-02 (session 22). Two halves: (1) how a real player plays Lords of
> the Realm II — synthesized from period strategy guides + the manual, as the
> ground truth for authoring playthrough traces; (2) the complete screen-ID
> map for `DAT_004eac50` (g_sim_current_screen), derived from the
> FUN_0040f1a0 render dispatch + each renderer's localized-string group in
> `L2.eng` (offset table at byte 8, 24-bit LE entries; dump recipe at the
> bottom).  The playthrough-trace program ("extend the trace turn by turn,
> port every screen it hits") navigates BY this map.

## 1. How the game is played (trace-authoring ground truth)

Sources: the Sierra manual, GameFAQs guides (JonSan16, brian_sulpher), the
Steam "How to Play" community guide, the-spoiler.com walkthrough,
openlotr2.readthedocs.io manual transcription.  Confirmed against the
binary where noted.

### The turn loop

One turn = one season (Winter → Spring → Summer → Fall; the season byte
rolls 4→1 on advance — verified in the port, session 3).  All lords take
their turns simultaneously; changes apply when the turn ends.  A normal
player turn touches, in rough order:

1. **County review** — the right-panel control panel shows the selected
   county's population, happiness, tax, rations.  Clicking each readout
   opens its detail panel (the 2×2 "action grid", screens
   0x14/0x16/0x15/0x19 below).  Happiness is THE core stat: it drives
   population growth and army morale.  Guides recommend keeping it ≥ 30
   and matching the tax rate to it (roughly 1% at 30-45 … 4% above 80).
   Ration levels shift happiness (None −8 … Normal +1 … Triple +7); ale
   bought from merchants adds up to +5.
2. **Field allocation** — a county's fields are worked on the map itself:
   each field tile is assigned grain / cattle / fallow / reclamation.
   Grain is planted in winter, grows in spring/summer (rat damage risk),
   harvests in fall.  Cattle graze year-round and double as a food store;
   dairy herds feed continuously.  Under Advanced Farming keep ~1/3 of
   fields fallow to preserve fertility.
3. **Labor allocation** — the workforce is split between farms and industry
   (screen 0x0f, "Idle people / Grain farming / Cattle farming / Field
   reclamation / Castle building / Iron mining / Stone quarrying / Wood
   cutting / Blacksmith").  Idle workers show blue, understaffed tasks red.
4. **Industry & weapons** — wood/stone/iron feed castle building and the
   blacksmith; the blacksmith produces one selected weapon type at a time
   (maces cheapest → crossbows/armor dearest; guides rank bows as the
   most valuable early weapon).
5. **Merchants** (screen 0x08/0x0c) — traveling merchants buy/sell grain,
   cattle, wood, iron, stone, weapons, and ale.  Selling surplus grain
   after a good harvest is the standard early economy; buying ale is the
   standard happiness patch.
6. **Armies** (screens 0x0a/0x17) — conscript a % of county population
   (happiness hit; min 50 men to form an army), arm them from the county's
   weapon stock, or hire mercenaries when offered (gold, no population
   cost; two mercenary-containing armies can't merge).  Armies demand
   seasonal wages; unpaid armies desert.
7. **Movement & conquest** — armies move on the strategic map with
   per-terrain movement costs (roads cheapest).  Standard opening: take
   the nearest NEUTRAL county with a small army (guides: ~2 seasons in).
   Moving onto enemy fields pillages them; onto a castle → siege
   (screen 0x1d: build catapults/towers/rams, or starve them out) or
   assault (battle screens 0x12/0x13).  Counties must stay contiguous —
   a realm split in two immediately loses the weaker part.
8. **Diplomacy** (screens 0x0b/0x1a) — gifts, compliments, insults,
   alliances, ask-ally-to-attack.  AI lords (Baron/Bishop/Countess/Knight)
   respond to strength; alliances break freely.
9. **End Turn** — the End-Turn button (entry 5 of table 0x4dc680, ported
   session 3) sets the realm ready; the AI realms think, movement/combat
   resolve, season rolls over, harvest/tax/population reports arrive as
   event scrolls.

### Standard opening (what our playthrough trace should do)

Turn 1 (Winter): review the home county's four panels; set taxes low
(happiness-matched); set rations Normal; assign every fallow field to
grain (winter is planting season) with some cattle; put spare labor on
wood + iron; blacksmith → bows; end turn.  Turns 2-4: adjust rations as
food dips before harvest, keep happiness ≥ 30, start conscripting once
population recovers, aim a ~100-man army at the nearest neutral county.
First siege/assault around turns 4-8.

## 2. The screen map (DAT_004eac50 / g_sim_current_screen)

Derived from FUN_0040f1a0 (render dispatch, 1117 B) + input cases in
FUN_0042ff10 + each renderer's string group (noted as `grp`).  PORTED =
exists in src/ today (session 22).

| screen | renderer | grp | what it is | status |
|---|---|---|---|---|
| 0x00 | (map composite) | — | strategic map | PORTED |
| 0x02 | FUN_00412143 | — | workforce icon-grid overlay | PORTED (s17) |
| 0x04 | FUN_0041b032 | 0x64 | terrain info ("Here Be Dragons") | PORTED |
| 0x05/0x06 | — | — | army-move sub-modes (dispatcher-only) | partial |
| 0x08 | FUN_00415fb7 | — | **Merchant screen** (merchant.pl8/mercgrid.pl8) | unported |
| 0x09 | FUN_00416925 | 0x46 | **Treasury/Court** (Gold/Arms/Iron/Stone/Wood, "Court of") | unported |
| 0x0a | FUN_00417ea7 | 0x45 | **Armory** — conscript/mercenaries | unported |
| 0x0b | FUN_00416cf3 | — | **Noble faces** (diplomacy partner select, faces.pl8) | unported |
| 0x0c | FUN_00416308 | — | **Trade grid** (icontrad.pl8; transitions to 0x08) | unported |
| 0x0f | FUN_00412b33 | 0x4a | **Labor allocation** (workforce commit via screen0f_commit) | unported |
| 0x10 | (map) | — | army-move/merge handoff | PORTED |
| 0x11 | FUN_004192b1 | 0x11 | **Split army** ("Army Division.") | unported |
| 0x12/0x13 | FUN_00422d22/FUN_00422ff1 | — | battle screens | unported |
| 0x14 | FUN_004110b1 | 0x49 | **Population report** | unported |
| 0x15 | FUN_0041152f | 0x56 | **Tax panel** | unported |
| 0x16 | FUN_004116fb | 0x55 | **Happiness report** | unported |
| 0x17 | FUN_00418653 (over 0x0a) | 0x45 | **Army create/change overlay** | unported |
| 0x18 | FUN_0041ad5d | 0x21 | **Send supplies** | unported |
| 0x19 | FUN_00411b72 | 0x57 | **Rations panel** (own arrow handler FUN_0043a379) | unported |
| 0x1a | FUN_0041789b | 0x48 | **Diplomacy actions** (gift/compliment/insult/alliance) | unported |
| 0x1b | FUN_00419789 | — | **Castle screen** (cas_back/caspics/cas_bits.pl8) | unported |
| 0x1c | FUN_0041e1dd | 0x24 | victory/defeat | unported |
| 0x1d | FUN_00421f14 | 0x53 | **Siege preparations** | unported |
| 0x1e | (confirm dialog) | — | move/claim/attack/garrison/besiege confirms | PORTED |
| 0x1f | FUN_0041e7e1 | — | (end-of-scenario family) | unported |
| 0x20 | FUN_0041593b | 0x23 | **Rankings** ("Most counties, …") | unported |
| 0x22 | — | — | season-rollover scroll (lazy bank reload seam, s3) | unported |
| 0x25 | FUN_0041543f | 0x3b | About box ("Release version 2.03") | unported |
| 0x27 | (report scroll) | — | event/report scrolls | PORTED |
| 0x28/0x29/0x2b | FUN_004233f7/FUN_00423241 | — | battle sub-screens | unported |
| 0x2e/0x2f | FUN_00421707/FUN_00421d09 | 0x25 | Battle Master ratings | unported |
| 0x31 | FUN_004154ea | 0x2d | Help options | unported |
| 0x32 | FUN_0040c928 | — | (options family) | unported |
| 0x35/0x36 | FUN_00414819(0/1) | — | (paired sub-screens) | unported |
| 0x39 | FUN_00414f68 | 0x32 | Advanced options | unported |
| 0x42/0x43 | FUN_0041515c/FUN_004152ea | 0x33/0x34 | Sound / Display options | unported |
| 0x44/0x45 | FUN_00425a6a/FUN_0041e5d0 | — | (unidentified) | unported |

### The county control panel's 2×2 action grid (FUN_00438ceb)

Gate: county owner byte `(&DAT_0053f9b5)[county*0x300] == DAT_0057c8cc`
(current county is the local player's) or spectate `DAT_0053f684 != 0`,
on an LMB press inside x∈(0x1e7,0x276), y∈(0xb5,0xf1):

| cell (x split at 0x224/0x237, y at 0xd4) | flip | screen |
|---|---|---|
| top-left | FUN_0043a8f2 | 0x14 population (+ FUN_004b3768(county voice id) + render) |
| top-right | FUN_0043a922 | 0x16 happiness |
| bottom-left | FUN_0043a939 | 0x15 tax |
| bottom-right | FUN_0043a846 | 0x19 rations (+ WAV cues s021_01/s021_02) |

All four panel screens share one input shape (42ff10.c:445-560): while
turn-idle run hud_btn_b → hud_btn_a → [0x19 only: FUN_0043a379 ration
arrows] → action-grid → scrollbar → panel-toggles → minimap-drag; if none
consume, ANY click (RMB direct, or LMB via generic-click FUN_0040e7e4)
closes back to screen 0 with demand_redraw=2; turn-busy also closes.
Because the action grid runs INSIDE the open panel, clicking another grid
cell switches panels directly.

### Minimap county-select (FUN_0043253a, the "tab strip" hook's mode-0 arm)

Not a tab strip: on RMB-press-free frames, cursor in x∈[0x1e0,0x25f],
y∈[0x19,0x98] looks up `(&DAT_0052ae10)[x + (y-0x19)*0x80]` — a 128-wide
county-id bitmap of the minimap — and if the county record exists, sets
current county `DAT_0057c93c` + selects it (FUN_0043278b).  This is how a
player switches counties without scrolling the map.

## 3. L2.eng dump recipe

```python
import struct
blob = open('vendor/original/L2.eng','rb').read()
sec = lambda n: struct.unpack('<I', blob[8+4*n:8+4*n+4])[0] & 0xFFFFFF
subs = [s for s in blob[sec(N):sec(N+1)].split(b'\0') if s.strip()]
```
Group 0x64 = county names (entry 0 = "Here Be Dragons!"), 0x15 = ration
level names, 0x14 = ?, 0x49/0x55/0x56/0x57 = the four county panels,
0x45 = armory, 0x46 = treasury, 0x48 = diplomacy, 0x4a = labor tasks,
0x53 = siege, 0x23 = rankings, 0x24 = victory/defeat.
