# Interactive battlefield — INPUT model (how to drive a tactical fight)

> 2026-07-09. Static RE (subagent sweep of `docs/decompiled/all.c`) of how the
> tactical battlefield accepts player input, so a Frida driver can command troops
> and win the manual fight (LoTR2 "AI cheats" video, beat #2 Path B). Companion to
> `battlefield-render.md` (the draw side) and `combat-battle-staging.md` (the
> auto-vs-interactive routing that gets us onto the battlefield). All addresses are
> `all.c` lines/VAs. Items not pinnable from code (exe not on this disk) are LEADs
> to confirm live before relying on them.

## Dispatch chain
Battlefield input is NOT a separate combat dispatcher — it runs through the SAME
per-tick input dispatcher `FUN_0042ff10` as strategic, keyed on the screen byte
`DAT_004eac50`. The three combat screens are switch cases **0x28 / 0x29 / 0x2a**.

```
FUN_004b99c0 (main tick, ~88532)
  ├─ FUN_004b191e()      ~88561  input SAMPLER (GetCursorPos + button edges)
  ├─ if DAT_0057c8fc==2 && 0x27<DAT_004eac50<0x2b  → FUN_004bc142()  combat RENDER
  ├─ else if DAT_0057c8fc==2 → FUN_0047ed9b()  ~88598  per-frame CURSOR→HEX resolver ★
  └─ FUN_0042ff10()      ~88719  INPUT DISPATCH
       switch(DAT_004eac50):
         0x28 '('  ~20480  deploy/pre-fight: edge-scroll only; RMB → 0x29
         0x29 ')'  ~20489  FIGHT phase — the real click chain (below)
         0x2a '*'  ~20496  drag-select in progress (rubber band) → back to 0x29
```

**Fight phase 0x29 handler chain** (each returns nonzero ⇒ input consumed):
- `FUN_0040deca(&DAT_004dc428,3)` — 3-entry keyboard/region table (shared w/ strategic)
- `FUN_00432221()` — edge-scroll (camera pan)
- `FUN_004329a4()` — **control-panel button hit-test** → `FUN_0040e3ee(0x1e0,0x1c0,&DAT_004dc710,5)`: table **`DAT_004dc710`**, 5 entries × 0x18 B, origin (0x1e0,0x1c0). Sets `DAT_0059154c` = action code, calls entry code-ptr (+0x8).
- `FUN_0043bf07()` — **map LMB**: click-select / start rubber-band drag-select (threshold 0x19 px via `FUN_00479cf7`).
- `FUN_0043c57d()` — **map RMB**: issue MOVE/ATTACK order → `FUN_0043c634(DAT_00553f88, DAT_00553f94)` (~27031): sets target `DAT_00553f14/18`, then local → `FUN_00479e90(...)` (the path/move/attack executor, @60886); networked → `FUN_0043eda0(0x43,0)`.
- `FUN_0043c2a9()` — unit-roster-panel click (select unit portrait via rect table `DAT_004d31f4`) + right-HUD deselect.

## Cursor → hex (the order-target source): `FUN_0047ed9b` (@62821)
Runs every combat frame BEFORE dispatch; the click handlers only READ what it writes:
- `FUN_004bc222(x,y)` → `DAT_005651d0` cursor-over-grid.
- `FUN_004bc2a5(x)+camera DAT_0055ce70` → `DAT_00553f88` = **target hex col**.
- `FUN_004bc2c5(y)+camera DAT_005678a0` → `DAT_00553f94` = **target hex row** (≤0x4f).
- scans 0x50 army records (stride **0x1b0**, base `DAT_005544ac`): `DAT_005533b8` unit
  under cursor, `DAT_00553f90` hovered friendly army, `DAT_0057cac8` enemy under cursor.

## Order model — select-then-order, RMB confirms (mirrors strategic move)
- **Select:** LMB over the grid → `FUN_0043bf07`. Small move (<0x19 px) = click-select
  one unit; larger = rubber-band group-select (screen → 0x2a while dragging).
- **Order:** RMB-released over a target hex → `FUN_0043c634` → `FUN_00479e90`. Attack
  vs move is IMPLICIT — the executor resolves it from whether the target hex holds an
  enemy (`DAT_0057cac8` already flagged by `FUN_0047ed9b`). So: **LMB a unit, RMB the
  enemy = attack.**

## Entry / exit
- **Entry to interactive battle:** `FUN_0042b7f7` (@17829) sets `DAT_0057c8fc=2`,
  `DAT_00568964=1`, camera → hex (0x20,0x21), screen → 0x28. Reached from the
  **pre-battle decision screen 0x1f**: `DAT_0059154c==5` → `DAT_0055302c=3` (assault /
  fight-it-out) → `FUN_0042b919()`+`FUN_0042b7f7()`. `DAT_0059154c==2` on that screen
  → auto/quick-resolve (never enters combat mode).
- **Exit:** battle-outcome screen 0x1c → any click → `FUN_004329ec` (@21225): resets
  `DAT_004eac50=0`, `DAT_0052af94=3` (resume strategic).

### Reconciliation with combat-battle-staging.md (LEAD)
`FUN_004a8aab` (player-attack resolver) sets `DAT_004eac50 = 0x12` for the interactive
branch, but the battlefield dispatch/render key on 0x28..0x2a and entry runs through
`FUN_0042b7f7`/screen 0x1f. So the interactive chain is almost certainly
**0x12 (dispatch handoff) → 0x1f (Fight?/Auto? decision) → assault → 0x28 deploy →
0x29 fight → 0x1c result**. The 0x13 quick-anim path skips 0x1f and just plays the
resolved outcome. **CONFIRM this transition chain live** (watch `DAT_004eac50` +
`DAT_0057c8fc` across the handoff). If a Fight/Auto decision screen (0x1f) appears,
that is ALSO the honest, filmable "the player chooses auto-calc → loses" moment — the
cleanest on-screen framing of beat #2, better than toggling the option in settings.

## Feasibility verdict — driving via Frida
FEASIBLE. Everything the game reads derives from injected cursor (`DAT_004e65fc/f4`,
640×480 client coords) + button edges the sampler computes (`DAT_004eafb4` LMB-press,
`DAT_004e65d8` LMB-release, `DAT_004e6900` RMB-release). The existing PostMessageA/WM_
path at pinned 640×480 already drives these (proven by the strategic End-Turn hit-test).
No keyboard needed for the core loop.

### Owner's winning line for the EVEN fight (2026-07-09) — the Path-B driving plan
Owner (real gameplay): *"win even with losing numbers — find a spot to camp and
bottleneck the enemy while archers snipe them."* So the manual win is NOT "same army
auto-wins" — it takes **skill**: hold a chokepoint (terrain funnel), keep archers back
and snipe the enemy as they bunch at the bottleneck. This SHARPENS beat #2's honesty:
the auto-calc buff matters precisely BECAUSE a skilled human would otherwise outplay an
even AI — the game rigs the shortcut you'd take instead of fighting. Drive Path B as:
(1) select archers, RMB-move them to a rear/high tile behind a terrain pinch; (2) let
the melee/enemy funnel into the choke; (3) RMB-target (snipe) the bunched enemy with the
archers each cooldown. LEAD: confirm ranged vs melee unit identity + attack range on the
battlefield (unit class in the roster panel; class weights {2,16,8,13,9,13,22} index).

**Minimal sequence (attacker stronger ⇒ just win the melee):**
1. Pre-battle 0x1f: LMB the assault/fight button (`DAT_0059154c==5`) → enters 0x28.
   (Or LMB auto-resolve `==2` for the trivial no-battlefield path = Path A capture.)
2. 0x28 → 0x29: RMB once on the field (deploy → fight).
3. LMB your unit's hex (or LMB-drag a box for group-select).
4. RMB the enemy unit's hex → move-to-contact / attack. Repeat per unit until enemy routed.
5. Result 0x1c: LMB once → back to strategic.

## Live entry experiment (2026-07-09) — what callq-shortcuts DO and DON'T give
Drove the fixture (human rec 4 @30,46 vs enemy rec 5 @30,48) on live retail via the
daemon. Findings:
- **Combat records need `+0x01` (owner human-flag) set BEFORE the entry** or the fight
  silently AUTO-RESOLVES. `callq FUN_004a7158(4,5)` with rec4 `+0x01`==0 → `FUN_004a6a30`
  saw "neither side human" → size 0 → auto-resolve (rec4 wiped, rec5 50→5 pyrrhic).
  The `+0x01` byte is copied from the strat human-flag by the NORMAL attack setup
  (all.c:17955), NOT by `FUN_004a7158` — so a direct callq skips it. Poke rec4 `+0x01`=1
  first → `callq FUN_004a7158(4,5)` returns **1** (interactive branch), sets screen 0x12.
- **Screen 0x12 does NOT auto-advance to the battlefield** (mode stays 0). It's a
  transitional dispatch code; the game's own 0x12 handler must run the entry.
- **`callq FUN_0042b7f7()` engages combat mode** (`DAT_0057c8fc`→2, zoom 32, ret 168) and
  the **battlefield RENDERS** — but landing screen was **0x22 "Battle Master ratings"**
  (a results/intro parchment over the field), and a click there EXITED to screen 0x2f
  mode 0. So the stitched callq path reaches combat mode + a rendered field but **NOT a
  driveable live tactical sim** — the real-time unit deployment/sim init (the 0x28 deploy
  phase) is done by other functions in the natural entry chain that the shortcut skips.
- **CONCLUSION:** to DRIVE an interactive battle (Path B footage + the kiting win), use the
  **ORGANIC flow** — order the human army to attack (the attack-order click seq), end turn,
  and take the game's "Fight" decision — which runs the full deploy init → a live 0x28/0x29
  battlefield. The callq shortcut is good for REACHING combat mode / results but not for a
  playable fight. (Next: RE the deploy-phase init between FUN_0042b7f7 and screen 0x28, or
  just drive the organic attack→endturn→Fight sequence.)

## The battle-SETUP chain (why a clean entry is fiddly) — FUN_0042b7f7 @17827
`FUN_0042b7f7` (the combat-mode entry) does NOT battle the arbitrary DAT_00568210/218 you
pass upstream — it **overwrites them to scratch records 1 and 2** (`DAT_00568210=1`,
`DAT_00568218=2`; disbands recs 1/2 via `FUN_0046ead0(0x52f254/0x52f3f8)`), then runs the
deploy/setup chain that populates the battlefield unit table (`DAT_005544ac`) from those
scratch slots: `FUN_0042aa90`, `FUN_0042ac0c`, `FUN_00498dcb`, `FUN_0042b9c4`,
`FUN_0042bf46` (+ camera to hex 0x20,0x21, mode 2). So the real combatants must be COPIED
into scratch records 1/2 BEFORE `FUN_0042b7f7` — that copy is done by the normal entry
(the "Fight" decision path), NOT by a bare callq. This is why `callq FUN_004a7158(4,5)` +
`callq FUN_0042b7f7()` battled empty slots and showed a garbage 0x22 ratings screen.
**To get a DRIVEABLE battle cleanly, either:** (a) drive the ORGANIC attack→endturn→Fight
flow (the game copies the combatants + runs setup), or (b) pre-load recs 1/2 with the two
armies' troop arrays (+0x16c) + owners + human-flags, THEN callq FUN_0042b7f7 — and hold
the screen at 0x28/0x29 without clicking the 0x22 intro (a click there auto-exits). Trace
which function copies the strategic combatants → scratch recs 1/2 in the Fight path (LEAD).

### GOTCHA: the attack traces AUTO-RESOLVE on load
`attack_field_click.jsonl` ("Destroy field?" → Yes → **silent execute**) fires the attack
IMMEDIATELY during the trace and it AUTO-RESOLVES (human rec4 wiped, enemy rec5 50→5) —
before you can set quick-combat=Fight or Hard. So it's NOT a controllable capture vehicle.
Use `load_map_clean.jsonl` (clean map, nothing resolved), set `DAT_0053f284=1` (Fight) +
sync the +0x01 human-flags first, then drive the attack.

## LEADs — confirm live before relying (exe not on this disk → tables undumped)
1. **`DAT_004dc710` control-panel table** (5×0x18 B @ VA 0x4dc710): per-button rects +
   which code-ptr = auto-resolve / retreat / formation / speed. Dump live.
2. **Battle-over signal to POLL** (orders execute async while 0x29 stays up): find the
   win/rout global feeding the 0x1c transition (likely near `DAT_0053f258` /
   battle-result gate). The driver must poll it, not click-and-assume.
3. **0x28 deploy interaction**: no per-hex placement handler found in the dispatch →
   assume auto-deploy (units pre-placed). Confirm a single RMB is all that's needed.
4. **Drag-select** (`FUN_00479cf7`, 0x19 px threshold): group-select needs press-move-
   release (3 WM_ events). Single-unit clicks avoid it — confirm a lone click selects one.
5. **`DAT_00553030` local-vs-networked**: everything assumes local SP (==0, direct
   `FUN_00479e90`). Verify it's 0, else orders route through the network queue
   (`FUN_0043eda0`) with different timing.

**Driver watch/poke set:** cursor `DAT_004e65fc/f4`; edges `DAT_004eafb4`/`DAT_004e65d8`/
`DAT_004e6900`; screen `DAT_004eac50` (want 0x29); mode `DAT_0057c8fc` (2=combat);
hovered hex `DAT_00553f88/94`; hovered unit `DAT_005533b8`; camera `DAT_0055ce70`/
`DAT_005678a0`. Live-trace breakpoints: `FUN_0047ed9b`(62821), `FUN_0043c634`(27031),
`FUN_00479e90`(60886), `FUN_004329a4`(21202), `FUN_004329ec`(21225).
