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

## Open (overlay enrichment, roadmap item 2)

- Income **displayed-vs-collected** ("People pay N" on the tax screen vs the
  actual treasury delta) — the headline income cheat; not yet wired into `data`.
- The **≤1800 free-gold AI grant** and the **256-slot event deck** — still
  host-tested only, need live-retail confirmation (slot 4 hit 1778 at turn 3,
  suggestively near the 1800 ceiling — not yet confirmed as the grant).
- Per-realm **army sizes** (troop-class counts, combat record `+0x16c`, 7×i16)
  for the overlay.
