# AI-POV visualization — RESOLVED (2026-07-09)

**Idea (for the LoTR II "AI cheats" video):** render the running game FROM AN AI
PLAYER'S POINT OF VIEW — their capital centred, their county shown as owned,
the HUD sourcing their (cheated) treasury — with a slopstudio overlay for the
hidden economic cheats. "You watch the enemy AI's economy from the inside."

**STATUS: DONE.** The AI POV renders correctly on live retail via a few memory
writes; no map/fog hooks were needed. Reusable driver: **`tools/ai_pov.py`**
(needs a running `tools/probe_daemon.py`).

    tools/ai_pov.py pov 2 --shot /tmp/ai2.png   # POV of realm slot 2 + capture
    tools/ai_pov.py human                        # restore the human POV
    tools/ai_pov.py data                         # per-realm treasury/upkeep + cheat math
    tools/ai_pov.py scan                          # (re)build the minimap region cache

## Why it's just pokes, not "fog hooks" (the lead's worry was unfounded)

Two facts about the strat map, confirmed in the port + live:

1. **No per-player strategic fog.** The fog-gate submode (`DAT_0053f264`, the
   per-tile bit-`0x20` mask) is a **new-game config value** (title tab 1,
   defaulted 0 by `game_init`), not live per-turn fog — the overlay renderer
   even calls it a "dev-mode filter". The whole strat map is visible to
   everyone; fog (if any) is tactical, not strategic. So there is nothing to
   "reveal" for an AI POV.
2. **Ownership is absolute.** Each map cell stores its **true owning realm**
   (cell `+7`), so every realm's counties already render in their own colour
   regardless of who's viewing. An AI's territory is *already on screen*.

So the earlier observation "poking the viewer didn't switch the map" was
expected: the viewer global doesn't drive the camera, and ownership colour is
viewer-independent. An AI POV is only three independent writes:

| what | global | note |
|------|--------|------|
| HUD + info-panel realm | `DAT_0057c8cc` (viewer / "local player", i32) | panel OWNED-branch tests `owner == 0057c8cc` |
| selected county | `s_slot_0057c93c` (curterr, i32) | drives the info card; set to an AI county |
| camera | `DAT_005651b4` col / `DAT_005651b8` row (i32) | **isometric** — don't compute by hand |

**Centring** uses the game's OWN transform: click the target realm's blob on
the minimap (it left-click-recentres, `hooks->minimap_drag`). `ai_pov.py`
finds that blob by scanning the static minimap **region-id grid**
(`DAT_0052aff0`, 128×128 bytes at screen origin (478,28), 1:1), mapping each
region-id → owner via the county records, and clicking the on-territory pixel
nearest the realm's centroid. Region-id in that grid **== county record index
== curterr id** (all the same numbering). Direct camera pokes work too, but the
iso projection makes hand-computed centring unreliable — prefer the minimap.

**Capture gotcha:** the strat map only presents when dirty. Force a full
re-render before grabbing a frame by writing **`DAT_0057d340 = 2`**
(g_demand_redraw) and nudging the cursor; retry (the present seat can miss the
first tick). `ai_pov.force_present_shot` does this.

## Verified live (retail, seeded new game 1268 Winter → +12 turns)

- POV switch works on every realm (slots 1..5 — this is a **5-player** game;
  slot 5's capital is region 1). E.g. slot 4 → capital **Dyfed** centred, HUD
  **1862 Crowns** (its real treasury), info card OWNED with **Tax 7%** (its own
  setting, vs the passive human's 0%). Human stays flat at 1000, AIs swing
  360–1862 — the "funded/played differently" signal is visible from inside.
- Restores cleanly (viewer/curterr/camera back to human; no crash).

## Upkeep cheat — extracted + proven live (for the overlay)

Per-realm economy lives in the strat record `g_player_strategic_352`
(`0x57bf00`, stride `0x160`): treasury `+0x118`, **total wage bill `+0xfc`**,
human-flag `+0x05` (1=human), active `+0x04`, personality `+0x07`, rank `+0x2b`.
`ai_pov.py data` prints them + the cheat math.

The wage divisor (`engine_unit_recompute_wage`, `engine_realm_upkeep.c`):

```
human owner (flag +5 == 1):  wage = value / 4          (ALWAYS, any difficulty)
AI owner    (flag +5 == 0):  wage = value / {3,5,10,10}[difficulty s_0053f23c]
```

So on the **two hardest** settings the AI pays `value/10` = **40% of the human's
`value/4`** for an identical army; on **easy** it pays `value/3` = **133%**
(nerfed). The cheat is **difficulty-gated** — a critical fact-check: "the AI
cheats on upkeep" is only true on hard.

**Proven live** by flipping `s_slot_0053f23c` 0→3 and calling the wage refresh
`callq 0x4ad426` (`engine_realm_refresh_wage_bills`, void): AI wage bills
{43,41,65,36} @ easy(/3) → {13,12,19,10} @ hard(/10), deterministic, restores
on flip-back. (Video note: to show the real cheat, boot the game on hard —
this seed is difficulty 0. A hard-difficulty new-game trace is the next step.)

## Overlay data — extracted + cheats proven live (roadmap item 2, 2026-07-09)

Driver: **`tools/cheat_data.py`** (`grant-table` / `snapshot` / `extract --turns N
[--difficulty D] --out CSV`). Per realm per turn it logs treasury, income
displayed vs collected, upkeep, grant, army strength + troop breakdown. A real
county = a record whose index is in the minimap region grid (1..14); records 15..
are armies / uninitialised **phantoms** whose `+5` byte can read as a bogus owner
(the 0x05/0x07 fill pattern — e.g. record 18 "owner 7", record 19) — skip them.

**The AI free-gold GRANT — the headline cheat, CONFIRMED per-turn.**
`engine_army_ai_set_rations_and_grant` (`FUN_0049d638`, brain-FSM step 3, run
every turn — the roster/brain state resets each turn cycle via
`engine_turn_reset_roster` `FUN_0049b6d3`) does `treasury += g_ai_cash_grant[
(level>=3?DC1E0:DC230) + diff*4 + pers*0x10]`, a `[personality][difficulty]`
table. Retail tables at `0x4dc1e0`/`0x4dc230` **match the port byte-for-byte**.
Values (free crowns/turn): weak AIs (level<3) 0..600; strong AIs (level>=3)
0..**1800** (pers 4, hardest). Humans get 0. **Proven it fires EVERY turn** by a
tracer (poked slot-4's grant entry to 50000 → its treasury jumped +~50000/turn,
partly spent on a ballooning army). The starting cash 1040/1100 = 1000 + the
difficulty-0 setup grant (40/100), so the grant runs at init too.
20-turn Hard extract (level-1 AIs): human flat 1000; each AI pocketed **8.4k–12.6k
free crowns**. **Difficulty-gated** — on Easy the grants are ~0.

**Income displayed ≠ collected.** Projected income `+0xc0` (the tax screen's
"People pay N", `engine_realm_recompute_realm_income` `FUN_0044b80b`) is ALWAYS
computed; collected income `+0xbc` (added to treasury,
`engine_realm_accrue_income` `FUN_0044b59b`) is **zeroed when the realm's event
scratch `+0x1a8` is set** (income-suppression events, e.g. 0x13b). Live Hard
extract shows every AI's displayed over-stating collected by ~150–500/game.

**Upkeep cheat** (see above): strat `+0xfc` wage bill, human value/4 vs AI
value/{3,5,10,10}; on Hard the AI pays value/10 = 41% of the human rate.

**Army sizes:** county record troop counts `+0x16c` (7×i16), weights
`{2,16,8,13,9,13,22}` → per-realm strength = Σ(count·weight) over owned counties.

**Still open:** the 256-slot event deck (host-tested only); to *film* the real
grant/upkeep cheats, a **hard-difficulty new-game trace** (with AI level ≥3 for the
1800 grant) — this seed is difficulty 0 / level-1 AIs, so `--difficulty` pokes it.
