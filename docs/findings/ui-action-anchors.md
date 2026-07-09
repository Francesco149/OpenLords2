# UI action anchors — the recipe to drive a full retail game (2026-07-09)

Grounded LIVE against retail `Lords2.exe` with the interactive probe
(`tools/probe_daemon.py` + `tools/probe.py`; see [[lotr2-retail-driving-poc]]).
Coordinates are GAME space (640×480); the agent rescales to the pinned client.
Every in-game LMB click needs the driver to **force `click_eligible`
(DAT_004e65f0)=1** first (baked into `probe_daemon._click`); `force_active` +
`suppress_edge_scroll` on; real-time (not turbo).

## Screen-id (`var_sim_current_screen` = DAT_004eac50)
0 = free-roam map · 0x27 = report/tutorial scroll (RMB to dismiss) · event
scrolls (Flooding etc.) sit at screen 0 with **`var_extended_pause`
(DAT_0053e910) ≠ 0** and DON'T dismiss on RMB — clear them with the agent's
reactive `probe_auto_dismiss(1)` (calls FUN_00476768). Full screen table:
`gameplay-and-screens.md`.

## Turn / state signals
- **season `s_slot_0057c934`** — 1 Spr / 2 Sum / 3 Aut / 4 Win; ticks EVERY turn.
- **year `0x00553edc`** — ticks only going into Winter (every 4 turns).
- **selected county `s_slot_0057c93c`** — current territory key (for county-switch).
- per-realm STRAT record `0x0057bf00 + slot*0x160`: +0x118 cash, +0x4 active,
  +0x5 human-flag, +0x2b rank, +0x7 personality. Players = slots 1..4 (1=human).
- county record `g_per_territory_420 + idx*0x1a4`.

## Actions (all verified to open / fire live)
| action | how | screen | controls |
|---|---|---|---|
| **end turn** | click **(558,469)** | — | fires the roll; wait for season change |
| **tax** | click "Tax" label **(515,218)** | 0x15 | ↑ **(197,178)** / ↓ **(243,178)**; reads "People pay N Crowns" = DISPLAYED income (the "economy lies" number) |
| **rations** | click "Ration" **(600,218)** | 0x19 | level ↑ **(340,148)** / ↓ **(378,148)**; grain↔meat mix ◄► slider (~195/315,228) |
| **conscript** (raise army) | bottom icon **(492,447)** | 0x17 | pop→soldiers slider (◄►), weapon inventory, Create/Change/Cancel (bottom-right) |
| **arm** (assign weapons) | bottom icon **(525,447)** | 0x0d | +1/−1/none/all per weapon over the muster |
| **send supplies** | bottom icon **(560,447)** | 0x18 | From/To + grain/cattle arrows + Dispatch |
| **castle** (build/upgrade) | bottom icon **(592,447)** | 0x1b | castle-build subsystem |
| **diplomacy** | bottom icon **(622,447)** | 0x0b→0x1a | noble-faces grid → Gift (±10 arrows, Yes/No) |
| **labor split** | HUD ◄► row (~490/620, **258**) | in-place | farm↔industry worker split (no screen change) |
| **market** | (merchant present only) | 0x0c / 0x08 | buy/sell arrows + confirm |
| **court/treasury** | (resource overview) | 0x09 | Gold/Arms/Iron/Stone/Wood (read) |

Back out of any screen with RMB **(300,240)** (→ map). County switching: click
a county on the map / minimap → `s_slot_0057c93c` updates + the HUD retargets
(the human starts with ONE county, so this matters only after expansion).

## Still to nail for a *complete* turn
- Labor detail grid (0x0f/0x02) opener (the in-place ◄► split covers the common case).
- Map-tile clicks for army move / besiege (need the minimap-recenter on the home
  county — boot already auto-centers there).
- Absolute-value setters (currently relative arrow-clicks; add a "zero then N ↑"
  helper, or read the county field to close the loop).
