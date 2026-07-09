# Staging a human battle — the auto-resolve defender-buff routing

> 2026-07-09. RE pass for the LoTR2 "AI cheats" video beat #2 ("never
> auto-resolve an even fight — the defender is secretly buffed"). Goal: from
> ONE save, capture the SAME even human-vs-AI fight resolving two ways —
> **auto-calc → human loses** (rigged) vs **fought interactively → human wins**.
> Pure static RE over `docs/decompiled/all.c`; nothing driven yet. Companion to
> `combat-resolution.md` (the resolver internals) — this doc is the *entry/routing*
> layer and the staging recipe.

## The three combat entry points differ on the buff — this is the crux

The defender difficulty-buff (`combat-resolution.md` §"Quick-resolve difficulty
multiplier": defender strength ×160/200/250/320/400 % by difficulty) fires **only
when `DAT_0053e8fc` (quick_resolve) is 1** inside the resolver `FUN_004aad07`
(all.c:79848 — gated `if (DAT_0053e8fc != 0)`, difficulty from `DAT_0055401c`).
The three functions that reach the resolver set that flag differently:

| entry | addr | sets `DAT_0053e8fc` | who calls it | buff? |
|---|---|---|---|---|
| dispatch (AI turn / army-move collision) | `FUN_004a7158` | **0** (all.c:78474) | army-move resolve (24337), turn driver (52022) | **NO** |
| combat_b twin (siege / defender-select) | `FUN_004a6c68` | **0** (all.c:78378) | siege/besiege collision | **NO** |
| **player attack resolution** | **`FUN_004a8aab`** | **1** (all.c:79058/79063/79073, ALL branches) | `FUN_0043b98a` thin wrapper (26531); turn-resolve phase `DAT_00569584==2` (73698, arg `DAT_0057cad4` = acting army) | **YES** |

**So the buff is a property of the player-initiated auto-calc resolution
(`FUN_004a8aab`), NOT of AI-vs-AI collisions.** This *sharpens* the video claim:
the community folklore is "never AUTO-RESOLVE an even fight" — and auto-resolve is
exactly `FUN_004a8aab`'s quick path. The rigging is on the button you click, which
is the honest, defensible framing. (The generic "the resolver buffs the defender"
in `behaviours-catalog.md` #1 is right about the resolver but must be pinned to
this entry — AI-turn combat via `FUN_004a7158` does NOT buff.)

## `FUN_004a8aab(attacker_terr)` — the player-attack resolver (all.c:79030)

1. `DAT_0056822c = attacker_rec[+0x249]` — the target **county** id (attacker
   record `g_per_territory_420`, stride 0x1a4, byte +0x249).
2. Load combat difficulty `DAT_0055401c` from the **defender county's** region
   record (base `0x53fb70`, stride 0x300 — same county record family as
   `g_per_army_768`; this is county-rec +0x1c0):
   - `[+0x03]==1 && [+0x01]!=0` → `DAT_0055401c = [+0x01] - 1`
   - `[+0x03]==2`              → `DAT_0055401c = [+0x39]` (0x53fba9)
   - else                     → `DAT_0055401c = [+0x00] - 1` (0x53fb70)
   `DAT_0057c910 = (difficulty > 1)`.
3. `DAT_00568210 = attacker`, `DAT_00568218 = defender county's army`
   (`*(int*)(0x53fb6c + defcounty*0x300)`), size the fight `FUN_004a6a30()`, then
   route (identical shape to `FUN_004a7158` but with `DAT_0053e8fc = 1`):

```
size == 0                                   -> AUTO-RESOLVE (silent)   resolver+aftermath        [buff applies]
size != 0 && DAT_0053f284==0 && DAT_004ee89c==0 -> 0x13 QUICK-ANIM (replay)  resolver+aftermath, screen 0x13  [buff applies]
size != 0 && (either flag != 0)             -> 0x12 INTERACTIVE battlefield   (resolver NOT called; real fight)  [no buff]
```

## The two routing flags (what they genuinely are)

- **`DAT_0053f284` = the persistent "manual/quick combat" game OPTION.** Toggled
  by the options handlers `FUN_0043470d` (all.c:22292) and settings case 4
  (33492); restored from saved config `DAT_0053f2b4` (73602). `0` → auto/quick;
  nonzero → always fight interactively. This is a REAL in-game setting — poking it
  is faithful to a player flipping the option.
- **`DAT_004ee89c` = "both combatants are real field armies."** Set inside
  `FUN_004a6a30` (all.c:78325): `=1` iff BOTH the attacker and defender territory
  records have `+0x01 != 0`. When set it FORCES the interactive battlefield
  regardless of the quick-combat option. Also computed there: `DAT_005533d0` =
  the local player's combat role (1=attacker, 2=defender, 0=uninvolved), which
  keys the `s080_0{1,2,3}.wav` cue.

### Consequence for staging (important)
A **field-army vs field-army** even fight has `DAT_004ee89c==1` → it can NEVER
auto-resolve; retail always sends it to the interactive battlefield. To get BOTH
paths from one fixture, the defender must be a **garrison/undefended-style target**
(one side's record +0x01 == 0 → `DAT_004ee89c==0`), so:
- `DAT_0053f284 = 0` → **0x13 quick-anim auto-resolve → human loses** (Path A, and
  it's animated on the battlefield = good footage).
- `DAT_0053f284 = 1` → **0x12 interactive → human plays → wins** (Path B).
Same save, flip one option byte. (Alternatively force each branch directly by
poking the flags before a `callq FUN_0043b98a(attacker)`.)

## Screen-state note
`DAT_004eac50` = 0x13 (quick-anim replay) / 0x12 (interactive) are the combat
*dispatch* codes set here. The battlefield RENDERER (`battlefield-render.md`) keys
on `DAT_0057c8fc==2` + `DAT_004eac50 ∈ {0x28..0x2a}` — the 0x12/0x13 handoff
transitions into that combat game-mode. Confirm the 0x12→0x28.. transition live
(LEAD).

## The even-fight number (from combat-resolution.md model)
Weighted strength `sum(count[c]*{2,16,8,13,9,13,22}[c])` +20 presence; winner test
is `attacker_strength < defender_strength` → **ties go to the attacker**. So set
attacker and defender to EQUAL weighted strength:
- **auto-calc (buff)** at Hard: `sd → sd*320/100` → `sa < sd` → **defender wins** → human loses.
- **interactive** (no buff, real fight): even → attacker wins ties → **human wins**.
Make `DAT_0055401c` (defender county difficulty) resolve to 3 for the ×320% (a
Hard game should, but poke/verify the county byte per the step-2 table above).

## Driving handles (for the daemon)
- Resolve a player attack with the buff armed: `callq FUN_0043b98a(attacker_terr)`
  (thin wrapper → `FUN_004a8aab`), OR let the natural turn-resolve phase
  (`DAT_00569584==2`) fire it on `DAT_0057cad4`.
- Force path: set `DAT_0053f284` (option) and/or the record `+0x01` bytes before
  the call; read `DAT_004eac50` after to confirm 0x13 vs 0x12.
- Read the outcome: `DAT_0057c924` = winning territory, `DAT_005440cc` = winner
  owner (set by the resolver, all.c:79874/79905).

## Open LEADS to confirm live
1. The 0x12 → 0x28.. battlefield-mode transition + how the interactive fight is
   actually won (tactical input — separate RE, task #3).
2. That a garrison target yields `+0x01==0` on the defender (→ `DAT_004ee89c==0`)
   so Path A is reachable — verify on the real save, not assumed.
3. The exact county-difficulty byte that makes `DAT_0055401c==3` on our seed.
